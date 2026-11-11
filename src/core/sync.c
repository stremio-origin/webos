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
  (void)ud; int l = plat_lifecycle(e);
  if (l > 0) { away = 0; resumed = 1; } else if (l < 0) away = 1;
  return 0;
}
/* a pull that changed the library: applied in a pause of the remote (a rebuild in the middle of a move is a
   hitch), at most 5 s later */
static void on_lib(int changed, void *ud) { (void)ud; if (!changed) return; board_invalidate_home(); if (!rebuildWanted) { rebuildWanted = 1; rebuildSince = app_now(); } }
static void on_addons(int changed, void *ud) {
  (void)ud; if (!changed) return;
  toast(T("Addons updated"), 2600, NULL);
  board_invalidate(); if ((SCR == SCR_HOME || SCR == SCR_DISC || SCR == SCR_ANIME) && !player_open()) board_rebuild();
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
void sync_tick(void) {
  double now = app_now();
  if (!libBoot && now - bootAt >= 0.7) { libBoot = 1; sb_pull_library(on_lib, NULL); }
  if (!addonBoot && now - bootAt >= 0.9) { addonBoot = 1; sb_sync_addons(on_addons, NULL); }
  if (resumed) { resumed = 0; if (sb_signed_in()) sb_sync_soon(1); } /* back from another app, or relaunched */
  if (now - idleAt >= 240) { idleAt = now; if (!away && app_idle() > 20 && sb_signed_in()) sb_sync_soon(0); }
  if (rebuildWanted && (app_idle() >= 0.7 || now - rebuildSince >= 5)) {
    rebuildWanted = 0;
    if (SCR == SCR_HOME && !player_open()) board_rebuild(); /* Library polls lib_version() itself; elsewhere: rebuilt when shown */
  }
}
