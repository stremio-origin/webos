#include "ui/support.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include "ui/icon.h"
#include "ui/qr.h"
#include "ui/glass.h"
#include "core/app.h"
#include "core/i18n.h"
#include <math.h>

/* everything in proportion to the code: the pill and its words read at any size; the pill is as wide as its two
   halves need (never less than the code), the code centred under it.
   K: the pill and its words larger than that (the support card reads them from a sofa; About and the guide use 1) */
static float K = 1, IK = 1.1f; static int addrLight; /* IK: the icons beside the pill's words, to their size (the card's larger) */ /* addrLight: the address stays light with the focus (the card has no accent row under it) */
static float pill_h(float Q) { return fmaxf(56, Q * 0.15f) * K; }
static int lab_px(float Q) { return (int)(fmaxf(20, Q * 0.07f) * K); }
static float half_w(float Q, const char *l) { int px = lab_px(Q); return px * IK + 12 + text_width(l, px, 800, 0) + 2 * px * 1.3f; }
static const char *lab(int k) { return k ? "GitHub" : T("Support"); }
#define K_ON(k) float k0 = K; K = k; IK = k > 1 ? 1.5f : 1.1f
#define K_OFF() K = k0; IK = 1.1f
float support_w_k(float Q, float k) { K_ON(k); float w = support_w(Q); K_OFF(); return w; }
float support_h_k(float Q, float k) { K_ON(k); float h = support_h(Q); K_OFF(); return h; }
float support_draw_k(float x, float y, float Q, float k, float sel, int f, const char *coffee, const char *coffeeShown, const char *gh, const char *ghShown) {
  K_ON(k); addrLight = 1; float h = support_draw(x, y, Q, sel, f, coffee, coffeeShown, gh, ghShown); K_OFF(); addrLight = 0; return h; }
float support_w(float Q) { float h = fmaxf(half_w(Q, lab(0)), half_w(Q, lab(1))); return fmaxf(Q, 2 * h); }
static GLuint glassTex; static int glassUp; void support_glass(unsigned int tex) { glassTex = tex; glassUp = 0; } void support_glass_picture(unsigned int tex) { glassTex = tex; glassUp = 1; }
float support_h(float Q) { return pill_h(Q) + Q * 0.06f + Q + 14 + text_line_height(lab_px(Q) - 4); }
float support_draw(float x, float y, float Q, float sel, int f, const char *coffee, const char *coffeeShown, const char *gh, const char *ghShown) {
  if (sel < 0) sel = 0; if (sel > 1) sel = 1;
  const char *I[2] = { "g_heart", "g_star" }; float PW = support_w(Q), hw = PW / 2, PH = pill_h(Q); int px = lab_px(Q); float is = px * IK;
  /* the pill: two halves, the light under the one showing */
  if (!glassTex) lglass_shell(x, y, PW, PH, PH / 2, WHITE_A(f ? 40 : 22), f ? 1.15f : 0.9f); /* over a row (Settings): clear glass, the row's colour through it */
  else { float v0 = y / SB_H, v1 = (y + PH) / SB_H; if (!glassUp) { v0 = 1 - v0; v1 = 1 - v1; } lglass_soft(0.45f); lglass_liquid(glassTex, x, y, PW, PH, PH / 2, x / SB_W, v0, (x + PW) / SB_W, v1, f ? 1.15f : 0.85f, 0, 7, 0); } lglass_light(0); glassTex = 0; /* the bar's glass */
  float lx = x + 6 + sel * (hw - 6), lw = hw - 6, lh = PH - 12; /* the half showing: the accent's glass, the bar's focus */
  gfx_rrect_v(lx, y + 6, lw, lh, lh / 2, col_alpha(ACC2 | 0xff000000u, f ? 0.95f : 0.62f), col_alpha(ACC | 0xff000000u, f ? 0.9f : 0.52f)); gfx_rrect_v(lx, y + 6, lw, lh, lh / 2, WHITE_A(40), WHITE_A(0)); gfx_ring(lx, y + 6, lw, lh, lh / 2, 1.2f, WHITE_A(70));
  for (int k = 0; k < 2; k++) { float on = 1 - fabsf(k - sel), cx = x + hw * k + hw / 2; on = on < 0 ? 0 : on; uint32_t c = col_mix(WHITE_A(200), WHITE_A(255), on);
    float tw = text_width(lab(k), px, 800, 0), w = is + 12 + tw, sx = cx - w / 2;
    icon_draw(I[k], sx, y + PH / 2 - is / 2, is, k && on < 0.5f ? RGB(0xff, 0xd4, 0x5a) : c); text_draw(lab(k), sx + is + 12, y + PH / 2 - text_line_height(px) / 2.0f, px, 800, c, 0, 0, ALIGN_LEFT); }
  /* the code, under the pill's middle, turning over between the two */
  float qx = x + PW / 2 - Q / 2, qy = y + PH + Q * 0.06f, turn = fabsf(cosf(3.14159265f * sel)), sx = turn < 0.02f ? 0.02f : turn; int which = sel < 0.5f ? 0 : 1;
  const char *u = which ? gh : coffee, *a = which ? ghShown : coffeeShown;
  gfx_scale_push(sx, 1, qx + Q / 2, qy + Q / 2);
  gfx_shadow(qx, qy + 8, Q, Q, Q * 0.09f, 24, BLACK_A(110)); if (!(u && u[0] && qr_draw(u, qx, qy, Q, Q * 0.09f, Q * 0.065f))) gfx_rrect(qx, qy, Q, Q, Q * 0.09f, WHITE_A(24));
  gfx_scale_pop();
  if (a && a[0]) { gfx_opacity_push(turn); text_draw(a, x + PW / 2, qy + Q + 14, px - 4, 600, f && !addrLight ? col_alpha(ACC_FG, 0.9f) : WHITE_A(f ? 235 : 200), 0, PW + 120, ALIGN_CENTER); gfx_opacity_pop(); }
  return support_h(Q);
}
