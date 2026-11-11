#include "core/stremio.h"
#include "core/net.h"
#include "core/store.h"
#include "core/json.h"
#include "core/i18n.h"
#include "core/addons.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <zlib.h>
#include <SDL2/SDL.h>

static unsigned libVer = 1;
static Uint32 libSyncT; static int libSyncing;

/* ---- small helpers ------------------------------------------------------------------------ */
static void urlenc(const char *s, char *out, size_t cap) {
  size_t o = 0; static const char *hex = "0123456789ABCDEF";
  for (; *s && o + 4 < cap; s++) { unsigned char c = (unsigned char)*s;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') out[o++] = (char)c;
    else { out[o++] = '%'; out[o++] = hex[c >> 4]; out[o++] = hex[c & 15]; } }
  out[o] = 0;
}
static void now_iso(char *out, size_t cap) { time_t t = time(NULL); struct tm tm; gmtime_r(&t, &tm); strftime(out, cap, "%Y-%m-%dT%H:%M:%S.000Z", &tm); }
static double tms(const cJSON *v) { /* _mtime as epoch ms or ISO text */
  if (cJSON_IsNumber(v)) return v->valuedouble;
  if (cJSON_IsString(v) && v->valuestring) { struct tm tm; memset(&tm, 0, sizeof tm); int ms = 0;
    if (sscanf(v->valuestring, "%d-%d-%dT%d:%d:%d.%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec, &ms) >= 6) {
      tm.tm_year -= 1900; tm.tm_mon -= 1; return (double)timegm(&tm) * 1000 + ms; } }
  return 0;
}
int sb_is_imdb(const char *id) { return id && id[0] == 't' && id[1] == 't'; }
static int digits_span(const char *s, const char *end, int *out) {
  if (!s || s >= end) return 0;
  long v = 0;
  for (const char *p = s; p < end; p++) {
    if (*p < '0' || *p > '9') return 0;
    v = v * 10 + (*p - '0');
    if (v > 999999) return 0;
  }
  if (out) *out = (int)v;
  return 1;
}
int sb_parse_video_id(const char *itemId, const char *vid, int *outSeason, int *outEp) {
  if (outSeason) *outSeason = -1;
  if (outEp) *outEp = -1;
  if (!vid || !*vid) return 0;
  if (itemId && *itemId) {
    size_t il = strlen(itemId);
    if (!strcmp(vid, itemId)) return 0;
    if (!strncmp(vid, itemId, il) && vid[il] == ':') {
      const char *rest = vid + il + 1, *end = rest + strlen(rest), *c = strchr(rest, ':');
      int s = -1, e = -1;
      if (c && !strchr(c + 1, ':') && digits_span(rest, c, &s) && digits_span(c + 1, end, &e)) {
        if (outSeason) *outSeason = s;
        if (outEp) *outEp = e;
        return 1;
      }
      if (!c && digits_span(rest, end, &e)) {
        if (outEp) *outEp = e;
        return 2;
      }
    }
  }
  const char *end = vid + strlen(vid), *cLast = strrchr(vid, ':');
  if (!cLast || cLast == vid) return 0;
  int e = -1;
  if (!digits_span(cLast + 1, end, &e)) return 0;
  const char *cPrev = cLast - 1;
  while (cPrev > vid && *cPrev != ':') cPrev--;
  if (*cPrev != ':') {
    /* single colon, e.g. "kitsu:123" with no episode suffix -> item id itself, not an episode */
    return 0;
  }
  /* check whether there is an earlier colon (3+ colons: "<ns>:<id>:<s>:<e>") or "tt<digits>:<s>:<e>" */
  const char *cFirst = strchr(vid, ':');
  int s = -1;
  if (cFirst < cPrev) {
    if (digits_span(cPrev + 1, cLast, &s)) {
      if (outSeason) *outSeason = s;
      if (outEp) *outEp = e;
      return 1;
    }
    return 0;
  }
  /* exactly two colons: "<p0>:<p1>:<e>" -> "tt12345:1:5" has season, "kitsu:43806:7" is season-less! */
  int tt = vid[0] == 't' && vid[1] == 't' && digits_span(vid + 2, cFirst, NULL);
  if (tt && digits_span(cFirst + 1, cLast, &s)) {
    if (outSeason) *outSeason = s;
    if (outEp) *outEp = e;
    return 1;
  }
  if (!tt) {
    if (outEp) *outEp = e;
    return 2;
  }
  return 0;
}
static void copy_str(char *dst, size_t cap, const char *s) { if (!cap) return; if (!s) { dst[0] = 0; return; } size_t n = strlen(s); if (n >= cap) n = cap - 1; memmove(dst, s, n); dst[n] = 0; }
/* picture addresses are rewritten for every card in every frame: a plain search (the library's strstr prepares each
   needle first, and printf copies), both measured on the TV's drawing thread */
static char *find_in(const char *h, const char *n) { size_t k = strlen(n); for (const char *p = strchr(h, n[0]); p; p = strchr(p + 1, n[0])) if (!strncmp(p, n, k)) return (char *)p; return NULL; }
static void replace_in(char *s, size_t cap, const char *from, const char *to) {
  char *p = find_in(s, from); if (!p) return; size_t fl = strlen(from), tl = strlen(to), rest = strlen(p + fl);
  if ((size_t)(p - s) + tl + rest + 1 > cap) return;
  memmove(p + tl, p + fl, rest + 1); memcpy(p, to, tl);
}

/* ---- account ------------------------------------------------------------------------------ */
int sb_init(void) { return 1; }
const char *sb_auth_key(void) { return store_get("authKey", NULL); }
int sb_signed_in(void) { return sb_auth_key() != NULL; }
const char *sb_user_email(void) { return js_str(store_node("user"), "email", ""); }
const char *sb_user_id(void) { return js_str(store_node("user"), "_id", ""); }
typedef struct { SbCb cb; void *ud; } CbBox;
static void on_login(int st, const unsigned char *b, size_t n, void *ud) {
  CbBox *x = ud; cJSON *j = st == 200 ? net_json(b) : NULL; const cJSON *r = js_obj(j, "result");
  const char *ak = js_str(r, "authKey", NULL);
  if (ak) { store_set("authKey", ak); const cJSON *u = js_obj(r, "user"); if (u) store_set_node("user", cJSON_Duplicate(u, 1)); store_flush(); x->cb(1, NULL, x->ud); }
  else { const cJSON *e = js_obj(j, "error"); x->cb(0, js_str(e, "message", st ? "Sign-in failed" : "No connection"), x->ud); }
  net_json_free(j); free(x);
}
void sb_login(const char *email, const char *password, SbCb cb, void *ud) {
  cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "email", email); cJSON_AddStringToObject(o, "password", password);
  char *body = cJSON_PrintUnformatted(o); cJSON_Delete(o);
  CbBox *x = malloc(sizeof *x); x->cb = cb; x->ud = ud;
  net_post_json(STREMIO_API "/api/login", body, on_login, x); free(body);
}
static void on_user(int st, const unsigned char *b, size_t n, void *ud) {
  CbBox *x = ud; cJSON *j = st == 200 ? net_json(b) : NULL; const cJSON *r = js_obj(j, "result");
  if (r) store_set_node("user", cJSON_Duplicate(r, 1));
  store_flush(); net_json_free(j); x->cb(1, NULL, x->ud); free(x);
}
void sb_finish_login(const char *ak, SbCb cb, void *ud) {
  store_set("authKey", ak);
  cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "authKey", ak); char *body = cJSON_PrintUnformatted(o); cJSON_Delete(o);
  CbBox *x = malloc(sizeof *x); x->cb = cb; x->ud = ud;
  net_post_json(STREMIO_API "/api/getUser", body, on_user, x); free(body);
}
void sb_logout(void) {
  const char *ak = sb_auth_key();
  if (ak) { cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "authKey", ak); char *body = cJSON_PrintUnformatted(o); cJSON_Delete(o); net_post_json(STREMIO_API "/api/logout", body, NULL, NULL); free(body); }
  sb_forget_local();
}
void sb_forget_local(void) { store_remove("authKey"); store_remove("user"); store_remove("addons"); store_remove("lib"); store_remove("streams"); store_flush(); lib_invalidate(); }
/* addons */
cJSON *sb_addons(void) { cJSON *a = store_node("addons"); return cJSON_IsArray(a) ? a : NULL; }
/* an addon can be switched off on this TV only (Settings > Addons): a local choice, never synced back to the
   account. Off, it gives no rows, metadata or streams here */
void sb_addon_key(const cJSON *a, char *out, size_t cap) { const char *id = js_str(js_obj(a, "manifest"), "id", NULL); copy_str(out, cap, id && *id ? id : js_str(a, "transportUrl", "")); }
int sb_addon_on(const cJSON *a) { char k[300], sk[320]; sb_addon_key(a, k, sizeof k); snprintf(sk, sizeof sk, "adOn_%s", k); return strcmp(store_get(sk, "true"), "false") != 0; }
typedef struct { void (*cb)(int, void *); void *ud; } ChBox;
static void on_addons(int st, const unsigned char *b, size_t n, void *ud) {
  ChBox *x = ud; cJSON *j = st == 200 ? net_json(b) : NULL; const cJSON *got = js_arr(js_obj(j, "result"), "addons");
  int changed = 0;
  if (got && cJSON_GetArraySize(got)) {
    char *next = cJSON_PrintUnformatted(got); cJSON *cur = sb_addons(); char *curs = cur ? cJSON_PrintUnformatted(cur) : NULL;
    if (!curs || strcmp(curs, next)) { store_set_node("addons", cJSON_Duplicate(got, 1)); store_flush(); changed = 1; }
    free(next); free(curs);
  }
  net_json_free(j); if (x->cb) x->cb(changed, x->ud); free(x);
}
void sb_sync_addons(void (*cb)(int, void *), void *ud) {
  const char *ak = sb_auth_key(); if (!ak) { if (cb) cb(0, ud); return; }
  cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "authKey", ak); cJSON_AddBoolToObject(o, "update", 1); char *body = cJSON_PrintUnformatted(o); cJSON_Delete(o);
  ChBox *x = malloc(sizeof *x); x->cb = cb; x->ud = ud; net_post_json(STREMIO_API "/api/addonCollectionGet", body, on_addons, x); free(body);
}
/* ---- catalogs ------------------------------------------------------------------------------ */
static const char *type_label(const char *t) {
  if (!strcasecmp(t, "movie") || !strcasecmp(t, "anime movies")) return T("Movies");
  if (!strcasecmp(t, "series") || !strcasecmp(t, "anime series")) return T("Series");
  if (!strcasecmp(t, "anime") || !strcasecmp(t, "other") || !strcasecmp(t, "tv") || !strcasecmp(t, "channel")) return "";
  return t;
}
static void cat_row_title(const char *cnm, const char *cid, const char *ctype, char *out, size_t cap) {
  const char *nm = (cnm && *cnm) ? cnm : cid;
  const char *suf = type_label(ctype);
  if (suf && *suf) snprintf(out, cap, "%s%s%s", nm, *nm ? " · " : "", suf);
  else snprintf(out, cap, "%s", nm);
}
static void ascii_low(const char *s, char *out, size_t cap) {
  copy_str(out, cap, s ? s : "");
  for (char *q = out; *q; q++) if (*q >= 'A' && *q <= 'Z') *q += 32;
}
static int has_anime_word(const char *low) {
  return strstr(low, "anime") || strstr(low, "kitsu") || strstr(low, "myanimelist") || strstr(low, "anisearch") || strstr(low, "anilist");
}
/* 1 = dedicated anime catalog/addon (shown first on Anime), 2 = anime service row inside a general addon (e.g. Crunchyroll), 0 = non-anime */
static int anime_cat_rank(const cJSON *ad, const cJSON *cat) {
  const cJSON *m = js_obj(ad, "manifest");
  char mlow[256], clow[320], tlow[64], ilow[128];
  snprintf(mlow, sizeof mlow, "%s %s", js_str(m, "id", ""), js_str(m, "name", ""));
  ascii_low(mlow, mlow, sizeof mlow);
  const char *ctype = js_str(cat, "type", ""), *cid = js_str(cat, "id", ""), *cnm = js_str(cat, "name", "");
  ascii_low(ctype, tlow, sizeof tlow); ascii_low(cid, ilow, sizeof ilow);
  snprintf(clow, sizeof clow, "%s %s %s", ctype, cid, cnm); ascii_low(clow, clow, sizeof clow);
  if (strstr(tlow, "anime") || has_anime_word(mlow) || has_anime_word(clow)) return 1;
  if (strstr(clow, "crunchyroll") || strstr(clow, "hidive") || strstr(clow, "funimation") ||
      !strcmp(ilow, "cru") || !strcmp(ilow, "streaming.cru")) return 2;
  return 0;
}
static void def_url(CatalogDef *d) { snprintf(d->url, sizeof d->url, "%s%s%s.json", d->path, d->extra[0] ? "/" : "", d->extra); }
static void def_set(CatalogDef *d, const char *title, const char *type, const char *path, const char *extra, int skip, const char *key) {
  memset(d, 0, sizeof *d); copy_str(d->title, sizeof d->title, title); copy_str(d->type, sizeof d->type, type); copy_str(d->path, sizeof d->path, path);
  copy_str(d->extra, sizeof d->extra, extra ? extra : ""); d->skip = skip; copy_str(d->key, sizeof d->key, key); def_url(d);
}
/* every installed non-anime catalog a board can show, in addon order (as the official board): one asked with nothing chosen,
   or with the first option of each required property (Cinemeta's "year" with this year); never one that needs a
   search or a list of ids. Anime catalogs live exclusively on SCR_ANIME. from: how many to pass over first */
