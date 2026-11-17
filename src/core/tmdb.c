#include "sbkeys.h" /* the app's own TMDB key, from the private keys file (tools/build.sh) */
#include "platform/plat.h"
#include "core/tmdb.h"
#include "core/httpd.h"
#include "core/store.h"
#include "core/i18n.h"
#include "core/app.h"
#include "core/json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/time.h>

#define FRESH_MS 259200000.0   /* 3 days */
#define ERR_SEC 300            /* a failed lookup is asked again after 5 min */
#define MAX_ENT 400
#define NSLOT 1024
#define WORKERS 4              /* four titles at a time, and four seasons at a time within one: TMDB's rate limit */
#define IMG "https://image.tmdb.org/t/p/w500"

/* ---- a lookup's result --------------------------------------------------------------------------- */
typedef struct { TmdbLoc pub; char *name, *overview, *logo, *poster, *genres[8]; TmdbEp *eps; } Loc;
static char *dupe(const char *s) { return strdup(s ? s : ""); }
static void loc_free(Loc *l) {
  if (!l) return;
  free(l->name); free(l->overview); free(l->logo); free(l->poster); for (int i = 0; i < l->pub.ngenres; i++) free(l->genres[i]);
  for (int i = 0; i < l->pub.neps; i++) { free((char *)l->eps[i].name); free((char *)l->eps[i].overview); }
  free(l->eps); free(l);
}
/* TMDB's stand-in name for an episode it has no title for */
static int generic_ep(const char *s) {
  static const char *P[] = { "\xd8\xa7\xd9\x84\xd8\xad\xd9\x84\xd9\x82\xd8\xa9", "Episode", "\xc3\x89pisode", "\xc3\xa9pisode", "Episodio", "Folge", "Aflevering", "Odcinek" };
  for (size_t i = 0; i < sizeof P / sizeof P[0]; i++) { size_t n = strlen(P[i]); if (strncasecmp(s, P[i], n)) continue;
    const char *p = s + n; while (*p == ' ' || *p == '\t') p++; if (!isdigit((unsigned char)*p)) continue; while (isdigit((unsigned char)*p)) p++; if (!*p) return 1; }
  return 0;
}
static int ep_cmp(const void *a, const void *b) { const TmdbEp *x = a, *y = b; return x->s != y->s ? x->s - y->s : x->e - y->e; }
/* the helper's JSON ({name, overview, genres, logo, poster, eps: {"s:e": [name, overview]}, lt}) -> a result */
static Loc *loc_from_json(const cJSON *v) {
  Loc *l = calloc(1, sizeof *l);
  l->name = dupe(js_str(v, "name", "")); l->overview = dupe(js_str(v, "overview", "")); l->logo = dupe(js_str(v, "logo", "")); l->poster = dupe(js_str(v, "poster", ""));
  const cJSON *g; cJSON_ArrayForEach(g, js_arr(v, "genres")) if (cJSON_IsString(g) && l->pub.ngenres < 8) l->genres[l->pub.ngenres++] = dupe(g->valuestring);
  const cJSON *eps = js_obj(v, "eps"), *e; int n = cJSON_GetArraySize(eps), k = 0;
  if (n > 0) l->eps = calloc((size_t)n, sizeof *l->eps);
  cJSON_ArrayForEach(e, eps) { int s, en; if (!e->string || sscanf(e->string, "%d:%d", &s, &en) != 2 || !cJSON_IsArray(e)) continue;
    const cJSON *a = cJSON_GetArrayItem(e, 0), *b = cJSON_GetArrayItem(e, 1); const char *nm = cJSON_IsString(a) ? a->valuestring : "", *ov = cJSON_IsString(b) ? b->valuestring : "";
    if (generic_ep(nm)) nm = "";
    if (!*nm && !*ov) continue;
    l->eps[k].s = s; l->eps[k].e = en; l->eps[k].name = dupe(nm); l->eps[k].overview = dupe(ov); k++; }
  if (k > 1) qsort(l->eps, (size_t)k, sizeof *l->eps, ep_cmp);
  l->pub.name = l->name; l->pub.overview = l->overview; l->pub.logo = l->logo; l->pub.poster = l->poster;
  for (int i = 0; i < l->pub.ngenres; i++) l->pub.genres[i] = l->genres[i];
  l->pub.eps = l->eps; l->pub.neps = k; l->pub.light = js_num(v, "lt", 0) != 0;
  return l;
}

