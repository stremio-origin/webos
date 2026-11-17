#include "core/coll.h"
#include "core/app.h"
#include "core/i18n.h"
#include "core/json.h"
#include "core/net.h"
#include "core/store.h"
#include "core/tmdb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WD "https://query.wikidata.org/sparql?format=json&query="
#define MAP_DAYS 30            /* which collection a title is in, kept this long */
#define NONE_DAYS 14           /* a title in none: asked again after this */
#define COLL_SECS (7 * 86400)  /* a collection's titles */
#define WD_BATCH 40            /* titles asked of Wikidata in one question (each with its links counted) */
#define TM_PAR 4               /* TMDB calls at once */
#define FILM_T "wd:Q11424 wd:Q24869 wd:Q506240 wd:Q93204 wd:Q202866 wd:Q29168811 wd:Q17123180 wd:Q20650540"
#define SER_T "wd:Q5398426 wd:Q1259759 wd:Q526877 wd:Q581714 wd:Q117467246 wd:Q63952888"

static unsigned ver = 1;
unsigned coll_version(void) { return ver; }
static int is_tt(const char *id) { if (!id || strncmp(id, "tt", 2) || !id[2] || strlen(id) > 12) return 0; for (const char *q = id + 2; *q; q++) if (*q < '0' || *q > '9') return 0; return 1; }
static const char *bval(const cJSON *b, const char *k) { return js_str(js_obj(b, k), "value", ""); }
static void sparql_url(const char *q, char *u, size_t cap) {
  size_t o = (size_t)snprintf(u, cap, "%s", WD);
  for (const char *s = q; *s && o + 4 < cap; s++) { unsigned char c = (unsigned char)*s;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') u[o++] = (char)c; else o += (size_t)snprintf(u + o, cap - o, "%%%02X", c); }
  u[o] = 0;
}

/* ---- which collection: a table of titles, kept as one file ---- */
enum { M_NONE = 0, M_HAS = 1, M_TMDB = -1, M_WD = -2, M_RETRY = -3 }; /* asked of TMDB, then of Wikidata; asked again later */
typedef struct { char id[16], type[8], key[24], name[96]; int st, tmdb; long at; } MapE;
static MapE *map; static int nmap, capmap, mapLoaded, mapDirty; static double mapSaveAt;
static int map_find(const char *id) { for (int i = 0; i < nmap; i++) if (!strcmp(map[i].id, id)) return i; return -1; }
static MapE *map_add(const char *id) { int i = map_find(id); if (i >= 0) return &map[i];
  if (nmap == capmap) { int nc = capmap ? capmap * 2 : 256; MapE *m = realloc(map, sizeof *m * (size_t)nc); if (!m) return NULL; map = m; capmap = nc; }
  MapE *e = &map[nmap++]; memset(e, 0, sizeof *e); snprintf(e->id, sizeof e->id, "%s", id); e->st = M_RETRY; return e; }
static void map_dirty(void) { mapDirty = 1; mapSaveAt = app_now() + 3; ver++; }
static void map_load(void) {
  if (mapLoaded) return; mapLoaded = 1; size_t len; unsigned char *b = cache_read("coll3:map", 0, &len); if (!b) return;
  cJSON *j = cJSON_Parse((const char *)b); free(b); const cJSON *e; long now = (long)time(NULL);
  if (cJSON_IsObject(j)) cJSON_ArrayForEach(e, j) { if (!e->string || !cJSON_IsArray(e)) continue;
    const cJSON *k = cJSON_GetArrayItem(e, 0), *nm = cJSON_GetArrayItem(e, 2), *ty = cJSON_GetArrayItem(e, 3); long at = (long)cJSON_GetNumberValue(cJSON_GetArrayItem(e, 1)); int tm = (int)cJSON_GetNumberValue(cJSON_GetArrayItem(e, 4));
    const char *ks = cJSON_IsString(k) ? k->valuestring : ""; if (now - at > (*ks ? MAP_DAYS : NONE_DAYS) * 86400L) continue; /* (old: asked again) */
    MapE *m = map_add(e->string); if (!m) continue; snprintf(m->key, sizeof m->key, "%s", ks); m->st = *ks ? M_HAS : M_NONE; m->at = at; m->tmdb = tm;
    snprintf(m->name, sizeof m->name, "%s", cJSON_IsString(nm) ? nm->valuestring : ""); snprintf(m->type, sizeof m->type, "%s", cJSON_IsString(ty) ? ty->valuestring : ""); }
  cJSON_Delete(j);
}
static void map_save(void) {
  cJSON *j = cJSON_CreateObject();
  for (int i = 0; i < nmap; i++) { const MapE *m = &map[i]; if (m->st != M_HAS && m->st != M_NONE) continue; cJSON *a = cJSON_CreateArray();
    cJSON_AddItemToArray(a, cJSON_CreateString(m->key)); cJSON_AddItemToArray(a, cJSON_CreateNumber((double)m->at)); cJSON_AddItemToArray(a, cJSON_CreateString(m->name)); cJSON_AddItemToArray(a, cJSON_CreateString(m->type)); cJSON_AddItemToArray(a, cJSON_CreateNumber(m->tmdb));
    cJSON_AddItemToObject(j, m->id, a); }
  char *s = cJSON_PrintUnformatted(j); cJSON_Delete(j); if (s) { cache_write("coll3:map", (const unsigned char *)s, strlen(s)); free(s); } mapDirty = 0;
}
static void set_has(MapE *m, const char *key, const char *name) { snprintf(m->key, sizeof m->key, "%s", key); snprintf(m->name, sizeof m->name, "%s", name ? name : ""); m->st = M_HAS; m->at = (long)time(NULL); map_dirty(); }
static void set_none(MapE *m) { m->key[0] = 0; m->st = M_NONE; m->at = (long)time(NULL); map_dirty(); }

/* ---- TMDB ids to IMDb ids, kept ("-": TMDB has none) ---- */
typedef struct { int tid; char tt[16]; } TT; static TT *tts; static int ntts, captts, ttsLoaded, ttsDirty;
static const char *tt_of(int tid) { for (int i = 0; i < ntts; i++) if (tts[i].tid == tid) return tts[i].tt; return NULL; }
static void tt_put(int tid, const char *tt) { if (tid <= 0 || tt_of(tid)) return; if (ntts == captts) { int nc = captts ? captts * 2 : 256; TT *t = realloc(tts, sizeof *t * (size_t)nc); if (!t) return; tts = t; captts = nc; }
  tts[ntts].tid = tid; snprintf(tts[ntts].tt, sizeof tts[ntts].tt, "%s", tt); ntts++; ttsDirty = 1; mapSaveAt = app_now() + 3; }
static void tts_load(void) { if (ttsLoaded) return; ttsLoaded = 1; size_t len; unsigned char *b = cache_read("coll2:tt", 30L * 86400, &len); if (!b) return; cJSON *j = cJSON_Parse((const char *)b); free(b); const cJSON *e;
  if (cJSON_IsObject(j)) cJSON_ArrayForEach(e, j) if (e->string && cJSON_IsString(e)) tt_put(atoi(e->string), e->valuestring); cJSON_Delete(j); ttsDirty = 0; }
static void tts_save(void) { cJSON *j = cJSON_CreateObject(); for (int i = 0; i < ntts; i++) { char k[16]; snprintf(k, sizeof k, "%d", tts[i].tid); cJSON_AddStringToObject(j, k, tts[i].tt); } char *s = cJSON_PrintUnformatted(j); cJSON_Delete(j); if (s) { cache_write("coll2:tt", (const unsigned char *)s, strlen(s)); free(s); } ttsDirty = 0; }

/* ---- TMDB calls: a queue, a few at once ---- */
enum { T_FIND, T_MOVIE, T_COLL, T_EXT };
typedef struct { int kind; char path[160], ref[24]; } TReq;
static TReq tq[512]; static int ntq, tFly;
static void tm_push(int kind, const char *path, const char *ref) { for (int i = 0; i < ntq; i++) if (tq[i].kind == kind && !strcmp(tq[i].path, path)) return; if (ntq >= 512) return;
  TReq *r = &tq[ntq++]; r->kind = kind; snprintf(r->path, sizeof r->path, "%s", path); snprintf(r->ref, sizeof r->ref, "%s", ref); }
static int have_tmdb(void) { char url[1200], hdr[480]; return tmdb_call("/configuration", url, sizeof url, hdr, sizeof hdr); }

/* ---- a collection's titles ---- */
#define NCOLL 24
typedef struct { Coll c; int state; double at, askAt; char lang[8]; int tid[COLL_MAXPART], ntid, extLeft; } CEnt; /* state 0 empty, 1 coming, 2 ready, 3 none; tid: TMDB's parts */
static CEnt ce[NCOLL]; static int ceNext;
static CEnt *ce_find(const char *key) { for (int i = 0; i < NCOLL; i++) if (ce[i].state && !strcmp(ce[i].c.key, key)) return &ce[i]; return NULL; }
static void ce_key(const CEnt *e, char *k, size_t cap) { snprintf(k, cap, "coll2:%s:%s", e->c.key, e->lang); }
static void ce_save(CEnt *e) {
  cJSON *j = cJSON_CreateObject(); cJSON_AddStringToObject(j, "name", e->c.name); cJSON_AddStringToObject(j, "overview", e->c.overview); cJSON_AddStringToObject(j, "poster", e->c.poster); cJSON_AddStringToObject(j, "backdrop", e->c.backdrop); cJSON_AddNumberToObject(j, "none", e->state == 3);
  cJSON *a = cJSON_AddArrayToObject(j, "parts"); for (int i = 0; i < e->c.n; i++) { const CollPart *p = &e->c.part[i]; cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "id", p->id); cJSON_AddStringToObject(o, "type", p->type); cJSON_AddStringToObject(o, "name", p->name); cJSON_AddStringToObject(o, "poster", p->poster); cJSON_AddNumberToObject(o, "year", p->year); cJSON_AddNumberToObject(o, "tmdb", p->tmdb); cJSON_AddItemToArray(a, o); }
  char *s = cJSON_PrintUnformatted(j); cJSON_Delete(j); char k[80]; ce_key(e, k, sizeof k); if (s) { cache_write(k, (const unsigned char *)s, strlen(s)); free(s); }
}
static int ce_load(CEnt *e) {
  char k[80]; ce_key(e, k, sizeof k); size_t len; unsigned char *b = cache_read(k, COLL_SECS, &len); if (!b) return 0; cJSON *j = cJSON_Parse((const char *)b); free(b); if (!cJSON_IsObject(j)) { cJSON_Delete(j); return 0; }
  snprintf(e->c.name, sizeof e->c.name, "%s", js_str(j, "name", "")); snprintf(e->c.overview, sizeof e->c.overview, "%s", js_str(j, "overview", "")); snprintf(e->c.poster, sizeof e->c.poster, "%s", js_str(j, "poster", "")); snprintf(e->c.backdrop, sizeof e->c.backdrop, "%s", js_str(j, "backdrop", ""));
  e->c.n = 0; const cJSON *p; cJSON_ArrayForEach(p, js_arr(j, "parts")) { if (e->c.n >= COLL_MAXPART) break; CollPart *x = &e->c.part[e->c.n++]; snprintf(x->id, sizeof x->id, "%s", js_str(p, "id", "")); snprintf(x->type, sizeof x->type, "%s", js_str(p, "type", "movie")); snprintf(x->name, sizeof x->name, "%s", js_str(p, "name", "")); snprintf(x->poster, sizeof x->poster, "%s", js_str(p, "poster", "")); x->year = (int)js_num(p, "year", 0); x->tmdb = (int)js_num(p, "tmdb", 0); }
  int none = (int)js_num(j, "none", 0); cJSON_Delete(j); e->state = none || e->c.n < 2 ? 3 : 2; return 1;
}
static int cmp_part(const void *a, const void *b) { const CollPart *x = a, *y = b; int ya = x->year ? x->year : 9999, yb = y->year ? y->year : 9999; return ya - yb; }
/* all in: the titles in the order they came out, missing pictures from what is known; fewer than two is no collection */
static void ce_finish(CEnt *e) {
  for (int i = 1; i < e->c.n; i++) for (int k = i; k > 0 && cmp_part(&e->c.part[k - 1], &e->c.part[k]) > 0; k--) { CollPart t = e->c.part[k]; e->c.part[k] = e->c.part[k - 1]; e->c.part[k - 1] = t; } /* (stable: same year, TMDB's own order) */
  for (int i = 0; i < e->c.n; i++) { CollPart *p = &e->c.part[i]; if (!p->poster[0] && p->id[0]) snprintf(p->poster, sizeof p->poster, "https://images.metahub.space/poster/small/%s/img", p->id); }
  /* (a franchise has no poster of its own: the Library shows the title of it that is there, never its oldest) */
  if (e->c.n && !e->c.backdrop[0] && e->c.part[0].id[0]) snprintf(e->c.backdrop, sizeof e->c.backdrop, "https://images.metahub.space/background/medium/%s/img", e->c.part[0].id);
  if (!e->c.name[0]) for (int i = 0; i < nmap; i++) if (!strcmp(map[i].key, e->c.key) && map[i].name[0]) { snprintf(e->c.name, sizeof e->c.name, "%s", map[i].name); break; }
  e->state = e->c.n >= 2 ? 2 : 3; e->at = app_now(); ce_save(e); ver++;
  fprintf(stderr, "[coll] %s: %s, %d titles%s\n", e->c.key, e->c.name, e->c.n, e->state == 3 ? " (not a collection)" : "");
}
/* a TMDB collection: shown at once with TMDB's parts (as the reference app does); each one's IMDb id fills in as it comes
   (one already known at once), one TMDB says has none is taken out */
