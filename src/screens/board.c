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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "core/hit.h"
#include "screens/player.h"
#include "screens/lib.h"

#define MAXROWS 128 /* every installed catalog has its row (the answers' tokens carry the row in 7 bits) */
#define FEAT_ROW (MAXROWS - 1) /* a token's row for a spotlight title */
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
#define GC_W 200.0f          /* Discover's grid: smaller than a row's posters (180 x 270 until 2026-11-08: a touch bigger, the user; still 8 a line) */
#define GC_H 300.0f
#define FILT_H 104.0f        /* Discover's filter strip (it stays put above the grid) */
#define DESC_PX 27
#define DESC_LH (27 * 1.5f)
#define TREND_Y 122.0f      /* Discover: the spotlight's "Trending now", fixed just under the bar's tabs */
#define DD_PX 25            /* Discover's face: its description a size down */
#define DD_LH (25 * 1.45f)
#define META_PX 25
/* cp: each card's resolved picture and words beside its item (cpSt 0 not yet, 1 final, 2 its translation may still
   come); pf: the ranges last fetched ahead (per call site), fetched again only when they move or a picture changes */
typedef struct { char title[96]; ItemList items; int col, wide, label, hidden, closing, filt, round, grid; double moveAt; float moveOff; short oldIdx[48]; /* moveAt: its cards gliding from where they were (oldIdx: each one's place before, -1 new; moveOff: the row's scroll then) */ /* grid: one line of Discover's listing (no title) */ /* round: Home's live row (badges) */ /* filt: Discover's filter strip */ float hk; double closeAt; Tween off, a; FocusPop pop; int popCol; Nudge nudge; double locAt;
  Item *gone; short goneIdx[8]; int ngone, opening; double goneAt, openAt; float goneOff; /* cards just taken out: fading and shrinking where they stood (the row's scroll then) as the rest glide into their places; opening: a row just come, growing in */
  CardPrep *cp; unsigned char *cpSt; int ncp; unsigned cpGen, cpLg; int pf[3][2]; unsigned char pfWait[3];
  /* a catalog that pages: where its next page starts (the items its addon sent so far), one page asked at a time */
  CatalogDef def; int hasDef, skipAt, skipAlt, more, paging; double pageAt; int retries; double retryAt; } Row; /* skipAlt: the offset to try instead when the first next page brought nothing new (an addon that pages only by its page size) */
