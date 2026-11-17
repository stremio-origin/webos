#include "ui/glass.h"
#include "core/app.h"
#include <math.h>
#include <string.h>

#define SHEET RGB(0x16, 0x14, 0x1d)
/* a texture drawn over the whole of a target (layout units cover its width; its height follows its aspect) */
static void into(GfxTarget *dst, uint32_t clear, GLuint tex, float u0, float v0, float u1, float v1, uint32_t tint) {
  gfx_target_begin(dst, clear);
  gfx_image(tex, 0, 0, SB_W, dst->h * SB_W / dst->w, 0, tint, u0, v0, u1, v1);
  gfx_target_end();
}
/* noise, 128 x 128, alpha only: made once */
static GLuint grain_tex(void) {
  static GLuint t; if (t) return t; unsigned char px[128 * 128]; unsigned s = 0x9e3779b9u;
  for (int i = 0; i < 128 * 128; i++) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; int a = (int)(s & 255), b = (int)((s >> 8) & 255); px[i] = (unsigned char)((a + b) / 2); } /* (two summed: softer than flat noise) */
  t = gfx_tex_create(px, 128, 128, 1, 0); return t;
}
void sglass_free(SGlass *g) { if (g->t.fbo) gfx_target_free(&g->t); memset(g, 0, sizeof *g); }
#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
void sglass_bake(SGlass *g, float x, float y, float w, float h) {
  Uint32 tb0 = SDL_GetTicks();
  int W = (int)ceilf(w / 4), H = (int)ceilf(h / 4); /* the blur works from a quarter of the sheet in each direction */
  int FW = (int)ceilf(w / 2), FH = (int)ceilf(h / 2); if (FW > 1024) { FH = FH * 1024 / FW; FW = 1024; } /* the glass kept at half: smooth when stretched, the grain fine */
  if (g->t.fbo && (g->t.w != FW || g->t.h != FH)) gfx_target_free(&g->t);
  g->ok = 0; tween_set(&g->a, 0);
  if (!g->t.fbo && !gfx_target_create(&g->t, FW, FH)) return;
  float alpha = 1; GLuint src = bg_glass_tex(&alpha); int live = !getenv("SB_GLASS_BG"); /* SB_GLASS_BG=1: the backdrop alone, as before */
  if (live) { src = 1; alpha = 1; } /* the page itself, as it is under the sheet */
  /* frosted: the backdrop under the sheet taken down in two even steps (no stair-steps from skipping texels), then a
     gaussian three times over (wider each time: a soft, round blur, never blocky). The steps' targets are kept between
     bakes: making framebuffers cost a quarter of a second on the TV */
  static GfxTarget s[3]; int ns = 0;
  if (src && alpha > 0.05f) {
    int bw = (W + 1) / 2, bh = (H + 1) / 2; /* blurred at an eighth of the sheet: frosted, not just softened */
    int sw[3] = { W, bw, bw }, sh[3] = { H, bh, bh };
    for (; ns < 3; ns++) { if (s[ns].fbo && (s[ns].w != sw[ns] || s[ns].h != sh[ns])) gfx_target_free(&s[ns]);
      if (!s[ns].fbo && !gfx_target_create(&s[ns], sw[ns], sh[ns])) break; }
    if (ns == 3) {
      float u0 = x / SB_W, u1 = (x + w) / SB_W, v0 = 1 - y / SB_H, v1 = 1 - (y + h) / SB_H; /* the baked backdrop is upside down */
      if (live) { /* what is under the sheet, drawn at a quarter of its size: the region scaled to fill the target */
        gfx_target_begin(&s[0], SHEET); float sc = SB_W / w; gfx_scale_push(sc, sc, 0, 0); gfx_offset_push(-x, -y); app_draw_under(); gfx_offset_pop(); gfx_scale_pop(); gfx_target_end(); }
      else into(&s[0], SHEET, src, clampf(u0, 0, 1), clampf(v0, 0, 1), clampf(u1, 0, 1), clampf(v1, 0, 1), WHITE_A(255));
      into(&s[1], SHEET, s[0].tex, 0, 1, 1, 0, WHITE_A(255));
      static const float spread[4] = { 1.0f, 1.4f, 1.8f, 2.6f }; /* four widening passes: round and even, no ghost of the shapes */
      for (int i = 0; i < 4; i++) { gfx_blur_pass(&s[2], s[1].tex, bw, bh, 0, spread[i]); gfx_blur_pass(&s[1], s[2].tex, bw, bh, 1, spread[i]); }
    }
  }
  float LH = FH * SB_W / FW; /* the target's height in layout units */
  gfx_target_begin(&g->t, SHEET);
  if (ns == 3) gfx_image(s[1].tex, 0, 0, SB_W, LH, 0, WHITE_A((int)(255 * alpha)), 0, 1, 1, 0);
  /* a dark neutral tint, the accent faint at the edges, a light sheen at the top */
  gfx_rect(0, 0, SB_W, LH, RGBA(18, 16, 24, 112)); /* (a little lighter than before: more of the page's colour comes through the finer glass) */
  /* the page's radial accent (0 in the middle, .03 at 60%, .11 at its rim beyond the corners) as four edge bands */
  float ex = SB_W * 0.3f, ey = LH * 0.3f;
  gfx_rrect_h(0, 0, ex, LH, 0, acc_a(0.05f), acc_a(0)); gfx_rrect_h(SB_W - ex, 0, ex, LH, 0, acc_a(0), acc_a(0.05f));
  gfx_rrect_v(0, 0, SB_W, ey, 0, acc_a(0.045f), acc_a(0)); gfx_rrect_v(0, LH - ey, SB_W, ey, 0, acc_a(0), acc_a(0.045f));
  gfx_rrect_v(0, 0, SB_W, LH * 0.35f, 0, WHITE_A(15), WHITE_A(0));
  /* a fine grain over it all (as frosted glass has): it also breaks the bands a smooth dark gradient shows on a TV panel */
  { GLuint nt = grain_tex(); float ts = 128.0f * SB_W / FW; /* one texel of noise per texel of glass */
    for (float ty = 0; ty < LH; ty += ts) for (float tx = 0; tx < SB_W; tx += ts) { gfx_alpha_tex(nt, tx, ty, ts, ts, WHITE_A(5)); gfx_alpha_tex_uv(nt, tx, ty, ts, ts, BLACK_A(6), 1, 1, 0, 0); } }
  gfx_target_end();
  g->ok = 1; tween_to(&g->a, 1, 0.3f, EASE_IN_OUT);
  if (getenv("SB_SLOWLOG")) fprintf(stderr, "[glass] bake %dx%d in %u ms\n", W, H, SDL_GetTicks() - tb0);
}
void sglass_tick(SGlass *g, float dt) { tween_tick(&g->a, dt); }
void sglass_draw(const SGlass *g, float x, float y, float w, float h, float r) {
  if (!(g->ok && g->a.v >= 0.999f && gfx_opacity_now() >= 0.999f)) gfx_rrect(x, y, w, h, r, SHEET); /* (under the glass once it is fully in: hidden) */
  if (g->ok && g->a.v > 0.001f) gfx_image(g->t.tex, x, y, w, h, r, WHITE_A((int)(255 * g->a.v + 0.5f)), 0, 1, 1, 0);
  if (r > 0) { gfx_clip_push(x, y, w, r + 6); gfx_ring(x, y, w, h, r, 1.2f, WHITE_A(38)); gfx_clip_pop(); } /* light catching its top edge */
}

