#include "ui/prompts.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include "ui/img.h"
#include "ui/icon.h"
#include "ui/glass.h"
#include "ui/widgets.h"
#include "core/app.h"
#include "core/i18n.h"
#include "core/keys.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* measured words, kept while they stay the same (drawn every frame) */
typedef struct { char s[640]; int px, wt, n, lines; float tr, w, mw; } Memo;
static int memo_same(Memo *m, const char *s, int px, int wt, float tr) { if (m->px == px && m->wt == wt && m->tr == tr && !strcmp(m->s, s)) return 1; snprintf(m->s, sizeof m->s, "%s", s); m->px = px; m->wt = wt; m->tr = tr; m->n = -2; m->mw = -1; return 0; }
static float memo_width(Memo *m, const char *s, int px, int wt, float tr) { if (!memo_same(m, s, px, wt, tr) || m->n != -1) { m->n = -1; m->w = text_width(s, px, wt, tr); } return m->w; }
static int memo_wrap(Memo *m, const char *s, float maxW, int lines, int px, int wt, float tr) { if (!memo_same(m, s, px, wt, tr) || m->mw != maxW || m->lines != lines || m->n < 0) { m->mw = maxW; m->lines = lines; m->n = text_wrap_count(s, maxW, lines, px, wt, tr); } return m->n; }
static const char *upper_of(const char *s) { static char src[200], up[200]; static unsigned lg; if (lg != i18n_gen() || strcmp(src, s)) { lg = i18n_gen(); snprintf(src, sizeof src, "%s", s); snprintf(up, sizeof up, "%s", s); text_upper(up, sizeof up); } return up; }
static int PX(float px, float s) { return (int)(px * s + 0.5f); }
/* words too small to read (a preview drawn tiny: under 7 px) are drawn as a soft bar their length, as a mock-up shows
   them; readable sizes as words */
#define TINY 7
static float bar_w(const char *t, int px, int wt, float tr) { return text_width(t, 20, wt, tr) * px / 20.0f; }
static float wtext(const char *t, float x, float y, int px, int wt, uint32_t c, float tr, float maxW, int al) {
  if (px >= TINY) return text_draw(t, x, y, px, wt, c, tr, maxW, al);
  float w = bar_w(t, px, wt, tr); if (maxW > 0 && w > maxW) w = maxW; float lh = text_line_height(px > 0 ? px : 1), bh = fmaxf(1.5f, px * 0.62f);
  gfx_rrect(al == ALIGN_RIGHT ? x - w : x, y + (lh - bh) / 2, w, bh, bh / 2, col_alpha(c, 0.75f)); return w; }
static void wwrapped(const char *t, float x, float y, float maxW, int lines, int px, int wt, uint32_t c, float lh) {
  if (px >= TINY) { text_draw_wrapped(t, x, y, maxW, lines, px, wt, c, 0, ALIGN_LEFT, lh); return; }
  float w = bar_w(t, px, wt, 0), bh = fmaxf(1.5f, px * 0.62f); for (int i = 0; i < lines && w > 0; i++) { float lw = fminf(w, maxW) * (i == lines - 1 && w > maxW ? 0.6f : 1); gfx_rrect(x, y + i * lh + (lh - bh) / 2 - 2, lw, bh, bh / 2, col_alpha(c, 0.6f)); w -= maxW; } }