#define NFEAT 8
/* what the hero shows: put in while it is invisible, all together */
typedef struct { char id[SB_ID], type[24], name[256], metaName[256], metaLine[96], desc[1024], logo[512]; int wait; unsigned sig; } HeroView;
/* what the drawing measures in the view, made once per view (and language): wrap counts, the meta line */
typedef struct { unsigned ver, lg; int spot, discName, discDesc, homeName; MetaParts mp; } HeroLay;
typedef struct {
  int kind;                       /* 0 home, 1 disc */
  Row rows[MAXROWS]; int n, rowIdx, built; unsigned gen, libVer;
  /* Discover's spotlight: this year's films and the trending series, alternating; it moves on every 4 s */
  Item feat[NFEAT]; int nfeat, featIdx, featOn, featLoad, featGotM, featGotS; ItemList featM, featS; double featAt, featArmAt; int swDir; /* featArmAt: when its timer started (the current dot fills up to featAt); swDir: the way the last move went */
  Tween spotY, rowsA, spotA, dotsA, dotW[NFEAT]; int heroSpot, dotsWant; double dotsGo; char scoped[SB_ID];
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
static int spotOn(Board *b) { return b->kind == 1 && b->featOn; }
static int filtOn(Board *b) { return b->kind == 1 && !b->featOn && b->rowIdx >= 0 && b->rowIdx < b->n && b->rows[b->rowIdx].filt; } /* the filter strip has the focus */
static int spotLike(Board *b) { return spotOn(b) || filtOn(b); } /* the spotlight stays up: on it, or on the strip under its dots */ /* the spotlight has the focus (its titles may still be on the way) */
static int focusOn(Board *b) { return cur() == b && !topNav.active && !spotOn(b); }
#define RD_D 178.0f /* a live badge's diameter */
static float card_w(const Row *r) { return r->round ? RD_D : r->wide ? RW_W : r->grid ? GC_W : RC_W; }
static float card_h(const Row *r) { return r->round ? RD_D : r->wide ? RW_H : r->grid ? GC_H : RC_H; }
static float row_h(const Row *r) { if (r->filt) return FILT_H; if (r->grid) return 18 + card_h(r) + 34; return (18 + text_line_height(31) + 26 + card_h(r) + 34 + (r->round ? 44 : 0)) * (r->closing || r->opening ? r->hk : 1); } /* (a live badge: its name under it) */ /* (a row closing: its height goes as the ones under it move up) */
static float step_of(const Row *r) { return card_w(r) + RGAP; }
static int grid_cols(void) { return (int)((SB_W - 96 + RGAP) / (GC_W + RGAP)); }
static float row_x0(const Row *r) { if (!r->grid) return 48; int n = grid_cols(); return (SB_W - (n * (GC_W + RGAP) - RGAP)) / 2; } /* (the grid centred under the centred strip) */
static const Item *focused_of(Board *b) { if (spotOn(b) && b->nfeat) return &b->feat[b->featIdx % b->nfeat]; if (b->rowIdx < 0 || b->rowIdx >= b->n) return NULL; Row *r = &b->rows[b->rowIdx]; return r->col < r->items.n ? &r->items.items[r->col] : NULL; }
int board_spot_up(void) { Board *b = cur(); return b && b->kind == 1 && b->featOn && !topNav.active; }

/* ---- rows ---- */
static void prep_free(Row *r) { free(r->cp); free(r->cpSt); r->cp = NULL; r->cpSt = NULL; r->ncp = 0; for (int k = 0; k < 3; k++) { r->pf[k][0] = r->pf[k][1] = -1; r->pfWait[k] = 0; } }
static void row_clear(Row *r) { itemlist_clear(&r->items); prep_free(r); free(r->gone); r->gone = NULL; r->ngone = 0; }
static Row *row_add(Board *b, const char *title, int wide, int label) {
  if (b->n >= MAXROWS) return NULL; Row *r = &b->rows[b->n++]; row_clear(r); memset(r, 0, sizeof *r); prep_free(r);
  snprintf(r->title, sizeof r->title, "%s", title); r->wide = wide; r->label = label; r->hidden = 1; r->closing = 0; r->hk = 1; r->popCol = -1; tween_set(&r->off, 0); tween_set(&r->a, 1); return r;
}
static void slide_row(Row *r) {
  if (r->grid) { tween_to(&r->off, 0, 0.18f, EASE_ROW); if (r->popCol != r->col) { r->popCol = r->col; focus_pop(&r->pop); } return; } /* (a grid line fits the screen: it never slides) */
  float w = step_of(r); int vis = (int)((SB_W - 96) / w);
  int k = r->col - (vis - 2); if (k < 0) k = 0; float off = k * w; if (r->col <= 0) off = 0;
  tween_to(&r->off, off, 0.18f, EASE_ROW);
  if (r->popCol != r->col) { r->popCol = r->col; focus_pop(&r->pop); } /* the floating copy pops on every move */
}
/* ---- Discover's filters: a type, a genre, a decade (the rows asked again with them; the decade sorts the answers) ---- */
static struct { char type[16], genre[48]; int cat, decade; /* the type, which of its catalogs, its genre (as the official Discover); decade: 0 (unused now) */
  int open, which, idx, n, top; float listW, roomMax, ow[200], ox[200], rx0, rroom, rfull, hx, hw, sxd; Tween wt[3]; double restAt; Tween sx; int hFrom, hTo; Tween hk, ha; char opt[200][96]; Tween a; float cx[4], cw[4], y; int nchip; } FL;
static int item_year(const Item *it) { for (const char *p = it->metaLine; *p; p++) if (p[0] >= '1' && p[0] <= '2' && p[1] >= '0' && p[1] <= '9' && p[2] >= '0' && p[2] <= '9' && p[3] >= '0' && p[3] <= '9') { int y = atoi(p); if (y >= 1890 && y <= 2100) return y; } return 0; }
static int year_ok(const Item *it) { if (!FL.decade) return 1; int y = item_year(it); if (!y) return 0; return FL.decade == 1 ? y < 1980 : y >= FL.decade && y < FL.decade + 10; }
static void year_keep(ItemList *l) { if (!FL.decade || !l) return; int k = 0; for (int i = 0; i < l->n; i++) if (year_ok(&l->items[i])) l->items[k++] = l->items[i]; l->n = k; }
/* a catalog answer finds its row through a packed token (board, row, generation): nothing to free */
static void *tok(Board *b, int row) { return (void *)(uintptr_t)(((uintptr_t)(b - boards) << 31) | ((uintptr_t)(b->gen & 0xffffff) << 7) | (uintptr_t)row); }
static void on_catalog(ItemList *l, int cached, void *ud) {
  (void)cached; uintptr_t t = (uintptr_t)ud; Board *b = &boards[(t >> 31) & 1]; int row = t & 127; unsigned gen = (t >> 7) & 0xffffff;
  if ((b->gen & 0xffffff) != gen || row >= b->n) { itemlist_free(l); return; }
  int got = l->n; if (b->kind == 1) year_keep(l); /* (Discover's decade) */
  { Row *r0 = &b->rows[row]; if (!got && l->total < 0 && r0->hasDef && r0->retries < 3) { static const double W[3] = { 8, 20, 45 }; r0->retryAt = app_now() + W[r0->retries]; r0->retries++; itemlist_free(l); /* no answer (an addon still making the list, busy, a blip): asked again in 8, 20 and 45 s before the row is left out */
      fprintf(stderr, "[home] %s: no answer, asked again in %.0f s\n", r0->title, W[r0->retries - 1]); return; } }
  Row *r = &b->rows[row]; row_clear(r); r->items = *l; free(l); r->locAt = 0;
  r->skipAt = got; r->skipAlt = r->items.total > got ? r->items.total : 0; r->more = r->hasDef && r->def.skip && r->items.n > 0; r->paging = 0; /* the next page from where the row stopped (16 kept of 100: asked from 100, titles 17-100 never showed), from the page's end if the addon only pages by its size */
  r->hidden = r->items.n == 0; if (r->col >= r->items.n) r->col = r->items.n ? r->items.n - 1 : 0;
  if (cur() == b) { slide_row(r); b->heroPending = b->heroPending || (b->rowIdx == row); if (b->rowIdx == row && !b->heroPending) { b->heroAt = app_now(); b->heroPending = 1; } }
}
static int first_visible_row(Board *b, int from, int dir);
static int filt_key(Board *b, int k);
static void filt_close(void);
static void filt_live(Board *b);
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
/* ---- Discover's listing: the catalog chosen in the strip, asked with its genre, as lines of posters; its next page
   asked as the focus nears the end ---- */
static void hero_soon(Board *b, int ms);
/* Discover's listing: one catalog, or under the type "All" (FL.type "*") the same catalog of every type it has (an
   addon's "Popular" for films and for series), its members' titles taken in turn. Each member is paged on its own;
   the listing only grows at its end (a member's next titles wait until every member with more has answered) */
#define DMEM 4
static struct { CatalogDef def; ItemList all; int more, paging, skipAt; double pageAt; unsigned gen;
  CatalogDef m[DMEM]; int nm; ItemList part[DMEM]; int used[DMEM], has[DMEM], mMore[DMEM], mSkip[DMEM], mWait[DMEM]; double askAt; } DS;
static void disc_merge(void);
static void on_disc_page1(ItemList *l, int more, void *ud);
#define DUD(i) ((void *)(uintptr_t)(((DS.gen & 0x0fffffffu) << 4) | (unsigned)(i)))
static int dud_ok(void *ud, int *i) { unsigned u = (unsigned)(uintptr_t)ud; *i = (int)(u & 15); return (u >> 4) == (DS.gen & 0x0fffffffu) && *i < DS.nm; }

static int grid_n(void) { return grid_cols(); }
static void disc_grid(Board *b) {
  int f = 0; while (f < b->n && !b->rows[f].filt) f++; if (f >= b->n) return; int N = grid_n();
  int gi = b->rowIdx > f && b->rowIdx < b->n && b->rows[b->rowIdx].grid ? (b->rowIdx - f - 1) * N + b->rows[b->rowIdx].col : -1;
  static float oa[MAXROWS]; static int on[MAXROWS]; int old = b->n; for (int i = 0; i < old; i++) { oa[i] = b->rows[i].a.v; on[i] = b->rows[i].grid ? b->rows[i].items.n : 0; } /* (how each line showed: the next page adds lines, it does not redraw them) */
  for (int i = f + 1; i < b->n; i++) row_clear(&b->rows[i]); b->n = f + 1;
  for (int k = 0; k * N < DS.all.n && b->n < MAXROWS; k++) { Row *r = row_add(b, "", 0, 0); if (!r) break; int c = DS.all.n - k * N < N ? DS.all.n - k * N : N;
    r->grid = 1; r->hidden = 0; r->items.items = malloc(sizeof(Item) * (size_t)c); if (!r->items.items) { b->n--; break; } memcpy(r->items.items, DS.all.items + k * N, sizeof(Item) * (size_t)c); r->items.n = c; r->items.total = c; }
  if (gi >= 0) { int ri = f + 1 + gi / N; if (ri >= b->n) ri = b->n - 1; b->rowIdx = ri; if (b->rows[ri].grid) { int c = gi % N; b->rows[ri].col = c < b->rows[ri].items.n ? c : b->rows[ri].items.n - 1; } }
  for (int i = f + 1; i < b->n; i++) { Row *r = &b->rows[i]; slide_row(r); tween_set(&r->off, r->off.to);
    if (i < old && on[i]) { tween_set(&r->a, oa[i]); if (r->items.n > on[i]) { r->moveAt = app_now(); r->moveOff = 0; for (int c = 0; c < 48; c++) r->oldIdx[c] = (short)(c < on[i] ? c : -1); } } /* a line that was there stays as it was; posters added to it fade in */
    else tween_set(&r->a, 0); } /* a new line fades in as it comes into view, never a pop */
}
static void on_disc(ItemList *l, int cached, void *ud) {
  (void)cached; int mi; if (!dud_ok(ud, &mi)) { itemlist_free(l); return; }
  if (DS.nm == 1) { /* one catalog: its answer is the listing (its fresh answer replaces its kept one), as before */
    itemlist_clear(&DS.all); itemlist_clear(&DS.part[0]); DS.used[0] = 0; DS.all = *l; free(l); DS.has[0] = 1; DS.mSkip[0] = DS.all.total > DS.all.n ? DS.all.total : DS.all.n; DS.mMore[0] = DS.m[0].skip && DS.all.n > 0; DS.more = DS.mMore[0]; DS.paging = 0;
    Board *b = &boards[1]; disc_grid(b); if (cur() == b) hero_soon(b, 0); return; }
  if (DS.has[mi] && DS.used[mi]) { itemlist_free(l); return; } /* (several: a fresh answer after the kept one is used only if nothing of it shows yet) */
  itemlist_clear(&DS.part[mi]); DS.part[mi] = *l; free(l); DS.has[mi] = 1; DS.mSkip[mi] = DS.part[mi].total > DS.part[mi].n ? DS.part[mi].total : DS.part[mi].n; DS.mMore[mi] = DS.m[mi].skip && DS.part[mi].n > 0;
  disc_merge(); return;
}
static void on_disc_page(ItemList *l, int more, void *ud) {
  int mi; if (!dud_ok(ud, &mi)) { itemlist_free(l); return; } DS.mWait[mi] = 0;
  if (!l) { DS.mMore[mi] = 0; DS.pageAt = app_now() + 10; } /* (a page that failed: that member ends there) */
  else { ItemList *p = &DS.part[mi]; int got = l->n; Item *g = realloc(p->items, (size_t)(p->n + l->n + 1) * sizeof(Item));
    if (g) { p->items = g; memcpy(p->items + p->n, l->items, sizeof(Item) * (size_t)l->n); p->n += l->n; }
    DS.mSkip[mi] += got; if (!got || !more) DS.mMore[mi] = 0; itemlist_free(l); }
  int w = 0; for (int i = 0; i < DS.nm; i++) w |= DS.mWait[i]; if (!w) DS.paging = 0;
  disc_merge();
}
static void on_disc_page1(ItemList *l, int more, void *ud) { /* one catalog: its next page at the listing's end, as before */
  int mi; if (!dud_ok(ud, &mi)) { itemlist_free(l); return; } DS.paging = 0; if (!l) { DS.pageAt = app_now() + 10; return; }
  int got = l->n, add = 0; Item *g = realloc(DS.all.items, (size_t)(DS.all.n + l->n + 1) * sizeof(Item));
  if (g) { DS.all.items = g; for (int i = 0; i < l->n; i++) { int dup = 0; for (int k = 0; k < DS.all.n && !dup; k++) dup = !strcmp(DS.all.items[k].id, l->items[i].id); if (!dup && l->items[i].id[0]) { DS.all.items[DS.all.n++] = l->items[i]; add++; } } }
  itemlist_free(l); DS.mSkip[0] += got; if (!got || !add || !more || DS.all.n >= (MAXROWS - 2) * grid_n()) DS.more = 0;
  disc_grid(&boards[1]);
}
/* the members' titles into the listing, in turn, each once: only as far as every member that still has titles (or
   more to come) can give one, so nothing already shown moves */
static void disc_merge(void) {
  int add = 0;
  for (;;) { int any = 0, stall = 0;
    for (int i = 0; i < DS.nm; i++) { if (DS.used[i] < DS.part[i].n) any = 1; else if (DS.mMore[i] || !DS.has[i]) stall = 1; }
    if (!any || stall) break;
    for (int i = 0; i < DS.nm; i++) { if (DS.used[i] >= DS.part[i].n) continue; Item *it = &DS.part[i].items[DS.used[i]++]; if (!it->id[0]) continue;
      int dup = 0; for (int k = 0; k < DS.all.n && !dup; k++) dup = !strcmp(DS.all.items[k].id, it->id); if (dup) continue;
      Item *g = realloc(DS.all.items, (size_t)(DS.all.n + 1) * sizeof(Item)); if (!g) break; DS.all.items = g; DS.all.items[DS.all.n++] = *it; add++; } }
  int more = 0; for (int i = 0; i < DS.nm; i++) more |= DS.mMore[i]; DS.more = more && DS.all.n < (MAXROWS - 2) * grid_n();
  if (add || !DS.more) { Board *b = &boards[1]; int first = DS.all.n == add; disc_grid(b); if (first && cur() == b) hero_soon(b, 0); }
}
/* a member that never answers: the others are shown without it after 5 s (from the board's tick) */
static void disc_wait_tick(void) { if (!DS.nm || !DS.askAt || app_now() - DS.askAt < 5) return; int late = 0; for (int i = 0; i < DS.nm; i++) if (!DS.has[i]) { DS.has[i] = 1; DS.mMore[i] = 0; late = 1; } DS.askAt = 0; if (late) disc_merge(); }
static void grid_carry(Board *b, int from, int to) { if (from < 0 || to < 0 || from >= b->n || to >= b->n || !b->rows[from].grid || !b->rows[to].grid) return; Row *t = &b->rows[to]; int c = b->rows[from].col; t->col = c < t->items.n ? c : t->items.n - 1; } /* (the listing's lines: straight up and down, the column kept) */
static void disc_more(Board *b) { if (b->kind != 1 || !DS.more || DS.paging || app_now() < DS.pageAt || b->rowIdx < b->n - 3) return;
  if (DS.nm == 1) { DS.paging = 1; sb_catalog_page(&DS.m[0], DS.mSkip[0], on_disc_page1, DUD(0)); return; }
  int asked = 0; for (int i = 0; i < DS.nm; i++) if (DS.mMore[i] && DS.used[i] >= DS.part[i].n - grid_n()) { DS.mWait[i] = 1; asked = 1; sb_catalog_page(&DS.m[i], DS.mSkip[i], on_disc_page, DUD(i)); } /* (the members running low) */
  DS.paging = asked; }
/* Under All: every type's catalogs, one entry for the same catalog of one addon across types (its name and addon the
   same), its members the types that have it; in the order the types come (films, series, the rest) */
#define DGRP 48
typedef struct { CatalogDef m[DMEM]; char addon[64]; int nm; } DGroup;
static DGroup dgrp[DGRP]; static int ndgrp;
static int disc_groups(void) {
  static char ty[8][16]; int nt = sb_disc_types(ty, 8); ndgrp = 0;
  for (int t = 0; t < nt; t++) { static CatalogDef cs[64]; static char an[64][64]; int nc = sb_disc_cats(ty[t], cs, an, 64);
    for (int i = 0; i < nc; i++) { int g = 0; for (; g < ndgrp; g++) if (!strcmp(dgrp[g].m[0].title, cs[i].title) && !strcmp(dgrp[g].addon, an[i])) break;
      if (g == ndgrp) { if (ndgrp >= DGRP) continue; ndgrp++; dgrp[g].nm = 0; snprintf(dgrp[g].addon, sizeof dgrp[g].addon, "%s", an[i]); }
      if (dgrp[g].nm < DMEM) dgrp[g].m[dgrp[g].nm++] = cs[i]; } }
  return ndgrp; }
static int disc_all(void) { return !strcmp(FL.type, "*"); }
/* the catalog list the strip offers for its type: the type's own, or under All the groups (each by its first member) */
static int disc_cats(CatalogDef *cs, char (*an)[64], int max) {
  if (!disc_all()) return sb_disc_cats(FL.type, cs, an, max);
  int n = disc_groups(); if (n > max) n = max; for (int i = 0; i < n; i++) { cs[i] = dgrp[i].m[0]; if (an) snprintf(an[i], 64, "%s", dgrp[i].addon); } return n; }
/* a member's genres as one list: under All, the genres any member has (needed when any member needs one) */
static int disc_cat_genres(char (*out)[48], int max, int *req) {
  *req = 0; if (!disc_all()) { static CatalogDef cs[64]; int nc = sb_disc_cats(FL.type, cs, NULL, 64); if (!nc) return 0; return sb_disc_cat_genres(&cs[FL.cat < nc ? FL.cat : 0], out, max, req); }
  int ng = disc_groups(); if (!ng) return 0; DGroup *g = &dgrp[FL.cat < ng ? FL.cat : 0]; int n = 0;
  for (int k = 0; k < g->nm; k++) { static char gg[199][48]; int r = 0, m = sb_disc_cat_genres(&g->m[k], gg, 199, &r); if (r) *req = 1;
    for (int i = 0; i < m && n < max; i++) { int dup = 0; for (int j = 0; j < n && !dup; j++) dup = !strcmp(out[j], gg[i]); if (!dup) snprintf(out[n++], 48, "%s", gg[i]); } }
  return n; }
static int disc_catalog(void) { /* the strip's choice into the listing's members (as the official app starts: its first ones; with two types or more, All) */
  static char ty[8][16]; int nt = sb_disc_types(ty, 8); if (!nt) return 0; int okT = nt >= 2 && disc_all(); for (int i = 0; i < nt; i++) if (!strcmp(ty[i], FL.type)) okT = 1;
  if (!okT) { snprintf(FL.type, sizeof FL.type, "%s", nt >= 2 ? "*" : ty[0]); FL.cat = 0; FL.genre[0] = 0; }
  DS.nm = 0;
  if (!disc_all()) { static CatalogDef cs[64]; int nc = sb_disc_cats(FL.type, cs, NULL, 64); if (!nc) return 0; if (FL.cat >= nc) FL.cat = 0;
    DS.m[0] = cs[FL.cat]; if (FL.genre[0]) sb_disc_with_genre(&DS.m[0], FL.genre); DS.nm = 1; return 1; }
  int ng = disc_groups(); if (!ng) return 0; if (FL.cat >= ng) FL.cat = 0; DGroup *g = &dgrp[FL.cat];
  for (int k = 0; k < g->nm; k++) { CatalogDef d = g->m[k]; static char gg[199][48]; int req = 0, m = sb_disc_cat_genres(&d, gg, 199, &req);
    if (FL.genre[0]) { int has = 0; for (int i = 0; i < m && !has; i++) has = !strcmp(gg[i], FL.genre); if (!has) continue; sb_disc_with_genre(&d, FL.genre); } /* (a genre: the members that have it) */
    else if (req && m) sb_disc_with_genre(&d, gg[0]); /* (one that needs a genre: its first, as the official app) */
    DS.m[DS.nm++] = d; }
  return DS.nm > 0; }
static void load_catalog_rows(Board *b, int disc, int keep) {
  int oldRow = b->rowIdx; KeptRow *kp = NULL; int nk = 0, focK = -1;
  if (keep && b->n && (kp = calloc((size_t)b->n, sizeof *kp))) { nk = b->n; focK = b->rowIdx < b->n ? b->rowIdx : -1;
    for (int i = 0; i < nk; i++) { Row *r = &b->rows[i]; snprintf(kp[i].title, sizeof kp[i].title, "%s", r->title); kp[i].def = r->def; kp[i].hasDef = r->hasDef; kp[i].col = r->col; if (r->col >= 0 && r->col < r->items.n) snprintf(kp[i].fid, sizeof kp[i].fid, "%s", r->items.items[r->col].id);
      kp[i].off = r->off.v; if (!r->hasDef && r->items.n && (kp[i].ids = malloc(sizeof *kp[i].ids * (size_t)(r->items.n < 48 ? r->items.n : 48)))) { kp[i].nids = r->items.n < 48 ? r->items.n : 48; for (int c = 0; c < kp[i].nids; c++) snprintf(kp[i].ids[c], 64, "%s", r->items.items[c].id); if ((kp[i].was = malloc(sizeof(Item) * (size_t)kp[i].nids))) { memcpy(kp[i].was, r->items.items, sizeof(Item) * (size_t)kp[i].nids); kp[i].nwas = kp[i].nids; } } kp[i].skipAt = r->skipAt; kp[i].more = r->more;
      if (keepItems && r->hasDef && r->items.n) { kp[i].items = r->items; memset(&r->items, 0, sizeof r->items); } } }
  for (int i = 0; i < b->n; i++) row_clear(&b->rows[i]); b->n = 0; b->gen++;
  if (!disc) board_cw_row(b, kp, nk);
  if (disc) { Row *f = row_add(b, "", 0, 0); if (f) { f->filt = 1; f->hidden = 0; } } /* Discover: the filter strip under the spotlight's dots */
  static CatalogDef defs[MAXROWS]; int nd = disc ? 0 : sb_home_defs(defs, MAXROWS - 1); /* (Discover: its listing, below) */
  if (disc) { DS.gen++; itemlist_clear(&DS.all); for (int i = 0; i < DMEM; i++) { itemlist_clear(&DS.part[i]); DS.used[i] = DS.has[i] = DS.mMore[i] = DS.mSkip[i] = DS.mWait[i] = 0; } DS.more = DS.paging = 0; DS.skipAt = 0; DS.pageAt = 0; DS.askAt = 0;
    if (disc_catalog()) { DS.askAt = app_now(); for (int i = 0; i < DS.nm; i++) sb_catalog_url(DS.m[i].url, DS.m[i].type, 100, on_disc, DUD(i)); fprintf(stderr, "[disc] %s %s (%d catalog%s)%s%s\n", FL.type, DS.m[0].title, DS.nm, DS.nm == 1 ? "" : "s", FL.genre[0] ? " · " : "", FL.genre); } } /* (one row kept for Continue Watching, or the strip) */
  int band = !disc && live_catalogs() > 0; /* the live row holds live: its catalogs are in the hub (All channels) */
  for (int d = 0; d < nd; d++) { if (band && live_type(defs[d].type)) continue; Row *r = row_add(b, defs[d].title, 0, 0); if (!r) break; int ri = (int)(r - b->rows); r->def = defs[d]; r->hasDef = 1;
    int k = -1; for (int j = 0; j < nk && k < 0; j++) if (same_row(&kp[j], r->title, &r->def, 1)) k = j;
    if (k >= 0) { r->col = kp[k].col; if (kp[k].items.n) { r->items = kp[k].items; memset(&kp[k].items, 0, sizeof kp[k].items); r->skipAt = kp[k].skipAt; r->more = kp[k].more; r->hidden = 0; continue; } } /* (what it had, kept: not asked again) */
    sb_catalog(&defs[d], on_catalog, tok(b, ri)); }
  if (keep) for (int i = 0; i < b->n; i++) { Row *r = &b->rows[i]; if (r->hasDef || r->closing || r->filt || r->grid) continue; int found = 0;
    for (int j = 0; j < nk && !found; j++) if (same_row(&kp[j], r->title, &r->def, 0)) found = 1;
    if (!found && nk && r->title[0]) { r->opening = 1; r->openAt = app_now(); r->hk = 0; tween_set(&r->a, 0); for (int c = 0; c < r->items.n && c < 48; c++) r->oldIdx[c] = -1; r->moveAt = app_now(); r->moveOff = 0; continue; } /* a row just come (Upcoming after a mark): it opens, its cards fading in */
    for (int j = 0; j < nk; j++) if (same_row(&kp[j], r->title, &r->def, 0)) { r->col = kp[j].col; row_ghosts(r, &kp[j], &r->items);
    for (int c = 0; c < r->items.n && kp[j].fid[0]; c++) if (!strcmp(r->items.items[c].id, kp[j].fid)) { r->col = c; break; }
    int moved = 0; for (int c = 0; c < r->items.n && c < 48; c++) { int o = -1; for (int q = 0; q < kp[j].nids && o < 0; q++) if (!strcmp(kp[j].ids[q], r->items.items[c].id)) o = q; r->oldIdx[c] = (short)o; if (o != c) moved = 1; }
    if ((moved || r->ngone || r->items.n != kp[j].nids) && kp[j].nids) { r->moveAt = app_now(); r->moveOff = kp[j].off; } break; } } /* (and the very title focused, wherever it moved: a channel just played goes first in Live TV; the cards glide there) */ /* (and the very title focused, wherever it moved: a channel just played goes first in Live TV) */ /* (Continue Watching, Upcoming, Live TV: by their names) */
  int focNew = -1; if (focK >= 0) for (int i = 0; i < b->n && focNew < 0; i++) if (same_row(&kp[focK], b->rows[i].title, &b->rows[i].def, b->rows[i].hasDef)) focNew = i;
  for (int j = 0; j < nk; j++) { itemlist_clear(&kp[j].items); free(kp[j].ids); free(kp[j].was); } free(kp);
  if (keep) for (int i = 0; i < b->n; i++) { slide_row(&b->rows[i]); tween_set(&b->rows[i].off, b->rows[i].off.to); } /* each row shown where it was, not slid back from its start */
  if (keep) { b->rowIdx = focNew >= 0 ? focNew : oldRow < b->n ? oldRow : (b->n ? b->n - 1 : 0); /* the same row as before, wherever it is now */ if (b->rowIdx < b->n && b->rows[b->rowIdx].closing) { int nx = first_visible_row(b, b->rowIdx + 1, 1); if (nx >= 0) b->rowIdx = nx; } /* (never on the row closing) */ for (int j = 0; j < b->n; j++) { Row *r = &b->rows[j]; if (r->items.n && r->col >= r->items.n) r->col = r->items.n - 1; } }
  else b->rowIdx = 0;
}
/* ---- the spotlight's own picks: this year's new films and the trending series, alternating (not the first row again) ---- */
static void hero_soon(Board *b, int ms);
static void logos_ahead(Board *b, int force);
static int feat_keep(const Item *it) { return it->id[0] && (it->background[0] || it->poster[0]); }
static void feat_done(Board *b) {
  if (!b->featGotM || !b->featGotS || b->nfeat) return; /* both lists in, once (a cached list answers again when refreshed) */
  int mv[4], sr[64], nm = 0, ns = 0;
  for (int i = 0; i < b->featM.n && nm < 4; i++) if (feat_keep(&b->featM.items[i])) mv[nm++] = i;
  for (int i = 0; i < b->featS.n && ns < 64; i++) if (feat_keep(&b->featS.items[i])) sr[ns++] = i;
  int srr[64], k = 0; for (int i = 4; i < ns; i++) srr[k++] = sr[i]; for (int i = 0; i < 4 && i < ns; i++) srr[k++] = sr[i]; /* a little further down the trending list than the row below */
  for (int i = 0; i < 4; i++) { if (i < nm && b->nfeat < NFEAT) b->feat[b->nfeat++] = b->featM.items[mv[i]]; if (i < k && b->nfeat < NFEAT) b->feat[b->nfeat++] = b->featS.items[srr[i]]; }
  itemlist_clear(&b->featM); itemlist_clear(&b->featS);
  if (!b->nfeat) { b->featLoad = 0; return; }
  b->featIdx = 0; for (int i = 0; i < NFEAT; i++) tween_set(&b->dotW[i], i == 0 ? 34 : 10);
  logos_ahead(b, 1); /* the spotlight's text art, all of it, now: it shows each title's art the moment it comes round */
  if (b->featOn && cur() == b) hero_soon(b, 0);
}
static void on_feat(ItemList *l, int cached, void *ud) {
  (void)cached; Board *b = &boards[1]; int series = (int)(uintptr_t)ud;
  if (b->nfeat || (series ? b->featGotS : b->featGotM)) { itemlist_free(l); return; }
  if (series) { b->featS = *l; b->featGotS = 1; } else { b->featM = *l; b->featGotM = 1; } free(l);
  feat_done(b);
}
static void feat_fill(Board *b) {
  if (b->kind != 1 || b->nfeat || b->featLoad) return; b->featLoad = 1; b->featGotM = b->featGotS = 0;
  time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm); char u[200];
  snprintf(u, sizeof u, CINEMETA "/catalog/movie/year/genre=%d.json", tm.tm_year + 1900); sb_catalog_url(u, "movie", 20, on_feat, (void *)0);
  sb_catalog_url(CINEMETA "/catalog/series/top.json", "series", 20, on_feat, (void *)1);
}
#define FEAT_SECS 5.0 /* the spotlight moves on by itself after this long (its current dot fills up meanwhile) */
static void feat_arm(Board *b) { if (b->kind == 1) { b->featArmAt = app_now(); b->featAt = b->featArmAt + FEAT_SECS; } }
static void feat_go(Board *b, int step) { int n = b->nfeat; if (!n) return; b->swDir = step > 0 ? 1 : -1; b->featIdx = (b->featIdx + step + n) % n; hero_soon(b, 1); feat_arm(b); } /* (it swipes the way it goes) */
static int first_visible_row(Board *b, int from, int dir) { for (int i = from; i >= 0 && i < b->n; i += dir) if (!b->rows[i].hidden && !b->rows[i].closing) return i; return -1; }

