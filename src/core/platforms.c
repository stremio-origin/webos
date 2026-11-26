/* the streaming platforms. A row of round badges on Home (each platform's own icon, as TMDB lists the
   ones of this country) and a page for each, as if the platform's own app were open: its Top 10 films and series of
   the moment, what is new, the best rated and its genres, every list asked of TMDB for that platform in this country.
   The titles open as any other (their IMDb id first), and the addons play them. */
#include "core/platforms.h"
#include "core/kids.h"
#include "core/tmdb.h"
#include "core/i18n.h"
#include "core/app.h"
#include "platform/plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static PlatformParams cur;

const PlatformParams *platform_current(void) { return &cur; }

static void place(char *lang, size_t lc, char *reg, size_t rc) {
  const char *l = plat_ui_locale(); if (!l || !*l) l = "en-US";
  snprintf(lang, lc, "%s", l); char *u = strchr(lang, '_'); if (u) *u = '-';
  const char *d = strchr(lang, '-'); snprintf(reg, rc, "%s", d && strlen(d + 1) == 2 ? d + 1 : "US");
}
static void ago(char *out, size_t cap, int days) { time_t t = time(NULL) - (time_t)days * 86400; struct tm tm; gmtime_r(&t, &tm); strftime(out, cap, "%Y-%m-%d", &tm); }

static void def(CatalogDef *d, const char *title, const char *type, const char *path, const char *key, int skip, int top10) {
  memset(d, 0, sizeof *d); snprintf(d->title, sizeof d->title, "%s", title); snprintf(d->type, sizeof d->type, "%s", type);
  snprintf(d->url, sizeof d->url, "tmdbk:%s", path); snprintf(d->path, sizeof d->path, "%s", d->url); snprintf(d->key, sizeof d->key, "%s", key); d->skip = skip; d->top10 = top10; }

int platforms_row(CatalogDef *out) {
  if (!tmdb_has_key()) return 0;
  char lang[24], reg[8], path[200]; place(lang, sizeof lang, reg, sizeof reg);
  snprintf(path, sizeof path, "p/watch/providers/movie?watch_region=%s&language=%s", reg, lang);
  def(out, T("Platforms"), "platform", path, "plat_row", 0, 0); out->round = 1; return 1;
}

/* the premieres: films reaching streaming in this country in the next weeks, and new series (their day on each card) */
int premieres_rows(CatalogDef *out, int max) {
  if (!tmdb_has_key() || max < 2) return 0;
  char lang[24], reg[8], a[16], b[16], c[16], path[480]; place(lang, sizeof lang, reg, sizeof reg); ago(a, sizeof a, 2); ago(b, sizeof b, -40); ago(c, sizeof c, 60);
  snprintf(path, sizeof path, "u/discover/movie?region=%s&with_release_type=4&release_date.gte=%s&release_date.lte=%s&sort_by=popularity.desc&language=%s", reg, a, b, lang);
  def(&out[0], T("Coming to streaming"), "movie", path, "prem_m", 0, 0); out[0].wide = 1;
  snprintf(path, sizeof path, "u/discover/tv?first_air_date.gte=%s&first_air_date.lte=%s&sort_by=popularity.desc&without_genres=10763,10767,10764&language=%s", a, b, lang);
  def(&out[1], T("New series premieres"), "series", path, "prem_s", 0, 0); out[1].wide = 1;
  (void)c; return 2;
}

