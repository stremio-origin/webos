#include "core/reviews.h"
#include "core/trakt.h"
#include "core/i18n.h"
#include "core/tmdb.h"
#include "core/net.h"
#include "core/stremio.h"
#include "core/json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>

#define KEEP 12
enum { SRC_META, SRC_WIKI, SRC_TRATE, SRC_TCOMM, SRC_FIND, SRC_MVOTE, SRC_MREV };
enum { RK_TRAKT = 0, RK_TMDB, RK_IMDB, RK_RT, RK_MC }; /* the sources' order in the tiles, and who gives the main score */
static Reviews kept[KEEP]; static int keptNext; static unsigned gen;
typedef struct { char key[192]; int src; } Ask; /* a request on its way: which entry it is for (the entry may be gone by then) */
typedef struct { char type[16], id[24]; int s, e; } Who;

static Reviews *find(const char *key) { for (int i = 0; i < KEEP; i++) if (kept[i].key[0] && !strcmp(kept[i].key, key)) return &kept[i]; return NULL; }
static void who_of(const Reviews *r, Who *w) { memset(w, 0, sizeof *w); sscanf(r->key, "%15[^|]|%23[^|]|%d|%d", w->type, w->id, &w->s, &w->e); }
static void touched(Reviews *r) { r->ver++; gen++; }
static Ask *ask_new(const Reviews *r, int src) { Ask *a = calloc(1, sizeof *a); snprintf(a->key, sizeof a->key, "%s", r->key); a->src = src; return a; }
/* a request that was sent counts as pending until its answer lands */
static void sent(Reviews *r, unsigned id, Ask *a) { if (id) r->pending++; else free(a); }

