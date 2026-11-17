/* Trakt's rows on Home (see traktrows.h) */
#include "core/traktrows.h"
#include "core/scrobble.h"
#include "core/store.h"
#include "core/i18n.h"
#include "core/json.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the six: the list (h history, w watchlist, r recommendations), the type, the title (the official addon's names) */
static const struct { const char *list, *type, *title, *key; } ROWS[6] = {
  { "h", "movie", "Trakt History", "trakt_hm" }, { "h", "series", "Trakt History", "trakt_hs" },
  { "w", "movie", "Trakt Watchlist", "trakt_wm" }, { "w", "series", "Trakt Watchlist", "trakt_ws" },
  { "r", "movie", "Trakt Recommendations", "trakt_rm" }, { "r", "series", "Trakt Recommendations", "trakt_rs" } };
#define PFX "trakt:"
int traktrows_is(const char *url) { return url && !strncmp(url, PFX, strlen(PFX)); }
int traktrows_official(void) {
  cJSON *a = sb_addons(); const cJSON *ad; if (!a) return 0;
  cJSON_ArrayForEach(ad, a) { const char *u = js_str(ad, "transportUrl", ""); if (strstr(u, "strem.io/trakt/addon/")) return 1; }
  return 0; }
static int wanted(void) { return scrobble_state() == TRAKT_LINKED && strcmp(store_get("traktRows", "true"), "false") && !traktrows_official(); }
int traktrows_defs(CatalogDef *out, int max) {
  if (!wanted()) return 0; int n = 0;
  for (int i = 0; i < 6 && n < max; i++) { CatalogDef *d = &out[n++]; memset(d, 0, sizeof *d);
    snprintf(d->title, sizeof d->title, "%s · %s", ROWS[i].title, T(!strcmp(ROWS[i].type, "movie") ? "Movies" : "Series"));
    snprintf(d->type, sizeof d->type, "%s", ROWS[i].type); snprintf(d->path, sizeof d->path, PFX "%s/%s", ROWS[i].list, ROWS[i].type);
    snprintf(d->url, sizeof d->url, "%s", d->path); snprintf(d->key, sizeof d->key, "%s", ROWS[i].key); } /* (no paging: skip 0) */
  return n; }
void traktrows_forget(void) { for (int i = 0; i < 6; i++) { char k[64]; snprintf(k, sizeof k, "cat:" PFX "%s/%s", ROWS[i].list, ROWS[i].type); cache_write(k, (const unsigned char *)"[]", 2); } }

/* Trakt's answer made an addon's: one entry a title (history repeats it for every play and every episode), its IMDb id
   (what the cards, the title page and the streams go by: titles Trakt knows only by another id are left out) */
typedef struct { NetCb cb; void *ud; char list, type; } Ask;
static void on_answer(int st, const unsigned char *b, size_t n, void *ud) {
  (void)n; Ask *k = ud; cJSON *j = st == 200 ? net_json(b) : NULL;
  if (!cJSON_IsArray(j)) { net_json_free(j); k->cb(st == 200 ? 502 : st, NULL, 0, k->ud); free(k); return; } /* (not a list: kept copy stays) */
  cJSON *o = cJSON_CreateObject(), *metas = cJSON_AddArrayToObject(o, "metas"); cJSON_AddNumberToObject(o, "cacheMaxAge", 300); /* (five minutes: Home made again soon after asks no more) */
  const char *tp = k->type == 'm' ? "movie" : "series", *field = k->type == 'm' ? "movie" : "show"; int got = 0;
  const cJSON *e; cJSON_ArrayForEach(e, j) { if (got >= 60) break;
    const cJSON *m = k->list == 'r' ? e : js_obj(e, field); const char *id = js_str(js_obj(m, "ids"), "imdb", "");
    if (!sb_is_imdb(id)) continue; int dup = 0; const cJSON *x; cJSON_ArrayForEach(x, metas) if (!strcmp(js_str(x, "id", ""), id)) { dup = 1; break; } if (dup) continue;
    cJSON *mt = cJSON_CreateObject(); cJSON_AddStringToObject(mt, "id", id); cJSON_AddStringToObject(mt, "type", tp); cJSON_AddStringToObject(mt, "name", js_str(m, "title", ""));
    int y = (int)js_num(m, "year", 0); if (y > 1800) { char ys[8]; snprintf(ys, sizeof ys, "%d", y); cJSON_AddStringToObject(mt, "releaseInfo", ys); }
    cJSON_AddItemToArray(metas, mt); got++; }
  net_json_free(j); char *s = cJSON_PrintUnformatted(o); cJSON_Delete(o);
  k->cb(200, (const unsigned char *)(s ? s : "{\"metas\":[]}"), s ? strlen(s) : 12, k->ud); free(s); free(k); }
void traktrows_fetch(const char *url, NetCb cb, void *ud) {
  char list = 0, type = 0; const char *p = traktrows_is(url) ? url + strlen(PFX) : "";
  if (p[0] && p[1] == '/') { list = p[0]; type = !strcmp(p + 2, "movie") ? 'm' : !strcmp(p + 2, "series") ? 's' : 0; }
  if (!type || !strchr("hwr", list)) { cb(404, NULL, 0, ud); return; }
  const char *t = type == 'm' ? "movies" : "shows"; char path[200];
  if (list == 'h') snprintf(path, sizeof path, "/sync/history/%s?page=1&limit=100", t);           /* most recent first */
  else if (list == 'w') snprintf(path, sizeof path, "/sync/watchlist/%s/added/desc?page=1&limit=60", t); /* newest added first */
  else snprintf(path, sizeof path, "/recommendations/%s?limit=40&ignore_collected=true", t);
  Ask *k = calloc(1, sizeof *k); if (!k) { cb(0, NULL, 0, ud); return; } k->cb = cb; k->ud = ud; k->list = list; k->type = type;
  if (!scrobble_get(path, on_answer, k)) { free(k); cb(0, NULL, 0, ud); } }
