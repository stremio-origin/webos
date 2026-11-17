#include "core/notify.h"
#include "core/addons.h"
#include "core/net.h"
#include "core/store.h"
#include "core/json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <SDL2/SDL.h>

#define EVERY (6 * 3600)        /* as the official apps ask */
#define MAXIDS 100              /* the library's series asked about, newest first */
#define AHEAD (14 * 86400.0)    /* Upcoming: what airs in the next two weeks */

static double iso_time(const char *s) { /* "2026-11-02T10:00:00.000Z" -> seconds; 0 when it isn't one */
  if (!s || !*s) return 0; struct tm tm; memset(&tm, 0, sizeof tm);
  if (sscanf(s, "%d-%d-%dT%d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) < 3) return 0;
  tm.tm_year -= 1900; tm.tm_mon--; return (double)timegm(&tm);
}
/* whose new episodes are looked for (as the official apps choose) */
static int eligible(const cJSON *it) {
  const char *ty = js_str(it, "type", ""); const cJSON *st = js_obj(it, "state");
  if (strcmp(ty, "series")) return 0; /* (films, live channels, "other": never) */
  if (js_bool(it, "removed", 0) || js_bool(it, "temp", 0) || js_bool(st, "noNotif", 0)) return 0;
  if (*js_str(js_obj(it, "behaviorHints"), "defaultVideoId", "")) return 0; /* a single-video title */
  return *js_str(it, "_id", "") != 0;
}

/* ---- asking ---- */
static struct { int busy, pending, gotAny; double retryAt, bootAt; cJSON *eps, *up; void (*changed)(void); int force; } N;
static cJSON *node(void) { cJSON *n = store_node("notifs"); return cJSON_IsObject(n) ? n : NULL; }
static void finish(void) {
  N.busy = 0;
  if (!N.gotAny) { cJSON_Delete(N.eps); cJSON_Delete(N.up); N.eps = N.up = NULL; N.retryAt = SDL_GetTicks() / 1000.0 + 1800; fprintf(stderr, "[notify] no answer: asked again in 30 min\n"); return; }
  cJSON *n = cJSON_CreateObject(); cJSON_AddNumberToObject(n, "at", (double)time(NULL)); cJSON_AddItemToObject(n, "eps", N.eps); cJSON_AddItemToObject(n, "up", N.up); N.eps = N.up = NULL;
  cJSON *old = node(); char *a = old ? cJSON_PrintUnformatted(cJSON_GetObjectItem(old, "eps")) : NULL, *b = cJSON_PrintUnformatted(cJSON_GetObjectItem(n, "eps"));
  char *a2 = old ? cJSON_PrintUnformatted(cJSON_GetObjectItem(old, "up")) : NULL, *b2 = cJSON_PrintUnformatted(cJSON_GetObjectItem(n, "up"));
  int same = a && b && !strcmp(a, b) && a2 && b2 && !strcmp(a2, b2); free(a); free(b); free(a2); free(b2);
  fprintf(stderr, "[notify] %d series' recent episodes, %d coming%s\n", cJSON_GetArraySize(cJSON_GetObjectItem(n, "eps")), cJSON_GetArraySize(cJSON_GetObjectItem(n, "up")), same ? " (as before)" : "");
  store_set_node("notifs", n); store_flush();
  if (!same && N.changed) N.changed();
}
static void take(const cJSON *metas, int calendar) {
  const cJSON *m; double now = (double)time(NULL);
  cJSON_ArrayForEach(m, metas) {
    const char *id = js_str(m, "id", js_str(m, "imdb_id", "")); if (!*id) continue;
    const cJSON *v; cJSON *keep = calendar ? NULL : cJSON_CreateArray(); double bestR = 0; const cJSON *best = NULL;
    cJSON_ArrayForEach(v, js_arr(m, "videos")) {
      double r = iso_time(js_str(v, "released", js_str(v, "firstAired", ""))); if (r <= 0) continue;
      int s = (int)js_num(v, "season", -1), e = (int)js_num(v, "episode", js_num(v, "number", -1)); if (s < 0 || e < 0) continue;
      if (calendar) { if (r > now && r <= now + AHEAD && (!best || r < bestR)) { best = v; bestR = r; } continue; } /* the soonest one coming */
      if (r > now) continue; /* only what is out */
      cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "id", js_str(v, "id", "")); cJSON_AddNumberToObject(o, "r", r); cJSON_AddNumberToObject(o, "s", s); cJSON_AddNumberToObject(o, "e", e); cJSON_AddItemToArray(keep, o);
    }
    if (calendar && best) { cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "sid", id); cJSON_AddStringToObject(o, "id", js_str(best, "id", "")); cJSON_AddNumberToObject(o, "r", bestR);
      cJSON_AddNumberToObject(o, "s", js_num(best, "season", 0)); cJSON_AddNumberToObject(o, "e", js_num(best, "episode", js_num(best, "number", 0))); cJSON_AddItemToArray(N.up, o); }
    if (keep) { if (cJSON_GetArraySize(keep) && !cJSON_GetObjectItemCaseSensitive(N.eps, id)) cJSON_AddItemToObject(N.eps, id, keep); else cJSON_Delete(keep); }
  }
}
static void on_answer(int st, const unsigned char *b, size_t n, void *ud) {
  (void)n; int calendar = (int)(intptr_t)ud; cJSON *j = st == 200 ? net_json(b) : NULL; const cJSON *ms = js_arr(j, "metasDetailed"); if (!ms) ms = js_arr(j, "metas");
  if (ms && N.eps) { N.gotAny = 1; take(ms, calendar); } else fprintf(stderr, "[notify] a %s catalog didn't answer (%d)\n", calendar ? "calendar" : "new episodes", st);
  net_json_free(j); if (--N.pending <= 0) finish();
}
static int cmp_mtime(const void *a, const void *b) { double x = iso_time(js_str(*(const cJSON *const *)a, "_mtime", "")), y = iso_time(js_str(*(const cJSON *const *)b, "_mtime", "")); return x < y ? 1 : x > y ? -1 : 0; }
static void ask(void) {
  cJSON *lib = store_node("lib"); if (!cJSON_IsObject(lib)) return;
  const cJSON *it, *picks[512]; int n = 0;
  cJSON_ArrayForEach(it, lib) if (eligible(it) && n < 512) picks[n++] = it;
  if (!n) { fprintf(stderr, "[notify] no series in the library to follow\n"); cJSON *z = cJSON_CreateObject(); cJSON_AddNumberToObject(z, "at", (double)time(NULL)); cJSON_AddItemToObject(z, "eps", cJSON_CreateObject()); cJSON_AddItemToObject(z, "up", cJSON_CreateArray()); store_set_node("notifs", z); return; }
  qsort(picks, (size_t)n, sizeof picks[0], cmp_mtime); if (n > MAXIDS) n = MAXIDS;
  N.eps = cJSON_CreateObject(); N.up = cJSON_CreateArray(); N.pending = 1; N.gotAny = 0; N.busy = 1; /* (1: held while the requests go out) */
  cJSON *a = sb_addons(); const cJSON *ad;
  if (a) cJSON_ArrayForEach(ad, a) { if (!sb_addon_on(ad)) continue; char base[512]; addon_base(ad, base, sizeof base);
    const cJSON *cat; cJSON_ArrayForEach(cat, js_arr(js_obj(ad, "manifest"), "catalogs")) {
      if (strcmp(js_str(cat, "type", ""), "series")) continue;
      const cJSON *ex; int kind = -1; cJSON_ArrayForEach(ex, js_arr(cat, "extra")) { const char *nm = js_str(ex, "name", ""); if (!strcmp(nm, "lastVideosIds")) kind = 0; else if (!strcmp(nm, "calendarVideosIds")) kind = 1; }
      if (kind < 0) continue;
      char ids[MAXIDS * 72] = ""; size_t o = 0; int k = 0;
      for (int i = 0; i < n; i++) { const char *id = js_str(picks[i], "_id", ""); if (!addon_serves(ad, "catalog", "series", id) || o + strlen(id) + 2 >= sizeof ids) continue; o += (size_t)snprintf(ids + o, sizeof ids - o, "%s%s", k ? "," : "", id); k++; }
      if (!k) continue;
      char ex2[sizeof ids * 3 + 64] = ""; addon_extra_add(ex2, sizeof ex2, kind ? "calendarVideosIds" : "lastVideosIds", ids);
      char url[sizeof ex2 + 1024]; addon_url(url, sizeof url, base, "catalog", "series", js_str(cat, "id", ""), ex2);
      N.pending++; net_get(url, on_answer, (void *)(intptr_t)kind);
      fprintf(stderr, "[notify] asking %s about %d series (%s)\n", addon_name(ad), k, kind ? "coming" : "new episodes"); } }
  if (--N.pending <= 0) finish();
}
void notify_refresh(void) { N.force = 1; }
void notify_tick(void (*changed)(void)) {
  N.changed = changed; double t = SDL_GetTicks() / 1000.0; if (!N.bootAt) N.bootAt = t;
  if (N.busy || !sb_signed_in() || t - N.bootAt < 3 || t < N.retryAt) return; /* (a moment after the start: the library pull goes first) */
  cJSON *n = node(); double at = n ? js_num(n, "at", 0) : 0;
  if (!N.force && at > 0 && (double)time(NULL) - at < EVERY) return;
  N.force = 0; ask();
}

