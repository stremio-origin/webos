#include "ui/xline.h"
#include "screens/board.h"
#include "ui/strip.h"
#include "screens/btrailer.h"
#include "core/notify.h"
#include "core/live.h"
#include "ui/livewall.h"
#include "screens/livepeek.h"
#include "screens/saver.h"
#include "core/keys.h"
#include "core/i18n.h"
#include "core/tmdb.h"
#include "ui/glass.h"
#include "ui/icon.h"
#include "ui/gfx.h"
#include "ui/shared.h"
#include "ui/text.h"
#include "ui/img.h"
#include "ui/widgets.h"
#include "ui/sound.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "core/hit.h"
#include "screens/player.h"
#include "screens/lib.h"

#define MAXROWS 128 /* every installed catalog has its row (the answers' tokens carry the row in 7 bits) */
#define DISC_ROW 254 /* MetaAsk sentinel for Discover's virtualized 5x3 grid */
#define ROWS_TOP 478.0f             /* a focused row ends ~80 px above the bottom edge */
#define HERO_X 48.0f
#define HERO_Y 100.0f
#define HERO_W (SB_W * 0.52f)
/* the boards' cards: fewer, bigger (about five and a half across, one row in full view) */
#define RC_W 300.0f
#define RC_H 450.0f
#define RW_W 500.0f
#define RW_H 281.0f
#define RGAP 28.0f
#define RD_D 178.0f /* a live badge's diameter */
#define DESC_PX 27
#define DESC_LH (27 * 1.5f)
#define META_PX 25

/* Discover 5x3 tile grid + single-line filter bar geometry (1920x1080) */
#define DISC_COLS 5
#define DISC_VIS_ROWS 3
#define DISC_TW 342.0f
#define DISC_TH 210.0f
#define DISC_GAP_X 24.0f
#define DISC_GAP_Y 44.0f
#define DISC_PITCH (DISC_TH + DISC_GAP_Y)
#define DISC_X0 ((SB_W - (DISC_COLS * DISC_TW + (DISC_COLS - 1) * DISC_GAP_X)) / 2.0f)
#define DISC_FILT_Y 102.0f
#define DISC_FILT_H 54.0f
#define DISC_INFO_Y 168.0f
#define DISC_GRID_TOP 224.0f
#define DISC_GRID_PAD 22.0f
#define DISC_GRID_H (SB_H - DISC_GRID_TOP - 14.0f)

/* cp: each card's resolved picture and words beside its item (cpSt 0 not yet, 1 final, 2 its translation may still
   come); pf: the ranges last fetched ahead (per call site), fetched again only when they move or a picture changes */
typedef struct { char title[96]; ItemList items; int col, wide, label, hidden, closing, round; double moveAt; float moveOff; short oldIdx[48]; /* moveAt: its cards gliding from where they were (oldIdx: each one's place before, -1 new; moveOff: the row's scroll then) */ /* round: Home's live row (badges) */ float hk; double closeAt; Tween off, a; FocusPop pop; int popCol; Nudge nudge; double locAt;
  Item *gone; short goneIdx[8]; int ngone, opening; double goneAt, openAt; float goneOff; /* cards just taken out: fading and shrinking where they stood (the row's scroll then) as the rest glide into their places; opening: a row just come, growing in */
  CardPrep *cp; unsigned char *cpSt; int ncp; unsigned cpGen, cpLg; int pf[3][2]; unsigned char pfWait[3];
  /* a catalog that pages: where its next page starts (the items its addon sent so far), one page asked at a time */
  CatalogDef def; int hasDef, skipAt, skipAlt, more, paging; double pageAt; int retries; double retryAt; } Row; /* skipAlt: the offset to try instead when the first next page brought nothing new (an addon that pages only by its page size) */
/* what the hero shows: put in while it is invisible, all together */
typedef struct { char id[SB_ID], type[24], name[256], metaName[256], metaLine[96], desc[1024], logo[512]; int wait; unsigned sig; } HeroView;
/* what the drawing measures in the view, made once per view (and language): wrap counts, the meta line */
typedef struct { unsigned ver, lg; int homeName; MetaParts mp; } HeroLay;
typedef struct {
  int kind;                       /* 0 home, 1 disc */
  Row rows[MAXROWS]; int n, rowIdx, built; unsigned gen, libVer;
  Tween hostY; Timer1 fade; Nudge nudge; int ptrHold, holdRow; /* the pointer moves the focus: the rows stay where they are (as for holdRow) until a key */
  double heroAt, fadeHold; int heroPending, fastBg; /* fadeHold: the fade-in waits (a moment at most) for the whole description */
  /* the hero, in one path: fade out, gather (details, translation, logo: 0.45 s at most), put in, fade in; anything
     better that arrives later comes in with one more fade */
  Item hero; int heroSet, heroShown, heroPhase; Tween heroA; double heroT0, laterAt, putAt; float heroFo; HeroView view; Tween logoIn; TmdbWait hw; int aheadDone;
  /* the view is compared with what it would be now only when something it reads moved: the hero item (heroVer), its
     translation, its logo's state */
  unsigned heroVer, inVer, viewVer; const void *inL, *inLn, *inLo, *inLl; char inLogo[512]; int inSt, inWait; HeroLay lay;
} Board;
enum { HP_IDLE, HP_GATHER, HP_LATER_WAIT, HP_LATER_OUT };
static Board boards[2];
static Board *cur(void) { return SCR == SCR_HOME ? &boards[0] : SCR == SCR_DISC ? &boards[1] : NULL; }
static int focusOn(Board *b) { return cur() == b && !topNav.active; }
static float card_w(const Row *r) { return r->round ? RD_D : r->wide ? RW_W : RC_W; }
static float card_h(const Row *r) { return r->round ? RD_D : r->wide ? RW_H : RC_H; }
static float row_h(const Row *r) { return (18 + text_line_height(31) + 26 + card_h(r) + 34 + (r->round ? 44 : 0)) * (r->closing || r->opening ? r->hk : 1); } /* (a live badge: its name under it) */ /* (a row closing: its height goes as the ones under it move up) */
static float step_of(const Row *r) { return card_w(r) + RGAP; }
static float row_x0(const Row *r) { (void)r; return 48; }
int board_spot_up(void) { return 0; }

/* ---- O(1) id hash helpers for Discover grid dedup ---- */
static unsigned id_hash(const char *s) {
  unsigned h = 2166136261u;
  for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
  return h ? h : 1u;
}
static int idtab_rebuild(int **tab, int *mask, const Item *items, int n, int wantCap) {
  int sz = 64; while (sz < wantCap * 2) sz <<= 1;
  int *nt = calloc((size_t)sz, sizeof *nt);
  if (!nt) return 0;
  int m = sz - 1;
  for (int i = 0; i < n; i++) {
    unsigned slot = id_hash(items[i].id) & (unsigned)m;
    while (nt[slot]) slot = (slot + 1u) & (unsigned)m;
    nt[slot] = i + 1;
  }
  free(*tab); *tab = nt; *mask = m;
  return 1;
}
static int idtab_has(const int *tab, int mask, const Item *items, const char *id) {
  if (!tab || !mask) return 0;
  unsigned slot = id_hash(id) & (unsigned)mask;
  while (tab[slot]) {
    if (!strcmp(items[tab[slot] - 1].id, id)) return 1;
    slot = (slot + 1u) & (unsigned)mask;
  }
  return 0;
}
static void idtab_put(int *tab, int mask, const char *id, int idx1) {
  if (!tab || !mask) return;
  unsigned slot = id_hash(id) & (unsigned)mask;
  while (tab[slot]) slot = (slot + 1u) & (unsigned)mask;
  tab[slot] = idx1;
}

/* ---- rows ---- */
static void prep_free(Row *r) { free(r->cp); free(r->cpSt); r->cp = NULL; r->cpSt = NULL; r->ncp = 0; for (int k = 0; k < 3; k++) { r->pf[k][0] = r->pf[k][1] = -1; r->pfWait[k] = 0; } }
static void row_clear(Row *r) { itemlist_clear(&r->items); prep_free(r); free(r->gone); r->gone = NULL; r->ngone = 0; }
static Row *row_add(Board *b, const char *title, int wide, int label) {
  if (b->n >= MAXROWS) return NULL; Row *r = &b->rows[b->n++]; row_clear(r); memset(r, 0, sizeof *r); prep_free(r);
  snprintf(r->title, sizeof r->title, "%s", title); r->wide = wide; r->label = label; r->hidden = 1; r->closing = 0; r->hk = 1; r->popCol = -1; tween_set(&r->off, 0); tween_set(&r->a, 1); return r;
}
static void slide_row(Row *r) {
  float w = step_of(r); int vis = (int)((SB_W - 96) / w);
  int k = r->col - (vis - 2); if (k < 0) k = 0; float off = k * w; if (r->col <= 0) off = 0;
  tween_to(&r->off, off, 0.18f, EASE_ROW);
  if (r->popCol != r->col) { r->popCol = r->col; focus_pop(&r->pop); } /* the floating copy pops on every move */
}

/* ============================================================================================
   DISCOVER: 1-line filter bar (Type, Subsection, Date, Sort, Genre) + virtualized 5x3 tile grid
   ============================================================================================ */
#define DMEM 4
#define DGRP 64
#define DC_PREP_N 48
typedef struct { CatalogDef m[DMEM]; char addon[64]; int nm; } DGroup;
typedef struct { int rawIdx; unsigned gen, lg; CardPrep cp; unsigned char st; unsigned tick; } DiscPrep;
/* Date filter modes: 0=Any, 1=curYear, 2=curYear-1, 3=curYear-2, 4=2020s, 5=2010s, 6=2000s, 7=1990s, 8=Classic(<1990) */
#define DATE_OPTS_N 9
/* Sort modes: 0=Default, 1=Newest, 2=Oldest, 3=Top rated, 4=A-Z */
#define SORT_OPTS_N 5

static struct {
  /* Filter bar state (5 pills: 0 Type, 1 Subsection, 2 Date, 3 Sort, 4 Genre) */
  char type[16], genre[48];
  int cat, dateMode, sortMode;
  int open, which, idx, n;
  float listW, roomMax, ow[200], ox[200], rx0, rroom, rfull, hx, hw, sxd;
  Tween wt[5]; double restAt; Tween sx; int hFrom, hTo; Tween hk, ha;
  char opt[200][96]; Tween a; float cx[5], cw[5], y; int nchip;
  int filtCol; /* 0..nchip-1 */
  /* Cached catalog list for current FL.type */
  int cacheValid; char cacheType[16]; unsigned cacheGen;
  CatalogDef *cats; char (*catAddon)[64]; int ncats;
  DGroup *dgrp; int ndgrp;
} FL;

static struct {
  CatalogDef *m; int nm;
  ItemList part[DMEM]; int used[DMEM], has[DMEM], mMore[DMEM], mSkip[DMEM], mPageSize[DMEM], mEmpty[DMEM], mWait[DMEM];
  Item *raw; int rawN, rawCap; int *idTab; int idMask;
  int *view; int viewN, viewCap; /* indices into raw[] after Date filter + Sort */
  int more, paging, loaded, autoPages; double pageAt, askAt; unsigned gen;
  int onFilt; /* 1 = focus on filter line, 0 = focus in 5x3 grid */
  int cur;    /* focused index in view[] */
  int scrollRow; Tween scrollY; Nudge nudge;
  DiscPrep prep[DC_PREP_N]; unsigned prepTick;
} DS;

static int cur_year_now(void) {
  static int y;
  if (!y) { time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm); y = tm.tm_year + 1900; }
  return y;
}
static int item_year(const Item *it) {
  for (const char *p = it->metaLine; *p; p++)
    if (p[0] >= '1' && p[0] <= '2' && p[1] >= '0' && p[1] <= '9' && p[2] >= '0' && p[2] <= '9' && p[3] >= '0' && p[3] <= '9') {
      int y = atoi(p); if (y >= 1890 && y <= 2100) return y;
    }
  return 0;
}
static float item_rating(const Item *it) {
  const char *p = strstr(it->metaLine, "IMDb ");
  if (!p) return -1.0f;
  float v = (float)atof(p + 5);
  return v > 0.0f ? v : -1.0f;
}
static int item_date_ok(const Item *it, int mode) {
  if (mode <= 0) return 1;
  int y = item_year(it), cy = cur_year_now();
  if (!y) return 0;
  switch (mode) {
    case 1: return y == cy;
    case 2: return y == cy - 1;
    case 3: return y == cy - 2;
    case 4: return y >= 2020 && y <= 2029;
    case 5: return y >= 2010 && y <= 2019;
    case 6: return y >= 2000 && y <= 2009;
    case 7: return y >= 1990 && y <= 1999;
    case 8: return y < 1990;
    default: return 1;
  }
}
static void date_opt_label(int mode, char *out, size_t cap) {
  int cy = cur_year_now();
  switch (mode) {
    case 0: snprintf(out, cap, "%s", T("Any date")); break;
    case 1: snprintf(out, cap, "%d", cy); break;
    case 2: snprintf(out, cap, "%d", cy - 1); break;
    case 3: snprintf(out, cap, "%d", cy - 2); break;
    case 4: snprintf(out, cap, "2020s"); break;
    case 5: snprintf(out, cap, "2010s"); break;
    case 6: snprintf(out, cap, "2000s"); break;
    case 7: snprintf(out, cap, "1990s"); break;
    case 8: snprintf(out, cap, "%s", T("Classic")); break;
    default: snprintf(out, cap, "%s", T("Any date")); break;
  }
}
static const char *sort_opt_label(int mode) {
  switch (mode) {
    case 1: return T("Newest");
    case 2: return T("Oldest");
    case 3: return T("Top rated");
    case 4: return T("A\xe2\x80\x93Z");
    default: return T("Default");
  }
}

static void hero_soon(Board *b, int ms);
static void logos_ahead(Board *b, int force);
static int first_visible_row(Board *b, int from, int dir);
static void disc_more(void);

static const Item *disc_focused(void) {
  if (DS.onFilt || DS.cur < 0 || DS.cur >= DS.viewN) return NULL;
  int ri = DS.view[DS.cur];
  return ri >= 0 && ri < DS.rawN ? &DS.raw[ri] : NULL;
}
static const Item *focused_of(Board *b) {
  if (b->kind == 1) return disc_focused();
  if (b->rowIdx < 0 || b->rowIdx >= b->n) return NULL;
  Row *r = &b->rows[b->rowIdx];
  return r->col >= 0 && r->col < r->items.n ? &r->items.items[r->col] : NULL;
}

