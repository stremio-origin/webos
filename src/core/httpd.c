#include "core/httpd.h"
#include "core/app.h"
#include "core/i18n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <poll.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0   /* the Mac: SO_NOSIGPIPE on each socket instead */
#endif
#define IDLE_SEC (15 * 60)
#define MAX_REQ 4096     /* a request line and its headers; anything longer is not one of ours */
#define MAX_CONN 8

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static int alive, stopReq, nconn; static double deadline;
static char tok[17];
static struct { int seq; char text[400]; } typed;
static unsigned hits;
static int (*keyFn)(const char *key), (*traktFn)(const char *key);

/* the pages speak the app's language: its translations of these, taken when a screen asks for the server
   (the server thread never reads the dictionaries, which a language change rebuilds) */
static const char *PK[] = { "Search on TV", "Search on your TV", "What you type shows up on the TV as you type.", "Film or series", "Sent", "Can\xe2\x80\x99t reach the TV",
  "Titles and descriptions", "Titles and descriptions in your language", "Paste your TMDB API key (or API Read Access Token). Get one at", "It stays on your TV.",
  "API key", "Save", "Checking\xe2\x80\xa6", "Saved. You can close this page.", "TMDB didn\xe2\x80\x99t accept this key.",
  "Ratings and reviews", "Ratings and reviews from Trakt", "Paste your Trakt Client ID. To get one:", "open", "sign in, then Settings \xe2\x80\xba Your API Apps \xe2\x80\xba New Application. Any name; as the redirect URI paste",
  "then Save and copy the Client ID.", "Client ID", "Trakt didn\xe2\x80\x99t accept this id.", "This code has changed. Scan the code on your TV again." };
#define NPK (int)(sizeof PK / sizeof PK[0])
static struct { char lang[8]; int rtl; char *t[NPK]; } str;

static double mono(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }
static void make_token(void) {
  if (tok[0]) return;
  const char *t = getenv("SB_PHONE_TOKEN"); /* tests only: a known token */
  if (t && strlen(t) == 16 && strspn(t, "0123456789abcdef") == 16) { snprintf(tok, sizeof tok, "%s", t); return; }
  unsigned char b[8] = { 0 }; int fd = open("/dev/urandom", O_RDONLY), ok = 0;
  if (fd >= 0) { ok = read(fd, b, 8) == 8; close(fd); }
  if (!ok) { unsigned long long x = (unsigned long long)time(NULL) ^ ((unsigned long long)getpid() << 32) ^ (unsigned long long)(mono() * 1e9); for (int i = 0; i < 8; i++) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; b[i] = (unsigned char)x; } }
  for (int i = 0; i < 8; i++) snprintf(tok + i * 2, 3, "%02x", b[i]);
}
static void take_strings(void) {
  pthread_mutex_lock(&mu);
  snprintf(str.lang, sizeof str.lang, "%s", i18n_lang()); str.rtl = i18n_rtl();
  for (int i = 0; i < NPK; i++) { free(str.t[i]); const char *v = T(PK[i]); size_t n = strlen(v); if (n > 300 * 4) n = 300 * 4; str.t[i] = malloc(n + 1); memcpy(str.t[i], v, n); str.t[i][n] = 0; }
  pthread_mutex_unlock(&mu);
}
static const char *pt(const char *k) { for (int i = 0; i < NPK; i++) if (!strcmp(PK[i], k)) return str.t[i] && *str.t[i] ? str.t[i] : k; return k; }

/* ---- a growing text buffer ---------------------------------------------------------------------- */
typedef struct { char *p; size_t n, cap; } Buf;
static void b_put(Buf *b, const char *s, size_t n) { if (b->n + n + 1 > b->cap) { b->cap = (b->n + n + 1) * 2; b->p = realloc(b->p, b->cap); } memcpy(b->p + b->n, s, n); b->n += n; b->p[b->n] = 0; }
static void b_s(Buf *b, const char *s) { b_put(b, s, strlen(s)); }
static void hx(Buf *b, const char *s) { /* HTML text and attributes */
  for (; *s; s++) { switch (*s) { case '&': b_s(b, "&amp;"); break; case '<': b_s(b, "&lt;"); break; case '>': b_s(b, "&gt;"); break; case '"': b_s(b, "&quot;"); break; default: b_put(b, s, 1); } } }
