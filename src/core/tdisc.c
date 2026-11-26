/* Discover, put in order. With a TMDB key, films and series come first in clear categories (trending
   today, popular, new to stream, new episodes, top rated, hidden gems), each with the genres in the app's language;
   the addons' own catalogs follow, as before. Asked of TMDB for this country, page after page as the grid scrolls. */
#include "core/tdisc.h"
#include "core/kids.h"
#include "core/tmdb.h"
#include "core/i18n.h"
#include "platform/plat.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

int tdisc_on(void) { return tmdb_has_key() && !kids_on(); }

static void place(char *lang, size_t lc, char *reg, size_t rc) {
  const char *l = plat_ui_locale(); if (!l || !*l) l = "en-US";
  snprintf(lang, lc, "%s", l); char *u = strchr(lang, '_'); if (u) *u = '-';
  const char *d = strchr(lang, '-'); snprintf(reg, rc, "%s", d && strlen(d + 1) == 2 ? d + 1 : "US");
}
static void ago(char *out, size_t cap, int days) { time_t t = time(NULL) - (time_t)days * 86400; struct tm tm; gmtime_r(&t, &tm); strftime(out, cap, "%Y-%m-%d", &tm); }

#define NOTV "&without_genres=10763,10767"   /* no news nor talk shows */
/* %N today, %1 a week ago, %2 two months ago, %4 four months ago, %R the country */
static const struct { const char *title, *key, *movie, *tv; } CATS[] = {
  { "Trending today", "tr", "/trending/movie/day?", "/trending/tv/day?" },
  { "Popular", "pop", "/discover/movie?sort_by=popularity.desc&vote_count.gte=50", "/discover/tv?sort_by=popularity.desc&vote_count.gte=50" NOTV },
  { "New to stream", "new", "/discover/movie?region=%R&with_release_type=4|5&release_date.gte=%2&release_date.lte=%N&sort_by=popularity.desc", NULL },
  { "New series", "nser", NULL, "/discover/tv?first_air_date.gte=%4&first_air_date.lte=%N&sort_by=popularity.desc&vote_count.gte=5" NOTV },
  { "New episodes", "air", NULL, "/discover/tv?air_date.gte=%1&air_date.lte=%N&sort_by=popularity.desc&vote_count.gte=20" NOTV },
  { "Top rated", "top", "/discover/movie?sort_by=vote_average.desc&vote_count.gte=1500", "/discover/tv?sort_by=vote_average.desc&vote_count.gte=400" NOTV },
  { "Hidden gems", "gem", "/discover/movie?sort_by=vote_average.desc&vote_average.gte=7.2&vote_count.gte=150&vote_count.lte=1200",
    "/discover/tv?sort_by=vote_average.desc&vote_average.gte=7.6&vote_count.gte=60&vote_count.lte=500" NOTV },
};
/* TMDB's genres, by the English names the app already translates */
static const struct { const char *name; int id; } MG[] = { { "Action", 28 }, { "Adventure", 12 }, { "Animation", 16 }, { "Comedy", 35 }, { "Crime", 80 },
  { "Documentary", 99 }, { "Drama", 18 }, { "Family", 10751 }, { "Fantasy", 14 }, { "History", 36 }, { "Horror", 27 }, { "Music", 10402 }, { "Mystery", 9648 },
  { "Romance", 10749 }, { "Sci-Fi", 878 }, { "Thriller", 53 }, { "War", 10752 }, { "Western", 37 } };
static const struct { const char *name; int id; } SG[] = { { "Action", 10759 }, { "Animation", 16 }, { "Comedy", 35 }, { "Crime", 80 }, { "Documentary", 99 },
  { "Drama", 18 }, { "Family", 10751 }, { "Kids", 10762 }, { "Mystery", 9648 }, { "Reality-TV", 10764 }, { "Sci-Fi", 10765 }, { "War", 10768 }, { "Western", 37 } };

static void expand(const char *q, char *out, size_t cap) {
  char lang[24], reg[8], n0[16], d7[16], d60[16], d120[16]; place(lang, sizeof lang, reg, sizeof reg);
  ago(n0, sizeof n0, 0); ago(d7, sizeof d7, 7); ago(d60, sizeof d60, 60); ago(d120, sizeof d120, 120);
  size_t n = 0; for (const char *p = q; *p && n + 24 < cap; p++) {
    const char *v = NULL; if (p[0] == '%') v = p[1] == 'N' ? n0 : p[1] == '1' ? d7 : p[1] == '2' ? d60 : p[1] == '4' ? d120 : p[1] == 'R' ? reg : NULL;
    if (v) { n += (size_t)snprintf(out + n, cap - n, "%s", v); p++; } else out[n++] = *p; }
  out[n] = 0; snprintf(out + n, cap - n, "%slanguage=%s", out[n - 1] == '?' ? "" : "&", lang);
}

int tdisc_cats(const char *type, CatalogDef *out, int max) {
  int tv = !strcmp(type, "series"), n = 0;
  for (size_t i = 0; i < sizeof CATS / sizeof CATS[0] && n < max; i++) { const char *q = tv ? CATS[i].tv : CATS[i].movie; if (!q) continue;
    char path[480]; expand(q, path, sizeof path); CatalogDef *d = &out[n++]; memset(d, 0, sizeof *d);
    snprintf(d->title, sizeof d->title, "%s", T(CATS[i].title)); snprintf(d->type, sizeof d->type, "%s", type);
    snprintf(d->url, sizeof d->url, "tmdbk:%s", path); snprintf(d->path, sizeof d->path, "%s", d->url); snprintf(d->key, sizeof d->key, "td_%c%s", tv ? 's' : 'm', CATS[i].key); d->skip = 1; }
  return n;
}
static int ours(const CatalogDef *d) { return !strncmp(d->key, "td_", 3) && strstr(d->path, "/discover/"); } /* (the trending lists take no genre) */
int tdisc_genres(const CatalogDef *d, char (*out)[48], int max) {
  if (!ours(d)) return 0; int tv = d->key[3] == 's', n = 0;
  if (tv) for (size_t i = 0; i < sizeof SG / sizeof SG[0] && n < max; i++) snprintf(out[n++], 48, "%s", T(SG[i].name));
  else for (size_t i = 0; i < sizeof MG / sizeof MG[0] && n < max; i++) snprintf(out[n++], 48, "%s", T(MG[i].name));
  return n;
}
void tdisc_with_genre(CatalogDef *d, const char *genre) {
  snprintf(d->url, sizeof d->url, "%s", d->path); if (!ours(d) || !genre || !*genre) return;
  int tv = d->key[3] == 's', id = 0;
  if (tv) { for (size_t i = 0; i < sizeof SG / sizeof SG[0] && !id; i++) if (!strcmp(T(SG[i].name), genre)) id = SG[i].id; }
  else for (size_t i = 0; i < sizeof MG / sizeof MG[0] && !id; i++) if (!strcmp(T(MG[i].name), genre)) id = MG[i].id;
  if (id) { size_t l = strlen(d->url); snprintf(d->url + l, sizeof d->url - l, "&with_genres=%d", id); }
}
