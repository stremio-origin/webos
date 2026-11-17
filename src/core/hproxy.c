#include "core/hproxy.h"
#include "core/net.h"
#include "core/sbdts.h"
#include "platform/plat.h"
#include <SDL2/SDL.h>
#include <dlfcn.h>
#include <time.h>
#include <sys/resource.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <unistd.h>
#include <math.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* the streams behind local addresses: a few at a time is plenty (the one playing, the next episode's) */
#define NENT 8
typedef struct { unsigned id; char url[4096], req[512], resp[256]; } Ent;
static Ent ents[NENT]; static int entAt; static unsigned nextId = 1;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static int lsock = -1, port;

static int send_all(int fd, const void *p, size_t n) { const char *c = p; while (n) { ssize_t k = send(fd, c, n, MSG_NOSIGNAL); if (k <= 0) return 0; c += k; n -= (size_t)k; } return 1; }
/* one answer's head, kept until its end so it is sent once, for the last hop (redirects are followed here) */
typedef struct { int fd, head, sent, status, failed; char hdr[2048]; size_t hl; const Ent *e;
  int pl, sniff; char base[4096]; unsigned char *buf; size_t bl, bc; char pend[3072]; int pendN; } Conn;
/* pl: an HLS playlist, kept whole to be rewritten; base: its address after redirects; sniff: its first bytes awaited
   (a playlist by its content though not by its address or type), its head (pend) held until they tell */
/* an address another one points to (a playlist's line, a redirect): absolute, from the root, or beside it */
static void url_resolve(const char *base, const char *ref, char *out, size_t cap) {
  if (strstr(ref, "://")) { snprintf(out, cap, "%s", ref); return; }
  const char *sch = strstr(base, "://"); if (!sch) { snprintf(out, cap, "%s", ref); return; }
  if (ref[0] == '/' && ref[1] == '/') { snprintf(out, cap, "%.*s%s", (int)(sch - base + 1), base, ref); return; }
  const char *host = sch + 3, *path = strchr(host, '/'); if (!path) path = host + strlen(host);
  if (ref[0] == '/') { snprintf(out, cap, "%.*s%s", (int)(path - base), base, ref); return; }
  const char *q = strpbrk(path, "?#"), *end = q ? q : path + strlen(path), *sl = end; while (sl > path && sl[-1] != '/') sl--;
  snprintf(out, cap, "%.*s%s", (int)(sl - base), base, ref);
}
static void pct_enc(const char *s, char *out, size_t cap) { size_t o = 0; static const char *H = "0123456789ABCDEF";
  for (; *s && o + 4 < cap; s++) { unsigned char c = (unsigned char)*s; if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("-_.~", c)) out[o++] = (char)c; else { out[o++] = '%'; out[o++] = H[c >> 4]; out[o++] = H[c & 15]; } } out[o] = 0; }
static void pct_dec(const char *s, char *out, size_t cap) { size_t o = 0; for (; *s && o + 1 < cap; s++) { if (*s == '%' && s[1] && s[2]) { char h[3] = { s[1], s[2], 0 }; out[o++] = (char)strtol(h, NULL, 16); s += 2; } else out[o++] = *s; } out[o] = 0; }
static int looks_playlist(const char *url) { const char *q = strpbrk(url, "?#"); size_t n = q ? (size_t)(q - url) : strlen(url); return (n > 5 && !strncasecmp(url + n - 5, ".m3u8", 5)) || (n > 4 && !strncasecmp(url + n - 4, ".m3u", 4)); }
/* the start of a playlist (after a byte order mark): 1 yes, 0 no, -1 too few bytes yet */
static int m3u_start(const unsigned char *b, size_t n) { size_t o = n >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF ? 3 : 0;
  if (n - o < 7) return n && memcmp(b + o, "#EXTM3U", n - o) ? 0 : -1; return !memcmp(b + o, "#EXTM3U", 7); }
