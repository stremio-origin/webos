#include "platform/plat.h"
#include <pthread.h>
#include "core/notify.h"
#include "core/stremio.h"
#include "core/scrobble.h"
#include "core/traktrows.h"
#include "core/traktwatch.h"
#include "core/forcedsubs.h"
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
static unsigned adGen; static int adGenOk; /* (see addon_gen) */
void sb_forget_local(void) { if (scrobble_state() != TRAKT_OFF) scrobble_unlink(); tw_forget(); /* (Trakt goes with the account: connected under it, never left for the next one) */ store_remove("authKey"); store_remove("user"); store_remove("addons"); store_remove("lib"); store_remove("streams"); store_flush(); lib_invalidate(); adGenOk = 0; }
/* addons */
cJSON *sb_addons(void) { cJSON *a = store_node("addons"); return cJSON_IsArray(a) ? a : NULL; }
/* a number for the addons as they are set up now (their addresses, which carry their configuration): the title
   pages kept in memory and on disk belong to one; set up again on any device, the old copies are not used (an addon
   that puts streams in its title pages would hand out the old configuration's, which it calls obsolete) */
static unsigned addon_gen(void) {
  if (adGenOk) return adGen;
  unsigned h = 2166136261u; cJSON *a = sb_addons(); const cJSON *ad;
  if (a) cJSON_ArrayForEach(ad, a) { for (const char *c = js_str(ad, "transportUrl", ""); *c; c++) h = (h ^ (unsigned char)*c) * 16777619u; h = (h ^ '\n') * 16777619u; }
  adGen = h; adGenOk = 1; return h;
}
/* an addon can be switched off on this TV only (Settings > Addons): a local choice, never synced back to the
   account. Off, it gives no rows, metadata or streams here */
void sb_addon_key(const cJSON *a, char *out, size_t cap) { const char *id = js_str(js_obj(a, "manifest"), "id", NULL); copy_str(out, cap, id && *id ? id : js_str(a, "transportUrl", "")); }
int sb_addon_on(const cJSON *a) { char k[300], sk[320]; sb_addon_key(a, k, sizeof k); snprintf(sk, sizeof sk, "adOn_%s", k); return strcmp(store_get(sk, "true"), "false") != 0; }
typedef struct { void (*cb)(int, void *); void *ud; } ChBox;
/* the last sync's outcome: how many addons came in and went out (by address) */
static int addAdded, addRemoved;
void sb_addon_sync_counts(int *added, int *removed) { if (added) *added = addAdded; if (removed) *removed = addRemoved; }
static int has_addon(const cJSON *list, const char *url) { const cJSON *a; cJSON_ArrayForEach(a, list) if (!strcmp(js_str(a, "transportUrl", ""), url)) return 1; return 0; }
static void on_addons(int st, const unsigned char *b, size_t n, void *ud) {
  ChBox *x = ud; cJSON *j = st == 200 ? net_json(b) : NULL; const cJSON *got = js_arr(js_obj(j, "result"), "addons");
  int changed = 0; addAdded = addRemoved = 0;
  if (!got || !cJSON_GetArraySize(got)) { changed = -1; fprintf(stderr, "[addons] sync failed (%d%s)\n", st, j ? ", no addon list" : ""); } /* no answer: said so, never "up to date" */
  else {
    char *next = cJSON_PrintUnformatted(got); cJSON *cur = sb_addons(); char *curs = cur ? cJSON_PrintUnformatted(cur) : NULL;
    if (!curs || strcmp(curs, next)) {
      const cJSON *a; cJSON_ArrayForEach(a, got) if (!cur || !has_addon(cur, js_str(a, "transportUrl", ""))) addAdded++;
      if (cur) cJSON_ArrayForEach(a, cur) if (!has_addon(got, js_str(a, "transportUrl", ""))) addRemoved++;
      store_set_node("addons", cJSON_Duplicate(got, 1)); store_flush(); changed = 1; adGenOk = 0; }
    fprintf(stderr, "[addons] sync: %d on the account, %d added, %d removed%s\n", cJSON_GetArraySize(got), addAdded, addRemoved, changed ? "" : ", nothing changed");
    free(next); free(curs);
  }
  net_json_free(j); if (x->cb) x->cb(changed, x->ud); free(x);
}
void sb_sync_addons(void (*cb)(int, void *), void *ud) {
  const char *ak = sb_auth_key(); if (!ak) { if (cb) cb(-2, ud); return; }
  cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "authKey", ak); cJSON_AddBoolToObject(o, "update", 1); char *body = cJSON_PrintUnformatted(o); cJSON_Delete(o);
  ChBox *x = malloc(sizeof *x); x->cb = cb; x->ud = ud; net_post_json(STREMIO_API "/api/addonCollectionGet", body, on_addons, x); free(body);
}
/* ---- catalogs ------------------------------------------------------------------------------ */
static const char *type_label(const char *t) { return !strcmp(t, "movie") ? T("Movies") : !strcmp(t, "series") ? T("Series") : t; }
static void def_url(CatalogDef *d) { snprintf(d->url, sizeof d->url, "%s%s%s.json", d->path, d->extra[0] ? "/" : "", d->extra); }
static void def_set(CatalogDef *d, const char *title, const char *type, const char *path, const char *extra, int skip, const char *key) {
  memset(d, 0, sizeof *d); copy_str(d->title, sizeof d->title, title); copy_str(d->type, sizeof d->type, type); copy_str(d->path, sizeof d->path, path);
  copy_str(d->extra, sizeof d->extra, extra ? extra : ""); d->skip = skip; copy_str(d->key, sizeof d->key, key); def_url(d);
}
/* every installed catalog a board can show, in addon order (as the official board): one asked with nothing chosen,
   or with the first option of each required property (Cinemeta's "year" with this year); never one that needs a
   search or a list of ids. from: how many to pass over first */
static int board_defs(CatalogDef *out, int max, int from) {
  int n = 0, seen = 0; cJSON *a = sb_addons(); const cJSON *ad; if (!a) return 0; int ai = 0;
  cJSON_ArrayForEach(ad, a) { if (!sb_addon_on(ad)) { ai++; continue; } const cJSON *cats = js_arr(js_obj(ad, "manifest"), "catalogs"), *cat; char base[512]; addon_base(ad, base, sizeof base); int ci = 0;
    if (cats) cJSON_ArrayForEach(cat, cats) {
      const char *ctype = js_str(cat, "type", NULL), *cid = js_str(cat, "id", NULL); char ex[200];
      if (ctype && cid && *ctype && *cid && addon_cat_default_extra(cat, ex, sizeof ex) == 0 && seen++ >= from && n < max) {
        CatalogDef *d = &out[n++]; const char *nm = js_str(cat, "name", ""); char title[96], key[48], path[512], te[96], ie[300];
        snprintf(title, sizeof title, "%s%s%s", nm, *nm ? " · " : "", type_label(ctype)); snprintf(key, sizeof key, "a%d_%d", ai, ci);
        addon_urlenc(ctype, te, sizeof te); addon_urlenc(cid, ie, sizeof ie); snprintf(path, sizeof path, "%s/catalog/%s/%s", base, te, ie);
        def_set(d, title, ctype, path, ex, addon_cat_extra(cat, "skip", NULL, NULL, NULL), key); }
      ci++; }
    ai++; }
  return n;
}
/* Home: every installed catalog, in the addons' order (it showed the first 8 and dropped the rest) */
int sb_home_defs(CatalogDef *out, int max) {
  int n = board_defs(out, max, 0);
  if (!n) {
    const char *defs[4][4] = { { "Popular · Movies", "movie", CINEMETA "/catalog/movie/top", "cin_m_top" }, { "Popular · Series", "series", CINEMETA "/catalog/series/top", "cin_s_top" },
      { "Top Rated · Movies", "movie", CINEMETA "/catalog/movie/imdbRating", "cin_m_ir" }, { "Top Rated · Series", "series", CINEMETA "/catalog/series/imdbRating", "cin_s_ir" } };
    for (int i = 0; i < 4 && n < max; i++) def_set(&out[n++], defs[i][0], defs[i][1], defs[i][2], NULL, 1, defs[i][3]);
  }
  n += traktrows_defs(out + n, max - n); /* Trakt's rows last, as the official Trakt addon's come (none when the account has that addon) */
  return n;
}
/* Discover, as the official app's: a type, one of that type's catalogs (every installed addon's, in their order, Cinemeta
   first as it is installed first), and the catalog's own genres; the listing is that catalog asked with them. Live
   types stay in All channels. A catalog is offered when it can be asked as it is (a required property: its first option,
   as the official app does) and never when it needs a search or a list of ids */
static int disc_cat_ok(const cJSON *cat, char *ex, size_t cap) { const char *t = js_str(cat, "type", NULL), *id = js_str(cat, "id", NULL);
  return t && id && *t && *id && strcmp(t, "tv") && strcmp(t, "channel") && addon_cat_default_extra(cat, ex, cap) == 0; }
int sb_disc_types(char (*out)[16], int max) {
  int n = 0; cJSON *a = sb_addons(); const cJSON *ad; static const char *const first[] = { "movie", "series" };
  for (int f = 0; f < 3; f++) { if (a) cJSON_ArrayForEach(ad, a) { if (!sb_addon_on(ad)) continue; const cJSON *cats = js_arr(js_obj(ad, "manifest"), "catalogs"), *cat;
      if (cats) cJSON_ArrayForEach(cat, cats) { char ex[200]; if (!disc_cat_ok(cat, ex, sizeof ex)) continue; const char *t = js_str(cat, "type", "");
        if (f < 2 ? strcmp(t, first[f]) : (!strcmp(t, "movie") || !strcmp(t, "series"))) continue; /* (films, then series, then the rest as met) */
        int dup = 0; for (int i = 0; i < n && !dup; i++) dup = !strcmp(out[i], t); if (!dup && n < max) copy_str(out[n++], 16, t); } } }
  if (!n && max >= 2) { copy_str(out[0], 16, "movie"); copy_str(out[1], 16, "series"); n = 2; }
  return n;
}
int sb_disc_cats(const char *type, CatalogDef *out, char (*addon)[64], int max) {
  int n = 0, ai = 0; cJSON *a = sb_addons(); const cJSON *ad;
  if (a) cJSON_ArrayForEach(ad, a) { if (!sb_addon_on(ad)) { ai++; continue; } const cJSON *cats = js_arr(js_obj(ad, "manifest"), "catalogs"), *cat; char base[512]; addon_base(ad, base, sizeof base); int ci = 0;
    if (cats) cJSON_ArrayForEach(cat, cats) { char ex[200];
      if (n < max && disc_cat_ok(cat, ex, sizeof ex) && !strcmp(js_str(cat, "type", ""), type)) {
        char key[48], path[512], te[96], ie[300]; const char *nm = js_str(cat, "name", ""), *cid = js_str(cat, "id", "");
        snprintf(key, sizeof key, "dc%d_%d", ai, ci); addon_urlenc(type, te, sizeof te); addon_urlenc(cid, ie, sizeof ie); snprintf(path, sizeof path, "%s/catalog/%s/%s", base, te, ie);
        def_set(&out[n], *nm ? nm : cid, type, path, ex, addon_cat_extra(cat, "skip", NULL, NULL, NULL), key);
        if (addon) copy_str(addon[n], 64, js_str(js_obj(ad, "manifest"), "name", "")); n++; }
      ci++; }
    ai++; }
  if (!n && max > 0) { int sr = !strcmp(type, "series"); def_set(&out[0], "Popular", sr ? "series" : "movie", sr ? CINEMETA "/catalog/series/top" : CINEMETA "/catalog/movie/top", NULL, 1, sr ? "dcs" : "dcm"); if (addon) copy_str(addon[0], 64, "Cinemeta"); n = 1; }
  return n;
}
/* the catalog behind a Discover def (its key "dc<addon>_<catalog>") */
static const cJSON *disc_cat_of(const CatalogDef *d) { int ai, ci; if (sscanf(d->key, "dc%d_%d", &ai, &ci) != 2) return NULL; cJSON *a = sb_addons(); const cJSON *ad = a ? cJSON_GetArrayItem(a, ai) : NULL;
  return ad ? cJSON_GetArrayItem(js_arr(js_obj(ad, "manifest"), "catalogs"), ci) : NULL; }
