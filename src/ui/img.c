#include "platform/plat.h"
#include "ui/img.h"
#include "ui/resample.h"
#include <SDL2/SDL.h>
#include "ui/gfx.h"
#include "core/net.h"
#include "core/store.h"
#include "core/tmdb.h"
#include "core/json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_GIF
#include "stb_image.h"
#include "ui/palette.h"
#include "stb_image_write.h"
#include "src/webp/decode.h"

/* the part (cx, cy, cw, ch) of the source to nw x nh: posters arrive at 500x750 for a 264x396 card and are area-averaged;
   a source smaller than its box (an episode still at 400 px on a 589 px card) is enlarged smoothly */
static unsigned char *resample(const unsigned char *src, int w, int h, float cx, float cy, float cw, float ch, int nw, int nh) {
  return sb_resample_rgba(src, w, h, cx, cy, cw, ch, nw, nh); } /* (ui/resample.c: shrinking area-averaged as before, a source smaller than its box enlarged smoothly instead of its pixels repeated) */
static unsigned char *downscale(const unsigned char *src, int w, int h, int nw, int nh) { return resample(src, w, h, 0, 0, (float)w, (float)h, nw, nh); }
/* rounded corners into the alpha (1 px of smoothing, the colour is kept so filtering leaves no dark rim) */
/* a soft blur, made once as the picture decodes (spoilers): three box passes each way, about a Gaussian of radius r */
static void box_pass(unsigned char *px, int w, int h, int r, int horiz) {
  int n = horiz ? w : h, lines = horiz ? h : w; unsigned char *tmp = malloc((size_t)n * 3); if (!tmp) return;
  for (int L = 0; L < lines; L++) {
    #define AT(i) (px + ((size_t)(horiz ? (size_t)L * w + (i) : (size_t)(i) * w + L)) * 4)
    int acc[3] = { 0, 0, 0 }, span = 2 * r + 1;
    for (int i = -r; i <= r; i++) { int k = i < 0 ? 0 : i >= n ? n - 1 : i; const unsigned char *p = AT(k); acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2]; }
    for (int i = 0; i < n; i++) { tmp[i * 3] = (unsigned char)(acc[0] / span); tmp[i * 3 + 1] = (unsigned char)(acc[1] / span); tmp[i * 3 + 2] = (unsigned char)(acc[2] / span);
      int a = i - r < 0 ? 0 : i - r, b = i + r + 1 >= n ? n - 1 : i + r + 1; const unsigned char *pa = AT(a), *pb = AT(b); for (int c = 0; c < 3; c++) acc[c] += pb[c] - pa[c]; }
    for (int i = 0; i < n; i++) { unsigned char *p = AT(i); p[0] = tmp[i * 3]; p[1] = tmp[i * 3 + 1]; p[2] = tmp[i * 3 + 2]; }
    #undef AT
  }
  free(tmp);
}
static void soften(unsigned char *px, int w, int h) { int r = w / 26 < 2 ? 2 : w / 26; for (int k = 0; k < 3; k++) { box_pass(px, w, h, r, 1); box_pass(px, w, h, r, 0); } }
static void round_corners(unsigned char *px, int w, int h, float r) {
  if (r <= 0) return; if (r > w / 2.0f) r = w / 2.0f; if (r > h / 2.0f) r = h / 2.0f;
  int n = (int)ceilf(r) + 1;
  for (int y = 0; y < n && y < h; y++) for (int x = 0; x < n && x < w; x++) {
    float dx = r - (x + 0.5f), dy = r - (y + 0.5f); if (dx < 0 || dy < 0) continue;
    float d = sqrtf(dx * dx + dy * dy) - r, k = 0.5f - d; if (k >= 1) continue; if (k < 0) k = 0;
    int xs[2] = { x, w - 1 - x }, ys[2] = { y, h - 1 - y };
    for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) { unsigned char *p = px + ((size_t)ys[j] * w + xs[i]) * 4; p[3] = (unsigned char)(p[3] * k + 0.5f); } }
}

typedef struct Img {
  char *url; unsigned hash; int w, h;           /* the cache key (a shaped variant carries its shape in it) and the requested size */
  char *src; int fit; float radius;             /* the picture's own address; fit -1 = as it is, else the shape (img_get_shaped) */
  int state;                                    /* 0 queued, 1 loading, 2 ready, 3 failed */
  unsigned char *px; int pw, ph, pch;           /* decoded pixels waiting for upload */
  GLuint tex; float aspect; int bytes;
  GLuint upTex; int upRow;                      /* a large picture uploading in bands over several frames */
  int urgent;                                   /* the picture the screen waits on (a backdrop): it goes up first, in bigger bands */
  int pin;                                      /* never evicted (title art: small, wanted at once) */
  struct Img *dnext; int inDone;                /* decoded, waiting for upload (inDone: in that list now) */
  int wantPal, palAlpha, palReady; Palette pal;       /* the colours of the picture, asked for by the accent */
  int wantTex;                                  /* someone draws it (a picture only asked for its colours is never uploaded) */
  unsigned lastUse;
  int fails; double failAt; int gone;           /* failed loads so far, and when the last one failed: asked again, it tries once more after a wait; gone: the host said it does not exist (never asked again) */
  double askAt, evictAt, readyAt; int shown; int pfOnly; unsigned seq; int saveLater; /* readyAt: when its decode was done (an on-screen one may wait a moment for the ones before it) */ /* saveLater: its small copy is written once the decode slot is free (corners after) */ /* pfOnly: only a look-ahead asked for it so far (nothing draws it yet); seq: when it was last queued */             /* askAt: when it was first asked for (the wait until it shows is measured); evictAt: when it was last let go; shown: it has been on screen once (no fade again) */
  struct Img *next;
} Img;
#define BUCKETS 1024
static Img *table[BUCKETS];
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
/* what to load: one list, each pick the one that matters most now: the backdrop the screen waits on, then what is on
   screen (drawn in the last two frames), then title art, then what a look-ahead asked for. What is on screen goes in
   the order it was asked for, which is the order it is drawn in (rows top to bottom, cards left to right): a row fills
   in from its start, never from its end or its middle; the others go most recently asked first (a fast scroll's passed
   titles wait behind the ones it reached) */