static int buf_add(Conn *c, const unsigned char *p, size_t n) {
  if (c->bl + n + 1 > c->bc) { size_t nc = (c->bl + n + 1) * 2; unsigned char *nb = realloc(c->buf, nc); if (!nb) return 0; c->buf = nb; c->bc = nc; }
  memcpy(c->buf + c->bl, p, n); c->bl += n; c->buf[c->bl] = 0; return 1; }
/* the held head and what came so far, sent as they are: not a playlist after all */
static int sniff_pass(Conn *c) { c->sniff = 0; int ok = send_all(c->fd, c->pend, (size_t)c->pendN) && (!c->bl || send_all(c->fd, c->buf, c->bl)); c->bl = 0; if (!ok) c->failed = 1; return ok; }
static const char *const PASS[] = { "content-type:", "content-length:", "content-range:", "accept-ranges:", "last-modified:", "etag:", NULL };
static int overridden(const Ent *e, const char *line) { /* the addon's response headers win over the server's */
  const char *c = strchr(line, ':'); if (!c) return 0; size_t n = (size_t)(c - line);
  for (const char *p = e->resp; *p; ) { const char *nl = strchr(p, '\n'); const char *pc = strchr(p, ':'); if (pc && (!nl || pc < nl) && (size_t)(pc - p) == n && !strncasecmp(p, line, n)) return 1; if (!nl) break; p = nl + 1; }
  return 0;
}
static void on_hdr(const char *line, void *ud) {
  Conn *c = ud; if (c->sent) return;
  int code; if (sscanf(line, "HTTP/%*s %d", &code) == 1) { c->status = code; c->hl = 0; return; } /* a new hop */
  if (!strncasecmp(line, "location:", 9)) { char loc[4096], nb[4096]; const char *v = line + 9; while (*v == ' ') v++; snprintf(loc, sizeof loc, "%.*s", (int)strcspn(v, "\r\n"), v); url_resolve(c->base, loc, nb, sizeof nb); snprintf(c->base, sizeof c->base, "%s", nb); } /* where the playlist really is: its own lines are beside it */
  if (line[0] == '\r' || line[0] == '\n') { if (c->status < 200 || (c->status >= 300 && c->status < 400)) return; /* the end of a redirect's head */
    if (c->status < 300 && !c->head && (c->pl || strcasestr(c->hdr, "mpegurl") || strcasestr(c->e->resp, "mpegurl"))) { c->pl = 1; c->sent = 1; return; } /* a playlist (by its address or its type): kept whole, rewritten, sent after */
    char head[3072]; int k = snprintf(head, sizeof head, "HTTP/1.1 %d %s\r\n%.*s", c->status, c->status < 300 ? "OK" : "Error", (int)c->hl, c->hdr);
    for (const char *p = c->e->resp; *p && k < (int)sizeof head - 4; ) { const char *nl = strchr(p, '\n'); size_t l = nl ? (size_t)(nl - p) : strlen(p); k += snprintf(head + k, sizeof head - k, "%.*s\r\n", (int)l, p); if (!nl) break; p = nl + 1; }
    k += snprintf(head + k, sizeof head - k, "Connection: close\r\n\r\n");
    k = k < (int)sizeof head ? k : (int)sizeof head - 1;
    if (c->status < 300 && !c->head) { memcpy(c->pend, head, (size_t)k); c->pendN = k; c->sniff = 1; c->sent = 1; return; } /* its first bytes decide */
    c->sent = 1; if (!send_all(c->fd, head, (size_t)k)) c->failed = 1; return; }
  for (int i = 0; PASS[i]; i++) if (!strncasecmp(line, PASS[i], strlen(PASS[i])) && !overridden(c->e, line)) { size_t l = strlen(line); if (c->hl + l < sizeof c->hdr) { memcpy(c->hdr + c->hl, line, l); c->hl += l; } }
}
static int on_body(const unsigned char *p, size_t n, void *ud) { Conn *c = ud; if (c->failed) return 0; if (!p) return 1; if (!c->sent) return 1;
  if (c->sniff) { if (!buf_add(c, p, n)) { c->failed = 1; return 0; } int m = m3u_start(c->buf, c->bl); if (m < 0) return 1;
    if (m) { c->sniff = 0; c->pl = 1; return 1; } return sniff_pass(c); } /* a playlist by its first line: kept whole as one */
  if (c->pl) { if (c->bl + n > 8u << 20) { c->failed = 1; return 0; } /* (a playlist is a few KB; 8 MB is not one) */
    if (!buf_add(c, p, n)) { c->failed = 1; return 0; } return 1; }
  if (!send_all(c->fd, p, n)) { c->failed = 1; return 0; } return 1; }
