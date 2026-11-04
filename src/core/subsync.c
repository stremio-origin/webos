#include "core/subsync.h"
#include "core/net.h"
#include "core/json.h"
#include "core/addons.h"
#include "platform/plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <pthread.h>

#define X_MAX 4000          /* the subtitle's lines kept for matching (only those near the reference) */
#define R_MAX 3000          /* the reference's lines */
#define TXT_MAX 240         /* bytes of each line's words */
#define PAIRS_MAX 20000
#define STEP 0.05
static const double SCALES[7] = { 1, 25 / 23.976, 23.976 / 25, 24 / 23.976, 23.976 / 24, 25.0 / 24, 24.0 / 25 };

typedef struct { double s, e; uint32_t *w; int nw, n; } WLine; /* w: its distinct words (hashed, sorted); n: all of them */
typedef struct { SyncRef r; float score; } RefCand;
struct SubSync {
  pthread_mutex_t mu; int refs; volatile int cancel, stage; int done; SyncResult res;
  double center, behind, ahead, range; WLine *x, *r; int nx, nr, total; /* bounded copies: times and hashed words */
  SubQuery q; char cur[1024], curLang[24]; RefCand *c; int nc, ccap; /* another subtitle as the reference: what to look for */
};
static void free_r(SubSync *j) { for (int i = 0; i < j->nr; i++) free(j->r[i].w); j->nr = 0; }
static void release(SubSync *j) {
  pthread_mutex_lock(&j->mu); int left = --j->refs; pthread_mutex_unlock(&j->mu); if (left) return;
  for (int i = 0; i < j->nx; i++) free(j->x[i].w); free_r(j);
  free(j->x); free(j->r); free(j->c); pthread_mutex_destroy(&j->mu); free(j);
}

/* ---- words: lower case, tags dropped, letters of the usual scripts kept, one-letter words ignored ---- */
static unsigned utf8_next(const unsigned char **p) {
  const unsigned char *s = *p; unsigned c = *s++;
  if (c >= 0xc0 && c < 0xe0 && (*s & 0xc0) == 0x80) { c = (c & 31) << 6 | (*s++ & 63); }
  else if (c >= 0xe0 && c < 0xf0 && (s[0] & 0xc0) == 0x80 && (s[1] & 0xc0) == 0x80) { c = (c & 15) << 12 | (s[0] & 63) << 6 | (s[1] & 63); s += 2; }
  else if (c >= 0xf0 && (s[0] & 0xc0) == 0x80 && (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80) { c = (c & 7) << 18 | (s[0] & 63) << 12 | (s[1] & 63) << 6 | (s[2] & 63); s += 3; }
  *p = s; return c;
}
static unsigned lower_cp(unsigned c) {
  if (c >= 'A' && c <= 'Z') return c + 32;
  if (c >= 0xc0 && c <= 0xde && c != 0xd7) return c + 32;
  if (c >= 0x391 && c <= 0x3a9) return c + 32;
  if (c >= 0x410 && c <= 0x42f) return c + 32;
  if (c >= 0x400 && c <= 0x40f) return c + 80;
  return c;
}
static int word_cp(unsigned c) {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c >= 0xc0 && c <= 0x24f) || (c >= 0x370 && c <= 0x3ff) || (c >= 0x400 && c <= 0x4ff) ||
    (c >= 0x590 && c <= 0x5ff) || (c >= 0x600 && c <= 0x6ff) || (c >= 0x3040 && c <= 0x30ff) || (c >= 0x4e00 && c <= 0x9fff);
}
static int cmp_u32(const void *a, const void *b) { uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b; return x < y ? -1 : x > y; }
static void words_of(const char *t, WLine *o) {
  uint32_t w[64]; int n = 0; uint32_t h = 2166136261u; int len = 0, skip = 0;
  const unsigned char *p = (const unsigned char *)(t ? t : "");
  for (;;) { unsigned c = *p ? utf8_next(&p) : 0;
    if (skip) { if (c == (unsigned)skip || !c) skip = 0; if (c) continue; }
    if (c == '<') { skip = '>'; c = ' '; } else if (c == '{') { skip = '}'; c = ' '; }
    c = lower_cp(c);
    if (c && word_cp(c)) { h = (h ^ c) * 16777619u; len++; continue; }
    if (len > 1 && n < 64) w[n++] = h;
    h = 2166136261u; len = 0; if (!c) break; }
  o->n = n; qsort(w, n, sizeof *w, cmp_u32); int u = 0; for (int i = 0; i < n; i++) if (!u || w[u - 1] != w[i]) w[u++] = w[i];
  o->nw = u; o->w = u ? malloc(u * sizeof *o->w) : NULL; if (o->w) memcpy(o->w, w, u * sizeof *w); else o->nw = 0;
}
static int inter_of(const WLine *a, const WLine *b) { int i = 0, k = 0, n = 0; while (i < a->nw && k < b->nw) { if (a->w[i] == b->w[k]) { n++; i++; k++; } else if (a->w[i] < b->w[k]) i++; else k++; } return n; }
static int cmp_wl(const void *a, const void *b) { double x = ((const WLine *)a)->s, y = ((const WLine *)b)->s; return x < y ? -1 : x > y; }
static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }

