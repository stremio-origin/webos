/* Similar: a grid of titles of the same genre. Also a person's films and series (OK on a face on a title page),
   the most notable first, from Wikidata (cast, director, writer, with their IMDb ids; ranked by how many wikis
   cover each). */
#include "screens/similar.h"
#include "core/keys.h"
#include "core/i18n.h"
#include "core/json.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include "ui/widgets.h"
#include "ui/img.h"
#include "screens/sheet.h"
#include <stdio.h>
#include <string.h>
#include "core/hit.h"
#include "core/net.h"
#include "core/coll.h"

#define COLS 6
static struct { DetailParams p; ItemList items; int idx; char title[300], sub[200]; int loading, empty, person, coll, pendTmdb; double pendAt; unsigned collVer; char from[16], ckey[24]; double loadT0; struct { float x, y; int shown; Timer1 pop; } ring; Tween gridY; Timer1 fade; Nudge nudge; unsigned gen; } G;
#define MAXPT 48
typedef struct { char id[16], type[8], name[128]; } PT;
typedef struct { char who[16]; int n; PT t[MAXPT]; } PersonMem;
static PersonMem pmem[8]; static int pmemN;
static void person_show(const PersonMem *m) {
  itemlist_clear(&G.items); G.items.items = calloc(MAXPT, sizeof(Item)); G.items.n = 0;
  for (int i = 0; i < m->n; i++) { Item *it = &G.items.items[G.items.n++]; snprintf(it->id, sizeof it->id, "%s", m->t[i].id); snprintf(it->type, sizeof it->type, "%s", m->t[i].type); snprintf(it->name, sizeof it->name, "%s", m->t[i].name);
    snprintf(it->poster, sizeof it->poster, "https://images.metahub.space/poster/small/%s/img", m->t[i].id); }
  G.loading = 0; G.empty = G.items.n == 0; timer_start(&G.fade, 0.3f); G.idx = 0; G.ring.shown = 0;
}
static void on_person(int st, const unsigned char *body, size_t len, void *ud) {
  (void)len; if ((unsigned)(uintptr_t)ud != G.gen || SCR != SCR_SIM) return;
  cJSON *j = st == 200 ? net_json(body) : NULL; const cJSON *b = js_arr(js_obj(j, "results"), "bindings");
  if (!b) { net_json_free(j); G.loading = 0; G.empty = 1; return; }
  PersonMem *m = &pmem[pmemN++ % 8]; memset(m, 0, sizeof *m); snprintf(m->who, sizeof m->who, "%s", G.p.it.id); const cJSON *r;
  cJSON_ArrayForEach(r, b) { if (m->n >= MAXPT) break; const char *tt = js_str(js_obj(r, "imdb"), "value", ""), *nm = js_str(js_obj(r, "lab"), "value", ""); if (!*tt || !*nm) continue;
    PT *t = &m->t[m->n++]; snprintf(t->id, sizeof t->id, "%s", tt); snprintf(t->name, sizeof t->name, "%s", nm); snprintf(t->type, sizeof t->type, "%s", !strcmp(js_str(js_obj(r, "s"), "value", ""), "1") ? "series" : "movie"); }
  net_json_free(j); person_show(m);
}
static void person_load(void) {
  for (int i = 0; i < 8; i++) if (pmem[i].who[0] && !strcmp(pmem[i].who, G.p.it.id)) { person_show(&pmem[i]); return; } /* kept in memory */
  const char *id = G.p.it.id; if (strncmp(id, "nm", 2)) { G.loading = 0; G.empty = 1; return; } for (const char *q = id + 2; *q; q++) if (*q < '0' || *q > '9') { G.loading = 0; G.empty = 1; return; }
  static const char *FILM = "wd:Q11424 wd:Q24869 wd:Q506240 wd:Q93204 wd:Q202866 wd:Q29168811 wd:Q17123180", *SER = "wd:Q5398426 wd:Q1259759 wd:Q526877 wd:Q581714";
  char q[1600]; snprintf(q, sizeof q, "SELECT ?imdb (SAMPLE(?l) AS ?lab) (MIN(?yy) AS ?yr) (MAX(?ser) AS ?s) (MAX(?sl) AS ?pop) WHERE { ?p wdt:P345 \"%s\" . "
    "{ ?f wdt:P161 ?p } UNION { ?f wdt:P57 ?p } UNION { ?f wdt:P58 ?p } ?f wdt:P345 ?imdb . FILTER(STRSTARTS(?imdb, \"tt\")) "
    "{ ?f wdt:P31 ?t . VALUES ?t { %s } BIND(0 AS ?ser) } UNION { ?f wdt:P31 ?t . VALUES ?t { %s } BIND(1 AS ?ser) } "
    "?f wikibase:sitelinks ?sl . OPTIONAL { ?f wdt:P577 ?d . BIND(YEAR(?d) AS ?yy) } OPTIONAL { ?f rdfs:label ?l . FILTER(LANG(?l) = \"en\") } } GROUP BY ?imdb ORDER BY DESC(?pop) LIMIT 60", id, FILM, SER);
  char u[5200] = "https://query.wikidata.org/sparql?format=json&query="; size_t o = strlen(u);
  for (const char *s = q; *s && o + 4 < sizeof u; s++) { unsigned char c = *s; if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '!' || c == '~' || c == '*' || c == '\'' || c == '(' || c == ')') u[o++] = c; else o += snprintf(u + o, sizeof u - o, "%%%02X", c); }
  u[o] = 0; net_get(u, on_person, (void *)(uintptr_t)G.gen);
}
static void on_cat(ItemList *l, int cached, void *ud) {
  (void)cached; if ((unsigned)(uintptr_t)ud != G.gen || SCR != SCR_SIM) { itemlist_free(l); return; }
  itemlist_clear(&G.items); G.items.items = calloc(30, sizeof(Item)); G.items.n = 0;
  for (int i = 0; i < l->n && G.items.n < 30; i++) { if (!strcmp(l->items[i].id, G.p.it.id)) continue; G.items.items[G.items.n++] = l->items[i]; }
  itemlist_free(l); G.loading = 0; G.empty = G.items.n == 0; timer_start(&G.fade, 0.3f); G.idx = 0; G.ring.shown = 0;
}
static void on_meta(cJSON *m, void *ud) {
  if ((unsigned)(uintptr_t)ud != G.gen || SCR != SCR_SIM) return;
  const char *nm = js_str(m, "name", G.p.it.name); if (*nm) snprintf(G.title, sizeof G.title, "%s", T1("Similar to {name}", "name", nm));
  const cJSON *gs = js_arr(m, "_genresEn"); if (!gs || !cJSON_GetArraySize(gs)) gs = js_arr(m, "genres"); /* Cinemeta's English genre, also when the page shows it translated */
  const char *g = gs && cJSON_GetArraySize(gs) && cJSON_IsString(cJSON_GetArrayItem(gs, 0)) ? cJSON_GetArrayItem(gs, 0)->valuestring : "";
  for (const char *q = g; *q; q++) if ((unsigned char)*q >= 0x80) { g = ""; break; } /* a translated name: the top titles instead */
  char u[512], ge[128] = ""; if (*g) { const char *s = g; char *d = ge; while (*s && d < ge + 120) { if ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '-') *d++ = *s; else { snprintf(d, 4, "%%%02X", (unsigned char)*s); d += 3; } s++; } *d = 0; }
  if (ge[0]) snprintf(u, sizeof u, CINEMETA "/catalog/%s/top/genre=%s.json", G.p.it.type, ge); else snprintf(u, sizeof u, CINEMETA "/catalog/%s/top.json", G.p.it.type);
  sb_catalog_url(u, G.p.it.type, 31, on_cat, (void *)(uintptr_t)G.gen);
}
/* a collection (from a film's page or the Library's Collections): its films in the order they came out, the one it
   was opened from marked, the focus on the first not watched yet, over the collection's own backdrop */