static void ce_tm_done(CEnt *e) {
  int n = e->ntid; e->c.n = n;
  for (int i = 0; i < n; i++) { const char *tt = tt_of(e->tid[i]); e->c.part[i].tmdb = e->tid[i]; snprintf(e->c.part[i].id, sizeof e->c.part[0].id, "%s", tt && is_tt(tt) ? tt : ""); }
  for (int i = 0; i < e->c.n; i++) { const char *tt = tt_of(e->c.part[i].tmdb); if (tt && !is_tt(tt)) { memmove(&e->c.part[i], &e->c.part[i + 1], sizeof(CollPart) * (size_t)(e->c.n - i - 1)); e->c.n--; i--; } } /* ("-": no IMDb id: the addons could not find it) */
  ce_finish(e);
}
/* an IMDb id came: the parts with that TMDB id take it (one TMDB has none for goes) */
static void ce_tt_came(int tid) {
  const char *tt = tt_of(tid); if (!tt) return; int any = 0;
  for (int k = 0; k < NCOLL; k++) { CEnt *e = &ce[k]; if (e->state != 2 && e->state != 3) continue;
    for (int i = 0; i < e->c.n; i++) { CollPart *p = &e->c.part[i]; if (p->tmdb != tid || p->id[0]) continue; any = 1;
      if (is_tt(tt)) { snprintf(p->id, sizeof p->id, "%s", tt); if (!p->poster[0]) snprintf(p->poster, sizeof p->poster, "https://images.metahub.space/poster/small/%s/img", tt); }
      else { memmove(p, p + 1, sizeof(CollPart) * (size_t)(e->c.n - i - 1)); e->c.n--; i--; } }
    if (any) { if (e->c.n < 2) e->state = 3; ce_save(e); } }
  if (any) ver++;
}

