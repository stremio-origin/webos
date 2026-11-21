#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/json.h"
#define SB_ID 256
#define CINEMETA "https://cinemeta.test"
typedef void (*NetCb)(int, const unsigned char *, size_t, void *);
typedef struct { int maxAge; } AddonCache;
static unsigned generation = 1;
static int key = 1, disk, callbacks;
static cJSON *addons, *result;
static char saved[8192];
static const char *disk_body;
typedef struct { int kind, used; NetCb cb; void *ud; } Pending;
static Pending pending[128]; static int np;
static unsigned enqueue(int kind, NetCb cb, void *ud) {
  assert(np < 128); pending[np] = (Pending){ kind, 0, cb, ud }; return ++np;
}
static unsigned net_get(const char *url, NetCb cb, void *ud) { (void)url; return enqueue(1, cb, ud); }
static unsigned net_get_first(const char *url, NetCb cb, void *ud) {
  assert(strstr(url, "cinemeta.test/meta/")); return enqueue(3, cb, ud);
}
static unsigned net_get_hdr_first(const char *url, const char *hdr, NetCb cb, void *ud) {
  assert(strstr(url, "external_ids")); assert(!strcmp(hdr, "TEST_HEADER")); return enqueue(2, cb, ud);
}
static unsigned net_file(const char *path, NetCb cb, void *ud) { (void)path; return enqueue(4, cb, ud); }
static cJSON *net_json(const unsigned char *b) { return b ? cJSON_Parse((const char *)b) : NULL; }
static void net_json_free(cJSON *j) { cJSON_Delete(j); }
static unsigned addon_gen(void) { return generation; }
static cJSON *sb_addons(void) { return addons; }
static int addon_serves(const cJSON *a, const char *r, const char *t, const char *id) {
  (void)a; (void)r; (void)t; (void)id; return 1;
}
static void addon_base(const cJSON *a, char *out, size_t cap) {
  (void)a; snprintf(out, cap, "https://addon.test");
}
static void addon_url(char *out, size_t cap, const char *base, const char *resource,
                      const char *type, const char *id, const char *extra) {
  (void)extra; snprintf(out, cap, "%s/%s/%s/%s.json", base, resource, type, id);
}
static int tmdb_call(const char *path, char *url, size_t cap, char *hdr, size_t hcap) {
  if (!key) return 0;
  snprintf(url, cap, "https://tmdb.test%s", path); snprintf(hdr, hcap, "TEST_HEADER"); return 1;
}
static int cache_file(const char *k, int age, char *path, size_t cap) {
  (void)k; (void)age; snprintf(path, cap, "fake-cache"); return disk;
}
static void cache_write(const char *k, const void *b, size_t n) {
  (void)k; assert(n < sizeof saved); memcpy(saved, b, n); saved[n] = 0;
}
static void addon_cache_hints(const cJSON *j, AddonCache *h) {
  h->maxAge = (int)js_num(j, "cacheMaxAge", 0);
}
static int sb_is_imdb(const char *id) { return id && !strncmp(id, "tt", 2); }
static void copy_str(char *out, size_t cap, const char *s) { snprintf(out, cap, "%s", s); }

/* Track request lifetimes separately from cJSON's internal allocations. */
static void *live[128]; static int live_n;
static void *track_calloc(size_t n, size_t s) {
  void *p = calloc(n, s); assert(p && live_n < 128); live[live_n++] = p; return p;
}
static void track_free(void *p) {
  for (int i = 0; i < live_n; i++) if (live[i] == p) { live[i] = live[--live_n]; break; }
  free(p);
}
#define calloc track_calloc
#define free track_free
#include "metadata-under-test.inc"
#undef calloc
#undef free