static unsigned disc_addon_gen = 1;
static void disc_cache_invalidate(void) { FL.cacheValid = 0; disc_addon_gen++; }
static int disc_all(void) { return !strcmp(FL.type, "*"); }
static int disc_ensure_cache(void) {
  if (!FL.cats) FL.cats = calloc(DGRP, sizeof *FL.cats);
  if (!FL.catAddon) FL.catAddon = calloc(DGRP, sizeof *FL.catAddon);
  if (!FL.dgrp) FL.dgrp = calloc(DGRP, sizeof *FL.dgrp);
  if (!DS.m) DS.m = calloc(DMEM, sizeof *DS.m);
  if (!FL.cats || !FL.catAddon || !FL.dgrp || !DS.m) return 0;
  if (FL.cacheValid && !strcmp(FL.cacheType, FL.type) && FL.cacheGen == disc_addon_gen) return FL.ncats;
  FL.cacheValid = 1; snprintf(FL.cacheType, sizeof FL.cacheType, "%s", FL.type); FL.cacheGen = disc_addon_gen;
  FL.ncats = 0; FL.ndgrp = 0;
  if (!disc_all()) {
    FL.ncats = sb_disc_cats(FL.type, FL.cats, FL.catAddon, DGRP);
    return FL.ncats;
  }
  char ty[16][16]; int nt = sb_disc_types(ty, 16);
  CatalogDef *cs = calloc(DGRP, sizeof *cs);
  char (*an)[64] = calloc(DGRP, sizeof *an);
  if (cs && an) {
    for (int t = 0; t < nt; t++) {
      int nc = sb_disc_cats(ty[t], cs, an, DGRP);
      for (int i = 0; i < nc; i++) {
        int g = 0;
        for (; g < FL.ndgrp; g++) if (!strcmp(FL.dgrp[g].m[0].title, cs[i].title) && !strcmp(FL.dgrp[g].addon, an[i])) break;
        if (g == FL.ndgrp) {
          if (FL.ndgrp >= DGRP) continue;
          FL.ndgrp++; FL.dgrp[g].nm = 0; snprintf(FL.dgrp[g].addon, sizeof FL.dgrp[g].addon, "%s", an[i]);
        }
        if (FL.dgrp[g].nm < DMEM) FL.dgrp[g].m[FL.dgrp[g].nm++] = cs[i];
      }
    }
    for (int i = 0; i < FL.ndgrp && i < DGRP; i++) {
      FL.cats[i] = FL.dgrp[i].m[0];
      snprintf(FL.catAddon[i], 64, "%s", FL.dgrp[i].addon);
    }
    FL.ncats = FL.ndgrp;
  }
  free(cs); free(an);
  return FL.ncats;
}
static int disc_cat_genres(char (*out)[48], int max, int *req) {
  *req = 0; int nc = disc_ensure_cache(); if (!nc) return 0;
  int ci = FL.cat < nc ? FL.cat : 0;
  if (!disc_all()) return sb_disc_cat_genres(&FL.cats[ci], out, max, req);
  DGroup *g = &FL.dgrp[ci]; int n = 0;
  char (*gg)[48] = calloc(199, sizeof *gg); if (!gg) return 0;
  for (int k = 0; k < g->nm; k++) {
    int r = 0, m = sb_disc_cat_genres(&g->m[k], gg, 199, &r); if (r) *req = 1;
    for (int i = 0; i < m && n < max; i++) {
      int dup = 0; for (int j = 0; j < n && !dup; j++) dup = !strcmp(out[j], gg[i]);
      if (!dup) snprintf(out[n++], 48, "%s", gg[i]);
    }
  }
  free(gg);
  return n;
}
static int disc_catalog(void) {
  char ty[16][16]; int nt = sb_disc_types(ty, 16);
  if (!nt) { DS.nm = 0; return 0; }
  int okT = nt >= 2 && disc_all();
  for (int i = 0; i < nt; i++) if (!strcmp(ty[i], FL.type)) okT = 1;
  if (!okT) { snprintf(FL.type, sizeof FL.type, "%s", nt >= 2 ? "*" : ty[0]); FL.cat = 0; FL.genre[0] = 0; FL.cacheValid = 0; }
  int nc = disc_ensure_cache(); if (!nc) { DS.nm = 0; return 0; }
  if (FL.cat >= nc) FL.cat = 0;
  DS.nm = 0;
  if (!disc_all()) {
    DS.m[0] = FL.cats[FL.cat];
    if (FL.genre[0]) sb_disc_with_genre(&DS.m[0], FL.genre);
    DS.nm = 1;
    return 1;
  }
  DGroup *g = &FL.dgrp[FL.cat];
  char (*gg)[48] = calloc(199, sizeof *gg);
  for (int k = 0; k < g->nm && DS.nm < DMEM; k++) {
    CatalogDef d = g->m[k]; int req = 0, m = gg ? sb_disc_cat_genres(&d, gg, 199, &req) : 0;
    if (FL.genre[0]) {
      int has = 0; for (int i = 0; i < m && !has; i++) has = !strcmp(gg[i], FL.genre);
      if (!has) continue;
      sb_disc_with_genre(&d, FL.genre);
    } else if (req && m) sb_disc_with_genre(&d, gg[0]);
    DS.m[DS.nm++] = d;
  }
  free(gg);
  return DS.nm > 0;
}

static void disc_raw_clear(void) {
  free(DS.raw); DS.raw = NULL; DS.rawN = DS.rawCap = 0;
  free(DS.idTab); DS.idTab = NULL; DS.idMask = 0;
  free(DS.view); DS.view = NULL; DS.viewN = DS.viewCap = 0;
  memset(DS.prep, 0, sizeof DS.prep);
  DS.cur = 0; DS.scrollRow = 0; tween_set(&DS.scrollY, 0);
}
static int disc_raw_append(const Item *it) {
  if (!it || !it->id[0]) return 0;
  if (!DS.idTab && DS.rawN > 0) idtab_rebuild(&DS.idTab, &DS.idMask, DS.raw, DS.rawN, DS.rawN + 64);
  if (DS.idTab && idtab_has(DS.idTab, DS.idMask, DS.raw, it->id)) return 0;
  if (DS.rawN >= DS.rawCap) {
    int nc = DS.rawCap ? DS.rawCap * 2 : 128;
    if (nc <= DS.rawN) nc = DS.rawN + 128;
    Item *g = realloc(DS.raw, (size_t)nc * sizeof(Item));
    if (!g) return 0;
    DS.raw = g; DS.rawCap = nc;
  }
  if (!DS.idTab || (DS.rawN + 1) * 2 > DS.idMask + 1) idtab_rebuild(&DS.idTab, &DS.idMask, DS.raw, DS.rawN, DS.rawCap);
  DS.raw[DS.rawN] = *it;
  if (DS.idTab) idtab_put(DS.idTab, DS.idMask, it->id, DS.rawN + 1);
  DS.rawN++;
  return 1;
}

static int cmp_disc_sort(const void *pa, const void *pb) {
  int ia = *(const int *)pa, ib = *(const int *)pb;
  const Item *a = &DS.raw[ia], *b = &DS.raw[ib];
  if (FL.sortMode == 1) {
    int ya = item_year(a), yb = item_year(b);
    if (ya != yb) { if (!ya) return 1; if (!yb) return -1; return yb - ya; }
  } else if (FL.sortMode == 2) {
    int ya = item_year(a), yb = item_year(b);
    if (ya != yb) { if (!ya) return 1; if (!yb) return -1; return ya - yb; }
  } else if (FL.sortMode == 3) {
    float ra = item_rating(a), rb = item_rating(b);
    if (ra != rb) { if (ra < 0) return 1; if (rb < 0) return -1; return rb > ra ? 1 : -1; }
  } else if (FL.sortMode == 4) {
    int c = strcasecmp(a->name, b->name);
    if (c != 0) return c;
  }
  return ia - ib;
}
static void disc_sync_scroll(int snap) {
  int rows = (DS.viewN + DISC_COLS - 1) / DISC_COLS;
  int r = DS.viewN ? DS.cur / DISC_COLS : 0;
  int maxTop = rows > DISC_VIS_ROWS ? rows - DISC_VIS_ROWS : 0;
  if (DS.scrollRow > maxTop) DS.scrollRow = maxTop;
  if (DS.scrollRow < 0) DS.scrollRow = 0;
  if (r < DS.scrollRow) DS.scrollRow = r;
  else if (r >= DS.scrollRow + DISC_VIS_ROWS) DS.scrollRow = r - (DISC_VIS_ROWS - 1);
  else if (r > 0 && r < rows - 1 && DS.scrollRow < r - 1) DS.scrollRow = r - 1;
  if (DS.scrollRow > maxTop) DS.scrollRow = maxTop;
  float ty = -(float)DS.scrollRow * DISC_PITCH;
  if (snap) tween_set(&DS.scrollY, ty);
  else tween_to(&DS.scrollY, ty, 0.22f, EASE_ROW);
}
static void disc_rebuild_view(int resetScroll) {
  char keepId[SB_ID] = "";
  if (!resetScroll && !DS.onFilt && DS.cur >= 0 && DS.cur < DS.viewN) {
    int ri = DS.view[DS.cur];
    if (ri >= 0 && ri < DS.rawN) snprintf(keepId, sizeof keepId, "%s", DS.raw[ri].id);
  }
  if (DS.rawN > DS.viewCap) {
    int nc = DS.rawN + 64;
    int *nv = realloc(DS.view, (size_t)nc * sizeof(int));
    if (!nv) return;
    DS.view = nv; DS.viewCap = nc;
  }
  int n = 0;
  for (int i = 0; i < DS.rawN; i++) if (item_date_ok(&DS.raw[i], FL.dateMode)) DS.view[n++] = i;
  if (FL.sortMode > 0 && n > 1) qsort(DS.view, (size_t)n, sizeof(int), cmp_disc_sort);
  DS.viewN = n;
  if (resetScroll) { DS.cur = 0; DS.scrollRow = 0; }
  else if (keepId[0]) {
    int found = -1;
    for (int i = 0; i < DS.viewN; i++) if (!strcmp(DS.raw[DS.view[i]].id, keepId)) { found = i; break; }
    if (found >= 0) DS.cur = found;
  }
  if (DS.cur >= DS.viewN) DS.cur = DS.viewN > 0 ? DS.viewN - 1 : 0;
  if (DS.cur < 0) DS.cur = 0;
  disc_sync_scroll(resetScroll);
  if (cur() == &boards[1]) hero_soon(&boards[1], 40);
  if (DS.more && !DS.paging && DS.viewN < DISC_COLS * DISC_VIS_ROWS && DS.autoPages < 8) {
    DS.autoPages++;
    disc_more();
  }
}

#define DUD(i) ((void *)(uintptr_t)(((DS.gen & 0x0fffffffu) << 4) | (unsigned)(i)))
static int dud_ok(void *ud, int *i) { unsigned u = (unsigned)(uintptr_t)ud; *i = (int)(u & 15); return (u >> 4) == (DS.gen & 0x0fffffffu) && *i < DS.nm; }

static void disc_merge(void) {
  int add = 0;
  for (;;) {
    int any = 0, stall = 0;
    for (int i = 0; i < DS.nm; i++) {
      if (DS.used[i] < DS.part[i].n) any = 1;
      else if (DS.mMore[i] || !DS.has[i]) stall = 1;
    }
    if (!any || stall) break;
    for (int i = 0; i < DS.nm; i++) {
      if (DS.used[i] >= DS.part[i].n) continue;
      if (disc_raw_append(&DS.part[i].items[DS.used[i]++])) add++;
    }
  }
  int more = 0, anyHas = 0;
  for (int i = 0; i < DS.nm; i++) { more |= DS.mMore[i]; anyHas |= DS.has[i]; }
  DS.more = more;
  if (anyHas) DS.loaded = 1;
  if (add || !DS.more || DS.viewN == 0) disc_rebuild_view(0);
}
static void on_disc(ItemList *l, int cached, void *ud) {
  int mi; if (!dud_ok(ud, &mi)) { itemlist_free(l); return; }
  if (DS.nm == 1) {
    if (cached && DS.rawN > 0) { itemlist_free(l); return; }
    int prevN = DS.rawN;
    if (prevN == 0) {
      disc_raw_clear();
      for (int i = 0; i < l->n; i++) disc_raw_append(&l->items[i]);
    } else {
      Item *old = DS.raw; int oldN = DS.rawN; DS.raw = NULL; DS.rawN = DS.rawCap = 0;
      free(DS.idTab); DS.idTab = NULL; DS.idMask = 0;
      for (int i = 0; i < l->n; i++) disc_raw_append(&l->items[i]);
      for (int i = 0; i < oldN; i++) disc_raw_append(&old[i]);
      free(old);
      memset(DS.prep, 0, sizeof DS.prep);
    }
    int freshTot = l->total > l->n ? l->total : l->n;
    itemlist_free(l);
    DS.has[0] = 1; DS.loaded = 1;
    if (freshTot > DS.mPageSize[0]) DS.mPageSize[0] = freshTot;
    if (prevN == 0) {
      DS.mSkip[0] = freshTot > DS.rawN ? freshTot : DS.rawN;
      DS.mMore[0] = DS.m[0].skip && DS.rawN > 0;
      DS.mEmpty[0] = 0;
    } else if (freshTot > DS.mSkip[0]) DS.mSkip[0] = freshTot;
    DS.more = DS.mMore[0]; DS.paging = 0;
    disc_rebuild_view(0);
    return;
  }
  if (DS.has[mi] && DS.used[mi]) { itemlist_free(l); return; }
  itemlist_clear(&DS.part[mi]); DS.part[mi] = *l; free(l);
  DS.has[mi] = 1;
  int tot = DS.part[mi].total > DS.part[mi].n ? DS.part[mi].total : DS.part[mi].n;
  DS.mSkip[mi] = tot; DS.mPageSize[mi] = tot; DS.mMore[mi] = DS.m[mi].skip && DS.part[mi].n > 0; DS.mEmpty[mi] = 0;
  disc_merge();
}
static void on_disc_page(ItemList *l, int more, void *ud) {
  int mi; if (!dud_ok(ud, &mi)) { itemlist_free(l); return; }
  DS.mWait[mi] = 0;
  if (!l) { DS.pageAt = app_now() + 2.5; }
  else {
    ItemList *p = &DS.part[mi]; int got = l->n, tot = l->total > got ? l->total : got;
    if (tot > DS.mPageSize[mi]) DS.mPageSize[mi] = tot;
    if (got > 0) {
      Item *g = realloc(p->items, (size_t)(p->n + got + 1) * sizeof(Item));
      if (g) { p->items = g; memcpy(p->items + p->n, l->items, sizeof(Item) * (size_t)got); p->n += got; }
      DS.mSkip[mi] += tot; DS.mEmpty[mi] = 0;
    } else if (tot > 0) {
      int step = DS.mPageSize[mi] > tot ? DS.mPageSize[mi] : tot;
      DS.mSkip[mi] += step; DS.mEmpty[mi]++;
    }
    if (!tot || more == 0 || DS.mEmpty[mi] >= 3) DS.mMore[mi] = 0;
    itemlist_free(l);
  }
  int w = 0; for (int i = 0; i < DS.nm; i++) w |= DS.mWait[i]; if (!w) DS.paging = 0;
  disc_merge();
}
static void on_disc_page1(ItemList *l, int more, void *ud) {
  int mi; if (!dud_ok(ud, &mi)) { itemlist_free(l); return; }
  DS.paging = 0;
  if (!l) { DS.pageAt = app_now() + 2.5; return; }
  int tot = l->total > l->n ? l->total : l->n, add = 0;
  for (int i = 0; i < l->n; i++) if (disc_raw_append(&l->items[i])) add++;
  itemlist_free(l);
  if (tot > DS.mPageSize[0]) DS.mPageSize[0] = tot;
  if (add > 0) { DS.mSkip[0] += tot; DS.mEmpty[0] = 0; }
  else if (tot > 0) { int step = DS.mPageSize[0] > tot ? DS.mPageSize[0] : tot; DS.mSkip[0] += step; DS.mEmpty[0]++; }
  if (!tot || more == 0 || DS.mEmpty[0] >= 3) DS.mMore[0] = 0;
  DS.more = DS.mMore[0];
  disc_rebuild_view(0);
  if (cur() == &boards[1]) disc_more();
}
static void disc_wait_tick(void) {
  if (!DS.nm || !DS.askAt || app_now() - DS.askAt < 5.0) return;
  int late = 0;
  for (int i = 0; i < DS.nm; i++) if (!DS.has[i]) { DS.has[i] = 1; DS.mMore[i] = 0; late = 1; }
  DS.askAt = 0;
  if (late) disc_merge();
}
static void disc_more(void) {
  if (!DS.more || DS.paging || app_now() < DS.pageAt || !DS.nm) return;
  int rows = (DS.viewN + DISC_COLS - 1) / DISC_COLS;
  int curRow = DS.viewN ? DS.cur / DISC_COLS : 0;
  int needMore = (rows - curRow <= 4) || (DS.viewN < DISC_COLS * DISC_VIS_ROWS && DS.autoPages < 8);
  if (!needMore) return;
  if (DS.nm == 1) {
    DS.paging = 1;
    sb_catalog_page(&DS.m[0], DS.mSkip[0], on_disc_page1, DUD(0));
    return;
  }
  int asked = 0;
  for (int i = 0; i < DS.nm; i++) {
    if (DS.mMore[i] && DS.used[i] >= DS.part[i].n - DISC_COLS * 2) {
      DS.mWait[i] = 1; asked = 1;
      sb_catalog_page(&DS.m[i], DS.mSkip[i], on_disc_page, DUD(i));
    }
  }
  DS.paging = asked;
}
static void disc_load(void) {
  DS.gen++; disc_raw_clear();
  for (int i = 0; i < DMEM; i++) {
    itemlist_clear(&DS.part[i]);
    DS.used[i] = DS.has[i] = DS.mMore[i] = DS.mSkip[i] = DS.mPageSize[i] = DS.mEmpty[i] = DS.mWait[i] = 0;
  }
  DS.more = DS.paging = DS.loaded = DS.autoPages = 0; DS.pageAt = 0; DS.askAt = 0;
  if (disc_catalog()) {
    DS.askAt = app_now();
    for (int i = 0; i < DS.nm; i++) sb_catalog_url(DS.m[i].url, DS.m[i].type, 100, on_disc, DUD(i));
    fprintf(stderr, "[disc] %s %s (%d catalog%s)%s%s\n", FL.type, DS.m[0].title, DS.nm, DS.nm == 1 ? "" : "s", FL.genre[0] ? " · " : "", FL.genre);
  } else {
    DS.loaded = 1;
  }
}

