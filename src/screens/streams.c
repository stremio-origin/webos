/* The streams sheet: an opaque side sheet from the right edge with soft leading corners and a shadow over the page's
   glass, a sentence-case heading, calm rows and the gliding accent highlight under the focus. */
#include "screens/streams.h"
#include <SDL2/SDL.h>
#include "screens/sheet.h"
#include "core/app.h"
#include "core/best.h"
#include "core/keys.h"
#include "core/i18n.h"
#include "ui/gfx.h"
#include "ui/glass.h"
#include "ui/text.h"
#include "ui/icon.h"
#include "ui/widgets.h"
#include "ui/sound.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/hit.h"
#include "core/stremio.h"
#include "core/json.h"

#define MAXG 48
#define MAXITEMS 300
#define SW 960.0f          /* the sheet */
#define PADX 52.0f
#define LW (SW - 2 * PADX) /* its content */
#define CHIPS_Y 200.0f
#define CLIP_Y 280.0f
#define LIST_PAD 14.0f /* the list starts this far under its clip: a held row's line runs outside the row, and the first one's top was cut */
#define DS_MAX 6           /* a row's words: at most this many lines, then an ellipsis */
enum { R_PLAIN, R_RESUME, R_BEST };
typedef struct { char name[96]; Stream *items; int n; int resume; } Group;
static struct {
  int open, zone; /* 0 list, 1 chips */ Tween x, op; /* the panel's slide and fade: an even ease-out (the quick-tween curve put it all in the first two frames) */ Tween out; int closing; SGlass glass;
  Group groups[MAXG]; int ng; unsigned tok; int loading; double t; char key[160];
  char type[16], id[64], vid[80], title[256], ep[400];
  int filter, chipIdx; int chips[MAXG + 1]; int nchips; QT chipsX;
  const Stream *items[MAXITEMS]; int itemGroup[MAXITEMS], kind[MAXITEMS]; int n, idx; QT listY; Nudge nudge; FPill pill;
  float itemH[MAXITEMS], itemY[MAXITEMS], headY[MAXITEMS]; int laid; double nwAt[MAXITEMS]; double enterAt; /* the rows rise in one after another from here (opening, a new filter) */
  const Stream *seen[MAXITEMS]; int nseen; char bestSum[192], resumeSum[192]; int bestShown;
  /* the tabs: the focus highlight and the current tab's underline glide from tab to tab (in the row's own x) */
  FPill chipPill; QT ulX, ulW; int ulOn;
  Tween phA, msgA, spA; /* the waiting rows, the "none" words and the spinner fade in and out (never pop) */
  /* the best pick: where the focus came to rest (its hint after 3 s), OK held on it, its details panel */
  double restAt; int restKey; int holding, swelled; double holdAt; int info; Tween infoK; QT infoY; float infoH, infoW, infoFrom[4], infoTo[4]; const Stream *infoS; SGlass pglass;
  /* measured once, not per frame: each row's header line (in layout), the chips' words and widths (chipsLaid), the total */
  char nm[MAXITEMS][600]; int total, chipsLaid; char chLbl[MAXG + 1][120], chCnt[MAXG + 1][12]; float chW[MAXG + 1]; unsigned chLg;
} S;
int streams_open(void) { return S.open; }
static void count_redo(void) { int n = 0; for (int g = 0; g < S.ng; g++) if (!S.groups[g].resume) n += S.groups[g].n; S.total = n; S.chipsLaid = 0; }
static int count(void) { return S.total; }
static void groups_clear(void) { for (int g = 0; g < S.ng; g++) free(S.groups[g].items); memset(S.groups, 0, sizeof S.groups); S.ng = 0; count_redo(); }
static void render_chips(void) { S.chipsLaid = 0;
  int curG = S.chipIdx < S.nchips ? S.chips[S.chipIdx] : -1; S.nchips = 0; S.chips[S.nchips++] = -1;
  for (int g = 0; g < S.ng; g++) if (S.groups[g].n && !S.groups[g].resume) S.chips[S.nchips++] = g;
  int k = 0; for (int i = 0; i < S.nchips; i++) if (S.chips[i] == curG) k = i; S.chipIdx = k;
}
static int was_seen(const Stream *s) { for (int i = 0; i < S.nseen; i++) if (S.seen[i] == s) return 1; return 0; }
static void rebuild(void) {
  const Stream *sel = S.idx < S.n ? S.items[S.idx] : NULL; int selKind = S.idx < S.n ? S.kind[S.idx] : 0; double now = app_now();
  int n = 0; double nw[MAXITEMS];
  for (int g = 0; g < S.ng && n < MAXITEMS - 1; g++) { if (!S.groups[g].n) continue; if (S.filter >= 0 && g != S.filter) continue;
    for (int i = 0; i < S.groups[g].n && n < MAXITEMS - 1; i++) { const Stream *s = &S.groups[g].items[i]; S.items[n] = s; S.itemGroup[n] = g; S.kind[n] = S.groups[g].resume ? R_RESUME : R_PLAIN;
      nw[n] = was_seen(s) ? 0 : now; n++; } } /* rows new to the list fade in (never pop) */
  /* best stream on: the pick for this TV right after the resume row */
  if (best_on() && S.filter < 0 && n) { const Stream *list[MAXITEMS]; int m = 0; for (int i = 0; i < n; i++) if (S.kind[i] == R_PLAIN) list[m++] = S.items[i];
    BestRate r; const Stream *b = m ? best_pick(list, m, best_dur(sb_meta_cached(S.type, S.id), S.type), NULL, -1, &r) : NULL;
    if (b) { int at = S.kind[0] == R_RESUME ? 1 : 0; memmove(S.items + at + 1, S.items + at, (n - at) * sizeof *S.items); memmove(S.itemGroup + at + 1, S.itemGroup + at, (n - at) * sizeof *S.itemGroup);
      memmove(S.kind + at + 1, S.kind + at, (n - at) * sizeof *S.kind); memmove(nw + at + 1, nw + at, (n - at) * sizeof *nw);
      S.items[at] = b; S.itemGroup[at] = -1; S.kind[at] = R_BEST; nw[at] = S.bestShown || was_seen(b) ? 0 : now; S.bestShown = 1; n++;
      snprintf(S.bestSum, sizeof S.bestSum, "%s", r.sum); } }
  for (int i = 0; i < n; i++) { if (nw[i] == 0) { /* kept rows keep their fade */ int k = -1; for (int j = 0; j < S.n; j++) if (S.items[j] == S.items[i] && S.kind[j] == S.kind[i]) { k = j; break; } S.nwAt[i] = k >= 0 ? S.nwAt[k] : 0; } else S.nwAt[i] = nw[i]; }
  S.n = n; S.nseen = 0; for (int i = 0; i < n && S.nseen < MAXITEMS; i++) S.seen[S.nseen++] = S.items[i];
  int k = -1; if (sel) for (int i = 0; i < S.n; i++) if (S.items[i] == sel && S.kind[i] == selKind) k = i;
  S.idx = k >= 0 ? k : (S.idx < S.n ? S.idx : (S.n ? S.n - 1 : 0)); S.laid = 0;
}
static void add(const Stream *arr, int n, int gIdx, const char *gName, int resume) {
  if (gIdx >= MAXG) return; Group *g = &S.groups[gIdx]; if (gIdx >= S.ng) S.ng = gIdx + 1; if (!g->name[0]) snprintf(g->name, sizeof g->name, "%s", gName ? gName : T("Addon")); g->resume = resume;
  int k = n < 40 - g->n ? n : 40 - g->n; if (k > 0) { g->items = realloc(g->items, (g->n + k) * sizeof *g->items); memcpy(g->items + g->n, arr, k * sizeof *arr); g->n += k; }
  count_redo();
  if (S.open) { render_chips(); rebuild(); }
}
static void on_part(StreamGroup *g, int gi, void *ud) { if ((unsigned)(uintptr_t)ud == S.tok) add(g->items, g->n, gi + 1, g->name, 0); free(g->items); } /* the part is ours: add() keeps copies */
static void on_done(int total, void *ud) { (void)total; if ((unsigned)(uintptr_t)ud != S.tok) return; S.loading = 0; if (S.open) rebuild(); }
static int key(int k);
static void tick(float dt);
static void draw(void);
static Screen sheet = { NULL, NULL, key, tick, draw, NULL, NULL, NULL, NULL, NULL };
static float sheet_x(void) { return app_rtl() ? 0 : SB_W - SW; }
static char emptyNote[200]; /* said instead of "No streams found." (an episode that has not aired) */
void streams_empty_note(const char *t) { snprintf(emptyNote, sizeof emptyNote, "%s", t ? t : ""); }
void streams_show(const char *type, const char *id, const char *vid, const char *title, const char *epLabel) {
  emptyNote[0] = 0;
  char k[160]; snprintf(k, sizeof k, "%s|%s", type, vid);
  float cur = S.closing ? S.out.v : S.open ? S.op.v : 0; S.open = 1; S.closing = 0; S.zone = 0; app_overlay = &sheet;
  /* from closed: it slides in once with its glass baked; reopened mid-close: the slide just turns back */
  if (cur < 0.01f) { tween_set(&S.op, 0); tween_set(&S.x, 48); tween_set(&S.phA, 1); tween_set(&S.msgA, 0); tween_set(&S.spA, 1); Uint32 b0 = SDL_GetTicks(); sglass_bake(&S.glass, sheet_x(), 0, SW, SB_H); fpill_snap(&S.pill); S.enterAt = app_now() + (SDL_GetTicks() - b0) / 1000.0 + 0.06; }
  else tween_set(&S.op, cur);
  tween_to(&S.op, 1, 0.2f, EASE_CSS_EASEOUT); tween_to(&S.x, 0, 0.26f, EASE_CSS_EASEOUT);
  snprintf(S.title, sizeof S.title, "%s", title ? title : ""); snprintf(S.ep, sizeof S.ep, "%s", epLabel ? epLabel : "");
  /* minimised and reopened: keep the results (and the focus) instead of asking every addon again */
  if (!strcmp(k, S.key) && (S.loading || S.ng) && app_now() - S.t < 600) { render_chips(); rebuild(); return; }
  snprintf(S.key, sizeof S.key, "%s", k); S.t = app_now(); S.n = 0; S.idx = 0; S.nseen = 0; S.bestShown = 0; groups_clear(); snprintf(S.type, sizeof S.type, "%s", type); snprintf(S.id, sizeof S.id, "%s", id); snprintf(S.vid, sizeof S.vid, "%s", vid);
  S.filter = -1; S.chipIdx = 0; S.nchips = 0; S.tok++; S.loading = 1; qt_set(&S.chipsX, 0); qt_set(&S.listY, 0);
  Stream r; if (sb_resume_stream(id, vid, &r)) { Stream x = r; /* (it keeps its addon: played again, it is remembered as that addon's, not as "Last used") */ snprintf(x.name, sizeof x.name, "%s", T("Resume last stream")); S.resumeSum[0] = 0; { BestRate br; memset(&br, 0, sizeof br); best_rate(&r, NULL, -1, best_dur(sb_meta_cached(S.type, S.id), S.type), &br); snprintf(S.resumeSum, sizeof S.resumeSum, "%s", br.sum); } /* its specs laid out as the best fit's are */
    add(&x, 1, 0, T("Last used"), 1); }
  else rebuild();
  sb_streams(type, vid, on_part, on_done, (void *)(uintptr_t)S.tok);
}
void streams_close(int instant) {
  if (!S.open && !instant) return; S.open = 0; if (app_overlay == &sheet) app_overlay = NULL; S.info = 0; S.holding = 0; tween_set(&S.infoK, 0);
  if (instant) { S.closing = 0; tween_set(&S.op, 0); return; }
  S.closing = 1; app_occluder_drop(); tween_set(&S.out, S.op.v); tween_to(&S.out, 0, 0.18f, EASE_IN_OUT); tween_to(&S.x, 32, 0.2f, EASE_CSS_EASEOUT);
}
static void play(int i) { const Stream *s = S.items[i]; fpill_press(&S.pill); app_play(s, S.type, S.id, S.vid, 0); }
/* the best pick's details: the row grows into a panel that hugs the stream's own words (every line as the addon wrote
   it, wrapped, nothing cut), on the sheets' glass; a long one scrolls with Up/Down */
