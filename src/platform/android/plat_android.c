/* Android TV: SDL's Android port runs the app on its own Java thread with a GL ES 2 surface. The Java side
   (androidtv/app/src/.../OriginActivity.java) copies the bundled assets out of the APK into the app's files folder
   before SDL starts, and turns the remote's keys SDL does not know (the colour keys, channel up/down, Play and Pause
   alone) into ones it does: F1-F4, Page Up/Down, Play/Pause and Break. Everything the app writes stays in that same
   private folder. */
#include "platform/plat.h"
#include "platform/mem.h"
#include "core/keys.h"
#include "core/applog.h"
#include <android/log.h>
#include <sys/system_properties.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char assetDir[1024], dataDir[1024], devName[200];

/* stderr (where the app logs, as on the TV) goes to Android's log (adb logcat -s StremioOrigin) and to
   <files>/stremioorigin.log, the same file the webOS build keeps in /tmp */
static int logPipe[2] = { -1, -1 }; static FILE *logFile;
static void *log_pump(void *u) {
  (void)u; char buf[1024]; size_t have = 0;
  for (;;) {
    ssize_t n = read(logPipe[0], buf + have, sizeof buf - 1 - have); if (n <= 0) break; have += (size_t)n; buf[have] = 0;
    char *s = buf, *nl;
    while ((nl = strchr(s, '\n'))) { *nl = 0; __android_log_write(ANDROID_LOG_INFO, "StremioOrigin", s); if (logFile) { fputs(s, logFile); fputc('\n', logFile); fflush(logFile); } s = nl + 1; }
    have = strlen(s); memmove(buf, s, have + 1);
    if (have >= sizeof buf - 1) { __android_log_write(ANDROID_LOG_INFO, "StremioOrigin", buf); have = 0; } /* a line longer than the buffer: as it is */
  }
  return NULL;
}
static void prop(const char *k, char *out) { out[0] = 0; __system_property_get(k, out); }