int sb_disc_cat_genres(const CatalogDef *d, char (*out)[48], int max, int *required) {
  *required = 0; const cJSON *cat = disc_cat_of(d), *opts = NULL, *o; int req = 0, n = 0; static const char *const G[] = { "genre" };
  if (!cat || !addon_cat_extra(cat, "genre", &req, &opts, NULL) || !opts) return 0;
  *required = req; (void)G;
  cJSON_ArrayForEach(o, opts) if (cJSON_IsString(o) && o->valuestring[0] && n < max) copy_str(out[n++], 48, o->valuestring);
  return n;
}
/* the catalog asked with that genre (none: as it is) */
void sb_disc_with_genre(CatalogDef *d, const char *genre) {
  const cJSON *cat = disc_cat_of(d); char ex[200] = "";
  if (cat) { const char *nm; int req; const cJSON *o; addon_cat_default_extra(cat, ex, sizeof ex);
    if (genre && *genre) { ex[0] = 0; addon_extra_add(ex, sizeof ex, "genre", genre); /* (its other required ones, as they were) */
      char dflt[200]; addon_cat_default_extra(cat, dflt, sizeof dflt); (void)nm; (void)req; (void)o;
      if (dflt[0] && !strstr(dflt, "genre=")) { size_t l = strlen(ex); snprintf(ex + l, sizeof ex - l, "&%s", dflt); } } }
  else if (genre && *genre) addon_extra_add(ex, sizeof ex, "genre", genre);
  copy_str(d->extra, sizeof d->extra, ex); def_url(d);
}
/* Discover: Cinemeta's trending and top rated, its genres (the ones its manifest offers), then every installed catalog */
int sb_disc_defs(CatalogDef *out, int max) {
  const char *defs[3][4] = { { "Trending · Movies", "movie", CINEMETA "/catalog/movie/top", "d_m_top" }, { "Trending · Series", "series", CINEMETA "/catalog/series/top", "d_s_top" }, { "Top Rated · Movies", "movie", CINEMETA "/catalog/movie/imdbRating", "d_m_ir" } };
  const char *genres[5] = { "Action", "Comedy", "Sci-Fi", "Drama", "Horror" };
  int n = 0;
  for (int i = 0; i < 3 && n < max; i++) def_set(&out[n++], defs[i][0], defs[i][1], defs[i][2], NULL, 1, defs[i][3]);
  for (int i = 0; i < 5 && n < max; i++) { char ex[64] = "", key[16]; addon_extra_add(ex, sizeof ex, "genre", genres[i]); snprintf(key, sizeof key, "d_g_%d", i); def_set(&out[n++], genres[i], "movie", CINEMETA "/catalog/movie/top", ex, 1, key); }
  if (n < max) n += board_defs(out + n, max - n, 0);
  return n;
}
/* Discover's filters: a type ("" films and series, "movie", "series") and a genre ("" any). With a genre: every
   installed catalog of that type that offers it and can be asked with it alone, asked with it (Cinemeta's when none) */