#define INFO_PAD 36.0f
#define INFO_M 64.0f   /* the screen's safe margin */
static const char *INFO_LBL[3] = { "Name", "Description", "File" };
static const char *info_text(const Stream *s, int i) { return i == 0 ? s->name : i == 1 ? s->title : s->filename; }
static const char *info_title(void) { return S.idx < S.n && S.kind[S.idx] == R_RESUME ? T("Resume last stream") : T("Best fit for this TV"); } /* the details panel's heading: the row it came from */
static void info_sub(const Stream *s, char *out, size_t cap) { const char *sum = S.idx < S.n && S.kind[S.idx] == R_RESUME ? S.resumeSum : S.bestSum; snprintf(out, cap, "%s%s%s", s->addon, s->addon[0] && sum[0] ? "  \xc2\xb7  " : "", sum); }
/* the widest line of some words (each of their lines measured alone) */
static float lines_w(const char *t, int px, int wt) { float m = 0; char b[600]; const char *p = t;
  while (*p) { const char *e = strchr(p, '\n'); size_t l = e ? (size_t)(e - p) : strlen(p); if (l >= sizeof b) l = sizeof b - 1; memcpy(b, p, l); b[l] = 0; float w = text_width(b, px, wt, 0); if (w > m) m = w; if (!e) break; p = e + 1; }
  return m; }
