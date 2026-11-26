/* kids profiles. A profile marked for a child sees only children's films and series, in every tab:
   rows asked of TMDB (Disney+ news, the big animation studios, children's films and series), with an age limit (US
   rating G or PG for films, TMDB's Kids genre for series) and no horror, thriller, crime or war. Discover and Search
   take the same rule. Titles come as "tmdb:<id>": opening one asks TMDB for its IMDb id first, so the addons find its
   streams. Without a TMDB key, Cinemeta's Family and Animation lists. */
#include "core/kids.h"
#include "core/profiles.h"
#include "core/tmdb.h"
#include "core/net.h"
#include "core/json.h"
#include "core/i18n.h"
#include "core/app.h"
#include "core/store.h"
#include "screens/surprise.h"
#include "platform/plat.h"
#include "screens/detail.h"
#include "ui/widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PFX "tmdbk:"
#define MOVIE_SAFE "&include_adult=false&certification_country=US&certification.lte=PG&without_genres=27,53,80,10752"
#define TV_SAFE "&include_adult=false&without_genres=80,99,10764,10766,10767,10768"

int kids_on(void) { int id = profiles_active(); return id > 0 && profiles_kids(id); }
int kids_url(const char *url) { return url && !strncmp(url, PFX, sizeof PFX - 1); }

/* the TV's language and country for TMDB ("es-ES", "ES"); English and the US when it says nothing */
static void place(char *lang, size_t lc, char *reg, size_t rc) {
  const char *l = plat_ui_locale(); if (!l || !*l) l = "en-US";
  snprintf(lang, lc, "%s", l); char *u = strchr(lang, '_'); if (u) *u = '-';
  const char *d = strchr(lang, '-'); snprintf(reg, rc, "%s", d && strlen(d + 1) == 2 ? d + 1 : "US");
}
static void ago(char *out, size_t cap, int days) { time_t t = time(NULL) - (time_t)days * 86400; struct tm tm; gmtime_r(&t, &tm); strftime(out, cap, "%Y-%m-%d", &tm); }

static void def(CatalogDef *d, const char *title, const char *type, const char *path, const char *key) {
  memset(d, 0, sizeof *d); snprintf(d->title, sizeof d->title, "%s", title); snprintf(d->type, sizeof d->type, "%s", type);
  snprintf(d->url, sizeof d->url, PFX "%s", path); snprintf(d->path, sizeof d->path, "%s", d->url); snprintf(d->key, sizeof d->key, "k_%s", key); d->skip = 1; }

typedef struct { const char *title, *type, *query, *key; } KRow;