static Img *pend[1024]; static int qn; static unsigned qSeq;
static Img *urg;   /* the one picture the screen waits on (the backdrop): taken before the list */
static unsigned frameNo;
static void q_push(Img *im, int pri) { (void)pri; im->seq = ++qSeq; pend[qn++] = im; } /* under mu; room checked by the caller */
static int q_class(const Img *im) { return im->urgent ? 4 : (!im->pfOnly && frameNo - im->lastUse <= 2) ? 3 : im->pin ? 2 : 1; }
static int q_before(const Img *a, int ca, const Img *b) { return ca == 3 ? a->seq < b->seq : a->seq > b->seq; } /* (same class: on screen, the first asked; else the newest) */
static Img *q_pop(void) { /* under mu, qn > 0 */
  int best = -1, bc = -1;
  for (int i = 0; i < qn; i++) { int c = q_class(pend[i]); if (c > bc || (c == bc && q_before(pend[i], c, pend[best]))) { best = i; bc = c; } }
  Img *im = pend[best]; pend[best] = pend[--qn]; return im; }
#define WORKERS_MAX 8
static Img *busy[WORKERS_MAX]; /* what each worker is loading now (under mu) */
/* decoded pictures waiting for upload: a list without a limit (a full array dropped them, and a dropped picture held
   its pixels and never showed: a backdrop waiting for one never changed again) */
static double mono_s(void);
static Img *doneHead; static int dn;
static void done_push(Img *im) { if (im->inDone) return; im->inDone = 1; im->readyAt = mono_s(); im->dnext = doneHead; doneHead = im; dn++; }   /* under mu; never twice (it would point to itself and the list never end) */
static Img *done_pop(void) { /* the same order as the loading: a backdrop first, then what is on screen (in its order), title art, the rest */
  Img **pp = &doneHead; int bc = -1; for (Img **q = &doneHead; *q; q = &(*q)->dnext) { int c = q_class(*q); if (c > bc || (c == bc && q_before(*q, c, *pp))) { bc = c; pp = q; } }
  if (*pp && bc == 3) { /* on screen: one ahead of it (asked before it, still loading) is waited for a moment, so a row shows in order, not wherever its pictures happen to be decoded first */
    const Img *im = *pp; int ahead = 0;
    for (int i = 0; i < qn && !ahead; i++) ahead = q_class(pend[i]) == 3 && pend[i]->seq < im->seq;
    for (int i = 0; i < WORKERS_MAX && !ahead; i++) ahead = busy[i] && busy[i] != im && q_class(busy[i]) == 3 && busy[i]->seq < im->seq;
    if (ahead && mono_s() - im->readyAt < 0.3) return NULL; } /* (never longer: a slow one does not hold the rest) */
  Img *im = *pp; if (im) { *pp = im->dnext; im->dnext = NULL; im->inDone = 0; dn--; } return im; }
#if defined(SB_WEBOS) || defined(SB_TIZEN) /* (the TVs) */
static long usedBytes, budgetBytes = 40L * 1024 * 1024; /* the TVs have little memory to spare */
#else
static long usedBytes, budgetBytes = 96L * 1024 * 1024;
#endif
static char cacheDir[1024];
/* what the pictures cost, logged every 30 s (img_log_stats): where each decode came from, what was let go and asked
   for again soon after (thrash), failures skipped, and how long a picture on screen waited from its first ask */
static long stNet, stOrig, stThumb, stEvict, stRe, stNeg; static float waitMs[128]; static int waitN;
#if defined(SB_TIZEN)
#define WORKERS 3   /* one core: fewer threads, each connection used more (a new TLS handshake there is ~1 s) */
#else
#define WORKERS 6   /* mostly waiting on the network; the decoding itself is limited to two at a time (dec_enter) */
#endif

static unsigned fnv(const char *s) { unsigned h = 2166136261u; while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; } return h; }
static Img *find(const char *url, unsigned h) { for (Img *i = table[h % BUCKETS]; i; i = i->next) if (i->hash == h && !strcmp(i->url, url)) return i; return NULL; }

/* ---- worker: fetch (cache first), decode, scale ---- */
/* the picture as it is drawn is kept too, re-encoded small (a poster at its card size is ~15 KB of JPEG, logos a
   small PNG for their transparency): the next visit decodes that in a few ms instead of the full-size original */