/* ---- liquid glass, drawn: the shell that every pill and button of the bar and the player share (the picture behind
   is read only where the app can read it: the top bar's backdrop; over the film the TV plays it on its own plane) ---- */
extern float lightAt;
void lglass_rim(float x, float y, float w, float h, float r, float a0, float a1, int topLeft) { /* the rim lit by a light at 135 degrees:
     the glare program's rim (stronger at the top) and its glint at the top-left, in one quad (no clipping: clipped
     pieces cost a draw call each); the far corner's softer light is the shell's own fine edge */
  (void)a0; if (!topLeft || a1 <= 0.004f) return; gfx_glare_at(x, y, w, h, r, -2.0f, WHITE_A((int)(fminf(a1, 1) * 255)), lightAt); }
float lightAt; /* where the light on the next pieces of glass is, along their top (0 the top-left corner) */
void lglass_light(float at) { lightAt = at; }
void lglass_shell(float x, float y, float w, float h, float r, uint32_t body, float k) {
  if (k <= 0.004f) return;
  gfx_shadow(x, y + h * 0.18f, w, h, r, fminf(h * 0.6f, 40), BLACK_A((int)(70 * fminf(k, 1))));
  if (body >> 24) gfx_rrect(x, y, w, h, r, col_alpha(body, fminf(k, 1)));
  gfx_rrect(x, y, w, h, r, acc_a(0.07f * fminf(k, 1.4f))); /* the accent's tint */
  gfx_rrect_v(x, y, w, h, r, WHITE_A((int)(fminf(30 * k, 70))), WHITE_A(0)); /* the sheen from the top */
  gfx_ring(x, y, w, h, r, fminf(h * 0.12f, 5), RGBA(176, 128, 255, (int)(fminf(16 * k, 28)))); /* the edge bending the light violet, as the liquid glass's does */
  gfx_ring(x, y, w, h, r, 1, WHITE_A((int)(fminf(36 * k, 90))));
  lglass_rim(x, y, w, h, r, 0.10f * k, 0.55f * fminf(k, 1.4f), 1); lglass_rim(x, y, w, h, r, 0.05f * k, 0.30f * fminf(k, 1.4f), 0);
}
void lglass_accent(float x, float y, float w, float h, float r, float a) { /* the lit one: accent glass, its sheen crossing it from corner to corner, a thin glassy edge in a lighter accent */
  if (a <= 0.004f) return;
  gfx_shadow(x, y + h * 0.18f, w, h, r, fminf(h * 0.6f, 40), BLACK_A((int)(70 * fminf(a, 1)))); /* (a plain shadow: no accent glow) */
  gfx_rrect_v(x, y, w, h, r, col_alpha(ACC2 | 0xff000000u, 0.94f * a), col_alpha(ACC | 0xff000000u, 0.94f * a));
  gfx_rrect_h(x, y, w, h, r, WHITE_A((int)(40 * a)), WHITE_A(0)); gfx_rrect_v(x, y, w, h, r, WHITE_A((int)(34 * a)), WHITE_A(0)); /* the sheen: brightest at the top left, gone by the bottom right */
  gfx_rrect_v(x, y + h * 0.5f, w, h * 0.5f, r, BLACK_A(0), BLACK_A((int)(26 * a))); /* (and the lower edge a touch deeper) */
  gfx_ring(x, y, w, h, r, 1.5f, col_alpha(col_mix(ACC2 | 0xff000000u, 0xffffffffu, 0.55f), 0.85f * a)); /* the thin glassy edge, in the accent's own light */
  gfx_ring(x + 1.5f, y + 1.5f, w - 3, h - 3, r > 1.5f ? r - 1.5f : 0, 1, WHITE_A((int)(30 * a)));
}
/* a selection in glass of any colour (body col): its sheen crossing it from the top-left corner to the bottom-right,
   the lower edge a touch deeper, a thin glassy outline in a lighter shade of the colour and a faint inner line */