/* two answers put the subtitle in clearly different places: more than 1.5 s apart at either end of the stretch matched */
static int apart(double f1, double d1, double f2, double d2, double t0, double t1) { return fabs(t0 * (f1 - f2) + d1 - d2) > 1.5 || fabs(t1 * (f1 - f2) + d1 - d2) > 1.5; }
static void x_span(const SubSync *j, double *t0, double *t1) {
  double lo = j->center - j->behind, hi = j->center + j->ahead; *t0 = 1e18; *t1 = -1e18;
  for (int i = 0; i < j->nx; i++) if (j->x[i].e > lo - j->range - 80 && j->x[i].s < hi + j->range + 80) { if (j->x[i].s < *t0) *t0 = j->x[i].s; if (j->x[i].e > *t1) *t1 = j->x[i].e; }
  if (*t0 > *t1) *t0 = *t1 = j->center;
}

/* ---- the same words on both sides: every pair of matching lines votes for a shift, the densest 0.6 s of votes wins ---- */
typedef struct { double xs, rs; } Pair;
static int by_text(SubSync *j, double *d, double *f) {
  double c = j->center; int nrn = 0; for (int i = 0; i < j->nr; i++) if (j->r[i].nw && j->r[i].e > c - j->behind && j->r[i].s < c + j->ahead) nrn++;
  if (nrn < 8) return 0;
  Pair *pr = malloc(PAIRS_MAX * sizeof *pr); if (!pr) return 0; int np = 0;
  for (int i = 0; i < j->nr && np < PAIRS_MAX && !j->cancel; i++) { const WLine *r = &j->r[i]; if (!r->nw || r->e <= c - j->behind || r->s >= c + j->ahead || r->n < 3) continue; /* "Yes." / "No!" are everywhere */
    double lo = r->s / 1.05 - j->range - 10, hi = r->s / 0.95 + j->range + 10; int a = 0, z = j->nx; while (a < z) { int m = (a + z) / 2; if (j->x[m].s < lo) a = m + 1; else z = m; }
    for (int k = a; k < j->nx && j->x[k].s <= hi && np < PAIRS_MAX; k++) { const WLine *x = &j->x[k]; if (x->n < 2) continue;
      int in = inter_of(x, r); if (in >= 2 && (double)in / (r->nw + x->nw - in) >= 0.6) { pr[np].xs = x->s; pr[np].rs = r->s; np++; } } }
  if (np < 4 || j->cancel) { free(pr); return 0; }
  double *ds = malloc(np * sizeof *ds); typedef struct { int n; double f, d; } Cl; Cl *cl = malloc(np * 7 * sizeof *cl); int ncl = 0;
  if (!ds || !cl) { free(ds); free(cl); free(pr); return 0; }
  Cl best = { 0, 1, 0 };
  for (int si = 0; si < 7; si++) { double fs = SCALES[si]; int n = 0; for (int i = 0; i < np; i++) { double v = pr[i].rs - pr[i].xs * fs; if (v >= -j->range && v <= j->range) ds[n++] = v; }
    qsort(ds, n, sizeof *ds, cmp_d);
    for (int i = 0, k = 0; i < n; i++) { while (ds[i] - ds[k] > 0.6) k++; int m = i - k + 1; double mid = ds[k + (m >> 1)];
      cl[ncl].n = m; cl[ncl].f = fs; cl[ncl].d = mid; ncl++;
      if (m > best.n) { best.n = m; best.f = fs; best.d = mid; } } } /* a tie goes to the more common frame-rate pair (listed first) */
  int second = 0; double t0, t1; x_span(j, &t0, &t1);
  for (int i = 0; i < ncl; i++) if (apart(cl[i].f, cl[i].d, best.f, best.d, t0, t1) && cl[i].n > second) second = cl[i].n;
  free(ds); free(cl); free(pr);
  if (best.n >= 4 && best.n >= 2.5 * (second > 1 ? second : 1)) { *d = round(best.d * 100) / 100; *f = best.f; return 1; }
  return 0;
}

/* ---- timing only: overlap with the reference's lines, and where lines start and end (sharp even when someone talks
   all the time) ---- */
typedef struct { const double *RS, *RE; int nR; const int *P; int N; double W0, W1; } Ref;
static double cover(const Ref *R, double a, double b) {
  double ia = (a - R->W0) / STEP, ib = (b - R->W0) / STEP; if (ia < 0) ia = 0; if (ia > R->N) ia = R->N; if (ib < 0) ib = 0; if (ib > R->N) ib = R->N;
  if (ib <= ia) return 0; return (R->P[(int)ib] - R->P[(int)ia]) * STEP;
}
static int near(const double *arr, int n, double v) { int l = 0, h = n - 1; while (l < h) { int m = (l + h) >> 1; if (arr[m] < v) l = m + 1; else h = m; }
  double a = fabs(arr[l] - v), b = l > 0 ? fabs(arr[l - 1] - v) : 1e9; return (a < b ? a : b) < 0.35; }
static double score(const Ref *R, const double *xs, int n, double dd) {
  double ov = 0, tot = 0; int hit = 0, cnt = 0;
  for (int q = 0; q < n; q++) { double s1 = xs[2 * q] + dd, e1 = xs[2 * q + 1] + dd; if (e1 <= R->W0 || s1 >= R->W1) continue;
    tot += fmin(e1, R->W1) - fmax(s1, R->W0); ov += cover(R, s1, e1); cnt += 2; if (near(R->RS, R->nR, s1)) hit++; if (near(R->RE, R->nR, e1)) hit++; }
  if (tot < 20) return -1;
  return 0.35 * ov / tot + 0.65 * hit / cnt;
}
typedef struct { double r, f, d; } Cand;
/* Every shift within the range at each frame-rate ratio by overlap alone (cheap: one look-up per line), the few best
   shifts of each ratio then scored in 50 ms steps with the line starts and ends too. The winner must beat the best
   answer that puts the subtitle clearly elsewhere; the overlap's peak over its average (z) is logged with the three
   best distinct answers. tm: the winner and its rival for the caller (the references' consensus) */
