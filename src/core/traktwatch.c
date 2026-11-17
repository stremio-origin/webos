/* What the connected Trakt account has watched (see traktwatch.h) */
#define _DEFAULT_SOURCE /* (timegm) */
#include "core/traktwatch.h"
#include "core/scrobble.h"
#include "core/store.h"
#include "core/net.h"
#include "core/json.h"
extern void plat_thread_background(int nice); /* (platform/plat.h: kept out, the test builds this alone) */
#include "cJSON.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern int player_open(void);
extern double app_now(void);

#define CHECK_EVERY 900.0   /* Trakt's last activities: every 15 minutes */
#define AFTER_FILM  5.0     /* and a few seconds after a film is closed (what was just watched) */
#define MAX_BODY    (12L << 20) /* an answer bigger than this is left (a TV's memory first) */

typedef struct { char id[16]; long long at; } TwM;                                   /* a film: when last watched */
typedef struct { char id[16]; long long at; int n; unsigned *se; long long *eat; } TwS; /* a show: its episodes (season << 16 | number, sorted) and when */
typedef struct { TwM *m; int nm; TwS *s; int ns; char am[40], ae[40]; } TwData;          /* am / ae: Trakt's last activity for films / episodes */
static TwData *D;             /* what shows (main thread only; never changed once made, replaced whole) */
static unsigned ver = 1, gen = 1;  /* gen: the connection's; an answer for an earlier one is dropped */
static struct { int loaded, busy, needM, needE, needP, wasOpen, writes; char pa[80]; double nextAt; char am[40], ae[40]; TwM *m; int nm; TwS *s; int ns; int haveM, haveE; } W;

static void data_free(TwData *d) { if (!d) return; free(d->m); for (int i = 0; i < d->ns; i++) { free(d->s[i].se); free(d->s[i].eat); } free(d->s); free(d); }
static long long iso_t(const char *s) { struct tm t; memset(&t, 0, sizeof t);
  if (!s || sscanf(s, "%d-%d-%dT%d:%d:%d", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min, &t.tm_sec) != 6) return 0;
  t.tm_year -= 1900; t.tm_mon -= 1; return (long long)timegm(&t); }
static int cmp_m(const void *a, const void *b) { return strcmp(((const TwM *)a)->id, ((const TwM *)b)->id); }
static int cmp_s(const void *a, const void *b) { return strcmp(((const TwS *)a)->id, ((const TwS *)b)->id); }
static int is_tt(const char *id) { return id && id[0] == 't' && id[1] == 't' && strlen(id) < 16; }

/* ---- reading: Trakt's answers and the disk copy, on a thread of their own ---- */
/* /sync/watched/movies: [{ last_watched_at, movie: { ids: { imdb } } }] */
static TwM *build_movies(const cJSON *j, int *n) {
  int cap = cJSON_GetArraySize(j); TwM *m = calloc(cap ? cap : 1, sizeof *m); *n = 0; if (!m) return NULL; const cJSON *e;
  cJSON_ArrayForEach(e, j) { const char *id = js_str(js_obj(js_obj(e, "movie"), "ids"), "imdb", ""); if (!is_tt(id)) continue;
    snprintf(m[*n].id, sizeof m[*n].id, "%s", id); m[*n].at = iso_t(js_str(e, "last_watched_at", "")); (*n)++; }
  qsort(m, *n, sizeof *m, cmp_m); return m; }
