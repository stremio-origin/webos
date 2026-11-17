/* The formatting is rough and I'm not going to bother. Pull requests that fix it are welcome; complaints without one help neither me nor the end user. */
/* The Mac preview: in the visible preview (SB_SHOW, or SB_REAL_VIDEO=1) the stream really plays (video_mac_av.m:
   AVFoundation, its pictures drawn where the TV's plane would be); otherwise (the scripted runs, SB_REAL_VIDEO=0)
   playback is simulated (a clock that runs) so the player's screens can be worked on. For testing: a URL with "fail" in it errors 1.2 s after loading; SB_VID_SIZE=1920x1080
   sets the picture's size (1920x800 otherwise), SB_VID_DUR its length in seconds, SB_VID_START where the first load starts, SB_VID_SUBS=1 adds two subtitle
   tracks in the file. */
#include "core/video.h"
#include <SDL2/SDL.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void av_load(const char *u); void av_unload(void); void av_play(void); void av_pause(void); void av_seek(double s); void av_rejoin(void);
double av_time(void); double av_duration(void); double av_buffered(void); VidState av_state(void); const char *av_error(void); int av_live(void); int av_has_media(void);
int av_width(void); int av_height(void); double av_fps(void); void av_volume(int pct, int easeMs); int av_audio_tracks(VidTrack *o, int max); int av_audio_current(void); void av_select_audio(int idx);
void av_window(float x, float y, float w, float h); void av_crop(int sx, int sy, int sw, int sh, float dx, float dy, float dw, float dh); void av_pump(void);
static int R(void) { static int r = -1; if (r < 0) r = getenv("SB_REAL_VIDEO") ? atoi(getenv("SB_REAL_VIDEO")) : getenv("SB_SHOW") != NULL; return r; } /* real playback */
static VidState st = VID_IDLE; static double t0, pos, dur = 5400, pausedAt; static int live; static char url[4096]; static int cur = 0; static int fails; static int vw = 1920, vh = 800;
int video_init(const char *appId) { (void)appId; const char *e = getenv("SB_VID_SIZE"); if (e) sscanf(e, "%dx%d", &vw, &vh); if (getenv("SB_VID_DUR")) dur = atof(getenv("SB_VID_DUR")); return 1; }
void video_set_live(int on) { (void)on; }
void video_live_rejoin(void) { if (R()) { av_rejoin(); return; } if (st == VID_PAUSED) { st = VID_PLAYING; t0 = SDL_GetTicks() / 1000.0 - pos; } }
void video_load(const char *u) { if (R()) { av_load(u); return; } snprintf(url, sizeof url, "%s", u); st = VID_LOADING; t0 = SDL_GetTicks() / 1000.0 + 1.2; pos = 0; live = 0; fails = strstr(u, "fail") != NULL; fprintf(stderr, "[video] load %s\n", u); }
void video_load_timed(const char *base, double durSec) { char u[4200]; snprintf(u, sizeof u, "%s?t=0", base); video_load(u); if (durSec > 0) dur = durSec; }
void video_unload(void) { if (R()) { av_unload(); return; } st = VID_IDLE; live = 0; url[0] = 0; }
void video_play(void) { if (R()) { av_play(); return; } if (st == VID_PAUSED) { st = VID_PLAYING; t0 = SDL_GetTicks() / 1000.0 - pos; } }
int video_has_media(void) { if (R()) return av_has_media(); return live; }
void video_set_volume(int pct, int easeMs) { if (R()) { av_volume(pct, easeMs); return; } (void)pct; (void)easeMs; }
void video_pause(void) { if (R()) { av_pause(); return; } if (st == VID_PLAYING) { st = VID_PAUSED; pausedAt = pos; } }
void video_seek(double s) { if (R()) { av_seek(s); return; } pos = s < 0 ? 0 : s > dur ? dur : s; t0 = SDL_GetTicks() / 1000.0 - pos; }
double video_time(void) { if (R()) return av_time(); return pos; }
double video_duration(void) { if (R()) return av_duration(); return st == VID_IDLE || st == VID_LOADING ? 0 : dur; }
double video_buffered_end(void) { if (R()) return av_buffered(); return pos + 90 > dur ? dur : pos + 90; }
VidState video_state(void) { if (R()) return av_state(); return st; }
const char *video_error(void) { if (R()) return av_error(); return ""; }
int video_live(void) { if (R()) return av_live(); return live; }
int video_settling(void) { return 0; }
int video_width(void) { if (R()) return av_width(); return live ? vw : 0; }
int video_height(void) { if (R()) return av_height(); return live ? vh : 0; }
double video_fps(void) { if (R()) return av_fps(); return 0; }
int video_hdr(char *out, int cap) { if (cap > 0) out[0] = 0; return 0; }
/* SB_VID_AUDIO="dts:eng:6,ac3:fre:6": the file's audio tracks (codec:language:channels); SB_VID_NODTS=1: DTS and TrueHD
   tracks answer as the TV's error 200 does (picture on, no sound) */