void plat_pre_init(const char *appId) {
  (void)appId;
  const char *files = SDL_AndroidGetInternalStoragePath(); /* /data/user/0/<package>/files: private to the app */
  snprintf(dataDir, sizeof dataDir, "%s", files ? files : "/data/local/tmp");
  snprintf(assetDir, sizeof assetDir, "%s/assets", dataDir); /* filled by OriginActivity from the APK */
  mkdir(dataDir, 0700);
  char lp[1100]; snprintf(lp, sizeof lp, "%s/stremioorigin.log", dataDir); logFile = fopen(lp, "w");
  if (pipe(logPipe) == 0) { setvbuf(stderr, NULL, _IOLBF, 0); dup2(logPipe[1], 2); pthread_t t; pthread_create(&t, NULL, log_pump, NULL); pthread_detach(t); }
  char man[PROP_VALUE_MAX], model[PROP_VALUE_MAX], rel[PROP_VALUE_MAX];
  prop("ro.product.manufacturer", man); prop("ro.product.model", model); prop("ro.build.version.release", rel);
  snprintf(devName, sizeof devName, "Android TV %s %s (Android %s)", man, model, rel);
  fprintf(stderr, "[store] data in %s\n", dataDir);
  applog_start(dataDir);
  SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1"); /* Back is the app's (SDL would otherwise close the activity) */
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
}
void plat_post_window(SDL_Window *w) { (void)w; }
const char *plat_asset_dir(void) { return assetDir; }
const char *plat_data_dir(void) { return dataDir; }
/* Android hands the window the screen's size; the app draws in it at that size (gfx scales from 1920x1080) */
void plat_window_size(int *w, int *h, Uint32 *flags) {
  SDL_DisplayMode m; if (SDL_GetDesktopDisplayMode(0, &m) == 0 && m.w > 0 && m.h > 0) { *w = m.w; *h = m.h; } else { *w = 1920; *h = 1080; }
  if (*w > 1920) { *h = *h * 1920 / *w; *w = 1920; } /* a 4K panel: the interface is drawn at 1080p (as on the LG sets), the video keeps its own resolution */
  *flags = SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN;
}
int plat_map_key(const SDL_KeyboardEvent *e) {
  switch (e->keysym.scancode) {
    case SDL_SCANCODE_AC_BACK: return K_BACK;
    case SDL_SCANCODE_F1: return K_RED; case SDL_SCANCODE_F2: return K_GREEN; case SDL_SCANCODE_F3: return K_YELLOW; case SDL_SCANCODE_F4: return K_BLUE;
    case SDL_SCANCODE_PAGEUP: return K_CH_UP; case SDL_SCANCODE_PAGEDOWN: return K_CH_DOWN;
    case SDL_SCANCODE_AUDIOPLAY: return K_PLAYPAUSE;
    case SDL_SCANCODE_PAUSE: return K_PAUSE;
    case SDL_SCANCODE_AUDIOSTOP: case SDL_SCANCODE_STOP: return K_STOP;
    case SDL_SCANCODE_AUDIOREWIND: case SDL_SCANCODE_AUDIOPREV: return K_REW;
    case SDL_SCANCODE_AUDIOFASTFORWARD: case SDL_SCANCODE_AUDIONEXT: return K_FF;
    case SDL_SCANCODE_SELECT: return K_OK;
    default: break;
  }
  switch (e->keysym.sym) {
    case SDLK_LEFT: return K_LEFT; case SDLK_RIGHT: return K_RIGHT; case SDLK_UP: return K_UP; case SDLK_DOWN: return K_DOWN;
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SELECT: return K_OK;
    case SDLK_AC_BACK: case SDLK_ESCAPE: return K_BACK;
    default: break;
  }
  return K_NONE;
}
const char *plat_device_name(void) { return devName; }
int plat_text_input(void) { return 0; } /* the remote has no keyboard: the app draws one (as on the TV) */
void plat_thread_background(int nice) { setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), nice); }
int plat_mem_total_mb(void) {
  FILE *f = fopen("/proc/meminfo", "r"); if (!f) return 0;
  char line[128]; long kb = 0; while (fgets(line, sizeof line, f)) if (sscanf(line, "MemTotal: %ld kB", &kb) == 1) break;
  fclose(f); return (int)(kb / 1024);
}
/* Home, another app or the screensaver send the activity to the back; SDL tells it with its app events */
int plat_lifecycle(const SDL_Event *e) {
  static int away;
  if (e->type == SDL_APP_WILLENTERBACKGROUND || e->type == SDL_APP_DIDENTERBACKGROUND) { if (!away) fprintf(stderr, "[life] to the back\n"); away = 1; return -1; }
  if ((e->type == SDL_APP_WILLENTERFOREGROUND || e->type == SDL_APP_DIDENTERFOREGROUND) && away) { away = 0; fprintf(stderr, "[life] back in front\n"); return 1; }
  return 0;
}
/* the TV's language, as Android reports it to SDL ("es-ES") */
const char *plat_ui_locale(void) {
  static char loc[24]; static int asked;
  if (asked) return loc[0] ? loc : NULL; asked = 1;
  SDL_Locale *l = SDL_GetPreferredLocales();
  if (l && l[0].language) snprintf(loc, sizeof loc, "%s%s%s", l[0].language, l[0].country ? "-" : "", l[0].country ? l[0].country : "");
  if (l) SDL_free(l);
  fprintf(stderr, "[i18n] TV language: %s\n", loc[0] ? loc : "(no answer)");
  return loc[0] ? loc : NULL;
}
/* no luna bus off webOS: every call answers "not made" and the callers carry on without it */
char *plat_luna_call(const char *uri, const char *payload, int timeoutMs) { (void)uri; (void)payload; (void)timeoutMs; return NULL; }
unsigned long plat_luna_subscribe(const char *uri, const char *payload, PlatLunaCb cb, void *ud) { (void)uri; (void)payload; (void)cb; (void)ud; return 0; }
void plat_luna_cancel(unsigned long handle) { (void)handle; }
void plat_luna_pump(void) {}
int plat_luna_send(const char *uri, const char *payload) { (void)uri; (void)payload; return 0; }
int plat_panel_lines(void) { SDL_DisplayMode m; return SDL_GetDesktopDisplayMode(0, &m) == 0 ? m.h : 0; }

/* memory, from the kernel as on webOS */
int plat_mem_free_mb(void) {
  FILE *f = fopen("/proc/meminfo", "r"); if (!f) return -1;
  char line[128]; long kb = -1; while (fgets(line, sizeof line, f)) if (sscanf(line, "MemAvailable: %ld kB", &kb) == 1) break;
  fclose(f); return kb < 0 ? -1 : (int)(kb / 1024);
}
void plat_mem_self(int *rssMb, int *swapMb) {
  *rssMb = *swapMb = -1; FILE *f = fopen("/proc/self/status", "r"); if (!f) return;
  char line[128]; long kb; while (fgets(line, sizeof line, f)) { if (sscanf(line, "VmRSS: %ld kB", &kb) == 1) *rssMb = (int)(kb / 1024); else if (sscanf(line, "VmSwap: %ld kB", &kb) == 1) *swapMb = (int)(kb / 1024); }
  fclose(f);
}
