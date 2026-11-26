/* SB_STRESS=<loops>: the app drives itself hard through every page (held arrows, fast tab switches, title pages,
   sheets, the card menu, Search, Settings, the player when SB_STRESS_PLAY gives a stream) and prints, per screen,
   how even the frames were (p50 / p95 / p99 / worst / late frames). Timed in milliseconds, so the same run means
   the same thing at any frame rate. Development only. */
#include "core/stress.h"
#include "core/app.h"
#include "core/keys.h"
#include "core/pace.h"
#include "screens/player.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { A_KEY, A_HOLDOK, A_PLAY, A_WAIT, A_HOME };
typedef struct { int a, k, count, every, wait; } Step;   /* count presses every ms (a held key repeats), then wait ms */
#define K(k, n, every, wait) { A_KEY, k, n, every, wait }
/* back to Home's first row whatever is open (Back only while something is open, never on Home itself: two there exit) */
#define HOME { A_HOME, 0, 0, 0, 0 }, K(K_UP, 8, 120, 300), K(K_DOWN, 1, 0, 600)
static const Step LOOP[] = {
  { A_WAIT, 0, 0, 0, 2500 }, HOME,
  /* Home: held Right and Left along a row, rows up and down, fast */
  K(K_RIGHT, 30, 125, 400), K(K_LEFT, 30, 125, 400), K(K_DOWN, 4, 300, 300), K(K_RIGHT, 8, 90, 300), K(K_DOWN, 2, 150, 300), HOME,
  /* the title page from a card, its buttons, episodes / streams, and back */
  K(K_DOWN, 1, 0, 400), K(K_RIGHT, 2, 200, 300), K(K_OK, 1, 0, 1800), /* a row below the first: OK on Continue Watching plays */ K(K_RIGHT, 3, 250, 300), K(K_DOWN, 3, 400, 1200), K(K_RIGHT, 4, 200, 400), HOME,
  /* the card menu (hold OK), down its entries, away */
  { A_HOLDOK, 0, 0, 0, 900 }, K(K_DOWN, 3, 200, 300), HOME,
  /* tabs: Discover (spotlight, rows), Library, back to Home, quickly */
  K(K_UP, 1, 0, 400), K(K_LEFT, 1, 0, 2500), K(K_DOWN, 2, 300, 300), K(K_RIGHT, 16, 110, 300), K(K_UP, 3, 300, 4500), HOME,
  K(K_UP, 1, 0, 300), K(K_RIGHT, 1, 0, 2500), K(K_DOWN, 1, 0, 300), K(K_RIGHT, 6, 150, 300), K(K_DOWN, 3, 250, 600), HOME,
  K(K_UP, 1, 0, 300), K(K_LEFT, 1, 0, 1500), K(K_RIGHT, 1, 0, 300), K(K_LEFT, 1, 0, 300), K(K_RIGHT, 2, 300, 1500), HOME,
  /* Search: a few letters, results, away */
  K(K_GREEN, 1, 0, 1500), K(K_RIGHT, 2, 200, 200), K(K_OK, 1, 0, 300), K(K_DOWN, 1, 0, 200), K(K_OK, 1, 0, 300), K(K_RIGHT, 1, 0, 200), K(K_OK, 1, 0, 2500), K(K_RIGHT, 8, 150, 600), HOME,
  /* Settings: categories, a picker, switches, away */
  K(K_RED, 1, 0, 1500), K(K_DOWN, 6, 250, 300), K(K_UP, 6, 120, 300), K(K_RIGHT, 1, 0, 400), K(K_DOWN, 3, 250, 300), K(K_OK, 1, 0, 600), K(K_DOWN, 2, 250, 300), HOME,
  /* the player (with SB_STRESS_PLAY): open, the controls, trays, seeking, away */
  { A_PLAY, 0, 0, 0, 4000 }, K(K_DOWN, 1, 0, 600), K(K_RIGHT, 6, 300, 500), K(K_OK, 1, 0, 800), K(K_DOWN, 2, 300, 300), K(K_BACK, 1, 0, 500), K(K_UP, 1, 0, 300), K(K_RIGHT, 8, 120, 1600), K(K_LEFT, 4, 120, 1600), HOME,
};
static const char *NAMES[] = { "sign-in", "home", "discover", "library", "anime", "title", "similar", "search", "settings", "player", "menu" };
#define NSTEP ((int)(sizeof LOOP / sizeof *LOOP))
static int loops, loop, step, done, on, sent; static Uint32 nextAt; static int keyDown;

