/* tests/traktwatch: what the Trakt account has watched (src/core/traktwatch.c) against a stand-in Trakt, clock and
   store: the disk copy, the checks and what they ask, the marks shown, hidden by hand and added by hand. Its thread
   runs for real; what it hands back is run here, as the drawing thread would. tests/traktwatch/run.sh */
#include "core/traktwatch.h"
#include "core/scrobble.h"
#include "core/net.h"
#include "cJSON.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- stand-ins ---- */
static int state = TRAKT_LINKED, tracking = 1, playing; static double clk = 100; static char dataDir[200], marks[8] = "true", cwSet[8] = "false"; static cJSON *root;
int scrobble_state(void) { return state; }
int scrobble_on(void) { return state == TRAKT_LINKED && tracking; }
double app_now(void) { return clk; }
int player_open(void) { return playing; }
void plat_thread_background(int n) { (void)n; }
const char *store_data_dir(void) { return dataDir; }
const char *store_get(const char *k, const char *d) { return !strcmp(k, "traktMarks") ? marks : !strcmp(k, "traktCw") ? cwSet : d; }
cJSON *store_node(const char *k) { return cJSON_GetObjectItemCaseSensitive(root, k); }
void store_set_node(const char *k, cJSON *n) { cJSON_DeleteItemFromObjectCaseSensitive(root, k); cJSON_AddItemToObject(root, k, n); }
void store_remove(const char *k) { cJSON_DeleteItemFromObjectCaseSensitive(root, k); }
cJSON *net_json(const unsigned char *b) { return b ? cJSON_Parse((const char *)b) : NULL; }
void net_json_free(cJSON *j) { cJSON_Delete(j); }
typedef struct { char path[200], body[2000]; NetCb cb; void *ud; int post; } Req; static Req R[64]; static int nr;
int scrobble_get(const char *path, NetCb cb, void *ud) { if (state != TRAKT_LINKED) return 0; Req *r = &R[nr++]; snprintf(r->path, sizeof r->path, "%s", path); r->body[0] = 0; r->cb = cb; r->ud = ud; r->post = 0; return 1; }
int scrobble_post(const char *path, cJSON *body, NetCb cb, void *ud) { Req *r = &R[nr++]; snprintf(r->path, sizeof r->path, "%s", path); char *s = cJSON_PrintUnformatted(body); snprintf(r->body, sizeof r->body, "%s", s ? s : ""); free(s); cJSON_Delete(body); r->cb = cb; r->ud = ud; r->post = 1; return 1; }
int scrobble_delete(const char *path, NetCb cb, void *ud) { Req *r = &R[nr++]; snprintf(r->path, sizeof r->path, "DELETE %s", path); r->body[0] = 0; r->cb = cb; r->ud = ud; r->post = 0; return 1; }
/* the thread's hand-back: kept, then run here */
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER; static NetCb backCb[16]; static void *backUd[16]; static int nback;
unsigned net_file(const char *p, NetCb cb, void *ud) { (void)p; pthread_mutex_lock(&mu); backCb[nback] = cb; backUd[nback] = ud; nback++; pthread_mutex_unlock(&mu); return 1; }
static int pump_once(void) { pthread_mutex_lock(&mu); int n = nback; NetCb c[16]; void *u[16]; memcpy(c, backCb, sizeof c); memcpy(u, backUd, sizeof u); nback = 0; pthread_mutex_unlock(&mu);
  for (int i = 0; i < n; i++) c[i](200, NULL, 0, u[i]); return n; }
/* everything the threads hand back, until they are quiet (a parse and a disk write may both come) */
static int pump(void) { int ran = 0, quiet = 0; for (int w = 0; w < 400 && quiet < 20; w++) { int k = pump_once(); ran += k; if (k) quiet = 0; else if (ran) quiet++; usleep(5000); } return ran; }
static void answer(const char *pathPart, int st, const char *body) { for (int i = nr - 1; i >= 0; i--) if (strstr(R[i].path, pathPart) && R[i].cb) { NetCb c = R[i].cb; R[i].cb = NULL; c(st, (const unsigned char *)body, body ? strlen(body) : 0, R[i].ud); return; } printf("(nothing asked at %s)\n", pathPart); }
static int asked(const char *pathPart) { int n = 0; for (int i = 0; i < nr; i++) if (strstr(R[i].path, pathPart)) n++; return n; }
static const Req *lastPost(void) { for (int i = nr - 1; i >= 0; i--) if (R[i].post) return &R[i]; return NULL; }
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static void tick(double secs) { for (double t = 0; t < secs; t += 0.5) { clk += 0.5; tw_tick(); } }
static int file_has(const char *x) { for (int w = 0; w < 200; w++) { char p[300]; snprintf(p, sizeof p, "%s/trakt-watched.json", dataDir); FILE *f = fopen(p, "rb"); if (f) { char b[4000] = ""; size_t k = fread(b, 1, sizeof b - 1, f); b[k] = 0; fclose(f); if (strstr(b, x)) return 1; } usleep(5000); } return 0; }