/* a catalog answer finds its row through a packed token (board, row, generation): nothing to free */
static void *tok(Board *b, int row) { return (void *)(uintptr_t)(((uintptr_t)(b - boards) << 31) | ((uintptr_t)(b->gen & 0xffffff) << 7) | (uintptr_t)row); }
static void on_catalog(ItemList *l, int cached, void *ud) {
  (void)cached; uintptr_t t = (uintptr_t)ud; Board *b = &boards[(t >> 31) & 1]; int row = t & 127; unsigned gen = (t >> 7) & 0xffffff;
  if ((b->gen & 0xffffff) != gen || row >= b->n) { itemlist_free(l); return; }
  int got = l->n;
  { Row *r0 = &b->rows[row]; if (!got && l->total < 0 && r0->hasDef && r0->retries < 3) { static const double W[3] = { 8, 20, 45 }; r0->retryAt = app_now() + W[r0->retries]; r0->retries++; itemlist_free(l); /* no answer (an addon still making the list, busy, a blip): asked again in 8, 20 and 45 s before the row is left out */
      fprintf(stderr, "[home] %s: no answer, asked again in %.0f s\n", r0->title, W[r0->retries - 1]); return; } }
  Row *r = &b->rows[row]; row_clear(r); r->items = *l; free(l); r->locAt = 0;
  r->skipAt = got; r->skipAlt = r->items.total > got ? r->items.total : 0; r->more = r->hasDef && r->def.skip && r->items.n > 0; r->paging = 0; /* the next page from where the row stopped (16 kept of 100: asked from 100, titles 17-100 never showed), from the page's end if the addon only pages by its size */
  r->hidden = r->items.n == 0; if (r->col >= r->items.n) r->col = r->items.n ? r->items.n - 1 : 0;
  if (cur() == b) { slide_row(r); b->heroPending = b->heroPending || (b->rowIdx == row); if (b->rowIdx == row && !b->heroPending) { b->heroAt = app_now(); b->heroPending = 1; } }
}
/* the rows as they were, found again by what they are (never by their place: a row appearing above, Upcoming episodes
   after a series is marked, would hand each row below the place of the one above it); on a library change
   (keepItems) a catalog row also keeps what it had loaded, its later pages and where it was in them */
typedef struct { char title[96], fid[SB_ID]; CatalogDef def; int hasDef; ItemList items; int col, skipAt, more; char (*ids)[SB_ID]; int nids; float off; Item *was; int nwas; } KeptRow; /* ids: a row without a catalog's titles in order (its cards glide to their new places) */ /* fid: the item focused in it */
static int keepItems;
static int same_row(const KeptRow *k, const char *title, const CatalogDef *d, int hasDef) {
  if (strcmp(k->title, title) || k->hasDef != hasDef) return 0; if (!hasDef) return 1;
  return !strcmp(k->def.type, d->type) && !strcmp(k->def.url, d->url) && !strcmp(k->def.path, d->path) && !strcmp(k->def.extra, d->extra); }
/* a row of the library's (Continue Watching, Upcoming, Live TV) gone empty: it stays a moment, its last cards fading
   and shrinking where they stood, then closes as the rows under it move up */
static void row_ghosts(Row *r, const KeptRow *k, const ItemList *now) {
  if (!k || !k->was || !k->nwas) return; int n = 0; for (int q = 0; q < k->nwas && n < 8; q++) { int in = 0; for (int c = 0; now && c < now->n && !in; c++) in = !strcmp(now->items[c].id, k->was[q].id); if (!in) n++; }
  if (!n || !(r->gone = malloc(sizeof(Item) * (size_t)n))) return;
  for (int q = 0; q < k->nwas && r->ngone < n; q++) { int in = 0; for (int c = 0; now && c < now->n && !in; c++) in = !strcmp(now->items[c].id, k->was[q].id); if (!in) { r->gone[r->ngone] = k->was[q]; r->goneIdx[r->ngone] = (short)q; r->ngone++; } }
  r->goneAt = app_now(); r->goneOff = k->off;
}
static const KeptRow *kept_row(const KeptRow *kp, int nk, const char *title) { for (int j = 0; j < nk; j++) if (!kp[j].hasDef && !strcmp(kp[j].title, title)) return &kp[j]; return NULL; }
static void closing_row(Board *b, const KeptRow *kp, int nk, const char *title, int wide, int round) {
  const KeptRow *k = kept_row(kp, nk, title); if (!k || !k->nwas) return; Row *z = row_add(b, title, wide, 0); if (!z) return;
  z->hidden = 0; z->closing = 1; z->closeAt = app_now(); z->hk = 1; z->round = round; row_ghosts(z, k, NULL); }
static void board_cw_row(Board *b, const KeptRow *kp, int nk) {
  ItemList cw; if (lib_cw_items(&cw) > 0) { Row *r = row_add(b, "Continue Watching", 1, 0); if (r) { r->items = cw; r->hidden = 0; } else itemlist_clear(&cw); } else { itemlist_clear(&cw); closing_row(b, kp, nk, "Continue Watching", 1, 0); }
  ItemList up; if (notify_upcoming(&up) > 0) { Row *r = row_add(b, "Upcoming episodes", 1, 0); if (r) { r->items = up; r->hidden = 0; } else itemlist_clear(&up); } else closing_row(b, kp, nk, "Upcoming episodes", 1, 0); /* the library's series airing in the next two weeks */
  ItemList lv; if (b->kind == 0 && live_band(&lv) > 0) { Row *r = row_add(b, "Live TV", 0, 0); if (r) { r->items = lv; r->hidden = 0; r->round = 1; } else itemlist_clear(&lv); } else { itemlist_clear(&lv); if (b->kind == 0) closing_row(b, kp, nk, "Live TV", 0, 1); } /* a live addon installed: All channels, the pinned, a few more */
}
static void load_catalog_rows(Board *b, int disc, int keep) {
  if (disc) {
    b->gen++;
    disc_load();
    return;
  }
  int oldRow = b->rowIdx; KeptRow *kp = NULL; int nk = 0, focK = -1;
  if (keep && b->n && (kp = calloc((size_t)b->n, sizeof *kp))) { nk = b->n; focK = b->rowIdx < b->n ? b->rowIdx : -1;
    for (int i = 0; i < nk; i++) { Row *r = &b->rows[i]; snprintf(kp[i].title, sizeof kp[i].title, "%s", r->title); kp[i].def = r->def; kp[i].hasDef = r->hasDef; kp[i].col = r->col; if (r->col >= 0 && r->col < r->items.n) snprintf(kp[i].fid, sizeof kp[i].fid, "%s", r->items.items[r->col].id);
      kp[i].off = r->off.v; if (!r->hasDef && r->items.n && (kp[i].ids = malloc(sizeof *kp[i].ids * (size_t)(r->items.n < 48 ? r->items.n : 48)))) { kp[i].nids = r->items.n < 48 ? r->items.n : 48; for (int c = 0; c < kp[i].nids; c++) snprintf(kp[i].ids[c], 64, "%s", r->items.items[c].id); if ((kp[i].was = malloc(sizeof(Item) * (size_t)kp[i].nids))) { memcpy(kp[i].was, r->items.items, sizeof(Item) * (size_t)kp[i].nids); kp[i].nwas = kp[i].nids; } } kp[i].skipAt = r->skipAt; kp[i].more = r->more;
      if (keepItems && r->hasDef && r->items.n) { kp[i].items = r->items; memset(&r->items, 0, sizeof r->items); } } }
  for (int i = 0; i < b->n; i++) row_clear(&b->rows[i]); b->n = 0; b->gen++;
  board_cw_row(b, kp, nk);
  static CatalogDef defs[MAXROWS]; int nd = sb_home_defs(defs, MAXROWS - 1);
  int band = live_catalogs() > 0; /* the live row holds live: its catalogs are in the hub (All channels) */
  for (int d = 0; d < nd; d++) { if (band && live_type(defs[d].type)) continue; Row *r = row_add(b, defs[d].title, 0, 0); if (!r) break; int ri = (int)(r - b->rows); r->def = defs[d]; r->hasDef = 1;
    int k = -1; for (int j = 0; j < nk && k < 0; j++) if (same_row(&kp[j], r->title, &r->def, 1)) k = j;
    if (k >= 0) { r->col = kp[k].col; if (kp[k].items.n) { r->items = kp[k].items; memset(&kp[k].items, 0, sizeof kp[k].items); r->skipAt = kp[k].skipAt; r->more = kp[k].more; r->hidden = 0; continue; } } /* (what it had, kept: not asked again) */
    sb_catalog(&defs[d], on_catalog, tok(b, ri)); }
  if (keep) for (int i = 0; i < b->n; i++) { Row *r = &b->rows[i]; if (r->hasDef || r->closing) continue; int found = 0;
    for (int j = 0; j < nk && !found; j++) if (same_row(&kp[j], r->title, &r->def, 0)) found = 1;
    if (!found && nk && r->title[0]) { r->opening = 1; r->openAt = app_now(); r->hk = 0; tween_set(&r->a, 0); for (int c = 0; c < r->items.n && c < 48; c++) r->oldIdx[c] = -1; r->moveAt = app_now(); r->moveOff = 0; continue; }
    for (int j = 0; j < nk; j++) if (same_row(&kp[j], r->title, &r->def, 0)) { r->col = kp[j].col; row_ghosts(r, &kp[j], &r->items);
    for (int c = 0; c < r->items.n && kp[j].fid[0]; c++) if (!strcmp(r->items.items[c].id, kp[j].fid)) { r->col = c; break; }
    int moved = 0; for (int c = 0; c < r->items.n && c < 48; c++) { int o = -1; for (int q = 0; q < kp[j].nids && o < 0; q++) if (!strcmp(kp[j].ids[q], r->items.items[c].id)) o = q; r->oldIdx[c] = (short)o; if (o != c) moved = 1; }
    if ((moved || r->ngone || r->items.n != kp[j].nids) && kp[j].nids) { r->moveAt = app_now(); r->moveOff = kp[j].off; } break; } }
  int focNew = -1; if (focK >= 0) for (int i = 0; i < b->n && focNew < 0; i++) if (same_row(&kp[focK], b->rows[i].title, &b->rows[i].def, b->rows[i].hasDef)) focNew = i;
  for (int j = 0; j < nk; j++) { itemlist_clear(&kp[j].items); free(kp[j].ids); free(kp[j].was); } free(kp);
  if (keep) for (int i = 0; i < b->n; i++) { slide_row(&b->rows[i]); tween_set(&b->rows[i].off, b->rows[i].off.to); }
  if (keep) { b->rowIdx = focNew >= 0 ? focNew : oldRow < b->n ? oldRow : (b->n ? b->n - 1 : 0); if (b->rowIdx < b->n && b->rows[b->rowIdx].closing) { int nx = first_visible_row(b, b->rowIdx + 1, 1); if (nx >= 0) b->rowIdx = nx; } for (int j = 0; j < b->n; j++) { Row *r = &b->rows[j]; if (r->items.n && r->col >= r->items.n) r->col = r->items.n - 1; } }
  else b->rowIdx = 0;
}
static int first_visible_row(Board *b, int from, int dir) { for (int i = from; i >= 0 && i < b->n; i += dir) if (!b->rows[i].hidden && !b->rows[i].closing) return i; return -1; }

/* ---- the hero ---- */
static void hero_soon(Board *b, int ms) { b->heroAt = app_now() + ms / 1000.0; b->heroPending = 1;
  const Item *it = focused_of(b); if (it) { char pu[512], lu[512]; accent_tint_fetch(it->id, sb_poster_of(it, pu, sizeof pu), sb_logo_of(it, lu, sizeof lu)); } }
