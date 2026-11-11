/* Styled ASS / SSA subtitles through libass.

   The thread: feeds libass the script header once and each event once (as the subtitle reader brings them), and renders
   only when what shows changes: at the next event start or end, after a seek, a new event that shows now, a new size;
   while an event with an animation (\t, \move, \k, \fad) shows, at the picture's rate, halved while a render takes over
   20 ms (down to a quarter). Renders above 100 ms for 3 s straight give up, not counting the 5 s after opening or a
   seek while libass's caches fill: the lines are drawn plain again (asssubs_too_slow). The blurs run in NEON on ARM
   TVs (c/blur_template.h). A finished
   frame is handed over whole: its pieces copied tight (8-bit alpha, colour, place), or, when libass says only the places
   moved, just the places.

   The main thread: puts a new frame's pieces in the atlas (pieces over 1000 px are cut in tiles), keeps them while the
   frame shows and draws one tinted quad each. Nothing runs while no styled track is open.

   Fonts: the file's own (its Matroska attachments, read once from the file, at most an eighth of the memory free and
   32 MB), else the bundled Inter (Noto Sans Arabic for Arabic, Persian and Urdu tracks) for every family the script
   names. No system fonts are ever scanned. libass's caches are kept small (the weakest TVs have 512 MB). */
#include "ui/asssubs.h"
#include "ui/atlas.h"
#include "ui/gfx.h"
#include "core/net.h"
#include "platform/plat.h"
#include "platform/mem.h"
#include "libass/ass.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <time.h>
#include <stdarg.h>

#define TILE 1000
static volatile double assRenderMs; /* the renders' running average, for Stats for nerds */
double asssubs_render_ms(void) { return assRenderMs; }

typedef struct { int x, y, w, h; uint32_t color; unsigned char *bm; } Piece;   /* bm: w*h alpha, tight */
typedef struct { Piece *p; int n, posOnly; int fw, fh; } Frame;
typedef struct Inst {
  pthread_t th; pthread_mutex_t mu; pthread_cond_t cv; int dead;
  MkvSession *mkv; int track; char url[4096], lang[16];
  /* from the main thread (under mu) */
  double t, tAt; int playing; double fps; unsigned gen; int fw, fh, vw, vh; float vx, vy, vww, vhh, scale;
  /* to it */
  Frame *pending; int slow, fail;
} Inst;
static Inst *cur;                                    /* the open one (main thread) */
static struct { Piece *p; int n; AtlasSlot *s; int *ns; int fw, fh; } shown; /* the frame on screen: its pieces and their atlas slots (several per tiled piece) */
static int failSaid;

