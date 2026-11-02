/* webOS: calls to system services over luna-service2 (the TV's language from its settings service, and the
   like). Two ways, tried in this order:
   - the system's helper library (libhelpers), the one the TV's own SDL makes its calls through. Its handle is
     registered under the app id, and in developer mode (the app installed with ares-install, jailed, no root)
     that is the one name the app's LS2 role lets call any service: the role the installer writes allows the
     anonymous name too but gives it no permissions, and the com.webos.media.client.* names may talk to
     com.webos.media only. Its replies arrive on its own thread;
   - a handle of our own (the libraries opened at run time, as the player's are in video_webos.c), on its own
     glib context, so nothing here runs on, or waits for, the player's service thread. It is what a rooted TV
     has always used.
   A refused call ("Denied method call", "not allowed") is logged and answered with its error payload, never
   waited on: the callers read a missing answer as "the TV does not say". */
#include "platform/plat.h"
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

typedef struct LSHandle LSHandle; typedef struct LSMessage LSMessage; typedef struct { int error_code; char *message; const char *file; int line; const char *func; char pad[64]; } LSErr;
typedef bool (*LSFilterFunc)(LSHandle *sh, LSMessage *reply, void *ctx);
static bool (*lsRegister)(const char *, LSHandle **, void *); static bool (*lsAttach)(LSHandle *, void *, void *);
static bool (*lsCallOne)(LSHandle *, const char *, const char *, LSFilterFunc, void *, unsigned long *, void *); static bool (*lsCallCancel)(LSHandle *, unsigned long, void *);
static bool (*lsCall)(LSHandle *, const char *, const char *, LSFilterFunc, void *, unsigned long *, void *);
static bool (*lsErrInit)(void *); static void (*lsErrFree)(void *); static const char *(*lsPayload)(LSMessage *);
static void *(*gCtxNew)(void); static void *(*gLoopNew)(void *, int); static int (*gCtxIter)(void *, int);
static LSHandle *bus; static void *ctx; static int broken;
static unsigned seq; static char *answer; static int answered;

/* a refusal by the bus or the service's own permission check: the app's role or ACG lacks it */
static int failed(const char *p) { const char *v = p ? strstr(p, "\"returnValue\"") : NULL; if (!v) return 0; v += 13; while (*v == ' ' || *v == ':') v++; return !strncmp(v, "false", 5); }
static int refused(const char *p) {
  return failed(p) && (strstr(p, "Denied") || strstr(p, "denied") || strstr(p, "not allowed") || strstr(p, "Not permitted") || strstr(p, "ermission"));
}
static void log_refused(const char *via, const char *uri, const char *p) {
  const char *t = strstr(p, "\"errorText\""); fprintf(stderr, "[luna] %s refused (%s): %.160s\n", uri, via, t ? t : p);
}

