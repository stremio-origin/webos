#include "core/addons.h"
#include "core/stremio.h"
#include "core/json.h"
#include <stdio.h>
#include <string.h>

/* as encodeURIComponent: what the addon SDK's router decodes */
void addon_urlenc(const char *s, char *out, size_t cap) {
  size_t o = 0; static const char *hex = "0123456789ABCDEF"; if (!cap) return;
  for (; s && *s && o + 4 < cap; s++) { unsigned char c = (unsigned char)*s;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("-_.!~*'()", c)) out[o++] = (char)c;
    else { out[o++] = '%'; out[o++] = hex[c >> 4]; out[o++] = hex[c & 15]; } }
  out[o] = 0;
}
void addon_base(const cJSON *ad, char *out, size_t cap) {
  snprintf(out, cap, "%s", js_str(ad, "transportUrl", "")); char *m = strstr(out, "/manifest.json"); if (m) *m = 0;
  size_t n = strlen(out); while (n && out[n - 1] == '/') out[--n] = 0;
}
const char *addon_name(const cJSON *ad) { const char *n = js_str(js_obj(ad, "manifest"), "name", ""); return *n ? n : "Addon"; }

static int has_str(const cJSON *arr, const char *s) { const cJSON *t; cJSON_ArrayForEach(t, arr) if (cJSON_IsString(t) && !strcmp(t->valuestring, s)) return 1; return 0; }
/* no list, or an empty one: every id */
static int prefix_ok(const cJSON *pf, const char *id) {
  if (!id || !cJSON_IsArray(pf) || !cJSON_GetArraySize(pf)) return 1;
  const cJSON *p; cJSON_ArrayForEach(p, pf) if (cJSON_IsString(p) && !strncmp(id, p->valuestring, strlen(p->valuestring))) return 1; return 0;
}
/* the first resource of that name decides (as the official core): a plain name takes the manifest's types and
   prefixes; the object form its own (its types default to the manifest's, as the SDK documents; no prefixes there
   means every id) */
/* anyOf: a NULL-terminated list of types also taken when the declared types leave them out (NULL: none) */
static int serves(const cJSON *ad, const char *resource, const char *type, const char *id, const char *const *anyOf) {
  if (!ad || !sb_addon_on(ad)) return 0; const cJSON *man = js_obj(ad, "manifest"), *res = js_arr(man, "resources"), *r; if (!res) return 0;
  int extra = 0; for (int k = 0; anyOf && anyOf[k] && !extra; k++) extra = type && !strcmp(type, anyOf[k]);
  cJSON_ArrayForEach(r, res) {
    if (cJSON_IsString(r) && r->valuestring && !strcmp(r->valuestring, resource)) return (extra || has_str(js_arr(man, "types"), type)) && prefix_ok(cJSON_GetObjectItemCaseSensitive(man, "idPrefixes"), id);
    if (cJSON_IsObject(r) && !strcmp(js_str(r, "name", ""), resource)) {
      const cJSON *ts = js_arr(r, "types"); if (!ts) ts = js_arr(man, "types");
      return (extra || (ts && has_str(ts, type))) && prefix_ok(cJSON_GetObjectItemCaseSensitive(r, "idPrefixes"), id); } }
  return 0;
}
int addon_serves(const cJSON *ad, const char *resource, const char *type, const char *id) { return serves(ad, resource, type, id, NULL); }
/* subtitles: an addon that declares them is asked for movies, series and anime whatever its types say (prefixes still apply) */
int addon_serves_subs(const cJSON *ad, const char *type, const char *id) { static const char *const MS[] = { "movie", "series", "anime", "Anime Movies", "Anime Series", NULL }; return serves(ad, "subtitles", type, id, MS); }
int addon_count_subs(const char *type, const char *id) {
  cJSON *a = sb_addons(); const cJSON *ad; int n = 0; if (a) cJSON_ArrayForEach(ad, a) if (addon_serves_subs(ad, type, id)) n++; return n;
}
void addon_extra_add(char *ex, size_t cap, const char *name, const char *value) {
  size_t l = strlen(ex); char v[600]; addon_urlenc(value ? value : "", v, sizeof v);
  snprintf(ex + l, cap > l ? cap - l : 0, "%s%s=%s", l ? "&" : "", name, v);
}
void addon_url(char *out, size_t cap, const char *base, const char *resource, const char *type, const char *id, const char *extra) {
  char t[96], i[400]; addon_urlenc(type, t, sizeof t); addon_urlenc(id, i, sizeof i);
  snprintf(out, cap, "%s/%s/%s/%s%s%s.json", base, resource, t, i, extra && *extra ? "/" : "", extra && *extra ? extra : "");
}