/* ---- Wikidata: which franchise (many titles at once), a franchise's titles ---- */
static double wdBackoff, wdFlyAt; static int wdFly;
static char wdAsked[WD_BATCH][16]; static int nWdAsked;
static void wd_failed(int st) { wdBackoff = app_now() + (st == 429 ? 120 : 30); } /* (rate limited: two minutes; busy: half a minute) */
/* a title's links, most specific first; the first one that two titles or more share (and not too many) is its
   collection: its franchise, the series it is part of, the work it is based on (a book or comic: a dozen adaptations at
   most, never every Dracula), its fictional universe (the Marvel Cinematic Universe) */
static const struct { const char *p; int max; } LINK[] = { { "P8345", COLL_MAXPART }, { "P179", COLL_MAXPART }, { "P144", 12 }, { "P1434", COLL_MAXPART } };
#define NLINK 4
static int link_of(const char *uri) { for (int i = 0; i < NLINK; i++) { const char *q = strrchr(uri, '/'); if (q && !strcmp(q + 1, LINK[i].p)) return i; } return -1; }
static void on_wd_map(int st, const unsigned char *body, size_t len, void *ud) {
  (void)len; (void)ud; wdFly = 0; cJSON *j = st == 200 ? net_json(body) : NULL; const cJSON *bs = js_arr(js_obj(j, "results"), "bindings"), *b;
  if (!bs) { fprintf(stderr, "[coll] Wikidata did not answer (%d): asked again later\n", st); net_json_free(j); nWdAsked = 0; wd_failed(st); return; } /* (they stay to be asked) */
  int got = 0;
  for (int i = 0; i < nWdAsked; i++) { int k = map_find(wdAsked[i]); if (k < 0 || map[k].st != M_WD) continue;
    int best = NLINK, bn = 0; char bq[24] = "", bl[96] = "";
    cJSON_ArrayForEach(b, bs) { if (strcmp(bval(b, "imdb"), wdAsked[i])) continue; int li = link_of(bval(b, "p")), n = atoi(bval(b, "n")); const char *g = strrchr(bval(b, "g"), '/');
      if (li < 0 || !g || n < 2 || n > LINK[li].max) continue; if (li < best || (li == best && n < bn)) { best = li; bn = n; snprintf(bq, sizeof bq, "%s", g + 1); snprintf(bl, sizeof bl, "%s", bval(b, "gl")); } }
    if (best < NLINK) { char key[24]; snprintf(key, sizeof key, "w%s_%s", LINK[best].p, bq); set_has(&map[k], key, bl); got++; } else set_none(&map[k]); }
  fprintf(stderr, "[coll] Wikidata: %d titles asked, %d in a collection\n", nWdAsked, got); nWdAsked = 0; net_json_free(j);
}
static void wd_map_send(void) {
  if (wdFly || app_now() < wdBackoff) return; char vals[WD_BATCH * 16 + 16] = ""; size_t o = 0; nWdAsked = 0;
  for (int i = 0; i < nmap && nWdAsked < WD_BATCH; i++) if (map[i].st == M_WD) { snprintf(wdAsked[nWdAsked++], 16, "%s", map[i].id); o += (size_t)snprintf(vals + o, sizeof vals - o, "\"%s\" ", map[i].id); }
  if (!nWdAsked) return;
  char q[WD_BATCH * 16 + 1200]; snprintf(q, sizeof q, "SELECT ?imdb ?p ?g (SAMPLE(?gl0) AS ?gl) (COUNT(DISTINCT ?m) AS ?n) WHERE { VALUES ?imdb { %s} VALUES ?p { wdt:P8345 wdt:P179 wdt:P144 wdt:P1434 } "
    "?f wdt:P345 ?imdb ; ?p ?g . ?o ?p ?g ; wdt:P345 ?m ; wdt:P31 ?t . VALUES ?t { " FILM_T " " SER_T " } FILTER(STRSTARTS(?m, \"tt\")) OPTIONAL { ?g rdfs:label ?gl0 . FILTER(LANG(?gl0) = \"en\" || LANG(?gl0) = \"mul\") } } GROUP BY ?imdb ?p ?g", vals);
  static char u[WD_BATCH * 60 + 4000]; sparql_url(q, u, sizeof u); wdFly = 1; wdFlyAt = app_now(); net_get(u, on_wd_map, NULL);
}
static char wdMemKey[24]; static int wdMemFly; static double wdMemAt;
static void on_wd_members(int st, const unsigned char *body, size_t len, void *ud) {
  (void)len; (void)ud; wdMemFly = 0; CEnt *e = ce_find(wdMemKey); cJSON *j = st == 200 ? net_json(body) : NULL; const cJSON *bs = js_arr(js_obj(j, "results"), "bindings"), *b;
  if (!e || e->state != 1) { net_json_free(j); return; }
  if (!bs) { fprintf(stderr, "[coll] Wikidata did not answer (%d) for %s: asked again later\n", st, wdMemKey); net_json_free(j); wd_failed(st); e->askAt = 0; return; }
  e->c.n = 0; cJSON_ArrayForEach(b, bs) { if (e->c.n >= COLL_MAXPART) break; const char *tt = bval(b, "imdb"); if (!is_tt(tt)) continue; CollPart *p = &e->c.part[e->c.n]; memset(p, 0, sizeof *p);
    snprintf(p->id, sizeof p->id, "%s", tt); snprintf(p->type, sizeof p->type, "%s", !strcmp(bval(b, "s"), "1") ? "series" : "movie"); snprintf(p->name, sizeof p->name, "%s", bval(b, "lab")); p->year = atoi(bval(b, "yr")); e->c.n++;
    if (!e->c.name[0] && *bval(b, "fl")) snprintf(e->c.name, sizeof e->c.name, "%s", bval(b, "fl")); }
  net_json_free(j); ce_finish(e);
}
static void wd_members_send(CEnt *e) {
  if (wdMemFly || app_now() < wdBackoff) return; wdMemFly = 1; wdMemAt = app_now(); snprintf(wdMemKey, sizeof wdMemKey, "%s", e->c.key); e->askAt = app_now();
  char pr[12] = "P8345", q0[20]; const char *us = strchr(e->c.key, '_'); if (us) { snprintf(pr, sizeof pr, "%.*s", (int)(us - e->c.key - 1), e->c.key + 1); snprintf(q0, sizeof q0, "%s", us + 1); } else snprintf(q0, sizeof q0, "%s", e->c.key + 1);
  char q[2400]; snprintf(q, sizeof q, "SELECT ?imdb (SAMPLE(?l) AS ?lab) (MIN(?yy) AS ?yr) (MAX(?ser) AS ?s) (SAMPLE(?fl0) AS ?fl) WHERE { ?f wdt:%s wd:%s ; wdt:P345 ?imdb . FILTER(STRSTARTS(?imdb, \"tt\")) "
    "{ ?f wdt:P31 ?t . VALUES ?t { " FILM_T " } BIND(0 AS ?ser) } UNION { ?f wdt:P31 ?t . VALUES ?t { " SER_T " } BIND(1 AS ?ser) } "
    "OPTIONAL { ?f wdt:P577 ?d . } OPTIONAL { ?f wdt:P580 ?d2 . } BIND(YEAR(COALESCE(?d2, ?d)) AS ?yy) OPTIONAL { ?f rdfs:label ?l . FILTER(LANG(?l) = \"en\" || LANG(?l) = \"mul\") } OPTIONAL { wd:%s rdfs:label ?fl0 . FILTER(LANG(?fl0) = \"en\" || LANG(?fl0) = \"mul\") } } GROUP BY ?imdb ORDER BY ?yr LIMIT %d", pr, q0, q0, COLL_MAXPART);
  static char u[6000]; sparql_url(q, u, sizeof u); net_get(u, on_wd_members, NULL);
}