/* ---- the system helper library ---- */
typedef struct HCtx { LSFilterFunc cb; void *ud; void *unknown; int multiple; int pub; unsigned long tok; } HCtx; /* libhelpers' HContext */
static int (*hCall)(const char *, const char *, HCtx *); static int (*hUnreg)(HCtx *); static const char *(*hMsg)(LSMessage *);
static int hState; /* 0 untried, 1 in use, -1 absent or silent */
static pthread_mutex_t hMu = PTHREAD_MUTEX_INITIALIZER;
static int helpers(void) {
  pthread_mutex_lock(&hMu);
  if (!hState) { hState = -1;
    void *H = getenv("SB_NOHELPERS") ? NULL : dlopen("libhelpers.so.2", RTLD_NOW); /* SB_NOHELPERS=1: development, the own handle only */
    if (!H && !getenv("SB_NOHELPERS")) H = dlopen("libhelpers.so", RTLD_NOW);
    if (H) { *(void **)(&hCall) = dlsym(H, "HLunaServiceCall"); *(void **)(&hMsg) = dlsym(H, "HLunaServiceMessage"); *(void **)(&hUnreg) = dlsym(H, "HUnregisterServiceCallback"); }
    if (hCall && hMsg) hState = 1;
    fprintf(stderr, "[luna] system helper library: %s\n", hState > 0 ? "in use" : "not there (own handle)"); }
  int ok = hState > 0; pthread_mutex_unlock(&hMu); return ok;
}
/* one reply, waited for with a deadline; a reply after the deadline frees what the caller left behind */
typedef struct { HCtx h; pthread_mutex_t m; pthread_cond_t c; int done, gone; char *out; } HSync;
static void hsync_free(HSync *s) { pthread_mutex_destroy(&s->m); pthread_cond_destroy(&s->c); free(s->out); free(s); }
static bool h_one(LSHandle *sh, LSMessage *msg, void *u) {
  (void)sh; HSync *s = u; const char *p = hMsg(msg); char *copy = p ? strdup(p) : NULL;
  pthread_mutex_lock(&s->m);
  if (s->gone) { pthread_mutex_unlock(&s->m); free(copy); hsync_free(s); return true; }
  if (!s->done) { s->out = copy; copy = NULL; s->done = 1; pthread_cond_signal(&s->c); }
  pthread_mutex_unlock(&s->m); free(copy); return true;
}
/* -1: the library could not send it; 0: no answer in time; 1: answered (*out may still be NULL) */
static int h_call(const char *uri, const char *payload, int timeoutMs, char **out) {
  *out = NULL; HSync *s = calloc(1, sizeof *s); if (!s) return -1;
  pthread_mutex_init(&s->m, NULL); pthread_cond_init(&s->c, NULL); s->h.cb = h_one; s->h.pub = 1;
  if (hCall(uri, payload, &s->h)) { hsync_free(s); return -1; }
  struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); ts.tv_sec += timeoutMs / 1000; ts.tv_nsec += (long)(timeoutMs % 1000) * 1000000L; if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
  pthread_mutex_lock(&s->m);
  while (!s->done) if (pthread_cond_timedwait(&s->c, &s->m, &ts) == ETIMEDOUT) break;
  if (!s->done) { s->gone = 1; pthread_mutex_unlock(&s->m); return 0; }
  *out = s->out; s->out = NULL; pthread_mutex_unlock(&s->m); hsync_free(s); return 1;
}
static bool h_drop(LSHandle *sh, LSMessage *msg, void *u) { (void)sh; const char *p = hMsg(msg); if (refused(p)) log_refused("helper", "a sent call", p); free(u); return true; }

