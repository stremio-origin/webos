#include "ui/livewall.h"
#include "ui/gfx.h"
#include "ui/img.h"
#include "ui/text.h"
#include "core/app.h"
#include "core/i18n.h"
#include "core/live.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

int gfx_in_target(void);

/* the grid, in layout units: tiles staggered column by column so the rows run on a slant */
#define TW 250.0f
#define TH 160.0f
#define PX (TW + 34)
#define PY (TH + 34)
#define SLANT 0.38f            /* each column sits this much of a row lower than the one before */
#define NL 28                  /* logos on the wall */
#define WW 2400.0f             /* the wall, layout units: wide and low, every surface pans across it */
#define WH 600.0f
#define TEXW 2048              /* baked once into this (0.85 texel a unit, so the hero is barely enlarged) */
#define TEXH 512
#define WIN 0.20f              /* the part of its width a card shows */
#define FADE 1.4               /* the wall comes in over this once it is baked (never a picture popping in) */

static char logos[NL][300]; static int nl, seenTot;
static GfxTarget wt; static int wtOk = -1, baked, final; static double checkAt, askAt, bakedAt; static unsigned tickFrame;
static int have[NL], nh; /* the logos that came, the only ones the wall shows */

/* the logos: spread over every catalog's channels, each once */
static void gather(void) {
  int tot = 0, nc = live_catalogs(); for (int c = 0; c < nc; c++) { int n = live_catalog(c, NULL, NULL); if (n > 0) tot += n; }
  if (tot == seenTot) return; seenTot = tot; if (baked && nh >= 12) return; /* a full wall stays as it is (no second bake showing) */
  nl = 0; if (!tot) return;
  int step = tot / (NL * 2); if (step < 1) step = 1;
  for (int pass = 0; pass < 2 && nl < NL; pass++)
    for (int c = 0; c < nc && nl < NL; c++) { const LiveCh *ch; int n = live_catalog(c, NULL, &ch); if (n <= 0 || !ch) continue;
      for (int i = pass * (step / 2 + 1) % (n > 0 ? n : 1); i < n && nl < NL; i += step) { const char *u = ch[i].logo; if (!*u) continue;
        int dup = 0; for (int k = 0; k < nl; k++) if (!strcmp(logos[k], u)) { dup = 1; break; } if (dup) continue;
        snprintf(logos[nl++], sizeof logos[0], "%s", u); } }
  final = 0; askAt = app_now();
}
static GLuint logo_tex(int i, float *asp) { return img_get(logos[i], (int)(TW * 0.66f), (int)(TH * 0.56f), asp); }

/* the grid over a w x h box at (x, y): every tile holds one of the logos that came */
static void grid(float x, float y, float w, float h) {
  int c1 = (int)floorf(w / PX);
  for (int c = -1; c <= c1; c++) { float sl = fmodf(c * SLANT, 1.0f); if (sl < 0) sl += 1; float base = -sl * PY; /* the column's slant moves its rows down */
    for (int r = -1; r <= (int)floorf(h / PY) + 1; r++) { float tx = x + c * PX, ty = y + r * PY - base;
      if (ty + TH < y || ty > y + h) continue;
      gfx_rrect_v(tx, ty, TW, TH, 26, WHITE_A(20), WHITE_A(9)); gfx_ring(tx, ty, TW, TH, 26, 1.5f, WHITE_A(22));
      float asp; int k = have[(((c * 7 + r * 11) % nh) + nh) % nh]; GLuint t = logo_tex(k, &asp);
      if (t && asp > 0) { float lw = TW * 0.60f, lh = lw / asp; if (lh > TH * 0.52f) { lh = TH * 0.52f; lw = lh * asp; }
        gfx_image(t, tx + TW / 2 - lw / 2, ty + TH / 2 - lh / 2, lw, lh, 0, WHITE_A(235), 0, 0, 1, 1); } } }
}