static void js(Buf *b, const char *s) { /* a JavaScript string literal; "<" escaped so no text can close the script */
  b_s(b, "\"");
  for (; *s; s++) { unsigned char c = (unsigned char)*s; char e[8];
    if (c == '"' || c == '\\') { e[0] = '\\'; e[1] = (char)c; b_put(b, e, 2); }
    else if (c == '<') b_s(b, "\\u003c");
    else if (c < 0x20) { snprintf(e, sizeof e, "\\u%04x", c); b_s(b, e); }
    else b_put(b, s, 1); }
  b_s(b, "\"");
}

/* ---- the pages (as the helper served them) ----------------------------------------------------- */
static void head(Buf *b, const char *title) {
  b_s(b, "<!doctype html><html lang=\""); hx(b, str.lang); b_s(b, "\" dir=\""); b_s(b, str.rtl ? "rtl" : "ltr"); b_s(b, "\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  /* the page in the app's accent (the colour chosen in Settings) */
  char acc[8], acc2[8], accR[24]; snprintf(acc, sizeof acc, "#%06x", (unsigned)(ACC & 0xffffff)); snprintf(acc2, sizeof acc2, "#%06x", (unsigned)(ACC2 & 0xffffff));
  snprintf(accR, sizeof accR, "%u,%u,%u", (unsigned)((ACC >> 16) & 255), (unsigned)((ACC >> 8) & 255), (unsigned)(ACC & 255));
  b_s(b, "<title>"); hx(b, title); b_s(b, "</title><style>:root{--a:"); b_s(b, acc); b_s(b, ";--a2:"); b_s(b, acc2); b_s(b, ";--ar:"); b_s(b, accR); b_s(b, "}body{margin:0;background:#0c0b11;color:#fff;font:17px -apple-system,Segoe UI,Roboto,\"Noto Sans Arabic\",Tahoma,sans-serif;display:flex;flex-direction:column;align-items:center;padding:52px 20px;text-align:center}"
    "h1{font-size:22px;margin:0 0 6px}p{margin:0 0 24px;color:#a9a3c0;max-width:520px;line-height:1.5}a{color:var(--a2)}input{width:100%;max-width:520px;box-sizing:border-box;font-size:19px;padding:16px 18px;border-radius:14px;border:2px solid var(--a);background:#16131f;color:#fff;outline:none}"
    "button{margin-top:14px;font-size:18px;font-weight:700;padding:14px 34px;border:0;border-radius:14px;background:var(--a);color:#fff}small{margin-top:14px;color:#a9a3c0}"
    /* the TV's two soft glows behind the page, slowly turning; sending, they sweep up towards the TV */
    "body{position:relative;overflow-x:hidden;min-height:100vh;box-sizing:border-box}body>*{position:relative;z-index:1}"
    ".g{position:fixed;z-index:0;width:120vmax;height:95vmax;border-radius:50%;pointer-events:none;background:radial-gradient(closest-side,rgba(var(--ar),.42),rgba(var(--ar),0));will-change:transform}"
    "#g1{left:-62vmax;top:-48vmax;animation:o1 70s linear infinite}#g2{right:-62vmax;bottom:-48vmax;opacity:.8;animation:o2 90s linear infinite}"
    "@keyframes o1{0%{transform:rotate(0) translate(6vmax,0)}100%{transform:rotate(360deg) translate(6vmax,0)}}@keyframes o2{0%{transform:rotate(0) translate(-6vmax,0)}100%{transform:rotate(-360deg) translate(-6vmax,0)}}"
    "#w{position:fixed;inset:0;z-index:0;pointer-events:none}"
    ".go #w{animation:up .7s cubic-bezier(.3,.7,.3,1)}@keyframes up{0%{transform:none;opacity:1}45%{transform:translateY(-14vh) scale(1.06);opacity:.55}100%{transform:none;opacity:1}}"
    "</style></head><body><div id=\"w\"><div class=\"g\" id=\"g1\"></div><div class=\"g\" id=\"g2\"></div></div>");
}
static void stale_page(Buf *b) { /* an old code (the app started again since), or a mistyped address: say so, not a bare error */
  head(b, pt("Search on TV")); b_s(b, "<h1>"); hx(b, pt("This code has changed. Scan the code on your TV again.")); b_s(b, "</h1></body></html>"); }
static void search_page(Buf *b) {
  head(b, pt("Search on TV")); b_s(b, "<h1>"); hx(b, pt("Search on your TV")); b_s(b, "</h1><p>"); hx(b, pt("What you type shows up on the TV as you type.")); b_s(b, "</p>");
  b_s(b, "<input id=\"q\" autofocus autocomplete=\"off\" placeholder=\""); hx(b, pt("Film or series")); b_s(b, "\"><small id=\"s\"></small><script>"
    "var q=document.getElementById(\"q\"),s=document.getElementById(\"s\"),t=null;function send(){var x=new XMLHttpRequest();x.open(\"GET\",location.pathname+\"/q?text=\"+encodeURIComponent(q.value));"
    "var d=document.body;d.classList.remove(\"go\");void d.offsetWidth;d.classList.add(\"go\");" /* the glows sweep up towards the TV */
    "x.onload=function(){s.textContent="); js(b, pt("Sent")); b_s(b, ";};x.onerror=function(){s.textContent="); js(b, pt("Can\xe2\x80\x99t reach the TV")); b_s(b, ";};x.send();}"
    "setTimeout(function(){document.body.classList.add(\"go\");},250);" /* opened: the glows sweep up once, towards the TV */
    "q.addEventListener(\"input\",function(){clearTimeout(t);t=setTimeout(send,250);});q.addEventListener(\"keydown\",function(e){if(e.keyCode===13){clearTimeout(t);send();q.blur();}});</script></body></html>");
}
static void tmdb_page(Buf *b) {
  head(b, pt("Titles and descriptions")); b_s(b, "<h1>"); hx(b, pt("Titles and descriptions in your language")); b_s(b, "</h1><p>"); hx(b, pt("Paste your TMDB API key (or API Read Access Token). Get one at"));
  b_s(b, " <a href=\"https://www.themoviedb.org/settings/api\" target=\"_blank\" rel=\"noopener\" dir=\"ltr\">themoviedb.org/settings/api</a>. "); hx(b, pt("It stays on your TV.")); b_s(b, "</p>");
  b_s(b, "<input id=\"k\" dir=\"ltr\" autocomplete=\"off\" autocapitalize=\"off\" spellcheck=\"false\" placeholder=\""); hx(b, pt("API key")); b_s(b, "\"><button id=\"b\">"); hx(b, pt("Save")); b_s(b, "</button><small id=\"s\"></small><script>"
    "var k=document.getElementById(\"k\"),s=document.getElementById(\"s\");document.getElementById(\"b\").onclick=function(){s.textContent="); js(b, pt("Checking\xe2\x80\xa6")); b_s(b, ";var x=new XMLHttpRequest();"
    "x.open(\"GET\",location.pathname+\"/set?key=\"+encodeURIComponent(k.value.trim()));x.onload=function(){s.textContent=x.status===204?"); js(b, pt("Saved. You can close this page.")); b_s(b, ":"); js(b, pt("TMDB didn\xe2\x80\x99t accept this key.")); b_s(b, ";};"
    "x.onerror=function(){s.textContent="); js(b, pt("Can\xe2\x80\x99t reach the TV")); b_s(b, ";};x.send();};</script></body></html>");
}
static void trakt_page(Buf *b) {
  head(b, pt("Ratings and reviews")); b_s(b, "<h1>"); hx(b, pt("Ratings and reviews from Trakt")); b_s(b, "</h1><p>"); hx(b, pt("Paste your Trakt Client ID. To get one:")); b_s(b, " "); hx(b, pt("open"));
  b_s(b, " <a href=\"https://trakt.tv/oauth/applications/new\" target=\"_blank\" rel=\"noopener\" dir=\"ltr\">trakt.tv/oauth/applications</a>, "); hx(b, pt("sign in, then Settings \xe2\x80\xba Your API Apps \xe2\x80\xba New Application. Any name; as the redirect URI paste"));
  b_s(b, " <code dir=\"ltr\">urn:ietf:wg:oauth:2.0:oob</code>, "); hx(b, pt("then Save and copy the Client ID.")); b_s(b, " "); hx(b, pt("It stays on your TV.")); b_s(b, "</p>");
  b_s(b, "<input id=\"k\" dir=\"ltr\" autocomplete=\"off\" autocapitalize=\"off\" spellcheck=\"false\" placeholder=\""); hx(b, pt("Client ID")); b_s(b, "\"><button id=\"b\">"); hx(b, pt("Save")); b_s(b, "</button><small id=\"s\"></small><script>"
    "var k=document.getElementById(\"k\"),s=document.getElementById(\"s\");document.getElementById(\"b\").onclick=function(){s.textContent="); js(b, pt("Checking\xe2\x80\xa6")); b_s(b, ";var x=new XMLHttpRequest();"
    "x.open(\"GET\",location.pathname+\"/set?key=\"+encodeURIComponent(k.value.trim()));x.onload=function(){s.textContent=x.status===204?"); js(b, pt("Saved. You can close this page.")); b_s(b, ":"); js(b, pt("Trakt didn\xe2\x80\x99t accept this id.")); b_s(b, ";};"
    "x.onerror=function(){s.textContent="); js(b, pt("Can\xe2\x80\x99t reach the TV")); b_s(b, ";};x.send();};</script></body></html>");
}

/* ---- requests ------------------------------------------------------------------------------------ */
static void sendall(int fd, const char *p, size_t n) { while (n) { ssize_t w = send(fd, p, n, MSG_NOSIGNAL); if (w <= 0) { if (w < 0 && errno == EINTR) continue; return; } p += w; n -= (size_t)w; } }
static void reply(int fd, int code, const char *extra, const char *body, size_t blen) {
  const char *st = code == 200 ? "OK" : code == 204 ? "No Content" : code == 302 ? "Found" : code == 400 ? "Bad Request" : code == 404 ? "Not Found" : "Error";
  char h[1024]; int n = snprintf(h, sizeof h, "HTTP/1.1 %d %s\r\n%s%sContent-Length: %zu\r\nConnection: close\r\n\r\n", code, st, code == 404 ? "" : "Cache-Control: no-store\r\n", extra ? extra : "", body ? blen : 0);
  sendall(fd, h, (size_t)n); if (body && blen) sendall(fd, body, blen);
}
static int hexv(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
/* one query parameter, decoded as a form value ('+' is a space); "" when absent */
static void qparam(const char *q, const char *name, char *out, size_t cap) {
  size_t nl = strlen(name), o = 0; out[0] = 0;
  while (q && *q) {
    const char *amp = strchr(q, '&'), *end = amp ? amp : q + strlen(q);
    if ((size_t)(end - q) > nl && !strncmp(q, name, nl) && q[nl] == '=') {
      for (const char *s = q + nl + 1; s < end && o + 1 < cap; s++) {
        if (*s == '+') out[o++] = ' ';
        else if (*s == '%' && s + 2 < end && hexv(s[1]) >= 0 && hexv(s[2]) >= 0) { out[o++] = (char)(hexv(s[1]) * 16 + hexv(s[2])); s += 2; }
        else out[o++] = *s; }
      out[o] = 0; return; }
    q = amp ? amp + 1 : NULL; }
}
/* what the phone typed, as the helper kept it: 80 characters at most, control characters and <> as spaces,
   broken UTF-8 dropped */
static void clean_text(const char *in, char *out, size_t cap) {
  size_t o = 0; int units = 0; const unsigned char *s = (const unsigned char *)in;
  while (*s && units < 80) {
    unsigned c = *s; int n = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 0, ok = n > 0;
    for (int i = 1; ok && i < n; i++) if ((s[i] & 0xc0) != 0x80) ok = 0;
    if (!ok) { s++; continue; }
    if (o + n + 1 > cap) break;
    if (n == 1 && (c < 0x20 || c == '<' || c == '>')) out[o++] = ' '; else { memcpy(out + o, s, n); o += n; }
    units += n == 4 ? 2 : 1; s += n; }
  out[o] = 0;
}
static void handle(int fd) {
  char req[MAX_REQ + 1]; size_t got = 0;
  while (got < MAX_REQ) { ssize_t r = recv(fd, req + got, MAX_REQ - got, 0); if (r <= 0) break; got += (size_t)r; req[got] = 0; if (strstr(req, "\r\n\r\n") || strstr(req, "\n\n")) break; }
  req[got] = 0;
  char *sp1 = strchr(req, ' '), *sp2 = sp1 ? strchr(sp1 + 1, ' ') : NULL;
  if (!sp1 || !sp2 || strncmp(req, "GET ", 4)) { reply(fd, 404, NULL, NULL, 0); return; }
  *sp2 = 0; char *target = sp1 + 1, *q = strchr(target, '?'); if (q) *q++ = 0;
  /* /t/<token>, then nothing, /q, /tmdb, /tmdb/set, /trakt or /trakt/set */
  const char *rest = NULL;
  if (!strncmp(target, "/t/", 3) && strlen(target) >= 19 && !strncmp(target + 3, tok, 16)) {
    rest = target + 19;
    if (*rest && strcmp(rest, "/q") && strcmp(rest, "/tmdb") && strcmp(rest, "/tmdb/set") && strcmp(rest, "/trakt") && strcmp(rest, "/trakt/set")) rest = NULL; }
  if (!rest) { if (!strncmp(target, "/t/", 3) || !strcmp(target, "/")) { Buf b = { 0 }; pthread_mutex_lock(&mu); stale_page(&b); pthread_mutex_unlock(&mu); reply(fd, 404, "Content-Type: text/html; charset=utf-8\r\n", b.p, b.n); free(b.p); } else reply(fd, 404, NULL, NULL, 0); return; }
  if (strcmp(rest, "/q") && strcmp(rest, "/tmdb/set") && strcmp(rest, "/trakt/set")) { pthread_mutex_lock(&mu); hits++; pthread_mutex_unlock(&mu); } /* a code was opened */
  if (!strcmp(rest, "/tmdb/set") || !strcmp(rest, "/trakt/set")) {
    char key[1024]; qparam(q, "key", key, sizeof key);
    pthread_mutex_lock(&mu); int (*fn)(const char *) = rest[1] == 't' && rest[2] == 'm' ? keyFn : traktFn; pthread_mutex_unlock(&mu);
    reply(fd, fn && fn(key) ? 204 : 400, NULL, NULL, 0); return; }
  if (!strcmp(rest, "/q")) {
    char raw[1024], t[400]; qparam(q, "text", raw, sizeof raw); clean_text(raw, t, sizeof t);
    pthread_mutex_lock(&mu); typed.seq++; snprintf(typed.text, sizeof typed.text, "%s", t); pthread_mutex_unlock(&mu);
    reply(fd, 204, NULL, NULL, 0); return; }
  Buf b = { 0 };
  pthread_mutex_lock(&mu); if (!strcmp(rest, "/tmdb")) tmdb_page(&b); else if (!strcmp(rest, "/trakt")) trakt_page(&b); else search_page(&b); pthread_mutex_unlock(&mu);
  reply(fd, 200, "Content-Type: text/html; charset=utf-8\r\n", b.p, b.n); free(b.p);
}
static void *conn_main(void *arg) {
  int fd = (int)(size_t)arg;
  handle(fd); shutdown(fd, SHUT_WR); close(fd);
  pthread_mutex_lock(&mu); nconn--; pthread_mutex_unlock(&mu);
  return NULL;
}
static void sock_opts(int fd) {
  struct timeval tv = { 5, 0 }; setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv); setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv); /* a phone that stops talking frees its thread */
#ifdef SO_NOSIGPIPE
  int one = 1; setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
}
static int port = HTTPD_PORT; /* the one it listens on: the first free from HTTPD_PORT (another app can hold it) */
static int listen_one(int pn);
static int listen_on(void) { for (int i = 0; i < 20; i++) { int fd = listen_one(HTTPD_PORT + i); if (fd >= 0) { if (port != HTTPD_PORT + i) fprintf(stderr, "[httpd] port %d busy: on %d\n", HTTPD_PORT, HTTPD_PORT + i); port = HTTPD_PORT + i; return fd; } } return -1; }
static int listen_one(int pn) {
  int fd = socket(AF_INET, SOCK_STREAM, 0); if (fd < 0) return -1;
  int one = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one); fcntl(fd, F_SETFD, FD_CLOEXEC);
  struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons((unsigned short)pn); a.sin_addr.s_addr = htonl(INADDR_ANY);
