#include "platform/plat.h"
#include "core/net.h"
#include "core/update.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <pthread.h>
#include <curl/curl.h>
#include <SDL2/SDL.h>
#ifndef SB_MAC
#include <dlfcn.h>
#endif

/* ---- the curl entry points, resolved at run time on the TV -------------------------------- */
static CURL *(*f_easy_init)(void);
static CURLcode (*f_easy_setopt)(CURL *, CURLoption, ...);
static CURLcode (*f_easy_perform)(CURL *);
static CURLcode (*f_easy_getinfo)(CURL *, CURLINFO, ...);
static void (*f_easy_cleanup)(CURL *);
static void (*f_easy_reset)(CURL *); /* (may be missing on a very old libcurl: then a handle per fetch, as before) */
static struct curl_slist *(*f_slist_append)(struct curl_slist *, const char *);
static void (*f_slist_free_all)(struct curl_slist *);
static CURLcode (*f_global_init)(long);
/* one cache for every thread's handle (optional: an old libcurl goes on without it): the TLS sessions and the DNS
   answers, and on a libcurl that does it well (7.68+) the open connections too. On a Samsung a full TLS handshake took
   ~1 s (measured), and six picture threads each made their own to the same host: now one does, the rest resume */
static CURLSH *(*f_share_init)(void); static CURLSHcode (*f_share_setopt)(CURLSH *, CURLSHoption, ...); static curl_version_info_data *(*f_version_info)(CURLversion);
static CURLSH *shareH; static pthread_mutex_t shareMu[8];
static void share_lock(CURL *h, curl_lock_data d, curl_lock_access a, void *u) { (void)h; (void)a; (void)u; pthread_mutex_lock(&shareMu[(unsigned)d & 7]); }
static void share_unlock(CURL *h, curl_lock_data d, void *u) { (void)h; (void)u; pthread_mutex_unlock(&shareMu[(unsigned)d & 7]); }
static void share_start(void) {
  if (shareH || !f_share_init || !f_share_setopt) return;
  for (int i = 0; i < 8; i++) pthread_mutex_init(&shareMu[i], NULL);
  CURLSH *sh = f_share_init(); if (!sh) return;
  f_share_setopt(sh, CURLSHOPT_LOCKFUNC, share_lock); f_share_setopt(sh, CURLSHOPT_UNLOCKFUNC, share_unlock);
  f_share_setopt(sh, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS); f_share_setopt(sh, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
  unsigned ver = 0; const char *vs = "?", *ssl = "?"; if (f_version_info) { curl_version_info_data *v = f_version_info(CURLVERSION_NOW); if (v) { ver = v->version_num; vs = v->version; ssl = v->ssl_version ? v->ssl_version : "?"; } }
  int conn = ver >= 0x074400 && f_share_setopt(sh, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT) == CURLSHE_OK;
  shareH = sh; fprintf(stderr, "[net] libcurl %s (%s): one cache of TLS sessions and DNS%s for every thread\n", vs, ssl, conn ? ", and of connections," : "");
}
static char caPath[1024];
/* "StremioOrigin/1.0.0 (webOS; native)": the running version, the platform it runs on */
static const char *ua(void) { static char u[96]; if (!u[0]) { const char *d = plat_device_name(); snprintf(u, sizeof u, "StremioOrigin/%s (%s; native)", update_current(), strstr(d, "webOS") ? "webOS" : strstr(d, "Android") ? "Android TV" : "Mac"); } return u; }

static int resolve(void) {
#ifdef SB_MAC
  f_easy_init = curl_easy_init; f_easy_setopt = curl_easy_setopt; f_easy_perform = curl_easy_perform; f_easy_getinfo = curl_easy_getinfo;
  f_easy_cleanup = curl_easy_cleanup; f_easy_reset = curl_easy_reset; f_slist_append = curl_slist_append; f_slist_free_all = curl_slist_free_all; f_global_init = curl_global_init;
  return 1;
#else
  static const char *names[] = { "libcurl.so.5", "libcurl.so.4", "libcurl.so", NULL };
  void *h = NULL; for (int i = 0; names[i] && !h; i++) h = dlopen(names[i], RTLD_NOW);
  if (!h) { fprintf(stderr, "[net] no libcurl: %s\n", dlerror()); return 0; }
#define L(v, n) do { *(void **)(&v) = dlsym(h, n); if (!v) { fprintf(stderr, "[net] missing %s\n", n); return 0; } } while (0)
  L(f_easy_init, "curl_easy_init"); L(f_easy_setopt, "curl_easy_setopt"); L(f_easy_perform, "curl_easy_perform"); L(f_easy_getinfo, "curl_easy_getinfo");
  *(void **)(&f_share_init) = dlsym(h, "curl_share_init"); *(void **)(&f_share_setopt) = dlsym(h, "curl_share_setopt"); *(void **)(&f_version_info) = dlsym(h, "curl_version_info");
  L(f_easy_cleanup, "curl_easy_cleanup"); *(void **)(&f_easy_reset) = dlsym(h, "curl_easy_reset"); L(f_slist_append, "curl_slist_append"); L(f_slist_free_all, "curl_slist_free_all"); L(f_global_init, "curl_global_init");
#undef L
  return 1;
#endif
}

/* ---- requests ------------------------------------------------------------------------------- */
typedef struct Req {
  unsigned id; char *url, *hdr, *post, *ctype, *method; size_t postLen; NetCb cb; void *ud; long timeoutMs; cJSON *json; /* postLen: a raw body (net_send), else post is text; json: the body already parsed on the worker */
  int status; unsigned char *body; size_t len, cap; int cancelled, done;
  int size; double total;  /* net_size: only the headers are read */
  int file;                /* net_file: url is a local path */
  struct Req *next;
} Req;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static Req *queue, *queueTail, *slowQ, *slowTail, *finished, *inflight[8], *fqueue, *fqueueTail;
static pthread_cond_t slowCv = PTHREAD_COND_INITIALIZER;
static pthread_cond_t fcv = PTHREAD_COND_INITIALIZER;
static unsigned nextId = 1; static int npending;
#define WORKERS 8   /* 0-4 take the quick requests (catalogs, details, the account); 5-7 the slow ones (stream addons, up to
                       45 s each): details never wait behind a debrid addon thinking */
#define SLOW0 5

static size_t on_data(char *p, size_t s, size_t n, void *ud) {
  Req *r = ud; size_t add = s * n;
  if (r->cancelled) return 0;
  if (r->len + add + 1 > r->cap) { r->cap = (r->len + add + 1) * 2; r->body = realloc(r->body, r->cap); }
  memcpy(r->body + r->len, p, add); r->len += add; r->body[r->len] = 0; return add;
}
/* net_size: "Content-Range: bytes 0-0/<total>" (a 200 instead: its Content-Length); each redirect starts over */
static size_t on_header(char *p, size_t s, size_t n, void *ud) {
  Req *r = ud; size_t len = s * n; char h[256]; size_t k = len < sizeof h - 1 ? len : sizeof h - 1; memcpy(h, p, k); h[k] = 0;
  if (!strncmp(h, "HTTP/", 5)) { r->total = 0; r->status = atoi(strchr(h, ' ') ? strchr(h, ' ') + 1 : "0"); }
  else if (!strncasecmp(h, "content-range:", 14)) { char *sl = strrchr(h, '/'); if (sl) r->total = atof(sl + 1); }
  else if (!strncasecmp(h, "content-length:", 15) && r->status == 200) r->total = atof(h + 15);
  return len;
}
static size_t on_size_data(char *p, size_t s, size_t n, void *ud) { (void)p; (void)s; (void)n; (void)ud; return 0; } /* never the body: a 200 would be the whole film */
/* Tailscale names (*.ts.net) from inside webOS's app jail: its /etc has no hosts or resolv.conf, so a name only the
   TV's hosts file or Tailscale's resolver knows can't be found here (a self-hosted addon on a tailnet answered
   nothing, though the TV itself reaches it). Such a name is asked of Tailscale's own resolver (100.100.100.100, served
   by Tailscale on the TV) and the answer pinned for libcurl (CURLOPT_RESOLVE: the name stays, so the certificate still
   matches); any other name resolves as before. No Tailscale on the TV: the question goes unanswered, nothing changes */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>
