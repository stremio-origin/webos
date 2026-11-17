/* Sign in: a code to scan with the phone on the left, email and password on the right. */
#include "ui/xline.h"
#include "screens/login.h"
#include "screens/splash.h"
#include "core/keys.h"
#include "core/i18n.h"
#include "core/net.h"
#include "core/store.h"
#include "core/json.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include "ui/qr.h"
#include "ui/img.h"
#include "ui/glass.h"
#include "ui/widgets.h"
#include "ui/osk.h"
#include "platform/plat.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include "core/hit.h"

static struct {
  int zone;                 /* 0 email, 1 password, 2 button */
  int editing;              /* OK on a field: typing goes in */
  char email[128], pass[128], err[256];
  char code[16], link[256], stat[128]; int qrOk, started, renewing; double pollAt, codeAt; unsigned pollReq;
  int revealed; double revealAt; Timer1 in;
  int done; Timer1 doneT; int left; double doneAt; int restarting; Timer1 restart;
  Nudge nudge; double t0;
  Osk osk;
} L;

static void reveal(void) { if (L.revealed) return; L.revealed = 1; timer_start(&L.in, 0.79f); }
static void renew(const char *why);
static void on_read(int status, const unsigned char *body, size_t len, void *ud) {
  (void)len; (void)ud; L.pollReq = 0; if (L.done) return;
  if (status == 404 || status == 410 || status == 400) { renew("the code expired"); return; } /* (the code gone: a new one) */
  if (status != 200) return;
  cJSON *j = net_json(body); if (!j) return;
  if (js_obj(j, "error") || js_str(j, "error", NULL)) { cJSON_Delete(j); return; } /* (not signed in on the phone yet: link.stremio.com answers {"error":{"code":101,"message":"Invalid or expired token"}} for a fresh, waiting code too, so it is no sign of an expiry; renewing on it made a new code every 3 s. The code is renewed at 4 minutes, or when the server answers 404/410/400) */
  const cJSON *r = js_obj(j, "result"); const char *ak = js_str(r, "authKey", NULL); if (!ak) ak = js_str(r, "auth_key", NULL);
  if (ak && *ak) { char k[256]; snprintf(k, sizeof k, "%s", ak); cJSON_Delete(j); extern void login_finish(const char *); login_finish(k); return; }
  cJSON_Delete(j);
}
static void on_create(int status, const unsigned char *body, size_t len, void *ud) {
  (void)len; (void)ud; cJSON *j = status == 200 ? net_json(body) : NULL;
  const char *code = js_str(j, "code", NULL), *link = js_str(j, "link", NULL);
  if (!code || !*code) { if (L.renewing) { L.renewing = 0; L.codeAt = app_now() - 200; cJSON_Delete(j); return; } /* (a new code not had: the shown one stays, asked again in a minute) */ snprintf(L.stat, sizeof L.stat, "%s", T("QR unavailable — use email")); reveal(); cJSON_Delete(j); return; }
  L.renewing = 0; L.codeAt = app_now(); snprintf(L.code, sizeof L.code, "%s", code); snprintf(L.link, sizeof L.link, "%s", link ? link : ""); if (!L.link[0]) snprintf(L.link, sizeof L.link, "https://link.stremio.com/%s", code);
  L.stat[0] = 0; L.qrOk = 1; /* (no status under the code: it simply works) */ L.pollAt = app_now() + 2.5; reveal(); /* everything comes in once the code is drawn */
  cJSON_Delete(j);
}
/* a code lasts a few minutes: a new one before it expires (4 min), or at once when the server says it is over; the
   shown one stays until the new one is here, so the code never blinks out */
static void renew(const char *why) { if (L.renewing || L.done) return; L.renewing = 1; fprintf(stderr, "[login] a new sign-in code (%s)\n", why); net_get("https://link.stremio.com/api/create", on_create, NULL); }
static void start_qr(void) { if (L.started) return; L.started = 1; L.stat[0] = 0; net_get("https://link.stremio.com/api/create", on_create, NULL); }
static void step(void) { if (--L.left <= 0 && !L.restarting) { L.restarting = 1; timer_start(&L.restart, 0.5f); } }
static void on_user(int ok, const char *err, void *ud) { (void)ok; (void)err; (void)ud; step(); }
static void on_addons(int changed, void *ud) { (void)changed; (void)ud; step(); }
static void on_lib(int changed, void *ud) { (void)changed; (void)ud; step(); }
/* signed in: the two halves step back, a slim line runs while the account comes down (library, addons), then
   the start screen fades in over it and Home is revealed from there like at any start: complete, never in pieces */
