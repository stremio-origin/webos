/* the parent PIN. A card with four places and a number pad worked with the arrows and OK (or the pointer):
   it checks the PIN, or makes a new one (typed twice). The PIN guards locked profiles, a child's settings and more time
   for a child; it is kept on this TV only. */
#include "screens/pinpad.h"
#include "core/app.h"
#include "core/i18n.h"
#include "core/keys.h"
#include "core/hit.h"
#include "core/store.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include "ui/glass.h"
#include "ui/widgets.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static struct { int open, mode, sel, n, stage, wrong; char typed[8], first[8], title[96]; double at, shakeAt; PinDone done; void *ud; Screen *prev; } P;
#define CW 640.0f
#define CH 760.0f
#define KW 150.0f
#define KH 86.0f
#define GAP 18.0f

int pin_is_set(void) { const char *p = store_get("parentPin", ""); return strlen(p) == 4; }
static int pin_ok(const char *t) { return !strcmp(store_get("parentPin", ""), t); }

static void finish(int ok) {
  if (!P.open) return; P.open = 0; app_overlay = P.prev; PinDone d = P.done; void *ud = P.ud; P.done = NULL;
  if (d) d(ok, ud);
}
static void typed_full(void) {
  if (P.mode == PIN_CHECK) { if (pin_ok(P.typed)) { finish(1); return; } P.wrong = 1; P.shakeAt = app_now(); P.n = 0; P.typed[0] = 0; return; }
  if (!P.stage) { snprintf(P.first, sizeof P.first, "%s", P.typed); P.stage = 1; P.n = 0; P.typed[0] = 0; P.wrong = 0; return; }
  if (!strcmp(P.first, P.typed)) { store_set("parentPin", P.typed); store_flush(); toast(T("Parent PIN saved"), 2200, NULL); finish(1); return; }
  P.stage = 0; P.wrong = 2; P.shakeAt = app_now(); P.n = 0; P.typed[0] = 0;
}
/* the pad: 1 to 9, then Delete, 0, Cancel */
static void press(int i) {
  if (i == 9) { if (P.n) P.typed[--P.n] = 0; return; }
  if (i == 11) { finish(0); return; }
  if (P.n >= 4) return;
  P.typed[P.n++] = (char)('0' + (i == 10 ? 0 : i + 1)); P.typed[P.n] = 0; P.wrong = 0;
  if (P.n == 4) typed_full();
}
static int key(int k) {
  if (!P.open) { app_overlay = P.prev; return 0; }
  int r = P.sel / 3, c = P.sel % 3;
  if (k == K_LEFT && c > 0) P.sel--; else if (k == K_RIGHT && c < 2) P.sel++;
  else if (k == K_UP && r > 0) P.sel -= 3; else if (k == K_DOWN && r < 3) P.sel += 3;
  else if (k == K_OK) press(P.sel);
  else if (k == K_BACK) { if (P.n) P.typed[--P.n] = 0; else finish(0); }
  return 1;
}
static void tick(float dt) { (void)dt; }
static void key_xy(int i, float *x, float *y) { float x0 = SB_W / 2 - (3 * KW + 2 * GAP) / 2, y0 = SB_H / 2 - CH / 2 + 300; *x = x0 + (i % 3) * (KW + GAP); *y = y0 + (i / 3) * (KH + GAP); }
static void on_focus(int a, int b) { (void)b; if (P.open) P.sel = a; }
static void on_press(int a, int b) { (void)b; if (P.open) { P.sel = a; press(a); } }
static void draw(void) {
  if (!P.open) return;
  float k = (float)((app_now() - P.at) / 0.25); k = k > 1 ? 1 : k; float x = SB_W / 2 - CW / 2, y = SB_H / 2 - CH / 2;
  gfx_opacity_push(k); gfx_rect(0, 0, SB_W, SB_H, BLACK_A(150));
  glass_rrect(x, y, CW, CH, 32, RGB(21, 18, 31), .14f, .02f, .19f);
  const char *sub = P.mode == PIN_CHECK ? (P.wrong ? T("Wrong PIN. Try again") : T("Enter the parent PIN"))
                  : P.wrong == 2 ? T("The two PINs were not the same. Again") : P.stage ? T("Type it again") : T("Choose a 4-digit parent PIN");
  text_draw(P.title, SB_W / 2, y + 44, 38, 700, WHITE_A(255), 0, CW - 60, ALIGN_CENTER);
  text_draw(sub, SB_W / 2, y + 104, 25, 500, P.wrong ? RGB(0xff, 0x8a, 0x8a) : WHITE_A(190), 0, CW - 60, ALIGN_CENTER);
  float sh = (float)(app_now() - P.shakeAt); float dx = sh < 0.4f ? 14 * sinf(sh * 40) * (1 - sh / 0.4f) : 0;
  for (int i = 0; i < 4; i++) { float cx = SB_W / 2 - 105 + i * 70 + dx, cy = y + 200;
    if (i < P.n) gfx_circle(cx, cy, 16, WHITE_A(255)); else gfx_ring(cx - 16, cy - 16, 32, 32, 16, 3, WHITE_A(120)); }
  for (int i = 0; i < 12; i++) { float kx, ky; key_xy(i, &kx, &ky); int on = P.sel == i;
    hit_add(kx, ky, KW, KH, on_focus, on_press, i, 0);
    gfx_rrect(kx, ky, KW, KH, 22, on ? WHITE_A(255) : WHITE_A(26));
    char lab[24]; if (i < 9) snprintf(lab, sizeof lab, "%d", i + 1); else if (i == 10) snprintf(lab, sizeof lab, "0"); else snprintf(lab, sizeof lab, "%s", i == 9 ? T("Delete") : T("Cancel"));
    int px = i == 9 || i == 11 ? 24 : 38; text_draw(lab, kx + KW / 2, ky + (KH - text_line_height(px)) / 2, px, 700, on ? RGB(17, 17, 17) : WHITE_A(235), 0, KW - 12, ALIGN_CENTER); }
  gfx_opacity_pop();
}
Screen pinPad = { NULL, NULL, key, tick, draw, NULL, NULL, NULL };

void pin_ask(int mode, const char *title, PinDone done, void *ud) {
  if (mode == PIN_CHECK && !pin_is_set()) { if (done) done(1, ud); return; } /* (no PIN made: nothing to guard with) */
  memset(&P, 0, sizeof P); P.open = 1; P.mode = mode; P.at = app_now(); P.done = done; P.ud = ud; P.sel = 0;
  snprintf(P.title, sizeof P.title, "%s", title && *title ? title : T("Parent PIN"));
  P.prev = app_overlay == &pinPad ? NULL : app_overlay; app_overlay = &pinPad;
}
int pin_open(void) { return P.open; }