static int ts_name(const char *h) { size_t n = strlen(h); return n > 7 && !strcasecmp(h + n - 7, ".ts.net"); }
static int url_host(const char *url, char *host, size_t cap, int *port) { /* scheme://[user@]host[:port]/... */
  const char *p = strstr(url, "://"); if (!p) return 0; int https = !strncasecmp(url, "https", 5); p += 3;
  const char *end = p + strcspn(p, "/?#"), *at = memchr(p, '@', (size_t)(end - p)); if (at) p = at + 1;
  const char *col = memchr(p, ':', (size_t)(end - p)); size_t hl = (size_t)((col ? col : end) - p); if (!hl || hl >= cap) return 0;
  memcpy(host, p, hl); host[hl] = 0; *port = col ? atoi(col + 1) : https ? 443 : 80; return 1;
}
static int dns_a(const char *host, char *ip, size_t cap, int *ttl) { /* one A question, UDP, 1.5 s */
  unsigned char q[300]; size_t k = 0; unsigned id = (unsigned)SDL_GetTicks() & 0xffff;
  q[k++] = id >> 8; q[k++] = id & 255; q[k++] = 1; q[k++] = 0; q[k++] = 0; q[k++] = 1; for (int i = 0; i < 6; i++) q[k++] = 0; /* recursion desired, one question */
  for (const char *l = host; *l; ) { const char *d = strchr(l, '.'); size_t n = d ? (size_t)(d - l) : strlen(l); if (!n || n > 63 || k + n + 6 > sizeof q) return 0; q[k++] = (unsigned char)n; memcpy(q + k, l, n); k += n; l += n; if (*l == '.') l++; }
  q[k++] = 0; q[k++] = 0; q[k++] = 1; q[k++] = 0; q[k++] = 1;
  int s = socket(AF_INET, SOCK_DGRAM, 0); if (s < 0) return 0;
  struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons(53); inet_pton(AF_INET, "100.100.100.100", &a.sin_addr);
  { const char *t = getenv("SB_TS_DNS"); char ta[64]; int tp = 53; if (t && sscanf(t, "%63[^:]:%d", ta, &tp) >= 1) { inet_pton(AF_INET, ta, &a.sin_addr); a.sin_port = htons((unsigned short)tp); } } /* SB_TS_DNS=127.0.0.1:5353: development, another resolver */
  unsigned char r[1500]; ssize_t got = -1;
  if (sendto(s, q, k, 0, (struct sockaddr *)&a, sizeof a) == (ssize_t)k) { struct pollfd pf = { s, POLLIN, 0 }; if (poll(&pf, 1, 1500) == 1) got = recv(s, r, sizeof r, 0); }
  close(s); if (got < 12 || r[0] != q[0] || r[1] != q[1] || (r[3] & 15)) return 0;
  int an = r[6] << 8 | r[7]; size_t o = 12;
  while (o < (size_t)got && r[o]) { if ((r[o] & 0xc0) == 0xc0) { o++; break; } o += (size_t)r[o] + 1; } o += 5; /* the question back */
  for (int i = 0; i < an && o + 10 < (size_t)got; i++) {
    if ((r[o] & 0xc0) == 0xc0) o += 2; else { while (o < (size_t)got && r[o]) o += (size_t)r[o] + 1; o++; }
    if (o + 10 > (size_t)got) break; int type = r[o] << 8 | r[o + 1]; int t = (int)((unsigned)r[o + 4] << 24 | (unsigned)r[o + 5] << 16 | (unsigned)r[o + 6] << 8 | r[o + 7]); int rl = r[o + 8] << 8 | r[o + 9]; o += 10;
    if (type == 1 && rl == 4 && o + 4 <= (size_t)got) { snprintf(ip, cap, "%u.%u.%u.%u", r[o], r[o + 1], r[o + 2], r[o + 3]); *ttl = t; return 1; }
    o += (size_t)rl; }
  return 0;
}
static pthread_mutex_t tsMu = PTHREAD_MUTEX_INITIALIZER;
static struct { char host[256], ip[16]; Uint32 until; int ok; } tsCache[8]; static int tsAt;
static struct curl_slist *ts_pin(CURL *c, const char *url) { /* (the list is freed by the caller after the request) */
  char host[256], ip[16] = ""; int port, ttl = 60; if (!url_host(url, host, sizeof host, &port) || !ts_name(host)) return NULL;
  Uint32 now = SDL_GetTicks(); int ok = -1;
  pthread_mutex_lock(&tsMu); for (int i = 0; i < 8; i++) if (tsCache[i].until > now && !strcasecmp(tsCache[i].host, host)) { ok = tsCache[i].ok; snprintf(ip, sizeof ip, "%s", tsCache[i].ip); } pthread_mutex_unlock(&tsMu);
  if (ok < 0) { ok = dns_a(host, ip, sizeof ip, &ttl); if (ttl < 60) ttl = 60;
    pthread_mutex_lock(&tsMu); int i = tsAt; tsAt = (tsAt + 1) % 8; snprintf(tsCache[i].host, sizeof tsCache[i].host, "%s", host); snprintf(tsCache[i].ip, sizeof tsCache[i].ip, "%s", ip); tsCache[i].ok = ok; tsCache[i].until = now + (Uint32)(ok ? ttl : 30) * 1000; pthread_mutex_unlock(&tsMu);
    fprintf(stderr, ok ? "[net] a .ts.net address found through Tailscale's resolver\n" : "[net] a .ts.net address: Tailscale's resolver didn't answer (Tailscale not running on this TV?)\n"); } /* (the name and its address stay out of the logs) */
  if (!ok) return NULL;
  char e[300]; snprintf(e, sizeof e, "%s:%d:%s", host, port, ip); struct curl_slist *l = f_slist_append(NULL, e); if (l) f_easy_setopt(c, CURLOPT_RESOLVE, l); return l;
}
static void log_fail(CURLcode rc, const char *url) { /* why a request failed, when it is a name that wasn't found (it said "no answer") */
  if (rc != CURLE_COULDNT_RESOLVE_HOST) return; char host[256]; int port; if (!url_host(url, host, sizeof host, &port)) return;
  fprintf(stderr, "[net] couldn't look up %s\n", ts_name(host) ? "a .ts.net address" : host); }
/* each thread keeps one handle for its requests (the API workers' and the pictures' alike): the connection to a host
   stays open between them, with the DNS and TLS-session caches: no DNS, TCP and TLS handshake, and no certificate list
   read again, per request (on the TV's CPU a handshake is a tenth of a second) */
