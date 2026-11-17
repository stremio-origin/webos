#include "core/scrobble.h"
#include "core/trakt.h"
#include "core/net.h"
#include "core/store.h"
#include "core/stremio.h"
#include "core/i18n.h"
#include "core/json.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#define AUTH "https://auth.trakt.tv"   /* every OAuth call (Trakt: never the API host for these) */
#define API  "https://api.trakt.tv"
#define START_AFTER 10.0               /* seconds of steady playback before Trakt hears of it (a source tried and left is not) */
#define JUMP 30.0                      /* a position this far from where it should be: a seek, Trakt told again */

#ifdef SB_TEST_CLOCK
double sb_test_clock(void); /* tests/scrobble: time as the test says */
static double mono(void) { return sb_test_clock(); }
#else
static double mono(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
#endif

/* ---- the connection: tokens in the data dir, never logged ---- */
/* acct: the Stremio account signed in when Trakt was connected (its _id). Trakt belongs to that account on this TV:
   another account signed in disconnects it, as does signing out (sb_forget_local); a file from before has none and is
   taken by the account signed in */
static struct { int inited, state, refreshing; char client[80], access[160], refresh[160], user[96], acct[64], path[1200]; long long expires; double retryAt, acctAt; } A;
static void auth_save(void) {
  cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "client", A.client); cJSON_AddStringToObject(o, "access", A.access); cJSON_AddStringToObject(o, "refresh", A.refresh);
  cJSON_AddNumberToObject(o, "expires", (double)A.expires); cJSON_AddStringToObject(o, "user", A.user); cJSON_AddStringToObject(o, "acct", A.acct); cJSON_AddBoolToObject(o, "relink", A.state == TRAKT_RELINK);
  char *s = cJSON_PrintUnformatted(o); cJSON_Delete(o); if (!s) return;
  char tmp[1300]; snprintf(tmp, sizeof tmp, "%s.tmp", A.path); FILE *f = fopen(tmp, "wb");
  if (f) { size_t n = strlen(s), w = fwrite(s, 1, n, f); fclose(f); if (w == n) rename(tmp, A.path); else remove(tmp); } /* whole or not at all: a refresh token works once */
  free(s);
}
static void auth_clear(void) { memset(A.access, 0, sizeof A.access); memset(A.refresh, 0, sizeof A.refresh); A.expires = 0; A.user[0] = 0; A.acct[0] = 0; A.client[0] = 0; A.state = TRAKT_OFF; remove(A.path); }
static void take_tokens(const cJSON *j) { /* a token answer: access, refresh, when it ends */
  snprintf(A.access, sizeof A.access, "%s", js_str(j, "access_token", "")); snprintf(A.refresh, sizeof A.refresh, "%s", js_str(j, "refresh_token", ""));
  double cr = js_num(j, "created_at", 0), ex = js_num(j, "expires_in", 7 * 86400.0); A.expires = (long long)((cr > 0 ? cr : (double)time(NULL)) + ex);
  snprintf(A.client, sizeof A.client, "%s", trakt_client_id()); A.state = TRAKT_LINKED; auth_save();
}
static void headers(char *out, size_t cap, int bearer) {
  int n = snprintf(out, cap, "trakt-api-version: 2\ntrakt-api-key: %s", A.client[0] ? A.client : trakt_client_id());
  if (bearer && n > 0 && (size_t)n < cap) snprintf(out + n, cap - (size_t)n, "\nAuthorization: Bearer %s", A.access);
}
static void post(const char *url, cJSON *body, int bearer, NetCb cb, void *ud) {
  char *s = cJSON_PrintUnformatted(body); cJSON_Delete(body); char h[512]; headers(h, sizeof h, bearer);
  net_post_hdr_t(url, s ? s : "{}", "application/json", h, 20000, cb, ud); free(s);
}
static void on_user(int st, const unsigned char *b, size_t n, void *ud) {
  (void)n; (void)ud; cJSON *j = st == 200 ? net_json(b) : NULL; const char *u = js_str(js_obj(j, "user"), "username", "");
  if (*u && A.state == TRAKT_LINKED) { snprintf(A.user, sizeof A.user, "%s", u); auth_save(); fprintf(stderr, "[trakt] connected, its name known\n"); } /* (never the account's name: the log may be shared) */
  net_json_free(j);
}
static void ask_user(void) { char h[512]; headers(h, sizeof h, 1); net_get_hdr(API "/users/settings", h, on_user, NULL); }
/* the account's card (Settings > Log out): its name, VIP, when it joined, and what it has watched; asked when the card
   shows, at most every 10 minutes, kept in memory only */