/* ---- the hero ---- */
static void hero_soon(Board *b, int ms) { b->heroAt = app_now() + ms / 1000.0; b->heroPending = 1;
  const Item *it = focused_of(b); if (it) { char pu[512], lu[512]; accent_tint_fetch(it->id, sb_poster_of(it, pu, sizeof pu), sb_logo_of(it, lu, sizeof lu)); } }
static Item *item_at(Board *b, int row, int col) {
  if (row == FEAT_ROW) return col < b->nfeat ? &b->feat[col] : NULL; /* a spotlight title */
  if (row >= b->n || col >= b->rows[row].items.n) return NULL; return &b->rows[row].items.items[col];
}
static void on_meta(cJSON *m, void *ud) {
  uintptr_t t = (uintptr_t)ud; Board *b = &boards[(t >> 31) & 1]; int row = (t >> 20) & 127, col = (t >> 8) & 0xfff; unsigned gen = t & 0xff;
  if (!m || (row != FEAT_ROW && (b->gen & 0xff) != gen)) return; Item *it = item_at(b, row, col); if (!it) return;
  if (!it->description[0]) { const char *d = cJSON_GetStringValue(cJSON_GetObjectItem(m, "description")); if (d) snprintf(it->description, sizeof it->description, "%s", d); }
  /* the details' facts are what the title page shows (a catalog can list a series' total running time where the
     details give an episode's): the line follows them, keeping the catalog's rating when the details have none */
  { char dl[96]; sb_meta_line(m, dl, sizeof dl); if (dl[0]) { const char *oi = strstr(it->metaLine, "IMDb "); if (!strstr(dl, "IMDb") && oi) { size_t l = strlen(dl); snprintf(dl + l, sizeof dl - l, "   %s", oi); }
      if (strcmp(dl, it->metaLine)) snprintf(it->metaLine, sizeof it->metaLine, "%s", dl); } }
  if (b->heroSet && !strcmp(b->hero.id, it->id)) { snprintf(b->hero.description, sizeof b->hero.description, "%s", it->description); snprintf(b->hero.metaLine, sizeof b->hero.metaLine, "%s", it->metaLine); b->heroVer++; }
}
static void meta_ask(Board *b, int row, int col) { Item *it = item_at(b, row, col); if (!it || !it->id[0]) return; /* (the details also when the catalog gave words: their facts are the title page's) */
  sb_meta(it->type, it->id, on_meta, (void *)(uintptr_t)(((uintptr_t)(b - boards) << 31) | ((uintptr_t)row << 20) | ((uintptr_t)(col & 0xfff) << 8) | (uintptr_t)(b->gen & 0xff))); }
static void logo_src(const Item *it, char *out, size_t cap) { const TmdbLoc *L = tmdb_loc(it->type, it->id, 1); if (L && L->logo && L->logo[0]) snprintf(out, cap, "%s", L->logo); else sb_logo_of(it, out, cap); }
/* the text art of the titles the hero may show next, asked for well before (at the exact size it is drawn: one
   decode, ready when the hero changes): every spotlight title, and the cards around the focus in its row and the
   rows next to it. Asked again every half second, so translated art that turns up later is fetched too and what
   is near stays recently used */
