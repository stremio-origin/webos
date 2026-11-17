/* The screensaver (ambient mode): one title at a time from Continue Watching and what is trending (never a channel), its
   backdrop at the screen's own size drifting and zooming very slowly (the direction alternates from scene to scene) and
   crossfading into the next; the title's logo (or its name) with a few lines about it low in a corner, coming in a
   moment after the picture and leaving before it; a small clock. Nothing else: no glass, no app mark. Each picture is
   drawn once with its shading baked in: per frame it is two picture quads and a few words. Every 40 s the clock moves
   to another free corner, and every other time the title trades sides (bottom right, sometimes bottom left), so nothing
   stays on the same pixels of the panel. */
#include "screens/saver.h"
#include "screens/player.h"
#include "core/app.h"
#include "core/i18n.h"
#include "core/store.h"
#include "core/live.h"
#include "core/tmdb.h"
#include "core/json.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include "ui/img.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAXSS 16
#define INFO_W 640.0f        /* the title's words */
#define EDGE 96.0f           /* from the panel's edges */
extern int board_saver_items(Item *out, int max);
typedef struct { GfxTarget t; int ok, kb; double at; Tween a; } SsLayer;
static struct {
  int on, n, i, top, loading, tw, th, big; Item list[MAXSS]; char url[512];
  SsLayer L[2]; Tween fade; double startAt, sceneAt, nextAt, checkAt, freeAt; int scene;
  /* the title: its logo or name, its line, what it is about */
  Item cur, nxt; char logo[512], meta[96], desc[1024]; int infoWant, infoOut, descLines; double infoSwapAt; Tween infoA, infoY; /* cur: the title shown; nxt: the one whose picture is on its way */
  /* the clock: its corner (0 top left, 1 top right, 2 bottom left, 3 bottom right), the one it goes to next */
  char hh[4], mm[4]; Timer1 hhNew, mmNew; double clockAt; int cpos, cnext; double cswapAt; Tween clockA;
  /* burn-in shifts */
  int shN, mir, imir, pairGone; double shiftAt, swapAt; Tween pairInfoA; float clockDx, clockDy, infoDx, infoDy;
} SS;

/* elsewhere than Home (a page, the guide, the sign-in): no screensaver, the screen only dims; a key brings it back */
static int dimOn; static Tween dimA;
int saver_on(void) { return SS.on || dimOn; }
int saver_covers(void) { return SS.on && SS.fade.v >= 0.999f; }

/* ---- the clock ---- */
static void clock_update(void) {
  time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm); char h[4], m[4]; if (app_clock_12()) snprintf(h, sizeof h, "%d", tm.tm_hour % 12 ? tm.tm_hour % 12 : 12); else snprintf(h, sizeof h, "%02d", tm.tm_hour); snprintf(m, sizeof m, "%02d", tm.tm_min);
  if (strcmp(h, SS.hh)) { if (SS.hh[0]) timer_start(&SS.hhNew, 0.8f); snprintf(SS.hh, sizeof SS.hh, "%s", h); } /* a new minute fades in; nothing else moves */
  if (strcmp(m, SS.mm)) { if (SS.mm[0]) timer_start(&SS.mmNew, 0.8f); snprintf(SS.mm, sizeof SS.mm, "%s", m); }
}