static TraktProfile PR; static double prAt = -1e9;
const TraktProfile *scrobble_profile(void) { return &PR; }
static void on_prof_user(int st, const unsigned char *b, size_t n, void *ud) {
  (void)n; (void)ud; cJSON *j = st == 200 ? net_json(b) : NULL; const cJSON *u = js_obj(j, "user");
  if (u) { snprintf(PR.user, sizeof PR.user, "%s", js_str(u, "username", "")); snprintf(PR.name, sizeof PR.name, "%s", js_str(u, "name", "")); PR.vip = js_bool(u, "vip", 0) || js_bool(u, "vip_ep", 0);
    snprintf(PR.joined, sizeof PR.joined, "%.10s", js_str(u, "joined_at", "")); PR.haveUser = 1; }
  net_json_free(j); }
static void on_prof_stats(int st, const unsigned char *b, size_t n, void *ud) {
  (void)n; (void)ud; cJSON *j = st == 200 ? net_json(b) : NULL;
  if (j) { const cJSON *m = js_obj(j, "movies"), *e = js_obj(j, "episodes"); PR.movies = (int)js_num(m, "watched", 0); PR.episodes = (int)js_num(e, "watched", 0); PR.minutes = (long)(js_num(m, "minutes", 0) + js_num(e, "minutes", 0)); PR.haveStats = 1; }
  net_json_free(j); }
void scrobble_profile_ask(void) {
  if (A.state != TRAKT_LINKED || !A.access[0]) { if (A.state != TRAKT_LINKED) memset(&PR, 0, sizeof PR); return; }
  if (mono() - prAt < 600) return; prAt = mono(); char h[512]; headers(h, sizeof h, 1);
  net_get_hdr(API "/users/settings", h, on_prof_user, NULL); net_get_hdr(API "/users/me/stats", h, on_prof_stats, NULL); }
static void on_refresh(int st, const unsigned char *b, size_t n, void *ud) {
  (void)n; (void)ud; A.refreshing = 0; cJSON *j = st == 200 ? net_json(b) : NULL;
  if (j && *js_str(j, "access_token", "")) { take_tokens(j); fprintf(stderr, "[trakt] access renewed\n"); }
  else if (st == 400 || st == 401 || st == 403) { A.state = TRAKT_RELINK; auth_save(); fprintf(stderr, "[trakt] the access could not be renewed (%d): connect again\n", st); } /* (the refresh token is spent or revoked) */
  else { A.retryAt = mono() + 600; fprintf(stderr, "[trakt] renewing failed (%d): tried again later\n", st); }
  net_json_free(j);
}
static void refresh_now(void) {
  if (A.refreshing || !A.refresh[0]) return; A.refreshing = 1;
  cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "refresh_token", A.refresh); cJSON_AddStringToObject(o, "client_id", A.client);
  cJSON_AddStringToObject(o, "redirect_uri", "urn:ietf:wg:oauth:2.0:oob"); cJSON_AddStringToObject(o, "grant_type", "refresh_token");
  post(AUTH "/oauth/token", o, 0, on_refresh, NULL);
}
static int due(void) { return A.state == TRAKT_LINKED && A.expires - (long long)time(NULL) < 2 * 86400; } /* two days before it ends */
void scrobble_init(void) {
  if (A.inited) return; A.inited = 1; snprintf(A.path, sizeof A.path, "%s/trakt.auth", store_data_dir());
  FILE *f = fopen(A.path, "rb"); if (!f) return;
  char buf[2048]; size_t n = fread(buf, 1, sizeof buf - 1, f); buf[n] = 0; fclose(f); cJSON *j = cJSON_Parse(buf); if (!j) return;
  snprintf(A.client, sizeof A.client, "%s", js_str(j, "client", "")); snprintf(A.access, sizeof A.access, "%s", js_str(j, "access", "")); snprintf(A.refresh, sizeof A.refresh, "%s", js_str(j, "refresh", ""));
  snprintf(A.user, sizeof A.user, "%s", js_str(j, "user", "")); snprintf(A.acct, sizeof A.acct, "%s", js_str(j, "acct", "")); A.expires = (long long)js_num(j, "expires", 0); int relink = js_bool(j, "relink", 0); cJSON_Delete(j);
  if (!A.access[0] || !A.refresh[0]) { auth_clear(); return; }
  /* signed out by Trakt before, or made with another Trakt app than this one now: Trakt would refuse it, so it asks again */
  A.state = relink || strcmp(A.client, trakt_client_id()) ? TRAKT_RELINK : TRAKT_LINKED;
  fprintf(stderr, "[trakt] %s%s\n", A.state == TRAKT_LINKED ? "connected" : "needs connecting again", A.user[0] ? "" : " (name not known yet)");
  if (A.state == TRAKT_LINKED && !A.user[0]) ask_user();
}
int scrobble_state(void) { if (!A.inited) scrobble_init(); return A.state; }
const char *scrobble_user(void) { return A.user; }
int scrobble_on(void) { return scrobble_state() == TRAKT_LINKED && strcmp(store_get("traktScrobble", "true"), "false") != 0; }