static Item *item_at(Board *b, int row, int col) {
  if (row == DISC_ROW) return col >= 0 && col < DS.rawN ? &DS.raw[col] : NULL;
  if (row < 0 || row >= b->n || col < 0 || col >= b->rows[row].items.n) return NULL; return &b->rows[row].items.items[col];
}
typedef struct { Board *b; int row, col; unsigned gen; char id[SB_ID]; } MetaAsk;
static void on_meta(cJSON *m, void *ud) {
  MetaAsk *u = ud; if (!u) return;
  Board *b = u->b; int row = u->row, col = u->col; unsigned gen = u->gen;
  if (!m || (row == DISC_ROW ? DS.gen != gen : b->gen != gen)) { free(u); return; }
  Item *it = item_at(b, row, col); if (!it || strcmp(it->id, u->id)) { free(u); return; }
  free(u);
  if (!it->description[0]) { const char *d = cJSON_GetStringValue(cJSON_GetObjectItem(m, "description")); if (d) snprintf(it->description, sizeof it->description, "%s", d); }
  /* the details' facts are what the title page shows (a catalog can list a series' total running time where the
     details give an episode's): the line follows them, keeping the catalog's rating when the details have none */
  { char dl[96]; sb_meta_line(m, dl, sizeof dl); if (dl[0]) { const char *oi = strstr(it->metaLine, "IMDb "); if (!strstr(dl, "IMDb") && oi) { size_t l = strlen(dl); snprintf(dl + l, sizeof dl - l, "   %s", oi); }
      if (strcmp(dl, it->metaLine)) snprintf(it->metaLine, sizeof it->metaLine, "%s", dl); } }
  if (b->heroSet && !strcmp(b->hero.id, it->id)) { snprintf(b->hero.description, sizeof b->hero.description, "%s", it->description); snprintf(b->hero.metaLine, sizeof b->hero.metaLine, "%s", it->metaLine); b->heroVer++; }
}
static void meta_ask(Board *b, int row, int col) {
  Item *it = item_at(b, row, col); if (!it || !it->id[0]) return;
  MetaAsk *u = calloc(1, sizeof *u); if (!u) return;
  u->b = b; u->row = row; u->col = col; u->gen = (row == DISC_ROW) ? DS.gen : b->gen;
  snprintf(u->id, sizeof u->id, "%s", it->id);
  sb_meta(it->type, it->id, on_meta, u);
}
static void logo_src(const Item *it, char *out, size_t cap) { const TmdbLoc *L = tmdb_loc(it->type, it->id, 1); if (L && L->logo && L->logo[0]) snprintf(out, cap, "%s", L->logo); else sb_logo_of(it, out, cap); }
static void logos_ahead(Board *b, int force) {
  static double at[2]; double now = app_now(); int bi = (int)(b - boards); if (!force && now - at[bi] < 0.5) return; at[bi] = now;
  char lu[512];
  if (b->kind == 1) {
    int c0 = DS.cur - 5, c1 = DS.cur + 10; if (c0 < 0) c0 = 0; if (c1 >= DS.viewN) c1 = DS.viewN - 1;
    for (int i = c0; i <= c1; i++) { logo_src(&DS.raw[DS.view[i]], lu, sizeof lu); if (lu[0]) img_prefetch_pri(lu, 520, 175); }
    return;
  }
  for (int ri = b->rowIdx - 1; ri <= b->rowIdx + 2; ri++) { if (ri < 0 || ri >= b->n || b->rows[ri].hidden) continue; Row *r = &b->rows[ri];
    for (int c = r->col - 2; c <= r->col + 8; c++) { if (c < 0 || c >= r->items.n) continue; logo_src(&r->items.items[c], lu, sizeof lu); if (lu[0]) img_prefetch_pri(lu, 520, 175); } }
}
/* a channel or All channels: no film details to wait for, a logo worth waiting for (never its name first), slower softer fades */
static int hero_live(const Item *it) { return live_type(it->type) || !strcmp(it->id, LIVE_ALL_ID); }
static float fade_out(const Board *b) { return hero_live(&b->hero) ? 0.20f : 0.12f; }
static float fade_in(const Board *b) { return hero_live(&b->hero) ? 0.34f : 0.20f; }
static double logo_wait(const Item *it) { return hero_live(it) ? 2.0 : 1.1; }
/* what the hero would show now (the translation and the logo as far as they are in) */
static void hero_view(Board *b, HeroView *v) {
  const Item *it = &b->hero; const TmdbLoc *L = tmdb_loc(it->type, it->id, 1);
  snprintf(v->id, sizeof v->id, "%s", it->id); snprintf(v->type, sizeof v->type, "%s", it->type); logo_src(it, v->logo, sizeof v->logo); v->wait = 0;
  if (v->logo[0]) { if (img_failed(v->logo)) v->logo[0] = 0; else if (!img_loaded(v->logo)) { img_get(v->logo, 520, 175, NULL); v->wait = app_now() - b->heroT0 < logo_wait(it); v->logo[0] = 0; } } /* no art by 1.1 s (a channel: 2 s): the name */
  snprintf(v->name, sizeof v->name, "%s", L && L->name && L->name[0] ? L->name : it->name);
  snprintf(v->metaName, sizeof v->metaName, "%s", L && L->name && L->name[0] && !(L->logo && L->logo[0]) && strcmp(L->name, it->name) ? L->name : "");
  snprintf(v->metaLine, sizeof v->metaLine, "%s", it->metaLine);
  snprintf(v->desc, sizeof v->desc, "%s", L && L->overview && L->overview[0] ? L->overview : it->description);
  unsigned h = 2166136261u; const char *parts[6] = { v->logo, v->wait ? "W" : "T", v->logo[0] ? "" : v->name, v->metaName, v->metaLine, hero_live(it) ? "" : v->desc }; /* (a channel's words arriving late never fade it again) */
  for (int i = 0; i < 6; i++) { for (const char *p = parts[i]; *p; p++) h = (h ^ (unsigned char)*p) * 16777619u; h = (h ^ 0x1f) * 16777619u; }
  v->sig = h;
}
/* the description's lines all rasterised, as each face draws it (the missing ones are asked for) */
static int desc_ready(Board *b, const char *d) { (void)b; return !d[0] || text_wrapped_ready(d, HERO_W, 3, DESC_PX, 400, 0); }
static const char *hero_key(const Board *b) { static char k[80]; snprintf(k, sizeof k, "logo:%s", b->view.id); return k; } /* (the title shown: the next one waits for its fade) */
static void hero_put(Board *b) {
  int late = b->view.wait; hero_view(b, &b->view); b->viewVer++; b->inVer = b->heroVer - 1; /* the next idle frame compares once */
  if (late && b->view.logo[0]) { tween_set(&b->logoIn, 0); tween_to(&b->logoIn, 1, 0.3f, EASE_CSS_EASEOUT); } /* a logo that came late fades in */
  else tween_set(&b->logoIn, 1);
  b->heroShown = 1; b->aheadDone = 0;
  if (b->kind == 1 || desc_ready(b, b->view.desc)) { b->fadeHold = 0; tween_to(&b->heroA, 1, fade_in(b), EASE_CSS_EASEOUT); } else b->fadeHold = app_now() + 0.15; /* every line in the same fade */
}
static void hero_apply(Board *b) {
  if (cur() != b) return;
  const Item *it = focused_of(b);
  if (!it && b->kind == 1 && DS.viewN > 0) it = &DS.raw[DS.view[DS.cur < DS.viewN ? DS.cur : 0]];
  if (!it) { tween_to(&b->heroA, 0, 0.12f, EASE_CSS_EASEIN); b->heroSet = 0; if (b->kind == 1) bg_clear(); return; } /* nothing to show yet: never the previous screen's title */
  int again = b->heroSet && b->heroShown && !strcmp(b->hero.id, it->id) && b->heroPhase == HP_IDLE; /* back from its page: it is already there */
  char bgu[512], pu[512]; sb_bg_mid(it, bgu, sizeof bgu); sb_poster_of(it, pu, sizeof pu);
  int fast = b->fastBg; b->fastBg = 0; /* entering a board: show its backdrop right away */
  float idle = app_idle(); int delay = (app_splash_done && !fast) ? (int)((0.4f - idle) * 1000) : 0; if (delay < 0) delay = 0;
  bg_set(bgu, delay, pu); /* shown ~0.4 s after the last move */
  if (b->kind == 1) { int ri = DS.cur >= 0 && DS.cur < DS.viewN ? DS.view[DS.cur] : 0; if (ri >= 0 && ri < DS.rawN) meta_ask(b, DISC_ROW, ri); }
  else if (b->rowIdx >= 0 && b->rowIdx < b->n) meta_ask(b, b->rowIdx, b->rows[b->rowIdx].col);
  if (again) { b->hero = *it; b->heroVer++; return; }
  float fo = fade_out(b); if (hero_live(it) && fo < 0.20f) fo = 0.20f; /* (into a channel: the softer one too) */
  b->hero = *it; b->heroVer++; b->heroSet = 1; b->heroShown = 0; b->heroPhase = HP_GATHER; b->heroT0 = app_now(); b->fadeHold = 0; b->heroFo = fo;
  tween_to(&b->heroA, 0, fo, EASE_CSS_EASEIN);
  if (b->kind != 1) { char lu[512]; logo_src(it, lu, sizeof lu); if (lu[0]) img_prefetch_pri(lu, 520, 175); }
}
/* the neighbours' details and logos while the remote rests: the hero then shows them at once, in one fade */
static void hero_ahead(Board *b) {
  if (b->aheadDone || app_idle() < 0.3f) return; b->aheadDone = 1;
  char bu[512];
  if (b->kind == 1) {
    int bd = img_budget_mb() >= 80 ? 2 : 1;
    for (int d = -bd; d <= bd; d++) { int k = DS.cur + d; if (!d || k < 0 || k >= DS.viewN) continue;
      const Item *it = &DS.raw[DS.view[k]]; if (sb_bg_mid(it, bu, sizeof bu)[0]) img_prefetch(bu, 1280, 720); }
    return;
  }
  if (b->rowIdx < 0 || b->rowIdx >= b->n) return;
  Row *r = &b->rows[b->rowIdx];
  for (int d = -2; d <= 3; d++) { int c = r->col + d; if (!d || c < 0 || c >= r->items.n) continue; const Item *it = &r->items.items[c];
    if (d >= -1) { meta_ask(b, b->rowIdx, c); char lu[512]; logo_src(it, lu, sizeof lu); if (lu[0]) img_prefetch_pri(lu, 520, 175); }
    int bd = img_budget_mb() >= 80 ? 2 : 1; if (d >= -bd && d <= bd && sb_bg_mid(it, bu, sizeof bu)[0]) img_prefetch(bu, 1280, 720); }
}
static void hero_tick(Board *b, float dt) {
  tween_tick(&b->heroA, dt); tween_tick(&b->logoIn, dt);
  if (!b->heroSet) return;
  double now = app_now(), el = now - b->heroT0;
  if (b->fadeHold > 0 && (now >= b->fadeHold || desc_ready(b, b->view.desc))) { b->fadeHold = 0; tween_to(&b->heroA, 1, fade_in(b), EASE_CSS_EASEOUT); }
  if (b->heroPhase == HP_GATHER) {
    if (b->kind == 1) { if (el >= b->heroFo + 0.01) { b->heroPhase = HP_IDLE; hero_put(b); } return; }
    const Item *it = &b->hero; char lu[512]; logo_src(it, lu, sizeof lu);
    int loc = tmdb_wait(&b->hw, it->type, it->id, 1, TMDB_WAIT_HERO), logo = !lu[0] || img_loaded(lu) || img_failed(lu), meta = it->description[0] != 0 && sb_meta_cached(it->type, it->id) != NULL;
    if (lu[0] && !logo) img_get(lu, 520, 175, NULL);
    if (loc && logo && meta) { const TmdbLoc *L = tmdb_loc(it->type, it->id, 1); meta = desc_ready(b, L && L->overview && L->overview[0] ? L->overview : it->description); }
    if (hero_live(it)) { if (el >= b->heroFo + 0.01 && (logo || el >= 1.5)) { b->heroPhase = HP_IDLE; hero_put(b); } return; }
    if (el >= b->heroFo + 0.01 && ((loc && logo && meta) || el >= 0.45)) { b->heroPhase = HP_IDLE; hero_put(b); }
    return;
  }
  if (b->heroPhase == HP_IDLE && b->heroShown) { hero_ahead(b);
    const Item *it = &b->hero; const TmdbLoc *L = tmdb_loc(it->type, it->id, 1);
    const void *ln = L ? (const void *)L->name : NULL, *lo = L ? (const void *)L->overview : NULL, *ll = L ? (const void *)L->logo : NULL;
    int moved = b->inVer != b->heroVer || b->inL != L || b->inLn != ln || b->inLo != lo || b->inLl != ll;
    if (moved) { logo_src(it, b->inLogo, sizeof b->inLogo); b->inVer = b->heroVer; b->inL = L; b->inLn = ln; b->inLo = lo; b->inLl = ll; }
    int st = !b->inLogo[0] ? 0 : img_failed(b->inLogo) ? 1 : img_loaded(b->inLogo) ? 2 : 3, wait = st == 3 && app_now() - b->heroT0 < logo_wait(it);
    if (st != b->inSt || wait != b->inWait) { b->inSt = st; b->inWait = wait; moved = 1; }
    if (moved) { HeroView v; hero_view(b, &v); if (v.sig != b->view.sig) { b->heroPhase = HP_LATER_WAIT; b->laterAt = now + 0.06; } }
    else if (st == 3 && b->kind != 1) img_get(b->inLogo, 520, 175, NULL);
    return; }
  if (b->heroPhase == HP_LATER_WAIT && now >= b->laterAt) { b->heroPhase = HP_LATER_OUT; b->putAt = now + fade_out(b) + 0.02; b->fadeHold = 0; tween_to(&b->heroA, 0, fade_out(b), EASE_CSS_EASEIN); return; }
  if (b->heroPhase == HP_LATER_OUT && now >= b->putAt) { b->heroPhase = HP_IDLE; hero_put(b); }
}
/* the logo fitted in its box (never larger than it was drawn at: 520x175) */
static int logo_fit(const char *u, float maxW, float maxH, float *w, float *h, GLuint *t) {
  float asp = 0; *t = u[0] ? img_get(u, 520, 175, &asp) : 0; if (!*t || asp <= 0) return 0;
  float W = 520, H = W / asp; if (H > 175) { H = 175; W = H * asp; }
  if (W > maxW) { W = maxW; H = W / asp; } if (H > maxH) { H = maxH; W = H * asp; }
  *w = W; *h = H; return 1;
}
static HeroLay *hero_lay(Board *b) {
  HeroLay *l = &b->lay; HeroView *v = &b->view;
  if (l->ver == b->viewVer && l->lg == i18n_gen()) return l;
  l->ver = b->viewVer; l->lg = i18n_gen();
  l->homeName = text_wrap_count(v->name, HERO_W, 2, 76, 800, -1.6f);
  meta_prep(&l->mp, v->metaLine, NULL, v->metaName, META_PX);
  return l;
}
static struct { char id[SB_ID]; double nameAt, logoAt; } HX[2];
static float hx_k(double t0, double d) { if (t0 <= 0) return 0; float x = (float)((app_now() - t0) / d); x = x < 0 ? 0 : x > 1 ? 1 : x; return x * x * (3 - 2 * x); }
static void hx_step(int k, const char *id, int logo, int nameShows, float *la, float *na) {
  if (strcmp(HX[k].id, id)) { snprintf(HX[k].id, sizeof HX[k].id, "%s", id); HX[k].nameAt = HX[k].logoAt = 0; }
  if (logo && !HX[k].logoAt) HX[k].logoAt = app_now(); if (!logo && nameShows && !HX[k].nameAt) HX[k].nameAt = app_now();
  float lk = logo ? hx_k(HX[k].logoAt, 0.3) : 0, nk = hx_k(HX[k].nameAt, 0.25);
  *la = lk; *na = logo ? nk * (1 - lk) : nk; }
static void hero_draw(Board *b) {
  if (b->kind == 1) return; /* Discover draws its own clean filter bar + focused info bar */
  float melt = btr_melt(), ta = 1 - 0.22f * melt, ma = 1 - 0.9f * melt;
  if (b->kind == 0) { static int pw; int po = player_open(); if (pw && !po && cur() == b) hero_soon(b, 0); pw = po; }
  if (b->kind == 0) { /* a live title: the channels' wall behind the hero (faded in and out on its own) */
    static float lk; static double lt; double now = app_now(); float dt = lt > 0 ? (float)(now - lt) : 0; lt = now; if (dt > 0.1f) dt = 0.1f;
    { const Item *f = cur() == b ? focused_of(b) : NULL;
      if (f && live_type(f->type) && strcmp(f->id, LIVE_ALL_ID) && !topNav.active && !app_overlay && !saver_on()) lpk_want(f, app_rtl() ? 64 : SB_W - 64 - LPK_W, LPK_Y, LPK_W, LPK_H); }
    int live = b->heroSet && live_type(b->view.type); lk += ((float)live - lk) * fminf(1, dt * 5); if (lk > 0.003f) livewall_hero(lk);
    { const Item *f = cur() == b && !topNav.active ? focused_of(b) : NULL; int ch = f && live_type(f->type) && strcmp(f->id, LIVE_ALL_ID);
      static float bxa; static Item last; if (ch) last = *f; bxa += ((ch ? 1.f : 0.f) - bxa) * fminf(1, dt * 6);
      if (bxa > 0.003f && last.id[0]) { char lu[512]; sb_logo_of(&last, lu, sizeof lu); lpk_draw(last.id, lu, app_rtl() ? 64 : SB_W - 64 - LPK_W, LPK_Y, LPK_W, LPK_H, 28, bxa, RGB(22, 20, 30), RGB(12, 11, 17)); } } }
  if (!b->heroSet) return; float a = b->heroA.v; if (a <= 0.001f) return;
  float rv = app_reveal_x(0, 0.5f), ry = 18 * (1 - ease_apply(EASE_WEB, rv)), ra = ease_apply(EASE_CSS_EASE, app_reveal_x(0, 0.45f));
  gfx_opacity_push(a * ra); gfx_offset_push(0, ry);
  int rtl = app_rtl(); float x = rtl ? SB_W - HERO_X : HERO_X, y = HERO_Y; int align = rtl ? ALIGN_RIGHT : ALIGN_LEFT;
  HeroView *v = &b->view; HeroLay *hl = hero_lay(b); float lw, lh; GLuint lt;
  if (!strcmp(v->id, LIVE_ALL_ID)) { /* All channels: LIVE and what is in it, its wordmark, a line under */
    int tot = 0, nc = live_catalogs(); for (int c = 0; c < nc; c++) { int n = live_catalog(c, NULL, NULL); if (n > 0) tot += n; }
    /* over the words: LIVE with its breathing dot, then what is in it */
    static float lw; static unsigned lg; if (!lw || lg != i18n_gen()) { lg = i18n_gen(); lw = text_width(T("LIVE"), 24, 800, 4.0f); }
    float lh = text_line_height(24), pu = 0.5f + 0.5f * sinf((float)app_now() * 3.0f), dx = rtl ? x - 8 : x + 8;
    gfx_circle(dx, y + lh / 2, 8 + 7 * pu, RGBA(0xff, 0x3b, 0x3b, (int)(70 * (1 - pu)))); gfx_circle(dx, y + lh / 2, 8, RGB(0xff, 0x3b, 0x3b));
    float lx = rtl ? x - 30 : x + 30; text_draw(T("LIVE"), lx, y, 24, 800, WHITE_A(255), 4.0f, 0, align);
    char nb[16], cb[16], line[160]; snprintf(nb, sizeof nb, "%d", tot); snprintf(cb, sizeof cb, "%d", nc);
    snprintf(line, sizeof line, "%s  ·  %s", T1("{n} channels", "n", nb), nc == 1 ? T("1 catalog") : T1("{n} catalogs", "n", cb));
    { static XLine xl; static WlSlot ws; static unsigned gen; static int had; if (had && !tot) gen++; had = tot > 0; /* (a light line over "Loading channels…" while they come, core/waitlines; the count crossfades in) */
      xline_draw_w(&xl, tot ? line : xline_wait(&ws, gen, T("Loading channels…"), 1, WL_LIVE, WL_ACT_NONE, 0), rtl ? lx - lw - 22 : lx + lw + 22, y + lh / 2 - text_line_height(26) / 2, 26, 500, 0.5f, WHITE_A(190), HERO_W, align); }
    y += lh + 14;
    y += livewall_word(x, y, 112, HERO_W, align) + 18;
    text_draw(T("All your live add-ons, in one place."), x, y, DESC_PX, 400, WHITE_A(204), 0, HERO_W, align);
    gfx_offset_pop(); gfx_opacity_pop(); return; }
  gfx_opacity_push(ta);
  { int hasLogo = logo_fit(v->logo, 480, 150, &lw, &lh, &lt); float la, na; hx_step(0, v->id, hasLogo, !v->wait, &la, &na); /* (the name and the art crossfade: hx_step) */
    if (hasLogo) { if (na > 0.003f) { gfx_opacity_push(na); text_draw_wrapped(v->name, x, y, HERO_W, 2, 76, 800, WHITE_A(255), -1.6f, align, 76 * 1.04f); gfx_opacity_pop(); }
      char sk[80]; snprintf(sk, sizeof sk, "logo:%s", v->id); sx_image(sk, lt, rtl ? x - lw : x, y, lw, lh, 0, WHITE_A((int)(255 * b->logoIn.v * la)), 0, 0, 1, 1); y += lh; } /* a little smaller: the rows get room */
    else { int lines; if (v->wait) lines = hl->homeName; else { gfx_opacity_push(na); lines = sx_text_wrapped(hero_key(b), v->name, x, y, HERO_W, 2, 76, 800, WHITE_A(255), -1.6f, align, 76 * 1.04f); gfx_opacity_pop(); } y += lines * 76 * 1.04f; } } /* text art on its way: the name waits unseen, then fades in */
  gfx_opacity_pop(); gfx_opacity_push(ma);
  if (live_type(v->type)) { /* a channel: its logo and its state under it (live_mark: LIVE once a stream of it works, Off air when none does), nothing else */
    static float k3[3]; static char kid[SB_ID]; if (strcmp(kid, v->id)) { snprintf(kid, sizeof kid, "%s", v->id); k3[0] = k3[1] = k3[2] = 0; } /* each channel's mark from nothing: it fades in once its own try says, never the last one's */
    { const char *ids[1] = { v->id }, *tys[1] = { v->type }; live_probe_visible(ids, tys, 1); } /* (tried now, not only once its preview starts) */
    y += 28; float cw = live_mark(x, y, 1.28f, rtl, k3, live_reach(v->id)), ch = 36 * 1.28f, cx0 = rtl ? x - cw : x;
    const char *cat = live_catalog_of(v->id); char cb[96] = ""; /* where it comes from, quietly, beside it (its catalog's name, not its type) */
    if (cat) { snprintf(cb, sizeof cb, "%s", cat); char *d = strrchr(cb, '\xc2'); size_t n = strlen(cb);
      if (d && (!strcmp(d, "\xc2\xb7 tv") || !strcmp(d, "\xc2\xb7 channel"))) { *d = 0; n = strlen(cb); while (n && cb[n - 1] == ' ') cb[--n] = 0; } }
    if (cb[0]) text_draw(cb, rtl ? cx0 - 22 : cx0 + cw + 22, y + ch / 2 - text_line_height(26) / 2, 26, 500, WHITE_A(175), 0.3f, HERO_W - cw - 22, align);
    gfx_opacity_pop(); gfx_offset_pop(); gfx_opacity_pop(); return; } /* (the box is drawn on its own, above) */
  y += 20; meta_draw_p(&hl->mp, x, y, WHITE_A(255), align); y += text_line_height(META_PX) > 34 ? text_line_height(META_PX) : 34;
  y += 16; text_draw_wrapped(v->desc, x, y, HERO_W, 3, DESC_PX, 400, WHITE_A(204), 0, align, DESC_LH);
  gfx_opacity_pop();
  gfx_offset_pop(); gfx_opacity_pop();
}