int video_audio_tracks(VidTrack *out, int max) {
  if (R()) return av_audio_tracks(out, max);
  const char *e = getenv("SB_VID_AUDIO");
  if (!e) { if (max < 2) return 0; memset(out, 0, 2 * sizeof *out); snprintf(out[0].lang, 16, "eng"); snprintf(out[0].name, 64, "English"); out[0].idx = 0; snprintf(out[1].lang, 16, "fre"); snprintf(out[1].name, 64, "French"); out[1].idx = 1; return 2; }
  if (st == VID_IDLE || st == VID_LOADING) return 0; /* the TV lists them once the stream is open */
  int n = 0; while (*e && n < max) { VidTrack *t = &out[n]; memset(t, 0, sizeof *t); int ch = 2;
    sscanf(e, "%15[^:,]:%15[^:,]:%d", t->codec, t->lang, &ch); t->channels = ch; snprintf(t->name, sizeof t->name, "%s %s", t->codec, ch == 6 ? "5.1" : ch == 8 ? "7.1" : "2.0"); t->idx = n++;
    const char *c = strchr(e, ','); if (!c) break; e = c + 1; }
  return n;
}
int video_audio_current(void) { if (R()) return av_audio_current(); return cur; }
void video_select_audio(int idx) { if (R()) { av_select_audio(idx); return; } cur = idx; }
int video_text_tracks(VidTrack *out, int max) {
  if (R()) return 0; /* (the file's own subtitles: not drawn by this preview) */
  if (!getenv("SB_VID_SUBS") || !live || max < 2) return 0; memset(out, 0, 2 * sizeof *out);
  snprintf(out[0].lang, 16, "eng"); snprintf(out[0].name, 64, "English"); out[0].idx = 0; snprintf(out[1].lang, 16, "fre"); snprintf(out[1].name, 64, "French"); out[1].idx = 1; return 2; }
void video_select_text(int idx) { (void)idx; }
VidPlaneCaps video_plane_caps(void) { VidPlaneCaps c = { 1, 0, 0, 0 }; const char *e;
  if ((e = getenv("SB_GROW_LEAD"))) c.leadMs = atoi(e); if ((e = getenv("SB_GROW_STEP"))) { c.stepMs = atoi(e); c.perFrame = c.stepMs <= 0; } if ((e = getenv("SB_GROW")) && !strcmp(e, "cut")) c.cut = 1; return c; }
void video_set_window(float x, float y, float w, float h) { if (R()) av_window(x, y, w, h); static char last[96]; char t[96]; snprintf(t, sizeof t, "%.0f,%.0f %.0fx%.0f", x, y, w, h); if (strcmp(t, last)) { snprintf(last, sizeof last, "%s", t); fprintf(stderr, "[video] window %s\n", t); } }
void video_pump(void) {
  if (R()) { av_pump(); return; }
  double now = SDL_GetTicks() / 1000.0;
  if (st == VID_LOADING && fails && now >= t0) { st = VID_ERROR; return; }
  if (st == VID_LOADING && now >= t0) { static int first = 1; double s0 = first && getenv("SB_VID_START") ? atof(getenv("SB_VID_START")) : 0; first = 0; st = VID_PLAYING; live = 1; t0 = now - s0; pos = s0; }
  if (st == VID_PLAYING) { pos = now - t0; if (pos >= dur) { pos = dur; st = VID_ENDED; } }
}

int video_partial_plane(void) { return 1; }
int video_set_crop(int sx, int sy, int sw, int sh, float dx, float dy, float dw, float dh) { if (R()) { av_crop(sx, sy, sw, sh, dx, dy, dw, dh); return 1; } fprintf(stderr, "[video] crop %d,%d %dx%d -> %.0f,%.0f %.0fx%.0f\n", sx, sy, sw, sh, dx, dy, dw, dh); return 1; }
void video_set_surface(int w, int h) { (void)w; (void)h; }
int video_audio_unsupported(void) {
  if (R()) return 0;
  if (!getenv("SB_VID_NODTS") || st != VID_PLAYING || pos < 1) return 0;
  VidTrack t[16]; int n = video_audio_tracks(t, 16); return cur < n && (strstr(t[cur].codec, "dts") || strstr(t[cur].codec, "truehd"));
}

int av_edges(uint32_t *o);
int video_edge_colors(uint32_t *out4) { return R() ? av_edges(out4) : 0; }