/* ---- connecting with a device code ---- */
static struct { int on, busy, done, over; unsigned tok; char device[160], code[24], url[160], err[160]; double interval, until, nextAt; void (*cb)(int, void *); void *ud; } L;
static void on_code(int st, const unsigned char *b, size_t n, void *ud) {
  (void)n; if ((unsigned)(uintptr_t)ud != L.tok || !L.on) return; cJSON *j = st == 200 ? net_json(b) : NULL;
  const char *dc = js_str(j, "device_code", ""), *uc = js_str(j, "user_code", "");
  if (!*dc || !*uc) { fprintf(stderr, "[trakt] no code (%d)\n", st); L.on = 0; net_json_free(j); if (L.cb) L.cb(0, L.ud); return; }
  snprintf(L.device, sizeof L.device, "%s", dc); snprintf(L.code, sizeof L.code, "%s", uc); snprintf(L.url, sizeof L.url, "%s", js_str(j, "verification_url", "https://trakt.tv/activate"));
  L.interval = js_num(j, "interval", 5); if (L.interval < 1) L.interval = 5; L.until = mono() + js_num(j, "expires_in", 600); L.nextAt = mono() + L.interval;
  fprintf(stderr, "[trakt] a code to enter at %s (for %.0f s)\n", L.url, js_num(j, "expires_in", 600));
  net_json_free(j); if (L.cb) L.cb(1, L.ud);
}
void scrobble_link_start(void (*cb)(int, void *), void *ud) {
  if (!A.inited) scrobble_init(); memset(&L, 0, sizeof L); L.on = 1; L.tok = (unsigned)(mono() * 1000); L.cb = cb; L.ud = ud;
  if (!trakt_client_id()[0]) { L.on = 0; if (cb) cb(0, ud); return; }
  cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "client_id", trakt_client_id());
  char *s = cJSON_PrintUnformatted(o); cJSON_Delete(o); char h[256]; snprintf(h, sizeof h, "trakt-api-version: 2\ntrakt-api-key: %s", trakt_client_id());
  net_post_hdr_t(AUTH "/oauth/device/code", s, "application/json", h, 20000, on_code, (void *)(uintptr_t)L.tok); free(s);
}
const char *scrobble_link_code(void) { return L.code; }
const char *scrobble_link_url(void) { return L.url; }
const char *scrobble_link_error(void) { return L.err; }
static void on_token(int st, const unsigned char *b, size_t n, void *ud) {
  (void)n; L.busy = 0; if ((unsigned)(uintptr_t)ud != L.tok || !L.on) return; cJSON *j = st == 200 ? net_json(b) : NULL;
  if (j && *js_str(j, "access_token", "")) { snprintf(A.acct, sizeof A.acct, "%s", sb_signed_in() ? sb_user_id() : ""); take_tokens(j); A.user[0] = 0; L.done = 1; fprintf(stderr, "[trakt] this TV is connected\n"); ask_user(); }
  else if (st == 400 || st == 0) { } /* not entered yet (or no answer): asked again at the interval */
  else if (st == 429) L.interval += 5; /* Trakt asks for slower polling */
  else { L.over = 1; snprintf(L.err, sizeof L.err, "%s", T(st == 418 ? "Trakt access was refused" : st == 410 || st == 404 ? "The code expired. Try again" : st == 409 ? "This code was already used. Try again" : "Couldn’t connect to Trakt. Try again"));
    fprintf(stderr, "[trakt] connecting ended (%d)\n", st); }
  net_json_free(j);
}
int scrobble_link_poll(void) {
  if (L.done) return 1; if (!L.on || L.over || !L.device[0]) return L.over || !L.on ? -1 : 0;
  double now = mono();
  if (now > L.until) { L.over = 1; snprintf(L.err, sizeof L.err, "%s", T("The code expired. Try again")); return -1; }
  if (!L.busy && now >= L.nextAt) { L.busy = 1; L.nextAt = now + L.interval;
    cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "code", L.device); cJSON_AddStringToObject(o, "client_id", trakt_client_id());
    char *s = cJSON_PrintUnformatted(o); cJSON_Delete(o); char h[256]; snprintf(h, sizeof h, "trakt-api-version: 2\ntrakt-api-key: %s", trakt_client_id());
    net_post_hdr_t(AUTH "/oauth/device/token", s, "application/json", h, 20000, on_token, (void *)(uintptr_t)L.tok); free(s); }
  return 0;
}
void scrobble_link_cancel(void) { L.on = 0; L.tok++; }
/* a read of the account's own data (its history, watchlist, recommendations): with the token, 0 when not connected */
int scrobble_get(const char *path, NetCb cb, void *ud) {
  if (scrobble_state() != TRAKT_LINKED || !A.access[0]) return 0; char url[400], h[512]; snprintf(url, sizeof url, API "%s", path); headers(h, sizeof h, 1);
  return net_get_hdr(url, h, cb, ud) != 0; }
