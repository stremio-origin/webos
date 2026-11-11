/* The Library, built for the couch: the focused title on top as on Home (its logo, its facts, two lines about it,
   over its own backdrop), the categories and the sort as one row of round buttons (the focused one opens into a
   pill with its name and count, the current category stays open on a quiet surface, the sort always shows its
   order), and a grid of large posters under them, the focused row on top. */
#include "screens/board.h"
#include "screens/btrailer.h"
#include "screens/lib.h"
#include "core/keys.h"
#include "core/i18n.h"
#include "core/json.h"
#include "core/store.h"
#include "ui/shared.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include "ui/img.h"
#include "ui/icon.h"
#include "ui/widgets.h"
#include "ui/sound.h"
#include "core/hit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stddef.h>

#define NF 6
#define NB (NF + 1)          /* the categories, then the sort */
#define COLS 5
#define MAXSHOW 200
#define LW 300.0f            /* the posters */
#define LH 450.0f
#define LSTEP 352.0f
#define LROWH 486.0f
#define GRID_X 62.0f
#define GRID_TOP 490.0f
#define ROW0_Y (GRID_TOP + 36)
#define RAIL_X 48.0f
#define RAIL_Y 410.0f
static const char *F_LB[NF] = { "All", "Movies", "Series", "In progress", "Watched", "Not watched" };
static const char *F_IC[NF] = { "lib_all", "lib_movie", "lib_series", "lib_prog", "lib_seen", "lib_new" };
static const char *S_KEY[4] = { "added", "watched", "az", "year" };
static const char *S_LB[4] = { "Recently added", "Recently watched", "A\xe2\x80\x93Z", "Newest" };

typedef struct { Item it; int year, cw, done; double added, seen; float pct, left; char poster[512]; } LibIt; /* poster: the address the card draws */
/* the head's words: its facts (they can come a moment later than the title, then they crossfade in place); mp and
   lftW: the meta line and the time left, measured when the facts are made */
typedef struct { int left; float pct; char meta[200], gen[200], bits[120], lft[80], desc[1024]; int measured; float lftW; MetaParts mp; } Facts;
#define FACTS_WORDS offsetof(Facts, measured) /* what is compared: the words, not their measures */
typedef struct { Item it; int on; char logo[512]; Facts f; } Head;
static struct {
  Tween sortDot; /* the lit dot of the sort pill (glides to the order chosen) */
  int built; unsigned ver; LibIt *all; int nall; int *items; int n; int counts[NF];
  int zone; /* 0 the rail, 1 the grid */ int tab, ri, idx;
  /* the rail: each button glides to its place, its label fades in as it opens; three pills (current, sort, focus) */
  float lw[NB]; char lbl[NB][96], cnt[NF][12]; Tween bx[NB], la[NB], bw[NB]; /* bw: each button's width, gliding as it opens into a pill with its name */ Pill pc, ps, pf; Timer1 press; Nudge nudge; int laid;
  /* the grid */
  Tween gridY, fsc, osc; int fcur, fold; Timer1 ringPop, gridIn; Tween ringA;   /* the focused poster lifts (1.05), the one left settles back */
  /* the head: what is shown, what it replaces (held until the crossfade is over), and what comes next */
  Head cur, prev; Facts fprev; Timer1 headIn, factsIn; char hLogo[512];
  char wantId[64], shownId[64]; int wantIdx, wantStage; double wantAt, logoDeadline, metaDeadline;
  LibIt remIt; int remIdx; double remAt; /* one just taken out: it fades and shrinks where it was while the ones after move back a place */
} L;