/* ---- scenes ---- */
/* the backdrop as large as the picture memory allows: the screen's own size where there is room, 720p on small TVs */
static const char *bg_hq(const Item *it, char *buf, size_t cap) {
  sb_bg_mid(it, buf, cap); if (!SS.big) return buf;
  char *p = strstr(buf, "/background/medium/"); if (p && strlen(buf) + 1 < cap) { char rest[512]; snprintf(rest, sizeof rest, "%s", p + 19); snprintf(p, cap - (size_t)(p - buf), "/background/large/%s", rest); }
  return buf;
}
static void bake(SsLayer *l, GLuint tex, float asp) {
  if (!l->t.fbo && !gfx_target_create(&l->t, SS.tw, SS.th)) { l->ok = 0; return; }
  gfx_target_begin(&l->t, RGB(0, 0, 0));
  float u0, v0, u1, v1; img_cover_uv(asp, SB_W / SB_H, 0, &u0, &v0, &u1, &v1);
  gfx_image(tex, 0, 0, SB_W, SB_H, 0, WHITE_A(224), u0, v0, u1, v1);                     /* the picture nearly as it is */
  gfx_rrect_v(0, SB_H * 0.48f, SB_W, SB_H * 0.52f + 1, 0, BLACK_A(0), BLACK_A(160));       /* a low shade: the words at the bottom read on any picture */
  gfx_rrect_v(0, 0, SB_W, SB_H * 0.22f, 0, BLACK_A(70), BLACK_A(0));                       /* a lighter one at the top, for the clock */
  gfx_target_end(); l->ok = 1;
}
static void year_of(const char *ml, char *out, size_t cap) {
  out[0] = 0; for (const char *q = ml; *q; q++) if (q[0] >= '0' && q[0] <= '9' && q[1] >= '0' && q[1] <= '9' && q[2] >= '0' && q[2] <= '9' && q[3] >= '0' && q[3] <= '9' && (q == ml || q[-1] < '0' || q[-1] > '9')) { snprintf(out, cap, "%.4s", q); return; }
}
/* what it is about: the translation, the title's own words, or its details once they come */
static void desc_fill(const cJSON *m) {
  const TmdbLoc *tl = tmdb_wanted(SS.cur.id) ? tmdb_loc(SS.cur.type, SS.cur.id, 1) : NULL;
  const char *d = tl && tl->overview && tl->overview[0] ? tl->overview : SS.cur.description[0] ? SS.cur.description : m ? js_str(m, "description", "") : "";
  snprintf(SS.desc, sizeof SS.desc, "%s", d); SS.descLines = d[0] ? text_wrap_count(d, INFO_W, 3, 25, 400, 0) : 0;
}
static void on_meta(cJSON *m, void *ud) { (void)ud; if (m && SS.on && !strcmp(js_str(m, "id", ""), SS.cur.id) && !SS.desc[0]) desc_fill(m); }
static void scene_load(void) {
  if (!SS.n) return; SS.nxt = SS.list[SS.i % SS.n]; SS.scene = SS.i++;
  bg_hq(&SS.nxt, SS.url, sizeof SS.url); SS.loading = 1; SS.nextAt = 0; /* (the title shown stays until this picture comes in) */
}
/* the title whose picture has come in: its logo, its line and its words, ready to fade in */
static void info_set(void) {
  SS.cur = SS.nxt; sb_logo_of(&SS.cur, SS.logo, sizeof SS.logo); if (SS.logo[0]) img_prefetch(SS.logo, 520, 170);
  char y[8]; year_of(SS.cur.metaLine, y, sizeof y); const char *ty = T(!strcmp(SS.cur.type, "series") ? "Series" : "Movie");
  if (y[0]) snprintf(SS.meta, sizeof SS.meta, "%s  \xc2\xb7  %s", y, ty); else snprintf(SS.meta, sizeof SS.meta, "%s", ty);
  cJSON *m = sb_meta_cached(SS.cur.type, SS.cur.id); SS.desc[0] = 0; desc_fill(m); if (!SS.desc[0] && !m) sb_meta(SS.cur.type, SS.cur.id, on_meta, NULL);
  SS.infoWant = 1;
}
static void scene_start(GLuint tex, float asp) {
  SS.loading = 0; int nx = !SS.top; SsLayer *l = &SS.L[nx];
  bake(l, tex, asp); if (!l->ok) { SS.nextAt = app_now() + 0.6; return; }
  l->kb = SS.scene % 2; l->at = app_now(); tween_set(&l->a, 0); tween_to(&l->a, 1, 1.8f, EASE_IN_OUT);
  tween_to(&SS.L[SS.top].a, 0, 1.8f, EASE_IN_OUT); SS.top = nx; /* the one fading away keeps drifting */
  SS.sceneAt = app_now(); SS.nextAt = SS.sceneAt + 12.2; SS.infoOut = 0; SS.infoWant = 0;
  /* the title shown goes with its picture (both fading as the next one comes in), then the next title fades in */
  if (SS.infoA.v > 0.001f) { tween_to(&SS.infoA, 0, 0.8f, EASE_IN_OUT); tween_to(&SS.infoY, 16, 1.0f, EASE_OUT); SS.infoSwapAt = SS.sceneAt + 0.85; } else { SS.infoSwapAt = 0; info_set(); }
}
static void nudge_rand(float *dx, float *dy) { *dx = (float)(rand() % 41 - 20); *dy = (float)(rand() % 31 - 15); } /* a little off its corner */
/* the clock's next corner: another one, never the title's */
static int clock_pick(int infoRight) { int ban = infoRight ? 3 : 2, c; do c = rand() % 4; while (c == ban || c == SS.cpos); return c; }
static int has_bg(const Item *it) { char b[512]; return it->id[0] && !live_type(it->type) && sb_bg_mid(it, b, sizeof b)[0]; }
static void start(void) {
  Item all[64]; int n = 0;
  { ItemList cw; if (lib_cw_items(&cw) > 0) for (int i = 0; i < cw.n && n < 8; i++) if (has_bg(&cw.items[i])) all[n++] = cw.items[i]; itemlist_clear(&cw); } /* Continue Watching first */
  { Item more[64]; int m = board_saver_items(more, 64); for (int i = 0; i < m && n < 64; i++) { if (!has_bg(&more[i])) continue; int dup = 0; for (int k = 0; k < n && !dup; k++) dup = !strcmp(all[k].id, more[i].id); if (!dup) all[n++] = more[i]; } } /* then what is trending (never a channel) */
  if (!n) return;
  for (int i = n - 1; i > 0; i--) { int j = rand() % (i + 1); Item t = all[i]; all[i] = all[j]; all[j] = t; } /* a different order each time */
  SS.n = n < MAXSS ? n : MAXSS; memcpy(SS.list, all, SS.n * sizeof(Item));
  SS.big = img_budget_mb() >= 80; int tw = SS.big ? 1920 : 1280, th = SS.big ? 1080 : 720; /* (a small TV: 720p, as before) */
  if (tw != SS.tw) { for (int i = 0; i < 2; i++) { gfx_target_free(&SS.L[i].t); SS.L[i].ok = 0; } SS.tw = tw; SS.th = th; }
  SS.on = 1; SS.i = 0; SS.freeAt = 0; tween_to(&SS.fade, 1, 1.1f, EASE_CSS_EASE); SS.startAt = app_now();
  tween_set(&SS.infoA, 0); tween_set(&SS.infoY, 16); SS.hh[0] = SS.mm[0] = 0; clock_update(); SS.clockAt = app_now();
  SS.shN = 0; SS.mir = SS.imir = rand() % 4 != 0; SS.pairGone = 0; SS.swapAt = SS.cswapAt = 0; SS.shiftAt = app_now() + 40; tween_set(&SS.pairInfoA, 1); /* mostly bottom right */
  SS.cpos = -1; SS.cpos = clock_pick(SS.imir); tween_set(&SS.clockA, 0); tween_to(&SS.clockA, 1, 1.2f, EASE_IN_OUT);
  nudge_rand(&SS.clockDx, &SS.clockDy); nudge_rand(&SS.infoDx, &SS.infoDy);
  scene_load(); fprintf(stderr, "[screensaver] on (%d titles, %dp)\n", SS.n, th);
}
void saver_stop(void) {
  if (dimOn) { dimOn = 0; tween_to(&dimA, 0, 0.35f, EASE_CSS_EASE); }
  if (!SS.on) return; SS.on = 0; SS.loading = 0; tween_to(&SS.fade, 0, 1.1f, EASE_CSS_EASE); tween_set(&SS.infoA, 0); SS.freeAt = app_now() + 1.2;
}
/* every 40 s the clock fades and comes back in another corner; every other time the title fades and comes back on the
   other side (bottom right most of the time, now and then bottom left) */