static double wall(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static void frame_free(Frame *f) { if (!f) return; if (!f->posOnly) for (int i = 0; i < f->n; i++) free(f->p[i].bm); free(f->p); free(f); }

/* ---- the file's fonts: its Attachments element, file by file, fonts only, within the cap ---- */
static int ebml_id(const unsigned char *b, int n, int o, unsigned *id) { if (o >= n) return 0; unsigned c = b[o]; int l = c & 0x80 ? 1 : c & 0x40 ? 2 : c & 0x20 ? 3 : c & 0x10 ? 4 : 0; if (!l || o + l > n) return 0; unsigned v = 0; for (int i = 0; i < l; i++) v = v * 256 + b[o + i]; *id = v; return l; }
static int ebml_size(const unsigned char *b, int n, int o, long long *sz) { if (o >= n) return 0; unsigned c = b[o]; int l = 1; unsigned m = 0x80; while (l <= 8 && !(c & m)) { l++; m >>= 1; } if (l > 8 || o + l > n) return 0; long long v = c & (m - 1); for (int i = 1; i < l; i++) v = v * 256 + b[o + i]; *sz = v; return l; }
static int read_at(NetRange *r, const char *url, long long at, long len, unsigned char **out, long *got) {
  long long total; int st; *out = NULL; *got = 0; return net_range_get(r, url, at, len, out, got, &total, &st, 15000) == NR_OK && *got > 0;
}
static int is_font(const char *name, const char *mime) {
  const char *e = strrchr(name, '.');
  return strstr(mime, "font") || strstr(mime, "truetype") || strstr(mime, "opentype") || (e && (!strcasecmp(e, ".ttf") || !strcasecmp(e, ".otf") || !strcasecmp(e, ".ttc")));
}
static int load_fonts(Inst *A, ASS_Library *lib, long long at) {
  long long cap = 32ll << 20; int fm = plat_mem_free_mb(); if (fm > 0 && (long long)fm * (1 << 20) / 8 < cap) cap = (long long)fm * (1 << 20) / 8;
  NetRange *r = net_range_new(); if (!r) return 0; unsigned char *b; long n; int fonts = 0; long long used = 0;
  if (!read_at(r, A->url, at, 16, &b, &n)) { net_range_free(r); return 0; }
  unsigned id; long long sz; int il = ebml_id(b, (int)n, 0, &id), sl = il ? ebml_size(b, (int)n, il, &sz) : 0; free(b);
  if (!il || id != 0x1941A469 || !sl) { net_range_free(r); return 0; }
  long long p = at + il + sl, end = p + sz;
  while (p < end && !A->dead) {
    if (!read_at(r, A->url, p, 4096, &b, &n)) break; /* an AttachedFile's head: name, type, then its data */
    int al = ebml_id(b, (int)n, 0, &id), as = al ? ebml_size(b, (int)n, al, &sz) : 0; if (!al || !as) { free(b); break; }
    long long fileEnd = p + al + as + sz; char name[256] = "", mime[96] = ""; long long dataAt = -1, dataLen = 0;
    if (id == 0x61A7) for (int o = al + as; o < n; ) { unsigned cid; long long cs; int cl = ebml_id(b, (int)n, o, &cid), csl = cl ? ebml_size(b, (int)n, o + cl, &cs) : 0; if (!cl || !csl) break;
      int d = o + cl + csl;
      if (cid == 0x465C) { dataAt = p + d; dataLen = cs; break; }
      if (d + cs > n) break;
      if (cid == 0x466E) snprintf(name, sizeof name, "%.*s", (int)(cs < 255 ? cs : 255), b + d); else if (cid == 0x4660) snprintf(mime, sizeof mime, "%.*s", (int)(cs < 95 ? cs : 95), b + d);
      o = d + (int)cs; }
    free(b);
    if (dataAt > 0 && dataLen > 0 && dataLen < (24 << 20) && is_font(name, mime) && used + dataLen <= cap) {
      unsigned char *f; long got; if (read_at(r, A->url, dataAt, (long)dataLen, &f, &got) && got == dataLen) { ass_add_font(lib, name, (char *)f, (int)dataLen); used += dataLen; fonts++; } free(f); /* (libass keeps its own copy) */
    }
    if (fileEnd <= p) break; p = fileEnd;
  }
  net_range_free(r);
  fprintf(stderr, "[ass] %d font(s) from the file, %.1f MB (at most %.0f MB here)\n", fonts, used / 1048576.0, cap / 1048576.0);
  return fonts;
}

/* ---- the thread ---- */
static void ass_log(int level, const char *fmt, va_list va, void *ud) { (void)ud; if (level > 2) return; char b[256]; vsnprintf(b, sizeof b, fmt, va); fprintf(stderr, "[ass] libass: %s\n", b); } /* errors and warnings only */
static void feed(void *ud, const char *ev, double s, double e) { if (getenv("SB_ASS_EVLOG")) fprintf(stderr, "[ass] event %.2f-%.2f %.60s\n", s, e, ev); ass_process_chunk((ASS_Track *)ud, (char *)ev, (int)strlen(ev), (long long)llround(s * 1000), (long long)llround((e - s) * 1000)); }
/* the next moment what shows can change (an event starting or ending), and whether an animated one shows now */
/* an event animates now: \t, \move and karaoke over its whole time, a plain \fad(in,out) only while it fades (the rest of
   it is still: the fade's end and the fade-out's start count as changes) */
static int animates(const ASS_Event *e, long long now, long long *edge) {
  const char *x = e->Text; if (!x) return 0;
  if (strstr(x, "\\t(") || strstr(x, "\\move") || strstr(x, "\\k") || strstr(x, "\\K") || strstr(x, "\\fade(")) return 1;
  const char *f = strstr(x, "\\fad("); if (!f) return 0;
  int a = 0, b = 0; if (sscanf(f + 5, "%d , %d", &a, &b) < 1 && sscanf(f + 5, "%d,%d", &a, &b) < 1) return 1;
  long long in = e->Start + a, out = e->Start + e->Duration - b;
  if (now < in || now >= out) return 1;
  *edge = in > now ? in : out; return 0;
}
static long long next_change(ASS_Track *t, long long now, int *anim) {
  long long nx = -1; *anim = 0;
  for (int i = 0; i < t->n_events; i++) { ASS_Event *e = &t->events[i]; long long s = e->Start, en = e->Start + e->Duration;
    if (s > now) { if (nx < 0 || s < nx) nx = s; }
    else if (en > now) { long long edge = -1; if (nx < 0 || en < nx) nx = en; if (!*anim && animates(e, now, &edge)) *anim = 1; if (edge > now && edge < nx) nx = edge; } }
  return nx;
}
static void add_bundled_font(ASS_Library *lib, const char *path, const char *name) {
  FILE *fp = fopen(path, "rb"); if (!fp) return;
  if (fseek(fp, 0, SEEK_END) == 0) {
    long sz = ftell(fp);
    if (sz > 0 && sz < (8 << 20) && fseek(fp, 0, SEEK_SET) == 0) {
      char *buf = malloc((size_t)sz);
      if (buf && (long)fread(buf, 1, (size_t)sz, fp) == sz) ass_add_font(lib, name, buf, (int)sz);
      free(buf);
    }
  }
  fclose(fp);
}
static void *run(void *arg) {
  Inst *A = arg; plat_thread_background(5); /* (below the drawing and the file reader: at normal priority its heavy renders kept the reader of the lines it draws waiting) */
  char interPath[1200], notoPath[1200];
  snprintf(interPath, sizeof interPath, "%s/fonts/InterVariable.ttf", plat_asset_dir());
  snprintf(notoPath, sizeof notoPath, "%s/fonts/NotoSansArabic.ttf", plat_asset_dir());
  int ar = !strncasecmp(A->lang, "ar", 2) || !strncasecmp(A->lang, "fa", 2) || !strncasecmp(A->lang, "per", 3) || !strncasecmp(A->lang, "ur", 2);
  /* Dual-font setup: libass checks default_family (with check_glyph) first, then falls back to default_font path.
     Registering the primary bundled font in ASS_Library and setting the companion bundled font as default_font ensures
     BOTH Latin (Inter Variable) and Arabic (Noto Sans Arabic) glyphs render even on 'und'/untagged MKV ASS tracks. */
  const char *font = ar ? interPath : notoPath;
  const char *fam  = ar ? "Noto Sans Arabic" : "Inter Variable";
  ASS_Library *lib = ass_library_init(); if (lib) ass_set_message_cb(lib, ass_log, NULL);
  if (lib) add_bundled_font(lib, ar ? notoPath : interPath, ar ? "NotoSansArabic.ttf" : "InterVariable.ttf");
  ASS_Renderer *rd = lib ? ass_renderer_init(lib) : NULL; ASS_Track *tr = lib ? ass_new_track(lib) : NULL;
  if (!rd || !tr) { fprintf(stderr, "[ass] libass would not start\n"); pthread_mutex_lock(&A->mu); A->fail = 1; pthread_mutex_unlock(&A->mu); goto out; }
  ass_set_cache_limits(rd, 800, 12); ass_set_hinting(rd, ASS_HINTING_NONE); ass_set_shaper(rd, ASS_SHAPING_COMPLEX);
  int fontsDone = 0; long long attach = -1; unsigned cursor = 0, gen = ~0u; int haveHeader = 0, fw = 0, fh = 0, vw = 0, vh = 0; float scale = 0;
  long long nextAt = -1, lastNow = -1; int anim = 0; double slowSince = 0, warmAt = wall(); unsigned warmGen = ~0u; int level = 0;
  struct { int n, frames; double sum, max, at; } stat = { 0, 0, 0, 0, wall() }; /* a line in the log every 30 s while it renders */
  for (;;) {
    pthread_mutex_lock(&A->mu);
    if (A->dead) { pthread_mutex_unlock(&A->mu); break; }
    double t = A->t + (A->playing ? wall() - A->tAt : 0), fps = A->fps > 1 ? A->fps : 24; int playing = A->playing; unsigned g = A->gen;
    int nfw = A->fw, nfh = A->fh, nvw = A->vw, nvh = A->vh; float nsc = A->scale, vx = A->vx, vy = A->vy, vww = A->vww, vhh = A->vhh;
    int fed = 0;
    if (!haveHeader && mkv_listed(A->mkv)) { mkv_style(A->mkv, A->track, 1); char *h = mkv_track_header(A->mkv, A->track); if (h) { ass_process_codec_private(tr, h, (int)strlen(h)); free(h); } haveHeader = 1; attach = mkv_attachments_at(A->mkv); }
    if (haveHeader) fed = mkv_events_since(A->mkv, A->track, &cursor, feed, tr); /* (under the reader's own lock, quick) */
    pthread_mutex_unlock(&A->mu);
    if (!haveHeader || nfw <= 0) { pthread_mutex_lock(&A->mu); if (!A->dead) { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); ts.tv_nsec += 200000000; if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; } pthread_cond_timedwait(&A->cv, &A->mu, &ts); } pthread_mutex_unlock(&A->mu); continue; }
    if (!fontsDone) { fontsDone = 1; ass_set_fonts(rd, font, fam, ASS_FONTPROVIDER_NONE, NULL, 0); /* first lines in the bundled font at once */
      if (attach >= 0 && load_fonts(A, lib, attach)) { ass_set_fonts(rd, font, fam, ASS_FONTPROVIDER_NONE, NULL, 1); gen = ~0u; } }
    int resized = nfw != fw || nfh != fh || nvw != vw || nvh != vh || nsc != scale;
    if (resized) { fw = nfw; fh = nfh; vw = nvw; vh = nvh; scale = nsc; ass_set_frame_size(rd, fw, fh); if (vw > 0 && vh > 0) ass_set_storage_size(rd, vw, vh);
      /* the picture's own rectangle inside the frame: the script is laid out over it */
      float k = fw / 1920.0f; int l = (int)(vx * k), tp = (int)(vy * (fh / 1080.0f)), rr = fw - (int)((vx + vww) * k), bb = fh - (int)((vy + vhh) * (fh / 1080.0f));
      ass_set_margins(rd, tp > 0 ? tp : 0, bb > 0 ? bb : 0, l > 0 ? l : 0, rr > 0 ? rr : 0); ass_set_use_margins(rd, 0); ass_set_font_scale(rd, scale > 0 ? scale : 1); }
    long long now = llround(t * 1000);
    int jumped = lastNow >= 0 && now < lastNow - 50; /* (seeks come as a new gen) */
    int force = g != gen || resized || jumped || lastNow < 0, need;
    if (force || fed) need = 1;                                                         /* (a new event may show now) */
    else if (!playing) need = 0;
    else if (anim) need = now >= lastNow + ((long long)(1000 / fps) << level) - 2;       /* an animation: the picture's rate */
    else need = nextAt >= 0 && now >= nextAt;                                           /* the next start or end */
    if (need) {
      int changed = 0; double t0 = wall();
      ASS_Image *im = ass_render_frame(rd, tr, now, &changed);
      { static int slowDev = -1; if (slowDev < 0) slowDev = getenv("SB_ASS_SLOW") ? atoi(getenv("SB_ASS_SLOW")) : 0; if (slowDev) { struct timespec z = { 0, slowDev * 1000000L }; nanosleep(&z, NULL); } } /* development: SB_ASS_SLOW=<ms> per render */
      double ms = (wall() - t0) * 1000;
      if (g != gen || resized) changed = 2;
      gen = g; lastNow = now; nextAt = next_change(tr, now, &anim);
      if (changed) {
        Frame *f = calloc(1, sizeof *f); int cnt = 0; for (ASS_Image *i = im; i; i = i->next) if (i->w > 0 && i->h > 0) cnt++;
        f->p = calloc(cnt ? cnt : 1, sizeof *f->p); f->posOnly = changed == 1; f->fw = fw; f->fh = fh;
        for (ASS_Image *i = im; i; i = i->next) { if (i->w <= 0 || i->h <= 0) continue; Piece *p = &f->p[f->n++]; p->x = i->dst_x; p->y = i->dst_y; p->w = i->w; p->h = i->h; p->color = i->color;
          if (!f->posOnly) { p->bm = malloc((size_t)i->w * i->h); if (p->bm) for (int y = 0; y < i->h; y++) memcpy(p->bm + (size_t)y * i->w, i->bitmap + (size_t)y * i->stride, i->w); } }
        pthread_mutex_lock(&A->mu);
        if (A->pending && A->pending->posOnly == 0 && f->posOnly) { /* the last whole frame was not taken yet: its places move, it stays whole */
          if (A->pending->n == f->n) for (int i = 0; i < f->n; i++) { A->pending->p[i].x = f->p[i].x; A->pending->p[i].y = f->p[i].y; }
          frame_free(f); }
        else { frame_free(A->pending); A->pending = f; }
        pthread_mutex_unlock(&A->mu);
      }
      { double rm = (wall() - t0) * 1000; assRenderMs = assRenderMs > 0 ? assRenderMs * 0.9 + rm * 0.1 : rm; } /* (Stats for nerds) */
      stat.n++; stat.sum += (wall() - t0) * 1000; if ((wall() - t0) * 1000 > stat.max) stat.max = (wall() - t0) * 1000; if (changed) stat.frames++;
      if (wall() - stat.at > (getenv("SB_ASS_NOFAIL") ? 10 : 30) && stat.n) { fprintf(stderr, "[ass] %d renders (%d changed) in %.0f s: %.1f ms on average, %.1f at most, %d events, rate 1/%d\n", stat.n, stat.frames, wall() - stat.at, stat.sum / stat.n, stat.max, tr->n_events, 1 << level); stat.n = stat.frames = 0; stat.sum = stat.max = 0; stat.at = wall(); }
      if (anim && ms > 20 && level < 2) level++; else if (!anim || ms < 8) level = 0; /* half the rate while renders are slow */
      { static int keep = -1; if (keep < 0) keep = getenv("SB_ASS_NOFAIL") != NULL; if (keep) ms = 0; } /* (development: measuring, never giving up) */
      if (g != warmGen) { warmGen = g; warmAt = wall(); } /* (a seek or a new size: its caches warm up again) */
      if (ms > 100 && wall() - warmAt > 5) { if (!slowSince) slowSince = wall(); else if (wall() - slowSince > 3) { fprintf(stderr, "[ass] renders over 100 ms for 3 s: plain lines from now on\n"); pthread_mutex_lock(&A->mu); A->fail = 1; pthread_mutex_unlock(&A->mu); break; } }
      else slowSince = 0;
    }
    /* sleep until the next change (or the next animation step), a new event, or a nudge from the main thread */
    double wait = 0.25;
    if (playing) { long long due = anim ? lastNow + ((long long)(1000 / fps) << level) : nextAt; if (due >= 0) { double w = (due - now) / 1000.0 + 0.0005; if (w < wait) wait = w; } }
    if (!need && wait < 0.002) wait = 0.002; /* (never a spin while the moment comes) */
    if (wait > 0.001) { pthread_mutex_lock(&A->mu); if (!A->dead && A->gen == gen) { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); long long ns = ts.tv_nsec + (long long)(wait * 1e9); ts.tv_sec += ns / 1000000000; ts.tv_nsec = ns % 1000000000; pthread_cond_timedwait(&A->cv, &A->mu, &ts); } pthread_mutex_unlock(&A->mu); }
  }