typedef struct { unsigned char *b; size_t n, cap; } WBuf;
static void wbuf_put(void *ctx, void *data, int size) { WBuf *w = ctx; if (w->n + size > w->cap) { w->cap = (w->n + size) * 2; w->b = realloc(w->b, w->cap); } memcpy(w->b + w->n, data, size); w->n += size; }
static void small_save(const Img *im, const unsigned char *px, int w, int h) {
  if (!strncmp(im->src, "file://", 7) || w * h > 1280 * 720) return;
  int opaque = 1; for (long i = 3, n = (long)w * h * 4; i < n; i += 4) if (px[i] != 255) { opaque = 0; break; }
  WBuf b = { 0 }; int ok;
  if (opaque) { unsigned char *rgb = malloc((size_t)w * h * 3); for (long i = 0, n = (long)w * h; i < n; i++) { rgb[i * 3] = px[i * 4]; rgb[i * 3 + 1] = px[i * 4 + 1]; rgb[i * 3 + 2] = px[i * 4 + 2]; }
    ok = stbi_write_jpg_to_func(wbuf_put, &b, w, h, 3, rgb, 90); free(rgb); }
  else ok = stbi_write_png_to_func(wbuf_put, &b, w, h, 4, px, w * 4);
  if (ok && b.n) { char key[2400]; snprintf(key, sizeof key, "sm:%dx%d:%s", im->w, im->h, im->url); cache_write(key, b.b, b.n); }
  free(b.b);
}
static int small_load(Img *im) {
  char key[2400]; snprintf(key, sizeof key, "sm:%dx%d:%s", im->w, im->h, im->url);
  size_t len = 0; unsigned char *data = cache_read(key, 14 * 86400, &len); if (!data) return 0;
  int w, h, ch; unsigned char *px = stbi_load_from_memory(data, (int)len, &w, &h, &ch, 4); free(data);
  if (!px) return 0;
  if (im->wantPal && !im->palReady) { int st = 1; while ((w / st) * (h / st) > 40000) st++; palette_of(px, w, h, im->palAlpha, st, &im->pal); im->palReady = 1; }
  if (im->fit >= 0) { round_corners(px, w, h, im->radius); im->aspect = (float)im->w / im->h; } else im->aspect = (float)w / h;
  im->px = px; im->pw = w; im->ph = h; im->pch = 4; return 1;
}
static pthread_mutex_t decMu = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t decCv = PTHREAD_COND_INITIALIZER; static int decBusy;
#if defined(SB_TIZEN)
#define DEC_SLOTS 1 /* a Samsung runs a third-party native app on one core (its cpuset, measured): two decodes only slowed each other and the drawing */
#else
#define DEC_SLOTS 2
#endif
static void dec_enter(void) { pthread_mutex_lock(&decMu); while (decBusy >= DEC_SLOTS) pthread_cond_wait(&decCv, &decMu); decBusy++; pthread_mutex_unlock(&decMu); }
static void dec_leave(void) { pthread_mutex_lock(&decMu); decBusy--; pthread_cond_signal(&decCv); pthread_mutex_unlock(&decMu); }
static void load_(Img *im);
static void decode_part(Img *im, unsigned char *data, size_t len, int fromNet, const char *key);
static double mono_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static void load(Img *im) { dec_enter(); int ok = small_load(im); dec_leave(); if (ok) __atomic_add_fetch(&stThumb, 1, __ATOMIC_RELAXED); else load_(im); }
static void load_(Img *im) {
  char key[2048]; snprintf(key, sizeof key, "img:%s", im->src);
  size_t len = 0; unsigned char *data = cache_read(key, 14 * 86400, &len);
  int fromNet = 0;
  if (!data && !strncmp(im->src, "file://", 7)) { FILE *f = fopen(im->src + 7, "rb"); if (f) { fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); if (n > 0) { data = malloc(n); len = fread(data, 1, n, f); } fclose(f); } } /* a bundled picture */
  else if (!data) { char nk[2100]; snprintf(nk, sizeof nk, "neg:%s", im->src); char np[1200];
    if (cache_file(nk, 86400, np, sizeof np)) { __atomic_add_fetch(&stNeg, 1, __ATOMIC_RELAXED); im->gone = 1; im->fails++; im->failAt = mono_s(); im->state = 3; return; } /* missing yesterday: not asked again for a day */
    int st; data = net_fetch_sync(im->src, &len, &st, 20000); fromNet = 1;
    if (!data && strstr(im->src, "images.metahub.space/")) { /* Metahub down, or without it: the same picture from TMDB, by the IMDb id in its address (kept under Metahub's) */
      const char *tt = strstr(im->src, "/tt"), *kind = strstr(im->src, "/background/") ? "backdrop_path" : strstr(im->src, "/poster/") ? "poster_path" : NULL;
      char id[16] = ""; if (tt) { size_t k = 1; while (k < 14 && tt[k] && tt[k] != '/') k++; snprintf(id, sizeof id, "%.*s", (int)(k - 1), tt + 1); }
      char path[64], url[1200], hdr[480]; snprintf(path, sizeof path, "/find/%s?external_source=imdb_id", id);
      if (kind && id[0] && tmdb_call_any(path, url, sizeof url, hdr, sizeof hdr)) { size_t fl = 0; int fs = 0; unsigned char *fb = net_fetch_sync_hdr(url, hdr[0] ? hdr : NULL, &fl, &fs, 10000);
        cJSON *j = fb ? cJSON_Parse((const char *)fb) : NULL; free(fb); const char *pp = NULL;
        const char *arr[] = { "movie_results", "tv_results" }; for (int a = 0; a < 2 && !pp; a++) { const cJSON *r = cJSON_GetArrayItem(js_arr(j, arr[a]), 0); const char *v = js_str(r, kind, ""); if (*v) pp = v; }
        if (pp) { char alt[400]; snprintf(alt, sizeof alt, "https://image.tmdb.org/t/p/%s%s", kind[0] == 'b' ? "w1280" : "w500", pp); int s2; data = net_fetch_sync(alt, &len, &s2, 20000); if (data) { st = 200; fprintf(stderr, "[img] Metahub had none: TMDB's %s for %s\n", kind[0] == 'b' ? "backdrop" : "poster", id); } }
        cJSON_Delete(j); } }
    if (!data && (st == 404 || st == 410)) { cache_write(nk, (const unsigned char *)"1", 1); im->gone = 1; } } /* (the host has no such picture: remembered, never retried) */
  if (!data || len < 64) { fprintf(stderr, "[img] failed %s\n", im->src); free(data); im->fails++; im->failAt = mono_s(); im->state = 3; return; }
  __atomic_add_fetch(fromNet ? &stNet : &stOrig, 1, __ATOMIC_RELAXED);
  dec_enter(); decode_part(im, data, len, fromNet, key); dec_leave();
  if (im->saveLater) { im->saveLater = 0; /* the small copy's encoding (tens of ms on the TV) no longer holds one of the two decode slots */
    if (im->px) { small_save(im, im->px, im->pw, im->ph); if (im->fit >= 0) round_corners(im->px, im->pw, im->ph, im->radius); } }
}
#if defined(SB_WEBOS) || defined(SB_TIZEN)
/* (from Samsung, wp-tizen c116528) the system's libjpeg-turbo (the TV's own picture loaders use it on Tizen; on webOS when the TV has it, else stb as before): a JPEG poster decoded at a half,
   quarter or eighth of its size straight from the DCT, with NEON; stb decodes it whole first (~150 ms a poster on the one core) */