/* once a frame: the logos are asked for until they are all in (or 8 s went by), then the wall is baked, once,
   and fades in; after that nothing here touches the pictures */
static void tick(void) {
  if (tickFrame == gfx_frame()) return; tickFrame = gfx_frame(); gather();
  if (final || !nl) return;
  double now = app_now(); if (now < checkAt || gfx_in_target()) return; checkAt = now + 0.4;
  int done = 0; nh = 0; for (int i = 0; i < nl; i++) { float asp; if (logo_tex(i, &asp)) { have[nh++] = i; done++; } else if (img_failed(logos[i])) done++; }
  if (done < nl && now - askAt < 8) return;
  if (!nh) { askAt = now - 4; return; } /* no logo yet (a busy start): asked again, the wall fades in once they come */
  final = 1;
  if (wtOk < 0) wtOk = gfx_target_create(&wt, TEXW, TEXH); if (!wtOk) return;
  float sc = SB_W / WW; gfx_target_begin(&wt, RGBA(14, 13, 20, 255)); gfx_scale_push(sc, sc, 0, 0); grid(0, 0, WW, WH); gfx_scale_pop(); gfx_target_end();
  if (!baked) bakedAt = now; baked = 1;
}
static float wall_in(void) { if (!baked) return 0; float t = (float)((app_now() - bakedAt) / FADE); if (t >= 1) return 1; return t * t * (3 - 2 * t); }
/* a window of the wall over a box (ws: the part of its width), wandering slowly over it and never resting */
static void window(float x, float y, float w, float h, float r, float ws, float a, float fu, float fv) {
  a *= wall_in(); if (a <= 0.003f) return; double now = app_now();
  float hs = ws * WW * h / (w * WH); if (hs > 0.95f) { ws *= 0.95f / hs; hs = 0.95f; }
  float u = (1 - ws) / 2 * (1 + 0.92f * sinf((float)now * fu)), v = (1 - hs) / 2 * (1 + 0.9f * sinf((float)now * fv + 1.3f));
  gfx_image(wt.tex, x, y, w, h, r, WHITE_A((int)(255 * a)), u, 1 - v, u + ws, 1 - v - hs);
}

static GLuint soft_tex(void) { static GLuint t; if (t) return t; enum { N = 64 }; static unsigned char px[N * N];
  for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) { float dx = (x + .5f) / N * 2 - 1, dy = (y + .5f) / N * 2 - 1, d = sqrtf(dx * dx + dy * dy); float a = d < 1 ? 1 - d * d : 0; px[y * N + x] = (unsigned char)(a * a * 255); }
  t = gfx_tex_create(px, N, N, 1, 1); return t; }

/* All channels set as a wordmark: capitals (where the language has them), heavy and tight. x is its anchor for
   align; returns the height used */
float livewall_word(float x, float y, int px, float maxW, int align) {
  static char wd[128]; static unsigned lg; if (lg != i18n_gen() || !wd[0]) { lg = i18n_gen(); snprintf(wd, sizeof wd, "%s", T("All channels"));
    int ascii = 1; for (const char *p = wd; *p; p++) if ((unsigned char)*p >= 128) ascii = 0; if (ascii) for (char *p = wd; *p; p++) *p = (char)toupper((unsigned char)*p); }
  text_draw(wd, x, y, px, 900, WHITE_A(255), -px * 0.025f, maxW, align);
  return text_line_height(px);
}

