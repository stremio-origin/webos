#include "core/rating.h"
#include "core/tmdb.h"
#include "core/net.h"
#include "core/i18n.h"
#include "core/json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

/* the country whose rating is shown, by the app's language; films without a key: which Wikidata rating */
static const char *RATE_CC[][2] = { { "en", "US" }, { "de", "DE" }, { "fr", "FR" }, { "es", "ES" }, { "it", "IT" }, { "nl", "NL" }, { "pl", "PL" }, { "hu", "HU" }, { "pt", "BR" }, { "ru", "RU" }, { "tr", "TR" }, { "ar", "AE" } };
static const char *RATE_WD[][2] = { { "de", "P1981" }, { "fr", "P2758" }, { "pt", "P3216" } }; /* FSK, CNC, ClassInd; MPA (P1657) otherwise */
#define KEEP 120

typedef struct { char k[40]; Rating r; } Kept;
static Kept kept[KEEP]; static int keptN, keptNext;
typedef struct { char type[8], id[24], lang[4], cc[4], k[40]; RatingCb cb; void *ud; } Ask;

static void keep(const char *k, const Rating *r) {
  for (int i = 0; i < keptN; i++) if (!strcmp(kept[i].k, k)) { kept[i].r = *r; return; }
  Kept *e = &kept[keptNext]; keptNext = (keptNext + 1) % KEEP; if (keptN < KEEP) keptN++;
  snprintf(e->k, sizeof e->k, "%s", k); e->r = *r;
}
static void done(Ask *a, const Rating *r) { Rating z; if (!r) { memset(&z, 0, sizeof z); r = &z; } keep(a->k, r); a->cb(r, a->ud); free(a); }
static void on_wiki(int st, const unsigned char *body, size_t len, void *ud) {
  Ask *a = ud; Rating r; memset(&r, 0, sizeof r);
  cJSON *j = st == 200 ? net_json(body) : NULL;
  const cJSON *b0 = cJSON_GetArrayItem(js_arr(js_obj(j, "results"), "bindings"), 0);
  snprintf(r.rating, sizeof r.rating, "%s", js_str(js_obj(b0, "l"), "value", ""));
  net_json_free(j);
  const char *l = r.rating; if (strcasestr(l, "no age restriction") || strcasestr(l, "tous publics") || !strcasecmp(l, "L") || !strcasecmp(l, "AL")) snprintf(r.rating, sizeof r.rating, "ALL");
  if (st == -1) { free(a); return; }
  done(a, &r);
}
static void wiki(Ask *a) {
  if (strcmp(a->type, "movie")) { done(a, NULL); return; }
  const char *prop = "P1657"; for (size_t i = 0; i < sizeof RATE_WD / sizeof RATE_WD[0]; i++) if (!strcmp(RATE_WD[i][0], a->lang)) prop = RATE_WD[i][1];
  char sp[512], enc[1600], url[1800]; static const char *H = "0123456789ABCDEF";
  snprintf(sp, sizeof sp, "SELECT ?l WHERE { ?f wdt:P345 \"%s\" . ?f wdt:%s ?v . ?v rdfs:label ?l . FILTER(LANG(?l) = \"en\") } LIMIT 1", a->id, prop);
  size_t o = 0; for (const unsigned char *s = (const unsigned char *)sp; *s && o + 4 < sizeof enc; s++) { if (isalnum(*s) || *s == '-' || *s == '_' || *s == '.' || *s == '~') enc[o++] = (char)*s; else { enc[o++] = '%'; enc[o++] = H[*s >> 4]; enc[o++] = H[*s & 15]; } }
  enc[o] = 0; snprintf(url, sizeof url, "https://query.wikidata.org/sparql?format=json&query=%s", enc);
  net_get(url, on_wiki, a);
}
static void fill(Rating *r, const char *rating, const cJSON *descs) {
  memset(r, 0, sizeof *r); snprintf(r->rating, sizeof r->rating, "%s", rating);
  const cJSON *d; cJSON_ArrayForEach(d, descs) if (cJSON_IsString(d) && r->ndesc < 4) snprintf(r->desc[r->ndesc++], sizeof r->desc[0], "%s", d->valuestring);
}
/* the entry for the country (the last one listed for it), else the US one */
static void pick(const cJSON *list, const char *cc, const cJSON **a, const cJSON **us) {
  const cJSON *x; *a = *us = NULL;
  cJSON_ArrayForEach(x, list) { const char *c = js_str(x, "iso_3166_1", ""); if (!strcmp(c, cc)) *a = x; if (!strcmp(c, "US")) *us = x; }
}
static void on_tv(int st, const unsigned char *body, size_t len, void *ud) {
  Ask *a = ud; if (st == -1) { free(a); return; }
  cJSON *j = st == 200 ? net_json(body) : NULL; const cJSON *c, *us, *x = NULL; pick(js_arr(j, "results"), a->cc, &c, &us);
  if (c && *js_str(c, "rating", "")) x = c; else if (us && *js_str(us, "rating", "")) x = us;
  Rating r; if (x) fill(&r, js_str(x, "rating", ""), js_arr(x, "descriptors"));
  net_json_free(j); done(a, x ? &r : NULL);
}
static const cJSON *certified(const cJSON *x) { const cJSON *d; cJSON_ArrayForEach(d, js_arr(x, "release_dates")) if (*js_str(d, "certification", "")) return d; return NULL; }
static void on_movie(int st, const unsigned char *body, size_t len, void *ud) {
  Ask *a = ud; if (st == -1) { free(a); return; }
  cJSON *j = st == 200 ? net_json(body) : NULL; const cJSON *c, *us, *d; pick(js_arr(j, "results"), a->cc, &c, &us);
  d = c ? certified(c) : NULL; if (!d && us) d = certified(us);
  Rating r; if (d) fill(&r, js_str(d, "certification", ""), js_arr(d, "descriptors"));
  net_json_free(j); if (d) done(a, &r); else wiki(a);
}
static void on_find(int st, const unsigned char *body, size_t len, void *ud) {
  Ask *a = ud; if (st == -1) { free(a); return; }
  cJSON *j = st == 200 ? net_json(body) : NULL;
  const cJSON *mv = cJSON_GetArrayItem(js_arr(j, "movie_results"), 0), *tv = cJSON_GetArrayItem(js_arr(j, "tv_results"), 0);
  char p[96]; unsigned ok = 0;
  if (tv) { snprintf(p, sizeof p, "/tv/%d/content_ratings", (int)js_num(tv, "id", 0)); ok = tmdb_request(p, on_tv, a); }
  else if (mv) { snprintf(p, sizeof p, "/movie/%d/release_dates", (int)js_num(mv, "id", 0)); ok = tmdb_request(p, on_movie, a); }
  net_json_free(j);
  if (!ok) wiki(a);
}
void rating_get(const char *type, const char *id, RatingCb cb, void *ud) {
  Rating z; memset(&z, 0, sizeof z);
  if (!id || strncmp(id, "tt", 2) || !id[2] || strspn(id + 2, "0123456789") != strlen(id + 2)) { cb(&z, ud); return; }
  Ask *a = calloc(1, sizeof *a); a->cb = cb; a->ud = ud;
  snprintf(a->type, sizeof a->type, "%s", type && !strcmp(type, "series") ? "series" : "movie"); snprintf(a->id, sizeof a->id, "%s", id);
  snprintf(a->lang, sizeof a->lang, "%.2s", i18n_lang()); snprintf(a->k, sizeof a->k, "r%s%s", a->id, a->lang);
  snprintf(a->cc, sizeof a->cc, "US"); for (size_t i = 0; i < sizeof RATE_CC / sizeof RATE_CC[0]; i++) if (!strcmp(RATE_CC[i][0], a->lang)) snprintf(a->cc, sizeof a->cc, "%s", RATE_CC[i][1]);
  for (int i = 0; i < keptN; i++) if (!strcmp(kept[i].k, a->k)) { Rating r = kept[i].r; free(a); cb(&r, ud); return; }
  char p[64]; snprintf(p, sizeof p, "/find/%s?external_source=imdb_id", a->id);
  if (!tmdb_request(p, on_find, a)) wiki(a);
}
/* a certification as an age: digits as they are (12, 16), letters by what they mean in their country */
int rating_age(const Rating *r) {
  const char *s = r ? r->rating : ""; if (!*s) return -1;
  if (strspn(s, "0123456789") == strlen(s)) return atoi(s);
  if (!strcasecmp(s, "ALL")) return 0;
  static const struct { const char *c; int age; } M[] = { { "G", 0 }, { "TV-G", 0 }, { "TV-Y", 0 }, { "U", 0 }, { "L", 0 }, { "AL", 0 }, { "TP", 0 }, { "A", 0 }, { "T", 0 },
    { "PG", 7 }, { "TV-PG", 7 }, { "TV-Y7", 7 }, { "TV-Y7-FV", 7 }, { "PG-13", 13 }, { "TV-14", 13 }, { "12A", 12 }, { "R", 16 }, { "NC-17", 18 }, { "TV-MA", 18 }, { "X", 18 } };
  for (size_t i = 0; i < sizeof M / sizeof M[0]; i++) if (!strcasecmp(s, M[i].c)) return M[i].age;
  int n = 0; for (const char *p = s; *p; p++) if (*p >= '0' && *p <= '9') { n = atoi(p); break; } /* "-12", "12+", "M/16": its number */
  return n > 0 && n <= 21 ? n : -1;
}
