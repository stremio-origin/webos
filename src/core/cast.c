/* Cast receiver (cast.h). Everything happens in cast_tick on the main thread over non-blocking sockets: the
   discovery answer, the WebSocket handshake and frames, pairing, the commands and the state sent back. */
#include "core/cast.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>

#ifdef MSG_NOSIGNAL
#define SEND_FLAGS MSG_NOSIGNAL
#else
#define SEND_FLAGS 0
#endif
#define MAXC 4
#define INCAP 65536
#define OUTCAP 32768
#define MAXTOK 8

typedef struct { int fd, ws, auth; char *in; int inN; char *out; int outN; int lastS; double lastAt, heardAt, pingAt; char *msg; int msgN, msgOp; unsigned trHash; } Client; /* trHash: the tracks last told */ /* msg: a message arriving in fragments */
static CastHooks H; static int udp = -1, lis = -1, on; static Client C[MAXC];
static char tvId[40], mac[24], code[8]; static int codeFor = -1, wrong; static double codeAt, lockUntil;

static double mono(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static void rnd(unsigned char *b, int n) {
  FILE *f = fopen("/dev/urandom", "rb"); int got = f ? (int)fread(b, 1, (size_t)n, f) : 0; if (f) fclose(f);
  for (int i = got; i < n; i++) b[i] = (unsigned char)(rand() ^ (int)(mono() * 1e6)); }
static void hex(const unsigned char *b, int n, char *out) { for (int i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", b[i]); }
static void nonblock(int fd) { int fl = fcntl(fd, F_GETFL, 0); fcntl(fd, F_SETFL, fl | O_NONBLOCK); }

/* ---- SHA-1 and base64, for the WebSocket handshake only ---- */
static uint32_t rol(uint32_t v, int s) { return (v << s) | (v >> (32 - s)); }
static void sha1(const unsigned char *msg, size_t len, unsigned char out[20]) {
  uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
  size_t ml = len + 9, nb = (ml + 63) / 64; unsigned char *m = calloc(nb, 64); if (!m) return;
  memcpy(m, msg, len); m[len] = 0x80; uint64_t bits = (uint64_t)len * 8; for (int i = 0; i < 8; i++) m[nb * 64 - 1 - i] = (unsigned char)(bits >> (8 * i));
  for (size_t b = 0; b < nb; b++) { uint32_t w[80]; const unsigned char *p = m + b * 64;
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) { uint32_t f, k;
      if (i < 20) { f = (bb & c) | (~bb & d); k = 0x5A827999; } else if (i < 40) { f = bb ^ c ^ d; k = 0x6ED9EBA1; }
      else if (i < 60) { f = (bb & c) | (bb & d) | (c & d); k = 0x8F1BBCDC; } else { f = bb ^ c ^ d; k = 0xCA62C1D6; }
      uint32_t t = rol(a, 5) + f + e + k + w[i]; e = d; d = c; c = rol(bb, 30); bb = a; a = t; }
    h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e; }
  free(m); for (int i = 0; i < 5; i++) { out[4 * i] = h[i] >> 24; out[4 * i + 1] = h[i] >> 16; out[4 * i + 2] = h[i] >> 8; out[4 * i + 3] = h[i]; }
}
static void b64(const unsigned char *in, int n, char *out) {
  static const char *A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; int o = 0;
  for (int i = 0; i < n; i += 3) { uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < n ? (uint32_t)in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
    out[o++] = A[v >> 18 & 63]; out[o++] = A[v >> 12 & 63]; out[o++] = i + 1 < n ? A[v >> 6 & 63] : '='; out[o++] = i + 2 < n ? A[v & 63] : '='; }
  out[o] = 0;
}

/* ---- the TV's id, its MAC (for waking it), the phones' tokens ---- */
static void ids(void) {
  const char *id = H.get("castId", ""); if (strlen(id) == 36) snprintf(tvId, sizeof tvId, "%s", id);
  else { unsigned char b[16]; char x[33]; rnd(b, 16); b[6] = (b[6] & 0x0f) | 0x40; b[8] = (b[8] & 0x3f) | 0x80; hex(b, 16, x);
    snprintf(tvId, sizeof tvId, "%.8s-%.4s-%.4s-%.4s-%.12s", x, x + 8, x + 12, x + 16, x + 20); H.set("castId", tvId); }
  mac[0] = 0; struct ifaddrs *ifs = NULL; if (getifaddrs(&ifs) != 0) return;
  for (struct ifaddrs *i = ifs; i; i = i->ifa_next) { if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || (i->ifa_flags & IFF_LOOPBACK) || !(i->ifa_flags & IFF_UP)) continue;
    char p[128]; snprintf(p, sizeof p, "/sys/class/net/%s/address", i->ifa_name); FILE *f = fopen(p, "r"); /* (Linux: webOS, Android TV) */
    if (f) { if (fgets(mac, sizeof mac, f)) mac[strcspn(mac, "\r\n")] = 0; fclose(f); }
    if (mac[0]) break; }
  freeifaddrs(ifs);
}
static int token_ok(const char *t) {
  if (!t || strlen(t) != 32) return 0; const char *l = H.get("castTokens", "");
  for (const char *p = l; (p = strstr(p, t)); p += 32) if ((p == l || p[-1] == ',') && (p[32] == 0 || p[32] == ',')) return 1;
  return 0;
}
static void token_new(char out[33]) {
  unsigned char b[16]; rnd(b, 16); hex(b, 16, out);
  const char *l = H.get("castTokens", ""); char nl[MAXTOK * 33 + 8]; snprintf(nl, sizeof nl, "%s", out);
  int n = 1; for (const char *p = l; *p && n < MAXTOK; ) { size_t k = strcspn(p, ","); if (k == 32) { size_t at = strlen(nl); snprintf(nl + at, sizeof nl - at, ",%.32s", p); n++; } p += k; if (*p == ',') p++; } /* newest first, the oldest let go */
  H.set("castTokens", nl);
}

/* ---- sending ---- */
static void flush(Client *c) {
  while (c->outN > 0) { ssize_t k = send(c->fd, c->out, (size_t)c->outN, SEND_FLAGS); if (k <= 0) return; memmove(c->out, c->out + k, (size_t)(c->outN - k)); c->outN -= (int)k; } }
static void put(Client *c, const void *b, int n) { if (c->outN + n > OUTCAP) return; /* (a phone not reading: dropped, never waited on) */ memcpy(c->out + c->outN, b, (size_t)n); c->outN += n; flush(c); }
static void send_frame(Client *c, int op, const char *s, int n) {
  unsigned char h[10]; int hl = 2; h[0] = 0x80 | op;
  if (n < 126) h[1] = (unsigned char)n; else if (n < 65536) { h[1] = 126; h[2] = n >> 8; h[3] = n & 255; hl = 4; } else return;
  if (c->outN + hl + n > OUTCAP) return; put(c, h, hl); put(c, s, n);
}
static void send_close(Client *c, int code) { char b[2] = { (char)(code >> 8), (char)(code & 255) }; send_frame(c, 8, b, 2); }
static void send_json(Client *c, cJSON *j) { char *s = cJSON_PrintUnformatted(j); if (s) { send_frame(c, 1, s, (int)strlen(s)); free(s); } cJSON_Delete(j); }
static void send_t(Client *c, const char *t) { cJSON *j = cJSON_CreateObject(); cJSON_AddStringToObject(j, "t", t); send_json(c, j); }
static void send_err(Client *c, const char *msg) { cJSON *j = cJSON_CreateObject(); cJSON_AddStringToObject(j, "t", "error"); cJSON_AddStringToObject(j, "msg", msg); send_json(c, j); }

void cast_error(const char *msg, const char *code, const char *id, int back) {
  for (int i = 0; i < MAXC; i++) if (C[i].fd >= 0 && C[i].auth) { cJSON *j = cJSON_CreateObject(); cJSON_AddStringToObject(j, "t", "error"); cJSON_AddStringToObject(j, "msg", msg ? msg : "");
    if (code && *code) cJSON_AddStringToObject(j, "code", code); if (id && *id) cJSON_AddStringToObject(j, "id", id); cJSON_AddNumberToObject(j, "back", back ? 1 : 0); send_json(&C[i], j); C[i].lastS = -1; } /* (the state right after it, at once) */
}
int cast_phones(void) { int n = 0; for (int i = 0; i < MAXC; i++) if (C[i].fd >= 0 && C[i].auth) n++; return n; }
static void pairing_end(void) { if (codeFor >= 0) { codeFor = -1; code[0] = 0; if (H.pairing) H.pairing(""); } }
static void drop(Client *c) {
  int was = c->auth, idx = (int)(c - C); flush(c); close(c->fd); c->fd = -1; c->auth = c->ws = 0; c->inN = c->outN = 0;
  if (codeFor == idx) pairing_end();
  if (was && H.connected) H.connected(cast_phones());
}

static unsigned fnv(const char *s) { unsigned h = 2166136261u; for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u; return h | 1; }
/* ---- the phone's messages ---- */
static const char *js(const cJSON *o, const char *k) { const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k); return cJSON_IsString(v) ? v->valuestring : ""; }
static double jn(const cJSON *o, const char *k) { const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k); return cJSON_IsNumber(v) ? v->valuedouble : 0; }
static void on_msg(Client *c, const char *s) {
  cJSON *j = cJSON_Parse(s); if (!j) return; const char *t = js(j, "t"); double now = mono();
  if (!strcmp(t, "pair")) {
    const char *cd = js(j, "code");
    if (now < lockUntil) send_err(c, "locked");
    else if (!*cd) { unsigned char b[2]; rnd(b, 2); snprintf(code, sizeof code, "%04d", ((b[0] << 8) | b[1]) % 10000); codeFor = (int)(c - C); codeAt = now; wrong = 0;
      fprintf(stderr, "[cast] pairing asked%s%s\n", getenv("SB_CAST_DEBUG") ? ", code " : "", getenv("SB_CAST_DEBUG") ? code : ""); /* (the code itself only in development) */ if (H.pairing) H.pairing(code); }
    else if (codeFor == (int)(c - C) && !strcmp(cd, code)) { char tok[33]; token_new(tok); pairing_end(); wrong = 0;
      cJSON *r = cJSON_CreateObject(); cJSON_AddStringToObject(r, "t", "paired"); cJSON_AddStringToObject(r, "token", tok); send_json(c, r); fprintf(stderr, "[cast] paired\n"); }
    else { if (++wrong >= 3) { lockUntil = now + 60; pairing_end(); wrong = 0; send_err(c, "locked"); } else send_err(c, "wrong code"); }
  } else if (!strcmp(t, "hello")) {
    if (token_ok(js(j, "token"))) { c->auth = 1; c->lastS = -1; cJSON *r = cJSON_CreateObject(); cJSON_AddStringToObject(r, "t", "ready"); cJSON_AddStringToObject(r, "name", H.name ? H.name() : "TV");
      if (H.playbest) { cJSON *cp = cJSON_AddArrayToObject(r, "caps"); cJSON_AddItemToArray(cp, cJSON_CreateString("best")); if (H.previews) cJSON_AddItemToArray(cp, cJSON_CreateString("previews")); } /* (the phone sends playbest only to a TV that says so) */
      send_json(c, r);
      fprintf(stderr, "[cast] phone connected\n"); if (H.connected) H.connected(cast_phones()); }
    else { send_t(c, "denied"); drop(c); }
  } else if (c->auth) {
    if (!strcmp(t, "play")) {
      CastSub subs[16]; int ns = 0; const cJSON *sl = cJSON_GetObjectItemCaseSensitive(j, "subs"), *e;
      cJSON_ArrayForEach(e, sl) { if (ns < 16 && *js(e, "url")) { subs[ns].url = js(e, "url"); subs[ns].lang = js(e, "lang"); ns++; } }
      const cJSON *m = cJSON_GetObjectItemCaseSensitive(j, "meta");
      CastPlay p = { js(j, "url"), js(j, "title"), js(m, "type"), js(m, "id"), "", js(m, "poster"), js(m, "background"), js(m, "logo"), jn(j, "pos"), (int)jn(m, "season"), (int)jn(m, "episode"), subs, ns };
      int chan = !strcmp(p.type, "tv") || !strcmp(p.type, "channel"); /* (a channel: no stream needed, the TV finds its own) */
      if ((*p.url || (chan && *p.id)) && H.play) { fprintf(stderr, "[cast] play \"%s\" at %.0f s, %d subtitles\n", p.title, p.pos, ns); H.play(&p); }
      else send_err(c, "no url");
    } else if (!strcmp(t, "playbest")) { /* the phone names the title; this TV picks its stream (its own best-stream rules: what this TV plays best) */
      const cJSON *m = cJSON_GetObjectItemCaseSensitive(j, "meta");
      CastPlay p = { "", js(j, "title"), js(m, "type"), js(m, "id"), js(m, "vid"), js(m, "poster"), js(m, "background"), js(m, "logo"), jn(j, "pos"), (int)jn(m, "season"), (int)jn(m, "episode"), NULL, 0 };
      if (*p.id && H.playbest) { fprintf(stderr, "[cast] playbest \"%s\" (%s) at %.0f s\n", p.title, *p.vid ? p.vid : p.id, p.pos); H.playbest(&p); }
      else send_err(c, "no id");
    } else if (!strcmp(t, "pause")) H.cmd(1, 0);
    else if (!strcmp(t, "resume")) H.cmd(2, 0);
    else if (!strcmp(t, "seek")) H.cmd(3, jn(j, "pos"));
    else if (!strcmp(t, "stop")) H.cmd(4, 0);
    else if (!strcmp(t, "playnow")) H.cmd(5, 0); /* (the seek previews' wait: the film starts now, as OK on the TV) */
    else if (!strcmp(t, "account")) { const cJSON *u = cJSON_GetObjectItemCaseSensitive(j, "user"); char *us = u ? cJSON_PrintUnformatted(u) : NULL;
      if (H.account) H.account(js(j, "authKey"), us ? us : ""); free(us); }
    else if (!strcmp(t, "settings")) { static const char *K[] = { "autoBest", "subsLang", "defaultAudioTrack", "enableDefaultAudioTrack", "subtitleSize", "subsColor", "subsBgStyle",
        "subsOutlineColor", "subsAssStyle", "subsPosPct", "enableNextVideo", "bingeWatch", "seekStep", "aspectMode", "bgTrailer", "bgTrailerVol", "uiSound", "uiSoundVol", "accent", "ssDelay", "uiLang", "subsFont", "subsFontAr", "subsBright", "seekPreviews" };
      /* (every setting the phone and the TV share, the language too; never one of this TV's own: its USB drive, Dolby Vision conversion) */
      const cJSON *pr = cJSON_GetObjectItemCaseSensitive(j, "prefs");
      for (int i = 0; i < (int)(sizeof K / sizeof *K); i++) { const cJSON *v = cJSON_GetObjectItemCaseSensitive(pr, K[i]); if (cJSON_IsString(v) && strlen(v->valuestring) < 64 && H.pref) H.pref(K[i], v->valuestring); } }
    else if (!strcmp(t, "sub") || !strcmp(t, "audio")) { if (H.track) { H.track(t, js(j, "id"), 0); c->trHash = 0; } } /* (the new choice told back at once) */
    else if (!strcmp(t, "tracks?")) { char *tr = H.tracks ? H.tracks() : NULL; /* asked: the tracks now, or empty lists when nothing plays */
      const char *empty = "{\"t\":\"tracks\",\"subs\":[],\"audio\":[],\"sub\":\"off\",\"aud\":\"\"}"; const char *m = tr ? tr : empty; send_frame(c, 1, m, (int)strlen(m)); c->trHash = tr ? fnv(tr) : 0; free(tr); }
    else if (!strcmp(t, "subdelay")) { if (H.track) { H.track(t, "", (int)jn(j, "ms")); c->trHash = 0; } }
    else if (!strcmp(t, "subsync")) { if (H.track) { H.track(t, "", 0); c->trHash = 0; } } /* (the subtitles synced to the speech, as the player's Sync; its progress told with the tracks) */
    else if (!strcmp(t, "key")) { static const char *K[] = { "up", "down", "left", "right", "ok", "back", "home", "play", "pause" }; const char *k = js(j, "k");
      for (int i = 0; i < 9; i++) if (!strcmp(k, K[i])) { if (H.key) H.key(CAST_K_UP + i); break; } }
  } /* (anything else before hello: no answer) */
  cJSON_Delete(j);
}