/* ---- TMDB answers ---- */
static void on_tm(int st, const unsigned char *body, size_t len, void *ud) {
  (void)len; TReq r = *(TReq *)ud; free(ud); tFly--; cJSON *j = st == 200 ? cJSON_Parse((const char *)body) : NULL;
  if (r.kind == T_FIND) { int k = map_find(r.ref); if (k >= 0 && map[k].st == M_TMDB) { MapE *m = &map[k]; const cJSON *mr = cJSON_GetArrayItem(js_arr(j, "movie_results"), 0);
      if (!j) m->st = st == 404 ? M_WD : M_RETRY; /* (no answer: again later; a 404: TMDB does not know it) */
      else if (mr) { m->tmdb = (int)js_num(mr, "id", 0); tt_put(m->tmdb, m->id); char p[96]; snprintf(p, sizeof p, "/movie/%d?language=%s", m->tmdb, i18n_lang()); tm_push(T_MOVIE, p, m->id); }
      else m->st = M_WD; } } /* (TMDB has it as no film: a franchise, perhaps) */
  else if (r.kind == T_MOVIE) { int k = map_find(r.ref); if (k >= 0 && map[k].st == M_TMDB) { MapE *m = &map[k]; const cJSON *bc = js_obj(j, "belongs_to_collection");
      if (!j) m->st = M_RETRY;
      else if (cJSON_IsObject(bc) && js_num(bc, "id", 0) > 0) { char key[24]; snprintf(key, sizeof key, "t%d", (int)js_num(bc, "id", 0)); set_has(m, key, js_str(bc, "name", "")); }
      else m->st = M_WD; } } /* (in no TMDB collection: a franchise of films and shows, perhaps) */
  else if (r.kind == T_COLL) { CEnt *e = ce_find(r.ref); if (e && e->state == 1) {
      if (!j) { e->state = 0; ver++; } /* (no answer: asked again the next time it is wanted) */
      else { const char *nm = js_str(j, "name", ""), *ov = js_str(j, "overview", ""), *po = js_str(j, "poster_path", ""), *bd = js_str(j, "backdrop_path", "");
        if (*nm) snprintf(e->c.name, sizeof e->c.name, "%s", nm); if (*ov) snprintf(e->c.overview, sizeof e->c.overview, "%s", ov);
        if (*po) snprintf(e->c.poster, sizeof e->c.poster, "https://image.tmdb.org/t/p/w342%s", po); if (*bd) snprintf(e->c.backdrop, sizeof e->c.backdrop, "https://image.tmdb.org/t/p/w1280%s", bd);
        e->ntid = 0; e->extLeft = 0; const cJSON *p;
        cJSON_ArrayForEach(p, js_arr(j, "parts")) { if (e->ntid >= COLL_MAXPART) break; int id = (int)js_num(p, "id", 0); if (id <= 0) continue;
          int i = e->ntid++; e->tid[i] = id; CollPart *x = &e->c.part[i]; memset(x, 0, sizeof *x); snprintf(x->type, sizeof x->type, "movie"); snprintf(x->name, sizeof x->name, "%s", js_str(p, "title", "")); x->year = atoi(js_str(p, "release_date", ""));
          const char *pp = js_str(p, "poster_path", ""); if (*pp) snprintf(x->poster, sizeof x->poster, "https://image.tmdb.org/t/p/w342%s", pp);
          if (!tt_of(id)) { char ep[64]; snprintf(ep, sizeof ep, "/movie/%d/external_ids", id); tm_push(T_EXT, ep, e->c.key); } }
        ce_tm_done(e); } } } /* (ready now: the IMDb ids follow, ce_tt_came) */
  else if (r.kind == T_EXT) { int tid = atoi(r.path + 7); const char *tt = js_str(j, "imdb_id", ""); if (is_tt(tt)) tt_put(tid, tt); else if (j) tt_put(tid, "-");
    ce_tt_came(tid); }
  cJSON_Delete(j);
}
static void tm_send(void) {
  while (tFly < TM_PAR && ntq) { TReq *r = malloc(sizeof *r); if (!r) return; *r = tq[0]; memmove(tq, tq + 1, sizeof tq[0] * (size_t)(ntq - 1)); ntq--;
    char url[1200], hdr[480]; if (!tmdb_call(r->path, url, sizeof url, hdr, sizeof hdr)) { free(r); continue; }
    tFly++; if (hdr[0]) net_get_hdr(url, hdr, on_tm, r); else net_get(url, on_tm, r); }
}