/* ---- the screen ---- */
static void board_enter_k(Board *b) {
  int fresh = !b->built || (b->kind == 0 && b->libVer != lib_version()), keep = b->n > 0;
  if (fresh) { keepItems = b->built && keep && b->kind != 1; b->built = 1; b->libVer = lib_version(); load_catalog_rows(b, b->kind == 1, keep); keepItems = 0; }
  if (app_splash_done) timer_start(&b->fade, 0.5f);
  if (b->kind != 1) {
    for (int i = 0; i < b->n; i++) { slide_row(&b->rows[i]); tween_set(&b->rows[i].off, b->rows[i].off.to); }
    tween_set(&b->hostY, 0);
  } else {
    disc_sync_scroll(1);
  }
  b->fastBg = 1; hero_soon(b, 70);
}
static void board_leave(void) {
  for (int i = 0; i < 2; i++) { Board *b = &boards[i]; if (cur() == b) continue;
    tween_set(&b->heroA, 0); b->heroShown = 0; b->heroPhase = HP_IDLE; b->heroSet = 0; b->fadeHold = 0; }
}
static void enter_home(const void *p) { (void)p; board_leave(); board_enter_k(&boards[0]); }
static void enter_disc(const void *p) { (void)p; board_leave(); board_enter_k(&boards[1]); }
static void on_page(ItemList *l, int more, void *ud) {
  uintptr_t t = (uintptr_t)ud; Board *b = &boards[(t >> 31) & 1]; int row = t & 127; unsigned gen = (t >> 7) & 0xffffff;
  if ((b->gen & 0xffffff) != gen || row >= b->n) { itemlist_free(l); return; }
  Row *r = &b->rows[row]; if (!r->paging) { itemlist_free(l); return; } r->paging = 0;
  if (!l) { r->pageAt = app_now() + 10; return; }
  int got = l->n;
  int add = 0; Item *grow = realloc(r->items.items, (size_t)(r->items.n + l->n + 1) * sizeof(Item));
  if (grow) { r->items.items = grow;
    for (int i = 0; i < l->n; i++) { int dup = 0; for (int k = 0; k < r->items.n && !dup; k++) dup = !strcmp(r->items.items[k].id, l->items[i].id); if (!dup && l->items[i].id[0]) r->items.items[r->items.n++] = l->items[i], add++; } }
  if (!add && got && r->skipAlt > r->skipAt) { r->skipAt = r->skipAlt; r->skipAlt = 0; itemlist_free(l); r->paging = 1; sb_catalog_page(&r->def, r->skipAt, on_page, tok(b, row)); return; }
  r->skipAlt = 0; r->skipAt += l->total > got ? l->total : got; if (!got || !add || more == 0 || r->items.n >= 1000) r->more = 0;
  if (getenv("SB_SLOWLOG")) fprintf(stderr, "[cat] page of %s: %d new, %d in the row%s\n", r->title, add, r->items.n, r->more ? "" : " (the end)");
  itemlist_free(l);
}
static void row_more(Board *b, int ri) {
  if (ri < 0 || ri >= b->n) return; Row *r = &b->rows[ri];
  if (!r->more || r->paging || r->items.n - r->col > 8 || app_now() < r->pageAt) return;
  r->paging = 1; sb_catalog_page(&r->def, r->skipAt, on_page, tok(b, ri));
}
static void move_h(Board *b, int dir) {
  Row *r = &b->rows[b->rowIdx]; int old = r->col;
  r->col = dir > 0 ? (r->col + 1 < r->items.n ? r->col + 1 : r->col) : (r->col > 0 ? r->col - 1 : 0);
  if (r->col == old) { nudge_start(&r->nudge, dir > 0 ? K_RIGHT : K_LEFT); return; }
  slide_row(r); hero_soon(b, 150); row_more(b, b->rowIdx);
}

/* ============================================================================================
   DISCOVER FILTER BAR (5 PILLS ON ONE LINE: TYPE, SUBSECTION, DATE, SORT, GENRE) & 5x3 GRID
   ============================================================================================ */
static int curOpt; static char optType[20][16];
static int disc_genres(char (*out)[48], int max, int *req) { return disc_cat_genres(out, max, req); }
static const char *type_txt(const char *t) {
  if (!strcmp(t, "*")) return T("All");
  if (!strcmp(t, "movie")) return T("Films");
  if (!strcmp(t, "series")) return T("Series");
  if (!strcmp(t, "anime")) return T("Anime");
  return t;
}
static int filt_chips(void) { char g[2][48]; int req; return disc_genres(g, 2, &req) > 0 ? 5 : 4; }
static const char *chip_txt(int i, char *buf, size_t cap) {
  if (FL.open && FL.which == i && FL.idx >= 0 && FL.idx < FL.n) {
    if (i == 1) { int nc = disc_ensure_cache(); if (FL.idx < nc) { snprintf(buf, cap, "%s", FL.cats[FL.idx].title); return buf; } }
    return FL.opt[FL.idx];
  }
  if (i == 0) return type_txt(FL.type[0] ? FL.type : "*");
  if (i == 1) { int nc = disc_ensure_cache(); if (!nc) return T("Subsection"); snprintf(buf, cap, "%s", FL.cats[FL.cat < nc ? FL.cat : 0].title); return buf; }
  if (i == 2) { date_opt_label(FL.dateMode, buf, cap); return buf; }
  if (i == 3) {
    if (FL.sortMode == 0) { snprintf(buf, cap, "%s: %s", T("Sort"), T("Default")); return buf; }
    snprintf(buf, cap, "%s", sort_opt_label(FL.sortMode)); return buf;
  }
  return FL.genre[0] ? FL.genre : T("All genres");
}
static int chip_is_active_filter(int i) {
  if (i == 2) return FL.dateMode != 0;
  if (i == 3) return FL.sortMode != 0;
  if (i == 4) return FL.genre[0] != 0;
  return 0;
}

#define FL_PAD 10.0f
#define OPT_TW 440.0f
#define OPT_H 44.0f
#define OPT_PX 21
#define OPT_PAD 16.0f
static void chev_draw(float cx, float cy, float k, int rtl, uint32_t col) {
  float an = rtl ? 1.5708f + 3.1416f * k : -1.5708f - 3.1416f * k, c = cosf(an), sn = sinf(an);
  const float P[3][2] = { { -6.0f, -3.0f }, { 0, 3.0f }, { 6.0f, -3.0f } };
  for (int sgm = 0; sgm < 2; sgm++) for (int q = 0; q <= 10; q++) {
    float t = q / 10.0f, x = P[sgm][0] + (P[sgm + 1][0] - P[sgm][0]) * t, y = P[sgm][1] + (P[sgm + 1][1] - P[sgm][1]) * t;
    gfx_circle(cx + x * c - y * sn, cy + x * sn + y * c, 1.4f, col);
  }
}
static float filt_list_w(void) { float w = FL.listW, m = FL.roomMax > 180 ? FL.roomMax : 760; return w < m ? w : m; }
static void hl_rect(int c, float *x, float *w) {
  if (c < 100) { int i = c < FL.nchip ? c : 0; *x = FL.cx[i]; *w = FL.cw[i]; return; }
  int j = c - 100; if (j < 0 || j >= FL.n) { *x = FL.cx[FL.which < FL.nchip ? FL.which : 0]; *w = 0; return; }
  float k = ease_apply(EASE_CSS_EASEOUT, FL.a.v), ox = FL.ox[j] - FL.sxd - 24 * (1 - k); *w = FL.ow[j]; *x = app_rtl() ? FL.rx0 + FL.rroom - ox - *w : FL.rx0 + ox;
}
static float hl_over(float hx, float hw, float x, float w) { float l = hx > x ? hx : x, r = hx + hw < x + w ? hx + hw : x + w; return w > 0 && r > l ? (r - l) / w : 0; }
static void filt_chip_focus(int a, int c) {
  (void)a; Board *b = cur(); if (!b || b->kind != 1) return;
  if (FL.open) { if (c == FL.which) FL.idx = -1; return; }
  topNav.active = 0; DS.onFilt = 1; FL.filtCol = c;
}
static void filt_open(int which) {
  FL.which = which; FL.n = 0; FL.idx = 0; curOpt = 0;
  if (which == 0) {
    char tt[16][16]; int nt = sb_disc_types(tt, 16), k = 0;
    if (nt >= 2) snprintf(optType[k++], 16, "*");
    for (int i = 0; i < nt && k < 20; i++) snprintf(optType[k++], 16, "%s", tt[i]);
    for (int i = 0; i < k && i < 200; i++) { snprintf(FL.opt[i], sizeof FL.opt[i], "%s", type_txt(optType[i])); if (!strcmp(optType[i], FL.type)) curOpt = i; }
    FL.n = k;
  } else if (which == 1) {
    int nc = disc_ensure_cache();
    for (int i = 0; i < nc && i < 200; i++) snprintf(FL.opt[i], sizeof FL.opt[i], "%s%s%s", FL.cats[i].title, FL.catAddon[i][0] ? " · " : "", FL.catAddon[i]);
    FL.n = nc; curOpt = FL.cat < nc ? FL.cat : 0;
  } else if (which == 2) {
    for (int i = 0; i < DATE_OPTS_N; i++) date_opt_label(i, FL.opt[i], sizeof FL.opt[i]);
    FL.n = DATE_OPTS_N; curOpt = FL.dateMode;
  } else if (which == 3) {
    for (int i = 0; i < SORT_OPTS_N; i++) snprintf(FL.opt[i], sizeof FL.opt[i], "%s", sort_opt_label(i));
    FL.n = SORT_OPTS_N; curOpt = FL.sortMode;
  } else {
    int req = 0, k = 0; char (*g)[48] = calloc(199, sizeof *g);
    int ng = g ? disc_genres(g, 199, &req) : 0;
    if (!req) snprintf(FL.opt[k++], sizeof FL.opt[k], "%s", T("All genres"));
    for (int i = 0; i < ng && k < 200; i++) { snprintf(FL.opt[k], sizeof FL.opt[k], "%s", g[i]); if (!strcmp(g[i], FL.genre)) curOpt = k; k++; }
    free(g); FL.n = k;
  }
  FL.idx = curOpt; FL.listW = 0;
  for (int i = 0; i < FL.n; i++) {
    float tw = text_width(FL.opt[i], OPT_PX, 700, 0);
    FL.ow[i] = (tw < OPT_TW ? tw : OPT_TW) + 2 * OPT_PAD;
    FL.ox[i] = FL.listW; FL.listW += FL.ow[i] + (i + 1 < FL.n ? 6 : 0);
  }
  tween_set(&FL.sx, 0);
  FL.open = 1; tween_set(&FL.a, 0); tween_to(&FL.a, 1, 0.30f, EASE_CSS_EASEOUT); sound_focus();
}
static void filt_close(void) { if (!FL.open) return; FL.open = 0; tween_to(&FL.a, 0, 0.24f, EASE_IN_OUT); }
static void genre_default(void) { int req; char g[1][48]; FL.genre[0] = 0; if (disc_genres(g, 1, &req) > 0 && req) snprintf(FL.genre, sizeof FL.genre, "%s", g[0]); }
static int filt_set(int i) {
  if (i < 0 || i >= FL.n) return 0;
  int ch = 0;
  if (FL.which == 0) {
    if (i < 20 && strcmp(optType[i], FL.type)) { snprintf(FL.type, sizeof FL.type, "%s", optType[i]); FL.cat = 0; FL.cacheValid = 0; genre_default(); ch = 2; }
  } else if (FL.which == 1) {
    if (i != FL.cat) { FL.cat = i; genre_default(); ch = 2; }
  } else if (FL.which == 2) {
    if (i != FL.dateMode) { FL.dateMode = i; DS.autoPages = 0; ch = 1; }
  } else if (FL.which == 3) {
    if (i != FL.sortMode) { FL.sortMode = i; ch = 1; }
  } else {
    int req = 0; char g[1][48]; disc_genres(g, 1, &req);
    const char *v = !req && i == 0 ? "" : FL.opt[i];
    if (strcmp(v, FL.genre)) { snprintf(FL.genre, sizeof FL.genre, "%s", v); ch = 2; }
  }
  if (ch) curOpt = i;
  return ch;
}
static void filt_apply(int ch) {
  if (ch == 2) { disc_load(); timer_start(&boards[1].fade, 0.35f); }
  else if (ch == 1) { disc_rebuild_view(1); }
}
static void filt_choose(void) {
  int i = FL.idx; if (i < 0 || i >= FL.n) { filt_close(); return; }
  int ch = filt_set(i); filt_close(); sound_play(SND_TOGGLE);
  if (ch) filt_apply(ch);
}
static void filt_live(void) {
  if (!FL.open || FL.idx < 0 || FL.idx == curOpt || app_now() - FL.restAt < 0.65) return;
  int ch = filt_set(FL.idx);
  if (ch) filt_apply(ch);
}
static void filt_pick(int i, int bb) { (void)bb; if (!FL.open) return; if (FL.idx != i) { FL.idx = i; FL.restAt = app_now(); return; } filt_choose(); }

