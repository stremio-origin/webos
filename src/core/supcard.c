/* The support card's timing (see supcard.h) */
#include "core/supcard.h"
#include <stdio.h>
#include <stdlib.h>

/* THE SCHEDULE, in starts: the first time at the 5th start; then, after each close, that many starts more (after the
   1st close 10, the 2nd 30, the 3rd 50, then 100 every time: the last number repeats). One line to change. */
static const int GAPS[] = { 5, 10, 30, 50, 100 };
#define NGAPS ((int)(sizeof GAPS / sizeof *GAPS))
int supc_gap(int closes) { return GAPS[closes < 0 ? 0 : closes >= NGAPS ? NGAPS - 1 : closes]; }

void supc_start(const SupStore *s, int autoReload) {
  if (autoReload) return; /* (the TV starting it again on its own: not the viewer's start) */
  int old = s->get("supStarts", 0), n = old + 1; s->set("supStarts", n);
  if (!s->get("supNext", 0)) { /* (first run of this schedule; a store from the old one, due every 5th, carries on) */
    if (s->get("supDue", 0)) s->set("supNext", n > GAPS[0] ? n : GAPS[0]);   /* it was due: still due */
    else if (old >= GAPS[0]) { s->set("supCloses", 1); s->set("supNext", n + supc_gap(1)); } /* it has shown and been closed */
    else s->set("supNext", GAPS[0]); }
}
int  supc_due(const SupStore *s) { return !s->get("supNever", 0) && s->get("supNext", 0) > 0 && s->get("supStarts", 0) >= s->get("supNext", 0); } /* (stays due until it shows) */
void supc_shown(const SupStore *s) { int c = s->get("supCloses", 0) + 1; s->set("supCloses", c); s->set("supNext", s->get("supStarts", 0) + supc_gap(c)); s->set("supDue", 0); }
void supc_never(const SupStore *s) { s->set("supNever", 1); s->set("supDue", 0); }

SupFocus supc_focus_start(void) { SupFocus s = { 0, 0 }; return s; }
int supc_key(SupFocus *s, int key, int hasGh) {
  if (key == SUPK_BACK) return SUPA_CLOSE; /* (Back closes from anywhere, and never ends it) */
  if (s->never) { /* on "Don't show again" */
    if (key == SUPK_OK) return SUPA_NEVER;
    if (key == SUPK_UP) { s->never = 0; return SUPA_MOVE; }
    return SUPA_NUDGE; }
  if (key == SUPK_DOWN) { s->never = 1; return SUPA_MOVE; }
  if (key == SUPK_OK) { if (!hasGh) return SUPA_NUDGE; s->side ^= 1; return SUPA_TURN; }
  if (key == SUPK_LEFT || key == SUPK_RIGHT) { int want = key == SUPK_RIGHT; if (!hasGh || s->side == want) return SUPA_NUDGE; s->side = want; return SUPA_TURN; }
  return SUPA_NUDGE; /* (Up on the codes: nowhere to go) */
}

#ifndef SUPC_TEST
#include "core/store.h"
static int st_get(const char *k, int def) { const char *v = store_get(k, NULL); return v && *v ? atoi(v) : def; }
static void st_set(const char *k, int v) { char b[16]; snprintf(b, sizeof b, "%d", v); store_set(k, b); }
const SupStore *supc_store(void) { static const SupStore s = { st_get, st_set }; return &s; }
#endif