static int board_defs(CatalogDef *out, int max, int from) {
  int n = 0, seen = 0; cJSON *a = sb_addons(); const cJSON *ad; if (!a) return 0; int ai = 0;
  cJSON_ArrayForEach(ad, a) { if (!sb_addon_on(ad)) { ai++; continue; } const cJSON *cats = js_arr(js_obj(ad, "manifest"), "catalogs"), *cat; char base[512]; addon_base(ad, base, sizeof base); int ci = 0;
    if (cats) cJSON_ArrayForEach(cat, cats) {
      const char *ctype = js_str(cat, "type", NULL), *cid = js_str(cat, "id", NULL); char ex[200];
      if (ctype && cid && *ctype && *cid && !anime_cat_rank(ad, cat) && addon_cat_default_extra(cat, ex, sizeof ex) == 0 && seen++ >= from && n < max) {
        CatalogDef *d = &out[n++]; const char *nm = js_str(cat, "name", ""); char title[96], key[48], path[512], te[96], ie[300];
        cat_row_title(nm, cid, ctype, title, sizeof title); snprintf(key, sizeof key, "a%d_%d", ai, ci);
        addon_urlenc(ctype, te, sizeof te); addon_urlenc(cid, ie, sizeof ie); snprintf(path, sizeof path, "%s/catalog/%s/%s", base, te, ie);
        def_set(d, title, ctype, path, ex, addon_cat_extra(cat, "skip", NULL, NULL, NULL), key); }
      ci++; }
    ai++; }
  return n;
}
#define HOME_ROWS 63   /* leaves slot 0 in MAXROWS (64) for Continue Watching without dropping row 63 */
int sb_home_defs(CatalogDef *out, int max) {
  int n = board_defs(out, max < HOME_ROWS ? max : HOME_ROWS, 0);
  if (!n) {
    const char *defs[4][4] = { { "Popular · Movies", "movie", CINEMETA "/catalog/movie/top", "cin_m_top" }, { "Popular · Series", "series", CINEMETA "/catalog/series/top", "cin_s_top" },
      { "Top Rated · Movies", "movie", CINEMETA "/catalog/movie/imdbRating", "cin_m_ir" }, { "Top Rated · Series", "series", CINEMETA "/catalog/series/imdbRating", "cin_s_ir" } };
    for (int i = 0; i < 4 && n < max; i++) def_set(&out[n++], defs[i][0], defs[i][1], defs[i][2], NULL, 1, defs[i][3]);
  }
  return n;
}
/* Anime: dedicated anime addons/catalogs first (rank 1), followed by anime streaming rows like Crunchyroll inside
   general catalog addons (rank 2). Excluded from Home/Discover so only anime is here and nothing is duplicated. */
int sb_anime_defs(CatalogDef *out, int max) {
  int n = 0; cJSON *a = sb_addons();
  if (!a) return 0;
  for (int pass = 1; pass <= 2 && n < max; pass++) {
    int ai = 0; const cJSON *ad;
    cJSON_ArrayForEach(ad, a) {
      if (!sb_addon_on(ad)) { ai++; continue; }
      const cJSON *cats = js_arr(js_obj(ad, "manifest"), "catalogs"), *cat;
      char base[512]; addon_base(ad, base, sizeof base); int ci = 0;
      if (cats) cJSON_ArrayForEach(cat, cats) {
        const char *ctype = js_str(cat, "type", NULL), *cid = js_str(cat, "id", NULL), *cnm = js_str(cat, "name", "");
        char ex[200];
        if (ctype && cid && *ctype && *cid && n < max && anime_cat_rank(ad, cat) == pass && addon_cat_default_extra(cat, ex, sizeof ex) == 0) {
          CatalogDef *d = &out[n++]; char title[96], key[48], path[512], te[96], ie[300];
          cat_row_title(cnm, cid, ctype, title, sizeof title);
          snprintf(key, sizeof key, "an%d_%d", ai, ci);
          addon_urlenc(ctype, te, sizeof te); addon_urlenc(cid, ie, sizeof ie);
          snprintf(path, sizeof path, "%s/catalog/%s/%s", base, te, ie);
          def_set(d, title, ctype, path, ex, addon_cat_extra(cat, "skip", NULL, NULL, NULL), key);
        }
        ci++;
      }
      ai++;
    }
  }
  return n;
}
/* Discover: Cinemeta's trending and top rated, its genres (the ones its manifest offers), then the installed catalogs
   Home has no room for */