/* an HLS playlist through here: every address in it (variants, segments, keys, init sections, audio and subtitle
   renditions) made one of ours, so each is fetched with the stream's headers too. Before, the playlist came through
   with them and everything it named went straight to its server without: a stream whose server checks them played
   for some channels and not others */
static void send_playlist(Conn *c, unsigned id) {
  size_t cap = c->bl * 3 + 65536, o = 0; char *out = malloc(cap); if (!out) return;
  char abs[4096], enc[12300];
  #define PUT(s, n) do { size_t _n = (n); if (o + _n < cap) { memcpy(out + o, (s), _n); o += _n; } } while (0)
  #define OURS(u) do { url_resolve(c->base, (u), abs, sizeof abs); pct_enc(abs, enc, sizeof enc); char _p[64]; int _k = snprintf(_p, sizeof _p, "http://127.0.0.1:%d/s/%u/u/", port, id); PUT(_p, (size_t)_k); PUT(enc, strlen(enc)); } while (0)
  for (char *ln = (char *)c->buf; ln && *ln; ) {
    char *nl = strchr(ln, '\n'); size_t L = nl ? (size_t)(nl - ln) : strlen(ln); size_t T = L; if (T && ln[T - 1] == '\r') T--;
    char save = ln[T]; ln[T] = 0;
    if (T && ln[0] != '#') OURS(ln); /* a variant or a segment */
    else { char *u = strstr(ln, "URI=\""); /* a key, an init section, a rendition */
      if (u) { u += 5; char *q = strchr(u, '"'); if (q) { *q = 0; PUT(ln, (size_t)(u - ln)); OURS(u); *q = '"'; PUT(q, strlen(q)); } else PUT(ln, T); }
      else PUT(ln, T); }
    ln[T] = save; PUT("\n", 1); ln = nl ? nl + 1 : NULL; }
  #undef OURS
  #undef PUT
  char head[256]; int k = snprintf(head, sizeof head, "HTTP/1.1 200 OK\r\nContent-Type: application/vnd.apple.mpegurl\r\nContent-Length: %zu\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n", o);
  if (send_all(c->fd, head, (size_t)k)) send_all(c->fd, out, o); free(out);
}
/* ---- a file's sound converted for the TV: a local HLS stream ----
   One conversion at a time (the film playing). Its reading goes through one kept-alive range connection with a ring of
   the last 4 MiB read (the demuxer steps back a little now and then); the library itself is loaded on the first
   conversion only. The playlist is the file's keyframes grouped into segments; each segment is made when the TV asks
   for it, on that request's own thread, and written through as it is made. A request arriving while another runs
   stops it (the TV moved on), then takes its place. */
