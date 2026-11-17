/* Seek previews (see seekprev.h). One context per film: the main thread owns the picture side (lookups, the one
   texture), a fetch thread reads keyframes one at a time, a decode thread turns them into small pictures. Both
   threads hold the context until they see it closed, so closing never waits on a read in flight. */
#include "core/seekprev.h"
#include "core/prevspike.h"
#include "core/sbdts.h"
#include "core/net.h"
#include "core/store.h"
#include "core/video.h"
#include "platform/plat.h"
#include "ui/gfx.h"
#include <SDL2/SDL.h>
#include <dlfcn.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/resource.h>
#include <sys/syscall.h>
#endif

int stbi_write_jpg_to_func(void (*fn)(void *, void *, int), void *ctx, int x, int y, int comp, const void *data, int quality);
unsigned char *stbi_load_from_memory(const unsigned char *buf, int len, int *x, int *y, int *comp, int req);
void stbi_image_free(void *p);
int (*sp_metered)(void); /* set by a platform that knows its link is metered (a phone on mobile data) */

static NetRange *pool[10]; static int npool; static pthread_mutex_t poolMu = PTHREAD_MUTEX_INITIALIZER; static MkvSession *warmFor;
static __thread NetRange *fnet; static __thread long fguess = 192 * 1024; /* (each fetch thread its own connection: a film closing while the next opens never shares one) */
#ifdef SB_PHONE
#define RING 64     /* pictures kept ready to draw (64 x 256 x 144: about 7 MB) */
#else
#define RING 24     /* a TV: pictures kept ready to draw (24 x 384 x 216: about 6 MB) */
#endif
#define SPREAD 96   /* the first pass over the film (8, 24, 48, 96 evenly) */
#define DENSE 60.0  /* the slide across the picture: every keyframe within this of the finger */
#define WIFI_CAP (3L << 20)
#define METER_CAP (1L << 20)

static double mono(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static double cpu_ms(void) { struct timespec t; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static void nice_to(int n) { /* this thread only: background fill on the small cores, a picture for the finger at once */
#ifdef __linux__
  setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), n);
#else
  (void)n;
#endif
}

/* ---- the decoder library (loaded once, off the main thread) ---- */
static const sbd_prev_api *api; static volatile int apiKnown;
static void load_once(void) {
  void *h = NULL;
#ifdef __ANDROID__
  h = dlopen("libsbprev.so", RTLD_NOW | RTLD_LOCAL); if (!h) h = dlopen("libsbdts.so", RTLD_NOW | RTLD_LOCAL);
#else
  char *base = SDL_GetBasePath(), p[1200];
#ifdef __APPLE__
  snprintf(p, sizeof p, "%slibsbdts.dylib", base ? base : "");
#else
  snprintf(p, sizeof p, "%slibsbdts.so", base ? base : "");
#endif
  SDL_free(base); h = dlopen(p, RTLD_NOW | RTLD_LOCAL);
#endif
  if (!h) return; const sbd_prev_api *(*get)(void) = (const sbd_prev_api *(*)(void))dlsym(h, "sbd_prev_get");
  const sbd_prev_api *a = get ? get() : NULL; if (a && a->api() == SBD_PREV_VERSION) api = a; }
static void *load_bg(void *x) { (void)x; static pthread_once_t once = PTHREAD_ONCE_INIT; pthread_once(&once, load_once); apiKnown = 1; return NULL; }
static int lib_ready(void) { static int asked; if (!asked) { asked = 1; pthread_t t; if (!pthread_create(&t, NULL, load_bg, NULL)) pthread_detach(t); } return apiKnown && api; }

/* ---- one film ---- */
typedef struct { unsigned char *jpg; int len; unsigned char st; } Ent;          /* st: 0 not yet, 1 being read, 2 a picture, 3 none, 4 made then let go for memory (counted, never read again; its neighbour shows) */
#define HELD(st) ((st) == 1 || (st) == 2 || (st) == 4)
typedef struct { int k; unsigned char *frame; long len; int focus; } Job;
typedef struct Ctx {
  pthread_mutex_t mu; pthread_cond_t cv; int refs, stop;
  MkvSession *m; int nk; double *kt; Ent *e; char codec[32]; unsigned char *priv; long privLen; char key[200];
  int partial, tor, torGo, torOut, strict; double torSince; /* tor: a torrent through the engine's preview reads (pieces far off only on a steady, healthy play, 96 at most) */
  double pos, dur, ahead, maxAhead, prevAhead, seekAt, nextBgAt, lastTick; int buffering, paused;
  double focusT; int focusOn, dense, dir;
  double slot; int W, H, hSet, tier, nDec, nDecT; double meanMs;                        /* tier 0 the whole grid, 1 the spread and the finger, 2 the finger only */
  Job q[32]; int nq, nFetch, solo, plain; int wantRgb[8]; int nWant;
  int lanes, clean, failRow, multi; double backUntil, backS, steadySince; long winMax; /* lanes: readers allowed now (3, one more per 2 clean reads, 4 while it plays, 8 paused); multi: -1 not known, 1 the server answers many ranges at once, 0 not */
  struct { int k; unsigned char *rgb; double used; } ring[RING];
  long jpgBytes, jpgMax; /* the pictures kept in memory and the most this device's memory allows (mem_thin) */
  long bytes, guess, capBytes; int got, fails, heavy, saved, prep, tight, roomy; double t0, playSince, fillUntil, bufBack; /* tight: the link under 3x the film's bitrate (while it plays only the finger's pictures); roomy: 6x or more (3 readers) */
  long fsz[64]; int nfsz; int npl, npl2; int exhausted, ph; /* ph: the plan's stage last picked (the dev log) */ /* exhausted: the plan has nothing left to read (every place it fills has its picture or can't have one) */ /* npl: keyframes read with no known place in their Cluster, npl2: those the first window didn't hold */ /* the last keyframes' sizes: the read window fits most of them (85 %), never the biggest seen */
  long srcPx, qBytes; int bigSaid, memSaid; /* qBytes: the keyframes waiting in q; the source's picture (width x height) once known: the player's, else the first picture made */
  int barT, barB, barN; /* the film's black bars (rows of the small picture), the least seen over its first pictures with something in them */ /* prep: the next episode's, made ahead (48 pictures, kept on this device) */
} Ctx;
static Ctx *cur; static pthread_mutex_t gmu = PTHREAD_MUTEX_INITIALIZER;
/* Memory. Decoding one keyframe of a film above 1080p takes far more than its picture: on the Android TV emulator
   (1 GB) two decoders on a 4K HEVC remux took the app from about 120 MB to 245-270 MB and the TV down to 14 MB free,
   where the system ends the app. So: a source above 1080p has one decoder, never two; while the app's memory guard
   says the TV is nearly out (level 2) nothing is decoded and the decoders are closed (their memory let go) until it
   eases; and on an Android TV box under 2.5 GB, a source above 1080p makes no pictures of its own at all (a lighter
   copy still can), its size taken from the player before the first decode so not even one is risked. */
#define SP_BIG_PX (1920L * 1088L * 11 / 10)
static volatile int spMemLvl;
void sp_mem_level(int lvl) { spMemLvl = lvl; }
static int sp_low_ram(void) {
#ifdef SB_ANDROIDTV
  int mb = plat_mem_total_mb(); return mb > 0 && mb < 2500;
#else
  return 0;
#endif
}
#ifdef SB_PHONE
#define NTEX 11
#else
#define NTEX 15 /* (a TV: four more for the row, so a picture still fading out keeps its texture: 1.8 MB) */
#endif
static unsigned texA[NTEX]; static int texKA[NTEX] = { [0 ... NTEX - 1] = -1 }, texWA[NTEX], texHA[NTEX], shownK = -1; static double texUse[NTEX]; /* 0-2: the picture shown and its neighbours on either side; 3-10: the TV row's pictures (sp_get_cell), the least used one taken */
static void cache_save(Ctx *c);
static void ctx_drop(Ctx *c) { int last; pthread_mutex_lock(&c->mu); last = --c->refs == 0; pthread_mutex_unlock(&c->mu); if (!last) return;
  if (c->m) mkv_release(c->m); for (int i = 0; i < c->nk; i++) free(c->e[i].jpg); for (int i = 0; i < c->nq; i++) free(c->q[i].frame); for (int i = 0; i < RING; i++) free(c->ring[i].rgb);
  free(c->kt); free(c->e); free(c->priv); pthread_mutex_destroy(&c->mu); pthread_cond_destroy(&c->cv); free(c); }

static int key_at_or_before(Ctx *c, double t) { int lo = 0, hi = c->nk - 1, r = 0; while (lo <= hi) { int m = (lo + hi) / 2; if (c->kt[m] <= t) { r = m; lo = m + 1; } else hi = m - 1; } return r; }
static int key_near(Ctx *c, double t) { int k = key_at_or_before(c, t); if (k + 1 < c->nk && fabs(c->kt[k + 1] - t) < fabs(c->kt[k] - t)) k++; return k; }
static int near_play(Ctx *c, double t) { double hi = c->ahead > 0 ? fmin(120, c->ahead + 12) : 12; return t >= c->pos - 10 && t <= c->pos + hi; }
static int in_window(Ctx *c, double t) { if (!c->partial || near_play(c, t)) return 1; return c->tor && c->torGo && !c->strict; } /* (a torrent: what's downloaded around the playhead costs nothing; further only while torGo) */
/* the keyframe to read for a slot at ts: its nearest, else (that one unreadable) a neighbour in the slot; -1 none to do (done, or nothing left) */
static int slot_pick(Ctx *c, double ts) { int k = key_near(c, ts); double h = c->slot * 0.5;
  /* done when a picture is made (or being read) within half a slot of it by time, whichever keyframe it is (a keyframe
     every 2 s: counting the 5 nearest keyframes only, a picture 6 s off left its slot open and the bar asked twice) */
  for (int j = k; j >= 0 && ts - c->kt[j] <= h; j--) if (HELD(c->e[j].st)) return -1;
  for (int j = k + 1; j < c->nk && c->kt[j] - ts <= h; j++) if (HELD(c->e[j].st)) return -1;
  for (int d = 0; d < c->nk; d++) { int any = 0; for (int s = -1; s <= 1; s += 2) { int j = k + d * s; if (j < 0 || j >= c->nk || (d == 0 && s > 0)) continue; if (fabs(c->kt[j] - ts) > c->slot * 0.6) continue; any = 1;
      if (c->e[j].st == 0 && in_window(c, c->kt[j])) return j; } if (!any) break; } /* (the nearest readable one in the slot) */
  return -1; }
/* a picture for ts with its own reach: done when one is made (or being read) within half a step of it (a TV: the remote's
   step, so every press finds a picture of its own), else the nearest readable keyframe within that reach */
static int fine_pick(Ctx *c, double ts, double h) { if (ts < 0 || ts > c->dur) return -1; int k = key_near(c, ts);
  for (int j = k; j >= 0 && ts - c->kt[j] <= h; j--) if (HELD(c->e[j].st)) return -1;
  for (int j = k + 1; j < c->nk && c->kt[j] - ts <= h; j++) if (HELD(c->e[j].st)) return -1;
  for (int d = 0; d < c->nk; d++) { int any = 0; for (int s = -1; s <= 1; s += 2) { int j = k + d * s; if (j < 0 || j >= c->nk || (d == 0 && s > 0)) continue; if (fabs(c->kt[j] - ts) > h) continue; any = 1;
      if (c->e[j].st == 0 && in_window(c, c->kt[j])) return j; } if (!any) break; }
  return -1; }