static float info_measure(const Stream *s, float w) {
  char sub[320]; info_sub(s, sub, sizeof sub); float h = text_line_height(30) + 8 + text_wrap_count(sub, w, 3, 24, 600, 0) * 32.0f;
  for (int i = 0; i < 3; i++) { const char *t = info_text(s, i); if (!t[0]) continue; h += 26 + text_line_height(21) + 6 + text_wrap_count(t, w, 80, 26, 400, 0) * 36.0f; }
  return h; }
static void info_open(void) {
  if (S.idx >= S.n) return; const Stream *s = S.items[S.idx]; S.infoS = s; S.info = 1; qt_set(&S.infoY, 0);
  /* its width: the widest line, between a minimum and the sheet's content; its height: the words, within the screen's margins */
  char sub[320]; info_sub(s, sub, sizeof sub); float nat = fmaxf(text_width(info_title(), 30, 700, -0.2f), lines_w(sub, 24, 600));
  for (int i = 0; i < 3; i++) { const char *t = info_text(s, i); if (t[0]) nat = fmaxf(nat, lines_w(t, 26, 400)); }
  float cw = clampf(ceilf(nat) + 2, 520, LW - 2 * INFO_PAD); S.infoW = cw; S.infoH = info_measure(s, cw);
  float pw = cw + 2 * INFO_PAD, ph = clampf(S.infoH + 2 * INFO_PAD, 180, SB_H - 2 * INFO_M);
  float rowY = CLIP_Y + LIST_PAD + S.listY.v + S.itemY[S.idx], py = clampf(rowY + S.itemH[S.idx] / 2 - ph / 2, INFO_M, SB_H - INFO_M - ph), rx = app_rtl() ? LW - pw : 0;
  S.infoFrom[0] = 0; S.infoFrom[1] = rowY; S.infoFrom[2] = LW; S.infoFrom[3] = S.itemH[S.idx];
  S.infoTo[0] = rx; S.infoTo[1] = py; S.infoTo[2] = pw; S.infoTo[3] = ph;
  sglass_bake(&S.pglass, sheet_x() + PADX + rx, py, pw, ph); /* the same frosted backdrop and accent as the sheet, for its own place */
  tween_to(&S.infoK, 1, 0.34f, EASE_ROW); }