out:
  if (tr) ass_free_track(tr); if (rd) ass_renderer_done(rd); if (lib) ass_library_done(lib);
  pthread_mutex_lock(&A->mu); int gone = A->dead; pthread_mutex_unlock(&A->mu);
  if (gone) { frame_free(A->pending); pthread_mutex_destroy(&A->mu); pthread_cond_destroy(&A->cv); free(A); } /* (else the main thread frees it when it closes) */
  else { pthread_mutex_lock(&A->mu); A->dead = 2; pthread_mutex_unlock(&A->mu); }
  return NULL;
}

/* ---- the main thread ---- */
static void shown_clear(void) {
  for (int i = 0; i < shown.n; i++) { for (int k = 0; k < shown.ns[i]; k++) atlas_release(&shown.s[i * 4 + k]); free(shown.p[i].bm); }
  free(shown.p); free(shown.s); free(shown.ns); memset(&shown, 0, sizeof shown);
}
static void upload(int i) { /* a piece into the atlas: whole, or as tiles of up to TILE px (at most 2x2: bigger is cut short) */
  Piece *p = &shown.p[i]; shown.ns[i] = 0; if (!p->bm) return;
  int tx = (p->w + TILE - 1) / TILE, ty = (p->h + TILE - 1) / TILE; if (tx > 2) tx = 2; if (ty > 2) ty = 2;
  for (int y = 0; y < ty; y++) for (int x = 0; x < tx; x++) {
    int w = p->w - x * TILE, h = p->h - y * TILE; if (w > TILE) w = TILE; if (h > TILE) h = TILE; if (w <= 0 || h <= 0) continue;
    unsigned char *b = p->bm; unsigned char *tmp = NULL;
    if (tx > 1 || ty > 1) { tmp = malloc((size_t)w * h); if (!tmp) continue; for (int r = 0; r < h; r++) memcpy(tmp + (size_t)r * w, p->bm + (size_t)(y * TILE + r) * p->w + x * TILE, w); b = tmp; }
    AtlasSlot *s = &shown.s[i * 4 + shown.ns[i]]; if (atlas_put(b, w, h, -1, 1, s)) shown.ns[i]++; free(tmp);
  }
}
void asssubs_open(MkvSession *mkv, int n, const char *url, const char *lang) {
  asssubs_close(); failSaid = 0;
  Inst *A = calloc(1, sizeof *A); if (!A) return;
  A->mkv = mkv; A->track = n; snprintf(A->url, sizeof A->url, "%s", url ? url : ""); snprintf(A->lang, sizeof A->lang, "%s", lang ? lang : ""); A->tAt = wall(); A->scale = 1;
  pthread_mutex_init(&A->mu, NULL); pthread_cond_init(&A->cv, NULL);
  if (pthread_create(&A->th, NULL, run, A)) { pthread_mutex_destroy(&A->mu); pthread_cond_destroy(&A->cv); free(A); return; }
  pthread_detach(A->th); cur = A;
  fprintf(stderr, "[ass] styled: track %d (%s)\n", n, lang && *lang ? lang : "?");
}
void asssubs_close(void) {
  shown_clear(); if (!cur) return;
  Inst *A = cur; cur = NULL;
  pthread_mutex_lock(&A->mu); int ended = A->dead == 2; A->dead = 1; pthread_cond_signal(&A->cv); pthread_mutex_unlock(&A->mu); /* it never touches the reader after this */
  if (ended) { frame_free(A->pending); pthread_mutex_destroy(&A->mu); pthread_cond_destroy(&A->cv); free(A); }
}
int asssubs_on(void) { return cur != NULL; }
int asssubs_too_slow(void) {
  if (!cur || failSaid) return 0; pthread_mutex_lock(&cur->mu); int f = cur->fail; pthread_mutex_unlock(&cur->mu);
  if (f) failSaid = 1; return f;
}
void asssubs_tick(double t, int playing, double fps, float vx, float vy, float vw, float vh, int videoW, int videoH, float scale) {
  Inst *A = cur; if (!A) return;
  float k = gfx_scale(); int fw = (int)(1920 * k + 0.5f), fh = (int)(1080 * k + 0.5f); if (fw > 1920) { fw = 1920; fh = 1080; } /* the drawable's pixels, at most full HD */
  pthread_mutex_lock(&A->mu);
  double pred = A->t + (A->playing ? wall() - A->tAt : 0);
  int nudge = fabs(pred - t) > 0.3 || playing != A->playing || fw != A->fw || fh != A->fh || videoW != A->vw || videoH != A->vh || scale != A->scale || vx != A->vx || vy != A->vy || vw != A->vww || vh != A->vhh;
  if (fabs(pred - t) > 0.3) A->gen++; /* a seek (or a delay changed): everything again at once */
  A->t = t; A->tAt = wall(); A->playing = playing; A->fps = fps; A->fw = fw; A->fh = fh; A->vw = videoW; A->vh = videoH; A->scale = scale; A->vx = vx; A->vy = vy; A->vww = vw; A->vhh = vh;
  if (nudge) pthread_cond_signal(&A->cv);
  Frame *f = A->pending; A->pending = NULL;
  pthread_mutex_unlock(&A->mu);
  if (!f) return;
  if (f->posOnly && f->n == shown.n) { for (int i = 0; i < f->n; i++) { shown.p[i].x = f->p[i].x; shown.p[i].y = f->p[i].y; } frame_free(f); return; } /* only the places moved */
  if (f->posOnly) { frame_free(f); return; } /* (a move for a frame that never came: the next whole one follows) */
  shown_clear(); shown.n = f->n; shown.p = f->p; shown.fw = f->fw; shown.fh = f->fh; f->p = NULL; f->n = 0; free(f);
  shown.s = calloc(shown.n ? shown.n * 4 : 1, sizeof *shown.s); shown.ns = calloc(shown.n ? shown.n : 1, sizeof *shown.ns);
  for (int i = 0; i < shown.n; i++) upload(i);
}
void asssubs_draw(void) {
  if (!cur || !shown.n || shown.fw <= 0) return;
  float kx = 1920.0f / shown.fw, ky = 1080.0f / shown.fh;
  for (int i = 0; i < shown.n; i++) { Piece *p = &shown.p[i]; uint32_t c = ((uint32_t)(255 - (p->color & 0xff)) << 24) | (p->color >> 8); if (!(c >> 24)) continue;
    int tx = (p->w + TILE - 1) / TILE; if (tx > 2) tx = 2;
    for (int k = 0; k < shown.ns[i]; k++) { AtlasSlot *s = &shown.s[i * 4 + k];
      if (!atlas_valid(s)) { for (int j = 0; j < shown.ns[i]; j++) atlas_release(&shown.s[i * 4 + j]); upload(i); s = &shown.s[i * 4 + k]; if (k >= shown.ns[i] || !atlas_valid(s)) break; } /* its page was reused: in again */
      int ox = (k % tx) * TILE, oy = (k / tx) * TILE; float u0, v0, u1, v1; atlas_uv(s, &u0, &v0, &u1, &v1); atlas_touch(s);
      gfx_alpha_tex_uv(atlas_tex(s->page), (p->x + ox) * kx, (p->y + oy) * ky, s->w * kx, s->h * ky, c, u0, v0, u1, v1); }
  }
}