int stress_on(void) { return on; }
const char *stress_screen(void) { int tag = player_open() ? (int)SCR_N : app_overlay ? (int)SCR_N + 1 : (int)SCR; return tag < (int)(sizeof NAMES / sizeof *NAMES) ? NAMES[tag] : "?"; }
void stress_init(void) { const char *e = getenv("SB_STRESS"); if (!e) return; loops = atoi(e); if (loops < 1) loops = 1; on = 1; nextAt = SDL_GetTicks() + 3000; fprintf(stderr, "[stress] %d loop(s) of every page\n", loops); }
void stress_tick(void) {
  if (!on) return;
  { static int last = -1; int tag = player_open() ? (int)SCR_N : app_overlay ? (int)SCR_N + 1 : (int)SCR; pace_tag(tag); /* the screen the next frame belongs to */
    if (tag != last) { fprintf(stderr, "[stress] -> %s\n", tag < (int)(sizeof NAMES / sizeof *NAMES) ? NAMES[tag] : "?"); last = tag; } }
  if (done || !SDL_TICKS_PASSED(SDL_GetTicks(), nextAt)) return;
  const Step *s = &LOOP[step];
  if (s->a == A_WAIT) { nextAt = SDL_GetTicks() + s->wait; sent = 0; }
  else if (s->a == A_HOLDOK) { if (!keyDown) { app_key(K_OK, 1, 0); keyDown = 1; nextAt = SDL_GetTicks() + 750; return; } app_key(K_OK, 0, 0); keyDown = 0; nextAt = SDL_GetTicks() + s->wait; }
  else if (s->a == A_HOME) { /* one Back every 450 ms while anything is open; then on */
    if (player_open() || app_overlay || SCR != SCR_HOME) { if (sent++ < 8) { app_key(K_BACK, 1, 0); app_key(K_BACK, 0, 0); nextAt = SDL_GetTicks() + 450; return; } }
    nextAt = SDL_GetTicks() + 300; }
  else if (s->a == A_PLAY) {
    const char *u = getenv("SB_STRESS_PLAY"); if (!u) goto next_loop; /* no stream: the player steps (the last block) are skipped */
    Stream st; memset(&st, 0, sizeof st); snprintf(st.url, sizeof st.url, "%s", u); snprintf(st.name, sizeof st.name, "Stress\n1080p"); snprintf(st.addon, sizeof st.addon, "Stress");
    app_play(&st, "movie", "tt27369017", "", 0); nextAt = SDL_GetTicks() + s->wait;
  } else { /* a press, or a held key: the first press, then repeats every `every` ms, then the release */
    if (sent < s->count) { app_key(s->k, 1, sent > 0); if (s->count == 1 || sent == s->count - 1) app_key(s->k, 0, 0); sent++; if (sent < s->count) { nextAt = SDL_GetTicks() + s->every; return; } }
    nextAt = SDL_GetTicks() + s->wait;
  }
  sent = 0; step++; if (getenv("SB_STRESS_LOG")) fprintf(stderr, "[stress] step %d\n", step);
  if (step < NSTEP) return;
next_loop:
  step = 0; loop++;
  fprintf(stderr, "[stress] loop %d of %d done\n", loop, loops);
  if (loop >= loops) { done = 1; pace_tag_report(NAMES, (int)(sizeof NAMES / sizeof *NAMES)); app_quit(); }
}