#ifdef SB_TPK
static pthread_key_t tlsKey; static pthread_once_t tlsOnce = PTHREAD_ONCE_INIT;
static void tls_end(void *c) { if (c) f_easy_cleanup((CURL *)c); }
static void tls_key(void) { pthread_key_create(&tlsKey, tls_end); }
static inline CURL *get_tlsCurl(void) { pthread_once(&tlsOnce, tls_key); return (CURL *)pthread_getspecific(tlsKey); }
static inline void set_tlsCurl(CURL *c) { pthread_once(&tlsOnce, tls_key); pthread_setspecific(tlsKey, c); }
#else
static __thread CURL *tlsCurl;
static pthread_key_t tlsKey; static pthread_once_t tlsOnce = PTHREAD_ONCE_INIT;
static void tls_end(void *c) { if (c) f_easy_cleanup((CURL *)c); }
static void tls_key(void) { pthread_key_create(&tlsKey, tls_end); }
#endif
static CURL *thread_handle(int *keep) {
  *keep = f_easy_reset != NULL;
#ifdef SB_TPK
  CURL *cur = get_tlsCurl();
  CURL *c = *keep && cur ? cur : f_easy_init(); if (!c) return NULL;
  if (*keep) { if (c == cur) f_easy_reset(c); else { set_tlsCurl(c); } }
#else
  CURL *c = *keep && tlsCurl ? tlsCurl : f_easy_init(); if (!c) return NULL;
  if (*keep) { if (c == tlsCurl) f_easy_reset(c); else { tlsCurl = c; pthread_once(&tlsOnce, tls_key); pthread_setspecific(tlsKey, c); } }
#endif
  f_easy_setopt(c, CURLOPT_TCP_KEEPALIVE, 1L); f_easy_setopt(c, CURLOPT_DNS_CACHE_TIMEOUT, 300L); f_easy_setopt(c, CURLOPT_MAXCONNECTS, 6L); if (shareH) f_easy_setopt(c, CURLOPT_SHARE, shareH); return c; }
static void thread_handle_done(CURL *c, int keep) { if (!keep) { f_easy_cleanup(c); return; } f_easy_setopt(c, CURLOPT_HTTPHEADER, (struct curl_slist *)NULL); f_easy_setopt(c, CURLOPT_RESOLVE, (struct curl_slist *)NULL); f_easy_setopt(c, CURLOPT_POSTFIELDS, (char *)NULL); } /* (the lists and the body it pointed at go with the request) */
static void perform(Req *r) {
  int keep; CURL *c = thread_handle(&keep); if (!c) { r->status = 0; return; }
  struct curl_slist *hs = NULL;
  f_easy_setopt(c, CURLOPT_URL, r->url); f_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_data); f_easy_setopt(c, CURLOPT_WRITEDATA, r); struct curl_slist *pin = ts_pin(c, r->url);
  f_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L); f_easy_setopt(c, CURLOPT_MAXREDIRS, 8L); f_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
  f_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 8000L); f_easy_setopt(c, CURLOPT_TIMEOUT_MS, r->timeoutMs > 0 ? r->timeoutMs : 20000L);
  f_easy_setopt(c, CURLOPT_USERAGENT, ua()); if (!r->size) f_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
  if (caPath[0]) f_easy_setopt(c, CURLOPT_CAINFO, caPath);
  if (r->size) { f_easy_setopt(c, CURLOPT_TIMEOUT_MS, 10000L); f_easy_setopt(c, CURLOPT_HEADERFUNCTION, on_header); f_easy_setopt(c, CURLOPT_HEADERDATA, r); f_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_size_data); hs = f_slist_append(hs, "Range: bytes=0-0"); }
  /* one header per line; one the caller names (User-Agent...) replaces curl's own */
  for (char *h = r->hdr, *nl; h && *h; h = nl ? nl + 1 : NULL) { nl = strchr(h, '\n'); if (nl) *nl = 0; if (*h) hs = f_slist_append(hs, h); if (nl) *nl = '\n'; }
  if (r->post) { if (r->ctype) { if (*r->ctype) { char ct[160]; snprintf(ct, sizeof ct, "Content-Type: %s", r->ctype); hs = f_slist_append(hs, ct); } } else if (!r->postLen) hs = f_slist_append(hs, "Content-Type: application/json");
    f_easy_setopt(c, CURLOPT_POSTFIELDS, r->post); f_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)(r->postLen ? r->postLen : strlen(r->post))); }
  if (r->method) f_easy_setopt(c, CURLOPT_CUSTOMREQUEST, r->method);
  if (hs) f_easy_setopt(c, CURLOPT_HTTPHEADER, hs);
  CURLcode rc = f_easy_perform(c);
  long code = 0; f_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
  r->status = rc == CURLE_OK ? (int)code : 0; log_fail(rc, r->url);
  if (r->size) { char t[32]; int k = snprintf(t, sizeof t, "%.0f", r->total > 0 ? r->total : 0); free(r->body); r->body = (unsigned char *)strdup(t); r->len = (size_t)k; r->status = (int)code; } /* the server's answer even without a size (a 404 says why a film will not play); 0 = no answer */
  thread_handle_done(c, keep); if (hs) f_slist_free_all(hs); if (pin) f_slist_free_all(pin);
}
static void parse_body(Req *r) {
  if (r->body && r->len > 1 && r->len < 16u * 1024 * 1024 && r->status >= 200 && r->status < 300) { const unsigned char *b = r->body; while (*b == ' ' || *b == '\n' || *b == '\r' || *b == '\t') b++; if (*b == '{' || *b == '[') r->json = cJSON_ParseWithLength((const char *)r->body, r->len); }
}
/* disk copies get a thread of their own: behind six stream addons (45 s each) a cached title page would wait as long */
static void *file_worker(void *arg) { plat_thread_background(5);
  (void)arg;
  for (;;) {
    pthread_mutex_lock(&mu);
    while (!fqueue) pthread_cond_wait(&fcv, &mu);
    Req *r = fqueue; fqueue = r->next; if (!fqueue) fqueueTail = NULL; r->next = NULL;
    pthread_mutex_unlock(&mu);
    if (!r->cancelled) { FILE *f = fopen(r->url, "rb"); r->status = 404;
      if (f) { fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        if (n > 0 && (r->body = malloc((size_t)n + 1)) && fread(r->body, 1, (size_t)n, f) == (size_t)n) { r->body[n] = 0; r->len = (size_t)n; r->status = 200; }
        fclose(f); }
      parse_body(r); }
    pthread_mutex_lock(&mu);
    r->done = 1; r->next = finished; finished = r;
    pthread_mutex_unlock(&mu);
  }
  return NULL;
}
unsigned net_file(const char *path, NetCb cb, void *ud) {
  Req *r = calloc(1, sizeof *r); r->url = strdup(path); r->cb = cb; r->ud = ud; r->file = 1;
  pthread_mutex_lock(&mu);
  r->id = nextId++; if (fqueueTail) fqueueTail->next = r; else fqueue = r; fqueueTail = r; npending++;
  pthread_cond_signal(&fcv); pthread_mutex_unlock(&mu);
  return r->id;
}
static void *worker(void *arg) { plat_thread_background(5);
  int slot = (int)(size_t)arg;
  for (;;) {
    pthread_mutex_lock(&mu);
    if (slot >= SLOW0) while (!slowQ) pthread_cond_wait(&slowCv, &mu); else while (!queue) pthread_cond_wait(&cv, &mu);
    Req *r; if (slot >= SLOW0) { r = slowQ; slowQ = r->next; if (!slowQ) slowTail = NULL; } else { r = queue; queue = r->next; if (!queue) queueTail = NULL; }
    r->next = NULL; inflight[slot] = r;
    pthread_mutex_unlock(&mu);
    if (!r->cancelled) { perform(r);
      /* a JSON answer is parsed here, off the drawing thread (catalogs, metadata, streams: tens of ms each on a TV) */
      parse_body(r); }
    pthread_mutex_lock(&mu);
    inflight[slot] = NULL; r->done = 1; r->next = finished; finished = r;
    pthread_mutex_unlock(&mu);
  }
  return NULL;
}
#ifndef SB_MAC
/* OpenSSL 1.0 (the TV's, under its libcurl) is safe across threads only with locks the app hands it: without them
   its shared tables were changed by two requests at once, and a long session ended in a crash inside it (a bad
   pointer in memcmp, called from libcrypto). 1.1 and later lock by themselves (no CRYPTO_num_locks to find): left as
   they are, as is a library someone already gave locks to */
