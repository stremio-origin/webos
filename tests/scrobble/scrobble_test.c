/* tests/scrobble: the Trakt scrobbler (src/core/scrobble.c) against a stand-in network and clock: what it sends for
   each way a film or an episode is watched, how it connects, renews and gives up. tests/scrobble/run.sh */
#include "core/scrobble.h"
#include "core/net.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/* ---- stand-ins ---- */
static double clk = 1000; double sb_test_clock(void) { return clk; }
static char dataDir[256], setting[16] = "true", client[80] = "0123456789abcdef0123456789abcdef0123456789a";
const char *store_data_dir(void) { return dataDir; }
const char *store_get(const char *k, const char *d) { return !strcmp(k, "traktScrobble") ? setting : d; }
const char *trakt_client_id(void) { return client; }
static char acctId[32] = "acct1"; /* the Stremio account signed in ("" signed out) */
int sb_signed_in(void) { return acctId[0] != 0; }
const char *sb_user_id(void) { return acctId; }
const char *T(const char *s) { return s; }
const char *T1(const char *s, const char *k, const char *v) { (void)k; (void)v; return s; }
typedef struct { char url[200], body[1200], hdr[600]; NetCb cb; void *ud; int answered; } Req;
static Req R[200]; static int nr;
unsigned net_post_hdr_t(const char *url, const char *body, const char *ct, const char *h, int to, NetCb cb, void *ud) {
  (void)ct; (void)to; Req *r = &R[nr++]; snprintf(r->url, sizeof r->url, "%s", url); snprintf(r->body, sizeof r->body, "%s", body ? body : ""); snprintf(r->hdr, sizeof r->hdr, "%s", h ? h : ""); r->cb = cb; r->ud = ud; r->answered = 0; return (unsigned)nr; }
unsigned net_get_hdr(const char *url, const char *h, NetCb cb, void *ud) { return net_post_hdr_t(url, "", NULL, h, 0, cb, ud); }
unsigned net_send(const char *m, const char *url, const char *h, const void *b, size_t n, int to, NetCb cb, void *ud) { (void)m; (void)b; (void)n; return net_post_hdr_t(url, "", NULL, h, to, cb, ud); }
cJSON *net_json(const unsigned char *b) { return b ? cJSON_Parse((const char *)b) : NULL; }
void net_json_free(cJSON *j) { cJSON_Delete(j); }

static void answer(int i, int st, const char *body) { Req *r = &R[i]; if (r->answered) return; r->answered = 1; if (r->cb) r->cb(st, (const unsigned char *)(body ? body : ""), body ? strlen(body) : 0, r->ud); }
static void answer_all(int st) { for (int i = 0; i < nr; i++) if (!R[i].answered) answer(i, st, st == 201 ? "{}" : NULL); }
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static int count(const char *urlPart) { int n = 0; for (int i = 0; i < nr; i++) if (strstr(R[i].url, urlPart)) n++; return n; }
static const Req *last(const char *urlPart) { for (int i = nr - 1; i >= 0; i--) if (strstr(R[i].url, urlPart)) return &R[i]; return NULL; }
static double progress_of(const Req *r) { cJSON *j = r ? cJSON_Parse(r->body) : NULL; double p = j ? cJSON_GetObjectItem(j, "progress")->valuedouble : -1; cJSON_Delete(j); return p; }

/* a frame loop: dt steps, the player's state and position moving with it */
static double pos;
static void play(const ScrobbleItem *it, int state, double secs, double dur) {
  for (double t = 0; t < secs; t += 0.5) { clk += 0.5; if (state == 1) pos += 0.5; scrobble_player(it, state, pos, dur); scrobble_tick(); answer_all(201); } }
static void flush(void) { for (int i = 0; i < 40; i++) { clk += 0.1; scrobble_tick(); answer_all(201); } } /* everything queued sent and answered */
static void reset(void) { flush(); nr = 0; }