int sb_disc_defs(CatalogDef *out, int max) {
  const char *defs[3][4] = { { "Trending · Movies", "movie", CINEMETA "/catalog/movie/top", "d_m_top" }, { "Trending · Series", "series", CINEMETA "/catalog/series/top", "d_s_top" }, { "Top Rated · Movies", "movie", CINEMETA "/catalog/movie/imdbRating", "d_m_ir" } };
  const char *genres[5] = { "Action", "Comedy", "Sci-Fi", "Drama", "Horror" };
  int n = 0;
  for (int i = 0; i < 3 && n < max; i++) def_set(&out[n++], defs[i][0], defs[i][1], defs[i][2], NULL, 1, defs[i][3]);
  for (int i = 0; i < 5 && n < max; i++) { char ex[64] = "", key[16]; addon_extra_add(ex, sizeof ex, "genre", genres[i]); snprintf(key, sizeof key, "d_g_%d", i); def_set(&out[n++], genres[i], "movie", CINEMETA "/catalog/movie/top", ex, 1, key); }
  if (n < max) n += board_defs(out + n, max - n, HOME_ROWS);
  return n;
}
void sb_meta_line(const cJSON *m, char *out, size_t cap) {
  const char *rel = js_str(m, "releaseInfo", NULL); char yr[32] = ""; if (rel) copy_str(yr, sizeof yr, rel); else { double y = js_num(m, "year", 0); if (y) snprintf(yr, sizeof yr, "%d", (int)y); else copy_str(yr, sizeof yr, js_str(m, "year", "")); }
  const char *rt = js_str(m, "runtime", ""); const cJSON *ir = cJSON_GetObjectItemCaseSensitive(m, "imdbRating"); char irs[16] = "";
  if (cJSON_IsNumber(ir)) snprintf(irs, sizeof irs, "%.1f", ir->valuedouble); else if (cJSON_IsString(ir) && ir->valuestring[0]) copy_str(irs, sizeof irs, ir->valuestring);
  snprintf(out, cap, "%s%s%s%s%s%s", yr, (*yr && (*rt || *irs)) ? "   " : "", rt, (*rt && *irs) ? "   " : "", *irs ? "IMDb " : "", irs);
}
void sb_fill_item(Item *it, const cJSON *m, const char *type) {
  memset(it, 0, sizeof *it);
  copy_str(it->id, sizeof it->id, js_str(m, "id", js_str(m, "_id", ""))); copy_str(it->type, sizeof it->type, js_str(m, "type", type ? type : ""));
  copy_str(it->name, sizeof it->name, js_str(m, "name", "")); copy_str(it->poster, sizeof it->poster, js_str(m, "poster", "")); copy_str(it->background, sizeof it->background, js_str(m, "background", ""));
  copy_str(it->logo, sizeof it->logo, js_str(m, "logo", "")); copy_str(it->description, sizeof it->description, js_str(m, "description", ""));
  const char *ml = js_str(m, "metaLine", NULL); if (ml) copy_str(it->metaLine, sizeof it->metaLine, ml); else sb_meta_line(m, it->metaLine, sizeof it->metaLine); /* the cached slim list keeps it ready-made */
}
void itemlist_free(ItemList *l) { if (l) { free(l->items); free(l); } }
void itemlist_clear(ItemList *l) { if (l) { free(l->items); l->items = NULL; l->n = 0; } } /* a list that lives in its owner */
static ItemList *list_from_metas(const cJSON *metas, const char *type, int max) {
  ItemList *l = calloc(1, sizeof *l); int n = cJSON_GetArraySize(metas); l->total = n; if (n > max) n = max; l->items = calloc(n ? n : 1, sizeof(Item));
  const cJSON *m; cJSON_ArrayForEach(m, metas) { if (l->n >= n) break; sb_fill_item(&l->items[l->n], m, type); if (l->items[l->n].id[0]) l->n++; }
  return l;
}
typedef struct { char url[512], type[16], key[600]; int n; /* key: "cat:" + the whole address (cut short, every catalog of one addon shared one file) */ void (*cb)(ItemList *, int, void *); void *ud; char *cachedJson; } CatReq;
static void on_catalog(int st, const unsigned char *b, size_t len, void *ud) {
  CatReq *r = ud; Uint32 tq0 = SDL_GetTicks(); cJSON *j = st == 200 ? net_json(b) : NULL; const cJSON *metas = js_arr(j, "metas");
  if (metas && cJSON_GetArraySize(metas)) {
    ItemList *l = list_from_metas(metas, r->type, r->n); Uint32 tq1 = SDL_GetTicks();
    /* the slim list is what is cached; a copy under 12 h old stays on screen (the fresh order shows next launch) */
    cJSON *slim = cJSON_CreateArray(); for (int i = 0; i < l->n; i++) { Item *it = &l->items[i]; cJSON *o = cJSON_CreateObject();
      cJSON_AddStringToObject(o, "id", it->id); cJSON_AddStringToObject(o, "type", it->type); cJSON_AddStringToObject(o, "name", it->name); cJSON_AddStringToObject(o, "poster", it->poster);
      cJSON_AddStringToObject(o, "background", it->background); cJSON_AddStringToObject(o, "logo", it->logo); cJSON_AddStringToObject(o, "description", it->description); cJSON_AddStringToObject(o, "metaLine", it->metaLine); cJSON_AddItemToArray(slim, o); }
    char *txt = cJSON_PrintUnformatted(slim); cJSON_Delete(slim);
    int same = r->cachedJson && !strcmp(r->cachedJson, txt);
    char cp[1200]; int fresh = r->cachedJson && cache_file(r->key, 43200, cp, sizeof cp);
    if (!fresh) cache_write(r->key, (unsigned char *)txt, strlen(txt));
    { AddonCache h; addon_cache_hints(j, &h); char hk[620], v[24]; snprintf(hk, sizeof hk, "cah:%s", r->url); int k = snprintf(v, sizeof v, "%ld", h.maxAge > 0 ? h.maxAge : 0); cache_write(hk, (unsigned char *)v, (size_t)k); } /* how long the addon says it holds */
    Uint32 tq2 = SDL_GetTicks(); if (!same && !fresh) r->cb(l, 0, r->ud); else itemlist_free(l);
    if (getenv("SB_SLOWLOG")) fprintf(stderr, "[cat] %d metas, %d kept: list %u ms, slim %u ms, page %u ms (%s)\n", cJSON_GetArraySize(metas), r->n, tq1 - tq0, tq2 - tq1, SDL_GetTicks() - tq2, same ? "same" : fresh ? "fresh copy shown" : "new");
    free(txt);
  } else if (!r->cachedJson) { ItemList *l = calloc(1, sizeof *l); l->items = calloc(1, sizeof(Item)); l->total = metas ? 0 : -1; r->cb(l, 0, r->ud); } /* no answer at all: -1 */
  net_json_free(j); free(r->cachedJson); free(r);
}
void sb_catalog_url(const char *url, const char *type, int n, void (*cb)(ItemList *, int, void *), void *ud) {
  CatReq *r = calloc(1, sizeof *r); copy_str(r->url, sizeof r->url, url); copy_str(r->type, sizeof r->type, type); snprintf(r->key, sizeof r->key, "cat:%s", url); r->n = n; r->cb = cb; r->ud = ud;
  size_t cl; unsigned char *t = cache_read(r->key, 0, &cl);
  if (t) { cJSON *c = cJSON_Parse((const char *)t); if (cJSON_IsArray(c) && cJSON_GetArraySize(c)) { r->cachedJson = (char *)t; cb(list_from_metas(c, type, n), 1, ud); } else free(t); cJSON_Delete(c); }
  /* the addon's own cacheMaxAge: a copy younger than that is the answer (no request) */
  if (r->cachedJson) { char hk[620], cp[1200]; snprintf(hk, sizeof hk, "cah:%s", url); size_t hl; unsigned char *h = cache_read(hk, 0, &hl); long age = h ? atol((const char *)h) : 0; free(h);
    if (age > 0 && cache_file(r->key, age, cp, sizeof cp)) { free(r->cachedJson); free(r); return; } }
  net_get(url, on_catalog, r);
}
void sb_catalog(const CatalogDef *d, void (*cb)(ItemList *, int, void *), void *ud) { sb_catalog_url(d->url, d->type, 16, cb, ud); }
/* a further page: the extra the catalog is shown with, and skip */
typedef struct { char type[16]; void (*cb)(ItemList *, int, void *); void *ud; } PageReq;
static void on_page(int st, const unsigned char *b, size_t len, void *ud) {
  (void)len; PageReq *r = ud; cJSON *j = st == 200 ? net_json(b) : NULL; const cJSON *metas = js_arr(j, "metas");
  const cJSON *hm = cJSON_GetObjectItemCaseSensitive(j, "hasMore"); int more = cJSON_IsBool(hm) ? cJSON_IsTrue(hm) : -1;
  if (st == 200 && !j) fprintf(stderr, "[addon] a catalog page that is not JSON\n");
  r->cb(st == 200 || st == 404 ? list_from_metas(metas, r->type, 200) : NULL, more, r->ud); /* (a 404 or no metas: the end) */
  net_json_free(j); free(r);
}
void sb_catalog_page(const CatalogDef *d, int skip, void (*cb)(ItemList *, int, void *), void *ud) {
  char ex[260], sk[16], url[800]; copy_str(ex, sizeof ex, d->extra); snprintf(sk, sizeof sk, "%d", skip); addon_extra_add(ex, sizeof ex, "skip", sk);
  snprintf(url, sizeof url, "%s/%s.json", d->path, ex);
  PageReq *r = calloc(1, sizeof *r); copy_str(r->type, sizeof r->type, d->type); r->cb = cb; r->ud = ud; net_get(url, on_page, r);
}
/* every catalog that takes a search (and needs nothing else), in addon order */
int sb_search_defs(const char *q, CatalogDef *out, int max) {
  int n = 0; cJSON *a = sb_addons(); const cJSON *ad; if (!a) return 0; static const char *const SN[1] = { "search" };
  cJSON_ArrayForEach(ad, a) { if (!sb_addon_on(ad)) continue; const cJSON *cats = js_arr(js_obj(ad, "manifest"), "catalogs"), *cat; char base[512]; addon_base(ad, base, sizeof base);
    if (cats) cJSON_ArrayForEach(cat, cats) { const char *ctype = js_str(cat, "type", NULL), *cid = js_str(cat, "id", NULL);
      if (!ctype || !cid || !addon_cat_accepts(cat, SN, 1) || n >= max) continue;
      char path[512], te[96], ie[300], ex[300] = ""; addon_urlenc(ctype, te, sizeof te); addon_urlenc(cid, ie, sizeof ie); snprintf(path, sizeof path, "%s/catalog/%s/%s", base, te, ie);
      addon_extra_add(ex, sizeof ex, "search", q); def_set(&out[n++], js_str(cat, "name", ""), ctype, path, ex, 0, ""); } }
  return n;
}

/* ---- pictures ------------------------------------------------------------------------------ */
const char *sb_poster_of(const Item *it, char *buf, size_t cap) {
  if (it->poster[0]) { copy_str(buf, cap, it->poster);
    if (find_in(buf, "images.metahub.space/poster/")) { replace_in(buf, cap, "/poster/small/", "/poster/medium/"); replace_in(buf, cap, "/poster/large/", "/poster/medium/"); /* small is WebP; medium is JPEG */ }
    else if (find_in(buf, "image.tmdb.org/t/p/")) { const char *k[] = { "/t/p/w500/", "/t/p/w780/", "/t/p/w1280/", "/t/p/original/" }; for (int i = 0; i < 4; i++) replace_in(buf, cap, k[i], "/t/p/w342/"); }
    return buf; }
  if (sb_is_imdb(it->id)) { snprintf(buf, cap, "https://images.metahub.space/poster/medium/%s/img", it->id); return buf; }
  buf[0] = 0; return buf;
}
static const char *bg_of(const Item *it, char *buf, size_t cap) {
  if (it->background[0]) copy_str(buf, cap, it->background); else if (sb_is_imdb(it->id)) snprintf(buf, cap, "https://images.metahub.space/background/medium/%s/img", it->id); else copy_str(buf, cap, it->poster);
  return buf;
}
const char *sb_bg_small(const Item *it, char *buf, size_t cap) { bg_of(it, buf, cap);
  if (find_in(buf, "images.metahub.space/background/")) { const char *k[] = { "/background/medium/", "/background/large/" }; for (int i = 0; i < 2; i++) replace_in(buf, cap, k[i], "/background/small/"); }
  else if (find_in(buf, "image.tmdb.org/t/p/")) { const char *k[] = { "/t/p/w500/", "/t/p/w780/", "/t/p/w1280/", "/t/p/original/" }; for (int i = 0; i < 4; i++) replace_in(buf, cap, k[i], "/t/p/w300/"); }
  return buf; }
const char *sb_bg_mid(const Item *it, char *buf, size_t cap) { bg_of(it, buf, cap);
  if (find_in(buf, "images.metahub.space/background/")) { const char *k[] = { "/background/small/", "/background/large/" }; for (int i = 0; i < 2; i++) replace_in(buf, cap, k[i], "/background/medium/"); }
  else if (find_in(buf, "image.tmdb.org/t/p/")) replace_in(buf, cap, "/t/p/original/", "/t/p/w1280/");
  return buf; }
const char *sb_logo_of(const Item *it, char *buf, size_t cap) { if (it->logo[0]) copy_str(buf, cap, it->logo); else if (sb_is_imdb(it->id)) snprintf(buf, cap, "https://images.metahub.space/logo/medium/%s/img", it->id); else buf[0] = 0; return buf; }
/* "2h 10min", "130 min", "1 h 5 m" (a year or a year span is not a count of minutes) */
static int unit_at(const char *q, char u, const char *longs[]) {
  if (*q != u) return 0; q++; for (int i = 0; longs[i]; i++) { size_t n = strlen(longs[i]); if (!strncmp(q, longs[i], n)) { q += n; break; } }
  return !(*q >= 'a' && *q <= 'z');
}
int sb_runtime_min(const char *s) {
  if (!s) return 0; char buf[128]; copy_str(buf, sizeof buf, s); for (char *q = buf; *q; q++) if (*q >= 'A' && *q <= 'Z') *q += 32;
  /* blank out "2019 - 2021" / "2019-" */
  for (char *q = buf; *q; q++) { int i = 0; while (q[i] >= '0' && q[i] <= '9') i++; if (i == 4 && (q == buf || !(q[-1] >= '0' && q[-1] <= '9'))) { char *e = q + 4; while (*e == ' ') e++;
      if (*e == '-' || !strncmp(e, "\xe2\x80\x93", 3)) { e += *e == '-' ? 1 : 3; while (*e == ' ') e++; int j = 0; while (e[j] >= '0' && e[j] <= '9') j++; if (j == 4) e += 4; memset(q, ' ', e - q); q = e - 1; } } }
  static const char *HL[] = { "ours", "our", "rs", "r", NULL }, *ML[] = { "inutes", "inute", "ins", "in", NULL };
  int h = -1, m = -1;
  for (const char *q = buf; *q; q++) { if (!(*q >= '0' && *q <= '9') || (q > buf && q[-1] >= '0' && q[-1] <= '9')) continue;
    int v = atoi(q); const char *e = q; while (*e >= '0' && *e <= '9') e++; while (*e == ' ') e++;
    if (h < 0 && unit_at(e, 'h', HL)) h = v; else if (m < 0 && unit_at(e, 'm', ML)) m = v; }
  if (h < 0 && m < 0) return 0;
  return (h > 0 ? h * 60 : 0) + (m > 0 ? m : 0);
}
void sb_fmt_runtime(int mn, char *out, size_t cap) {
  if (mn <= 0) { out[0] = 0; return; } char h[8], m[8]; snprintf(h, sizeof h, "%d", mn / 60); snprintf(m, sizeof m, "%d", mn % 60);
  if (mn / 60) copy_str(out, cap, mn % 60 ? T2("{h}h {m}min", "h", h, "m", m) : T1("{h}h", "h", h)); else copy_str(out, cap, T1("{m}min", "m", m));
}

/* ---- metadata cache ------------------------------------------------------------------------ */
typedef struct { char key[96]; cJSON *meta; unsigned use; } MetaEnt;
static MetaEnt metaMem[40]; static unsigned metaUse, metaVer = 1; /* metaVer: bumps whenever an entry comes or goes */
static int meta_slot(const char *type, const char *id) { char k[96]; snprintf(k, sizeof k, "%s_%s", type, id); for (int i = 0; i < 40; i++) if (metaMem[i].meta && !strcmp(metaMem[i].key, k)) { metaMem[i].use = ++metaUse; return i; } return -1; }
cJSON *sb_meta_cached(const char *type, const char *id) { int i = meta_slot(type, id); return i >= 0 ? metaMem[i].meta : NULL; }
static void meta_put(const char *k, cJSON *m) { metaVer++; int slot = 0; unsigned old = 0xffffffffu; for (int i = 0; i < 40; i++) { if (!metaMem[i].meta) { slot = i; break; } if (metaMem[i].use < old) { old = metaMem[i].use; slot = i; } }
  if (metaMem[slot].meta) cJSON_Delete(metaMem[slot].meta); copy_str(metaMem[slot].key, sizeof metaMem[slot].key, k); metaMem[slot].meta = m; metaMem[slot].use = ++metaUse; }