static void coll_show(void) {
  if (!G.coll || G.collVer == coll_version()) return; G.collVer = coll_version(); int was = !G.loading; /* (shown already: its films' ids filled in place, the focus kept) */
  const Coll *c = coll_get(G.ckey); if (!c) { if (app_now() - G.loadT0 > 30) { G.loading = 0; G.empty = 1; } return; }
  itemlist_clear(&G.items); G.items.items = calloc((size_t)(c->n ? c->n : 1), sizeof(Item)); G.items.n = 0; int first = -1;
  for (int i = 0; i < c->n; i++) { Item *it = &G.items.items[G.items.n++]; if (c->part[i].id[0]) snprintf(it->id, sizeof it->id, "%s", c->part[i].id); else snprintf(it->id, sizeof it->id, "tmdb:%d", c->part[i].tmdb); /* (its IMDb id on its way) */ snprintf(it->type, sizeof it->type, "%s", c->part[i].type[0] ? c->part[i].type : "movie"); snprintf(it->name, sizeof it->name, "%s", c->part[i].name); snprintf(it->poster, sizeof it->poster, "%s", c->part[i].poster);
    if (c->part[i].year) snprintf(it->metaLine, sizeof it->metaLine, "%d", c->part[i].year); if (first < 0 && !lib_item_watched(it)) first = i; }
  if (c->name[0]) snprintf(G.title, sizeof G.title, "%s", c->name);
  { char n[16]; snprintf(n, sizeof n, "%d", c->n); int y0 = c->n ? c->part[0].year : 0, y1 = c->n ? c->part[c->n - 1].year : 0; char yr[24] = ""; if (y0 && y1 && y1 != y0) snprintf(yr, sizeof yr, "  ·  %d\xe2\x80\x93%d", y0, y1); else if (y0) snprintf(yr, sizeof yr, "  ·  %d", y0);
    int nf = 0; for (int i = 0; i < c->n; i++) if (strcmp(c->part[i].type, "series")) nf++; /* (films alone: "8 films"; with shows: "6 titles") */
    snprintf(G.sub, sizeof G.sub, "%s%s", nf == c->n ? T1("{n} films", "n", n) : T1("{n} titles", "n", n), yr); }
  if (was) { if (G.idx >= G.items.n) G.idx = G.items.n ? G.items.n - 1 : 0; return; }
  G.loading = 0; G.empty = G.items.n == 0; timer_start(&G.fade, 0.3f); G.idx = first >= 0 ? first : 0; G.ring.shown = 0;
  if (c->backdrop[0]) bg_set(c->backdrop, 0, c->poster);
}
static void enter(const void *pp) {
  if (pp) G.p = *(const DetailParams *)pp; G.gen++; itemlist_clear(&G.items); G.idx = 0; G.loading = 1; G.loadT0 = app_now(); G.empty = 0; G.ring.shown = 0; tween_set(&G.gridY, 0);
  G.person = !strcmp(G.p.it.type, "person"); G.coll = !strcmp(G.p.it.type, "collection"); G.sub[0] = 0;
  if (G.coll) { snprintf(G.ckey, sizeof G.ckey, "%s", G.p.it.id + 5); G.loadT0 = app_now(); G.collVer = 0; snprintf(G.from, sizeof G.from, "%s", G.p.it.videoId); snprintf(G.title, sizeof G.title, "%s", G.p.it.name[0] ? G.p.it.name : T("Collection")); coll_show(); return; }

  if (G.person) { snprintf(G.title, sizeof G.title, "%s", G.p.it.name); person_load(); return; }
  snprintf(G.title, sizeof G.title, "%s", T("Similar"));
  sb_meta(G.p.it.type, G.p.it.id, on_meta, (void *)(uintptr_t)G.gen);
}
static const Item *focused(void) { return G.idx < G.items.n ? &G.items.items[G.idx] : NULL; }
static void open_it(const Item *it) { if (!strncmp(it->id, "tmdb:", 5)) { G.pendTmdb = atoi(it->id + 5); G.pendAt = app_now(); return; } /* (its IMDb id on its way: opened when it comes, see tick) */
  DetailParams p; memset(&p, 0, sizeof p); p.it = *it; app_go(SCR_DETAIL, &p, 0); }