#include <dlfcn.h>
static void *(*tj_init)(void); static int (*tj_destroy)(void *);
static int (*tj_hdr)(void *, const unsigned char *, unsigned long, int *, int *, int *, int *);
static int (*tj_dec)(void *, const unsigned char *, unsigned long, unsigned char *, int, int, int, int, int);
static pthread_once_t tjOnce = PTHREAD_ONCE_INIT;
static void tj_open(void) {
  const char *names[] = { "libturbojpeg.so.0", "libturbojpeg.so", NULL }; void *lib = NULL;
  for (int i = 0; names[i] && !lib; i++) lib = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
  if (lib) { tj_init = (void *(*)(void))dlsym(lib, "tjInitDecompress"); tj_destroy = (int (*)(void *))dlsym(lib, "tjDestroy");
    tj_hdr = (int (*)(void *, const unsigned char *, unsigned long, int *, int *, int *, int *))dlsym(lib, "tjDecompressHeader3");
    tj_dec = (int (*)(void *, const unsigned char *, unsigned long, unsigned char *, int, int, int, int, int))dlsym(lib, "tjDecompress2"); }
  if (!tj_init || !tj_destroy || !tj_hdr || !tj_dec) tj_init = NULL;
  fprintf(stderr, "[img] jpeg: %s\n", tj_init ? "the system's, scaled" : "built in");
}
static unsigned char *jpeg_scaled(const Img *im, const unsigned char *data, size_t len, int *w, int *h) {
  if (len < 4 || data[0] != 0xFF || data[1] != 0xD8 || im->w <= 0 || im->h <= 0) return NULL;
  pthread_once(&tjOnce, tj_open); if (!tj_init) return NULL;
  void *t = tj_init(); if (!t) return NULL;
  int w0, h0, sub, cs; unsigned char *px = NULL;
  if (!tj_hdr(t, data, (unsigned long)len, &w0, &h0, &sub, &cs) && w0 > 0 && h0 > 0) {
    int n = 1; while (n < 8 && (w0 + 2 * n - 1) / (2 * n) >= im->w && (h0 + 2 * n - 1) / (2 * n) >= im->h) n *= 2; /* (cover: both sides at least the box) */
    int sw = (w0 + n - 1) / n, sh = (h0 + n - 1) / n;
    px = malloc((size_t)sw * sh * 4);
    if (px && !tj_dec(t, data, (unsigned long)len, px, sw, sw * 4, sh, 7 /* RGBA */, 0)) { *w = sw; *h = sh; }
    else { free(px); px = NULL; } }
  tj_destroy(t); return px;
}
#endif
static void decode_part(Img *im, unsigned char *data, size_t len, int fromNet, const char *key) {
  int w, h, ch = 4; unsigned char *px;
  if (len > 12 && !memcmp(data, "RIFF", 4) && !memcmp(data + 8, "WEBP", 4)) { /* metahub serves WebP: decoded straight at the size it is shown (a third of the work for a poster) */
    int w0 = 0, h0 = 0; px = NULL;
    if (WebPGetInfo(data, len, &w0, &h0) && w0 > 0 && h0 > 0 && im->w > 0 && im->h > 0) {
      float sx = (float)im->w / w0, sy = (float)im->h / h0, sc = sx > sy ? sx : sy; /* (cover: the larger factor) */
      if (sc < 0.9f) { WebPDecoderConfig cf; if (WebPInitDecoderConfig(&cf)) { cf.options.use_scaling = 1; cf.options.scaled_width = (int)(w0 * sc + 0.999f); cf.options.scaled_height = (int)(h0 * sc + 0.999f);
          cf.output.colorspace = MODE_RGBA; if (WebPDecode(data, len, &cf) == VP8_STATUS_OK) { px = cf.output.u.RGBA.rgba; w = cf.output.width; h = cf.output.height; cf.output.is_external_memory = 1; /* (ours now: freed with free()) */ } else WebPFreeDecBuffer(&cf.output); } } }
    if (!px) px = WebPDecodeRGBA(data, len, &w, &h); }
  else {
#if defined(SB_WEBOS) || defined(SB_TIZEN)
    if (!(px = jpeg_scaled(im, data, len, &w, &h)))
#endif
    px = stbi_load_from_memory(data, (int)len, &w, &h, &ch, 4); } /* (a JPEG at a half, quarter or eighth of its size when that still covers its box) */
  if (px && fromNet) { int shownBig = !(im->fit >= 0) && (long)im->w * im->h > 1280L * 720; /* (no small copy is kept for those) */
    if (len <= 300 * 1024 || shownBig || !strncmp(im->src, "file://", 7)) cache_write(key, data, len); } /* a big original is not kept when its small copy is: a 783 KB backdrop pushed out dozens of posters */
  free(data);
  if (!px) { fprintf(stderr, "[img] undecodable %s\n", im->src); im->fails++; im->failAt = mono_s(); im->state = 3; return; }
  if (im->wantPal && !im->palReady) { int st = 1; while ((w / st) * (h / st) > 40000) st++; palette_of(px, w, h, im->palAlpha, st, &im->pal); im->palReady = 1; }
  if (im->fit >= 0) { /* the exact box: crop (or stretch), scale, round the corners (fit 10 + shape: softened too) */
    int shape = im->fit % 10; float cx = 0, cy = 0, cw = (float)w, ch = (float)h, ba = (float)im->w / im->h, ta = (float)w / h;
    if (shape > 0) { if (ta > ba) { cw = h * ba; cx = (w - cw) / 2; } else if (ta < ba) { ch = w / ba; cy = shape == 2 ? 0 : (h - ch) / 2; } }
    unsigned char *out = resample(px, w, h, cx, cy, cw, ch, im->w, im->h);
    free(px); if (!out) { im->state = 3; return; }
    if (im->fit >= 10) soften(out, im->w, im->h);
    im->saveLater = 1; /* its small copy (before the corners: they are put back on load) is made out of the decode slot */
    im->px = out; im->pw = im->w; im->ph = im->h; im->pch = 4; im->aspect = ba; return;
  }
  /* scale down to the shown size (cover: the smaller of the two scale factors keeps it covering) */
  int tw = im->w, th = im->h; float sx = (float)tw / w, sy = (float)th / h, s = sx > sy ? sx : sy;
  if (s < 1.0f) {
    int nw = (int)(w * s + 0.5f), nh = (int)(h * s + 0.5f); if (nw < 1) nw = 1; if (nh < 1) nh = 1;
    unsigned char *out = downscale(px, w, h, nw, nh);
    if (out) { free(px); px = out; w = nw; h = nh; }
  }
  im->saveLater = 1;
  im->px = px; im->pw = w; im->ph = h; im->pch = 4; im->aspect = (float)w / h;
}
static void *worker(void *a) { plat_thread_background(10); int wid = (int)(intptr_t)a;
  for (;;) {
    pthread_mutex_lock(&mu);
    while (!qn && !urg) pthread_cond_wait(&cv, &mu);
    Img *im; if (urg) { im = urg; urg = NULL; } else im = q_pop();
    if (im->state == 1 || im->inDone) { pthread_mutex_unlock(&mu); continue; } /* queued twice (urgent and in a queue): another worker has it already */
    /* asked for, then nobody asked again for 1.5 s (scrolled past): not loaded now; the next ask queues it again */
    if (!im->pin && !im->wantPal && frameNo - im->lastUse > 30) { im->state = 2; im->wantTex = 0; pthread_mutex_unlock(&mu); continue; } /* (half a second: a card a fast scroll went past is not fetched, as the reference app refuses what is off screen while it moves) */
    im->state = 1; if (wid < WORKERS_MAX) busy[wid] = im;
    pthread_mutex_unlock(&mu);
    load(im);
    pthread_mutex_lock(&mu); if (wid < WORKERS_MAX) busy[wid] = NULL;
    if (im->state != 3) im->state = 2;
    done_push(im);
    pthread_mutex_unlock(&mu);
  }
  return NULL;
}
int img_init(const char *dir) {
  snprintf(cacheDir, sizeof cacheDir, "%s", dir);
  for (int i = 0; i < WORKERS; i++) { pthread_t t; pthread_create(&t, NULL, worker, (void *)(intptr_t)i); pthread_detach(t); }
  return 1;
}
static float drawScale = 1.0f;
void img_set_draw_scale(float sc) { drawScale = sc > 0 && sc < 1 ? sc : 1.0f; }
void img_set_budget_mb(int mb) { budgetBytes = (long)mb * 1024 * 1024; }
int img_budget_mb(void) { return (int)(budgetBytes / (1024 * 1024)); }
void img_queue_stats(int *queued, int *waiting) { pthread_mutex_lock(&mu); *queued = qn; *waiting = dn; pthread_mutex_unlock(&mu); }
int img_used_mb(void) { return (int)(usedBytes / 1048576); }
void img_frame(void) { frameNo++;
  if (frameNo % 60 == 0) { /* the pictures drawn in the last few seconds (and the pinned ones): their files are never pruned */
    static char (*nm)[40]; static int cap; int n = 0;
    for (int b = 0; b < BUCKETS; b++) for (Img *i = table[b]; i; i = i->next) { if (!(i->pin || (i->tex && frameNo - i->lastUse < 240))) continue;
      if (n + 2 > cap) { int nc = cap ? cap * 2 : 256; void *g = realloc(nm, sizeof *nm * (size_t)nc); if (!g) break; nm = g; cap = nc; }
      char k[2400]; snprintf(k, sizeof k, "img:%s", i->src); cache_basename(k, nm[n++], 40); snprintf(k, sizeof k, "sm:%dx%d:%s", i->w, i->h, i->url); cache_basename(k, nm[n++], 40); }
    cache_protect_set((const char (*)[40])nm, n); } }