/* ---- reading ---- */
int notify_new(const cJSON *it, char *vid, size_t cap, double *newestAt) {
  if (!eligible(it)) return 0; cJSON *n = node(); const cJSON *eps = cJSON_GetObjectItemCaseSensitive(n ? cJSON_GetObjectItem(n, "eps") : NULL, js_str(it, "_id", ""));
  if (!cJSON_IsArray(eps)) return 0;
  double last = iso_time(js_str(js_obj(it, "state"), "lastWatched", "")), now = (double)time(NULL), first = 0, newest = 0; int cnt = 0; const cJSON *v, *fv = NULL;
  cJSON_ArrayForEach(v, eps) { double r = js_num(v, "r", 0); if (r <= 0 || r > now || (last > 0 && r <= last)) continue; /* out, and since it was last watched (never watched: all of it, as the official apps) */
    cnt++; if (r > newest) newest = r; if (!fv || r < first) { fv = v; first = r; } }
  if (!cnt) return 0;
  if (vid) snprintf(vid, cap, "%s", js_str(fv, "id", "")); if (newestAt) *newestAt = newest;
  return cnt;
}
static int cmp_air(const void *a, const void *b) { double x = ((const Item *)a)->airAt, y = ((const Item *)b)->airAt; return x < y ? -1 : x > y; }
int notify_upcoming(ItemList *out) {
  memset(out, 0, sizeof *out); cJSON *n = node(); const cJSON *up = n ? cJSON_GetObjectItem(n, "up") : NULL; int m = cJSON_GetArraySize(up); if (m <= 0) return 0;
  out->items = calloc((size_t)m, sizeof(Item)); if (!out->items) return 0;
  double now = (double)time(NULL); const cJSON *u;
  cJSON_ArrayForEach(u, up) { double r = js_num(u, "r", 0); if (r <= now || r > now + AHEAD) continue; /* (aired since it was asked: no longer coming) */
    const cJSON *l = lib_get(js_str(u, "sid", "")); if (!l || !eligible(l)) continue;
    if (cJSON_GetObjectItemCaseSensitive(store_node("upHide"), js_str(u, "id", ""))) continue; /* dismissed by hand: until the next episode is the one coming */
    Item *it = &out->items[out->n++]; memset(it, 0, sizeof *it);
    snprintf(it->id, sizeof it->id, "%s", js_str(l, "_id", "")); snprintf(it->type, sizeof it->type, "series"); snprintf(it->name, sizeof it->name, "%s", js_str(l, "name", ""));
    snprintf(it->poster, sizeof it->poster, "%s", js_str(l, "poster", "")); snprintf(it->background, sizeof it->background, "%s", js_str(l, "background", "")); snprintf(it->logo, sizeof it->logo, "%s", js_str(l, "logo", ""));
    snprintf(it->videoId, sizeof it->videoId, "%s", js_str(u, "id", "")); it->airAt = r; }
  if (!out->n) { free(out->items); out->items = NULL; return 0; }
  qsort(out->items, (size_t)out->n, sizeof(Item), cmp_air); return out->n;
}
void notify_dismiss(const char *id) {
  cJSON *l = lib_get(id); if (!l) return; cJSON *s = cJSON_GetObjectItemCaseSensitive(l, "state"); if (!s) return;
  time_t t = time(NULL); struct tm tm; gmtime_r(&t, &tm); char iso[40]; strftime(iso, sizeof iso, "%Y-%m-%dT%H:%M:%S.000Z", &tm);
  cJSON_DeleteItemFromObjectCaseSensitive(s, "lastWatched"); cJSON_AddStringToObject(s, "lastWatched", iso); lib_save(l); /* (synced: the other apps see it seen too) */
}

