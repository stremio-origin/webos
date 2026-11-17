/* Seek previews, the measuring spike (development only: SB_PREVSPIKE=<frames>, off otherwise). Once a film plays
   steadily, one low-priority thread reads that many of the video's keyframes spread over it (the MKV reader's own
   index: each keyframe's block read through a handle of its own), decodes each one to a small picture through the
   converter library's optional preview call, and logs what each cost: bytes, requests, read time, decode CPU and wall
   time, memory, and how far the film was buffered before and after. Nothing is drawn and nothing is kept: it only
   measures. It waits while less than 30 s is buffered (15 s stops it) and gives way to a seek. SB_PREVW: the
   picture's width (256), SB_PREVPACE: ms between frames (500), SB_PREVDUMP: a folder for the first pictures (PPM). */
#include "core/prevspike.h"
#include "core/net.h"
#include "core/sbdts.h"
#include "platform/plat.h"
#include "core/store.h"
#include <SDL2/SDL.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>
#include <unistd.h>

static struct { pthread_t th; int on, stop; MkvSession *mkv; double ahead, pos, steadySince; int seekGen; char url[2048]; } S;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;

static double mono(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static double cpu(void) { struct timespec t; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static long rss_kb(void) { /* resident now (Linux), else the peak */
#ifdef __linux__
  FILE *f = fopen("/proc/self/statm", "r"); long a = 0, r = 0; if (f) { if (fscanf(f, "%ld %ld", &a, &r) != 2) r = 0; fclose(f); } if (r) return r * (sysconf(_SC_PAGESIZE) / 1024);
#endif
  struct rusage u; getrusage(RUSAGE_SELF, &u);
#ifdef __APPLE__
  return u.ru_maxrss / 1024;
#else
  return u.ru_maxrss;
#endif
}
static long peak_kb(void) { struct rusage u; getrusage(RUSAGE_SELF, &u);
#ifdef __APPLE__
  return u.ru_maxrss / 1024;
#else
  return u.ru_maxrss;
#endif
}
/* the converter library beside the app, loaded once whoever asks first (pthread_once: a race with another thread
   can't see it half done); its own load for the sound converter is a separate one, dlopen counts them; never closed */
static const sbd_prev_api *loaded;
static void load_once(void) {
  char *base = SDL_GetBasePath(), p[1200];
#ifdef __APPLE__
  snprintf(p, sizeof p, "%slibsbdts.dylib", base ? base : "");
#else
  snprintf(p, sizeof p, "%slibsbdts.so", base ? base : "");
#endif
  SDL_free(base);
  void *h = NULL;
#ifdef __ANDROID__
  h = dlopen("libsbprev.so", RTLD_NOW | RTLD_LOCAL); /* (a phone: a preview-only decoder, by name from the app's own libraries; else the converter's) */
  if (!h) h = dlopen("libsbdts.so", RTLD_NOW | RTLD_LOCAL);
#endif
  if (!h) h = dlopen(p, RTLD_NOW | RTLD_LOCAL); if (!h) { fprintf(stderr, "[prev] no converter library\n"); return; }
  const sbd_prev_api *(*get)(void) = (const sbd_prev_api *(*)(void))dlsym(h, "sbd_prev_get");
  const sbd_prev_api *a = get ? get() : NULL; if (!a || a->api() != SBD_PREV_VERSION) { fprintf(stderr, "[prev] the library has no preview decoder\n"); return; }
  loaded = a; fprintf(stderr, "[prev] the library can make previews\n");
}
static const sbd_prev_api *load(void) { static pthread_once_t once = PTHREAD_ONCE_INIT; pthread_once(&once, load_once); return loaded; }
static volatile int loadedKnown;
static void *load_bg(void *a) { (void)a; plat_thread_background(10); load(); loadedKnown = 1; return NULL; }
int prev_available(void) { /* never loads on the caller's thread: the first ask starts it in the background, "no" until it is in */
  static int asked; if (!asked) { asked = 1; pthread_t t; if (!pthread_create(&t, NULL, load_bg, NULL)) pthread_detach(t); }
  return loadedKnown && loaded != NULL; }
int prev_enabled(void) { return strcmp(store_get("seekPreviews", "true"), "false") != 0; }
static int stopping(void) { pthread_mutex_lock(&mu); int s = S.stop; pthread_mutex_unlock(&mu); return s; }
static double ahead_now(void) { pthread_mutex_lock(&mu); double a = S.ahead; pthread_mutex_unlock(&mu); return a; }

static void *run(void *arg) { (void)arg; plat_thread_background(10);
  MkvSession *m = S.mkv; const sbd_prev_api *A = load(); NetRange *net = NULL; void *dec = NULL; unsigned char *priv = NULL; long privLen = 0; char codec[32] = "";
  int want = atoi(getenv("SB_PREVSPIKE")), W = getenv("SB_PREVW") ? atoi(getenv("SB_PREVW")) : 256, pace = getenv("SB_PREVPACE") ? atoi(getenv("SB_PREVPACE")) : 500; const char *dump = getenv("SB_PREVDUMP");
  if (want < 2) want = 48; if (W < 64 || W > 512) W = 256; int H = W * 9 / 16; H &= ~1;
  int ok = 0, fails = 0, nk = mkv_key_count(m); double sumMs = 0, maxCpu = 0, sumCpu = 0, sumWall = 0, t0 = mono(); long sumBytes = 0, base = rss_kb(), peak0 = peak_kb();
  if (!A) goto out;
  int vt = mkv_video_track(m, codec, sizeof codec, &priv, &privLen); if (vt != 1) { fprintf(stderr, "[prev] the video track can't be read as stored (%d)\n", vt); goto out; }
  long d0 = rss_kb(); dec = A->open(codec, priv, (int)privLen); if (!dec) { fprintf(stderr, "[prev] no decoder for %s\n", codec); goto out; }
  net = net_range_new(); if (!net) goto out;
  fprintf(stderr, "[prev] spike: %s, %d keyframes known, %d asked, %dx%d pictures, %d ms apart; memory %ld KB (+%ld with the decoder open)\n", codec, nk, want, W, H, pace, base, rss_kb() - d0);
  unsigned char *rgb = malloc((size_t)W * H * 3); long guess = 192 * 1024; int dumped = 0;
  for (int i = 0; i < want && !stopping() && !mkv_dead(m); i++) {
    while (!stopping() && !mkv_dead(m) && ahead_now() >= 0 && ahead_now() < 30) SDL_Delay(500); /* (less than 30 s buffered: wait; -1 is a source that doesn't say) */
    if (stopping() || mkv_dead(m)) break;
    int k = (int)((long long)i * (nk - 1) / (want - 1)); double kt = 0, a0 = ahead_now(), r0 = mono(); unsigned char *fr; long fl, by; int rq;
    int e = mkv_key_read(m, net, k, guess, 3 * 1024 * 1024, &fr, &fl, &kt, &by, &rq); double r1 = mono(); sumBytes += by;
    if (e) { fails++; fprintf(stderr, "[prev] #%d key %d at %.1f s: not read (%d) after %.0f ms, %ld B in %d request(s)\n", i, k, kt, e, (r1 - r0) * 1000, by, rq); SDL_Delay(pace); continue; }
    guess = (long)(guess * 0.7 + fl * 1.25 * 0.3); /* (the next read sized to the frames seen) */
    int sw = 0, sh = 0, dark = 0; float dar = 0; double c0 = cpu(), w0 = mono(); int dr = A->frame(dec, fr, (int)fl, W, H, rgb, &sw, &sh, &dar, &dark); double c1 = cpu(), w1 = mono(); free(fr);
    double dc = (c1 - c0) * 1000, dw = (w1 - w0) * 1000; long now = rss_kb();
    if (dr) { fails++; fprintf(stderr, "[prev] #%d key %d at %.1f s: %ld B read in %.0f ms (%d req), not decoded (%.0f ms)\n", i, k, kt, fl, (r1 - r0) * 1000, rq, dw); }
    else { ok++; sumMs += (r1 - r0) * 1000; sumCpu += dc; sumWall += dw; if (dc > maxCpu) maxCpu = dc;
      fprintf(stderr, "[prev] #%d key %d at %.1f s: %ld B, %d req, read %.0f ms; decode %.0f ms CPU (%.0f wall), %dx%d%s; memory %ld KB (%+ld); buffered %.0f -> %.0f s\n",
        i, k, kt, fl, rq, (r1 - r0) * 1000, dc, dw, sw, sh, dark ? " dark" : "", now, now - base, a0, ahead_now());
      if (dump && dumped < 6) { char fn[1400]; snprintf(fn, sizeof fn, "%s/prev%02d.ppm", dump, dumped++); FILE *f = fopen(fn, "wb"); if (f) { fprintf(f, "P6 %d %d 255\n", W, H); fwrite(rgb, 1, (size_t)W * H * 3, f); fclose(f); } } }
    SDL_Delay(pace);
  }
  free(rgb);
  fprintf(stderr, "[prev] spike done in %.0f s: %d pictures, %d failed; %ld KB read (%.0f KB a picture); read %.0f ms, decode %.0f ms CPU (%.0f wall, the most %.0f) on average; memory now %+ld KB, peak %+ld KB\n",
    mono() - t0, ok, fails, sumBytes / 1024, ok ? sumBytes / 1024.0 / ok : 0, ok ? sumMs / ok : 0, ok ? sumCpu / ok : 0, ok ? sumWall / ok : 0, maxCpu, rss_kb() - base, peak_kb() - peak0);
out:
  if (dec) A->close(dec); free(priv); net_range_free(net); mkv_release(m);
  pthread_mutex_lock(&mu); S.on = 0; pthread_mutex_unlock(&mu); return NULL;
}

void prevspike_tick(MkvSession *mkv, int playing, double pos, double ahead) {
  if (!getenv("SB_PREVSPIKE") || !mkv || !prev_enabled()) return;
  double now = mono(); pthread_mutex_lock(&mu); S.ahead = ahead; S.pos = pos;
  if (!playing) S.steadySince = 0; else if (!S.steadySince) S.steadySince = now;
  static MkvSession *done; int start = !S.on && done != mkv && S.steadySince && now - S.steadySince >= 15 && mkv_key_count(mkv) > 1;
  if (start) { S.on = 1; S.stop = 0; S.mkv = mkv; done = mkv; mkv_retain(mkv); if (pthread_create(&S.th, NULL, run, NULL)) { S.on = 0; mkv_release(mkv); } else pthread_detach(S.th); }
  pthread_mutex_unlock(&mu);
}
void prevspike_stop(void) { pthread_mutex_lock(&mu); S.stop = 1; S.steadySince = 0; pthread_mutex_unlock(&mu); }