static double step_s(void) { int s = atoi(store_get("seekStep", "10")); return s > 0 ? s : 10; }
static double spread_t(Ctx *c, int j) { double t = c->dur * (j + 0.5) / SPREAD; return c->slot > 0 ? floor(t / c->slot + 0.5) * c->slot : t; } /* (the first pass on the grid: its pictures are the grid's too) */
static int sp_order(int i) { static int ord[SPREAD], made; if (!made) { int n = 0; char used[SPREAD] = { 0 }; for (int step = SPREAD; step >= 1 && n < SPREAD; step /= 2) for (int j = step / 2; j < SPREAD && n < SPREAD; j += step) if (!used[j]) { used[j] = 1; ord[n++] = j; } for (int j = 0; j < SPREAD && n < SPREAD; j++) if (!used[j]) { used[j] = 1; ord[n++] = j; } made = 1; } return ord[i]; }
/* what to read next: the finger (or the paused place), around the playhead, the spread, the grid; *focus: for the finger */
static int pick(Ctx *c, int *focus) {
  double f = c->focusOn ? c->focusT : c->paused ? c->pos : -1; *focus = f >= 0;
  if (f >= 0) {
    if (c->focusOn && c->dense) { int k0 = key_near(c, f); for (int d = 0; d < c->nk; d++) { int any = 0; for (int s = -1; s <= 1; s += 2) { int j = k0 + d * s; if (j < 0 || j >= c->nk || (d == 0 && s > 0)) continue; if (fabs(c->kt[j] - f) > DENSE) continue; any = 1;
            if (c->e[j].st == 0 && in_window(c, c->kt[j])) { c->ph = 1; return j; } } if (!any) break; } }
#ifndef SB_PHONE
    if (c->focusOn) { double st = step_s(); int dir = c->dir ? c->dir : 1; for (int d = 0; d <= 6; d++) for (int s2 = 0; s2 < 2; s2++) { double ts = f + (s2 ? -d : d) * dir * st; if (s2 && d > 1) continue; int k = fine_pick(c, ts, st * 0.5); if (k >= 0) { c->ph = 2; return k; } } } /* (a TV: a picture for every press: where it is, the next six presses the way it moves, one back) */
#endif
    if (c->focusOn && c->dir) for (int d = 0; d <= 2; d++) { double ts = f + d * c->dir * c->slot; if (ts < 0 || ts > c->dur) break; int k = slot_pick(c, ts); if (k >= 0) { c->ph = 3; return k; } } /* (where the finger is, then the next two the way it moves) */
    for (int d = 0; d <= (c->focusOn ? 3 : 1); d++) for (int s = -1; s <= 1; s += 2) { if (d == 0 && s > 0) continue; double ts = f + d * s * c->slot; /* (held or paused: just around it, the film coarse comes next) */ if (ts < 0 || ts > c->dur) continue; int k = slot_pick(c, ts); if (k >= 0) { c->ph = 3; return k; } } }
  *focus = 0; if (c->tier >= 2) return -1;
  if (c->tor) { /* a torrent: the downloaded stretch first (free), then the next 20 minutes (played anyway), then the film coarse, 24 -> 48 -> 96; never the fine grid far off */
    double a = fmax(0, c->pos - 10), b = fmin(c->dur, c->pos + (c->ahead > 0 ? fmin(120, c->ahead + 12) : 12)); int n = (int)((b - a) / c->slot) + 1, top = 1; while (top < n) top <<= 1;
    c->strict = 1; for (int st = top; st >= 1; st >>= 1) for (int i = 0; i < n; i += st) { int k = slot_pick(c, a + i * c->slot); if (k >= 0) { c->strict = 0; return k; } } c->strict = 0;
    if (!c->torGo) return -1;
    for (int i = 0; i < SPREAD; i++) { int j = sp_order(i); int k = slot_pick(c, spread_t(c, j)); if (k >= 0) return k; } /* (the film coarse first, 24 -> 48 -> 96: a scrub lands anywhere) */
    { double a2 = b, b2 = fmin(c->dur, c->pos + 1200); double sl = c->slot * 3; int n2 = (int)((b2 - a2) / sl) + 1; for (int i = 0; i < n2; i++) { int k = slot_pick(c, a2 + i * sl); if (k >= 0) return k; } }
    return -1; }
#ifndef SB_PHONE
  { double st = step_s(); for (int i = 0; i <= 3; i++) for (int s2 = 0; s2 < 2; s2++) { if (s2 && i > 1) continue; int k = fine_pick(c, c->pos + (s2 ? -i : i) * st, st * 0.5); if (k >= 0) { c->ph = 4; return k; } } } /* (the first presses from where it plays: three ahead, one back) */ /* (a TV: the first presses from where it plays, every step 2 minutes ahead and one back, before the film coarse) */
  for (int i = 0; i < 48; i++) { int j = sp_order(i); int k = slot_pick(c, spread_t(c, j)); if (k >= 0) { c->ph = 5; return k; } }
  { double st = step_s(); for (int i = 4; i <= 12; i++) for (int s2 = 0; s2 < 2; s2++) { if (s2 && i > 6) continue; int k = fine_pick(c, c->pos + (s2 ? -i : i) * st, st * 0.5); if (k >= 0) { c->ph = 4; return k; } } } /* (then the rest of the presses near it: 2 minutes ahead, one back) */ /* (then the film coarse, 48) */
#endif
  for (int i = 0; i < SPREAD; i++) { int j = sp_order(i); int k = slot_pick(c, spread_t(c, j)); if (k >= 0) { c->ph = 6; return k; } } /* (the whole film first, 96 evenly: a seek anywhere, the end too, lands near one; then where it plays) */
  { double a = fmax(0, c->pos - 600), b = fmin(c->dur, c->pos + 1200); int n = (int)((b - a) / c->slot) + 1, top = 1; while (top < n) top <<= 1; /* (10 min before to 20 min after where it plays, coarse to fine: where a seek most likely goes) */
    for (int st = top; st >= 1; st >>= 1) for (int i = 0; i < n; i += st) { int k = slot_pick(c, a + i * c->slot); if (k >= 0) { c->ph = 7; return k; } } }
  for (int i = 0; i < SPREAD; i++) { int j = sp_order(i); int k = slot_pick(c, spread_t(c, j)); if (k >= 0) return k; } /* (coarse to fine: the whole film covered within seconds, then the gaps halved) */
  if (c->tier >= 1) return -1;
  int n = (int)(c->dur / c->slot) + 1, top = 1; while (top < n) top <<= 1; for (int st = top; st >= 1; st >>= 1) for (int i = 0; i < n; i += st) { int k = slot_pick(c, i * c->slot); if (k >= 0) { c->ph = 9; return k; } } /* (the rest of the grid coarse to fine over the whole film: every part sharper at once, never one end first) */
  return -1; }
/* may the background fill read now: never while it buffers or just after a seek; playing, only while the buffer is
   well ahead (60 % of the most seen) and not falling, at the pace the decodes cost (about a fifth of a small core) */
static int may_fill(Ctx *c, double now) { /* the film first, always: no fill while it buffers or starts, just after a seek, or unless its buffer is well filled and holding; the buffer falling stops it a while; paused, at once */
  if (sp_metered && sp_metered()) return 0; if (now < c->backUntil) return 0;
  if (c->paused) return 1;
  if (c->buffering || now - c->seekAt < 3) { c->steadySince = 0; return 0; }
  if (c->tight && !c->tor) return 0; /* (a tight link: nothing beside the film while it plays) */
#ifdef SB_PHONE
  if (now < c->fillUntil || now - c->playSince < (c->roomy ? 2 : 10)) return 0;
#else
  if (now < c->fillUntil || now - c->playSince < 2) return 0; /* (a TV: 2 s of play after a seek, then the buffer decides) */
#endif /* (playing without a stall first: 2 s on a roomy link, 10 s otherwise; after a stall, a while longer) */
  if (c->ahead < 0) { Ctx *p = c->prep == 1 ? cur : NULL; return !(p && (p->buffering || (p->ahead >= 0 && p->ahead < 18))); } /* (nothing plays it: a download, or the next episode, made only while the one playing has a good buffer) */
  if (c->ahead < c->prevAhead - 2) { c->steadySince = 0; c->backUntil = now + 10; return 0; } /* (the buffer draining: the readers step back) */
#ifdef SB_PHONE
  double need = c->tight || c->heavy ? 27 : 18; /* (seconds buffered: the player keeps at most 30) */
#else
  double need = c->tight || c->heavy ? 14 : 8; /* (a TV: 8 s buffered and holding; the draining check above steps the readers back at once) */
#endif
  if (c->ahead >= need) { if (!c->steadySince) c->steadySince = now; } else { c->steadySince = 0; return 0; }
  return now - c->steadySince >= (c->got < 24 ? 1 : 3); }

static void size_seen(Ctx *c, long fl) { c->fsz[c->nfsz++ % 64] = fl; if (fl > c->winMax) c->winMax = fl; }
static long win_fit(Ctx *c) { int n = c->nfsz < 64 ? c->nfsz : 64; if (n < 4) return c->winMax > 0 ? (long)(c->winMax * 1.25) + 65536 : 0; long a[64]; memcpy(a, c->fsz, n * sizeof *a);
  for (int i = 1; i < n; i++) { long v = a[i]; int j = i; while (j > 0 && a[j - 1] > v) { a[j] = a[j - 1]; j--; } a[j] = v; } return (long)(a[n * 85 / 100] * 1.15) + 32768; } /* (a bigger one is read on by a second request) */
static void enqueue(Ctx *c, int k, unsigned char *fr, long fl, int focus) { if (c->nq < 32) { int at = c->nq; if (focus) { at = 0; while (at < c->nq && c->q[at].focus) at++; memmove(c->q + at + 1, c->q + at, sizeof(Job) * (c->nq - at)); } /* (the finger's pictures first, ahead of the fill) */
    c->q[at].k = k; c->q[at].frame = fr; c->q[at].len = fl; c->q[at].focus = focus; c->nq++; c->qBytes += fl; pthread_cond_broadcast(&c->cv); } else { free(fr); c->e[k].st = 0; } }
static void went_ok(Ctx *c) { c->failRow = 0; c->backS = 0; int top = c->tight ? 1 : c->heavy ? 2 : 4; /* (a heavy film or a tight link: fewer readers beside the film) */ /* (four at most, paused too: a debrid account counts every connection, playback's with them; TorBox allows 8 in all) */ if (++c->clean >= 2 && c->lanes < top) { c->clean = 0; c->lanes++; } } /* (one more reader per 2 clean reads) */
static void went_busy(Ctx *c, const char *why) { c->lanes = 1; c->clean = 0; c->backS = c->backS > 0 ? fmin(30, c->backS * 2) : 2; c->backUntil = mono() + c->backS; c->failRow++;
  fprintf(stderr, "[seekprev] %s: one reader, waiting %.0f s\n", why, c->backS); if (c->failRow >= 5) { c->stop = 1; fprintf(stderr, "[seekprev] 5 refusals in a row: none for this film\n"); } }
static void ensure_net(void) { if (!fnet) { pthread_mutex_lock(&poolMu); if (npool) fnet = pool[--npool]; pthread_mutex_unlock(&poolMu); } if (!fnet) fnet = net_range_new(); }
#define BATCH 20
static void *fetch_run(void *arg) { Ctx *c = arg; nice_to(10);
  pthread_mutex_lock(&c->mu); int me = c->nFetch++; if (me == 0) fprintf(stderr, "[seekprev] reading (%s)\n", c->partial ? "near the playhead" : "the whole film");
  while (!c->stop) {
    if (c->prep == 1 && c->got >= 48) { cache_save(c); c->stop = 1; fprintf(stderr, "[seekprev] the next episode's 48 pictures made and kept\n"); break; }
    int room = c->nq < 24 && c->qBytes < (sp_low_ram() ? 8L << 20 : 16L << 20); /* (by count and by bytes: 24 keyframes of a 4K remux are 25-70 MB) */
    double now = mono(); int focus = 0, k = room ? pick(c, &focus) : -1; if (room) c->exhausted = k < 0 && c->tier == 0 && !c->partial;
    if (!focus && c->capBytes > 0 && c->bytes >= c->capBytes) { static int said; if (!said) { said = 1; fprintf(stderr, "[seekprev] %ld MB read: no more background fill for this film (the finger still gets its pictures)\n", c->bytes >> 20); } k = -1; }
    int rcap = c->paused ? 4 : c->tor ? 3 : c->roomy ? 3 : 1; /* (playing: few readers beside the film; paused: more) */
    int ok = k >= 0 && me < c->lanes && me < rcap && now >= c->backUntil && (focus || may_fill(c, now)) && !(c->focusOn && !focus && me > 0) && !(c->got < 48 && (c->buffering || c->paused) && me > 0); /* (the film still loading: one reader only) */ /* (the finger down: only one reader goes on filling) */
    if (!ok) { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); ts.tv_nsec += 200000000; if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; } pthread_cond_timedwait(&c->cv, &c->mu, &ts); continue; }
    long cap = sp_metered && sp_metered() ? METER_CAP : WIFI_CAP;
    /* a batch: this one and the next ones the plan wants, all in one request, when the server answers that way */
    long long st0; long sl0; int fb = c->focusOn && focus && c->multi == 1; /* (the finger's spot and its neighbours in one request, where the server answers that way) */
#ifndef SB_PHONE
    fb = 0; /* (a TV: the finger's keyframe alone, at once, on each free reader: a batch of 8 for it took 4-6 s) */