/* a write to the account (marks made by hand): the body is taken (freed), 0 when not connected */
int scrobble_post(const char *path, cJSON *body, NetCb cb, void *ud) {
  if (scrobble_state() != TRAKT_LINKED || !A.access[0]) { cJSON_Delete(body); return 0; } char url[400]; snprintf(url, sizeof url, API "%s", path); post(url, body, 1, cb, ud); return 1; }
/* a DELETE on the account (a paused position dismissed by hand): 0 when not connected */
int scrobble_delete(const char *path, NetCb cb, void *ud) {
  if (scrobble_state() != TRAKT_LINKED || !A.access[0]) return 0; char url[400], h[512]; snprintf(url, sizeof url, API "%s", path); headers(h, sizeof h, 1);
  return net_send("DELETE", url, h, "", 0, 20000, cb, ud) != 0; }
void scrobble_unlink(void) {
  if (A.access[0]) { cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "token", A.access); cJSON_AddStringToObject(o, "client_id", A.client); post(AUTH "/oauth/revoke", o, 0, NULL, NULL); } /* (best effort: the tokens go here whatever it answers) */
  auth_clear(); fprintf(stderr, "[trakt] disconnected\n");
}

/* ---- what Trakt is told: a short queue, sent one at a time, in order ---- */
enum { J_START, J_PAUSE, J_STOP };
typedef struct { int kind, tries; char key[160]; char *body; double notBefore, made; } Job;
#define QN 16
static Job Q[QN]; static int qn, inFlight;
static const char *KIND[3] = { "start", "pause", "stop" };
static void q_drop(int i) { free(Q[i].body); memmove(&Q[i], &Q[i + 1], sizeof(Job) * (size_t)(qn - i - 1)); qn--; }
static void q_add(int kind, const char *key, cJSON *body) {
  if (kind != J_STOP) for (int i = qn - 1; i >= (inFlight ? 1 : 0); i--) if (Q[i].kind != J_STOP && !strcmp(Q[i].key, key)) q_drop(i); /* a newer start / pause of the same item replaces one not sent yet */
  if (qn == QN) { int i = 0; while (i < qn && (Q[i].kind == J_STOP || (inFlight && i == 0))) i++; if (i == qn) i = inFlight ? 1 : 0; if (i < qn) q_drop(i); } /* full: the oldest start / pause goes first */
  char *s = cJSON_PrintUnformatted(body); cJSON_Delete(body); if (!s || qn == QN) { free(s); return; }
  Job *j = &Q[qn++]; memset(j, 0, sizeof *j); j->kind = kind; snprintf(j->key, sizeof j->key, "%s", key); j->body = s; j->made = mono();
}
static void on_sent(int st, const unsigned char *b, size_t n, void *ud) {
  (void)b; (void)n; (void)ud; inFlight = 0; if (!qn) return; Job *j = &Q[0];
  if (st == 201 || st == 200 || st == 409) { fprintf(stderr, "[trakt] %s sent%s\n", KIND[j->kind], st == 409 ? " (already watched just now)" : ""); q_drop(0); return; }
  if (st == 404 || st == 422 || st == 400) { fprintf(stderr, "[trakt] %s not taken (%d): Trakt doesn't know this item or its progress\n", KIND[j->kind], st); q_drop(0); return; }
  if (st == 401 || st == 403) { fprintf(stderr, "[trakt] %s refused (%d): renewing the access\n", KIND[j->kind], st); if (A.state == TRAKT_LINKED) refresh_now(); j->tries++; if (j->tries > 2) q_drop(0); return; }
  j->tries++; double wait = st == 429 ? 60 : 15 * j->tries; /* no answer, a busy Trakt: later */
  if (j->tries > (j->kind == J_STOP ? 6 : 3)) { fprintf(stderr, "[trakt] %s given up (%d)\n", KIND[j->kind], st); q_drop(0); return; }
  j->notBefore = mono() + wait; fprintf(stderr, "[trakt] %s failed (%d): again in %.0f s\n", KIND[j->kind], st, wait);
}
/* whose Trakt it is, once a second: a file without an account goes to the one signed in; another account signed in
   than the one that connected it: disconnected (never sent, never shown under that account). Signed out, or the
   account's id not known yet (a sign-in still fetching it): left as it is */