/* ---- the key ------------------------------------------------------------------------------------- */
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static char key[420], builtin[420], keyPath[1200], locDir[1200], apiBase[256];
static char phoneKey[420]; static unsigned phoneKeyGen, seenKeyGen;  /* a key saved from the phone (server thread) */
static int key_ok(const char *k) { size_t n = strlen(k); if (n < 20 || n > 400) return 0; for (const char *p = k; *p; p++) if (!isalnum((unsigned char)*p) && *p != '.' && *p != '_' && *p != '-') return 0; return 1; }
static void strip_ws(const char *in, char *out, size_t cap) { size_t o = 0; for (; *in && o + 1 < cap; in++) if (!isspace((unsigned char)*in)) out[o++] = *in; out[o] = 0; }
static int write_file(const char *p, const char *data, size_t len) {
  char tmp[1300]; snprintf(tmp, sizeof tmp, "%s.tmp", p); FILE *f = fopen(tmp, "wb"); if (!f) return 0;
  size_t w = fwrite(data, 1, len, f); fclose(f); if (w != len) { remove(tmp); return 0; } return rename(tmp, p) == 0;
}
static char *read_file(const char *p, size_t *len) {
  FILE *f = fopen(p, "rb"); if (!f) return NULL; fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  if (n < 0 || n > 8 * 1024 * 1024) { fclose(f); return NULL; }
  char *b = malloc((size_t)n + 1); size_t g = fread(b, 1, (size_t)n, f); fclose(f); b[g] = 0; if (len) *len = g; return b;
}
static void url_enc(const char *s, char *out, size_t cap) { static const char *H = "0123456789ABCDEF"; size_t o = 0;
  for (; *s && o + 4 < cap; s++) { unsigned char c = (unsigned char)*s; if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out[o++] = (char)c; else { out[o++] = '%'; out[o++] = H[c >> 4]; out[o++] = H[c & 15]; } }
  out[o] = 0; }
/* the URL and header of a call with this key: a v4 "API Read Access Token" (longer than 40) goes as a header */
static void call_url(const char *path, const char *k, char *url, size_t ucap, char *hdr, size_t hcap) {
  int bearer = strlen(k) > 40; hdr[0] = 0;
  if (bearer) { snprintf(url, ucap, "%s%s", apiBase, path); snprintf(hdr, hcap, "Authorization: Bearer %s", k); }
  else { char e[1300]; url_enc(k, e, sizeof e); snprintf(url, ucap, "%s%s%capi_key=%s", apiBase, path, strchr(path, '?') ? '&' : '?', e); }
}
/* worker threads: a blocking call; *st the HTTP status (0 = no answer) */
static cJSON *tmdb_get(const char *path, const char *k, int *st) {
  char url[2048], hdr[480]; call_url(path, k, url, sizeof url, hdr, sizeof hdr);
  size_t len = 0; unsigned char *b = net_fetch_sync_hdr(url, hdr[0] ? hdr : NULL, &len, st, 12000);
  cJSON *j = b ? cJSON_Parse((const char *)b) : NULL; free(b); return j; /* a worker thread: parsed here */
}
/* the key page's Save (server thread): checked against TMDB before it is kept */
static int key_from_phone(const char *raw) {
  char k[420]; strip_ws(raw, k, sizeof k); if (!key_ok(k)) return 0;
  int st = 0; cJSON *j = tmdb_get("/configuration", k, &st); cJSON_Delete(j);
  if (st != 200) return 0;
  if (!write_file(keyPath, k, strlen(k))) return 0;
  pthread_mutex_lock(&mu); snprintf(phoneKey, sizeof phoneKey, "%s", k); phoneKeyGen++; pthread_mutex_unlock(&mu);
  return 1;
}

