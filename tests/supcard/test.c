/* The support card's timing (core/supcard.c) against a fake store: due on the 5th real start, then after each close
   +10, +30, +50 and +100 every time; an autoReload never counts; due stays due until it shows; "Don't show again"
   lasts; a store from the old schedule (every 5th) carries on; then the card's keys */
#include "core/supcard.h"
#include <stdio.h>
#include <string.h>
static struct { char k[16][24]; int v[16], n; } F;
static int fget(const char *k, int d) { for (int i = 0; i < F.n; i++) if (!strcmp(F.k[i], k)) return F.v[i]; return d; }
static void fset(const char *k, int v) { for (int i = 0; i < F.n; i++) if (!strcmp(F.k[i], k)) { F.v[i] = v; return; } snprintf(F.k[F.n], 24, "%s", k); F.v[F.n++] = v; }
static const SupStore S = { fget, fset };
static int fails;
#define CHECK(c, what) do { if (!(c)) { fails++; printf("FAIL: %s\n", what); } } while (0)
static void reset(void) { memset(&F, 0, sizeof F); }
/* starts until it is due; returns the start number it became due at (closing it at once each time) */
static int next_due(void) { for (int i = 0; i < 1000; i++) { supc_start(&S, 0); if (supc_due(&S)) return fget("supStarts", 0); } return -1; }
int main(void) {
  char m[160];
  /* the due starts, each closed at once: 5, 15, 45, 95, 195, 295 ... */
  reset(); { int want[12] = { 5, 15, 45, 95, 195, 295, 395, 495, 595, 695, 795, 895 }; printf("due at starts:");
    for (int i = 0; i < 12; i++) { int d = next_due(); printf(" %d", d); snprintf(m, sizeof m, "due #%d at start %d (got %d)", i + 1, want[i], d); CHECK(d == want[i], m); supc_shown(&S); } printf("\n"); }
  reset(); for (int i = 1; i <= 4; i++) supc_start(&S, 0); CHECK(!supc_due(&S), "not due before the 5th start");
  supc_start(&S, 0); CHECK(supc_due(&S), "due on the 5th start");
  reset(); for (int i = 1; i <= 4; i++) supc_start(&S, 0); for (int i = 0; i < 5; i++) supc_start(&S, 1); CHECK(!supc_due(&S), "autoReload starts are not counted");
  supc_start(&S, 0); CHECK(supc_due(&S), "the 5th real start, after autoReloads");
  reset(); for (int i = 1; i <= 5; i++) supc_start(&S, 0); for (int i = 0; i < 3; i++) supc_start(&S, 0); CHECK(supc_due(&S), "due stays due until shown (went straight to playback)");
  supc_shown(&S); CHECK(!supc_due(&S), "closed: not due");
  for (int i = 0; i < 9; i++) supc_start(&S, 0); CHECK(!supc_due(&S), "closed at start 8: not due 9 starts later");
  supc_start(&S, 0); CHECK(supc_due(&S), "closed at start 8: due 10 starts later (18)");
  supc_shown(&S); for (int i = 0; i < 29; i++) supc_start(&S, 0); CHECK(!supc_due(&S), "2nd close: not due 29 starts later");
  supc_start(&S, 0); CHECK(supc_due(&S), "2nd close: due 30 starts later");
  reset(); for (int i = 1; i <= 5; i++) supc_start(&S, 0); supc_never(&S); CHECK(!supc_due(&S), "Don't show again: not due");
  for (int i = 6; i <= 400; i++) supc_start(&S, 0); CHECK(!supc_due(&S), "Don't show again lasts (to start 400)");
  /* a store from the old schedule (due every 5th) */
  reset(); fset("supStarts", 12); supc_start(&S, 0); CHECK(!supc_due(&S) && fget("supNext", 0) == 23, "old store, closed before: next 10 starts on (23)");
  reset(); fset("supStarts", 10); fset("supDue", 1); supc_start(&S, 0); CHECK(supc_due(&S), "old store, due: still due");
  reset(); fset("supStarts", 3); supc_start(&S, 0); CHECK(!supc_due(&S) && fget("supNext", 0) == 5, "old store, not shown yet: due at the 5th");
  reset(); fset("supStarts", 30); fset("supNever", 1); supc_start(&S, 0); CHECK(!supc_due(&S), "old store, Don't show again: lasts");
  /* the keys: the coffee's code first, OK turns it over and back, Left / Right pick a side (the same in both
     directions: the pill is not mirrored); Down reaches Don't show again, Up comes back, OK there ends it; Back closes */
  { SupFocus f = supc_focus_start();
    CHECK(f.side == 0 && !f.never, "starts on the codes, the coffee's");
    CHECK(supc_key(&f, SUPK_OK, 1) == SUPA_TURN && f.side == 1, "OK turns it to GitHub");
    CHECK(supc_key(&f, SUPK_OK, 1) == SUPA_TURN && f.side == 0, "OK again: the coffee");
    CHECK(supc_key(&f, SUPK_RIGHT, 1) == SUPA_TURN && f.side == 1, "Right: GitHub");
    CHECK(supc_key(&f, SUPK_RIGHT, 1) == SUPA_NUDGE && f.side == 1, "Right again: stays on GitHub");
    CHECK(supc_key(&f, SUPK_LEFT, 1) == SUPA_TURN && f.side == 0, "Left: the coffee");
    CHECK(supc_key(&f, SUPK_UP, 1) == SUPA_NUDGE && !f.never, "Up on the codes: nowhere to go");
    CHECK(supc_key(&f, SUPK_DOWN, 1) == SUPA_MOVE && f.never, "Down: Don't show again");
    CHECK(supc_key(&f, SUPK_DOWN, 1) == SUPA_NUDGE && f.never, "Down again: nowhere to go");
    CHECK(supc_key(&f, SUPK_LEFT, 1) == SUPA_NUDGE && supc_key(&f, SUPK_RIGHT, 1) == SUPA_NUDGE && f.never && f.side == 0, "Left / Right on Don't show again: nothing turns");
    CHECK(supc_key(&f, SUPK_UP, 1) == SUPA_MOVE && !f.never && f.side == 0, "Up: back on the codes, as they were");
    f.never = 1; CHECK(supc_key(&f, SUPK_BACK, 1) == SUPA_CLOSE, "Back on Don't show again closes (not for good)");
    CHECK(supc_key(&f, SUPK_OK, 1) == SUPA_NEVER, "OK on Don't show again ends it");
    f = supc_focus_start(); CHECK(supc_key(&f, SUPK_BACK, 1) == SUPA_CLOSE, "Back on the codes closes");
    f = supc_focus_start(); CHECK(supc_key(&f, SUPK_OK, 0) == SUPA_NUDGE && supc_key(&f, SUPK_RIGHT, 0) == SUPA_NUDGE && f.side == 0, "only the coffee's code: it stays");
  }
  printf("supcard: %s\n", fails ? "FAILED" : "all passed"); return fails ? 1 : 0;
}
