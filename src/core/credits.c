#include "core/credits.h"
#include "core/tmdb.h"
#include "core/store.h"
#include "core/json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXC 60
#define KEEP 6
#define DISK_AGE (3 * 86400)
typedef struct { char id[24], type[12]; int state; /* 0 asking, 1 known */ Credit c[MAXC]; int n; unsigned use; } Title;
static Title titles[KEEP]; static unsigned useN, gen;
unsigned credits_gen(void) { return gen; }

static Title *find(const char *id) { for (int i = 0; i < KEEP; i++) if (titles[i].id[0] && !strcmp(titles[i].id, id)) return &titles[i]; return NULL; }
static Title *slot(void) { Title *o = &titles[0]; for (int i = 0; i < KEEP; i++) { if (!titles[i].id[0]) return &titles[i]; if (titles[i].use < o->use) o = &titles[i]; } return o; }
static void put(Title *t, const char *name, const char *role, const char *path, int tid, int kind) {
  if (t->n >= MAXC || !name || !*name) return; for (int i = 0; i < t->n; i++) if (t->c[i].tid == tid && t->c[i].kind == kind) return;
  Credit *c = &t->c[t->n++]; memset(c, 0, sizeof *c); snprintf(c->name, sizeof c->name, "%s", name); snprintf(c->role, sizeof c->role, "%s", role ? role : "");
  if (path && *path) snprintf(c->img, sizeof c->img, "https://image.tmdb.org/t/p/w185%s", path); c->tid = tid; c->kind = kind; }
/* a credits answer (a film's credits, a series' aggregate credits) into the title: directors and writers first (a film's), then the cast */
static void take(Title *t, const cJSON *j) {
  t->n = 0; const cJSON *x;
  if (strcmp(t->type, "series")) { int nd = 0, nw = 0;
    cJSON_ArrayForEach(x, js_arr(j, "crew")) { const char *job = js_str(x, "job", ""); int tid = (int)js_num(x, "id", 0);
      if (!strcmp(job, "Director") && nd < 2) { put(t, js_str(x, "name", ""), "", js_str(x, "profile_path", ""), tid, CRED_DIRECTOR); nd++; }
      else if ((!strcmp(job, "Screenplay") || !strcmp(job, "Writer")) && nw < 2) { put(t, js_str(x, "name", ""), "", js_str(x, "profile_path", ""), tid, CRED_WRITER); nw++; } } }
  cJSON_ArrayForEach(x, js_arr(j, "cast")) { const char *role = js_str(x, "character", NULL);
    if (!role) { const cJSON *r = cJSON_GetArrayItem(js_arr(x, "roles"), 0); role = js_str(r, "character", ""); } /* (a series: the first part they play) */
    put(t, js_str(x, "name", ""), role, js_str(x, "profile_path", ""), (int)js_num(x, "id", 0), CRED_CAST); }
}
static void done(Title *t, const unsigned char *body, size_t len, int ok) {
  t->state = 1; if (ok && body) { char k[64]; snprintf(k, sizeof k, "cred1:%s", t->id); cache_write(k, body, len); }
  gen++; fprintf(stderr, "[credits] %d people\n", t->n);
}
typedef struct { int i; char id[24]; } Req; /* a request's title, found again by its id (the slot may have gone to another) */
static Title *req_title(Req *q) { Title *t = &titles[q->i]; return strcmp(t->id, q->id) || t->state ? NULL : t; }
static Req *req_new(Title *t) { Req *q = malloc(sizeof *q); if (q) { q->i = (int)(t - titles); snprintf(q->id, sizeof q->id, "%s", t->id); } return q; }
static void on_credits(int st, const unsigned char *b, size_t n, void *ud) {
  Req *q = ud; Title *t = req_title(q); free(q); if (!t) return; cJSON *j = st == 200 ? net_json(b) : NULL;
  if (j) take(t, j); done(t, b, n, j != NULL); net_json_free(j);
}
static void on_find(int st, const unsigned char *b, size_t n, void *ud) {
  (void)n; Req *q = ud; Title *t = req_title(q); if (!t) { free(q); return; } cJSON *j = st == 200 ? net_json(b) : NULL; int series = !strcmp(t->type, "series");
  const cJSON *r = cJSON_GetArrayItem(js_arr(j, series ? "tv_results" : "movie_results"), 0); if (!r) r = cJSON_GetArrayItem(js_arr(j, series ? "movie_results" : "tv_results"), 0);
  int tid = (int)js_num(r, "id", 0); net_json_free(j);
  if (!tid) { free(q); done(t, NULL, 0, 0); return; }
  char p[96]; snprintf(p, sizeof p, series ? "/tv/%d/aggregate_credits" : "/movie/%d/credits", tid);
  if (!tmdb_request(p, on_credits, q)) { free(q); done(t, NULL, 0, 0); }
}
int credits_get(const char *type, const char *imdbId, const Credit **out) {
  *out = NULL; if (!imdbId || strncmp(imdbId, "tt", 2) || !tmdb_has_key()) return 0;
  Title *t = find(imdbId); if (t) { t->use = ++useN; if (!t->state) return -1; *out = t->c; return t->n; }
  t = slot(); memset(t, 0, sizeof *t); snprintf(t->id, sizeof t->id, "%s", imdbId); snprintf(t->type, sizeof t->type, "%s", type && !strcmp(type, "series") ? "series" : "movie"); t->use = ++useN;
  char k[64]; snprintf(k, sizeof k, "cred1:%s", imdbId); size_t len = 0; unsigned char *d = cache_read(k, DISK_AGE, &len);
  if (d) { cJSON *j = cJSON_ParseWithLength((const char *)d, len); free(d); if (j) { take(t, j); cJSON_Delete(j); t->state = 1; *out = t->c; return t->n; } }
  char p[96]; snprintf(p, sizeof p, "/find/%s?external_source=imdb_id", imdbId);
  Req *q = req_new(t); if (!q || !tmdb_request(p, on_find, q)) { free(q); t->state = 1; return 0; }
  return -1;
}
typedef struct { void (*cb)(const char *, void *); void *ud; } Ask;
static void on_ext(int st, const unsigned char *b, size_t n, void *ud) {
  (void)n; Ask *a = ud; cJSON *j = st == 200 ? net_json(b) : NULL; a->cb(js_str(j, "imdb_id", ""), a->ud); net_json_free(j); free(a); }
void credits_person_imdb(int tid, void (*cb)(const char *, void *), void *ud) {
  Ask *a = malloc(sizeof *a); if (!a) { cb("", ud); return; } a->cb = cb; a->ud = ud; char p[64]; snprintf(p, sizeof p, "/person/%d/external_ids", tid);
  if (!tmdb_request(p, on_ext, a)) { free(a); cb("", ud); }
}
