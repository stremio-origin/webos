#include "core/bgtrailer.h"
#include "core/trailer.h"
#include "core/store.h"
#include "platform/mem.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

enum { S_OFF, S_META, S_FIND, S_LOAD, S_SHOW, S_GONE };
static BgtHooks H; static int st, idleOn; static unsigned gen, tok;
static double lastCheck, hiddenAt, memAt, loadAt; static char tid[SB_ID], ttype[16];
static char down[2][2048]; static int nDown, iDown; /* the lower MP4s still to fall back to (720p, then 480p) */
static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

void bgt_init(const BgtHooks *h) { if (h) H = *h; }
BgtState bgt_state(void) { static const BgtState m[] = { BGT_OFF, BGT_WAIT, BGT_WAIT, BGT_LOAD, BGT_SHOW, BGT_GONE }; return m[st]; }

/* a soft stop: paused and hidden in the same frame; the decoder waits for a pause in the navigation (unloading
   the pipeline in the key handler made the next frame late) */
static void stop_soft(void) {
  gen++; if (tok) { trailer_cancel(tok); tok = 0; }
  if (st == S_LOAD || st == S_SHOW) {
    int was = st == S_SHOW; if (H.hide) H.hide(!was, H.ud);
    st = S_GONE; hiddenAt = now_s() + (was ? 0.46 : 0); /* it was showing: the picture fades back over it first */
  } else if (st == S_META || st == S_FIND) st = S_OFF;
}
void bgt_release_now(void) {
  gen++; if (tok) { trailer_cancel(tok); tok = 0; }
  if (st == S_LOAD || st == S_SHOW) { if (H.hide) H.hide(1, H.ud); }
  if (st == S_LOAD || st == S_SHOW || st == S_GONE) { if (H.unload) H.unload(H.ud); }
  st = S_OFF;
}
void bgt_key(void) { if (idleOn || st != S_OFF) { if (st == S_LOAD || st == S_SHOW) fprintf(stderr, "[btr] stop: input\n"); idleOn = 0; stop_soft(); } }
void bgt_playing(void) { if (st == S_LOAD) st = S_SHOW; }
void bgt_ended(void) { if (st == S_LOAD || st == S_SHOW) { fprintf(stderr, "[btr] stop: the video ended or failed\n"); stop_soft(); } }
/* the sharper one would not play (or never started): the next lower one, while nothing shows yet; once it shows, a
   failure ends it as before (never a second start under the eyes) */
static int step_down(const char *why) {
  if (st != S_LOAD || iDown >= nDown || !H.load) return 0;
  fprintf(stderr, "[btr] %s: the lower one (%d of %d)\n", why, iDown + 1, nDown); loadAt = now_s();
  H.load(down[iDown++], "video/mp4", tid, H.ud); return 1; }
void bgt_failed(void) { if (!step_down("failed")) bgt_ended(); }

static void on_found(const Trailer *t, const char *err, void *ud) {
  if ((unsigned)(uintptr_t)ud != gen || st != S_FIND) return;
  tok = 0;
  if (!t || !idleOn) { st = S_OFF; return; }
  /* the setting decides, not the free memory (the TV makes room for its own player); the free memory is only logged */
  nDown = 0; iDown = 0; for (int i = 0; i < 2; i++) if (t->down[i][0]) snprintf(down[nDown++], sizeof down[0], "%s", t->down[i]);
  st = S_LOAD; memAt = now_s() + 10; loadAt = now_s(); fprintf(stderr, "[btr] load %s (%s, %d lower to fall back to), %d MB free\n", tid, t->mime, nDown, plat_mem_free_mb());
  if (H.load) H.load(t->url, t->mime, tid, H.ud);
}
static void on_meta(cJSON *m, void *ud) {
  if ((unsigned)(uintptr_t)ud != gen || st != S_META) return;
  char imdb[24], yt[128]; trailer_imdb_id(m, tid, imdb, sizeof imdb); trailer_yt_ids(m, yt, sizeof yt);
  if (!m || !idleOn || (!imdb[0] && !yt[0])) { st = S_OFF; return; }
  st = S_FIND; tok = trailer_find_meta(m, tid, 1, on_found, (void *)(uintptr_t)gen);
}
static void try_start(const BgtView *v) {
  if (v->shedding) return;   /* the TV is struggling: no trailer for now */
  if (v->spotlight) return;  /* the spotlight moves on by itself */
  if (st != S_OFF || v->blocked || !v->focused || !v->focused->id[0] || !v->focused->type[0]) return;
  if (!strcmp(store_get("bgTrailer", "true"), "false") || getenv("SB_NOBTR")) return; /* (SB_NOBTR: development, measuring without it) */
  snprintf(tid, sizeof tid, "%s", v->focused->id); snprintf(ttype, sizeof ttype, "%s", v->focused->type);
  st = S_META; sb_meta(ttype, tid, on_meta, (void *)(uintptr_t)++gen);
}
void bgt_tick(const BgtView *v) {
  double t = now_s();
  if (st == S_GONE && t >= hiddenAt && (v->idle >= 0.7f || t - hiddenAt >= 4)) { st = S_OFF; if (H.unload) H.unload(H.ud); }
  if (t - lastCheck < 1) return;
  lastCheck = t;
  if (st == S_LOAD && t - loadAt > 12) step_down("not started after 12 s"); /* (a sharp one the link can't bring in time) */
  int idle = v->idle > 5 && v->browsing && v->focused && !v->blocked;
  if (idle && !idleOn) { idleOn = 1; try_start(v); }
  else if (!idle && idleOn) { if (st == S_LOAD || st == S_SHOW) fprintf(stderr, "[btr] stop: idle %.1f s, board %d, focused %d, blocked %d\n", v->idle, v->browsing, v->focused != NULL, v->blocked); idleOn = 0; stop_soft(); }
}