/* ---- the work queue ------------------------------------------------------------------------------ */
enum { E_OK = 0, E_BADKEY, E_NET };
typedef struct Job { struct Job *next; char type[8], id[16], lang[8], key[420]; int light, err; unsigned gen; Loc *v; int none; } Job;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static Job *qHead, *qTail, *done; static int workersOn; static volatile int haveDone;
static double now_ms(void) { struct timeval tv; gettimeofday(&tv, NULL); return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0; }
static const char *best_image(const cJSON *list, const char *lang) { /* the most voted one in that language, PNG before SVG (logos are drawn as pictures) */
  const cJSON *x, *b = NULL;
  cJSON_ArrayForEach(x, list) { const char *fp = js_str(x, "file_path", ""); size_t n = strlen(fp);
    if (strcmp(js_str(x, "iso_639_1", ""), lang) || (n >= 4 && !strcasecmp(fp + n - 4, ".svg"))) continue;
    double va = js_num(x, "vote_average", 0), vb = b ? js_num(b, "vote_average", 0) : 0;
    if (!b || va > vb || (va == vb && js_num(x, "vote_count", 0) > js_num(b, "vote_count", 0))) b = x; }
  return b ? js_str(b, "file_path", "") : "";
}
typedef struct { pthread_mutex_t m; const Job *j; int tid, *ss, nss, next; cJSON *eps; } Seasons;
static void *season_worker(void *arg) {
  Seasons *S = arg;
  for (;;) {
    pthread_mutex_lock(&S->m); int i = S->next < S->nss ? S->next++ : -1; pthread_mutex_unlock(&S->m); if (i < 0) break;
    char p[160]; snprintf(p, sizeof p, "/tv/%d/season/%d?language=%s", S->tid, S->ss[i], S->j->lang);
    int st = 0; cJSON *s = tmdb_get(p, S->j->key, &st); const cJSON *e;
    pthread_mutex_lock(&S->m);
    cJSON_ArrayForEach(e, js_arr(s, "episodes")) { const char *nm = js_str(e, "name", ""), *ov = js_str(e, "overview", ""); if (!*nm && !*ov) continue;
      char k[32]; snprintf(k, sizeof k, "%d:%d", S->ss[i], (int)js_num(e, "episode_number", 0));
      cJSON *a = cJSON_CreateArray(); cJSON_AddItemToArray(a, cJSON_CreateString(nm)); cJSON_AddItemToArray(a, cJSON_CreateString(ov));
      cJSON_DeleteItemFromObjectCaseSensitive(S->eps, k); cJSON_AddItemToObject(S->eps, k, a); }
    pthread_mutex_unlock(&S->m); cJSON_Delete(s);
  }
  return NULL;
}
/* one title from TMDB: v in the helper's form, or NULL with the reason in *err (*none: TMDB does not know it) */
static cJSON *fetch(const Job *j, int *err, int *none) {
  char p[256]; int st = 0; *err = E_OK; *none = 0;
  snprintf(p, sizeof p, "/find/%s?external_source=imdb_id", j->id);
  cJSON *f = tmdb_get(p, j->key, &st);
  if (st == 401) { cJSON_Delete(f); *err = E_BADKEY; return NULL; }
  if (!f) { *err = E_NET; return NULL; }
  const cJSON *mv = cJSON_GetArrayItem(js_arr(f, "movie_results"), 0), *tv = cJSON_GetArrayItem(js_arr(f, "tv_results"), 0);
  const char *kind = !strcmp(j->type, "series") ? (tv ? "tv" : mv ? "movie" : "") : (mv ? "movie" : tv ? "tv" : "");
  if (!*kind) { cJSON_Delete(f); *none = 1; return NULL; }
  int tid = (int)js_num(!strcmp(kind, "tv") ? tv : mv, "id", 0); cJSON_Delete(f);
  char il[3]; snprintf(il, sizeof il, "%s", j->lang); const char *rg = j->lang[2] == '-' ? j->lang + 3 : ""; /* pt-BR: the words of "pt" in "BR", the pictures of "pt" */
  snprintf(p, sizeof p, "/%s/%d?language=%s&append_to_response=images,translations&include_image_language=%s", kind, tid, j->lang, il);
  cJSON *d = tmdb_get(p, j->key, &st);
  if (!d || !js_num(d, "id", 0)) { cJSON_Delete(d); *err = E_NET; return NULL; }
  const char *name = "", *ov = ""; const cJSON *t;
  cJSON_ArrayForEach(t, js_arr(js_obj(d, "translations"), "translations")) { const cJSON *td = js_obj(t, "data"); if (!td || strcmp(js_str(t, "iso_639_1", ""), il) || (*rg && strcmp(js_str(t, "iso_3166_1", ""), rg))) continue;
    if (!*name) { name = js_str(td, "title", ""); if (!*name) name = js_str(td, "name", ""); }
    if (!*ov) ov = js_str(td, "overview", ""); }
  cJSON *v = cJSON_CreateObject(), *gs = cJSON_CreateArray();
  cJSON_AddStringToObject(v, "name", name); cJSON_AddStringToObject(v, "overview", ov);
  cJSON_ArrayForEach(t, js_arr(d, "genres")) { const char *g = js_str(t, "name", ""); if (*g) cJSON_AddItemToArray(gs, cJSON_CreateString(g)); }
  cJSON_AddItemToObject(v, "genres", gs);
  const cJSON *im = js_obj(d, "images"); const char *lg = best_image(js_arr(im, "logos"), il), *po = best_image(js_arr(im, "posters"), il); char u[600];
  snprintf(u, sizeof u, "%s%s", *lg ? IMG : "", lg); cJSON_AddStringToObject(v, "logo", u);
  snprintf(u, sizeof u, "%s%s", *po ? IMG : "", po); cJSON_AddStringToObject(v, "poster", u);
  cJSON *eps = cJSON_CreateObject(); cJSON_AddItemToObject(v, "eps", eps);
  if (strcmp(kind, "tv")) { cJSON_Delete(d); return v; }
  if (j->light) { cJSON_AddNumberToObject(v, "lt", 1); cJSON_Delete(d); return v; }
  int ss[40], nss = 0; cJSON_ArrayForEach(t, js_arr(d, "seasons")) if (js_num(t, "episode_count", 0) && nss < 40) ss[nss++] = (int)js_num(t, "season_number", 0);
  cJSON_Delete(d);
  if (nss) {
    Seasons S; memset(&S, 0, sizeof S); pthread_mutex_init(&S.m, NULL); S.j = j; S.tid = tid; S.ss = ss; S.nss = nss; S.eps = eps;
    pthread_t th[3]; int nth = 0;
    for (int i = 0; i < 3 && i + 1 < nss; i++) if (!pthread_create(&th[nth], NULL, season_worker, &S)) nth++;
    season_worker(&S);
    for (int i = 0; i < nth; i++) pthread_join(th[i], NULL);
    pthread_mutex_destroy(&S.m);
  }
  return v;
}
static void prune(void) { /* keep the folder small: the oldest out past 800 titles */
  DIR *d = opendir(locDir); if (!d) return;
  struct { char n[64]; time_t t; } *fl = NULL; int n = 0, cap = 0; struct dirent *e;
  while ((e = readdir(d))) { if (e->d_name[0] == '.' || strlen(e->d_name) >= 64) continue;
    if (n == cap) { cap = cap ? cap * 2 : 256; fl = realloc(fl, (size_t)cap * sizeof *fl); }
    char p[1300]; struct stat st; snprintf(p, sizeof p, "%s/%s", locDir, e->d_name); snprintf(fl[n].n, 64, "%s", e->d_name); fl[n].t = stat(p, &st) ? 0 : st.st_mtime; n++; }
  closedir(d);
  if (n > 800) for (int k = 0; k < 200; k++) { int o = -1; for (int i = 0; i < n; i++) if (fl[i].n[0] && (o < 0 || fl[i].t < fl[o].t)) o = i;
    if (o < 0) break; char p[1300]; snprintf(p, sizeof p, "%s/%s", locDir, fl[o].n); remove(p); fl[o].n[0] = 0; }
  free(fl);
}
static void run(Job *j) {
  char path[1400]; snprintf(path, sizeof path, "%s/%s_%s.json", locDir, j->lang, j->id);
  char *t = read_file(path, NULL); cJSON *c = t ? cJSON_Parse(t) : NULL; free(t);
  const cJSON *cv0 = js_obj(c, "v");
  if (cv0 && now_ms() - js_num(c, "at", 0) < FRESH_MS && (j->light || !js_num(cv0, "lt", 0))) { /* on disk and fresh */
    if (js_num(cv0, "none", 0)) j->none = 1; else j->v = loc_from_json(cv0);
    cJSON_Delete(c); return; }
  cJSON_Delete(c);
  cJSON *v = fetch(j, &j->err, &j->none);
  if (j->err) return;
  if (j->none) { v = cJSON_CreateObject(); cJSON_AddNumberToObject(v, "none", 1); }
  else j->v = loc_from_json(v);
  cJSON *o = cJSON_CreateObject(); cJSON_AddNumberToObject(o, "at", (double)(long long)now_ms()); cJSON_AddItemToObject(o, "v", v);
  char *s = cJSON_PrintUnformatted(o); cJSON_Delete(o);
  if (s) { mkdir(locDir, 0755); write_file(path, s, strlen(s)); free(s); }
  if (rand() % 20 == 0) prune();
}
static void *worker(void *arg) { plat_thread_background(10);
  (void)arg;
  for (;;) {
    pthread_mutex_lock(&mu); while (!qHead) pthread_cond_wait(&cv, &mu);
    Job *j = qHead; qHead = j->next; if (!qHead) qTail = NULL; j->next = NULL; pthread_mutex_unlock(&mu);
    run(j);
    pthread_mutex_lock(&mu); j->next = done; done = j; haveDone = 1; pthread_mutex_unlock(&mu);
  }
  return NULL;
}