static int cmp_u(const void *a, const void *b) { unsigned x = *(const unsigned *)a, y = *(const unsigned *)b; return x < y ? -1 : x > y; }
/* /sync/watched/shows: [{ last_watched_at, show: { ids: { imdb } }, seasons: [{ number, episodes: [{ number, last_watched_at }] }] }] */
static TwS *build_shows(const cJSON *j, int *n) {
  int cap = cJSON_GetArraySize(j); TwS *s = calloc(cap ? cap : 1, sizeof *s); *n = 0; if (!s) return NULL; const cJSON *e;
  cJSON_ArrayForEach(e, j) { const char *id = js_str(js_obj(js_obj(e, "show"), "ids"), "imdb", ""); if (!is_tt(id)) continue;
    int ne = 0; const cJSON *se, *ep; cJSON_ArrayForEach(se, js_arr(e, "seasons")) ne += cJSON_GetArraySize(js_arr(se, "episodes"));
    TwS *o = &s[*n]; snprintf(o->id, sizeof o->id, "%s", id); o->at = iso_t(js_str(e, "last_watched_at", ""));
    o->se = calloc(ne ? ne : 1, sizeof *o->se); o->eat = calloc(ne ? ne : 1, sizeof *o->eat); if (!o->se || !o->eat) { free(o->se); free(o->eat); continue; }
    /* (sorted by season and number with their times: an insertion keeps both arrays in step) */
    cJSON_ArrayForEach(se, js_arr(e, "seasons")) { int sn = (int)js_num(se, "number", -1); if (sn < 0 || sn > 0xffff) continue;
      cJSON_ArrayForEach(ep, js_arr(se, "episodes")) { int en = (int)js_num(ep, "number", -1); if (en < 0 || en > 0xffff || o->n >= ne) continue;
        unsigned k = (unsigned)sn << 16 | (unsigned)en; long long at = iso_t(js_str(ep, "last_watched_at", "")); int i = o->n++;
        while (i > 0 && o->se[i - 1] > k) { o->se[i] = o->se[i - 1]; o->eat[i] = o->eat[i - 1]; i--; } o->se[i] = k; o->eat[i] = at; } }
    (*n)++; }
  qsort(s, *n, sizeof *s, cmp_s); return s; }
/* the disk copy: {"am","ae","m":[[id,at]...],"s":[[id,at,[s,e,at,...]]...]} */
static TwData *from_disk(const cJSON *j) {
  TwData *d = calloc(1, sizeof *d); if (!d) return NULL; snprintf(d->am, sizeof d->am, "%s", js_str(j, "am", "")); snprintf(d->ae, sizeof d->ae, "%s", js_str(j, "ae", ""));
  const cJSON *ma = js_arr(j, "m"), *sa = js_arr(j, "s"), *e; int cm = cJSON_GetArraySize(ma), cs = cJSON_GetArraySize(sa);
  d->m = calloc(cm ? cm : 1, sizeof *d->m); d->s = calloc(cs ? cs : 1, sizeof *d->s); if (!d->m || !d->s) { data_free(d); return NULL; }
  cJSON_ArrayForEach(e, ma) { const cJSON *id = cJSON_GetArrayItem(e, 0); if (!cJSON_IsString(id) || !is_tt(id->valuestring)) continue;
    const cJSON *at = cJSON_GetArrayItem(e, 1); snprintf(d->m[d->nm].id, sizeof d->m[d->nm].id, "%s", id->valuestring); d->m[d->nm].at = cJSON_IsNumber(at) ? (long long)at->valuedouble : 0; d->nm++; }
  cJSON_ArrayForEach(e, sa) { const cJSON *id = cJSON_GetArrayItem(e, 0), *eps = cJSON_GetArrayItem(e, 2); if (!cJSON_IsString(id) || !is_tt(id->valuestring) || !cJSON_IsArray(eps)) continue;
    TwS *o = &d->s[d->ns]; int ne = cJSON_GetArraySize(eps) / 3; snprintf(o->id, sizeof o->id, "%s", id->valuestring); const cJSON *at = cJSON_GetArrayItem(e, 1); o->at = cJSON_IsNumber(at) ? (long long)at->valuedouble : 0;
    o->se = calloc(ne ? ne : 1, sizeof *o->se); o->eat = calloc(ne ? ne : 1, sizeof *o->eat); if (!o->se || !o->eat) { free(o->se); free(o->eat); continue; }
    const cJSON *x = eps->child; for (int i = 0; i < ne && x && x->next && x->next->next; i++) { o->se[i] = (unsigned)x->valuedouble << 16 | (unsigned)x->next->valuedouble; o->eat[i] = (long long)x->next->next->valuedouble; x = x->next->next->next; o->n++; }
    d->ns++; }
  qsort(d->m, d->nm, sizeof *d->m, cmp_m); qsort(d->s, d->ns, sizeof *d->s, cmp_s); /* (kept sorted, sorted again in case) */
  return d; }