/* each tab's rows: %R is the country, %L the language, %D the date a year and a half ago */
static const KRow HOME[] = {
  { "New on Disney+", "movie", "/discover/movie?with_watch_providers=337&watch_region=%R&sort_by=popularity.desc&primary_release_date.gte=%D" MOVIE_SAFE, "dnew" },
  { "Disney+ series", "series", "/discover/tv?with_watch_providers=337&watch_region=%R&with_genres=10762&sort_by=popularity.desc" TV_SAFE, "dser" },
  { "Popular children's films", "movie", "/discover/movie?with_genres=10751&sort_by=popularity.desc&vote_count.gte=50" MOVIE_SAFE, "mpop" },
  { "Children's series", "series", "/discover/tv?with_genres=10762&sort_by=popularity.desc&vote_count.gte=10" TV_SAFE, "spop" },
  { "Pixar", "movie", "/discover/movie?with_companies=3&sort_by=popularity.desc" MOVIE_SAFE, "pixar" },
  { "Walt Disney Animation", "movie", "/discover/movie?with_companies=6125&sort_by=popularity.desc" MOVIE_SAFE, "wdas" },
  { "DreamWorks Animation", "movie", "/discover/movie?with_companies=521&sort_by=popularity.desc" MOVIE_SAFE, "dwa" },
  { "Illumination", "movie", "/discover/movie?with_companies=6704&sort_by=popularity.desc" MOVIE_SAFE, "illu" },
  { "New children's films", "movie", "/discover/movie?with_genres=10751&sort_by=popularity.desc&primary_release_date.gte=%D" MOVIE_SAFE, "mnew" },
  { "Best-rated animated films", "movie", "/discover/movie?with_genres=16&sort_by=vote_average.desc&vote_count.gte=1500" MOVIE_SAFE, "anitop" },
  { "Disney classics", "movie", "/discover/movie?with_companies=2&with_genres=16&primary_release_date.lte=2005-12-31&sort_by=vote_count.desc" MOVIE_SAFE, "dclas" },
  { "Studio Ghibli", "movie", "/discover/movie?with_companies=10342&sort_by=popularity.desc" MOVIE_SAFE, "ghibli" },
};
static const KRow FILMS[] = {
  { "New on Disney+", "movie", "/discover/movie?with_watch_providers=337&watch_region=%R&sort_by=popularity.desc&primary_release_date.gte=%D" MOVIE_SAFE, "dnew" },
  { "Popular on Disney+", "movie", "/discover/movie?with_watch_providers=337&watch_region=%R&sort_by=popularity.desc" MOVIE_SAFE, "dpop" },
  { "Popular children's films", "movie", "/discover/movie?with_genres=10751&sort_by=popularity.desc&vote_count.gte=50" MOVIE_SAFE, "mpop" },
  { "New children's films", "movie", "/discover/movie?with_genres=10751&sort_by=popularity.desc&primary_release_date.gte=%D" MOVIE_SAFE, "mnew" },
  { "Best-rated animated films", "movie", "/discover/movie?with_genres=16&sort_by=vote_average.desc&vote_count.gte=1500" MOVIE_SAFE, "anitop" },
  { "Pixar", "movie", "/discover/movie?with_companies=3&sort_by=popularity.desc" MOVIE_SAFE, "pixar" },
  { "Walt Disney Animation", "movie", "/discover/movie?with_companies=6125&sort_by=popularity.desc" MOVIE_SAFE, "wdas" },
  { "DreamWorks Animation", "movie", "/discover/movie?with_companies=521&sort_by=popularity.desc" MOVIE_SAFE, "dwa" },
  { "Illumination", "movie", "/discover/movie?with_companies=6704&sort_by=popularity.desc" MOVIE_SAFE, "illu" },
  { "Disney classics", "movie", "/discover/movie?with_companies=2&with_genres=16&primary_release_date.lte=2005-12-31&sort_by=vote_count.desc" MOVIE_SAFE, "dclas" },
  { "Adventures", "movie", "/discover/movie?with_genres=10751,12&sort_by=popularity.desc&vote_count.gte=50" MOVIE_SAFE, "madv" },
  { "Comedies", "movie", "/discover/movie?with_genres=10751,35&sort_by=popularity.desc&vote_count.gte=50" MOVIE_SAFE, "mcom" },
  { "Fantasy", "movie", "/discover/movie?with_genres=10751,14&sort_by=popularity.desc&vote_count.gte=50" MOVIE_SAFE, "mfan" },
};
static const KRow SERIES[] = {
  { "Disney+ series", "series", "/discover/tv?with_watch_providers=337&watch_region=%R&with_genres=10762&sort_by=popularity.desc" TV_SAFE, "dser" },
  { "Children's series", "series", "/discover/tv?with_genres=10762&sort_by=popularity.desc&vote_count.gte=10" TV_SAFE, "spop" },
  { "New children's series", "series", "/discover/tv?with_genres=10762&sort_by=popularity.desc&first_air_date.gte=%D" TV_SAFE, "snew" },
  { "Best-rated children's series", "series", "/discover/tv?with_genres=10762&sort_by=vote_average.desc&vote_count.gte=150" TV_SAFE, "stop" },
  { "Animated series for children", "series", "/discover/tv?with_genres=16,10762&sort_by=popularity.desc" TV_SAFE, "sani" },
  { "Children's series on Netflix", "series", "/discover/tv?with_watch_providers=8&watch_region=%R&with_genres=10762&sort_by=popularity.desc" TV_SAFE, "snfx" },
  { "Children's series on Prime Video", "series", "/discover/tv?with_watch_providers=119&watch_region=%R&with_genres=10762&sort_by=popularity.desc" TV_SAFE, "sprime" },
};
static const KRow ANIME[] = {
  { "Studio Ghibli", "movie", "/discover/movie?with_companies=10342&sort_by=popularity.desc" MOVIE_SAFE, "ghibli" },
  { "Japanese animated films", "movie", "/discover/movie?with_origin_country=JP&with_genres=16&sort_by=popularity.desc" MOVIE_SAFE, "jpmov" },
  { "Japanese series for children", "series", "/discover/tv?with_origin_country=JP&with_genres=16,10762&sort_by=popularity.desc" TV_SAFE, "jpser" },
};

static void expand(const char *q, char *out, size_t cap) {
  char lang[24], reg[8], d18[16]; place(lang, sizeof lang, reg, sizeof reg); ago(d18, sizeof d18, 540);
  size_t n = 0; for (const char *p = q; *p && n + 24 < cap; p++) {
    if (p[0] == '%' && (p[1] == 'R' || p[1] == 'D')) { n += (size_t)snprintf(out + n, cap - n, "%s", p[1] == 'R' ? reg : d18); p++; }
    else out[n++] = *p; }
  out[n] = 0; snprintf(out + n, cap - n, "&language=%s", lang);
}
static int rows(const KRow *r, int nr, CatalogDef *out, int max, const char *only) {
  int n = 0; for (int i = 0; i < nr && n < max; i++) { if (only && strcmp(r[i].type, only)) continue; char path[480]; expand(r[i].query, path, sizeof path); def(&out[n++], T(r[i].title), r[i].type, path, r[i].key); }
  return n; }