#define META_BASES 8
typedef struct { char type[16], id[64], key[96]; void (*cb)(cJSON *, void *); void *ud; int fresh; char bases[META_BASES][512]; int nb, bi; } MetaReq;
static void meta_try(MetaReq *r);
static void on_meta(int st, const unsigned char *b, size_t n, void *ud) {
  MetaReq *r = ud; cJSON *j = st == 200 ? net_json(b) : NULL; cJSON *m = j ? cJSON_DetachItemFromObjectCaseSensitive(j, "meta") : NULL; net_json_free(j);
  if (!cJSON_IsObject(m)) { cJSON_Delete(m); meta_try(r); return; }
  char ck[128]; snprintf(ck, sizeof ck, "meta:%s", r->key); cache_write(ck, b, n); /* the answer as it came ({"meta":...}): printing a series' tree again took a frame */
  meta_put(r->key, m);
  if (!r->fresh) r->cb(m, r->ud);
  free(r);
}
static void meta_try(MetaReq *r) {
  if (r->bi >= r->nb) { if (!r->fresh) r->cb(NULL, r->ud); free(r); return; }
  char url[800]; addon_url(url, sizeof url, r->bases[r->bi++], "meta", r->type, r->id, NULL);
  net_get_pri(url, on_meta, r);
}
static void meta_go(MetaReq *r) {
  if (!r->nb && !r->fresh) { r->cb(NULL, r->ud); free(r); return; }
  if (!r->nb) { free(r); return; }
  meta_try(r);
}
static void on_meta_disk(int st, const unsigned char *b, size_t n, void *ud) {
  MetaReq *r = ud; (void)n; cJSON *c = st == 200 ? net_json(b) : NULL; AddonCache h; addon_cache_hints(c, &h);
  if (cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(c, "meta"))) { cJSON *m = cJSON_DetachItemFromObjectCaseSensitive(c, "meta"); cJSON_Delete(c); c = m; } /* written as the answer came */
  if (h.maxAge > 0 && cJSON_IsObject(c)) { char ck[128], cp[1200]; snprintf(ck, sizeof ck, "meta:%s", r->key); if (cache_file(ck, h.maxAge, cp, sizeof cp)) r->nb = 0; } /* younger than the addon's cacheMaxAge: no request */
  cJSON *m = sb_meta_cached(r->type, r->id); /* asked again meanwhile and answered from the addons */
  if (m) { cJSON_Delete(c); r->fresh = 1; r->cb(m, r->ud); }
  else if (cJSON_IsObject(c)) { meta_put(r->key, c); r->fresh = 1; r->cb(c, r->ud); } else cJSON_Delete(c);
  meta_go(r);
}
/* the addons that describe this id, in addon order (as the official core picks them), each asked in turn until one
   answers; Cinemeta last for an IMDb id when it is not among them */
void sb_meta(const char *type, const char *id, void (*cb)(cJSON *, void *), void *ud) {
  cJSON *m = sb_meta_cached(type, id); if (m) { cb(m, ud); return; }
  MetaReq *r = calloc(1, sizeof *r); copy_str(r->type, sizeof r->type, type); copy_str(r->id, sizeof r->id, id); snprintf(r->key, sizeof r->key, "%s_%s", type, id); r->cb = cb; r->ud = ud;
  cJSON *a = sb_addons(); const cJSON *ad; int cine = 0;
  if (a) cJSON_ArrayForEach(ad, a) { if (r->nb >= META_BASES || !addon_serves(ad, "meta", type, id)) continue; addon_base(ad, r->bases[r->nb], 512); if (!strcmp(r->bases[r->nb], CINEMETA)) cine = 1; r->nb++; }
  if (sb_is_imdb(id) && !cine && r->nb < META_BASES && (!strcmp(type, "movie") || !strcmp(type, "series"))) copy_str(r->bases[r->nb++], 512, CINEMETA);
  /* a disk copy under 12 h old shows first (read and parsed on the file thread: a series' copy is a tree of hundreds of
     episodes), then the addons are asked anyway so the next visit is current */
  char ck[128], cp[1200]; snprintf(ck, sizeof ck, "meta:%s", r->key);
  if (cache_file(ck, 43200, cp, sizeof cp)) { net_file(cp, on_meta_disk, r); return; }
  meta_go(r);
}

/* ---- library ------------------------------------------------------------------------------- */
static cJSON *lib_arr(void) { cJSON *l = store_node("lib"); if (!cJSON_IsObject(l)) { l = cJSON_CreateObject(); store_set_node("lib", l); } return l; }
cJSON *lib_get(const char *id) { return cJSON_GetObjectItemCaseSensitive(lib_arr(), id); }
int lib_has(const char *id) { cJSON *it = lib_get(id); return it && !js_bool(it, "removed", 1); }
void lib_invalidate(void) { libVer++; }
unsigned lib_version(void) { return libVer; }
static void push_item(cJSON *it) {
  const char *ak = sb_auth_key(); if (!ak) return;
  cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "authKey", ak); cJSON_AddStringToObject(o, "collection", "libraryItem"); cJSON *ch = cJSON_CreateArray(); cJSON_AddItemToArray(ch, cJSON_Duplicate(it, 1)); cJSON_AddItemToObject(o, "changes", ch);
  char *body = cJSON_PrintUnformatted(o); cJSON_Delete(o); net_post_json(STREMIO_API "/api/datastorePut", body, NULL, NULL); free(body);
}
static void lib_store(cJSON *it, int push) { char iso[40]; now_iso(iso, sizeof iso); cJSON_DeleteItemFromObjectCaseSensitive(it, "_mtime"); cJSON_AddStringToObject(it, "_mtime", iso); libVer++; store_set_node("lib", store_node("lib")); if (push) push_item(it); }
void lib_save(cJSON *it) { lib_store(it, 1); }
static void set_str(cJSON *o, const char *k, const char *v) { cJSON_DeleteItemFromObjectCaseSensitive(o, k); cJSON_AddStringToObject(o, k, v ? v : ""); }
static void set_bool(cJSON *o, const char *k, int v) { cJSON_DeleteItemFromObjectCaseSensitive(o, k); cJSON_AddBoolToObject(o, k, v); }
/* the year as the title's metadata gives it ("2019", "2019–2021"), when that metadata is at hand */
static void meta_year(const Item *it, char *out, size_t cap) {
  out[0] = 0; const cJSON *m = sb_meta_cached(it->type, it->id), *y = cJSON_GetObjectItemCaseSensitive(m, "year");
  if (cJSON_IsString(y) && y->valuestring) copy_str(out, cap, y->valuestring); else if (cJSON_IsNumber(y)) snprintf(out, cap, "%d", (int)y->valuedouble);
}
cJSON *lib_new_item(const Item *it) {
  char iso[40]; now_iso(iso, sizeof iso); cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "_id", it->id); cJSON_AddStringToObject(o, "name", it->name); cJSON_AddStringToObject(o, "type", it->type); cJSON_AddStringToObject(o, "poster", it->poster); cJSON_AddStringToObject(o, "posterShape", "poster");
  cJSON_AddStringToObject(o, "background", it->background); cJSON_AddStringToObject(o, "logo", it->logo); cJSON_AddStringToObject(o, "year", ""); cJSON_AddBoolToObject(o, "removed", 1); cJSON_AddBoolToObject(o, "temp", 1);
  cJSON_AddStringToObject(o, "_ctime", iso); cJSON_AddStringToObject(o, "_mtime", iso);
  cJSON *s = cJSON_CreateObject(); cJSON_AddStringToObject(s, "lastWatched", iso); cJSON_AddNumberToObject(s, "timeWatched", 0); cJSON_AddNumberToObject(s, "timeOffset", 0); cJSON_AddNumberToObject(s, "overallTimeWatched", 0);
  cJSON_AddNumberToObject(s, "timesWatched", 0); cJSON_AddNumberToObject(s, "flaggedWatched", 0); cJSON_AddNumberToObject(s, "duration", 0); cJSON_AddStringToObject(s, "video_id", ""); cJSON_AddStringToObject(s, "watched", ""); cJSON_AddBoolToObject(s, "noNotif", 0);
  cJSON_AddItemToObject(o, "state", s);
  cJSON_AddItemToObject(lib_arr(), it->id, o); return o;
}
void lib_toggle(const Item *it) {
  cJSON *l = lib_get(it->id); const cJSON *rm = cJSON_GetObjectItemCaseSensitive(l, "removed");
  if (l && cJSON_IsFalse(rm)) { set_bool(l, "removed", 1); lib_save(l); return; } /* removing leaves temp as it was */
  if (!l) l = lib_new_item(it);
  set_bool(l, "removed", 0); set_bool(l, "temp", 0);
  /* what the title page knows wins over what the item had */
  char yr[32]; meta_year(it, yr, sizeof yr);
  set_str(l, "name", it->name[0] ? it->name : js_str(l, "name", "")); set_str(l, "type", it->type[0] ? it->type : js_str(l, "type", ""));
  set_str(l, "poster", it->poster[0] ? it->poster : js_str(l, "poster", "")); set_str(l, "posterShape", "poster");
  set_str(l, "background", it->background[0] ? it->background : js_str(l, "background", "")); set_str(l, "logo", it->logo[0] ? it->logo : js_str(l, "logo", ""));
  set_str(l, "year", yr[0] ? yr : js_str(l, "year", ""));
  lib_save(l);
}
static void item_from_lib(Item *out, const cJSON *l) {
  memset(out, 0, sizeof *out); const cJSON *s = js_obj(l, "state");
  copy_str(out->id, sizeof out->id, js_str(l, "_id", "")); copy_str(out->type, sizeof out->type, js_str(l, "type", "movie")); copy_str(out->name, sizeof out->name, js_str(l, "name", ""));
  copy_str(out->poster, sizeof out->poster, js_str(l, "poster", "")); copy_str(out->background, sizeof out->background, js_str(l, "background", "")); copy_str(out->logo, sizeof out->logo, js_str(l, "logo", ""));
  const char *vid = js_str(s, "video_id", ""); copy_str(out->videoId, sizeof out->videoId, *vid ? vid : out->id);
  double d = js_num(s, "duration", 0), t = js_num(s, "timeOffset", 0); out->progress = d > 0 ? (float)(t / d) : 0; out->left = d > 0 ? (float)((d - t) / 60000.0) : 0;
}
static int cmp_mtime(const void *a, const void *b) { double x = tms(cJSON_GetObjectItemCaseSensitive(*(cJSON **)a, "_mtime")), y = tms(cJSON_GetObjectItemCaseSensitive(*(cJSON **)b, "_mtime")); return x < y ? 1 : x > y ? -1 : 0; }
int lib_cw_items(ItemList *out) {
  cJSON *lib = lib_arr(); cJSON *picks[256]; int n = 0; const cJSON *it;
  cJSON_ArrayForEach(it, lib) { const cJSON *s = js_obj(it, "state"); const char *ty = js_str(it, "type", "");
    if (!strcmp(ty, "other") || (js_bool(it, "removed", 0) && !js_bool(it, "temp", 0))) continue;
    double to = js_num(s, "timeOffset", 0), d = js_num(s, "duration", 0); if (to <= 0) continue; if (d > 0 && to > d * 0.96) continue;
    if (!js_str(it, "name", "")[0]) continue; if (n < 256) picks[n++] = (cJSON *)it; }
  qsort(picks, n, sizeof picks[0], cmp_mtime); if (n > 12) n = 12;
  out->items = calloc(n ? n : 1, sizeof(Item)); out->n = n; for (int i = 0; i < n; i++) { item_from_lib(&out->items[i], picks[i]); out->items[i].cw = 1; }
  return n;
}
int lib_cw_info(const Item *it, CwInfo *out) {
  cJSON *l = lib_get(it->id); const cJSON *s = js_obj(l, "state"); if (!s) return 0;
  double to = js_num(s, "timeOffset", 0), d = js_num(s, "duration", 0); if (!(to > 0) || !(d > 0) || to > d * 0.96) return 0;
  const char *vid = js_str(s, "video_id", ""); if ((!strcmp(it->type, "series") || !strcasecmp(it->type, "Anime Series")) && !*vid) return 0;
  copy_str(out->vid, sizeof out->vid, *vid ? vid : it->id); out->pct = (float)(to / d); out->leftMin = (float)((d - to) / 60000.0); return 1;
}
static char progId[64]; static Uint32 progPushT;
void lib_set_progress(const char *id, const Item *it, const char *vid, double to, double dur, int force) {
  cJSON *l = lib_get(id); if (!l) l = lib_new_item(it); cJSON *s = cJSON_GetObjectItemCaseSensitive(l, "state"); if (!s) { s = cJSON_CreateObject(); cJSON_AddItemToObject(l, "state", s); }
  if (it && !strcmp(it->id, id)) { /* its pictures and name from the title playing now: art kept wrong once (a title cast over another) is put right */
    const char *k[4] = { "name", "poster", "background", "logo" }, *v[4] = { it->name, it->poster, it->background, it->logo };
    for (int i = 0; i < 4; i++) if (v[i][0] && strcmp(js_str(l, k[i], ""), v[i])) { cJSON_DeleteItemFromObjectCaseSensitive(l, k[i]); cJSON_AddStringToObject(l, k[i], v[i]); } }
  char iso[40]; now_iso(iso, sizeof iso);
  cJSON_DeleteItemFromObjectCaseSensitive(s, "timeOffset"); cJSON_AddNumberToObject(s, "timeOffset", to); cJSON_DeleteItemFromObjectCaseSensitive(s, "duration"); cJSON_AddNumberToObject(s, "duration", dur);
  cJSON_DeleteItemFromObjectCaseSensitive(s, "video_id"); cJSON_AddStringToObject(s, "video_id", vid ? vid : ""); cJSON_DeleteItemFromObjectCaseSensitive(s, "lastWatched"); cJSON_AddStringToObject(s, "lastWatched", iso);
  /* kept here every time; sent to the account at most once a minute, and at a pause, the close and the end
     (the sync picks up anything that did not go) */
  int push = force || strcmp(progId, id) || SDL_GetTicks() - progPushT >= 60000;
  if (push) { copy_str(progId, sizeof progId, id); progPushT = SDL_GetTicks(); }
  lib_store(l, push);
}