static pthread_mutex_t *sslLocks;
static void ssl_lock(int mode, int n, const char *file, int line) { (void)file; (void)line; if (mode & 1) pthread_mutex_lock(&sslLocks[n]); else pthread_mutex_unlock(&sslLocks[n]); }
static unsigned long ssl_id(void) { return (unsigned long)pthread_self(); }
static void (*sslSetNum)(void *, unsigned long);
static void ssl_threadid(void *id) { sslSetNum(id, (unsigned long)pthread_self()); } /* (each thread its own id: with one id for all, OpenSSL took them for one thread and they blocked each other) */
static void ssl_locks(void) {
  static const char *names[] = { "libcrypto.so.1.0.0", "libcrypto.so.1.0.2", "libcrypto.so.10", "libcrypto.so", NULL };
  void *h = NULL; for (int i = 0; names[i] && !h; i++) h = dlopen(names[i], RTLD_NOW | RTLD_NOLOAD); /* the one curl brought in */
  if (!h) { fprintf(stderr, "[net] tls: no OpenSSL 1.0 loaded (nothing to lock)\n"); return; }
  int (*num)(void) = NULL; void (*setLock)(void (*)(int, int, const char *, int)) = NULL; void *(*getLock)(void) = NULL; void (*setId)(unsigned long (*)(void)) = NULL; int (*setTid)(void (*)(void *)) = NULL;
  *(void **)(&num) = dlsym(h, "CRYPTO_num_locks"); *(void **)(&setLock) = dlsym(h, "CRYPTO_set_locking_callback"); *(void **)(&getLock) = dlsym(h, "CRYPTO_get_locking_callback");
  *(void **)(&setId) = dlsym(h, "CRYPTO_set_id_callback"); *(void **)(&setTid) = dlsym(h, "CRYPTO_THREADID_set_callback");
  if (!num || !setLock) { fprintf(stderr, "[net] tls: OpenSSL locks by itself\n"); return; }
  if (getLock && getLock()) { fprintf(stderr, "[net] tls: OpenSSL already has locks\n"); return; }
  int n = num(); if (n <= 0) return; sslLocks = calloc((size_t)n, sizeof *sslLocks); if (!sslLocks) return;
  for (int i = 0; i < n; i++) pthread_mutex_init(&sslLocks[i], NULL);
  *(void **)(&sslSetNum) = dlsym(h, "CRYPTO_THREADID_set_numeric"); /* from the library itself: it is not in the global names */
  if (setTid && sslSetNum) setTid(ssl_threadid); else if (setId) setId(ssl_id); else { fprintf(stderr, "[net] tls: no thread id call: left as it was\n"); return; }
  setLock(ssl_lock); fprintf(stderr, "[net] tls: OpenSSL 1.0 given %d locks for the request threads\n", n);
}
#endif
int net_init(const char *ca) {
  (void)ua(); /* made once here, before any worker thread reads it */
  if (!resolve()) return 0;
#ifndef SB_MAC
  ssl_locks(); /* before the first request and the worker threads */
#endif
  f_global_init(CURL_GLOBAL_ALL);
  share_start(); /* (after the global init, before any worker) */
  if (ca) snprintf(caPath, sizeof caPath, "%s", ca);
  for (int i = 0; i < WORKERS; i++) { pthread_t t; pthread_create(&t, NULL, worker, (void *)(size_t)i); pthread_detach(t); }
  { pthread_t t; pthread_create(&t, NULL, file_worker, NULL); pthread_detach(t); }
  return 1;
}
static void push_req(Req *r) { /* under mu */
  if (r->timeoutMs > 20000) { if (slowTail) slowTail->next = r; else slowQ = r; slowTail = r; pthread_cond_signal(&slowCv); }
  else { if (queueTail) queueTail->next = r; else queue = r; queueTail = r; pthread_cond_signal(&cv); }
}
/* one that is waited on as the user types (search suggestions): ahead of everything queued */
unsigned net_get_first(const char *url, NetCb cb, void *ud) {
  Req *r = calloc(1, sizeof *r); r->url = strdup(url); r->cb = cb; r->ud = ud;
  pthread_mutex_lock(&mu); r->id = nextId++; r->next = queue; queue = r; if (!queueTail) queueTail = r; npending++; pthread_cond_signal(&cv); pthread_mutex_unlock(&mu); return r->id; }
static unsigned enqueue_x(const char *url, const char *hdr, const char *post, const char *ctype, long timeoutMs, int size, NetCb cb, void *ud) {
  Req *r = calloc(1, sizeof *r); r->url = strdup(url); r->hdr = hdr ? strdup(hdr) : NULL; r->post = post ? strdup(post) : NULL; r->ctype = ctype ? strdup(ctype) : NULL; r->cb = cb; r->ud = ud; r->timeoutMs = timeoutMs; r->size = size;
  pthread_mutex_lock(&mu);
  r->id = nextId++; push_req(r); npending++;
  pthread_mutex_unlock(&mu);
  return r->id;
}
static unsigned enqueue_c(const char *url, const char *hdr, const char *post, const char *ctype, long timeoutMs, NetCb cb, void *ud) { return enqueue_x(url, hdr, post, ctype, timeoutMs, 0, cb, ud); }
static unsigned enqueue_t(const char *url, const char *hdr, const char *post, long timeoutMs, NetCb cb, void *ud) { return enqueue_x(url, hdr, post, NULL, timeoutMs, 0, cb, ud); }
static unsigned enqueue(const char *url, const char *hdr, const char *post, NetCb cb, void *ud) { return enqueue_t(url, hdr, post, 0, cb, ud); }
unsigned net_get_timeout(const char *url, int timeoutMs, NetCb cb, void *ud) { return enqueue_t(url, NULL, NULL, timeoutMs, cb, ud); }
unsigned net_get(const char *url, NetCb cb, void *ud) { return enqueue(url, NULL, NULL, cb, ud); }
unsigned net_get_hdr(const char *url, const char *hdr, NetCb cb, void *ud) { return enqueue(url, hdr, NULL, cb, ud); }
unsigned net_size(const char *url, NetCb cb, void *ud) { return enqueue_x(url, NULL, NULL, NULL, 10000, 1, cb, ud); }
unsigned net_post_json(const char *url, const char *json, NetCb cb, void *ud) { return enqueue(url, NULL, json, cb, ud); }
unsigned net_post_hdr_t(const char *url, const char *body, const char *contentType, const char *headers, int timeoutMs, NetCb cb, void *ud) {
  return enqueue_c(url, headers, body ? body : "", contentType ? contentType : "", timeoutMs, cb, ud);
}
unsigned net_send(const char *method, const char *url, const char *headers, const void *body, size_t len, int timeoutMs, NetCb cb, void *ud) {
  Req *r = calloc(1, sizeof *r); r->url = strdup(url); r->hdr = headers ? strdup(headers) : NULL; r->method = strcmp(method, "POST") ? strdup(method) : NULL; r->cb = cb; r->ud = ud; r->timeoutMs = timeoutMs;
  r->post = malloc(len + 1); memcpy(r->post, body, len); r->post[len] = 0; r->postLen = len;
  pthread_mutex_lock(&mu);
  r->id = nextId++; push_req(r); npending++;
  pthread_mutex_unlock(&mu);
  return r->id;
}
void net_cancel(unsigned id) {
  pthread_mutex_lock(&mu);
  for (Req *r = queue; r; r = r->next) if (r->id == id) r->cancelled = 1;
  for (Req *r = slowQ; r; r = r->next) if (r->id == id) r->cancelled = 1;
  for (Req *r = fqueue; r; r = r->next) if (r->id == id) r->cancelled = 1;
  for (int i = 0; i < WORKERS; i++) if (inflight[i] && inflight[i]->id == id) inflight[i]->cancelled = 1;
  for (Req *r = finished; r; r = r->next) if (r->id == id) r->cancelled = 1;
  pthread_mutex_unlock(&mu);
}
int net_pending(void) { return npending; }
/* parsed answers are freed on a thread of their own: deleting a catalog's tree (thousands of small blocks, made by a
   worker) took 10-25 ms of a frame on the TV */