static void filt_bar_draw(int active) {
  int rtl = app_rtl(), n = filt_chips(); if (FL.filtCol >= n) FL.filtCol = n - 1;
  float h = DISC_FILT_H, cy = DISC_FILT_Y;
  char buf[5][96]; const char *t[5]; float tw[5], w[5], tot = 0;
  FL.y = cy; FL.nchip = n;
  for (int i = 0; i < n; i++) {
    t[i] = chip_txt(i, buf[i], sizeof buf[i]);
    tw[i] = text_width(t[i], 22, 700, 0);
    if (tw[i] > 300) tw[i] = 300;
    float ww = 22 + tw[i] + 8 + 16 + 22;
    if (FL.wt[i].v < 1) tween_set(&FL.wt[i], ww);
    else if (fabsf(FL.wt[i].to - ww) > 0.5f) tween_to(&FL.wt[i], ww, 0.24f, EASE_ROW);
    tween_tick(&FL.wt[i], app_dt()); w[i] = FL.wt[i].v;
    tot += w[i] + (i ? 10 : 0);
  }
  FL.roomMax = SB_W - 112 - tot - FL_PAD;
  if (FL.roomMax < 260) FL.roomMax = 260;
  float ka = FL.which < n && FL.a.v > 0.001f ? ease_apply(EASE_CSS_EASEOUT, FL.a.v) : 0;
  float full = ka > 0 ? filt_list_w() : 0, room = (FL_PAD + full) * ka;
  float totalAll = tot + room;
  float x = SB_W / 2 - totalAll / 2; if (x < 48) x = 48;
  for (int i = 0; i < n; i++) {
    float px = rtl ? SB_W - x - w[i] : x;
    FL.cx[i] = px; FL.cw[i] = w[i];
    x += w[i] + 10; if (i == FL.which) x += room;
  }
  if (FL.which < n) {
    float px = FL.cx[FL.which], pw = FL.cw[FL.which];
    FL.rroom = full * ka; FL.rfull = full;
    FL.rx0 = rtl ? px - FL_PAD - FL.rroom : px + pw + FL_PAD;
    float want = strip_drawn(FL.sx.to, FL.listW, full);
    if (FL.open && FL.idx >= 0 && FL.idx < FL.n) want = strip_target(want, FL.ox[FL.idx], FL.ow[FL.idx], FL.listW, full);
    if (fabsf(want - FL.sx.to) > 0.5f) tween_to(&FL.sx, want, 0.24f, EASE_ROW);
    tween_tick(&FL.sx, app_dt()); FL.sxd = strip_drawn(FL.sx.v, FL.listW, full);
  }
  int fi = !active ? -1 : FL.open ? (FL.idx >= 0 ? 100 + FL.idx : FL.which) : FL.filtCol;
  if (fi >= 0 && fi != FL.hTo) { if (FL.ha.v < 0.01f) FL.hFrom = fi; else FL.hFrom = FL.hTo; FL.hTo = fi; tween_set(&FL.hk, 0); tween_to(&FL.hk, 1, GLIDE_DUR, GLIDE_EASE); }
  tween_to(&FL.ha, fi >= 0 ? 1 : 0, 0.16f, EASE_IN_OUT); tween_tick(&FL.hk, app_dt()); tween_tick(&FL.ha, app_dt());
  float hx = 0, hw = 0, hh = h, ha = FL.ha.v;
  { float ax, aw, bx2, bw2; hl_rect(FL.hFrom, &ax, &aw); hl_rect(FL.hTo, &bx2, &bw2); float g = FL.hk.v; if (aw <= 0) { ax = bx2; aw = bw2; }
    hx = ax + (bx2 - ax) * g; hw = aw + (bw2 - aw) * g;
    float ah = FL.hFrom >= 100 ? OPT_H : h, bh = FL.hTo >= 100 ? OPT_H : h; hh = ah + (bh - ah) * g; }
  for (int i = 0; i < n; i++) {
    int set = chip_is_active_filter(i); float px = FL.cx[i];
    gfx_rrect(px, cy, w[i], h, h / 2, RGBA(18, 16, 26, active ? 195 : 165));
    gfx_rrect(px, cy, w[i], h, h / 2, WHITE_A(active ? 18 : 12));
    gfx_ring(px, cy, w[i], h, h / 2, set ? 2 : 1, set ? col_alpha(ACC2 | 0xff000000u, 0.9f) : WHITE_A(42));
  }
  if (ha > 0.001f && hw > 1) {
    float nx = nudge_x(&DS.nudge);
    int inLine = FL.hFrom >= 100 && FL.hTo >= 100 && FL.rroom > 1;
    if (inLine) gfx_clip_push(FL.rx0, cy - 8, FL.rroom, h + 16);
    lglass_accent(hx + nx, cy + (h - hh) / 2, hw, hh, hh / 2, ha);
    if (inLine) gfx_clip_pop();
  }
  for (int i = 0; i < n; i++) {
    int set = chip_is_active_filter(i); float px = FL.cx[i], u = hl_over(hx, hw, px, w[i]) * ha;
    uint32_t tc = col_mix(WHITE_A(set ? 255 : 228), ACC_FG, u);
    float maxTw = w[i] - 22 - 8 - 16 - 22; if (maxTw < 10) maxTw = 10;
    float tx = rtl ? px + w[i] - 22 : px + 22;
    gfx_clip_push(px + 10, cy, w[i] - 20, h);
    text_draw(t[i], tx, cy + h / 2 - text_line_height(22) / 2, 22, 700, tc, 0, maxTw, rtl ? ALIGN_RIGHT : ALIGN_LEFT);
    gfx_clip_pop();
    float ax = rtl ? px + 22 - 2 : px + w[i] - 22 - 16 + 2, ay = cy + h / 2 - 8, rk = i == FL.which ? FL.a.v : 0;
    chev_draw(ax + 8, ay + 8, ease_apply(EASE_CSS_EASEOUT, rk), rtl, tc);
    hit_add(px, cy, w[i], h, filt_chip_focus, NULL, 0, i);
  }
  FL.hx = hx; FL.hw = hw;
  float oa = FL.a.v;
  if (oa > 0.001f && FL.which < n && FL.rroom >= 2) {
    float room2 = FL.rroom, x0 = FL.rx0, y0 = cy;
    int live = FL.open && oa > 0.5f;
    int mb = strip_more_before(FL.sxd), ma = strip_more_after(FL.sxd, room2, FL.listW), fl = rtl ? ma : mb, fr = rtl ? mb : ma;
    gfx_clip_push(x0, y0 - 8, room2, h + 16); gfx_opacity_push(oa * oa);
    gfx_rrect(x0, y0 + (h - OPT_H) / 2, room2, OPT_H, OPT_H / 2, RGBA(14, 13, 22, 195));
    for (int i = 0; i < FL.n; i++) {
      float cx2, ww2; hl_rect(100 + i, &cx2, &ww2);
      if (cx2 + ww2 < x0 - 2 || cx2 > x0 + room2 + 2) continue;
      int isCur = i == curOpt; float u = hl_over(FL.hx, FL.hw, cx2, ww2) * FL.ha.v;
      float oy = y0 + (h - OPT_H) / 2;
      if (isCur && u < 0.5f) { float k = strip_fade_at(cx2 + ww2 / 2, x0, room2, fl, fr); if (k > 0.05f) gfx_ring(cx2, oy, ww2, OPT_H, OPT_H / 2, 1.5f, WHITE_A((int)(75 * k))); }
      text_draw_hfade(FL.opt[i], cx2 + OPT_PAD, oy + OPT_H / 2 - text_line_height(OPT_PX) / 2, OPT_PX, isCur ? 700 : 600, col_mix(WHITE_A(isCur ? 255 : 205), ACC_FG, u), 0, OPT_TW, x0, room2, fl, fr);
      if (live) hit_add(cx2, oy, ww2, OPT_H, filt_pick, NULL, i, 0);
    }
    gfx_opacity_pop(); gfx_clip_pop();
  }
}

static const CardPrep *disc_prep_for(int rawIdx) {
  unsigned tg = tmdb_generation(), lg = i18n_gen();
  DS.prepTick++;
  int empty = -1, lru = 0;
  for (int i = 0; i < DC_PREP_N; i++) {
    if (DS.prep[i].st && DS.prep[i].rawIdx == rawIdx && DS.prep[i].gen == tg && DS.prep[i].lg == lg) {
      DS.prep[i].tick = DS.prepTick;
      return &DS.prep[i].cp;
    }
    if (!DS.prep[i].st && empty < 0) empty = i;
    if (DS.prep[i].tick < DS.prep[lru].tick) lru = i;
  }
  int slot = empty >= 0 ? empty : lru;
  DiscPrep *p = &DS.prep[slot];
  p->rawIdx = rawIdx; p->gen = tg; p->lg = lg; p->st = 1; p->tick = DS.prepTick;
  card_prep(&p->cp, &DS.raw[rawIdx], 1, NULL);
  return &p->cp;
}
static void disc_tile_focus(int idx, int unused) {
  (void)unused;
  if (idx < 0 || idx >= DS.viewN) return;
  int was = topNav.active;
  if (!was && !DS.onFilt && DS.cur == idx) return;
  topNav.active = 0; DS.onFilt = 0; if (FL.open) filt_close();
  DS.cur = idx;
  hero_soon(&boards[1], 120);
  disc_more();
}
static int discMkRaw = -1;
static unsigned discMkGen;
static void disc_draw_tile(int vi, float x, float y, int focused, float alpha, float melt) {
  if (alpha <= 0.001f) return;
  int rawIdx = DS.view[vi];
  const Item *it = &DS.raw[rawIdx];
  const CardPrep *cp = disc_prep_for(rawIdx);
  unsigned long long lk = lift_key(&DS, rawIdx);
  float hp = focused ? app_hold_progress() : 0;
  if (alpha < 0.999f) gfx_opacity_push(alpha);
  lift_begin_z(lk, focused, hp, x, y, DISC_TW, DISC_TH, CARD_R, 0.065f);
  card_draw_prep(it, cp, x, y, DISC_TW, DISC_TH, 1, 0, 1);
  int rtl = app_rtl();
  int yr = item_year(it); float rt = item_rating(it);
  float tagA = focused ? (1.0f - melt) : 1.0f;
  if ((yr > 0 || rt > 0) && tagA > 0.01f) {
    char tag[32];
    if (yr > 0 && rt > 0) snprintf(tag, sizeof tag, "%d · %.1f", yr, rt);
    else if (yr > 0) snprintf(tag, sizeof tag, "%d", yr);
    else snprintf(tag, sizeof tag, "IMDb %.1f", rt);
    float tw = text_width(tag, 18, 700, 0.1f) + 20, th = 28;
    float tx = rtl ? x + DISC_TW - 10 - tw : x + 10, ty = y + 10;
    if (tagA < 0.999f) gfx_opacity_push(tagA);
    gfx_rrect(tx, ty, tw, th, th / 2, RGBA(10, 10, 16, 190));
    gfx_ring(tx, ty, tw, th, th / 2, 1, WHITE_A(36));
    text_draw(tag, tx + tw / 2, ty + (th - text_line_height(18)) / 2, 18, 700, WHITE_A(235), 0.1f, 0, ALIGN_CENTER);
    if (tagA < 0.999f) gfx_opacity_pop();
  }
  int watched = lib_item_watched(it), inLib = lib_has(it->id);
  float bx = rtl ? x + 10 : x + DISC_TW - 10 - 34, by = y + 10;
  if (inLib) {
    float sx = watched ? (rtl ? bx + 40 : bx - 40) : bx;
    gfx_circle(sx + 17, by + 17, 17, ACC | 0xff000000u);
    icon_draw("saved", sx + 8, by + 8, 18, ACC_FG);
  }
  if (watched) {
    gfx_circle(bx + 17, by + 17, 17, ACC | 0xff000000u);
    icon_draw("check", bx + 7, by + 7, 20, ACC_FG);
  }
  if (hp > 0) hold_trace_draw(x, y, DISC_TW, DISC_TH, CARD_R, ease_apply(EASE_IN_OUT, hp));
  if (focused && btr_playing() && melt > 0.9f) { discMkRaw = rawIdx; discMkGen = DS.gen; }
  if (melt > 0.01f && discMkGen == DS.gen && discMkRaw == rawIdx) board_playing_mark(x, y, DISC_TH, melt);
  lift_end(lk, x, y, DISC_TW, DISC_TH, CARD_R);
  if (alpha < 0.999f) gfx_opacity_pop();
}
static void disc_draw(Board *b) {
  int rtl = app_rtl();
  int active = focusOn(b);
  float rv = app_reveal_x(0, 0.45f), ra = ease_apply(EASE_CSS_EASE, rv);
  /* Background trailer playing: melt back scrim, filter bar, and non-focused tiles like Home while keeping the presenting tile crisp */
  float melt = btr_melt();
  float dimK = 1.0f - 0.86f * melt;
  gfx_opacity_push(ra);
  if (melt < 0.999f) {
    if (melt > 0.001f) gfx_opacity_push(1.0f - melt);
    gfx_rrect_v(0, 90, SB_W, SB_H - 90, 0, RGBA(12, 11, 17, 155), RGBA(12, 11, 17, 215));
    if (melt > 0.001f) gfx_opacity_pop();
  }
  {
    const Item *it = !DS.onFilt ? disc_focused() : NULL;
    float lx = rtl ? SB_W - DISC_X0 : DISC_X0, rx = rtl ? DISC_X0 : SB_W - DISC_X0;
    int al = rtl ? ALIGN_RIGHT : ALIGN_LEFT, ar = rtl ? ALIGN_LEFT : ALIGN_RIGHT;
    if (it) {
      const TmdbLoc *L = tmdb_loc(it->type, it->id, 1);
      const char *nm = (L && L->name && L->name[0]) ? L->name : it->name;
      float nmW = text_width(nm, 28, 700, -0.3f); if (nmW > 860) nmW = 860;
      float ta = 1.0f - 0.22f * melt, ma = 1.0f - 0.90f * melt;
      if (ta < 0.999f) gfx_opacity_push(ta);
      text_draw(nm, lx, DISC_INFO_Y + 4, 28, 700, WHITE_A(250), -0.3f, 860, al);
      if (ta < 0.999f) gfx_opacity_pop();
      if (it->metaLine[0] && ma > 0.01f) {
        float mx = rtl ? lx - nmW - 22 : lx + nmW + 22;
        if (ma < 0.999f) gfx_opacity_push(ma);
        text_draw(it->metaLine, mx, DISC_INFO_Y + 9, 21, 500, WHITE_A(175), 0.1f, 520, al);
        if (ma < 0.999f) gfx_opacity_pop();
      }
    } else if (dimK > 0.01f) {
      char catBuf[96]; const char *cn = chip_txt(1, catBuf, sizeof catBuf);
      char sub[192];
      snprintf(sub, sizeof sub, "%s · %s%s%s", type_txt(FL.type[0] ? FL.type : "*"), cn, FL.genre[0] ? " · " : "", FL.genre);
      if (dimK < 0.999f) gfx_opacity_push(dimK);
      text_draw(sub, lx, DISC_INFO_Y + 7, 24, 600, WHITE_A(205), -0.2f, 1100, al);
      if (dimK < 0.999f) gfx_opacity_pop();
    }
    if (DS.viewN > 0 && dimK > 0.01f) {
      int rows = (DS.viewN + DISC_COLS - 1) / DISC_COLS;
      int curRow = (!DS.onFilt ? DS.cur / DISC_COLS : DS.scrollRow) + 1;
      char st[64];
      snprintf(st, sizeof st, "%s %d / %d  ·  %d%s", T("Row"), curRow, rows, DS.viewN, DS.more ? "+" : "");
      if (dimK < 0.999f) gfx_opacity_push(dimK);
      text_draw(st, rx, DISC_INFO_Y + 9, 21, 600, WHITE_A(145), 0.2f, 420, ar);
      if (dimK < 0.999f) gfx_opacity_pop();
    }
  }
  gfx_clip_push(0, DISC_GRID_TOP, SB_W, DISC_GRID_H);
  float baseY = DISC_GRID_TOP + DISC_GRID_PAD + DS.scrollY.v;
  if (DS.viewN == 0) {
    if (!DS.loaded || DS.paging) {
      for (int r = 0; r < DISC_VIS_ROWS; r++) for (int c = 0; c < DISC_COLS; c++) {
        float x = DISC_X0 + c * (DISC_TW + DISC_GAP_X); if (rtl) x = SB_W - x - DISC_TW;
        float y = DISC_GRID_TOP + DISC_GRID_PAD + r * DISC_PITCH;
        ph_box(x, y, DISC_TW, DISC_TH, CARD_R);
      }
    } else {
      const char *h = T("No titles match these filters");
      const char *s = T("Try another date, genre or subsection");
      text_draw(h, SB_W / 2, DISC_GRID_TOP + 280, 34, 700, WHITE_A(220), -0.3f, SB_W - 200, ALIGN_CENTER);
      text_draw(s, SB_W / 2, DISC_GRID_TOP + 330, 24, 400, WHITE_A(150), 0, SB_W - 200, ALIGN_CENTER);
    }
  } else {
    int totalRows = (DS.viewN + DISC_COLS - 1) / DISC_COLS;
    int r0 = (int)floorf((-DS.scrollY.v - DISC_GRID_PAD - DISC_PITCH) / DISC_PITCH);
    int r1 = (int)ceilf((-DS.scrollY.v + DISC_GRID_H) / DISC_PITCH);
    if (r0 < 0) r0 = 0; if (r1 >= totalRows) r1 = totalRows - 1;
    int focIdx = (active && !DS.onFilt && DS.cur >= 0 && DS.cur < DS.viewN) ? DS.cur : -1;
    float nx = nudge_x(&DS.nudge), ny = nudge_y(&DS.nudge);
    gfx_offset_push(nx, ny);
    lift_fade(1.0f - melt);
    for (int r = r0; r <= r1; r++) {
      float ry = baseY + r * DISC_PITCH;
      float rk = 1, rdy = 0;
      if (b->fade.on) {
        int visRow = r - DS.scrollRow; if (visRow < 0) visRow = 0; if (visRow > 3) visRow = 3;
        float t = (timer_x(&b->fade) * 0.5f - 0.05f * visRow) / 0.34f;
        rk = ease_apply(EASE_CSS_EASEOUT, t < 0 ? 0 : t > 1 ? 1 : t);
        rdy = 22 * (1 - rk);
      }
      for (int c = 0; c < DISC_COLS; c++) {
        int vi = r * DISC_COLS + c; if (vi >= DS.viewN) break;
        float x = DISC_X0 + c * (DISC_TW + DISC_GAP_X); if (rtl) x = SB_W - x - DISC_TW;
        hit_add(x, ry + rdy, DISC_TW, DISC_TH, disc_tile_focus, NULL, vi, 0);
        if (vi == focIdx) continue;
        disc_draw_tile(vi, x, ry + rdy, 0, rk * dimK, melt);
      }
    }
    if (focIdx >= 0) {
      int fr = focIdx / DISC_COLS, fc = focIdx % DISC_COLS;
      float x = DISC_X0 + fc * (DISC_TW + DISC_GAP_X); if (rtl) x = SB_W - x - DISC_TW;
      float ry = baseY + fr * DISC_PITCH;
      disc_draw_tile(focIdx, x, ry, 1, 1.0f - 0.15f * melt, melt);
    }
    lift_fade(1.0f);
    if (DS.paging && dimK > 0.01f) {
      float py = baseY + totalRows * DISC_PITCH;
      if (py < DISC_GRID_TOP + DISC_GRID_H) {
        if (dimK < 0.999f) gfx_opacity_push(dimK);
        for (int c = 0; c < DISC_COLS; c++) {
          float x = DISC_X0 + c * (DISC_TW + DISC_GAP_X); if (rtl) x = SB_W - x - DISC_TW;
          ph_box(x, py, DISC_TW, DISC_TH, CARD_R);
        }
        if (dimK < 0.999f) gfx_opacity_pop();
      }
    }
    gfx_offset_pop();
    {
      int p0 = (r1 + 1) * DISC_COLS, p1 = p0 + DISC_COLS * 2 - 1;
      if (p1 >= DS.viewN) p1 = DS.viewN - 1;
      img_prefetch_mode(1);
      for (int vi = p0; vi <= p1; vi++) {
        const CardPrep *cp = disc_prep_for(DS.view[vi]);
        if (cp && cp->pre[0]) img_get_shaped(cp->pre, (int)DISC_TW, (int)DISC_TH, CARD_R, 1);
      }
      img_prefetch_mode(0);
    }
  }
  gfx_clip_pop();
  if (dimK > 0.01f) {
    if (dimK < 0.999f) gfx_opacity_push(dimK);
    filt_bar_draw(active && DS.onFilt);
    if (dimK < 0.999f) gfx_opacity_pop();
  }
  gfx_opacity_pop();
}