static char *to_disk(const TwData *d) {
  cJSON *o = cJSON_CreateObject(), *ma = NULL, *sa = NULL; cJSON_AddStringToObject(o, "am", d->am); cJSON_AddStringToObject(o, "ae", d->ae);
  ma = cJSON_AddArrayToObject(o, "m"); sa = cJSON_AddArrayToObject(o, "s");
  for (int i = 0; i < d->nm; i++) { cJSON *e = cJSON_CreateArray(); cJSON_AddItemToArray(e, cJSON_CreateString(d->m[i].id)); cJSON_AddItemToArray(e, cJSON_CreateNumber((double)d->m[i].at)); cJSON_AddItemToArray(ma, e); }
  for (int i = 0; i < d->ns; i++) { const TwS *s = &d->s[i]; cJSON *e = cJSON_CreateArray(), *eps = cJSON_CreateArray(); cJSON_AddItemToArray(e, cJSON_CreateString(s->id)); cJSON_AddItemToArray(e, cJSON_CreateNumber((double)s->at));
    for (int k = 0; k < s->n; k++) { cJSON_AddItemToArray(eps, cJSON_CreateNumber(s->se[k] >> 16)); cJSON_AddItemToArray(eps, cJSON_CreateNumber(s->se[k] & 0xffff)); cJSON_AddItemToArray(eps, cJSON_CreateNumber((double)s->eat[k])); }
    cJSON_AddItemToArray(e, eps); cJSON_AddItemToArray(sa, e); }
  char *t = cJSON_PrintUnformatted(o); cJSON_Delete(o); return t; }
static void disk_path(char *p, size_t cap) { snprintf(p, cap, "%s/trakt-watched.json", store_data_dir()); }

typedef struct { int kind, gen; char *body; char path[1200]; TwData *d; TwM *m; int nm; TwS *s; int ns; } Job; /* kind: 0 the disk copy, 1 films, 2 shows, 3 a write */
static void job_done(int st, const unsigned char *b, size_t n, void *ud);
static void *job_run(void *a) { Job *k = a; plat_thread_background(5);
  if (k->kind == 3) { char tmp[1300]; snprintf(tmp, sizeof tmp, "%s.tmp", k->path); FILE *f = fopen(tmp, "wb"); size_t len = strlen(k->body);
    if (f) { size_t w = fwrite(k->body, 1, len, f); fclose(f); if (w == len) rename(tmp, k->path); else remove(tmp); } free(k->body); k->body = NULL; net_file("", job_done, k); return NULL; } /* (whole or not at all; then told: written for a connection gone meanwhile, it goes) */
  if (k->kind == 0) { FILE *f = fopen(k->path, "rb"); if (f) { fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
      if (n > 0 && n < MAX_BODY) { char *t = malloc((size_t)n + 1); if (t && fread(t, 1, (size_t)n, f) == (size_t)n) { t[n] = 0; cJSON *j = cJSON_Parse(t); if (j) k->d = from_disk(j); cJSON_Delete(j); } free(t); } fclose(f); } }
  else { cJSON *j = cJSON_Parse(k->body); free(k->body); k->body = NULL;
    if (cJSON_IsArray(j)) { if (k->kind == 1) k->m = build_movies(j, &k->nm); else k->s = build_shows(j, &k->ns); } cJSON_Delete(j); }
  net_file("", job_done, k); return NULL; } /* (handed back on the drawing thread's next frame) */
static void job_start(Job *k) { pthread_t t; if (pthread_create(&t, NULL, job_run, k) == 0) { pthread_detach(t); return; } job_run(k); }

