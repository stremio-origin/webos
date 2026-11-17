/* tests/traktrows: Trakt's rows on Home (src/core/traktrows.c) against a stand-in Trakt: which rows Home gets, what
   each list asks, and the answers made an addon's (one card a title, IMDb ids only). tests/traktrows/run.sh */
#include "core/traktrows.h"
#include "core/scrobble.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- stand-ins ---- */
static int state = TRAKT_LINKED; static char rowsSetting[8] = "true"; static cJSON *addons;
int scrobble_state(void) { return state; }
const char *store_get(const char *k, const char *d) { return !strcmp(k, "traktRows") ? rowsSetting : d; }
cJSON *sb_addons(void) { return addons; }
int sb_is_imdb(const char *id) { return id && id[0] == 't' && id[1] == 't'; }
const char *T(const char *s) { return s; }
static char cacheKeys[8][80]; static int ncache;
void cache_write(const char *key, const unsigned char *d, size_t n) { (void)d; (void)n; if (ncache < 8) snprintf(cacheKeys[ncache++], 80, "%s", key); }
cJSON *net_json(const unsigned char *b) { return b ? cJSON_Parse((const char *)b) : NULL; }
void net_json_free(cJSON *j) { cJSON_Delete(j); }
static char asked[300]; static NetCb pend; static void *pendUd;
int scrobble_get(const char *path, NetCb cb, void *ud) { if (state != TRAKT_LINKED) return 0; snprintf(asked, sizeof asked, "%s", path); pend = cb; pendUd = ud; return 1; }
/* what Home's catalog code gets */
static int gotSt; static char got[4000];
static void home_cb(int st, const unsigned char *b, size_t n, void *ud) { (void)n; (void)ud; gotSt = st; snprintf(got, sizeof got, "%s", b ? (const char *)b : ""); }
static void answer(int st, const char *body) { NetCb c = pend; pend = NULL; c(st, (const unsigned char *)body, body ? strlen(body) : 0, pendUd); }

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static int metas_n(void) { cJSON *j = cJSON_Parse(got); int n = cJSON_GetArraySize(cJSON_GetObjectItem(j, "metas")); cJSON_Delete(j); return n; }
static const char *meta_id(int i) { static char b[32]; cJSON *j = cJSON_Parse(got); cJSON *m = cJSON_GetArrayItem(cJSON_GetObjectItem(j, "metas"), i); snprintf(b, sizeof b, "%s", m ? cJSON_GetObjectItem(m, "id")->valuestring : ""); cJSON_Delete(j); return b; }