/* ---- what the main thread knows ------------------------------------------------------------------ */
enum { S_NONE = 0, S_PENDING, S_READY, S_ERR };
typedef struct { char id[16]; unsigned char used, state, fullAsked, light; unsigned lastUse; double retryAt; Loc *v; } Ent;
static Ent tab[NSLOT]; static int nent; static unsigned gen = 1, lastFrame = ~0u; static char tabLang[8]; static int resetReq;
static unsigned hash(const char *s) { unsigned h = 2166136261u; for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u; return h; }
static Ent *find(const char *id, int add) {
  for (unsigned i = hash(id) & (NSLOT - 1), n = 0; n < NSLOT; i = (i + 1) & (NSLOT - 1), n++) {
    Ent *e = &tab[i];
    if (!e->used) { if (!add) return NULL; memset(e, 0, sizeof *e); e->used = 1; snprintf(e->id, sizeof e->id, "%s", id); nent++; return e; }
    if (!strcmp(e->id, id)) return e; }
  return NULL;
}
static void drop_queue(void) { pthread_mutex_lock(&mu); while (qHead) { Job *j = qHead; qHead = j->next; free(j); } qTail = NULL; pthread_mutex_unlock(&mu); }
static void reset(void) { for (int i = 0; i < NSLOT; i++) if (tab[i].used) loc_free(tab[i].v); memset(tab, 0, sizeof tab); nent = 0; gen++; drop_queue(); }
/* past 400 titles, the ones not drawn for the longest go until three quarters are left (what is being
   fetched, or was drawn this frame or the last, stays); a rebuilt table: no holes in the probing */