/* ---- the own handle ---- */
static void err_reset(LSErr *e) { if (lsErrFree && e->message) lsErrFree(e); memset(e, 0, sizeof *e); if (lsErrInit) lsErrInit(e); }
#define SYM(lib, var, name) do { *(void **)(&var) = dlsym(lib, name); if (!var) { fprintf(stderr, "[luna] no %s\n", name); return 0; } } while (0)
static int setup(void) {
  if (bus) return 1; if (broken) return 0; broken = 1; /* one try per run */
  void *L = dlopen("libluna-service2.so.3", RTLD_NOW), *G = dlopen("libglib-2.0.so.0", RTLD_NOW);
  if (!L || !G) { fprintf(stderr, "[luna] luna/glib not found\n"); return 0; }
  SYM(L, lsRegister, "LSRegister"); SYM(L, lsAttach, "LSGmainAttach"); SYM(L, lsCallOne, "LSCallOneReply"); SYM(L, lsPayload, "LSMessageGetPayload");
  *(void **)(&lsCallCancel) = dlsym(L, "LSCallCancel"); *(void **)(&lsCall) = dlsym(L, "LSCall"); *(void **)(&lsErrInit) = dlsym(L, "LSErrorInit"); *(void **)(&lsErrFree) = dlsym(L, "LSErrorFree");
  SYM(G, gCtxNew, "g_main_context_new"); SYM(G, gLoopNew, "g_main_loop_new"); SYM(G, gCtxIter, "g_main_context_iteration");
  /* the app's own name first: the role the installer writes gives it the app's permissions (tvpower among
     them), which anonymous does not get and the media-client name does not allow outside com.webos.media.
     Then anonymous (a rooted TV lets it call anything), then a media-client name */
  const char *app = getenv("APPID"); if (!app) app = "tv.stremioorigin.app"; const char *shortId = strrchr(app, '.') ? strrchr(app, '.') + 1 : app;
  char name[160]; snprintf(name, sizeof name, "com.webos.media.client.%s.sys%d", shortId, (int)getpid());
  const char *tries[3] = { app, NULL, name }; LSErr e; memset(&e, 0, sizeof e);
  for (int i = 0; i < 3 && !bus; i++) { err_reset(&e); LSHandle *h = NULL;
    if (lsRegister(tries[i], &h, &e) && h) { bus = h; fprintf(stderr, "[luna] registered as %s\n", tries[i] ? tries[i] : "(anonymous)"); } else fprintf(stderr, "[luna] register %s refused: %d %s\n", tries[i] ? tries[i] : "(anonymous)", e.error_code, e.message ? e.message : "?"); }
  if (!bus) { err_reset(&e); return 0; }
  ctx = gCtxNew(); void *loop = gLoopNew(ctx, 0); err_reset(&e);
  if (!lsAttach(bus, loop, &e)) { fprintf(stderr, "[luna] attach failed: %s\n", e.message ? e.message : "?"); err_reset(&e); bus = NULL; return 0; }
  err_reset(&e); broken = 0; return 1;
}
static bool on_reply(LSHandle *h, LSMessage *m, void *u) {
  (void)h; if ((unsigned)(uintptr_t)u != seq || answered) return true; /* a late answer to a call given up on */
  const char *p = lsPayload(m); answer = p ? strdup(p) : NULL; answered = 1; return true;
}
static bool on_drop(LSHandle *h, LSMessage *m, void *u) { (void)h; (void)u; const char *p = lsPayload(m); if (refused(p)) log_refused("own handle", "a sent call", p); return true; }
static pthread_mutex_t callMu = PTHREAD_MUTEX_INITIALIZER;
static char *own_call(const char *uri, const char *payload, int timeoutMs) {
  if (!setup()) return NULL;
  seq++; answered = 0; answer = NULL; unsigned long tok = 0; LSErr e; memset(&e, 0, sizeof e); if (lsErrInit) lsErrInit(&e);
  if (!lsCallOne(bus, uri, payload, on_reply, (void *)(uintptr_t)seq, &tok, &e)) { fprintf(stderr, "[luna] %s failed: %s\n", uri, e.message ? e.message : "?"); err_reset(&e); return NULL; }
  err_reset(&e);
  /* only this thread runs the context: the reply is dispatched from here */
  Uint32 t0 = SDL_GetTicks(); while (!answered && SDL_GetTicks() - t0 < (Uint32)timeoutMs) if (!gCtxIter(ctx, 0)) usleep(2000);
  if (!answered) { fprintf(stderr, "[luna] %s: no answer in %d ms\n", uri, timeoutMs); if (lsCallCancel) { lsCallCancel(bus, tok, &e); err_reset(&e); } seq++; return NULL; }
  if (lsErrFree && e.message) lsErrFree(&e);
  char *r = answer; answer = NULL; return r;
}