int platform_defs(CatalogDef *out, int max) {
  if (!cur.id || !tmdb_has_key()) return 0;
  char lang[24], reg[8], now[16], d90[16], d30[16]; place(lang, sizeof lang, reg, sizeof reg); ago(now, sizeof now, 0); ago(d90, sizeof d90, 120); ago(d30, sizeof d30, 30);
  char base[96]; snprintf(base, sizeof base, "with_watch_providers=%d&watch_region=%s&language=%s", cur.id, reg, lang);
  const char *P = cur.name; int n = 0; char path[480], key[48];
#define ROW(title, type, k, skip, top) do { if (n < max) { snprintf(key, sizeof key, "p%d_%s", cur.id, k); def(&out[n++], title, type, path, key, skip, top); } } while (0)
  snprintf(path, sizeof path, "/discover/movie?%s&sort_by=popularity.desc&vote_count.gte=20", base); ROW(T1("Top 10 films on {p}", "p", P), "movie", "tm", 0, 1);
  snprintf(path, sizeof path, "/discover/tv?%s&sort_by=popularity.desc&vote_count.gte=20&without_genres=10763,10767", base); ROW(T1("Top 10 series on {p}", "p", P), "series", "ts", 0, 1);
  snprintf(path, sizeof path, "/discover/movie?%s&sort_by=popularity.desc&primary_release_date.gte=%s&primary_release_date.lte=%s", base, d90, now); ROW(T("New films"), "movie", "nm", 1, 0);
  snprintf(path, sizeof path, "/discover/tv?%s&sort_by=popularity.desc&air_date.gte=%s&air_date.lte=%s&without_genres=10763,10767", base, d30, now); ROW(T("New episodes"), "series", "ns", 1, 0);
  snprintf(path, sizeof path, "/discover/movie?%s&sort_by=vote_average.desc&vote_count.gte=500", base); ROW(T("Best-rated films"), "movie", "rm", 1, 0);
  snprintf(path, sizeof path, "/discover/tv?%s&sort_by=vote_average.desc&vote_count.gte=300", base); ROW(T("Best-rated series"), "series", "rs", 1, 0);
  static const struct { const char *name; int id; } MG[] = { { "Action", 28 }, { "Comedy", 35 }, { "Drama", 18 }, { "Thriller", 53 }, { "Sci-Fi", 878 }, { "Horror", 27 },
    { "Romance", 10749 }, { "Animation", 16 }, { "Family", 10751 }, { "Documentary", 99 } };
  static const struct { const char *name; int id; } SG[] = { { "Crime", 80 }, { "Comedy", 35 }, { "Drama", 18 }, { "Sci-Fi", 10765 }, { "Action", 10759 }, { "Animation", 16 }, { "Documentary", 99 } };
  for (size_t i = 0; i < sizeof MG / sizeof MG[0] && n < max; i++) { char t[96]; snprintf(t, sizeof t, "%s · %s", T(MG[i].name), T("Movies"));
    snprintf(path, sizeof path, "/discover/movie?%s&with_genres=%d&sort_by=popularity.desc&vote_count.gte=30", base, MG[i].id); snprintf(key, sizeof key, "p%d_gm%d", cur.id, MG[i].id); def(&out[n++], t, "movie", path, key, 1, 0); }
  for (size_t i = 0; i < sizeof SG / sizeof SG[0] && n < max; i++) { char t[96]; snprintf(t, sizeof t, "%s · %s", T(SG[i].name), T("Series"));
    snprintf(path, sizeof path, "/discover/tv?%s&with_genres=%d&sort_by=popularity.desc&vote_count.gte=20", base, SG[i].id); snprintf(key, sizeof key, "p%d_gs%d", cur.id, SG[i].id); def(&out[n++], t, "series", path, key, 1, 0); }
  return n;
#undef ROW
}

int platform_open(const Item *it) {
  if (strncmp(it->id, "plat:", 5)) return 0;
  PlatformParams p; memset(&p, 0, sizeof p); p.id = atoi(it->id + 5); snprintf(p.name, sizeof p.name, "%s", it->name); snprintf(p.logo, sizeof p.logo, "%s", it->logo[0] ? it->logo : it->poster);
  app_go(SCR_PLATFORM, &p, 0); return 1;
}
int platform_enter(const PlatformParams *p) { /* 1 when it is another platform than last time (its rows made anew) */
  if (!p) return 0; int other = p->id != cur.id; cur = *p; return other; }