#endif
    if ((fb || !c->focusOn) && c->multi != 0 && mkv_key_where(c->m, k, &st0, &sl0)) {
      int ks[BATCH], nb = 0, lim = c->got == 0 ? 2 : c->multi < 0 ? 4 : c->tight || c->heavy ? 8 : BATCH; /* (the first two alone: a first picture in seconds, even from a slow server) */ /* (smaller batches beside a heavy film: each ends sooner) */ long long sts[BATCH]; long lns[BATCH]; char npf[BATCH]; long win = win_fit(c); if (win <= 0) win = 196608;
#ifndef SB_PHONE
      if (c->nfsz < 4 && c->winMax <= 0) win = 448 * 1024; /* (a TV: the first window as big as a 1080p keyframe, or every one needs a second read) */
#endif
      if (win < 131072) win = 131072; if (win > 2L << 20) win = 2L << 20;
      if (fb) lim = 8;
      long tot = 0; /* (a batch of about 4 MB at most, so it ends in seconds and a seek never waits on one) */
#ifndef SB_PHONE
      if (lim > 8) lim = 8; /* (a TV: 8 at most, so pictures keep coming every few seconds, never one long wait) */
#endif
      MkvAim am[BATCH]; char aim[BATCH]; int wt = 0, nAim = 0; /* (aim: a keyframe with no place in its Cluster, read where it most likely lies: small reads, so two of them count as one) */
      while (nb < BATCH && wt < lim * 2) { long long st; long sl; if (nb && (k = pick(c, &focus)) < 0) break; if ((fb ? !focus : c->focusOn) || !mkv_key_where(c->m, k, &st, &sl)) break; c->e[k].st = 1; ks[nb] = k; sts[nb] = st; npf[nb] = sl < 0;
        aim[nb] = sl < 0 && mkv_aim_start(c->m, k, &am[nb]); wt += aim[nb] && (c->paused || c->ahead < 0) ? 1 : 2; /* (twice as many only while nothing plays beside them) */ if (aim[nb]) { sts[nb] = am[nb].at; lns[nb] = am[nb].len; nAim++; nb++; tot += 256 * 1024; if (tot >= (4L << 20)) break; continue; }
        { int fit = c->nfsz >= 4 && (c->npl < 8 || c->npl2 * 10 < c->npl * 3); /* (no place known: the window most keyframes need once seen, the rest read on; 1 MB while unknown, or when the first window misses 30 %) */
          lns[nb] = sl < 0 ? (fit ? win + 65536 : (win > (1L << 20) ? win : (1L << 20))) : win + sl; } nb++;
#ifndef SB_PHONE
        tot += lns[nb - 1]; if (tot >= (4L << 20)) break;
#else
        (void)tot;
#endif
      } /* (a keyframe with no place in its Cluster: 1 MB of it, most such keyframes lie within) */
      if (nb < 2) { if (nb) { k = ks[0]; c->e[k].st = 0; } goto single; } /* (one alone, aimed or not: mkv_key_read does the same on its own) */ /* (only one to ask: the plain way) */
      pthread_mutex_unlock(&c->mu); ensure_net(); unsigned char *parts[BATCH]; long plen[BATCH]; int hs = 0; double r0 = mono();
      int e = mkv_dead(c->m) ? NR_NET : net_range_multi(fnet, mkv_url(c->m), sts, lns, nb, parts, plen, &hs, 15000);
      int got = 0; long by = 0; for (int i = 0; i < nb; i++) if (parts[i]) by += plen[i];
      unsigned char *fr[BATCH]; long fl[BATCH]; int fr_r[BATCH];
      for (int i = 0; i < nb; i++) { fr[i] = NULL; fl[i] = 0; fr_r[i] = -1; if (!parts[i] || aim[i]) continue; long b2 = 0; int rq = 0; fr_r[i] = mkv_key_from_part(c->m, fnet, ks[i], parts[i], plen[i], cap, &fr[i], &fl[i], &b2, &rq); by += b2; free(parts[i]); if (!fr_r[i]) got++; if (npf[i] && !fr_r[i]) { pthread_mutex_lock(&c->mu); c->npl++; if (b2 > 0) c->npl2++; pthread_mutex_unlock(&c->mu); } }
      int aimParts = 0, aimOld = 0, aimRounds = 0; long aimBy = 0;
      if (nAim) { char pend[BATCH]; /* the aimed ones: each round feeds what came and asks for what each wants next, all in one request */
        for (int i = 0; i < nb; i++) { pend[i] = 0; if (!aim[i]) continue; if (parts[i]) { aimParts++; aimBy += plen[i]; fr_r[i] = mkv_aim_feed(c->m, &am[i], parts[i], plen[i], cap, &fr[i], &fl[i]); free(parts[i]); parts[i] = NULL; pend[i] = fr_r[i] == 1; } }
        for (int round = 0; round < 4; round++) { int ix[BATCH], m = 0; long long s2[BATCH]; long l2[BATCH]; unsigned char *p2[BATCH]; long pl2[BATCH]; for (int i = 0; i < nb; i++) if (pend[i]) { ix[m] = i; s2[m] = am[i].at; l2[m] = am[i].len; m++; } if (!m) break; aimRounds++;
          int e2; if (m == 1) { p2[0] = NULL; pl2[0] = 0; e2 = mkv_dead(c->m) ? NR_NET : mkv_range(c->m, fnet, s2[0], l2[0], &p2[0], &pl2[0]); } else { int h2 = 0; if (mkv_dead(c->m)) { e2 = NR_NET; for (int q = 0; q < m; q++) { p2[q] = NULL; pl2[q] = 0; } } else e2 = net_range_multi(fnet, mkv_url(c->m), s2, l2, m, p2, pl2, &h2, 15000);
            if (e2 == NR_NORANGE) { for (int q = 0; q < m; q++) { p2[q] = NULL; pl2[q] = 0; } for (int q = 0; q < m; q++) { e2 = mkv_range(c->m, fnet, s2[q], l2[q], &p2[q], &pl2[q]); if (e2 != NR_OK) break; } } } /* (ranges the server joined into one plain answer: each on its own) */
          for (int q = 0; q < m; q++) { int i = ix[q]; pend[i] = 0; if (!p2[q]) { fr_r[i] = -1; continue; } by += pl2[q]; aimBy += pl2[q]; fr_r[i] = mkv_aim_feed(c->m, &am[i], p2[q], pl2[q], cap, &fr[i], &fl[i]); free(p2[q]); pend[i] = fr_r[i] == 1; }
          if (e2 != NR_OK) { if (e == NR_OK) e = e2 == NR_NORANGE ? NR_NET : e2; for (int i = 0; i < nb; i++) pend[i] = 0; break; } }
        for (int i = 0; i < nb; i++) { if (!aim[i]) continue; if (pend[i]) fr_r[i] = -1; mkv_aim_end(&am[i]);
          if (fr_r[i] == -5) { long b2 = 0; int rq = 0; aimOld++; fr_r[i] = mkv_dead(c->m) ? -1 : mkv_key_read(c->m, fnet, ks[i], 1L << 20, cap, &fr[i], &fl[i], NULL, &b2, &rq); by += b2; } /* (not found where expected: its Cluster walked, as before) */
          if (!fr_r[i]) got++; } }
      pthread_mutex_lock(&c->mu); c->bytes += by;
      if (c->multi < 0 && nb >= 2 && (e == NR_OK || e == NR_NORANGE)) { /* (one range alone tells nothing: a server answers it plainly either way) */ c->multi = e == NR_OK && (got > 0 || aimParts > 0); fprintf(stderr, "[seekprev] this server %s many keyframes in one request\n", c->multi ? "answers" : "doesn't answer"); }
      if (getenv("SB_SPDBG")) { long fs = 0, ws = 0; int np = 0; for (int i = 0; i < nb; i++) { fs += fl[i]; ws += lns[i]; np += npf[i]; } fprintf(stderr, "[seekprev] batch of %d: %d pictures, %ld KB in %.0f ms (%d, HTTP %d); asked %ld KB, frames %ld KB, %d with no place, window %ld KB, misses %d/%d\n", nb, got, by / 1024, (mono() - r0) * 1000, e, hs, ws / 1024, fs / 1024, np, win / 1024, c->npl2, c->npl); }
      if (nAim && getenv("SB_SPDBG")) fprintf(stderr, "[seekprev] of them %d read where they should lie: %ld KB in %d more requests, %d walked the old way\n", nAim, aimBy / 1024, aimRounds, aimOld);
      for (int i = 0; i < nb; i++) { if (!fr_r[i]) { size_seen(c, fl[i]); enqueue(c, ks[i], fr[i], fl[i], fb); } else c->e[ks[i]].st = fr_r[i] == -3 || fr_r[i] == -4 ? 3 : 0; }
      if (e == NR_BUSY) went_busy(c, "the server asks to slow down"); else if (e == NR_OK && got) went_ok(c); else if (e != NR_NORANGE) went_busy(c, "a read failed");
      if (e == NR_NORANGE && c->multi == 1) { if (++c->plain >= 2) { c->multi = 0; fprintf(stderr, "[seekprev] this server stopped answering many keyframes in one request: one at a time\n"); } } else if (e == NR_OK) c->plain = 0; /* (never the same batch asked again and again) */
      continue; }
    /* one keyframe: the finger's, a server that takes one range at a time, or one whose place in its Cluster is still to find */
  single:
    c->e[k].st = 1; pthread_mutex_unlock(&c->mu); ensure_net();
    unsigned char *fr = NULL; long fl = 0, by = 0; double kt = 0, rt0 = mono(); int r = mkv_dead(c->m) ? -1 : sp_fetch_key(c->m, k, cap, &fr, &fl, &kt, &by);
    if (getenv("SB_SPDBG")) fprintf(stderr, "[seekprev] read #%d k%d (%s): %ld KB in %.0f ms, frame %ld KB, r %d\n", me, k, focus ? "finger" : "fill", by / 1024, (mono() - rt0) * 1000, fl / 1024, r);
    pthread_mutex_lock(&c->mu); c->bytes += by;
    if (r && c->fails < 5) fprintf(stderr, "[seekprev] keyframe %d at %.0f s not read (%d, %ld B)\n", k, c->kt[k], r, by);
    if (r == -1) { c->e[k].st = 0; went_busy(c, "a read failed"); continue; }
    if (r || c->stop) { free(fr); c->e[k].st = 3; c->fails++; if (r == -2 || (c->got == 0 && c->fails >= 3)) { c->stop = 1; fprintf(stderr, "[seekprev] its keyframes can't be read: none for this film\n"); } continue; }
    size_seen(c, fl); went_ok(c); if (c->tor && !near_play(c, c->kt[k])) c->torOut++; enqueue(c, k, fr, fl, focus);
  }
  pthread_mutex_unlock(&c->mu); if (fnet) { net_range_free(fnet); fnet = NULL; } ctx_drop(c); return NULL; }

/* the pictures of one film never take more memory than the device can spare (set from its RAM in ctx_make, the
   smallest TVs first). Over it, the ones far from where it plays are thinned: the picture closest to its neighbours
   goes (so what stays is spread evenly, every other one first), still counted as made, never read again; a scrub
   there shows the neighbour. Within 10 minutes of the playhead or the finger nothing goes. (the lock held) */
static void mem_thin(Ctx *c) { if (c->jpgMax <= 0 || c->jpgBytes <= c->jpgMax) return; int let = 0; long was = c->jpgBytes;
  for (int pass = 0; pass < 2 && c->jpgBytes > c->jpgMax / 10 * 9; pass++) { double keep = pass ? 60 : 600;
    while (c->jpgBytes > c->jpgMax / 10 * 9) { int best = -1, prev = -1, cand = -1; double bestGap = 1e18, prevT = 0, candPrevT = 0;
      for (int i = 0; i < c->nk; i++) { if (c->e[i].st != 2 || !c->e[i].jpg) continue;
        if (cand >= 0) { double gap = c->kt[i] - candPrevT; if (gap < bestGap) { bestGap = gap; best = cand; } cand = -1; } /* (cand: a picture with one made before it; i is the one after) */
        if (prev >= 0 && fabs(c->kt[i] - c->pos) > keep && !(c->focusOn && fabs(c->kt[i] - c->focusT) <= keep) && i != shownK) { cand = i; candPrevT = prevT; }
        prev = i; prevT = c->kt[i]; }
      if (best < 0) break; c->jpgBytes -= c->e[best].len; free(c->e[best].jpg); c->e[best].jpg = NULL; c->e[best].len = 0; c->e[best].st = 4; let++; } }
  if (let) fprintf(stderr, "[seekprev] memory: %d pictures far from the playhead let go (%ld of %ld KB kept, at most %ld)\n", let, c->jpgBytes / 1024, was / 1024, c->jpgMax / 1024); }