static void logos_ahead(Board *b, int force) {
  static double at[2]; double now = app_now(); int bi = (int)(b - boards); if (!force && now - at[bi] < 0.5) return; at[bi] = now;
  char lu[512];
  for (int i = 0; i < b->nfeat; i++) { logo_src(&b->feat[i], lu, sizeof lu); if (lu[0]) img_prefetch_pri(lu, 520, 175); }
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
static int desc_ready(Board *b, const char *d) { return !d[0] || (b->kind == 1 ? text_wrapped_ready(d, 1080, 2, DESC_PX, 400, 0) : text_wrapped_ready(d, HERO_W, 3, DESC_PX, 400, 0)); }
static const char *hero_key(const Board *b) { static char k[80]; snprintf(k, sizeof k, "logo:%s", b->view.id); return k; } /* (the title shown: the next one waits for its fade) */
static void hero_put(Board *b) {
  int late = b->view.wait; hero_view(b, &b->view); b->viewVer++; b->inVer = b->heroVer - 1; /* the next idle frame compares once */
  if (late && b->view.logo[0]) { tween_set(&b->logoIn, 0); tween_to(&b->logoIn, 1, 0.3f, EASE_CSS_EASEOUT); } /* a logo that came late fades in */
  else if (b->kind == 1 && b->view.logo[0] && !sx_arrived(hero_key(b))) { tween_set(&b->logoIn, 0); tween_to(&b->logoIn, 1, 0.45f, EASE_CSS_EASE); } /* Discover's big logo: a soft fade of its own, never a pop over the picture */
  else tween_set(&b->logoIn, 1);
  b->heroSpot = spotLike(b); b->heroShown = 1; b->aheadDone = 0; /* Discover's layouts come and go with the content, while the hero is invisible */
  if (desc_ready(b, b->view.desc)) { b->fadeHold = 0; tween_to(&b->heroA, 1, fade_in(b), EASE_CSS_EASEOUT); } else b->fadeHold = app_now() + 0.15; /* every line in the same fade */
}
static void hero_apply(Board *b) {
  if (cur() != b) return; const Item *it = focused_of(b); if (!it && filtOn(b) && b->nfeat) it = &b->feat[b->featIdx % b->nfeat]; /* the strip: the spotlight's title stays */
  if (!it) { tween_to(&b->heroA, 0, 0.12f, EASE_CSS_EASEIN); b->heroSet = 0; return; } /* nothing to show yet: never the previous screen's title */
  int again = b->heroSet && b->heroShown && !strcmp(b->hero.id, it->id) && b->heroPhase == HP_IDLE && b->heroSpot == spotLike(b); /* back from its page: it is already there */
  char bgu[512], pu[512]; sb_bg_mid(it, bgu, sizeof bgu); sb_poster_of(it, pu, sizeof pu);
  int fast = b->fastBg; b->fastBg = 0; /* entering a board: show its backdrop right away */
  float idle = app_idle(); int delay = (app_splash_done && !fast) ? (int)((0.4f - idle) * 1000) : 0; if (delay < 0) delay = 0;
  bg_set(bgu, delay, pu); /* shown ~0.4 s after the last move */
  if (spotOn(b) && b->nfeat) { char lu[512]; accent_scope(it->id, pu, sb_logo_of(it, lu, sizeof lu)); snprintf(b->scoped, sizeof b->scoped, "%s", it->id); } /* the spotlight wears each title's colours; its dot moves in the same moment */
  { int sp = spotLike(b) && b->nfeat; meta_ask(b, sp ? FEAT_ROW : b->rowIdx, sp ? b->featIdx % b->nfeat : b->rows[b->rowIdx].col); }
  if (again) { b->hero = *it; b->heroVer++; return; }
  float fo = fade_out(b); if (hero_live(it) && fo < 0.20f) fo = 0.20f; /* (into a channel: the softer one too) */
  b->hero = *it; b->heroVer++; b->heroSet = 1; b->heroShown = 0; b->heroPhase = HP_GATHER; b->heroT0 = app_now(); b->fadeHold = 0; b->heroFo = fo;
  tween_to(&b->heroA, 0, fo, EASE_CSS_EASEIN);
  { char lu[512]; logo_src(it, lu, sizeof lu); if (lu[0]) img_prefetch_pri(lu, 520, 175); } /* (the view shown keeps its own logo until it has faded out: it swapped in under the old words) */
}
/* the neighbours' details and logos while the remote rests: the hero then shows them at once, in one fade */
static void hero_ahead(Board *b) {
  if (b->aheadDone || app_idle() < 0.3f || b->rowIdx >= b->n) return; b->aheadDone = 1;
  char bu[512];
  if (spotOn(b)) { if (b->nfeat > 1) { const Item *nx = &b->feat[(b->featIdx + 1) % b->nfeat]; if (sb_bg_mid(nx, bu, sizeof bu)[0]) img_prefetch(bu, 1280, 720); } return; } /* the spotlight's next title, before it turns */
  Row *r = &b->rows[b->rowIdx];
  for (int d = -2; d <= 3; d++) { int c = r->col + d; if (!d || c < 0 || c >= r->items.n) continue; const Item *it = &r->items.items[c];
    if (d >= -1) { meta_ask(b, b->rowIdx, c); char lu[512]; logo_src(it, lu, sizeof lu); if (lu[0]) img_prefetch_pri(lu, 520, 175); }
    int bd = img_budget_mb() >= 80 ? 2 : 1; if (d >= -bd && d <= bd && sb_bg_mid(it, bu, sizeof bu)[0]) img_prefetch(bu, 1280, 720); } /* the backdrops either side, at the size the backdrop draws them: two each way where memory allows, one on a small TV (each is 3.6 MB) */
}
static void hero_tick(Board *b, float dt) {
  tween_tick(&b->heroA, dt); tween_tick(&b->logoIn, dt);
  if (!b->heroSet) return;
  double now = app_now(), el = now - b->heroT0;
  if (b->fadeHold > 0 && (now >= b->fadeHold || desc_ready(b, b->view.desc))) { b->fadeHold = 0; tween_to(&b->heroA, 1, fade_in(b), EASE_CSS_EASEOUT); }
  if (b->heroPhase == HP_GATHER) {
    const Item *it = &b->hero; char lu[512]; logo_src(it, lu, sizeof lu);
    int loc = tmdb_wait(&b->hw, it->type, it->id, 1, TMDB_WAIT_HERO), logo = !lu[0] || img_loaded(lu) || img_failed(lu), meta = it->description[0] != 0 && sb_meta_cached(it->type, it->id) != NULL; /* the details too: their facts, not the catalog's, so the line never changes after it shows */
    if (lu[0] && !logo) img_get(lu, 520, 175, NULL);
    if (loc && logo && meta) { const TmdbLoc *L = tmdb_loc(it->type, it->id, 1); meta = desc_ready(b, L && L->overview && L->overview[0] ? L->overview : it->description); } /* its lines rasterised while it is invisible */
    if (hero_live(it)) { if (el >= b->heroFo + 0.01 && (logo || el >= 1.5)) { b->heroPhase = HP_IDLE; hero_put(b); } return; } /* a channel: its logo only */
    if (el >= b->heroFo + 0.01 && ((loc && logo && meta) || el >= 0.45)) { b->heroPhase = HP_IDLE; hero_put(b); } /* the fade-out is over first */
    return;
  }
  if (b->heroPhase == HP_IDLE && b->heroShown) { hero_ahead(b);
    /* what hero_view reads, cheaply; the view itself is made and compared only when one of them moved */
    const Item *it = &b->hero; const TmdbLoc *L = tmdb_loc(it->type, it->id, 1);
    const void *ln = L ? (const void *)L->name : NULL, *lo = L ? (const void *)L->overview : NULL, *ll = L ? (const void *)L->logo : NULL;
    int moved = b->inVer != b->heroVer || b->inL != L || b->inLn != ln || b->inLo != lo || b->inLl != ll;
    if (moved) { logo_src(it, b->inLogo, sizeof b->inLogo); b->inVer = b->heroVer; b->inL = L; b->inLn = ln; b->inLo = lo; b->inLl = ll; }
    int st = !b->inLogo[0] ? 0 : img_failed(b->inLogo) ? 1 : img_loaded(b->inLogo) ? 2 : 3, wait = st == 3 && app_now() - b->heroT0 < logo_wait(it);
    if (st != b->inSt || wait != b->inWait) { b->inSt = st; b->inWait = wait; moved = 1; }
    if (moved) { HeroView v; hero_view(b, &v); if (v.sig != b->view.sig) { b->heroPhase = HP_LATER_WAIT; b->laterAt = now + 0.06; } } /* better data after it showed: one more fade */
    else if (st == 3) img_get(b->inLogo, 520, 175, NULL); /* still on its way: asked as the view asks */
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
/* a logo by its area, not one box: a wide one and a squarer one look the same weight (a squarer one, held to one
   height, came out small); within maxW x maxH */
static int logo_fit_area(const char *u, float area, float maxW, float maxH, float *w, float *h, GLuint *t) {
  float asp = 0; *t = u[0] ? img_get(u, 520, 175, &asp) : 0; if (!*t || asp <= 0) return 0;
  float W = sqrtf(area * asp), H = W / asp; if (W > maxW) { W = maxW; H = W / asp; } if (H > maxH) { H = maxH; W = H * asp; }
  *w = W; *h = H; return 1;
}
/* Discover's own face: everything centred over the picture; with the spotlight up a small accent label above the
   title, the dots under it, a soft dark glow behind the words and the rows lowered and dimmed */
static uint32_t acc_light(void) { return col_mix(ACC, 0xffffffffu, 0.2f); }
static HeroLay *hero_lay(Board *b) {
  HeroLay *l = &b->lay; HeroView *v = &b->view;
  if (l->ver == b->viewVer && l->lg == i18n_gen() && l->spot == b->heroSpot) return l;
  l->ver = b->viewVer; l->lg = i18n_gen(); l->spot = b->heroSpot;
  l->discName = text_wrap_count(v->name, 1700, 1, 58, 800, -1.6f);
  l->discDesc = v->desc[0] ? text_wrap_count(v->desc, 1500, 2, DD_PX, 400, 0) : 0; /* (wide: the spotlight spans the screen) */
  l->homeName = text_wrap_count(v->name, HERO_W, 2, 76, 800, -1.6f);
  meta_prep(&l->mp, v->metaLine, NULL, v->metaName, META_PX);
  return l;
}
/* the spotlight's label, upper-cased once per language */
static const char *trending_up(void) { static char eye[64]; static unsigned lg; if (lg != i18n_gen()) { lg = i18n_gen(); snprintf(eye, sizeof eye, "%s", T("Trending now")); text_upper(eye, sizeof eye); } return eye; }
/* the title's name and its text art, crossfading: the name (the art late, or none) fades in from where it begins to
   show, and once the art comes it fades in over the name as the name goes. Never a pop (k: 0 Home, 1 Discover) */
static struct { char id[SB_ID]; double nameAt, logoAt; } HX[2];
static float hx_k(double t0, double d) { if (t0 <= 0) return 0; float x = (float)((app_now() - t0) / d); x = x < 0 ? 0 : x > 1 ? 1 : x; return x * x * (3 - 2 * x); }
static void hx_step(int k, const char *id, int logo, int nameShows, float *la, float *na) {
  if (strcmp(HX[k].id, id)) { snprintf(HX[k].id, sizeof HX[k].id, "%s", id); HX[k].nameAt = HX[k].logoAt = 0; }
  if (logo && !HX[k].logoAt) HX[k].logoAt = app_now(); if (!logo && nameShows && !HX[k].nameAt) HX[k].nameAt = app_now();
  float lk = logo ? hx_k(HX[k].logoAt, 0.3) : 0, nk = hx_k(HX[k].nameAt, 0.25);
  *la = lk; *na = logo ? nk * (1 - lk) : nk; }
static void hero_draw_disc(Board *b, float a, float ta, float ma) {
  int spot = b->heroSpot; float titlePx = 58, cx = SB_W / 2;
  /* the space it has: from under "Trending now" (or the bar, for the selected poster's face) down to just above the strip;
     the logo takes what the line and the words leave (as large as that allows, by its area), and the block sits in the
     middle of it: no band left empty above or below */
  HeroView *v = &b->view; HeroLay *hl = hero_lay(b); int dl = hl->discDesc;
  float metaH = text_line_height(META_PX) > 34 ? text_line_height(META_PX) : 34, textH = 16 + metaH + (dl ? 10 + dl * DD_LH : 0);
  float top = spot ? TREND_Y + text_line_height(22) + 22 : TREND_Y, bot = spot ? ROWS_TOP - 30 : ROWS_TOP - 10, avail = bot - top - textH;
  float maxLogoH = avail < 90 ? 90 : avail > 210 ? 210 : avail;
  float lw = 0, lh = 0, headH; GLuint lt = 0; int drewLogo = logo_fit_area(v->logo, 100000, 760, maxLogoH, &lw, &lh, &lt);
  if (drewLogo) headH = lh; else headH = hl->discName * titlePx * 1.04f;
  float bodyH = headH + textH, y = top + (bot - top - bodyH) / 2; if (y + bodyH > bot) y = bot - bodyH; /* (centred in its space; never over the strip) */
  if (spot) { /* the spotlight's label, where it always was (nothing under it moves): a little bigger and brighter than before,
       an accent dot before it; it comes in and goes with the title's logo */
    static float tw; static unsigned lg; if (lg != i18n_gen() || tw <= 0) { lg = i18n_gen(); tw = text_width(trending_up(), 25, 800, 5.0f); }
    float ty = TREND_Y, l22 = text_line_height(22), ly = ty + (l22 - text_line_height(25)) / 2; int rtl = app_rtl(); (void)l22; /* (a fixed spot just under the bar: it never moves with the titles) */
    static int shown; static double inAt; /* it comes in once with the title's logo, then stays put while the titles change (only its dot's colour follows them, as the page dots do) */
    float sa = b->spotA.v; /* (the spotlight itself showing: the hero's own fade between titles does not touch it) */
    if (sa < 0.01f) shown = 0; else if (!shown && a * ta * b->logoIn.v > 0.5f) { shown = 1; inAt = app_now(); }
    float ke = shown ? ease_apply(EASE_OUT, clampf((float)((app_now() - inAt) / 0.45), 0, 1)) : 0, la = sa * ke;
    if (la > 0.002f) { gfx_opacity_push(la); gfx_offset_push(0, 12 * (1 - ke));
      text_draw(trending_up(), cx + 9, ly, 25, 800, WHITE_A(236), 5.0f, 0, ALIGN_CENTER);
      gfx_circle(rtl ? cx + 9 + tw / 2 + 16 : cx + 9 - tw / 2 - 16, ly + text_line_height(25) / 2, 5, acc_light() | 0xff000000u);
      gfx_offset_pop(); gfx_opacity_pop(); }
    }
  gfx_offset_push(0, 0); /* (a spotlight move only fades: the next title fades in where it stands, nothing slides) */
  gfx_opacity_push(a * ta);
  { float la, na; hx_step(1, b->hero.id, drewLogo, !v->wait, &la, &na); char sk[80]; snprintf(sk, sizeof sk, "logo:%s", b->hero.id);
    if (na > 0.003f) { float ny = drewLogo ? y + lh / 2 - titlePx * 1.04f / 2 : y; gfx_opacity_push(na); if (drewLogo) text_draw_wrapped(v->name, cx, ny, 1700, 1, (int)titlePx, 800, WHITE_A(255), -1.6f, ALIGN_CENTER, titlePx * 1.04f); else sx_text_wrapped(sk, v->name, cx, ny, 1700, 1, (int)titlePx, 800, WHITE_A(255), -1.6f, ALIGN_CENTER, titlePx * 1.04f); gfx_opacity_pop(); }
    if (drewLogo) sx_image(sk, lt, cx - lw / 2, y, lw, lh, 0, WHITE_A((int)(255 * b->logoIn.v * la)), 0, 0, 1, 1); } /* the name as text art moves across like a logo */
  gfx_opacity_pop(); gfx_opacity_push(a * ma);
  y += headH + 16; meta_draw_p(&hl->mp, cx, y, WHITE_A(255), ALIGN_CENTER); y += metaH;
  if (dl) text_draw_wrapped(v->desc, cx, y + 10, 1500, 2, DD_PX, 400, WHITE_A(204), 0, ALIGN_CENTER, DD_LH);
  gfx_opacity_pop(); gfx_offset_pop();
}
static void dots_draw(Board *b) {
  float da = b->dotsA.v; if (da <= 0.001f || !b->nfeat) return;
  int n = b->nfeat, f = !topNav.active, ci = b->featIdx % n, rtl = app_rtl(); float tot = 0; for (int i = 0; i < n; i++) tot += b->dotW[i].v + 10;
  float x = SB_W / 2 - tot / 2 + 5, y = ROWS_TOP - 16; /* (just above the pills) */ uint32_t al = acc_light();
  static Tween ds; tween_to(&ds, spotOn(b) && f ? 1 : 0, 0.3f, EASE_CSS_EASEOUT); tween_tick(&ds, app_dt()); /* the spotlight focused: its current dot taller (and wider, by its width's own glide) */
  /* its timer: how far it is to moving on by itself (shown while it runs; a press starts it again) */
  float p = b->featAt > 0 && spotOn(b) && f ? clampf((float)((app_now() - b->featArmAt) / (b->featAt - b->featArmAt)), 0, 1) : -1;
  static Tween ta; tween_to(&ta, p >= 0 ? 1 : 0, 0.25f, EASE_CSS_EASE); tween_tick(&ta, app_dt()); static float lastP = 1; if (p >= 0) lastP = p;
  gfx_opacity_push(da);
  for (int i = 0; i < n; i++) { int c = i == ci; float w = b->dotW[i].v, h = c ? 10 + 6 * ds.v : 10, dy = y - (h - 10) / 2, dx = rtl ? SB_W - x - w : x;
    if (!c) { gfx_rrect(dx, dy, w, h, h / 2, f ? WHITE_A(107) : WHITE_A(71)); x += w + 10; continue; }
    float tk = ta.v; /* the current one: a quiet track with its fill growing along it, or (no timer) all lit */
    if (tk < 0.999f) gfx_rrect(dx, dy, w, h, h / 2, col_alpha(f ? al : WHITE_A(179), 1 - tk));
    if (tk > 0.001f) { float fw = w * lastP; gfx_opacity_push(tk); gfx_rrect(dx, dy, w, h, h / 2, WHITE_A(64));
      if (fw > 0.5f) { gfx_clip_push(rtl ? dx + w - fw : dx, dy - 1, fw, h + 2); gfx_rrect(dx, dy, w, h, h / 2, al | 0xff000000u); gfx_clip_pop(); } /* (the lit part cut from the whole dot: its ends stay round) */
      gfx_opacity_pop(); }
    x += w + 10; }
  gfx_opacity_pop();
}
/* the glow behind the spotlight's words: an ellipse filling its box, .62 at the centre, .34 at 55%, nothing at the edge */
static GLuint scrim_tex(void) {
  static GLuint t; if (t) return t; const int N = 64; unsigned char px[64 * 64];
  for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) { float dx = (x + .5f) / N * 2 - 1, dy = (y + .5f) / N * 2 - 1, d = sqrtf(dx * dx + dy * dy);
    float a = d < .55f ? .62f - (.62f - .34f) * d / .55f : d < 1 ? .34f * (1 - (d - .55f) / .45f) : 0; px[y * N + x] = (unsigned char)(a * 255 + .5f); }
  t = gfx_tex_create(px, N, N, 1, 1); return t;
}
static void hero_draw(Board *b) {
  float melt = btr_melt(), ta = 1 - (b->kind == 1 ? 0.81f : 0.22f) * melt, ma = 1 - 0.9f * melt; /* the trailer: the title stays at .78 (Discover's big one goes with the rows), the rest .1 */
  if (b->kind == 1 && b->spotA.v > 0.001f) gfx_alpha_tex(scrim_tex(), 110, 96, SB_W - 220, 400, RGBA(12, 11, 17, (int)(255 * b->spotA.v))); /* painted once, one quad */
  if (b->kind == 1) dots_draw(b);
  if (b->kind == 0) { static int pw; int po = player_open(); if (pw && !po && cur() == b) hero_soon(b, 0); pw = po; } /* back from the player: the hero and the backdrop are the focused card's again (never the channel that played) */
  if (b->kind == 0) { /* a live title: the channels' wall behind the hero (faded in and out on its own) */
    static float lk; static double lt; double now = app_now(); float dt = lt > 0 ? (float)(now - lt) : 0; lt = now; if (dt > 0.1f) dt = 0.1f;
    { const Item *f = cur() == b ? focused_of(b) : NULL; /* a channel's hero up: its preview plays in the box on the right */
      if (f && live_type(f->type) && strcmp(f->id, LIVE_ALL_ID) && !topNav.active && !app_overlay && !saver_on()) /* (the focused channel, its hero or not: back from the player it plays on) */ lpk_want(f, app_rtl() ? 64 : SB_W - 64 - LPK_W, LPK_Y, LPK_W, LPK_H); }
    int live = b->heroSet && live_type(b->view.type); /* (the title shown) */ lk += ((float)live - lk) * fminf(1, dt * 5); if (lk > 0.003f) livewall_hero(lk);
    { const Item *f = cur() == b && !topNav.active ? focused_of(b) : NULL; int ch = f && live_type(f->type) && strcmp(f->id, LIVE_ALL_ID); /* the preview box: in its place while channels are focused (its own fade, never the hero's), the focused one's logo in it */
      static float bxa; static Item last; if (ch) last = *f; bxa += ((ch ? 1.f : 0.f) - bxa) * fminf(1, dt * 6);
      if (bxa > 0.003f && last.id[0]) { char lu[512]; sb_logo_of(&last, lu, sizeof lu); lpk_draw(last.id, lu, app_rtl() ? 64 : SB_W - 64 - LPK_W, LPK_Y, LPK_W, LPK_H, 28, bxa, RGB(22, 20, 30), RGB(12, 11, 17)); } } } /* (the box over the wall) */
  if (!b->heroSet) return; float a = b->heroA.v; if (a <= 0.001f && !(b->kind == 1 && b->heroSpot)) return;
  /* first launch: the hero rises 18 px and fades in (no delay) */
  float rv = app_reveal_x(0, 0.5f), ry = 18 * (1 - ease_apply(EASE_WEB, rv)), ra = ease_apply(EASE_CSS_EASE, app_reveal_x(0, 0.45f));
  if (b->kind == 1) { gfx_opacity_push(ra); gfx_offset_push(0, ry); hero_draw_disc(b, a, ta, ma); gfx_offset_pop(); gfx_opacity_pop(); return; }
  if (a <= 0.001f) return;
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
  if (fresh) { keepItems = b->built && keep; b->built = 1; b->libVer = lib_version(); load_catalog_rows(b, b->kind == 1, keep); keepItems = 0; } /* (built before: only the library moved) */
  if (b->kind == 1) { feat_fill(b); feat_arm(b); /* (the rows are where they stay: nothing lowered or dimmed on arrival) */ } /* arriving on the spotlight: the rows start where they will rest (no slide down after) */
  if (app_splash_done) timer_start(&b->fade, 0.5f); /* every switch to this tab: the rows rise in one after another (row_in) */
  for (int i = 0; i < b->n; i++) { slide_row(&b->rows[i]); tween_set(&b->rows[i].off, b->rows[i].off.to); }
  tween_set(&b->hostY, 0); b->fastBg = 1; hero_soon(b, 70);
}
static void board_leave(void) {
  for (int i = 0; i < 2; i++) { Board *b = &boards[i]; if (cur() == b) continue; /* another board: the last title leaves at once (its own will fade in) */
    tween_set(&b->heroA, 0); b->heroShown = 0; b->heroPhase = HP_IDLE; b->heroSet = 0; b->fadeHold = 0; }
}
static void enter_home(const void *p) { (void)p; board_leave(); board_enter_k(&boards[0]); }
static void enter_disc(const void *p) { (void)p; board_leave(); board_enter_k(&boards[1]); }
/* the next page when the focus is a few cards from the row's end (the official apps load a catalog's pages the same
   way, with skip); a page that brings nothing new, a 404 or hasMore false ends it; a failed one is asked again later */
static void on_page(ItemList *l, int more, void *ud) {
  uintptr_t t = (uintptr_t)ud; Board *b = &boards[(t >> 31) & 1]; int row = t & 127; unsigned gen = (t >> 7) & 0xffffff;
  if ((b->gen & 0xffffff) != gen || row >= b->n) { itemlist_free(l); return; }
  Row *r = &b->rows[row]; if (!r->paging) { itemlist_free(l); return; } r->paging = 0; /* the row was filled again meanwhile */
  if (!l) { r->pageAt = app_now() + 10; return; }
  int got = l->n; if (b->kind == 1) year_keep(l);
  int add = 0; Item *grow = realloc(r->items.items, (size_t)(r->items.n + l->n + 1) * sizeof(Item));
  if (grow) { r->items.items = grow;
    for (int i = 0; i < l->n; i++) { int dup = 0; for (int k = 0; k < r->items.n && !dup; k++) dup = !strcmp(r->items.items[k].id, l->items[i].id); if (!dup && l->items[i].id[0]) r->items.items[r->items.n++] = l->items[i], add++; } }
  if (!add && got && r->skipAlt > r->skipAt) { r->skipAt = r->skipAlt; r->skipAlt = 0; itemlist_free(l); r->paging = 1; sb_catalog_page(&r->def, r->skipAt, on_page, tok(b, row)); return; } /* (the same titles again: this addon pages by its own size; the next ask goes from its page's end) */
  r->skipAlt = 0; r->skipAt += l->total > got ? l->total : got; if (!got || (!add && !FL.decade) || more == 0 || r->items.n >= 1000) r->more = 0;
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
  if (r->col == old) { nudge_start(&r->nudge, dir > 0 ? K_RIGHT : K_LEFT); return; } /* the row's end */
  slide_row(r); hero_soon(b, 150); row_more(b, b->rowIdx);
}
static void spot_leave(Board *b) { b->featOn = 0; b->featAt = 0; if (b->scoped[0]) { b->scoped[0] = 0; accent_scope(NULL, NULL, NULL); } }
static int board_key(Board *b, int k) {
  if (b->ptrHold) { b->ptrHold = 0; Row *hr = b->rowIdx >= 0 && b->rowIdx < b->n ? &b->rows[b->rowIdx] : NULL; if (hr) slide_row(hr); } /* a key: the rows follow the focus again */
  if (spotOn(b)) { feat_fill(b);
    if (k == K_LEFT || k == K_RIGHT) { feat_go(b, k == K_RIGHT ? 1 : -1); sound_focus(); return 1; } /* a move by hand taps; the dots stepping by themselves do not */
    if (k == K_DOWN) { if (b->n) { int n = first_visible_row(b, 0, 1); spot_leave(b); b->rowIdx = n >= 0 ? n : 0; hero_soon(b, 150); } return 1; }
    if (k == K_UP) { topnav_focus(); return 1; }
    if (k == K_OK) { const Item *it = focused_of(b); if (it) app_open_item(it); return 1; }
    return k != K_BACK; }
  if (!b->n || b->rowIdx >= b->n) { if (k == K_UP) topnav_focus(); return 1; }
  if (b->rows[b->rowIdx].filt && filt_key(b, k)) return 1;
  if (k == K_BACK && b->kind == 1 && b->rows[b->rowIdx].filt) { topNav.idx = NAV_DISC; topnav_focus(); return 1; } /* Back on the strip: up to the bar, on Discover's own tab */
  Row *r = &b->rows[b->rowIdx];
  if (k == K_RIGHT) move_h(b, 1);
  else if (k == K_LEFT) move_h(b, -1);
  else if (k == K_DOWN) { int n = first_visible_row(b, b->rowIdx + 1, 1); if (n >= 0) { grid_carry(b, b->rowIdx, n); b->rowIdx = n; hero_soon(b, 150); disc_more(b); } else nudge_start(&r->nudge, K_DOWN); }
  else if (k == K_UP) { int n = first_visible_row(b, b->rowIdx - 1, -1); if (n >= 0) { grid_carry(b, b->rowIdx, n); b->rowIdx = n; hero_soon(b, 150); } else if (b->kind == 1) { b->featOn = 1; hero_soon(b, 150); feat_arm(b); } else topnav_focus(); }
  else if (k == K_OK) { const Item *it = focused_of(b); if (it) app_open_item(it); }
  else if (k == K_BACK && b->kind == 1 && r->grid) { int f = 0; while (f < b->n && !b->rows[f].filt) f++; if (f >= b->n) return 0; /* Discover's grid: Back goes to its top, the first pill (Back there: the bar) */
    b->rowIdx = f; b->rows[f].col = 0; hero_soon(b, 150); }
  else if (k == K_BACK && b->kind == 0) { int n = first_visible_row(b, 0, 1); if (n < 0 || n >= b->rowIdx) return 0; /* Home scrolled down: Back goes to its top row first (Back there: the bar), as Discover's grid does */
    grid_carry(b, b->rowIdx, n); b->rowIdx = n; hero_soon(b, 150); }
  else if (k == K_BACK) return 0;
  return 1;
}
static int key_home(int k) { return board_key(&boards[0], k); }
static int key_disc(int k) { return board_key(&boards[1], k); }
static const Item *focused_cur(void) { Board *b = cur(); return b ? focused_of(b) : NULL; }
/* the pointer on a card: the highlight moves there, the row follows as for the arrows */
static void card_focus(int r, int c) { Board *b = cur(); if (!b || r >= b->n) return; Row *row = &b->rows[r]; if (c >= row->items.n) return; int was = topNav.active; if (!was && !spotOn(b) && r == b->rowIdx && c == row->col) return;
  if (!b->ptrHold) { b->ptrHold = 1; b->holdRow = spotOn(b) ? -1 : b->rowIdx; } /* pointed at: highlighted where it is, nothing slides or scrolls under the hand */
  topNav.active = 0; if (spotOn(b)) spot_leave(b); b->rowIdx = r; row->col = c; if (row->popCol != c) { row->popCol = c; focus_pop(&row->pop); } hero_soon(b, 150); row_more(b, r); }
static void board_tick(float dt) {
  disc_wait_tick();
  for (int k = 0; k < 2; k++) { Board *bb = &boards[k]; double now = app_now(); for (int i = 0; i < bb->n; i++) { Row *r = &bb->rows[i]; if (r->retryAt > 0 && now >= r->retryAt && r->hasDef) { r->retryAt = 0; sb_catalog(&r->def, on_catalog, tok(bb, i)); } } } /* (rows that had no answer: asked again) */
  Board *b = cur(); if (!b) return; tween_tick(&FL.a, dt); if (FL.open && !filtOn(b)) filt_close(); if (b->kind == 1) filt_live(b);
  timer_tick(&b->fade, dt); nudge_tick(&b->nudge, dt);
  if (b->kind == 1) { int sp = spotOn(b), hs = b->heroSet && b->heroSpot;
    /* the rows lower and dim with the spotlight's face (it changes while the hero is invisible); its dots come with it */
    int rs = spotLike(b) && (hs || b->nfeat > 0 || b->featLoad); /* (the spotlight coming, or its list on its way: lowered already, not after its title has loaded) */ /* the rows follow the focus at once: leaving the dots, they rise and light up with the poster's own focus, not after the hero has changed */
    tween_to(&b->spotY, 0, 0.42f, EASE_WEB); (void)rs; /* (in the grid too: the face above shows the selected poster, the grid stays put) */
    tween_to(&b->rowsA, 1, 0.28f, EASE_CSS_EASE); tween_to(&b->spotA, hs ? 1 : 0, 0.4f, EASE_CSS_EASE); /* the rows never dimmed; the spotlight's glow with its title */
    int dOn = spotLike(b) && hs && b->nfeat; if (dOn != b->dotsWant) { b->dotsWant = dOn; b->dotsGo = app_now() + (dOn ? 0.1 : 0); } /* in after a 0.1 s pause */
    if (app_now() >= b->dotsGo) tween_to(&b->dotsA, dOn ? 1 : 0, dOn ? 0.35f : 0.3f, EASE_CSS_EASE); tween_tick(&b->dotsA, dt);
    tween_tick(&b->spotY, dt); tween_tick(&b->rowsA, dt); tween_tick(&b->spotA, dt);
    { int fz = sp && !topNav.active; for (int i = 0; i < b->nfeat; i++) { tween_to(&b->dotW[i], i == b->featIdx % b->nfeat ? (fz ? 60 : 34) : 10, 0.45f, EASE_SNAP); tween_tick(&b->dotW[i], dt); } } /* (focused: the current one larger, the others moving aside) */
    /* the spotlight's colours only while it has the focus */
    if (b->scoped[0] && (!sp || topNav.active)) { b->scoped[0] = 0; accent_scope(NULL, NULL, NULL); }
    else if (sp && b->nfeat && !topNav.active && !b->scoped[0] && b->heroSet && b->heroSpot) { const Item *it = &b->hero; char pu[512], lu[512]; accent_scope(it->id, sb_poster_of(it, pu, sizeof pu), sb_logo_of(it, lu, sizeof lu)); snprintf(b->scoped, sizeof b->scoped, "%s", it->id); }
    if (b->featAt && app_now() >= b->featAt) { b->featAt = 0;
      if (sp && !topNav.active) { if (app_idle() < FEAT_SECS - 0.25) feat_arm(b); else feat_go(b, 1); } } } /* (something pressed meanwhile: its timer starts again) */
  if (b->heroPending && app_now() >= b->heroAt) { b->heroPending = 0; hero_apply(b); }
  hero_tick(b, dt); logos_ahead(b, 0);
  /* the rows above the focused one scroll out of the way and fade as they go (0.14 s); only the focused row and
     the next one are shown, a row coming into view fades in (0.28 s) */
  int base = b->ptrHold ? b->holdRow : b->rowIdx, fo = focusOn(b) && base >= 0, vi = fo ? base : first_visible_row(b, 0, 1); if (vi < 0) vi = 0;
  float y = 0; if (fo) for (int i = 0; i < base; i++) if (!b->rows[i].hidden && !b->rows[i].filt) y += row_h(&b->rows[i]); /* (Discover's strip stays put: the grid scrolls under it) */
  tween_to(&b->hostY, -y, 0.22f, EASE_ROW); tween_tick(&b->hostY, dt);
  int nx = first_visible_row(b, vi + 1, 1);
  for (int i = 0; i < b->n; i++) { Row *r = &b->rows[i]; if (r->closing) { float k = (float)((app_now() - r->closeAt) / 0.45); if (k >= 1) { r->closing = 0; r->hidden = 1; r->hk = 1; } else r->hk = 1 - ease_apply(EASE_CSS_EASE, k); }
    else if (r->opening) { float k = (float)((app_now() - r->openAt) / 0.45); if (k >= 1) { r->opening = 0; r->hk = 1; } else r->hk = ease_apply(EASE_CSS_EASE, k); }
    tween_tick(&r->off, dt); focus_tick(&r->pop, dt); nudge_tick(&r->nudge, dt);
    int gone = i < vi && !r->filt, show = i == vi || i == nx || r->filt || (b->kind == 1 && r->grid && i > vi && i <= vi + 3); /* (the Discover grid: the rows that fit, not only the next) */ if (r->closing) tween_to(&r->a, 0, 0.4f, EASE_CSS_EASEOUT); /* (its last title fades out with it, not in a flash) */ else if (gone) tween_to(&r->a, 0, 0.14f, EASE_CSS_EASEOUT); else tween_to(&r->a, show ? 1 : 0, 0.28f, EASE_CSS_EASE); tween_tick(&r->a, dt); }
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
/* the strip, as the official Discover's: a type, one of its catalogs, the catalog's genre (when it has genres); each a
   pill with its value; OK on one opens its list under it */
static int curOpt; static char optType[16][16]; /* the list's choice in use; the types behind the type list */
static int disc_genres(char (*out)[48], int max, int *req) { return disc_cat_genres(out, max, req); }
static const char *type_txt(const char *t) { return !strcmp(t, "*") ? T("All") : !strcmp(t, "movie") ? T("Films") : !strcmp(t, "series") ? T("Series") : t; }
static const char *chip_txt(int i, char *buf, size_t cap) {
  if (FL.open && FL.which == i && FL.idx >= 0 && FL.idx < FL.n) { /* open: the choice the highlight is on (a catalog without its addon) */
    if (i == 1) { static CatalogDef cs[64]; int nc = disc_cats(cs, NULL, 64); if (FL.idx < nc) { snprintf(buf, cap, "%s", cs[FL.idx].title); return buf; } }
    return FL.opt[FL.idx]; }
  if (i == 0) return type_txt(FL.type[0] ? FL.type : "movie");
  if (i == 1) { static CatalogDef cs[64]; int nc = disc_cats(cs, NULL, 64); if (!nc) return T("Catalog"); snprintf(buf, cap, "%s", cs[FL.cat < nc ? FL.cat : 0].title); return buf; }
  return FL.genre[0] ? FL.genre : T("All genres");
}
static int filt_chips(void) { char g[2][48]; int req; return disc_genres(g, 2, &req) > 0 ? 3 : 2; }
static void filt_chip_focus(int r, int c) { Board *b = cur(); if (!b || r >= b->n) return; if (FL.open) { if (c == FL.which) FL.idx = -1; return; } /* (open: the pointer on its pill puts the highlight back on it) */ topNav.active = 0; if (spotOn(b)) { b->featOn = 0; b->featAt = 0; } b->rowIdx = r; b->rows[r].col = c; hero_soon(b, 150); }
#define FL_PAD 12.0f        /* an open list: its gap from its pill */
#define OPT_TW 520.0f       /* a choice's words at most this wide (an ellipsis after) */
#define OPT_H 46.0f         /* a choice: a little smaller than its pill (56), centred on the same line */
#define OPT_PX 22
#define OPT_PAD 18.0f
/* a pill's chevron, centred at (cx, cy): pointing to where its choices open when closed, back to the pill when they are
   open, turning over as they open and close; two short strokes of dots, nothing else */
static void chev_draw(float cx, float cy, float k, int rtl, uint32_t col) {
  float an = rtl ? 1.5708f + 3.1416f * k : -1.5708f - 3.1416f * k, c = cosf(an), sn = sinf(an); /* (right closed, left open; mirrored) */ const float P[3][2] = { { -6.5f, -3.2f }, { 0, 3.2f }, { 6.5f, -3.2f } };
  for (int sgm = 0; sgm < 2; sgm++) for (int q = 0; q <= 10; q++) { float t = q / 10.0f, x = P[sgm][0] + (P[sgm + 1][0] - P[sgm][0]) * t, y = P[sgm][1] + (P[sgm + 1][1] - P[sgm][1]) * t;
    gfx_circle(cx + x * c - y * sn, cy + x * sn + y * c, 1.45f, col); }
}
static float filt_list_w(void) { float w = FL.listW, m = FL.roomMax > 200 ? FL.roomMax : 900; return w < m ? w : m; } /* the room its line takes: all of it, or what the row has left (then it scrolls) */
/* the strip's one highlight, on a pill (code < 100) or an open pill's choice (100 + its index): where it is now */
static void hl_rect(int c, float *x, float *w) {
  if (c < 100) { int i = c < FL.nchip ? c : 0; *x = FL.cx[i]; *w = FL.cw[i]; return; }
  int j = c - 100; if (j < 0 || j >= FL.n) { *x = FL.cx[FL.which < FL.nchip ? FL.which : 0]; *w = 0; return; }
  float k = ease_apply(EASE_CSS_EASEOUT, FL.a.v), ox = FL.ox[j] - FL.sxd - 24 * (1 - k); *w = FL.ow[j]; *x = app_rtl() ? FL.rx0 + FL.rroom - ox - *w : FL.rx0 + ox;
}
static float hl_over(float hx, float hw, float x, float w) { float l = hx > x ? hx : x, r = hx + hw < x + w ? hx + hw : x + w; return w > 0 && r > l ? (r - l) / w : 0; } /* how much of a box the highlight covers */
static void filt_draw(Board *b, Row *r, int ri, float y, float rowA, int active) {
  int rtl = app_rtl(), n = filt_chips(); if (r->col >= n) r->col = n - 1; float h = 56, cy = y + 24; char buf[3][96]; const char *t[3]; float tw[3], w[3], tot = 0;
  gfx_opacity_push(rowA); FL.y = cy; FL.nchip = n;
  for (int i = 0; i < n; i++) { t[i] = chip_txt(i, buf[i], sizeof buf[i]); tw[i] = text_width(t[i], 24, 700, 0); float ww = 26 + tw[i] + 10 + 18 + 26;
    if (FL.wt[i].v < 1) tween_set(&FL.wt[i], ww); else if (fabsf(FL.wt[i].to - ww) > 0.5f) tween_to(&FL.wt[i], ww, 0.24f, EASE_ROW); tween_tick(&FL.wt[i], app_dt()); w[i] = FL.wt[i].v; /* (its words changed: it widens or narrows smoothly) */
    tot += w[i] + (i ? 12 : 0); }
  FL.roomMax = SB_W - 96 - tot - FL_PAD; /* (what the row has left for an open pill's choices) */
  float ka = FL.which < n && FL.a.v > 0.001f ? ease_apply(EASE_CSS_EASEOUT, FL.a.v) : 0, full = ka > 0 ? filt_list_w() : 0, room = (FL_PAD + full) * ka; tot += room; /* an open pill's room, on its right: the pills after it move aside */
  float x = SB_W / 2 - tot / 2; /* centred under the spotlight */
  for (int i = 0; i < n; i++) { float px = rtl ? SB_W - x - w[i] : x; FL.cx[i] = px; FL.cw[i] = w[i]; x += w[i] + 12; if (i == FL.which) x += room; }
  /* the open pill's room and where its line is scrolled to (the focused choice in view) */
  if (FL.which < n) { float px = FL.cx[FL.which], pw = FL.cw[FL.which]; FL.rroom = full * ka; FL.rfull = full; FL.rx0 = rtl ? px - FL_PAD - FL.rroom : px + pw + FL_PAD;
    float want = strip_drawn(FL.sx.to, FL.listW, full); if (FL.open && FL.idx >= 0 && FL.idx < FL.n) want = strip_target(want, FL.ox[FL.idx], FL.ow[FL.idx], FL.listW, full); /* (the focused one whole, clear of the faded ends) */
    if (fabsf(want - FL.sx.to) > 0.5f) tween_to(&FL.sx, want, 0.24f, EASE_ROW); tween_tick(&FL.sx, app_dt()); FL.sxd = strip_drawn(FL.sx.v, FL.listW, full); } /* (drawn never past an end, whatever the glide is doing) */
  /* the highlight: one accent pill gliding from where the focus was to where it is (both as they stand this frame) */
  int fi = !active ? -1 : FL.open ? (FL.idx >= 0 ? 100 + FL.idx : FL.which) : r->col;
  if (fi >= 0 && fi != FL.hTo) { if (FL.ha.v < 0.01f) FL.hFrom = fi; else FL.hFrom = FL.hTo; FL.hTo = fi; tween_set(&FL.hk, 0); tween_to(&FL.hk, 1, GLIDE_DUR, GLIDE_EASE); }
  tween_to(&FL.ha, fi >= 0 ? 1 : 0, 0.16f, EASE_IN_OUT); tween_tick(&FL.hk, app_dt()); tween_tick(&FL.ha, app_dt());
  float hx = 0, hw = 0, hh = h, ha = FL.ha.v; { float ax, aw, bx2, bw2; hl_rect(FL.hFrom, &ax, &aw); hl_rect(FL.hTo, &bx2, &bw2); float g = FL.hk.v; if (aw <= 0) { ax = bx2; aw = bw2; } hx = ax + (bx2 - ax) * g; hw = aw + (bw2 - aw) * g;
    float ah = FL.hFrom >= 100 ? OPT_H : h, bh = FL.hTo >= 100 ? OPT_H : h; hh = ah + (bh - ah) * g; } /* (its height too: a choice is smaller than a pill) */
  /* the pills (quiet), the highlight over them, then their words (taking the highlight's colours where it lies) */
  for (int i = 0; i < n; i++) { int set = i == 2 && FL.genre[0]; float px = FL.cx[i]; gfx_rrect(px, cy, w[i], h, h / 2, WHITE_A(active ? 24 : 18)); gfx_ring(px, cy, w[i], h, h / 2, set ? 2 : 1, set ? col_alpha(ACC2, 0.9f) : WHITE_A(36)); }
  if (ha > 0.001f && hw > 1) { float nx = nudge_x(&r->nudge); int inLine = FL.hFrom >= 100 && FL.hTo >= 100 && FL.rroom > 1; if (inLine) gfx_clip_push(FL.rx0, cy - 8, FL.rroom, h + 16); lglass_accent(hx + nx, cy + (h - hh) / 2, hw, hh, hh / 2, ha); if (inLine) gfx_clip_pop(); } /* (along the choices: inside their room) */
  for (int i = 0; i < n; i++) { int set = i == 2 && FL.genre[0]; float px = FL.cx[i], u = hl_over(hx, hw, px, w[i]) * ha;
    uint32_t tc = col_mix(WHITE_A(set ? 255 : 225), ACC_FG, u); float tx = rtl ? px + w[i] - 26 - tw[i] : px + 26;
    gfx_clip_push(px + 12, cy, w[i] - 24 - 18, h); text_draw(t[i], tx, cy + h / 2 - text_line_height(24) / 2, 24, 700, tc, 0, 0, ALIGN_LEFT); gfx_clip_pop(); /* (inside its pill while it changes size) */
    float ax = rtl ? px + 26 - 2 : px + w[i] - 26 - 18 + 2, ay = cy + h / 2 - 9, rk = i == FL.which ? FL.a.v : 0;
    chev_draw(ax + 9, ay + 9, ease_apply(EASE_CSS_EASEOUT, rk), rtl, tc); /* its chevron turns over as its choices open and close */
    hit_add(px, cy, w[i], h, filt_chip_focus, NULL, ri, i); }
  FL.hx = hx; FL.hw = hw;
  gfx_opacity_pop();
}
/* the list under a pill: its choices, the one in use ticked; Up / Down, OK chooses, Back closes */
static void filt_open(int which) {
  FL.which = which; FL.n = 0; FL.idx = 0; FL.top = 0; curOpt = 0;
  if (which == 0) { static char tt[15][16]; int nt = sb_disc_types(tt, 15), k = 0; if (nt >= 2) snprintf(optType[k++], 16, "*"); /* (two types or more: All first) */
    for (int i = 0; i < nt; i++) snprintf(optType[k++], 16, "%s", tt[i]);
    for (int i = 0; i < k && i < 200; i++) { snprintf(FL.opt[i], sizeof FL.opt[i], "%s", type_txt(optType[i])); if (!strcmp(optType[i], FL.type)) curOpt = i; } FL.n = k; }
  else if (which == 1) { static CatalogDef cs[64]; static char an[64][64]; int nc = disc_cats(cs, an, 64);
    for (int i = 0; i < nc && i < 200; i++) snprintf(FL.opt[i], sizeof FL.opt[i], "%s%s%s", cs[i].title, an[i][0] ? " · " : "", an[i]); FL.n = nc; curOpt = FL.cat < nc ? FL.cat : 0; }
  else { int req, k = 0; static char g[199][48]; int ng = disc_genres(g, 199, &req); if (!req) snprintf(FL.opt[k++], sizeof FL.opt[k], "%s", T("All genres"));
    for (int i = 0; i < ng && k < 200; i++) { snprintf(FL.opt[k], sizeof FL.opt[k], "%s", g[i]); if (!strcmp(g[i], FL.genre)) curOpt = k; k++; } FL.n = k; }
  FL.idx = curOpt; if (FL.idx > 3) FL.top = FL.idx - 3; FL.listW = 0; for (int i = 0; i < FL.n; i++) { { float tw = text_width(FL.opt[i], OPT_PX, 700, 0); FL.ow[i] = (tw < OPT_TW ? tw : OPT_TW) + 2 * OPT_PAD; } /* (as drawn: bold, a long name cut at OPT_TW) */ FL.ox[i] = FL.listW; FL.listW += FL.ow[i] + (i + 1 < FL.n ? 6 : 0); } tween_set(&FL.sx, 0); /* (the choices in one line, measured once) */
  FL.open = 1; tween_set(&FL.a, 0); tween_to(&FL.a, 1, 0.32f, EASE_CSS_EASEOUT); sound_focus(); /* its choices open out in the line, the pills after it move aside */
}
static void filt_close(void) { if (!FL.open) return; FL.open = 0; tween_to(&FL.a, 0, 0.26f, EASE_IN_OUT); } /* its choices go back into the pill, the pills after it close the gap */
static void filt_apply(Board *b) { /* the listing asked again with the strip's choice; the focus stays on the strip, on the same pill */
  int col = FL.which < 3 ? FL.which : 0; load_catalog_rows(b, 1, 0); b->rowIdx = 0; if (b->n && b->rows[0].filt) b->rows[0].col = col; b->featOn = 0; timer_start(&b->fade, 0.5f); for (int i = 0; i < b->n; i++) { slide_row(&b->rows[i]); tween_set(&b->rows[i].off, b->rows[i].off.to); }
}
static void genre_default(void) { int req; char g[1][48]; FL.genre[0] = 0; if (disc_genres(g, 1, &req) > 0 && req) snprintf(FL.genre, sizeof FL.genre, "%s", g[0]); } /* (a genre the catalog needs: its first, as the official app) */
static int filt_set(int i) { /* the choice i of the open pill taken (the official rules: a type resets its catalog and genre, a catalog its genre); 1 when it changed */
  if (i < 0 || i >= FL.n) return 0; int ch = 0;
  if (FL.which == 0) { if (i < 16 && strcmp(optType[i], FL.type)) { snprintf(FL.type, sizeof FL.type, "%s", optType[i]); FL.cat = 0; genre_default(); ch = 1; } }
  else if (FL.which == 1) { if (i != FL.cat) { FL.cat = i; genre_default(); ch = 1; } }
  else { int req; char g[1][48]; disc_genres(g, 1, &req); const char *v = !req && i == 0 ? "" : FL.opt[i]; if (strcmp(v, FL.genre)) { snprintf(FL.genre, sizeof FL.genre, "%s", v); ch = 1; } }
  if (ch) curOpt = i; return ch;
}
static void filt_choose(Board *b) {
  int i = FL.idx; if (i < 0 || i >= FL.n) { filt_close(); return; }
  int ch = filt_set(i); filt_close(); sound_play(SND_TOGGLE); if (ch) filt_apply(b);
}
/* the highlight resting on a choice: it applies by itself, the line staying open (as the Library's filters do) */
static void filt_live(Board *b) { if (!FL.open || FL.idx < 0 || FL.idx == curOpt || app_now() - FL.restAt < 0.7) return; if (filt_set(FL.idx)) filt_apply(b); }
#define FL_LH 54.0f
#define FL_VIS 7
static void filt_pick(int i, int bb) { (void)bb; Board *b = cur(); if (!b || !FL.open) return; if (FL.idx != i) { FL.idx = i; FL.restAt = app_now(); return; } filt_choose(b); }
/* (the pointer on the open pill itself: the highlight goes back onto it) */
static void filt_list_draw(Board *b) {
  float a = FL.a.v; if (b->kind != 1 || a <= 0.001f || FL.which >= FL.nchip) return; int rtl = app_rtl(), live = FL.open && a > 0.5f;
  /* an open pill's choices: in the same line, on its right (the pills after it have moved aside for them); the room
     opens out from the pill and the choices slide in from it; more than fit: the line scrolls to the focused one and
     its words fade into the end that has more beyond it (ui/strip.h). The strip's highlight comes along them (drawn
     with the strip) */
  float room = FL.rroom, ph = 56, x0 = FL.rx0, y0 = FL.y;
  if (room < 2) return;
  int mb = strip_more_before(FL.sxd), ma = strip_more_after(FL.sxd, room, FL.listW), fl = rtl ? ma : mb, fr = rtl ? mb : ma; /* (the start is on the right in RTL) */
  gfx_clip_push(x0, y0 - 8, room, ph + 16); gfx_opacity_push(a * a);
  for (int i = 0; i < FL.n; i++) { float cx, w; hl_rect(100 + i, &cx, &w); if (cx + w < x0 - 2 || cx > x0 + room + 2) continue;
    int cur = i == curOpt; float u = hl_over(FL.hx, FL.hw, cx, w) * FL.ha.v;
    float oy = y0 + (ph - OPT_H) / 2; /* (centred on the pills' line) */
    if (cur && u < 0.5f) { float k = strip_fade_at(cx + w / 2, x0, room, fl, fr); if (k > 0.05f) gfx_ring(cx, oy, w, OPT_H, OPT_H / 2, 1.5f, WHITE_A((int)(70 * k))); } /* the one in use */
    text_draw_hfade(FL.opt[i], cx + OPT_PAD, oy + OPT_H / 2 - text_line_height(OPT_PX) / 2, OPT_PX, cur ? 700 : 600, col_mix(WHITE_A(cur ? 255 : 200), ACC_FG, u), 0, OPT_TW, x0, room, fl, fr);
    if (live) hit_add(cx, oy, w, OPT_H, filt_pick, NULL, i, 0); }
  gfx_opacity_pop(); gfx_clip_pop();
}
static int filt_key(Board *b, int k) { /* the strip's own keys; 0: as any row */
  Row *r = &b->rows[b->rowIdx];
  if (FL.open) {
    if (k == K_RIGHT || k == K_LEFT) { int d = app_mirror_key(k) == K_RIGHT ? 1 : -1, n = filt_chips(); /* along the open line; before its first choice, the pill itself; past either end, the next pill */
      FL.restAt = app_now();
      if (FL.idx + d >= 0 && FL.idx + d < FL.n) FL.idx += d;
      else if (d < 0 && FL.idx == 0) FL.idx = -1;
      else if (d < 0 && FL.idx < 0) { filt_close(); if (r->col > 0) r->col--; else nudge_start(&r->nudge, k); }
      else if (d > 0 && FL.idx < 0 && FL.n) FL.idx = 0;
      else { filt_close(); if (r->col + 1 < n) r->col++; else nudge_start(&r->nudge, k); }
      sound_focus(); }
    else if (k == K_OK) { if (FL.idx < 0) filt_close(); else filt_choose(b); } /* OK on the pill: closed again */
    else if (k == K_BACK || k == K_UP || k == K_DOWN) { filt_close(); }
    return 1; }
  int n = filt_chips();
  if (k == K_RIGHT || k == K_LEFT) { int d = app_mirror_key(k) == K_RIGHT ? 1 : -1; if (r->col + d < 0 || r->col + d >= n) nudge_start(&r->nudge, k); else r->col += d; return 1; }
  if (k == K_OK) { filt_open(r->col); return 1; }
  return 0;
}
/* a card's place while its row reorders: from where it stood (at the row's scroll then) to where it is now, eased; a
   new one fades in (*fa) */
static float move_x(Row *r, int i, float x, float step, float *fa) {
  if (fa) *fa = 1; if (r->moveAt <= 0) return x; float t = (float)((app_now() - r->moveAt) / 0.5); if (t >= 1) { r->moveAt = 0; return x; }
  float k = t < 0.5f ? 4 * t * t * t : 1 - (-2 * t + 2) * (-2 * t + 2) * (-2 * t + 2) / 2; int o = i < 48 ? r->oldIdx[i] : -1;
  if (o < 0) { if (fa) *fa = k; return x; } float ox = row_x0(r) + o * step - r->moveOff; if (app_rtl()) ox = SB_W - ox - card_w(r); return ox + (x - ox) * k; }
static void row_draw(Board *b, Row *r, int ri, float y, int near, float rowA) {
  if (r->filt) { filt_draw(b, r, ri, y, rowA, focusOn(b) && ri == b->rowIdx); return; }
  int rtl = app_rtl(); float lh = text_line_height(31), melt = btr_melt();
  int active = focusOn(b) && ri == b->rowIdx;
  /* first launch: each row rises 30 px and fades in, 60 ms after the one above */
  float d = 0.04f + (ri < 3 ? ri : 3) * 0.06f, rx = app_revealing() ? 30 * (1 - ease_apply(EASE_WEB, app_reveal_x(d, 0.5f))) : 0, ra = ease_apply(EASE_CSS_EASE, app_reveal_x(d, 0.4f)) * rowA;
  /* the trailer: rows at .75; the focused row (its title, its cards and the floating copy, as one group) to .19 */
  float ta = ra * (1 - (active || b->kind == 1 ? 0.81f : 0.25f) * melt), ca = ta; /* (Discover: every line melts as Home's focused row does; the selected poster stays clear, below) */
  if (ta <= 0.001f && ca <= 0.001f) return;
  if (!r->locAt && r->items.n && near) r->locAt = app_now();
  gfx_offset_push(nudge_x(&r->nudge), nudge_y(&r->nudge) + rx);
  if (ta < 1) gfx_opacity_push(ta);
  if (!r->grid) text_draw(T(r->title), rtl ? SB_W - 52 : 52, y + 18, 31, 700, RGB(0xe9, 0xe9, 0xee), -0.3f, SB_W - 104, rtl ? ALIGN_RIGHT : ALIGN_LEFT);
  if (ta < 1) gfx_opacity_pop();
  float cy = r->grid ? y + 18 : y + 18 + lh + 26, step = step_of(r), off = r->off.v, cw = card_w(r), ch = card_h(r);
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
  hero_draw(b);
  int disc = b->kind == 1; float sy = disc ? b->spotY.v : 0, ra0 = disc ? b->rowsA.v : 1, clipTop = disc ? ROWS_TOP + sy + FILT_H - 8 : ROWS_TOP; /* (Discover: the grid scrolls under its strip, which stays put) */
  gfx_clip_push(0, clipTop, SB_W, SB_H - clipTop);
  gfx_offset_push(0, b->hostY.v + sy);
  float y = ROWS_TOP;
  int vis = 0, filtI = -1; float filtY = 0;
  for (int i = 0; i < b->n; i++) { Row *r = &b->rows[i]; if (r->hidden) continue; float h = row_h(r);
    if (disc && r->filt) { filtI = i; filtY = y; y += h; continue; } /* (drawn after, where it stays) */
    /* a tab switch: each row in view rises 28 px and fades in, 50 ms after the one above (0.32 s each, the glide's curve) */
    float rk = 1, rdy = 0; if (b->fade.on) { float t = (timer_x(&b->fade) * 0.5f - 0.05f * (vis < 4 ? vis : 4)) / 0.38f; rk = ease_apply(EASE_CSS_EASEOUT, t < 0 ? 0 : t > 1 ? 1 : t); rdy = 28 * (1 - rk); }
    int inView = r->a.v > 0.001f && y + b->hostY.v + sy < SB_H && y + h + b->hostY.v + sy > ROWS_TOP;
    if (inView) { vis++; if (rdy > 0.01f) gfx_offset_push(0, rdy); row_draw(b, r, i, y, abs(i - b->rowIdx) <= 3, (r->filt ? 1 : ra0) * rk * r->a.v); if (rdy > 0.01f) gfx_offset_pop(); }
    else if (abs(i - b->rowIdx) <= 2) { int c0 = (int)(r->off.v / step_of(r)); row_prefetch(r, 2, c0 - 1, c0 + 7); } /* off screen but one or two presses away */
    y += h; }
  gfx_offset_pop();
  gfx_clip_pop();
  if (filtI >= 0) { Row *r = &b->rows[filtI]; float rk = 1; /* (the strip never replays: a choice applied keeps it as it is) */
    float sm = 1 - 0.81f * btr_melt(); /* a background trailer: the strip melts with the rows */
    gfx_offset_push(0, sy); row_draw(b, r, filtI, filtY, 1, rk * r->a.v * sm); if (sm < 1) gfx_opacity_push(sm); filt_list_draw(b); if (sm < 1) gfx_opacity_pop(); gfx_offset_pop(); } /* the strip and its list over everything: never cut */
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
  if (!b->built) { b->built = 1; b->libVer = lib_version(); load_catalog_rows(b, b->kind == 1, 0); if (b->kind == 1) feat_fill(b); return 0; }
  const Item *it = focused_of(b); if (!it) return 0;
  if (b->kind == 1 && spotOn(b) && !b->nfeat) return 0; /* the spotlight's titles first */
  int sp = spotOn(b), row = sp ? FEAT_ROW : b->rowIdx, col = sp ? b->featIdx % b->nfeat : b->rows[b->rowIdx].col;
  if (!sb_meta_cached(it->type, it->id)) meta_ask(b, row, col);
  int loc = !tmdb_wanted(it->id) || tmdb_loc(it->type, it->id, 1) != NULL; /* (asks for it) */
  char u[512]; int pics = 1, cards = 1;
  /* pictures only where memory allows: on a small budget another tab's pictures would push out the ones on screen
     (they loaded again in view) */
  if (img_budget_mb() >= 80) {
    logo_src(it, u, sizeof u); if (u[0]) { img_prefetch_pri(u, 520, 175); pics = pics && (img_loaded(u) || img_failed(u)); }
    if (sb_bg_mid(it, u, sizeof u)[0]) { img_get(u, 1280, 720, NULL); pics = pics && (img_loaded(u) || img_failed(u)); } /* at the size the backdrop draws it */
    cards = first_cards(b, b->kind == 1 ? 1 : 2); }
  return loc && pics && cards && sb_meta_cached(it->type, it->id) != NULL;
}
/* right after start: Discover, Home and the Library get their first screen ready one after the other, a little
   at a time (every quarter second, never while the remote moves), so a tab switch never shows a first load; each
   gets 10 s at most */
void tabs_warm_tick(void) {
  static double t0, next, tabAt; static int step;
  if (step >= 3 || !app_splash_done || SCR == SCR_LOGIN || player_open()) return;
  double now = app_now(); if (!t0) { t0 = now + 1.5; return; } /* the first screen settles first */
  if (now < t0 || now < next || app_idle() < 0.6f) return; next = now + 0.25;
  if (!tabAt) tabAt = now;
  int done = step == 0 ? board_warm(&boards[1]) : step == 1 ? board_warm(&boards[0]) : lib_warm();
  if (done || now - tabAt > 10) { static const char *const nm[3] = { "Discover", "Home", "Library" }; fprintf(stderr, "[warm] %s %s in %.1f s\n", nm[step], done ? "ready" : "gave up", now - tabAt); step++; tabAt = 0; }
}
void board_invalidate(void) { boards[0].built = 0; }
/* the bar hands the focus down: the first row (Discover: its spotlight) */
void board_from_bar(void) { Board *b = cur(); if (!b) return; int n = first_visible_row(b, 0, 1); b->rowIdx = n >= 0 ? n : 0;
  if (b->kind == 1) { b->featOn = 1; feat_arm(b); } hero_soon(b, 150); }
/* the titles of the moment for the screensaver: the spotlight, then the first rows of Home and Discover (6 of each) */
int board_saver_items(Item *out, int max) {
  int n = 0; Board *d = &boards[1];
  #define TAKE(it) do { const Item *x_ = (it); char bu_[512]; int dup_ = 0; if (!x_->id[0] || n >= max) break; for (int q_ = 0; q_ < n; q_++) if (!strcmp(out[q_].id, x_->id)) dup_ = 1; if (!dup_ && sb_bg_mid(x_, bu_, sizeof bu_)[0]) out[n++] = *x_; } while (0)
  for (int j = 0; j < d->nfeat && j < 6; j++) TAKE(&d->feat[j]);
  for (int bi = 0; bi < 2; bi++) { Board *b = &boards[bi]; for (int r = 0; r < b->n && r < 4; r++) for (int j = 0; j < b->rows[r].items.n && j < 6; j++) TAKE(&b->rows[r].items.items[j]); }
  #undef TAKE
  return n;
}
/* where the focused card is (a title page grows out of it) */
static int board_origin(float *x, float *y) {
  Board *b = cur(); if (!b || spotOn(b) || b->rowIdx >= b->n || topNav.active) return 0; Row *r = &b->rows[b->rowIdx]; if (r->col >= r->items.n) return 0;
  float cy = ROWS_TOP + b->hostY.v; for (int i = 0; i < b->rowIdx; i++) if (!b->rows[i].hidden) cy += row_h(&b->rows[i]);
  float cw = card_w(r), ch = card_h(r), cx = row_x0(r) + r->col * step_of(r) - r->off.v; if (app_rtl()) cx = SB_W - cx - cw;
  *x = cx + cw / 2; *y = cy + 18 + (r->grid ? 0 : text_line_height(31) + 26) + ch / 2; if (b->kind == 1) *y += b->spotY.v; return 1;
}
/* the library or the addon list changed under the board on screen: its rows reload where they stand, the
   highlight stays on the same row (found by its title: Continue Watching comes and goes) and card, and no
   entrance is replayed */
void board_rebuild(void) { /* (a sync or a change of the library or the addons) the rows found again by what they are, the focus on the very title, what they had loaded kept, the cards gliding to new places */
  Board *b = cur(); if (!b) return;
  if (b->kind == 1) { b->libVer = lib_version(); hero_soon(b, 70); return; } /* (Discover keeps its listing where it is) */
  b->built = 1; b->libVer = lib_version(); keepItems = 1; load_catalog_rows(b, b->kind == 1, b->n > 0); keepItems = 0;
  hero_soon(b, 70);
}
void board_refresh_current(void) { Board *b = cur(); if (b) { b->built = 0; board_enter_k(b); } }
/* the library changed under the page (watched, added, removed): its rows take the change in place, nothing replays (the
   card that changed says so itself) */
void board_refresh_quiet(void) { Board *b = cur(); if (!b) return; if (b->kind == 1) { b->libVer = lib_version(); return; } /* Discover: its catalog does not change with the library (each poster shows its own saved / watched badge); nothing is asked again, nothing moves */
  b->built = 1; b->libVer = lib_version(); keepItems = 1; load_catalog_rows(b, b->kind == 1, b->n > 0); keepItems = 0; } /* (a library change: the catalogs' rows keep what they had) */
/* the row the focus is on, when it lists id (a channel's line-up: Up / Down in the player goes along it) */
int board_row_items(const char *id, const Item **items) {
  Board *b = cur(); if (!b || b->rowIdx < 0 || b->rowIdx >= b->n) return 0; const Row *r = &b->rows[b->rowIdx];
  for (int i = 0; i < r->items.n; i++) if (!strcmp(r->items.items[i].id, id)) { *items = r->items.items; return r->items.n; } return 0;
}
Screen homeScreen = { enter_home, NULL, key_home, board_tick, board_draw, focused_cur, NULL, NULL, board_origin };
Screen discScreen = { enter_disc, NULL, key_disc, board_tick, board_draw, focused_cur, NULL, NULL, board_origin };
__attribute__((constructor)) static void board_kinds(void) { boards[0].kind = 0; boards[1].kind = 1; boards[1].featOn = 1; tween_set(&boards[1].rowsA, 1); for (int i = 0; i < 2; i++) tween_set(&boards[i].logoIn, 1); } /* Discover opens on its spotlight */
int board_sample(Item *out, int max, char *rowTitle, int cap) {
  int n = 0; if (cap > 0) rowTitle[0] = 0;
  for (int k = 0; k < 2 && !n; k++) { Board *b = &boards[k];
    for (int i = 0; i < b->n && n < max; i++) { const Row *r = &b->rows[i]; if (r->wide || r->hidden) continue;
      int got = 0; for (int j = 0; j < r->items.n && n < max; j++) { const Item *it = &r->items.items[j]; if (!it->poster[0] || (strcmp(it->type, "movie") && strcmp(it->type, "series"))) continue; out[n++] = *it; got = 1; } /* (films and shows with their posters: never a channel) */
      if (got && cap > 0 && !rowTitle[0]) snprintf(rowTitle, (size_t)cap, "%s", T(r->title)); } }
  return n;
}
