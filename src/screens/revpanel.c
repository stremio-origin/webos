#include "core/episodeart.h"
/* The reviews (see revpanel.h). Two ways in: from a card (hold OK, Reviews) it grows out of the card as a glass
   panel and folds back on Back; from the title page (Down, or its Reviews button) it rises into the page itself under
   the page's buttons, which stay at the top. Left: the picture with its tag (or the series / seasons switch), the
   logo or name, the words, every source's score in a ring; right: the score with its spread, the comments' tabs and
   the comments (Up / Down through them, OK opens a long one whole or shows a spoiler). */
#include "screens/revpanel.h"
#include "screens/detail.h"
#include "screens/sheet.h"
#include "core/app.h"
#include "core/keys.h"
#include "core/i18n.h"
#include "core/json.h"
#include "core/hit.h"
#include "core/trakt.h"
#include "core/trailer.h"
#include "ui/qrcard.h"
#include "ui/gfx.h"
#include "ui/glass.h"
#include "ui/text.h"
#include "ui/img.h"
#include "ui/icon.h"
#include "ui/widgets.h"
#include "ui/sound.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define PW 1600.0f
#define PH 920.0f
#define PX ((SB_W - PW) / 2)
#define PY ((SB_H - PH) / 2)
#define PAD 44.0f
#define LW 660.0f           /* the left column */
#define GAP 48.0f
#define RW (PW - 2 * PAD - LW - GAP) /* the right one */
#define STILL_H 371.0f      /* 16:9 of the left column */
#define BD 64.0f            /* an option's round button */
#define BG 12.0f
#define SYN_LH 34.0f
#define CM_LH 31.0f         /* a comment's lines */
#define CM_LINES 3          /* shown; OK opens it whole */
#define CM_FULL 14
#define MAXOPT 6
#define MAXSW 41           /* the switch: the series, then each season */
#define PG_TOP 168.0f      /* in the page: the reviews' top, under the title page's buttons held at the top */
#define RING_D 84.0f       /* a source's ring */
#define RING_ROW (22 + RING_D + 6 + 32 + 24) /* the scores under the synopsis: the gap, a ring each, its score, its name */
#define LOGO_W 400.0f
#define LOGO_H 120.0f
enum { O_PLAY, O_STREAMS, O_WATCHED, O_TRAILER, O_LIB };
typedef struct { int kind; QT x; Tween la; int f; double fAt; } Opt;
static struct {
  int open, closing, page; Item it, it_ep; /* it_ep: as given (an episode keeps its video id) */ /* page: risen from the foot of the title page (Down), not grown out of a button */ int season, episode, stackSeason, sel; /* sel: 0 the show, 1 the season (the switch) */
  const Reviews *rv; float from[5]; Timer1 in, out; SGlass glass;
  int zone; /* 0 options, 1 comments, 2 the switch */ int optIdx, nopt, cIdx, expanded; Opt opt[MAXOPT]; FPill pill, cpill; Nudge nudge; QT listY; double openAt;
  /* laid out once per change (the answers, the language, the library, what is opened): never per frame */
  unsigned lay, rvVer, lg, lv; int layExp, laySel;
  char tag[64], chip[MAXSW][40],  title[300], info[200], syn[1100], still[512], logo[512], spost[160]; int logoOn, layLogo; int synN, synMax, nShown; const RevTile *tiles[6];
  char scoreS[16], votesS[64], when[REV_MAXC][48]; int pct[10], nl[REV_MAXC], more[REV_MAXC]; float cy[REV_MAXC], ch[REV_MAXC], listH; QT cyA[REV_MAXC]; int animN; double tileAt[6]; int tileN; double apAt[8]; unsigned apSig[8]; char tileV[6][16]; /* when each part first had something to show (it fades in from then) */ int tab, ntab, layTab, vis[REV_MAXC], nvis; char tabSrc[4][12]; int tabN[4]; Tween tbF[4]; FPill tpill; float tabX[4], tabW[4]; double tabAt; int tabDir; /* the comments' tabs: all, then each source; the highlight glides between them, the list slides in */ Tween chF[MAXSW]; FPill spill; int ns, seasonList[MAXSW - 1]; QT swX; /* the seasons the switch steps through, its scroll */ /* the comments' places as they slide, the chips' focus as it fades */
  char optLbl[MAXOPT][64]; const char *optIcon[MAXOPT]; float optW[MAXOPT]; float ttlW; int ttlN;
} R;
static Screen panel; static Screen *under;
static float quint(float x) { return 1 - powf(1 - x, 5); }
enum { AP_STILL, AP_LOGO, AP_INFO, AP_SYN, AP_SCORE, AP_TABS, AP_COMM, AP_SPOST, AP_N };
/* a part's fade: 0 while it has nothing, then in over 0.35 s from when it first had something; there from the start
   (the panel's own entrance carries it), it is simply there */
static float ap_sig(int i, int present, unsigned sig) { /* sig: what it shows; a new one fades in again (only that part) */
  if (!present) { R.apAt[i] = 0; return 0; } double now = app_now(); if (R.apAt[i] <= 0) { R.apAt[i] = now - R.openAt < 0.12 ? now - 1 : now; R.apSig[i] = sig; }
  else if (sig != R.apSig[i]) { R.apSig[i] = sig; R.apAt[i] = now; }
  float k = (float)((now - R.apAt[i]) / 0.35); return k >= 1 ? 1 : 1 - (1 - k) * (1 - k) * (1 - k); }
static float ap(int i, int present) { return ap_sig(i, present, R.apSig[i]); }
static unsigned hsh(const char *s) { unsigned h = 2166136261u; for (; s && *s; s++) h = (h ^ (unsigned char)*s) * 16777619u; return h; }
static int is_ep(void) { return R.episode > 0; }
static const cJSON *meta(void) { return sb_meta_cached(R.it.type, R.it.id); }
static const cJSON *video(void) { const cJSON *v; char vid[80]; if (!is_ep()) return NULL; snprintf(vid, sizeof vid, "%s:%d:%d", R.it.id, R.season, R.episode);
  cJSON_ArrayForEach(v, js_arr(meta(), "videos")) if (!strcmp(js_str(v, "id", ""), vid)) return v; return NULL; }