static void jw(void *ctx, void *data, int n) { struct { unsigned char *p; int len, cap; } *b = ctx; if (b->len + n > b->cap) { int nc = (b->len + n) * 2; unsigned char *q = realloc(b->p, nc); if (!q) return; b->p = q; b->cap = nc; } memcpy(b->p + b->len, data, n); b->len += n; }
static void ring_put(Ctx *c, int k, unsigned char *rgb) { int o = 0; for (int i = 0; i < RING; i++) { if (c->ring[i].k == k && c->ring[i].rgb) { free(rgb); c->ring[i].used = mono(); return; } if (!c->ring[i].rgb || c->ring[i].used < c->ring[o].used) o = i; if (!c->ring[o].rgb) break; }
  free(c->ring[o].rgb); c->ring[o].rgb = rgb; c->ring[o].k = k; c->ring[o].used = mono(); }

static int cache_dir(char *out, size_t cap);
static void *decode_run(void *arg) { Ctx *c = arg; void *dec = NULL; int lowered = 1; nice_to(10);
  pthread_mutex_lock(&c->mu); int me = c->nDecT++;
  while (!c->stop) {
    { /* memory first (see SP_BIG_PX): the size from the player while none is made yet (the film playing; the next
         episode's like it; never for a lighter copy, which is picked light) */
      if (!c->srcPx && c->prep != 3) { long vw = video_width(), vh = video_height(); if (vw > 0 && vh > 0) c->srcPx = vw * vh; }
      int big = c->srcPx > SP_BIG_PX, unknownHeavy = !c->srcPx && c->heavy && c->prep != 3;
      if (sp_low_ram() && big) { if (!c->bigSaid) { c->bigSaid = 1; fprintf(stderr, "[seekprev] a film above 1080p on a box with %d MB: no pictures of its own (memory)\n", plat_mem_total_mb()); } c->stop = 1; pthread_cond_broadcast(&c->cv); break; }
      int hold = spMemLvl >= 2 || (big && me > 0) || (sp_low_ram() && unknownHeavy && mono() - c->t0 < 20);
      if (sp_low_ram() && unknownHeavy && mono() - c->t0 >= 20) { if (!c->bigSaid) { c->bigSaid = 1; fprintf(stderr, "[seekprev] a heavy film whose size the player never told, on a box with %d MB: no pictures of its own (memory)\n", plat_mem_total_mb()); } c->stop = 1; pthread_cond_broadcast(&c->cv); break; }
      if (hold) { if (dec) { pthread_mutex_unlock(&c->mu); api->close(dec); dec = NULL; pthread_mutex_lock(&c->mu); }
        if (spMemLvl >= 2 && !c->memSaid && me == 0) { c->memSaid = 1; fprintf(stderr, "[seekprev] the TV is nearly out of memory: no pictures made until it eases\n"); } else if (spMemLvl < 2) c->memSaid = 0;
        struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); ts.tv_sec += 1; pthread_cond_timedwait(&c->cv, &c->mu, &ts); continue; } }
    if (me > 0 && (!c->hSet || (!(c->paused || c->ahead < 0) && c->nq < 6))) { if (dec) { pthread_mutex_unlock(&c->mu); api->close(dec); dec = NULL; pthread_mutex_lock(&c->mu); } struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); ts.tv_sec += 1; pthread_cond_timedwait(&c->cv, &c->mu, &ts); continue; } /* (the second decoder: only while nothing plays, the film is held or paused, or a queue builds) */
    if (!c->nq && !c->nWant) { if (dec) { pthread_mutex_unlock(&c->mu); api->close(dec); dec = NULL; pthread_mutex_lock(&c->mu); continue; } /* (nothing waiting: the decoder's memory let go at once) */
      pthread_cond_wait(&c->cv, &c->mu); continue; }
    if (c->nWant) { int k = c->wantRgb[--c->nWant]; if (k < 0 || k >= c->nk || c->e[k].st != 2) continue; int have = 0; for (int i = 0; i < RING; i++) if (c->ring[i].rgb && c->ring[i].k == k) have = 1; if (have) continue;
      int len = c->e[k].len; unsigned char *cp = malloc(len); if (!cp) continue; memcpy(cp, c->e[k].jpg, len); pthread_mutex_unlock(&c->mu);
      int x, y, n; unsigned char *px = stbi_load_from_memory(cp, len, &x, &y, &n, 3); free(cp); unsigned char *rgb = NULL; if (px) { rgb = malloc((size_t)x * y * 3); if (rgb) memcpy(rgb, px, (size_t)x * y * 3); stbi_image_free(px); }
      pthread_mutex_lock(&c->mu); if (rgb && x == c->W && y == c->H) ring_put(c, k, rgb); else free(rgb); continue; }
    Job j = c->q[0]; memmove(c->q, c->q + 1, sizeof(Job) * (--c->nq)); c->qBytes -= j.len; pthread_cond_broadcast(&c->cv); int W = c->W, H = c->H, hSet = c->hSet; pthread_mutex_unlock(&c->mu);
    if (j.focus == lowered) { nice_to(j.focus ? 0 : 10); lowered = !j.focus; } /* (a picture for the finger: at once, on any core) */
    if (getenv("SB_SPDUMP")) { static int nd; char d[1100]; if (nd < 40 && cache_dir(d, sizeof d)) { char f[1200]; snprintf(f, sizeof f, "%s/dump_k%05d.bin", d, j.k); FILE *o = fopen(f, "wb"); if (o) { fwrite(j.frame, 1, j.len, o); fclose(o); nd++; }
        if (nd == 1) { snprintf(f, sizeof f, "%s/dump_priv.bin", d); o = fopen(f, "wb"); if (o) { fwrite(c->priv, 1, c->privLen, o); fclose(o); } } } } /* (development: the frames as decoded, to check them elsewhere) */
    if (!dec) dec = api->open(c->codec, c->priv, (int)c->privLen);
    unsigned char *rgb = dec ? malloc((size_t)W * H * 3) : NULL; int sw = 0, sh = 0, dark = 0; float dar = 0; double c0 = cpu_ms();
    int r = rgb ? api->frame(dec, j.frame, (int)j.len, W, H, rgb, &sw, &sh, &dar, &dark) : -1;
    if (!r && !hSet && dar > 0.5f && dar < 3.5f) { int h2 = ((int)(W / dar + 0.5f)) & ~1; if (h2 >= 16 && abs(h2 - H) > 2) { H = h2; free(rgb); rgb = malloc((size_t)W * H * 3); r = rgb ? api->frame(dec, j.frame, (int)j.len, W, H, rgb, &sw, &sh, &dar, &dark) : -1; } } /* (its own shape, from the first picture: never a stretched one) */
    double ms = cpu_ms() - c0; free(j.frame);
    unsigned char *jpg = NULL; int jl = 0; if (!r) { struct { unsigned char *p; int len, cap; } b = { NULL, 0, 0 }; stbi_write_jpg_to_func(jw, &b, W, H, 3, rgb, 72); jpg = b.p; jl = b.len; }
    pthread_mutex_lock(&c->mu);
    if (!r && !c->hSet) { c->hSet = 1; c->H = H; }
    if (!r && sw > 0 && sh > 0) c->srcPx = (long)sw * sh;
    if (r || !jpg || H != c->H) { free(jpg); free(rgb); c->e[j.k].st = 3; c->fails++; continue; }
    c->e[j.k].jpg = jpg; c->e[j.k].len = jl; c->e[j.k].st = 2; c->got++; ring_put(c, j.k, rgb); c->jpgBytes += jl; mem_thin(c);
    c->meanMs = c->nDec < 3 ? (c->meanMs * c->nDec + ms) / (c->nDec + 1) : c->meanMs * 0.8 + ms * 0.2; c->nDec++;
    if (c->nDec >= 3) { int t = c->meanMs > 1500 ? 2 : c->meanMs > 600 ? 1 : 0; if (t != c->tier) { c->tier = t; fprintf(stderr, "[seekprev] %.0f ms a picture: %s\n", c->meanMs, t == 2 ? "only where the finger is" : t == 1 ? "the spread and the finger" : "the whole film"); } }
    if (c->got <= 3 || c->got % 25 == 0) fprintf(stderr, "[seekprev] %d pictures (%d none), %ld KB read, %.0f ms CPU each (last %.0f, %dx%d from %dx%d)\n", c->got, c->fails, c->bytes / 1024, c->meanMs, ms, W, H, sw, sh);
  }
  pthread_mutex_unlock(&c->mu); if (dec) api->close(dec); ctx_drop(c); return NULL; }

/* ---- the pictures kept on this device: a film opened again has its whole bar at once ----
   One file per film in the app's own storage, named by its title, episode, file size and keyframe count (never the
   link): the pictures as made (JPEG), with their keyframes. Written on pause and as the film closes, through a temporary
   file and a rename. All of them together at most 5 % of the free space and 200 MB, the least recently used going
   first; none at all with under 200 MB free. */
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <utime.h>
#define CACHE_MAX (200LL << 20)
#ifdef SB_PHONE
#define CACHE_FREE_MIN (200LL << 20)
#define CACHE_SHARE 20
#else
#define CACHE_FREE_MIN (40LL << 20)   /* (a TV with little free space keeps at least the last films: a few MB each) */
#define CACHE_SHARE 10
#endif
static int cache_dir(char *out, size_t cap) { const char *d = plat_data_dir(); if (!d || !*d) return 0; snprintf(out, cap, "%s/prev", d); mkdir(out, 0700); return 1; }
static void cache_name(Ctx *c, char *out, size_t cap) { char d[1100]; if (!cache_dir(d, sizeof d)) { out[0] = 0; return; } char k[256]; snprintf(k, sizeof k, "%s", c->key); { char *q = strchr(k, '|'); if (q) q = strchr(q + 1, '|'); if (q) *q = 0; } /* (the title and episode; the file itself told apart by its keyframe count) */
  unsigned h = 2166136261u; for (const char *p = k; *p; p++) h = (h ^ (unsigned char)*p) * 16777619u; h = (h ^ (unsigned)c->nk) * 16777619u; snprintf(out, cap, "%s/%08x-%d.sp", d, h, c->nk); }
static long long cache_free(const char *d) { struct statvfs v; if (statvfs(d, &v)) return -1; return (long long)v.f_bavail * (long long)v.f_frsize; }
static void cache_trim(const char *d) { long long fr = cache_free(d), cap = fr > 0 ? fr / CACHE_SHARE : 0; if (cap > CACHE_MAX) cap = CACHE_MAX; /* (the least recently used go until all fit) */
  for (int round = 0; round < 64; round++) { DIR *dd = opendir(d); if (!dd) return; struct dirent *e; long long tot = 0; time_t old = 0; char oldN[512] = "";
    while ((e = readdir(dd))) { if (!strstr(e->d_name, ".sp")) continue; char f[1700]; snprintf(f, sizeof f, "%s/%s", d, e->d_name); struct stat st; if (stat(f, &st)) continue; tot += st.st_size; if (!oldN[0] || st.st_mtime < old) { old = st.st_mtime; snprintf(oldN, sizeof oldN, "%s", e->d_name); } }
    closedir(dd); if (tot <= cap || !oldN[0]) return; char f[1700]; snprintf(f, sizeof f, "%s/%s", d, oldN); unlink(f); } }
static void cache_load(Ctx *c) { char fn[1200]; cache_name(c, fn, sizeof fn); if (!fn[0]) return; FILE *f = fopen(fn, "rb"); if (!f) return; char mg[6] = ""; int nk = 0, W = 0, H = 0, n = 0, got = 0;
  if (fread(mg, 1, 5, f) != 5 || memcmp(mg, "SBPV1", 5) || fread(&nk, 4, 1, f) != 1 || fread(&W, 4, 1, f) != 1 || fread(&H, 4, 1, f) != 1 || fread(&n, 4, 1, f) != 1 || nk != c->nk || W != c->W || n < 0 || n > nk) { fclose(f); return; }
  for (int i = 0; i < n; i++) { int k = -1, len = 0; if (fread(&k, 4, 1, f) != 1 || fread(&len, 4, 1, f) != 1 || k < 0 || k >= nk || len <= 0 || len > (1 << 20)) break; unsigned char *j = malloc(len); if (!j || fread(j, 1, len, f) != (size_t)len) { free(j); break; }
    if (c->e[k].st == 2) { free(j); continue; } c->e[k].jpg = j; c->e[k].len = len; c->e[k].st = 2; c->jpgBytes += len; got++; }
  fclose(f); if (got) { c->H = H; c->hSet = 1; c->got += got; c->saved = got; utime(fn, NULL); fprintf(stderr, "[seekprev] %d pictures kept on this device: the whole bar at once\n", got); } }
/* black bars at the top and bottom of a picture (letterboxing): rows whose brightest pixel stays dark; a picture
   that is nearly all dark (a dark scene, a fade) tells nothing */