typedef struct JF { cJSON *j; struct JF *next; } JF;
static JF *jfHead; static pthread_mutex_t jfMu = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t jfCv = PTHREAD_COND_INITIALIZER; static int jfThread;
static void *jf_run(void *a) { plat_thread_background(15); (void)a; for (;;) { pthread_mutex_lock(&jfMu); while (!jfHead) pthread_cond_wait(&jfCv, &jfMu); JF *f = jfHead; jfHead = NULL; pthread_mutex_unlock(&jfMu);
    while (f) { JF *n = f->next; cJSON_Delete(f->j); free(f); f = n; } } return NULL; }
void net_json_free(cJSON *j) {
  if (!j) return;
  pthread_mutex_lock(&jfMu);
  if (!jfThread) { pthread_t t; if (pthread_create(&t, NULL, jf_run, NULL) == 0) { pthread_detach(t); jfThread = 1; } }
  if (!jfThread) { pthread_mutex_unlock(&jfMu); cJSON_Delete(j); return; }
  JF *f = malloc(sizeof *f); f->j = j; f->next = jfHead; jfHead = f; pthread_cond_signal(&jfCv); pthread_mutex_unlock(&jfMu);
}
static const unsigned char *curBody; static cJSON *curJson;
cJSON *net_json(const unsigned char *body) {
  if (body && body == curBody && curJson) { cJSON *j = curJson; curJson = NULL; return j; } /* the tree the worker made: the caller owns it */
  return body ? cJSON_Parse((const char *)body) : NULL;
}
void net_pump(void) {
  pthread_mutex_lock(&mu); Req *list = finished; finished = NULL; pthread_mutex_unlock(&mu);
  /* finished is LIFO; deliver in completion order */
  Req *rev = NULL; while (list) { Req *n = list->next; list->next = rev; rev = list; list = n; }
  while (rev) {
    Req *r = rev; rev = r->next;
    curBody = r->body; curJson = r->json; r->json = NULL;
    { static int slowLog = -1; if (slowLog < 0) slowLog = getenv("SB_SLOWLOG") != NULL; Uint32 tc0 = slowLog ? SDL_GetTicks() : 0;
      if (r->cb) { if (r->cancelled) r->cb(-1, NULL, 0, r->ud); else r->cb(r->status, r->body ? r->body : (const unsigned char *)"", r->len, r->ud); }
      if (slowLog && SDL_GetTicks() - tc0 > 6) fprintf(stderr, "[netcb] %u ms for %.120s (%zu bytes)\n", SDL_GetTicks() - tc0, r->url, r->len); } /* development: which answers cost a frame */
    if (curJson) net_json_free(curJson); curJson = NULL; curBody = NULL; /* the callback did not want the tree */
    pthread_mutex_lock(&mu); npending--; pthread_mutex_unlock(&mu);
    free(r->url); free(r->hdr); free(r->post); free(r->ctype); free(r->method); free(r->body); free(r);
  }
}
unsigned char *net_fetch_sync(const char *url, size_t *len, int *status, int timeoutMs) { return net_fetch_sync_hdr(url, NULL, len, status, timeoutMs); }
static long fetchN, fetchNew; /* fetches, and the new connections they made (the rest reused one) */
void net_fetch_stats(long *n, long *fresh) { *n = __atomic_load_n(&fetchN, __ATOMIC_RELAXED); *fresh = __atomic_load_n(&fetchNew, __ATOMIC_RELAXED); }
unsigned char *net_fetch_sync_hdr(const char *url, const char *hdr, size_t *len, int *status, int timeoutMs) {
  Req r; memset(&r, 0, sizeof r); r.url = (char *)url;
  int keep; CURL *c = thread_handle(&keep); if (!c) { *status = 0; *len = 0; return NULL; }
  struct curl_slist *hs = hdr ? f_slist_append(NULL, hdr) : NULL; if (hs) f_easy_setopt(c, CURLOPT_HTTPHEADER, hs);
  f_easy_setopt(c, CURLOPT_URL, url); f_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_data); f_easy_setopt(c, CURLOPT_WRITEDATA, &r); struct curl_slist *pin = ts_pin(c, url);
  f_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L); f_easy_setopt(c, CURLOPT_MAXREDIRS, 8L); f_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
  f_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 8000L); f_easy_setopt(c, CURLOPT_TIMEOUT_MS, (long)timeoutMs);
  f_easy_setopt(c, CURLOPT_USERAGENT, ua()); f_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
  if (caPath[0]) f_easy_setopt(c, CURLOPT_CAINFO, caPath);
  CURLcode rc = f_easy_perform(c); long code = 0, nc = 0; f_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code); f_easy_getinfo(c, CURLINFO_NUM_CONNECTS, &nc);
  __atomic_add_fetch(&fetchN, 1, __ATOMIC_RELAXED); if (nc > 0) __atomic_add_fetch(&fetchNew, 1, __ATOMIC_RELAXED);
  thread_handle_done(c, keep);
  if (hs) f_slist_free_all(hs); if (pin) f_slist_free_all(pin); log_fail(rc, url);
  *status = rc == CURLE_OK ? (int)code : 0; *len = r.len;
  if (*status != 200 && *status != 206) { free(r.body); return NULL; }
  return r.body;
}

