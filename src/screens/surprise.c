/* Surprise me. A card asks for a mood (and film, series or either); one well-rated title for it is picked
   from TMDB at random, one already watched passed over, and its page opens. A child's profile picks among children's titles. */
#include "screens/surprise.h"
#include "core/app.h"
#include "core/kids.h"
#include "core/tmdb.h"
#include "core/net.h"
#include "core/json.h"
#include "core/i18n.h"
#include "core/keys.h"
#include "core/hit.h"
#include "core/stremio.h"
#include "platform/plat.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include "ui/glass.h"
#include "ui/widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct { const char *name; int movie, tv; } Mood;
static const Mood MOODS[] = { { "Something to laugh at", 35, 35 }, { "Action and adventure", 28, 10759 }, { "Something scary", 27, 9648 }, { "Something moving", 18, 18 },
  { "Something to think about", 878, 10765 }, { "Romance", 10749, 18 }, { "For the whole family", 10751, 10751 }, { "Anything", 0, 0 } };
#define NM ((int)(sizeof MOODS / sizeof MOODS[0]))
static struct { int open, sel, kind, busy, tries; unsigned gen; double at; Item cand[20]; int ncand, tried[20]; Screen *prev; } S; /* kind 0 either, 1 film, 2 series */
#define CW 760.0f
#define RH 62.0f