/* no TMDB key: Cinemeta's family and animation lists (genre names as Cinemeta has them) */
static int cinemeta(int kind, CatalogDef *out, int max) {
  static const struct { const char *title, *type, *genre; int kind; } C[] = {
    { "Children's films", "movie", "Family", 0 }, { "Animated films", "movie", "Animation", 0 }, { "Animated series", "series", "Animation", 0 },
    { "Children's films", "movie", "Family", 2 }, { "Animated films", "movie", "Animation", 2 }, { "Animated series", "series", "Animation", 3 }, { "Children's series", "series", "Family", 3 } };
  int n = 0; for (size_t i = 0; i < sizeof C / sizeof C[0] && n < max; i++) { if (C[i].kind != (kind == 1 || kind == 4 ? 0 : kind)) continue;
    CatalogDef *d = &out[n++]; memset(d, 0, sizeof *d); snprintf(d->title, sizeof d->title, "%s", T(C[i].title)); snprintf(d->type, sizeof d->type, "%s", C[i].type);
    snprintf(d->path, sizeof d->path, CINEMETA "/catalog/%s/top", C[i].type); snprintf(d->extra, sizeof d->extra, "genre=%s", C[i].genre);
    snprintf(d->url, sizeof d->url, "%s/%s.json", d->path, d->extra); snprintf(d->key, sizeof d->key, "kc_%d", (int)i); d->skip = 1; }
  return n; }

int foryou_defs(int kind, CatalogDef *out, int max);
int kids_defs(int kind, CatalogDef *out, int max) {
  if (!tmdb_has_key()) return cinemeta(kind, out, max);
  if (kind == 2) return rows(FILMS, sizeof FILMS / sizeof FILMS[0], out, max, NULL);
  if (kind == 3) return rows(SERIES, sizeof SERIES / sizeof SERIES[0], out, max, NULL);
  if (kind == 4) return rows(ANIME, sizeof ANIME / sizeof ANIME[0], out, max, NULL);
  if (max < 2) return rows(HOME, sizeof HOME / sizeof HOME[0], out, max, NULL);
  CatalogDef *s0 = &out[0]; memset(s0, 0, sizeof *s0); snprintf(s0->title, sizeof s0->title, "%s", T("Surprise me")); snprintf(s0->type, sizeof s0->type, "platform");
  snprintf(s0->url, sizeof s0->url, PFX "s/"); snprintf(s0->path, sizeof s0->path, "%s", s0->url); snprintf(s0->key, sizeof s0->key, "k_surp"); s0->round = 1;
  int f = foryou_defs(0, out + 1, max - 1); /* (what she watched: more of it, children's titles only) */
  return 1 + f + rows(HOME, sizeof HOME / sizeof HOME[0], out + 1 + f, max - 1 - f, NULL);
}
int kids_disc_cats(const char *type, CatalogDef *out, int max) {
  if (!tmdb_has_key()) { CatalogDef all[8]; int n = cinemeta(0, all, 8), k = 0; for (int i = 0; i < n && k < max; i++) if (!strcmp(all[i].type, type)) out[k++] = all[i]; return k; }
  return !strcmp(type, "series") ? rows(SERIES, sizeof SERIES / sizeof SERIES[0], out, max, NULL) : rows(FILMS, sizeof FILMS / sizeof FILMS[0], out, max, "movie");
}
int kids_search_defs(const char *q, CatalogDef *out, int max) {
  if (!tmdb_has_key() || max < 2) return 0; char enc[300]; size_t n = 0;
  for (const unsigned char *p = (const unsigned char *)q; *p && n + 4 < sizeof enc; p++) { if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == '.') enc[n++] = (char)*p; else n += (size_t)snprintf(enc + n, sizeof enc - n, "%%%02X", *p); }
  enc[n] = 0; char lang[24], reg[8], path[480]; place(lang, sizeof lang, reg, sizeof reg);
  snprintf(path, sizeof path, "f/search/movie?query=%s&include_adult=false&language=%s", enc, lang); def(&out[0], T("Films"), "movie", path, "qm");
  snprintf(path, sizeof path, "f/search/tv?query=%s&include_adult=false&language=%s", enc, lang); def(&out[1], T("Series"), "series", path, "qs");
  out[0].skip = out[1].skip = 0; return 2;
}
void kids_feat(char *movieUrl, size_t mc, char *seriesUrl, size_t sc) {
  CatalogDef d; char path[480];
  expand(HOME[0].query, path, sizeof path); def(&d, "", "movie", path, "f1"); snprintf(movieUrl, mc, "%s", d.url);
  expand(HOME[3].query, path, sizeof path); def(&d, "", "series", path, "f2"); snprintf(seriesUrl, sc, "%s", d.url);
}