static void bars_of(const unsigned char *rgb, int W, int H, int *t, int *b) { int top = 0, bot = 0;
  for (int y = 0; y < H / 2; y++) { int mx = 0; const unsigned char *r = rgb + (size_t)y * W * 3; for (int x = 0; x < W * 3; x += 3) { int v = r[x] + r[x + 1] + r[x + 2]; if (v > mx) mx = v; } if (mx > 60) break; top++; }
  for (int y = H - 1; y >= H / 2; y--) { int mx = 0; const unsigned char *r = rgb + (size_t)y * W * 3; for (int x = 0; x < W * 3; x += 3) { int v = r[x] + r[x + 1] + r[x + 2]; if (v > mx) mx = v; } if (mx > 60) break; bot++; }
  *t = top; *b = bot; }
static void bars_learn(Ctx *c, const unsigned char *rgb) { int t, b; bars_of(rgb, c->W, c->H, &t, &b); if (t + b >= c->H * 3 / 4) return; /* (all dark: nothing learnt) */
  if (!c->barN) { c->barT = t; c->barB = b; } else { if (t < c->barT) c->barT = t; if (b < c->barB) c->barB = b; } c->barN++; }
typedef struct { char fn[1200]; int nk, W, H, n; int *k, *len; unsigned char **jpg; } SaveJob;
static void *sp_cache_write(void *a) { SaveJob *j = a; nice_to(10); char d[1100]; snprintf(d, sizeof d, "%s", j->fn); char *sl = strrchr(d, '/'); if (sl) *sl = 0;
  long long fr = cache_free(d); long long sz = 21; for (int i = 0; i < j->n; i++) sz += 8 + j->len[i];
  if (fr > CACHE_FREE_MIN + sz) { char tmp[1300]; snprintf(tmp, sizeof tmp, "%s.tmp", j->fn); FILE *f = fopen(tmp, "wb"); int ok = f != NULL;
    if (ok) { ok = fwrite("SBPV1", 1, 5, f) == 5 && fwrite(&j->nk, 4, 1, f) == 1 && fwrite(&j->W, 4, 1, f) == 1 && fwrite(&j->H, 4, 1, f) == 1 && fwrite(&j->n, 4, 1, f) == 1;
      for (int i = 0; ok && i < j->n; i++) ok = fwrite(&j->k[i], 4, 1, f) == 1 && fwrite(&j->len[i], 4, 1, f) == 1 && fwrite(j->jpg[i], 1, j->len[i], f) == (size_t)j->len[i]; if (fclose(f)) ok = 0; }
    if (ok && !rename(tmp, j->fn)) { fprintf(stderr, "[seekprev] %d pictures kept on this device (%lld KB)\n", j->n, sz / 1024); cache_trim(d); } else unlink(tmp); }
  for (int i = 0; i < j->n; i++) free(j->jpg[i]); free(j->k); free(j->len); free(j->jpg); free(j); return NULL; }
static void cache_save(Ctx *c) { /* (caller holds c->mu): a copy of what's made, written by a thread of its own */
  if (c->got <= c->saved || c->got < 4) return; SaveJob *j = calloc(1, sizeof *j); if (!j) return; cache_name(c, j->fn, sizeof j->fn); if (!j->fn[0]) { free(j); return; }
  int n = 0; for (int i = 0; i < c->nk; i++) if (c->e[i].st == 2 && c->e[i].jpg) n++; j->k = malloc(n * sizeof(int) + 1); j->len = malloc(n * sizeof(int) + 1); j->jpg = malloc(n * sizeof(void *) + 1); if (!j->k || !j->len || !j->jpg) { free(j->k); free(j->len); free(j->jpg); free(j); return; }
  for (int i = 0; i < c->nk; i++) if (c->e[i].st == 2 && c->e[i].jpg) { unsigned char *cp = malloc(c->e[i].len); if (!cp) continue; memcpy(cp, c->e[i].jpg, c->e[i].len); j->k[j->n] = i; j->len[j->n] = c->e[i].len; j->jpg[j->n] = cp; j->n++; }
  j->nk = c->nk; j->W = c->W; j->H = c->H; c->saved = c->got; pthread_t t; if (pthread_create(&t, NULL, sp_cache_write, j)) { for (int i = 0; i < j->n; i++) free(j->jpg[i]); free(j->k); free(j->len); free(j->jpg); free(j); } else pthread_detach(t); }

/* ---- the fetch: the MKV reader's own ranged read, on a kept-alive connection of this module ---- */
/* connections opened and warmed while the film's index is read, taken by the readers as they start */
static void *warm_one(void *a) { MkvSession *m = a; nice_to(10); for (int i = 0; i < 60 && !net_led_known(mkv_url(m)) && !mkv_dead(m); i++) SDL_Delay(100); /* (once the film's own read has followed its link: each warm one goes straight there) */
  NetRange *r = mkv_dead(m) ? NULL : net_range_new(); if (r && mkv_net_warm(m, r) == 0) { pthread_mutex_lock(&poolMu); if (npool < 10) { pool[npool++] = r; r = NULL; } pthread_mutex_unlock(&poolMu); } if (r) net_range_free(r); mkv_release(m); return NULL; }
static void warm_start(MkvSession *m, int n) { if (warmFor == m) return; warmFor = m; pthread_mutex_lock(&poolMu); for (int i = 0; i < npool; i++) net_range_free(pool[i]); npool = 0; pthread_mutex_unlock(&poolMu);
  for (int i = 0; i < n; i++) { mkv_retain(m); pthread_t t; if (pthread_create(&t, NULL, warm_one, m)) mkv_release(m); else pthread_detach(t); } }
int sp_fetch_key(MkvSession *m, int k, long maxBytes, unsigned char **frame, long *flen, double *t, long *bytes) {
  if (!fnet) { pthread_mutex_lock(&poolMu); if (npool) fnet = pool[--npool]; pthread_mutex_unlock(&poolMu); } if (!fnet) fnet = net_range_new(); if (!fnet) return -1; int rq = 0;
  int r = mkv_key_read(m, fnet, k, fguess, maxBytes, frame, flen, t, bytes, &rq); if (!r) fguess = (long)(fguess * 0.7 + *flen * 1.25 * 0.3); return r; }

/* ---- the player's side ---- */
int sp_torrents(void) { return prev_enabled() && !strcmp(store_get("seekPreviewsTorrent", "false"), "true"); }
static void ctx_close(void) { pthread_mutex_lock(&gmu); Ctx *c = cur; cur = NULL; pthread_mutex_unlock(&gmu); if (!c) return;
  pthread_mutex_lock(&c->mu); if (!c->partial || c->tor) cache_save(c); c->stop = 1; pthread_cond_broadcast(&c->cv);
  fprintf(stderr, "[seekprev] closed: %d pictures, %d none, %ld KB read in %.0f s, %.0f ms CPU each\n", c->got, c->fails, c->bytes / 1024, mono() - c->t0, c->meanMs); pthread_mutex_unlock(&c->mu); ctx_drop(c);
  for (int i = 0; i < NTEX; i++) texKA[i] = -1; shownK = -1; }
void sp_close(void) { ctx_close(); }
static void link_for(Ctx *c, double mbps);
static Ctx *ctx_make(MkvSession *m, const char *key, int partial, int prep, double off) {
  static MkvSession *said; int nk = mkv_key_count(m); if (nk < 4) { if (said != m) { said = m; fprintf(stderr, "[seekprev] waiting: %d keyframes known\n", nk); } return NULL; } char codec[32]; unsigned char *pv = NULL; long pl = 0; int vt = mkv_video_track(m, codec, sizeof codec, &pv, &pl); if (vt != 1) { if (said != m) { said = m; fprintf(stderr, "[seekprev] the video can't be read as stored (%d)\n", vt); } return NULL; }
  Ctx *c = calloc(1, sizeof *c); if (!c) return NULL; c->kt = calloc(nk, sizeof *c->kt); c->e = calloc(nk, sizeof *c->e); if (!c->kt || !c->e) { free(c->kt); free(c->e); free(c); return NULL; }
  for (int i = 0; i < nk; i++) { mkv_key_time(m, i, &c->kt[i]); c->kt[i] += off; } /* (off: another copy of the film, its times as the film's) */
  if (pl > 0) { c->priv = malloc(pl); if (c->priv) { memcpy(c->priv, pv, pl); c->privLen = pl; } }
  int mb = plat_mem_total_mb();
#ifdef SB_PHONE
  c->W = mb >= 6000 ? 320 : 256;
#else
  c->W = mb >= 1500 ? 480 : 384; /* (a TV: a picture of about a third of the screen: 256 px came out blurred, magnified 2.7x) */
#endif
  c->H = (c->W * 9 / 16) & ~1;
  { const char *dv = getenv("SB_PREV_MEM"); long m = dv ? atol(dv) : mb <= 0 ? 32 : mb < 800 ? 16 : mb < 1100 ? 24 : mb < 1600 ? 32 : mb < 3100 ? 48 : 96; c->jpgMax = m << 20; } /* (the film's pictures in memory: 16 MB on a TV under 800 MB of RAM (about 900 of them), more with more; SB_PREV_MEM: development) */
#ifdef SB_PHONE
  c->slot = mb <= 0 || mb >= 4000 ? 10 : mb >= 2000 ? 15 : 30;
#else
  c->slot = step_s(); /* a TV, a hard rule: one picture every seek step (Settings > Playback), so every press has its own */
#endif
  { double sz = 0, du = 0; const char *q = key ? strrchr(key, '|') : NULL; if (q) { du = atof(q + 1); const char *p2 = q - 1; while (p2 > key && *p2 != '|') p2--; sz = atof(p2 + 1); } if (!(du > 0)) du = c->kt[nk - 1];
    double mbps = du > 0 && sz > 0 ? sz * 8 / du / 1e6 : 0; c->heavy = mbps >= 20;
#ifdef SB_PHONE
    if (c->heavy && c->slot < 30) c->slot = 30;
#endif /* (a heavy file, a remux, 4K: its keyframes are big, a picture every 30 s) */
    link_for(c, mbps);
    { long long lo = (mbps >= 35 ? 500LL : 300LL) << 20, cap = sz > 0 ? (long long)(sz * 0.10) : lo; if (cap < lo) cap = lo; if (cap > (800LL << 20)) cap = 800LL << 20; c->capBytes = (long)cap; } /* (a tenth of the file, 300 to 800 MB: some services count what is read) */ fprintf(stderr, "[seekprev] %.0f Mbit/s: a picture every %.0f s, at most %ld MB read\n", mbps, c->slot, c->capBytes >> 20); } /* (memory decides: the picture's width and how close the slots are) */
  snprintf(c->codec, sizeof c->codec, "%s", codec); snprintf(c->key, sizeof c->key, "%s", key ? key : ""); c->m = m; mkv_retain(m); c->nk = nk; c->partial = partial; c->t0 = mono(); c->seekAt = -10; c->guess = 192 * 1024;
  for (int i = 0; i < RING; i++) c->ring[i].k = -1;
  if (partial && !prep && sp_torrents() && !strncmp(mkv_url(m), "http://127.0.0.1", 16)) { /* a torrent through the engine: its keyframes read as previews (lowest priority, never the playhead) */
    char u[600]; snprintf(u, sizeof u, "%s%spreview", mkv_url(m), strchr(mkv_url(m), '?') ? "&" : "?"); mkv_set_key_url(m, u); c->tor = 1; c->multi = 0; c->capBytes = 500L << 20; }
  c->prep = prep; if (!partial || c->tor) cache_load(c); /* (opened before: its pictures at once) */
  pthread_mutex_init(&c->mu, NULL); pthread_cond_init(&c->cv, NULL); int nf = c->tor ? 3 : prep == 1 || partial || (sp_metered && sp_metered()) ? 1 : 4; /* (a torrent: three preview reads at once, each one piece at the lowest priority) */ c->lanes = nf < 3 ? nf : 3; if (c->tight) c->lanes = 1; else if (c->heavy && c->lanes > 2) c->lanes = 2; c->multi = c->tor ? 0 : -1; /* (the wait is the server's answer, not the phone: six at once for the first pass) */ c->refs = 1 + nf + 1;
  for (int i = 0; i < nf; i++) { pthread_t a; if (pthread_create(&a, NULL, fetch_run, c)) c->refs--; else pthread_detach(a); } { pthread_t b; if (pthread_create(&b, NULL, decode_run, c)) c->refs--; else pthread_detach(b); }
#ifndef SB_PHONE
  if (!partial) { c->refs++; pthread_t b2; if (pthread_create(&b2, NULL, decode_run, c)) c->refs--; else pthread_detach(b2); } /* (a TV: a second decoder for the held, paused or idle times) */
#endif
  fprintf(stderr, "[seekprev] %s, %d keyframes, slots of %.0f s, pictures %d px wide%s\n", codec, nk, c->slot, c->W, partial ? ", only near the playhead (a source still coming)" : ""); return c; }
