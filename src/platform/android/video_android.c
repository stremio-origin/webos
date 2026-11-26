/* Android TV: Media3 (ExoPlayer), the player Android TV apps use, on a SurfaceView under SDL's transparent GL surface
   (the same arrangement as webOS's video plane under the app's window). The Java side is OriginPlayer.java: it owns
   the player on a thread of its own; every call here only posts to it, and video_pump() reads one snapshot of its state
   per frame, so the drawing thread never waits on the player. */
#include "core/video.h"
#include <SDL2/SDL.h>
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static jclass P; /* tv.stremioorigin.app.OriginPlayer, held from JNI_OnLoad (the app's class loader is known only there) */
static jmethodID mInit, mLoad, mUnload, mPlay, mPause, mSeek, mVolume, mAudio, mText, mWindow, mPoll, mError, mTracks, mHdr;

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
  (void)reserved; JNIEnv *env; if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) return JNI_VERSION_1_6;
  jclass c = (*env)->FindClass(env, "tv/stremioorigin/app/OriginPlayer");
  if (!c) { (*env)->ExceptionClear(env); return JNI_VERSION_1_6; }
  P = (*env)->NewGlobalRef(env, c);
#define M(v, n, s) v = (*env)->GetStaticMethodID(env, P, n, s)
  M(mInit, "init", "()V"); M(mLoad, "load", "(Ljava/lang/String;)V"); M(mUnload, "unload", "()V"); M(mPlay, "play", "()V"); M(mPause, "pause", "()V");
  M(mSeek, "seek", "(D)V"); M(mVolume, "volume", "(II)V"); M(mAudio, "selectAudio", "(I)V"); M(mText, "selectText", "(I)V");
  M(mWindow, "window", "(IIII)V"); M(mPoll, "poll", "()[D"); M(mError, "error", "()Ljava/lang/String;"); M(mTracks, "tracks", "(I)Ljava/lang/String;");
  M(mHdr, "hdr", "()Ljava/lang/String;");
#undef M
  if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); P = NULL; }
  return JNI_VERSION_1_6;
}
static JNIEnv *env_(void) { return P ? (JNIEnv *)SDL_AndroidGetJNIEnv() : NULL; }
static void chk(JNIEnv *e) { if ((*e)->ExceptionCheck(e)) { (*e)->ExceptionDescribe(e); (*e)->ExceptionClear(e); } }
static void call_v(jmethodID m) { JNIEnv *e = env_(); if (e && m) { (*e)->CallStaticVoidMethod(e, P, m); chk(e); } }

/* the snapshot read once a frame */
enum { S_STATE, S_POS, S_DUR, S_BUF, S_W, S_H, S_FPS, S_LIVE, S_MEDIA, S_AUNSUP, S_SETTLING, S_CURAUD, S_N };
static double snap[S_N];
static char err[200], hdrS[24];
static VidTrack aud[16], txt[24]; static int naud, ntxt; static unsigned trackGen, trackSeen = ~0u;
static struct { int on; char base[4096]; double off, dur; } timed;
static int winX = -1, winY, winW, winH;