typedef struct { Cand best, second; double z; int ok, local; } TimingAns;
#define PEAKS 6
static int by_timing(SubSync *j, double *d, double *f, float *sc, TimingAns *tm) {
  double c = j->center, lo = c - j->behind, hi = c + j->ahead, rg = j->range; int nR = 0;
  double *RS = malloc(j->nr * sizeof *RS), *RE = malloc(j->nr * sizeof *RE), *xs = malloc(j->nx * 2 * sizeof *xs), *ov = NULL; int *P = NULL; Cand *all = NULL; int ok = 0;
  if (tm) memset(tm, 0, sizeof *tm);
  if (!RS || !RE || !xs) goto out;
  double W0 = 1e18, W1 = -1e18;
  for (int i = 0; i < j->nr; i++) if (j->r[i].e > lo && j->r[i].s < hi) { RS[nR] = j->r[i].s; RE[nR] = j->r[i].e; nR++; if (j->r[i].s < W0) W0 = j->r[i].s; if (j->r[i].e > W1) W1 = j->r[i].e; }
  if (nR < 6 || W1 - W0 > 4 * 3600) goto out;
  int N = (int)ceil((W1 - W0) / STEP) + 1; unsigned char *A = calloc(N, 1); P = calloc(N + 1, sizeof *P); if (!A || !P) { free(A); goto out; }
  for (int i = 0; i < nR; i++) { int k0 = (int)floor((RS[i] - W0) / STEP), k1 = (int)floor((RE[i] - W0) / STEP); if (k0 < 0) k0 = 0; if (k1 > N - 1) k1 = N - 1; for (int k = k0; k <= k1; k++) A[k] = 1; }
  for (int i = 0; i < N; i++) P[i + 1] = P[i] + A[i]; free(A);
  qsort(RS, nR, sizeof *RS, cmp_d); qsort(RE, nR, sizeof *RE, cmp_d);
  Ref R = { RS, RE, nR, P, N, W0, W1 };
  int nd = (int)(2 * rg / 0.2) + 1; ov = malloc(nd * sizeof *ov);
  int cap = 7 * PEAKS * 17, na = 0; all = malloc(cap * sizeof *all); if (!all || !ov) goto out;
  Cand best = { -1, 1, 0 }; double bestZ = 0, t0, t1; x_span(j, &t0, &t1);
  for (int si = 0; si < 7 && !j->cancel; si++) { double fs = SCALES[si]; int n = 0; /* a subtitle timed for another frame rate is the right one stretched from 0 */
    for (int i = 0; i < j->nx; i++) { double a = j->x[i].s * fs, b = j->x[i].e * fs; if (b > W0 - rg - 5 && a < W1 + rg + 5) { xs[2 * n] = a; xs[2 * n + 1] = b; n++; } }
    if (n < 6) continue;
    double sum = 0, sq = 0; int nv = 0;
    for (int k = 0; k < nd; k++) { double dd = -rg + k * 0.2, o = 0, tot = 0;
      for (int q = 0; q < n; q++) { double s1 = xs[2 * q] + dd, e1 = xs[2 * q + 1] + dd; if (e1 <= W0 || s1 >= W1) continue; tot += fmin(e1, W1) - fmax(s1, W0); o += cover(&R, s1, e1); }
      ov[k] = tot >= 20 ? o / tot : -1; if (ov[k] >= 0) { sum += ov[k]; sq += ov[k] * ov[k]; nv++; } }
    if (nv < 3) continue;
    double mean = sum / nv, sd = sqrt(fmax(sq / nv - mean * mean, 1e-9));
    int pk[PEAKS], np = 0; /* the best local peaks of the overlap */
    for (int k = 0; k < nd; k++) { if (ov[k] < 0) continue; int top = 1; for (int q = k - 3; q <= k + 3 && top; q++) if (q >= 0 && q < nd && q != k && (ov[q] > ov[k] || (ov[q] == ov[k] && q < k))) top = 0; if (!top) continue;
      if (np < PEAKS) pk[np++] = k; else { int w = 0; for (int q = 1; q < PEAKS; q++) if (ov[pk[q]] < ov[pk[w]]) w = q; if (ov[k] > ov[pk[w]]) pk[w] = k; } }
    for (int p = 0; p < np && !j->cancel; p++) { double z = (ov[pk[p]] - mean) / sd;
      for (int s = 0; s <= 16; s++) { double fd = -rg + pk[p] * 0.2 - 0.4 + s * STEP, r = score(&R, xs, n, fd); if (r < 0) continue;
        if (na < cap) { all[na].r = r; all[na].f = fs; all[na].d = fd; na++; } if (r > best.r) { best.r = r; best.f = fs; best.d = fd; bestZ = z; } } } }
  if (best.r < 0 || j->cancel) goto out;
  /* the three best answers that are clearly apart, for the log; the second is the winner's rival */
  Cand top[3]; int nt = 0;
  for (int pass = 0; pass < 3; pass++) { Cand b = { -1, 1, 0 };
    for (int i = 0; i < na; i++) { int fresh = 1; for (int q = 0; q < nt; q++) if (!apart(all[i].f, all[i].d, top[q].f, top[q].d, t0, t1)) fresh = 0; if (fresh && all[i].r > b.r) b = all[i]; }
    if (b.r < 0) break; top[nt++] = b; }
  double second = nt > 1 ? top[1].r : 0, s2 = round(best.r * 100) / 100, m = round((best.r - second) * 100) / 100;
  *d = round(best.d * 100) / 100; *f = best.f; *sc = (float)s2;
  ok = s2 >= 0.45 && m >= 0.08 && (rg <= 60 || bestZ >= 4.5); /* over a wide range: another film's subtitle peaks at z 2-4, the right one at 6 and more */
  { char ln[300]; int o = 0; for (int q = 0; q < nt; q++) o += snprintf(ln + o, sizeof ln - o, "%s%+.2f s x%.4f = %.2f", q ? " | " : "", top[q].d, top[q].f, top[q].r);
    fprintf(stderr, "[sync] timing over %.0f s (+-%.0f s): %s; margin %.2f, peak z %.1f%s\n", W1 - W0, rg, ln, m, bestZ, ok ? "" : " (unsure)"); }
  if (tm) { tm->best = best; tm->second = nt > 1 ? top[1] : (Cand){ 0, 1, 0 }; tm->z = bestZ; tm->ok = ok; }
out:
  free(RS); free(RE); free(xs); free(P); free(all); free(ov); return ok;
}
static int match(SubSync *j, SyncResult *r) {
  if (by_text(j, &r->d, &r->f)) { r->st = SYNC_OK; r->byWords = 1; r->score = 1; fprintf(stderr, "[subsync] words d=%.2f f=%.4f\n", r->d, r->f); return 1; }
  if (!j->cancel && by_timing(j, &r->d, &r->f, &r->score, NULL)) { r->st = SYNC_OK; return 1; }
  return 0;
}
/* against a whole other subtitle: all of it first; one made for another cut fits only part of the film with one shift,
   so when the whole is unsure the 20 minutes around the playhead decide (that is where it is watched). tm: the
   timing matcher's last answer (for the references' consensus) */