static int by_use(const void *a, const void *b) { const Ent *x = a, *y = b; return x->lastUse < y->lastUse ? -1 : x->lastUse > y->lastUse; }
static void evict(void) {
  Ent *old = malloc(sizeof tab); int n = 0; for (int i = 0; i < NSLOT; i++) if (tab[i].used) old[n++] = tab[i];
  qsort(old, (size_t)n, sizeof *old, by_use);
  memset(tab, 0, sizeof tab); nent = 0; int drop = n - MAX_ENT * 3 / 4;
  for (int i = 0; i < n; i++) {
    if (drop > 0 && old[i].state != S_PENDING && old[i].lastUse + 1 < app_frame()) { loc_free(old[i].v); drop--; continue; }
    Ent *e = find(old[i].id, 1); *e = old[i]; }
  free(old);
}
static void install(Job *j) {
  Ent *e = find(j->id, 1); if (!e) { loc_free(j->v); return; }
  if (j->err) { /* asked again in 5 min; a row's result already in stays */
    e->retryAt = app_now() + ERR_SEC; e->state = e->v ? S_READY : S_ERR; if (!j->light) e->fullAsked = 0; return; }
  if (j->none) { if (!e->v) e->state = S_READY; e->fullAsked = 0; if (!e->v) e->light = 0; return; } /* TMDB does not know it: the English stays */
  if (e->v && !e->v->pub.light && j->v->pub.light) { loc_free(j->v); e->state = S_READY; return; } /* a late row lookup never replaces a page's */
  loc_free(e->v); e->v = j->v; e->state = S_READY; if (!j->v->pub.light) e->fullAsked = 0; /* a page's lookup still on its way keeps the page waiting */
}
static int inited;
static void drain(void) {
  if (!inited) tmdb_init(); /* on first use: nothing to start at launch */
  unsigned f = app_frame(); if (f == lastFrame) return; lastFrame = f;
  pthread_mutex_lock(&mu); int kg = phoneKeyGen != seenKeyGen; if (kg) { seenKeyGen = phoneKeyGen; snprintf(key, sizeof key, "%s", phoneKey); } pthread_mutex_unlock(&mu);
  if (kg || resetReq || strcmp(tabLang, i18n_lang())) { resetReq = 0; snprintf(tabLang, sizeof tabLang, "%s", i18n_lang()); reset(); }
  if (nent > MAX_ENT) evict();
  if (!haveDone) return;
  pthread_mutex_lock(&mu); Job *list = done; done = NULL; haveDone = 0; pthread_mutex_unlock(&mu);
  while (list) { Job *j = list; list = j->next; if (j->gen == gen) install(j); else loc_free(j->v); free(j); }
}
static void enqueue(const char *type, const char *id, int light, int first) {
  Job *j = calloc(1, sizeof *j);
  snprintf(j->type, sizeof j->type, "%s", !strcmp(type, "series") ? "series" : "movie"); snprintf(j->id, sizeof j->id, "%s", id);
  snprintf(j->lang, sizeof j->lang, "%s", i18n_lang()); snprintf(j->key, sizeof j->key, "%s", key); j->light = light; j->gen = gen;
  pthread_mutex_lock(&mu);
  if (!workersOn) { workersOn = 1; for (int i = 0; i < WORKERS; i++) { pthread_t t; if (!pthread_create(&t, NULL, worker, NULL)) pthread_detach(t); } }
  if (first) { j->next = qHead; qHead = j; if (!qTail) qTail = j; }
  else { if (qTail) qTail->next = j; else qHead = j; qTail = j; }
  pthread_cond_signal(&cv); pthread_mutex_unlock(&mu);
}
static void to_front(const char *id) { /* still waiting in the queue: ahead of the others */
  pthread_mutex_lock(&mu);
  for (Job *p = NULL, *j = qHead; j; p = j, j = j->next) if (!strcmp(j->id, id)) {
    if (p) { p->next = j->next; if (qTail == j) qTail = p; j->next = qHead; qHead = j; } break; }
  pthread_mutex_unlock(&mu);
}
static int is_imdb(const char *id) { if (!id || strncmp(id, "tt", 2)) return 0; size_t n = 0; for (const char *p = id + 2; *p; p++, n++) if (*p < '0' || *p > '9') return 0; return n >= 5 && n <= 10; }

