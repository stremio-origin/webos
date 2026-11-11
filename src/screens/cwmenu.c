/* The card menu (hold OK on any card or episode): a glass card as tall as its options, centred on its side, the
   accent highlight gliding under the focus. */
#include "ui/sound.h"
#include "ui/orbs.h"
#include "screens/cwmenu.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include "screens/detail.h"
#include "screens/revpanel.h"
#include "screens/sheet.h"
#include "core/app.h"
#include "core/keys.h"
#include "core/i18n.h"
#include "core/json.h"
#include "core/trailer.h"
#include "ui/gfx.h"
#include "ui/glass.h"
#include "ui/img.h"
#include "ui/text.h"
#include "ui/icon.h"
#include "ui/widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/hit.h"

#define CW 640.0f
#define PX 36.0f
#define IH 64.0f    /* an option, and 4 between them */
enum { A_STREAMS, A_PLAY, A_RESTART, A_DETAILS, A_WATCHED, A_TRAILER, A_SIMILAR, A_REVIEWS, A_LIB };
static struct {
  int open, closing, idx, n; int acts[9]; char labels[9][96]; const char *icons[9]; Item it; char kind[32], sub[400];
  Tween x, op; /* the panel's slide and fade: an even ease-out (the quick-tween curve put it all in the first two frames) */ Tween out, grow; double openAt; SGlass glass; FPill pill; Nudge nudge; float h, titleLines;
  float from[5]; /* the card that was held, as it sat on screen: Reviews grows out of it */
} M;
static void close(void) { if (!M.open) return; M.open = 0; M.closing = 1; app_occluder_drop(); if (app_overlay) app_overlay = NULL; tween_set(&M.out, M.op.v); tween_to(&M.out, 0, 0.18f, EASE_IN_OUT); tween_to(&M.x, 28, 0.2f, EASE_CSS_EASEOUT); }
static void add(int a, const char *icon, const char *label) { if (M.n >= 9) return; M.acts[M.n] = a; M.icons[M.n] = icon; snprintf(M.labels[M.n], sizeof M.labels[M.n], "%s", label); M.n++; }
static void on_meta_toggle(cJSON *m, void *ud) { (void)ud; Item it = M.it; lib_toggle_watched(&it, js_arr(m, "videos")); app_refresh_boards(); if (SCR == SCR_DETAIL) detail_refresh_watched(); }
/* what an action did, said on the title's own card: a glass chip over it ("Added to Library", "Marked as watched");
   adding, it then folds into its icon, which flies up to the Library tab in one eased arc (a faint trail behind it)
   and lights it as it lands; watched, it folds into the card's check, which pops in; a removal: the icon comes down
   from the tab and opens into the chip, which fades. About a second, all of it: quick, never abrupt */
static struct { int kind; double t0; float px, py; char text[96]; const char *icon; int landed; char poster[512]; float lx, ly, vx, vy; } FX; /* poster: the title's, carried in the bubble; lx..vy its last place and speed */ /* kind: 1 added, 2 removed, 3 watched, 4 unwatched */
static void fx_start(int kind, const char *icon, const char *text) {
  float x = SB_W / 2, y = SB_H / 2; Screen *sc = screens[SCR]; if (!(sc && sc->origin && sc->origin(&x, &y))) { x = SB_W / 2; y = SB_H / 2; }
  FX.kind = kind; FX.t0 = app_now(); FX.px = x; FX.py = y; FX.icon = icon; FX.landed = 0; snprintf(FX.text, sizeof FX.text, "%s", text); FX.poster[0] = 0; FX.lx = x; FX.ly = y; FX.vx = FX.vy = 0;
}
static float fx_e(float u) { u = u < 0 ? 0 : u > 1 ? 1 : u; return u * u * (3 - 2 * u); }
static float fx_c(float u) { return ease_apply(EASE_STD, u < 0 ? 0 : u > 1 ? 1 : u); } /* the flights: away softly, in softly */
#define FX_IN 0.16f    /* the chip comes in */
#define FX_FOLD 0.52f  /* it folds to its icon */
#define FX_FLY 0.38f   /* the bubble leaves for the tab */
#define FX_FLYD 0.55f  /* and takes this long */
/* the icon on its way, at u (0 the card, 1 the tab): an arc above the straight line, shrinking as it nears */
static void fx_icon_at(float u, float a, float fromX, float fromY) {
  float lx = topNav.chipX[NAV_LIB] + topNav.chipW[NAV_LIB] / 2, ly = 50, e = fx_c(u);
  float cx = fromX + (lx - fromX) * e, cy = fromY + (ly - fromY) * e - 110 * sinf(3.14159f * e), sc = 1 - 0.45f * e;
  gfx_opacity_push(a); gfx_scale_push(sc, sc, cx, cy); gfx_shadow(cx - 29, cy - 21, 58, 58, 29, 20, BLACK_A(110)); gfx_circle(cx, cy, 29, RGBA(16, 14, 22, 212)); gfx_circle(cx, cy, 19, ACC); icon_draw(FX.icon, cx - 13, cy - 13, 26, WHITE_A(255)); gfx_scale_pop(); gfx_opacity_pop();
}
/* added: the chip, with the title's small poster in it, leaves as a bubble of liquid glass for the Library tab. It bends
   the backdrop under it wherever it is (more as it goes faster), stretches the way it moves and wobbles as it slows, the
   poster inside lagging a little; near the tab the tab reaches for it and swallows it, then jiggles and settles */