int main(void) {
  snprintf(dataDir, sizeof dataDir, "/tmp/sbscrobble.%d", (int)getpid()); char cmd[300]; snprintf(cmd, sizeof cmd, "mkdir -p %s", dataDir); if (system(cmd)) return 1;
  ScrobbleItem movie = { "movie", "tt0000001", "A Test Film", "2001", -1, -1 }, ep = { "series", "tt0000002", "A Show", "2010", 2, 5 }, ep6 = { "series", "tt0000002", "A Show", "2010", 2, 6 };
  ScrobbleItem anime = { "series", "kitsu:123", "Anime", "", 1, 3 }, tmdbMovie = { "movie", "tmdb:603", "Another", "1999", -1, -1 };

  /* not connected: nothing at all */
  scrobble_init(); CHECK(scrobble_state() == TRAKT_OFF, "starts disconnected");
  pos = 0; play(&movie, 1, 60, 6000); scrobble_player_end(pos, 6000, 0); scrobble_tick(); CHECK(nr == 0, "nothing sent when not connected (%d)", nr);

  /* connecting: a code, "not yet" a few times, then the tokens and the name */
  int done = -2; scrobble_link_start(NULL, NULL); CHECK(count("/oauth/device/code") == 1, "asks for a code");
  CHECK(strstr(last("/oauth/device/code")->body, client) && !strstr(last("/oauth/device/code")->body, "secret"), "the code request: client id, no secret");
  answer(nr - 1, 200, "{\"device_code\":\"dev1\",\"user_code\":\"ABCD1234\",\"verification_url\":\"https://trakt.tv/activate\",\"expires_in\":600,\"interval\":5}");
  CHECK(!strcmp(scrobble_link_code(), "ABCD1234"), "the code shown");
  for (int i = 0; i < 3; i++) { clk += 5.1; done = scrobble_link_poll(); answer(nr - 1, 400, NULL); } CHECK(done == 0 && count("/oauth/device/token") == 3, "polls at the interval while pending (%d)", count("/oauth/device/token"));
  clk += 5.1; scrobble_link_poll(); answer(nr - 1, 429, NULL); double before = clk; int n0 = count("/oauth/device/token");
  for (int i = 0; i < 12; i++) { clk += 1; scrobble_link_poll(); } CHECK(count("/oauth/device/token") == n0 + 1, "slows down after 429 (%d in %.0f s)", count("/oauth/device/token") - n0, clk - before);
  char tok[300]; snprintf(tok, sizeof tok, "{\"access_token\":\"acc1\",\"refresh_token\":\"ref1\",\"created_at\":%lld,\"expires_in\":604800}", (long long)time(NULL));
  answer(nr - 1, 200, tok); CHECK(scrobble_link_poll() == 1 && scrobble_state() == TRAKT_LINKED, "connected");
  CHECK(strstr(last("/users/settings")->hdr, "Bearer acc1") != NULL, "asks the name with the token"); answer(nr - 1, 200, "{\"user\":{\"username\":\"tester\"}}");
  CHECK(!strcmp(scrobble_user(), "tester"), "the name kept");
  { char p2[300]; snprintf(p2, sizeof p2, "%s/trakt.auth", dataDir); FILE *f = fopen(p2, "rb"); char b[600] = ""; if (f) { size_t k = fread(b, 1, sizeof b - 1, f); b[k] = 0; fclose(f); } CHECK(strstr(b, "\"refresh\":\"ref1\""), "tokens on disk"); }

  /* a film: nothing in the first 10 s, then start; a pause; resumed; a jump; closed half way */
  reset(); pos = 0; play(&movie, 0, 3, 6000); play(&movie, 1, 8, 6000); CHECK(count("/scrobble/") == 0, "nothing in the first seconds (%d)", count("/scrobble/"));
  play(&movie, 1, 4, 6000); CHECK(count("/scrobble/start") == 1, "start after 10 s of playing");
  { const Req *r = last("/scrobble/start"); CHECK(strstr(r->body, "\"imdb\":\"tt0000001\"") && strstr(r->body, "\"title\":\"A Test Film\"") && strstr(r->body, "\"year\":2001"), "the film named: %s", r->body); CHECK(strstr(r->hdr, "Bearer acc1") && strstr(r->hdr, "trakt-api-key: ") && strstr(r->hdr, "trakt-api-version: 2"), "headers"); }
  play(&movie, 1, 30, 6000); CHECK(count("/scrobble/start") == 1, "no repeat while it plays on");
  play(&movie, 2, 5, 6000); CHECK(count("/scrobble/pause") == 1, "pause");
  play(&movie, 2, 30, 6000); CHECK(count("/scrobble/pause") == 1, "one pause, however long");
  play(&movie, 1, 12, 6000); CHECK(count("/scrobble/start") == 2, "start again after the pause");
  pos += 1200; play(&movie, 0, 2, 6000); play(&movie, 1, 2, 6000); CHECK(count("/scrobble/start") == 3, "a jump said once it plays (%d)", count("/scrobble/start"));
  { double p = progress_of(last("/scrobble/start")); CHECK(p > 20 && p < 21, "the jump's progress %.2f", p); }
  pos = 3000; scrobble_player(&movie, 1, pos, 6000); scrobble_player_end(3000, 6000, 0); flush();
  CHECK(count("/scrobble/stop") == 1 && progress_of(last("/scrobble/stop")) == 50, "stop at 50 %% on close (%.2f)", progress_of(last("/scrobble/stop")));
  scrobble_player_end(3000, 6000, 0); scrobble_tick(); CHECK(count("/scrobble/stop") == 1, "one stop");

  /* played to its end: stop at 100 */
  reset(); pos = 5900; play(&movie, 1, 12, 6000); scrobble_player_end(6000, 6000, 1); flush();
  CHECK(count("/scrobble/stop") == 1 && progress_of(last("/scrobble/stop")) == 100, "stop at 100 at the end");

  /* a source tried and left at once: nothing; resumed near the end and closed: a stop, Trakt marks it */
  reset(); pos = 10; play(&movie, 1, 4, 6000); scrobble_player_end(pos, 6000, 0); flush(); CHECK(count("/scrobble/") == 0, "a source left at once: nothing");
  reset(); pos = 30; play(&movie, 1, 12, 6000); scrobble_player_end(pos, 6000, 0); flush(); CHECK(count("/scrobble/start") == 1 && count("/scrobble/stop") == 0, "left under 1 %%: no stop (Trakt ignores it)");
  reset(); pos = 5000; play(&movie, 1, 3, 6000); scrobble_player_end(5003, 6000, 0); flush(); CHECK(count("/scrobble/stop") == 1, "near the end: stop even without a start");

  /* an episode, then the next one in the same player */
  reset(); pos = 0; play(&ep, 1, 12, 2700); { const Req *r = last("/scrobble/start"); CHECK(r && strstr(r->body, "\"show\":{\"ids\":{\"imdb\":\"tt0000002\"}}") && strstr(r->body, "\"season\":2") && strstr(r->body, "\"number\":5"), "the episode named: %s", r ? r->body : "(none)"); }
  pos = 2650; play(&ep, 1, 2, 2700); scrobble_player_end(2700, 2700, 1); flush(); pos = 0; play(&ep6, 1, 12, 2600);
  CHECK(count("/scrobble/stop") == 1 && count("/scrobble/start") >= 2 && strstr(last("/scrobble/start")->body, "\"number\":6"), "next episode: stop then start (%d stops, %d starts)", count("/scrobble/stop"), count("/scrobble/start"));
  scrobble_player_end(pos, 2600, 0); flush();

  /* what Trakt can't name, or must not hear of */
  reset(); pos = 0; play(&anime, 1, 20, 1400); scrobble_player_end(pos, 1400, 0); flush(); CHECK(count("/scrobble/") == 0, "an anime catalog's own id: nothing");
  reset(); pos = 0; play(&movie, 1, 20, 0); scrobble_player_end(pos, 0, 0); flush(); CHECK(count("/scrobble/") == 0, "no length (live): nothing");
  reset(); pos = 0; play(&tmdbMovie, 1, 12, 7000); CHECK(last("/scrobble/start") && strstr(last("/scrobble/start")->body, "\"tmdb\":603"), "a TMDB id named"); scrobble_player_end(pos, 7000, 0); flush();
  snprintf(setting, sizeof setting, "false"); nr = 0; pos = 0; play(&movie, 1, 20, 6000); scrobble_player_end(pos, 6000, 0); flush(); CHECK(count("/scrobble/") == 0, "switched off: nothing"); snprintf(setting, sizeof setting, "true");

  /* failures: no network, then it goes; a duplicate; a 401 renews the access */
  reset(); pos = 0; for (double t = 0; t < 12; t += 0.5) { clk += 0.5; pos += 0.5; scrobble_player(&movie, 1, pos, 6000); scrobble_tick(); }
  CHECK(count("/scrobble/start") == 1, "start sent"); answer(nr - 1, 0, NULL); scrobble_tick(); CHECK(count("/scrobble/start") == 1, "not again at once");
  clk += 16; scrobble_tick(); CHECK(count("/scrobble/start") == 2, "tried again later"); answer(nr - 1, 201, "{}");
  scrobble_player_end(600, 6000, 0); scrobble_tick(); answer(nr - 1, 409, "{}"); scrobble_tick(); CHECK(count("/scrobble/stop") == 1, "409 taken as done");
  reset(); pos = 0; for (double t = 0; t < 12; t += 0.5) { clk += 0.5; pos += 0.5; scrobble_player(&movie, 1, pos, 6000); scrobble_tick(); }
  answer(nr - 1, 401, NULL); scrobble_tick(); CHECK(count("/oauth/token") == 1 && strstr(last("/oauth/token")->body, "\"refresh_token\":\"ref1\"") && strstr(last("/oauth/token")->body, "refresh_token"), "401: renews with the refresh token");
  snprintf(tok, sizeof tok, "{\"access_token\":\"acc2\",\"refresh_token\":\"ref2\",\"created_at\":%lld,\"expires_in\":604800}", (long long)time(NULL)); answer(nr - 1, 200, tok);
  clk += 16; scrobble_tick(); CHECK(strstr(last("/scrobble/start")->hdr, "Bearer acc2") != NULL, "sent again with the new token");
  { char p2[300]; snprintf(p2, sizeof p2, "%s/trakt.auth", dataDir); FILE *f = fopen(p2, "rb"); char b[600] = ""; if (f) { size_t k = fread(b, 1, sizeof b - 1, f); b[k] = 0; fclose(f); } CHECK(strstr(b, "ref2") && !strstr(b, "ref1"), "the used refresh token replaced on disk"); }
  answer_all(201); scrobble_player_end(pos, 6000, 0); flush();

  /* the renewal refused: connect again, nothing sent meanwhile */
  reset(); pos = 0; for (double t = 0; t < 12; t += 0.5) { clk += 0.5; pos += 0.5; scrobble_player(&movie, 1, pos, 6000); scrobble_tick(); }
  answer(nr - 1, 401, NULL); scrobble_tick(); answer(nr - 1, 400, NULL); CHECK(scrobble_state() == TRAKT_RELINK, "refused renewal: connect again");
  int before2 = count("/scrobble/"); play(&movie, 1, 30, 6000); CHECK(count("/scrobble/") == before2, "nothing sent while signed out");

  /* disconnect: revoked, tokens gone */
  reset(); scrobble_unlink(); CHECK(count("/oauth/revoke") == 1 && scrobble_state() == TRAKT_OFF, "disconnect revokes");
  { char p2[300]; snprintf(p2, sizeof p2, "%s/trakt.auth", dataDir); CHECK(access(p2, F_OK) != 0, "tokens deleted"); }

  /* Trakt belongs to the Stremio account that connected it */
  { char p2[300]; snprintf(p2, sizeof p2, "%s/trakt.auth", dataDir);
    #define FILE_HAS(x) ({ FILE *f_ = fopen(p2, "rb"); char b_[800] = ""; if (f_) { size_t k_ = fread(b_, 1, sizeof b_ - 1, f_); b_[k_] = 0; fclose(f_); } strstr(b_, x) != NULL; })
    #define CONNECT(acc) do { reset(); scrobble_link_start(NULL, NULL); answer(nr - 1, 200, "{\"device_code\":\"d\",\"user_code\":\"C\",\"verification_url\":\"u\",\"expires_in\":600,\"interval\":5}"); clk += 5.1; scrobble_link_poll(); \
      snprintf(tok, sizeof tok, "{\"access_token\":\"%s\",\"refresh_token\":\"r%s\",\"created_at\":%lld,\"expires_in\":604800}", acc, acc, (long long)time(NULL)); answer(nr - 1, 200, tok); scrobble_link_poll(); answer_all(200); } while (0)
    CONNECT("accA"); CHECK(scrobble_state() == TRAKT_LINKED && FILE_HAS("\"acct\":\"acct1\""), "connected: the account signed in kept with it");
    clk += 2; scrobble_tick(); CHECK(scrobble_state() == TRAKT_LINKED && count("/oauth/revoke") == 0, "the same account: stays connected");
    snprintf(acctId, sizeof acctId, "%s", ""); clk += 2; scrobble_tick(); CHECK(scrobble_state() == TRAKT_LINKED, "signed out (left to sb_forget_local): not touched here");
    reset(); snprintf(acctId, sizeof acctId, "acct2"); clk += 2; scrobble_tick();
    CHECK(scrobble_state() == TRAKT_OFF && count("/oauth/revoke") == 1 && access(p2, F_OK) != 0, "another account signed in: disconnected, revoked, tokens deleted");
    pos = 0; play(&movie, 1, 20, 6000); scrobble_player_end(pos, 6000, 0); flush(); CHECK(count("/scrobble/") == 0, "nothing sent for the other account");
    snprintf(acctId, sizeof acctId, "%s", ""); CONNECT("accB"); CHECK(scrobble_state() == TRAKT_LINKED && FILE_HAS("\"acct\":\"\""), "connected signed out: no account yet");
    snprintf(acctId, sizeof acctId, "acct3"); clk += 2; scrobble_tick(); CHECK(scrobble_state() == TRAKT_LINKED && FILE_HAS("\"acct\":\"acct3\""), "no account kept (a file from before): taken by the one signed in");
    scrobble_unlink(); }
  snprintf(cmd, sizeof cmd, "rm -rf %s", dataDir); if (system(cmd)) {}
  printf(fails ? "%d failed\n" : "all ok\n", fails); return fails ? 1 : 0;
}