#define RING (4 << 20)
static int start(void);
typedef struct { char url[4096]; NetRange *net; long long size; unsigned char *ring; long long start; int len; volatile int *stop; int contig; } Reader;
static int ring_put(Reader *r, long long at, const unsigned char *b, int n) { /* appends at = start + len */
  for (int i = 0; i < n; ) { int o = (int)((at + i) % RING), k = n - i < RING - o ? n - i : RING - o; memcpy(r->ring + o, b + i, k); i += k; }
  r->len += n; if (r->len > RING) { r->start += r->len - RING; r->len = RING; } return n;
}
static int rd_read(void *ud, long long pos, unsigned char *buf, int n) {
  Reader *r = ud; if (r->size > 0 && pos >= r->size) return 0;
  if (!(pos >= r->start && pos < r->start + r->len)) {
    long long end = r->start + r->len, at = pos;
    if (r->len && pos >= end && pos - end < (1 << 20)) at = end; else { r->start = pos; r->len = 0; } /* a short hop forward reads through */
    int want = r->contig > 2 ? (2 << 20) : (512 << 10); /* reading on: larger requests; a seek's first look: a small one */
    for (int tries = 0;; tries++) {
      if (r->stop && *r->stop) return -1;
      unsigned char *b = NULL; long g = 0; long long total = -1; int st = 0;
      int e = net_range_get(r->net, r->url, at, want, &b, &g, &total, &st, 15000);
      if (total > 0) r->size = total;
      if (g > 0) { ring_put(r, at, b, (int)g); free(b); break; }
      free(b);
      if (e == NR_OK || (r->size > 0 && at >= r->size)) return 0;
      if ((e != NR_NET && e != NR_TIMEOUT && e != NR_BUSY) || tries >= 3) { fprintf(stderr, "[dts] read at %lld failed (%d, HTTP %d)\n", at, e, st); return -1; }
      SDL_Delay(500u << tries);
    }
    r->contig = at == end ? r->contig + 1 : 0;
    if (!(pos >= r->start && pos < r->start + r->len)) return -1;
  }
  int k = (int)(r->start + r->len - pos); if (k > n) k = n;
  for (int i = 0; i < k; ) { int o = (int)((pos + i) % RING), m = k - i < RING - o ? k - i : RING - o; memcpy(buf + i, r->ring + o, m); i += m; }
  return k;
}
static const sbd_api *D; static int dTried;
static int dts_load(void) { /* the converter library, beside the app */
  if (D) return 1; if (dTried) return 0; dTried = 1;
  char *base = SDL_GetBasePath(), p[1200];
#ifdef __APPLE__
  snprintf(p, sizeof p, "%slibsbdts.dylib", base ? base : "");
#else
  snprintf(p, sizeof p, "%slibsbdts.so", base ? base : "");
#endif
  SDL_free(base);
  void *h = dlopen(p, RTLD_NOW | RTLD_LOCAL); if (!h) { fprintf(stderr, "[dts] no converter library: %s\n", dlerror()); return 0; }
  const sbd_api *(*get)(void) = (const sbd_api *(*)(void))dlsym(h, "sbd_get");
  if (!get || !get() || get()->api() != SBD_API_VERSION) { fprintf(stderr, "[dts] the converter library does not match\n"); return 0; }
  D = get(); fprintf(stderr, "[dts] converter %s\n", D->version()); return 1;
}
int hls_conv_available(void) {
  if (D) return 1; char *base = SDL_GetBasePath(), p[1200];
#ifdef __APPLE__
  snprintf(p, sizeof p, "%slibsbdts.dylib", base ? base : "");
#else
  snprintf(p, sizeof p, "%slibsbdts.so", base ? base : "");
#endif
  SDL_free(base); return access(p, R_OK) == 0;
}
static double mono_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static int rss_mb(void) {
#ifdef __APPLE__
  struct rusage u; getrusage(RUSAGE_SELF, &u); return (int)(u.ru_maxrss >> 20); /* (the peak, on the Mac) */
#else
  FILE *f = fopen("/proc/self/statm", "r"); long a = 0, b = 0; if (f) { if (fscanf(f, "%ld %ld", &a, &b) != 2) b = 0; fclose(f); } return (int)(b * (sysconf(_SC_PAGESIZE) / 1024) / 1024);
#endif
}
typedef struct Conv { unsigned id; int ord, flags, state, refs, running; volatile int stop, dead; char err[160]; Reader rd; SbdSrc *src; sbd_info info; pthread_mutex_t runMu; double t0; } Conv;
static Conv *conv; static unsigned convN; /* the current one, under mu */
static void conv_unref(Conv *c) { /* the last holder frees it (the run in progress, the opening thread, the current slot) */
  pthread_mutex_lock(&mu); int last = --c->refs == 0; pthread_mutex_unlock(&mu); if (!last) return;
  if (c->src) D->close(c->src); net_range_free(c->rd.net); free(c->rd.ring); pthread_mutex_destroy(&c->runMu); free(c);
}
static Conv *conv_get(unsigned id) { pthread_mutex_lock(&mu); Conv *c = conv && conv->id == id ? conv : NULL; if (c) c->refs++; pthread_mutex_unlock(&mu); return c; }
static void *conv_open(void *a) {
  plat_thread_background(5); Conv *c = a; char err[160] = ""; SbdSrc *s = NULL;
  c->rd.net = net_range_new(); c->rd.ring = malloc(RING); c->rd.stop = &c->dead;
  if (!c->rd.net || !c->rd.ring) snprintf(err, sizeof err, "out of memory");
  else if (!dts_load()) snprintf(err, sizeof err, "no converter library");
  else { unsigned char b1; if (rd_read(&c->rd, 0, &b1, 1) < 1) snprintf(err, sizeof err, "the file can't be read"); /* (its first bytes, and its size with them) */
    else s = D->open(rd_read, &c->rd, c->rd.size, c->ord, c->flags, 2.0, &c->dead, err, sizeof err); }
  pthread_mutex_lock(&mu);
  if (s) { c->src = s; c->info = *D->info(s); c->state = 1; } else { c->state = -1; snprintf(c->err, sizeof c->err, "%s", err[0] ? err : "failed"); }
  pthread_mutex_unlock(&mu);
  if (s) fprintf(stderr, "[dts] path: converted, %s (%s %dx%d, %d %s), %d segments of %.0f s, opened in %.1f s, %d MB\n", c->info.path, c->info.vcodec, c->info.width, c->info.height,
                 c->info.inCh, c->info.acodec, c->info.nseg, c->info.dur, mono_s() - c->t0, rss_mb());
  else fprintf(stderr, "[dts] the converter can't take this file: %s\n", c->err);
  conv_unref(c); return NULL;
}
void hls_conv_start(const char *url, int audioOrd, int flags) {
  hls_conv_stop();
  Conv *c = calloc(1, sizeof *c); if (!c) return;
  snprintf(c->rd.url, sizeof c->rd.url, "%s", url); c->rd.size = -1; c->running = -100; c->ord = audioOrd; c->flags = (flags & HC_NOINDEX ? SBD_NOINDEX : 0) | (flags & HC_DV81 ? SBD_DV81 : 0) | (flags & HC_COPYAUDIO ? SBD_COPYAUDIO : 0); c->refs = 2; c->t0 = mono_s(); pthread_mutex_init(&c->runMu, NULL);
  if (!start()) { c->state = -1; snprintf(c->err, sizeof c->err, "no local server"); }
  pthread_mutex_lock(&mu); c->id = ++convN; conv = c; pthread_mutex_unlock(&mu);
  if (c->state < 0) { conv_unref(c); return; }
  pthread_t t; if (pthread_create(&t, NULL, conv_open, c)) { c->state = -1; snprintf(c->err, sizeof c->err, "thread"); conv_unref(c); return; } pthread_detach(t);
}
int hls_conv_poll(char *url, size_t cap, char *why, size_t whyCap, HlsConvInfo *info) {
  pthread_mutex_lock(&mu); Conv *c = conv; int st = c ? c->state : -1;
  if (c && st > 0) { if (c->info.indexed) snprintf(url, cap, "http://127.0.0.1:%d/h/%u/index.m3u8", port, c->id); else snprintf(url, cap, "http://127.0.0.1:%d/h/%u/all.ts", port, c->id);
    if (info) { memset(info, 0, sizeof *info); info->indexed = c->info.indexed; info->dur = c->info.dur; info->outCh = c->info.outCh; info->dvIn = c->info.dvIn; info->dvOut = c->info.dvOut;
      snprintf(info->vcodec, sizeof info->vcodec, "%s", c->info.vcodec); snprintf(info->path, sizeof info->path, "%s", c->info.path); } }
  if (st < 0 && why) snprintf(why, whyCap, "%s", c ? c->err : "none");
  pthread_mutex_unlock(&mu); return st;
}
void hls_conv_stop(void) {
  pthread_mutex_lock(&mu); Conv *c = conv; conv = NULL; if (c) { c->dead = 1; c->stop = 1; } pthread_mutex_unlock(&mu);
  if (c) { fprintf(stderr, "[dts] conversion ended (%d MB)\n", rss_mb()); conv_unref(c); }
}
static int fd_write(void *ud, const unsigned char *p, int n) { /* one HTTP chunk per write (the muxer's 32 KB) */
  int fd = *(int *)ud; char h[16]; int hl = snprintf(h, sizeof h, "%x\r\n", n);
  return send_all(fd, h, (size_t)hl) && send_all(fd, p, (size_t)n) && send_all(fd, "\r\n", 2) ? n : -1; }