/* dismissed by hand from Continue Watching / Upcoming: kept on this device, and only until something happens that
   puts the title back (Continue Watching: it is watched or marked again, or an episode newer than the dismissal comes
   out; Upcoming: the next episode is another one) */
static cJSON *hide_node(const char *k) { cJSON *n = store_node(k); if (cJSON_IsObject(n)) return n; store_set_node(k, cJSON_CreateObject()); return store_node(k); }
void notify_cw_dismiss(const Item *it) {
  cJSON *l = lib_get(it->id); if (!l) return; if (it->newEp) notify_dismiss(it->id); /* (its new episodes seen, as the official apps dismiss: synced) */
  cJSON *n = hide_node("cwHide"), *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "lw", js_str(js_obj(l, "state"), "lastWatched", "")); cJSON_AddNumberToObject(o, "at", (double)time(NULL));
  cJSON_DeleteItemFromObjectCaseSensitive(n, it->id); cJSON_AddItemToObject(n, it->id, o);
  double now = (double)time(NULL); cJSON *e = n->child; while (e) { cJSON *nx = e->next; if (now - js_num(e, "at", 0) > 400 * 86400.0) cJSON_Delete(cJSON_DetachItemViaPointer(n, e)); e = nx; } /* (a year on: forgotten) */
  store_set_node("cwHide", n); fprintf(stderr, "[cw] dismissed %s\n", it->id);
}
int notify_cw_hidden(const cJSON *l, double newestNew) {
  const cJSON *o = cJSON_GetObjectItemCaseSensitive(store_node("cwHide"), js_str(l, "_id", "")); if (!o) return 0;
  if (strcmp(js_str(o, "lw", ""), js_str(js_obj(l, "state"), "lastWatched", ""))) return 0; /* watched or marked since */
  return !(newestNew > js_num(o, "at", 0)); /* an episode out since the dismissal: back */
}
void notify_up_dismiss(const Item *it) {
  if (!it->videoId[0]) return; cJSON *n = hide_node("upHide"); double now = (double)time(NULL);
  cJSON_DeleteItemFromObjectCaseSensitive(n, it->videoId); cJSON_AddNumberToObject(n, it->videoId, it->airAt > 0 ? it->airAt : now);
  cJSON *e = n->child; while (e) { cJSON *nx = e->next; if (cJSON_IsNumber(e) && e->valuedouble < now - 86400) cJSON_Delete(cJSON_DetachItemViaPointer(n, e)); e = nx; } /* (aired: no longer needed) */
  store_set_node("upHide", n); fprintf(stderr, "[upcoming] dismissed %s\n", it->videoId);
}