#define REF_LOCAL 600.0
static int match_ref(SubSync *j, SyncResult *r, TimingAns *tm) {
  memset(tm, 0, sizeof *tm);
  if (by_text(j, &r->d, &r->f)) { r->st = SYNC_OK; r->byWords = 1; r->score = 1; fprintf(stderr, "[sync] words (whole file) %+.2f s x%.4f\n", r->d, r->f); return 1; }
  if (j->cancel) return 0;
  if (by_timing(j, &r->d, &r->f, &r->score, tm)) { r->st = SYNC_OK; return 1; }
  double b = j->behind, a = j->ahead; j->behind = j->ahead = REF_LOCAL; int ok = 0; TimingAns lt;
  if (!j->cancel && by_text(j, &r->d, &r->f)) { r->st = SYNC_OK; r->byWords = 1; r->score = 1; fprintf(stderr, "[sync] words (near %.0f s) %+.2f s x%.4f\n", j->center, r->d, r->f); ok = 1; }
  else if (!j->cancel) { ok = by_timing(j, &r->d, &r->f, &r->score, &lt); if (ok) r->st = SYNC_OK; if (ok || lt.best.r > tm->best.r) { *tm = lt; tm->local = 1; } }
  j->behind = b; j->ahead = a; return ok;
}
static void finish(SubSync *j, SyncResult r) { pthread_mutex_lock(&j->mu); j->res = r; j->done = 1; pthread_mutex_unlock(&j->mu); release(j); }
static void *sync_main(void *arg) {
  SubSync *j = arg; SyncResult r = { SYNC_UNSURE, 0, 1, 0, 0 };
  if (j->total < 10) r.st = SYNC_NOSUB; else match(j, &r);
  finish(j, r); return NULL;
}
static SubSync *job_new(double center, double behind, double ahead, int nx, int nr) {
  SubSync *j = calloc(1, sizeof *j); if (!j) return NULL;
  pthread_mutex_init(&j->mu, NULL); j->refs = 2; j->center = center; j->behind = behind; j->ahead = ahead; j->range = 30;
  j->x = calloc(nx < X_MAX ? (nx ? nx : 1) : X_MAX, sizeof *j->x); j->r = calloc(nr < R_MAX ? (nr ? nr : 1) : R_MAX, sizeof *j->r);
  if (!j->x || !j->r) { free(j->x); free(j->r); pthread_mutex_destroy(&j->mu); free(j); return NULL; }
  return j;
}
/* only the subtitle's lines that any frame-rate ratio and shift could bring near the reference */
static void load_x(SubSync *j, const Cue *X, int nx) {
  double lo = (j->center - j->behind - j->range - 50) / 1.05, hi = (j->center + j->ahead + j->range + 50) / 0.95; char tmp[TXT_MAX + 1];
  for (int i = 0; i < nx && j->nx < X_MAX; i++) { if (X[i].e < lo || X[i].s > hi) continue; WLine *o = &j->x[j->nx++]; o->s = X[i].s; o->e = X[i].e; snprintf(tmp, sizeof tmp, "%s", X[i].t ? X[i].t : ""); words_of(tmp, o); }
  qsort(j->x, j->nx, sizeof *j->x, cmp_wl);
  j->total = nx; /* (few of them near the playhead: the matchers say they are unsure) */
}
static void start(SubSync *j, void *(*fn)(void *)) { pthread_t t; if (pthread_create(&t, NULL, fn, j)) fn(j); else pthread_detach(t); }
SubSync *subsync_start(const Cue *X, int nx, const SyncLine *R, int nr, double center) {
  SubSync *j = job_new(center, SYNC_BEHIND_S, SYNC_AHEAD_S, nx, nr); if (!j) return NULL;
  load_x(j, X, nx); char tmp[TXT_MAX + 1];
  for (int i = 0; i < nr && j->nr < R_MAX; i++) { WLine *o = &j->r[j->nr++]; o->s = R[i].s; o->e = R[i].e; snprintf(tmp, sizeof tmp, "%s", R[i].t ? R[i].t : ""); words_of(tmp, o); }
  start(j, sync_main); return j;
}