/* a full queue leaves the picture waiting (state 5), asked again on its next request, never failed for good */
static void enqueue_at(Img *im, int front) { pthread_mutex_lock(&mu);
  if (qn < 1024) { q_push(im, front); im->state = 0; pthread_cond_signal(&cv); } else im->state = 5;
  pthread_mutex_unlock(&mu); }
static void enqueue(Img *im) { enqueue_at(im, 0); }
static int pfMode; /* the caller is looking ahead (img_prefetch_mode): what it asks for is not on screen */
void img_prefetch_mode(int on) { pfMode = on; }
static Img *request_as(const char *key, const char *src, int w, int h, int fit, float radius) {
  unsigned hs = fnv(key); Img *im = find(key, hs);
  if (im) { im->lastUse = frameNo; if (!pfMode && im->pfOnly) { im->pfOnly = 0; if (!im->tex) im->askAt = mono_s(); } /* (wanted on screen from now: its wait counts from here) */ if (im->state == 5) enqueue(im);
    if (im->state == 3 && !im->gone && im->fails > 0 && im->fails < 6 && mono_s() - im->failAt > 1.5 * (1 << (im->fails - 1))) { fprintf(stderr, "[img] again (%d) %s\n", im->fails, im->src); enqueue(im); } /* failed (a network blip): asked for again, it tries again after 1.5, 3, 6, 12, 24 s */
    if (!im->wantTex) { im->wantTex = 1; if (im->state == 2 && !im->tex && !im->px) { if (im->evictAt > 0 && mono_s() - im->evictAt < 10) stRe++; im->askAt = mono_s(); im->state = 0; enqueue(im); } } /* it was loaded for its colours only (or let go): now load it to draw */
    return im; }
  if (pfMode && usedBytes > budgetBytes * 9 / 10) return NULL; /* a look-ahead never crowds out what is on screen */
  if (drawScale < 1) { w = (int)(w * drawScale + 0.999f); h = (int)(h * drawScale + 0.999f); radius *= drawScale; }
  im = calloc(1, sizeof *im); im->url = strdup(key); im->src = key == src ? im->url : strdup(src); im->hash = hs; im->w = w; im->h = h; im->fit = fit; im->radius = radius; im->lastUse = frameNo; im->askAt = mono_s();
  im->wantTex = 1; im->pfOnly = pfMode;
  im->next = table[hs % BUCKETS]; table[hs % BUCKETS] = im;
  enqueue(im);
  return im;
}
static Img *request(const char *url, int w, int h) { return request_as(url, url, w, h, -1, 0); }
/* cache keys are built for every label / picture in every frame: printf's float formatting was a tenth of the
   drawing thread's time on the TV */