/* the link against the film that plays (another copy making the pictures: still the playing film's bitrate) */
static void link_for(Ctx *c, double mbps) { extern double best_net_mbps(void); double link = best_net_mbps();
#ifdef SB_PHONE
  c->tight = (mbps > 0 && link > 0 && mbps * 3 > link) || (link <= 0 && mbps >= 20); c->roomy = !c->tight && link > 0 && mbps > 0 && link >= mbps * 6;
#else /* a TV: wired or a steady Wi-Fi, and the buffer guard (may_fill) stops the readers as soon as the film's buffer falls: tight under 1.5x the film's bitrate, roomy from 2x */
  c->tight = (mbps > 0 && link > 0 && mbps * 1.5 > link) || (link <= 0 && mbps >= 20); c->roomy = !c->tight && link > 0 && mbps > 0 && link >= mbps * 2;
#endif
  fprintf(stderr, "[seekprev] link %.0f Mbit/s for a %.0f Mbit/s film: %s\n", link, mbps, c->tight ? "while it plays only the finger's pictures, the rest paused" : c->roomy ? "3 readers beside it" : "1 reader beside it"); }
static double key_mbps(const char *key) { double sz = 0, du = 0; const char *q = key ? strrchr(key, '|') : NULL; if (q) { du = atof(q + 1); const char *p2 = q - 1; while (p2 > key && *p2 != '|') p2--; sz = atof(p2 + 1); } return du > 0 && sz > 0 ? sz * 8 / du / 1e6 : 0; }
static void ctx_open(MkvSession *m, const char *key, int partial) { Ctx *c = ctx_make(m, key, partial, 0, 0); if (!c) return; pthread_mutex_lock(&gmu); cur = c; pthread_mutex_unlock(&gmu); }

static Ctx *srcC; static MkvSession *srcM; static double srcOff; /* another copy of the film making its pictures (core/prevsrc), and how far its times sit from the film's */
/* what plays and how it goes, into a context (the film's, or another copy's making its pictures beside it) */
static void ctx_state(Ctx *c, double pos, double dur, double ahead, int buffering, int paused) {
  pthread_mutex_lock(&c->mu); double now = mono();
#ifndef SB_PHONE
  { double st = step_s(); if (st != c->slot) { fprintf(stderr, "[seekprev] the seek step is %.0f s now: a picture every %.0f s\n", st, st); c->slot = st; } }
#endif
  if (c->lastTick > 0 && !c->paused && fabs(pos - (c->pos + (now - c->lastTick))) > 5) c->seekAt = now; /* (a jump: a seek, its reading first) */
  int steady = c->lastTick > 0 && c->playSince > 0 && now - c->playSince >= 10; /* (it had played on for 10 s: a buffering now is a real stall, not the start or a seek's own loading) */
  if (c->lastTick <= 0 || paused || buffering || now - c->seekAt < 1) c->playSince = now; /* (since when it plays on without a pause, stall or seek) */
#ifdef SB_PHONE
  steady = 1;
#endif
  if (buffering && !c->buffering && !paused && steady && now - c->seekAt > 3) { c->bufBack = c->bufBack > 0 ? fmin(300, c->bufBack * 2) : 30; c->fillUntil = now + c->bufBack; c->playSince = now; fprintf(stderr, "[seekprev] the film stalled: no fill for %.0f s\n", c->bufBack); } /* (a stall while it plays: the readers step back, longer each time) */
  if (paused && !c->paused && (!c->partial || c->tor)) cache_save(c); /* (paused: what's made kept, in case the app goes) */
  c->prevAhead = c->ahead; c->pos = pos; c->dur = dur; c->ahead = ahead; c->buffering = buffering; c->paused = paused; c->lastTick = now;
  if (c->tor) { int ok = (paused || (ahead >= 30 && !buffering)) && !(sp_metered && sp_metered()); if (ok) { if (!c->torSince) c->torSince = now; } else c->torSince = 0; /* (a torrent: far pieces once it plays steadily, its buffer full for 8 s, or paused: nothing playing to starve) */
    int go = c->torSince && (paused || now - c->torSince >= 8) && now - c->seekAt >= 5 && c->torOut < SPREAD && (c->capBytes <= 0 || c->bytes < c->capBytes);
    if (go != c->torGo) { c->torGo = go; fprintf(stderr, "[seekprev] torrent: %s (%d far pictures)\n", go ? "steady, reading further pieces" : "only what's downloaded", c->torOut); } }
  if (ahead > c->maxAhead) c->maxAhead = ahead; if (buffering) c->seekAt = now > c->seekAt ? fmax(c->seekAt, now - 2) : c->seekAt;
  pthread_cond_broadcast(&c->cv); pthread_mutex_unlock(&c->mu); }
void sp_player_tick(MkvSession *m, const char *key, int partial, int live, double pos, double dur, double ahead, int buffering, int paused) {
  if (!(dur > 60) && m) { int nk = mkv_key_count(m); double lt = 0; if (nk >= 4 && mkv_key_time(m, nk - 1, &lt) && lt > 60) dur = lt + 5; } /* (the player not playing yet: the film's length from its last keyframe, so the pictures start as soon as the index is in) */
  if (m && !live && !partial && prev_enabled() && lib_ready() && mkv_key_count(m) < 4) warm_start(m,
#ifdef SB_PHONE
    9
#else
    3 /* (a TV: three, less TLS work while the film loads) */
#endif
    ); int torOn = partial && sp_torrents() && m && !strncmp(mkv_url(m), "http://127.0.0.1", 16); /* (the index still coming: the readers' connections opened meanwhile) */
  int want = m && !live && (!partial || torOn) && dur > 60 && prev_enabled() && lib_ready(); /* (a direct file only for now: a torrent or a download still coming has none) */
  { static int lastWhy = -1; int why = !m ? 1 : live ? 2 : partial && !torOn ? 6 : !(dur > 60) ? 3 : !prev_enabled() ? 4 : !lib_ready() ? 5 : 0; { static double dbgAt; if (getenv("SB_SPDBG") && mono() - dbgAt > 5) { dbgAt = mono(); fprintf(stderr, "[seekprev] tick: m %p live %d pos %.0f dur %.0f ahead %.0f why %d cur %p\n", (void *)m, live, pos, dur, ahead, why, (void *)cur); } }
    if (why != lastWhy) { lastWhy = why; if (why) fprintf(stderr, "[seekprev] off: %s\n", why == 1 ? "no index reader for this source" : why == 2 ? "live" : why == 3 ? "no duration yet" : why == 4 ? "switched off" : why == 6 ? "a torrent or a download still coming" : "no decoder library yet"); } }
  if (want && srcC && srcM == m && (!cur || cur->m != m)) { if (cur) ctx_close(); Ctx *a = srcC; srcC = NULL; srcM = NULL; /* the copy verified: its pictures become the film's */
    pthread_mutex_lock(&a->mu); if (srcOff != 0) for (int i = 0; i < a->nk; i++) a->kt[i] += srcOff; a->prep = 0; link_for(a, key_mbps(key)); pthread_mutex_unlock(&a->mu);
    pthread_mutex_lock(&gmu); cur = a; pthread_mutex_unlock(&gmu); fprintf(stderr, "[seekprev] the pictures come from a lighter copy of the film (%d made, %+.3f s): %s, %d keyframes\n", a->got, srcOff, a->codec, a->nk); }
  if (!m && srcC && !live) { if (cur) ctx_close(); ctx_state(srcC, pos, dur, ahead, buffering, paused); return; } /* (waiting for the copy's check: the film's own not started; the copy minds the film's buffer) */
  if (cur && (!want || cur->m != m)) ctx_close();
  if (!want) return; if (!cur) ctx_open(m, key, partial); Ctx *c = cur; if (!c) return;
  ctx_state(c, pos, dur, ahead, buffering, paused); if (srcC && srcC != c) ctx_state(srcC, pos, dur, ahead, buffering, paused); }
int sp_active(void) { return cur != NULL; }
/* the next episode made ahead: its index read by the caller's own session, 48 pictures over it on one connection,
   kept on this device for when it opens; NULL stops it */
static Ctx *prepC; static MkvSession *prepM;
static void prep_stop(void) { Ctx *c = prepC; prepC = NULL; prepM = NULL; if (!c) return; pthread_mutex_lock(&c->mu); if (!c->stop) cache_save(c); c->stop = 1; pthread_cond_broadcast(&c->cv); pthread_mutex_unlock(&c->mu); ctx_drop(c); }
void sp_prepare(MkvSession *m, const char *key) { if (!m) { prep_stop(); return; } if (prepM == m || !prev_enabled() || !lib_ready() || (sp_metered && sp_metered())) return; if (mkv_key_count(m) < 4) return;
  prep_stop(); prepC = ctx_make(m, key, 0, 1, 0); if (prepC) { pthread_mutex_lock(&prepC->mu); prepC->dur = prepC->kt[prepC->nk - 1] + 5; prepC->ahead = -1; pthread_cond_broadcast(&prepC->cv); pthread_mutex_unlock(&prepC->mu); /* (its length from its last keyframe: nothing plays it to say) */ prepM = m; fprintf(stderr, "[seekprev] making the next episode's pictures ahead\n"); } }
/* a downloaded file's previews made before the download says it's finished: every picture of the grid read from the
   file on the phone (no network, no caps), kept on this device for when it plays. One job at a time */
typedef struct { char url[2100], key[300]; volatile int done; } MakeJob;
static MakeJob *mk;
static void *make_run(void *a) { MakeJob *j = a; nice_to(10); double t0 = mono(); mkvWantCues = 1; MkvSession *m = mkv_open(j->url, 0, 0);
  while (m && mkv_key_count(m) < 4 && !mkv_dead(m) && mono() - t0 < 30) SDL_Delay(200);
  lib_ready(); while (!apiKnown && mono() - t0 < 40) SDL_Delay(100);
  Ctx *c = m && api && mkv_key_count(m) >= 4 ? ctx_make(m, j->key, 0, 2, 0) : NULL;
  if (c) { pthread_mutex_lock(&c->mu); c->dur = c->kt[c->nk - 1] + 5; c->ahead = -1; c->capBytes = 0; pthread_cond_broadcast(&c->cv); pthread_mutex_unlock(&c->mu); int last = -1; double lastAt = mono();
    for (;;) { SDL_Delay(400); pthread_mutex_lock(&c->mu); int g = c->got + c->fails, n = (int)(c->dur / c->slot) + 1, st = c->stop; pthread_mutex_unlock(&c->mu); if (g != last) { last = g; lastAt = mono(); }
      if (st || g >= n || mono() - lastAt > 15 || mono() - t0 > 900) break; }
    pthread_mutex_lock(&c->mu); fprintf(stderr, "[seekprev] a download's previews: %d pictures in %.0f s\n", c->got, mono() - t0); cache_save(c); c->stop = 1; pthread_cond_broadcast(&c->cv); pthread_mutex_unlock(&c->mu); ctx_drop(c); }
  else fprintf(stderr, "[seekprev] a download's previews: none (no index or no decoder)\n");
  if (m) mkv_close(m); j->done = 1; return NULL; }
int sp_make_file(const char *url, const char *key) { if (!prev_enabled() || mk) return 0; lib_ready(); MakeJob *j = calloc(1, sizeof *j); if (!j) return 0; snprintf(j->url, sizeof j->url, "%s", url); snprintf(j->key, sizeof j->key, "%s", key);
  pthread_t t; if (pthread_create(&t, NULL, make_run, j)) { free(j); return 0; } pthread_detach(t); mk = j; return 1; }
int sp_make_busy(void) { if (!mk) return 0; if (mk->done) { free(mk); mk = NULL; return 0; } return 1; }
int sp_prepared(void) { Ctx *c = prepC; if (!c) return 0; pthread_mutex_lock(&c->mu); int d = c->stop; pthread_mutex_unlock(&c->mu); return d; }
/* how much of the film has its pictures, as a scrub finds them: the share of its length within 1.5 slots of a picture
   (the same "close" a scrub lands by, sp_snap), 0..100; -1 no previews. The grid goes on refining after 100 */