static void fx_glass(float x, float y, float w, float h, float pull, float a) { /* liquid glass over the backdrop, anywhere on the screen */
  float al = 1; GLuint bt = bg_glass_tex(&al); if (bt && al > 0.05f) gfx_liquid(bt, x, y, w, h, h / 2, pull, a * al, x / SB_W, 1 - y / SB_H, (x + w) / SB_W, 1 - (y + h) / SB_H); }
#define FX_TABX() (topNav.chipX[NAV_LIB] + topNav.chipW[NAV_LIB] / 2)
static void fx_bubble(float u, float t, float cw0) {
  float lx = FX_TABX(), ly = 50, e = fx_c(u);
  float cx = FX.px + (lx - FX.px) * e, cy = FX.py + (ly - FX.py) * e - 120 * sinf(3.14159f * e);
  float dt = app_dt(); if (dt > 0) { float vx = (cx - FX.lx) / dt, vy = (cy - FX.ly) / dt; FX.vx += (vx - FX.vx) * fminf(1, dt * 18); FX.vy += (vy - FX.vy) * fminf(1, dt * 18); } FX.lx = cx; FX.ly = cy;
  float sp = sqrtf(FX.vx * FX.vx + FX.vy * FX.vy), k = fminf(sp / 3200.0f, 1), ax = fabsf(FX.vx) / (sp + 1), ay = fabsf(FX.vy) / (sp + 1); /* its speed, and which way */
  float sw = 1 + 0.22f * k * ax - 0.10f * k * ay, sh = 1 + 0.22f * k * ay - 0.10f * k * ax; /* stretched along its way */
  float wob = 0.10f * sinf(t * 19.0f) * (1 - e) * (1 - k); sw += wob; sh -= wob; /* the bubble's jiggle as it slows */
  float m = fx_e(u / 0.45f), w = cw0 + (54 - cw0) * m, h = 64 + (54 - 64) * m, sink = fx_e((u - 0.80f) / 0.20f), sc = 1 - 0.85f * sink; /* the pill rounds into a drop early in its flight */
  if (u > 0.68f) { float bk = fx_e((u - 0.68f) / 0.32f), top = ly + 26, bot = cy - h * 0.3f * sc; /* the neck of liquid joining it to the tab, thickening as they meet */
    if (bot > top + 2) { float nw = 18 + 30 * bk, nx = cx - nw / 2 + (lx - cx) * 0.5f * bk; nw *= 1 - 0.3f * fx_e((u - 0.9f) / 0.1f);
      gfx_opacity_push(0.9f * bk); fx_glass(nx, top, nw, bot - top, 16, 1); gfx_rrect_v(nx, top, nw, bot - top, nw / 2, col_alpha(ACC | 0xff000000u, 0.34f), col_alpha(ACC2 | 0xff000000u, 0.40f)); gfx_opacity_pop(); } }
  float dx = lx - cx, dy = ly - cy, pullT = sink * 0.6f; cx += dx * pullT; cy += dy * pullT; /* drawn into the tab's mouth */
  gfx_opacity_push(1 - 0.5f * sink); gfx_scale_push(sw * sc, sh * sc, cx, cy);
  float x = cx - w / 2, y = cy - h / 2;
  gfx_shadow(x, y + 10, w, h, h / 2, 30, BLACK_A(100));
  fx_glass(x, y, w, h, fminf(h * 0.5f, 28) * (1 + 0.6f * k), 1);
  gfx_rrect_v(x, y, w, h, h / 2, col_alpha(ACC2 | 0xff000000u, 0.42f), col_alpha(ACC | 0xff000000u, 0.32f));
  gfx_rrect_v(x, y, w, h, h / 2, WHITE_A(40), WHITE_A(0)); gfx_ring(x, y, w, h, h / 2, 1, WHITE_A(34)); /* (a hairline: the glass, not an outline) */
  { float ph = h - 12, pw = ph * 2 / 3, ppx = x + 8 - FX.vx * 0.004f, ppy = y + 6 - FX.vy * 0.004f; /* the title's poster, lagging a little behind the glass */
    GLuint pt = FX.poster[0] ? img_get_shaped(FX.poster, 66, 99, 8, 0) : 0; float pa = 1 - fx_e((u - 0.15f) / 0.4f); /* (the poster gone before it reaches the tab) */
    if (pt && pa < 0.999f) { gfx_opacity_push(1 - pa); icon_draw(FX.icon, cx - 13, cy - 13, 26, ACC_FG); gfx_opacity_pop(); }
    if (pt && pa > 0.01f) { gfx_opacity_push(pa); gfx_clip_push(x + 4, y + 2, w - 8, h - 4); gfx_image(pt, ppx, ppy, pw, ph, 8, WHITE_A(255), 0, 0, 1, 1); gfx_ring(ppx, ppy, pw, ph, 8, 1, WHITE_A(60)); gfx_clip_pop(); gfx_opacity_pop(); }
    else if (!pt) icon_draw(FX.icon, cx - 13, cy - 13, 26, ACC_FG);
    float ta = 1 - fx_e(u / 0.32f); if (ta > 0.01f) { gfx_opacity_push(ta); text_draw(FX.text, x + 8 + (pt ? pw : 30) + 14, cy - text_line_height(24) / 2.0f, 24, 700, ACC_FG, 0.1f, w - pw - 40, ALIGN_LEFT); gfx_opacity_pop(); } }
  gfx_scale_pop(); gfx_opacity_pop();
}
/* the Library tab taking it in: it leans and swells towards the bubble as it comes (r 0..1), then jiggles and settles (j: s since) */
static void fx_tab(float r, float j) {
  float lx = topNav.chipX[NAV_LIB], lw = topNav.chipW[NAV_LIB], cx = lx + lw / 2, cy = 50;
  float lean = r * (1 - fminf(j * 6, 1)), jig = j > 0 ? expf(-9.0f * j) * sinf(j * 26.0f) : 0, a = fminf(1, r * 1.4f) * (j > 0 ? 1 - fx_e(j / 0.45f) : 1); /* (settled and gone within half a second) */
  if (a <= 0.004f) return;
  float sx = 1 + 0.10f * lean + 0.10f * jig, sy = 1 + 0.16f * lean - 0.10f * jig, ox = (FX.lx - cx) * 0.06f * lean, oy = (FX.ly - cy) * 0.10f * lean; /* reaching for it, then the jiggle */
  float w = lw, h = 66, x = cx - w / 2 + ox, y = cy - h / 2 + oy;
  gfx_scale_push(sx, sy, cx + ox, cy + oy); gfx_opacity_push(a);
  fx_glass(x, y, w, h, 24 * (1 + lean), 1);
  gfx_rrect_v(x, y, w, h, h / 2, col_alpha(ACC2 | 0xff000000u, 0.26f), col_alpha(ACC | 0xff000000u, 0.18f)); gfx_rrect_v(x, y, w, h, h / 2, WHITE_A(30), WHITE_A(0)); gfx_ring(x, y, w, h, h / 2, 1, WHITE_A(30));
  text_draw(T("Library"), x + w / 2, y + h / 2 - text_line_height(32) / 2.0f, 32, 700, WHITE_A(255), 0, 0, ALIGN_CENTER); /* (its word stays, riding the swell) */
  gfx_opacity_pop(); gfx_scale_pop();
}
static void fx_draw(void) {
  if (!FX.kind) return; float t = (float)(app_now() - FX.t0);
  if (t > (FX.kind == 1 ? 1.45f : FX.kind == 2 ? 1.25f : 1.0f)) { FX.kind = 0; return; } /* (added: the tab has settled by then) */
  float lx = topNav.chipX[NAV_LIB], lw = topNav.chipW[NAV_LIB];
  if (FX.kind == 2) { /* removed: its icon leaves the Library tab and comes down to the card, where it opens into the chip */
    if (t < 0.3f) { float u = t / 0.3f; gfx_ring(lx - 6, 50 - 38 - 6, lw + 12, 76 + 12, 44, 2.5f, col_alpha(ACC2, 0.5f * (1 - u))); fx_icon_at(1 - u, fx_e(t / 0.08f), FX.px, FX.py); return; }
    t -= 0.3f; }
  float in = fx_e(t / FX_IN), fold = FX.kind == 3 ? fx_e((t - FX_FOLD) / 0.12f) : 0, /* (added: it stays a pill, and flies as one) */ go = FX.kind == 2 || FX.kind == 4 ? fx_e((t - 0.62f) / 0.22f) : 0;
  float tw = text_width(FX.text, 24, 600, 0.1f), ch = FX.kind == 1 ? 64 : 58, cw = (FX.kind == 1 && FX.poster[0] ? 8 + 35 + 14 + tw + 26 : 58 + tw + 22) * (1 - fold) + ch * fold; /* the chip; folding, it closes down to its round icon */
  if (FX.kind == 1 && t >= FX_FLY) { float u = (t - FX_FLY) / FX_FLYD;
    float land = FX_FLY + FX_FLYD, j = t > land ? t - land : 0; (void)lx; (void)lw;
    if (u > 0.6f) fx_tab(fminf((u - 0.6f) / 0.4f, 1), j); /* the tab reaching for it, then swallowing it */
    if (u < 1) fx_bubble(u, t, cw);
    else if (!FX.landed) { FX.landed = 1; orbs_pulse(ORB_DONE, 0.8f); }
    return; }
  if (FX.kind == 1) { float s0 = 0.9f + 0.1f * ease_apply(EASE_OUT_BACK, t / FX_IN); /* added: the bubble itself from the start (the poster in it, jiggling as it waits) */
    gfx_opacity_push(in); gfx_scale_push(s0, s0, FX.px, FX.py); fx_bubble(0, t, cw); gfx_scale_pop(); gfx_opacity_pop(); return; }
  float sc = (0.9f + 0.1f * ease_apply(EASE_OUT_BACK, t / FX_IN)) * (FX.kind == 3 ? 1 - 0.3f * fx_e((t - 0.62f) / 0.24f) : 1);
  float a = in * (1 - go) * (FX.kind == 3 ? 1 - fx_e((t - 0.66f) / 0.24f) : 1), cx = FX.px, cy = FX.py;
  if (a > 0.003f) {
    float x = cx - cw / 2, y = cy - ch / 2; gfx_opacity_push(a); gfx_scale_push(sc, sc, cx, cy);
    lglass_shell(x, y, cw, ch, ch / 2, RGBA(16, 14, 22, 205), 1); /* glass */
    gfx_circle(x + ch / 2, cy, 19, ACC); icon_draw(FX.icon, x + ch / 2 - 13, cy - 13, 26, WHITE_A(255));
    if (fold < 0.99f) { gfx_opacity_push(1 - fold); text_draw(FX.text, x + ch + 4, cy - text_line_height(24) / 2.0f, 24, 600, WHITE_A(245), 0.1f, cw - ch - 14, ALIGN_LEFT); gfx_opacity_pop(); }
    gfx_scale_pop(); gfx_opacity_pop(); }
}
static void act(int a) {
  Item it = M.it; fpill_press(&M.pill);
  switch (a) {
    case A_STREAMS: close(); detail_open_streams(it.videoId); break;
    case A_PLAY: close(); app_open_item(&it); break;
    case A_RESTART: close(); app_play_from_start(&it); break;
    case A_DETAILS: { close(); DetailParams p; memset(&p, 0, sizeof p); p.it = it; app_go(SCR_DETAIL, &p, 0); break; }
    case A_WATCHED: { int was = lib_is_marked_watched(&it); sound_play(SND_TOGGLE); if (was) fx_start(4, "eyeOff", T("Marked as unwatched")); else { fx_start(3, "check", T("Marked as watched")); card_mark_pop(it.id); } } close(); if (!strcmp(it.type, "series") || !strcasecmp(it.type, "anime") || !strcasecmp(it.type, "Anime Series")) { cJSON *m = sb_meta_cached(it.type, it.id); if (m) on_meta_toggle(m, NULL); else sb_meta(it.type, it.id, on_meta_toggle, NULL); } else on_meta_toggle(NULL, NULL); break;
    case A_TRAILER: close(); detail_trailer(it.type, it.id, it.name); break;
    case A_SIMILAR: { close(); DetailParams p; memset(&p, 0, sizeof p); p.it = it; app_go(SCR_SIM, &p, 0); break; }
    case A_REVIEWS: close(); revpanel_show(&it, 0, M.from[0], M.from[1], M.from[2], M.from[3], M.from[4]); break;
    case A_LIB: { int in = lib_has(it.id); sound_play(SND_TOGGLE); fx_start(in ? 2 : 1, in ? "minus" : "plus", T(in ? "Removed from Library" : "Added to Library")); sb_poster_of(&it, FX.poster, sizeof FX.poster); } lib_toggle(&it); close(); app_refresh_boards(); if (SCR == SCR_DETAIL) detail_paint_lib(); break;
  }
}
static int key(int k) {
  int i0 = M.idx;
  if (k == K_DOWN) M.idx = M.idx + 1 < M.n ? M.idx + 1 : M.idx;
  else if (k == K_UP) M.idx = M.idx > 0 ? M.idx - 1 : 0;
  else if (k == K_OK) { act(M.acts[M.idx]); return 1; }
  else if (k == K_LEFT || k == K_BACK) { close(); return 1; }
  if (M.idx == i0) nudge_start(&M.nudge, k);
  return 1;
}
static float items_y(void) { return 40 + 30 + 6 + M.titleLines * 46 + (M.sub[0] ? 4 + text_line_height(24) : 0) + 26; }
static void tick(float dt) {
  tween_tick(&M.x, dt > 0.034f ? 0.034f : dt); tween_tick(&M.op, dt > 0.034f ? 0.034f : dt); tween_tick(&M.out, dt); tween_tick(&M.grow, dt > 0.034f ? 0.034f : dt); if (M.closing && !M.out.on) M.closing = 0;
  nudge_tick(&M.nudge, dt); fpill_tick(&M.pill, dt); sglass_tick(&M.glass, dt);
  if (M.open) fpill_to(&M.pill, 0, M.idx * (IH + 4), CW - 2 * PX, IH, GLIDE_DUR);
}
static void item_focus(int i, int b) { (void)b; if (M.idx != i) M.idx = i; }
static void item_act(int i, int b) { (void)b; M.idx = i; act(M.acts[i]); }
static void outside_act(int a, int b) { (void)a; (void)b; close(); }
static float card_x(void) { return app_rtl() ? 32 : SB_W - 32 - CW; }
static void draw_menu(void);
static void draw(void) { draw_menu(); fx_draw(); } /* (the action's chip outlives the menu) */
static void draw_menu(void) {
  float a = M.closing ? M.out.v : M.open ? M.op.v : 0; if (a <= 0.001f) return;
  int rtl = app_rtl(); float cx = card_x(), cy = SB_H / 2 - M.h / 2;
  if (M.open) { hit_act(0, 0, SB_W, SB_H, outside_act, 0, 0); hit_shield(cx, cy, CW, M.h); }
  /* the card grows into place as it slides in (0.94 to 1) and settles back a little as it leaves */
  float sc = M.closing ? 0.98f + 0.02f * M.out.v : 0.96f + 0.04f * M.grow.v;
  gfx_opacity_push(a); gfx_offset_push(M.x.v * (rtl ? -1 : 1), 0); gfx_scale_push(sc, sc, cx + CW / 2, SB_H / 2);
  if (M.glass.ok && M.glass.a.v >= 0.999f) gfx_shadow_under(cx, cy + 30, CW, M.h, 28, 80, BLACK_A(140), cx, cy, CW, M.h, 28); else gfx_shadow(cx, cy + 30, CW, M.h, 28, 80, BLACK_A(140)); /* (the card opaque: its middle needs no shadow) */
  sglass_draw(&M.glass, cx, cy, CW, M.h, 28); gfx_ring(cx, cy, CW, M.h, 28, 1, WHITE_A(18));
  if (M.open && !M.closing && a >= 0.999f && fabsf(M.x.v) < 0.01f && sc >= 0.9999f && M.glass.ok && M.glass.a.v >= 0.999f) app_occluder(cx + 24, cy + 24, CW - 48, M.h - 48); /* still and opaque */
  float W = CW - 2 * PX, x0 = cx + PX, lx = rtl ? x0 + W : x0; int al = rtl ? ALIGN_RIGHT : ALIGN_LEFT;
  float y = cy + 40; text_draw(M.kind, lx, y + (30 - text_line_height(24)) / 2, 24, 600, WHITE_A(184), 0, W, al); y += 30 + 6;
  text_draw_wrapped(M.it.name, lx, y + (46 - text_line_height(38)) / 2, W, 2, 38, 700, WHITE_A(255), -0.5f, al, 46); y += M.titleLines * 46;
  if (M.sub[0]) { y += 4; text_draw(M.sub, lx, y, 24, 400, WHITE_A(199), 0, W, al); y += text_line_height(24); }
  y += 26;
  gfx_offset_push(x0 + nudge_x(&M.nudge), y + nudge_y(&M.nudge)); if (M.open) fpill_halo(&M.pill, 16, 0.97f, acc_a(0.34f)); fpill_draw(&M.pill, 16, 0.97f); gfx_offset_pop();
  float since = (float)(app_now() - M.openAt);
  for (int i = 0; i < M.n; i++) { int f = i == M.idx; float iy = y + i * (IH + 4), nx = f ? nudge_x(&M.nudge) : 0, ny = f ? nudge_y(&M.nudge) : 0;
    /* the options rise in one after another as the card opens */
    float d = i == 0 ? 0 : (i < 5 ? i : 4) * 0.025f, k = ease_quint((since - d) / 0.22f), ia = M.open ? k : 1, iy2 = M.open ? 10 * (1 - k) : 0;
    if (M.closing) { float o = 1 - M.out.v; iy2 = 6 * o * (1 + 0.1f * (M.n - 1 - i)); } /* leaving, they sink, the last ones furthest */
    if (M.open) hit_add(x0, iy, W, IH, item_focus, item_act, i, 0);
    if (ia < 1) gfx_opacity_push(ia);
    float ix = rtl ? x0 + W - 20 - 30 + nx : x0 + 20 + nx, ty = iy + iy2 + ny;
    icon_draw(M.icons[i], ix, ty + (IH - 30) / 2, 30, f ? ACC_FG : WHITE_A(176));

    text_draw(M.labels[i], rtl ? ix - 20 : ix + 30 + 20, ty + (IH - text_line_height(27)) / 2, 27, 600, f ? ACC_FG : WHITE_A(217), 0, W - 90, al);
    if (ia < 1) gfx_opacity_pop(); }
  gfx_scale_pop(); gfx_offset_pop(); gfx_opacity_pop();
}
static Screen menu = { NULL, NULL, key, tick, draw, NULL, NULL, NULL };
/* the same chip for an action taken elsewhere (the title page's Library button): added flies to the Library tab,
   removed comes down from it, from the focused element */