/* ---- another subtitle as the reference: when the file has no text track (HTTP and debrid files, mostly), a subtitle
   made for this very file is the next best thing. Found by the file's hash first (OpenSubtitles is asked again with it;
   any list's entry the hash found), then those the stream carried, then the one whose file name is closest to the
   stream's. Any language: only the timing counts, as with the file's own tracks (the same words vote when it is the
   same language; else where lines start and end decides). One reference is held at a time, read in full (up to 4 MB)
   and kept only near the playhead; a few are tried at most. ---- */
#define REF_BYTES (4u << 20)
#define REF_BEHIND 1e6         /* a whole file: all of it is matched, not only the playhead's surroundings */
#define REF_AHEAD 1e6
#define REF_RANGE 300.0        /* and a wider range of shifts (a subtitle made for another release can be a minute or more off) */
typedef struct { SubSync *j; unsigned char *b; size_t n, cap, max; } Sink;
static int sink(const unsigned char *p, size_t n, void *ud) {
  Sink *s = ud; if (s->j->cancel) return 0; if (!p || !n) return 1; if (s->n + n + 1 > s->max) return 0;
  if (s->n + n + 1 > s->cap) { size_t c = s->cap ? s->cap : 65536; while (c < s->n + n + 1) c *= 2; unsigned char *b = realloc(s->b, c); if (!b) return 0; s->b = b; s->cap = c; }
  memcpy(s->b + s->n, p, n); s->n += n; s->b[s->n] = 0; return 1;
}
static unsigned char *fetch(SubSync *j, const char *url, size_t max, size_t *len) {
  Sink s = { j, NULL, 0, 0, max }; int st = net_stream_sync(url, 20000, sink, &s);
  if (st < 200 || st >= 300 || !s.n) { if (!j->cancel) fprintf(stderr, "[sync] reference download failed (HTTP %d, %u bytes)\n", st, (unsigned)s.n); free(s.b); return NULL; }
  *len = s.n; return s.b;
}
static void cand_add(SubSync *j, const SyncRef *r) {
  if (!r->url[0] || !strcmp(r->url, j->cur)) return; /* never the subtitle being synced */
  for (int i = 0; i < j->nc; i++) if (!strcmp(j->c[i].r.url, r->url)) { j->c[i].r.hm |= r->hm; j->c[i].r.own |= r->own; if (!j->c[i].r.rel[0]) snprintf(j->c[i].r.rel, sizeof j->c[i].r.rel, "%s", r->rel); return; }
  if (j->nc == j->ccap) { int c = j->ccap ? j->ccap * 2 : 64; if (c > 256) return; /* (bounded: the first 256 listed) */ RefCand *n = realloc(j->c, c * sizeof *n); if (!n) return; j->c = n; j->ccap = c; }
  memset(&j->c[j->nc], 0, sizeof j->c[0]); j->c[j->nc++].r = *r;
}
/* the file's name in words: lower case, letters and digits; the usual containers and languages left out */
static int name_words(const char *s, char w[][24], int max) {
  static const char *skip[] = { "mkv", "mp4", "avi", "srt", "sub", "ass", "ssa", "vtt", "www", "com", "org", "eng", "english", "en", "the", "and", NULL };
  int n = 0; char cur[24]; int l = 0;
  for (const char *p = s;; p++) { unsigned char c = (unsigned char)*p; int al = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    if (al) { if (l < 23) cur[l++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c); continue; }
    if (l >= 2 || (l == 1 && cur[0] >= '0' && cur[0] <= '9')) { cur[l] = 0; int sk = 0; for (int k = 0; skip[k]; k++) if (!strcmp(skip[k], cur)) sk = 1;
      for (int k = 0; k < n && !sk; k++) if (!strcmp(w[k], cur)) sk = 1;
      if (!sk && n < max) snprintf(w[n++], 24, "%s", cur); }
    l = 0; if (!c) break; }
  return n;
}
static int ep_tag(char w[][24], int n, int *se, int *ep) { for (int i = 0; i < n; i++) if (sscanf(w[i], "s%de%d", se, ep) == 2) return 1; return 0; }
static int has_cd(char w[][24], int n) { for (int i = 0; i < n; i++) if ((w[i][0] == 'c' && w[i][1] == 'd' && w[i][2] >= '1' && w[i][2] <= '9' && !w[i][3])) return 1; return 0; }
/* how well a subtitle's file name matches the stream's: the share of the stream's words it has, its release group
   counting extra; another episode or one disc of several: no match */