static void conv_serve(int fd, unsigned id, const char *path, int head) {
  Conv *c = conv_get(id); int seg = -1; double from = 0; const char *nm = strchr(path + 3, '/');
  if (!c || c->state <= 0 || !nm) { const char *b = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"; send_all(fd, b, strlen(b)); if (c) conv_unref(c); return; }
  nm++;
  if (!strncmp(nm, "index.m3u8", 10)) { /* the keyframe index as a VOD playlist */
    int n = c->info.nseg; size_t cap = 256 + (size_t)n * 40; char *pl = malloc(cap); size_t k = 0; double mx = 1;
    for (int i = 0; i < n; i++) { double d = D->seg_dur(c->src, i); if (d > mx) mx = d; }
    k += snprintf(pl + k, cap - k, "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:%d\n#EXT-X-MEDIA-SEQUENCE:0\n#EXT-X-PLAYLIST-TYPE:VOD\n", (int)ceil(mx));
    for (int i = 0; i < n && k < cap; i++) k += snprintf(pl + k, cap - k, "#EXTINF:%.3f,\n%d.ts\n", D->seg_dur(c->src, i), i);
    if (k < cap) k += snprintf(pl + k, cap - k, "#EXT-X-ENDLIST\n");
    char h[200]; int hl = snprintf(h, sizeof h, "HTTP/1.1 200 OK\r\nContent-Type: application/vnd.apple.mpegurl\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", k);
    send_all(fd, h, (size_t)hl); if (!head) send_all(fd, pl, k); free(pl); conv_unref(c); return;
  }
  if (sscanf(nm, "%d.ts", &seg) != 1) { seg = -1; const char *t = strstr(nm, "t="); from = t ? atof(t + 2) : 0; if (strncmp(nm, "all.ts", 6)) { conv_unref(c); return; } }
  if (seg >= c->info.nseg || (seg >= 0 && !c->info.indexed)) { const char *b = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"; send_all(fd, b, strlen(b)); conv_unref(c); return; }
  /* the length is not known before the segment is made: sent in chunks, the last one saying it is complete */
  const char *h = "HTTP/1.1 200 OK\r\nContent-Type: video/mp2t\r\nCache-Control: no-cache\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n";
  if (head) { send_all(fd, h, strlen(h)); conv_unref(c); return; }
  /* the segment after the one being made waits for it (players fetch the next one ahead) and then continues it; any
     other stops the run in progress (a seek, or the same segment asked again) */
  /* (the head goes out at once: a player opening the next segment while still reading this one waits for it, and this
     one's run would wait for that player) */
  if (!send_all(fd, h, strlen(h))) { conv_unref(c); return; }
  pthread_mutex_lock(&mu); int follows = seg >= 0 && c->running >= 0 && seg == c->running + 1; if (!follows) c->stop = 1; pthread_mutex_unlock(&mu);
  pthread_mutex_lock(&c->runMu); c->stop = c->dead;
  pthread_mutex_lock(&mu); c->running = seg; pthread_mutex_unlock(&mu);
  {
    sbd_stats st; char err[160] = ""; c->rd.stop = &c->stop;
    int r = D->run(c->src, seg, from, fd_write, &fd, &c->stop, &st, err, sizeof err);
    if (r == 0) send_all(fd, "0\r\n\r\n", 5); /* (a run cut short ends without it: the player sees it incomplete) */
    c->rd.stop = &c->dead;
    if (seg < 0) fprintf(stderr, "[dts] stream from %.0f s: %s, %.1f MB, cpu %.1f s over %.0f s\n", from, r == 0 ? "to the end" : r == -2 ? "stopped" : err, st.bytes / 1048576.0, st.cpu, st.wall);
    else fprintf(stderr, "[dts] segment %d%s: %.1f-%.1f s, %s, %.0f KB, cpu %.0f ms, wall %.0f ms (reading %.0f), queue %.1f MB, %d MB\n", seg, st.fresh ? " (seek)" : "", D->seg_start(c->src, seg), D->seg_start(c->src, seg) + D->seg_dur(c->src, seg),
                 r == 0 ? "done" : r == -2 ? "stopped" : err, st.bytes / 1024.0, st.cpu * 1000, st.wall * 1000, st.readWait * 1000, st.peakQueue / 1048576.0, rss_mb());
    if (st.rpuBad || (seg <= 0 && st.rpuOk)) fprintf(stderr, "[dv] segment %d: %ld RPUs converted, %ld left out, %ld enhancement-layer units dropped\n", seg, st.rpuOk, st.rpuBad, st.elDropped);
  }
  pthread_mutex_lock(&mu); c->running = -100; pthread_mutex_unlock(&mu);
  pthread_mutex_unlock(&c->runMu); conv_unref(c);
}

static void *serve(void *a) {
  plat_thread_background(5);
  int fd = (int)(intptr_t)a; char req[8192]; size_t got = 0;
  while (got < sizeof req - 1) { ssize_t k = recv(fd, req + got, sizeof req - 1 - got, 0); if (k <= 0) break; got += (size_t)k; req[got] = 0; if (strstr(req, "\r\n\r\n")) break; }
  req[got] = 0; char method[8] = "", path[8192] = ""; unsigned id = 0; /* (a playlist's address of ours carries a whole address) */
  if (sscanf(req, "%7s %8191s", method, path) == 2 && sscanf(path, "/h/%u/", &id) == 1) { conv_serve(fd, id, path, !strcmp(method, "HEAD")); close(fd); return NULL; }
  if (sscanf(path, "/s/%u", &id) == 1) {
    Ent e; int found = 0; pthread_mutex_lock(&mu); for (int i = 0; i < NENT; i++) if (ents[i].id == id) { e = ents[i]; found = 1; } pthread_mutex_unlock(&mu);
    if (found) {
      char hdrs[1024]; snprintf(hdrs, sizeof hdrs, "%s", e.req);
      const char *rg = strcasestr(req, "\r\nrange:"); if (rg) { rg += 8; while (*rg == ' ') rg++; size_t l = strcspn(rg, "\r\n"); size_t hl = strlen(hdrs); snprintf(hdrs + hl, sizeof hdrs - hl, "Range: %.*s\n", (int)(l < 200 ? l : 200), rg); }
      /* /s/<id>/u/<address>: one an HLS playlist of this stream names (see send_playlist), else the stream itself */
      char target[4096]; const char *us = strchr(path + 3, '/'); if (us && !strncmp(us, "/u/", 3)) pct_dec(us + 3, target, sizeof target); else snprintf(target, sizeof target, "%s", e.url);
      Conn c; memset(&c, 0, sizeof c); c.fd = fd; c.e = &e; c.head = !strcmp(method, "HEAD"); snprintf(c.base, sizeof c.base, "%s", target); c.pl = looks_playlist(target);
      Uint32 tq = SDL_GetTicks(); int st = net_pipe_sync(target, hdrs, c.head, on_hdr, on_body, &c, 60);
      if (getenv("SB_HPROXY_LOG")) { const char *q = strpbrk(target, "?"); const char *b2 = target; for (const char *p2 = target; *p2 && (!q || p2 < q); p2++) if (*p2 == '/') b2 = p2 + 1; int bl = (int)(q ? q - b2 : (long)strlen(b2)); fprintf(stderr, "[hproxy] %s %.*s -> %d (%s, %u ms)\n", c.head ? "HEAD" : "GET", bl > 60 ? 60 : bl, b2, c.status, c.pl ? "playlist" : "data", SDL_GetTicks() - tq); } /* (development: what the TV fetched through here) */
      if (c.sniff) { if (!c.failed) sniff_pass(&c); else { const char *b = "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"; send_all(fd, b, strlen(b)); } } /* (it ended before its first line said anything) */
      if (c.pl && c.sent && !c.failed && c.bl > 3 && c.buf[0] == 0xEF && c.buf[1] == 0xBB && c.buf[2] == 0xBF) { memmove(c.buf, c.buf + 3, c.bl - 2); c.bl -= 3; } /* (its byte order mark: off) */
      if (c.pl && c.sent && !c.failed && c.bl && !strncmp((const char *)c.buf, "#EXTM3U", 7)) send_playlist(&c, id);
      else if (c.pl && c.sent && c.bl && !c.failed) { char b[160]; int k = snprintf(b, sizeof b, "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", c.bl); if (send_all(fd, b, (size_t)k)) send_all(fd, c.buf, c.bl); } /* named like one, but not: as it came */
      else if (c.pl && c.sent) { const char *b = "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"; send_all(fd, b, strlen(b)); fprintf(stderr, "[hproxy] a playlist failed (HTTP %d)\n", st); } /* (nothing went out yet for it) */
      free(c.buf);
      if (!c.sent) { char b[128]; int k = snprintf(b, sizeof b, "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"); send_all(fd, b, (size_t)k); fprintf(stderr, "[hproxy] the source failed (HTTP %d)\n", st); }
    } else { const char *b = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"; send_all(fd, b, strlen(b)); } }
  close(fd); return NULL;
}
static void *accept_loop(void *a) {
  (void)a; plat_thread_background(5);
  for (;;) { int fd = accept(lsock, NULL, NULL); if (fd < 0) continue;
#ifdef SO_NOSIGPIPE
    int one = 1; setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    pthread_t t; if (pthread_create(&t, NULL, serve, (void *)(intptr_t)fd)) close(fd); else pthread_detach(t); }
  return NULL;
}
static int start(void) {
  if (lsock >= 0) return 1;
  int s = socket(AF_INET, SOCK_STREAM, 0); if (s < 0) return 0;
  struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0; /* this device only, any free port */
  socklen_t al = sizeof a;
  if (bind(s, (struct sockaddr *)&a, sizeof a) || listen(s, 16) || getsockname(s, (struct sockaddr *)&a, &al)) { close(s); return 0; }
  port = ntohs(a.sin_port); lsock = s;
  pthread_t t; if (pthread_create(&t, NULL, accept_loop, NULL)) { close(s); lsock = -1; return 0; } pthread_detach(t);
  fprintf(stderr, "[hproxy] on 127.0.0.1:%d\n", port); return 1;
}
int hproxy_url(const char *url, const char *req, const char *resp, char *out, size_t cap) {
  if (!start()) { snprintf(out, cap, "%s", url); return 0; }
  pthread_mutex_lock(&mu);
  Ent *e = NULL; for (int i = 0; i < NENT; i++) if (ents[i].id && !strcmp(ents[i].url, url) && !strcmp(ents[i].req, req ? req : "") && !strcmp(ents[i].resp, resp ? resp : "")) e = &ents[i];
  if (!e) { e = &ents[entAt]; entAt = (entAt + 1) % NENT; e->id = nextId++; snprintf(e->url, sizeof e->url, "%s", url); snprintf(e->req, sizeof e->req, "%s", req ? req : ""); snprintf(e->resp, sizeof e->resp, "%s", resp ? resp : ""); }
  /* the file's name stays at the end: players and the subtitle reader guess the container from it */
  const char *q = strchr(url, '?'), *sl = url; for (const char *p = url; *p && (!q || p < q); p++) if (*p == '/') sl = p + 1;
  size_t nl = q ? (size_t)(q - sl) : strlen(sl); if (nl > 120) nl = 0;
  snprintf(out, cap, "http://127.0.0.1:%d/s/%u/%.*s", port, e->id, (int)nl, sl);
  pthread_mutex_unlock(&mu); return 1;
}