/* the watched bitfield (stremio-core's format): "<anchor video id>:<anchor index+1>:<base64(zlib(bits))>" */
#define MAX_WATCH_VIDS 4096
static int cmp_vid(const void *a, const void *b) { const double *x = a, *y = b; if (x[0] != y[0]) return x[0] < y[0] ? -1 : 1; if (x[1] != y[1]) return x[1] < y[1] ? -1 : 1; return x[2] < y[2] ? -1 : x[2] > y[2] ? 1 : 0; }
static int ordered_ids(const cJSON *videos, char ids[][80], int max) { /* (season, episode) order, those without both first, stable */
  int n = 0; const cJSON *v; static double tmp[MAX_WATCH_VIDS][3]; static const cJSON *ptr[MAX_WATCH_VIDS];
  cJSON_ArrayForEach(v, videos) { if (n >= max || n >= MAX_WATCH_VIDS) break;
    const cJSON *s = cJSON_GetObjectItemCaseSensitive(v, "season"), *e = cJSON_GetObjectItemCaseSensitive(v, "episode");
    if (!cJSON_IsNumber(e)) e = cJSON_GetObjectItemCaseSensitive(v, "number");
    int hasE = cJSON_IsNumber(e), hasS = cJSON_IsNumber(s);
    tmp[n][0] = hasS && hasE ? s->valuedouble : -1e18;
    tmp[n][1] = hasE ? e->valuedouble : -1e18;
    tmp[n][2] = n; ptr[n] = v; n++; }
  qsort(tmp, n, sizeof tmp[0], cmp_vid);
  for (int i = 0; i < n; i++) copy_str(ids[i], 80, js_str(ptr[(int)tmp[i][2]], "id", ""));
  return n;
}
static int b64dec(const char *s, unsigned char *out, int cap) {
  static const char *A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; unsigned val = 0; int bits = -8, o = 0;
  for (; *s && *s != '='; s++) { const char *p = strchr(A, *s); if (!p) continue; val = ((val << 6) | (unsigned)(p - A)) & 0x3ffffu; bits += 6; if (bits >= 0) { if (o < cap) out[o++] = (unsigned char)((val >> bits) & 255); bits -= 8; } }
  return o;
}
static void b64enc(const unsigned char *in, int n, char *out) {
  static const char *A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; int o = 0;
  for (int i = 0; i < n; i += 3) { unsigned v = (in[i] << 16) | (i + 1 < n ? in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
    out[o++] = A[(v >> 18) & 63]; out[o++] = A[(v >> 12) & 63]; out[o++] = i + 1 < n ? A[(v >> 6) & 63] : '='; out[o++] = i + 2 < n ? A[v & 63] : '='; }
  out[o] = 0;
}
void lib_ep_watched_map(const cJSON *lit, const cJSON *videos, int *flags) {
  int n = cJSON_GetArraySize(videos); if (n > MAX_WATCH_VIDS) n = MAX_WATCH_VIDS;
  for (int i = 0; i < n; i++) flags[i] = 0;
  const cJSON *s = js_obj(lit, "state"); const char *str = js_str(s, "watched", "");
  static char ids[MAX_WATCH_VIDS][80]; int nid = ordered_ids(videos, ids, MAX_WATCH_VIDS);
  if (*str && nid) {
    char buf[4096]; copy_str(buf, sizeof buf, str); char *last = strrchr(buf, ':'); if (last) { *last = 0; char *b64 = last + 1; char *c2 = strrchr(buf, ':');
      if (c2) { *c2 = 0; int alen = atoi(c2 + 1); const char *anchor = buf;
        unsigned char comp[4096]; int cn = b64dec(b64, comp, sizeof comp); unsigned char bytes[4096]; uLongf bn = sizeof bytes;
        if (cn > 0 && uncompress(bytes, &bn, comp, cn) == Z_OK) { int ai = -1; for (int i = 0; i < nid; i++) if (!strcmp(ids[i], anchor)) { ai = i; break; }
          if (ai >= 0) { int off = alen - ai - 1; static int w[MAX_WATCH_VIDS]; for (int i = 0; i < nid; i++) { int p = i + off; w[i] = p >= 0 && p < nid && (p >> 3) < (int)bn && ((bytes[p >> 3] >> (p & 7)) & 1); }
            /* map back from ordered ids to the videos' order */
            int k = 0; const cJSON *v; cJSON_ArrayForEach(v, videos) { if (k >= n) break; const char *id = js_str(v, "id", ""); for (int i = 0; i < nid; i++) if (w[i] && !strcmp(ids[i], id)) { flags[k] = 1; break; } k++; } } } } }
  }
  const cJSON *we = js_arr(s, "watchedEpisodes"); if (we) { int k = 0; const cJSON *v; cJSON_ArrayForEach(v, videos) { if (k >= n) break; const cJSON *e; cJSON_ArrayForEach(e, we) if (cJSON_IsString(e) && !strcmp(e->valuestring, js_str(v, "id", ""))) flags[k] = 1; k++; } }
}
static void ep_encode(cJSON *s, const cJSON *videos, const int *flags) {
  int nv = cJSON_GetArraySize(videos); if (nv > MAX_WATCH_VIDS) nv = MAX_WATCH_VIDS;
  static char ids[MAX_WATCH_VIDS][80]; int nid = ordered_ids(videos, ids, MAX_WATCH_VIDS); unsigned char bytes[(MAX_WATCH_VIDS + 7) / 8] = { 0 }; int last = -1;
  for (int i = 0; i < nid; i++) { int k = 0; const cJSON *v; int on = 0; cJSON_ArrayForEach(v, videos) { if (k >= nv) break; if (!strcmp(js_str(v, "id", ""), ids[i])) on = flags[k]; k++; } if (on) { bytes[i >> 3] |= 1 << (i & 7); last = i; } }
  if (last < 0) last = 0; int nb = (nid + 7) / 8; unsigned char comp[1024]; uLongf cn = sizeof comp; compress(comp, &cn, bytes, nb);
  char b64[1500]; b64enc(comp, (int)cn, b64); char out[1700]; snprintf(out, sizeof out, "%s:%d:%s", nid ? ids[last] : "undefined", last + 1, b64);
  cJSON_DeleteItemFromObjectCaseSensitive(s, "watched"); cJSON_AddStringToObject(s, "watched", out); cJSON_DeleteItemFromObjectCaseSensitive(s, "watchedEpisodes");
}
void lib_set_eps_watched(cJSON *lit, const cJSON *videos, const char **epIds, int n, int on) {
  int nv = cJSON_GetArraySize(videos); if (nv > MAX_WATCH_VIDS) nv = MAX_WATCH_VIDS;
  static int flags[MAX_WATCH_VIDS]; lib_ep_watched_map(lit, videos, flags);
  int k = 0; const cJSON *v; cJSON_ArrayForEach(v, videos) { if (k >= nv) break; const char *id = js_str(v, "id", ""); for (int i = 0; i < n; i++) if (!strcmp(epIds[i], id)) flags[k] = on; k++; }
  cJSON *s = cJSON_GetObjectItemCaseSensitive(lit, "state"); ep_encode(s, videos, flags);
  if (on) { char iso[40]; now_iso(iso, sizeof iso); cJSON_DeleteItemFromObjectCaseSensitive(s, "lastWatched"); cJSON_AddStringToObject(s, "lastWatched", iso);
    const char *cur = js_str(s, "video_id", ""); for (int i = 0; i < n; i++) if (!strcmp(epIds[i], cur)) { cJSON_DeleteItemFromObjectCaseSensitive(s, "timeOffset"); cJSON_AddNumberToObject(s, "timeOffset", 0); } }
  lib_save(lit);
}
void lib_set_movie_watched(cJSON *lit, int on) {
  cJSON *s = cJSON_GetObjectItemCaseSensitive(lit, "state"); if (!s) return;
  if (on) { double tw = js_num(s, "timesWatched", 0) + 1; char iso[40]; now_iso(iso, sizeof iso); cJSON_DeleteItemFromObjectCaseSensitive(s, "timesWatched"); cJSON_AddNumberToObject(s, "timesWatched", tw);
    cJSON_DeleteItemFromObjectCaseSensitive(s, "lastWatched"); cJSON_AddStringToObject(s, "lastWatched", iso); cJSON_DeleteItemFromObjectCaseSensitive(s, "timeOffset"); cJSON_AddNumberToObject(s, "timeOffset", 0); }
  else { cJSON_DeleteItemFromObjectCaseSensitive(s, "timesWatched"); cJSON_AddNumberToObject(s, "timesWatched", 0); cJSON_DeleteItemFromObjectCaseSensitive(s, "flaggedWatched"); cJSON_AddNumberToObject(s, "flaggedWatched", 0); }
  cJSON_DeleteItemFromObjectCaseSensitive(s, "watchedEpisodes"); lib_save(lit);
}
int lib_movie_watched(const cJSON *l) { const cJSON *s = js_obj(l, "state"); if (!s) return 0; if (js_num(s, "timesWatched", 0) > 0) return 1;
  const cJSON *we = js_arr(s, "watchedEpisodes"), *e; const char *id = js_str(l, "_id", ""); if (we) cJSON_ArrayForEach(e, we) if (cJSON_IsString(e) && !strcmp(e->valuestring, id)) return 1; return 0; }
int lib_series_watched(const cJSON *l, const cJSON *videos) {
  const cJSON *s = js_obj(l, "state"); if (!s || (!js_str(s, "watched", "")[0] && !js_arr(s, "watchedEpisodes"))) return 0; if (!videos || !cJSON_GetArraySize(videos)) return 0;
  int hasPosSeason = 0; const cJSON *v;
  cJSON_ArrayForEach(v, videos) if (js_num(v, "season", 0) > 0) { hasPosSeason = 1; break; }
  static int flags[MAX_WATCH_VIDS]; lib_ep_watched_map(l, videos, flags); int k = 0, n = 0; time_t now = time(NULL);
  cJSON_ArrayForEach(v, videos) { if (k >= MAX_WATCH_VIDS) break;
    const cJSON *se = cJSON_GetObjectItemCaseSensitive(v, "season");
    if (hasPosSeason ? (!cJSON_IsNumber(se) || se->valuedouble <= 0) : (cJSON_IsNumber(se) && se->valuedouble == 0)) { k++; continue; }
    const char *rel = js_str(v, "released", js_str(v, "firstAired", NULL)); double r = 0; if (rel) { cJSON tmp = { 0 }; tmp.type = cJSON_String; tmp.valuestring = (char *)rel; r = tms(&tmp); }
    if (rel && *rel && (!r || r > (double)now * 1000)) { k++; continue; }
    n++; if (!flags[k]) return 0; k++; }
  return n > 0;
}
static int meta_slot_any(const char *type, const char *id) {
  int s = type && *type ? meta_slot(type, id) : -1;
  if (s < 0) s = meta_slot("series", id);
  if (s < 0) s = meta_slot("anime", id);
  return s;
}
static cJSON *meta_cached_any(const char *type, const char *id) { int s = meta_slot_any(type, id); return s >= 0 ? metaMem[s].meta : NULL; }
static int type_may_have_eps(const char *t) { return t && (!strcmp(t, "series") || !strcasecmp(t, "anime") || !strcasecmp(t, "Anime Series")); }
/* the badge is asked for every card in every frame and a series' answer decodes its bitfield against every episode:
   it is kept per title until the library or the metadata cache moves (a series: also each minute, it counts only
   the episodes aired by now). A hit touches the metadata as the lookup did, so the titles on screen stay cached. */
typedef struct { char id[64]; unsigned char series, on; unsigned lv, mv; long mn; int slot; } WMemo;
static WMemo wMemo[256];
int lib_item_watched(const Item *it) {
  int series = type_may_have_eps(it->type); long mn = series ? (long)(time(NULL) / 60) : 0;
  unsigned h = 2166136261u; for (const char *p = it->id; *p; p++) h = (h ^ (unsigned char)*p) * 16777619u;
  WMemo *w = NULL;
  for (int k = 0; k < 8; k++) { WMemo *e = &wMemo[(h + k) & 255]; if (!e->id[0] || (!strcmp(e->id, it->id) && e->series == series)) { w = e; break; } }
  if (!w) w = &wMemo[h & 255];
  if (w->id[0] && w->lv == libVer && w->mv == metaVer && w->mn == mn && !strcmp(w->id, it->id) && w->series == series) { if (w->slot >= 0) metaMem[w->slot].use = ++metaUse; return w->on; }
  cJSON *l = lib_get(it->id); int on = 0, slot = -1;
  if (l) {
    if (series) {
      slot = meta_slot_any(it->type, it->id);
      const cJSON *vids = js_arr(slot >= 0 ? metaMem[slot].meta : NULL, "videos");
      if (vids && cJSON_GetArraySize(vids) > 1) on = lib_series_watched(l, vids);
      else if (!strcmp(it->type, "series") || !strcasecmp(it->type, "Anime Series")) on = lib_series_watched(l, vids);
      else on = lib_movie_watched(l); /* anime film with no episode list */
    } else on = lib_movie_watched(l);
  }
  copy_str(w->id, sizeof w->id, it->id); w->series = (unsigned char)series; w->on = (unsigned char)on; w->lv = libVer; w->mv = metaVer; w->mn = mn; w->slot = slot;
  return on;
}
int lib_is_marked_watched(const Item *it) {
  cJSON *l = lib_get(it->id); if (!l) return 0;
  if (type_may_have_eps(it->type) && sb_parse_video_id(it->id, it->videoId, NULL, NULL) > 0) {
    cJSON *m = meta_cached_any(it->type, it->id); const cJSON *videos = js_arr(m, "videos"); if (!videos) return 0;
    int nv = cJSON_GetArraySize(videos); if (nv > MAX_WATCH_VIDS) nv = MAX_WATCH_VIDS;
    static int flags[MAX_WATCH_VIDS]; lib_ep_watched_map(l, videos, flags); int k = 0; const cJSON *v;
    cJSON_ArrayForEach(v, videos) { if (k >= nv) break; if (!strcmp(js_str(v, "id", ""), it->videoId)) return flags[k]; k++; }
    return 0;
  }
  if (type_may_have_eps(it->type)) {
    cJSON *m = meta_cached_any(it->type, it->id); const cJSON *videos = js_arr(m, "videos");
    if (videos && cJSON_GetArraySize(videos) > 1) return lib_series_watched(l, videos);
    if (!strcmp(it->type, "series") || !strcasecmp(it->type, "Anime Series")) return lib_series_watched(l, videos);
  }
  return lib_movie_watched(l);
}
void lib_toggle_watched(const Item *it, const cJSON *videos) {
  cJSON *l = lib_get(it->id); if (!l) l = lib_new_item(it);
  if (type_may_have_eps(it->type) && sb_parse_video_id(it->id, it->videoId, NULL, NULL) > 0) {
    int on = !lib_is_marked_watched(it); const char *ids[1] = { it->videoId }; if (videos) lib_set_eps_watched(l, videos, ids, 1, on);
    return;
  }
  if (type_may_have_eps(it->type) && videos && cJSON_GetArraySize(videos) > 1) {
    int on = !lib_series_watched(l, videos);
    int hasPosSeason = 0; const cJSON *v;
    cJSON_ArrayForEach(v, videos) if (js_num(v, "season", 0) > 0) { hasPosSeason = 1; break; }
    static const char *ids[MAX_WATCH_VIDS]; int n = 0; time_t now = time(NULL);
    cJSON_ArrayForEach(v, videos) {
      if (n >= MAX_WATCH_VIDS) break;
      const cJSON *se = cJSON_GetObjectItemCaseSensitive(v, "season");
      if (hasPosSeason ? (!cJSON_IsNumber(se) || se->valuedouble <= 0) : (cJSON_IsNumber(se) && se->valuedouble == 0)) continue;
      if (on) {
        const char *rel = js_str(v, "released", js_str(v, "firstAired", NULL)); double r = 0;
        if (rel) { cJSON tmp = { 0 }; tmp.type = cJSON_String; tmp.valuestring = (char *)rel; r = tms(&tmp); }
        if (rel && *rel && (!r || r > (double)now * 1000)) continue;
      }
      const char *vid = js_str(v, "id", ""); if (*vid) ids[n++] = vid;
    }
    if (n) lib_set_eps_watched(l, videos, ids, n, on);
    return;
  }
  lib_set_movie_watched(l, !lib_is_marked_watched(it));
}

/* the series' episode list from the memory or the disk copy of its metadata (any age); *own = what to free */
static const cJSON *videos_cached(const char *type, const char *id, cJSON **own) {
  *own = NULL; cJSON *m = meta_cached_any(type, id); if (m) return js_arr(m, "videos");
  const char *tys[2] = { type && *type ? type : "series", "series" };
  for (int i = 0; i < 2; i++) {
    char ck[128]; snprintf(ck, sizeof ck, "meta:%s_%s", tys[i], id); size_t cl; unsigned char *t = cache_read(ck, 0, &cl);
    if (!t) continue;
    *own = cJSON_Parse((const char *)t); free(t); const cJSON *mo = js_obj(*own, "meta"); return js_arr(mo ? mo : *own, "videos");
  }
  return NULL;
}
/* one-time fixes for items older builds wrote: the private watched list goes into the shared bitfield, and
   removed:false + temp:true (which every app shows in the Library) becomes the official removed:true + temp:true */
static int lib_migrate_legacy(void) {
  cJSON *lib = lib_arr(), *it; int ch = 0;
  cJSON_ArrayForEach(it, lib) { cJSON *s = cJSON_GetObjectItemCaseSensitive(it, "state");
    if (cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(it, "removed")) && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(it, "temp"))) { set_bool(it, "removed", 1); lib_save(it); ch = 1; }
    const cJSON *we = js_arr(s, "watchedEpisodes"); if (!we || !cJSON_GetArraySize(we)) continue;
    const char *id = js_str(it, "_id", ""), *ty = js_str(it, "type", "");
    if (type_may_have_eps(ty)) { cJSON *own; const cJSON *vids = videos_cached(ty, id, &own); int n = cJSON_GetArraySize(vids); if (!vids || !n) { cJSON_Delete(own); continue; }
      if (n > MAX_WATCH_VIDS) n = MAX_WATCH_VIDS;
      int *flags = calloc(n, sizeof *flags); lib_ep_watched_map(it, vids, flags); ep_encode(s, vids, flags); free(flags); cJSON_Delete(own); }
    else { int mine = 0; const cJSON *e; cJSON_ArrayForEach(e, we) if (cJSON_IsString(e) && !strcmp(e->valuestring, id)) mine = 1;
      if (mine && !(js_num(s, "timesWatched", 0) > 0)) { cJSON_DeleteItemFromObjectCaseSensitive(s, "timesWatched"); cJSON_AddNumberToObject(s, "timesWatched", 1); } }
    cJSON_DeleteItemFromObjectCaseSensitive(s, "watchedEpisodes"); lib_save(it); ch = 1; }
  return ch;
}
unsigned sb_lib_sync_age(void) { return libSyncT ? SDL_GetTicks() - libSyncT : 0xffffffffu; }