static void close_card(void) { if (!S.open) return; S.open = 0; if (app_overlay == &surpriseCard) app_overlay = S.prev; }
static void place(char *lang, size_t lc, char *reg, size_t rc) {
  const char *l = plat_ui_locale(); if (!l || !*l) l = "en-US";
  snprintf(lang, lc, "%s", l); char *u = strchr(lang, '_'); if (u) *u = '-';
  const char *d = strchr(lang, '-'); snprintf(reg, rc, "%s", d && strlen(d + 1) == 2 ? d + 1 : "US");
}
static void try_next(void);
static void on_ext(int st, const unsigned char *body, size_t len, void *ud) {
  (void)len; if ((unsigned)(uintptr_t)ud != S.gen) return;
  cJSON *j = st == 200 ? net_json(body) : NULL; const char *imdb = js_str(j, "imdb_id", ""); char id[24]; snprintf(id, sizeof id, "%s", imdb); net_json_free(j);
  if (strncmp(id, "tt", 2)) { try_next(); return; }
  cJSON *lit = lib_get(id); Item *c = &S.cand[S.tried[S.tries - 1]];
  if (lit && !strcmp(c->type, "movie") && lib_movie_watched(lit) && S.tries < 6) { try_next(); return; } /* (a film watched already: another) */
  S.busy = 0; DetailParams p; memset(&p, 0, sizeof p); p.it = *c; snprintf(p.it.id, sizeof p.it.id, "%s", id); close_card(); app_go(SCR_DETAIL, &p, 0);
}
static void try_next(void) {
  if (S.tries >= S.ncand || S.tries >= 8) { S.busy = 0; toast(T("Nothing found."), 2000, NULL); return; }
  int k; do { k = rand() % S.ncand; } while (0); for (int i = 0; i < S.tries; i++) if (S.tried[i] == k) { k = (k + 1) % S.ncand; i = -1; }
  S.tried[S.tries++] = k; Item *c = &S.cand[k]; char path[64]; snprintf(path, sizeof path, "/%s/%d/external_ids", !strcmp(c->type, "series") ? "tv" : "movie", atoi(c->id + 5));
  if (!tmdb_request(path, on_ext, (void *)(uintptr_t)S.gen)) { S.busy = 0; toast(T("Nothing found."), 2000, NULL); }
}
static int tvAsked;
static void on_list(int st, const unsigned char *body, size_t len, void *ud) {
  (void)len; if ((unsigned)(uintptr_t)ud != S.gen) return;
  cJSON *j = st == 200 ? net_json(body) : NULL; const cJSON *res = js_arr(j, "results"), *x; S.ncand = 0; S.tries = 0;
  if (res) cJSON_ArrayForEach(x, res) { if (S.ncand >= 20) break; int id = (int)js_num(x, "id", 0); const char *nm = js_str(x, "title", js_str(x, "name", "")), *pp = js_str(x, "poster_path", ""), *bp = js_str(x, "backdrop_path", "");
    if (id <= 0 || !*nm || !*pp) continue; Item *it = &S.cand[S.ncand++]; memset(it, 0, sizeof *it);
    snprintf(it->id, sizeof it->id, "tmdb:%d", id); snprintf(it->type, sizeof it->type, "%s", tvAsked ? "series" : "movie"); snprintf(it->name, sizeof it->name, "%s", nm);
    snprintf(it->poster, sizeof it->poster, "https://image.tmdb.org/t/p/w342%s", pp); if (*bp) snprintf(it->background, sizeof it->background, "https://image.tmdb.org/t/p/w1280%s", bp);
    snprintf(it->description, sizeof it->description, "%s", js_str(x, "overview", "")); }
  net_json_free(j);
  if (!S.ncand) { S.busy = 0; toast(T("Nothing found."), 2000, NULL); return; }
  try_next();
}
static void pick(void) {
  if (!tmdb_has_key()) { toast(T("Needs a TMDB key"), 2500, NULL); return; }
  const Mood *m = &MOODS[S.sel]; int kids = kids_on(); tvAsked = S.kind == 2 ? 1 : S.kind == 1 ? 0 : rand() % 2;
  char lang[24], reg[8], path[600], g[40] = ""; place(lang, sizeof lang, reg, sizeof reg); int gid = tvAsked ? m->tv : m->movie;
  if (kids) snprintf(g, sizeof g, "&with_genres=%s%d", tvAsked ? "10762," : "10751,", gid && gid != 27 ? gid : tvAsked ? 16 : 16);
  else if (gid) snprintf(g, sizeof g, "&with_genres=%d", gid);
  int page = 1 + rand() % (kids ? 3 : 8);
  if (tvAsked) snprintf(path, sizeof path, "/discover/tv?sort_by=popularity.desc&vote_average.gte=%s&vote_count.gte=%d%s&without_genres=10763,10767,10764%s&page=%d&language=%s",
                        kids ? "6.5" : "7.5", kids ? 30 : 300, g, kids ? ",80,99" : "", page, lang);
  else snprintf(path, sizeof path, "/discover/movie?sort_by=popularity.desc&vote_average.gte=%s&vote_count.gte=%d%s%s&page=%d&language=%s",
                kids ? "6.5" : "7", kids ? 100 : 800, g, kids ? "&certification_country=US&certification.lte=PG&without_genres=27,53,80,10752" : "", page, lang);
  S.busy = 1; S.gen++;
  if (!tmdb_request(path, on_list, (void *)(uintptr_t)S.gen)) { S.busy = 0; toast(T("Nothing found."), 2000, NULL); }
}
static int key(int k) {
  if (!S.open) { app_overlay = S.prev; return 0; }
  if (k == K_UP && S.sel > -1) S.sel--; else if (k == K_DOWN && S.sel < NM - 1) S.sel++;
  else if ((k == K_LEFT || k == K_RIGHT) && S.sel == -1) S.kind = (S.kind + (k == K_RIGHT ? 1 : 2)) % 3;
  else if (k == K_OK) { if (S.sel == -1) S.kind = (S.kind + 1) % 3; else if (!S.busy) pick(); }
  else if (k == K_BACK) close_card();
  return 1;
}
static void tick(float dt) { (void)dt; }
static void on_focus(int a, int b) { (void)b; if (S.open) S.sel = a; }
static void on_act(int a, int b) { (void)b; if (!S.open) return; S.sel = a; if (a == -1) S.kind = (S.kind + 1) % 3; else if (!S.busy) pick(); }
static void draw(void) {
  if (!S.open) return; float k = (float)((app_now() - S.at) / 0.25); if (k > 1) k = 1;
  float h = 190 + (NM + 1) * (RH + 6) + 40, x = SB_W / 2 - CW / 2, y = SB_H / 2 - h / 2;
  gfx_opacity_push(k); gfx_rect(0, 0, SB_W, SB_H, BLACK_A(150));
  glass_rrect(x, y, CW, h, 32, RGB(21, 18, 31), .14f, .02f, .19f);
  text_draw(T("Surprise me"), SB_W / 2, y + 40, 42, 800, WHITE_A(255), 0, CW - 60, ALIGN_CENTER);
  text_draw(S.busy ? T("Looking for something good…") : T("What do you feel like watching?"), SB_W / 2, y + 102, 26, 500, WHITE_A(190), 0, CW - 60, ALIGN_CENTER);
  float ry = y + 170;
  for (int i = -1; i < NM; i++) { int on = S.sel == i; float rx = x + 50, rw = CW - 100;
    hit_add(rx, ry, rw, RH, on_focus, on_act, i, 0); gfx_rrect(rx, ry, rw, RH, RH / 2, on ? WHITE_A(255) : WHITE_A(i == -1 ? 34 : 18));
    char lab[160]; if (i == -1) snprintf(lab, sizeof lab, "‹  %s  ›", S.kind == 1 ? T("A film") : S.kind == 2 ? T("A series") : T("A film or a series")); else snprintf(lab, sizeof lab, "%s", T(MOODS[i].name));
    text_draw(lab, SB_W / 2, ry + (RH - text_line_height(27)) / 2, 27, on ? 700 : 500, on ? RGB(17, 17, 17) : WHITE_A(230), 0, rw - 30, ALIGN_CENTER);
    ry += RH + 6 + (i == -1 ? 14 : 0); }
  gfx_opacity_pop();
}
Screen surpriseCard = { NULL, NULL, key, tick, draw, NULL, NULL, NULL };
void surprise_show(void) {
  static int seeded; if (!seeded) { seeded = 1; srand((unsigned)time(NULL)); }
  S.open = 1; S.sel = 0; S.busy = 0; S.at = app_now(); S.gen++; S.prev = app_overlay == &surpriseCard ? NULL : app_overlay; app_overlay = &surpriseCard;
}