/* one call at a time: the answer slot and the context are shared, and callers include worker threads */
char *plat_luna_call(const char *uri, const char *payload, int timeoutMs) {
  pthread_mutex_lock(&callMu);
  char *r = NULL; Uint32 t0 = SDL_GetTicks();
  if (helpers()) {
    int st = h_call(uri, payload, timeoutMs, &r);
    if (st == 0) { fprintf(stderr, "[luna] %s: no answer through the helper in %d ms: own handle from now on\n", uri, timeoutMs); pthread_mutex_lock(&hMu); hState = -1; pthread_mutex_unlock(&hMu); }
    if (st == 1 && !refused(r)) { pthread_mutex_unlock(&callMu); return r; }
    if (st == 1) log_refused("helper", uri, r);
  }
  int left = timeoutMs - (int)(SDL_GetTicks() - t0); if (left < 500) left = 500;
  char *o = own_call(uri, payload, left);
  if (o && refused(o)) log_refused("own handle", uri, o);
  if (o && (!r || !refused(o))) { free(r); r = o; } else free(o); /* the better of the two answers */
  pthread_mutex_unlock(&callMu); return r;
}
int plat_luna_send(const char *uri, const char *payload) {
  if (helpers()) { HCtx *h = calloc(1, sizeof *h); if (h) { h->cb = h_drop; h->pub = 1; if (!hCall(uri, payload, h)) return 1; free(h); } }
  pthread_mutex_lock(&callMu);
  int ok = 0;
  if (setup()) { LSErr e; memset(&e, 0, sizeof e); if (lsErrInit) lsErrInit(&e); unsigned long tok = 0;
    ok = lsCallOne(bus, uri, payload, on_drop, NULL, &tok, &e); if (!ok) fprintf(stderr, "[luna] %s failed: %s\n", uri, e.message ? e.message : "?"); err_reset(&e); }
  pthread_mutex_unlock(&callMu); return ok;
}

/* calls that answer more than once (the Homebrew Channel's install with its progress, the screen saver's
   requests): every reply reaches its callback from plat_luna_pump() on the main thread, so the callbacks never
   race the app. The own handle's replies are dispatched when its context runs there; the helper's arrive on its
   thread and wait in a queue */
#define SUBS 4
static struct { unsigned long tok; PlatLunaCb cb; void *ud; int on, viaHelper; unsigned gen; } subs[SUBS];
typedef struct { HCtx h; int slot; unsigned gen; } HSub; /* h first: the helper hands its address back */
static HSub hsubs[SUBS];
typedef struct QMsg { int slot; unsigned gen; char *p; struct QMsg *next; } QMsg;
static QMsg *qHead, *qTail; static pthread_mutex_t qMu = PTHREAD_MUTEX_INITIALIZER;
static unsigned long nextTok = 1u << 30; /* the helper's subscriptions: handles apart from the bus's own tokens */
static bool on_sub(LSHandle *h, LSMessage *m, void *u) {
  (void)h; int i = (int)(uintptr_t)u; if (i < 0 || i >= SUBS || !subs[i].on) return true;
  const char *p = lsPayload(m); subs[i].cb(p ? p : "", subs[i].ud); return true;
}
static bool h_sub(LSHandle *h, LSMessage *m, void *u) {
  (void)h; const char *p = hMsg(m); QMsg *q = calloc(1, sizeof *q); if (!q) return true;
  pthread_mutex_lock(&qMu); q->slot = ((HSub *)u)->slot; q->gen = ((HSub *)u)->gen; q->p = strdup(p ? p : "");
  if (qTail) qTail->next = q; else qHead = q; qTail = q; pthread_mutex_unlock(&qMu); return true;
}
unsigned long plat_luna_subscribe(const char *uri, const char *payload, PlatLunaCb cb, void *ud) {
  int i = 0; while (i < SUBS && subs[i].on) i++; if (i == SUBS) return 0;
  subs[i].cb = cb; subs[i].ud = ud; subs[i].gen++;
  if (helpers()) {
    pthread_mutex_lock(&qMu); memset(&hsubs[i].h, 0, sizeof hsubs[i].h); hsubs[i].h.cb = h_sub; hsubs[i].h.multiple = 1; hsubs[i].h.pub = 1; hsubs[i].slot = i; hsubs[i].gen = subs[i].gen; pthread_mutex_unlock(&qMu);
    if (!hCall(uri, payload, &hsubs[i].h)) { subs[i].on = 1; subs[i].viaHelper = 1; subs[i].tok = nextTok++; return subs[i].tok; }
  }
  pthread_mutex_lock(&callMu);
  if (!setup() || !lsCall) { pthread_mutex_unlock(&callMu); return 0; }
  LSErr e; memset(&e, 0, sizeof e); if (lsErrInit) lsErrInit(&e); unsigned long tok = 0;
  subs[i].on = 1; subs[i].viaHelper = 0;
  if (!lsCall(bus, uri, payload, on_sub, (void *)(uintptr_t)i, &tok, &e) || !tok) { fprintf(stderr, "[luna] %s failed: %s\n", uri, e.message ? e.message : "?"); err_reset(&e); subs[i].on = 0; pthread_mutex_unlock(&callMu); return 0; }
  err_reset(&e); subs[i].tok = tok; pthread_mutex_unlock(&callMu); return tok;
}
/* a subscription that must not go through the system helper library: libhelpers acknowledges the subscribe
   call and then never delivers its events (measured on webOS 6.4 against the TV's screen-saver request), while
   a plain subscription on the app's own handle receives them */