static char *key_int(char *p, long v) { char t[24]; int n = 0; unsigned long u = v < 0 ? (unsigned long)(-v) : (unsigned long)v; do { t[n++] = (char)('0' + u % 10); u /= 10; } while (u); if (v < 0) *p++ = '-'; while (n) *p++ = t[--n]; return p; }
static long key_round(float f) { return (long)(f >= 0 ? f + 0.5f : f - 0.5f); }
static const char *shaped_key(char *b, size_t cap, const char *url, int w, int h, float radius, int fit) {
  char *q = b; q = key_int(q, w); *q++ = 'x'; q = key_int(q, h); *q++ = ':'; q = key_int(q, fit); *q++ = ':'; q = key_int(q, key_round(radius * 10)); *q++ = '|';
  size_t n = strlen(url), room = cap - (size_t)(q - b) - 1; if (n > room) n = room; memcpy(q, url, n); q[n] = 0; return b; }
GLuint img_get_shaped(const char *url, int w, int h, float radius, int fit) {
  if (!url || !*url || w <= 0 || h <= 0) return 0;
  char k[2300]; Img *im = request_as(shaped_key(k, sizeof k, url, w, h, radius, fit), url, w, h, fit, radius);
  return im ? im->tex : 0;
}
int img_shaped_loaded(const char *url, int w, int h, float radius, int fit) { char k[2300]; shaped_key(k, sizeof k, url, w, h, radius, fit); Img *im = find(k, fnv(k)); return im && im->tex; }
int img_shaped_failed(const char *url, int w, int h, float radius, int fit) { char k[2300]; shaped_key(k, sizeof k, url, w, h, radius, fit); Img *im = find(k, fnv(k)); return im && im->state == 3; }
GLuint img_get(const char *url, int w, int h, float *aspect) {
  if (!url || !*url) return 0;
  Img *im = request(url, w, h); if (!im) { if (aspect) *aspect = (float)w / h; return 0; }
  if (aspect) *aspect = im->aspect > 0 ? im->aspect : (float)w / h;
  return im->tex;
}
void img_prefetch(const char *url, int w, int h) { if (url && *url && usedBytes < budgetBytes * 85 / 100) { int pm = pfMode; pfMode = 1; request(url, w, h); pfMode = pm; } } /* looking ahead only while there is room: it never pushes out what is on screen */
/* the address a plain picture on the screen was loaded from (the player shows the very logo the page showed) */
const char *img_src_of(GLuint tex) { if (!tex) return NULL; for (int b = 0; b < BUCKETS; b++) for (Img *i = table[b]; i; i = i->next) if (i->tex == tex && i->fit < 0) return i->src; return NULL; }
/* the picture the screen is waiting on (the backdrop of the focused title): out of the stacks into the slot taken
   next, so a fast scroll's posters, asked after it, never hold it back; the one it replaces goes back on top */