void livewall_card(float x, float y, float w, float h, float r, int focused) {
  tick(); double now = app_now(); uint32_t ac = ACC2 | 0xff000000u; float cx = x + w / 2, cy = y + h / 2, m = w < h ? w : h; int label = fabsf(w - h) > 1;
  gfx_rrect(x, y, w, h, r, RGB(14, 13, 20));
  window(x, y, w, h, r, WIN * (label ? 1.25f : 1), 1, 0.071f, 0.053f);
  /* the theme over it: the accent from above, deep below, and a light turning slowly round */
  float ph = (float)fmod(now / 11.0, 1) * 6.2832f;
  gfx_rrect_v(x, y, w, h, r, col_alpha(col_mix(ac, RGB(10, 9, 15), 0.30f), 0.34f), col_alpha(col_mix(ac, RGB(6, 5, 10), 0.8f), 0.74f));
  gfx_rrect_h(x, y, w, h, r, col_alpha(ac, 0.22f * (0.5f + 0.5f * cosf(ph))), col_alpha(ac, 0.22f * (0.5f - 0.5f * cosf(ph))));
  gfx_alpha_tex(soft_tex(), cx - m * (label ? 0.7f : 0.42f), cy - m * 0.42f, m * (label ? 1.4f : 0.84f), m * 0.84f, BLACK_A(150)); /* a soft dark at the centre, under the mark */
  static float lw; static unsigned lg; if (!lw || lg != i18n_gen()) { lg = i18n_gen(); lw = text_width(T("LIVE"), 19, 800, 2.6f); }
  float lh = text_line_height(19), pu = 0.5f + 0.5f * sinf((float)now * 3.0f);
  if (label) { /* the wide tile: the wordmark, as on Home's hero */
    float wy = cy - (lh + 8 + text_line_height(34)) / 2, tw = 16 + lw, tx = cx - tw / 2;
    gfx_circle(tx + 4, wy + lh / 2, 6 + 3 * pu, RGBA(0xff, 0x4a, 0x4a, (int)(60 * (1 - pu)))); gfx_circle(tx + 4, wy + lh / 2, 4.2f, RGB(0xff, 0x4a, 0x4a));
    text_draw(T("LIVE"), tx + 16, wy, 19, 800, WHITE_A(235), 2.6f, 0, ALIGN_LEFT);
    livewall_word(cx, wy + lh + 8, 34, w - 32, ALIGN_CENTER); }
  else { /* the round badge: LIVE alone at its centre with its red dot (the name is under the card) */
    float tw = 16 + lw, tx = cx - tw / 2, ty = cy - lh / 2;
    gfx_circle(tx + 4, ty + lh / 2, 6 + 3 * pu, RGBA(0xff, 0x4a, 0x4a, (int)(60 * (1 - pu)))); gfx_circle(tx + 4, ty + lh / 2, 4.2f, RGB(0xff, 0x4a, 0x4a));
    text_draw(T("LIVE"), tx + 16, ty, 19, 800, WHITE_A(245), 2.6f, 0, ALIGN_LEFT); }
  gfx_ring(x, y, w, h, r, 1.5f, focused ? col_alpha(ac, 0.9f) : WHITE_A(40));
}

/* Home's hero over a live title: the wall across its top, the accent glowing in from the right, dark where the words and the rows sit */
void livewall_hero(float a) {
  tick(); double now = app_now(); uint32_t ac = ACC2 | 0xff000000u, deep = RGB(10, 9, 15); const float H = 620;
  gfx_rect(0, 0, SB_W, SB_H, col_alpha(deep, a));
  window(0, 0, SB_W, H, 0, SB_W / WW, 0.46f * a, 0.021f, 0.031f);
  float ph = (float)fmod(now / 16.0, 1) * 6.2832f;
  gfx_alpha_tex(soft_tex(), 900 + 90 * cosf(ph), -420 + 60 * sinf(ph), 1400, 1100, col_alpha(ac, 0.40f * a));
  gfx_rrect_h(0, 0, SB_W * 0.78f, H, 0, col_alpha(deep, 0.96f * a), col_alpha(deep, 0));
  gfx_rrect_v(0, 330, SB_W, H - 330, 0, col_alpha(deep, 0), col_alpha(deep, a));
}
void livewall_backdrop(float a) { tick(); window(0, 0, SB_W, 620, 0, SB_W / WW, a, 0.021f, 0.031f); } /* (the hub is dark below its tabs) */