int main(void) {
  CatalogDef d[8];
  /* which rows */
  int n = traktrows_defs(d, 8); CHECK(n == 6, "six rows when connected (%d)", n);
  CHECK(!strcmp(d[0].title, "Trakt History · Movies") && !strcmp(d[1].title, "Trakt History · Series") && !strcmp(d[4].title, "Trakt Recommendations · Movies"), "the official addon's names: %s / %s", d[0].title, d[4].title);
  CHECK(!strcmp(d[2].url, "trakt:w/movie") && !strcmp(d[2].type, "movie") && d[2].skip == 0 && traktrows_is(d[2].url) && !traktrows_is("https://x/catalog/movie/top.json"), "a row's address and type");
  CHECK(traktrows_defs(d, 2) == 2, "never more than asked");
  state = TRAKT_OFF; CHECK(traktrows_defs(d, 8) == 0, "not connected: none"); state = TRAKT_RELINK; CHECK(traktrows_defs(d, 8) == 0, "to connect again: none"); state = TRAKT_LINKED;
  snprintf(rowsSetting, sizeof rowsSetting, "false"); CHECK(traktrows_defs(d, 8) == 0, "switched off: none"); snprintf(rowsSetting, sizeof rowsSetting, "true");
  addons = cJSON_Parse("[{\"transportUrl\":\"https://v3-cinemeta.strem.io/manifest.json\"},{\"transportUrl\":\"https://www.strem.io/trakt/addon/0123456789abcdef01234567/manifest.json\"}]");
  CHECK(traktrows_official() && traktrows_defs(d, 8) == 0, "the official Trakt addon in the account: its rows, never ours");
  cJSON_Delete(addons); addons = cJSON_Parse("[{\"transportUrl\":\"https://v3-cinemeta.strem.io/manifest.json\"}]"); CHECK(!traktrows_official() && traktrows_defs(d, 8) == 6, "without it: ours");

  /* history: one card a title, newest first, other ids left out */
  traktrows_fetch("trakt:h/movie", home_cb, NULL); CHECK(strstr(asked, "/sync/history/movies?") != NULL, "history of films asked: %s", asked);
  answer(200, "[{\"type\":\"movie\",\"movie\":{\"title\":\"Film A\",\"year\":2001,\"ids\":{\"imdb\":\"tt0000001\"}}},{\"type\":\"movie\",\"movie\":{\"title\":\"Film B\",\"ids\":{\"tmdb\":5}}},"
    "{\"type\":\"movie\",\"movie\":{\"title\":\"Film A\",\"ids\":{\"imdb\":\"tt0000001\"}}},{\"type\":\"movie\",\"movie\":{\"title\":\"Film C\",\"ids\":{\"imdb\":\"tt0000003\"}}}]");
  CHECK(gotSt == 200 && metas_n() == 2 && !strcmp(meta_id(0), "tt0000001") && !strcmp(meta_id(1), "tt0000003"), "history: one a title, in order, IMDb only (%d: %s)", metas_n(), got);
  CHECK(strstr(got, "\"type\":\"movie\"") && strstr(got, "\"name\":\"Film A\"") && strstr(got, "\"releaseInfo\":\"2001\"") && strstr(got, "\"cacheMaxAge\":300"), "an addon's shape: %s", got);
  traktrows_fetch("trakt:h/series", home_cb, NULL); CHECK(strstr(asked, "/sync/history/shows?") != NULL, "history of shows asked");
  answer(200, "[{\"type\":\"episode\",\"episode\":{\"season\":1,\"number\":2},\"show\":{\"title\":\"Show A\",\"ids\":{\"imdb\":\"tt0000010\"}}},{\"type\":\"episode\",\"show\":{\"title\":\"Show A\",\"ids\":{\"imdb\":\"tt0000010\"}}},{\"type\":\"episode\",\"show\":{\"title\":\"Show B\",\"ids\":{\"imdb\":\"tt0000011\"}}}]");
  CHECK(metas_n() == 2 && !strcmp(meta_id(0), "tt0000010") && strstr(got, "\"type\":\"series\""), "a show once for all its episodes: %s", got);
  /* watchlist and recommendations */
  traktrows_fetch("trakt:w/series", home_cb, NULL); CHECK(strstr(asked, "/sync/watchlist/shows/added/desc") != NULL, "watchlist, newest added first: %s", asked);
  answer(200, "[{\"rank\":1,\"type\":\"show\",\"show\":{\"title\":\"Show C\",\"ids\":{\"imdb\":\"tt0000020\"}}}]"); CHECK(metas_n() == 1 && !strcmp(meta_id(0), "tt0000020"), "watchlist entry: %s", got);
  traktrows_fetch("trakt:r/movie", home_cb, NULL); CHECK(strstr(asked, "/recommendations/movies?") != NULL, "recommendations asked: %s", asked);
  answer(200, "[{\"title\":\"Film D\",\"year\":2010,\"ids\":{\"imdb\":\"tt0000030\"}},{\"title\":\"Film E\",\"ids\":{\"imdb\":\"\"}}]"); CHECK(metas_n() == 1 && !strcmp(meta_id(0), "tt0000030"), "recommendations: the films themselves: %s", got);
  /* what goes wrong */
  traktrows_fetch("trakt:w/movie", home_cb, NULL); answer(0, NULL); CHECK(gotSt == 0 && !got[0], "no answer: no body (the kept copy stays)");
  traktrows_fetch("trakt:w/movie", home_cb, NULL); answer(200, "<html>"); CHECK(gotSt == 502 && !got[0], "not a list: no body");
  traktrows_fetch("trakt:w/movie", home_cb, NULL); answer(200, "[]"); CHECK(gotSt == 200 && metas_n() == 0, "an empty list: an empty row");
  traktrows_fetch("trakt:x/movie", home_cb, NULL); CHECK(gotSt == 404, "an unknown list");
  state = TRAKT_OFF; gotSt = -1; traktrows_fetch("trakt:h/movie", home_cb, NULL); CHECK(gotSt == 0, "not connected: answered at once, nothing asked"); state = TRAKT_LINKED;
  traktrows_forget(); CHECK(ncache == 6 && !strcmp(cacheKeys[0], "cat:trakt:h/movie"), "a new connection: the six kept copies cleared (%d, %s)", ncache, cacheKeys[0]);
  cJSON_Delete(addons);
  printf(fails ? "%d failed\n" : "all ok\n", fails); return fails ? 1 : 0;
}