static void acct_check(void) {
  if (A.state == TRAKT_OFF || mono() - A.acctAt < 1) return; A.acctAt = mono();
  const char *id = sb_signed_in() ? sb_user_id() : ""; if (!id || !*id) return;
  if (!A.acct[0]) { snprintf(A.acct, sizeof A.acct, "%s", id); auth_save(); fprintf(stderr, "[trakt] kept for the Stremio account signed in\n"); }
  else if (strcmp(A.acct, id)) { fprintf(stderr, "[trakt] another Stremio account is signed in: Trakt disconnected\n"); scrobble_unlink(); }
}
void scrobble_tick(void) {
  if (!A.inited) scrobble_init();
  acct_check();
  if (due() && !A.refreshing && mono() >= A.retryAt) refresh_now();
  if (!qn || inFlight || A.refreshing) return;
  if (A.state != TRAKT_LINKED) { while (qn) q_drop(0); return; } /* signed out meanwhile: nothing to send it with */
  Job *j = &Q[0]; double now = mono(); if (now < j->notBefore) return;
  if (j->kind != J_STOP && now - j->made > 600) { q_drop(0); return; } /* a start / pause ten minutes old says nothing true any more */
  char url[96], h[512]; snprintf(url, sizeof url, API "/scrobble/%s", KIND[j->kind]); headers(h, sizeof h, 1);
  inFlight = 1; if (!net_post_hdr_t(url, j->body, "application/json", h, 20000, on_sent, NULL)) { inFlight = 0; j->notBefore = now + 15; }
}