int addon_cat_extra(const cJSON *cat, const char *name, int *req, const cJSON **opts, int *optsLimit) {
  if (req) *req = 0; if (opts) *opts = NULL; if (optsLimit) *optsLimit = 1;
  const cJSON *ex = cJSON_GetObjectItemCaseSensitive(cat, "extra"), *e;
  if (cJSON_IsArray(ex)) { cJSON_ArrayForEach(e, ex) if (!strcmp(js_str(e, "name", ""), name)) {
      if (req) *req = js_bool(e, "isRequired", 0); const cJSON *o = js_arr(e, "options"); if (opts) *opts = o && cJSON_GetArraySize(o) ? o : NULL;
      if (optsLimit) { int l = (int)js_num(e, "optionsLimit", 1); *optsLimit = l < 1 ? 1 : l; }
      return 1; }
    return 0; }
  /* the old short form */
  const cJSON *sup = js_arr(cat, "extraSupported"), *rq = js_arr(cat, "extraRequired");
  if (!sup || !has_str(sup, name)) return 0; if (req && rq) *req = has_str(rq, name); return 1;
}
static int each_extra(const cJSON *cat, int i, const char **name, int *req, const cJSON **opts) {
  const cJSON *ex = cJSON_GetObjectItemCaseSensitive(cat, "extra");
  if (cJSON_IsArray(ex)) { const cJSON *e = cJSON_GetArrayItem(ex, i); if (!e) return 0; *name = js_str(e, "name", ""); *req = js_bool(e, "isRequired", 0); const cJSON *o = js_arr(e, "options"); *opts = o && cJSON_GetArraySize(o) ? o : NULL; return 1; }
  const cJSON *sup = js_arr(cat, "extraSupported"), *rq = js_arr(cat, "extraRequired"), *s = sup ? cJSON_GetArrayItem(sup, i) : NULL; if (!s) return 0;
  *name = cJSON_IsString(s) ? s->valuestring : ""; *req = rq && has_str(rq, *name); *opts = NULL; return 1;
}
int addon_cat_accepts(const cJSON *cat, const char *const *names, int n) {
  for (int k = 0; k < n; k++) if (!addon_cat_extra(cat, names[k], NULL, NULL, NULL)) return 0;
  const char *nm; int req; const cJSON *o;
  for (int i = 0; each_extra(cat, i, &nm, &req, &o); i++) { if (!req) continue; int got = 0; for (int k = 0; k < n; k++) if (!strcmp(names[k], nm)) got = 1; if (!got) return 0; }
  return 1;
}
int addon_cat_default_extra(const cJSON *cat, char *out, size_t cap) {
  out[0] = 0; const char *nm; int req; const cJSON *o;
  for (int i = 0; each_extra(cat, i, &nm, &req, &o); i++) { if (!req) continue; const cJSON *f = o ? cJSON_GetArrayItem(o, 0) : NULL; if (!cJSON_IsString(f)) return -1; addon_extra_add(out, cap, nm, f->valuestring); }
  return 0;
}
void addon_cache_hints(const cJSON *j, AddonCache *c) {
  c->maxAge = (long)js_num(j, "cacheMaxAge", -1); c->staleRevalidate = (long)js_num(j, "staleRevalidate", -1); c->staleError = (long)js_num(j, "staleError", -1);
}