/* two-way sync with the account, newest _mtime wins */
typedef struct { void (*cb)(int, void *); void *ud; int changed; char ids[500][64]; int nwant, at; } PullReq;
static void pull_next(PullReq *r);
static void on_pull_get(int st, const unsigned char *b, size_t n, void *ud) {
  PullReq *r = ud; cJSON *j = st == 200 ? net_json(b) : NULL; const cJSON *items = js_arr(j, "result"), *it;
  if (items) cJSON_ArrayForEach(it, items) { const char *id = js_str(it, "_id", NULL); if (id) { cJSON_DeleteItemFromObjectCaseSensitive(lib_arr(), id); cJSON_AddItemToObject(lib_arr(), id, cJSON_Duplicate(it, 1)); r->changed = 1; } }
  net_json_free(j); pull_next(r);
}
static void pull_next(PullReq *r) {
  if (r->at >= r->nwant) { if (lib_migrate_legacy()) r->changed = 1; if (r->changed) { libVer++; store_set_node("lib", store_node("lib")); store_flush(); } libSyncing = 0; if (r->cb) r->cb(r->changed, r->ud); free(r); return; }
  cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "authKey", sb_auth_key()); cJSON_AddStringToObject(o, "collection", "libraryItem"); cJSON *ids = cJSON_CreateArray();
  for (int k = 0; k < 100 && r->at < r->nwant; k++, r->at++) cJSON_AddItemToArray(ids, cJSON_CreateString(r->ids[r->at]));
  cJSON_AddItemToObject(o, "ids", ids); cJSON_AddBoolToObject(o, "all", 0); char *body = cJSON_PrintUnformatted(o); cJSON_Delete(o);
  net_post_json(STREMIO_API "/api/datastoreGet", body, on_pull_get, r); free(body);
}
static void on_pull_meta(int st, const unsigned char *b, size_t n, void *ud) {
  PullReq *r = ud; cJSON *j = st == 200 ? net_json(b) : NULL; const cJSON *rows = js_arr(j, "result");
  if (!rows) { net_json_free(j); libSyncing = 0; int ch = lib_migrate_legacy(); if (r->cb) r->cb(ch, r->ud); free(r); return; }
  cJSON *lib = lib_arr(); const cJSON *row; cJSON *remote = cJSON_CreateObject();
  /* what the account has newer than us */
  typedef struct { char id[64]; double mt; } Want; static Want wants[2000]; int nw = 0;
  cJSON_ArrayForEach(row, rows) { if (!cJSON_IsArray(row) || cJSON_GetArraySize(row) < 2) continue; const char *id = cJSON_GetArrayItem(row, 0)->valuestring; double mt = tms(cJSON_GetArrayItem(row, 1)); if (!id) continue;
    cJSON_AddNumberToObject(remote, id, mt); cJSON *loc = cJSON_GetObjectItemCaseSensitive(lib, id);
    if ((!loc || tms(cJSON_GetObjectItemCaseSensitive(loc, "_mtime")) < mt) && nw < 2000) { copy_str(wants[nw].id, 64, id); wants[nw].mt = mt; nw++; } }
  /* what we have newer than the account (or it never saw) */
  cJSON *push = cJSON_CreateArray(); int np = 0; const cJSON *it;
  cJSON_ArrayForEach(it, lib) { const char *id = js_str(it, "_id", NULL); if (!id || !strcmp(js_str(it, "type", ""), "other")) continue; const cJSON *rm = cJSON_GetObjectItemCaseSensitive(remote, id);
    int go = rm ? tms(cJSON_GetObjectItemCaseSensitive(it, "_mtime")) > rm->valuedouble : (!js_bool(it, "removed", 0) || js_bool(it, "temp", 0));
    if (go && np < 100) { cJSON_AddItemToArray(push, cJSON_Duplicate(it, 1)); np++; } }
  if (np) { cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "authKey", sb_auth_key()); cJSON_AddStringToObject(o, "collection", "libraryItem"); cJSON_AddItemToObject(o, "changes", push); char *body = cJSON_PrintUnformatted(o); cJSON_Delete(o); net_post_json(STREMIO_API "/api/datastorePut", body, NULL, NULL); free(body); }
  else cJSON_Delete(push);
  cJSON_Delete(remote);
  /* newest first, at most 500 */
  for (int i = 1; i < nw; i++) { Want w = wants[i]; int k = i - 1; while (k >= 0 && wants[k].mt < w.mt) { wants[k + 1] = wants[k]; k--; } wants[k + 1] = w; }
  r->nwant = nw > 500 ? 500 : nw; for (int i = 0; i < r->nwant; i++) copy_str(r->ids[i], 64, wants[i].id); r->at = 0;
  net_json_free(j); pull_next(r);
}
void sb_pull_library(void (*cb)(int, void *), void *ud) {
  const char *ak = sb_auth_key(); if (!ak || libSyncing) { if (cb) cb(0, ud); return; }
  libSyncing = 1; libSyncT = SDL_GetTicks();
  cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "authKey", ak); cJSON_AddStringToObject(o, "collection", "libraryItem"); char *body = cJSON_PrintUnformatted(o); cJSON_Delete(o);
  PullReq *r = calloc(1, sizeof *r); r->cb = cb; r->ud = ud; net_post_json(STREMIO_API "/api/datastoreMeta", body, on_pull_meta, r); free(body);
}