void cwmenu_fx_lib(int added, const Item *it) { app_overlay_keep(&menu); fx_start(added ? 1 : 2, added ? "plus" : "minus", T(added ? "Added to Library" : "Removed from Library")); if (it) sb_poster_of(it, FX.poster, sizeof FX.poster); }
void cwmenu_show(const Item *it) {
  float fx, fy, fw, fh, fr; int held = hold_trace_rect(&fx, &fy, &fw, &fh, &fr);
  if (it->ep) { revpanel_show(it, 0, held ? fx : 0, held ? fy : 0, held ? fw : 0, held ? fh : 0, held ? fr : 0); return; } /* an episode held: its reviews, with the menu's options among them */
  M.from[0] = held ? fx : 0; M.from[1] = held ? fy : 0; M.from[2] = held ? fw : 0; M.from[3] = held ? fh : 0; M.from[4] = held ? fr : 0;
  float cur = M.closing ? M.out.v : M.open ? M.op.v : 0;
  M.it = *it; M.open = 1; M.closing = 0; M.idx = 0; M.n = 0; app_overlay = &menu;
  const char *kl = (!strcasecmp(it->type, "anime") || !strcasecmp(it->type, "Anime Movies") || !strcasecmp(it->type, "Anime Series")) ? "Anime" : !strcmp(it->type, "series") ? "Series" : "Movie";
  snprintf(M.kind, sizeof M.kind, "%s", T(kl));
  M.sub[0] = 0; int se = -1, ep = -1, vr = sb_parse_video_id(it->id, it->videoId, &se, &ep);
  if (vr == 1) { char s[12], e[12]; snprintf(s, sizeof s, "%d", se); snprintf(e, sizeof e, "%d", ep); snprintf(M.sub, sizeof M.sub, "%s", T2("Season {s} · Episode {e}", "s", s, "e", e)); }
  else if (vr == 2) { char e[12]; snprintf(e, sizeof e, "%d", ep); snprintf(M.sub, sizeof M.sub, "%s", T1("Episode {n}", "n", e)); }
  if (vr > 0 && it->epName[0]) { size_t l = strlen(M.sub); snprintf(M.sub + l, sizeof M.sub - l, "  ·  %s", it->epName); }
  if (it->ep) add(A_STREAMS, "play", T("Streams")); else add(A_PLAY, "play", T(it->cw ? "Resume" : "Play"));
  Uint32 tm0 = SDL_GetTicks(); CwInfo ci; if (lib_cw_info(it, &ci) && (!it->ep || !strcmp(ci.vid, it->videoId))) add(A_RESTART, "restart", T("Watch from the beginning"));
  if (!it->ep) add(A_DETAILS, "info", T("Details"));
  int isEpSeries = !strcmp(it->type, "series") || !strcasecmp(it->type, "Anime Series") || (!strcasecmp(it->type, "anime") && vr > 0);
  int canMark = !isEpSeries || vr > 0;
  if (canMark) { int w = lib_is_marked_watched(it); add(A_WATCHED, w ? "eyeOff" : "eye", T(w ? "Mark as unwatched" : "Mark as watched")); }
  if (trailer_available(sb_meta_cached(it->type, it->id), it->id)) add(A_TRAILER, "trailer", T("Trailer"));
  if (!it->ep) add(A_SIMILAR, "similar", T("Watch something similar"));
  if (!it->ep && sb_is_imdb(it->id)) add(A_REVIEWS, "reviews", T("Reviews"));
  int inL = lib_has(it->id); add(A_LIB, inL ? "minus" : "plus", T(inL ? "Remove from Library" : "Add to Library"));
  if (getenv("SB_SLOWLOG")) fprintf(stderr, "[menu] options in %u ms\n", SDL_GetTicks() - tm0);
  M.titleLines = (float)text_wrap_count(it->name, CW - 2 * PX, 2, 38, 700, -0.5f); if (M.titleLines < 1) M.titleLines = 1;
  M.h = items_y() + M.n * (IH + 4) + 30; /* as tall as its options */
  /* from closed: it slides in once with its glass baked (the card's own part of the page); reopened mid-close: it turns back */
  if (cur < 0.01f) { tween_set(&M.op, 0); tween_set(&M.x, 40); tween_set(&M.grow, 0); fpill_snap(&M.pill); Uint32 b0 = SDL_GetTicks(); sglass_bake(&M.glass, card_x(), SB_H / 2 - M.h / 2, CW, M.h); M.openAt = app_now() + (SDL_GetTicks() - b0) / 1000.0 + 0.016; } /* the options start once the glass is made */
  else { tween_set(&M.op, cur); M.openAt = app_now() - 1; }
  tween_to(&M.op, 1, 0.2f, EASE_CSS_EASEOUT); tween_to(&M.x, 0, 0.26f, EASE_CSS_EASEOUT); tween_to(&M.grow, 1, 0.26f, EASE_CSS_EASEOUT); /* slides and grows into place, softly */
}