void img_urgent(const char *url, int w, int h) {
  if (!url || !*url) return; Img *im = request(url, w, h); if (!im) return; im->urgent = 1; if (im->state != 0 || im == urg) return;
  pthread_mutex_lock(&mu);
  int found = 0; for (int i = 0; i < qn && !found; i++) if (pend[i] == im) { pend[i] = pend[--qn]; found = 1; }
  if (found) { if (urg && qn < 1024) q_push(urg, 0); urg = im; pthread_cond_signal(&cv); } /* (a picture already taken by a worker is not in the list: nothing to do) */
  pthread_mutex_unlock(&mu);
}
/* title art: decoded before the posters waiting in the queue, and kept */
void img_prefetch_pri(const char *url, int w, int h) {
  if (!url || !*url) return; Img *im = find(url, fnv(url));
  if (!im) { int pm = pfMode; pfMode = 0; im = request(url, w, h); pfMode = pm; if (!im) return; } /* (served before the look-ahead by being title art) */
  else im->lastUse = frameNo;
  /* the last ones kept (a logo at its drawn size is ~360 KB): as many as an eighth of the picture budget holds, 8 to 32
     (32 on a 40 MB TV was a third of its budget, and the posters on screen were let go for them) */
  static Img *pinned[32]; static int pn; int cap = (int)(budgetBytes / 8 / (360L * 1024)); cap = cap < 8 ? 8 : cap > 32 ? 32 : cap;
  if (!im->pin) { if (pn >= cap) pn = 0; if (pinned[pn]) pinned[pn]->pin = 0; pinned[pn] = im; pn = (pn + 1) % cap; im->pin = 1; }
}
/* the colours of a picture: 1 when known (out filled), 0 while it loads, -1 when it cannot be loaded */
int img_palette(const char *url, int w, int h, int alpha, Palette *out) {
  if (!url || !*url) return -1;
  Img *im = find(url, fnv(url));
  if (!im) { int pm = pfMode; pfMode = 0; im = request(url, w, h); pfMode = pm; if (!im) return 0; im->wantTex = 0; } /* only its colours: decoded, never uploaded */
  if (im->state == 3) return -1;
  if (im->palReady) { *out = im->pal; return 1; }
  if (!im->wantPal) { im->wantPal = 1; im->palAlpha = alpha;
    if ((im->state == 2 || im->tex) && !im->px) { /* already decoded without it: load again for the colours (not while its pixels wait to go up) */
      pthread_mutex_lock(&mu); if (qn < 1024) { q_push(im, 0); pthread_cond_signal(&cv); } pthread_mutex_unlock(&mu); } }
  return 0;
}
int img_failed(const char *url) { Img *im = find(url, fnv(url)); return im && im->state == 3; }
int img_loaded(const char *url) { Img *im = find(url, fnv(url)); return im && im->tex; }

/* uploads a few finished decodes per frame (each upload is a copy into GPU memory) */
/* uploads take about 3/4 MB a frame: the TV's driver rearranges every texel on the CPU as it uploads (a 1280x720
   backdrop in one go was a 25-30 ms frame), so a large picture goes up in bands of rows over a few frames and is
   only shown once complete */
#if defined(SB_TIZEN)
#define UP_BUDGET (1024L * 1024) /* a Samsung's Mali takes ~170 KB a millisecond (measured on a Tizen 5.5 set): ~6 ms a frame */
#else
#define UP_BUDGET (320L * 1024) /* this TV converts ~100 KB a millisecond for its GPU: ~3 ms a frame */
#endif
#define UP_URGENT (768L * 1024) /* a backdrop: in a few frames, as before (its fade waits for the whole picture) */
static int upLog = -1; /* SB_UPLOG: each upload's size and time (development) */
static void up_log(const char *what, long bytes, Uint64 t0) { if (upLog < 0) upLog = getenv("SB_UPLOG") != NULL; if (!upLog) return; double ms = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency(); fprintf(stderr, "[up] %s %ld KB %.1f ms\n", what, bytes / 1024, ms); }
static Img *upCur;
static void up_done(Img *im) { im->tex = im->upTex; im->upTex = 0; gfx_tex_fade_in(im->tex); im->shown = 1; /* every arrival fades in, a reload too: one let go under a fast scroll and loaded again came back over its placeholder at once (a pop); one loaded again out of sight has faded before it is seen */
  if (im->askAt > 0 && frameNo - im->lastUse <= 2) { waitMs[waitN++ & 127] = (float)((mono_s() - im->askAt) * 1000); im->askAt = 0; } /* (on screen: how long it took) */ im->bytes = im->pw * im->ph * im->pch; usedBytes += im->bytes; free(im->px); im->px = NULL; im->lastUse = frameNo; }
