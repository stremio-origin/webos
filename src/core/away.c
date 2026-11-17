/* The app behind another app (see away.h) */
#include "core/away.h"
#include "core/json.h"
#include "platform/plat.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

extern int app_splash_done;
extern void app_back_in_front(void);   /* core/app.c: the remote's idle time and the screen's rate, afresh */
extern void btr_release(void);         /* screens/btrailer.c: the background trailer, its sound and decoder, gone now */
extern void player_away(int behind);
extern void lpk_away(void);            /* screens/livepeek.c: a channel previewing goes */   /* screens/player.c: behind, a film playing is paused; in front again, its controls */

static volatile int unfocused;  /* (set from SDL's event watch: the main thread's own event pump) */
static struct { int asked, trusted, refused, gotFirst; unsigned long sub; char fg[96], own[96]; } F;
static int behind;
static pthread_mutex_t fgMu = PTHREAD_MUTEX_INITIALIZER; /* (on a TV without the helper library the replies come on another thread) */

int app_focused(void) { return !unfocused; }
int app_behind(void) { return behind; }
void away_focus(int l) { if (l < 0) unfocused = 1; else if (l > 0) unfocused = 0; }

/* {"appId":"com.webos.app.home", ...}, or {"returnValue":false, ...} when refused */
static void on_fg(const char *p, void *ud) {
  (void)ud; cJSON *j = cJSON_Parse(p ? p : ""); const cJSON *rv = cJSON_GetObjectItemCaseSensitive(j, "returnValue");
  if (cJSON_IsFalse(rv)) { if (!F.refused) fprintf(stderr, "[life] which app is in front: refused (%.120s): never taken as behind\n", p ? p : ""); F.refused = 1; F.trusted = 0; cJSON_Delete(j); if (F.sub) { plat_luna_cancel(F.sub); F.sub = 0; } return; }
  const char *id = js_str(j, "appId", NULL); if (!id) { cJSON_Delete(j); return; } /* (a reply without it: nothing said) */
  pthread_mutex_lock(&fgMu); snprintf(F.fg, sizeof F.fg, "%s", id); pthread_mutex_unlock(&fgMu); cJSON_Delete(j);
  if (!F.gotFirst) { F.gotFirst = 1; /* asked while in front: it must name this app, else it is not believed for this run */
    F.trusted = !unfocused && !strcmp(F.fg, F.own);
    fprintf(stderr, "[life] which app is in front: %s (%s)\n", F.fg, F.trusted ? "this one: believed" : "not this one while in front: not believed, never taken as behind"); }
  else fprintf(stderr, "[life] in front: %s\n", F.fg[0] ? F.fg : "(none)");
}
void away_tick(void) {
  if (!F.asked && app_splash_done && !unfocused) { F.asked = 1; /* (once, after the start, while in front) */
    const char *a = getenv("APPID"); snprintf(F.own, sizeof F.own, "%s", a && *a ? a : "tv.stremioorigin.app");
    F.sub = plat_luna_subscribe("luna://com.webos.applicationManager/getForegroundAppInfo", "{\"subscribe\":true}", on_fg, NULL); }
  pthread_mutex_lock(&fgMu);
  if (!unfocused && F.trusted && strcmp(F.fg, F.own)) snprintf(F.fg, sizeof F.fg, "%s", F.own); /* in front: this app is, whatever was last said (an update missed must never make the next overlay "another app") */
  int b = unfocused && F.trusted && F.fg[0] && strcmp(F.fg, F.own); /* (an empty name: no other app said) */
  char fg[96]; snprintf(fg, sizeof fg, "%s", F.fg); pthread_mutex_unlock(&fgMu);
  if (b == behind) return;
  behind = b;
  if (b) { fprintf(stderr, "[life] behind %s: nothing drawn, a film playing paused, no trailer\n", fg); btr_release(); lpk_away(); player_away(1); }
  else { fprintf(stderr, "[life] in front again\n"); app_back_in_front(); player_away(0); }
}