/* ---- the calls ---- */
int coll_of(const char *type, const char *imdb, char *key, size_t cap, const char **name) {
  if (key && cap) key[0] = 0; if (name) *name = ""; if (!is_tt(imdb) || !type) return -1; int film = !strcmp(type, "movie"); if (!film && strcmp(type, "series")) return -1;
  map_load(); tts_load(); int k = map_find(imdb);
  if (k >= 0 && map[k].st == M_HAS) { if (key) snprintf(key, cap, "%s", map[k].key); if (name) { CEnt *e = ce_find(map[k].key); *name = e && e->state == 2 && e->c.name[0] ? e->c.name : map[k].name; } return 1; }
  if (k >= 0 && map[k].st == M_NONE) return -1;
  if (k < 0 || map[k].st == M_RETRY) { MapE *m = map_add(imdb); if (!m) return 0; snprintf(m->type, sizeof m->type, "%s", type);
    if (film && have_tmdb()) { m->st = M_TMDB; char p[96];
      if (m->tmdb > 0) { snprintf(p, sizeof p, "/movie/%d?language=%s", m->tmdb, i18n_lang()); tm_push(T_MOVIE, p, imdb); } /* (its TMDB id known from its details: no look-up) */
      else { snprintf(p, sizeof p, "/find/%s?external_source=imdb_id", imdb); tm_push(T_FIND, p, imdb); } } else m->st = M_WD; }
  return 0;
}
const Coll *coll_get(const char *key) {
  if (!key || (key[0] != 't' && key[0] != 'w') || !key[1]) return NULL; map_load(); tts_load(); CEnt *e = ce_find(key);
  if (e && e->state >= 2 && strcmp(e->lang, i18n_lang())) { e->state = 0; e = NULL; } /* (another language since: asked again) */
  if (e) return e->state == 2 ? &e->c : NULL;
  e = &ce[ceNext]; ceNext = (ceNext + 1) % NCOLL; memset(e, 0, sizeof *e); snprintf(e->c.key, sizeof e->c.key, "%s", key); snprintf(e->lang, sizeof e->lang, "%s", i18n_lang());
  if (ce_load(e)) { e->at = app_now(); return e->state == 2 ? &e->c : NULL; } /* kept from before */
  e->state = 1;
  if (key[0] == 't') { char p[96]; snprintf(p, sizeof p, "/collection/%s?language=%s", key + 1, i18n_lang()); tm_push(T_COLL, p, key); }
  return NULL; /* (a franchise: asked of Wikidata from coll_pump, one at a time) */
}
int coll_known(const char *key) { if (!coll_get(key)) { CEnt *e = ce_find(key); return e && e->state == 3 ? -1 : 0; } return 1; }
int coll_index(const Coll *c, const char *imdb) { if (!c || !imdb) return -1; for (int i = 0; i < c->n; i++) if (c->part[i].id[0] && !strcmp(c->part[i].id, imdb)) return i;
  int k = map_find(imdb); if (k >= 0 && map[k].tmdb > 0) for (int i = 0; i < c->n; i++) if (c->part[i].tmdb == map[k].tmdb) return i; /* (its IMDb id still on its way: by TMDB's) */
  return -1; }