static void answer(cJSON *m, void *ud) { (void)ud; callbacks++; result = m; }
static void reply(int kind, int status, const char *body) {
  for (int i = 0; i < np; i++) if (!pending[i].used && pending[i].kind == kind) {
    pending[i].used = 1; pending[i].cb(status, (const unsigned char *)body, body ? strlen(body) : 0, pending[i].ud); return;
  }
  assert(!"No pending request of that kind");
}
static void reset(void) {
  assert(live_n == 0);
  for (int i = 0; i < np; i++) assert(pending[i].used);
  for (int i = 0; i < 40; i++) { cJSON_Delete(metaMem[i].meta); memset(&metaMem[i], 0, sizeof metaMem[i]); }
  cJSON_Delete(addons); addons = cJSON_Parse("[{}]"); generation++;
  np = callbacks = disk = 0; key = 1; result = NULL; saved[0] = 0;
}
static const char *primary = "{\"meta\":{\"id\":\"tmdb:70796\",\"name\":\"Preferred addon\",\"videos\":[{\"id\":\"tt5788792:1:1\",\"season\":1,\"episode\":1}]}}";
static const char *fallback = "{\"meta\":{\"id\":\"tt5788792\",\"name\":\"Maisel fallback\",\"videos\":[{\"id\":\"tt5788792:1:1\",\"season\":1,\"episode\":1}]}}";
static const char *mapping = "{\"imdb_id\":\"tt5788792\"}";
static void start(void) { sb_meta("series", "tmdb:70796", answer, NULL); }
int main(void) {
  reset(); start(); reply(2, 200, mapping); reply(3, 200, fallback);
  assert(callbacks == 1 && result);
  assert(!strcmp(js_str(result, "id", ""), "tmdb:70796"));
  assert(!strcmp(js_str(cJSON_GetArrayItem(js_arr(result, "videos"), 0), "id", ""), "tt5788792:1:1"));
  assert(strstr(saved, "tmdb:70796")); reply(1, 504, "{}"); assert(callbacks == 1);

  reset(); start(); reply(1, 200, primary); reply(2, 200, mapping);
  assert(callbacks == 1 && !strcmp(js_str(result, "name", ""), "Preferred addon"));

  reset(); start(); reply(2, 200, mapping); reply(1, 200, primary); reply(3, 200, fallback);
  assert(callbacks == 1 && !strcmp(js_str(result, "name", ""), "Preferred addon"));

  reset(); start(); reply(1, 504, "{}"); assert(!callbacks); reply(2, 200, mapping); reply(3, 200, fallback);
  assert(callbacks == 1 && result);

  reset(); start(); reply(2, 200, "{\"imdb_id\":null}"); reply(1, 504, "{}");
  assert(callbacks == 1 && !result);

  reset(); start(); reply(2, -1, NULL); reply(1, -1, NULL); assert(callbacks == 1 && !result);

  reset(); start(); reply(2, 200, mapping); reply(3, 200, "{\"meta\":{\"id\":\"tt999\",\"videos\":[{}]}}");
  reply(1, 504, "{}"); assert(callbacks == 1 && !result);

  reset(); start(); reply(1, 200, "{\"meta\":{\"id\":\"tmdb:70796\",\"videos\":[]}}");
  assert(!callbacks); reply(2, 200, mapping); reply(3, 200, fallback); assert(callbacks == 1 && result);

  reset(); start(); reply(1, 200, "{\"meta\":{\"id\":\"tmdb:70796\",\"videos\":[]}}");
  reply(2, 504, "{}"); assert(callbacks == 1 && result); /* a genuinely unaired series */

  reset(); start(); generation++; reply(1, 200, primary); reply(2, 200, mapping);
  assert(callbacks == 1 && !result && !saved[0]);

  reset(); key = 0; start(); assert(np == 1); reply(1, 200, primary); assert(callbacks == 1);

  reset(); sb_meta("series", "tmdb:70796/unsafe", answer, NULL); assert(np == 1);
  reply(1, 504, "{}"); assert(callbacks == 1 && !result);

  reset(); disk = 1; disk_body = primary; start(); reply(4, 200, disk_body);
  assert(callbacks == 1 && result); reply(2, 200, mapping); reply(3, 200, fallback); reply(1, 504, "{}");
  assert(callbacks == 1); /* disk delivery followed by refresh, not a second screen callback */

  reset(); disk = 1; start(); reply(4, 200, "{\"cacheMaxAge\":86400,\"meta\":{\"id\":\"tmdb:70796\",\"videos\":[{}]}}");
  assert(np == 1 && callbacks == 1); /* honour a still-fresh addon cache */

  reset(); sb_meta("series", "tt5788792", answer, NULL); assert(np == 2);
  reply(3, 200, fallback); reply(1, 504, "{}");
  assert(callbacks == 1 && !strcmp(js_str(result, "id", ""), "tt5788792"));

  reset(); sb_meta("series", "tt5788792", answer, NULL);
  reply(1, 504, "{}"); assert(!callbacks); reply(3, 200, fallback); assert(callbacks == 1 && result);

  reset(); cJSON_Delete(addons); addons = cJSON_Parse("[]");
  sb_meta("series", "tt5788792", answer, NULL); assert(np == 1);
  reply(1, 200, fallback); assert(callbacks == 1 && result); /* don't hedge Cinemeta against itself */

  reset(); start(); reply(2, 200, "{\"imdb_id\":\"tt123/unsafe\"}"); reply(1, 504, "{}");
  assert(callbacks == 1 && !result);

  reset(); disk = 1; start();
  reply(4, 200, "{\"cacheMaxAge\":86400,\"meta\":{\"id\":\"tmdb:70796\",\"videos\":[]}}");
  assert(!callbacks); reply(2, 200, mapping); reply(3, 200, fallback); reply(1, 504, "{}");
  assert(callbacks == 1 && cJSON_GetArraySize(js_arr(result, "videos")) == 1);

  reset(); puts("PASS: 19 metadata race/failure/cache/identity cases; no leaked request contexts");
  cJSON_Delete(addons); return 0;
}