/* "Jan 8, 2021" from "2021-01-08..." (as the title page writes it) */
static void date_of(const char *r, char *out, size_t cap) {
  out[0] = 0; int y, m, d; if (!r || strlen(r) < 10 || sscanf(r, "%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12) return;
  static const char *M[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" }; char ds[8], ys[8], mon[32]; snprintf(ds, sizeof ds, "%d", d); snprintf(ys, sizeof ys, "%d", y); snprintf(mon, sizeof mon, "%s", T(M[m - 1]));
  char tmp[64]; snprintf(tmp, sizeof tmp, "%s", T2("{mon} {d}, {y}", "mon", mon, "d", ds)); char *yp = strstr(tmp, "{y}"); if (yp) { *yp = 0; snprintf(out, cap, "%s%s%s", tmp, ys, yp + 3); } else snprintf(out, cap, "%s", tmp);
}
static void compact(int n, char *out, size_t cap) { if (n >= 1000000) snprintf(out, cap, "%.1fM", n / 1e6); else if (n >= 10000) snprintf(out, cap, "%dk", n / 1000); else if (n >= 1000) snprintf(out, cap, "%.1fk", n / 1e3); else snprintf(out, cap, "%d", n); }
static void ago(int days, char *out, size_t cap) {
  char n[16]; if (days < 0) { out[0] = 0; return; }
  if (days == 0) snprintf(out, cap, "%s", T("Today")); else if (days == 1) snprintf(out, cap, "%s", T("Yesterday"));
  else if (days < 31) { snprintf(n, sizeof n, "%d", days); snprintf(out, cap, "%s", T1("{n} days ago", "n", n)); }
  else if (days < 365) { snprintf(n, sizeof n, "%d", days / 30); snprintf(out, cap, "%s", days / 30 == 1 ? T("A month ago") : T1("{n} months ago", "n", n)); }
  else { snprintf(n, sizeof n, "%d", days / 365); snprintf(out, cap, "%s", days / 365 == 1 ? T("A year ago") : T1("{n} years ago", "n", n)); }
}
/* ---- the options: what the card menu offers, here as round buttons ---- */
static int ep_watched(void) { return lib_is_marked_watched(&R.it); }
static void opt_label(int kind, char *out, size_t cap, const char **icon) {
  switch (kind) {
    case O_PLAY: { cJSON *lb = lib_get(R.it.id); int rs = R.it.cw || js_num(js_obj(lb, "state"), "timeOffset", 0) > 60000; *icon = "play"; snprintf(out, cap, "%s", T(rs ? "Resume" : "Play")); break; }
    case O_STREAMS: *icon = "play"; snprintf(out, cap, "%s", T("Streams")); break;
    case O_WATCHED: { int w = ep_watched(); *icon = w ? "eyeOff" : "eye"; snprintf(out, cap, "%s", T(w ? "Mark as unwatched" : "Mark as watched")); break; }
    case O_TRAILER: *icon = "trailer"; snprintf(out, cap, "%s", T("Trailer")); break;
    default: { int on = lib_has(R.it.id); *icon = on ? "check" : "plus"; snprintf(out, cap, "%s", T(on ? "In Library" : "Library")); }
  }
}
static void make_opts(void) {
  int kinds[MAXOPT], n = 0, onPage = SCR == SCR_DETAIL && !strcmp(detail_id(), R.it.id);
  if (is_ep()) kinds[n++] = O_STREAMS; else if (!onPage) kinds[n++] = O_PLAY; else if (strcmp(R.it.type, "series")) kinds[n++] = O_STREAMS;
  if (is_ep() || strcmp(R.it.type, "series") || !R.season) kinds[n++] = O_WATCHED;
  if (trailer_available(meta(), R.it.id)) kinds[n++] = O_TRAILER; kinds[n++] = O_LIB;
  int same = n == R.nopt; for (int i = 0; same && i < n; i++) same = R.opt[i].kind == kinds[i];
  if (!same) { for (int i = 0; i < n; i++) { Opt *o = &R.opt[i]; float x = i < R.nopt ? o->x.v : (i ? R.opt[i - 1].x.v + BD + BG : 0); memset(o, 0, sizeof *o); o->kind = kinds[i]; qt_set(&o->x, x); } R.nopt = n; }
  if (R.optIdx >= R.nopt) R.optIdx = R.nopt - 1;
}
static void layout(void);
static void toggle_watched(void) {
  Item it = R.it; sound_play(SND_TOGGLE); const cJSON *m = meta(); lib_toggle_watched(&it, js_arr(m, "videos")); app_refresh_boards(); if (SCR == SCR_DETAIL) detail_refresh_watched(); if (ep_watched() && !is_ep()) card_mark_pop(it.id);
}
static void close_panel(void) { if (!R.open || R.closing) return; R.closing = 1; app_occluder_drop(); timer_start(&R.out, R.page ? 0.34f : 0.26f); }
static void finish(void) { R.open = R.closing = 0; if (app_overlay == &panel) app_overlay = under; under = NULL; }
static void act(int kind) {
  Item it = R.it; fpill_press(&R.pill);
  switch (kind) {
    case O_PLAY: close_panel(); app_open_item(&it); break;
    case O_STREAMS: close_panel(); if (SCR == SCR_DETAIL) detail_open_streams(is_ep() ? it.videoId : it.id); else app_open_item(&it); break;
    case O_WATCHED: toggle_watched(); break;
    case O_TRAILER: close_panel(); detail_trailer(it.type, it.id, it.name); break;
    case O_LIB: sound_play(SND_TOGGLE); lib_toggle(&it); app_refresh_boards(); if (SCR == SCR_DETAIL) detail_paint_lib(); break;
  }
}
static int key_card(void) { return !R.rv->nc && !R.rv->pending && !trakt_has_key(); } /* the comments' place holds the Trakt card */
/* the switch: the show's reviews, or the season's */
static void tab_go(int t) { if (t == R.tab || t < 0 || t >= R.ntab) return; R.tabDir = t > R.tab ? 1 : -1; R.tab = t; R.cIdx = 0; R.expanded = -1; R.animN = 0; qt_set(&R.listY, 0); R.tabAt = app_now(); sound_focus(); }
static void select_sel(int sel) { if (sel == R.sel || sel < 0 || sel > R.ns) return; R.sel = sel; R.season = sel ? R.seasonList[sel - 1] : 0; R.rv = reviews_get(R.it.type, R.it.id, R.season, 0); R.cIdx = 0; R.expanded = -1; R.animN = 0; R.tab = 0; /* (what stays the same stays; what changes fades in by itself) */ qt_set(&R.listY, 0); fpill_snap(&R.cpill); sound_play(SND_TOGGLE); }
static int key(int k) {
  if (R.closing) { if (!R.open) return 1; finish(); return 1; }
  if (k == K_BACK) { close_panel(); return 1; }
  if (R.zone == 0 && R.page) { /* in the page: its own row is the title page's buttons, held at the top */
    if (k == K_DOWN) { if (R.stackSeason) { R.zone = 2; sound_focus(); } else if (R.rv->nc) { R.zone = 3; sound_focus(); } else if (key_card()) { R.zone = 1; sound_focus(); } else nudge_start(&R.nudge, k); } /* (the buttons, the seasons, the tabs, the comments: top to bottom) */
    else if (k == K_UP) close_panel(); else detail_page_btn_key(k);
    return 1; }
  if (R.zone == 0) { int b0 = R.optIdx;
    if (k == K_RIGHT) R.optIdx = R.optIdx + 1 < R.nopt ? R.optIdx + 1 : R.optIdx; else if (k == K_LEFT) R.optIdx = R.optIdx > 0 ? R.optIdx - 1 : 0;
    else if (k == K_DOWN) { if (R.rv->nc) { R.zone = 3; sound_focus(); } else if (key_card()) { R.zone = 1; sound_focus(); } else nudge_start(&R.nudge, k); }
    else if (k == K_UP) { if (R.stackSeason) { R.zone = 2; sound_focus(); } else if (R.page) close_panel(); else nudge_start(&R.nudge, k); } /* (risen from the page: Up lets it down again) */
    else if (k == K_OK) { if (R.optIdx < R.nopt) act(R.opt[R.optIdx].kind); }
    if ((k == K_LEFT || k == K_RIGHT) && R.optIdx == b0) nudge_start(&R.nudge, k); else if (k == K_LEFT || k == K_RIGHT) sound_focus();
    return 1; }
  if (R.zone == 2) {
    if (k == K_LEFT || k == K_RIGHT) { int d = (k == K_RIGHT) != app_rtl() ? 1 : -1, s = R.sel + d; if (s >= 0 && s <= R.ns) select_sel(s); else nudge_start(&R.nudge, k); } /* (each step shows that one's reviews) */
    else if (k == K_OK) nudge_start(&R.nudge, k);
    else if (R.page && k == K_DOWN) { if (R.rv->nc) { R.zone = 3; sound_focus(); } else if (key_card()) { R.zone = 1; sound_focus(); } else nudge_start(&R.nudge, k); }
    else if (R.page && k == K_UP) { R.zone = 0; sound_focus(); }
    else if (k == K_DOWN) { R.zone = 0; sound_focus(); } else nudge_start(&R.nudge, k);
    return 1; }
  if (R.zone == 3) { /* the tabs */
    if (k == K_LEFT || k == K_RIGHT) { int rtl = app_rtl(), d = (k == K_RIGHT) != rtl ? 1 : -1, t = R.tab + d; if (t >= 0 && t < R.ntab) tab_go(t); else nudge_start(&R.nudge, k); }
    else if (k == K_UP) { R.zone = R.page && R.stackSeason ? 2 : 0; sound_focus(); } else if (k == K_DOWN || k == K_OK) { if (R.nvis) { R.zone = 1; R.cIdx = 0; sound_focus(); } else nudge_start(&R.nudge, k); } else nudge_start(&R.nudge, k);
    return 1; }
  if (!R.rv->nc) { if (k == K_UP) { R.zone = 0; sound_focus(); } else if (k == K_OK && key_card()) qrcard_show_trakt(NULL, NULL); else nudge_start(&R.nudge, k); return 1; }
  int c0 = R.cIdx;
  if (k == K_DOWN) R.cIdx = R.cIdx + 1 < R.nvis ? R.cIdx + 1 : R.cIdx;
  else if (k == K_UP) { if (R.cIdx > 0) R.cIdx--; else { R.zone = 3; sound_focus(); return 1; } }
  else if (k == K_OK) { R.expanded = R.expanded == R.cIdx ? -1 : R.cIdx; sound_play(SND_TOGGLE); return 1; }
  if (R.cIdx == c0) nudge_start(&R.nudge, k); else sound_focus();
  return 1;
}
/* ---- the layout: strings and heights, made again when something they show changed ---- */
static void tiles_pick(void) { R.nShown = 0; for (int i = 0; i < R.rv->ntiles && R.nShown < 6; i++) R.tiles[R.nShown++] = &R.rv->tiles[i]; } /* every source, the main score's too */
static void layout(void) {
  unsigned lg = i18n_gen(), lv = lib_version();
  if (R.lay && R.rvVer == R.rv->ver && R.lg == lg && R.lv == lv && R.layExp == R.expanded && R.laySel == R.sel && R.layTab == R.tab && R.layLogo == (R.logo[0] && !img_failed(R.logo))) return;
  R.lay++; R.rvVer = R.rv->ver; R.lg = lg; R.lv = lv; R.layExp = R.expanded; R.laySel = R.sel; R.layTab = R.tab;
  const cJSON *m = meta(), *v = video(); char n[16], s[16]; int rtl = app_rtl();
  /* the words on the left */
  const char *nm = js_str(m, "locName", NULL); if (!nm || !*nm) nm = js_str(m, "name", R.it.name);
  if (is_ep()) { snprintf(n, sizeof n, "%d", R.episode); snprintf(s, sizeof s, "%d", R.season); snprintf(R.tag, sizeof R.tag, "%s", T1("Episode {n}", "n", n)); text_upper(R.tag, sizeof R.tag);
    const char *en = js_str(v, "name", js_str(v, "title", R.it.epName)); snprintf(R.title, sizeof R.title, "%s%s%s", T2("S{s}E{e}", "s", s, "e", n), *en ? "  \xc2\xb7  " : "", en);
    char d[64], rt[32] = ""; date_of(js_str(v, "released", js_str(v, "firstAired", NULL)), d, sizeof d); int mn = sb_runtime_min(js_str(v, "runtime", "")); if (mn) sb_fmt_runtime(mn, rt, sizeof rt);
    snprintf(R.info, sizeof R.info, "%s%s%s", rt, rt[0] && d[0] ? "  \xc2\xb7  " : "", d);
    snprintf(R.syn, sizeof R.syn, "%s", js_str(v, "overview", js_str(v, "description", "")));
    const char *th = js_str(v, "thumbnail", NULL); if (th && *th) snprintf(R.still, sizeof R.still, "%s", th); else if (sb_is_imdb(R.it.id)) snprintf(R.still, sizeof R.still, "https://episodes.metahub.space/%s/%d/%d/w780.jpg", R.it.id, R.season, R.episode); else R.still[0] = 0; episode_art_upgrade_tmdb(R.still, sizeof R.still, 780); }
  else { snprintf(R.chip[0], sizeof R.chip[0], "%s", T(!strcmp(R.it.type, "series") ? "Series" : "Movie"));
    for (int i = 0; i < R.ns; i++) { snprintf(s, sizeof s, "%d", R.seasonList[i]); snprintf(R.chip[i + 1], sizeof R.chip[i + 1], "%s", T1("Season {n}", "n", s)); }
    snprintf(R.tag, sizeof R.tag, "%s", R.chip[R.sel]); text_upper(R.tag, sizeof R.tag);
    snprintf(R.title, sizeof R.title, "%s", nm);
    const char *rel = js_str(m, "releaseInfo", js_str(m, "year", "")); char rt[32] = ""; int mn = sb_runtime_min(js_str(m, "runtime", "")); if (mn) sb_fmt_runtime(mn, rt, sizeof rt);
    char gen[120] = ""; const cJSON *gs = js_arr(m, "genres"); if (gs) { int k = 0; const cJSON *g; cJSON_ArrayForEach(g, gs) { if (k >= 3 || !cJSON_IsString(g)) break; size_t l = strlen(gen); snprintf(gen + l, sizeof gen - l, "%s%s", k ? T(", ") : "", T(g->valuestring)); k++; } }
    snprintf(R.info, sizeof R.info, "%s%s%s%s%s", rel, *rel && *rt ? "  \xc2\xb7  " : "", rt, (*rel || *rt) && *gen ? "  \xc2\xb7  " : "", gen);
    snprintf(R.syn, sizeof R.syn, "%s", js_str(m, "description", R.it.description)); R.spost[0] = 0;
    if (R.sel > 0) { int sn = R.seasonList[R.sel - 1], ne = 0, yr = 0; const cJSON *v; /* a season: its words, its episodes and year, its poster */
      cJSON_ArrayForEach(v, js_arr(m, "videos")) if ((int)js_num(v, "season", -1) == sn) { ne++; const char *r = js_str(v, "released", js_str(v, "firstAired", "")); int y = atoi(r); if (y > 1900 && (!yr || y < yr)) yr = y; }
      char ns[12], es[12], ys[12]; snprintf(ns, sizeof ns, "%d", sn); snprintf(es, sizeof es, "%d", ne); snprintf(ys, sizeof ys, "%d", yr);
      snprintf(R.info, sizeof R.info, "%s%s%s%s%s", T1("Season {n}", "n", ns), ne ? "  \xc2\xb7  " : "", ne ? T1("{n} episodes", "n", es) : "", yr ? "  \xc2\xb7  " : "", yr ? ys : "");
      if (R.rv->overview[0]) snprintf(R.syn, sizeof R.syn, "%s", R.rv->overview); snprintf(R.spost, sizeof R.spost, "%s", R.rv->poster); }
    Item it = R.it; if (m) { sb_fill_item(&it, m, R.it.type); } char u[512]; sb_bg_mid(&it, u, sizeof u); snprintf(R.still, sizeof R.still, "%s", u); /* (the page's backdrop: its decoded copy is reused) */ }
  if (!is_ep()) { const char *ll = js_str(m, "locLogo", NULL); Item it = R.it; if (m) sb_fill_item(&it, m, R.it.type); char lu[512]; sb_logo_of(&it, lu, sizeof lu); snprintf(R.logo, sizeof R.logo, "%s", ll && *ll ? ll : lu); } else R.logo[0] = 0; /* the title page's own logo (its cached copy) */
  R.logoOn = R.layLogo = R.logo[0] && !img_failed(R.logo);
  R.ttlN = R.logoOn ? 0 : text_wrap_count(R.title, LW, 2, 34, 700, 0);
  /* the synopsis: four lines, or all that fits under the still when opened */
  tiles_pick(); float ty = PAD + STILL_H + 20 + (R.logoOn ? LOGO_H + 14 : R.ttlN * 40) + text_line_height(24) + 14, tilesH = RING_ROW; /* (the scores under it, there or on their way) */ R.synMax = (int)((PH - PAD - 40 - ty - tilesH) / SYN_LH); if (R.synMax < 3) R.synMax = 3; /* all of the synopsis that fits above the scores, always */
  R.synN = R.syn[0] ? text_wrap_count(R.syn, LW, R.synMax, 25, 400, 0) : 0;
  tiles_pick();
  /* the score */
  snprintf(R.scoreS, sizeof R.scoreS, "%.1f", R.rv->score); R.votesS[0] = 0;
  if (R.rv->scoreSrc[0]) { char c[16]; compact(R.rv->votes, c, sizeof c); if (R.rv->votes > 0) snprintf(R.votesS, sizeof R.votesS, "%s  \xc2\xb7  %s", T1("{n} ratings", "n", c), R.rv->scoreSrc); else snprintf(R.votesS, sizeof R.votesS, "%s", R.rv->scoreSrc); }
  { int tot = 0; for (int i = 0; i < 10; i++) tot += R.rv->dist[i]; for (int i = 0; i < 10; i++) R.pct[i] = tot ? (int)(100.0f * R.rv->dist[i] / tot + 0.5f) : 0; }
  /* the comments: their time, lines and places */
  /* the tabs: all, then each source the comments came from (with how many); the list shows the tab's */
  R.ntab = 1; R.tabN[0] = R.rv->nc; snprintf(R.tabSrc[0], sizeof R.tabSrc[0], "%s", "");
  for (int i = 0; i < R.rv->nc; i++) { const char *f = R.rv->c[i].from; int t = 1; while (t < R.ntab && strcmp(R.tabSrc[t], f)) t++; if (t == R.ntab && t < 4) { snprintf(R.tabSrc[t], sizeof R.tabSrc[t], "%s", f); R.tabN[t] = 0; R.ntab++; } if (t < 4) R.tabN[t]++; }
  if (R.tab >= R.ntab) R.tab = 0;
  R.nvis = 0; for (int i = 0; i < R.rv->nc; i++) if (!R.tab || !strcmp(R.rv->c[i].from, R.tabSrc[R.tab])) R.vis[R.nvis++] = i;
  if (R.cIdx >= R.nvis) R.cIdx = R.nvis ? R.nvis - 1 : 0;
  float y = 0; for (int i = 0; i < R.nvis; i++) { const RevComment *c = &R.rv->c[R.vis[i]]; ago(c->days, R.when[i], sizeof R.when[i]);
    int lines = i == R.expanded ? CM_FULL : CM_LINES; R.more[i] = !c->spoiler && text_wrap_count(c->text, RW - 32, CM_LINES + 1, 23, 400, 0) > CM_LINES; /* (cut short: OK reads it whole) */ R.nl[i] = c->spoiler && i != R.expanded ? 0 : text_wrap_count(c->text, RW - 32, lines, 23, 400, 0); /* (0: a spoiler kept hidden) */
    R.cy[i] = y; R.ch[i] = revpanel_comment_h(c, RW, R.nl[i]); y += R.ch[i] + 12; }
  R.listH = y > 0 ? y - 12 : 0;
  make_opts(); for (int i = 0; i < R.nopt; i++) { opt_label(R.opt[i].kind, R.optLbl[i], sizeof R.optLbl[i], &R.optIcon[i]); R.optW[i] = text_width(R.optLbl[i], 24, 600, 0); }
  (void)rtl;
}
static void tick(float dt) {
  if (!R.open && !R.closing) return;
  timer_tick(&R.in, dt); sglass_tick(&R.glass, dt); nudge_tick(&R.nudge, dt); fpill_tick(&R.pill, dt); fpill_tick(&R.cpill, dt); qt_tick(&R.listY, dt);
  if (R.closing) { if (timer_tick(&R.out, dt) >= 1) finish(); return; }
  R.rv = reviews_get(R.it.type, R.it.id, R.season, R.episode); layout();
  /* the options: the focused one opens into a pill with its name, the others slide aside */
  { double now = app_now(); float x = 0, fx = 0, fw = BD; int fi = R.zone == 0 ? R.optIdx : -1;
    for (int i = 0; i < R.nopt; i++) { Opt *o = &R.opt[i]; int f = i == fi; if (f != o->f) { o->f = f; o->fAt = now; } float w = f ? BD + R.optW[i] + 26 : BD;
      qt_to(&o->x, x, 0.3f); qt_tick(&o->x, dt); if (f) { fx = x; fw = w; } x += w + BG;
      if (f && now - o->fAt >= 0.06) tween_to(&o->la, 1, 0.18f, EASE_IN_OUT); else if (!f) tween_to(&o->la, 0, 0.18f, EASE_IN_OUT); tween_tick(&o->la, dt); }
    if (fi >= 0 && !R.page) fpill_to(&R.pill, fx, 0, fw, BD, GLIDE_DUR); else fpill_hide(&R.pill); }
  /* the comments slide to their places (one opened or closed pushes the others), new ones start where they are */
  for (int i = 0; i < R.nvis; i++) { if (i >= R.animN) qt_set(&R.cyA[i], R.cy[i]); else qt_to(&R.cyA[i], R.cy[i], 0.3f); qt_tick(&R.cyA[i], dt); } R.animN = R.nvis;
  for (int i = 0; i < 4; i++) { tween_to(&R.tbF[i], R.zone == 3 && R.tab == i ? 1 : 0, 0.18f, EASE_IN_OUT); tween_tick(&R.tbF[i], dt); }
  if (R.zone == 3 && R.tab < R.ntab && R.tabW[R.tab] > 0) fpill_to(&R.tpill, R.tabX[R.tab], 0, R.tabW[R.tab], 14 + text_line_height(22), GLIDE_DUR); else fpill_hide(&R.tpill); fpill_tick(&R.tpill, dt);
  for (int i = 0; i <= R.ns && i < MAXSW; i++) { tween_to(&R.chF[i], R.zone == 2 && R.sel == i ? 1 : 0, 0.18f, EASE_IN_OUT); tween_tick(&R.chF[i], dt); } qt_tick(&R.swX, dt);
  /* the comments: the highlight under the focused one, the list scrolled so it shows whole */
  if (R.zone == 1 && R.cIdx < R.nvis) { int i = R.cIdx; fpill_to(&R.cpill, 0, R.cy[i], RW, R.ch[i], GLIDE_DUR); }
  else if (R.zone == 1 && key_card()) fpill_to(&R.cpill, 0, 0, RW, 32 + text_line_height(26) + 6 + 2 * 28, GLIDE_DUR);
  else fpill_hide(&R.cpill);
  if (R.zone == 1 && !R.rv->nc && !key_card()) R.zone = 0; /* the card went (the id came): back to the options */
}
/* ---- drawing ---- */
static void outside(int a, int b) { (void)a; (void)b; close_panel(); }
static void opt_focus(int i, int b) { (void)b; if (R.zone != 0 || R.optIdx != i) { R.zone = 0; R.optIdx = i; } }
static void opt_act(int i, int b) { (void)b; R.zone = 0; R.optIdx = i; act(R.opt[i].kind); }
static void cm_focus(int i, int b) { (void)b; if (R.zone != 1 || R.cIdx != i) { R.zone = 1; R.cIdx = i; } }
static void cm_act(int i, int b) { (void)b; R.zone = 1; R.cIdx = i; key(K_OK); }
static void sw_act(int i, int b) { (void)b; R.zone = 2; select_sel(i); }
static void tab_act(int i, int b) { (void)b; R.zone = 3; tab_go(i); }
/* a small glass chip with words (the tag on the still) */
static float chip(float x, float y, const char *s, float f, int cur, int rtl) { /* f: how focused, 0..1 (it fades) */
  float w = text_width(s, 22, 700, 0.3f) + 36, h = 14 + text_line_height(22), cx = rtl ? x - w : x;
  if (f < 0.999f) { gfx_shadow(cx, y + 4, w, h, h / 2, 18, BLACK_A(110)); gfx_rrect(cx, y, w, h, h / 2, cur ? acc_a(0.55f) : RGBA(14, 12, 20, 178)); gfx_rrect_v(cx, y, w, h, h / 2, WHITE_A(26), WHITE_A(6)); gfx_ring(cx, y, w, h, h / 2, 1, WHITE_A(46)); }
  if (f > 0.001f) { gfx_opacity_push(f); gfx_rrect(cx, y, w, h, h / 2, ACC); gfx_rrect_h(cx, y, w, h, h / 2, WHITE_A(0), WHITE_A(56)); gfx_ring_out(cx, y, w, h, h / 2, 4, acc_a(0.34f)); gfx_opacity_pop(); }
  text_draw(s, cx + w / 2, y + 7, 22, 700, col_mix(WHITE_A(245), ACC_FG, f), 0.3f, 0, ALIGN_CENTER);
  return w;
}
/* ---- each source as itself: its badge, its colours, its score as a share of the most it gives ---- */
static float share_of(const char *val) { /* "8.0" of 10, "85%" or a Metascore "78" of 100 */
  float v = (float)atof(val); size_t n = strlen(val); if (n && val[n - 1] == '%') return v / 100; return strchr(val, '.') || v <= 10 ? v / 10 : v / 100; }
static uint32_t verdict_col(const char *src, float k) { /* the colour each source gives a score of this share */
  if (!strcmp(src, "Rotten Tomatoes")) return k >= 0.6f ? RGB(250, 50, 10) : RGB(10, 200, 85);
  if (!strcmp(src, "Metacritic")) return k >= 0.61f ? RGB(102, 204, 51) : k >= 0.4f ? RGB(255, 204, 51) : RGB(255, 64, 64);
  if (!strcmp(src, "IMDb")) return RGB(245, 197, 24);
  if (!strcmp(src, "TMDB")) return k >= 0.7f ? RGB(33, 208, 122) : k >= 0.4f ? RGB(210, 213, 49) : RGB(219, 35, 96);
  if (!strcmp(src, "Trakt")) return RGB(237, 34, 36);
  return ACC; }
/* the source's mark, h high, its left edge at x (its right with rtl); the width taken */
float revpanel_badge(float x, float y, float h, const char *src, float k, int rtl) {
  float w = h, bx; uint32_t c = verdict_col(src, k);
  if (!strcmp(src, "IMDb")) { float px = h * 0.5f; w = text_width("IMDb", px, 900, -0.4f) + h * 0.5f; bx = rtl ? x - w : x;
    gfx_rrect(bx, y, w, h, h * 0.18f, c); text_draw("IMDb", bx + w / 2, y + (h - text_line_height(px)) / 2, px, 900, RGB(0, 0, 0), -0.4f, 0, ALIGN_CENTER); return w; }
  if (!strcmp(src, "TMDB")) { float px = h * 0.46f; w = text_width("TMDB", px, 900, 0.2f) + h * 0.6f; bx = rtl ? x - w : x;
    gfx_rrect_h(bx, y, w, h, h / 2, RGB(144, 206, 161), RGB(1, 180, 228)); text_draw("TMDB", bx + w / 2, y + (h - text_line_height(px)) / 2, px, 900, RGB(13, 37, 63), 0.2f, 0, ALIGN_CENTER); return w; }
  bx = rtl ? x - w : x; float cx = bx + w / 2, cy = y + h / 2, r = h / 2;
  if (!strcmp(src, "Rotten Tomatoes")) { /* a tomato with its leaves, or the green splat */
    if (k >= 0.6f) { gfx_circle(cx, cy + r * 0.08f, r * 0.9f, c); gfx_circle(cx - r * 0.3f, cy - r * 0.08f, r * 0.28f, WHITE_A(60));
      gfx_circle(cx - r * 0.22f, cy - r * 0.72f, r * 0.24f, RGB(0, 145, 45)); gfx_circle(cx + r * 0.22f, cy - r * 0.72f, r * 0.24f, RGB(0, 145, 45)); }
    else { gfx_circle(cx, cy, r * 0.62f, c); for (int i = 0; i < 6; i++) { float a = i * 1.047f + 0.3f; gfx_circle(cx + cosf(a) * r * 0.62f, cy + sinf(a) * r * 0.62f, r * 0.3f, c); } }
    return w; }
  if (!strcmp(src, "Metacritic")) { gfx_circle(cx, cy, r, RGB(10, 10, 12)); gfx_ring(bx, y, w, h, r, h * 0.08f, RGB(255, 204, 51));
    text_draw("m", cx, cy - text_line_height(h * 0.62f) / 2 - h * 0.04f, h * 0.62f, 900, WHITE_A(255), 0, 0, ALIGN_CENTER); return w; }
  if (!strcmp(src, "Trakt")) { gfx_circle(cx, cy, r, c); gfx_ring(bx + h * 0.2f, y + h * 0.2f, h * 0.6f, h * 0.6f, h * 0.3f, h * 0.08f, WHITE_A(255));
    text_draw("\xe2\x9c\x93", cx, cy - text_line_height(h * 0.36f) / 2, h * 0.36f, 900, WHITE_A(255), 0, 0, ALIGN_CENTER); return w; }
  gfx_circle(cx, cy, r, c); return w;
}
static float badge_w(float h, const char *src) {
  if (!strcmp(src, "IMDb")) return text_width("IMDb", h * 0.5f, 900, -0.4f) + h * 0.5f;
  if (!strcmp(src, "TMDB")) return text_width("TMDB", h * 0.46f, 900, 0.2f) + h * 0.6f;
  return h; }
/* an arc from the top, clockwise, k of the way round: round dots close enough to read as one stroke */
static void arc(float cx, float cy, float r, float th, float k, uint32_t c) {
  if (k <= 0.002f) return; int n = (int)(k * 6.2832f * r / (th * 0.35f)) + 2;
  for (int i = 0; i < n; i++) { float a = -1.5708f + 6.2832f * k * i / (n - 1); gfx_circle(cx + cosf(a) * r, cy + sinf(a) * r, th / 2, c); } }
/* one source: its mark in a ring that fills to its score in the source's colour (g: how far, as it comes in), the
   score under it; d the ring's size, cx its middle */
void revpanel_draw_ring_only(float cx, float y, float d, const char *src, const char *val, float g) {
  float k = share_of(val); k = k < 0 ? 0 : k > 1 ? 1 : k; uint32_t c = verdict_col(src, k); float r = d / 2, th = d * 0.07f, cy = y + r;
  gfx_circle(cx, cy, r - th, WHITE_A(14)); gfx_ring(cx - r, cy - r, d, d, r, th, WHITE_A(30));
  arc(cx, cy, r - th / 2, th, k * g, c);
  float roundMark = strcmp(src, "IMDb") && strcmp(src, "TMDB"), in = d - 2 * th - 6, bh = roundMark ? in * 0.78f : in * 0.42f, bw = badge_w(bh, src); if (bw > in) { bh *= in / bw; bw = badge_w(bh, src); } /* the mark as big as the ring lets it */
  revpanel_badge(cx - bw / 2, cy - bh / 2, bh, src, k, 0);
}
void revpanel_draw_ring(float cx, float y, float d, const char *src, const char *val, float g) {
  revpanel_draw_ring_only(cx, y, d, src, val, g); float k = share_of(val); k = k < 0 ? 0 : k > 1 ? 1 : k; uint32_t c = verdict_col(src, k);
  if (!strcmp(src, "Metacritic")) { float vw = text_width(val, 22, 900, 0) + 16, vh = 30; gfx_rrect(cx - vw / 2, y + d + 8, vw, vh, 8, c); text_draw(val, cx, y + d + 8 + (vh - text_line_height(22)) / 2, 22, 900, RGB(0, 0, 0), 0, 0, ALIGN_CENTER); }
  else text_draw(val, cx, y + d + 8 + (30 - text_line_height(26)) / 2, 26, 800, WHITE_A(255), -0.4f, 0, ALIGN_CENTER);
}

/* the big score, its votes under it, and the spread (10 down to 1, each with its share) beside it */
float revpanel_draw_score(float x, float y, float w, float score, const char *src, int votes, const int *dist, int pending) {
  int rtl = app_rtl(), al = rtl ? ALIGN_RIGHT : ALIGN_LEFT; float sx = rtl ? x + w : x, SW = 200;
  if (score <= 0) { if (pending) { ph_text(sx, y + 6, 180, 1, 48, 56, al); ph_text(sx, y + 70, 300, 1, 20, 26, al); } else text_draw(T("No ratings yet"), sx, y + 10, 24, 400, WHITE_A(150), 0, w, al); return 110; }
  char s[16], vs[64]; snprintf(s, sizeof s, "%.1f", score); float nw = text_draw(s, sx, y - 10, 72, 800, WHITE_A(255), -2.0f, 0, al);
  text_draw("/10", rtl ? sx - nw - 6 : sx + nw + 6, y + 30, 24, 700, WHITE_A(120), 0, 0, al); /* (out of what) */
  float bdw = revpanel_badge(sx, y + 72, 28, src, score / 10, rtl) + 10;
  { char c[16]; compact(votes, c, sizeof c); if (votes > 0) snprintf(vs, sizeof vs, "%s", T1("{n} ratings", "n", c)); else snprintf(vs, sizeof vs, "%s", src); }
  text_draw(vs, rtl ? sx - bdw : sx + bdw, y + 72 + (28 - text_line_height(20)) / 2, 20, 500, WHITE_A(150), 0, SW + 40 - bdw, al);
  int tot = 0; for (int i = 0; dist && i < 10; i++) tot += dist[i];
  if (tot <= 0) return 110;
  float bx0 = x + SW + 40, bw = w - SW - 40 - 34 - 58, rh = 14; /* the label, the bar, its share */
  for (int k = 0; k < 10; k++) { int i = 9 - k; float by = y + 2 + k * rh, p = (float)dist[i] / tot; char lb[4], ps[8]; snprintf(lb, sizeof lb, "%d", i + 1); snprintf(ps, sizeof ps, "%d%%", (int)(100 * p + 0.5f));
    float lx = rtl ? x + w - (bx0 - x) : bx0, bx = rtl ? lx - 34 - bw : lx + 34, px = rtl ? bx - 12 : bx + bw + 12;
    text_draw(lb, rtl ? lx : lx + 26, by + (rh - text_line_height(15)) / 2, 15, 600, WHITE_A(170), 0, 0, ALIGN_RIGHT);
    gfx_rrect(bx, by + 3, bw, 8, 4, WHITE_A(26)); if (p > 0.001f) gfx_rrect(rtl ? bx + bw - bw * p : bx, by + 3, bw * p, 8, 4, col_alpha(verdict_col(src, 0.8f), k < 3 ? 1.0f : 0.55f + 0.06f * (9 - k)));
    text_draw(ps, px, by + (rh - text_line_height(15)) / 2, 15, 600, WHITE_A(170), 0, 0, rtl ? ALIGN_RIGHT : ALIGN_LEFT); }
  return 10 * rh + 8;
}
int revpanel_comment_lines(const RevComment *c, float w, int max) { return text_wrap_count(c->text, w - 32, max, 23, 400, 0); }
float revpanel_comment_h(const RevComment *c, float w, int lines) { (void)c; (void)w; return 16 + 48 + (lines > 0 ? lines : 1) * CM_LH + 10 + text_line_height(20) + 16; }
/* one comment: the avatar's initial, the name, when, the score; the words; likes and replies */
float revpanel_draw_comment(float x, float y, float w, const RevComment *c, int lit, int lines, float when) {
  int rtl = app_rtl(), al = rtl ? ALIGN_RIGHT : ALIGN_LEFT; float h = revpanel_comment_h(c, w, lines), tx = rtl ? x + w - 16 : x + 16, ax = rtl ? x + w - 16 - 40 : x + 16;
  uint32_t fg = lit ? ACC_FG : WHITE_A(255), dim = lit ? col_alpha(ACC_FG, 0.75f) : WHITE_A(150), body = lit ? col_alpha(ACC_FG, 0.92f) : WHITE_A(212);
  if (!lit) { gfx_rrect(x, y, w, h, 18, WHITE_A(14)); gfx_ring(x, y, w, h, 18, 1, WHITE_A(18)); }
  gfx_circle(ax + 20, y + 16 + 20, 20, lit ? col_alpha(ACC_FG, 0.22f) : acc_a(0.55f));
  { char ini[8] = { 0 }; const unsigned char *u = (const unsigned char *)c->user; int n = *u < 0x80 ? 1 : (*u & 0xe0) == 0xc0 ? 2 : (*u & 0xf0) == 0xe0 ? 3 : 4; memcpy(ini, c->user, (size_t)n); text_upper(ini, sizeof ini);
    text_draw(ini, ax + 20, y + 16 + 20 - text_line_height(20) / 2.0f, 20, 700, fg, 0, 0, ALIGN_CENTER); }
  char sc[12] = ""; float sw = 0; if (c->score > 0) { snprintf(sc, sizeof sc, "%d/10", c->score); sw = text_width(sc, 20, 700, 0) + 22 + 12; }
  float nx = rtl ? tx - 52 : tx + 52, ny = y + 16 + 20 - text_line_height(24) / 2.0f, nw = text_draw(c->user, nx, ny, 24, 700, fg, 0, (w - 84 - sw) * 0.6f, al);
  if (when > 0) { char ws[48]; ago(c->days, ws, sizeof ws); if (ws[0]) { char l[64]; snprintf(l, sizeof l, "\xc2\xb7  %s", ws); float room = w - 84 - sw - nw - 12; if (text_width(l, 21, 500, 0) <= room) { gfx_opacity_push(when); text_draw(l, rtl ? nx - nw - 12 : nx + nw + 12, y + 16 + 20 - text_line_height(21) / 2.0f, 21, 500, dim, 0, 0, al); gfx_opacity_pop(); } } } /* (left out where the score leaves no room) */
  if (sc[0]) { sw -= 12; float sx = rtl ? x + 16 : x + w - 16 - sw;
    gfx_rrect(sx, y + 16 + 6, sw, 28, 14, lit ? col_alpha(ACC_FG, 0.18f) : acc_a(0.22f)); text_draw(sc, sx + sw / 2, y + 16 + 20 - text_line_height(20) / 2.0f, 20, 700, lit ? ACC_FG : ACC2, 0, 0, ALIGN_CENTER); }
  float by = y + 16 + 48;
  if (c->spoiler && lines == 0) { text_draw(T("Spoiler  \xc2\xb7  OK shows it"), tx, by + (CM_LH - text_line_height(23)) / 2, 23, 500, dim, 0, w - 32, al); lines = 1; }
  else text_draw_wrapped(c->text, tx, by + (CM_LH - text_line_height(23)) / 2, w - 32, lines, 23, 400, body, 0, al, CM_LH);
  { char f[96] = ""; char n[16]; if (c->likes > 0) { snprintf(n, sizeof n, "%d", c->likes); snprintf(f, sizeof f, "%s", c->likes == 1 ? T("1 like") : T1("{n} likes", "n", n)); }
    if (c->replies > 0) { snprintf(n, sizeof n, "%d", c->replies); size_t l = strlen(f); snprintf(f + l, sizeof f - l, "%s%s", l ? "  \xc2\xb7  " : "", c->replies == 1 ? T("1 reply") : T1("{n} replies", "n", n)); }
    float fy = by + lines * CM_LH + 10, bh = text_line_height(20) - 4, bw = badge_w(bh, c->from); revpanel_badge(tx, fy + 2, bh, c->from, 0.8f, rtl); /* where it was written */
    text_draw(f[0] ? f : c->from, rtl ? tx - bw - 10 : tx + bw + 10, fy, 20, 500, dim, 0, w - 42 - bw, al); }
  return h;
}
static void draw_left(float x, float top, int rtl) {
  int al = rtl ? ALIGN_RIGHT : ALIGN_LEFT; float tx = rtl ? x + LW : x, y = top;
  /* the still (an episode) or the backdrop, the poster when there is none */
  { GLuint t = 0; float u0 = 0, v0 = 0, u1 = 1, v1 = 1, rad = 0;
    if (R.still[0] && is_ep()) { t = img_get_shaped(R.still, 589, 331, 18, 1); } /* the episode list's own cut of the still */
    else if (R.still[0]) { float asp; t = img_get(R.still, 1280, 720, &asp); if (t) { img_cover_uv(asp, LW / STILL_H, 0, &u0, &v0, &u1, &v1); rad = 18; } } /* the page's backdrop, as decoded for it */
    float sa = ap(AP_STILL, t != 0); if (sa < 1) ph_surface(x, y, LW, STILL_H, 18); if (t) gfx_image(t, x, y, LW, STILL_H, rad, WHITE_A((int)(255 * sa)), u0, v0, u1, v1); /* (the picture fades in over the waiting look) */
    if (t) gfx_rrect_v(x, y + STILL_H * 0.6f, LW, STILL_H * 0.4f, 0, BLACK_A(0), BLACK_A((int)(110 * sa))); }
  /* a season: its own poster on the picture, crossfading as the season changes */
  { float pa = 0; GLuint pt = 0; if (R.spost[0]) { pt = img_get_shaped(R.spost, 170, 255, 12, 1); pa = ap_sig(AP_SPOST, pt != 0, hsh(R.spost)); } else ap(AP_SPOST, 0);
    if (pt && pa > 0) { float pw = 170, ph2 = 255, px = rtl ? x + 20 : x + LW - 20 - pw, py = y + STILL_H - 20 - ph2 + 10 * (1 - pa);
      gfx_opacity_push(pa); gfx_shadow(px, py + 10, pw, ph2, 12, 30, BLACK_A(170)); gfx_image(pt, px, py, pw, ph2, 0, WHITE_A(255), 0, 0, 1, 1); gfx_ring(px, py, pw, ph2, 12, 1, WHITE_A(40)); gfx_opacity_pop(); } }
  /* the tag, or the show / season switch */
  { float cx = rtl ? x + LW - 16 : x + 16, cy = y + 16;
    if (R.stackSeason) { /* the series, then each season: a row that scrolls to keep the chosen one in view */
      float ws[MAXSW], xs[MAXSW], tot = 0, vw = LW - 32; for (int i = 0; i <= R.ns; i++) { ws[i] = text_width(R.chip[i], 22, 700, 0.3f) + 36; xs[i] = tot; tot += ws[i] + 10; } tot -= 10;
      float want = R.swX.to; if (xs[R.sel] - want < 0) want = xs[R.sel]; if (xs[R.sel] + ws[R.sel] - want > vw) want = xs[R.sel] + ws[R.sel] - vw; if (want > tot - vw) want = tot - vw; if (want < 0) want = 0; qt_to(&R.swX, want, 0.3f);
      gfx_clip_push(x + 8, cy - 12, LW - 16, 14 + text_line_height(22) + 30);
      float chh = 14 + text_line_height(22); fpill_to(&R.spill, xs[R.sel], 0, ws[R.sel], chh, GLIDE_DUR); fpill_tick(&R.spill, app_dt()); /* the chosen one's highlight glides along */
      for (int i = 0; i <= R.ns; i++) { float ox = xs[i] - R.swX.v; if (ox + ws[i] < -20 || ox > vw + 20) continue; float bx = rtl ? cx - ox : cx + ox, bl = rtl ? bx - ws[i] : bx;
        hit_add(bl, cy, ws[i], chh, NULL, sw_act, i, 0); gfx_shadow(bl, cy + 4, ws[i], chh, chh / 2, 18, BLACK_A(110)); gfx_rrect(bl, cy, ws[i], chh, chh / 2, RGBA(14, 12, 20, 178)); gfx_ring(bl, cy, ws[i], chh, chh / 2, 1, WHITE_A(46)); }
      { float f = R.zone == 2 ? 1 : 0.55f, ox = R.spill.x.v - R.swX.v, pw = R.spill.w.v, bl = rtl ? cx - ox - pw : cx + ox, nx = R.zone == 2 ? nudge_x(&R.nudge) : 0; /* bright with the focus, softer without */
        gfx_opacity_push(f * R.spill.a.v); gfx_rrect(bl + nx, cy, pw, chh, chh / 2, ACC | 0xff000000u); if (R.zone == 2) gfx_ring_out(bl + nx, cy, pw, chh, chh / 2, 4, acc_a(0.34f)); gfx_opacity_pop(); }
      for (int i = 0; i <= R.ns; i++) { float ox = xs[i] - R.swX.v; if (ox + ws[i] < -20 || ox > vw + 20) continue; float bx = rtl ? cx - ox : cx + ox, bl = rtl ? bx - ws[i] : bx;
        float on = 1 - fminf(1, fabsf(R.spill.x.v - xs[i]) / (ws[i] + 10)); text_draw(R.chip[i], bl + ws[i] / 2, cy + 7, 22, 700, col_mix(WHITE_A(235), ACC_FG | 0xff000000u, on * (R.zone == 2 ? 1 : 0.7f)), 0.3f, 0, ALIGN_CENTER); }
      gfx_clip_pop(); }
    else chip(cx, cy, R.tag, 0, 0, rtl); }
  y += STILL_H + 20;
  if (R.logoOn) { float asp; GLuint t = img_get(R.logo, 520, 175, &asp); /* the title page's logo (the same decoded copy) */
    if (t) { float w = LOGO_W, h = w / asp; if (h > LOGO_H) { h = LOGO_H; w = h * asp; } float la = ap(AP_LOGO, 1); gfx_image(t, rtl ? x + LW - w : x, y + (LOGO_H - h) + 8 * (1 - la), w, h, 0, WHITE_A((int)(255 * la)), 0, 0, 1, 1); }
    y += LOGO_H + 14; }
  else { text_draw_wrapped(R.title, tx, y + (40 - text_line_height(34)) / 2, LW, 2, 34, 700, WHITE_A(255), -0.3f, al, 40); y += R.ttlN * 40; }
  { float ia = ap_sig(AP_INFO, R.info[0] != 0, hsh(R.info)); if (ia > 0) { gfx_opacity_push(ia); text_draw(R.info, tx, y, 24, 600, WHITE_A(184), 0, LW, al); gfx_opacity_pop(); } } y += text_line_height(24) + 14;
  if (R.synN) { float ya = ap_sig(AP_SYN, 1, hsh(R.syn)); gfx_opacity_push(ya); gfx_offset_push(0, 10 * (1 - ya)); text_draw_wrapped(R.syn, tx, y + (SYN_LH - text_line_height(25)) / 2, LW, R.synMax, 25, 400, WHITE_A(204), 0, al, SYN_LH); gfx_offset_pop(); gfx_opacity_pop(); y += R.synN * SYN_LH + 22; }
  else if (R.rv->pending && !meta()) { ph_text(tx, y + 4, LW, 3, 25, SYN_LH, al); y += 3 * SYN_LH + 22; }
  if (!R.synN) ap(AP_SYN, 0);
  /* what other sources say: one tile each, those still on their way as the waiting look */
  /* every source's score under the synopsis: its mark in a ring filled to the score, the score, its name */
  y = top + STILL_H + 20 + (R.logoOn ? LOGO_H + 14 : R.ttlN * 40) + text_line_height(24) + 14 + R.synMax * SYN_LH + 22; /* (always in the same place) */
  int ph = R.rv->pending && R.nShown < 3, n = R.nShown + ph; if (!n) return;
  double now = app_now(); { int k = 0; for (int i = 0; i < R.nShown && i < 6; i++) if (strcmp(R.tileV[i], R.tiles[i]->val) || i >= R.tileN) { snprintf(R.tileV[i], sizeof R.tileV[i], "%s", R.tiles[i]->val); R.tileAt[i] = fmax(now, R.openAt + 0.3) + 0.08 * k++; } if (R.nShown > R.tileN) R.tileN = R.nShown; } /* (a ring comes in, or fills again, only when its score is new) */
  float cw = LW / (n < 5 ? 5 : n);
  for (int i = 0; i < n; i++) { float cx = rtl ? x + LW - cw * (i + 0.5f) : x + cw * (i + 0.5f);
    if (i >= R.nShown) { ph_circle(cx, y + RING_D / 2, RING_D / 2); continue; }
    float u = (float)((now - R.tileAt[i]) / 0.8); u = u < 0 ? 0 : u > 1 ? 1 : u; float a = u * 3 > 1 ? 1 : u * 3; const RevTile *t = R.tiles[i];
    gfx_opacity_push(a); gfx_offset_push(0, 10 * (1 - quint(a))); revpanel_draw_ring_only(cx, y, RING_D, t->src, t->val, quint(u));
    text_draw(t->val, cx, y + RING_D + 6, 28, 800, WHITE_A(255), -0.4f, cw - 8, ALIGN_CENTER);
    { float np = text_width(t->src, 18, 600, 0) > cw - 6 ? 15 : 18; text_draw(t->src, cx, y + RING_D + 6 + text_line_height(28), np, 600, WHITE_A(175), 0, cw - 4, ALIGN_CENTER); } /* (a long name a size down, whole) */
    gfx_offset_pop(); gfx_opacity_pop(); }
}
static void draw_right(float x, float top, float bottom, int rtl) {
  int al = rtl ? ALIGN_RIGHT : ALIGN_LEFT; float tx = rtl ? x + RW : x, y = top, nx = R.zone == 0 ? nudge_x(&R.nudge) : 0, ny = R.zone == 0 ? nudge_y(&R.nudge) : 0;
  /* the options (in the page: the title page's own buttons, above) */
  if (!R.page) {
  if (R.pill.a.v > 0.001f) { float px = rtl ? x + RW - R.pill.x.v - R.pill.w.v : x + R.pill.x.v; gfx_offset_push(px - R.pill.x.v + nx, y + ny); fpill_draw(&R.pill, -1, 0.96f); gfx_offset_pop(); }
  for (int i = 0; i < R.nopt; i++) { const Opt *o = &R.opt[i]; float bx = rtl ? x + RW - o->x.v - BD : x + o->x.v, fo = o->f ? nx : 0, fy = o->f ? ny : 0, hw = o->f ? BD + R.optW[i] + 26 : BD;
    hit_add(rtl ? bx + BD - hw : bx, y, hw, BD, opt_focus, opt_act, i, 0);
    if (!o->f) gfx_circle(bx + BD / 2, y + BD / 2, BD / 2, o->kind == O_WATCHED && ep_watched() ? acc_a(0.4f) : o->kind == O_LIB && lib_has(R.it.id) ? acc_a(0.4f) : WHITE_A(46));
    uint32_t fg = o->f ? ACC_FG : WHITE_A(255); icon_draw(R.optIcon[i], (rtl ? bx + BD - 18 - 28 : bx + 18) + fo, y + 18 + fy, 28, fg);
    if (o->la.v > 0.001f) { gfx_opacity_push(o->la.v); text_draw(R.optLbl[i], (rtl ? bx + BD - 56 : bx + 56) + fo, y + (BD - text_line_height(24)) / 2 + fy, 24, 600, fg, 0, 0, al); gfx_opacity_pop(); } }
  y += BD + 30; }
  /* the score and its spread */
  text_draw(T("Ratings and reviews"), tx, y, 22, 600, WHITE_A(160), 0, RW, al); y += text_line_height(22) + 12;
  { float sa = ap_sig(AP_SCORE, R.rv->score > 0, (unsigned)(R.rv->score * 100) ^ ((unsigned)R.rv->votes << 9) ^ hsh(R.rv->scoreSrc)), sh = 110; /* the score: its waiting look gives way to it */
    if (sa < 1 && (R.rv->pending || R.rv->score > 0)) { gfx_opacity_push(1 - sa); sh = revpanel_draw_score(x, y, RW, 0, "", 0, NULL, 1); gfx_opacity_pop(); }
    else if (sa <= 0) sh = revpanel_draw_score(x, y, RW, 0, "", 0, NULL, 0);
    if (sa > 0) { gfx_opacity_push(sa); gfx_offset_push(0, 10 * (1 - sa)); sh = revpanel_draw_score(x, y, RW, R.rv->score, R.rv->scoreSrc, R.rv->votes, R.rv->hasDist ? R.rv->dist : NULL, 0); gfx_offset_pop(); gfx_opacity_pop(); }
    y += sh + 22; }
  /* the comments */
  float ta = ap_sig(AP_TABS, R.rv->nc > 0, (unsigned)R.sel); if (R.rv->nc) gfx_opacity_push(ta);
  if (R.rv->nc) { float cx = rtl ? x + RW : x, th = 14 + text_line_height(22); /* the tabs: All, then each source, with how many */
    if (R.tpill.a.v > 0.001f) { gfx_offset_push(x, y); fpill_halo(&R.tpill, -1, 0.97f, acc_a(0.34f)); fpill_draw(&R.tpill, -1, 0.97f); gfx_offset_pop(); }
    for (int t = 0; t < R.ntab; t++) { char lb[48], n[12]; snprintf(n, sizeof n, "%d", R.tabN[t]); snprintf(lb, sizeof lb, "%s  %s", t ? R.tabSrc[t] : T("All"), n);
      float f = R.tbF[t].v, w = text_width(lb, 22, 700, 0.3f) + 36, bx = rtl ? cx - w : cx; R.tabX[t] = bx - x; R.tabW[t] = w; hit_add(bx, y, w, th, NULL, tab_act, t, 0);
      if (f < 0.999f) { gfx_opacity_push(1 - f); gfx_rrect(bx, y, w, th, th / 2, R.tab == t ? acc_a(0.45f) : WHITE_A(16)); gfx_ring(bx, y, w, th, th / 2, 1, WHITE_A(R.tab == t ? 60 : 30)); gfx_opacity_pop(); }
      text_draw(lb, bx + w / 2, y + 7, 22, 700, col_mix(WHITE_A(R.tab == t ? 255 : 190), ACC_FG, f), 0.3f, 0, ALIGN_CENTER); cx += (rtl ? -1 : 1) * (w + 10); }
    y += 14 + text_line_height(22) + 16; gfx_opacity_pop(); }
  else { text_draw(T("Comments"), tx, y, 22, 600, WHITE_A(160), 0, RW, al); y += text_line_height(22) + 12; }
  float vh = bottom - y;
  if (!R.rv->nc) { if (R.rv->pending) { for (int k = 0; k < 2; k++) { float cy = y + k * 150; ph_circle(rtl ? x + RW - 36 : x + 36, cy + 36, 20); ph_text(rtl ? x + RW - 68 : x + 68, cy + 22, 220, 1, 24, 30, al); ph_text(tx, cy + 64, RW - 32, 2, 23, CM_LH, al); } }
    else if (trakt_has_key()) text_draw(T("No comments yet"), tx, y + 6, 24, 400, WHITE_A(150), 0, RW, al);
    else { /* no Trakt id yet: a card in the comments' place, OK on it shows the code for the phone */
      int f = R.zone == 1; float h = 32 + text_line_height(26) + 6 + 2 * 28, kx = rtl ? x + RW - 24 - 36 : x + 24, ktx = rtl ? x + RW - 24 - 36 - 20 : x + 24 + 36 + 20; hit_add(x, y, RW, h, cm_focus, cm_act, 0, 0);
      if (R.cpill.a.v > 0.001f) { gfx_offset_push(x + (f ? nudge_x(&R.nudge) : 0), y + (f ? nudge_y(&R.nudge) : 0)); fpill_halo(&R.cpill, 18, 0.985f, acc_a(0.34f)); fpill_draw(&R.cpill, 18, 0.985f); gfx_offset_pop(); }
      if (f) gfx_offset_push(nudge_x(&R.nudge), nudge_y(&R.nudge)); else { gfx_rrect(x, y, RW, h, 18, WHITE_A(14)); gfx_ring(x, y, RW, h, 18, 1, WHITE_A(18)); }
      icon_draw("reviews", kx, y + 16 + 2, 36, f ? ACC_FG : ACC2);
      text_draw(T("People\xe2\x80\x99s comments come with a Trakt id"), ktx, y + 16, 26, 700, f ? ACC_FG : WHITE_A(255), 0, RW - 100, al);
      text_draw_wrapped(T("OK shows a code for your phone; later, Settings \xe2\x80\xba Keys"), ktx, y + 16 + text_line_height(26) + 6, RW - 100, 2, 22, 500, f ? col_alpha(ACC_FG, 0.8f) : WHITE_A(150), 0, al, 28);
      if (f) gfx_offset_pop(); }
    return; }
  /* scrolled so the focused one shows whole */
  if (R.zone == 1 && R.cIdx < R.nvis) { int i = R.cIdx; float want = R.listY.to; if (R.cy[i] + R.ch[i] - want > vh) want = R.cy[i] + R.ch[i] - vh; if (R.cy[i] - want < 0) want = R.cy[i]; if (want > R.listH - vh) want = fmaxf(0, R.listH - vh); qt_to(&R.listY, want, 0.3f); }
  float tk = R.tabAt > 0 ? (float)((app_now() - R.tabAt) / 0.32) : 1; tk = tk > 1 ? 1 : tk; tk = quint(tk); /* (a new tab: its list slides in from its side) */
  float ca = ap_sig(AP_COMM, R.nvis > 0, (unsigned)R.sel * 31u + (unsigned)R.nvis); gfx_clip_push(x - 8, y - 8, RW + 16, vh + 16); gfx_opacity_push((0.2f + 0.8f * tk) * ca); gfx_offset_push(0, 14 * (1 - ca)); gfx_offset_push(48 * (1 - tk) * R.tabDir * (rtl ? -1 : 1), y - R.listY.v);
  float cnx = R.zone == 1 ? nudge_x(&R.nudge) : 0, cny = R.zone == 1 ? nudge_y(&R.nudge) : 0;
  if (R.cpill.a.v > 0.001f) { gfx_offset_push(x + cnx, cny); fpill_halo(&R.cpill, 18, 0.985f, acc_a(0.34f)); fpill_draw(&R.cpill, 18, 0.985f); gfx_offset_pop(); }
  for (int i = 0; i < R.nvis; i++) { float cy = i < R.animN ? R.cyA[i].v : R.cy[i]; if (cy + R.ch[i] < R.listY.v - 20 || cy > R.listY.v + vh + 20) continue; int f = R.zone == 1 && i == R.cIdx;
    hit_add(x, cy, RW, R.ch[i], cm_focus, cm_act, i, 0);
    if (f) gfx_offset_push(cnx, cny); revpanel_draw_comment(x, cy, RW, &R.rv->c[R.vis[i]], f, R.nl[i], 1);
    if (f && R.more[i]) { float hy = cy + 16 + 48 + (R.nl[i] > 0 ? R.nl[i] : 1) * CM_LH + 10 + text_line_height(20) / 2; /* a long one, focused: OK reads it whole (or folds it back) */
      key_hint(rtl ? x + 16 : x + RW - 16, hy, K_OK, R.expanded == i ? T("Less") : T("Read more"), col_alpha(ACC_FG, 0.9f), !rtl); }
    if (f) gfx_offset_pop(); }
  gfx_offset_pop(); gfx_offset_pop(); gfx_opacity_pop(); gfx_clip_pop();
}
static void draw(void) {
  if (!R.open && !R.closing) return;
  float u = R.closing ? 1 - timer_x(&R.out) : R.in.on ? timer_x(&R.in) : 1, e = R.closing ? u * u * (3 - 2 * u) : quint(u); int rtl = app_rtl();
  float fx = R.from[2] > 0 ? R.from[0] : PX + PW / 2 - 60, fy = R.from[2] > 0 ? R.from[1] : PY + PH / 2 - 60, fw = R.from[2] > 0 ? R.from[2] : 120, fh = R.from[2] > 0 ? R.from[3] : 120, fr = R.from[2] > 0 ? R.from[4] : 24;
  float x = fx + (PX - fx) * e, y = fy + (PY - fy) * e, w = fw + (PW - fw) * e, h = fh + (PH - fh) * e, r = fr + (28 - fr) * e, ca = (e - 0.5f) / 0.5f; ca = ca < 0 ? 0 : ca > 1 ? 1 : ca;
  if (R.page) { x = PX; w = PW; h = PH; r = 28; y = PY + (SB_H + 60 - PY) * (1 - e); ca = 1; } /* risen from the page's foot, whole all the way */
  if (!R.closing && !R.page) { hit_act(0, 0, SB_W, SB_H, outside, 0, 0); hit_shield(x, y, w, h); }
  if (R.page) { /* in the page: no box of its own, the reviews rise into the title page under its buttons */
    float lx = rtl ? PX + PW - PAD - LW : PX + PAD, rx = rtl ? PX + PAD : PX + PAD + LW + GAP, top = PG_TOP + 140 * (1 - e);
    gfx_opacity_push(e); draw_left(lx, top, rtl); draw_right(rx, top, SB_H - 40 + 140 * (1 - e), rtl); gfx_opacity_pop(); return; }
  int pill = !R.page && R.from[2] > 0 && R.from[4] >= R.from[3] * 0.45f; /* opened from a round button: it starts as that button and swells into the panel */
  gfx_opacity_push(pill || R.page ? 1 : e < 0.15f ? e / 0.15f : 1);
  if (R.glass.ok && R.glass.a.v >= 0.999f && e >= 0.999f) gfx_shadow_under(x, y + 30, w, h, r, 80, BLACK_A(150), x, y, w, h, r); else gfx_shadow(x, y + 24, w, h, r, 60, BLACK_A((int)(150 * e)));
  sglass_draw(&R.glass, x, y, w, h, r); gfx_rrect(x, y, w, h, r, BLACK_A((int)(60 * (1 - e) * (1 - e)))); if (pill && e < 1) { float k = 1 - e; gfx_rrect(x, y, w, h, r, col_alpha(ACC | 0xff000000u, k * k * (3 - 2 * k))); } /* (the button's accent giving way to the glass) */ gfx_ring(x, y, w, h, r, 1, WHITE_A(22)); /* (the held picture's darkness gives way to the glass) */
  if (R.open && !R.closing && e >= 0.999f && R.glass.ok && R.glass.a.v >= 0.999f) app_occluder(x + 24, y + 24, w - 48, h - 48); /* still and opaque */
  if (ca > 0.004f) { gfx_opacity_push(ca); gfx_clip_push(x, y, w, h); gfx_offset_push(0, 8 * (1 - ca)); /* (inside the box as it grows: nothing past its edges) */
    float lx = rtl ? PX + PW - PAD - LW : PX + PAD, rx = rtl ? PX + PAD : PX + PAD + LW + GAP, top = PY + PAD, bottom = PY + PH - PAD;
    draw_left(lx, top, rtl); draw_right(rx, top, bottom, rtl);
    gfx_offset_pop(); gfx_clip_pop(); gfx_opacity_pop(); }
  gfx_opacity_pop();
}
static Screen panel = { NULL, NULL, key, tick, draw, NULL, NULL, NULL };
/* what the previews show (Settings > Keys, the guide): made-up but true to life */
const RevComment *revpanel_sample(int i) {
  static RevComment c[3]; static unsigned lg = ~0u; if (lg != i18n_gen()) { lg = i18n_gen(); memset(c, 0, sizeof c);
    snprintf(c[2].user, sizeof c[2].user, "leo.m"); snprintf(c[2].from, sizeof c[2].from, "Trakt"); c[2].days = 12; c[2].score = 9; c[2].likes = 5;
    snprintf(c[2].text, sizeof c[2].text, "%s", T("Watched it twice in a week. The music alone is worth it."));
    snprintf(c[0].user, sizeof c[0].user, "marta_k"); snprintf(c[0].from, sizeof c[0].from, "Trakt"); c[0].days = 81; c[0].score = 8; c[0].likes = 12; c[0].replies = 3;
    snprintf(c[0].text, sizeof c[0].text, "%s", T("Sharp, playful and far better paced than it has any right to be. The heist in the middle is a joy."));
    snprintf(c[1].user, sizeof c[1].user, "cinephile_77"); snprintf(c[1].from, sizeof c[1].from, "TMDB"); c[1].days = 204; c[1].score = 7;
    snprintf(c[1].text, sizeof c[1].text, "%s", T("A warm, generous film that trusts its audience. Not every turn lands, but the ending earns its tears.")); }
  return &c[i < 0 || i > 2 ? 0 : i];
}
const int *revpanel_sample_dist(void) { static const int d[10] = { 2, 1, 1, 2, 4, 7, 14, 27, 24, 18 }; return d; }
static int nextList[MAXSW - 1], nextN;
void revpanel_seasons(const int *list, int n) { if (n > MAXSW - 1) n = MAXSW - 1; nextN = n > 0 ? n : 0; if (nextN) memcpy(nextList, list, sizeof(int) * (size_t)nextN); }
void revpanel_show(const Item *it, int stackSeason, float fx, float fy, float fw, float fh, float fr) {
  if (R.open) finish();
  memset(&R, 0, sizeof R); R.it = *it; R.it_ep = *it; R.open = 1; R.expanded = -1; R.openAt = app_now();
  const char *c = strrchr(it->videoId, ':'); /* an episode card: "tt123:1:2" */
  if (it->ep && c) { const char *p = c - 1; while (p > it->videoId && *p != ':') p--; if (*p == ':') { R.season = atoi(p + 1); R.episode = atoi(c + 1); } }
  if (R.episode <= 0) { R.episode = 0; R.season = 0; R.stackSeason = !strcmp(it->type, "series") ? stackSeason : 0; } /* (the switch is the title's) */
  { char *cl = strchr(R.it.id, ':'); if (cl) *cl = 0; } /* the title's own id */
  if (!R.stackSeason || !nextN) R.stackSeason = 0; else { R.ns = nextN; memcpy(R.seasonList, nextList, sizeof(int) * (size_t)nextN); } nextN = 0; /* (the seasons, from the title page) */
  R.rv = reviews_get(R.it.type, R.it.id, R.season, R.episode);
  R.from[0] = fx; R.from[1] = fy; R.from[2] = fw; R.from[3] = fh; R.from[4] = fr;
  timer_start(&R.in, R.page ? 0.42f : 0.32f); fpill_snap(&R.pill); fpill_snap(&R.cpill); qt_set(&R.listY, 0);
  sglass_bake(&R.glass, PX, PY, PW, PH); /* where it settles */
  layout();
  if (app_overlay != &panel) under = app_overlay; app_overlay = &panel;
}
float revpanel_page_k(void) { if (!R.page || (!R.open && !R.closing)) return 0; float u = R.closing ? 1 - timer_x(&R.out) : R.in.on ? timer_x(&R.in) : 1; return R.closing ? u * u * (3 - 2 * u) : quint(u); }
void revpanel_page_close(void) { if (R.page && R.open && !R.closing) close_panel(); }
int revpanel_page_episode(Item *out) { if (!R.page || !R.open || R.closing || !is_ep()) return 0; if (out) *out = R.it_ep; return 1; }
int revpanel_page_btns(void) { return R.page && R.open && !R.closing && R.zone == 0; }
void revpanel_show_page(const Item *it) { revpanel_show(it, nextN > 0, 0, 0, 0, 0, 0); R.page = 1; timer_start(&R.in, 0.42f); }