double sp_progress_f(void) { Ctx *c = cur ? cur : srcC; if (!c) return -1; static double at, last = -1; static Ctx *lc; if (lc == c && mono() - at < 0.25) return last; lc = c; at = mono();
  /* the share of the film that has its pictures, or never can: a place counts once a keyframe within reach (1.5 slots)
     has its picture or failed for good, or no keyframe reaches it at all; only places still waiting on a keyframe count
     against it, so a few unreadable keyframes or a film's last stretch never hold it at 99 % */
  pthread_mutex_lock(&c->mu); double r = c->slot * 1.5, step = c->slot / 4 > 0.5 ? c->slot / 4 : 0.5; long n = 0, wait = 0; int j0 = 0;
  for (double x = 0; x < c->dur && c->nk > 0; x += step) { n++; while (j0 < c->nk && c->kt[j0] < x - r) j0++; int done = 0, any = 0;
    for (int j = j0; j < c->nk && c->kt[j] <= x + r; j++) { any = 1; if (c->e[j].st >= 2) { done = 1; break; } }
    if (any && !done) wait++; }
  int ex = c->exhausted && !c->nq; pthread_mutex_unlock(&c->mu); last = n ? (n - wait) * 100.0 / n : 0; if (ex && (c->capBytes <= 0 || c->bytes < c->capBytes)) last = 100; /* (the plan done: whatever still "waits" is never read: all there is) */ else if (wait && last > 99.9) last = 99.9; return last; }
int sp_done(void) { Ctx *c = cur ? cur : srcC; if (!c) return 0; pthread_mutex_lock(&c->mu); int busy = c->nq > 0; for (int i = 0; i < c->nk && !busy; i++) if (c->e[i].st == 1) busy = 1;
  int d = (c->exhausted && !busy) || (c->capBytes > 0 && c->bytes >= c->capBytes) || c->stop; pthread_mutex_unlock(&c->mu); return d; } /* nothing more will be made in the background (all there is, or its reading limit) */
int sp_progress(void) { double f = sp_progress_f(); if (f < 0) return -1; int p = (int)f; return f < 100 && p > 99 ? 99 : p; }
int sp_may_make(void) { lib_ready(); return !apiKnown || api != NULL; } /* (asked as a film opens: a library still loading counts, its index is cheap) */
void sp_focus(double t, int dense) { Ctx *c = cur; if (!c) return; pthread_mutex_lock(&c->mu); if (t >= 0 && c->focusOn && fabs(t - c->focusT) > 0.2) c->dir = t > c->focusT ? 1 : -1; c->focusOn = t >= 0; if (t >= 0) c->focusT = t; c->dense = dense;
#ifndef SB_PHONE
  if (t >= 0) { /* a TV: the row's places four steps either side unpacked ahead by the decoder (the nearest last, so
       first: the asks are taken newest first), so a slide finds them ready and the drawing never has to */
    double st = atof(store_get("seekStep", "10")); if (st < 1) st = 10;
    static const int D[] = { 4, -4, 3, -3, 2, -2, 1, -1 };
    for (int i = 0; i < 8; i++) { double ct = t + D[i] * st; if (ct < 0 || (c->dur > 0 && ct > c->dur)) continue;
      int kb = key_at_or_before(c, ct), a2 = -1, b2 = -1; for (int j = kb; j >= 0 && a2 < 0; j--) if (c->e[j].st == 2) a2 = j; for (int j = kb + 1; j < c->nk && b2 < 0; j++) if (c->e[j].st == 2) b2 = j;
      int k = a2 < 0 ? b2 : b2 < 0 ? a2 : (ct - c->kt[a2] <= c->kt[b2] - ct ? a2 : b2); if (k < 0) continue;
      int have = 0; for (int r = 0; r < RING && !have; r++) if (c->ring[r].rgb && c->ring[r].k == k) have = 1; for (int r = 0; r < c->nWant && !have; r++) if (c->wantRgb[r] == k) have = 1; if (have) continue;
      if (c->nWant == 8) memmove(c->wantRgb, c->wantRgb + 1, 7 * sizeof(int)), c->nWant = 7; c->wantRgb[c->nWant++] = k; } }
#endif
 pthread_cond_broadcast(&c->cv); pthread_mutex_unlock(&c->mu); }
void sp_trim(void) { Ctx *c = cur; if (!c) return; pthread_mutex_lock(&c->mu); for (int i = 0; i < RING; i++) { free(c->ring[i].rgb); c->ring[i].rgb = NULL; c->ring[i].k = -1; } pthread_mutex_unlock(&c->mu); }
/* a picture into texture slot (the lock held): unpacked if need be, uploaded once; tol >= 0: while it unpacks, the
   slot's last picture stands in when that near */
static int tex_out(Ctx *c, int slot, int k, int kb, int exact, double t, double tol, SpPic *out) {
  unsigned tex = texA[slot]; int texK = texKA[slot], texW = texWA[slot], texH = texHA[slot];
  unsigned char *rgb = NULL; for (int i = 0; i < RING; i++) if (c->ring[i].rgb && c->ring[i].k == k) { rgb = c->ring[i].rgb; c->ring[i].used = mono(); }
  int W = c->W, H = c->H, ok = 0;
  /* not unpacked yet: here and now, but at most what a frame can take. The first one in each frame always (the middle
     is asked for first), more only while the frame has spent under 3 ms on them: a few cores take well under a
     millisecond each and go on as before; one slow core (8 ms each, two missed a frame) does one, the rest are
     unpacked by the decoder and their places keep what they showed meanwhile */
  static unsigned upF; static double upMs; if (gfx_frame() != upF) { upF = gfx_frame(); upMs = 0; }
  if (!rgb && texK != k && c->e[k].jpg && (upMs == 0 || upMs < 3)) { double u0 = mono(); int x, y, n; unsigned char *px = stbi_load_from_memory(c->e[k].jpg, c->e[k].len, &x, &y, &n, 3);
    if (px && x == W && y == H) { unsigned char *cp = malloc((size_t)x * y * 3); if (cp) { memcpy(cp, px, (size_t)x * y * 3); ring_put(c, k, cp); rgb = cp; } } if (px) stbi_image_free(px); upMs += (mono() - u0) * 1000 + 0.001; }
  if (rgb && texK != k) { if (!tex || texW != W || texH < H) { if (tex) gfx_tex_free(tex); unsigned char *z = calloc((size_t)W * W, 3); tex = z ? gfx_tex_create(z, W, W, 3, 1) : 0; free(z); texW = W; texH = W; }
    if (tex) { gfx_tex_update(tex, rgb, 0, 0, W, H, 3); texK = k; { static Ctx *bc; static int seen[16], ns; if (bc != c) { bc = c; ns = 0; } int dup = 0; for (int i = 0; i < ns; i++) if (seen[i] == k) dup = 1; /* (each picture learnt from once: the same dark one counted three times cropped to a band) */
        if (!dup && c->barN < 16) { if (ns < 16) seen[ns++] = k; bars_learn(c, rgb); } } } }
  if (texK == k) ok = 1;
  else { int q = 0; for (int i = 0; i < c->nWant; i++) if (c->wantRgb[i] == k) q = 1; if (!q) { if (c->nWant == 8) memmove(c->wantRgb, c->wantRgb + 1, 7 * sizeof(int)), c->nWant = 7; c->wantRgb[c->nWant++] = k; pthread_cond_broadcast(&c->cv); }
    if (texK >= 0 && texK < c->nk && fabs(c->kt[texK] - t) <= tol) { k = texK; exact = 0; ok = 1; } } /* (while its picture is unpacked: the one shown, if near) */
  if (ok) { int bt = c->barN >= 3 ? c->barT : 0, bb = c->barN >= 3 ? c->barB : 0; if (bt > H * 15 / 100) bt = H * 15 / 100; if (bb > H * 15 / 100) bb = H * 15 / 100; /* (never more than 15 % a side) */ /* (the bars cut off once 3 pictures agree: the rounded corners on the picture itself) */
    out->tex = tex; out->w = W; out->h = H - bt - bb; out->exact = exact && k == kb; out->t = c->kt[k]; out->u1 = 1; out->v0 = (float)bt / texH; out->v1 = (float)(H - bb) / texH; out->k = k; if (slot == 1) shownK = k; }
  texA[slot] = tex; texKA[slot] = texK; texWA[slot] = texW; texHA[slot] = texH; texUse[slot] = mono(); return ok; }
/* slot 1: the picture for t (the one a scrub lands on); 0 and 2: a neighbour, never the same picture as skip */
int sp_get_at(double t, int slot, int skip, SpPic *out) {
  Ctx *c = cur; if (!c || slot < 0 || slot > 2) return 0; pthread_mutex_lock(&c->mu);
  double tol = c->dur + 1; int kb = key_at_or_before(c, t), k = -1, exact = 0;
  /* steady as the finger moves: the middle picture is the last one made at or before t (else the first after), so it
     changes only when the finger passes another picture; its neighbours are the pictures made just before and just after
     it (slot 0 / 2, from skip), never ones found by a time window that jump with it */
  if (slot == 1 || skip < 0) {
#ifdef SB_PHONE
    for (int j = kb; j >= 0 && k < 0; j--) if (c->e[j].st == 2) k = j; for (int j = kb + 1; j < c->nk && k < 0; j++) if (c->e[j].st == 2) k = j;
#else /* a TV: the nearest picture made, either side (the remote jumps 10 s and more a press: the closest one at once, the exact one following) */
    int a = -1, b = -1; for (int j = kb; j >= 0 && a < 0; j--) if (c->e[j].st == 2) a = j; for (int j = kb + 1; j < c->nk && b < 0; j++) if (c->e[j].st == 2) b = j;
    k = a < 0 ? b : b < 0 ? a : (t - c->kt[a] <= c->kt[b] - t ? a : b);
#endif
    exact = k == kb; }
  else if (slot == 0) { for (int j = skip - 1; j >= 0 && k < 0; j--) if (c->e[j].st == 2) k = j; }
  else { for (int j = skip + 1; j < c->nk && k < 0; j++) if (c->e[j].st == 2) k = j; }
  if (k < 0) { pthread_mutex_unlock(&c->mu); return 0; }
  int ok = tex_out(c, slot, k, kb, exact, t, tol, out); pthread_mutex_unlock(&c->mu); return ok; }
/* the TV row: the picture made nearest t within reach (reach < 0: anywhere), each in a texture of its own (eight kept,
   the least used one taken), so a row of them slides without one picture's upload replacing another's */
int sp_get_cell(double t, double reach, int focus, SpPic *out) {
  Ctx *c = cur; if (!c) return 0; pthread_mutex_lock(&c->mu); int kb = key_at_or_before(c, t), a = -1, b = -1;
  for (int j = kb; j >= 0 && a < 0; j--) if (c->e[j].st == 2) a = j; for (int j = kb + 1; j < c->nk && b < 0; j++) if (c->e[j].st == 2) b = j;
  int k = a < 0 ? b : b < 0 ? a : (t - c->kt[a] <= c->kt[b] - t ? a : b);
  if (k < 0 || (reach >= 0 && fabs(c->kt[k] - t) > reach)) { pthread_mutex_unlock(&c->mu); return 0; }
  int slot = -1; for (int i = 3; i < NTEX; i++) if (texKA[i] == k && texA[i]) slot = i;
  if (slot < 0) { slot = 3; for (int i = 4; i < NTEX; i++) if (texUse[i] < texUse[slot]) slot = i; }
  int ok = tex_out(c, slot, k, kb, k == kb, t, -1, out); if (ok && focus) shownK = k; pthread_mutex_unlock(&c->mu); return ok; }
int sp_get(double t, SpPic *out) { return sp_get_at(t, 1, -1, out); }
double sp_shown_t(void) { Ctx *c = cur; if (!c || shownK < 0 || shownK >= c->nk) return -1; return c->kt[shownK]; }
void sp_dev_log(void) { Ctx *c = cur ? cur : srcC; static double t0, at; if (!c) { t0 = 0; return; } double now = mono(); if (!t0) t0 = now; if (now - at < 2) return; at = now; /* (development: SB_PREV_LOG) */
  double pf = sp_progress_f(); pthread_mutex_lock(&c->mu); int inq = c->nq, rd = 0; for (int i = 0; i < c->nk; i++) if (c->e[i].st == 1) rd++;
  fprintf(stderr, "[prevlog] %5.1f s  %5.1f %%  %4d made  %3d none  %6ld KB  queue %2d  reading %2d  stage %d%s  buffered %.0f%s\n", now - t0, pf, c->got, c->fails, c->bytes / 1024, inq, rd, c->ph, c->exhausted ? " (all asked)" : "", c->ahead, c->paused ? " paused" : "");
  pthread_mutex_unlock(&c->mu); }
int sp_made(void) { Ctx *c = cur; return c ? c->got : 0; } /* (pictures made for this film so far) */
double sp_slot(void) { Ctx *c = cur; return c ? c->slot : 15; }