int video_init(const char *appId) { (void)appId; if (!P) { fprintf(stderr, "[video] no OriginPlayer class: no playback\n"); return 0; } call_v(mInit); return 1; }
static void load_url(const char *u) {
  JNIEnv *e = env_(); if (!e) return; jstring s = (*e)->NewStringUTF(e, u); (*e)->CallStaticVoidMethod(e, P, mLoad, s); (*e)->DeleteLocalRef(e, s); chk(e);
  memset(snap, 0, sizeof snap); snap[S_STATE] = VID_LOADING; err[0] = 0; naud = ntxt = 0; trackSeen = ~0u;
}
void video_load(const char *url) { timed.on = 0; fprintf(stderr, "[video] load %.200s\n", url); load_url(url); }
void video_load_timed(const char *base, double durSec) {
  timed.on = 1; snprintf(timed.base, sizeof timed.base, "%s", base); timed.off = 0; timed.dur = durSec;
  char u[4200]; snprintf(u, sizeof u, "%s?t=0", base); fprintf(stderr, "[video] load (timed) %.200s\n", u); load_url(u);
}
void video_unload(void) { timed.on = 0; call_v(mUnload); memset(snap, 0, sizeof snap); naud = ntxt = 0; }
void video_play(void) { call_v(mPlay); }
void video_pause(void) { call_v(mPause); }
void video_seek(double sec) {
  if (sec < 0) sec = 0;
  if (timed.on) { /* one continuous conversion: a seek is a new load from that moment */
    char u[4200]; snprintf(u, sizeof u, "%s?t=%.0f", timed.base, sec); load_url(u); timed.off = sec; return; }
  JNIEnv *e = env_(); if (e) { (*e)->CallStaticVoidMethod(e, P, mSeek, (jdouble)sec); chk(e); snap[S_POS] = sec; snap[S_SETTLING] = 1; }
}
double video_time(void) { return snap[S_POS] + (timed.on ? timed.off : 0); }
double video_duration(void) { return timed.on ? timed.dur : snap[S_DUR]; }
double video_buffered_end(void) { return snap[S_BUF] + (timed.on ? timed.off : 0); }
VidState video_state(void) { return (VidState)(int)snap[S_STATE]; }
const char *video_error(void) { return err; }
int video_live(void) { return snap[S_LIVE] > 0; }
int video_has_media(void) { return snap[S_MEDIA] > 0; }
void video_set_volume(int pct, int easeMs) { JNIEnv *e = env_(); if (e) { (*e)->CallStaticVoidMethod(e, P, mVolume, (jint)pct, (jint)easeMs); chk(e); } }
int video_settling(void) { return snap[S_SETTLING] > 0; }
int video_width(void) { return (int)snap[S_W]; }
int video_height(void) { return (int)snap[S_H]; }
double video_fps(void) { return snap[S_FPS]; }
int video_hdr(char *out, int cap) { if (cap > 0) snprintf(out, (size_t)cap, "%s", hdrS); return hdrS[0] != 0; }

/* "idx\tlang\tname\tcodec\tchannels\tforced\n" per track */
static int parse_tracks(const char *s, VidTrack *out, int max) {
  int n = 0; while (s && *s && n < max) {
    VidTrack *t = &out[n]; memset(t, 0, sizeof *t); const char *nl = strchr(s, '\n'); size_t len = nl ? (size_t)(nl - s) : strlen(s);
    char line[256]; if (len >= sizeof line) len = sizeof line - 1; memcpy(line, s, len); line[len] = 0;
    char *f[6] = { 0 }; int k = 0; char *p = line; f[k++] = p; while (*p && k < 6) { if (*p == '\t') { *p = 0; f[k++] = p + 1; } p++; }
    if (k >= 6) { t->idx = atoi(f[0]); snprintf(t->lang, sizeof t->lang, "%s", f[1]); snprintf(t->name, sizeof t->name, "%s", f[2]); snprintf(t->codec, sizeof t->codec, "%s", f[3]); t->channels = atoi(f[4]); t->forced = atoi(f[5]); n++; }
    s = nl ? nl + 1 : NULL; }
  return n;
}
static void refresh_tracks(void) {
  if (trackSeen == trackGen) return; trackSeen = trackGen;
  JNIEnv *e = env_(); if (!e) return;
  for (int kind = 0; kind < 2; kind++) {
    jstring s = (jstring)(*e)->CallStaticObjectMethod(e, P, mTracks, (jint)kind); chk(e);
    const char *c = s ? (*e)->GetStringUTFChars(e, s, NULL) : NULL;
    if (kind == 0) naud = parse_tracks(c, aud, 16); else ntxt = parse_tracks(c, txt, 24);
    if (c) (*e)->ReleaseStringUTFChars(e, s, c); if (s) (*e)->DeleteLocalRef(e, s); }
}
int video_audio_tracks(VidTrack *out, int max) { refresh_tracks(); int n = naud < max ? naud : max; memcpy(out, aud, (size_t)n * sizeof *out); return n; }
int video_audio_current(void) { return (int)snap[S_CURAUD]; }
void video_select_audio(int idx) { JNIEnv *e = env_(); if (e) { (*e)->CallStaticVoidMethod(e, P, mAudio, (jint)idx); chk(e); } }
int video_text_tracks(VidTrack *out, int max) { refresh_tracks(); int n = ntxt < max ? ntxt : max; memcpy(out, txt, (size_t)n * sizeof *out); return n; }
void video_select_text(int idx) { JNIEnv *e = env_(); if (e) { (*e)->CallStaticVoidMethod(e, P, mText, (jint)idx); chk(e); } }

