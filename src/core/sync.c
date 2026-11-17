#include "core/notify.h"
#include "core/away.h"
#include "core/sync.h"
#include "core/app.h"
#include "core/i18n.h"
#include "screens/board.h"
#include "screens/player.h"
#include "platform/plat.h"
#include <SDL2/SDL.h>

static double bootAt, idleAt; static int libBoot, addonBoot, away;
static volatile int resumed;               /* set from SDL's event watch, read once a frame */
static int rebuildWanted; static double rebuildSince;

/* events are watched here, not taken: the frame loop still sees all of them */
static int SDLCALL on_event(void *ud, SDL_Event *e) {
  (void)ud; int l = plat_lifecycle(e); away_focus(l); /* (the focus for the whole app: core/away.c) */
  if (l > 0) { away = 0; resumed = 1; } else if (l < 0) away = 1;
  return 0;
}
/* a pull that changed the library: applied in a pause of the remote (a rebuild in the middle of a move is a
   hitch), at most 5 s later */
static void on_lib(int changed, void *ud) { (void)ud; if (!changed) return; board_invalidate(); if (!rebuildWanted) { rebuildWanted = 1; rebuildSince = app_now(); } }
static void on_addons(int changed, void *ud) {
  (void)ud; if (changed <= 0) return;
  toast(T("Addons updated"), 2600, NULL);
  board_invalidate(); if (SCR == SCR_HOME && !player_open()) board_rebuild();
}
void sb_sync_soon(int force) {
  if (!libBoot) return; /* the pull a moment after start covers it */
  if (!force && sb_lib_sync_age() < 60000) return;
  sb_pull_library(on_lib, NULL);
}
void sync_init(void) {
  bootAt = idleAt = app_now();
  if (!sb_signed_in()) libBoot = addonBoot = 1; /* signing in fetches both itself */
  SDL_AddEventWatch(on_event, NULL);
}
static void on_notify(void) { board_invalidate(); if (!rebuildWanted) { rebuildWanted = 1; rebuildSince = app_now(); } } /* new episodes or what is coming changed: the rows again, in a pause */
void sync_tick(void) {
  double now = app_now();
  notify_tick(on_notify);
  if (!libBoot && now - bootAt >= 0.7) { libBoot = 1; sb_pull_library(on_lib, NULL); }
  if (!addonBoot && now - bootAt >= 0.9) { addonBoot = 1; sb_sync_addons(on_addons, NULL); }
  if (resumed) { resumed = 0; if (sb_signed_in()) { sb_sync_soon(1); if (addonBoot) sb_sync_addons(on_addons, NULL); } /* (the addons too: one set up again on another device meanwhile) */ extern void app_trailer_wait(void); app_trailer_wait(); } /* back from another app, or relaunched (a phone's relaunch: its play comes before any trailer) */
  if (now - idleAt >= 240) { idleAt = now; if (!away && app_idle() > 20 && sb_signed_in()) sb_sync_soon(0); }
  if (rebuildWanted && (app_idle() >= 0.7 || now - rebuildSince >= 5)) {
    rebuildWanted = 0;
    if ((SCR == SCR_HOME || SCR == SCR_LIB) && !player_open()) board_rebuild(); /* elsewhere: rebuilt when shown */
  }
}
