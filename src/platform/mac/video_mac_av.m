/* The formatting is rough and I'm not going to bother. Pull requests that fix it are welcome; complaints without one help neither me nor the end user. */
/* The Mac preview's real playback (the visible preview, or SB_REAL_VIDEO=1): AVFoundation plays the stream (HLS, MP4,
   MOV; H.264, HEVC; AAC, AC-3, E-AC-3) with its sound, and its frames come out as pictures the app draws under its
   own window, where the TV's video plane would be (video_mac_underlay), or in a see-through box (video_mac_hole).
   Polled once a frame from video_pump on the main thread, as the TV's backend is; nothing here runs on the TV. */
#define GL_SILENCE_DEPRECATION 1
#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#include <OpenGL/gl.h>
#include "core/video.h"
#include "ui/gfx.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <spawn.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
extern char **environ;

static AVPlayer *pl; static AVPlayerItem *item; static AVPlayerItemVideoOutput *out;
static VidState st = VID_IDLE; static int shown, paused, hasMedia, ended; static char err[256];
static GLuint tex; static int tw, th; /* the last frame, as a texture */
static float wx, wy, ww = 1920, wh = 1080; static int crop; static int cx, cy, cw, ch; /* where it goes, and the part of the frame */
static float vol = 1, volTo = 1, volStep; static double lastPump; static float rad; static uint32_t edges[4]; static int edgesOk; /* edges: the picture's left, top, right, bottom colours */
void video_mac_radius(float r) { rad = r; }
int av_edges(uint32_t *o) { if (!edgesOk) return 0; for (int i = 0; i < 4; i++) o[i] = edges[i]; return 1; }

static double secs(CMTime t) { return CMTIME_IS_NUMERIC(t) ? CMTimeGetSeconds(t) : 0; }
static void drop(void) {
  if (pl) [pl pause]; if (item && out) [item removeOutput:out];
  pl = nil; item = nil; out = nil; edgesOk = 0; st = VID_IDLE; shown = paused = hasMedia = ended = 0; err[0] = 0; }

/* ---- streams AVFoundation can't open (MKV and the like): if ffmpeg is on this Mac, it copies the video, makes the
   sound stereo AAC and writes HLS into a temp folder that a tiny server on 127.0.0.1 hands to AVPlayer. A seek past
   what is written starts ffmpeg again from there (times then count from that offset). Preview only. ---- */
static int rxOn, rxGen, rxPort = -1, rxStarted; static pid_t rxPid; static double rxOff, rxDur, rxT0; static char rxSrc[4096], rxRoot[512], rxVc[32];
static pthread_mutex_t rxMu = PTHREAD_MUTEX_INITIALIZER;
static const char *ffbin(const char *name) { static char p[2][300]; int k = name[2] == 'p'; const char *e = getenv("SB_FFMPEG_DIR");
  const char *dirs[] = { e ? e : "", "/opt/homebrew/bin", "/usr/local/bin", "/usr/bin" };
  for (int i = 0; i < 4; i++) { if (!dirs[i][0]) continue; snprintf(p[k], sizeof p[k], "%s/%s", dirs[i], name); if (!access(p[k], X_OK)) return p[k]; } return NULL; }
static int av_native(const char *u) { /* what AVPlayer opens itself: MP4/MOV/HLS by the address */
  char path[1024]; size_t n = strcspn(u, "?#"); if (n >= sizeof path) n = sizeof path - 1; for (size_t i = 0; i < n; i++) path[i] = (char)tolower((unsigned char)u[i]); path[n] = 0;
  const char *ext[] = { ".mp4", ".m4v", ".mov", ".m3u8", ".m3u", ".mp3", ".m4a", ".aac" };
  for (int i = 0; i < 8; i++) { size_t l = strlen(ext[i]); if (n >= l && !strcmp(path + n - l, ext[i])) return 1; }
  return strstr(path, "m3u8") || strstr(path, "/hls/") || strstr(path, "manifest") || strstr(path, "media-imdb.com") || strstr(path, "googlevideo.com") || strncmp(path, "http", 4); }