#ifdef SB_MAC
  if (!getenv("SB_PHONE_LAN")) a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* the preview: this machine only (an open port made macOS ask for network access at every new build); SB_PHONE_LAN=1 opens it to the phone */
#endif
  if (bind(fd, (struct sockaddr *)&a, sizeof a) || listen(fd, 8)) { close(fd); return -1; }
  return fd;
}
static void *srv_main(void *arg) {
  int lfd = (int)(size_t)arg;
  for (;;) {
    pthread_mutex_lock(&mu);
    if (stopReq || mono() > deadline) { close(lfd); alive = 0; pthread_mutex_unlock(&mu); return NULL; } /* closed before it says so: a new start can bind again */
    pthread_mutex_unlock(&mu);
    struct pollfd p = { lfd, POLLIN, 0 }; /* woken every half second to see whether it should go */
    if (poll(&p, 1, 500) <= 0 || !(p.revents & POLLIN)) continue;
    int c = accept(lfd, NULL, NULL); if (c < 0) continue;
    fcntl(c, F_SETFD, FD_CLOEXEC); sock_opts(c);
    pthread_mutex_lock(&mu); int busy = nconn >= MAX_CONN; if (!busy) nconn++; pthread_mutex_unlock(&mu);
    if (busy) { close(c); continue; }
    pthread_t t; pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED); pthread_attr_setstacksize(&at, 128 * 1024);
    if (pthread_create(&t, &at, conn_main, (void *)(size_t)c)) { close(c); pthread_mutex_lock(&mu); nconn--; pthread_mutex_unlock(&mu); }
    pthread_attr_destroy(&at);
  }
}