static long up_band(Img *im, long budget) { /* rows up to the budget, in whole 16-row blocks; what was sent */
  long row = (long)im->pw * im->pch; int rows = (int)(budget / row); rows = rows < 16 ? 16 : rows & ~15; if (im->upRow + rows > im->ph) rows = im->ph - im->upRow;
  Uint64 u0 = SDL_GetPerformanceCounter(); gfx_tex_update(im->upTex, im->px + (size_t)im->upRow * row, 0, im->upRow, im->pw, rows, im->pch); up_log("band", (long)rows * row, u0); im->upRow += rows;
  if (im->upRow >= im->ph) { up_done(im); upCur = NULL; }
  return rows * row;
}
void img_pump(void) {
  long sent = 0;
  /* a poster (540 KB) goes up whole in a frame (about 5 ms on the TV) instead of in two halves, and with the remote at
     rest about two can (a bigger burst made the next move stutter on a 2-core 1 GB box): up to ~90 a second, not 24 */
  extern float app_idle(void); long BUD = app_idle() > 0.4f ? UP_BUDGET * 5 / 2 : UP_BUDGET;
  if (upCur) { if (!upCur->wantTex) { gfx_tex_recycle(upCur->upTex, upCur->pw, upCur->ph, upCur->pch, 1); upCur->upTex = 0; free(upCur->px); upCur->px = NULL; upCur->state = 0; upCur = NULL; } else { sent += up_band(upCur, upCur->urgent ? UP_URGENT : UP_BUDGET); if (upCur) return; } }
  Uint64 t0 = SDL_GetPerformanceCounter(); double hz = (double)SDL_GetPerformanceFrequency();
  for (int n = 0; n < 6 && sent < BUD; n++) {
    if (sent && (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / hz > (BUD > UP_BUDGET ? 8.0 : 6.0)) return; /* first uploads also allocate: time counts too */
    pthread_mutex_lock(&mu); Img *im = done_pop(); pthread_mutex_unlock(&mu);
    if (!im) return;
    if (im->px && (im->tex || !im->wantTex)) { free(im->px); im->px = NULL; continue; } /* a reload for the colours only, or nobody draws it */
    if (!im->px) continue;
    long bytes = (long)im->pw * im->ph * im->pch;
    long cap = im->urgent ? UP_URGENT : BUD;
    if (bytes <= cap - sent || (!sent && bytes <= cap * 2)) { sent += bytes; Uint64 u0 = SDL_GetPerformanceCounter(); im->upTex = gfx_tex_create(im->px, im->pw, im->ph, im->pch, 1); up_log("whole", bytes, u0); up_done(im); continue; }
    if (sent) { pthread_mutex_lock(&mu); done_push(im); pthread_mutex_unlock(&mu); return; } /* next frame, first */
    { Uint64 u0 = SDL_GetPerformanceCounter(); im->upTex = gfx_tex_create(NULL, im->pw, im->ph, im->pch, 1); up_log("alloc", bytes, u0); } im->upRow = 0; upCur = im; up_band(im, cap); return;
  }
}
void img_release_unused(int keepMb) {
  long keep = keepMb > 0 ? (long)keepMb * 1024 * 1024 : budgetBytes;
  if (usedBytes <= keep) return;
  /* drop the least recently used textures until under the budget; keep what was drawn in the last few frames */
  for (int pass = 0; pass < 8 && usedBytes > keep; pass++) {
    unsigned oldest = 0xffffffffu;
    for (int b = 0; b < BUCKETS; b++) for (Img *i = table[b]; i; i = i->next) if (i->tex && !i->pin && i->lastUse < oldest) oldest = i->lastUse;
    if (oldest == 0xffffffffu) return;
    if (frameNo - oldest < (pass < 4 ? 120u : 3u)) { if (pass < 4 && usedBytes > keep + keep / 8) { pass = 3; continue; } return; } /* what was on screen in the last 2 s stays (a step back never reloads it); a real overflow (an eighth over) takes what was not drawn in the last 3 frames: before, that step was never reached once all were that recent, and a fast scroll went to 67 of 40 MB (Android TV's stress loops) */
    for (int b = 0; b < BUCKETS; b++) for (Img *i = table[b]; i; i = i->next)
      if (i->tex && !i->pin && i->lastUse <= oldest + 30) { gfx_tex_recycle(i->tex, i->pw, i->ph, i->pch, 1); i->tex = 0; usedBytes -= i->bytes; i->bytes = 0; i->evictAt = mono_s(); stEvict++;
        /* loaded again (from its small copy) only when it is asked for again: queueing it at once made a loop under
           memory pressure (decode, upload, evict, decode...) that filled the queue, and a new backdrop could not get in */
        i->state = 2; i->wantTex = 0; }
  }
}
void img_cover_uv(float ta, float ba, int top, float *u0, float *v0, float *u1, float *v1) {
  *u0 = 0; *v0 = 0; *u1 = 1; *v1 = 1;
  if (ta > ba) { float k = ba / ta; *u0 = (1 - k) / 2; *u1 = *u0 + k; }  /* texture wider than the box: crop the sides */
  else if (ta < ba) { float k = ta / ba; if (top) { *v0 = 0; *v1 = k; } else { *v0 = (1 - k) / 2; *v1 = *v0 + k; } }
}
static int fcmp(const void *a, const void *b) { float x = *(const float *)a, y = *(const float *)b; return x < y ? -1 : x > y; }
void img_log_stats(void) {
  long fn, fnew; net_fetch_stats(&fn, &fnew); int n = waitN < 128 ? waitN : 128; float w[128], p50 = 0, p95 = 0;
  if (n) { memcpy(w, waitMs, sizeof(float) * (size_t)n); qsort(w, (size_t)n, sizeof *w, fcmp); p50 = w[n / 2]; p95 = w[(n * 95) / 100 < n ? (n * 95) / 100 : n - 1]; }
  fprintf(stderr, "[img] fetches %ld (new connections %ld) · decoded from net %ld, kept original %ld, kept thumbnail %ld · failures skipped %ld · let go %ld, asked again within 10 s %ld · on-screen wait p50 %.0f ms p95 %.0f ms (%d)\n",
          fn, fnew, __atomic_load_n(&stNet, __ATOMIC_RELAXED), __atomic_load_n(&stOrig, __ATOMIC_RELAXED), __atomic_load_n(&stThumb, __ATOMIC_RELAXED), __atomic_load_n(&stNeg, __ATOMIC_RELAXED), stEvict, stRe, p50, p95, n);
}