static void rm_dir(const char *d) { DIR *dd = opendir(d); if (!dd) return; struct dirent *e; char f[1024];
  while ((e = readdir(dd))) { if (e->d_name[0] == '.') continue; snprintf(f, sizeof f, "%s/%s", d, e->d_name); struct stat st2; if (!stat(f, &st2) && S_ISDIR(st2.st_mode)) rm_dir(f); else unlink(f); }
  closedir(dd); rmdir(d); }
static void rx_kill(void) { pthread_mutex_lock(&rxMu); if (rxPid > 0) { kill(-rxPid, SIGKILL); waitpid(rxPid, NULL, WNOHANG); /* (KILL: ffmpeg reconnecting to a stream may sit on a TERM and outlive the app, holding its ports) */ } rxPid = 0; pthread_mutex_unlock(&rxMu); }
static void rx_exit(void) { rx_kill(); if (rxRoot[0]) rm_dir(rxRoot); }
static void gen_dir(int g, char *o, size_t cap) { snprintf(o, cap, "%s/g%d", rxRoot, g); }

/* the server: GET /g<n>/<file> from the temp folder, with byte ranges; one thread per connection */
static void *rx_conn(void *a) {
  int c = (int)(intptr_t)a; char req[4096]; ssize_t n = recv(c, req, sizeof req - 1, 0); if (n <= 0) { close(c); return NULL; } req[n] = 0;
  char path[512] = ""; sscanf(req, "GET %511s", path); char *q = strchr(path, '?'); if (q) *q = 0;
  char f[1100]; FILE *fp = NULL; const char *ct = "application/octet-stream";
  if (path[0] == '/' && !strstr(path, "..")) { snprintf(f, sizeof f, "%s%s", rxRoot, path); fp = fopen(f, "rb"); }
  if (strstr(path, ".m3u8")) ct = "application/vnd.apple.mpegurl"; else if (strstr(path, ".m4s") || strstr(path, ".mp4")) ct = "video/mp4";
  if (!fp) { const char *r = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"; send(c, r, strlen(r), 0); close(c); return NULL; }
  fseek(fp, 0, SEEK_END); long len = ftell(fp); long a0 = 0, a1 = len - 1; int part = 0; char *rg = strcasestr(req, "\nRange: bytes=");
  if (rg) { long x = -1, y = -1; if (sscanf(rg + 14, "%ld-%ld", &x, &y) >= 1 && x >= 0 && x < len) { a0 = x; if (y >= x && y < len) a1 = y; part = 1; } }
  char h[512]; int hl = part ? snprintf(h, sizeof h, "HTTP/1.1 206 Partial Content\r\nContent-Type: %s\r\nContent-Length: %ld\r\nContent-Range: bytes %ld-%ld/%ld\r\nAccept-Ranges: bytes\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n", ct, a1 - a0 + 1, a0, a1, len)
                             : snprintf(h, sizeof h, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\nAccept-Ranges: bytes\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n", ct, len);
  send(c, h, hl, 0); fseek(fp, a0, SEEK_SET); char buf[65536]; long left = a1 - a0 + 1;
  while (left > 0) { size_t k = fread(buf, 1, left < (long)sizeof buf ? (size_t)left : sizeof buf, fp); if (!k || send(c, buf, k, 0) <= 0) break; left -= (long)k; }
  fclose(fp); close(c); return NULL; }
static void *rx_serve(void *a) { int s = (int)(intptr_t)a;
  for (;;) { int c = accept(s, NULL, NULL); if (c < 0) continue; int one = 1; setsockopt(c, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
    pthread_t t; if (pthread_create(&t, NULL, rx_conn, (void *)(intptr_t)c)) close(c); else pthread_detach(t); } return NULL; }
static int rx_server(void) {
  if (rxPort >= 0) return rxPort; rxPort = 0;
  snprintf(rxRoot, sizeof rxRoot, "%s/sbpreview-%d", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (int)getpid()); mkdir(rxRoot, 0700); atexit(rx_exit);
  int s = socket(AF_INET, SOCK_STREAM, 0); struct sockaddr_in ad = { 0 }; ad.sin_family = AF_INET; ad.sin_addr.s_addr = htonl(INADDR_LOOPBACK); socklen_t al = sizeof ad;
  if (s < 0 || bind(s, (struct sockaddr *)&ad, sizeof ad) || listen(s, 16) || getsockname(s, (struct sockaddr *)&ad, &al)) { if (s >= 0) close(s); return 0; }
  pthread_t t; if (pthread_create(&t, NULL, rx_serve, (void *)(intptr_t)s)) { close(s); return 0; } pthread_detach(t);
  rxPort = ntohs(ad.sin_port); return rxPort; }

/* ffprobe (the first time) then ffmpeg, off the main thread; ffmpeg runs under a small shell in its own process group
   that ends it if the app goes away */
typedef struct { int gen, probe; double ss; char src[4096], dir[600]; } RxJob;
static int capture(char *const argv[], char *out, size_t cap) {
  int fd[2]; if (pipe(fd)) return -1; posix_spawn_file_actions_t fa; posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_adddup2(&fa, fd[1], 1); posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0); posix_spawn_file_actions_addclose(&fa, fd[0]);
  pid_t p; int r = posix_spawn(&p, argv[0], &fa, NULL, argv, environ); posix_spawn_file_actions_destroy(&fa); close(fd[1]);
  size_t k = 0; ssize_t m; while (r == 0 && k + 1 < cap && (m = read(fd[0], out + k, cap - 1 - k)) > 0) k += (size_t)m; out[k] = 0; close(fd[0]); if (r == 0) waitpid(p, NULL, 0); return r; }
static void *rx_job(void *a) {
  RxJob *j = a; char vc[32] = ""; double dur = 0;
  if (j->probe) { const char *fp = ffbin("ffprobe"); char o[2048] = "";
    if (fp) { char *av[] = { (char *)fp, "-v", "error", "-select_streams", "v:0", "-show_entries", "stream=codec_name:format=duration", "-of", "default=nw=1", j->src, NULL }; capture(av, o, sizeof o); }
    char *x = strstr(o, "codec_name="); if (x) sscanf(x + 11, "%31[^\n]", vc); x = strstr(o, "duration="); if (x) dur = atof(x + 9); }
  pthread_mutex_lock(&rxMu);
  if (j->gen == rxGen) {
    if (j->probe) { snprintf(rxVc, sizeof rxVc, "%s", vc); rxDur = dur; fprintf(stderr, "[video] mac: ffmpeg makes HLS of it (video %s, %.0f s)\n", vc[0] ? vc : "?", dur); }
    mkdir(j->dir, 0700); char ss[32], seg[700], pl8[700], ppid[16]; snprintf(ss, sizeof ss, "%.3f", j->ss); snprintf(seg, sizeof seg, "%s/s%%05d.m4s", j->dir); snprintf(pl8, sizeof pl8, "%s/index.m3u8", j->dir); snprintf(ppid, sizeof ppid, "%d", (int)getpid());
    int hevc = !strcmp(rxVc, "hevc"); const char *ff = ffbin("ffmpeg");
    char *av[64]; int n = 0;
    av[n++] = "/bin/sh"; av[n++] = "-c"; av[n++] = "P=$1; shift; \"$@\" & C=$!; while kill -0 $P 2>/dev/null && kill -0 $C 2>/dev/null; do sleep 1; done; kill -9 $C 2>/dev/null"; av[n++] = "sh"; av[n++] = ppid;
    av[n++] = (char *)ff; av[n++] = "-nostdin"; av[n++] = "-loglevel"; av[n++] = "error"; av[n++] = "-y";
    if (!strncmp(j->src, "http", 4)) { av[n++] = "-reconnect"; av[n++] = "1"; av[n++] = "-reconnect_streamed"; av[n++] = "1"; av[n++] = "-reconnect_delay_max"; av[n++] = "5"; }
    if (j->ss > 0) { av[n++] = "-ss"; av[n++] = ss; }
    av[n++] = "-i"; av[n++] = j->src; av[n++] = "-map"; av[n++] = "0:v:0"; av[n++] = "-map"; av[n++] = "0:a:0?"; av[n++] = "-c:v"; av[n++] = "copy";
    if (hevc) { av[n++] = "-tag:v"; av[n++] = "hvc1"; }
    av[n++] = "-c:a"; av[n++] = "aac"; av[n++] = "-ac"; av[n++] = "2"; av[n++] = "-b:a"; av[n++] = "192k";
    av[n++] = "-f"; av[n++] = "hls"; av[n++] = "-hls_time"; av[n++] = "4"; av[n++] = "-hls_list_size"; av[n++] = "0"; av[n++] = "-hls_playlist_type"; av[n++] = "event";
    av[n++] = "-hls_segment_type"; av[n++] = "fmp4"; av[n++] = "-hls_flags"; av[n++] = "independent_segments+temp_file"; av[n++] = "-hls_segment_filename"; av[n++] = seg; av[n++] = pl8; av[n] = NULL;
    posix_spawnattr_t at; posix_spawnattr_init(&at); posix_spawnattr_setflags(&at, POSIX_SPAWN_SETPGROUP); posix_spawnattr_setpgroup(&at, 0);
    pid_t p = 0; if (ff && posix_spawn(&p, "/bin/sh", NULL, &at, av, environ) == 0) rxPid = p; posix_spawnattr_destroy(&at);
  }
  pthread_mutex_unlock(&rxMu); free(j); return NULL; }
static void rx_begin(double ss, int probe) {
  rx_kill(); pthread_mutex_lock(&rxMu); rxGen++; rxOff = ss; rxStarted = 0; rxT0 = CACurrentMediaTime(); pthread_mutex_unlock(&rxMu);
  RxJob *j = calloc(1, sizeof *j); j->gen = rxGen; j->probe = probe; j->ss = ss; snprintf(j->src, sizeof j->src, "%s", rxSrc); gen_dir(rxGen, j->dir, sizeof j->dir);
  pthread_t t; if (pthread_create(&t, NULL, rx_job, j)) free(j); else pthread_detach(t); }
static void rx_stop(void) { if (!rxOn) return; rxOn = 0; rx_kill(); rxGen++; if (rxRoot[0]) { rm_dir(rxRoot); mkdir(rxRoot, 0700); } }

static void start_item(NSURL *url) {
  item = [AVPlayerItem playerItemWithURL:url];
  out = [[AVPlayerItemVideoOutput alloc] initWithPixelBufferAttributes:@{ (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA) }];
  [item addOutput:out];
  pl = [AVPlayer playerWithPlayerItem:item]; pl.volume = vol; if (getenv("SB_MAC_MUTE")) pl.muted = YES; [pl play]; /* (SB_MAC_MUTE: test runs stay silent) */
}
/* the converted stream: its playlist once two pieces are written */
static void rx_pump(void) {
  if (item) return; char d[600], f[700]; gen_dir(rxGen, d, sizeof d); snprintf(f, sizeof f, "%s/index.m3u8", d);
  FILE *fp = fopen(f, "rb"); int segs = 0; if (fp) { char ln[512]; while (fgets(ln, sizeof ln, fp)) if (!strncmp(ln, "#EXTINF", 7)) segs++; fclose(fp); }
  int ended = 0; pthread_mutex_lock(&rxMu); if (rxPid > 0) { int stt; if (waitpid(rxPid, &stt, WNOHANG) == rxPid) { rxPid = -1; ended = 1; } } pthread_mutex_unlock(&rxMu);
  if (segs >= 2 || (ended && segs >= 1)) { char u[128]; snprintf(u, sizeof u, "http://127.0.0.1:%d/g%d/index.m3u8", rxPort, rxGen); start_item([NSURL URLWithString:[NSString stringWithUTF8String:u]]); return; }
  if (ended || CACurrentMediaTime() - rxT0 > 60) { st = VID_ERROR; snprintf(err, sizeof err, "ffmpeg could not read it"); fprintf(stderr, "[video] mac: %s\n", err); rx_kill(); }
}

void av_load(const char *u) {
  drop(); rx_stop(); if (!u || !*u) return;
  if (!av_native(u) && ffbin("ffmpeg") && rx_server() > 0) {
    snprintf(rxSrc, sizeof rxSrc, "%s", u); rxOn = 1; rxDur = 0; rxVc[0] = 0; rx_begin(0, 1); st = VID_LOADING;
    fprintf(stderr, "[video] mac: playing %s (through ffmpeg)\n", u); return; }
  NSString *s = [NSString stringWithUTF8String:u]; NSURL *url = s ? [NSURL URLWithString:s] : nil;
  if (!url && s) url = [NSURL URLWithString:[s stringByAddingPercentEncodingWithAllowedCharacters:[NSCharacterSet URLQueryAllowedCharacterSet]]];
  if (!url) { st = VID_ERROR; snprintf(err, sizeof err, "bad address"); return; }
  start_item(url);
  st = VID_LOADING; fprintf(stderr, "[video] mac: playing %s\n", u);
}
void av_unload(void) { drop(); rx_stop(); }
void av_play(void) { if (pl) { [pl play]; paused = 0; } }
void av_pause(void) { if (pl) { [pl pause]; paused = 1; st = VID_PAUSED; } }
void av_seek(double s) { if (s < 0) s = 0;
  if (rxOn) { double have = 0; if (item) for (NSValue *v in item.seekableTimeRanges) { double x = secs(CMTimeRangeGetEnd(v.CMTimeRangeValue)); if (x > have) have = x; }
    if (item && s >= rxOff && s <= rxOff + have) { [pl seekToTime:CMTimeMakeWithSeconds(s - rxOff, 600)]; return; }
    int was = paused; drop(); paused = was; st = VID_LOADING; rx_begin(rxDur > 0 && s > rxDur ? rxDur : s, 0); return; } /* not written yet: ffmpeg from there */
  if (pl) [pl seekToTime:CMTimeMakeWithSeconds(s, 600)]; }
void av_rejoin(void) { if (!item) return; NSValue *v = item.seekableTimeRanges.lastObject; if (v) { CMTimeRange r = v.CMTimeRangeValue; [pl seekToTime:CMTimeRangeGetEnd(r)]; } [pl play]; paused = 0; }
double av_time(void) { if (rxOn) return rxOff + (item ? secs(item.currentTime) : 0); return item ? secs(item.currentTime) : 0; }
double av_duration(void) { if (rxOn) return rxDur; if (!item) return 0; double d = secs(item.duration); return d > 0 && d < 1e7 ? d : 0; }
double av_buffered(void) { if (!item) return rxOn ? rxOff : 0; double e = rxOn ? rxOff : 0; for (NSValue *v in item.loadedTimeRanges) { double x = (rxOn ? rxOff : 0) + secs(CMTimeRangeGetEnd(v.CMTimeRangeValue)); if (x > e) e = x; } return e; }
VidState av_state(void) { return st; }
const char *av_error(void) { return err; }
int av_live(void) { return shown; }
int av_has_media(void) { return hasMedia; }
int av_width(void) { return item && shown ? (int)item.presentationSize.width : 0; }
int av_height(void) { return item && shown ? (int)item.presentationSize.height : 0; }
double av_fps(void) { if (!item) return 0; for (AVPlayerItemTrack *t in item.tracks) if ([t.assetTrack.mediaType isEqualToString:AVMediaTypeVideo]) return t.assetTrack.nominalFrameRate; return 0; }
void av_volume(int pct, int easeMs) { volTo = pct / 100.0f; if (easeMs <= 0) { vol = volTo; volStep = 0; if (pl) pl.volume = vol; } else volStep = (volTo - vol) / (easeMs / 1000.0f); }

static AVMediaSelectionGroup *group(NSString *kind) { if (!item) return nil; AVAsset *a = item.asset;
  if ([a statusOfValueForKey:@"availableMediaCharacteristicsWithMediaSelectionOptions" error:nil] != AVKeyValueStatusLoaded) return nil;
  return [a mediaSelectionGroupForMediaCharacteristic:kind]; }
int av_audio_tracks(VidTrack *o, int max) {
  AVMediaSelectionGroup *g = group(AVMediaCharacteristicAudible); int n = 0;
  for (AVMediaSelectionOption *op in g.options) { if (n >= max) break; VidTrack *t = &o[n]; memset(t, 0, sizeof *t);
    NSString *lg = op.extendedLanguageTag ?: op.locale.languageCode ?: @""; snprintf(t->lang, sizeof t->lang, "%s", lg.UTF8String);
    snprintf(t->name, sizeof t->name, "%s", (op.displayName ?: @"").UTF8String); t->idx = n++; t->channels = 2; }
  return n; }
int av_audio_current(void) { AVMediaSelectionGroup *g = group(AVMediaCharacteristicAudible); if (!g) return 0; AVMediaSelectionOption *s = [item.currentMediaSelection selectedMediaOptionInMediaSelectionGroup:g]; NSUInteger i = s ? [g.options indexOfObject:s] : 0; return i == NSNotFound ? 0 : (int)i; }
void av_select_audio(int idx) { AVMediaSelectionGroup *g = group(AVMediaCharacteristicAudible); if (g && idx >= 0 && idx < (int)g.options.count) [item selectMediaOption:g.options[idx] inMediaSelectionGroup:g]; }

void av_window(float x, float y, float w, float h) { wx = x; wy = y; ww = w; wh = h; crop = 0; }
void av_crop(int sx, int sy, int sw, int sh, float dx, float dy, float dw, float dh) { av_window(dx, dy, dw, dh); crop = 1; cx = sx; cy = sy; cw = sw; ch = sh; }

/* once a frame: the item's state, the volume's ease, and the newest picture into the texture */
void av_pump(void) {
  if (rxOn && !item && st != VID_ERROR) rx_pump();
  if (!item) return;
  double now = CACurrentMediaTime(), dt = lastPump > 0 ? now - lastPump : 0; lastPump = now;
  if (volStep != 0) { vol += volStep * (float)dt; if ((volStep > 0 && vol >= volTo) || (volStep < 0 && vol <= volTo)) { vol = volTo; volStep = 0; } if (pl) pl.volume = vol; }
  if (item.status == AVPlayerItemStatusFailed) { if (st != VID_ERROR) { snprintf(err, sizeof err, "%s", (item.error.localizedDescription ?: @"failed").UTF8String); fprintf(stderr, "[video] mac: %s\n", err); } st = VID_ERROR; return; }
  if (item.status == AVPlayerItemStatusReadyToPlay) { hasMedia = 1; if (rxOn && !rxStarted) { rxStarted = 1; [pl seekToTime:kCMTimeZero]; if (paused) [pl pause]; } } /* (a growing playlist: from its start, not its edge) */
  double d = av_duration(); if (d > 0 && av_time() >= d - 0.25 && hasMedia && !paused) { st = VID_ENDED; ended = 1; }
  else if (paused) st = VID_PAUSED;
  else if (pl.timeControlStatus == AVPlayerTimeControlStatusPlaying) st = shown ? VID_PLAYING : VID_LOADING;
  else st = shown ? VID_BUFFERING : VID_LOADING;
  CMTime t = [out itemTimeForHostTime:now];
  if (![out hasNewPixelBufferForItemTime:t]) return;
  CVPixelBufferRef pb = [out copyPixelBufferForItemTime:t itemTimeForDisplay:NULL]; if (!pb) return;
  CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
  int w = (int)CVPixelBufferGetWidth(pb), h = (int)CVPixelBufferGetHeight(pb), stride = (int)CVPixelBufferGetBytesPerRow(pb); const void *px = CVPixelBufferGetBaseAddress(pb);
  if (!tex || w != tw || h != th) { if (tex) gfx_tex_free(tex); tex = gfx_tex_create(NULL, w, h, 4, 1); tw = w; th = h; }
  gfx_flush(); glBindTexture(GL_TEXTURE_2D, tex); glPixelStorei(GL_UNPACK_ALIGNMENT, 4); glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / 4);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, px); glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  { /* the colours at its edges (a band an eighth deep, sampled sparsely): the glow round the preview follows them */
    const unsigned char *b = px; int bw = w / 8, bh = h / 8; unsigned acc[4][3] = { { 0 } }, cnt[4] = { 0 };
    for (int j = 0; j < 12; j++) for (int i = 0; i < 6; i++) { int sy = h * (j * 2 + 1) / 24, sx = bw * (i * 2 + 1) / 12; const unsigned char *p0 = b + (size_t)sy * stride + (size_t)sx * 4, *p1 = b + (size_t)sy * stride + (size_t)(w - 1 - sx) * 4;
      acc[0][0] += p0[2]; acc[0][1] += p0[1]; acc[0][2] += p0[0]; cnt[0]++; acc[2][0] += p1[2]; acc[2][1] += p1[1]; acc[2][2] += p1[0]; cnt[2]++; }
    for (int j = 0; j < 6; j++) for (int i = 0; i < 12; i++) { int sx = w * (i * 2 + 1) / 24, sy = bh * (j * 2 + 1) / 12; const unsigned char *p0 = b + (size_t)sy * stride + (size_t)sx * 4, *p1 = b + (size_t)(h - 1 - sy) * stride + (size_t)sx * 4;
      acc[1][0] += p0[2]; acc[1][1] += p0[1]; acc[1][2] += p0[0]; cnt[1]++; acc[3][0] += p1[2]; acc[3][1] += p1[1]; acc[3][2] += p1[0]; cnt[3]++; }
    for (int e = 0; e < 4; e++) edges[e] = 0xff000000u | ((acc[e][0] / cnt[e]) << 16) | ((acc[e][1] / cnt[e]) << 8) | (acc[e][2] / cnt[e]); edgesOk = 1; }
  CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly); CVPixelBufferRelease(pb);
  if (!shown) { shown = 1; fprintf(stderr, "[video] mac: first picture %dx%d\n", w, h); }
}
/* the picture where the TV's plane would show it: in its window (a crop: that part of the frame) */
static void frame_uv(float *u0, float *v0, float *u1, float *v1) { *u0 = 0; *v0 = 0; *u1 = 1; *v1 = 1;
  if (crop && tw > 0 && th > 0) { *u0 = (float)cx / tw; *v0 = (float)cy / th; *u1 = (float)(cx + cw) / tw; *v1 = (float)(cy + ch) / th; } }
void video_mac_underlay(void) {
  if (!item || !shown || !tex) return; float u0, v0, u1, v1; frame_uv(&u0, &v0, &u1, &v1);
  gfx_image(tex, wx, wy, ww, wh, rad, 0xffffffffu, u0, v0, u1, v1);
}
/* a see-through box (the TV erases there to show its plane): the part of the picture that falls in it, rounded */
void video_mac_hole(float x, float y, float w, float h, float r, float a) {
  if (!item || !shown || !tex || ww <= 0 || wh <= 0) return; float u0, v0, u1, v1; frame_uv(&u0, &v0, &u1, &v1);
  float fu = (x - wx) / ww, fv = (y - wy) / wh, gu = (x + w - wx) / ww, gv = (y + h - wy) / wh; /* the box within the window */
  gfx_image(tex, x, y, w, h, r, ((unsigned)(255 * (a > 1 ? 1 : a)) << 24) | 0xffffffu, u0 + (u1 - u0) * fu, v0 + (v1 - v0) * fv, u0 + (u1 - u0) * gu, v0 + (v1 - v0) * gv);
}
int video_mac_real(void) { return item != nil; }