/* ---- a download read as it arrives (worker threads only: it blocks) ---------------------------- */
typedef struct { NetSink sink; void *ud; int stopped; } StreamCtx;
static size_t stream_data(char *p, size_t s, size_t n, void *ud) { StreamCtx *x = ud; if (!x->sink((const unsigned char *)p, s * n, x->ud)) { x->stopped = 1; return 0; } return s * n; }
static int stream_poll(void *ud, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) { (void)a; (void)b; (void)c; (void)d; StreamCtx *x = ud; if (!x->sink(NULL, 0, x->ud)) { x->stopped = 1; return 1; } return 0; } /* a stalled transfer can still be stopped */
int net_stream_sync(const char *url, int timeoutMs, NetSink sink, void *ud) {
  if (!f_easy_init && !resolve()) return 0;
  CURL *c = f_easy_init(); if (!c) return 0;
  StreamCtx x = { sink, ud, 0 };
  f_easy_setopt(c, CURLOPT_URL, url); f_easy_setopt(c, CURLOPT_WRITEFUNCTION, stream_data); f_easy_setopt(c, CURLOPT_WRITEDATA, &x); struct curl_slist *pin = ts_pin(c, url);
  f_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, stream_poll); f_easy_setopt(c, CURLOPT_XFERINFODATA, &x); f_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
  f_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L); f_easy_setopt(c, CURLOPT_MAXREDIRS, 8L); f_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
  f_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 8000L); f_easy_setopt(c, CURLOPT_TIMEOUT_MS, (long)timeoutMs);
  f_easy_setopt(c, CURLOPT_USERAGENT, ua()); /* no Accept-Encoding: the bytes counted are the bytes that crossed the line */
  if (caPath[0]) f_easy_setopt(c, CURLOPT_CAINFO, caPath);
  CURLcode rc = f_easy_perform(c); long code = 0; f_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code); f_easy_cleanup(c); if (pin) f_slist_free_all(pin); log_fail(rc, url);
  if (x.stopped) return -1;
  return rc == CURLE_OK ? (int)code : 0;
}

/* ---- a GET passed through as it arrives, with the caller's headers (the stream proxy: worker threads only) ---------- */
typedef struct { NetHdrSink hs; NetSink sink; void *ud; int stopped; } PipeCtx;
static size_t pipe_hdr(char *p, size_t s, size_t n, void *ud) { PipeCtx *x = ud; size_t l = s * n; char line[1024]; size_t k = l < sizeof line - 1 ? l : sizeof line - 1; memcpy(line, p, k); line[k] = 0; x->hs(line, x->ud); return l; }
static size_t pipe_data(char *p, size_t s, size_t n, void *ud) { PipeCtx *x = ud; if (!x->sink((const unsigned char *)p, s * n, x->ud)) { x->stopped = 1; return 0; } return s * n; }
static int pipe_poll(void *ud, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) { (void)a; (void)b; (void)c; (void)d; PipeCtx *x = ud; if (!x->sink(NULL, 0, x->ud)) { x->stopped = 1; return 1; } return 0; }
int net_pipe_sync(const char *url, const char *headers, int head, NetHdrSink hs, NetSink sink, void *ud, int stallSec) {
  if (!f_easy_init && !resolve()) return 0;
  CURL *c = f_easy_init(); if (!c) return 0;
  PipeCtx x = { hs, sink, ud, 0 }; struct curl_slist *sl = NULL;
  char *hd = headers ? strdup(headers) : NULL; for (char *h = hd, *nl; h && *h; h = nl ? nl + 1 : NULL) { nl = strchr(h, '\n'); if (nl) *nl = 0; if (*h) sl = f_slist_append(sl, h); }
  f_easy_setopt(c, CURLOPT_URL, url); f_easy_setopt(c, CURLOPT_HEADERFUNCTION, pipe_hdr); f_easy_setopt(c, CURLOPT_HEADERDATA, &x); struct curl_slist *pin = ts_pin(c, url);
  f_easy_setopt(c, CURLOPT_WRITEFUNCTION, pipe_data); f_easy_setopt(c, CURLOPT_WRITEDATA, &x);
  f_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, pipe_poll); f_easy_setopt(c, CURLOPT_XFERINFODATA, &x); f_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
  f_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L); f_easy_setopt(c, CURLOPT_MAXREDIRS, 8L); f_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
  f_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 10000L); f_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L); f_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, (long)(stallSec > 0 ? stallSec : 60));
  f_easy_setopt(c, CURLOPT_USERAGENT, "Mozilla/5.0 (Web0S; Linux) StremioOrigin"); /* a User-Agent among the headers replaces it */
  if (head) f_easy_setopt(c, CURLOPT_NOBODY, 1L);
  if (sl) f_easy_setopt(c, CURLOPT_HTTPHEADER, sl);
  if (caPath[0]) f_easy_setopt(c, CURLOPT_CAINFO, caPath);
  CURLcode rc = f_easy_perform(c); long code = 0; f_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code); f_easy_cleanup(c); if (sl) f_slist_free_all(sl); free(hd); if (pin) f_slist_free_all(pin); log_fail(rc, url);
  if (x.stopped) return -1;
  return rc == CURLE_OK ? (int)code : 0;
}

/* ---- ranged reads ---------------------------------------------------------------------------- */
struct NetRange { CURL *c; char *orig, *eff; int noDirect; }; /* eff: where orig led (its redirects followed once), asked directly after */
typedef struct { unsigned char *buf; long len, got, extra; int status, isRange; long long reqStart, crStart, total; int norange; } RangeCtx;
static size_t range_hdr(char *p, size_t s, size_t n, void *ud) {
  RangeCtx *r = ud; size_t l = s * n; char line[512]; size_t k = l < sizeof line - 1 ? l : sizeof line - 1; memcpy(line, p, k); line[k] = 0;
  int code; if (sscanf(line, "HTTP/%*s %d", &code) == 1) { r->status = code; r->crStart = -1; } /* each redirect hop starts again */
  else if (!strncasecmp(line, "content-range:", 14)) { long long a, b, t; char star[4];
    if (sscanf(line + 14, " bytes %lld-%lld/%lld", &a, &b, &t) == 3) { r->crStart = a; r->total = t; }
    else if (sscanf(line + 14, " bytes %lld-%lld/%1s", &a, &b, star) == 3) r->crStart = a; }
  else if (!strncasecmp(line, "content-length:", 15) && r->status == 200) { long long cl = atoll(line + 15); if (cl > 0) r->total = cl; }
  return l;
}
static size_t range_data(char *p, size_t s, size_t n, void *ud) {
  RangeCtx *r = ud; size_t add = s * n;
  if (r->status >= 300 && r->status < 400) return add;                    /* a redirect's body */
  if (r->status == 200 && r->isRange) { r->norange = 1; return 0; }       /* the server ignored the Range */
  if (r->status == 206 && r->crStart >= 0 && r->crStart != r->reqStart) { r->norange = 1; return 0; } /* another part than asked */
  long need = r->len - r->got, take = (long)add < need ? (long)add : need; if (take < 0) take = 0;
  if (take) { memcpy(r->buf + r->got, p, take); r->got += take; }
  if (r->status == 206) { r->extra += (long)add - take; return r->extra > 65536 ? 0 : add; } /* the part asked for: read to its end, so the connection is kept for the next one (stopping it short closed it: a new TLS handshake for every read, seconds each on a TV); a server sending far past it is stopped */
  return r->got >= r->len ? 0 : add;                                      /* a 200 for the whole file: stopped once it has what was asked */
}
NetRange *net_range_new(void) { if (!f_easy_init && !resolve()) return NULL; NetRange *r = calloc(1, sizeof *r); r->c = f_easy_init(); if (!r->c) { free(r); return NULL; } return r; }
void net_range_free(NetRange *r) { if (!r) return; if (r->c) f_easy_cleanup(r->c); free(r->orig); free(r->eff); free(r); }
static int range_once(NetRange *h, const char *url, long long start, long len, unsigned char **out, long *got, long long *total, int *httpStatus, int timeoutMs) {
  *out = NULL; *got = 0; if (total) *total = -1; if (httpStatus) *httpStatus = 0;
  if (!h || len <= 0) return NR_NET;
  RangeCtx r; memset(&r, 0, sizeof r); r.buf = malloc(len); r.len = len; r.total = -1; r.crStart = -1; r.reqStart = start;
  r.isRange = start > 0; /* asked from past the start: a 200 means no Range support */
  char rg[64]; snprintf(rg, sizeof rg, "%lld-%lld", start, start + len - 1);
  CURL *c = h->c;
  f_easy_setopt(c, CURLOPT_URL, url); f_easy_setopt(c, CURLOPT_RANGE, rg); struct curl_slist *pin = ts_pin(c, url);
  f_easy_setopt(c, CURLOPT_HEADERFUNCTION, range_hdr); f_easy_setopt(c, CURLOPT_HEADERDATA, &r);
  f_easy_setopt(c, CURLOPT_WRITEFUNCTION, range_data); f_easy_setopt(c, CURLOPT_WRITEDATA, &r);
  f_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L); f_easy_setopt(c, CURLOPT_MAXREDIRS, 5L); f_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
  f_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 8000L); f_easy_setopt(c, CURLOPT_TIMEOUT_MS, (long)timeoutMs);
  f_easy_setopt(c, CURLOPT_USERAGENT, "Mozilla/5.0 (Web0S; Linux) StremioOrigin"); f_easy_setopt(c, CURLOPT_TCP_KEEPALIVE, 1L);
  if (caPath[0]) f_easy_setopt(c, CURLOPT_CAINFO, caPath);
  CURLcode rc = f_easy_perform(c); if (pin) { f_easy_setopt(c, CURLOPT_RESOLVE, (struct curl_slist *)NULL); f_slist_free_all(pin); } /* (a kept-alive handle: no list left behind it) */ log_fail(rc, url);
  if (httpStatus) *httpStatus = r.status;
  if (total) *total = r.total;
  if (r.norange) { free(r.buf); return NR_NORANGE; }
  if (r.got > 0 && r.status == 0 && !strncmp(url, "file://", 7)) r.status = 206; /* (a file on the phone: no status, its range read as asked) */
  if (r.got > 0 && (r.status == 200 || r.status == 206)) { *out = r.buf; *got = r.got; return NR_OK; } /* full, or a server that cut the response: what came */
  free(r.buf);
  if (rc == CURLE_OPERATION_TIMEDOUT) return NR_TIMEOUT;
  if (r.status == 429 || r.status >= 500) return NR_BUSY;
  if (r.status >= 400) return NR_HTTP;
  return NR_NET;
}
/* a stream's address that redirects (an addon's or a debrid's link) is followed once; its reads then go straight to
   where it led (each one through the redirect cost seconds: a file's subtitles read hundreds of small parts), and back
   through the original when that stops answering (a link's token expired) */