static void info_close(void) { S.info = 0; tween_to(&S.infoK, 0, 0.26f, EASE_ROW); }
/* an episode's list: its Watched pill by its name at the top (Up from the list or the providers) */
static int is_ep_list(void) { return S.ep[0] && strchr(S.vid, ':') != NULL; }
static Item ep_item(void) { Item it; memset(&it, 0, sizeof it); snprintf(it.id, sizeof it.id, "%s", S.id); snprintf(it.type, sizeof it.type, "%s", S.type); snprintf(it.videoId, sizeof it.videoId, "%s", S.vid); it.ep = 1; return it; }
static void ep_toggle(void) { Item it = ep_item(); sound_play(SND_TOGGLE); lib_toggle_watched(&it, js_arr(sb_meta_cached(S.type, S.id), "videos")); app_refresh_boards(); }
static void ep_act(int a, int b) { (void)a; (void)b; S.zone = 2; ep_toggle(); }
static int key(int k) {
  if (S.zone == 2) { /* the Watched pill */
    if (k == K_OK) ep_toggle(); else if (k == K_DOWN) { S.zone = S.nchips > 1 ? 1 : 0; sound_focus(); } else if (k == K_BACK || k == K_LEFT) streams_close(0); else nudge_start(&S.nudge, k);
    return 1; }
  if (S.info) { float mx = S.infoH - (S.infoTo[3] - 2 * INFO_PAD); if (mx < 0) mx = 0;
    /* any way out closes it (it shrinks back into its row): Back, OK, Left/Right, or Up/Down past its ends */
    if (k == K_BACK || k == K_OK || k == K_LEFT || k == K_RIGHT) info_close();
    else if (k == K_DOWN || k == K_UP || k == K_CH_DOWN || k == K_CH_UP) {
      float st = (k == K_CH_DOWN || k == K_CH_UP) ? 480 : 160;
      float to = S.infoY.to + ((k == K_DOWN || k == K_CH_DOWN) ? st : -st);
      to = to < 0 ? 0 : to > mx ? mx : to; if (to == S.infoY.to) info_close(); else qt_to(&S.infoY, to, 0.24f); }
    return 1; }
  if (S.zone == 1) {
    if (k == K_RIGHT) { if (S.chipIdx >= S.nchips - 1) { nudge_start(&S.nudge, k); return 1; } S.chipIdx++; S.filter = S.chips[S.chipIdx]; S.idx = 0; S.enterAt = app_now(); rebuild(); }
    else if (k == K_LEFT) { if (S.chipIdx > 0) { S.chipIdx--; S.filter = S.chips[S.chipIdx]; S.idx = 0; S.enterAt = app_now(); rebuild(); } else streams_close(0); }
    else if (k == K_DOWN || k == K_CH_DOWN || k == K_OK) { if (S.n) S.zone = 0; else if (k == K_DOWN || k == K_CH_DOWN) nudge_start(&S.nudge, K_DOWN); }
    else if (k == K_UP || k == K_CH_UP) { if (is_ep_list()) { S.zone = 2; sound_focus(); } else nudge_start(&S.nudge, K_UP); }
    else if (k == K_BACK) streams_close(0);
    return 1; }
  if (k == K_DOWN) { if (S.idx < S.n - 1) S.idx++; else nudge_start(&S.nudge, k); }
  else if (k == K_CH_DOWN) { if (S.idx < S.n - 1) { S.idx = S.idx + 6 < S.n ? S.idx + 6 : S.n - 1; sound_focus(); } else nudge_start(&S.nudge, K_DOWN); }
  else if (k == K_UP) { if (S.idx > 0) S.idx--; else if (S.nchips > 1) S.zone = 1; else if (is_ep_list()) { S.zone = 2; sound_focus(); } else nudge_start(&S.nudge, k); }
  else if (k == K_CH_UP) { if (S.idx > 0) { S.idx = S.idx > 6 ? S.idx - 6 : 0; sound_focus(); } else if (S.nchips > 1) S.zone = 1; else if (is_ep_list()) { S.zone = 2; sound_focus(); } else nudge_start(&S.nudge, K_UP); }
  else if (k == K_RIGHT) nudge_start(&S.nudge, k);
  else if (k == K_OK) { if (S.idx < S.n && S.kind[S.idx] != R_PLAIN) { S.holding = 1; S.swelled = 0; S.holdAt = app_now(); } /* a tap plays it, held: its details */
    else if (S.idx < S.n) play(S.idx); }
  else if (k == K_LEFT || k == K_BACK) streams_close(0);
  return 1;
}
/* the row's header line: the addon's name lines joined into one compact line */
static void row_name(int i, char *out, size_t cap) {
  const Stream *s = S.items[i];
  if (S.kind[i] == R_BEST) { snprintf(out, cap, "%s", T("Best fit for this TV")); return; }
  if (S.kind[i] == R_RESUME) { snprintf(out, cap, "%s", s->name); return; }
  size_t o = 0; out[0] = 0; const char *p = s->name;
  while (*p) { const char *e = strchr(p, '\n'); size_t l = e ? (size_t)(e - p) : strlen(p); while (l && (*p == ' ' || *p == '\t' || *p == '\r')) { p++; l--; } while (l && (p[l - 1] == ' ' || p[l - 1] == '\t' || p[l - 1] == '\r')) l--;
    if (l && o + 6 < cap) o += snprintf(out + o, cap - o, "%s%.*s", o ? "  \xc2\xb7  " : "", (int)l, p); if (!e) break; p = e + 1; }
  if (!out[0]) snprintf(out, cap, "%s", s->addon);
}
static const char *row_desc(int i) {
  const Stream *s = S.items[i]; if (S.kind[i] == R_BEST) return S.bestSum; if (S.kind[i] == R_RESUME && S.resumeSum[0]) return S.resumeSum;
  if (!s->url[0] && s->ext[0]) { static char b[600]; snprintf(b, sizeof b, "%s%s%s", s->title, s->title[0] ? "\n" : "", T("Open on another device")); return b; } /* a web page: it opens on the phone */
  return s->title; }