static int key(int k) {
  if (!G.items.n) { if (k == K_UP) topnav_focus(); return k != K_BACK; } int i0 = G.idx;
  if (k == K_RIGHT) G.idx = G.idx + 1 < G.items.n ? G.idx + 1 : G.idx;
  else if (k == K_LEFT) { if (G.idx > 0) G.idx--; }
  else if (k == K_DOWN) G.idx = G.idx + COLS < G.items.n ? G.idx + COLS : (G.items.n - 1 > G.idx && G.idx / COLS < (G.items.n - 1) / COLS ? G.items.n - 1 : G.idx);
  else if (k == K_UP) { if (G.idx - COLS >= 0) G.idx -= COLS; else { topnav_focus(); return 1; } }
  else if (k == K_OK) { open_it(&G.items.items[G.idx]); return 1; }
  else if (k == K_BACK) return 0;
  if (G.idx == i0) nudge_start(&G.nudge, k);
  return 1;
}
#define ROWH (CARD_H + 26.0f)
/* the text art of the titles around the focus, for their pages (drawn there at 520x175): ready when one opens */
static void logos_ahead(void) { static double at0; double now = app_now(); if (now - at0 < 0.5) return; at0 = now;
  for (int i = G.idx - COLS; i <= G.idx + COLS; i++) { if (i < 0 || i >= G.items.n) continue; char lu[512]; sb_logo_of(&G.items.items[i], lu, sizeof lu); if (lu[0]) img_prefetch_pri(lu, 520, 175); } }