#define ACTS(m, e) "{\"movies\":{\"watched_at\":\"" m "\"},\"episodes\":{\"watched_at\":\"" e "\"}}"
int main(void) {
  root = cJSON_CreateObject(); snprintf(dataDir, sizeof dataDir, "/tmp/sbtraktw.%d", (int)getpid()); char cmd[300]; snprintf(cmd, sizeof cmd, "mkdir -p %s", dataDir); if (system(cmd)) return 1;
  /* a disk copy from before: shown at once, before Trakt is asked */
  { char p[300]; snprintf(p, sizeof p, "%s/trakt-watched.json", dataDir); FILE *f = fopen(p, "wb"); fputs("{\"am\":\"A0\",\"ae\":\"E0\",\"m\":[[\"tt0000009\",1000]],\"s\":[[\"tt0000090\",1000,[1,1,1000]]]}", f); fclose(f); }
  tw_tick(); pump(); CHECK(tw_movie("tt0000009") && tw_ep("tt0000090", 1, 1) && !tw_ep("tt0000090", 1, 2), "the disk copy shown at once");
  CHECK(nr == 0, "nothing asked at the start itself");
  /* a moment later: Trakt's last activities; both changed: films then shows asked again */
  tick(6); CHECK(asked("/sync/last_activities") == 1, "last activities asked a moment after the start");
  answer("/sync/last_activities", 200, ACTS("A1", "E1")); CHECK(asked("/sync/watched/movies") == 1, "films asked again");
  answer("/sync/watched/movies", 200, "[{\"plays\":2,\"last_watched_at\":\"2024-01-02T03:04:05.000Z\",\"movie\":{\"title\":\"x\",\"ids\":{\"imdb\":\"tt0000001\",\"tmdb\":5}}},{\"movie\":{\"ids\":{\"tmdb\":7}}}]");
  pump(); CHECK(asked("/sync/watched/shows") == 1, "then shows");
  answer("/sync/watched/shows", 200, "[{\"last_watched_at\":\"2024-02-01T00:00:00.000Z\",\"show\":{\"ids\":{\"imdb\":\"tt0000002\"}},\"seasons\":[{\"number\":1,\"episodes\":[{\"number\":3,\"last_watched_at\":\"2024-02-01T00:00:00.000Z\"},{\"number\":1,\"last_watched_at\":\"2024-01-20T00:00:00.000Z\"}]},{\"number\":2,\"episodes\":[{\"number\":1,\"last_watched_at\":\"2024-02-01T00:00:00.000Z\"}]}]}]");
  pump();
  CHECK(tw_movie("tt0000001") && !tw_movie("tt0000009"), "the films replaced (the old copy's gone)");
  CHECK(tw_show_known("tt0000002") && tw_ep("tt0000002", 1, 1) && tw_ep("tt0000002", 1, 3) && tw_ep("tt0000002", 2, 1) && !tw_ep("tt0000002", 1, 2) && !tw_show_known("tt0000090"), "episodes by season and number");
  CHECK(file_has("\"am\":\"A1\"") && file_has("tt0000002"), "kept on disk with Trakt's activity times");
  /* nothing new: nothing more asked; checked again 15 minutes on */
  unsigned v0 = tw_ver(); tick(60); CHECK(asked("/sync/last_activities") == 1, "not asked again within the 15 minutes");
  tick(900); CHECK(asked("/sync/last_activities") == 2, "asked again after 15 minutes"); answer("/sync/last_activities", 200, ACTS("A1", "E1"));
  CHECK(asked("/sync/watched/") == 2 && tw_ver() == v0, "the same activity: no list asked, nothing changed");
  /* episodes only changed: shows alone asked, the films kept */
  tick(901); answer("/sync/last_activities", 200, ACTS("A1", "E2")); CHECK(asked("/sync/watched/movies") == 1 && asked("/sync/watched/shows") == 2, "only shows asked again");
  answer("/sync/watched/shows", 200, "[{\"show\":{\"ids\":{\"imdb\":\"tt0000002\"}},\"seasons\":[{\"number\":1,\"episodes\":[{\"number\":1,\"last_watched_at\":\"2024-01-20T00:00:00.000Z\"}]}]}]"); pump();
  CHECK(tw_movie("tt0000001") && tw_ep("tt0000002", 1, 1) && !tw_ep("tt0000002", 1, 3), "films kept, shows replaced");
  /* a film plays: never asked meanwhile; closed: asked a few seconds after */
  playing = 1; tick(1000); CHECK(asked("/sync/last_activities") == 3, "never while a film plays (%d)", asked("/sync/last_activities"));
  playing = 0; tick(1); CHECK(asked("/sync/last_activities") == 3, "not at the very moment it closes"); tick(6); CHECK(asked("/sync/last_activities") == 4, "a few seconds after it closes");
  answer("/sync/last_activities", 200, ACTS("A1", "E2"));
  /* unmarked by hand: hidden here, never removed from Trakt; marked again: shown, nothing added (Trakt has it) */
  int n0 = nr; tw_mark_movie("tt0000001", 0); CHECK(!tw_movie("tt0000001") && nr == n0, "unmarked: hidden, nothing sent");
  tw_mark_movie("tt0000001", 1); CHECK(tw_movie("tt0000001") && nr == n0, "marked again: shown, nothing added (Trakt has it)");
  /* a film Trakt lacks: added */
  tw_mark_movie("tt0000003", 1); { const Req *p = lastPost(); CHECK(p && !strcmp(p->path, "/sync/history") && strstr(p->body, "\"imdb\":\"tt0000003\""), "added to the history: %s", p ? p->body : "(none)"); }
  { int before = asked("/sync/last_activities"); answer("/sync/history", 201, "{}"); tick(6); CHECK(asked("/sync/last_activities") == before + 1, "read back soon after"); answer("/sync/last_activities", 200, ACTS("A1", "E2")); }
  /* episodes: only those Trakt lacks are added; unmarked ones hidden */
  int se[6] = { 1, 1, 1, 2, 1, 3 }; tw_mark_eps("tt0000002", se, 3, 1); { const Req *p = lastPost(); CHECK(p && strstr(p->body, "\"number\":2") && strstr(p->body, "\"number\":3") && !strstr(p->body, "\"number\":1}"), "only the missing episodes: %s", p ? p->body : "(none)"); }
  n0 = nr; int one[2] = { 1, 1 }; tw_mark_eps("tt0000002", one, 1, 1); CHECK(nr == n0, "all on Trakt: nothing sent");
  tw_mark_eps("tt0000002", one, 1, 0); CHECK(!tw_ep("tt0000002", 1, 1) && nr == n0, "an episode unmarked: hidden, nothing sent"); tw_mark_eps("tt0000002", one, 1, 1); CHECK(tw_ep("tt0000002", 1, 1), "and shown again");
  /* tracking off: nothing told to Trakt; marks switched off: none shown */
  tracking = 0; n0 = nr; tw_mark_movie("tt0000004", 1); CHECK(nr == n0, "tracking off: nothing added"); tracking = 1;
  snprintf(marks, sizeof marks, "false"); CHECK(!tw_movie("tt0000001") && !tw_ep("tt0000002", 1, 1) && !tw_on(), "switched off: no marks"); snprintf(marks, sizeof marks, "true");
  /* paused positions (Continue Watching from Trakt): asked when Trakt's pauses move; one list, newest first */
  snprintf(cwSet, sizeof cwSet, "true"); tick(901);
  answer("/sync/last_activities", 200, "{\"movies\":{\"watched_at\":\"A1\",\"paused_at\":\"P1\"},\"episodes\":{\"watched_at\":\"E2\",\"paused_at\":\"Q1\"}}");
  CHECK(asked("/sync/playback") == 1 && asked("/sync/watched/") == 3, "pauses moved: only the paused positions asked (%d)", asked("/sync/playback"));
  answer("/sync/playback", 200, "[{\"id\":11,\"progress\":40.5,\"paused_at\":\"2024-03-01T00:00:00.000Z\",\"type\":\"movie\",\"movie\":{\"title\":\"Film P\",\"ids\":{\"imdb\":\"tt0000050\"}}},"
    "{\"id\":12,\"progress\":10,\"paused_at\":\"2024-04-01T00:00:00.000Z\",\"type\":\"episode\",\"episode\":{\"season\":2,\"number\":3},\"show\":{\"title\":\"Show P\",\"ids\":{\"imdb\":\"tt0000060\"}}},"
    "{\"id\":13,\"progress\":20,\"paused_at\":\"2024-01-01T00:00:00.000Z\",\"type\":\"episode\",\"episode\":{\"season\":2,\"number\":1},\"show\":{\"title\":\"Show P\",\"ids\":{\"imdb\":\"tt0000060\"}}},"
    "{\"id\":14,\"progress\":30,\"paused_at\":\"2024-01-01T00:00:00.000Z\",\"type\":\"movie\",\"movie\":{\"ids\":{\"tmdb\":9}}}]");
  { const TwPlay *pl; int np = tw_plays(&pl); CHECK(np == 3 && !strcmp(pl[0].vid, "tt0000060:2:3") && !strcmp(pl[0].type, "series") && !strcmp(pl[1].id, "tt0000050") && pl[1].pct > 0.40f && pl[1].pct < 0.41f, "three, newest first, IMDb only (%d: %s)", np, np ? pl[0].vid : "");
    const TwPlay *one = tw_play_of("tt0000060"); CHECK(one && !strcmp(one->vid, "tt0000060:2:3"), "a title's newest pause"); }
  tick(901); answer("/sync/last_activities", 200, "{\"movies\":{\"watched_at\":\"A1\",\"paused_at\":\"P1\"},\"episodes\":{\"watched_at\":\"E2\",\"paused_at\":\"Q1\"}}");
  CHECK(asked("/sync/playback") == 1, "pauses unchanged: not asked again");
  /* dismissed: each of the title's pauses deleted on Trakt, gone here, never back */
  tw_play_drop("tt0000060"); CHECK(asked("DELETE /sync/playback/12") == 1 && asked("DELETE /sync/playback/13") == 1 && !tw_play_of("tt0000060") && tw_play_of("tt0000050"), "dismissed: deleted on Trakt, gone here");
  tick(901); answer("/sync/last_activities", 200, "{\"movies\":{\"watched_at\":\"A1\",\"paused_at\":\"P2\"},\"episodes\":{\"watched_at\":\"E2\",\"paused_at\":\"Q2\"}}");
  answer("/sync/playback", 200, "[{\"id\":12,\"progress\":10,\"paused_at\":\"2024-04-01T00:00:00.000Z\",\"type\":\"episode\",\"episode\":{\"season\":2,\"number\":3},\"show\":{\"title\":\"Show P\",\"ids\":{\"imdb\":\"tt0000060\"}}}]");
  CHECK(!tw_play_of("tt0000060"), "a dismissed pause Trakt still lists: never shown again");
  snprintf(cwSet, sizeof cwSet, "false"); { const TwPlay *pl; CHECK(tw_plays(&pl) == 0 && !tw_play_of("tt0000050"), "switched off: none"); }
  /* another connection: everything goes */
  tw_mark_movie("tt0000001", 0); tw_forget(); CHECK(!tw_movie("tt0000001") && !tw_show_known("tt0000002") && !cJSON_GetObjectItem(root, "twHide") && !cJSON_GetObjectItem(root, "twPbDrop"), "forgotten: marks, hidden ones, dismissed pauses");
  { char p[300]; snprintf(p, sizeof p, "%s/trakt-watched.json", dataDir); CHECK(access(p, F_OK) != 0, "the disk copy deleted"); }
  /* an answer for the old connection is dropped; a failure leaves what shows */
  state = TRAKT_OFF; tick(30); CHECK(nr == nr, "(not connected: nothing)"); state = TRAKT_LINKED;
  tick(1); pump(); tick(6); answer("/sync/last_activities", 500, NULL); CHECK(!tw_movie("tt0000001"), "Trakt failing: nothing invented");
  snprintf(cmd, sizeof cmd, "rm -rf %s", dataDir); if (system(cmd)) {} cJSON_Delete(root);
  printf(fails ? "%d failed\n" : "all ok\n", fails); return fails ? 1 : 0;
}