/* ---- the main thread's side ---------------------------------------------------------------------- */
int httpd_start(void) {
  make_token(); take_strings();
  pthread_mutex_lock(&mu);
  deadline = mono() + IDLE_SEC; stopReq = 0;
  if (alive) { pthread_mutex_unlock(&mu); return 1; } /* on already (or still closing: it stays) */
  pthread_mutex_unlock(&mu);
  int fd = listen_on(); if (fd < 0) { fprintf(stderr, "[httpd] ports %d-%d: %s\n", HTTPD_PORT, HTTPD_PORT + 19, strerror(errno)); return 0; }
  pthread_mutex_lock(&mu); alive = 1; pthread_mutex_unlock(&mu);
  pthread_t t; pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED); pthread_attr_setstacksize(&at, 128 * 1024);
  int bad = pthread_create(&t, &at, srv_main, (void *)(size_t)fd); pthread_attr_destroy(&at);
  if (bad) { close(fd); pthread_mutex_lock(&mu); alive = 0; pthread_mutex_unlock(&mu); return 0; }
  return 1;
}
/* the TV's address on its network: the one its own traffic leaves by (a datagram socket "connected" outward sends
   nothing and says which address the route takes), else the first IPv4 address that is up and running and not the
   loopback, a self-assigned 169.254 one or Wi-Fi Direct's 192.168.49 (a side interface many TVs keep up) */