/* ---- the player's item, followed ---- */
static struct { char key[160]; cJSON *media; int sent, jumped; /* sent: 0 nothing yet, 1 started, 2 paused */ double playingSince, lastPos, lastAt, lastDur, lastProg; } C;
static cJSON *media_of(const ScrobbleItem *it) { /* Trakt's object for it, NULL when it can't be named */
  const char *id = it->id ? it->id : ""; cJSON *ids = cJSON_CreateObject();
  if (!strncmp(id, "tt", 2) && id[2] >= '0' && id[2] <= '9') { char t[24]; size_t k = 0; while (id[k] && id[k] != ':' && k + 1 < sizeof t) { t[k] = id[k]; k++; } t[k] = 0; cJSON_AddStringToObject(ids, "imdb", t); }
  else if (!strncmp(id, "tmdb:", 5) && atoi(id + 5) > 0) cJSON_AddNumberToObject(ids, "tmdb", atoi(id + 5));
  else if (!strncmp(id, "tvdb:", 5) && atoi(id + 5) > 0) cJSON_AddNumberToObject(ids, "tvdb", atoi(id + 5));
  else { cJSON_Delete(ids); return NULL; } /* anime catalogs' own ids, channels: Trakt can't tell what they are */
  cJSON *m = cJSON_CreateObject();
  if (!strcmp(it->type, "movie")) { cJSON *mv = cJSON_AddObjectToObject(m, "movie"); if (it->title && *it->title) cJSON_AddStringToObject(mv, "title", it->title); if (it->year && atoi(it->year) > 1800) cJSON_AddNumberToObject(mv, "year", atoi(it->year)); cJSON_AddItemToObject(mv, "ids", ids); }
  else if (!strcmp(it->type, "series") && it->season >= 0 && it->episode > 0) { cJSON *sh = cJSON_AddObjectToObject(m, "show"); cJSON_AddItemToObject(sh, "ids", ids); cJSON *ep = cJSON_AddObjectToObject(m, "episode"); cJSON_AddNumberToObject(ep, "season", it->season); cJSON_AddNumberToObject(ep, "number", it->episode); }
  else { cJSON_Delete(ids); cJSON_Delete(m); return NULL; }
  return m;
}
static double prog(double pos, double dur) { double p = dur > 0 ? pos / dur * 100 : 0; return p < 0 ? 0 : p > 100 ? 100 : (double)((int)(p * 100)) / 100; }
static void send(int kind, double p) {
  if (!C.media) return; cJSON *b = cJSON_Duplicate(C.media, 1); cJSON_AddNumberToObject(b, "progress", p); q_add(kind, C.key, b); C.lastProg = p;
  fprintf(stderr, "[trakt] %s at %.1f %%\n", KIND[kind], p);
}
static void item_end(double pos, double dur, int ended) {
  if (C.media && scrobble_on()) { double p = ended ? 100 : prog(pos, dur);
    if (C.sent || p >= 80) { if (p >= 1) send(J_STOP, p); } } /* (under 1 %: Trakt ignores it; never started and not near the end: nothing to say) */
  cJSON_Delete(C.media); memset(&C, 0, sizeof C);
}
void scrobble_player(const ScrobbleItem *it, int state, double pos, double dur) {
  if (!it || !it->id || !it->type) return;
  char key[160]; snprintf(key, sizeof key, "%s|%s|%d|%d", it->type, it->id, it->season, it->episode);
  if (strcmp(key, C.key)) { if (C.key[0]) item_end(C.lastPos, C.lastDur, 0); snprintf(C.key, sizeof C.key, "%s", key); C.media = scrobble_on() && dur > 0 && dur < 1e7 ? media_of(it) : NULL; C.lastPos = pos; C.lastAt = mono(); } /* another item (the next episode in the same player) */
  if (!C.media) { if (scrobble_on() && dur > 0 && dur < 1e7) C.media = media_of(it); if (!C.media) return; } /* (its length known a moment after it opens) */
  if (!scrobble_on()) return;
  double now = mono(), expect = C.lastPos + (state == 1 ? now - C.lastAt : 0);
  if (C.sent == 1 && (pos - expect > JUMP || expect - pos > JUMP)) C.jumped = 1; /* a seek (it usually passes through buffering): said once it plays again */
  if (state == 1) {
    if (!C.playingSince) C.playingSince = now;
    if (C.sent == 1 && C.jumped) { C.jumped = 0; send(J_START, prog(pos, dur)); } /* where it plays now */
    else if (C.sent != 1 && now - C.playingSince >= START_AFTER) { send(J_START, prog(pos, dur)); C.sent = 1; C.jumped = 0; }
  } else { C.playingSince = 0;
    if (state == 2 && C.sent == 1) { send(J_PAUSE, prog(pos, dur)); C.sent = 2; } }
  C.lastPos = pos; C.lastAt = now; if (dur > 0 && dur < 1e7) C.lastDur = dur;
}
void scrobble_player_end(double pos, double dur, int ended) { if (C.key[0]) item_end(pos, dur, ended); }