/* a search answer keeps only what is for children: Family, Animation or Kids, and none of horror, thriller, crime, war */
static int child_safe(const cJSON *r) {
  const cJSON *g = js_arr(r, "genre_ids"), *x; int ok = 0; if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "adult"))) return 0;
  if (g) cJSON_ArrayForEach(x, g) { int v = x->valueint; if (v == 27 || v == 53 || v == 80 || v == 10752) return 0; if (v == 16 || v == 10751 || v == 10762) ok = 1; }
  return ok; }

typedef struct { char type[16]; int filter, max; void (*cb)(ItemList *, int, void *); void (*pcb)(ItemList *, int, void *); void *ud; } KReq;

/* the platforms of this country (TMDB's watch providers): the best known first, in this order, then the country's own most
   watched ones (never an add-on channel of another one, nor an "with ads" twin), fourteen at most */
static const int FAMOUS[] = { 8, 337, 1899, 119, 350, 149, 1773, 531, 63, 283, 35, 11, 300, 15, 386 };
static int channel_twin(const char *nm) { return strstr(nm, "Channel") || strstr(nm, "with Ads") || strstr(nm, "Standard with") || strstr(nm, "Basic with") || strstr(nm, "Store") || strstr(nm, "Play Movies") || !strcmp(nm, "Amazon Video"); } /* (rental shops: nothing to watch with a subscription) */
static void surprise_item(Item *it);
static ItemList *parse_providers(cJSON *j) {
  const cJSON *res = js_arr(j, "results"), *x; ItemList *l = calloc(1, sizeof *l); l->items = calloc(16, sizeof(Item)); l->total = j ? 0 : -1;
  if (!res) return l; surprise_item(&l->items[l->n++]); /* (Surprise me, first among the platforms) */ char reg[8], lang[24]; place(lang, sizeof lang, reg, sizeof reg);
  for (int pass = 0; pass < 2; pass++) for (size_t f = 0; f < (pass ? 999 : sizeof FAMOUS / sizeof FAMOUS[0]) && l->n < 15; f++) {
    const cJSON *pick = NULL; int best = 1 << 30;
    cJSON_ArrayForEach(x, res) { int id = (int)js_num(x, "provider_id", 0); const char *nm = js_str(x, "provider_name", ""), *lp = js_str(x, "logo_path", ""); if (!id || !*nm || !*lp) continue;
      int have = 0; for (int i = 0; i < l->n && !have; i++) have = atoi(l->items[i].id + 5) == id; if (have) continue;
      if (!pass) { if (id == FAMOUS[f]) { pick = x; break; } continue; }
      if (channel_twin(nm)) continue; int pr = (int)js_num(js_obj(x, "display_priorities"), reg, js_num(x, "display_priority", 999)); if (pr < best) { best = pr; pick = x; } }
    if (!pick) { if (pass) break; continue; }
    if (pass && best > 25) break;
    Item *it = &l->items[l->n++]; snprintf(it->id, sizeof it->id, "plat:%d", (int)js_num(pick, "provider_id", 0)); snprintf(it->type, sizeof it->type, "platform");
    snprintf(it->name, sizeof it->name, "%s", js_str(pick, "provider_name", "")); snprintf(it->logo, sizeof it->logo, "https://image.tmdb.org/t/p/w300%s", js_str(pick, "logo_path", ""));
    snprintf(it->poster, sizeof it->poster, "%s", it->logo); snprintf(it->description, sizeof it->description, "%s", T1("Films and series on {p}", "p", it->name)); }
  l->total = l->n; return l; }

static ItemList *parse(const unsigned char *body, const KReq *r, int *more) {
  if (!strcmp(r->type, "platform")) { cJSON *pj = body ? net_json(body) : NULL; ItemList *pl = parse_providers(pj); net_json_free(pj); if (more) *more = 0; return pl; }
  cJSON *j = body ? net_json(body) : NULL; const cJSON *res = js_arr(j, "results"), *x; ItemList *l = calloc(1, sizeof *l);
  int n = res ? cJSON_GetArraySize(res) : 0; l->items = calloc(n ? (size_t)n : 1, sizeof(Item)); l->total = j ? n : -1;
  if (more) { int pg = (int)js_num(j, "page", 1), tp = (int)js_num(j, "total_pages", 1); *more = pg < tp && pg < 25; }
  if (res) cJSON_ArrayForEach(x, res) { if (l->n >= r->max) break; if (r->filter && !child_safe(x)) continue;
    int id = (int)js_num(x, "id", 0); const char *pp = js_str(x, "poster_path", ""), *bp = js_str(x, "backdrop_path", "");
    const char *nm = js_str(x, "title", js_str(x, "name", "")), *dt = js_str(x, "release_date", js_str(x, "first_air_date", ""));
    if (id <= 0 || !*nm || !*pp) continue;
    Item *it = &l->items[l->n++]; snprintf(it->id, sizeof it->id, "tmdb:%d", id); snprintf(it->type, sizeof it->type, "%s", r->type);
    snprintf(it->name, sizeof it->name, "%s", nm); snprintf(it->poster, sizeof it->poster, "https://image.tmdb.org/t/p/w342%s", pp);
    if (*bp) snprintf(it->background, sizeof it->background, "https://image.tmdb.org/t/p/w1280%s", bp);
    snprintf(it->description, sizeof it->description, "%s", js_str(x, "overview", "")); if (strlen(dt) >= 4) snprintf(it->metaLine, sizeof it->metaLine, "%.4s", dt); }
  net_json_free(j); return l; }