unsigned long plat_luna_subscribe_own(const char *uri, const char *payload, PlatLunaCb cb, void *ud) {
  int i = 0; while (i < SUBS && subs[i].on) i++; if (i == SUBS) return 0;
  subs[i].cb = cb; subs[i].ud = ud; subs[i].gen++;
  pthread_mutex_lock(&callMu);
  if (!setup() || !lsCall) { pthread_mutex_unlock(&callMu); return 0; }
  LSErr e; memset(&e, 0, sizeof e); if (lsErrInit) lsErrInit(&e); unsigned long tok = 0;
  subs[i].on = 1; subs[i].viaHelper = 0;
  if (!lsCall(bus, uri, payload, on_sub, (void *)(uintptr_t)i, &tok, &e) || !tok) { fprintf(stderr, "[luna] %s failed: %s\n", uri, e.message ? e.message : "?"); err_reset(&e); subs[i].on = 0; pthread_mutex_unlock(&callMu); return 0; }
  err_reset(&e); subs[i].tok = tok; pthread_mutex_unlock(&callMu); return tok;
}
/* the answer to such a subscription, from the same handle the request came to */
int plat_luna_send_own(const char *uri, const char *payload) {
  pthread_mutex_lock(&callMu);
  int ok = 0;
  if (setup()) { LSErr e; memset(&e, 0, sizeof e); if (lsErrInit) lsErrInit(&e); unsigned long tok = 0;
    ok = lsCallOne(bus, uri, payload, on_drop, NULL, &tok, &e); if (!ok) fprintf(stderr, "[luna] %s failed: %s\n", uri, e.message ? e.message : "?"); err_reset(&e); }
  pthread_mutex_unlock(&callMu); return ok;
}
void plat_luna_cancel(unsigned long handle) {
  for (int i = 0; i < SUBS; i++) if (subs[i].on && subs[i].tok == handle) { subs[i].on = 0;
    if (subs[i].viaHelper) { if (hUnreg) hUnreg(&hsubs[i].h); }
    else if (lsCallCancel) { LSErr e; memset(&e, 0, sizeof e); if (lsErrInit) lsErrInit(&e); lsCallCancel(bus, handle, &e); err_reset(&e); } }
}
void plat_luna_pump(void) {
  pthread_mutex_lock(&qMu); QMsg *q = qHead; qHead = qTail = NULL; pthread_mutex_unlock(&qMu);
  { while (q) { QMsg *n = q->next; if (subs[q->slot].on && subs[q->slot].viaHelper && subs[q->slot].gen == q->gen) subs[q->slot].cb(q->p, subs[q->slot].ud); free(q->p); free(q); q = n; } }
  int any = 0; for (int i = 0; i < SUBS; i++) any |= subs[i].on && !subs[i].viaHelper; if (!any || !bus) return;
  for (int n = 0; n < 8 && gCtxIter(ctx, 0); n++) {}
}
