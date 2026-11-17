#include "core/trakt.h"
#include "core/store.h"
#include "core/httpd.h"
#include "cJSON.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "sbkeys.h" /* the app's own Trakt app, from the private keys file (tools/build.sh) */

#define TRAKT_API "https://api.trakt.tv"
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static char key[80], builtin[80], keyPath[1200]; static int inited; static unsigned gen;
static char phoneKey[80]; static unsigned phoneKeyGen, seenKeyGen; /* an id saved from the phone (server thread) */
static int key_ok(const char *k) { size_t n = strlen(k); if (n < 32 || n > 72) return 0; for (const char *p = k; *p; p++) if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_') return 0; return 1; } /* a client id: 64 hex digits (old apps) or 43 URL-safe letters (new ones) */
static void strip_ws(const char *in, char *out, size_t cap) { size_t o = 0; for (; *in && o + 1 < cap; in++) if (!isspace((unsigned char)*in)) out[o++] = *in; out[o] = 0; }
static void headers(const char *k, char *out, size_t cap) { snprintf(out, cap, "Content-Type: application/json\ntrakt-api-version: 2\ntrakt-api-key: %s", k); }
static int write_file(const char *p, const char *data, size_t len) {
  char tmp[1300]; snprintf(tmp, sizeof tmp, "%s.tmp", p); FILE *f = fopen(tmp, "wb"); if (!f) return 0;
  size_t w = fwrite(data, 1, len, f); fclose(f); if (w != len) { remove(tmp); return 0; } return rename(tmp, p) == 0;
}
/* the phone page's Save (server thread): the id is tried on Trakt before it is kept */
static int key_from_phone(const char *raw) {
  char k[80]; strip_ws(raw, k, sizeof k); if (!key_ok(k)) return 0;
  char h[200]; headers(k, h, sizeof h); size_t len = 0; int st = 0; unsigned char *b = net_fetch_sync_hdr(TRAKT_API "/movies/trending?limit=1", h, &len, &st, 12000); free(b);
  if (st != 200) return 0;
  if (!write_file(keyPath, k, strlen(k))) return 0;
  pthread_mutex_lock(&mu); snprintf(phoneKey, sizeof phoneKey, "%s", k); phoneKeyGen++; pthread_mutex_unlock(&mu);
  return 1;
}
static void drain(void) { /* an id that came from the phone since the last look */
  if (!inited) trakt_init();
  pthread_mutex_lock(&mu); int kg = phoneKeyGen != seenKeyGen; if (kg) { seenKeyGen = phoneKeyGen; snprintf(key, sizeof key, "%s", phoneKey); } pthread_mutex_unlock(&mu);
  if (kg) gen++;
}
void trakt_init(void) {
  if (inited) return; inited = 1;
  snprintf(keyPath, sizeof keyPath, "%s/trakt.key", store_data_dir());
  FILE *f = fopen(keyPath, "rb"); if (f) { char t[200]; size_t n = fread(t, 1, sizeof t - 1, f); t[n] = 0; fclose(f); strip_ws(t, key, sizeof key); if (!key_ok(key)) key[0] = 0; }
  httpd_set_trakt_handler(key_from_phone);
  if (SBK_TRAKT_CLIENT_ID_LEN < sizeof builtin) { for (int i = 0; i < SBK_TRAKT_CLIENT_ID_LEN; i++) builtin[i] = (char)(SBK_TRAKT_CLIENT_ID[i] ^ SBK_TRAKT_CLIENT_ID_PAD[i % 32]); builtin[SBK_TRAKT_CLIENT_ID_LEN] = 0; if (!key_ok(builtin)) builtin[0] = 0; }
}
int trakt_builtin(void) { if (!inited) trakt_init(); return builtin[0] != 0; }
int trakt_has_key(void) { drain(); return key[0] || builtin[0]; }
void trakt_key_clear(void) { drain(); remove(keyPath); key[0] = 0; gen++; }
unsigned trakt_generation(void) { drain(); return gen; }
const char *trakt_client_id(void) { drain(); return key[0] ? key : builtin; }
/* Trakt's "too many requests" (429): a minute with no calls, the reviews show the other sources meanwhile */
static time_t pausedTo;
typedef struct { NetCb cb; void *ud; } Fwd;
static void on_answer(int st, const unsigned char *body, size_t len, void *ud) {
  Fwd f = *(Fwd *)ud; free(ud); if (st == 429) { pausedTo = time(NULL) + 60; fprintf(stderr, "[trakt] 429: no calls for a minute\n"); }
  f.cb(st, body, len, f.ud);
}
unsigned trakt_request(const char *path, NetCb cb, void *ud) {
  drain(); const char *k = key[0] ? key : builtin; if (!k[0] || time(NULL) < pausedTo) return 0; /* a user's own id first: their own request quota */
  char url[600], h[200]; snprintf(url, sizeof url, TRAKT_API "%s", path); headers(k, h, sizeof h);
  Fwd *f = malloc(sizeof *f); if (!f) return 0; f->cb = cb; f->ud = ud;
  unsigned id = net_get_hdr(url, h, on_answer, f); if (!id) free(f); return id;
}