/* ---- the handshake and the frames ---- */
static int lan(const struct sockaddr_in *a) { uint32_t ip = ntohl(a->sin_addr.s_addr);
  return (ip >> 24) == 10 || (ip >> 20) == 0xAC1 || (ip >> 16) == 0xC0A8 || (ip >> 16) == 0xA9FE || (ip >> 24) == 127; }
static void handshake(Client *c) {
  c->in[c->inN] = 0; char *end = strstr(c->in, "\r\n\r\n"); if (!end) { if (c->inN > 8000) drop(c); return; }
  char key[128] = "", ver[16] = ""; int up = 0; for (char *l = c->in; l && l < end; l = strstr(l, "\r\n") ? strstr(l, "\r\n") + 2 : NULL) {
    if (!strncasecmp(l, "Sec-WebSocket-Key:", 18)) { const char *v = l + 18; while (*v == ' ') v++; snprintf(key, sizeof key, "%.*s", (int)strcspn(v, "\r\n "), v); }
    else if (!strncasecmp(l, "Sec-WebSocket-Version:", 22)) { const char *v = l + 22; while (*v == ' ') v++; snprintf(ver, sizeof ver, "%.*s", (int)strcspn(v, "\r\n "), v); }
    else if (!strncasecmp(l, "Upgrade:", 8)) { const char *v = l + 8; while (*v == ' ') v++; up = !strncasecmp(v, "websocket", 9); } }
  if (strncmp(c->in, "GET ", 4) || !up) { const char *r = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"; put(c, r, (int)strlen(r)); drop(c); return; } /* (RFC 6455 4.2.1) */
  if (strcmp(ver, "13")) { const char *r = "HTTP/1.1 426 Upgrade Required\r\nSec-WebSocket-Version: 13\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"; put(c, r, (int)strlen(r)); drop(c); return; }
  if (!key[0]) { const char *r = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n"; put(c, r, (int)strlen(r)); drop(c); return; }
  char cat[200]; snprintf(cat, sizeof cat, "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key); unsigned char d[20]; sha1((const unsigned char *)cat, strlen(cat), d);
  char acc[40]; b64(d, 20, acc); char r[256]; int n = snprintf(r, sizeof r, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", acc);
  put(c, r, n); c->ws = 1; int used = (int)(end + 4 - c->in); memmove(c->in, c->in + used, (size_t)(c->inN - used)); c->inN -= used;
}
static void frames(Client *c) {
  while (c->fd >= 0 && c->inN >= 2) {
    unsigned char *b = (unsigned char *)c->in; int fin = b[0] & 128, op = b[0] & 15, masked = b[1] & 128; uint64_t n = b[1] & 127; int hl = 2;
    if (b[0] & 0x70) { send_close(c, 1002); drop(c); return; } /* (no extensions were agreed) */
    if (n == 126) { if (c->inN < 4) return; n = (uint64_t)b[2] << 8 | b[3]; hl = 4; } else if (n == 127) { send_close(c, 1009); drop(c); return; }
    if (!masked) { send_close(c, 1002); drop(c); return; } /* a client's frames are masked (5.1) */
    if (n > INCAP - 16 || (op >= 8 && (n > 125 || !fin))) { send_close(c, n > INCAP - 16 ? 1009 : 1002); drop(c); return; }
    if ((uint64_t)c->inN < hl + 4 + n) return;
    unsigned char *mk = b + hl, *p = b + hl + 4; for (uint64_t i = 0; i < n; i++) p[i] ^= mk[i & 3];
    char save = (char)p[n]; p[n] = 0; c->heardAt = mono();
    if (op == 8) { if (n >= 2) send_frame(c, 8, (const char *)p, 2); else send_frame(c, 8, "", 0); drop(c); return; } /* the close echoed */
    else if (op == 9) send_frame(c, 10, (const char *)p, (int)n);
    else if (op == 1 || op == 2 || op == 0) { /* data: whole, or its fragments joined (5.4); control frames may come between them */
      if (op != 0) { c->msgN = 0; c->msgOp = op; } else if (!c->msgOp) { send_close(c, 1002); drop(c); return; }
      if (!c->msg) c->msg = malloc(INCAP + 1);
      if (!c->msg || c->msgN + (int)n > INCAP) { send_close(c, 1009); drop(c); return; }
      memcpy(c->msg + c->msgN, p, (size_t)n); c->msgN += (int)n;
      if (fin) { int mo = c->msgOp; c->msgOp = 0; c->msg[c->msgN] = 0; if (mo == 1) on_msg(c, c->msg); /* (binary: not used) */ } }
    else if (op != 10) { send_close(c, 1002); drop(c); return; }
    p[n] = (unsigned char)save; int used = hl + 4 + (int)n; if (c->fd < 0) return; memmove(c->in, c->in + used, (size_t)(c->inN - used)); c->inN -= used; }
}

/* ---- the state, about once a second while something plays, at once on a change ---- */
/* the subtitles and audio: told when they change (a list arriving, a pick here or on the phone), checked a few times a second */
static void tracks(double now) {
  static double at; if (now - at < 0.3 || !H.tracks) return; at = now; char *s = NULL; unsigned h = 0;
  for (int i = 0; i < MAXC; i++) { Client *c = &C[i]; if (c->fd < 0 || !c->auth) continue;
    if (!s) { s = H.tracks(); if (!s) break; h = fnv(s); }
    if (c->trHash != h) { c->trHash = h; send_frame(c, 1, s, (int)strlen(s)); } }
  free(s);
}
static void states(double now) {
  static const char *S[] = { "idle", "loading", "playing", "paused", "ended", "error", "choosing" }; char title[256]; double pos, du; int s = -1;
  char ty[16] = "", id[128] = "", vid[160] = ""; /* (SB_ID / SB_VID: a long addon id whole, #81) */ int any = 0; for (int i = 0; i < MAXC; i++) if (C[i].fd >= 0 && C[i].auth) any = 1;
  if (any && H.ident) { static char lastId[200]; char cur[200]; H.ident(ty, sizeof ty, id, sizeof id, vid, sizeof vid); snprintf(cur, sizeof cur, "%s|%s", id, vid); /* what plays: asked once a pass, only with a phone connected */
    if (strcmp(cur, lastId)) { snprintf(lastId, sizeof lastId, "%s", cur); for (int k = 0; k < MAXC; k++) C[k].lastS = -1; } } /* (another title or episode: told at once) */
  for (int i = 0; i < MAXC; i++) { Client *c = &C[i]; if (c->fd < 0 || !c->auth) continue;
    if (s < 0) { s = H.state ? H.state(&pos, &du, title, sizeof title) : CAST_IDLE; if (s < 0 || s > 6) s = 0; }
    if (s == c->lastS && (s == CAST_IDLE || s == CAST_ENDED || s == CAST_ERROR || now - c->lastAt < 1.0)) continue;
    c->lastS = s; c->lastAt = now; cJSON *j = cJSON_CreateObject(); cJSON_AddStringToObject(j, "t", "state"); cJSON_AddStringToObject(j, "s", S[s]);
    cJSON_AddNumberToObject(j, "pos", (int)(pos * 10) / 10.0); cJSON_AddNumberToObject(j, "dur", (int)du); cJSON_AddStringToObject(j, "title", title);
    if (id[0]) { cJSON_AddStringToObject(j, "type", ty); cJSON_AddStringToObject(j, "id", id); cJSON_AddStringToObject(j, "vid", vid); } /* (what plays: the phone's remote follows a change made on the TV) */
    if (id[0] && H.chips) { cJSON *ca = cJSON_AddArrayToObject(j, "chips"); H.chips(ca); } /* (the player's own info line: the remote shows it as the TV does) */
    if (id[0] && H.sync) { char sst[120] = ""; int sr = H.sync(sst, sizeof sst); cJSON_AddNumberToObject(j, "syncing", sr > 0); cJSON_AddStringToObject(j, "syncSt", sr >= 0 ? sst : ""); } /* (Sync's progress: with the state, never the whole tracks again) */
    if (id[0] && H.previews) { int pc = 0, tg = 0, eta = 0; if (H.previews(&pc, &tg, &eta)) { cJSON *pv = cJSON_AddObjectToObject(j, "previews"); cJSON_AddNumberToObject(pv, "pct", pc); cJSON_AddNumberToObject(pv, "target", tg); cJSON_AddNumberToObject(pv, "eta", eta); } } /* (the film waits for its seek previews: the phone shows how far, and "playnow" starts it) */
    if (id[0] && H.p2p) { const char *pl = H.p2p(); if (pl && *pl) cJSON_AddStringToObject(j, "p2p", pl); } /* (a torrent: its peers and speed, as the player's corner) */
    if (id[0] && H.info) { char inf[96] = ""; H.info(inf, sizeof inf); if (inf[0]) cJSON_AddStringToObject(j, "info", inf); } /* (the stream in a few words, under the remote's title) */
    send_json(c, j); }
}

/* discovery's port shareable: a socket a closed app leaves behind (the Mac has kept some) never holds it from the next */
#ifdef SO_REUSEPORT
#define REUSEPORT(fd) do { int one_ = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one_, sizeof one_); } while (0)
#else
#define REUSEPORT(fd) do { } while (0)
#endif
int cast_start(const CastHooks *h) {
  if (on) return 1; H = *h; for (int i = 0; i < MAXC; i++) C[i].fd = -1; ids();
  int one = 1; struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY);
  udp = socket(AF_INET, SOCK_DGRAM, 0);
  if (udp >= 0) { setsockopt(udp, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one); setsockopt(udp, SOL_SOCKET, SO_BROADCAST, &one, sizeof one); REUSEPORT(udp); a.sin_port = htons(CAST_UDP_PORT);
    if (bind(udp, (struct sockaddr *)&a, sizeof a) != 0) { close(udp); udp = -1; } else nonblock(udp); }
  if (udp < 0) { /* the port held (an app before this one that has not let go of it): this network's own address only, so a phone asking each address still finds it (its broadcast does not reach it then) */
    struct ifaddrs *ifs = NULL; if (!getifaddrs(&ifs)) { for (struct ifaddrs *f = ifs; f && udp < 0; f = f->ifa_next) { if (!f->ifa_addr || f->ifa_addr->sa_family != AF_INET || (f->ifa_flags & IFF_LOOPBACK)) continue;
        struct sockaddr_in b = *(struct sockaddr_in *)f->ifa_addr; if (!lan(&b)) continue; b.sin_port = htons(CAST_UDP_PORT); int u = socket(AF_INET, SOCK_DGRAM, 0); if (u < 0) continue;
        setsockopt(u, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one); REUSEPORT(u); if (bind(u, (struct sockaddr *)&b, sizeof b) == 0) { nonblock(u); udp = u; fprintf(stderr, "[cast] discovery: the port is held, this address only\n"); } else close(u); }
      freeifaddrs(ifs); } }
  lis = socket(AF_INET, SOCK_STREAM, 0);
  if (lis >= 0) { setsockopt(lis, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one); a.sin_port = htons(CAST_WS_PORT);
    if (bind(lis, (struct sockaddr *)&a, sizeof a) != 0 || listen(lis, 4) != 0) { close(lis); lis = -1; } else nonblock(lis); }
  on = udp >= 0 || lis >= 0;
  fprintf(stderr, "[cast] receiver: discovery %s, control %s, mac %s\n", udp >= 0 ? "on" : "off", lis >= 0 ? "on" : "off", mac[0] ? "known" : "unknown");
  return lis >= 0;
}
void cast_tick(void) {
  if (!on) return; double now = mono();
  if (udp >= 0) for (int k = 0; k < 8; k++) { char b[64]; struct sockaddr_in from; socklen_t fl = sizeof from;
    ssize_t n = recvfrom(udp, b, sizeof b - 1, 0, (struct sockaddr *)&from, &fl); if (n <= 0) break; b[n] = 0;
    if (strncmp(b, "SBORIGIN?", 9) || !lan(&from)) continue;
    char r[400]; int rn = snprintf(r, sizeof r, "{\"id\":\"%s\",\"name\":\"%s\",\"plat\":\"%s\",\"ver\":\"%s\",\"port\":%d,\"mac\":\"%s\",\"paired\":%s}\n",
      tvId, H.name ? H.name() : "TV", H.plat, H.ver, CAST_WS_PORT, mac, H.get("castTokens", "")[0] ? "true" : "false");
    sendto(udp, r, (size_t)rn, 0, (struct sockaddr *)&from, fl); }
  if (lis >= 0) for (int k = 0; k < 2; k++) { struct sockaddr_in from; socklen_t fl = sizeof from; int fd = accept(lis, (struct sockaddr *)&from, &fl); if (fd < 0) break;
    int slot = -1; for (int i = 0; i < MAXC; i++) if (C[i].fd < 0) { slot = i; break; }
    if (slot < 0 || !lan(&from)) { close(fd); continue; } /* (LAN only; four phones at most) */
    nonblock(fd); int one = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
#ifdef SO_NOSIGPIPE
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    Client *c = &C[slot]; if (!c->in) c->in = malloc(INCAP + 1); if (!c->out) c->out = malloc(OUTCAP); if (!c->in || !c->out) { close(fd); continue; }
    c->fd = fd; c->ws = c->auth = 0; c->inN = c->outN = 0; c->lastS = -1; c->lastAt = 0; c->heardAt = c->pingAt = mono(); c->msgN = c->msgOp = 0; }
  for (int i = 0; i < MAXC; i++) { Client *c = &C[i]; if (c->fd < 0) continue;
    for (;;) { if (c->inN >= INCAP) { drop(c); break; } ssize_t n = recv(c->fd, c->in + c->inN, (size_t)(INCAP - c->inN), 0);
      if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) { drop(c); break; } if (n < 0) break; c->inN += (int)n; c->heardAt = now;
      if (!c->ws) handshake(c); if (c->fd >= 0 && c->ws) frames(c); if (c->fd < 0) break; }
    if (c->fd < 0) continue;
    /* a phone gone without a word (out of Wi-Fi, asleep): pinged after 20 s of silence, let go after 60 (a socket never upgraded, after 10) */
    if (!c->ws && now - c->heardAt > 10) { drop(c); continue; }
    if (c->ws && now - c->heardAt > 60) { send_close(c, 1001); drop(c); continue; }
    if (c->ws && now - c->heardAt > 20 && now - c->pingAt > 20) { c->pingAt = now; send_frame(c, 9, "", 0); }
    flush(c); }
  if (codeFor >= 0 && now - codeAt > 120) pairing_end(); /* a code not typed in two minutes goes */
  states(now); tracks(now);
}
void cast_stop(void) { for (int i = 0; i < MAXC; i++) if (C[i].fd >= 0) drop(&C[i]); if (udp >= 0) close(udp); if (lis >= 0) close(lis); udp = lis = -1; on = 0; }