static float name_score(const char *file, const char *rel) {
  if (!file[0] || !rel[0]) return 0;
  char a[48][24], b[48][24]; int na = name_words(file, a, 48), nb = name_words(rel, b, 48); if (!na || !nb) return 0;
  int s1, e1, s2, e2; if (ep_tag(a, na, &s1, &e1) && ep_tag(b, nb, &s2, &e2) && (s1 != s2 || e1 != e2)) return 0;
  if (has_cd(b, nb) && !has_cd(a, na)) return 0;
  int common = 0; for (int i = 0; i < na; i++) for (int k = 0; k < nb; k++) if (!strcmp(a[i], b[k])) { common++; break; }
  const char *dash = strrchr(file, '-'); float bonus = 0;
  if (dash) { char g[2][24]; if (name_words(dash + 1, g, 2) >= 1) for (int k = 0; k < nb; k++) if (!strcmp(g[0], b[k])) bonus = .25f; }
  float sc = (float)common / na + bonus; return sc > 1 ? 1 : sc;
}
static int cmp_cand(const void *a, const void *b) { float x = ((const RefCand *)a)->score, y = ((const RefCand *)b)->score; return x < y ? 1 : x > y ? -1 : 0; }
/* the kind of release a name says: 1 Blu-ray, 2 web, 3 DVD, 4 a recording in the cinema (CAM, TS, screener), 0 unknown */
static int rel_kind(const char *name) {
  char w[48][24]; int n = name_words(name, w, 48), k = 0;
  for (int i = 0; i < n; i++) { const char *x = w[i];
    if (!strcmp(x, "cam") || !strcmp(x, "hdcam") || !strcmp(x, "camrip") || !strcmp(x, "ts") || !strcmp(x, "hdts") || !strcmp(x, "telesync") || !strcmp(x, "tc") || !strcmp(x, "telecine") || !strcmp(x, "scr") || !strcmp(x, "dvdscr") || !strcmp(x, "screener") || !strcmp(x, "r5")) return 4;
    if (!k && (!strcmp(x, "bluray") || !strcmp(x, "bdrip") || !strcmp(x, "brrip") || !strcmp(x, "bdremux") || !strcmp(x, "remux") || !strcmp(x, "bd") || !strcmp(x, "uhd"))) k = 1;
    if (!k && (!strcmp(x, "web") || !strcmp(x, "webrip") || !strcmp(x, "webdl") || !strcmp(x, "amzn") || !strcmp(x, "nf") || !strcmp(x, "dsnp") || !strcmp(x, "hmax") || !strcmp(x, "atvp"))) k = 2;
    if (!k && (!strcmp(x, "dvdrip") || !strcmp(x, "dvd") || !strcmp(x, "dvd5") || !strcmp(x, "dvd9"))) k = 3; }
  return k;
}
/* best first: found by the hash, carried by the stream, a close file name; then any other subtitle of this video (a
   few of them agreeing is as good), the same kind of release as the stream's first, the language of the subtitle being
   synced a little before others (the same words can vote), a release several languages were made for before a lone
   one. Cinema recordings and single discs of a split film never, unless the stream is one */