void lglass_tint(float x, float y, float w, float h, float r, uint32_t col, float a) {
  if (a <= 0.004f) return; uint32_t c = col | 0xff000000u; float ca = ((col >> 24) & 255) / 255.0f * a;
  gfx_rrect(x, y, w, h, r, col_alpha(c, ca));
  gfx_rrect_h(x, y, w, h, r, WHITE_A((int)(40 * a)), WHITE_A(0)); gfx_rrect_v(x, y, w, h, r, WHITE_A((int)(34 * a)), WHITE_A(0));
  gfx_rrect_v(x, y + h * 0.5f, w, h * 0.5f, r, BLACK_A(0), BLACK_A((int)(26 * ca)));
  gfx_ring(x, y, w, h, r, 1.5f, col_alpha(col_mix(c, 0xffffffffu, 0.55f), 0.85f * a));
  gfx_ring(x + 1.5f, y + 1.5f, w - 3, h - 3, r > 1.5f ? r - 1.5f : 0, 1, WHITE_A((int)(30 * a)));
}
/* a moving pill's stretch from its speed (units a second): 0..1, smoothed in s */
float lglass_stretch(float *s, float *last, float x, float dt) { float v = dt > 0 ? fabsf(x - *last) / dt : 0; *last = x; *s += (fminf(v / 2600.0f, 1) - *s) * fminf(1, dt * 14); return *s; }
/* a switch in glass: the track glass, turning to accent glass as it comes on (white glass on a lit row), the knob a glass
   bead that stretches into a capsule as it travels and rounds again as it lands; k where the knob is (0 off, 1 on), c
   how far its colours have turned, lit a focused (accent) row */