static int lan_ip(char *out, size_t cap) {
  out[0] = 0;
  { int s = socket(AF_INET, SOCK_DGRAM, 0); if (s >= 0) { struct sockaddr_in d; memset(&d, 0, sizeof d); d.sin_family = AF_INET; d.sin_port = htons(53); inet_pton(AF_INET, "8.8.8.8", &d.sin_addr);
      struct sockaddr_in me; socklen_t ml = sizeof me; if (!connect(s, (struct sockaddr *)&d, sizeof d) && !getsockname(s, (struct sockaddr *)&me, &ml)) { unsigned h = ntohl(me.sin_addr.s_addr);
        if (h && (h >> 24) != 127 && (h >> 16) != 0xa9fe) inet_ntop(AF_INET, &me.sin_addr, out, (socklen_t)cap); } close(s); } }
  if (out[0]) return 1;
  struct ifaddrs *ifs = NULL, *i; if (getifaddrs(&ifs)) return 0;
  for (i = ifs; i && !out[0]; i = i->ifa_next) {
    if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || (i->ifa_flags & IFF_LOOPBACK) || !(i->ifa_flags & IFF_UP) || !(i->ifa_flags & IFF_RUNNING)) continue;
    struct in_addr a = ((struct sockaddr_in *)i->ifa_addr)->sin_addr; unsigned h = ntohl(a.s_addr);
    if ((h >> 16) == 0xa9fe || (h >> 8) == 0xc0a831 || h == 0) continue;
    inet_ntop(AF_INET, &a, out, (socklen_t)cap); }
  freeifaddrs(ifs); return out[0] != 0;
}
int httpd_url(const char *suffix, char *out, size_t cap) {
  char ip[64]; out[0] = 0;
  if (!httpd_start() || !lan_ip(ip, sizeof ip)) return 0;
  snprintf(out, cap, "http://%s:%d/t/%s%s", ip, port, tok, suffix ? suffix : ""); return 1;
}
void httpd_url_shown(const char *url, char *out, size_t cap) {
  const char *t = strstr(url, "/t/"); if (!t) { snprintf(out, cap, "%s", url); return; }
  const char *e = t + 3; while ((*e >= '0' && *e <= '9') || (*e >= 'a' && *e <= 'f')) e++;
  snprintf(out, cap, "%.*s/t/\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2%s", (int)(t - url), url, e);
}
int httpd_typed(int since, char *out, size_t cap) {
  pthread_mutex_lock(&mu); int s = typed.seq; if (s > since && out && cap) snprintf(out, cap, "%s", typed.text); pthread_mutex_unlock(&mu); return s;
}
unsigned httpd_hits(void) { pthread_mutex_lock(&mu); unsigned h = hits; pthread_mutex_unlock(&mu); return h; }
void httpd_set_key_handler(int (*fn)(const char *key)) { pthread_mutex_lock(&mu); keyFn = fn; pthread_mutex_unlock(&mu); }
void httpd_set_trakt_handler(int (*fn)(const char *key)) { pthread_mutex_lock(&mu); traktFn = fn; pthread_mutex_unlock(&mu); }