static int disc_key(Board *b, int k) {
  if (DS.onFilt) {
    if (FL.open) {
      if (k == K_RIGHT || k == K_LEFT) {
        int d = app_mirror_key(k) == K_RIGHT ? 1 : -1, n = filt_chips();
        FL.restAt = app_now();
        if (FL.idx + d >= 0 && FL.idx + d < FL.n) FL.idx += d;
        else if (d < 0 && FL.idx == 0) FL.idx = -1;
        else if (d < 0 && FL.idx < 0) { filt_close(); if (FL.filtCol > 0) FL.filtCol--; else nudge_start(&DS.nudge, k); }
        else if (d > 0 && FL.idx < 0 && FL.n) FL.idx = 0;
        else { filt_close(); if (FL.filtCol + 1 < n) FL.filtCol++; else nudge_start(&DS.nudge, k); }
        sound_focus();
      } else if (k == K_OK) { if (FL.idx < 0) filt_close(); else filt_choose(); }
      else if (k == K_BACK || k == K_UP || k == K_DOWN) filt_close();
      return 1;
    }
    int n = filt_chips();
    if (k == K_RIGHT || k == K_LEFT) {
      int d = app_mirror_key(k) == K_RIGHT ? 1 : -1;
      if (FL.filtCol + d < 0 || FL.filtCol + d >= n) nudge_start(&DS.nudge, k);
      else FL.filtCol += d;
      return 1;
    }
    if (k == K_OK) { filt_open(FL.filtCol); return 1; }
    if (k == K_DOWN || k == K_CH_DOWN) {
      if (DS.viewN > 0) { DS.onFilt = 0; disc_sync_scroll(0); hero_soon(b, 120); disc_more(); }
      else nudge_start(&DS.nudge, K_DOWN);
      return 1;
    }
    if (k == K_UP || k == K_CH_UP || k == K_BACK) { topNav.idx = NAV_DISC; topnav_focus(); return 1; }
    return 1;
  }
  if (DS.viewN <= 0) { DS.onFilt = 1; return 1; }
  int row = DS.cur / DISC_COLS, col = DS.cur % DISC_COLS;
  int totalRows = (DS.viewN + DISC_COLS - 1) / DISC_COLS;
  if (k == K_LEFT) {
    if (col > 0) { DS.cur--; hero_soon(b, 120); }
    else nudge_start(&DS.nudge, K_LEFT);
  } else if (k == K_RIGHT) {
    if (col + 1 < DISC_COLS && DS.cur + 1 < DS.viewN) { DS.cur++; hero_soon(b, 120); }
    else nudge_start(&DS.nudge, K_RIGHT);
  } else if (k == K_DOWN) {
    if (row + 1 < totalRows) {
      int nx = (row + 1) * DISC_COLS + col;
      if (nx >= DS.viewN) nx = DS.viewN - 1;
      DS.cur = nx; disc_sync_scroll(0); hero_soon(b, 120); disc_more();
    } else {
      disc_more();
      nudge_start(&DS.nudge, K_DOWN);
    }
  } else if (k == K_CH_DOWN) {
    int nr = row + DISC_VIS_ROWS; if (nr >= totalRows) nr = totalRows - 1;
    if (nr > row) {
      int nx = nr * DISC_COLS + col; if (nx >= DS.viewN) nx = DS.viewN - 1;
      DS.cur = nx; disc_sync_scroll(0); hero_soon(b, 120); disc_more();
    } else { disc_more(); nudge_start(&DS.nudge, K_DOWN); }
  } else if (k == K_UP) {
    if (row > 0) { DS.cur -= DISC_COLS; disc_sync_scroll(0); hero_soon(b, 120); }
    else { DS.onFilt = 1; hero_soon(b, 120); }
  } else if (k == K_CH_UP) {
    if (row > 0) {
      int nr = row - DISC_VIS_ROWS; if (nr < 0) nr = 0;
      DS.cur = nr * DISC_COLS + col; disc_sync_scroll(0); hero_soon(b, 120);
    } else { DS.onFilt = 1; hero_soon(b, 120); }
  } else if (k == K_OK) {
    const Item *it = disc_focused(); if (it) app_open_item(it);
  } else if (k == K_BACK) {
    DS.onFilt = 1; FL.filtCol = 0; hero_soon(b, 120);
  } else return 0;
  return 1;
}

static int board_key(Board *b, int k) {
  if (b->kind == 1) return disc_key(b, k);
  if (b->ptrHold) { b->ptrHold = 0; Row *hr = b->rowIdx >= 0 && b->rowIdx < b->n ? &b->rows[b->rowIdx] : NULL; if (hr) slide_row(hr); }
  if (!b->n || b->rowIdx >= b->n) { if (k == K_UP) topnav_focus(); return 1; }
  Row *r = &b->rows[b->rowIdx];
  if (k == K_RIGHT) move_h(b, 1);
  else if (k == K_LEFT) move_h(b, -1);
  else if (k == K_DOWN) { int n = first_visible_row(b, b->rowIdx + 1, 1); if (n >= 0) { b->rowIdx = n; hero_soon(b, 150); } else nudge_start(&r->nudge, K_DOWN); }
  else if (k == K_UP) { int n = first_visible_row(b, b->rowIdx - 1, -1); if (n >= 0) { b->rowIdx = n; hero_soon(b, 150); } else topnav_focus(); }
  else if (k == K_OK) { const Item *it = focused_of(b); if (it) app_open_item(it); }
  else if (k == K_BACK && b->kind == 0) { int n = first_visible_row(b, 0, 1); if (n < 0 || n >= b->rowIdx) return 0; b->rowIdx = n; hero_soon(b, 150); }
  else if (k == K_BACK) return 0;
  return 1;
}
static int key_home(int k) { return board_key(&boards[0], k); }
static int key_disc(int k) { return board_key(&boards[1], k); }
static const Item *focused_cur(void) { Board *b = cur(); return b ? focused_of(b) : NULL; }
static void card_focus(int r, int c) { Board *b = cur(); if (!b || b->kind == 1 || r >= b->n) return; Row *row = &b->rows[r]; if (c >= row->items.n) return; int was = topNav.active; if (!was && r == b->rowIdx && c == row->col) return;
  if (!b->ptrHold) { b->ptrHold = 1; b->holdRow = b->rowIdx; }
  topNav.active = 0; b->rowIdx = r; row->col = c; if (row->popCol != c) { row->popCol = c; focus_pop(&row->pop); } hero_soon(b, 150); row_more(b, r); }
static void board_tick(float dt) {
  disc_wait_tick();
  { Board *bb = &boards[0]; double now = app_now(); for (int i = 0; i < bb->n; i++) { Row *r = &bb->rows[i]; if (r->retryAt > 0 && now >= r->retryAt && r->hasDef) { r->retryAt = 0; sb_catalog(&r->def, on_catalog, tok(bb, i)); } } }
  Board *b = cur(); if (!b) return;
  timer_tick(&b->fade, dt); nudge_tick(&b->nudge, dt);
  if (b->kind == 1) {
    tween_tick(&FL.a, dt); tween_tick(&DS.scrollY, dt); nudge_tick(&DS.nudge, dt);
    if (FL.open && (!DS.onFilt || topNav.active)) filt_close();
    filt_live();
    if (b->heroPending && app_now() >= b->heroAt) { b->heroPending = 0; hero_apply(b); }
    hero_tick(b, dt); logos_ahead(b, 0);
    if (focusOn(b)) disc_more();
    return;
  }
  if (b->heroPending && app_now() >= b->heroAt) { b->heroPending = 0; hero_apply(b); }
  hero_tick(b, dt); logos_ahead(b, 0);
  int base = b->ptrHold ? b->holdRow : b->rowIdx, fo = focusOn(b) && base >= 0, vi = fo ? base : first_visible_row(b, 0, 1); if (vi < 0) vi = 0;
  float y = 0; if (fo) for (int i = 0; i < base; i++) if (!b->rows[i].hidden) y += row_h(&b->rows[i]);
  tween_to(&b->hostY, -y, 0.22f, EASE_ROW); tween_tick(&b->hostY, dt);
  int nx = first_visible_row(b, vi + 1, 1);
  for (int i = 0; i < b->n; i++) { Row *r = &b->rows[i]; if (r->closing) { float k = (float)((app_now() - r->closeAt) / 0.45); if (k >= 1) { r->closing = 0; r->hidden = 1; r->hk = 1; } else r->hk = 1 - ease_apply(EASE_CSS_EASE, k); }
    else if (r->opening) { float k = (float)((app_now() - r->openAt) / 0.45); if (k >= 1) { r->opening = 0; r->hk = 1; } else r->hk = ease_apply(EASE_CSS_EASE, k); }
    tween_tick(&r->off, dt); focus_tick(&r->pop, dt); nudge_tick(&r->nudge, dt);
    int gone = i < vi, show = i == vi || i == nx; if (r->closing) tween_to(&r->a, 0, 0.4f, EASE_CSS_EASEOUT); else if (gone) tween_to(&r->a, 0, 0.14f, EASE_CSS_EASEOUT); else tween_to(&r->a, show ? 1 : 0, 0.28f, EASE_CSS_EASE); tween_tick(&r->a, dt); }
}
/* the art a card shows: the translated poster when there is one (a card near the cursor holds its English poster
   back for up to 0.7 s while the translation is on its way) */
static const char *card_art(Row *r, const Item *it, int near, char *buf, size_t cap, int *hold, int *final) {
  *hold = 0; *final = 1; if (r->wide || !tmdb_wanted(it->id)) return NULL;
  const TmdbLoc *L = tmdb_loc(it->type, it->id, 1);
  if (L && L->poster && L->poster[0]) { snprintf(buf, cap, "%s", L->poster); char *p = strstr(buf, "/t/p/w500/"); if (p) memcpy(p, "/t/p/w342/", 10); return buf; }
  if (!L) *final = 0; /* nothing known yet (or nothing at all): asked again each frame, as before */
  if (near && !L && app_now() - r->locAt < TMDB_WAIT_CARD && tmdb_state(it->type, it->id, 1) == TMDB_PENDING) *hold = 1;
  return NULL;
}
/* the cards' resolved pictures and words start again when the items, the lookups or the language change */
static void prep_check(Row *r) {
  if (r->cp && r->ncp == r->items.n && r->cpGen == tmdb_generation() && r->cpLg == i18n_gen()) return;
  if (r->cp && r->ncp < r->items.n && r->cpGen == tmdb_generation() && r->cpLg == i18n_gen()) { /* a page came: the cards made so far stay */
    CardPrep *cp = realloc(r->cp, (size_t)r->items.n * sizeof *cp); unsigned char *st = cp ? realloc(r->cpSt, (size_t)r->items.n) : NULL;
    if (cp) r->cp = cp; if (st) { r->cpSt = st; memset(r->cp + r->ncp, 0, (size_t)(r->items.n - r->ncp) * sizeof *r->cp); memset(r->cpSt + r->ncp, 0, (size_t)(r->items.n - r->ncp)); r->ncp = r->items.n; return; } }
  prep_free(r); if (!r->items.n) return;
  r->cp = calloc((size_t)r->items.n, sizeof *r->cp); r->cpSt = calloc((size_t)r->items.n, 1); if (!r->cp || !r->cpSt) { prep_free(r); return; }
  r->ncp = r->items.n; r->cpGen = tmdb_generation(); r->cpLg = i18n_gen();
}
/* card i's prepared look (made the first time, again only when its translated poster comes); *changed: made now */
static const CardPrep *row_prep(Row *r, int i, int near, int *hold, int *changed) {
  *hold = 0; if (changed) *changed = 0; if (!r->cp || i < 0 || i >= r->ncp) return NULL;
  CardPrep *cp = &r->cp[i]; if (r->cpSt[i] == 1) return cp;
  char lb[512]; int fin; const char *art = card_art(r, &r->items.items[i], near, lb, sizeof lb, hold, &fin);
  if (!r->cpSt[i] || art) { card_prep(cp, &r->items.items[i], r->wide, art); if (changed) *changed = 1; }
  r->cpSt[i] = fin ? 1 : 2; return cp;
}
/* posters asked for before they are needed (a request is a lookup once it is loading): the next cards to the right
   and a couple behind in rows near the focus, and the first screenful of rows just off screen, so moving never
   uncovers an empty card */
/* (asked once per range: a range that stays put asks again only for a card whose translated poster came since) */
static void row_prefetch(Row *r, int slot, int from, int to) {
  if (from < 0) from = 0; if (to > r->items.n - 1) to = r->items.n - 1;
  prep_check(r); int same = r->pf[slot][0] == from && r->pf[slot][1] == to; if (same && !r->pfWait[slot]) return;
  r->pf[slot][0] = from; r->pf[slot][1] = to; int wait = 0;
  int cw = (int)card_w(r), ch = (int)card_h(r), fit = r->wide ? 1 : 0;
  img_prefetch_mode(1); /* (a look-ahead: after what is on screen) */
  for (int i = from; i <= to; i++) { int hold, ch2; const CardPrep *cp = row_prep(r, i, 0, &hold, &ch2); if (!cp) continue; if (r->cpSt[i] != 1) wait = 1;
    const Item *it = &r->items.items[i]; if (r->cpSt[i] != 1 && !r->wide && tmdb_wanted(it->id) && tmdb_state(it->type, it->id, 1) == TMDB_PENDING) continue; /* its translated poster still being looked up: not the English one now (it would be fetched and decoded twice); asked once the answer is in */
    if ((!same || ch2) && cp->pre[0]) img_get_shaped(cp->pre, cw, ch, CARD_R, fit); }
  img_prefetch_mode(0);
  r->pfWait[slot] = (unsigned char)wait;
}
/* "its trailer is playing": three slim bars rising and falling out of step, on a small glass chip in the card's
   top left corner (it comes and goes with the trailer) */
static Row *mkRow; static int mkCol = -1;
#define playing_mark board_playing_mark /* (the Library draws it too) */
void board_playing_mark(float x, float y, float h, float a) {
  float cw = 58, chh = 40, px = x + 14, py = y + 14; (void)h; double t = app_now(); /* the top left corner: clear of the title and progress at the bottom */
  gfx_opacity_push(a); { float tt = (float)t; lglass_light(0.018f * sinf(tt * 0.157f) + 0.012f * sinf(tt * 0.263f + 1.3f) + 0.008f * sinf(tt * 0.411f + 4.1f)); } /* the same glass as the bar and the player, its light wandering */
  lglass_shell(px, py, cw, chh, chh / 2, RGBA(12, 11, 17, 160), 1); lglass_light(0);
  for (int i = 0; i < 3; i++) { float k = 0.5f + 0.5f * sinf((float)(t * 5.2 + i * 1.9)), bh = 8 + 14 * k, bx = px + 17 + i * 9;
    gfx_rrect(bx, py + chh / 2 + 11 - bh, 5, bh, 2.5f, i == 1 ? ACC2 : WHITE_A(235)); }
  gfx_opacity_pop();
}
/* a card's place while its row reorders: from where it stood (at the row's scroll then) to where it is now, eased; a
   new one fades in (*fa) */
static float move_x(Row *r, int i, float x, float step, float *fa) {
  if (fa) *fa = 1; if (r->moveAt <= 0) return x; float t = (float)((app_now() - r->moveAt) / 0.5); if (t >= 1) { r->moveAt = 0; return x; }
  float k = t < 0.5f ? 4 * t * t * t : 1 - (-2 * t + 2) * (-2 * t + 2) * (-2 * t + 2) / 2; int o = i < 48 ? r->oldIdx[i] : -1;
  if (o < 0) { if (fa) *fa = k; return x; } float ox = row_x0(r) + o * step - r->moveOff; if (app_rtl()) ox = SB_W - ox - card_w(r); return ox + (x - ox) * k; }
