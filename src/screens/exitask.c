/* Leaving the app: Back at the top (Home, the tab bar up) asks first, on a card in the look of the others (the sheets'
   glass): "Leave the app?" with Stay (focused) and Leave. Left / Right between them, OK chooses; Back, or Back held
   down by pressing it again and again, stays: nobody leaves by bumping Back. A click on a button chooses it; a click
   elsewhere stays. It rises in and settles (0.3 s), as the log card. */
#include "screens/exitask.h"
#include "core/app.h"
#include "core/i18n.h"
#include "core/keys.h"
#include "core/hit.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include "ui/glass.h"
#include "ui/widgets.h"
#include "screens/sheet.h"
#include <math.h>
#include <stdio.h>

static struct { int open, sel; double at, outAt; FPill pill; } E; static SGlass glass;
#define EW 760.0f
#define EH 300.0f
#define BW 250.0f
#define BH 72.0f
static void close_card(int leave) { if (!E.open) return; E.open = 0; E.outAt = app_now(); fpill_hide(&E.pill); if (leave) { fprintf(stderr, "[exit] chosen on the card\n"); app_quit(); } }
static int key(int k) {
  if (!E.open) { app_overlay = NULL; app_key(app_mirror_key(k), 1, 0); return 1; } /* on its way out: the key is the page's */
  int rtl = app_rtl();
  if (k == K_LEFT || k == K_RIGHT) { int to = (k == K_RIGHT) != rtl ? 1 : 0; E.sel = to; return 1; } /* 0 Stay (start side), 1 Leave */
  if (k == K_OK) { fpill_press(&E.pill); close_card(E.sel == 1); return 1; }
  if (k == K_BACK) { close_card(0); return 1; }
  return 1; /* the rest waits for an answer */
}
static void btn_x(int i, float *x) { float x0 = SB_W / 2 - EW / 2, gap = 24, total = 2 * BW + gap, left = x0 + (EW - total) / 2; int pos = app_rtl() ? 1 - i : i; *x = left + pos * (BW + gap); }
static void tick(float dt) {
  sglass_tick(&glass, dt); fpill_tick(&E.pill, dt);
  if (!E.open) { if (app_now() - E.outAt >= 0.32 && app_overlay == &exitAsk) app_overlay = NULL; return; }
  float bx; btn_x(E.sel, &bx); fpill_to(&E.pill, bx, SB_H / 2 - EH / 2 + EH - 48 - BH, BW, BH, 0.3f);
}
static void on_btn(int a, int b) { (void)b; if (!E.open) return; E.sel = a; close_card(a == 1); }
static void on_focus(int a, int b) { (void)b; if (E.open) E.sel = a; }
static void on_out(int a, int b) { (void)a; (void)b; close_card(0); }
static float ease_q(float x) { return x <= 0 ? 0 : x >= 1 ? 1 : 1 - powf(1 - x, 4); }
static void draw(void) {
  float op, dy, sc;
  if (E.open) { float k = ease_q((float)(app_now() - E.at) / 0.3f); op = k; dy = 18 * (1 - k); sc = 0.97f + 0.03f * k; }
  else { float t = (float)(app_now() - E.outAt), k = ease_q(t / 0.22f), o = t / 0.18f; o = o > 1 ? 1 : o; op = 1 - (o < 0.5f ? 2 * o * o : 1 - powf(-2 * o + 2, 2) / 2); dy = 14 * k; sc = 1 - 0.03f * k; }
  if (op <= 0.001f) return;
  float x = SB_W / 2 - EW / 2, y = SB_H / 2 - EH / 2, cx = SB_W / 2, by = y + EH - 48 - BH;
  if (E.open) { hit_act(0, 0, SB_W, SB_H, on_out, 0, 0); for (int i = 0; i < 2; i++) { float bx; btn_x(i, &bx); hit_add(bx, by, BW, BH, on_focus, on_btn, i, 0); } }
  gfx_opacity_push(op); gfx_offset_push(0, dy); gfx_scale_push(sc, sc, cx, y + EH / 2);
  gfx_rect(0, 0, SB_W, SB_H, BLACK_A(90)); /* (the page dims behind it) */
  if (glass.ok && glass.a.v >= 0.999f) gfx_shadow_under(x, y + 30, EW, EH, 32, 80, BLACK_A(150), x, y, EW, EH, 32); else gfx_shadow(x, y + 30, EW, EH, 32, 80, BLACK_A(150));
  sglass_draw(&glass, x, y, EW, EH, 32); gfx_ring(x, y, EW, EH, 32, 1, WHITE_A(22));
  text_draw(T("Leave the app?"), cx, y + 64, 42, 700, WHITE_A(255), -0.5f, EW - 96, ALIGN_CENTER);
  fpill_draw(&E.pill, -1, 0.96f);
  for (int i = 0; i < 2; i++) { float bx; btn_x(i, &bx); int on = E.sel == i;
    if (!on) gfx_rrect(bx, by, BW, BH, BH / 2, WHITE_A(20));
    text_draw(i ? T("Leave") : T("Stay"), bx + BW / 2, by + BH / 2 - text_line_height(30) / 2, 30, on ? 700 : 500, WHITE_A(on ? 255 : 210), 0, BW - 24, ALIGN_CENTER); }
  gfx_scale_pop(); gfx_offset_pop(); gfx_opacity_pop();
}
Screen exitAsk = { NULL, NULL, key, tick, draw, NULL, NULL, NULL };
void exitask_show(void) {
  if (app_overlay == &exitAsk && E.open) return;
  E.sel = 0; E.at = app_now(); E.open = 1; fpill_snap(&E.pill);
  sglass_bake(&glass, SB_W / 2 - EW / 2, SB_H / 2 - EH / 2, EW, EH);
  app_overlay = &exitAsk; fprintf(stderr, "[exit] asked\n");
}