void ui_next_card(float right, float bottom, float s, const NextCardSpec *c, float *ox, float *oy, float *ow, float *oh) {
  float k = c->compact < 0 ? 0 : c->compact > 1 ? 1 : c->compact, big = 1 - k;
  float cw = (540 + (420 - 540) * k) * s, cx = right - cw, artH = (286 + (218 - 286) * k) * s, lh2 = text_line_height(PX(21, s));
  static Memo ovW; float ovn = c->ov && c->ov[0] && big > 0 ? memo_wrap(&ovW, c->ov, cw - 44 * s, 2, PX(24, s), 400, 0) : 0;
  float ovH = ovn ? (6 + ovn * 33) * s * big : 0; /* (the summary: the large card's alone; its room closes as the card becomes the compact one) */
  float ch = 16 * s + artH + 18 * s + lh2 + 6 * s + text_line_height(PX(30, s)) + ovH + 18 * s + 36 * s + 18 * s, cy = bottom - ch;
  if (ox) *ox = cx; if (oy) *oy = cy; if (ow) *ow = cw; if (oh) *oh = ch;
  lglass_shell(cx, cy, cw, ch, 28 * s, RGBA(14, 12, 22, 220), 1);
  float ax = cx + 16 * s, ay = cy + 16 * s; GLuint t = c->art && c->art[0] ? img_get_shaped(c->art, (int)(cw - 32 * s), (int)artH, 18 * s, 1) : 0; ph_picture(t, ax, ay, cw - 32 * s, artH, 18 * s, WHITE_A(255));
  if (c->autoK >= 0) { gfx_clip_push(ax, ay + artH - 6 * s, (cw - 32 * s) * c->autoK, 6 * s); gfx_rrect(ax, ay + artH - 24 * s, cw - 32 * s, 24 * s, 18 * s, ACC2); gfx_clip_pop(); } /* the countdown: a bar under the picture */
  float y = ay + artH + 18 * s; float w = wtext(upper_of(c->label), ax + 6 * s, y, PX(21, s), 800, ACC2, 2.0f * s, 0, ALIGN_LEFT);
  if (c->autoK >= 0) { char n[8]; snprintf(n, sizeof n, "%d", c->autoLeft < 0 ? 0 : c->autoLeft); char st[64]; snprintf(st, sizeof st, " · %s", T1("Starts in {n} s", "n", n)); wtext(st, ax + 6 * s + w + (PX(21, s) < TINY ? 4 * s : 0), y, PX(21, s), 600, WHITE_A(204), 0.4f * s, 0, ALIGN_LEFT); }
  y += lh2 + 6 * s;
  { float sw = wtext(c->se, ax + 6 * s, y, PX(30, s), 800, WHITE_A(255), -0.3f * s, 0, ALIGN_LEFT);
    if (c->title && c->title[0]) { static Memo mdW; float dw = memo_width(&mdW, "\xc2\xb7", PX(30, s), 800, 0); if (PX(30, s) >= TINY) text_draw("\xc2\xb7", ax + 6 * s + sw + 12 * s, y, PX(30, s), 800, WHITE_A(102), 0, 0, ALIGN_LEFT);
      wtext(c->title, ax + 6 * s + sw + 12 * s + dw + 12 * s, y, PX(30, s), 800, WHITE_A(255), -0.3f * s, cw - 44 * s - sw - 24 * s - dw, ALIGN_LEFT); }
    y += text_line_height(PX(30, s)); }
  if (ovH > 0) { gfx_clip_push(ax, y, cw - 32 * s, ovH); gfx_opacity_push(big * big);
    wwrapped(c->ov, ax + 6 * s, y + 6 * s + (33 * s - text_line_height(PX(24, s))) / 2, cw - 44 * s, 2, PX(24, s), 400, WHITE_A(199), 33 * s);
    gfx_opacity_pop(); gfx_clip_pop(); y += ovH; }
  y += 18 * s; { float hx = ax + 6 * s, cy2 = y + 18 * s; hx += key_hint_s(hx, cy2, K_OK, c->key, WHITE_A(230), 0, s) + 28 * s;
    if (k < 0.5f || hx + 48 * s + text_width(T("Watch credits"), PX(22, s), 600, 0) <= cx + cw - 16 * s) key_hint_s(hx, cy2, K_BACK, T("Watch credits"), WHITE_A(230), 0, s); } /* (the compact card: Back's words only where they fit; the key works as always) */
}

float ui_pill_height(float s) { return 74 * s; }
float ui_pill_width(float s, const char *label, const char *icon) { static Memo pw[2]; static int t; Memo *m = &pw[t ^= 1]; if (!strcmp(pw[0].s, label) && pw[0].px == PX(26, s)) m = &pw[0]; else if (!strcmp(pw[1].s, label) && pw[1].px == PX(26, s)) m = &pw[1];
  return (2 + 64) * s + memo_width(m, label, PX(26, s), 700, 0) + (icon ? 42 * s : 0); }
void ui_pill(float x, float y, float w, float s, const char *label, const char *icon, int lit, float fillK, float nextK) {
  float h = 74 * s, r = 37 * s;
  if (lit) lglass_accent(x, y, w, h, r, 1); else lglass_shell(x, y, w, h, r, RGBA(18, 16, 28, 200), 1); /* glass: lit in the accent when focused */
  if (fillK > 0) { float fw = h + (w - h) * fillK; gfx_opacity_push(fminf(1, fillK * 6)); gfx_clip_push(x, y, fw, h); lglass_accent(x, y, w, h, r, 1); gfx_clip_pop(); gfx_opacity_pop(); } /* counting down: a pill growing out of its left end, eased */
  if (nextK > 0) { gfx_opacity_push(fminf(1, nextK * 6)); gfx_rrect(x, y, h + (w - h) * nextK, h, r, WHITE_A(71)); gfx_opacity_pop(); }
  float tx = x + 33 * s; uint32_t col = lit ? ACC_FG : WHITE_A(255);
  if (icon) { icon_draw(icon, tx, y + 22 * s, 30 * s, col); tx += 42 * s; }
  text_draw(label, tx, y + (h - text_line_height(PX(26, s))) / 2, PX(26, s), 700, col, 0, 0, ALIGN_LEFT);
}