/* ---- the checks ---- */
static void finish(int ok) { /* the new picture: what was asked again, what was not kept */
  if (ok && (W.haveM || W.haveE)) { TwData *d = calloc(1, sizeof *d);
    if (d) { snprintf(d->am, sizeof d->am, "%s", W.haveM ? W.am : D ? D->am : ""); snprintf(d->ae, sizeof d->ae, "%s", W.haveE ? W.ae : D ? D->ae : ""); /* (a part not read again keeps its own: never marked as read) */
      if (W.haveM) { d->m = W.m; d->nm = W.nm; W.m = NULL; } else if (D) { d->m = D->m; d->nm = D->nm; D->m = NULL; D->nm = 0; }
      if (W.haveE) { d->s = W.s; d->ns = W.ns; W.s = NULL; } else if (D) { d->s = D->s; d->ns = D->ns; D->s = NULL; D->ns = 0; }
      if (!d->m) d->m = calloc(1, sizeof *d->m); if (!d->s) d->s = calloc(1, sizeof *d->s);
      data_free(D); D = d; ver++; fprintf(stderr, "[trakt] watched: %d films, %d shows%s%s\n", d->nm, d->ns, W.haveM ? " (films asked again)" : "", W.haveE ? " (episodes asked again)" : "");
      Job *k = calloc(1, sizeof *k); if (k) { k->kind = 3; k->gen = (int)gen; disk_path(k->path, sizeof k->path); k->body = to_disk(D); if (k->body) { W.writes++; job_start(k); } else free(k); } } }
  free(W.m); W.m = NULL; if (W.s) { for (int i = 0; i < W.ns; i++) { free(W.s[i].se); free(W.s[i].eat); } free(W.s); W.s = NULL; }
  W.haveM = W.haveE = 0; W.busy = 0; }
static void on_list(int st, const unsigned char *b, size_t n, void *ud);
/* ---- paused positions: a short list, read here (never kept on disk: asked at the start and when Trakt's pauses move) ---- */
static TwPlay *PB; static int npb; static char pbActs[80];
static int dropped(long long pid) { const cJSON *d = store_node("twPbDrop"), *e; if (cJSON_IsArray(d)) cJSON_ArrayForEach(e, d) if (cJSON_IsNumber(e) && (long long)e->valuedouble == pid) return 1; return 0; }
static int cmp_play(const void *a, const void *b) { long long x = ((const TwPlay *)a)->at, y = ((const TwPlay *)b)->at; return x < y ? 1 : x > y ? -1 : 0; }
static void ask_next(void);
/* /sync/playback: [{ id, progress (%), paused_at, type: movie|episode, movie: { title, ids }, episode: { season, number }, show: { title, ids } }] */
static void on_playback(int st, const unsigned char *b, size_t n, void *ud) {
  if ((unsigned)(uintptr_t)ud != gen) return; cJSON *j = st == 200 && (long)n < MAX_BODY ? net_json(b) : NULL;
  if (cJSON_IsArray(j)) { int cap = cJSON_GetArraySize(j), k = 0; TwPlay *p = calloc(cap ? cap : 1, sizeof *p); const cJSON *e;
    if (p) { cJSON_ArrayForEach(e, j) { long long pid = (long long)js_num(e, "id", 0); double pr = js_num(e, "progress", 0); if (pr <= 0 || pr >= 100 || dropped(pid)) continue;
        int ep = !strcmp(js_str(e, "type", ""), "episode"); const cJSON *m = js_obj(e, ep ? "show" : "movie"); const char *id = js_str(js_obj(m, "ids"), "imdb", ""); if (!is_tt(id)) continue;
        TwPlay *o = &p[k]; o->pid = pid; snprintf(o->id, sizeof o->id, "%s", id); snprintf(o->title, sizeof o->title, "%s", js_str(m, "title", "")); o->pct = (float)(pr / 100); o->at = iso_t(js_str(e, "paused_at", ""));
        if (ep) { const cJSON *x = js_obj(e, "episode"); int sn = (int)js_num(x, "season", -1), en = (int)js_num(x, "number", -1); if (sn < 0 || en < 0) continue; snprintf(o->vid, sizeof o->vid, "%s:%d:%d", id, sn, en); snprintf(o->type, sizeof o->type, "series"); }
        else { snprintf(o->vid, sizeof o->vid, "%s", id); snprintf(o->type, sizeof o->type, "movie"); }
        k++; }
      qsort(p, k, sizeof *p, cmp_play); free(PB); PB = p; npb = k; snprintf(pbActs, sizeof pbActs, "%s", W.pa); ver++; fprintf(stderr, "[trakt] %d paused position(s)\n", k); } }
  else if (st) fprintf(stderr, "[trakt] paused positions not read (%d)\n", st);
  net_json_free(j); ask_next(); }
static void ask_next(void) {
  if (W.needM && !W.haveM) { W.needM = 0; if (scrobble_get("/sync/watched/movies", on_list, (void *)(uintptr_t)(gen << 1 | 0))) return; finish(0); return; }
  if (W.needE && !W.haveE) { W.needE = 0; if (scrobble_get("/sync/watched/shows", on_list, (void *)(uintptr_t)(gen << 1 | 1))) return; finish(0); return; }
  if (W.needP) { W.needP = 0; if (scrobble_get("/sync/playback", on_playback, (void *)(uintptr_t)gen)) return; }
  finish(1); }