/* ---- streams ------------------------------------------------------------------------------- */
/* the subtitles stream objects carried (stream.subtitles), by the stream's address: the player asks for them */
typedef struct { unsigned long long h; SubEntry *subs; int n; } StreamSubs;
static StreamSubs ssubs[128]; static int ssubsAt;
static unsigned long long str_hash(const char *s) { unsigned long long h = 1469598103934665603ULL; for (; s && *s; s++) h = (h ^ (unsigned char)*s) * 1099511628211ULL; return h; }
static void stream_subs_keep(const char *key, const cJSON *arr, const char *addon) {
  int n = cJSON_GetArraySize(arr); if (!key[0] || n <= 0) return; if (n > 40) n = 40;
  unsigned long long h = str_hash(key); StreamSubs *e = NULL; for (int i = 0; i < 128; i++) if (ssubs[i].h == h) e = &ssubs[i];
  if (!e) { e = &ssubs[ssubsAt]; ssubsAt = (ssubsAt + 1) % 128; } free(e->subs); e->h = h; e->subs = calloc(n, sizeof *e->subs); e->n = 0;
  const cJSON *x; cJSON_ArrayForEach(x, arr) { if (e->n >= n) break; const char *u = js_str(x, "url", NULL); if (!u || !*u) continue; SubEntry *o = &e->subs[e->n++];
    copy_str(o->url, sizeof o->url, u); copy_str(o->lang, sizeof o->lang, js_str(x, "lang", "und")); for (char *p = o->lang; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;
    copy_str(o->label, sizeof o->label, js_str(x, "label", "")); copy_str(o->addon, sizeof o->addon, addon); o->from = SUB_STREAM; o->n = e->n; }
}
int sb_stream_subs(const Stream *s, SubEntry **out) {
  *out = NULL; unsigned long long h = str_hash(s->url[0] ? s->url : s->ext);
  for (int i = 0; i < 128; i++) if (ssubs[i].h == h && ssubs[i].n) { *out = malloc(ssubs[i].n * sizeof **out); memcpy(*out, ssubs[i].subs, ssubs[i].n * sizeof **out); return ssubs[i].n; }
  return 0;
}
static void hdr_lines(const cJSON *o, char *out, size_t cap) { /* {"User-Agent": "x"} -> "User-Agent: x\n" */
  out[0] = 0; size_t l = 0; const cJSON *h; if (cJSON_IsObject(o)) cJSON_ArrayForEach(h, o) { if (!cJSON_IsString(h) || !h->string || strpbrk(h->string, "\r\n:") || strpbrk(h->valuestring, "\r\n")) continue; /* no header smuggling */
    int k = snprintf(out + l, cap - l, "%s: %s\n", h->string, h->valuestring); if (k < 0 || (size_t)k >= cap - l) { out[l] = 0; break; } l += (size_t)k; }
}
/* a magnet link as the engine's address: its hash and trackers */
static int magnet_url(const char *m, char *out, size_t cap) {
  const char *x = strstr(m, "xt=urn:btih:"); if (!x) return 0; x += 12; int n = 0; while (n < 40 && ((x[n] >= '0' && x[n] <= '9') || (x[n] >= 'a' && x[n] <= 'f') || (x[n] >= 'A' && x[n] <= 'F'))) n++; if (n != 40) return 0;
  int o = snprintf(out, cap, "p2p://%.40s/?", x), nt = 0;
  for (const char *t = strstr(m, "tr="); t && nt < 12; t = strstr(t + 3, "tr=")) { if (t[-1] != '&' && t[-1] != '?') continue; size_t l = strcspn(t + 3, "&"); if (o + l + 5 < cap) { o += snprintf(out + o, cap - o, "%str=%.*s", nt ? "&" : "", (int)l, t + 3); nt++; } }
  return 1;
}
/* one stream object as the app keeps it; 0 when it has nothing to play or open */
static int stream_parse(Stream *o, const cJSON *s, const char *addon) {
  memset(o, 0, sizeof *o); if (!cJSON_IsObject(s)) return 0;
  const char *url = js_str(s, "url", NULL), *yt = js_str(s, "ytId", NULL), *ih = js_str(s, "infoHash", NULL), *ext = js_str(s, "externalUrl", NULL);
  if (url && !strncmp(url, "magnet:", 7)) { if (!magnet_url(url, o->url, sizeof o->url)) return 0; o->p2p = 1; }
  else if (url && *url) { if (strlen(url) >= sizeof o->url) { fprintf(stderr, "[streams] %s: a %u-byte address is too long to keep, left out\n", addon, (unsigned)strlen(url)); return 0; } copy_str(o->url, sizeof o->url, url); } /* cut short it would only ever 404 */
  else if (yt && *yt) snprintf(o->url, sizeof o->url, "ytid:%s", yt); /* played through the trailers' YouTube path */
  else if (ih && *ih) { const cJSON *fi = cJSON_GetObjectItemCaseSensitive(s, "fileIdx"); int o2 = cJSON_IsNumber(fi) ? snprintf(o->url, sizeof o->url, "p2p://%s/%d?", ih, (int)fi->valuedouble) : snprintf(o->url, sizeof o->url, "p2p://%s/?", ih);
    const cJSON *src = js_arr(s, "sources"), *t; int nt = 0; if (src) cJSON_ArrayForEach(t, src) { if (nt >= 12 || !cJSON_IsString(t)) continue; if (!strncmp(t->valuestring, "tracker:", 8)) { char e[512]; urlenc(t->valuestring + 8, e, sizeof e); o2 += snprintf(o->url + o2, sizeof o->url - o2, "%str=%s", nt ? "&" : "", e); nt++; } }
    o->p2p = 1; }
  else if (ext && *ext) copy_str(o->ext, sizeof o->ext, ext); /* a web page (or a stremio:// link): another device opens it */
  else return 0;
  copy_str(o->addon, sizeof o->addon, addon); copy_str(o->name, sizeof o->name, js_str(s, "name", "")); copy_str(o->title, sizeof o->title, js_str(s, "description", js_str(s, "title", "")));
  const cJSON *bh = js_obj(s, "behaviorHints"); copy_str(o->filename, sizeof o->filename, js_str(bh, "filename", "")); copy_str(o->binge, sizeof o->binge, js_str(bh, "bingeGroup", ""));
  o->notWeb = js_bool(bh, "notWebReady", 0); o->size = js_numstr(bh, "videoSize", 0); copy_str(o->hash, sizeof o->hash, js_str(bh, "videoHash", ""));
  const cJSON *ph = js_obj(bh, "proxyHeaders"); if (o->url[0] && !o->p2p && strncmp(o->url, "ytid:", 5)) { hdr_lines(js_obj(ph, "request"), o->hdrs, sizeof o->hdrs); hdr_lines(js_obj(ph, "response"), o->rhdrs, sizeof o->rhdrs); }
  const cJSON *subs = js_arr(s, "subtitles"); o->nsubs = subs ? cJSON_GetArraySize(subs) : 0; if (o->nsubs) stream_subs_keep(o->url[0] ? o->url : o->ext, subs, addon);
  return 1;
}
typedef struct { unsigned tok; int pending, total, cancelled, count; StreamPartCb part; StreamDoneCb done; StreamAnsCb ans; void *ud; cJSON *own; } StreamReq;
typedef struct { StreamReq *req; int idx; char name[96]; } StreamPart;
static unsigned streamTok;
static void streams_answer(StreamPart *p, const cJSON *arr) {
  StreamReq *r = p->req; const cJSON *s;
  StreamGroup g; copy_str(g.name, sizeof g.name, p->name); int na = arr ? cJSON_GetArraySize(arr) : 0; if (!r->ans && na > 40) na = 40; /* the best-stream pick reads all of them, as the web does: a 4K-first addon lists its 1080p files late */
  g.items = calloc(na ? na : 1, sizeof(Stream)); g.n = 0;
  if (arr) cJSON_ArrayForEach(s, arr) { if (g.n >= na) break; if (stream_parse(&g.items[g.n], s, p->name)) g.n++; }
  r->total += g.n; if (g.n) r->part(&g, p->idx, r->ud); else free(g.items);
}
static void stream_part_done(StreamPart *p) {
  StreamReq *r = p->req;
  --r->pending; if (!r->cancelled && r->ans) r->ans(r->pending, r->count, r->ud);
  if (r->pending <= 0) { if (!r->cancelled) r->done(r->total, r->ud); cJSON_Delete(r->own); free(r); }
  free(p);
}
static void on_streams(int st, const unsigned char *b, size_t n, void *ud) {
  StreamPart *p = ud; StreamReq *r = p->req;
  if (!r->cancelled) { cJSON *j = st == 200 ? net_json(b) : NULL;
    if (st != 200 || !j) fprintf(stderr, "[addon] streams from %s: %s\n", p->name, st == 200 ? "not JSON" : st > 0 ? "HTTP error" : "no answer");
    streams_answer(p, js_arr(j, "streams")); net_json_free(j); }
  stream_part_done(p);
}
/* a title's own video streams (meta.videos[].streams): they replace the addons' for that video, as the official apps do */
static const cJSON *video_streams(const char *type, const char *vid) {
  for (int i = 0; i < 40; i++) { const cJSON *m = metaMem[i].meta; if (!m || strncmp(metaMem[i].key, type, strlen(type)) || metaMem[i].key[strlen(type)] != '_') continue;
    const cJSON *v; cJSON_ArrayForEach(v, js_arr(m, "videos")) if (!strcmp(js_str(v, "id", ""), vid)) { const cJSON *ss = js_arr(v, "streams"); if (ss && cJSON_GetArraySize(ss)) return ss; } }
  return NULL;
}
static void on_own_streams(int st, const unsigned char *b, size_t n, void *ud) { (void)st; (void)b; (void)n; StreamPart *p = ud; if (!p->req->cancelled) streams_answer(p, p->req->own); stream_part_done(p); }
unsigned sb_streams(const char *type, const char *vid, StreamPartCb part, StreamDoneCb done, void *ud) { return sb_streams_ex(type, vid, part, done, NULL, ud); }
static unsigned streams_req(const char *type, const char *vid, int only, StreamPartCb part, StreamDoneCb done, StreamAnsCb ans, void *ud);
unsigned sb_streams_ex(const char *type, const char *vid, StreamPartCb part, StreamDoneCb done, StreamAnsCb ans, void *ud) { return streams_req(type, vid, -1, part, done, ans, ud); }
unsigned sb_streams_one(const char *type, const char *vid, int which, StreamPartCb part, StreamDoneCb done, void *ud) { return streams_req(type, vid, which, part, done, NULL, ud); }
int sb_stream_addons(const char *type, const char *vid, char (*names)[96], int max) {
  if (video_streams(type, vid)) { if (max > 0) copy_str(names[0], 96, T("This title")); return max > 0; }
  cJSON *a = sb_addons(); const cJSON *ad; int n = 0;
  if (a) cJSON_ArrayForEach(ad, a) { if (n >= max) break; if (!addon_serves(ad, "stream", type, vid)) continue; copy_str(names[n++], 96, addon_name(ad)); }
  return n;
}
/* only >= 0: that one of the addons serving the video (in the user's order), alone */
static unsigned streams_req(const char *type, const char *vid, int only, StreamPartCb part, StreamDoneCb done, StreamAnsCb ans, void *ud) {
  cJSON *a = sb_addons(); StreamReq *r = calloc(1, sizeof *r); r->tok = ++streamTok; r->part = part; r->done = done; r->ans = ans; r->ud = ud;
  const cJSON *own = video_streams(type, vid);
  if (own && only > 0) own = NULL, a = NULL; /* (its own list is the one group) */
  if (own) { r->own = cJSON_Duplicate(own, 1); StreamPart *p = calloc(1, sizeof *p); p->req = r; copy_str(p->name, sizeof p->name, T("This title")); r->pending = r->count = 1;
    net_file("", on_own_streams, p); return r->tok; } /* (a file that is not there: answered on the next frame, never inside this call) */
  int idx = 0; const cJSON *ad;
  if (a) cJSON_ArrayForEach(ad, a) { if (!addon_serves(ad, "stream", type, vid)) continue; if (only >= 0 && idx != only) { idx++; continue; }
    StreamPart *p = calloc(1, sizeof *p); p->req = r; p->idx = idx++; copy_str(p->name, sizeof p->name, addon_name(ad));
    char base[512], url[900]; addon_base(ad, base, sizeof base); addon_url(url, sizeof url, base, "stream", type, vid, NULL);
    r->pending++; net_get_timeout(url, 45000, on_streams, p); } /* slow debrid addons can take 20-30 s: the phone and PC apps wait as long */
  r->count = r->pending; if (!r->pending) { done(0, ud); free(r); return 0; }
  return r->tok;
}
void sb_stream_info(const Stream *s, char *out, size_t cap) { snprintf(out, cap, "%s %s %s", s->name[0] ? s->name : s->addon, s->title, s->filename); for (char *p = out; *p; p++) if (*p == '\n' || *p == '\r' || *p == '\t') *p = ' '; }
/* a remembered stream is only offered again to the account that played it, and only while the addon it came from is
   still installed and on (another account's, an older one's or a removed addon's streams never come back) */
static int resume_valid(const cJSON *s) {
  const char *acct = js_str(s, "acct", NULL); if (!acct || strcmp(acct, sb_user_id())) return 0;
  const char *an = js_str(s, "addon", ""); cJSON *a = sb_addons(); const cJSON *ad; if (!a || !an[0]) return 0;
  cJSON_ArrayForEach(ad, a) if (sb_addon_on(ad) && !strcmp(addon_name(ad), an)) return 1;
  return 0;
}
int sb_resume_stream(const char *id, const char *vid, Stream *out) {
  char k[200]; snprintf(k, sizeof k, "%s_%s", id, vid); const cJSON *all = store_node("streams"), *s = all ? cJSON_GetObjectItemCaseSensitive(all, k) : NULL; if (!s) return 0;
  if (!resume_valid(s)) { fprintf(stderr, "[resume] %s: the remembered stream (%s) is not this account's or its addon is gone: not used\n", k, js_str(s, "addon", "?")); return 0; }
  memset(out, 0, sizeof *out); copy_str(out->url, sizeof out->url, js_str(s, "url", "")); copy_str(out->name, sizeof out->name, js_str(s, "name", "")); copy_str(out->addon, sizeof out->addon, js_str(s, "addon", ""));
  copy_str(out->title, sizeof out->title, js_str(s, "title", "")); out->p2p = !strncmp(out->url, "p2p://", 6);
  copy_str(out->hdrs, sizeof out->hdrs, js_str(s, "hdrs", "")); copy_str(out->rhdrs, sizeof out->rhdrs, js_str(s, "rhdrs", "")); copy_str(out->binge, sizeof out->binge, js_str(s, "binge", ""));
  copy_str(out->filename, sizeof out->filename, js_str(s, "filename", "")); copy_str(out->hash, sizeof out->hash, js_str(s, "hash", "")); out->size = js_num(s, "size", 0);
  return out->url[0] != 0;
}
void sb_remember_stream(const char *id, const char *vid, const Stream *s) {
  if (!s->url[0]) return;
  cJSON *all = store_node("streams"); if (!cJSON_IsObject(all)) { all = cJSON_CreateObject(); store_set_node("streams", all); }
  char k[200]; snprintf(k, sizeof k, "%s_%s", id, vid); cJSON_DeleteItemFromObjectCaseSensitive(all, k);
  cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "url", s->url); cJSON_AddStringToObject(o, "name", s->name); cJSON_AddStringToObject(o, "addon", s->addon); cJSON_AddStringToObject(o, "title", s->title);
  cJSON_AddStringToObject(o, "acct", sb_user_id()); /* whose it is */
  /* what the stream needs again to play the same way: its headers, its group for the next episode, its file's facts */
  if (s->hdrs[0]) cJSON_AddStringToObject(o, "hdrs", s->hdrs); if (s->rhdrs[0]) cJSON_AddStringToObject(o, "rhdrs", s->rhdrs); if (s->binge[0]) cJSON_AddStringToObject(o, "binge", s->binge);
  if (s->filename[0]) cJSON_AddStringToObject(o, "filename", s->filename); if (s->hash[0]) cJSON_AddStringToObject(o, "hash", s->hash); if (s->size > 0) cJSON_AddNumberToObject(o, "size", s->size);
  cJSON_AddItemToObject(all, k, o);
  if (cJSON_GetArraySize(all) > 60) cJSON_DeleteItemFromArray(all, 0);
  store_set_node("streams", all);
}