#define NM_PX 27
#define NM_LH 35.0f   /* 27px at 1.3 */
#define DS_PX 26
#define DS_LH 37.0f   /* 26px at 1.42 */
#define ICO 26.0f     /* the play mark before the best and resume names, and its room */
static float head_h(void) { return 26 + text_line_height(24) + 10; }
static void layout(void) {
  if (S.laid) return; S.laid = 1; float y = 0; int lastG = -2;
  for (int i = 0; i < S.n; i++) { int g = S.itemGroup[i];
    S.headY[i] = -1; if (S.filter < 0 && S.kind[i] == R_PLAIN && g != lastG) { S.headY[i] = y; y += head_h(); } if (S.kind[i] == R_PLAIN) lastG = g;
    char *nm = S.nm[i]; row_name(i, nm, sizeof S.nm[i]); const char *ds = row_desc(i); int ico = S.kind[i] != R_PLAIN;
    int nl = text_wrap_count(nm, LW - 48 - (ico ? ICO + 10 : 0), 2, NM_PX, 600, 0.2f), dl = ds[0] ? text_wrap_count(ds, LW - 48, DS_MAX, DS_PX, 400, 0) : 0;
    S.itemY[i] = y; S.itemH[i] = 18 + nl * NM_LH + 8 + dl * DS_LH + 20; y += S.itemH[i] + 8; }
}
static void tick(float dt) {
  tween_tick(&S.x, dt > 0.034f ? 0.034f : dt); tween_tick(&S.op, dt > 0.034f ? 0.034f : dt); tween_tick(&S.out, dt); if (S.closing && !S.out.on && S.out.v <= 0) S.closing = 0;
  nudge_tick(&S.nudge, dt); qt_tick(&S.chipsX, dt); qt_tick(&S.listY, dt); fpill_tick(&S.pill, dt); sglass_tick(&S.glass, dt);
  layout(); float y = S.idx > 0 && S.idx - 1 < S.n ? S.itemY[S.idx - 1] : 0; /* the card above the focus stays fully visible */
  qt_to(&S.listY, -y, 0.24f);
  if (S.zone == 0 && S.idx < S.n) fpill_to(&S.pill, 0, S.itemY[S.idx], LW, S.itemH[S.idx], GLIDE_DUR); else fpill_hide(&S.pill);
  fpill_tick(&S.chipPill, dt); qt_tick(&S.ulX, dt); qt_tick(&S.ulW, dt); tween_to(&S.phA, S.loading && !S.n ? 1 : 0, 0.22f, EASE_IN_OUT); tween_to(&S.msgA, !S.loading && !S.n ? 1 : 0, 0.26f, EASE_IN_OUT); tween_to(&S.spA, S.loading ? 1 : 0, 0.25f, EASE_IN_OUT);
  tween_tick(&S.phA, dt); tween_tick(&S.msgA, dt); tween_tick(&S.spA, dt); tween_tick(&S.infoK, dt); qt_tick(&S.infoY, dt); sglass_tick(&S.pglass, dt);
  { int rk = S.zone * 1000 + S.idx + (S.idx < S.n ? S.kind[S.idx] * 100000 : 0); if (rk != S.restKey) { S.restKey = rk; S.restAt = app_now(); } }
  if (S.holding) {
    if (S.zone != 0 || S.idx >= S.n || S.kind[S.idx] == R_PLAIN || !S.open) S.holding = 0;
    else if (app_hold_k(S.holdAt) >= 1) { S.holding = 0; info_open(); }
    else if (!app_ok_down()) { S.holding = 0; play(S.idx); }
    else if (app_hold_k(S.holdAt) > 0 && !S.swelled) { S.swelled = 1; sound_play(SND_HOLD); } }
}
static void chip_focus(int i, int b) { (void)b; if (S.zone != 1 || S.chipIdx != i) { S.zone = 1; S.chipIdx = i; } }
static void chip_act(int i, int b) { (void)b; if (S.filter != S.chips[i]) { S.zone = 1; S.chipIdx = i; S.filter = S.chips[i]; S.idx = 0; rebuild(); } }
static void item_focus(int i, int b) { (void)b; if (S.zone != 0 || S.idx != i) { S.zone = 0; S.idx = i; } }
static void item_act(int i, int b) { (void)b; S.zone = 0; S.idx = i; play(i); }
static void outside_act(int a, int b) { (void)a; (void)b; streams_close(0); } /* beside the sheet: put it away */
/* the small spinner: a quarter turn of the accent on a faint ring */
static void spinner(float cx, float cy) {
  gfx_ring(cx - 10, cy - 10, 20, 20, 10, 3, acc_a(0.25f)); float ph = (float)fmod(app_now() / 0.8, 1) * 6.2832f;
  for (int i = 0; i < 7; i++) { float an = ph - 2.36f + i * 0.26f; gfx_circle(cx + cosf(an) * 8.5f, cy + sinf(an) * 8.5f, 1.5f, ACC2); }
}
static void chip_text(int i, char *lbl, size_t lc, char *cnt, size_t cc) { int g = S.chips[i]; if (g < 0) { snprintf(lbl, lc, "%s", T("All")); snprintf(cnt, cc, "%d", count()); } else { snprintf(lbl, lc, "%s", S.groups[g].name); snprintf(cnt, cc, "%d", S.groups[g].n); } }
static void draw_chip_row(float x0, float y, int rtl) {
  float x = 0; float xs[MAXG + 1], *ws = S.chW;
  if (!S.chipsLaid || S.chLg != i18n_gen()) { S.chipsLaid = 1; S.chLg = i18n_gen();
    for (int i = 0; i < S.nchips; i++) { chip_text(i, S.chLbl[i], sizeof S.chLbl[i], S.chCnt[i], sizeof S.chCnt[i]); ws[i] = 44 + text_width(S.chLbl[i], 24, 600, 0) + 10 + text_width(S.chCnt[i], 24, 600, 0); } }
  for (int i = 0; i < S.nchips; i++) { xs[i] = x; x += ws[i] + 6; }
  float want = 0; if (S.zone == 1 && S.chipIdx < S.nchips) { float e = xs[S.chipIdx] + ws[S.chipIdx]; if (e > LW - 20) want = e - LW + 20; }
  qt_to(&S.chipsX, want, 0.24f);
  gfx_clip_push(x0 - 6, y - 6, LW + 12, 76);
  /* the focus highlight glides from tab to tab as the list's does; the current tab's underline follows it */
  if (S.zone == 1 && S.chipIdx < S.nchips) fpill_to(&S.chipPill, xs[S.chipIdx], 0, ws[S.chipIdx], 56, GLIDE_DUR); else fpill_hide(&S.chipPill);
  int cu = -1; for (int i = 0; i < S.nchips; i++) if (S.chips[i] == S.filter) cu = i;
  if (cu >= 0) { float ux = xs[cu] + 20, uw = ws[cu] - 40; if (!S.ulOn) { qt_set(&S.ulX, ux); qt_set(&S.ulW, uw); S.ulOn = 1; } else { qt_to(&S.ulX, ux, GLIDE_DUR); qt_to(&S.ulW, uw, GLIDE_DUR); } } else S.ulOn = 0;
  #define CHIP_X(lx_, w_) (rtl ? x0 + LW - ((lx_) - S.chipsX.v) - (w_) : x0 + (lx_) - S.chipsX.v)
  { float nx = S.zone == 1 ? nudge_x(&S.nudge) : 0;
    if (S.chipPill.a.v > 0.001f) { gfx_offset_push(CHIP_X(S.chipPill.x.v, S.chipPill.w.v) - S.chipPill.x.v + nx, y); fpill_halo(&S.chipPill, 14, 0.96f, acc_a(0.34f)); fpill_draw(&S.chipPill, 14, 0.96f); gfx_offset_pop(); }
    if (S.ulOn) gfx_rrect(CHIP_X(S.ulX.v, S.ulW.v) + (cu == S.chipIdx && S.zone == 1 ? nx : 0), y + 56 + 5, S.ulW.v, 4, 2, acc_a(0.9f)); } /* 4 px, the text's width, just under the tab */
  for (int i = 0; i < S.nchips; i++) { const char *lbl = S.chLbl[i], *cnt = S.chCnt[i];
    int cur = S.chips[i] == S.filter, f = S.zone == 1 && i == S.chipIdx; float cx = CHIP_X(xs[i], ws[i]), h = 56;
    float nx = f ? nudge_x(&S.nudge) : 0; cx += nx;
    hit_add(cx, y, ws[i], h, chip_focus, chip_act, i, 0);
    uint32_t fg = f ? ACC_FG : cur ? WHITE_A(255) : WHITE_A(191); float ty = y + (h - text_line_height(24)) / 2;
    if (rtl) { text_draw(cnt, cx + 22, ty, 24, 600, col_alpha(fg, 0.66f), 0, 0, ALIGN_LEFT); text_draw(lbl, cx + ws[i] - 22, ty, 24, 600, fg, 0, 0, ALIGN_RIGHT); }
    else { float w = text_draw(lbl, cx + 22, ty, 24, 600, fg, 0, 0, ALIGN_LEFT); text_draw(cnt, cx + 22 + w + 10, ty, 24, 600, col_alpha(fg, 0.66f), 0, 0, ALIGN_LEFT); } }
  gfx_clip_pop();
  #undef CHIP_X
}
/* a row's way in: rows rise 22 px and fade in one after another (40 ms apart, 0.32 s each, the page's ease-out) when
   the sheet opens or the filter changes; a row an addon just sent fades and rises 12 px; closing, they sink a little
   with the sheet. Alpha and offset only: no extra layer */