/* where a link led, shared by every handle (a few links): a new connection to the same stream reads straight there
   instead of paying the redirect again (a debrid link: seconds each) */
static struct { char *orig, *eff; } LED[8]; static int ledNext; static pthread_mutex_t ledMu = PTHREAD_MUTEX_INITIALIZER;
static void led_put(const char *o, const char *e) { pthread_mutex_lock(&ledMu); int k = -1; for (int i = 0; i < 8; i++) if (LED[i].orig && !strcmp(LED[i].orig, o)) k = i; if (k < 0) { k = ledNext; ledNext = (ledNext + 1) % 8; }
  char *a = strdup(o), *b = strdup(e); if (a && b) { free(LED[k].orig); free(LED[k].eff); LED[k].orig = a; LED[k].eff = b; } else { free(a); free(b); } pthread_mutex_unlock(&ledMu); }
static void led_drop(const char *o) { pthread_mutex_lock(&ledMu); for (int i = 0; i < 8; i++) if (LED[i].orig && !strcmp(LED[i].orig, o)) { free(LED[i].orig); free(LED[i].eff); LED[i].orig = LED[i].eff = NULL; } pthread_mutex_unlock(&ledMu); }
int net_led_known(const char *url) { pthread_mutex_lock(&ledMu); int k = 0; for (int i = 0; i < 8; i++) if (LED[i].orig && !strcmp(LED[i].orig, url)) k = 1; pthread_mutex_unlock(&ledMu); return k; }
static void led_get(NetRange *h, const char *url) { if (h->eff && h->orig && !strcmp(h->orig, url)) return; pthread_mutex_lock(&ledMu);
  for (int i = 0; i < 8; i++) if (LED[i].orig && !strcmp(LED[i].orig, url)) { char *a = strdup(LED[i].orig), *b = strdup(LED[i].eff); if (a && b) { free(h->orig); free(h->eff); h->orig = a; h->eff = b; } else { free(a); free(b); } break; }
  pthread_mutex_unlock(&ledMu); }
/* many byte ranges in one request (multipart/byteranges): parts[i] (malloc'd, or NULL when the answer lacked it) and
   plen[i]. NR_NORANGE: the server doesn't answer several ranges at once (a 200, one part, an error for the form) */
typedef struct { unsigned char *buf; long len, cap, max; int status; char ctype[200]; } MultiCtx;
static size_t multi_hdr(char *p, size_t s, size_t n, void *ud) { MultiCtx *m = ud; size_t l = s * n; char line[512]; size_t k = l < sizeof line - 1 ? l : sizeof line - 1; memcpy(line, p, k); line[k] = 0;
  int code; if (sscanf(line, "HTTP/%*s %d", &code) == 1) { m->status = code; m->ctype[0] = 0; m->len = 0; }
  else if (!strncasecmp(line, "content-type:", 13)) { char *v = line + 13; while (*v == ' ') v++; size_t vl = strcspn(v, "\r\n"); if (vl >= sizeof m->ctype) vl = sizeof m->ctype - 1; memcpy(m->ctype, v, vl); m->ctype[vl] = 0; }
  return l; }
static size_t multi_data(char *p, size_t s, size_t n, void *ud) { MultiCtx *m = ud; size_t add = s * n; if (m->status != 206) return add; if (m->len + (long)add > m->max) return 0;
  if (m->len + (long)add > m->cap) { long nc = (m->len + (long)add) * 2; if (nc > m->max) nc = m->max; unsigned char *q = realloc(m->buf, nc); if (!q) return 0; m->buf = q; m->cap = nc; } memcpy(m->buf + m->len, p, add); m->len += add; return add; }