static void row_draw(Board *b, Row *r, int ri, float y, int near, float rowA) {
  int rtl = app_rtl(); float lh = text_line_height(31), melt = btr_melt();
  int active = focusOn(b) && ri == b->rowIdx;
  float d = 0.04f + (ri < 3 ? ri : 3) * 0.06f, rx = app_revealing() ? 30 * (1 - ease_apply(EASE_WEB, app_reveal_x(d, 0.5f))) : 0, ra = ease_apply(EASE_CSS_EASE, app_reveal_x(d, 0.4f)) * rowA;
  float ta = ra * (1 - (active ? 0.81f : 0.25f) * melt), ca = ta;
  if (ta <= 0.001f && ca <= 0.001f) return;
  if (!r->locAt && r->items.n && near) r->locAt = app_now();
  gfx_offset_push(nudge_x(&r->nudge), nudge_y(&r->nudge) + rx);
  if (ta < 1) gfx_opacity_push(ta);
  text_draw(T(r->title), rtl ? SB_W - 52 : 52, y + 18, 31, 700, RGB(0xe9, 0xe9, 0xee), -0.3f, SB_W - 104, rtl ? ALIGN_RIGHT : ALIGN_LEFT);
  if (ta < 1) gfx_opacity_pop();
  float cy = y + 18 + lh + 26, step = step_of(r), off = r->off.v, cw = card_w(r), ch = card_h(r);
  int i0 = (int)(off / step) - 1, i1 = (int)((off + SB_W - 48) / step) + 1; if (i0 < 0) i0 = 0; if (i1 > r->items.n - 1) i1 = r->items.n - 1;
  if (ca < 1) gfx_opacity_push(ca);
  prep_check(r); lift_fade(1 - melt); /* (no shadow or glass of its own over the trailer) */
  if (r->ngone) { float gk = (float)((app_now() - r->goneAt) / 0.38); if (gk >= 1) { r->ngone = 0; free(r->gone); r->gone = NULL; } else { gk = ease_apply(EASE_CSS_EASEOUT, gk); float s = 0.86f + 0.14f * (1 - gk); /* the ones taken out: fading and shrinking where they stood */
    for (int g = 0; g < r->ngone; g++) { float gx = row_x0(r) + r->goneIdx[g] * step - r->goneOff; if (rtl) gx = SB_W - gx - cw; if (gx > SB_W || gx + cw < 0) continue;
      gfx_opacity_push(1 - gk); gfx_scale_push(s, s, gx + cw / 2, cy + ch / 2); if (r->round) card_draw_round(&r->gone[g], gx, cy, cw); else card_draw_wh(&r->gone[g], gx, cy, cw, ch, r->wide, r->label, 0); gfx_scale_pop(); gfx_opacity_pop(); } } }
  for (int i = i0; i <= i1; i++) {
    float x = row_x0(r) + i * step - off; if (rtl) x = SB_W - x - cw; /* (the ones after one taken out glide back: move_x) */
    float mfa; x = move_x(r, i, x, step, &mfa);
    int load = near && i >= r->col - 2 && i < r->col + 9;
    hit_add(x, cy, cw, ch, card_focus, NULL, ri, i);
    if (active && i == r->col) continue; /* the focused card is drawn last, over its neighbours as it grows */
    if (mfa < 1) gfx_opacity_push(mfa); /* (a card new to the row fades in as the rest glide) */
    int hold; const CardPrep *cp = row_prep(r, i, load, &hold, NULL); unsigned long long lk = lift_key(r, i);
    float cr = r->round ? cw / 2 : CARD_R; lift_begin(lk, 0, 0, x, cy, cw, ch, cr); /* (the one the focus just left settles back) */
    if (r->round) { card_draw_round(&r->items.items[i], x, cy, cw); text_draw(r->items.items[i].name, x + cw / 2, cy + ch + 14, 22, 600, WHITE_A(200), 0, cw + 8, ALIGN_CENTER); }
    else card_draw_prep(&r->items.items[i], cp, x, cy, cw, ch, r->wide, r->label, !hold && (load || near));
    if (melt > 0.01f && mkRow == r && mkCol == i) playing_mark(x, cy, ch, melt); /* (the focus has moved on: it fades where it was) */
    lift_end(lk, x, cy, cw, ch, cr); if (mfa < 1) gfx_opacity_pop();
  }
  if (ca < 1) gfx_opacity_pop();
  if (near) { row_prefetch(r, 0, i1 + 1, i1 + 6); row_prefetch(r, 1, i0 - 2, i0 - 1); }
  if (active && r->col < r->items.n) { float x = row_x0(r) + r->col * step - off; if (rtl) x = SB_W - x - cw; x = move_x(r, r->col, x, step, NULL);
    int hold; const CardPrep *cp = row_prep(r, r->col, 1, &hold, NULL);
    float fa = ra * (1 - 0.15f * melt); if (fa < 1) gfx_opacity_push(fa); /* the card whose trailer plays stays clear (.85) while the rest melts */
    float hp = app_hold_progress(); unsigned long long lk = lift_key(r, r->col);
    float cr = r->round ? cw / 2 : CARD_R; lift_begin(lk, 1, hp, x, cy, cw, ch, cr);
    if (r->round) { card_draw_round(&r->items.items[r->col], x, cy, cw); text_draw(r->items.items[r->col].name, x + cw / 2, cy + ch + 14, 22, 700, WHITE_A(255), 0, cw + 8, ALIGN_CENTER); }
    else card_draw_prep(&r->items.items[r->col], cp, x, cy, cw, ch, r->wide, r->label, 1);
    if (hp > 0) hold_trace_draw(x, cy, cw, ch, cr, ease_apply(EASE_IN_OUT, hp));
    if (btr_playing() && melt > 0.9f) { mkRow = r; mkCol = r->col; } /* the card whose trailer plays: the mark stays on it, and goes with it */
    if (melt > 0.01f && mkRow == r && mkCol == r->col) playing_mark(x, cy, ch, melt);
    lift_end(lk, x, cy, cw, ch, cr);
    if (fa < 1) gfx_opacity_pop(); }
  lift_fade(1);
  gfx_offset_pop();
}
static void board_draw(void) {
  Board *b = cur(); if (!b) return;
  if (b->kind == 1) { disc_draw(b); return; }
  hero_draw(b);
  gfx_clip_push(0, ROWS_TOP, SB_W, SB_H - ROWS_TOP);
  gfx_offset_push(0, b->hostY.v);
  float y = ROWS_TOP;
  int vis = 0;
  for (int i = 0; i < b->n; i++) { Row *r = &b->rows[i]; if (r->hidden) continue; float h = row_h(r);
    float rk = 1, rdy = 0; if (b->fade.on) { float t = (timer_x(&b->fade) * 0.5f - 0.05f * (vis < 4 ? vis : 4)) / 0.38f; rk = ease_apply(EASE_CSS_EASEOUT, t < 0 ? 0 : t > 1 ? 1 : t); rdy = 28 * (1 - rk); }
    int inView = r->a.v > 0.001f && y + b->hostY.v < SB_H && y + h + b->hostY.v > ROWS_TOP;
    if (inView) { vis++; if (rdy > 0.01f) gfx_offset_push(0, rdy); row_draw(b, r, i, y, abs(i - b->rowIdx) <= 3, rk * r->a.v); if (rdy > 0.01f) gfx_offset_pop(); }
    else if (abs(i - b->rowIdx) <= 2) { int c0 = (int)(r->off.v / step_of(r)); row_prefetch(r, 2, c0 - 1, c0 + 7); }
    y += h; }
  gfx_offset_pop();
  gfx_clip_pop();
}
/* the first cards of the first rows (as drawn): asked for; whether all are in */
static int first_cards(Board *b, int nrows) {
  int seen = 0, want = 0, got = 0; if (!b->n) return 0;
  for (int r = 0; r < b->n && seen < nrows; r++) { Row *row = &b->rows[r]; if (row->hidden || !row->items.n) continue; seen++;
    prep_check(row);
    for (int c = 0; c < 6 && c < row->items.n; c++) { want++; int hold; const CardPrep *cp = row_prep(row, c, 0, &hold, NULL); const char *src = cp ? cp->pre : "";
      if (!*src) { got++; continue; } int cw = (int)card_w(row), ch = (int)card_h(row), fit = row->wide ? 1 : 0; /* the same shaped picture the card draws */
      img_get_shaped(src, cw, ch, CARD_R, fit); if (img_shaped_loaded(src, cw, ch, CARD_R, fit) || img_shaped_failed(src, cw, ch, CARD_R, fit)) got++; } }
  return seen && got >= want;
}
int board_home_ready(void) {
  Board *b = &boards[0]; if (!first_cards(b, 2)) return 0;
  if (!b->heroSet || !b->heroShown) return 0;
  return bg_current()[0] && img_loaded(bg_current());
}
/* a tab not on screen made ready in the background: its rows (and Discover's spotlight) asked for, then what its
   first screen shows: the first title's details, translation, logo and backdrop, the first cards. 1 when all is in */
static int board_warm(Board *b) {
  if (cur() == b) return 1;
  if (!b->built) { b->built = 1; b->libVer = lib_version(); load_catalog_rows(b, b->kind == 1, 0); return 0; }
  if (b->kind == 1) {
    if (!DS.loaded) return 0;
    if (DS.viewN == 0) return 1;
    if (img_budget_mb() >= 80) {
      int want = DS.viewN < 10 ? DS.viewN : 10, got = 0;
      for (int i = 0; i < want; i++) {
        const CardPrep *cp = disc_prep_for(DS.view[i]);
        const char *src = cp ? cp->pre : "";
        if (!*src) { got++; continue; }
        img_get_shaped(src, (int)DISC_TW, (int)DISC_TH, CARD_R, 1);
        if (img_shaped_loaded(src, (int)DISC_TW, (int)DISC_TH, CARD_R, 1) || img_shaped_failed(src, (int)DISC_TW, (int)DISC_TH, CARD_R, 1)) got++;
      }
      return got >= want;
    }
    return 1;
  }
  const Item *it = focused_of(b); if (!it) return 0;
  if (!sb_meta_cached(it->type, it->id)) meta_ask(b, b->rowIdx, b->rows[b->rowIdx].col);
  int loc = !tmdb_wanted(it->id) || tmdb_loc(it->type, it->id, 1) != NULL;
  char u[512]; int pics = 1, cards = 1;
  if (img_budget_mb() >= 80) {
    logo_src(it, u, sizeof u); if (u[0]) { img_prefetch_pri(u, 520, 175); pics = pics && (img_loaded(u) || img_failed(u)); }
    if (sb_bg_mid(it, u, sizeof u)[0]) { img_get(u, 1280, 720, NULL); pics = pics && (img_loaded(u) || img_failed(u)); }
    cards = first_cards(b, 2); }
  return loc && pics && cards && sb_meta_cached(it->type, it->id) != NULL;
}
void tabs_warm_tick(void) {
  static double t0, next, tabAt; static int step;
  if (step >= 3 || !app_splash_done || SCR == SCR_LOGIN || player_open()) return;
  double now = app_now(); if (!t0) { t0 = now + 1.5; return; }
  if (now < t0 || now < next || app_idle() < 0.6f) return; next = now + 0.25;
  if (!tabAt) tabAt = now;
  int done = step == 0 ? board_warm(&boards[1]) : step == 1 ? board_warm(&boards[0]) : lib_warm();
  if (done || now - tabAt > 10) { static const char *const nm[3] = { "Discover", "Home", "Library" }; fprintf(stderr, "[warm] %s %s in %.1f s\n", nm[step], done ? "ready" : "gave up", now - tabAt); step++; tabAt = 0; }
}
void board_invalidate(void) { disc_cache_invalidate(); boards[0].built = 0; }
void board_from_bar(void) {
  Board *b = cur(); if (!b) return;
  if (b->kind == 1) { DS.onFilt = DS.viewN == 0 ? 1 : 0; hero_soon(b, 120); return; }
  int n = first_visible_row(b, 0, 1); b->rowIdx = n >= 0 ? n : 0;
  hero_soon(b, 150);
}
int board_saver_items(Item *out, int max) {
  int n = 0;
  #define TAKE(it) do { const Item *x_ = (it); char bu_[512]; int dup_ = 0; if (!x_->id[0] || n >= max) break; for (int q_ = 0; q_ < n; q_++) if (!strcmp(out[q_].id, x_->id)) dup_ = 1; if (!dup_ && sb_bg_mid(x_, bu_, sizeof bu_)[0]) out[n++] = *x_; } while (0)
  if (boards[1].built) for (int j = 0; j < DS.viewN && j < 8; j++) TAKE(&DS.raw[DS.view[j]]);
  { Board *b = &boards[0]; for (int r = 0; r < b->n && r < 4; r++) for (int j = 0; j < b->rows[r].items.n && j < 6; j++) TAKE(&b->rows[r].items.items[j]); }
  #undef TAKE
  return n;
}
static int board_origin(float *x, float *y) {
  Board *b = cur(); if (!b || topNav.active) return 0;
  if (b->kind == 1) {
    if (DS.onFilt || DS.cur < 0 || DS.cur >= DS.viewN) return 0;
    int r = DS.cur / DISC_COLS, c = DS.cur % DISC_COLS;
    float cx = DISC_X0 + c * (DISC_TW + DISC_GAP_X); if (app_rtl()) cx = SB_W - cx - DISC_TW;
    float cy = DISC_GRID_TOP + DISC_GRID_PAD + DS.scrollY.v + r * DISC_PITCH;
    *x = cx + DISC_TW / 2; *y = cy + DISC_TH / 2;
    return 1;
  }
  if (b->rowIdx < 0 || b->rowIdx >= b->n) return 0; Row *r = &b->rows[b->rowIdx]; if (r->col >= r->items.n) return 0;
  float cy = ROWS_TOP + b->hostY.v; for (int i = 0; i < b->rowIdx; i++) if (!b->rows[i].hidden) cy += row_h(&b->rows[i]);
  float cw = card_w(r), ch = card_h(r), cx = row_x0(r) + r->col * step_of(r) - r->off.v; if (app_rtl()) cx = SB_W - cx - cw;
  *x = cx + cw / 2; *y = cy + 18 + text_line_height(31) + 26 + ch / 2; return 1;
}
void board_rebuild(void) {
  Board *b = cur(); if (!b) return;
  if (b->kind == 1) { b->libVer = lib_version(); if (!b->built) { b->built = 1; load_catalog_rows(b, 1, 0); } hero_soon(b, 70); return; }
  b->built = 1; b->libVer = lib_version(); keepItems = 1; load_catalog_rows(b, 0, b->n > 0); keepItems = 0;
  hero_soon(b, 70);
}
void board_refresh_current(void) { Board *b = cur(); if (b) { b->built = 0; board_enter_k(b); } }
void board_refresh_quiet(void) { Board *b = cur(); if (!b) return; if (b->kind == 1) { b->libVer = lib_version(); return; }
  b->built = 1; b->libVer = lib_version(); keepItems = 1; load_catalog_rows(b, 0, b->n > 0); keepItems = 0; }
int board_row_items(const char *id, const Item **items) {
  Board *b = cur(); if (!b || b->kind == 1 || b->rowIdx < 0 || b->rowIdx >= b->n) return 0; const Row *r = &b->rows[b->rowIdx];
  for (int i = 0; i < r->items.n; i++) if (!strcmp(r->items.items[i].id, id)) { *items = r->items.items; return r->items.n; } return 0;
}
Screen homeScreen = { enter_home, NULL, key_home, board_tick, board_draw, focused_cur, NULL, NULL, board_origin };
Screen discScreen = { enter_disc, NULL, key_disc, board_tick, board_draw, focused_cur, NULL, NULL, board_origin };
__attribute__((constructor)) static void board_kinds(void) { boards[0].kind = 0; boards[1].kind = 1; for (int i = 0; i < 2; i++) tween_set(&boards[i].logoIn, 1); }
int board_sample(Item *out, int max, char *rowTitle, int cap) {
  int n = 0; if (cap > 0) rowTitle[0] = 0;
  { Board *b = &boards[0];
    for (int i = 0; i < b->n && n < max; i++) { const Row *r = &b->rows[i]; if (r->wide || r->hidden) continue;
      int got = 0; for (int j = 0; j < r->items.n && n < max; j++) { const Item *it = &r->items.items[j]; if (!it->poster[0] || (strcmp(it->type, "movie") && strcmp(it->type, "series"))) continue; out[n++] = *it; got = 1; }
      if (got && cap > 0 && !rowTitle[0]) snprintf(rowTitle, (size_t)cap, "%s", T(r->title)); } }
  if (!n && boards[1].built) {
    for (int j = 0; j < DS.viewN && n < max; j++) { const Item *it = &DS.raw[DS.view[j]]; if (!it->poster[0] || (strcmp(it->type, "movie") && strcmp(it->type, "series"))) continue; out[n++] = *it; }
    if (n && cap > 0 && !rowTitle[0]) snprintf(rowTitle, (size_t)cap, "%s", T("Discover"));
  }
  return n;
}