void lglass_switch(float x, float y, float w, float h, float k, float c, int lit) {
  float r = h / 2;
  lglass_shell(x, y, w, h, r, lit ? WHITE_A(40) : RGBA(20, 20, 28, 120), 0.9f);
  if (c > 0.004f) { if (lit) { gfx_rrect(x, y, w, h, r, WHITE_A((int)(235 * c))); gfx_rrect_v(x, y, w, h, r, WHITE_A(0), BLACK_A((int)(24 * c))); } else lglass_accent(x, y, w, h, r, c); }
  float kd = h - 8, st = sinf(3.14159f * fminf(fmaxf(k, 0), 1)), kw = kd * (1 + 0.45f * st), kx = x + 4 + (w - 8 - kd) * k - (kw - kd) / 2, ky = y + 4; /* the bead, stretched mid-way */
  uint32_t kc = lit ? col_mix(WHITE_A(255), ACC | 0xff000000u, c) : WHITE_A(255);
  gfx_shadow(kx, ky + 2, kw, kd, kd / 2, 6, BLACK_A(90));
  gfx_rrect(kx, ky, kw, kd, kd / 2, kc);
  gfx_rrect_v(kx, ky, kw, kd, kd / 2, WHITE_A(lit && c > 0.5f ? 60 : 0), BLACK_A(28)); /* rounded: a little shade at its foot */
  gfx_rrect_v(kx + kw * 0.18f, ky + 2, kw * 0.64f, kd * 0.42f, kd * 0.21f, WHITE_A(150), WHITE_A(0)); /* its highlight */
  gfx_ring(kx, ky, kw, kd, kd / 2, 1, WHITE_A(lit && c > 0.5f ? 120 : 200));
}

static float lgSoft = 1; void lglass_soft(float k) { lgSoft = k; }
void lglass_liquid(GLuint t, float x, float y, float w, float h, float r, float u0, float v0, float u1, float v1, float k, float dark, int seed, int small) {
  if (k <= 0.004f) return;
  float n = (float)seed, tm = (float)app_now() * (1 + 0.13f * n) + 17.0f * n, p = 2.39f * n; /* (as the bar's pieces: each its own pace and phase) */
  float su = 0.5f + 0.4f * (0.55f * sinf(tm * 0.27f + p) + 0.3f * sinf(tm * 0.43f + 2.0f + 1.7f * p) + 0.15f * sinf(tm * 0.71f + 4.4f + 2.3f * p)), sv = 0.32f + 0.1f * sinf(tm * 0.33f + 1.0f + p);
  lglass_light(su - 0.12f);
  if (!t) { lglass_shell(x, y, w, h, r, RGBA(14, 14, 22, 120), k); return; }
  gfx_shadow(x, y + 12, w, h, r, 40, BLACK_A((int)(64 * fminf(k, 1))));
  gfx_liquid_spot(su, sv, (small ? 0.6f : 1.0f) * fminf(1, 80 / h) * lgSoft); gfx_liquid(t, x, y, w, h, r, fminf(h * 0.5f, 30) * lgSoft, fminf(k, 1), u0, v0, u1, v1); lgSoft = 1; /* (the spot's bend grows with the glass: a big sheet takes a pill's) */
  if (dark > 0.004f) gfx_rrect(x, y, w, h, r, col_alpha(PAGE | 0xff000000u, fminf(dark, 1)));
  gfx_rrect(x, y, w, h, r, RGBA(10, 10, 16, (int)(fminf(18 * k, 50))));
  gfx_rrect(x, y, w, h, r, acc_a(0.08f * k));
  gfx_rrect_v(x, y, w, h, r, WHITE_A((int)(fminf(28 * k, 70))), WHITE_A(0));
  gfx_ring(x, y, w, h, r, 1, WHITE_A((int)(fminf(34 * k, 90))));
  lglass_rim(x, y, w, h, r, 0.10f * k, (small ? 0.17f : 0.28f) * fminf(k, 1.4f), 1); lglass_rim(x, y, w, h, r, 0.05f * k, 0.30f * fminf(k, 1.4f), 0);
}