static void shift(void) {
  SS.shN++;
  if (SS.shN % 2 == 0) { int right = rand() % 3 != 0; if (right != SS.mir) { SS.mir = right; SS.pairGone = 1; tween_to(&SS.pairInfoA, 0, 0.9f, EASE_IN_OUT); SS.swapAt = app_now() + 1.2; } }
  SS.cnext = clock_pick(SS.mir); tween_to(&SS.clockA, 0, 0.8f, EASE_IN_OUT); SS.cswapAt = app_now() + 1.0;
}
/* the app's own screen saver is on in Settings (not Off): the TV's is held off while the app is in front, so the app's
   comes instead (read off the main thread by the TV's request: kept here each frame) */
volatile int saver_tv_hold = 1;
void saver_tick(float dt) {
  double now = app_now(); saver_tv_hold = strcmp(store_get("ssDelay", "5"), "0") != 0;
  tween_tick(&SS.fade, dt); tween_tick(&dimA, dt);
  if (dimOn && (player_open() || app_idle() < 1)) { dimOn = 0; tween_to(&dimA, 0, 0.35f, EASE_CSS_EASE); }
  if (!SS.on) {
    if (SS.freeAt && now >= SS.freeAt) { SS.freeAt = 0; for (int i = 0; i < 2; i++) { gfx_target_free(&SS.L[i].t); SS.L[i].ok = 0; } } /* the pictures give their memory back */
    if (now < SS.checkAt) return; SS.checkAt = now + 10;
    double m = atof(store_get("ssDelay", "5"));
    if (m <= 0 || (player_open() && !player_paused()) || app_idle() < m * 60 || dimOn) return;
    if (!app_splash_done || SCR == SCR_LOGIN) { dimOn = 1; tween_to(&dimA, 1, 1.6f, EASE_CSS_EASE); fprintf(stderr, "[screensaver] dimmed\n"); return; } /* everywhere but the sign-in (and over a paused film) */
    start(); return; }
  if (player_open() && !player_paused()) { saver_stop(); return; } /* (a film playing again ends it) */
  for (int i = 0; i < 2; i++) tween_tick(&SS.L[i].a, dt);
  tween_tick(&SS.infoA, dt); tween_tick(&SS.infoY, dt); tween_tick(&SS.pairInfoA, dt); tween_tick(&SS.clockA, dt); timer_tick(&SS.hhNew, dt); timer_tick(&SS.mmNew, dt);
  if (now - SS.clockAt >= 15) { SS.clockAt = now; clock_update(); }
  if (now >= SS.shiftAt) { SS.shiftAt = now + 40; shift(); }
  if (SS.swapAt && now >= SS.swapAt) { SS.swapAt = 0; if (SS.pairGone) { SS.pairGone = 0; SS.imir = SS.mir; nudge_rand(&SS.infoDx, &SS.infoDy); tween_to(&SS.pairInfoA, 1, 0.9f, EASE_IN_OUT); } }
  if (SS.cswapAt && now >= SS.cswapAt) { SS.cswapAt = 0; SS.cpos = SS.cnext; nudge_rand(&SS.clockDx, &SS.clockDy); tween_to(&SS.clockA, 1, 1.0f, EASE_IN_OUT); }
  if (SS.loading) { float asp; GLuint t = img_get(SS.url, SS.tw, SS.th, &asp);
    if (t) scene_start(t, asp); else if (img_failed(SS.url)) { if (SS.big && strstr(SS.url, "/background/large/")) { sb_bg_mid(&SS.nxt, SS.url, sizeof SS.url); } else { SS.loading = 0; SS.nextAt = now + 0.6; } } } /* (no large copy: the usual one) */
  else if (SS.nextAt && now >= SS.nextAt) scene_load();
  /* the title comes in a moment after its picture (with its logo, or the name when it has none) and stays as long as
     the picture does: it leaves only with it, as the next picture comes in */
  if (SS.infoWant && now - SS.sceneAt >= 0.9 && !SS.infoSwapAt) {
    int ready = !SS.logo[0] || img_loaded(SS.logo) || img_failed(SS.logo) || now - SS.sceneAt > 3; if (SS.logo[0]) img_get(SS.logo, 520, 170, NULL);
    if (ready) { SS.infoWant = 0; tween_to(&SS.infoA, 1, 0.9f, EASE_IN_OUT); tween_to(&SS.infoY, 0, 1.1f, EASE_OUT); } }
  if (SS.infoSwapAt && now >= SS.infoSwapAt) { SS.infoSwapAt = 0; info_set(); } /* (the old title has faded with its picture: the new one's turn) */
}