/* ---- the data ---- */
static double iso_ms(const cJSON *v) {
  if (cJSON_IsNumber(v)) return v->valuedouble;
  if (cJSON_IsString(v) && v->valuestring) { struct tm tm; memset(&tm, 0, sizeof tm); int ms = 0;
    if (sscanf(v->valuestring, "%d-%d-%dT%d:%d:%d.%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec, &ms) >= 6) { tm.tm_year -= 1900; tm.tm_mon -= 1; return (double)timegm(&tm) * 1000 + ms; } }
  return 0;
}
static void load(void) {
  free(L.all); L.all = NULL; L.nall = 0; cJSON *lib = store_node("lib"); if (!cJSON_IsObject(lib)) return;
  int cap = cJSON_GetArraySize(lib); if (!cap) return; L.all = calloc(cap, sizeof *L.all);
  const cJSON *x; cJSON_ArrayForEach(x, lib) {
    if (js_bool(x, "removed", 0) || !js_str(x, "name", "")[0]) continue;
    LibIt *o = &L.all[L.nall++]; Item *it = &o->it; const cJSON *st = js_obj(x, "state");
    const char *ty = js_str(x, "type", "movie");
    snprintf(it->id, sizeof it->id, "%s", js_str(x, "_id", "")); snprintf(it->type, sizeof it->type, "%s", *ty ? ty : "movie");
    snprintf(it->name, sizeof it->name, "%s", js_str(x, "name", "")); snprintf(it->poster, sizeof it->poster, "%s", js_str(x, "poster", ""));
    snprintf(it->background, sizeof it->background, "%s", js_str(x, "background", "")); snprintf(it->logo, sizeof it->logo, "%s", js_str(x, "logo", ""));
    const char *vid = js_str(st, "video_id", ""); snprintf(it->videoId, sizeof it->videoId, "%s", *vid ? vid : it->id);
    o->year = atoi(js_str(x, "year", "")); if (!o->year) o->year = atoi(js_str(x, "releaseInfo", ""));
    o->added = iso_ms(cJSON_GetObjectItemCaseSensitive(x, "_ctime")); if (!o->added) o->added = iso_ms(cJSON_GetObjectItemCaseSensitive(x, "_mtime"));
    o->seen = iso_ms(cJSON_GetObjectItemCaseSensitive(st, "lastWatched"));
    CwInfo ci; o->cw = lib_cw_info(it, &ci); if (o->cw) { o->pct = ci.pct; o->left = ci.leftMin; it->progress = ci.pct; it->left = ci.leftMin; }
    o->done = lib_item_watched(it);
    sb_poster_of(it, o->poster, sizeof o->poster);
  }
}
static int lib_is_ep(const LibIt *o) {
  const char *t = o->it.type;
  if (!strcmp(t, "series") || !strcasecmp(t, "Anime Series")) return 1;
  if (!strcmp(t, "movie") || !strcasecmp(t, "Anime Movies")) return 0;
  if (sb_parse_video_id(o->it.id, o->it.videoId, NULL, NULL) > 0) return 1;
  cJSON *m = sb_meta_cached(t, o->it.id); if (m) { const cJSON *vs = js_arr(m, "videos"); if (vs) return cJSON_GetArraySize(vs) > 1; }
  cJSON *l = lib_get(o->it.id); if (l && js_str(js_obj(l, "state"), "watched", "")[0]) return 1;
  return 1; /* most type="anime" entries without cached meta yet are episodic anime series */
}
static int pass(const LibIt *o, int f) {
  switch (f) { case 0: return 1; case 1: return !lib_is_ep(o); case 2: return lib_is_ep(o);
    case 3: return o->cw; case 4: return o->done; default: return !o->done && !o->cw; }
}
static int sort_key(void) { const char *s = store_get("libSort", "added"); for (int i = 0; i < 4; i++) if (!strcmp(s, S_KEY[i])) return i; return 0; }
static int g_sk;
static int cmp(const void *a, const void *b) {
  const LibIt *x = &L.all[*(const int *)a], *y = &L.all[*(const int *)b]; double d;
  if (g_sk == 2) return strcasecmp(x->it.name, y->it.name);
  if (g_sk == 3 && y->year != x->year) return y->year - x->year;
  if (g_sk == 1 && (d = y->seen - x->seen) != 0) return d > 0 ? 1 : -1;
  d = y->added - x->added; return d > 0 ? 1 : d < 0 ? -1 : 0;
}
static void head_want(void);
static void render(void) {
  free(L.items); L.items = NULL; L.n = 0; g_sk = sort_key();
  for (int t = 0; t < NF; t++) { L.counts[t] = 0; for (int j = 0; j < L.nall; j++) if (pass(&L.all[j], t)) L.counts[t]++; snprintf(L.cnt[t], sizeof L.cnt[t], "%d", L.counts[t]); }
  int *tmp = malloc((L.nall ? L.nall : 1) * sizeof *tmp), m = 0; for (int j = 0; j < L.nall; j++) if (pass(&L.all[j], L.tab)) tmp[m++] = j;
  qsort(tmp, m, sizeof *tmp, cmp); L.n = m < MAXSHOW ? m : MAXSHOW; L.items = tmp;
  if (L.idx > L.n - 1) L.idx = L.n ? L.n - 1 : 0;
  timer_start(&L.gridIn, 0.3f + 0.12f); /* (the pill row keeps its motion: a filter applied on the move must not snap it) */ /* a new filter or order: the cards rise into place one column after another */
}
static const LibIt *at(int i) { return i >= 0 && i < L.n ? &L.all[L.items[i]] : NULL; }

/* ---- the rail ---- */
static void rail_labels(void) {
  for (int i = 0; i < NB; i++) {
    if (i < NF) { snprintf(L.lbl[i], sizeof L.lbl[i], "%s", T(F_LB[i])); L.lw[i] = text_width(L.lbl[i], 26, 600, 0) + 12 + text_width(L.cnt[i], 26, 600, 0) + 26; }
    else { snprintf(L.lbl[i], sizeof L.lbl[i], "%s", T(S_LB[sort_key()])); L.lw[i] = text_width(L.lbl[i], 26, 600, 0) + 16 + 3 * 13 + 22 + 26; } } /* (and its four dots, the current one a short pill) */
}
static int is_open(int i) { int fi = L.zone == 0 ? L.ri : -1; return i == fi || i == L.tab || i == NB - 1; }
static float btn_w(int i) { return is_open(i) ? 76 + L.lw[i] : 76; }
static void rail_paint(int snap) {
  rail_labels(); float x = 0; int fi = L.zone == 0 ? L.ri : -1; /* the bar taking the focus leaves the pill where it was, as the page does */
  float px[NB], pw[NB];
  for (int i = 0; i < NB; i++) { float w = btn_w(i); if (i == NB - 1) x = SB_W - 2 * RAIL_X - w; /* the order sits apart at the far end: it sorts, it does not filter */ px[i] = x; pw[i] = w;
    if (snap || !L.laid) { tween_set(&L.bx[i], x); tween_set(&L.bw[i], w); } else { tween_to(&L.bx[i], x, GLIDE_DUR, GLIDE_EASE); tween_to(&L.bw[i], w, GLIDE_DUR, GLIDE_EASE); }
    tween_to(&L.la[i], is_open(i) ? 1 : 0, snap ? 0 : 0.18f, EASE_IN_OUT); if (snap || !L.laid) tween_set(&L.la[i], is_open(i) ? 1 : 0);
    x += w + 16; }
  L.laid = 1;
  if (fi != L.tab) pill_to(&L.pc, px[L.tab], RAIL_Y, pw[L.tab], 76); else pill_hide(&L.pc);
  if (fi != NB - 1) pill_to(&L.ps, px[NB - 1], RAIL_Y, pw[NB - 1], 76); else pill_hide(&L.ps);
  if (fi >= 0) pill_to(&L.pf, px[fi], RAIL_Y, pw[fi], 76); else pill_hide(&L.pf);
}

/* ---- the head: the focused title, shortly after the focus rests there ---- */
static void head_facts(const Item *h, const cJSON *m, Facts *F) {
  const LibIt *o = NULL; for (int j = 0; j < L.nall; j++) if (!strcmp(L.all[j].it.id, h->id)) { o = &L.all[j]; break; }
  memset(F, 0, sizeof *F);
  if (m) { const char *rel = js_str(m, "releaseInfo", js_str(m, "year", "")), *rt = js_str(m, "runtime", ""), *ir = js_str(m, "imdbRating", ""); char irb[16];
    { const cJSON *n = cJSON_GetObjectItemCaseSensitive(m, "imdbRating"); if (cJSON_IsNumber(n) && n->valuedouble > 0) { snprintf(irb, sizeof irb, "%.1f", n->valuedouble); ir = irb; } } /* a number too */
    snprintf(F->meta, sizeof F->meta, "%s %s %s%s", rel, rt, *ir ? "IMDb " : "", ir);
    const cJSON *gs = js_arr(m, "genres"); if (gs) { int n = 0; const cJSON *g; cJSON_ArrayForEach(g, gs) { if (n >= 3 || !cJSON_IsString(g)) break; size_t l = strlen(F->gen); snprintf(F->gen + l, sizeof F->gen - l, "%s%s", n ? T(", ") : "", T(g->valuestring)); n++; } }
    snprintf(F->desc, sizeof F->desc, "%s", js_str(m, "description", "")); }
  else if (o) { char y[16] = ""; if (o->year) snprintf(y, sizeof y, "%d", o->year);
    const char *kl = (!strcasecmp(o->it.type, "anime") || !strcasecmp(o->it.type, "Anime Movies") || !strcasecmp(o->it.type, "Anime Series")) ? "Anime" : lib_is_ep(o) ? "Series" : "Movie";
    snprintf(F->bits, sizeof F->bits, "%s%s%s", T(kl), y[0] ? "  \xc2\xb7  " : "", y); }
  if (o && o->cw) { if (o->left < 1) snprintf(F->lft, sizeof F->lft, "%s", T("Less than a minute left")); else { char r[32]; int mn = (int)(o->left + 0.5f); sb_fmt_runtime(mn < 1 ? 1 : mn, r, sizeof r); snprintf(F->lft, sizeof F->lft, "%s", T1("{t} left", "t", r)); } F->left = 1; F->pct = o->pct; }
  else if (o && o->done) snprintf(F->lft, sizeof F->lft, "%s", T("Watched"));
}
/* the facts came after the title showed: the old words stay and the new ones crossfade over them (0.35 s, the page's
   'ease'), never a redraw from empty */
static void on_head_meta(cJSON *m, void *ud) { (void)ud; if (!m || SCR != SCR_LIB || !L.cur.on || strcmp(js_str(m, "id", ""), L.cur.it.id)) return;
  Facts F; head_facts(&L.cur.it, m, &F); if (!memcmp(&F, &L.cur.f, FACTS_WORDS)) return;
  L.fprev = L.cur.f; L.cur.f = F; timer_start(&L.factsIn, 0.35f); }
/* a title's details asked once: its answer is on its way (the meta call itself does not merge requests) */
static void meta_once(const Item *it) {
  static char asked[16][64]; static double askedAt[16]; static int an; double now = app_now();
  if (sb_meta_cached(it->type, it->id)) return;
  for (int i = 0; i < 16; i++) if (!strcmp(asked[i], it->id) && now - askedAt[i] < 15) return;
  snprintf(asked[an], sizeof asked[an], "%s", it->id); askedAt[an] = now; an = (an + 1) % 16;
  sb_meta(it->type, it->id, on_head_meta, NULL);
}
/* the titles around the focus, once it rests: their backdrops (at the size the backdrop draws them), logos and
   details are ready before the remote gets there, so the next one shows at once */
static void head_ahead(int idx) {
  for (int d = -2; d <= 2; d++) { const LibIt *o = at(idx + d); if (!o) continue; char bu[512];
    if (d) { const char *b = sb_bg_mid(&o->it, bu, sizeof bu); if (b && *b) img_prefetch(b, 1280, 720); }
    if (sb_is_imdb(o->it.id)) { char lu[160]; snprintf(lu, sizeof lu, "https://images.metahub.space/logo/medium/%s/img", o->it.id); img_prefetch_pri(lu, 520, 140); } else if (o->it.logo[0]) img_prefetch_pri(o->it.logo, 520, 140);
    meta_once(&o->it); }
}
/* the text art of the posters around the focus, every way it can move next, asked for at once (at the size the
   head draws it), and again every half second while it rests (kept recently used, never evicted under it) */
static void logos_ahead(int idx) {
  for (int d = -COLS - 1; d <= COLS + 1; d++) { const LibIt *o = at(idx + d); if (!o) continue;
    if (sb_is_imdb(o->it.id)) { char lu[160]; snprintf(lu, sizeof lu, "https://images.metahub.space/logo/medium/%s/img", o->it.id); img_prefetch_pri(lu, 520, 140); } else if (o->it.logo[0]) img_prefetch_pri(o->it.logo, 520, 140); }
}
static void head_want(void) {
  logos_ahead(L.zone == 1 ? L.idx : 0);
  const LibIt *o = at(L.zone == 1 ? L.idx : 0);
  if (!o) { if (L.cur.on) { L.prev = L.cur; L.cur.on = 0; timer_start(&L.headIn, 0.35f); } L.shownId[0] = L.wantId[0] = 0; L.wantStage = 0; return; }
  if (!strcmp(L.shownId, o->it.id)) { L.wantStage = 0; L.wantId[0] = 0; return; } /* already there (or on its way) */
  if (L.wantStage && !strcmp(L.wantId, o->it.id)) return;
  snprintf(L.wantId, sizeof L.wantId, "%s", o->it.id); L.wantIdx = L.items[L.zone == 1 ? L.idx : 0]; L.wantStage = 1; L.wantAt = app_now() + (L.shownId[0] ? 0.22 : 0);
}
static void head_tick(void) {
  double now = app_now();
  { static double lgAt; if (now - lgAt > 0.5) { lgAt = now; logos_ahead(L.zone == 1 ? L.idx : 0); } }
  if (L.wantStage == 1 && now >= L.wantAt) { const LibIt *o = &L.all[L.wantIdx]; snprintf(L.shownId, sizeof L.shownId, "%s", o->it.id); L.wantStage = 2;
    if (sb_is_imdb(o->it.id)) snprintf(L.hLogo, sizeof L.hLogo, "https://images.metahub.space/logo/medium/%s/img", o->it.id); else snprintf(L.hLogo, sizeof L.hLogo, "%s", o->it.logo);
    if (L.hLogo[0]) img_prefetch_pri(L.hLogo, 520, 140); L.logoDeadline = now + 0.7; L.metaDeadline = now + 0.45;
    meta_once(&o->it);
    head_ahead(L.zone == 1 ? L.idx : 0);
    char bu[512], pu[512]; bg_set(sb_bg_mid(&o->it, bu, sizeof bu), 0, sb_poster_of(&o->it, pu, sizeof pu)); } /* the backdrop keeps the old one up until the new one is decoded, then crossfades */
  if (L.wantStage == 2) { const LibIt *o = &L.all[L.wantIdx];
    cJSON *m = sb_meta_cached(o->it.type, o->it.id);
    int ready = (!L.hLogo[0] || img_loaded(L.hLogo) || img_failed(L.hLogo) || now >= L.logoDeadline) && (m || now >= L.metaDeadline); if (L.hLogo[0]) img_get(L.hLogo, 520, 140, NULL);
    if (ready) { L.wantStage = 0; L.wantId[0] = 0; if (L.hLogo[0] && !img_loaded(L.hLogo)) L.hLogo[0] = 0; /* too late: the name */
      /* the title shown so far stays and fades out while the new one fades in over it (0.35 s, the page's 'ease') */
      float k = L.headIn.on ? timer_x(&L.headIn) : 1; if (L.cur.on && k > 0.5f) L.prev = L.cur; /* (mid-crossfade: the one most seen stays) */
      L.cur.it = o->it; L.cur.on = 1; snprintf(L.cur.logo, sizeof L.cur.logo, "%s", L.hLogo); head_facts(&o->it, m, &L.cur.f); L.factsIn.on = 0;
      timer_start(&L.headIn, 0.35f); } }
}

/* ---- the screen ---- */
static void lift(int i) { if (i == L.fcur) return; L.fold = L.fcur; tween_set(&L.osc, L.fsc.v); tween_to(&L.osc, 1, 0.24f, EASE_OUT); L.fcur = i; tween_set(&L.fsc, 1); if (i >= 0) { tween_to(&L.fsc, 1.05f, 0.24f, EASE_OUT); static double last; if (focus_pops(&last)) timer_start(&L.ringPop, 0.2f); else L.ringPop.on = 0; } } /* held: no pop, the outline stays lit on the poster */
static void paint(void) { rail_paint(0); head_want();
  lift(L.zone == 1 ? L.idx : -1);
  tween_to(&L.ringA, L.zone == 1 ? 1 : 0, 0.16f, EASE_IN_OUT);
  int row = L.zone == 1 ? L.idx / COLS : 0; tween_to(&L.gridY, -row * LROWH, 0.32f, EASE_OUT); }
static void enter(const void *p) {
  (void)p; int keep = L.built && L.ver == lib_version(); if (!L.built) L.zone = 1; /* the first visit opens on the grid */ L.built = 1;
  if (!keep) { L.ver = lib_version(); load(); render(); }
  L.pc.dur = L.ps.dur = L.pf.dur = GLIDE_DUR; L.cur.on = L.cur.on && L.shownId[0]; L.prev.on = 0; L.headIn.on = L.factsIn.on = 0; rail_paint(1);
  if (!L.n) { L.zone = 0; L.ri = L.tab; }
  L.fcur = L.fold = -1; paint(); tween_set(&L.gridY, L.gridY.to);
  timer_start(&L.gridIn, 0.3f + 0.12f); if (L.cur.on) timer_start(&L.headIn, 0.35f); /* every visit: the cards rise in and the head fades up again, not only the first */
}
/* before the first visit: the titles sorted, the first two rows of posters (as drawn) and the first title's
   backdrop, logo and details */
int lib_warm(void) {
  if (SCR == SCR_LIB) return 1;
  if (!L.built) { L.zone = 1; L.built = 1; L.ver = lib_version(); load(); render(); return 0; }
  int ok = 1;
  if (img_budget_mb() < 80) { const LibIt *o = at(0); if (o) head_ahead(0); return !o || sb_meta_cached(o->it.type, o->it.id) != NULL; } /* a small picture budget: the data only (see board_warm) */
  for (int i = 0; i < COLS * 2 && i < L.n; i++) { const char *src = at(i)->poster; if (!src[0]) continue;
    img_get_shaped(src, (int)LW, (int)LH, CARD_R, 0); if (!img_shaped_loaded(src, (int)LW, (int)LH, CARD_R, 0) && !img_shaped_failed(src, (int)LW, (int)LH, CARD_R, 0)) ok = 0; }
  const LibIt *o = at(0); if (!o) return ok;
  char bu[512]; const char *b = sb_bg_mid(&o->it, bu, sizeof bu); if (b && *b) { img_get(b, 1280, 720, NULL); if (!img_loaded(b) && !img_failed(b)) ok = 0; }
  head_ahead(0); return ok && sb_meta_cached(o->it.type, o->it.id) != NULL;
}
void lib_from_bar(void) { L.zone = 0; L.ri = L.tab; paint(); }
static const Item *focused(void) { const LibIt *o = L.zone == 1 ? at(L.idx) : NULL; return o ? &o->it : NULL; }
static int key(int k) {
  if (L.zone == 0) {
    int r0 = L.ri;
    if (k == K_RIGHT) { if (L.ri < NB - 1) L.ri++; else nudge_start(&L.nudge, k); }
    else if (k == K_LEFT) { if (L.ri > 0) L.ri--; else nudge_start(&L.nudge, k); }
    if (L.ri != r0) sound_play(SND_FILTER); /* the filters' own tap */
    if ((k == K_RIGHT || k == K_LEFT) && L.ri < NF && L.ri != L.tab) { L.tab = L.ri; L.idx = 0; render(); } /* a filter applies as the focus lands on it */
    else if (k == K_UP || k == K_CH_UP) { topnav_focus(); rail_paint(0); return 1; }
    else if (k == K_OK && L.ri == NB - 1) { int si = (sort_key() + 1) % 4; store_set("libSort", S_KEY[si]); sound_play(SND_FILTER); L.idx = 0; timer_start(&L.press, 0.25f); render(); paint(); return 1; } /* OK on the sort: the next order */
    else if (k == K_DOWN || k == K_CH_DOWN || (k == K_OK && L.ri < NF)) { L.ri = L.tab; if (L.n) L.zone = 1; else nudge_start(&L.nudge, K_DOWN); } /* down (or OK) into the titles */
    else if (k == K_BACK) return 0;
    paint(); return 1; }
  int i0 = L.idx;
  if (k == K_RIGHT) { if (L.idx % COLS < COLS - 1 && L.idx < L.n - 1) L.idx++; else nudge_start(&L.nudge, k); }
  else if (k == K_LEFT) { if (L.idx % COLS > 0) L.idx--; else nudge_start(&L.nudge, k); }
  else if (k == K_DOWN) { if (L.idx + COLS < L.n) L.idx += COLS; else if (L.idx / COLS < (L.n - 1) / COLS) L.idx = L.n - 1; else nudge_start(&L.nudge, k); }
  else if (k == K_CH_DOWN) {
    if (L.idx / COLS < (L.n - 1) / COLS) { int nx = L.idx + COLS * 5; L.idx = nx < L.n ? nx : L.n - 1; }
    else nudge_start(&L.nudge, K_DOWN);
  }
  else if (k == K_UP) { if (L.idx >= COLS) L.idx -= COLS; else { L.zone = 0; L.ri = L.tab; } }
  else if (k == K_CH_UP) {
    if (L.idx >= COLS) L.idx = L.idx >= COLS * 5 ? L.idx - COLS * 5 : L.idx % COLS;
    else { L.zone = 0; L.ri = L.tab; }
  }
  else if (k == K_BACK) { if (L.idx >= COLS) { L.idx = L.idx % COLS < L.n ? L.idx % COLS : 0; paint(); return 1; } return 0; }
  (void)i0; paint(); return 1;
}
static void tick(float dt) {
  if (L.built && L.ver != lib_version()) { L.ver = lib_version();
    int nw = L.n; LibIt *was = nw ? malloc(sizeof(LibIt) * (size_t)nw) : NULL; if (was) for (int i = 0; i < nw; i++) was[i] = *at(i); /* the grid as it was, to find one taken out */
    load(); render();
    if (was && L.n == nw - 1) for (int i = 0; i < nw; i++) { int in = 0; for (int j = 0; j < L.n && !in; j++) in = !strcmp(at(j)->it.id, was[i].it.id); if (!in) { L.remIt = was[i]; L.remIdx = i; L.remAt = app_now(); break; } }
    free(was); if (!L.n) { L.zone = 0; L.ri = L.tab; } paint(); } /* the library changed (the card menu, a sync) */
  for (int i = 0; i < NB; i++) { tween_tick(&L.bx[i], dt); tween_tick(&L.la[i], dt); tween_tick(&L.bw[i], dt); }
  pill_tick(&L.pc, dt); pill_tick(&L.ps, dt); pill_tick(&L.pf, dt); timer_tick(&L.press, dt); nudge_tick(&L.nudge, dt);
  tween_tick(&L.gridY, dt); tween_tick(&L.sortDot, dt); tween_tick(&L.fsc, dt); tween_tick(&L.osc, dt); tween_tick(&L.ringA, dt); timer_tick(&L.ringPop, dt); timer_tick(&L.gridIn, dt); timer_tick(&L.headIn, dt); timer_tick(&L.factsIn, dt);
  head_tick();
}
static float col_x(int c) { float x = GRID_X + c * LSTEP; return app_rtl() ? SB_W - x - LW : x; }
/* the pointer: on a category or the sort, on a poster */
static void rail_focus(int i, int b) { (void)b; int was = topNav.active; topNav.active = 0; if (was || L.zone != 0 || L.ri != i) { L.zone = 0; L.ri = i; paint(); } }
static void card_focus(int i, int b) { (void)b; int was = topNav.active; topNav.active = 0; if (was || L.zone != 1 || L.idx != i) { L.zone = 1; L.idx = i; paint(); } }
static void draw_card(const LibIt *o, float x, float y, int load) {
  const char *src = o->poster; GLuint t = src[0] && (load || img_shaped_loaded(src, (int)LW, (int)LH, CARD_R, 0)) ? img_get_shaped(src, (int)LW, (int)LH, CARD_R, 0) : 0;
  int failed = !t && (!src[0] || img_shaped_failed(src, (int)LW, (int)LH, CARD_R, 0));
  if (!failed) ph_picture(t, x, y, LW, LH, CARD_R, WHITE_A(255)); /* the waiting look until the poster has faded in over it (corners are in the picture) */
  else { ph_surface(x, y, LW, LH, CARD_R); text_draw_wrapped(o->it.name, app_rtl() ? x + LW - 16 : x + 16, y + 16, LW - 32, 6, 28, 400, WHITE_A(230), 0, ALIGN_START, 28 * 1.3f); } /* the title only when the art failed */
  if (o->done) { float bx = app_rtl() ? x + 12 : x + LW - 12 - 46; gfx_shadow(bx, y + 14, 46, 46, 23, 10, BLACK_A(140)); gfx_circle(bx + 23, y + 12 + 23, 23, acc_a(0.96f)); icon_draw("check", bx + 9, y + 12 + 9, 28, WHITE_A(255)); }
  if (o->cw) { gfx_rrect(x + 12, y + LH - 12 - 5, LW - 24, 5, 2.5f, WHITE_A(64)); float pw = (LW - 24) * clampf(o->pct, 0, 1); if (pw > 1) gfx_rrect(app_rtl() ? x + LW - 12 - pw : x + 12, y + LH - 12 - 5, pw, 5, 2.5f, WHITE_A(255)); }
}
static void facts_draw(Facts *F, float hx, float y, int al, int rtl) {
  float mw;
  if (!F->measured || ((F->meta[0] || F->gen[0]) && meta_stale(&F->mp))) { F->measured = 1; /* on the first frame they show */
    if (F->meta[0] || F->gen[0]) meta_prep(&F->mp, F->meta, F->gen, NULL, 25); F->lftW = F->lft[0] ? text_width(F->lft, 25, 600, 0) : 0; }
  if (F->meta[0] || F->gen[0]) mw = meta_draw_p(&F->mp, hx, y, WHITE_A(255), al);
  else mw = text_draw(F->bits, hx, y, 25, 600, WHITE_A(255), 0, 0, al);
  if (F->lft[0]) { float lx = mw > 0 ? 22 : 0, tw = F->lftW; text_draw(F->lft, rtl ? hx - mw - lx : hx + mw + lx, y, 25, 600, WHITE_A(255), 0, 0, al);
    if (F->left) { float bx = rtl ? hx - mw - lx - tw - 18 - 200 : hx + mw + lx + tw + 18, by = y + text_line_height(25) / 2 - 3; gfx_rrect(bx, by, 200, 6, 3, WHITE_A(64)); float pw = 200 * clampf(F->pct, 0, 1); gfx_rrect(rtl ? bx + 200 - pw : bx, by, pw, 6, 3, WHITE_A(255)); } }
  y += text_line_height(25) + 12;
  if (F->desc[0]) text_draw_wrapped(F->desc, hx, y, 1150, 2, 27, 400, WHITE_A(209), 0, al, 40);
}
/* one title in the head at alpha a; old = the facts it had a moment ago, still fading out under the new ones (fk) */
static void head_draw(Head *h, Facts *old, float a, float fk) {
  if (a <= 0.001f) return; int rtl = app_rtl(), al = rtl ? ALIGN_RIGHT : ALIGN_LEFT; float hx = rtl ? SB_W - 48 : 48;
  gfx_opacity_push(a);
  float y = 104; float asp; GLuint lt = h->logo[0] ? img_get(h->logo, 520, 140, &asp) : 0;
  if (lt) { float w = 520, hh = w / asp; if (hh > 140) { hh = 140; w = hh * asp; } { char sk[80]; snprintf(sk, sizeof sk, "logo:%s", h->it.id); sx_image(sk, lt, rtl ? hx - w : hx, y, w, hh, 0, WHITE_A(255), 0, 0, 1, 1); } y += hh; }
  else { char sk[80]; snprintf(sk, sizeof sk, "logo:%s", h->it.id); sx_text_wrapped(sk, h->it.name, hx, y, 1200, 1, 64, 800, WHITE_A(255), -1, al, 64 * 1.05f); y += 64 * 1.05f; }
  y += 18;
  if (old && fk < 1) { gfx_opacity_push(1 - fk); facts_draw(old, hx, y, al, rtl); gfx_opacity_pop(); gfx_opacity_push(fk); facts_draw(&h->f, hx, y, al, rtl); gfx_opacity_pop(); }
  else facts_draw(&h->f, hx, y, al, rtl);
  gfx_opacity_pop();
}
static void draw(void) {
  int rtl = app_rtl(); int al = rtl ? ALIGN_RIGHT : ALIGN_LEFT; float hx = rtl ? SB_W - 48 : 48;
  /* a background trailer playing: the page melts back as Home's does (its shade gone, the rail and the posters to .19, the
     head to .6, the focused poster kept clear at .85) */
  float melt = btr_melt();
  /* the shade that keeps the grid readable over the backdrop */
  if (melt > 0) gfx_opacity_push(1 - melt);
  gfx_rrect_v(0, 0, SB_W, 380, 0, RGBA(12, 11, 17, 0), RGBA(12, 11, 17, 89)); gfx_rrect_v(0, 380, SB_W, 220, 0, RGBA(12, 11, 17, 89), RGBA(12, 11, 17, 235)); gfx_rect(0, 600, SB_W, SB_H - 600, RGBA(12, 11, 17, 235));
  if (melt > 0) { gfx_opacity_pop(); gfx_opacity_push(1 - 0.4f * melt); }
  /* the head: the new title over the one it replaces, crossfading; facts that came late over the earlier ones */
  { float k = L.headIn.on ? ease_apply(EASE_CSS_EASE, timer_x(&L.headIn)) : 1;
    if (L.prev.on && k < 1) head_draw(&L.prev, NULL, 1 - k, 0);
    if (L.cur.on) head_draw(&L.cur, L.factsIn.on ? &L.fprev : NULL, k, L.factsIn.on ? ease_apply(EASE_CSS_EASE, timer_x(&L.factsIn)) : 1); }
  if (melt > 0) { gfx_opacity_pop(); gfx_opacity_push(1 - 0.81f * melt); }
  /* the rail: pills under the buttons */
  float nx = L.zone == 0 ? nudge_x(&L.nudge) : 0, ny = L.zone == 0 ? nudge_y(&L.nudge) : 0;
  #define RX(px, w) (rtl ? SB_W - RAIL_X - (px) - (w) : RAIL_X + (px))
  { const Pill *ps[2] = { &L.pc, &L.ps }; for (int k = 0; k < 2; k++) { const Pill *p = ps[k]; if (p->a.v > 0.001f) { gfx_opacity_push(p->a.v); gfx_rrect(RX(p->x.v, p->w.v), RAIL_Y, p->w.v, 76, 38, WHITE_A(41)); gfx_opacity_pop(); } } }
  if (L.pf.a.v > 0.001f) { float pk = L.press.on ? (timer_x(&L.press) < 0.36f ? 1 - 0.04f * timer_x(&L.press) / 0.36f : 0.96f + 0.04f * (timer_x(&L.press) - 0.36f) / 0.64f) : 1; /* OK: the highlight gives under the press */
    float px = RX(L.pf.x.v, L.pf.w.v) + nx; gfx_opacity_push(L.pf.a.v); gfx_scale_push(pk, pk, px, RAIL_Y + 38); gfx_rrect(px, RAIL_Y + ny, L.pf.w.v, 76, 38, WHITE_A(255)); gfx_scale_pop(); gfx_opacity_pop(); }
  for (int i = 0; i < NB; i++) { int f = L.zone == 0 && i == L.ri, open = is_open(i); float w = L.bw[i].v > 1 ? L.bw[i].v : btn_w(i), x = RX(L.bx[i].v, w), bxn = f ? nx : 0, byn = f ? ny : 0;
    hit_add(x, RAIL_Y, w, 76, rail_focus, NULL, i, 0);
    if (!open && L.la[i].v < 0.999f) gfx_circle(rtl ? x + w - 38 : x + 38, RAIL_Y + 38, 38, WHITE_A((int)(41 * (1 - L.la[i].v)))); /* a closed button: its own quiet disc */
    uint32_t fg = f ? RGB(0x15, 0x13, 0x1c) : WHITE_A(255);
    icon_draw(i < NF ? F_IC[i] : "lib_sort", (rtl ? x + w - 21 - 34 : x + 21) + bxn, RAIL_Y + 21 + byn, 34, fg);
    float ok = L.lw[i] > 1 ? (w - 76) / L.lw[i] : 0; ok = ok < 0 ? 0 : ok > 1 ? 1 : ok; /* the name shows as far as the pill has opened: never ahead of it */
    float la = L.la[i].v < ok * ok ? L.la[i].v : ok * ok; if (la > 0.001f) { gfx_opacity_push(la); gfx_clip_push(x, RAIL_Y, w - 18, 76); /* the name shows as the pill widens */ float tx = (rtl ? x + w - 66 : x + 66) + bxn, ty = RAIL_Y + 38 - text_line_height(26) / 2.0f + byn;
      float tw = text_draw(L.lbl[i], tx, ty, 26, 600, fg, 0, 0, al);
      if (i < NF) text_draw(L.cnt[i], rtl ? tx - tw - 12 : tx + tw + 12, ty, 26, 600, col_alpha(fg, 0.6f), 0, 0, al);
      else { /* the sort's orders as dots, the current one lit; a press moves the light to the next */
        float d0 = rtl ? tx - tw - 16 - 5 : tx + tw + 16 + 5, cy = RAIL_Y + 38 + byn, dir = rtl ? -1 : 1; tween_to(&L.sortDot, (float)sort_key(), GLIDE_DUR, GLIDE_EASE);
        /* the current order a short pill, the others dots; the pill flows from one into the next as it changes */
        float xx = d0 - 3.5f * dir; for (int d = 0; d < 4; d++) { float k = 1 - fabsf(d - L.sortDot.v); k = k < 0 ? 0 : k; float w = 7 + 15 * k;
          gfx_rrect(rtl ? xx - w : xx, cy - 3.5f, w, 7, 3.5f, col_mix(col_alpha(fg, 0.32f), fg, k)); xx += dir * (w + 6); } }
      gfx_clip_pop(); gfx_opacity_pop(); } }
  #undef RX
  /* the grid: the focused row on top, the rows around it */
  if (melt > 0) gfx_opacity_pop();
  gfx_clip_push(34, GRID_TOP, SB_W - 34, SB_H - GRID_TOP);
  if (!L.n) { float y = GRID_TOP + 14 + 30; int any = L.nall > 0;
    text_draw(any ? T("Nothing here yet") : T("Your library is empty."), hx, y, 30, 600, WHITE_A(217), 0, SB_W - 96, al);
    text_draw(any ? T("Try another filter") : T("Hold OK on any title (or use + Library) to add it here."), hx, y + text_line_height(30) + 10, 25, 400, WHITE_A(191), 0, SB_W - 96, al); }

  float gy = L.gridY.v; int r0 = (int)(-gy / LROWH + 0.5f), fr = L.idx / COLS, rows = (L.n + COLS - 1) / COLS;
  float gx = L.zone == 1 ? nudge_x(&L.nudge) : 0, gyn = L.zone == 1 ? nudge_y(&L.nudge) : 0, y0;
  float rk = L.remAt > 0 ? (float)((app_now() - L.remAt) / 0.4) : 1; int rem = rk < 1; if (rem) rk = 1 - powf(1 - rk, 3); else L.remAt = 0;
  if (rem) { int j = L.remIdx; float sx = col_x(j % COLS), sy = ROW0_Y + (j / COLS) * LROWH + gy, s = 0.86f + 0.14f * (1 - rk); /* the one taken out, fading and shrinking where it was */
    gfx_opacity_push(1 - rk); gfx_scale_push(s, s, sx + LW / 2, sy + LH / 2); draw_card(&L.remIt, sx, sy, 0); gfx_scale_pop(); gfx_opacity_pop(); }
  for (int pass = 0; pass < 2; pass++) /* the focused poster last, over its neighbours as it grows */
  for (int r = r0 - 1; r <= r0 + 2; r++) { if (r < 0 || r >= rows) continue; float y = ROW0_Y + r * LROWH + gy; if (y > SB_H || y + LH < GRID_TOP) continue;
    int near = r >= fr - 1 && r <= fr + 3;
    for (int c = 0; c < COLS; c++) { int i = r * COLS + c; if (i >= L.n) break; const LibIt *o = at(i); float x = col_x(c);
      if (rem && i >= L.remIdx) { int j = i + 1; float ox = col_x(j % COLS), oy = ROW0_Y + (j / COLS) * LROWH + gy; x = ox + (x - ox) * rk; y0 = oy + (y - oy) * rk; } else y0 = y; /* (one taken out before it: from its old place to its new one) */
      int isF = L.zone == 1 && i == L.idx; if (isF != pass) continue;
      hit_add(x, y0, LW, LH, card_focus, NULL, i, 0);
      float ia = 1, iy = 0; if (L.gridIn.on) { float t = L.gridIn.t - (c < 4 ? c * 0.03f : 0.12f); float k = clampf(t / 0.3f, 0, 1); k = 1 - powf(1 - k, 5); ia = k; iy = 26 * (1 - k); }
      ia *= 1 - (isF ? 0.15f : 0.81f) * melt; /* (melting with a trailer: the focused one stays clear) */
      if (ia < 1) gfx_opacity_push(ia);
      float hold = isF ? app_hold_progress() : 0; unsigned long long lk = lift_key(&L, i);
      gfx_offset_push(isF ? gx : 0, (isF ? gyn : 0) + iy); lift_begin(lk, isF, hold, x, y0, LW, LH, CARD_R);
      draw_card(o, x, y0, isF || near); if (hold > 0) hold_trace_draw(x, y0, LW, LH, CARD_R, ease_apply(EASE_IN_OUT, hold));
      { static int mk = -1; if (isF && btr_playing() && melt > 0.9f) mk = i; if (melt > 0.01f && i == mk) board_playing_mark(x, y0, LH, melt); } /* its trailer playing: the mark, as on Home (it stays on that poster and goes with it) */
      lift_end(lk, x, y0, LW, LH, CARD_R); gfx_offset_pop();
      if (ia < 1) gfx_opacity_pop(); } }
  gfx_clip_pop();
}
static int origin(float *x, float *y) {
  if (L.zone != 1 || !L.n || topNav.active) return 0; int c = L.idx % COLS, r = L.idx / COLS;
  *x = col_x(c) + LW / 2; *y = ROW0_Y + r * LROWH + L.gridY.v + LH / 2; return 1;
}
Screen libScreen = { enter, NULL, key, tick, draw, focused, NULL, NULL, origin };