/* ---- the premieres: what comes out on the platforms in the next weeks, each with its day. A series' day is
   its first air date; a film's is its digital release in this country (asked of each film), the one streaming services use.
   The day one comes out, a notice on Home (once a day). */
static double day_at(const char *ymd) { struct tm tm; memset(&tm, 0, sizeof tm); if (sscanf(ymd, "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) != 3) return 0; tm.tm_year -= 1900; tm.tm_mon--; tm.tm_hour = 12; tm.tm_isdst = -1; return (double)mktime(&tm); }
typedef struct { KReq *r; ItemList *l; int left; } UpJob;
static int by_day(const void *a, const void *b) { double x = ((const Item *)a)->airAt, y = ((const Item *)b)->airAt; return x < y ? -1 : x > y; }
static void up_done(UpJob *u) {
  ItemList *l = u->l; int k = 0; double now = (double)time(NULL), from = now - 2 * 86400, to = now + 40 * 86400;
  for (int i = 0; i < l->n; i++) if (l->items[i].airAt >= from && l->items[i].airAt <= to) l->items[k++] = l->items[i];
  l->n = k; l->total = k; qsort(l->items, (size_t)l->n, sizeof(Item), by_day);
  char today[16], names[300] = ""; time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm); strftime(today, sizeof today, "%Y-%m-%d", &tm); int nt = 0;
  for (int i = 0; i < l->n; i++) { time_t a = (time_t)l->items[i].airAt; struct tm b; localtime_r(&a, &b); char d[16]; strftime(d, sizeof d, "%Y-%m-%d", &b);
    if (!strcmp(d, today) && nt < 3) { size_t m = strlen(names); snprintf(names + m, sizeof names - m, "%s%s", nt ? ", " : "", l->items[i].name); nt++; } }
  char key[24]; snprintf(key, sizeof key, "premToast-%c", u->r->type[0]);
  extern int player_open(void);
  if (nt && strcmp(store_get(key, ""), today) && !player_open()) { store_set(key, today); toast(T1("Out today: {t}", "t", names), 6000, NULL); }
  u->r->cb(l, 0, u->r->ud); free(u->r); free(u);
}
static void on_reldate(int st, const unsigned char *body, size_t len, void *ud) {
  (void)len; Item *it = ud; UpJob *u; memcpy(&u, it->epName, sizeof u); it->epName[0] = 0; /* (the job rode in the item's spare field while it was asked) */
  cJSON *j = st == 200 ? net_json(body) : NULL; char lang[24], reg[8]; place(lang, sizeof lang, reg, sizeof reg); double best = 0;
  const cJSON *res = js_arr(j, "results"), *x; if (res) cJSON_ArrayForEach(x, res) { if (strcmp(js_str(x, "iso_3166_1", ""), reg)) continue;
    const cJSON *rd = js_arr(x, "release_dates"), *y; if (rd) cJSON_ArrayForEach(y, rd) { int ty = (int)js_num(y, "type", 0); if (ty != 4) continue; double a = day_at(js_str(y, "release_date", "")); if (a > 0 && (!best || a < best)) best = a; } }
  net_json_free(j); it->airAt = best;
  if (--u->left == 0) up_done(u);
}
static void on_up(int st, const unsigned char *body, size_t len, void *ud) {
  (void)len; KReq *r = ud; ItemList *l = parse(st == 200 ? body : NULL, r, NULL);
  if (!strcmp(r->type, "series")) { cJSON *j = st == 200 ? net_json(body) : NULL; const cJSON *res = js_arr(j, "results"), *x;
    if (res) cJSON_ArrayForEach(x, res) { char id[24]; snprintf(id, sizeof id, "tmdb:%d", (int)js_num(x, "id", 0)); for (int i = 0; i < l->n; i++) if (!strcmp(l->items[i].id, id)) l->items[i].airAt = day_at(js_str(x, "first_air_date", "")); }
    net_json_free(j); UpJob *u = calloc(1, sizeof *u); u->r = r; u->l = l; up_done(u); return; }
  UpJob *u = calloc(1, sizeof *u); u->r = r; u->l = l; u->left = l->n;
  if (!l->n) { up_done(u); return; }
  int n = l->n; for (int i = 0; i < n; i++) { Item *it = &l->items[i]; memcpy(it->epName, &u, sizeof u); char path[64]; snprintf(path, sizeof path, "/movie/%d/release_dates", atoi(it->id + 5));
    if (!tmdb_request(path, on_reldate, it)) { it->epName[0] = 0; if (--u->left == 0) { up_done(u); return; } } }
}
static void on_list(int st, const unsigned char *body, size_t len, void *ud) { (void)len; KReq *r = ud; ItemList *l = parse(st == 200 ? body : NULL, r, NULL); r->cb(l, 0, r->ud); free(r); }
static void on_page(int st, const unsigned char *body, size_t len, void *ud) { (void)len; KReq *r = ud; int more = 0; ItemList *l = st == 200 ? parse(body, r, &more) : NULL; r->pcb(l, more, r->ud); free(r); }

/* f/: a search kept to children's titles; p/: the platforms; u/: the premieres; s/: the Surprise me badge alone */
static const char *tmdb_path(const char *url, int *filter) { const char *p = url + sizeof PFX - 1; *filter = !strncmp(p, "f/", 2); return *filter || !strncmp(p, "p/", 2) || !strncmp(p, "u/", 2) || !strncmp(p, "s/", 2) ? p + 1 : p; }
static void surprise_item(Item *it) { memset(it, 0, sizeof *it); snprintf(it->id, sizeof it->id, "surp:"); snprintf(it->type, sizeof it->type, "platform"); snprintf(it->name, sizeof it->name, "%s", T("Surprise me")); snprintf(it->description, sizeof it->description, "%s", T("One good title for tonight, by mood")); }
/* ---- For you: what this profile watched (its own library: finished, half-seen, or only saved, the
   latest first) asks TMDB for its recommendations; each title scores by how near the top it came and how recently and
   how fully its source was watched; the best twenty, without the sources themselves. "Because you watched X": one source. */
typedef struct { char id[24], type[16], name[128]; double at, w; } Seed;
static double iso_ms(const char *s) { struct tm tm; memset(&tm, 0, sizeof tm); if (!s || sscanf(s, "%d-%d-%dT%d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) < 3) return 0; tm.tm_year -= 1900; tm.tm_mon--; return (double)timegm(&tm); }
static int by_recent(const void *a, const void *b) { double x = ((const Seed *)a)->at, y = ((const Seed *)b)->at; return x > y ? -1 : x < y; }
static int seeds(Seed *out, int max, int engagedOnly) {
  cJSON *lib = store_node("lib"); const cJSON *it; int n = 0; static Seed all[400]; int na = 0;
  if (!cJSON_IsObject(lib)) return 0;
  cJSON_ArrayForEach(it, lib) { if (na >= 400) break; const char *id = js_str(it, "_id", it->string ? it->string : ""), *ty = js_str(it, "type", "");
    if (strncmp(id, "tt", 2) || (strcmp(ty, "movie") && strcmp(ty, "series")) || !js_str(it, "name", "")[0]) continue;
    const cJSON *s = js_obj(it, "state"); int done = js_num(s, "timesWatched", 0) > 0 || js_num(s, "flaggedWatched", 0) > 0, part = js_num(s, "timeOffset", 0) > 0 || js_num(s, "timeWatched", 0) > 0;
    int removed = js_bool(it, "removed", 0) && !js_bool(it, "temp", 0); double w = done ? 1.0 : part ? 0.8 : removed ? 0 : 0.4;
    if (w <= 0 || (engagedOnly && w < 0.8)) continue;
    double at = iso_ms(js_str(s, "lastWatched", "")); if (at <= 0) at = iso_ms(js_str(it, "_mtime", ""));
    Seed *d = &all[na++]; snprintf(d->id, sizeof d->id, "%s", id); snprintf(d->type, sizeof d->type, "%s", ty); snprintf(d->name, sizeof d->name, "%s", js_str(it, "name", "")); d->at = at; d->w = w; }
  qsort(all, (size_t)na, sizeof(Seed), by_recent);
  for (int i = 0; i < na && n < max; i++) out[n++] = all[i];
  return n; }

int foryou_defs(int kind, CatalogDef *out, int max) {
  if (!tmdb_has_key() || max < 1) return 0; Seed s[2]; int n = 0;
  if (seeds(s, 1, 0) < 1) return 0; /* (nothing watched yet: nothing to go by) */
  const char *ty = kind == 2 ? "movie" : kind == 3 ? "series" : "";
  CatalogDef *d = &out[n++]; memset(d, 0, sizeof *d); snprintf(d->title, sizeof d->title, "%s", T(kind == 2 ? "Films for you" : kind == 3 ? "Series for you" : "For you"));
  snprintf(d->type, sizeof d->type, "%s", *ty ? ty : "movie"); snprintf(d->url, sizeof d->url, PFX "r/%s", ty); snprintf(d->path, sizeof d->path, "%s", d->url); snprintf(d->key, sizeof d->key, "fy_%d", kind);
  if (kind == 0) { int ns = seeds(s, 2, 1); for (int i = 0; i < ns && n < max; i++) { d = &out[n++]; memset(d, 0, sizeof *d);
      snprintf(d->title, sizeof d->title, "%s", T1("Because you watched {t}", "t", s[i].name)); snprintf(d->type, sizeof d->type, "%s", s[i].type);
      snprintf(d->url, sizeof d->url, PFX "b/%s/%s", s[i].id, s[i].type); snprintf(d->path, sizeof d->path, "%s", d->url); snprintf(d->key, sizeof d->key, "fb_%s", s[i].id); } }
  return n;
}

typedef struct { int id; char media[8]; double score; Item it; } Cand;
typedef struct { void (*cb)(ItemList *, int, void *); void *ud; int max, left, kids; char want[16]; Seed s[6]; int ns, seedTmdb[6]; Cand *c; int nc, cap; } FyJob;
typedef struct { FyJob *j; int k; } FyStep;
static void fy_finish(FyJob *j) {
  for (int a = 0; a < j->nc; a++) for (int b = a + 1; b < j->nc; b++) if (j->c[b].score > j->c[a].score) { Cand t = j->c[a]; j->c[a] = j->c[b]; j->c[b] = t; }
  ItemList *l = calloc(1, sizeof *l); l->items = calloc((size_t)(j->max ? j->max : 1), sizeof(Item));
  for (int i = 0; i < j->nc && l->n < j->max; i++) { int src = 0; for (int k = 0; k < j->ns; k++) if (j->seedTmdb[k] == j->c[i].id) src = 1; if (!src) l->items[l->n++] = j->c[i].it; }
  l->total = l->n; fprintf(stderr, "[foryou] %d titles from %d watched\n", l->n, j->ns);
  j->cb(l, 0, j->ud); free(j->c); free(j); }
static void fy_done_one(FyJob *j) { if (--j->left == 0) fy_finish(j); }
static void on_fy_recs(int st, const unsigned char *body, size_t len, void *ud) {
  (void)len; FyStep *p = ud; FyJob *j = p->j; int k = p->k; free(p);
  cJSON *js = st == 200 ? net_json(body) : NULL; const cJSON *res = js_arr(js, "results"), *x; int pos = 0, tv = !strcmp(j->s[k].type, "series");
  double w = j->s[k].w / (1.0 + k * 0.25);
  if (res) cJSON_ArrayForEach(x, res) { pos++; int id = (int)js_num(x, "id", 0); const char *nm = js_str(x, "title", js_str(x, "name", "")), *pp = js_str(x, "poster_path", ""), *bp = js_str(x, "backdrop_path", "");
    const char *mt = js_str(x, "media_type", tv ? "tv" : "movie"); int isTv = !strcmp(mt, "tv");
    if (id <= 0 || !*nm || !*pp || (*j->want && (isTv != !strcmp(j->want, "series")))) continue;
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(x, "adult")) || (j->kids && !child_safe(x))) continue;
    double sc = w * (1.0 - pos / 25.0) + js_num(x, "vote_average", 0) / 100.0; int f = -1;
    for (int i = 0; i < j->nc && f < 0; i++) if (j->c[i].id == id && !strcmp(j->c[i].media, mt)) f = i;
    if (f >= 0) { j->c[f].score += sc; continue; }
    if (j->nc >= j->cap) { int nc2 = j->cap ? j->cap * 2 : 64; Cand *n2 = realloc(j->c, sizeof(Cand) * (size_t)nc2); if (!n2) continue; j->c = n2; j->cap = nc2; }
    Cand *c = &j->c[j->nc++]; memset(c, 0, sizeof *c); c->id = id; snprintf(c->media, sizeof c->media, "%s", isTv ? "tv" : "movie"); c->score = sc;
    Item *it = &c->it; snprintf(it->id, sizeof it->id, "tmdb:%d", id); snprintf(it->type, sizeof it->type, "%s", isTv ? "series" : "movie"); snprintf(it->name, sizeof it->name, "%s", nm);
    snprintf(it->poster, sizeof it->poster, "https://image.tmdb.org/t/p/w342%s", pp); if (*bp) snprintf(it->background, sizeof it->background, "https://image.tmdb.org/t/p/w1280%s", bp);
    snprintf(it->description, sizeof it->description, "%s", js_str(x, "overview", "")); }
  net_json_free(js); fy_done_one(j);
}
static void on_fy_find(int st, const unsigned char *body, size_t len, void *ud) {
  (void)len; FyStep *p = ud; FyJob *j = p->j; int k = p->k;
  cJSON *js = st == 200 ? net_json(body) : NULL; int tv = !strcmp(j->s[k].type, "series");
  const cJSON *arr = js_arr(js, tv ? "tv_results" : "movie_results"); const cJSON *f = arr ? cJSON_GetArrayItem(arr, 0) : NULL; int id = f ? (int)js_num(f, "id", 0) : 0; net_json_free(js);
  if (id <= 0) { free(p); fy_done_one(j); return; }
  j->seedTmdb[k] = id; char lang[24], reg[8], path[160]; place(lang, sizeof lang, reg, sizeof reg);
  snprintf(path, sizeof path, "/%s/%d/recommendations?language=%s&page=1", tv ? "tv" : "movie", id, lang);
  if (!tmdb_request(path, on_fy_recs, p)) { free(p); fy_done_one(j); }
}
static void foryou_fetch(const char *raw, int n, void (*cb)(ItemList *, int, void *), void *ud) {
  FyJob *j = calloc(1, sizeof *j); j->cb = cb; j->ud = ud; j->max = n > 0 && n < 20 ? 20 : n > 0 ? n : 20; j->kids = kids_on();
  if (!strncmp(raw, "r/", 2)) { snprintf(j->want, sizeof j->want, "%s", raw + 2); j->ns = seeds(j->s, 6, 0); }
  else { char id[24] = "", ty[16] = ""; sscanf(raw + 2, "%23[^/]/%15s", id, ty); snprintf(j->s[0].id, sizeof j->s[0].id, "%s", id); snprintf(j->s[0].type, sizeof j->s[0].type, "%s", ty); j->s[0].w = 1; j->ns = id[0] ? 1 : 0; }
  if (!j->ns) { fy_finish(j); return; }
  j->left = j->ns;
  for (int k = 0; k < j->ns; k++) { FyStep *p = malloc(sizeof *p); p->j = j; p->k = k; char path[96]; snprintf(path, sizeof path, "/find/%s?external_source=imdb_id", j->s[k].id);
    if (!tmdb_request(path, on_fy_find, p)) { free(p); if (--j->left == 0) { fy_finish(j); return; } } }
}

void kids_fetch(const char *url, const char *type, int n, void (*cb)(ItemList *, int, void *), void *ud) {
  const char *raw = url + sizeof PFX - 1;
  if (!strncmp(raw, "s/", 2)) { ItemList *l = calloc(1, sizeof *l); l->items = calloc(1, sizeof(Item)); surprise_item(&l->items[0]); l->n = l->total = 1; cb(l, 0, ud); return; }
  if (!strncmp(raw, "r/", 2) || !strncmp(raw, "b/", 2)) { foryou_fetch(raw, n, cb, ud); return; } /* (For you, Because you watched) */
  KReq *r = calloc(1, sizeof *r); snprintf(r->type, sizeof r->type, "%s", type); r->max = n > 0 ? n : 20; r->cb = cb; r->ud = ud;
  const char *p = tmdb_path(url, &r->filter);
  if (!tmdb_request(p, !strncmp(raw, "u/", 2) ? on_up : on_list, r)) { ItemList *l = calloc(1, sizeof *l); l->items = calloc(1, sizeof(Item)); l->total = -1; free(r); cb(l, 0, ud); }
}
void kids_page(const char *url, const char *type, int skip, void (*cb)(ItemList *, int, void *), void *ud) {
  KReq *r = calloc(1, sizeof *r); snprintf(r->type, sizeof r->type, "%s", type); r->max = 40; r->pcb = cb; r->ud = ud;
  char path[600]; const char *p = tmdb_path(url, &r->filter); snprintf(path, sizeof path, "%s&page=%d", p, skip / 20 + 1);
  if (!tmdb_request(path, on_page, r)) { free(r); cb(NULL, 0, ud); }
}

/* a TMDB title opened: its IMDb id first (the addons find streams by it), then its page */
static unsigned openGen;
static void on_ext(int st, const unsigned char *body, size_t len, void *ud) {
  (void)len; Item *it = ud; unsigned g = (unsigned)(uintptr_t)0; (void)g;
  cJSON *j = st == 200 ? net_json(body) : NULL; const char *imdb = js_str(j, "imdb_id", "");
  if (it->ep == (int)openGen && !strncmp(imdb, "tt", 2)) { DetailParams p; memset(&p, 0, sizeof p); p.it = *it; p.it.ep = 0; snprintf(p.it.id, sizeof p.it.id, "%s", imdb); app_go(SCR_DETAIL, &p, 0); }
  else if (it->ep == (int)openGen) toast(T("Nothing found."), 2000, NULL);
  net_json_free(j); free(it);
}
int kids_open(const Item *it) {
  if (!strcmp(it->id, "surp:")) { surprise_show(); return 1; } /* (Surprise me) */
  { extern int platform_open(const Item *it); if (platform_open(it)) return 1; } /* (a platform: its page) */
  if (strncmp(it->id, "tmdb:", 5)) return 0;
  Item *c = malloc(sizeof *c); if (!c) return 1; *c = *it; c->ep = (int)++openGen; /* (ep carries which press this is: only the last one opens) */
  char path[64]; snprintf(path, sizeof path, "/%s/%d/external_ids", !strcmp(it->type, "series") ? "tv" : "movie", atoi(it->id + 5));
  if (!tmdb_request(path, on_ext, c)) { free(c); toast(T("Nothing found."), 2000, NULL); }
  return 1;
}