void coll_hint_tmdb(const char *imdb, int tmdb) { if (!is_tt(imdb) || tmdb <= 0) return; map_load(); tts_load(); tt_put(tmdb, imdb); MapE *m = map_add(imdb); if (m && !m->tmdb) m->tmdb = tmdb; }

/* once a frame: TMDB's queue, Wikidata's questions (one at a time), the files */
void coll_pump(void) {
  if (!mapLoaded) return; double now = app_now(); tm_send();
  if (wdFly && now - wdFlyAt > 60) wdFly = 0; if (wdMemFly && now - wdMemAt > 60) wdMemFly = 0; /* (answers that never came) */
  { static double firstWd; int any = 0; for (int i = 0; i < nmap && !any; i++) any = map[i].st == M_WD; if (any && !firstWd) firstWd = now; if (!any) firstWd = 0; if (any && now - firstWd > 0.25) wd_map_send(); }
  if (!wdMemFly) for (int i = 0; i < NCOLL; i++) if (ce[i].state == 1 && ce[i].c.key[0] == 'w' && !ce[i].askAt) { wd_members_send(&ce[i]); break; } /* (franchises waiting for Wikidata) */
  { static double redo; if (now > redo) { redo = now + 30; for (int i = 0; i < nmap; i++) if (map[i].st == M_RETRY && map[i].type[0]) { char t[8], id[16]; snprintf(t, sizeof t, "%s", map[i].type); snprintf(id, sizeof id, "%s", map[i].id); coll_of(t, id, NULL, 0, NULL); } } } /* (failed asks: again) */
  if ((mapDirty || ttsDirty) && now >= mapSaveAt) { if (mapDirty) map_save(); if (ttsDirty) tts_save(); }
}