double sp_snap(double t) {
  Ctx *c = cur; if (!c || !prev_enabled()) return t; pthread_mutex_lock(&c->mu); double r = t;
#ifdef SB_PHONE
  double reach = c->slot * 1.5;
#else
  double reach = 2.0; /* (a TV: where the remote aimed; a picture shown further off stands in, it never moves the seek) */
#endif
  if (shownK >= 0 && shownK < c->nk && c->e[shownK].st == 2 && fabs(c->kt[shownK] - t) <= reach) r = c->kt[shownK]; /* (lands on the picture shown only when it is close: a far one shown for speed is not where the finger is) */
  else { int k = key_at_or_before(c, t); if (t - c->kt[k] <= 1.5 && t >= c->kt[k]) r = c->kt[k]; }
  pthread_mutex_unlock(&c->mu); return r; }

/* ---- another copy of the film making the pictures (core/prevsrc chooses it) ----
   A lighter copy of the same title (a 1080p or 720p SDR file beside a 4K remux) makes the pictures at a fraction of
   the reads and the decoding. It starts as soon as its index is in (the title page, before Play), on its own
   context; the film playing hands it the playhead's state so it never takes the film's bandwidth; once the copy is
   proven the same cut (sp_verify), the player passes it and its pictures become the film's, its times moved by the
   offset found. Its pictures are kept on this device like any film's (its own keyframe count names them). */
void sp_source_start(MkvSession *m, const char *key) { if (!m || srcM == m || !prev_enabled() || !lib_ready() || mkv_key_count(m) < 4) return;
  sp_source_stop(); Ctx *c = ctx_make(m, key, 0, 3, 0); if (!c) return; pthread_mutex_lock(&c->mu); c->dur = c->kt[c->nk - 1] + 5; c->ahead = -1; pthread_cond_broadcast(&c->cv); pthread_mutex_unlock(&c->mu);
  srcC = c; srcM = m; srcOff = 0; fprintf(stderr, "[seekprev] a lighter copy makes the pictures ahead: %s, %d keyframes, %d already kept\n", c->codec, c->nk, c->got); }
void sp_source_stop(void) { Ctx *c = srcC; srcC = NULL; srcM = NULL; if (!c) return; pthread_mutex_lock(&c->mu); cache_save(c); c->stop = 1; pthread_cond_broadcast(&c->cv);
  fprintf(stderr, "[seekprev] the lighter copy stopped: %d pictures, %ld KB read in %.0f s\n", c->got, c->bytes / 1024, mono() - c->t0); pthread_mutex_unlock(&c->mu); ctx_drop(c); }
int sp_source_on(MkvSession *m) { return srcC && (!m || srcM == m); }
int sp_source_made(void) { Ctx *c = srcC; if (!c) return 0; pthread_mutex_lock(&c->mu); int g = c->got; pthread_mutex_unlock(&c->mu); return g; }
void sp_source_offset(double off) { srcOff = off; }
void sp_source_idle(void) { Ctx *c = srcC; if (!c) return; pthread_mutex_lock(&c->mu); c->ahead = -1; c->buffering = 0; c->paused = 0; c->backUntil = 0; pthread_cond_broadcast(&c->cv); pthread_mutex_unlock(&c->mu); } /* (nothing plays: it reads freely) */

/* ---- is the copy the same cut as the film ----
   1. their lengths within 4 s; 2. their keyframes line up: encoders put a keyframe on most scene cuts, so the
   right offset makes hundreds of keyframe times meet within a few milliseconds while a wrong one meets only by
   chance (a histogram of the time differences, 5 ms bins over +-2 s: the best 15 ms must hold 12 meetings or more and
   5x what chance gives); 3. the pictures agree: up to 8 keyframes that meet, spread over the film, read from both and
   decoded small; each pair compared by the brightness of a 24 x 12 grid of the picture (its black bars left out,
   so a cropped copy compares with a letterboxed one), normalised correlation: 3 pairs at 0.8 or more and none under
   0.6, flat or dark pictures skipped. Anything short of that: the film's own keyframes make its pictures. */
typedef struct { MkvSession *a, *b; volatile int state, gone; double off; } Vfy;
static Vfy *vf; static pthread_mutex_t vfMu = PTHREAD_MUTEX_INITIALIZER;
static int grid_of(const unsigned char *rgb, int W, int H, double *g, double *sd) { int t, b; bars_of(rgb, W, H, &t, &b); int h = H - t - b; if (h < H / 4) return 0;
  double sum = 0, sq = 0; for (int gy = 0; gy < 12; gy++) for (int gx = 0; gx < 24; gx++) { int y0 = t + gy * h / 12, y1 = t + (gy + 1) * h / 12, x0 = gx * W / 24, x1 = (gx + 1) * W / 24; double v = 0; int n = 0;
      for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) { const unsigned char *p = rgb + ((size_t)y * W + x) * 3; v += (p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8; n++; }
      v = n ? v / n : 0; g[gy * 24 + gx] = v; sum += v; sq += v * v; }
  double m = sum / 288; *sd = sqrt(fmax(0, sq / 288 - m * m)); for (int i = 0; i < 288; i++) g[i] -= m; return 1; }
static double ncc(const double *a, const double *b) { double ab = 0, aa = 0, bb = 0; for (int i = 0; i < 288; i++) { ab += a[i] * b[i]; aa += a[i] * a[i]; bb += b[i] * b[i]; } return aa > 0 && bb > 0 ? ab / sqrt(aa * bb) : 0; }
static void *vfy_run(void *x) { Vfy *v = x; nice_to(10); double t0 = mono(); int res = -1; const char *why = "";
  double *ta = NULL, *tb = NULL; void *da = NULL, *db = NULL; NetRange *ra = NULL, *rb = NULL; unsigned char *pa = NULL, *pb = NULL, *rgb = NULL; long la_ = 0, lb_ = 0;
  for (int i = 0; i < 100 && !apiKnown; i++) SDL_Delay(100); if (!api) { why = "no decoder"; goto out; }
  int na = mkv_key_count(v->a), nb = mkv_key_count(v->b); if (na < 20 || nb < 20) { why = "too few keyframes"; goto out; }
  ta = malloc(na * sizeof *ta); tb = malloc(nb * sizeof *tb); if (!ta || !tb) goto out;
  for (int i = 0; i < na; i++) mkv_key_time(v->a, i, &ta[i]); for (int j = 0; j < nb; j++) mkv_key_time(v->b, j, &tb[j]);
  if (fabs(ta[na - 1] - tb[nb - 1]) > 4) { why = "their lengths differ"; goto out; }
  { enum { NB = 801 }; static int hist[NB]; memset(hist, 0, sizeof hist); long pairs = 0; int i0 = 0;
    for (int j = 0; j < nb; j++) { double s = tb[j]; while (i0 < na && ta[i0] < s - 2) i0++; for (int i = i0; i < na && ta[i] <= s + 2; i++) { int bi = (int)lround((ta[i] - s) / 0.005) + 400; if (bi >= 0 && bi < NB) { hist[bi]++; pairs++; } } }
    int best = 0, at = 400; for (int bi = 1; bi < NB - 1; bi++) { int w = hist[bi - 1] + hist[bi] + hist[bi + 1]; if (w > best) { best = w; at = bi; } }
    double chance = pairs * 3.0 / NB; fprintf(stderr, "[seekprev] copy check: %d keyframes meet at %+.3f s (chance %.1f, %ld pairs)\n", best, (at - 400) * 0.005, chance, pairs);
    if (best < 12 || best < chance * 5) { why = "their keyframes don't line up"; goto out; }
    double so = 0; int sn = 0; i0 = 0; double c0 = (at - 400) * 0.005;
    for (int j = 0; j < nb; j++) { double s = tb[j]; while (i0 < na && ta[i0] < s + c0 - 0.008) i0++; if (i0 < na && fabs(ta[i0] - s - c0) <= 0.008) { so += ta[i0] - s; sn++; } }
    v->off = sn ? so / sn : c0; }
  { char ca[32], cb[32]; unsigned char *xa = NULL, *xb = NULL; long xla = 0, xlb = 0;
    if (mkv_video_track(v->a, ca, sizeof ca, &xa, &xla) != 1 || mkv_video_track(v->b, cb, sizeof cb, &xb, &xlb) != 1) { free(xa); free(xb); why = "a video track that can't be read"; goto out; }
    da = api->open(ca, xa, (int)xla); db = api->open(cb, xb, (int)xlb); free(xa); free(xb); if (!da || !db) { why = "no decoder for one of them"; goto out; } }
  ra = net_range_new(); rb = net_range_new(); const int W = 192, H = 108; rgb = malloc(W * H * 3); if (!ra || !rb || !rgb) goto out;
  { int good = 0, bad = 0, tried = 0, ia = 0; double lb = tb[nb - 1];
    for (int q = 0; q < 8 && good < 3 && bad == 0 && !v->gone; q++) { double want = lb * (0.12 + q * 0.11); /* (spread: 12 %, 23 % ... 89 %) */
      int bj = -1; double bd = 1e9; ia = 0; for (int j = 0; j < nb; j++) { if (fabs(tb[j] - want) > lb * 0.05) continue; double s = tb[j] + v->off; while (ia < na && ta[ia] < s - 0.012) ia++; if (ia < na && fabs(ta[ia] - s) <= 0.012 && fabs(tb[j] - want) < bd) { bd = fabs(tb[j] - want); bj = j; } }
      if (bj < 0) continue; int ai = 0; { double s = tb[bj] + v->off; int lo = 0, hi = na - 1; while (lo < hi) { int m = (lo + hi) / 2; if (ta[m] < s - 0.012) lo = m + 1; else hi = m; } ai = lo; }
      double kt; long by; int rq; free(pa); free(pb); pa = pb = NULL;
      if (mkv_key_read(v->a, ra, ai, 1 << 20, 8 << 20, &pa, &la_, &kt, &by, &rq) || mkv_key_read(v->b, rb, bj, 192 << 10, 4 << 20, &pb, &lb_, &kt, &by, &rq)) continue;
      double ga[288], gb[288], sa, sb; int sw, sh, dk; float dar;
      if (api->frame(da, pa, (int)la_, W, H, rgb, &sw, &sh, &dar, &dk) || !grid_of(rgb, W, H, ga, &sa)) continue;
      if (api->frame(db, pb, (int)lb_, W, H, rgb, &sw, &sh, &dar, &dk) || !grid_of(rgb, W, H, gb, &sb)) continue;
      tried++; if (sa < 6 || sb < 6) { fprintf(stderr, "[seekprev] copy check at %.0f s: too flat to tell\n", tb[bj]); continue; }
      double r = ncc(ga, gb); fprintf(stderr, "[seekprev] copy check at %.0f s: the pictures agree %.2f\n", tb[bj], r); if (r >= 0.8) good++; else if (r < 0.6) bad++; }
    fprintf(stderr, "[seekprev] copy check: %d of %d compared agree, %d differ\n", good, tried, bad);
    if (good >= 3 && !bad) res = 1; else why = bad ? "a picture differs" : "not enough pictures to compare"; }
out:
  fprintf(stderr, "[seekprev] copy check: %s in %.1f s%s%s\n", res > 0 ? "the same cut" : "not used", mono() - t0, res > 0 ? "" : ": ", res > 0 ? "" : why);
  if (da) api->close(da); if (db) api->close(db); net_range_free(ra); net_range_free(rb); free(pa); free(pb); free(rgb); free(ta); free(tb);
  mkv_release(v->a); mkv_release(v->b); pthread_mutex_lock(&vfMu); int gone = v->gone; v->state = res; pthread_mutex_unlock(&vfMu); if (gone) free(v); return NULL; }
int sp_verify_start(MkvSession *film, MkvSession *copy) { sp_verify_reset(); Vfy *v = calloc(1, sizeof *v); if (!v) return 0; v->a = film; v->b = copy; mkv_retain(film); mkv_retain(copy);
  pthread_t t; if (pthread_create(&t, NULL, vfy_run, v)) { mkv_release(film); mkv_release(copy); free(v); return 0; } pthread_detach(t); vf = v; return 1; }
int sp_verify_state(double *off) { Vfy *v = vf; if (!v) return -2; pthread_mutex_lock(&vfMu); int s = v->state; pthread_mutex_unlock(&vfMu); if (s > 0 && off) *off = v->off; return s; } /* 0 running, 1 the same, -1 not, -2 none */
void sp_verify_reset(void) { Vfy *v = vf; vf = NULL; if (!v) return; pthread_mutex_lock(&vfMu); int done = v->state != 0; v->gone = 1; pthread_mutex_unlock(&vfMu); if (done) free(v); }