/* the picture's rectangle in layout units (1920x1080); the Java side scales it to the screen */
void video_set_window(float x, float y, float w, float h) {
  int X = (int)x, Y = (int)y, W = (int)w, H = (int)h;
  if (X < 0) { W += X; X = 0; } if (Y < 0) { H += Y; Y = 0; } if (X + W > 1920) W = 1920 - X; if (Y + H > 1080) H = 1080 - Y;
  if (X == winX && Y == winY && W == winW && H == winH) return; winX = X; winY = Y; winW = W; winH = H;
  JNIEnv *e = env_(); if (e) { (*e)->CallStaticVoidMethod(e, P, mWindow, (jint)X, (jint)Y, (jint)W, (jint)H); chk(e); }
}
int video_partial_plane(void) { return 1; }
int video_set_crop(int sx, int sy, int sw, int sh, float dx, float dy, float dw, float dh) { (void)sx; (void)sy; (void)sw; (void)sh; (void)dx; (void)dy; (void)dw; (void)dh; return 0; } /* the picture stays whole */
void video_set_surface(int w, int h) { (void)w; (void)h; }
int video_audio_unsupported(void) { return snap[S_AUNSUP] > 0; }

void video_pump(void) {
  JNIEnv *e = env_(); if (!e) return;
  jdoubleArray a = (jdoubleArray)(*e)->CallStaticObjectMethod(e, P, mPoll); chk(e); if (!a) return;
  jsize n = (*e)->GetArrayLength(e, a); double v[S_N + 2]; memset(v, 0, sizeof v);
  (*e)->GetDoubleArrayRegion(e, a, 0, n < S_N + 1 ? n : S_N + 1, v); (*e)->DeleteLocalRef(e, a);
  int prev = (int)snap[S_STATE]; memcpy(snap, v, sizeof snap);
  unsigned gen = (unsigned)v[S_N]; if (gen != trackGen) trackGen = gen;
  if ((int)snap[S_STATE] == VID_ERROR && prev != VID_ERROR) {
    jstring s = (jstring)(*e)->CallStaticObjectMethod(e, P, mError); chk(e);
    const char *c = s ? (*e)->GetStringUTFChars(e, s, NULL) : NULL; snprintf(err, sizeof err, "%s", c ? c : "");
    if (c) (*e)->ReleaseStringUTFChars(e, s, c); if (s) (*e)->DeleteLocalRef(e, s);
    fprintf(stderr, "[video] error: %s\n", err); }
  if (snap[S_LIVE] > 0 && !hdrS[0]) {
    jstring s = (jstring)(*e)->CallStaticObjectMethod(e, P, mHdr); chk(e);
    const char *c = s ? (*e)->GetStringUTFChars(e, s, NULL) : NULL; snprintf(hdrS, sizeof hdrS, "%s", c ? c : "");
    if (c) (*e)->ReleaseStringUTFChars(e, s, c); if (s) (*e)->DeleteLocalRef(e, s); }
  if (snap[S_STATE] == VID_LOADING || snap[S_STATE] == VID_IDLE) hdrS[0] = 0;
}