static void on_list(int st, const unsigned char *b, size_t n, void *ud) {
  unsigned g = (unsigned)(uintptr_t)ud >> 1; int shows = (int)((uintptr_t)ud & 1); if (g != gen) return; /* (another connection meanwhile) */
  if (st != 200 || !b || (long)n > MAX_BODY) { fprintf(stderr, "[trakt] watched %s not read (%d, %ld bytes)\n", shows ? "shows" : "films", st, (long)n); finish(W.haveM || W.haveE); return; }
  Job *k = calloc(1, sizeof *k); if (!k || !(k->body = malloc(n + 1))) { free(k); finish(0); return; } memcpy(k->body, b, n); k->body[n] = 0; k->kind = shows ? 2 : 1; k->gen = (int)g; job_start(k); }
static void job_done(int st, const unsigned char *b, size_t n, void *ud) { (void)st; (void)b; (void)n; Job *k = ud;
  if (k->kind == 3) { W.writes--; if ((unsigned)k->gen != gen) remove(k->path); free(k); return; } /* (another account's must never be read back) */
  if ((unsigned)k->gen != gen) { if (k->d) data_free(k->d); free(k->m); if (k->s) { for (int i = 0; i < k->ns; i++) { free(k->s[i].se); free(k->s[i].eat); } free(k->s); } free(k); return; }
  if (k->kind == 0) { if (k->d && !D) { D = k->d; ver++; fprintf(stderr, "[trakt] watched, kept: %d films, %d shows\n", D->nm, D->ns); } else data_free(k->d); free(k); return; }
  if (k->kind == 1) { if (k->m) { W.m = k->m; W.nm = k->nm; W.haveM = 1; } }
  else if (k->s) { W.s = k->s; W.ns = k->ns; W.haveE = 1; }
  free(k); ask_next(); }
static void on_acts(int st, const unsigned char *b, size_t n, void *ud) {
  (void)n; if ((unsigned)(uintptr_t)ud != gen) return; cJSON *j = st == 200 ? net_json(b) : NULL;
  if (!j) { net_json_free(j); W.busy = 0; if (st) fprintf(stderr, "[trakt] last activities not read (%d)\n", st); return; }
  snprintf(W.am, sizeof W.am, "%s", js_str(js_obj(j, "movies"), "watched_at", "")); snprintf(W.ae, sizeof W.ae, "%s", js_str(js_obj(j, "episodes"), "watched_at", ""));
  snprintf(W.pa, sizeof W.pa, "%s|%s", js_str(js_obj(j, "movies"), "paused_at", ""), js_str(js_obj(j, "episodes"), "paused_at", "")); net_json_free(j);
  int marks = tw_on(); W.needM = marks && (!D || strcmp(W.am, D->am)); W.needE = marks && (!D || strcmp(W.ae, D->ae)); W.needP = tw_cw_on() && strcmp(W.pa, pbActs); W.haveM = W.haveE = 0;
  if (!W.needM && !W.needE && !W.needP) { W.busy = 0; return; } /* (nothing new: nothing asked) */
  ask_next(); }

int tw_on(void) { return scrobble_state() == TRAKT_LINKED && strcmp(store_get("traktMarks", "true"), "false") != 0; }
int tw_cw_on(void) { return scrobble_state() == TRAKT_LINKED && strcmp(store_get("traktCw", "true"), "false") != 0; }
unsigned tw_ver(void) { return ver; }
void tw_tick(void) {
  if (scrobble_state() != TRAKT_LINKED) return;
  double now = app_now();
  if (!W.loaded && !W.writes) { W.loaded = 1; /* (never while a write is out: one for a connection gone could be read back) */ W.nextAt = now + 5; Job *k = calloc(1, sizeof *k); if (k) { k->kind = 0; k->gen = (int)gen; disk_path(k->path, sizeof k->path); job_start(k); } } /* the disk copy first, Trakt a moment after the start */
  int open = player_open(); if (W.wasOpen && !open) W.nextAt = now + AFTER_FILM; W.wasOpen = open; /* (closed: what was just watched, a moment later) */
  if ((!tw_on() && !tw_cw_on()) || W.busy || now < W.nextAt || open) return; /* (never while a film plays) */
  W.nextAt = now + CHECK_EVERY; if (scrobble_get("/sync/last_activities", on_acts, (void *)(uintptr_t)gen)) W.busy = 1; }