/* ---- what came ---- */
static void tile(Reviews *r, int rank, const char *src, const char *val) {
  for (int i = 0; i < r->ntiles; i++) if (r->tiles[i].rank == rank) { snprintf(r->tiles[i].val, sizeof r->tiles[i].val, "%s", val); return; }
  if (r->ntiles >= 6) return; int at = r->ntiles; while (at > 0 && r->tiles[at - 1].rank > rank) { r->tiles[at] = r->tiles[at - 1]; at--; } /* in the sources' fixed order */
  RevTile *t = &r->tiles[at]; t->rank = rank; snprintf(t->src, sizeof t->src, "%s", src); snprintf(t->val, sizeof t->val, "%s", val); r->ntiles++;
}
static void score(Reviews *r, int rank, float v, int votes, const char *src) {
  if (v <= 0) return; if (r->scoreSrc[0] && rank > r->rank) return;
  r->score = v; r->votes = votes; r->rank = rank; snprintf(r->scoreSrc, sizeof r->scoreSrc, "%s", src);
}
/* the comment's words: whitespace runs as one space, Trakt's [spoiler] marks dropped, cut whole at REV_TEXT */
static void words(const char *in, char *out, size_t cap) {
  size_t o = 0; int sp = 1;
  for (const char *s = in; *s; ) {
    if (*s == '[' && (!strncasecmp(s, "[spoiler]", 9) || !strncasecmp(s, "[/spoiler]", 10))) { s += s[1] == '/' ? 10 : 9; continue; }
    unsigned char c = (unsigned char)*s; int n = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 1;
    if (n == 1 && (isspace(c) || c < 0x20)) { if (!sp && o + 1 < cap) out[o++] = ' '; sp = 1; s++; continue; }
    if (o + n + 4 >= cap) { memcpy(out + o, "\xe2\x80\xa6", 3); o += 3; break; }
    memcpy(out + o, s, n); o += n; s += n; sp = 0; }
  while (o > 0 && out[o - 1] == ' ') o--; out[o] = 0;
}
static int days_ago(const char *iso) {
  struct tm tm; memset(&tm, 0, sizeof tm); if (!iso || sscanf(iso, "%d-%d-%dT%d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) < 3) return -1;
  tm.tm_year -= 1900; tm.tm_mon -= 1; time_t t = timegm(&tm), now = time(NULL); if (t <= 0 || now < t) return 0; return (int)((now - t) / 86400);
}
static void comment(Reviews *r, const char *from, const char *user, const char *text, int score, int likes, int replies, int spoiler, const char *created) {
  if (!text || !*text || r->nc >= REV_MAXC) return;
  if (!r->c) { r->c = calloc(REV_MAXC, sizeof *r->c); if (!r->c) return; }
  RevComment *c = &r->c[r->nc]; memset(c, 0, sizeof *c); words(text, c->text, sizeof c->text); if (!c->text[0]) return;
  snprintf(c->from, sizeof c->from, "%s", from); snprintf(c->user, sizeof c->user, "%s", user && *user ? user : "\xe2\x80\x94");
  c->score = score < 0 ? 0 : score > 10 ? 10 : score; c->likes = likes; c->replies = replies; c->spoiler = spoiler; c->days = days_ago(created); r->nc++;
}
static void trakt_ratings(Reviews *r, const cJSON *j) {
  float v = (float)js_num(j, "rating", 0); int votes = (int)js_num(j, "votes", 0); if (v <= 0) return;
  const cJSON *d = js_obj(j, "distribution"); int any = 0;
  if (d) for (int i = 0; i < 10; i++) { char k[4]; snprintf(k, sizeof k, "%d", i + 1); r->dist[i] = (int)js_num(d, k, 0); any |= r->dist[i] > 0; }
  r->hasDist = any; char s[12]; snprintf(s, sizeof s, "%d%%", (int)(v * 10 + 0.5f)); tile(r, RK_TRAKT, "Trakt", s); score(r, RK_TRAKT, v, votes, "Trakt");
}
static void trakt_comments(Reviews *r, const cJSON *j) {
  const cJSON *c; cJSON_ArrayForEach(c, j) { const cJSON *u = js_obj(c, "user"); const char *nm = js_str(u, "username", js_str(u, "name", ""));
    comment(r, "Trakt", nm, js_str(c, "comment", ""), (int)js_num(js_obj(c, "user_stats"), "rating", 0), (int)js_num(c, "likes", 0), (int)js_num(c, "replies", 0), js_bool(c, "spoiler", 0), js_str(c, "created_at", NULL)); }
}
static void tmdb_vote(Reviews *r, const cJSON *j) {
  { const char *ov = js_str(j, "overview", ""), *pp = js_str(j, "poster_path", ""); /* a season's own words and poster (its answer carries them) */
    if (*ov) snprintf(r->overview, sizeof r->overview, "%s", ov); if (*pp == '/' && strlen(pp) < 100) snprintf(r->poster, sizeof r->poster, "https://image.tmdb.org/t/p/w342%s", pp); }
  float v = (float)js_num(j, "vote_average", 0); if (v <= 0) return; char s[12]; snprintf(s, sizeof s, "%.1f", v); tile(r, RK_TMDB, "TMDB", s); score(r, RK_TMDB, v, (int)js_num(j, "vote_count", 0), "TMDB");
}
static void tmdb_reviews(Reviews *r, const cJSON *j) {
  const cJSON *c; cJSON_ArrayForEach(c, js_arr(j, "results")) { const cJSON *d = js_obj(c, "author_details"); const char *nm = js_str(d, "username", js_str(c, "author", ""));
    comment(r, "TMDB", nm, js_str(c, "content", ""), (int)(js_num(d, "rating", 0) + 0.5), 0, 0, 0, js_str(c, "created_at", NULL)); }
}
/* Wikidata's review scores: Rotten Tomatoes' tomatometer ("85%"), Metacritic's ("78/100") */
static void wiki(Reviews *r, const cJSON *j) {
  const cJSON *b; cJSON_ArrayForEach(b, js_arr(js_obj(j, "results"), "bindings")) { const char *by = js_str(js_obj(b, "by"), "value", ""), *v = js_str(js_obj(b, "s"), "value", ""); size_t n = strlen(v);
    const char *q = strrchr(by, '/'); q = q ? q + 1 : by;
    if (!strcmp(q, "Q105584") && n && v[n - 1] == '%') tile(r, RK_RT, "Rotten Tomatoes", v);
    else if (!strcmp(q, "Q150248") && n > 4 && !strcmp(v + n - 4, "/100")) { char s[12]; snprintf(s, sizeof s, "%.*s", (int)(n - 4), v); tile(r, RK_MC, "Metacritic", s); } }
}
/* ---- the sources, asked ---- */
static void on_net(int st, const unsigned char *body, size_t len, void *ud) {
  Ask *a = ud; Reviews *r = find(a->key); (void)len;
  { static const char *SN[] = { "details", "wikidata", "trakt ratings", "trakt comments", "tmdb find", "tmdb vote", "tmdb reviews" }; /* (each source's answer in the log: one that never shows says why there) */
    fprintf(stderr, "[reviews] %s for %s: status %d, %u bytes\n", a->src >= 0 && a->src < 7 ? SN[a->src] : "?", a->key, st, (unsigned)len); }
  if (r && st != -1) { cJSON *j = st == 200 ? net_json(body) : NULL;
    if (j) switch (a->src) { case SRC_WIKI: wiki(r, j); break; case SRC_TRATE: trakt_ratings(r, j); break; case SRC_TCOMM: trakt_comments(r, j); break;
      case SRC_MVOTE: tmdb_vote(r, j); break; case SRC_MREV: tmdb_reviews(r, j); break; default: break; }
    net_json_free(j); r->pending--; touched(r); }
  free(a);
}
static void ask_tmdb(Reviews *r, int tid) {
  Who w; who_of(r, &w); char p[160]; int tv = !strcmp(w.type, "series"); Ask *a;
  if (w.e) snprintf(p, sizeof p, "/tv/%d/season/%d/episode/%d", tid, w.s, w.e); else if (w.s) snprintf(p, sizeof p, "/tv/%d/season/%d?language=%s", tid, w.s, i18n_lang()); /* (in the app's language: its words show) */ else snprintf(p, sizeof p, "/%s/%d", tv ? "tv" : "movie", tid);
  a = ask_new(r, SRC_MVOTE); sent(r, tmdb_request(p, on_net, a), a);
  if (!w.s) { snprintf(p, sizeof p, "/%s/%d/reviews", tv ? "tv" : "movie", tid); a = ask_new(r, SRC_MREV); sent(r, tmdb_request(p, on_net, a), a); /* TMDB reviews are the title's */ }
}
static void on_find(int st, const unsigned char *body, size_t len, void *ud) {
  Ask *a = ud; Reviews *r = find(a->key); (void)len;
  if (r && st != -1) { cJSON *j = st == 200 ? net_json(body) : NULL; r->pending--;
    const cJSON *mv = cJSON_GetArrayItem(js_arr(j, "movie_results"), 0), *tv = cJSON_GetArrayItem(js_arr(j, "tv_results"), 0); int tid = (int)js_num(tv ? tv : mv, "id", 0);
    net_json_free(j); if (tid) ask_tmdb(r, tid); touched(r); }
  free(a);
}
static void ask_trakt(Reviews *r) {
  Who w; who_of(r, &w); char base[160], p[200]; int tv = !strcmp(w.type, "series"); Ask *a;
  if (w.e) snprintf(base, sizeof base, "/shows/%s/seasons/%d/episodes/%d", w.id, w.s, w.e); else if (w.s) snprintf(base, sizeof base, "/shows/%s/seasons/%d", w.id, w.s); else snprintf(base, sizeof base, "/%s/%s", tv ? "shows" : "movies", w.id);
  snprintf(p, sizeof p, "%s/ratings", base); a = ask_new(r, SRC_TRATE); sent(r, trakt_request(p, on_net, a), a);
  snprintf(p, sizeof p, "%s/comments/likes?limit=%d", base, REV_MAXC); a = ask_new(r, SRC_TCOMM); sent(r, trakt_request(p, on_net, a), a);
}
static void ask_wiki(Reviews *r) {
  Who w; who_of(r, &w); char sp[400], enc[1300], url[1500]; static const char *H = "0123456789ABCDEF";
  snprintf(sp, sizeof sp, "SELECT ?by ?s WHERE { ?f wdt:P345 \"%s\" . ?f p:P444 ?st . ?st ps:P444 ?s . ?st pq:P447 ?by . } LIMIT 12", w.id);
  size_t o = 0; for (const unsigned char *s = (const unsigned char *)sp; *s && o + 4 < sizeof enc; s++) { if (isalnum(*s) || *s == '-' || *s == '_' || *s == '.' || *s == '~') enc[o++] = (char)*s; else { enc[o++] = '%'; enc[o++] = H[*s >> 4]; enc[o++] = H[*s & 15]; } }
  enc[o] = 0; snprintf(url, sizeof url, "https://query.wikidata.org/sparql?format=json&query=%s", enc);
  Ask *a = ask_new(r, SRC_WIKI); sent(r, net_get(url, on_net, a), a);
}
/* the title's details: the IMDb rating (an episode's own when its entry carries one), TMDB's id for the TMDB calls */
static void meta_in(Reviews *r, const cJSON *m) {
  Who w; who_of(r, &w); float ir = 0;
  if (w.e) { char vid[SB_VID]; snprintf(vid, sizeof vid, "%s:%d:%d", w.id, w.s, w.e); const cJSON *v; cJSON_ArrayForEach(v, js_arr(m, "videos")) if (!strcmp(js_str(v, "id", ""), vid)) { ir = (float)js_numstr(v, "rating", js_numstr(v, "imdbRating", 0)); break; } }
  else if (!w.s) ir = (float)js_numstr(m, "imdbRating", 0);
  if (ir > 0) { char s[12]; snprintf(s, sizeof s, "%.1f", ir); tile(r, RK_IMDB, "IMDb", s); score(r, RK_IMDB, ir, 0, "IMDb"); }
  int tid = (int)js_num(m, "moviedb_id", 0);
  if (tid) ask_tmdb(r, tid);
  else if (tmdb_has_key()) { char p[64]; snprintf(p, sizeof p, "/find/%s?external_source=imdb_id", w.id); Ask *a = ask_new(r, SRC_FIND); sent(r, tmdb_request(p, on_find, a), a); }
}
static void on_meta(cJSON *m, void *ud) { Ask *a = ud; Reviews *r = find(a->key); if (r) { r->pending--; if (m) meta_in(r, m); touched(r); } free(a); }
static void start(Reviews *r) {
  Who w; who_of(r, &w); r->asked = 1;
  if (sb_is_imdb(w.id)) { if (!w.s) ask_wiki(r); ask_trakt(r); }
  cJSON *m = sb_meta_cached(w.type, w.id);
  if (m) meta_in(r, m); else { Ask *a = ask_new(r, SRC_META); r->pending++; sb_meta(w.type, w.id, on_meta, a); }
  touched(r);
}
const Reviews *reviews_get(const char *type, const char *id, int season, int episode) {
  char key[192]; snprintf(key, sizeof key, "%s|%s|%d|%d", type, id, season, episode); /* (a long addon id whole: #81) */
  { static unsigned tg, kg; static int seen; unsigned t2 = trakt_generation(), k2 = tmdb_generation(); /* a key came or went: all is asked again */
    if (seen && (t2 != tg || k2 != kg)) { for (int i = 0; i < KEEP; i++) { free(kept[i].c); memset(&kept[i], 0, sizeof kept[i]); } gen++; } seen = 1; tg = t2; kg = k2; }
  Reviews *r = find(key);
  if (!r) { r = &kept[keptNext]; keptNext = (keptNext + 1) % KEEP; free(r->c); memset(r, 0, sizeof *r); snprintf(r->key, sizeof r->key, "%s", key); start(r); }
  return r;
}