static int type_ok(const char *t, const char *want) { return *want ? !strcmp(t, want) : (!strcmp(t, "movie") || !strcmp(t, "series")); }
static int has_opt(const cJSON *opts, const char *v) { const cJSON *o; cJSON_ArrayForEach(o, opts) if (cJSON_IsString(o) && !strcmp(o->valuestring, v)) return 1; return 0; }
int sb_disc_defs_f(CatalogDef *out, int max, const char *type, const char *genre) {
  if (!*type && !*genre) return sb_disc_defs(out, max);
  int n = 0;
  if (!*genre) { static CatalogDef all[128]; int m = sb_disc_defs(all, 128); for (int i = 0; i < m && n < max; i++) if (type_ok(all[i].type, type)) out[n++] = all[i]; return n; }
  static const char *const G[] = { "genre" }; cJSON *a = sb_addons(); const cJSON *ad; int ai = 0;
  if (a) cJSON_ArrayForEach(ad, a) { if (!sb_addon_on(ad)) { ai++; continue; } const cJSON *cats = js_arr(js_obj(ad, "manifest"), "catalogs"), *cat; char base[512]; addon_base(ad, base, sizeof base); int ci = 0;
    if (cats) cJSON_ArrayForEach(cat, cats) { const char *ct = js_str(cat, "type", ""), *cid = js_str(cat, "id", ""); const cJSON *opts = NULL;
      if (n < max && *cid && type_ok(ct, type) && addon_cat_extra(cat, "genre", NULL, &opts, NULL) && opts && has_opt(opts, genre) && addon_cat_accepts(cat, G, 1)) {
        char ex[200] = "", title[96], key[48], path[512], te[96], ie[300]; const char *nm = js_str(cat, "name", ""); addon_extra_add(ex, sizeof ex, "genre", genre);
        snprintf(title, sizeof title, "%s%s%s", nm, *nm ? " · " : "", type_label(ct)); snprintf(key, sizeof key, "f%d_%d", ai, ci);
        addon_urlenc(ct, te, sizeof te); addon_urlenc(cid, ie, sizeof ie); snprintf(path, sizeof path, "%s/catalog/%s/%s", base, te, ie);
        def_set(&out[n++], title, ct, path, ex, addon_cat_extra(cat, "skip", NULL, NULL, NULL), key); }
      ci++; }
    ai++; }
  if (!n) { const char *ty[2] = { "movie", "series" }; for (int i = 0; i < 2 && n < max; i++) { if (!type_ok(ty[i], type)) continue; char ex[200] = "", path[200], key[16]; addon_extra_add(ex, sizeof ex, "genre", genre); snprintf(path, sizeof path, CINEMETA "/catalog/%s/top", ty[i]); snprintf(key, sizeof key, "fc_%d", i);
      def_set(&out[n++], i ? "Popular · Series" : "Popular · Movies", ty[i], path, ex, 1, key); } }
  return n;
}
/* the genres the installed catalogs of that type offer (each once, in the order met); a short list when none says */
int sb_disc_genres(char (*out)[48], int max, const char *type) {
  int n = 0; static const char *const G[] = { "genre" }; cJSON *a = sb_addons(); const cJSON *ad;
  if (a) cJSON_ArrayForEach(ad, a) { if (!sb_addon_on(ad)) continue; const cJSON *cats = js_arr(js_obj(ad, "manifest"), "catalogs"), *cat;
    if (cats) cJSON_ArrayForEach(cat, cats) { const cJSON *opts = NULL, *o; if (!type_ok(js_str(cat, "type", ""), type) || !addon_cat_extra(cat, "genre", NULL, &opts, NULL) || !opts || !addon_cat_accepts(cat, G, 1)) continue;
      cJSON_ArrayForEach(o, opts) { if (!cJSON_IsString(o) || !o->valuestring[0] || n >= max) continue; int dup = 0; for (int i = 0; i < n && !dup; i++) dup = !strcasecmp(out[i], o->valuestring); if (!dup) snprintf(out[n++], 48, "%s", o->valuestring); } } }
  if (!n) { static const char *const D[] = { "Action", "Adventure", "Animation", "Comedy", "Crime", "Documentary", "Drama", "Family", "Fantasy", "History", "Horror", "Mystery", "Romance", "Sci-Fi", "Thriller", "War", "Western" };
    for (int i = 0; i < (int)(sizeof D / sizeof *D) && n < max; i++) snprintf(out[n++], 48, "%s", D[i]); }
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
typedef struct { char url[512], type[16], key[600]; int n; /* key: "cat:" + the whole address (cut short, every catalog of one addon shared one file) */ void (*cb)(ItemList *, int, void *); void *ud; char *cachedJson; int force; } CatReq; /* force: asked past the addon's cacheMaxAge, the kept copy not shown again */
/* a big list (a live addon's 500 channels: turning it into items and its slim copy took 100-450 ms) is done on a
   thread of its own and handed back on the next frame; the rows' small lists stay as they were (at once) */
#define CAT_BIG 100
/* the addon answered and no longer serves this list: a page instead of JSON (a retired server redirects to a web page),
   an empty list, or 404/410. Its kept copy goes and the list is given as gone (total -2): a server that does not
   answer (offline, a timeout, 5xx, 429) keeps the kept copy */
static int cat_gone(int st, const cJSON *j) { const cJSON *m = js_arr(j, "metas"); return (st == 200 && (!j || (m && !cJSON_GetArraySize(m)))) ? 1 : (st == 404 || st == 410) ? 2 : 0; } /* 1: once (a sign-in page, a source down for a while), 2: for sure */
/* a list counts as gone on a second launch that finds it so (a hotel's sign-in page or an addon's source down for an
   hour leaves it), at once for a 404/410; any answer with channels clears the count */
static long launchId(void) { static long id; if (!id) id = (long)time(NULL); return id; }
static int cat_strike(const CatReq *r, int how) { char k[620]; snprintf(k, sizeof k, "gone:%s", r->url);
  if (!how) { size_t l; unsigned char *d = cache_read(k, 0, &l); if (d && l > 1) cache_write(k, (const unsigned char *)"0", 1); free(d); return 0; }
  if (how == 2) return 1; size_t l; unsigned char *d = cache_read(k, 0, &l); long was = d ? atol((const char *)d) : 0; free(d);
  if (was > 0 && was != launchId()) return 1; if (was != launchId()) fprintf(stderr, "[addon] a list answered without channels: kept, dropped if the next launch finds it so too\n"); char v[24]; int n = snprintf(v, sizeof v, "%ld", launchId()); cache_write(k, (const unsigned char *)v, (size_t)n); return 0; }
static void cat_drop(CatReq *r, void (*cb)(ItemList *, int, void *), void *ud) { cache_write(r->key, (const unsigned char *)"[]", 2); fprintf(stderr, "[addon] a list the addon no longer serves: its kept copy dropped\n");
  ItemList *l = calloc(1, sizeof *l); l->items = calloc(1, sizeof(Item)); l->total = -2; cb(l, 0, ud); }
typedef struct { CatReq *r; cJSON *j; ItemList *l; int st, same, fresh, cached, age, none, gone; } CatJob;
static void cat_work(CatJob *k);
/* the big lists asked this launch (drawing thread only): the once-a-launch recheck skips those already asked */
static int big_asked(const char *url, int mark) { static unsigned H[256]; unsigned h = 2166136261u; for (const char *p = url; *p; p++) h = (h ^ (unsigned char)*p) * 16777619u; if (!h) h = 1;
  unsigned i = h & 255; for (int n = 0; n < 256 && H[i]; n++, i = (i + 1) & 255) if (H[i] == h) return 1; if (mark && !H[i]) H[i] = h; return 0; }
static void on_cat_job(int st, const unsigned char *b, size_t n, void *ud);
/* a kept copy under 12 h old stays on screen (the fresh order shows next launch: rows never reshuffle under the remote),
   except where the list is about now (#81: live sports' times and ids went stale): an events or sport catalog, or an
   addon saying its list ages within the hour (cacheMaxAge; 0: never kept as current), whose fresh answer always
   shows. Longer ages (Cinemeta says 4 h) keep the 12 h rule */
static int cat_kept_fresh(const char *key, const cJSON *j, const char *type) {
  static const char *const LIVE[] = { "events", "event", "sport", "sports", "live" }; /* (TV channels: their own once-a-launch recheck, live.c) */
  for (unsigned i = 0; i < sizeof LIVE / sizeof *LIVE; i++) if (type && !strcmp(type, LIVE[i])) return 0;
  AddonCache h; addon_cache_hints(j, &h); char cp[1200]; if (h.maxAge == 0) return 0;
  return cache_file(key, h.maxAge > 0 && h.maxAge < 3600 ? h.maxAge : 43200, cp, sizeof cp); }
static void *cat_job_run(void *a) { plat_thread_background(5); CatJob *k = a; cat_work(k); net_file("", on_cat_job, k); return NULL; } /* (net_file of nothing: answered on the drawing thread next frame) */
static void on_catalog(int st, const unsigned char *b, size_t len, void *ud) {
  CatReq *r = ud;
  if (r->n > CAT_BIG) { CatJob *k = calloc(1, sizeof *k); k->r = r; k->st = st; k->j = st == 200 ? net_json(b) : NULL; pthread_t t;
    if (pthread_create(&t, NULL, cat_job_run, k) == 0) { pthread_detach(t); return; }
    cat_work(k); on_cat_job(0, NULL, 0, k); return; } /* (no thread: here, as before) */
  Uint32 tq0 = SDL_GetTicks(); cJSON *j = st == 200 ? net_json(b) : NULL; const cJSON *metas = js_arr(j, "metas");
  if (metas && cJSON_GetArraySize(metas)) {
    ItemList *l = list_from_metas(metas, r->type, r->n); Uint32 tq1 = SDL_GetTicks();
    /* the slim list is what is cached; a copy under 12 h old stays on screen (the fresh order shows next launch) */
    cJSON *slim = cJSON_CreateArray(); for (int i = 0; i < l->n; i++) { Item *it = &l->items[i]; cJSON *o = cJSON_CreateObject();
      cJSON_AddStringToObject(o, "id", it->id); cJSON_AddStringToObject(o, "type", it->type); cJSON_AddStringToObject(o, "name", it->name); cJSON_AddStringToObject(o, "poster", it->poster);
      cJSON_AddStringToObject(o, "background", it->background); cJSON_AddStringToObject(o, "logo", it->logo); cJSON_AddStringToObject(o, "description", it->description); cJSON_AddStringToObject(o, "metaLine", it->metaLine); cJSON_AddItemToArray(slim, o); }
    char *txt = cJSON_PrintUnformatted(slim); cJSON_Delete(slim);
    int same = r->cachedJson && !strcmp(r->cachedJson, txt);
    int fresh = r->cachedJson && cat_kept_fresh(r->key, j, r->type);
    if (!fresh) cache_write(r->key, (unsigned char *)txt, strlen(txt));
    { AddonCache h; addon_cache_hints(j, &h); char hk[620], v[24]; snprintf(hk, sizeof hk, "cah:%s", r->url); int k = snprintf(v, sizeof v, "%ld", h.maxAge > 0 ? h.maxAge : 0); cache_write(hk, (unsigned char *)v, (size_t)k); } /* how long the addon says it holds */
    Uint32 tq2 = SDL_GetTicks(); if (!same && !fresh) r->cb(l, 0, r->ud); else itemlist_free(l);
    if (getenv("SB_SLOWLOG")) fprintf(stderr, "[cat] %d metas, %d kept: list %u ms, slim %u ms, page %u ms (%s)\n", cJSON_GetArraySize(metas), r->n, tq1 - tq0, tq2 - tq1, SDL_GetTicks() - tq2, same ? "same" : fresh ? "fresh copy shown" : "new");
    free(txt);
  } else if (!r->cachedJson) { if (!metas) { char h[96] = ""; const char *p = strstr(r->url, "://"); if (p) { p += 3; size_t k = strcspn(p, "/:?"); if (k > 90) k = 90; memcpy(h, p, k); h[k] = 0; } fprintf(stderr, "[catalog] %s answered %d with no list\n", h, st); } /* (the host only: an address carries an addon's private settings) */
    ItemList *l = calloc(1, sizeof *l); l->items = calloc(1, sizeof(Item)); l->total = metas ? 0 : -1; r->cb(l, 0, r->ud); } /* no answer at all: -1 */
  net_json_free(j); free(r->cachedJson); free(r);
}
/* the same work as on_catalog's, off the drawing thread: the items and the slim copy compared with the one kept */
static void cat_work(CatJob *k) {
  CatReq *r = k->r; Uint32 tq0 = SDL_GetTicks(); const cJSON *metas = js_arr(k->j, "metas");
  if (metas && cJSON_GetArraySize(metas)) { cat_strike(r, 0);
    ItemList *l = list_from_metas(metas, r->type, r->n);
    cJSON *slim = cJSON_CreateArray(); for (int i = 0; i < l->n; i++) { Item *it = &l->items[i]; cJSON *o = cJSON_CreateObject();
      cJSON_AddStringToObject(o, "id", it->id); cJSON_AddStringToObject(o, "type", it->type); cJSON_AddStringToObject(o, "name", it->name); cJSON_AddStringToObject(o, "poster", it->poster);
      cJSON_AddStringToObject(o, "background", it->background); cJSON_AddStringToObject(o, "logo", it->logo); cJSON_AddStringToObject(o, "description", it->description); cJSON_AddStringToObject(o, "metaLine", it->metaLine); cJSON_AddItemToArray(slim, o); }
    char *txt = cJSON_PrintUnformatted(slim); cJSON_Delete(slim);
    k->same = r->cachedJson && txt && !strcmp(r->cachedJson, txt);
    k->fresh = r->cachedJson && cat_kept_fresh(r->key, k->j, r->type);
    if (!k->fresh && txt) cache_write(r->key, (unsigned char *)txt, strlen(txt));
    { AddonCache h; addon_cache_hints(k->j, &h); char hk[620], v[24]; snprintf(hk, sizeof hk, "cah:%s", r->url); int n = snprintf(v, sizeof v, "%ld", h.maxAge > 0 ? h.maxAge : 0); cache_write(hk, (unsigned char *)v, (size_t)n); }
    if (getenv("SB_SLOWLOG")) fprintf(stderr, "[cat] %d metas, %d kept, on its own thread: %u ms (%s)\n", cJSON_GetArraySize(metas), r->n, SDL_GetTicks() - tq0, k->same ? "same" : k->fresh ? "fresh copy shown" : "new");
    free(txt); k->l = l; }
  else { k->none = 1; int g = cat_gone(k->st, k->j); k->gone = g && r->cachedJson && cat_strike(r, g); } /* (no answer, 5xx, 429: nothing counted, nothing cleared) */
  net_json_free(k->j); k->j = NULL;
}
static void on_cat_job(int st, const unsigned char *b, size_t n, void *ud) { (void)st; (void)b; (void)n; CatJob *k = ud; CatReq *r = k->r;
  if (k->l) { if (!k->same && !k->fresh) r->cb(k->l, 0, r->ud); else itemlist_free(k->l); }
  else if (k->gone) cat_drop(r, r->cb, r->ud);
  else if (k->none && !r->cachedJson) { ItemList *l = calloc(1, sizeof *l); l->items = calloc(1, sizeof(Item)); l->total = -1; r->cb(l, 0, r->ud); } /* no answer at all: -1 */
  free(r->cachedJson); free(r); free(k); }
/* a big list's kept copy: read and turned into items off the drawing thread too, then asked for as before */
static void on_cat_cached(int st, const unsigned char *b, size_t n, void *ud) { (void)st; (void)b; (void)n; CatJob *k = ud; CatReq *r = k->r;
  if (k->l) r->cb(k->l, 1, r->ud);
  if (k->age) { free(r->cachedJson); free(r); free(k); return; } /* (younger than the addon's cacheMaxAge: no request) */
  free(k); big_asked(r->url, 1); if (traktrows_is(r->url)) traktrows_fetch(r->url, on_catalog, r); else net_get(r->url, on_catalog, r); }
static void *cat_cached_run(void *a) { plat_thread_background(5); CatJob *k = a; CatReq *r = k->r;
  size_t cl; unsigned char *t = cache_read(r->key, 0, &cl);
  if (t) { cJSON *c = cJSON_Parse((const char *)t); if (cJSON_IsArray(c) && cJSON_GetArraySize(c)) { r->cachedJson = (char *)t; if (!r->force) k->l = list_from_metas(c, r->type, r->n); } else free(t); cJSON_Delete(c); }
  if (r->cachedJson) { char hk[620], cp[1200]; snprintf(hk, sizeof hk, "cah:%s", r->url); size_t hl; unsigned char *h = cache_read(hk, 0, &hl); long age = h ? atol((const char *)h) : 0; free(h);
    k->age = age > 0 && cache_file(r->key, age, cp, sizeof cp) && !r->force; }
  net_file("", on_cat_cached, k); return NULL; }
void sb_catalog_url(const char *url, const char *type, int n, void (*cb)(ItemList *, int, void *), void *ud) {
  CatReq *r = calloc(1, sizeof *r); copy_str(r->url, sizeof r->url, url); copy_str(r->type, sizeof r->type, type); snprintf(r->key, sizeof r->key, "cat:%s", url); r->n = n; r->cb = cb; r->ud = ud;
  if (n > CAT_BIG) { CatJob *k = calloc(1, sizeof *k); k->r = r; pthread_t t; if (pthread_create(&t, NULL, cat_cached_run, k) == 0) { pthread_detach(t); return; } free(k); } /* (a big list: its kept copy read on its own thread) */
  size_t cl; unsigned char *t = cache_read(r->key, 0, &cl);
  if (t) { cJSON *c = cJSON_Parse((const char *)t); if (cJSON_IsArray(c) && cJSON_GetArraySize(c)) { r->cachedJson = (char *)t; cb(list_from_metas(c, type, n), 1, ud); } else free(t); cJSON_Delete(c); }
  /* the addon's own cacheMaxAge: a copy younger than that is the answer (no request) */
  if (r->cachedJson) { char hk[620], cp[1200]; snprintf(hk, sizeof hk, "cah:%s", url); size_t hl; unsigned char *h = cache_read(hk, 0, &hl); long age = h ? atol((const char *)h) : 0; free(h);
    if (age > 0 && cache_file(r->key, age, cp, sizeof cp)) { free(r->cachedJson); free(r); return; } }
  if (traktrows_is(url)) { traktrows_fetch(url, on_catalog, r); return; } /* (Trakt's rows: asked of Trakt with the account's token, answered as an addon would) */
  net_get_timeout(url, 45000, on_catalog, r); /* (an addon that makes its lists on demand, AIOMetadata from TMDB, took more than the 20 s of other calls: the row stayed hidden) */
}
/* a big list asked again past the addon's cacheMaxAge (its kept copy is not handed back again: only a change, or gone) */
void sb_catalog_recheck(const char *url, const char *type, int n, void (*cb)(ItemList *, int, void *), void *ud) { if (big_asked(url, 1)) return; /* (asked already this launch) */
  CatReq *r = calloc(1, sizeof *r); copy_str(r->url, sizeof r->url, url); copy_str(r->type, sizeof r->type, type); snprintf(r->key, sizeof r->key, "cat:%s", url); r->n = n > CAT_BIG ? n : CAT_BIG + 1; r->cb = cb; r->ud = ud; r->force = 1;
  CatJob *k = calloc(1, sizeof *k); k->r = r; pthread_t t; if (pthread_create(&t, NULL, cat_cached_run, k) == 0) { pthread_detach(t); return; } free(k); free(r); }
void sb_catalog(const CatalogDef *d, void (*cb)(ItemList *, int, void *), void *ud) { sb_catalog_url(d->url, d->type, 16, cb, ud); }
/* a further page: the extra the catalog is shown with, and skip */
typedef struct { char type[16]; void (*cb)(ItemList *, int, void *); void *ud; } PageReq;
/* a further page: its items made on a thread of their own (up to 200: 10-20 ms on a TV's drawing thread), handed back
   on the next frame */
typedef struct { PageReq *r; cJSON *j; int st, more; ItemList *l; } PageJob;
static void page_work(PageJob *k) { const cJSON *metas = js_arr(k->j, "metas");
  const cJSON *hm = cJSON_GetObjectItemCaseSensitive(k->j, "hasMore"); k->more = cJSON_IsBool(hm) ? cJSON_IsTrue(hm) : -1;
  if (k->st == 200 && !k->j) fprintf(stderr, "[addon] a catalog page that is not JSON\n");
  k->l = k->st == 200 || k->st == 404 ? list_from_metas(metas, k->r->type, 200) : NULL; /* (a 404 or no metas: the end) */
  net_json_free(k->j); k->j = NULL; }
static void on_page_done(int st, const unsigned char *b, size_t n, void *ud) { (void)st; (void)b; (void)n; PageJob *k = ud; k->r->cb(k->l, k->more, k->r->ud); free(k->r); free(k); }
static void *page_run(void *a) { plat_thread_background(5); page_work(a); net_file("", on_page_done, a); return NULL; }
static void on_page(int st, const unsigned char *b, size_t len, void *ud) {
  (void)len; PageJob *k = calloc(1, sizeof *k); k->r = ud; k->st = st; k->j = st == 200 ? net_json(b) : NULL;
  pthread_t t; if (pthread_create(&t, NULL, page_run, k) == 0) { pthread_detach(t); return; }
  page_work(k); on_page_done(0, NULL, 0, k); /* (no thread: here, as before) */
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
const char *sb_poster_of(const Item *it, char *buf, size_t cap) { if (getenv("SB_POSTER_FAIL")) { snprintf(buf, cap, "https://invalid.invalid/poster/%s.jpg", it->id); return buf; } /* (development: every poster fails, to see the stand-ins) */
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
/* a wide card's backdrop (430 wide, 460 focused): a source at least that wide, never smaller (TMDB's w300 and
   metahub's small were stretched 1.5 times and showed their blocks); the decode scales it to the card */
const char *sb_bg_small(const Item *it, char *buf, size_t cap) { bg_of(it, buf, cap); /* a card's backdrop: Metahub's small one (480x270, ~12 KB, against ~600 KB for the 1920x1080 medium: the reference app's choice for cards; the disk cache keeps fifty times more of them) */
  if (find_in(buf, "images.metahub.space/background/")) { const char *k[] = { "/background/medium/", "/background/large/" }; for (int i = 0; i < 2; i++) replace_in(buf, cap, k[i], "/background/small/"); }
  else if (find_in(buf, "image.tmdb.org/t/p/")) { const char *k[] = { "/t/p/w300/", "/t/p/w500/", "/t/p/w1280/", "/t/p/original/" }; for (int i = 0; i < 4; i++) replace_in(buf, cap, k[i], "/t/p/w780/"); }
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
typedef struct { char key[SB_ID + 32]; cJSON *meta; unsigned use, gen; } MetaEnt; /* gen: the addons' setup it came under */
static MetaEnt metaMem[40]; static unsigned metaUse, metaVer = 1; /* metaVer: bumps whenever an entry comes or goes */
static int meta_slot(const char *type, const char *id) { char k[SB_ID + 32]; snprintf(k, sizeof k, "%s_%s", type, id); unsigned g = addon_gen(); for (int i = 0; i < 40; i++) if (metaMem[i].meta && metaMem[i].gen == g && !strcmp(metaMem[i].key, k)) { metaMem[i].use = ++metaUse; return i; } return -1; }
cJSON *sb_meta_cached(const char *type, const char *id) { int i = meta_slot(type, id); return i >= 0 ? metaMem[i].meta : NULL; }
static void meta_put(const char *k, cJSON *m) { metaVer++; int slot = 0; unsigned old = 0xffffffffu; for (int i = 0; i < 40; i++) { if (!metaMem[i].meta) { slot = i; break; } if (metaMem[i].use < old) { old = metaMem[i].use; slot = i; } }
  if (metaMem[slot].meta) cJSON_Delete(metaMem[slot].meta); copy_str(metaMem[slot].key, sizeof metaMem[slot].key, k); metaMem[slot].meta = m; metaMem[slot].use = ++metaUse; metaMem[slot].gen = addon_gen(); }
/* a title page's disk copy: under the addons' setup too */
static void meta_ck(char *out, size_t cap, const char *key) { snprintf(out, cap, "meta%08x:%s", addon_gen(), key); }
#define META_BASES 8
typedef struct { char type[16], id[SB_ID], key[SB_ID + 32]; void (*cb)(cJSON *, void *); void *ud; int fresh; char bases[META_BASES][512]; int nb, bi; } MetaReq;
static void meta_try(MetaReq *r);
static void on_meta(int st, const unsigned char *b, size_t n, void *ud) {
  MetaReq *r = ud; cJSON *j = st == 200 ? net_json(b) : NULL; cJSON *m = j ? cJSON_DetachItemFromObjectCaseSensitive(j, "meta") : NULL; net_json_free(j);
  if (!cJSON_IsObject(m)) { cJSON_Delete(m); meta_try(r); return; }
  char ck[SB_ID + 48]; meta_ck(ck, sizeof ck, r->key); cache_write(ck, b, n); /* the answer as it came ({"meta":...}): printing a series' tree again took a frame */
  meta_put(r->key, m);
  if (!r->fresh) r->cb(m, r->ud);
  free(r);
}
static void meta_try(MetaReq *r) {
  if (r->bi >= r->nb) { if (!r->fresh) r->cb(NULL, r->ud); free(r); return; }
  char url[800]; addon_url(url, sizeof url, r->bases[r->bi++], "meta", r->type, r->id, NULL);
  net_get(url, on_meta, r);
}
static void meta_go(MetaReq *r) {
  if (!r->nb && !r->fresh) { r->cb(NULL, r->ud); free(r); return; }
  if (!r->nb) { free(r); return; }
  meta_try(r);
}
static void on_meta_disk(int st, const unsigned char *b, size_t n, void *ud) {
  MetaReq *r = ud; (void)n; cJSON *c = st == 200 ? net_json(b) : NULL; AddonCache h; addon_cache_hints(c, &h);
  if (cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(c, "meta"))) { cJSON *m = cJSON_DetachItemFromObjectCaseSensitive(c, "meta"); cJSON_Delete(c); c = m; } /* written as the answer came */
  if (h.maxAge > 0 && cJSON_IsObject(c)) { char ck[SB_ID + 48], cp[1200]; meta_ck(ck, sizeof ck, r->key); if (cache_file(ck, h.maxAge, cp, sizeof cp)) r->nb = 0; } /* younger than the addon's cacheMaxAge: no request */
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
  char ck[SB_ID + 48], cp[1200]; meta_ck(ck, sizeof ck, r->key);
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
/* Continue Watching, as the official apps make it: what is part watched, and the series with an episode out since
   they were last watched; ordered by when each last moved (a new episode: when it came out), at most 100 */
typedef struct { cJSON *it; double at; int newEp; char vid[SB_VID]; const TwPlay *tw; } CwPick; /* tw: Trakt's paused position, when it is the newer (it NULL: Trakt's alone) */
#define TW_NEWER 60000.0 /* ms: Trakt's position counts as the newer only this much after the library's (our own pause reaches Trakt a moment after the library) */
static double lib_last_ms(const cJSON *l) { return tms(cJSON_GetObjectItemCaseSensitive(js_obj(l, "state"), "lastWatched")); }
static int cmp_cw(const void *a, const void *b) { double x = ((const CwPick *)a)->at, y = ((const CwPick *)b)->at; return x < y ? 1 : x > y ? -1 : 0; }
int lib_cw_items(ItemList *out) {
  cJSON *lib = lib_arr(); const TwPlay *tp = NULL; int ntp = tw_plays(&tp); int cap = cJSON_GetArraySize(lib) + ntp, n = 0; const cJSON *it; CwPick *p = calloc((size_t)(cap ? cap : 1), sizeof *p); if (!p) { out->items = calloc(1, sizeof(Item)); out->n = 0; return 0; }
  cJSON_ArrayForEach(it, lib) { const cJSON *s = js_obj(it, "state"); const char *ty = js_str(it, "type", "");
    if (!strcmp(ty, "other") || !strcmp(ty, "tv") || !strcmp(ty, "channel") || (js_bool(it, "removed", 0) && !js_bool(it, "temp", 0)) || !js_str(it, "name", "")[0]) continue; /* (live channels: never, as in the official apps) */
    double to = js_num(s, "timeOffset", 0), d = js_num(s, "duration", 0); int part = to > 0 && !(d > 0 && to > d * 0.96);
    char vid[SB_VID] = ""; double newest = 0; int ne = notify_new(it, vid, sizeof vid, &newest);
    if (!part && !ne) continue;
    if (notify_cw_hidden(it, ne ? newest : 0)) continue; /* dismissed by hand, nothing new since */
    CwPick *c = &p[n++]; c->it = (cJSON *)it; c->newEp = ne; snprintf(c->vid, sizeof c->vid, "%s", vid);
    c->at = ne ? newest : tms(cJSON_GetObjectItemCaseSensitive(it, "_mtime")); if (ne && part) { double mt = tms(cJSON_GetObjectItemCaseSensitive(it, "_mtime")); if (mt > c->at) c->at = mt; } }
  /* Trakt's paused positions (another app, another device: Settings > Trakt > Continue Watching from Trakt): one entry a
     title, the newer position winning; a title watched or dismissed here since stays as the library says */
  for (int i = 0; i < ntp; i++) { const TwPlay *t = &tp[i]; double tat = (double)t->at * 1000; int k = -1;
    for (int j = 0; j < n && k < 0; j++) if (!strcmp(p[j].it ? js_str(p[j].it, "_id", "") : p[j].tw->id, t->id)) k = j;
    if (k >= 0) { if (p[k].it && tat > p[k].at + TW_NEWER && !p[k].tw) { p[k].tw = t; p[k].at = tat; p[k].newEp = 0; } continue; } /* (one entry: an older pause of the same title adds nothing) */
    const cJSON *l = lib_get(t->id); if (l && (lib_last_ms(l) + TW_NEWER >= tat || notify_cw_hidden(l, 0))) continue;
    if (n < cap) { CwPick *c = &p[n++]; c->it = NULL; c->tw = t; c->at = tat; } }
  qsort(p, (size_t)n, sizeof *p, cmp_cw); if (n > 100) n = 100;
  out->items = calloc(n ? n : 1, sizeof(Item)); out->n = n;
  for (int i = 0; i < n; i++) { Item *o = &out->items[i];
    if (!p[i].it) { const TwPlay *t = p[i].tw; memset(o, 0, sizeof *o); copy_str(o->id, sizeof o->id, t->id); copy_str(o->type, sizeof o->type, t->type); copy_str(o->name, sizeof o->name, t->title); /* (its pictures by its IMDb id, as Cinemeta's) */
      snprintf(o->poster, sizeof o->poster, "https://images.metahub.space/poster/medium/%s/img", t->id); snprintf(o->background, sizeof o->background, "https://images.metahub.space/background/medium/%s/img", t->id); snprintf(o->logo, sizeof o->logo, "https://images.metahub.space/logo/medium/%s/img", t->id);
      copy_str(o->videoId, sizeof o->videoId, t->vid); o->progress = t->pct; o->cw = 1; continue; } /* (no length known: no "min left") */
    item_from_lib(o, p[i].it); o->cw = 1; o->newEp = p[i].newEp;
    if (p[i].tw) { copy_str(o->videoId, sizeof o->videoId, p[i].tw->vid); o->progress = p[i].tw->pct; o->left = 0; continue; } /* (Trakt's is the newer: where it was paused) */
    const cJSON *s = js_obj(p[i].it, "state"); double to = js_num(s, "timeOffset", 0), d = js_num(s, "duration", 0);
    if (p[i].newEp && !(to > 0 && !(d > 0 && to > d * 0.96))) { snprintf(o->videoId, sizeof o->videoId, "%s", p[i].vid); o->progress = 0; o->left = 0; } } /* nothing part watched: the first new episode, from its start */
  free(p); return n;
}
int lib_cw_info(const Item *it, CwInfo *out) {
  cJSON *l = lib_get(it->id); const cJSON *s = js_obj(l, "state");
  const TwPlay *t = tw_play_of(it->id); if (t && (double)t->at * 1000 > (s ? lib_last_ms(l) : 0) + TW_NEWER) { copy_str(out->vid, sizeof out->vid, t->vid); out->pct = t->pct; out->leftMin = 0; return 1; } /* (Trakt's pause is the newer) */
  if (!s) return 0;
  double to = js_num(s, "timeOffset", 0), d = js_num(s, "duration", 0); if (!(to > 0) || !(d > 0) || to > d * 0.96) return 0;
  const char *vid = js_str(s, "video_id", ""); if (!strcmp(it->type, "series") && !*vid) return 0;
  copy_str(out->vid, sizeof out->vid, *vid ? vid : it->id); out->pct = (float)(to / d); out->leftMin = (float)((d - to) / 60000.0); return 1;
}
/* where to resume a title's video from Trakt: its pause there is the newer than the library's (0..1), 0 none */
float lib_trakt_resume(const char *id, const char *vid) {
  const TwPlay *t = tw_play_of(id); if (!t || strcmp(t->vid, vid && *vid ? vid : id)) return 0; cJSON *l = lib_get(id);
  return (double)t->at * 1000 > (js_obj(l, "state") ? lib_last_ms(l) : 0) + TW_NEWER ? t->pct : 0; }
static char progId[SB_ID]; static Uint32 progPushT;
void lib_set_progress(const char *id, const Item *it, const char *vid, double to, double dur, int force) {
  cJSON *l = lib_get(id); if (!l) l = lib_new_item(it); cJSON *s = cJSON_GetObjectItemCaseSensitive(l, "state"); if (!s) { s = cJSON_CreateObject(); cJSON_AddItemToObject(l, "state", s); }
  if (it && !strcmp(it->id, id)) { /* its pictures and name from the title playing now: art kept wrong once (a title cast over another) is put right */
    const char *k[4] = { "name", "poster", "background", "logo" }, *v[4] = { it->name, it->poster, it->background, it->logo };
    for (int i = 0; i < 4; i++) if (v[i][0] && strcmp(js_str(l, k[i], ""), v[i])) { cJSON_DeleteItemFromObjectCaseSensitive(l, k[i]); cJSON_AddStringToObject(l, k[i], v[i]); } }
  if (js_bool(l, "removed", 0) && !js_bool(l, "temp", 0) && to > 0) { set_bool(l, "temp", 1); fprintf(stderr, "[lib] %s: out of the library and watched again: back in Continue Watching\n", id); } /* (as the official apps: watched again, it is in Continue Watching, still not in the library) */
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
static int cmp_vid(const void *a, const void *b) { const double *x = a, *y = b; if (x[0] != y[0]) return x[0] < y[0] ? -1 : 1; if (x[1] != y[1]) return x[1] < y[1] ? -1 : 1; return x[2] < y[2] ? -1 : x[2] > y[2] ? 1 : 0; }
static int ordered_ids(const cJSON *videos, char ids[][80], int max) { /* (season, episode) order, those without both first, stable */
  int n = 0, cap = cJSON_GetArraySize(videos); if (cap > max) cap = max; if (cap <= 0) return 0; const cJSON *v; /* (on the heap: a long series' keys are too big for a stack) */
  double (*keys)[3] = malloc((size_t)cap * sizeof *keys), (*tmp)[3] = malloc((size_t)cap * sizeof *tmp); const cJSON **ptr = malloc((size_t)cap * sizeof *ptr);
  if (!keys || !tmp || !ptr) { free(keys); free(tmp); free(ptr); return 0; }
  cJSON_ArrayForEach(v, videos) { if (n >= cap) break; const cJSON *s = cJSON_GetObjectItemCaseSensitive(v, "season"), *e = cJSON_GetObjectItemCaseSensitive(v, "episode"); int both = cJSON_IsNumber(s) && cJSON_IsNumber(e);
    keys[n][0] = both ? s->valuedouble : -1e18; keys[n][1] = both ? e->valuedouble : -1e18; keys[n][2] = n; ptr[n] = v; n++; }
  for (int i = 0; i < n; i++) { tmp[i][0] = keys[i][0]; tmp[i][1] = keys[i][1]; tmp[i][2] = keys[i][2]; }
  qsort(tmp, n, sizeof tmp[0], cmp_vid);
  for (int i = 0; i < n; i++) copy_str(ids[i], 80, js_str(ptr[(int)tmp[i][2]], "id", ""));
  free(keys); free(tmp); free(ptr); return n;
}
static int b64dec(const char *s, unsigned char *out, int cap) {
  static const char *A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; unsigned val = 0; int bits = -8, o = 0; /* (unsigned and kept to the bits still owed: a signed one shifted past 31 bits on any long string) */
  for (; *s && *s != '='; s++) { const char *p = strchr(A, *s); if (!p) continue; val = ((val << 6) | (unsigned)(p - A)) & 0xffffffu; bits += 6; if (bits >= 0) { if (o < cap) out[o++] = (unsigned char)((val >> bits) & 255); bits -= 8; } }
  return o;
}
static void b64enc(const unsigned char *in, int n, char *out) {
  static const char *A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; int o = 0;
  for (int i = 0; i < n; i += 3) { unsigned v = (in[i] << 16) | (i + 1 < n ? in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
    out[o++] = A[(v >> 18) & 63]; out[o++] = A[(v >> 12) & 63]; out[o++] = i + 1 < n ? A[(v >> 6) & 63] : '='; out[o++] = i + 2 < n ? A[v & 63] : '='; }
  out[o] = 0;
}
/* the library's own marks: what is written back starts from these (never from Trakt's) */
static void ep_watched_raw(const cJSON *lit, const cJSON *videos, int *flags) {
  int n = cJSON_GetArraySize(videos); if (n > WATCH_MAX) n = WATCH_MAX; for (int i = 0; i < n; i++) flags[i] = 0; /* (flags hold WATCH_MAX at most: a longer series' later episodes read as unwatched) */
  const cJSON *s = js_obj(lit, "state"); const char *str = js_str(s, "watched", "");
  static char ids[WATCH_MAX][80]; int nid = ordered_ids(videos, ids, WATCH_MAX);
  if (*str && nid) {
    char buf[4096]; copy_str(buf, sizeof buf, str); char *last = strrchr(buf, ':'); if (last) { *last = 0; char *b64 = last + 1; char *c2 = strrchr(buf, ':');
      if (c2) { *c2 = 0; int alen = atoi(c2 + 1); const char *anchor = buf;
        unsigned char comp[4096]; int cn = b64dec(b64, comp, sizeof comp); unsigned char bytes[4096]; uLongf bn = sizeof bytes;
        if (cn > 0 && uncompress(bytes, &bn, comp, cn) == Z_OK) { int ai = -1; for (int i = 0; i < nid; i++) if (!strcmp(ids[i], anchor)) { ai = i; break; }
          if (ai >= 0) { int off = alen - ai - 1; static int w[WATCH_MAX]; for (int i = 0; i < nid; i++) { int p = i + off; w[i] = p >= 0 && p < nid && (p >> 3) < (int)bn && ((bytes[p >> 3] >> (p & 7)) & 1); }
            /* map back from ordered ids to the videos' order */
            int k = 0; const cJSON *v; cJSON_ArrayForEach(v, videos) { if (k >= n) break; const char *id = js_str(v, "id", ""); for (int i = 0; i < nid; i++) if (w[i] && !strcmp(ids[i], id)) { flags[k] = 1; break; } k++; } } } } }
  }
  const cJSON *we = js_arr(s, "watchedEpisodes"); if (we) { int k = 0; const cJSON *v; cJSON_ArrayForEach(v, videos) { if (k >= n) break; const cJSON *e; cJSON_ArrayForEach(e, we) if (cJSON_IsString(e) && !strcmp(e->valuestring, js_str(v, "id", ""))) flags[k] = 1; k++; } }
}
/* the show a library entry or an episode list is of ("tt123" from "tt123:1:2"); "" none */
static void show_id_of(const cJSON *lit, const cJSON *videos, char *out, size_t cap) {
  const char *id = js_str(lit, "_id", ""); if (*id) { copy_str(out, cap, id); return; }
  const char *vid = js_str(cJSON_GetArrayItem(videos, 0), "id", ""); size_t k = strcspn(vid, ":"); if (k >= cap) k = cap - 1; memcpy(out, vid, k); out[k] = 0; }
/* what shows: the library's marks, and on top of them the episodes the connected Trakt account has watched (never
   written back: Settings > Trakt > Watched marks from Trakt) */
void lib_ep_watched_map(const cJSON *lit, const cJSON *videos, int *flags) {
  ep_watched_raw(lit, videos, flags); if (!tw_on()) return;
  char sid[SB_ID]; show_id_of(lit, videos, sid, sizeof sid); if (!tw_show_known(sid)) return;
  int n = cJSON_GetArraySize(videos); if (n > WATCH_MAX) n = WATCH_MAX; int k = 0; const cJSON *v;
  cJSON_ArrayForEach(v, videos) { if (k >= n) break; const cJSON *se = cJSON_GetObjectItemCaseSensitive(v, "season"), *ep = cJSON_GetObjectItemCaseSensitive(v, "episode");
    if (!flags[k] && cJSON_IsNumber(se) && cJSON_IsNumber(ep) && tw_ep(sid, (int)se->valuedouble, (int)ep->valuedouble)) flags[k] = 1; k++; } }
static void ep_encode(cJSON *s, const cJSON *videos, const int *flags) {
  static char ids[WATCH_MAX][80]; int nid = ordered_ids(videos, ids, WATCH_MAX); unsigned char bytes[WATCH_MAX / 8] = { 0 }; int last = -1;
  for (int i = 0; i < nid; i++) { int k = 0; const cJSON *v; int on = 0; cJSON_ArrayForEach(v, videos) { if (!strcmp(js_str(v, "id", ""), ids[i])) on = flags[k]; k++; } if (on) { bytes[i >> 3] |= 1 << (i & 7); last = i; } }
  if (last < 0) last = 0; int nb = (nid + 7) / 8; unsigned char comp[WATCH_MAX / 8 + 128]; uLongf cn = sizeof comp; compress(comp, &cn, bytes, nb);
  char b64[(WATCH_MAX / 8 + 128) * 4 / 3 + 8]; b64enc(comp, (int)cn, b64); char out[sizeof b64 + 128]; snprintf(out, sizeof out, "%s:%d:%s", nid ? ids[last] : "undefined", last + 1, b64);
  cJSON_DeleteItemFromObjectCaseSensitive(s, "watched"); cJSON_AddStringToObject(s, "watched", out); cJSON_DeleteItemFromObjectCaseSensitive(s, "watchedEpisodes");
}
/* where an episode sits in the series' (season, episode) order: its place, -1 unknown; *season its season (-1 none) */
int lib_ep_rank(const cJSON *videos, const char *vid, int *season) {
  if (season) *season = -1; if (!videos || !vid || !*vid) return -1;
  static char ids[WATCH_MAX][80]; int nid = ordered_ids(videos, ids, WATCH_MAX), at = -1; for (int i = 0; i < nid; i++) if (!strcmp(ids[i], vid)) { at = i; break; }
  if (at >= 0 && season) { const cJSON *v; cJSON_ArrayForEach(v, videos) if (!strcmp(js_str(v, "id", ""), vid)) { const cJSON *se = cJSON_GetObjectItemCaseSensitive(v, "season"); *season = cJSON_IsNumber(se) ? (int)se->valuedouble : -1; break; } }
  return at;
}
/* watching again from an earlier episode: every episode after it (the specials left alone) is unwatched; how many were */
int lib_unwatch_after(cJSON *lit, const cJSON *videos, const char *vid) {
  static char ids[WATCH_MAX][80]; int nid = ordered_ids(videos, ids, WATCH_MAX), r0 = -1; for (int i = 0; i < nid; i++) if (!strcmp(ids[i], vid)) { r0 = i; break; } if (r0 < 0) return 0;
  int n = cJSON_GetArraySize(videos); if (n > WATCH_MAX) n = WATCH_MAX; static int flags[WATCH_MAX]; ep_watched_raw(lit, videos, flags);
  int k = 0, cnt = 0; const cJSON *v; cJSON_ArrayForEach(v, videos) { if (k >= n) break; const cJSON *se = cJSON_GetObjectItemCaseSensitive(v, "season"); const char *id = js_str(v, "id", "");
    if (flags[k] && !(cJSON_IsNumber(se) && se->valuedouble == 0)) for (int i = r0 + 1; i < nid; i++) if (!strcmp(ids[i], id)) { flags[k] = 0; cnt++; break; }
    k++; }
  if (cnt) { cJSON *s = cJSON_GetObjectItemCaseSensitive(lit, "state"); if (s) { ep_encode(s, videos, flags); lib_save(lit); } }
  return cnt;
}
/* the series' newest aired episode watched: every aired episode counts as watched (the specials left as they are), so
   the series is watched until a new one airs (it then comes back as a new episode, and in Upcoming before that).
   0 when vid is not the newest aired one */
static double ep_aired(const cJSON *v) { const char *rel = js_str(v, "released", js_str(v, "firstAired", NULL)); if (!rel) return 0; cJSON t = { 0 }; t.type = cJSON_String; t.valuestring = (char *)rel; return tms(&t); }
/* an episode watched to its end: it and every aired episode before it watched (the specials left as they are) */
int lib_mark_through(cJSON *lit, const cJSON *videos, const char *vid) {
  static char ids[WATCH_MAX][80]; int nid = ordered_ids(videos, ids, WATCH_MAX), r0 = -1; for (int i = 0; i < nid; i++) if (!strcmp(ids[i], vid)) { r0 = i; break; } if (r0 < 0) return 0;
  double now = (double)time(NULL) * 1000; static const char *on[WATCH_MAX]; int n = 0; const cJSON *v;
  cJSON_ArrayForEach(v, videos) { const char *id = js_str(v, "id", ""); const cJSON *se = cJSON_GetObjectItemCaseSensitive(v, "season"); int me = !strcmp(id, vid);
    if (!me && cJSON_IsNumber(se) && se->valuedouble == 0) continue; double r = ep_aired(v); if (!me && r > now) continue;
    for (int i = 0; i <= r0; i++) if (!strcmp(ids[i], id)) { if (n < WATCH_MAX) on[n++] = id; break; } }
  if (n) lib_set_eps_watched(lit, videos, on, n, 1); return n;
}
int lib_caught_up(cJSON *lit, const cJSON *videos, const char *vid) {
  static char ids[WATCH_MAX][80]; int nid = ordered_ids(videos, ids, WATCH_MAX), r0 = -1; for (int i = 0; i < nid; i++) if (!strcmp(ids[i], vid)) { r0 = i; break; } if (r0 < 0) return 0;
  double now = (double)time(NULL) * 1000; static const char *aired[WATCH_MAX]; int na = 0; const cJSON *v;
  cJSON_ArrayForEach(v, videos) { const cJSON *se = cJSON_GetObjectItemCaseSensitive(v, "season"); if (cJSON_IsNumber(se) && se->valuedouble == 0) continue; double r = ep_aired(v); if (!r || r > now) continue;
    const char *id = js_str(v, "id", ""); for (int i = r0 + 1; i < nid; i++) if (!strcmp(ids[i], id)) return 0; /* an aired one after it: not the newest */
    if (na < WATCH_MAX) aired[na++] = id; }
  if (!na) return 0; lib_set_eps_watched(lit, videos, aired, na, 1); return na;
}
void lib_set_eps_watched(cJSON *lit, const cJSON *videos, const char **epIds, int n, int on) {
  int nv = cJSON_GetArraySize(videos); static int flags[WATCH_MAX]; ep_watched_raw(lit, videos, flags);
  int k = 0; const cJSON *v; cJSON_ArrayForEach(v, videos) { const char *id = js_str(v, "id", ""); for (int i = 0; i < n; i++) if (!strcmp(epIds[i], id)) flags[k] = on; k++; }
  cJSON *s = cJSON_GetObjectItemCaseSensitive(lit, "state"); ep_encode(s, videos, flags);
  if (on) { char iso[40]; now_iso(iso, sizeof iso); cJSON_DeleteItemFromObjectCaseSensitive(s, "lastWatched"); cJSON_AddStringToObject(s, "lastWatched", iso);
    const char *cur = js_str(s, "video_id", ""); for (int i = 0; i < n; i++) if (!strcmp(epIds[i], cur)) { cJSON_DeleteItemFromObjectCaseSensitive(s, "timeOffset"); cJSON_AddNumberToObject(s, "timeOffset", 0); } }
  (void)nv; lib_save(lit);
}
void lib_set_movie_watched(cJSON *lit, int on) {
  cJSON *s = cJSON_GetObjectItemCaseSensitive(lit, "state"); if (!s) return;
  if (on) { double tw = js_num(s, "timesWatched", 0) + 1; char iso[40]; now_iso(iso, sizeof iso); cJSON_DeleteItemFromObjectCaseSensitive(s, "timesWatched"); cJSON_AddNumberToObject(s, "timesWatched", tw);
    cJSON_DeleteItemFromObjectCaseSensitive(s, "lastWatched"); cJSON_AddStringToObject(s, "lastWatched", iso); cJSON_DeleteItemFromObjectCaseSensitive(s, "timeOffset"); cJSON_AddNumberToObject(s, "timeOffset", 0); }
  else { cJSON_DeleteItemFromObjectCaseSensitive(s, "timesWatched"); cJSON_AddNumberToObject(s, "timesWatched", 0); cJSON_DeleteItemFromObjectCaseSensitive(s, "flaggedWatched"); cJSON_AddNumberToObject(s, "flaggedWatched", 0); }
  cJSON_DeleteItemFromObjectCaseSensitive(s, "watchedEpisodes"); lib_save(lit);
}
int lib_movie_watched(const cJSON *l) { if (tw_movie(js_str(l, "_id", ""))) return 1; const cJSON *s = js_obj(l, "state"); if (!s) return 0; if (js_num(s, "timesWatched", 0) > 0) return 1;
  const cJSON *we = js_arr(s, "watchedEpisodes"), *e; const char *id = js_str(l, "_id", ""); if (we) cJSON_ArrayForEach(e, we) if (cJSON_IsString(e) && !strcmp(e->valuestring, id)) return 1; return 0; }
int lib_series_watched(const cJSON *l, const cJSON *videos) {
  if (!videos || !cJSON_GetArraySize(videos)) return 0; const cJSON *s = js_obj(l, "state"); char sid[SB_ID]; show_id_of(l, videos, sid, sizeof sid); /* (l NULL: a show not in the library, its marks from Trakt alone) */
  if (!tw_show_known(sid) && (!s || (!js_str(s, "watched", "")[0] && !js_arr(s, "watchedEpisodes")))) return 0;
  static int flags[WATCH_MAX]; lib_ep_watched_map(l, videos, flags); int k = 0, n = 0; const cJSON *v; time_t now = time(NULL);
  cJSON_ArrayForEach(v, videos) { const cJSON *se = cJSON_GetObjectItemCaseSensitive(v, "season"); if (!cJSON_IsNumber(se) || se->valuedouble == 0) { k++; continue; }
    const char *rel = js_str(v, "released", js_str(v, "firstAired", NULL)); double r = 0; if (rel) { cJSON tmp = { 0 }; tmp.type = cJSON_String; tmp.valuestring = (char *)rel; r = tms(&tmp); }
    if (!r || r > (double)now * 1000) { k++; continue; } n++; if (!flags[k]) return 0; k++; }
  return n > 0;
}
/* the badge is asked for every card in every frame and a series' answer decodes its bitfield against every episode:
   it is kept per title until the library or the metadata cache moves (a series: also each minute, it counts only
   the episodes aired by now). A hit touches the metadata as the lookup did, so the titles on screen stay cached. */
typedef struct { char id[SB_ID]; unsigned char series, on; unsigned lv, mv; long mn; int slot; } WMemo;
static WMemo wMemo[256];
int lib_item_watched(const Item *it) {
  int series = !strcmp(it->type, "series"); long mn = series ? (long)(time(NULL) / 60) : 0;
  unsigned h = 2166136261u; for (const char *p = it->id; *p; p++) h = (h ^ (unsigned char)*p) * 16777619u;
  WMemo *w = NULL;
  for (int k = 0; k < 8; k++) { WMemo *e = &wMemo[(h + k) & 255]; if (!e->id[0] || (!strcmp(e->id, it->id) && e->series == series)) { w = e; break; } }
  if (!w) w = &wMemo[h & 255];
  unsigned lv = libVer + (tw_ver() * 2u + (unsigned)tw_on()) * 7919u; /* (Trakt's marks move it too, and their switch) */
  if (w->id[0] && w->lv == lv && w->mv == metaVer && w->mn == mn && !strcmp(w->id, it->id) && w->series == series) { if (w->slot >= 0) metaMem[w->slot].use = ++metaUse; return w->on; }
  cJSON *l = lib_get(it->id); int on = 0, slot = -1;
  if (l || tw_on()) { if (series) { slot = meta_slot("series", it->id); on = lib_series_watched(l, js_arr(slot >= 0 ? metaMem[slot].meta : NULL, "videos")); } else on = l ? lib_movie_watched(l) : tw_movie(it->id); }
  copy_str(w->id, sizeof w->id, it->id); w->series = (unsigned char)series; w->on = (unsigned char)on; w->lv = lv; w->mv = metaVer; w->mn = mn; w->slot = slot;
  return on;
}
static const cJSON *videos_cached(const char *id, cJSON **own);
/* ---- marking watched by hand: one model for a film, a whole show and an episode (the player's credits use it too) ----
   episode ON : it and every aired episode before it watched (the specials left as they are); the show's place moves
                to the start of the next aired episode ("Next episode" in Continue Watching), or, nothing next out,
                the show leaves Continue Watching. A place already further on stays.
   episode OFF: it and every episode after it, in every later season, unwatched (the specials left as they are); the
                show waits at it ("Next episode") while something before it is still watched, else it leaves Continue
                Watching. A place before it stays; part watched on it stays.
   show ON    : every aired episode watched (specials left), out of Continue Watching.  show OFF: every episode
                unwatched, out of Continue Watching.  a special: only itself, the place untouched. */
static int ep_item(const Item *it) { return strcmp(it->type, "movie") && strcmp(it->type, "other") && it->videoId[0] && strcmp(it->videoId, it->id); } /* (a series' or an anime's episode) */
static int ep_special(const cJSON *v) { const cJSON *se = cJSON_GetObjectItemCaseSensitive(v, "season"); return cJSON_IsNumber(se) && se->valuedouble == 0; }
static int ep_index(const cJSON *videos, const char *vid) { int k = 0; const cJSON *v; cJSON_ArrayForEach(v, videos) { if (!strcmp(js_str(v, "id", ""), vid)) return k; k++; } return -1; }
static int ep_flag(const cJSON *l, const cJSON *videos, const char *vid) {
  int k = ep_index(videos, vid); if (k < 0 || k >= WATCH_MAX) return 0; static int flags[WATCH_MAX]; lib_ep_watched_map(l, videos, flags); return flags[k]; }
static void place_wait(cJSON *l, const char *vid) { lib_set_progress(js_str(l, "_id", ""), NULL, vid, 1, 0, 1); }
static void place_drop(cJSON *l) { cJSON *s = cJSON_GetObjectItemCaseSensitive(l, "state"); if (!s) return;
  cJSON_DeleteItemFromObjectCaseSensitive(s, "timeOffset"); cJSON_AddNumberToObject(s, "timeOffset", 0); cJSON_DeleteItemFromObjectCaseSensitive(s, "duration"); cJSON_AddNumberToObject(s, "duration", 0); lib_save(l); }
static void series_mark_ep(cJSON *l, const cJSON *videos, const char *vid, int on) {
  int k = ep_index(videos, vid); if (k < 0) return; const cJSON *me = cJSON_GetArrayItem(videos, k);
  if (ep_special(me)) { const char *one[1] = { vid }; lib_set_eps_watched(l, videos, one, 1, on); return; }
  static char ids[WATCH_MAX][80]; int nid = ordered_ids(videos, ids, WATCH_MAX), rm = -1, rc = -1;
  const cJSON *s = js_obj(l, "state"); char cur[SB_VID]; snprintf(cur, sizeof cur, "%s", js_str(s, "video_id", "")); double off = js_num(s, "timeOffset", 0);
  for (int i = 0; i < nid; i++) { if (!strcmp(ids[i], vid)) rm = i; if (cur[0] && off > 0 && !strcmp(ids[i], cur)) rc = i; } /* (a place at 0 is no place: the show is out of Continue Watching) */
  if (rm < 0) return;
  if (on) {
    if (!lib_mark_through(l, videos, vid)) { const char *one[1] = { vid }; lib_set_eps_watched(l, videos, one, 1, 1); }
    if (rc > rm) return; /* the show is further on already */
    double now = (double)time(NULL) * 1000; char nv[80] = "";
    for (int i = rm + 1; i < nid && !nv[0]; i++) { int j = ep_index(videos, ids[i]); const cJSON *v = j >= 0 ? cJSON_GetArrayItem(videos, j) : NULL; if (!v || ep_special(v)) continue;
      if (ep_aired(v) > now) break; snprintf(nv, sizeof nv, "%s", ids[i]); }
    if (nv[0]) { place_wait(l, nv); fprintf(stderr, "[lib] marked watched: the show waits at %s\n", nv); } else { place_drop(l); fprintf(stderr, "[lib] marked watched: nothing next out, out of Continue Watching\n"); }
  } else {
    const char *one[1] = { vid }; lib_set_eps_watched(l, videos, one, 1, 0); int n = lib_unwatch_after(l, videos, vid);
    if (rc >= 0 && rc < rm) return; /* the show's place is before it */
    if (rc == rm && off > 1) return; /* part watched on it: kept */
    static int fl[WATCH_MAX]; ep_watched_raw(l, videos, fl); int before = 0;
    for (int i = 0; i < rm && !before; i++) { int j = ep_index(videos, ids[i]); const cJSON *v = j >= 0 && j < WATCH_MAX ? cJSON_GetArrayItem(videos, j) : NULL; if (v && !ep_special(v) && fl[j]) before = 1; }
    if (before) place_wait(l, vid); else place_drop(l);
    fprintf(stderr, "[lib] marked unwatched: %s and %d later episode(s); %s\n", vid, n, before ? "the show waits at it" : "out of Continue Watching");
  }
}
static void series_mark_all(cJSON *l, const cJSON *videos, int on) {
  double now = (double)time(NULL) * 1000; static const char *ids[2048]; int n = 0; const cJSON *v;
  cJSON_ArrayForEach(v, videos) { if (on && ep_special(v)) continue; double r = ep_aired(v); if (on && (!r || r > now)) continue; if (n < 2048) ids[n++] = js_str(v, "id", ""); }
  if (n) lib_set_eps_watched(l, videos, ids, n, on); place_drop(l);
}
int lib_is_marked_watched(const Item *it) {
  cJSON *l = lib_get(it->id); if (!l && !tw_on()) return 0; if (strcmp(it->type, "series") && !ep_item(it)) return l ? lib_movie_watched(l) : tw_movie(it->id);
  cJSON *own = NULL; const cJSON *videos = videos_cached(it->id, &own); int w = 0;
  if (videos) w = ep_item(it) ? ep_flag(l, videos, it->videoId) : lib_series_watched(l, videos);
  cJSON_Delete(own); return w;
}
/* a title's name and pictures from its metadata (memory or disk copy): a library entry made from an episode's card or
   row had the episode's name or none, and a nameless entry is never in a row (fresh: replace all, else fill the gaps) */
static void lib_fill_art(cJSON *l, const char *type, const char *id, int fresh) {
  cJSON *own = NULL; const cJSON *mo = sb_meta_cached(type, id);
  if (!mo) { char ck[SB_ID + 48], mk[SB_ID + 32]; snprintf(mk, sizeof mk, "%s_%s", type, id); meta_ck(ck, sizeof ck, mk); size_t cl; unsigned char *t = cache_read(ck, 0, &cl); if (t) { own = cJSON_Parse((const char *)t); free(t); } mo = js_obj(own, "meta"); if (!mo) mo = own; }
  if (mo) { const char *k[4] = { "name", "poster", "background", "logo" };
    for (int i = 0; i < 4; i++) { const char *v = js_str(mo, k[i], ""); if (*v && (fresh || !js_str(l, k[i], "")[0])) { cJSON_DeleteItemFromObjectCaseSensitive(l, k[i]); cJSON_AddStringToObject(l, k[i], v); } } }
  cJSON_Delete(own);
}
static cJSON *lib_for_mark(const Item *it) { cJSON *l = lib_get(it->id); int fresh = !l; if (!l) l = lib_new_item(it); if (fresh || !js_str(l, "name", "")[0]) lib_fill_art(l, it->type, it->id, fresh); return l; }
static void set_watched(const Item *it, const cJSON *videos, int on) {
  cJSON *l = lib_for_mark(it);
  if (strcmp(it->type, "series") && !ep_item(it)) { lib_set_movie_watched(l, on); return; }
  cJSON *own = NULL; if (!videos || !cJSON_GetArraySize(videos)) videos = videos_cached(it->id, &own); /* (its episodes from the disk copy when not in memory) */
  if (videos) { if (ep_item(it)) series_mark_ep(l, videos, it->videoId, on); else series_mark_all(l, videos, on); }
  cJSON_Delete(own);
}
/* a mark by hand, told to Trakt as the library takes it (see traktwatch.h: only what Trakt lacks is added, unmarking
   only hides): an episode on, it and the aired ones before it; off, it and every one after it; a special, itself; a
   whole show, every aired episode (on) or every one (off) */
static void trakt_hand(const Item *it, const cJSON *videos, int on) {
  if (!tw_on()) return;
  if (strcmp(it->type, "series") && !ep_item(it)) { tw_mark_movie(it->id, on); return; }
  cJSON *own = NULL; if (!videos || !cJSON_GetArraySize(videos)) videos = videos_cached(it->id, &own); int nv = cJSON_GetArraySize(videos); if (!videos || nv <= 0) { cJSON_Delete(own); return; }
  int *se = malloc(sizeof *se * 2 * (size_t)nv), n = 0; if (!se) { cJSON_Delete(own); return; }
  int me = ep_item(it) ? ep_index(videos, it->videoId) : -1; const cJSON *mev = me >= 0 ? cJSON_GetArrayItem(videos, me) : NULL;
  double ms = js_num(mev, "season", -1), mn = js_num(mev, "episode", -1); /* (the order is season, then number: compared straight, never sorted for each) */
  double now = (double)time(NULL) * 1000; const cJSON *v; int k = 0;
  cJSON_ArrayForEach(v, videos) { const cJSON *s = cJSON_GetObjectItemCaseSensitive(v, "season"), *e = cJSON_GetObjectItemCaseSensitive(v, "episode"); int take = 0;
    if (cJSON_IsNumber(s) && cJSON_IsNumber(e)) {
      if (!ep_item(it)) take = !on || (!ep_special(v) && ep_aired(v) > 0 && ep_aired(v) <= now);
      else if (k == me) take = 1;
      else if (mev && ms >= 0 && mn >= 0 && !ep_special(mev) && !ep_special(v)) { double vs = s->valuedouble, ve = e->valuedouble; int before = vs < ms || (vs == ms && ve < mn), after = vs > ms || (vs == ms && ve > mn);
        take = on ? (before && ep_aired(v) <= now) : after; } }
    if (take) { se[2 * n] = (int)s->valuedouble; se[2 * n + 1] = (int)e->valuedouble; n++; } k++; }
  char sid[SB_ID]; show_id_of(NULL, videos, sid, sizeof sid); if (!sid[0]) copy_str(sid, sizeof sid, it->id);
  tw_mark_eps(sid, se, n, on); free(se); cJSON_Delete(own);
}
void lib_set_watched(const Item *it, const cJSON *videos, int on) { set_watched(it, videos, on); trakt_hand(it, videos, on); } /* (by hand) */
void lib_episode_watched(const Item *it, const cJSON *videos) { set_watched(it, videos, 1); } /* (the player, at the end: Trakt hears it from the scrobble, never twice) */
void lib_toggle_watched(const Item *it, const cJSON *videos) { /* the direction read from the same episode list the mark uses */
  cJSON *l = lib_get(it->id); int w;
  if (l && (!strcmp(it->type, "series") || ep_item(it)) && videos && cJSON_GetArraySize(videos)) w = ep_item(it) ? ep_flag(l, videos, it->videoId) : lib_series_watched(l, videos); else w = lib_is_marked_watched(it);
  lib_set_watched(it, videos, !w); }

/* the series' episode list from the memory or the disk copy of its metadata (any age); *own = what to free */
static const cJSON *videos_cached(const char *id, cJSON **own) {
  *own = NULL; cJSON *m = sb_meta_cached("series", id); if (m) return js_arr(m, "videos");
  char ck[SB_ID + 48], mk[SB_ID + 32]; snprintf(mk, sizeof mk, "series_%s", id); meta_ck(ck, sizeof ck, mk); size_t cl; unsigned char *t = cache_read(ck, 0, &cl); if (!t) return NULL;
  *own = cJSON_Parse((const char *)t); free(t); const cJSON *mo = js_obj(*own, "meta"); return js_arr(mo ? mo : *own, "videos");
}
/* one-time fixes for items older builds wrote: the private watched list goes into the shared bitfield, and
   removed:false + temp:true (which every app shows in the Library) becomes the official removed:true + temp:true */
static int lib_migrate_legacy(void) {
  cJSON *lib = lib_arr(), *it; int ch = 0;
  cJSON_ArrayForEach(it, lib) { cJSON *s = cJSON_GetObjectItemCaseSensitive(it, "state");
    if (cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(it, "removed")) && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(it, "temp"))) { set_bool(it, "removed", 1); lib_save(it); ch = 1; }
    const cJSON *we = js_arr(s, "watchedEpisodes"); if (!we || !cJSON_GetArraySize(we)) continue;
    const char *id = js_str(it, "_id", "");
    if (!strcmp(js_str(it, "type", ""), "series")) { cJSON *own; const cJSON *vids = videos_cached(id, &own); int n = cJSON_GetArraySize(vids); if (!vids || !n) { cJSON_Delete(own); continue; }
      int *flags = calloc(n, sizeof *flags); ep_watched_raw(it, vids, flags); ep_encode(s, vids, flags); free(flags); cJSON_Delete(own); }
    else { int mine = 0; const cJSON *e; cJSON_ArrayForEach(e, we) if (cJSON_IsString(e) && !strcmp(e->valuestring, id)) mine = 1;
      if (mine && !(js_num(s, "timesWatched", 0) > 0)) { cJSON_DeleteItemFromObjectCaseSensitive(s, "timesWatched"); cJSON_AddNumberToObject(s, "timesWatched", 1); } }
    cJSON_DeleteItemFromObjectCaseSensitive(s, "watchedEpisodes"); lib_save(it); ch = 1; }
  return ch;
}
unsigned sb_lib_sync_age(void) { return libSyncT ? SDL_GetTicks() - libSyncT : 0xffffffffu; }

/* two-way sync with the account, newest _mtime wins */
typedef struct { void (*cb)(int, void *); void *ud; int changed; char (*ids)[SB_ID]; int nwant, at; } PullReq; /* ids: every title the account has newer, newest first */
static void pull_next(PullReq *r);
static void on_pull_get(int st, const unsigned char *b, size_t n, void *ud) {
  PullReq *r = ud; cJSON *j = st == 200 ? net_json(b) : NULL; const cJSON *items = js_arr(j, "result"), *it;
  if (items) cJSON_ArrayForEach(it, items) { const char *id = js_str(it, "_id", NULL); if (id) { cJSON_DeleteItemFromObjectCaseSensitive(lib_arr(), id); cJSON_AddItemToObject(lib_arr(), id, cJSON_Duplicate(it, 1)); r->changed = 1; } }
  net_json_free(j); pull_next(r);
}
static void pull_next(PullReq *r) {
  if (r->at >= r->nwant) { if (lib_migrate_legacy()) r->changed = 1; if (r->changed) { libVer++; store_set_node("lib", store_node("lib")); store_flush(); } libSyncing = 0; fprintf(stderr, "[lib] sync done: %d titles taken from the account, %d in the library\n", r->nwant, cJSON_GetArraySize(lib_arr())); if (r->cb) r->cb(r->changed, r->ud); free(r->ids); free(r); return; }
  cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "authKey", sb_auth_key()); cJSON_AddStringToObject(o, "collection", "libraryItem"); cJSON *ids = cJSON_CreateArray();
  for (int k = 0; k < 100 && r->at < r->nwant; k++, r->at++) cJSON_AddItemToArray(ids, cJSON_CreateString(r->ids[r->at]));
  cJSON_AddItemToObject(o, "ids", ids); cJSON_AddBoolToObject(o, "all", 0); char *body = cJSON_PrintUnformatted(o); cJSON_Delete(o);
  net_post_json(STREMIO_API "/api/datastoreGet", body, on_pull_get, r); free(body);
}
typedef struct { char id[SB_ID]; double mt; } Want; /* a title the account has newer, and when it changed there */
static int want_newer(const void *a, const void *b) { double x = ((const Want *)a)->mt, y = ((const Want *)b)->mt; return x < y ? 1 : x > y ? -1 : 0; }
static void on_pull_meta(int st, const unsigned char *b, size_t n, void *ud) {
  PullReq *r = ud; cJSON *j = st == 200 ? net_json(b) : NULL; const cJSON *rows = js_arr(j, "result");
  if (!rows) { net_json_free(j); libSyncing = 0; int ch = lib_migrate_legacy(); if (r->cb) r->cb(ch, r->ud); free(r); return; }
  cJSON *lib = lib_arr(); const cJSON *row; cJSON *remote = cJSON_CreateObject();
  /* what the account has newer than us */
  int nrows = cJSON_GetArraySize(rows); Want *wants = malloc(sizeof(Want) * (size_t)(nrows > 0 ? nrows : 1)); int nw = 0; /* (a library of thousands: every one of them, not the first 2000) */
  if (!wants) { net_json_free(j); libSyncing = 0; if (r->cb) r->cb(0, r->ud); free(r); return; }
  cJSON_ArrayForEach(row, rows) { if (!cJSON_IsArray(row) || cJSON_GetArraySize(row) < 2) continue; const char *id = cJSON_GetArrayItem(row, 0)->valuestring; double mt = tms(cJSON_GetArrayItem(row, 1)); if (!id) continue;
    cJSON_AddNumberToObject(remote, id, mt); cJSON *loc = cJSON_GetObjectItemCaseSensitive(lib, id);
    if ((!loc || tms(cJSON_GetObjectItemCaseSensitive(loc, "_mtime")) < mt) && nw < nrows) { copy_str(wants[nw].id, sizeof wants[nw].id, id); wants[nw].mt = mt; nw++; } }
  /* what we have newer than the account (or it never saw) */
  cJSON *push = cJSON_CreateArray(); int np = 0, npAll = 0; const cJSON *it; /* sent 100 to a request, all of them (it stopped at the first 100) */
  cJSON_ArrayForEach(it, lib) { const char *id = js_str(it, "_id", NULL); if (!id || !strcmp(js_str(it, "type", ""), "other")) continue; const cJSON *rm = cJSON_GetObjectItemCaseSensitive(remote, id);
    int go = rm ? tms(cJSON_GetObjectItemCaseSensitive(it, "_mtime")) > rm->valuedouble : (!js_bool(it, "removed", 0) || js_bool(it, "temp", 0));
    if (go) { cJSON_AddItemToArray(push, cJSON_Duplicate(it, 1)); np++; npAll++; }
    if (np == 100 || (np && !it->next)) { cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "authKey", sb_auth_key()); cJSON_AddStringToObject(o, "collection", "libraryItem"); cJSON_AddItemToObject(o, "changes", push); char *body = cJSON_PrintUnformatted(o); cJSON_Delete(o); net_post_json(STREMIO_API "/api/datastorePut", body, NULL, NULL); free(body); push = cJSON_CreateArray(); np = 0; } }
  cJSON_Delete(push);
  cJSON_Delete(remote);
  /* newest first, every one (it took the newest 500 a sync: a library of thousands filled in over many syncs) */
  qsort(wants, (size_t)nw, sizeof *wants, want_newer);
  r->ids = malloc(sizeof *r->ids * (size_t)(nw ? nw : 1)); r->nwant = r->ids ? nw : 0; for (int i = 0; i < r->nwant; i++) copy_str(r->ids[i], sizeof r->ids[i], wants[i].id); r->at = 0; free(wants);
  fprintf(stderr, "[lib] sync: %d titles on the account, %d newer there, %d sent\n", nrows, nw, npAll);
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
    copy_str(o->label, sizeof o->label, js_str(x, "label", "")); copy_str(o->addon, sizeof o->addon, addon); o->from = SUB_STREAM; o->n = e->n; o->forced = forcedsubs_entry(x, o->label); }
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
  unsigned g = addon_gen();
  for (int i = 0; i < 40; i++) { const cJSON *m = metaMem[i].meta; if (!m || metaMem[i].gen != g || strncmp(metaMem[i].key, type, strlen(type)) || metaMem[i].key[strlen(type)] != '_') continue;
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
  if (ans) ans(r->pending, r->count, ud); /* (how many were asked, at once: a wait can say how many are left from its start) */
  return r->tok;
}
void sb_stream_info(const Stream *s, char *out, size_t cap) { snprintf(out, cap, "%s %s %s", s->name[0] ? s->name : s->addon, s->title, s->filename); for (char *p = out; *p; p++) if (*p == '\n' || *p == '\r' || *p == '\t') *p = ' '; }
/* a remembered stream is only offered again to the account that played it, and only while the addon it came from is
   still installed and on (another account's, an older one's or a removed addon's streams never come back) */
/* (and only while that addon is set up as it was then: addons put their configuration in their stream addresses, so
   a stream remembered before the addon was configured again on any device is the old configuration's; played, the
   addon answers that it is obsolete. A stream remembered before its addon's address was kept: never used again) */
static int resume_valid(const cJSON *s) {
  const char *acct = js_str(s, "acct", NULL); if (!acct || strcmp(acct, sb_user_id())) return 0;
  const char *an = js_str(s, "addon", ""), *tu = js_str(s, "tu", ""); cJSON *a = sb_addons(); const cJSON *ad; if (!a || !an[0] || !tu[0]) return 0;
  cJSON_ArrayForEach(ad, a) if (sb_addon_on(ad) && !strcmp(addon_name(ad), an) && !strcmp(js_str(ad, "transportUrl", ""), tu)) return 1;
  return 0;
}
/* the address of the addon a stream came from (by its name; of two with that name, the first on) */
static const char *addon_tu(const char *name) {
  cJSON *a = sb_addons(); const cJSON *ad; if (!a || !name[0]) return NULL;
  cJSON_ArrayForEach(ad, a) if (sb_addon_on(ad) && !strcmp(addon_name(ad), name)) return js_str(ad, "transportUrl", NULL);
  return NULL;
}
int sb_resume_stream(const char *id, const char *vid, Stream *out) {
  char k[200]; snprintf(k, sizeof k, "%s_%s", id, vid); const cJSON *all = store_node("streams"), *s = all ? cJSON_GetObjectItemCaseSensitive(all, k) : NULL; if (!s) return 0;
  if (!resume_valid(s)) { fprintf(stderr, "[resume] %s: the remembered stream (%s) is not this account's, or its addon is gone or set up again since: not used\n", k, js_str(s, "addon", "?")); return 0; }
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
  { const char *tu = addon_tu(s->addon); if (tu && *tu) cJSON_AddStringToObject(o, "tu", tu); } /* its addon as it is set up now */
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
  SugReq *r = malloc(sizeof *r); r->cb = cb; r->ud = ud; net_get(url, on_sug, r);
}