void login_finish(const char *ak) {
  if (L.done) return; L.done = 1; L.editing = 0; SDL_StopTextInput(); timer_start(&L.doneT, 0.6f); L.doneAt = app_now(); L.left = 3;
  sb_finish_login(ak, on_user, NULL); sb_sync_addons(on_addons, NULL); sb_pull_library(on_lib, NULL);
}
static void on_login(int ok, const char *err, void *ud) { (void)ud;
  if (!ok) { snprintf(L.err, sizeof L.err, "%s", err && *err ? err : T("Sign-in failed")); return; }
  L.err[0] = 0; /* sb_login stored the key and the user */ login_finish(sb_auth_key());
}
static void do_email(void) {
  if (!L.email[0] || !L.pass[0]) { snprintf(L.err, sizeof L.err, "%s", T("Enter email and password")); return; }
  L.err[0] = 0; toast(T("Signing in…"), 1500, NULL); sb_login(L.email, L.pass, on_login, NULL);
}
static void enter(const void *p) { (void)p;
  L.zone = 0; L.editing = 0; L.t0 = app_now(); if (!L.revealed) L.revealAt = app_now() + 1.6; /* the halves wait for the QR code (1.6 s at most), then rise in together */
  start_qr();
}
static void leave(void) { if (L.pollReq) net_cancel(L.pollReq); L.pollReq = 0; SDL_StopTextInput(); L.editing = 0; }
static int key(int k) {
  if (L.done) return 1;
  if (L.editing && L.osk.open) { char *f = L.zone ? L.pass : L.email; int r = osk_key(&L.osk, k, f, 128);
    if (r == 2) { osk_close(&L.osk); L.editing = 0; L.zone = L.zone == 0 ? 1 : 2; } else if (!r && (k == K_UP)) { osk_close(&L.osk); L.editing = 0; } return 1; }
  if (L.editing) {
    if (k == K_BACK) { char *f = L.zone ? L.pass : L.email; size_t n = strlen(f); if (n) { n--; while (n && (f[n] & 0xc0) == 0x80) n--; f[n] = 0; } else { L.editing = 0; SDL_StopTextInput(); } return 1; }
    if (k == K_OK || k == K_DOWN) { L.editing = 0; SDL_StopTextInput(); L.zone = L.zone == 0 ? 1 : 2; return 1; }
    if (k == K_UP) { L.editing = 0; SDL_StopTextInput(); return 1; }
    return 1;
  }
  if (k == K_BACK) { app_quit(); return 1; } /* nowhere to go back to: Back closes the app */
  if ((k == K_DOWN && L.zone == 2) || (k == K_UP && L.zone == 0) || k == K_LEFT || k == K_RIGHT) { nudge_start(&L.nudge, k); return 1; }
  if (k == K_DOWN) L.zone = L.zone < 2 ? L.zone + 1 : 2;
  else if (k == K_UP) L.zone = L.zone > 0 ? L.zone - 1 : 0;
  else if (k == K_OK) { if (L.zone < 2) { L.editing = 1; if (plat_text_input()) SDL_StartTextInput(); else osk_open(&L.osk); } else do_email(); }
  return 1;
}
static void text(const char *s) { if (!L.editing || L.done) return; char *f = L.zone ? L.pass : L.email; if (strlen(f) + strlen(s) < 127) strcat(f, s); }
static void tick(float dt) {
  timer_tick(&L.in, dt); nudge_tick(&L.nudge, dt); osk_tick(&L.osk, dt);
  if (!L.revealed && app_now() >= L.revealAt) reveal();
  if (L.qrOk && !L.done && !L.renewing && app_now() - L.codeAt > 240) renew("4 minutes old");
  if (L.qrOk && !L.done && !L.pollReq && app_now() >= L.pollAt) { char u[320]; snprintf(u, sizeof u, "https://link.stremio.com/api/read?code=%s", L.code); L.pollReq = net_get(u, on_read, NULL); L.pollAt = app_now() + 2.5; }
  if (L.done) { timer_tick(&L.doneT, dt);
    if (!L.restarting && app_now() - L.doneAt > 9) { L.restarting = 1; timer_start(&L.restart, 0.5f); } /* a slow network never keeps it here */
    if (L.restarting && timer_tick(&L.restart, dt) >= 1 && !L.restart.on) { /* the app restarts behind the start screen */
      L.restart.on = 0; L.restarting = 2; app_splash_done = 0; splash_start(1, L.doneAt); topNav.idx = NAV_HOME; app_go(SCR_HOME, NULL, 1); memset(&L, 0, sizeof L); } }
}
/* the glow: two big soft discs wandering slowly (as rings of falling alpha: no radial shader needed) */
static void blob(float cx, float cy, uint32_t c, float peak) {
  float R = 550; gfx_alpha_tex(gfx_radial_tex(), cx - R, cy - R, 2 * R, 2 * R, col_alpha(c, peak * 0.55f)); /* a soft disc from a small texture: one cheap quad */
}
static void zone_focus(int z, int b) { (void)b; if (L.zone != z) { if (L.editing) { L.editing = 0; SDL_StopTextInput(); osk_close(&L.osk); } L.zone = z; } }
static double glowT0 = -1; /* the glows' own clock: kept past signing in, the start screen carries them on */
void login_glows_draw(void) {
  if (glowT0 < 0) glowT0 = app_now(); float t = (float)(app_now() - glowT0);
  float a = fmodf(t / 22, 2), b = fmodf(t / 27, 2); a = a > 1 ? 2 - a : a; b = b > 1 ? 2 - b : b; a = ease_apply(EASE_IN_OUT, a); b = ease_apply(EASE_IN_OUT, b);
  if (!getenv("SB_NOGLASS")) { blob(-380 + 550 + 760 * a, -420 + 550 + 300 * a, RGB(140, 92, 255), 0.55f);
  blob(SB_W + 420 - 550 - 820 * b, SB_H + 480 - 550 - 260 * b, RGB(96, 140, 255), 0.42f); }
}
/* the glows, small, for the glass over them (made in the tick each frame: they move; never while drawing, no target is nested) */
static GfxTarget glowT; static int glowOk = -1, glowFresh;
void login_glow_bake(void) {
  if (glowOk < 0) glowOk = gfx_target_create(&glowT, 384, 216);
  if (!glowOk) return; { extern unsigned app_frame(void); if (glowFresh && app_frame() % 4) return; } /* (every fourth frame: they drift slowly) */
  gfx_target_begin(&glowT, PAGE | 0xff000000u); login_backdrop_draw(); gfx_target_end(); glowFresh = 1;
}
GLuint login_glow_tex(void) { return glowOk > 0 && glowFresh ? glowT.tex : 0; }
/* the liquid glass over the glows (the bar's), and the accent's glass lit over it when focused */
static void lpill(float x, float y, float w, float h, float r, int foc, int seed) {
  lglass_liquid(login_glow_tex(), x, y, w, h, r, x / SB_W, 1 - y / SB_H, (x + w) / SB_W, 1 - (y + h) / SB_H, foc ? 1.15f : 0.85f, 0, seed, w < 300);
  if (foc) { gfx_rrect_v(x, y, w, h, r, col_alpha(ACC2 | 0xff000000u, 0.46f), col_alpha(ACC | 0xff000000u, 0.36f)); gfx_rrect_v(x, y, w, h, r, WHITE_A(40), WHITE_A(0)); gfx_ring(x, y, w, h, r, 1.2f, WHITE_A(70)); }
  lglass_light(0);
}
void login_backdrop_draw(void) {
  static char url[600]; if (!url[0]) snprintf(url, sizeof url, "file://%s/splashbg.jpg", plat_asset_dir());
  gfx_rect(0, 0, SB_W, SB_H, PAGE | 0xff000000u); float asp; GLuint t = img_get(url, 1920, 1080, &asp);
  if (t) { float u0, v0, u1, v1; img_cover_uv(asp, SB_W / SB_H, 0, &u0, &v0, &u1, &v1); gfx_image(t, 0, 0, SB_W, SB_H, 0, WHITE_A(255), u0, v0, u1, v1); }
  login_glows_draw();
}
static void draw(void) {
  login_glows_draw();
  int rtl = app_rtl();
  /* the box: QR half | divider | form half, centred */
  float h2 = text_line_height(40), qrH = h2 + 10 + text_line_height(24) + 26 + 300 + 20 + text_line_height(46) + (L.stat[0] ? 14 + text_line_height(24) : 0);
  float lin = 18 * 2 + text_line_height(28), fH = h2 + 22 + lin + 16 + lin + 16 + 10 + (16 * 2 + text_line_height(29)) + 16 + 28;
  float qrW = 300, fW = 520; /* the code's half is as wide as its widest line (the web's flex box), then the box is centred */
  { const char *ls[3] = { T("Sign in with your phone"), T("Scan the code with your camera"), L.stat }; const int px[3] = { 40, 24, 24 }, wt[3] = { 700, 400, 400 };
    for (int i = 0; i < 3; i++) { float w = text_width(ls[i], px[i], wt[i], 0); if (w > qrW) qrW = w; } float cw = text_width(L.code, 46, 800, 9) + 9; if (cw > qrW) qrW = cw; }
  float boxW = qrW + 90 + 1 + 90 + fW, boxH = qrH > fH ? qrH : fH, bx = SB_W / 2 - boxW / 2, by = SB_H / 2 - boxH / 2;
  by -= L.osk.a.v * (osk_height() + 30) / 2; /* the keyboard below: the box moves up to share the screen */
  float doneK = L.done ? ease_apply(EASE_OUT, timer_x(&L.doneT) * 0.6f / 0.45f > 1 ? 1 : timer_x(&L.doneT) * 0.6f / 0.45f) : 0;
  float boxA = L.done ? 1 - (timer_x(&L.doneT) * 0.6f / 0.35f > 1 ? 1 : timer_x(&L.doneT) * 0.6f / 0.35f) : 1;
  if (boxA > 0.001f) {
    gfx_opacity_push(boxA); gfx_scale_push(1 - 0.04f * doneK, 1 - 0.04f * doneK, SB_W / 2, SB_H / 2);
    float inX = L.in.on ? timer_x(&L.in) : (L.revealed ? 1 : 0);
    float k1 = L.revealed ? ease_apply(EASE_OUT, inX * 0.79f / 0.7f > 1 ? 1 : inX * 0.79f / 0.7f) : 0, k2 = L.revealed ? ease_apply(EASE_OUT, (inX * 0.79f - 0.09f) / 0.7f < 0 ? 0 : (inX * 0.79f - 0.09f) / 0.7f) : 0;
    /* QR half */
    float qx = rtl ? bx + boxW - qrW : bx, qcx = qx + qrW / 2, y = by + (boxH - qrH) / 2;
    gfx_opacity_push(k1 < 0.004f ? 0.004f : k1); gfx_offset_push(0, 18 * (1 - k1));
    text_draw(T("Sign in with your phone"), qcx, y, 40, 700, WHITE_A(255), 0, qrW, ALIGN_CENTER); y += h2 + 10;
    text_draw(T("Scan the code with your camera"), qcx, y, 24, 400, WHITE_A(191), 0, qrW, ALIGN_CENTER); y += text_line_height(24) + 26;
    if (L.qrOk) qr_draw(L.link, qcx - 150, y, 300, 18, 12); else gfx_rrect(qcx - 150, y, 300, 300, 18, WHITE_A(255)); y += 300 + 20;
    text_draw(L.code, qcx + 4.5f, y, 46, 800, WHITE_A(255), 9, 0, ALIGN_CENTER); y += text_line_height(46) + 14;
    text_draw(L.stat, qcx, y, 24, 400, WHITE_A(191), 0, qrW, ALIGN_CENTER);
    gfx_offset_pop(); gfx_opacity_pop();
    /* the divider */
    gfx_rect(rtl ? bx + fW + 90 : bx + qrW + 90, by, 1, boxH, WHITE_A((int)(36 * k1)));
    /* the form */
    float fx = rtl ? bx : bx + qrW + 90 + 1 + 90; y = by + (boxH - fH) / 2;
    gfx_opacity_push(k2 < 0.004f ? 0.004f : k2); gfx_offset_push(0, 18 * (1 - k2));
    text_draw(T("Or use email"), rtl ? fx + fW : fx, y, 40, 700, WHITE_A(255), 0, fW, rtl ? ALIGN_RIGHT : ALIGN_LEFT); y += h2 + 22;
    for (int f = 0; f < 2; f++) { int foc = L.zone == f; const char *v = f ? L.pass : L.email; char shown[256]; if (f) { size_t n = 0; for (const char *p = v; *p; p++) if ((*p & 0xc0) != 0x80) n++; for (size_t i = 0; i < n && i < 120; i++) { shown[i * 3] = (char)0xe2; shown[i * 3 + 1] = (char)0x80; shown[i * 3 + 2] = (char)0xa2; } shown[(n < 120 ? n : 120) * 3] = 0; } else snprintf(shown, sizeof shown, "%s", v);
      float nx = foc ? nudge_x(&L.nudge) : 0, ny = foc ? nudge_y(&L.nudge) : 0;
      hit_add(fx, y, fW, lin, zone_focus, NULL, f, 0);
      lpill(fx + nx, y + ny, fW, lin, lin / 2, foc, 4 + f);
      if (shown[0]) text_draw(shown, rtl ? fx + fW - 26 + nx : fx + 26 + nx, y + 18 + ny, 28, 400, WHITE_A(255), 0, fW - 52, rtl ? ALIGN_RIGHT : ALIGN_LEFT);
      else text_draw(T(f ? "Password" : "Email"), rtl ? fx + fW - 26 + nx : fx + 26 + nx, y + 18 + ny, 28, 400, WHITE_A(133), 0, fW - 52, rtl ? ALIGN_RIGHT : ALIGN_LEFT);
      if (foc && L.editing && ((int)(app_now() * 2) & 1)) { float cw = text_width(shown, 28, 400, 0); gfx_rect((rtl ? fx + fW - 26 - cw - 3 : fx + 26 + cw + 2) + nx, y + 18 + 2 + ny, 3, text_line_height(28) - 4, WHITE_A(255)); }
      y += lin + 16; }
    y += 10; { int foc = L.zone == 2; const char *lbl = T("Sign in"); float bw = text_width(lbl, 29, 700, 0.2f) + 92, bh = 16 * 2 + text_line_height(29), bxx = fx + fW / 2 - bw / 2;
      float nx = foc ? nudge_x(&L.nudge) : 0, ny = foc ? nudge_y(&L.nudge) : 0;
      hit_add(bxx, y, bw, bh, zone_focus, NULL, 2, 0);
      lpill(bxx + nx, y + ny, bw, bh, bh / 2, foc, 6);
      text_draw(lbl, bxx + bw / 2 + nx, y + 16 + ny, 29, 700, WHITE_A(255), 0.2f, 0, ALIGN_CENTER); y += bh + 16; }
    if (L.err[0]) text_draw(L.err, rtl ? fx + fW : fx, y, 24, 400,
 RGB(0xff, 0x9a, 0x9a), 0, fW, rtl ? ALIGN_RIGHT : ALIGN_LEFT);
    if (L.osk.a.v > 0.001f) { float kw = 10 * 76 + 9 * 8; osk_draw(&L.osk, fx + fW / 2 - kw / 2 + (L.osk.a.v < 1 ? 0 : 0), by + boxH + 30, kw); } /* the TV's keyboard under the fields */
    gfx_offset_pop(); gfx_opacity_pop();
    gfx_scale_pop(); gfx_opacity_pop();
  }
  if (L.done) { /* "Signed in! Loading…" with the running line */
    float x = timer_x(&L.doneT) * 0.6f; float da = x < 0.2f ? 0 : (x - 0.2f) / 0.4f > 1 ? 1 : (x - 0.2f) / 0.4f;
    login_loading_draw(L.doneAt, da);
  }
}
void login_loading_draw(double since, float a) {
  if (a <= 0.004f) return; gfx_opacity_push(a);
  { static XLine xl; static WlSlot ws; /* a light line over it while a big library comes in (core/waitlines): the same place, nothing moves */
    xline_draw_w(&xl, xline_wait(&ws, (unsigned)(since * 1000), T("Signed in! Loading…"), 1, WL_SIGNIN, WL_ACT_NONE, 0), SB_W / 2, SB_H / 2 - 44, 32, 700, 0, WHITE_A(255), 0, ALIGN_CENTER); }
  float ly = SB_H / 2 - 44 + text_line_height(32) + 28, lx = SB_W / 2 - 190; gfx_rrect(lx, ly, 380, 4, 2, WHITE_A(31));
  gfx_clip_push(lx, ly, 380, 4); float ph = fmodf((float)(app_now() - since) / 1.25f, 1), p = ease_apply(EASE_IN_OUT, ph), sx = lx + 380 * 0.42f * (-1 + 3.4f * p);
  gfx_rrect_h(sx, ly, 380 * 0.21f, 4, 2, col_alpha(ACC2, 0), ACC2); gfx_rrect_h(sx + 380 * 0.21f, ly, 380 * 0.21f, 4, 2, ACC2, col_alpha(ACC2, 0)); gfx_clip_pop();
  gfx_opacity_pop();
}
Screen loginScreen = { enter, leave, key, tick, draw, NULL, NULL, text };