static unsigned char *mem_find(unsigned char *h, long hn, const char *nd, long nn) { for (long i = 0; i + nn <= hn; i++) if (h[i] == (unsigned char)nd[0] && !memcmp(h + i, nd, nn)) return h + i; return NULL; }
int net_range_multi(NetRange *h, const char *url, const long long *st, const long *ln, int n, unsigned char **parts, long *plen, int *httpStatus, int timeoutMs) {
  for (int i = 0; i < n; i++) { parts[i] = NULL; plen[i] = 0; } if (!h || n <= 0) return NR_NET; if (!h->noDirect) led_get(h, url);
  int direct = h->eff && h->orig && !strcmp(h->orig, url); char *rg = malloc((size_t)n * 48 + 8); if (!rg) return NR_NET; rg[0] = 0; long want = 0;
  for (int i = 0, o = 0; i < n; i++) { o += snprintf(rg + o, 48, "%s%lld-%lld", i ? "," : "", st[i], st[i] + ln[i] - 1); want += ln[i]; }
  for (int pass = 0; pass < 2; pass++) {
    MultiCtx m; memset(&m, 0, sizeof m); m.max = want + n * 1024L + 65536; CURL *c = h->c; /* (no reset: the handle keeps its shared TLS and DNS caches) */
    f_easy_setopt(c, CURLOPT_URL, direct ? h->eff : url); f_easy_setopt(c, CURLOPT_RANGE, rg); struct curl_slist *pin = ts_pin(c, url);
    f_easy_setopt(c, CURLOPT_HEADERFUNCTION, multi_hdr); f_easy_setopt(c, CURLOPT_HEADERDATA, &m); f_easy_setopt(c, CURLOPT_WRITEFUNCTION, multi_data); f_easy_setopt(c, CURLOPT_WRITEDATA, &m);
    f_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L); f_easy_setopt(c, CURLOPT_MAXREDIRS, 5L); f_easy_setopt(c, CURLOPT_NOSIGNAL, 1L); f_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 8000L); f_easy_setopt(c, CURLOPT_TIMEOUT_MS, (long)timeoutMs);
    f_easy_setopt(c, CURLOPT_USERAGENT, "Mozilla/5.0 (Web0S; Linux) StremioOrigin"); f_easy_setopt(c, CURLOPT_TCP_KEEPALIVE, 1L); if (caPath[0]) f_easy_setopt(c, CURLOPT_CAINFO, caPath);
    CURLcode rc = f_easy_perform(c); if (pin) { f_easy_setopt(c, CURLOPT_RESOLVE, (struct curl_slist *)NULL); f_slist_free_all(pin); }
    if (httpStatus) *httpStatus = m.status;
    if (direct && !pass && (rc != CURLE_OK || (m.status >= 400 && m.status != 416 && m.status != 429))) { free(m.buf); led_drop(url); free(h->eff); h->eff = NULL; direct = 0; continue; } /* (where the link led stopped answering: through the link again) */
    if (m.status == 429 || m.status == 503) { free(m.buf); free(rg); return NR_BUSY; }
    if (rc == CURLE_OPERATION_TIMEDOUT) { free(m.buf); free(rg); return NR_TIMEOUT; }
    if (rc != CURLE_OK && !(rc == CURLE_WRITE_ERROR && m.status != 206)) { free(m.buf); free(rg); return NR_NET; }
    const char *bd = strcasestr(m.ctype, "boundary="); if (m.status != 206 || !strcasestr(m.ctype, "multipart/byteranges") || !bd) { free(m.buf); free(rg); return m.status == 206 || m.status == 200 || m.status == 400 || m.status == 416 || m.status == 501 ? NR_NORANGE : m.status >= 400 ? NR_HTTP : NR_NET; }
    char sep[96]; bd += 9; if (*bd == '"') bd++; size_t bl = strcspn(bd, "\";\r\n "); if (bl > 80) bl = 80; snprintf(sep, sizeof sep, "--%.*s", (int)bl, bd);
    unsigned char *q = m.buf, *end = m.buf + m.len; long sl = (long)strlen(sep);
    while ((q = mem_find(q, end - q, sep, sl))) { q += sl; if (q + 2 <= end && q[0] == '-' && q[1] == '-') break;
      unsigned char *he = mem_find(q, end - q, "\r\n\r\n", 4); if (!he) break; long long a = -1, b = -1;
      { char hd[1024]; long hl = he - q < (long)sizeof hd - 1 ? he - q : (long)sizeof hd - 1; memcpy(hd, q, hl); hd[hl] = 0; char *cr = strcasestr(hd, "content-range:"); if (cr) sscanf(cr + 14, " bytes %lld-%lld", &a, &b); }
      unsigned char *d = he + 4; if (a < 0 || b < a || d + (b - a + 1) > end) { q = d; continue; }
      for (int i = 0; i < n; i++) if (!parts[i] && st[i] >= a && st[i] <= b) { long take = (long)(b - st[i] + 1); if (take > ln[i]) take = ln[i]; parts[i] = malloc(take); if (parts[i]) { memcpy(parts[i], d + (st[i] - a), take); plen[i] = take; } } /* (a server may join neighbouring ranges into one part) */
      q = d + (b - a + 1); }
    free(m.buf); free(rg); int any = 0; for (int i = 0; i < n; i++) if (parts[i]) any = 1; return any ? NR_OK : NR_NORANGE; }
  free(rg); return NR_NET; }
int net_range_get(NetRange *h, const char *url, long long start, long len, unsigned char **out, long *got, long long *total, int *httpStatus, int timeoutMs) {
  if (!h) return range_once(h, url, start, len, out, got, total, httpStatus, timeoutMs);
  if (!h->noDirect) led_get(h, url); /* (learnt by another connection: straight there) */
  if (h->noDirect && h->orig && strcmp(h->orig, url)) h->noDirect = 0; /* (another stream on this handle: learnt afresh) */
  int direct = h->eff && h->orig && !strcmp(h->orig, url); struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
  int rc = range_once(h, direct ? h->eff : url, start, len, out, got, total, httpStatus, timeoutMs);
  if (direct && (rc == NR_HTTP || rc == NR_NET)) { fprintf(stderr, "[range] where the link led stopped answering (%d, HTTP %d): through the link again\n", rc, httpStatus ? *httpStatus : 0); char *was = h->eff; h->eff = NULL; direct = 0; led_drop(url); rc = range_once(h, url, start, len, out, got, total, httpStatus, timeoutMs);
    if (rc == NR_OK || rc == NR_NORANGE) { char *e = NULL; f_easy_getinfo(h->c, CURLINFO_EFFECTIVE_URL, &e); if (e && was && !strcmp(e, was)) { h->noDirect = 1; fprintf(stderr, "[range] where the link leads answers only through it: read through the link from now on\n"); } } free(was); } /* (the same place again: not an expired token, it needs the link (a cookie, a referrer); never tried directly again for this stream) */
  clock_gettime(CLOCK_MONOTONIC, &t1); double ms = (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6; static double lastSaid;
  if ((ms > 1500 || rc != NR_OK) && t1.tv_sec - lastSaid >= 1) { lastSaid = t1.tv_sec; long nc = 0; double tc = 0, ts = 0, tf = 0; f_easy_getinfo(h->c, CURLINFO_NUM_CONNECTS, &nc); f_easy_getinfo(h->c, CURLINFO_CONNECT_TIME, &tc); f_easy_getinfo(h->c, CURLINFO_APPCONNECT_TIME, &ts); f_easy_getinfo(h->c, CURLINFO_STARTTRANSFER_TIME, &tf);
    fprintf(stderr, "[range] %.0f ms for %ld B at %lld, %s: %d (HTTP %d) · new connections %ld, connect %.0f ms, tls %.0f ms, first byte %.0f ms\n", ms, len, start, direct ? "direct" : "through the link", rc, httpStatus ? *httpStatus : 0, nc, tc * 1e3, ts * 1e3, tf * 1e3); }
  if (!direct && !h->noDirect && (rc == NR_OK || rc == NR_NORANGE)) { char *e = NULL; f_easy_getinfo(h->c, CURLINFO_EFFECTIVE_URL, &e);
    if (e && *e && strcmp(e, url)) { char *o = strdup(url), *f = strdup(e); if (o && f) { free(h->orig); free(h->eff); h->orig = o; h->eff = f; led_put(o, f); fprintf(stderr, "[range] the link redirects: read straight from where it led (%.0f ms through it)\n", ms); } else { free(o); free(f); } } }
  return rc;
}