static void rank(SubSync *j) {
  int vk = rel_kind(j->q.filename); char vw[48][24]; int nv = name_words(j->q.filename, vw, 48), vcd = has_cd(vw, nv);
  for (int i = 0; i < j->nc; i++) { RefCand *c = &j->c[i]; float ns = name_score(j->q.filename, c->r.rel); int rk = rel_kind(c->r.rel);
    char w[48][24]; int nw = name_words(c->r.rel, w, 48);
    if ((rk == 4 && vk != 4) || (has_cd(w, nw) && !vcd)) { c->score = -1; continue; }
    int sib = 0; for (int k = 0; k < j->nc; k++) if (k != i && c->r.rel[0] && name_score(c->r.rel, j->c[k].r.rel) >= .8f) sib++;
    c->score = c->r.hm ? 5 + ns : c->r.own ? 4 + ns : ns >= .5f ? 3 + ns : 1 + .6f * ns + (vk && rk == vk ? .3f : 0) + (sib ? .1f : 0);
    if (j->curLang[0] && !strcmp(c->r.lang, j->curLang)) c->score += .2f;
    if (j->q.fps > 0 && c->r.fps > 0) c->score += fabs(c->r.fps / j->q.fps - 1) < .002 ? .4f : -.1f; } /* timed for the picture's frame rate: first */
  qsort(j->c, j->nc, sizeof *j->c, cmp_cand);
}
/* OpenSubtitles asked with the file's hash: the entries it found by the hash */
static void by_hash(SubSync *j) {
  SubQuery *q = &j->q; char ex[600] = "", n[32]; addon_extra_add(ex, sizeof ex, "videoHash", q->hash);
  if (q->size > 0) { snprintf(n, sizeof n, "%.0f", q->size); addon_extra_add(ex, sizeof ex, "videoSize", n); }
  char url[2400]; addon_url(url, sizeof url, "https://opensubtitles-v3.strem.io", "subtitles", q->type, q->vid, ex);
  size_t len = 0; unsigned char *b = fetch(j, url, 2u << 20, &len); if (!b) return;
  cJSON *js = cJSON_ParseWithLength((const char *)b, len); free(b); const cJSON *arr = js_arr(js, "subtitles"), *e; int found = 0;
  if (arr) cJSON_ArrayForEach(e, arr) { if (found >= 32) break; SubEntry se; memset(&se, 0, sizeof se); subs_entry_facts(&se, e); const char *u = js_str(e, "url", NULL);
    if (!se.hm || !u || strlen(u) >= sizeof se.url) continue;
    SyncRef r; memset(&r, 0, sizeof r); snprintf(r.url, sizeof r.url, "%s", u); subs_lang_canon(js_str(e, "lang", ""), r.lang, sizeof r.lang); snprintf(r.rel, sizeof r.rel, "%s", se.rel);
    snprintf(r.from, sizeof r.from, "OpenSubtitles"); r.hm = 1; r.fps = se.fps; cand_add(j, &r); found++; }
  fprintf(stderr, "[sync] OpenSubtitles by hash %s: %d subtitle(s) for this file\n", q->hash, found);
  cJSON_Delete(js);
}
/* a subtitle the same as the one being synced (one file in two lists, or two uploads of one): its times are ours */
static int same_times(SubSync *j, const Cue *c, int n) {
  int cnt = 0, hit = 0;
  for (int i = 0; i < j->nx; i++) { double v = j->x[i].s; int l = 0, h = n - 1; while (l < h) { int m = (l + h) >> 1; if (c[m].s < v) l = m + 1; else h = m; }
    cnt++; if (fabs(c[l].s - v) < 0.02 || (l > 0 && fabs(c[l - 1].s - v) < 0.02)) hit++; }
  return cnt >= 10 && hit >= cnt * 0.9;
}
/* each reference's answer; several are asked for, and the job decides once one is clearly right or two agree */
typedef struct { double f, d, r, m; int ok, local; } RefAns;
#define REF_TRIES 6
static int agree(const SubSync *j, const RefAns *a, const RefAns *b) {
  double t0, t1; x_span(j, &t0, &t1);
  if (a->local || b->local) { double l0 = j->center - REF_LOCAL, l1 = j->center + REF_LOCAL; if (l0 > t0) t0 = l0; if (l1 < t1) t1 = l1; } /* (an answer for the playhead's surroundings: compared there) */
  return !apart(a->f, a->d, b->f, b->d, t0, t1);
}
static void *ref_main(void *arg) {
  plat_thread_background(5);
  SubSync *j = arg; SyncResult r = { SYNC_NOREF, 0, 1, 0, 0 }; SubQuery *q = &j->q;
  if (j->total < 10) { r.st = SYNC_NOSUB; finish(j, r); return NULL; }
  if (!q->hash[0] && q->hashable && !j->cancel) { double sz = q->size; char h[17]; if (subs_os_hash(q->url, &sz, h)) { snprintf(q->hash, sizeof q->hash, "%s", h); q->size = sz; } }
  if (q->hash[0] && q->type[0] && q->vid[0] && !j->cancel) by_hash(j);
  rank(j);
  { int use = 0; for (int i = 0; i < j->nc; i++) use += j->c[i].score >= 0; fprintf(stderr, "[sync] %d candidate reference(s), %d usable (cinema recordings and split discs left out)\n", j->nc, use); }
  RefAns ans[REF_TRIES]; int na = 0, tried = 0, fails = 0, done = 0; double prints[REF_TRIES][3]; int np = 0;
  for (int i = 0; i < j->nc && !j->cancel && tried < REF_TRIES && !done; i++) { const RefCand *c = &j->c[i]; if (c->score < 0) break; tried++;
    fprintf(stderr, "[sync] reference %d: %s from %s, %s, rank %.2f (%s)\n", tried, c->r.lang[0] ? c->r.lang : "?", c->r.from, c->r.hm ? "found by the file's hash" : c->r.own ? "carried by the stream" : c->score >= 3 ? "file name match" : "another subtitle of this video", c->score, c->r.rel[0] ? c->r.rel : c->r.url);
    j->stage = 1; size_t len = 0; unsigned char *b = fetch(j, c->r.url, REF_BYTES, &len); if (!b) { fails++; continue; }
    Cue *cs = NULL; int n = subs_parse_body(b, len, c->r.lang, &cs); free(b);
    if (n < 20) { fprintf(stderr, "[sync] reference unreadable (%d lines)\n", n); subs_free(cs, n); fails++; continue; }
    if (same_times(j, cs, n)) { subs_free(cs, n);
      if (c->r.hm || c->r.own) { fprintf(stderr, "[sync] reference has the same times as this subtitle: it already fits the file\n"); r.st = SYNC_OK; r.d = 0; r.f = 1; r.score = 1; done = 1; break; }
      fprintf(stderr, "[sync] reference has the same times as this subtitle: skipped\n"); continue; }
    { double fp[3] = { n > 10 ? cs[10].s : 0, cs[n / 2].s, cs[n - 1].e }; int dup = 0; /* the same timing as a reference already asked (one release in two languages): not a second opinion */
      for (int q2 = 0; q2 < np && !dup; q2++) dup = fabs(prints[q2][0] - fp[0]) < .02 && fabs(prints[q2][1] - fp[1]) < .02 && fabs(prints[q2][2] - fp[2]) < .02;
      if (dup) { subs_free(cs, n); fprintf(stderr, "[sync] reference timed like one already asked: skipped\n"); tried--; continue; }
      if (np < REF_TRIES) { memcpy(prints[np++], fp, sizeof fp); } }
    if (q->fps > 0 && c->r.fps > 0 && fabs(c->r.fps / q->fps - 1) >= .002) { double k = c->r.fps / q->fps; int std = 0; /* timed for another frame rate (a 25 fps release is the film played faster): brought to this picture's */
      for (int si = 1; si < 7; si++) if (fabs(k / SCALES[si] - 1) < .003) std = 1;
      if (std) { for (int i2 = 0; i2 < n; i2++) { cs[i2].s *= k; cs[i2].e *= k; } fprintf(stderr, "[sync] reference timed for %.3f fps, the picture is %.3f: its times x%.4f\n", c->r.fps, q->fps, k); } }
    char tmp[TXT_MAX + 1]; free_r(j);
    for (int k = 0; k < n && j->nr < R_MAX; k++) { WLine *o = &j->r[j->nr++]; o->s = cs[k].s; o->e = cs[k].e; snprintf(tmp, sizeof tmp, "%s", cs[k].t ? cs[k].t : ""); words_of(tmp, o); }
    subs_free(cs, n);
    fprintf(stderr, "[sync] reference read: %d lines (%d kept)\n", n, j->nr);
    j->stage = 0; SyncResult one = { SYNC_UNSURE, 0, 1, 0, 0 }; TimingAns tm; int ok = match_ref(j, &one, &tm);
    RefAns a; memset(&a, 0, sizeof a);
    if (ok) { a.ok = 1; a.f = one.f; a.d = one.d; a.r = one.byWords ? 1 : tm.best.r; a.m = one.byWords ? 1 : tm.best.r - tm.second.r; a.local = !one.byWords && tm.best.r > 0 && tm.local; }
    else if (tm.best.r >= .35 && tm.z >= 5) { a.f = tm.best.f; a.d = round(tm.best.d * 100) / 100; a.r = tm.best.r; a.m = tm.best.r - tm.second.r; a.local = tm.local; } /* not clear alone: kept in case another agrees */
    else { fprintf(stderr, "[sync] nothing clear against reference %d\n", tried); continue; }
    if (na < REF_TRIES) ans[na++] = a;
    fprintf(stderr, "[sync] reference %d says %+.2f s x%.4f (score %.2f, margin %.2f)%s\n", tried, a.d, a.f, a.r, a.m, a.ok ? "" : ", not clear alone");
    if (a.ok && a.r >= .55 && a.m >= .15) { r.st = SYNC_OK; r.f = a.f; r.d = a.d; r.score = (float)a.r; done = 1; fprintf(stderr, "[sync] clear answer from reference %d\n", tried); break; }
    for (int q2 = 0; q2 < na - 1 && !done; q2++) if (agree(j, &ans[q2], &a)) { const RefAns *w = ans[q2].r >= a.r ? &ans[q2] : &a;
      r.st = SYNC_OK; r.f = w->f; r.d = w->d; r.score = (float)w->r; done = 1; fprintf(stderr, "[sync] references %d and %d agree: %+.2f s x%.4f\n", q2 + 1, tried, w->d, w->f); } }
  if (!done && !j->cancel) { int best = -1, clash = 0; /* the clear answers left: taken when they do not contradict each other */
    for (int i = 0; i < na; i++) if (ans[i].ok && (best < 0 || ans[i].r > ans[best].r)) best = i;
    for (int i = 0; i < na && best >= 0; i++) if (ans[i].ok && i != best && !agree(j, &ans[i], &ans[best])) clash = 1;
    if (best >= 0 && !clash) { r.st = SYNC_OK; r.f = ans[best].f; r.d = ans[best].d; r.score = (float)ans[best].r; fprintf(stderr, "[sync] one clear answer, none against it: %+.2f s x%.4f\n", r.d, r.f); }
    else if (clash) fprintf(stderr, "[sync] the references disagree\n"); }
  if (r.st == SYNC_OK) fprintf(stderr, "[sync] lined up: %+.2f s%s\n", r.d, fabs(r.f - 1) > 0.001 ? ", speed fixed" : "");
  else { r.st = na ? SYNC_UNSURE : fails && fails == tried ? SYNC_NOSUB : tried ? SYNC_UNSURE : SYNC_NOREF;
    if (!j->cancel) fprintf(stderr, "[sync] %s\n", r.st == SYNC_UNSURE ? "no reference gave a clear answer" : r.st == SYNC_NOSUB ? "the references could not be read" : "no other subtitle made for this file"); }
  finish(j, r); return NULL;
}
SubSync *subsync_start_ref(const Cue *X, int nx, double center, const SubQuery *q, const char *curUrl, const char *curLang, const SyncRef *refs, int nrefs) {
  SubSync *j = job_new(center, REF_BEHIND, REF_AHEAD, nx, R_MAX); if (!j) return NULL;
  j->range = REF_RANGE; load_x(j, X, nx); j->q = *q; snprintf(j->cur, sizeof j->cur, "%s", curUrl ? curUrl : ""); subs_lang_canon(curLang ? curLang : "", j->curLang, sizeof j->curLang);
  if (!j->q.filename[0]) { /* the file's name from its address (debrid links end with it) */
    const char *u = q->url, *e = u + strcspn(u, "?#"), *b = e; while (b > u && b[-1] != '/') b--; size_t o = 0;
    for (const char *p = b; p < e && o + 1 < sizeof j->q.filename; p++) { if (*p == '%' && p + 2 < e) { unsigned v; if (sscanf(p + 1, "%2x", &v) == 1) { j->q.filename[o++] = (char)v; p += 2; continue; } } j->q.filename[o++] = *p == '+' ? ' ' : *p; }
    j->q.filename[o] = 0; }
  for (int i = 0; i < nrefs; i++) { SyncRef r = refs[i]; subs_lang_canon(refs[i].lang, r.lang, sizeof r.lang); cand_add(j, &r); }
  j->stage = 1; start(j, ref_main); return j;
}
int subsync_stage(SubSync *j) { return j ? j->stage : 0; }
int subsync_poll(SubSync *j, SyncResult *out) {
  if (!j) return 0; pthread_mutex_lock(&j->mu); int d = j->done; if (d && out) *out = j->res; pthread_mutex_unlock(&j->mu); return d;
}
void subsync_drop(SubSync *j) { if (!j) return; j->cancel = 1; release(j); }