/* ---- drawing ---- */
static void layer_draw(const SsLayer *l, float dim) {
  if (!l->ok || l->a.v <= 0.001f) return;
  float k = (float)((app_now() - l->at) / 13.0); k = clampf(k, 0, 1);
  float s = l->kb ? 1.10f - 0.07f * k : 1.03f + 0.07f * k, tx = (l->kb ? -0.01f + 0.02f * k : 0.012f - 0.024f * k) * SB_W, ty = (l->kb ? 0.008f - 0.012f * k : 0.006f - 0.012f * k) * SB_H;
  float w = SB_W * s, h = SB_H * s, x = SB_W / 2 - w / 2 + s * tx, y = SB_H / 2 - h / 2 + s * ty;
  int c = (int)(255 * (1 - dim) + 0.5f); /* the slow settling dims the picture itself: no extra full-screen layer */
  gfx_image(l->t.tex, x, y, w, h, 0, RGBA(c, c, c, (int)(255 * l->a.v + 0.5f)), 0, 1, 1, 0);
}
void saver_draw(void) {
  if (dimA.v > 0.001f) gfx_rect(0, 0, SB_W, SB_H, BLACK_A((int)(175 * dimA.v)));
  float fa = SS.fade.v; if (fa <= 0.001f) return;
  gfx_opacity_push(fa);
  const SsLayer *top = &SS.L[SS.top], *und = &SS.L[!SS.top];
  if (!(top->ok && top->a.v >= 0.999f)) gfx_rect(0, 0, SB_W, SB_H, RGB(0, 0, 0));
  float dk = clampf((float)((app_now() - SS.startAt) / 60.0), 0, 1), dim = SS.on ? 0.22f * (1 - powf(1 - dk, 2)) : 0; /* the TV settles: a little darker over the first minute */
  layer_draw(und, dim); layer_draw(top, dim);
  /* the clock: small and light, in its corner */
  static float cell, wc; if (!cell) { for (char d = '0'; d <= '9'; d++) { char t[2] = { d, 0 }; float w = text_width(t, 54, 300, 0); if (w > cell) cell = w; } cell -= 1; wc = text_width(":", 54, 300, 0) + 2; } /* tabular figures, measured once */
  if (SS.hh[0] && SS.clockA.v > 0.001f) { float tw = cell * 2 + wc + cell * 2, lh = text_line_height(54); int right = SS.cpos == 1 || SS.cpos == 3, low = SS.cpos >= 2;
    float x = right ? SB_W - EDGE - tw - SS.clockDx : EDGE + SS.clockDx, y = low ? SB_H - 84 - lh - SS.clockDy : 64 + SS.clockDy;
    gfx_opacity_push(SS.clockA.v * 0.9f);
    float ha = SS.hhNew.on ? timer_x(&SS.hhNew) : 1, ma = SS.mmNew.on ? timer_x(&SS.mmNew) : 1;
    if (ha < 1) gfx_opacity_push(ha); for (int k = 0; k < 2 && SS.hh[k]; k++) { char t[2] = { SS.hh[k], 0 }; text_draw(t, x + cell * k + cell / 2, y, 54, 300, WHITE_A(235), 0, 0, ALIGN_CENTER); } if (ha < 1) gfx_opacity_pop();
    text_draw(":", x + cell * 2 + 1, y - 3, 54, 300, WHITE_A(235), 0, 0, ALIGN_LEFT);
    if (ma < 1) gfx_opacity_push(ma); for (int k = 0; k < 2 && SS.mm[k]; k++) { char t[2] = { SS.mm[k], 0 }; text_draw(t, x + cell * 2 + wc + cell * k + cell / 2, y, 54, 300, WHITE_A(235), 0, 0, ALIGN_CENTER); } if (ma < 1) gfx_opacity_pop();
    gfx_opacity_pop(); }
  /* the title: its logo or its name, "year · type", and a few lines about it */
  float ia = SS.infoA.v * SS.pairInfoA.v;
  if (ia > 0.001f && SS.on) { gfx_opacity_push(ia);
    int right = SS.imir; float ax = right ? SB_W - EDGE - SS.infoDx : EDGE + SS.infoDx; int al = right ? ALIGN_RIGHT : ALIGN_LEFT;
    float asp = 0; GLuint lt = SS.logo[0] ? img_get(SS.logo, 520, 170, &asp) : 0; float lw = 0, lh = 0, th = 0;
    if (lt && asp > 0) { lw = 520; lh = lw / asp; if (lh > 170) { lh = 170; lw = lh * asp; } th = lh; }
    else { static int ln; static char lnName[256]; static unsigned lnLg; if (lnLg != i18n_gen() || strcmp(lnName, SS.cur.name)) { lnLg = i18n_gen(); snprintf(lnName, sizeof lnName, "%s", SS.cur.name); ln = text_wrap_count(SS.cur.name, INFO_W, 2, 60, 800, -1.2f); } /* once per title */
      th = ln * 60 * 1.05f; }
    float mh = text_line_height(24), dh = SS.descLines * 36, y = SB_H - 84 - SS.infoDy - dh - (dh > 0 ? 14 : 0) - mh - 18 - th + SS.infoY.v;
    if (lt) gfx_image(lt, right ? ax - lw : ax, y, lw, lh, 0, WHITE_A(255), 0, 0, 1, 1);
    else text_draw_wrapped(SS.cur.name, ax, y, INFO_W, 2, 60, 800, WHITE_A(255), -1.2f, al, 60 * 1.05f);
    y += th + 18; text_draw(SS.meta, ax, y, 24, 600, WHITE_A(190), 0.4f, INFO_W, al); y += mh + 14;
    if (SS.descLines) text_draw_wrapped(SS.desc, ax, y, INFO_W, 3, 25, 400, WHITE_A(215), 0, al, 36);
    gfx_opacity_pop(); }
  gfx_opacity_pop();
}