static float row_in(int i, int vis, double now, float *dy) {
  float e = (float)((now - S.enterAt - 0.025 * (vis < 8 ? vis : 8)) / 0.24); e = e < 0 ? 0 : e > 1 ? 1 : e; e = 1 - (1 - e) * (1 - e) * (1 - e);
  float f = S.nwAt[i] > 0 && now - S.nwAt[i] < 0.3 ? (float)((now - S.nwAt[i]) / 0.3) : 1; f = 1 - (1 - f) * (1 - f) * (1 - f);
  float out = S.closing ? 1 - S.out.v : 0;
  *dy = (1 - e) * 14 + (1 - f) * 10 + out * 8; return e * f;
}
/* the details panel: the best row's accent plate grows into the panel and turns to the sheets' glass on the way; its
   words come in once it has room */
static void draw_info(float x0, int rtl) {
  const Stream *s = S.infoS; float k = S.infoK.v, e = clampf(k, 0, 1);
  float px = x0 + lerpf(S.infoFrom[0], S.infoTo[0], e), py = lerpf(S.infoFrom[1], S.infoTo[1], e), pw = lerpf(S.infoFrom[2], S.infoTo[2], e), ph = lerpf(S.infoFrom[3], S.infoTo[3], e);
  hit_shield(px, py, pw, ph);
  gfx_shadow(px, py + 14, pw, ph, 20, 46, BLACK_A((int)(120 * e)));
  sglass_draw(&S.pglass, px, py, pw, ph, 20);
  if (e < 1) gfx_rrect(px, py, pw, ph, 20, col_alpha(ACC, 1 - e)); /* (the row's accent, fading as it grows) */
  gfx_ring(px, py, pw, ph, 20, 1, WHITE_A((int)(31 * e)));
  float ca = clampf((k - 0.55f) / 0.45f, 0, 1); if (ca <= 0.001f) return;
  int al = rtl ? ALIGN_RIGHT : ALIGN_LEFT; float w = S.infoW, tx = rtl ? px + pw - INFO_PAD : px + INFO_PAD;
  gfx_clip_push(px + 4, py + 4, pw - 8, ph - 8); gfx_opacity_push(ca); gfx_offset_push(0, (1 - ca) * 8 - S.infoY.v);
  float y = py + INFO_PAD; char sub[320]; info_sub(s, sub, sizeof sub);
  text_draw(info_title(), tx, y, 30, 700, WHITE_A(255), -0.2f, w, al); y += text_line_height(30) + 8;
  y += text_draw_wrapped(sub, tx, y + (32 - text_line_height(24)) / 2, w, 3, 24, 600, ACC3, 0, al, 32) * 32.0f;
  for (int i = 0; i < 3; i++) { const char *t = info_text(s, i); if (!t[0]) continue; y += 26;
    char lb[64]; snprintf(lb, sizeof lb, "%s", T(INFO_LBL[i])); text_upper(lb, sizeof lb);
    text_draw(lb, tx, y, 21, 800, WHITE_A(166), 1.4f, w, al); y += text_line_height(21) + 6;
    y += text_draw_wrapped(t, tx, y + (36 - text_line_height(26)) / 2, w, 80, 26, 400, WHITE_A(230), 0, al, 36) * 36.0f; }
  gfx_offset_pop(); gfx_opacity_pop(); gfx_clip_pop();
  /* more below: a soft hint of it at the bottom edge */
  float mx = S.infoH - (S.infoTo[3] - 2 * INFO_PAD);
  if (mx > 1 && S.infoY.v < mx - 1) { gfx_opacity_push(ca); gfx_clip_push(px, py + ph - 56, pw, 56); gfx_rrect_v(px, py + ph - 76, pw, 76, 20, BLACK_A(0), BLACK_A(90)); gfx_clip_pop(); gfx_opacity_pop(); }
}
static void draw(void) {
  float a = S.closing ? S.out.v : S.open ? S.op.v : 0; if (a <= 0.001f) return;
  layout(); /* (the rows' words are made there; normally the tick already did) */
  int rtl = app_rtl(); float sx = sheet_x(), dx = S.x.v * (rtl ? -1 : 1);
  gfx_opacity_push(a); gfx_offset_push(dx, 0);
  if (S.open) { hit_act(0, 0, SB_W, SB_H, outside_act, 0, 0); hit_shield(sx, 0, SW, SB_H); }
  /* the sheet: soft leading corners (the far ones are off the screen), its shadow, the glass */
  float bx = rtl ? sx - 40 : sx, bw = SW + 40;
  if (S.glass.ok && S.glass.a.v >= 0.999f) gfx_shadow_under(rtl ? bx + 24 : bx - 24, 0, bw, SB_H, 32, 70, BLACK_A(128), bx, 0, bw, SB_H, 32); else gfx_shadow(rtl ? bx + 24 : bx - 24, 0, bw, SB_H, 32, 70, BLACK_A(128)); /* (the sheet opaque: none under it) */
  sglass_draw(&S.glass, bx, 0, bw, SB_H, 32);
  if (S.open && !S.closing && a >= 0.999f && fabsf(dx) < 0.01f && S.glass.ok && S.glass.a.v >= 0.999f) app_occluder(bx + 24, 0, bw - 48, SB_H); /* still and opaque */
  gfx_clip_push(rtl ? sx + SW - 1 : sx, 1, 1, SB_H - 2); gfx_ring(bx, 0, bw, SB_H, 32, 1, WHITE_A(18)); gfx_clip_pop(); /* its inner edge */
  float x0 = sx + PADX, lx = rtl ? x0 + LW : x0; int al = rtl ? ALIGN_RIGHT : ALIGN_LEFT;
  float y = 52; float hw = text_draw(T("Streams"), lx, y + (30 - text_line_height(24)) / 2, 24, 600, WHITE_A(184), 0, 0, al);
  if (S.spA.v > 0.001f) { gfx_opacity_push(S.spA.v); spinner(rtl ? lx - hw - 14 - 10 : lx + hw + 14 + 10, y + 15 + 1); gfx_opacity_pop(); }
  y += 30 + 6; text_draw(S.title, lx, y + (44 - text_line_height(36)) / 2, 36, 700, WHITE_A(255), -0.4f, LW, al); y += 44;
  if (S.ep[0]) { y += 4; float ew = text_draw(S.ep, lx, y, 24, 500, WHITE_A(199), 0, LW - 300, al);
    if (is_ep_list()) { Item it = ep_item(); int on = lib_is_marked_watched(&it), f = S.zone == 2; const char *lb = T(on ? "Watched" : "Mark as watched"); /* its Watched pill */
      float th = text_line_height(24), h = 40, w = 36 + 10 + text_width(lb, 21, 600, 0) + 22, px = rtl ? lx - ew - 20 - w : lx + ew + 20, py = y + th / 2 - h / 2, nx = f ? nudge_x(&S.nudge) : 0;
      hit_add(px, py, w, h, NULL, ep_act, 0, 0); gfx_offset_push(nx, 0);
      if (f) { gfx_rrect(px, py, w, h, h / 2, ACC | 0xff000000u); gfx_ring_out(px, py, w, h, h / 2, 4, acc_a(0.34f)); } else { gfx_rrect(px, py, w, h, h / 2, on ? acc_a(0.4f) : WHITE_A(20)); gfx_ring(px, py, w, h, h / 2, 1, WHITE_A(46)); }
      uint32_t fg = f ? ACC_FG : WHITE_A(240); icon_draw(on ? "check" : "eye", rtl ? px + w - 14 - 24 : px + 14, py + 8, 24, fg); text_draw(lb, rtl ? px + w - 46 : px + 46, py + (h - text_line_height(21)) / 2, 21, 600, fg, 0, 0, al);
      gfx_offset_pop(); } }
  if (S.nchips > 1) draw_chip_row(x0, CHIPS_Y, rtl);
  /* the list */
  gfx_clip_push(x0 - 16, CLIP_Y, LW + 32, SB_H - CLIP_Y); gfx_offset_push(0, CLIP_Y + LIST_PAD + S.listY.v); /* (room at the sides for a held row's line too) */
  if (S.msgA.v > 0.001f && !S.n) { const char *m = S.filter >= 0 ? T("No streams from this provider yet…") : emptyNote[0] ? emptyNote : T("No streams found."); text_draw(m, rtl ? lx - 22 : lx + 22, 28 + 8 * (1 - S.msgA.v), 26, 400, WHITE_A((int)(199 * S.msgA.v)), 0, LW - 44, al); }
  if (S.phA.v > 0.001f) { gfx_opacity_push(S.phA.v); /* rows on their way: an addon's header and rows laid out as the real ones (their plates, a
       name line and two lines of words in the waiting look), fading down the list */
    float yy = 0, rh = 18 + NM_LH + 8 + 2 * DS_LH + 20; static const float NW[4] = { 0.36f, 0.5f, 0.42f, 0.3f }, DW[4] = { 0.78f, 0.64f, 0.72f, 0.7f };
    ph_text(rtl ? lx - 22 : lx + 22, yy + 26, 170, 1, 24, text_line_height(24), al); yy += head_h();
    for (int i = 0; i < 4 && yy < SB_H - CLIP_Y; i++) { gfx_opacity_push(1 - 0.22f * i);
      gfx_rrect(x0, yy, LW, rh, 18, WHITE_A(14)); float tx = rtl ? lx - 24 : x0 + 24;
      ph_text(tx, yy + 18, (LW - 48) * NW[i], 1, NM_PX, NM_LH, al); ph_text(tx, yy + 18 + NM_LH + 8, (LW - 48) * DW[i], 2, DS_PX, DS_LH, al);
      gfx_opacity_pop(); yy += rh + 8; } gfx_opacity_pop(); }
  float top = -S.listY.v - 10, bot = -S.listY.v + SB_H - CLIP_Y; double now = app_now();
  /* the highlight under the focus, then the rows */
  int vis0 = -1; for (int i = 0; i < S.n; i++) if (S.itemY[i] + S.itemH[i] >= top) { vis0 = i; break; } /* the stagger counts from the first row in view */
  for (int i = 0; i < S.n; i++) { float iy = S.itemY[i], ih = S.itemH[i]; if (iy > bot) break; if (iy + ih < top) continue; /* every row's plate first, under the highlight too: no hole where it glides to */
    float rdy, fa = row_in(i, i - vis0, now, &rdy);
    gfx_rrect(x0, iy + rdy, LW, ih, 18, WHITE_A((int)(14 * fa + .5f))); }
  if (S.zone == 0 && S.n) { float pdy, pa = row_in(S.idx, S.idx - vis0, now, &pdy); /* the highlight comes in with its row */
    gfx_opacity_push(pa * (1 - S.infoK.v)); gfx_offset_push(x0 + nudge_x(&S.nudge), nudge_y(&S.nudge) + pdy); fpill_halo(&S.pill, 18, 0.97f, acc_a(0.34f)); fpill_draw(&S.pill, 18, 0.97f); gfx_offset_pop(); gfx_opacity_pop(); } /* the rows' own radius */
  for (int i = 0; i < S.n; i++) { float iy = S.itemY[i], ih = S.itemH[i]; if (iy > bot) break; if (iy + ih < top && (S.headY[i] < 0 || S.headY[i] + head_h() < top)) continue;
    int g = S.itemGroup[i]; float rdy, fa = row_in(i, i - vis0, now, &rdy);
    if (fa < 1) gfx_opacity_push(fa); if (rdy > 0.01f) gfx_offset_push(0, rdy);
    if (S.headY[i] >= 0) text_draw(S.groups[g].name, rtl ? lx - 22 : lx + 22, S.headY[i] + 26, 24, 600, WHITE_A(184), 0, LW - 44, al);
    int f = S.zone == 0 && i == S.idx; float nx = f ? nudge_x(&S.nudge) : 0, ny = f ? nudge_y(&S.nudge) : 0;
    hit_add(x0, iy, LW, ih, item_focus, item_act, i, 0);
    const char *nm = S.nm[i]; const char *ds = row_desc(i); int ico = S.kind[i] != R_PLAIN;
    float tx = rtl ? lx - 24 + nx : x0 + 24 + nx, ty = iy + 18 + ny; uint32_t nc = f ? ACC_FG : ACC3;
    if (ico) { icon_draw("play", rtl ? tx - ICO : tx, ty + (NM_LH - ICO) / 2 + 1, ICO, nc); tx += rtl ? -(ICO + 10) : ICO + 10; }
    int nl = text_draw_wrapped(nm, tx, ty + (NM_LH - text_line_height(NM_PX)) / 2, LW - 48 - (ico ? ICO + 10 : 0), 2, NM_PX, 600, nc, 0.2f, al, NM_LH); ty += nl * NM_LH + 8;
    if (ds[0]) text_draw_wrapped(ds, rtl ? lx - 24 + nx : x0 + 24 + nx, ty + (DS_LH - text_line_height(DS_PX)) / 2, LW - 48, DS_MAX, DS_PX, 400, f ? ACC_FG : WHITE_A(204), 0, al, DS_LH);
    if (S.kind[i] != R_PLAIN && f) { float ha = (float)((now - S.restAt - 3) / 0.4); ha = ha < 0 ? 0 : ha > 1 ? 1 : ha; /* resting on it 3 s: how to see all of it, faint */
      if (S.holding || S.info) ha = 0;
      if (ha > 0) key_hint(rtl ? x0 + 24 + nx : x0 + LW - 24 + nx, iy + 18 + ny + NM_LH / 2, K_OK, T("Hold for details"), col_alpha(ACC_FG, 0.8f * ha), !rtl); /* the remote's OK, then what holding it does */
      float hk = S.holding ? app_hold_k(S.holdAt) : 0; if (hk > 0) hold_trace_draw(x0 + nx, iy + ny, LW, ih, 18, ease_apply(EASE_IN_OUT, hk)); }
    if (rdy > 0.01f) gfx_offset_pop(); if (fa < 1) gfx_opacity_pop(); }
  gfx_offset_pop(); gfx_clip_pop();
  if (S.infoK.v > 0.001f && S.infoS) draw_info(x0, rtl);
  gfx_offset_pop(); gfx_opacity_pop();
}
/* movie streams minimised (opened, then closed with Left/Back): hint how to bring them back */