/* ---- what shows ---- */
static const TwM *find_m(const char *id) { if (!D || !is_tt(id)) return NULL; TwM k; memset(&k, 0, sizeof k); snprintf(k.id, sizeof k.id, "%s", id); return bsearch(&k, D->m, D->nm, sizeof *D->m, cmp_m); }
static const TwS *find_s(const char *id) { if (!D || !is_tt(id)) return NULL; TwS k; memset(&k, 0, sizeof k); snprintf(k.id, sizeof k.id, "%s", id); return bsearch(&k, D->s, D->ns, sizeof *D->s, cmp_s); }
static long long ep_at(const TwS *s, int season, int ep) { if (!s || season < 0 || ep < 0) return -1; unsigned k = (unsigned)season << 16 | (unsigned)ep; const unsigned *p = bsearch(&k, s->se, s->n, sizeof *s->se, cmp_u); return p ? s->eat[p - s->se] : -1; }
/* hidden here by hand: the time it was hidden (0 none); a newer play on Trakt shows it again */
static long long hidden_at(const char *key) { const cJSON *t = store_node("twHide"), *h = cJSON_IsObject(t) ? cJSON_GetObjectItemCaseSensitive(t, key) : NULL; return cJSON_IsNumber(h) ? (long long)h->valuedouble : 0; }
static void hide_set(const char *key, int on) {
  cJSON *h = store_node("twHide"); cJSON *c = cJSON_IsObject(h) ? cJSON_Duplicate(h, 1) : cJSON_CreateObject(); if (!c) return;
  cJSON_DeleteItemFromObjectCaseSensitive(c, key); if (on) cJSON_AddNumberToObject(c, key, (double)time(NULL));
  if (cJSON_GetArraySize(c) > 2000) cJSON_DeleteItemFromArray(c, 0); /* (the oldest go: a hidden mark is a small thing) */
  store_set_node("twHide", c); ver++; }
int tw_movie(const char *id) { if (!tw_on()) return 0; const TwM *m = find_m(id); if (!m) return 0; long long h = hidden_at(id); return !h || m->at > h; }
int tw_show_known(const char *id) { return tw_on() && find_s(id) != NULL; }
int tw_ep(const char *showId, int season, int episode) {
  if (!tw_on()) return 0; long long at = ep_at(find_s(showId), season, episode); if (at < 0) return 0;
  char key[48]; snprintf(key, sizeof key, "%s:%d:%d", showId, season, episode); long long h = hidden_at(key); return !h || at > h; }

/* ---- by hand ---- */
static void on_added(int st, const unsigned char *b, size_t n, void *ud) { (void)b; (void)n; (void)ud;
  if (st == 201 || st == 200) { fprintf(stderr, "[trakt] added to the history by hand\n"); if (W.nextAt > app_now() + AFTER_FILM) W.nextAt = app_now() + AFTER_FILM; } /* (read back soon: the marks then come from Trakt too) */
  else fprintf(stderr, "[trakt] the history not added to (%d)%s\n", st, st == 420 ? ": the account's limit" : ""); }
static int may_add(void) { return tw_on() && scrobble_on(); } /* (tracking switched off: nothing told to Trakt) */
void tw_mark_movie(const char *id, int on) {
  if (!tw_on() || !is_tt(id)) return;
  if (!on) { if (find_m(id)) hide_set(id, 1); return; } /* never removed from Trakt: hidden here */
  if (hidden_at(id)) hide_set(id, 0);
  if (!may_add() || find_m(id)) return; /* (Trakt has it: a second play would count it twice) */
  cJSON *o = cJSON_CreateObject(), *ms = cJSON_AddArrayToObject(o, "movies"), *m = cJSON_CreateObject(), *ids = cJSON_AddObjectToObject(m, "ids");
  cJSON_AddStringToObject(ids, "imdb", id); cJSON_AddItemToArray(ms, m); scrobble_post("/sync/history", o, on_added, NULL); }