/* ---- search suggestions (IMDb's autocomplete) ----------------------------------------------- */
typedef struct { void (*cb)(ItemList *, char[6][128], int, void *); void *ud; } SugReq;
static void on_sug(int st, const unsigned char *b, size_t n, void *ud) {
  SugReq *r = ud; cJSON *j = st == 200 ? net_json(b) : NULL; const cJSON *d = js_arr(j, "d"), *x;
  ItemList *l = calloc(1, sizeof *l); l->items = calloc(16, sizeof(Item)); char names[6][128]; int nn = 0;
  if (d) cJSON_ArrayForEach(x, d) { if (l->n >= 12) break; const char *id = js_str(x, "id", ""), *qid = js_str(x, "qid", ""); if (strncmp(id, "tt", 2)) continue;
    const char *type = (!strcmp(qid, "tvSeries") || !strcmp(qid, "tvMiniSeries")) ? "series" : (!strcmp(qid, "movie") || !strcmp(qid, "tvMovie") || !strcmp(qid, "tvSpecial") || !strcmp(qid, "short") || !strcmp(qid, "video")) ? "movie" : NULL; if (!type) continue;
    Item *it = &l->items[l->n++]; memset(it, 0, sizeof *it); copy_str(it->id, sizeof it->id, id); copy_str(it->type, sizeof it->type, type); copy_str(it->name, sizeof it->name, js_str(x, "l", ""));
    const char *img = js_str(js_obj(x, "i"), "imageUrl", NULL); if (img) { copy_str(it->poster, sizeof it->poster, img); char *v = strstr(it->poster, "._V1_."); if (v) { char ext[8]; copy_str(ext, sizeof ext, v + 5); snprintf(v, sizeof it->poster - (v - it->poster), "._V1_QL75_UX300_%s", ext); } }
    else snprintf(it->poster, sizeof it->poster, "https://images.metahub.space/poster/medium/%s/img", id);
    double y = js_num(x, "y", 0); if (y) snprintf(it->metaLine, sizeof it->metaLine, "%d", (int)y);
    if (nn < 6) { int dup = 0; for (int i = 0; i < nn; i++) if (!strcasecmp(names[i], it->name)) dup = 1; if (!dup) copy_str(names[nn++], 128, it->name); } }
  if (!d) l->total = -1; /* no answer */
  net_json_free(j); r->cb(l, names, nn, r->ud); free(r);
}
void sb_suggest(const char *q, void (*cb)(ItemList *, char[6][128], int, void *), void *ud) {
  char s[128]; copy_str(s, sizeof s, q); for (char *p = s; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;
  char first = (s[0] >= 'a' && s[0] <= 'z') || (s[0] >= '0' && s[0] <= '9') ? s[0] : 'x'; char enc[400]; urlenc(s, enc, sizeof enc);
  char url[600]; snprintf(url, sizeof url, "https://v3.sg.media-imdb.com/suggestion/%c/%s.json", first, enc);
  SugReq *r = malloc(sizeof *r); r->cb = cb; r->ud = ud; net_get_pri(url, on_sug, r);
}