void tmdb_init(void) {
  if (inited) return; inited = 1;
  snprintf(keyPath, sizeof keyPath, "%s/tmdb.key", store_data_dir()); snprintf(locDir, sizeof locDir, "%s/cache/loc", store_data_dir());
  const char *b = getenv("SB_TMDB"); snprintf(apiBase, sizeof apiBase, "%s", b && *b ? b : "https://api.themoviedb.org/3"); /* SB_TMDB: tests only */
  for (int i = 0; i < SBK_TMDB_API_KEY_LEN && i + 1 < (int)sizeof builtin; i++) builtin[i] = (char)(SBK_TMDB_API_KEY[i] ^ SBK_TMDB_API_KEY_PAD[i % 32]);
  if (!key_ok(builtin)) builtin[0] = 0;
  char *t = read_file(keyPath, NULL); if (t) { strip_ws(t, key, sizeof key); free(t); }
  if (!key[0]) snprintf(key, sizeof key, "%s", builtin); /* a user's own key first, else the app's */
  snprintf(tabLang, sizeof tabLang, "%s", i18n_lang());
  httpd_set_key_handler(key_from_phone);
}
int tmdb_has_key(void) { drain(); return key[0] != 0; }
/* a call for another part of the app (collections): its URL and header with the key in use; 0 when there is none */
/* the same from any thread (the pictures' workers): the key as it stands, nothing else touched */
int tmdb_call_any(const char *path, char *url, size_t ucap, char *hdr, size_t hcap) { if (!key[0] || !apiBase[0]) return 0; call_url(path, key, url, ucap, hdr, hcap); return 1; }
int tmdb_call(const char *path, char *url, size_t ucap, char *hdr, size_t hcap) { drain(); if (!key[0] || !apiBase[0]) return 0; call_url(path, key, url, ucap, hdr, hcap); return 1; }
void tmdb_key_clear(void) { remove(keyPath); snprintf(key, sizeof key, "%s", builtin); resetReq = 1; }
int tmdb_builtin(void) { if (!inited) tmdb_init(); return builtin[0] != 0; }
unsigned tmdb_generation(void) { drain(); return gen; }
int tmdb_wanted(const char *id) { drain(); return key[0] && strcmp(i18n_lang(), "en") && is_imdb(id); }
static Ent *ask(const char *type, const char *id, int light) {
  if (!tmdb_wanted(id)) return NULL;
  Ent *e = find(id, 1); if (!e) return NULL;
  e->lastUse = app_frame();
  int full = !light && !strcmp(type, "series");
  if (e->state == S_NONE || (e->state == S_ERR && app_now() >= e->retryAt)) { e->state = S_PENDING; e->light = (unsigned char)light; e->fullAsked = !light; enqueue(type, id, light, 0); }
  else if (full && !e->fullAsked && ((e->state == S_PENDING && e->light) || (e->state == S_READY && e->v && e->v->pub.light && app_now() >= e->retryAt))) { e->fullAsked = 1; enqueue(type, id, 0, 0); } /* a row's lookup has no episodes */
  return e;
}
int tmdb_state(const char *type, const char *id, int light) {
  Ent *e = ask(type, id, light); if (!e) return TMDB_OFF;
  if (e->state == S_PENDING) return TMDB_PENDING;
  if (e->fullAsked && !light && !strcmp(type, "series")) return TMDB_PENDING; /* the page waits for the episodes */
  if (e->state == S_ERR) return TMDB_NONE;
  return e->v ? TMDB_READY : TMDB_NONE;
}
const TmdbLoc *tmdb_loc(const char *type, const char *id, int light) { Ent *e = ask(type, id, light); return e && e->v ? &e->v->pub : NULL; }
void tmdb_first(const char *type, const char *id, int light) { if (ask(type, id, light)) to_front(id); }
int tmdb_wait(TmdbWait *w, const char *type, const char *id, int light, float maxSec) {
  if (strcmp(w->id, id) || w->light != light) { snprintf(w->id, sizeof w->id, "%s", id); w->light = light; w->at = app_now(); tmdb_first(type, id, light); }
  if (tmdb_state(type, id, light) != TMDB_PENDING) return 1;
  return app_now() - w->at >= maxSec;
}
int tmdb_episode(const TmdbLoc *L, int season, int episode, const char **name, const char **overview) {
  if (name) *name = ""; if (overview) *overview = "";
  if (!L || !L->neps) return 0;
  TmdbEp k = { season, episode, NULL, NULL }; const TmdbEp *e = bsearch(&k, L->eps, (size_t)L->neps, sizeof k, ep_cmp);
  if (!e) return 0;
  if (name) *name = e->name; if (overview) *overview = e->overview; return 1;
}
static void set_str(cJSON *o, const char *k, const char *v) { cJSON *x = cJSON_GetObjectItemCaseSensitive(o, k); if (cJSON_IsString(x)) cJSON_SetValuestring(x, v); else { cJSON_DeleteItemFromObjectCaseSensitive(o, k); cJSON_AddStringToObject(o, k, v); } }
void tmdb_apply_meta(cJSON *m, const TmdbLoc *L) {
  if (!m || !L) return;
  const char *ul = i18n_lang(); if (!strcmp(js_str(m, "_loc", ""), ul)) return;
  if (!L->light) set_str(m, "_loc", ul); /* a row's lookup (no episodes): the page's still applies later */
  if (*L->name && strcmp(L->name, js_str(m, "name", ""))) set_str(m, "locName", L->name);
  if (*L->overview) set_str(m, "description", L->overview);
  if (L->ngenres) { cJSON *g = cJSON_CreateArray(); for (int i = 0; i < L->ngenres; i++) cJSON_AddItemToArray(g, cJSON_CreateString(L->genres[i]));
    if (!cJSON_GetObjectItemCaseSensitive(m, "_genresEn")) { cJSON *en = cJSON_DetachItemFromObjectCaseSensitive(m, "genres"); cJSON_AddItemToObject(m, "_genresEn", en ? en : cJSON_CreateArray()); } /* lookups (Similar) keep the English ones */
    else cJSON_DeleteItemFromObjectCaseSensitive(m, "genres");
    cJSON_AddItemToObject(m, "genres", g); }
  if (*L->logo) set_str(m, "locLogo", L->logo);
  if (*L->poster) set_str(m, "locPoster", L->poster);
  cJSON *v; cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(m, "videos")) {
    const char *nm, *ov; if (!tmdb_episode(L, (int)js_numstr(v, "season", -1), (int)js_numstr(v, "episode", -1), &nm, &ov)) continue;
    int hasTitle = cJSON_GetObjectItemCaseSensitive(v, "title") != NULL, hasDesc = cJSON_GetObjectItemCaseSensitive(v, "description") != NULL;
    if (*nm) { if (hasTitle) set_str(v, "title", nm); if (cJSON_GetObjectItemCaseSensitive(v, "name") || !hasTitle) set_str(v, "name", nm); }
    if (*ov) { if (hasDesc) set_str(v, "description", ov); if (cJSON_GetObjectItemCaseSensitive(v, "overview") || !hasDesc) set_str(v, "overview", ov); }
  }
}
unsigned tmdb_request(const char *path, NetCb cb, void *ud) {
  drain(); if (!key[0]) return 0;
  char url[2048], hdr[480]; call_url(path, key, url, sizeof url, hdr, sizeof hdr);
  return net_get_hdr(url, hdr[0] ? hdr : NULL, cb, ud);
}
/* a title's original language (TMDB's original_language, "ja", "es"...), for "Original" audio: asked once, kept in the
   store (origLangs: "tt123:ja tt456:es", the last 300; "-" for one TMDB doesn't know). NULL while it is asked */