void tw_mark_eps(const char *showId, const int *se, int n, int on) {
  if (!tw_on() || !is_tt(showId) || n <= 0) return; const TwS *s = find_s(showId); char key[48];
  if (!on) { for (int i = 0; i < n; i++) if (ep_at(s, se[2 * i], se[2 * i + 1]) >= 0) { snprintf(key, sizeof key, "%s:%d:%d", showId, se[2 * i], se[2 * i + 1]); hide_set(key, 1); } return; }
  for (int i = 0; i < n; i++) { snprintf(key, sizeof key, "%s:%d:%d", showId, se[2 * i], se[2 * i + 1]); if (hidden_at(key)) hide_set(key, 0); }
  if (!may_add()) return;
  cJSON *o = cJSON_CreateObject(), *shows = cJSON_AddArrayToObject(o, "shows"), *sh = cJSON_CreateObject(), *ids = cJSON_AddObjectToObject(sh, "ids"), *seasons = cJSON_AddArrayToObject(sh, "seasons"); int added = 0;
  cJSON_AddStringToObject(ids, "imdb", showId);
  for (int i = 0; i < n; i++) { int sn = se[2 * i], en = se[2 * i + 1]; if (sn < 0 || en < 0 || ep_at(s, sn, en) >= 0) continue; /* (Trakt has it) */
    cJSON *sea = NULL, *x; cJSON_ArrayForEach(x, seasons) if ((int)js_num(x, "number", -1) == sn) { sea = x; break; }
    if (!sea) { sea = cJSON_CreateObject(); cJSON_AddNumberToObject(sea, "number", sn); cJSON_AddArrayToObject(sea, "episodes"); cJSON_AddItemToArray(seasons, sea); }
    cJSON *e = cJSON_CreateObject(); cJSON_AddNumberToObject(e, "number", en); cJSON_AddItemToArray(cJSON_GetObjectItemCaseSensitive(sea, "episodes"), e); added++; }
  cJSON_AddItemToArray(shows, sh); if (!added) { cJSON_Delete(o); return; }
  scrobble_post("/sync/history", o, on_added, NULL); }
int tw_plays(const TwPlay **out) { *out = PB; return tw_cw_on() ? npb : 0; }
const TwPlay *tw_play_of(const char *id) { if (!tw_cw_on() || !id) return NULL; for (int i = 0; i < npb; i++) if (!strcmp(PB[i].id, id)) return &PB[i]; return NULL; } /* (newest first: the first is the newest) */
static void on_play_deleted(int st, const unsigned char *b, size_t n, void *ud) { (void)b; (void)n; (void)ud; fprintf(stderr, "[trakt] a paused position %s (%d)\n", st == 204 || st == 200 ? "deleted" : "not deleted, hidden here", st); }
void tw_play_drop(const char *id) {
  if (!id || !npb) return; cJSON *d = store_node("twPbDrop"); cJSON *c = cJSON_IsArray(d) ? cJSON_Duplicate(d, 1) : cJSON_CreateArray(); int k = 0;
  for (int i = 0; i < npb; i++) { if (strcmp(PB[i].id, id)) { PB[k++] = PB[i]; continue; }
    char path[64]; snprintf(path, sizeof path, "/sync/playback/%lld", PB[i].pid); if (scrobble_on()) scrobble_delete(path, on_play_deleted, NULL); /* (Trakt's own: gone for every app; with tracking off nothing is told to Trakt, it is only hidden here) */
    if (c) cJSON_AddItemToArray(c, cJSON_CreateNumber((double)PB[i].pid)); } /* (and never shown here again, whatever Trakt answers) */
  if (c) { while (cJSON_GetArraySize(c) > 200) cJSON_DeleteItemFromArray(c, 0); store_set_node("twPbDrop", c); }
  if (k != npb) { npb = k; ver++; } }
void tw_forget(void) {
  gen++; data_free(D); D = NULL; finish(0); W.loaded = 0; W.needM = W.needE = W.needP = 0; ver++; /* (writes still out: counted down as they come back) */
  free(PB); PB = NULL; npb = 0; pbActs[0] = 0;
  char p[1200]; disk_path(p, sizeof p); remove(p); store_remove("twHide"); store_remove("twPbDrop"); }