static void tick(float dt) {
  coll_show(); logos_ahead();
  if (G.pendTmdb) { if (app_now() - G.pendAt > 15) G.pendTmdb = 0; else for (int i = 0; i < G.items.n; i++) { const Coll *c = coll_get(G.ckey); if (!c) break; for (int k = 0; k < c->n; k++) if (c->part[k].tmdb == G.pendTmdb && c->part[k].id[0]) { Item it = G.items.items[i]; for (int q = 0; q < G.items.n; q++) if (!strcmp(G.items.items[q].id, c->part[k].id)) it = G.items.items[q]; G.pendTmdb = 0; open_it(&it); return; } break; } } /* (a film picked before its id came) */
  timer_tick(&G.ring.pop, dt); tween_tick(&G.gridY, dt); timer_tick(&G.fade, dt); nudge_tick(&G.nudge, dt);
  int row = G.idx / COLS; float y = row > 1 ? (row - 1) * ROWH : 0; tween_to(&G.gridY, -y, 0.3f, EASE_OUT);
  /* the ring jumps to the card and lands with a small pop (never glides across the others) */
  if (G.items.n) { int rtl = app_rtl(); float cx = 48 + 14 + (G.idx % COLS) * (CARD_W + CARD_GAP), cy = 16 + row * ROWH; if (rtl) cx = SB_W - cx - CARD_W;
    if (!G.ring.shown || G.ring.x != cx || G.ring.y != cy) { static double last; int pop = !G.ring.shown || focus_pops(&last); G.ring.x = cx; G.ring.y = cy; G.ring.shown = 1; if (pop) timer_start(&G.ring.pop, 0.2f); else G.ring.pop.on = 0; } } /* held: it jumps, lit */
}
static int origin(float *x, float *y) { if (topNav.active || G.idx >= G.items.n) return 0; int row = G.idx / COLS; float cx = 48 + 14 + (G.idx % COLS) * (CARD_W + CARD_GAP); if (app_rtl()) cx = SB_W - cx - CARD_W; *x = cx + CARD_W / 2; *y = 120 + 104 + G.gridY.v + 16 + row * ROWH + CARD_H / 2; return 1; }
static void sim_focus(int i, int b) { (void)b; int was = topNav.active; topNav.active = 0; if (was || G.idx != i) G.idx = i; }
static void draw(void) {
  int rtl = app_rtl();
  text_draw(G.title, rtl ? SB_W - 48 : 48, 120, 44, 800, WHITE_A(255), -0.8f, SB_W - 96, rtl ? ALIGN_RIGHT : ALIGN_LEFT);
  if (G.sub[0]) text_draw(G.sub, rtl ? SB_W - 48 : 48, 120 + 58, 24, 600, WHITE_A(180), 0, SB_W - 96, rtl ? ALIGN_RIGHT : ALIGN_LEFT); /* (a collection: how many films, which years) */
  float top = 120 + 104; gfx_clip_push(34, top - 28, SB_W - 34, SB_H - top + 28); /* (from 28 above the grid: the first line's focused card grows 9% and has its outline; cut at the grid's top, its top edge was lost) */
  float fa = G.fade.on ? 1 - powf(1 - timer_x(&G.fade), 2) : 1; gfx_opacity_push(fa);
  gfx_offset_push(nudge_x(&G.nudge), nudge_y(&G.nudge) + top + G.gridY.v);
  if (G.loading) { /* loading: a thin line under the title, a short accent stroke sweeping along it */
    double t = app_now() - G.loadT0; float in = (float)((t - 0.15) / 0.3); in = in < 0 ? 0 : in > 1 ? 1 : in;
    if (in > 0) { float W = 360, x0 = rtl ? SB_W - 48 - 40 - W : 48 + 40, ph = (float)fmod(t, 1.4) / 1.4f, e = ph * ph * (3 - 2 * ph), sw = W * 0.32f, sx = x0 - sw + (W + sw) * e;
      gfx_opacity_push(in); gfx_rrect(x0, 52, W, 3, 1.5f, WHITE_A(22));
      float l = sx < x0 ? x0 : sx, r = sx + sw > x0 + W ? x0 + W : sx + sw; if (r - l > 1) gfx_rrect(l, 52, r - l, 3, 1.5f, ACC); gfx_opacity_pop(); } } /* the stroke enters and leaves at the line's ends */
  if (G.empty && !G.loading) text_draw(T("Nothing found."), rtl ? SB_W - 48 - 40 : 48 + 40, 40, 27, 400, WHITE_A(191), 0, 0,
 rtl ? ALIGN_RIGHT : ALIGN_LEFT);
  /* the cards; the focused one last, over its neighbours as it grows */
  int fi = !topNav.active && G.idx < G.items.n ? G.idx : -1; float hp = app_hold_progress();
  for (int pass = 0; pass < 2; pass++) for (int i = 0; i < G.items.n; i++) { if ((i == fi) != pass) continue;
    int row = i / COLS; float y = 16 + row * ROWH; if (y + G.gridY.v > SB_H - top || y + CARD_H + G.gridY.v < 0) continue;
    float x = 48 + 14 + (i % COLS) * (CARD_W + CARD_GAP); if (rtl) x = SB_W - x - CARD_W; unsigned long long lk = lift_key(&G, i);
    lift_begin(lk, pass, pass ? hp : 0, x, y, CARD_W, CARD_H, CARD_R);
    card_draw(&G.items.items[i], x, y, 0, 1, abs(row - G.idx / COLS) <= 2);
    if (G.coll && G.from[0] && !strcmp(G.items.items[i].id, G.from)) { /* the one it was opened from: a quiet glass tag in its corner (the app's pill: dark glass, a fine edge, an accent dot) */
      const char *tf = T(strcmp(G.items.items[i].type, "series") ? "This film" : "This show"); float fs = 18, tw = text_width(tf, fs, 700, 0.2f), h = 34, w = 14 + 8 + 9 + tw + 14, px = rtl ? x + CARD_W - 12 - w : x + 12, py = y + 12;
      gfx_shadow(px, py + 4, w, h, h / 2, 14, BLACK_A(90)); gfx_rrect(px, py, w, h, h / 2, BLACK_A(165)); gfx_ring(px, py, w, h, h / 2, 1.2f, WHITE_A(44));
      gfx_circle(px + 14 + 4, py + h / 2, 4, ACC2 | 0xff000000u); text_draw(tf, px + 14 + 8 + 9, py + (h - text_line_height(fs)) / 2, fs, 700, WHITE_A(240), 0.2f, 0, ALIGN_LEFT); }
    if (pass && hp > 0) hold_trace_draw(x, y, CARD_W, CARD_H, CARD_R, ease_apply(EASE_IN_OUT, hp));
    lift_end(lk, x, y, CARD_W, CARD_H, CARD_R); hit_add(x, y, CARD_W, CARD_H, sim_focus, NULL, i, 0); }
  gfx_offset_pop(); gfx_opacity_pop(); gfx_clip_pop();
}
Screen simScreen = { enter, NULL, key, tick, draw, focused, open_it, NULL, origin };