static struct { char id[24]; int busy; } OL[8];
static int ol_get(const char *id, char *out, size_t cap) { const char *l = store_get("origLangs", ""); size_t n = strlen(id);
  for (const char *p = l; (p = strstr(p, id)); p += n) if ((p == l || p[-1] == ' ') && p[n] == ':') { const char *v = p + n + 1; size_t k = strcspn(v, " "); if (k >= cap) k = cap - 1; memcpy(out, v, k); out[k] = 0; return 1; }
  return 0; }
static void ol_put(const char *id, const char *lang) { char cur[8]; if (ol_get(id, cur, sizeof cur)) return; const char *l = store_get("origLangs", ""); static char b[8192];
  snprintf(b, sizeof b, "%s%s%s:%s", l, *l ? " " : "", id, *lang ? lang : "-"); int words = 1; for (const char *p = b; *p; p++) words += *p == ' ';
  char *q = b; while (words > 300 && (q = strchr(q, ' '))) { q++; words--; } store_set("origLangs", q ? q : b); }
static void on_orig(int st, const unsigned char *b, size_t n, void *ud) { (void)n; int k = (int)(intptr_t)ud; cJSON *j = st == 200 ? net_json(b) : NULL;
  if (k >= 0 && k < 8 && OL[k].busy) { OL[k].busy = 0;
    if (j) { const cJSON *a = cJSON_GetObjectItemCaseSensitive(j, "movie_results"), *r = cJSON_GetArraySize(a) ? cJSON_GetArrayItem(a, 0) : NULL;
      if (!r) { a = cJSON_GetObjectItemCaseSensitive(j, "tv_results"); r = cJSON_GetArraySize(a) ? cJSON_GetArrayItem(a, 0) : NULL; }
      const char *lg = r ? js_str(r, "original_language", "") : ""; ol_put(OL[k].id, lg); fprintf(stderr, "[tmdb] %s: original language %s\n", OL[k].id, *lg ? lg : "unknown"); }
    else fprintf(stderr, "[tmdb] %s: original language not had (%d)\n", OL[k].id, st); } /* (not kept: asked again next time) */
  net_json_free(j); }
const char *tmdb_orig_lang(const char *imdbId) {
  static char v[8]; if (!imdbId || strncmp(imdbId, "tt", 2)) return ""; char id[24]; snprintf(id, sizeof id, "%.*s", (int)strcspn(imdbId, ":"), imdbId);
  if (ol_get(id, v, sizeof v)) return strcmp(v, "-") ? v : "";
  for (int i = 0; i < 8; i++) if (OL[i].busy && !strcmp(OL[i].id, id)) return NULL;
  int k = -1; for (int i = 0; i < 8; i++) if (!OL[i].busy) { k = i; break; } if (k < 0) return NULL;
  char p[96]; snprintf(p, sizeof p, "/find/%s?external_source=imdb_id", id); snprintf(OL[k].id, sizeof OL[k].id, "%s", id); OL[k].busy = 1;
  if (!tmdb_request(p, on_orig, (void *)(intptr_t)k)) { OL[k].busy = 0; return ""; } /* (no key: nothing to know it from) */
  return NULL; }
