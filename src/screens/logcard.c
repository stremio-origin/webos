/* Share logs: a card in the look of the others (the sheets' glass) with the 4-character code that finds the sealed
   logs and a code to scan that opens a new issue for this build with the log code already in it: the viewer says
   what happened and sends it. A running line while the logs are sealed and sent, the reason when that failed. Any
   key or click closes it (the remote's OK drawn at its foot). It rises in and settles (0.3 s). */
#include "screens/logcard.h"
#include "core/app.h"
#include "core/applog.h"
#include "core/i18n.h"
#include "core/keys.h"
#include "core/hit.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include "ui/glass.h"
#include "ui/qr.h"
#include "ui/widgets.h"
#include "core/update.h"
#include "platform/plat.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static struct { int open, started, state; double at, doneAt, outAt; char out[16], err[200], url[600]; } C; static SGlass glass;
/* where this build's issues go, and what it says it is: each platform's own repo (an Android TV report filed on the
   webOS repo as "webOS" was the wrong place) */
static const char *issues_url(void) { return strstr(plat_device_name(), "Android") ? "https://github.com/stremio-origin/androidtv/issues/new" : "https://github.com/stremio-origin/webos/issues/new"; }
static const char *plat_label(void) { return strstr(plat_device_name(), "Android") ? "Android%20TV" : "webOS"; }
#define CW 1040.0f
#define CH 470.0f
static void close_card(void) { if (!C.open) return; C.open = 0; C.outAt = app_now(); }
static int key(int k) {
  if (!C.open) { app_overlay = NULL; app_key(app_mirror_key(k), 1, 0); return 1; } /* on its way out: the key is the page's */
  (void)k; close_card(); return 1; /* any key (Back too) closes it */
}
static void tick(float dt) {
  sglass_tick(&glass, dt);
  if (!C.open) { if (app_now() - C.outAt >= 0.32 && app_overlay == &logCard) app_overlay = NULL; return; }
  if (!C.started && app_now() - C.at >= 0.6) { C.started = 1; applog_share_start(); } /* the latest lines reach the kept log first */
  if (C.started && !C.state) {
    char o[64]; int st = applog_share_state(o, sizeof o);
    if (st == 1 && strlen(o) == 4) { C.state = 1; snprintf(C.out, sizeof C.out, "%s", o); C.doneAt = app_now();
      /* the issue, prefilled: its title, the log code and the build in the text */
      snprintf(C.url, sizeof C.url, "%s?title=%s&body=Log%%20code%%3A%%20%s%%0AVersion%%3A%%20%s%%20(%s)%%0A%%0AWhat%%20happened%%3A%%20", issues_url(), "Issue%20from%20the%20TV", o, update_current(), plat_label()); }
    else if (st == -1 || (st == 0 && app_now() - C.at > 40.6)) { C.state = -1; C.doneAt = app_now();
      snprintf(C.err, sizeof C.err, "%s", T(st == -1 && (!strcmp(o, "net") || !strcmp(o, "timeout")) ? "No internet connection" : "Couldn’t share the logs. Try again in a moment")); }
  }
}
static void on_click(int a, int b) { (void)a; (void)b; close_card(); }
static float ease_q(float x) { return x <= 0 ? 0 : x >= 1 ? 1 : 1 - powf(1 - x, 4); } /* ~ cubic-bezier(.23,1,.32,1) */
static void draw(void) {
  float op, dy, sc;
  if (C.open) { float k = ease_q((float)(app_now() - C.at) / 0.3f); op = k; dy = 18 * (1 - k); sc = 0.97f + 0.03f * k; }
  else { float t = (float)(app_now() - C.outAt), k = ease_q(t / 0.22f), o = t / 0.18f; o = o > 1 ? 1 : o; op = 1 - (o < 0.5f ? 2 * o * o : 1 - powf(-2 * o + 2, 2) / 2); dy = 14 * k; sc = 1 - 0.03f * k; }
  if (op <= 0.001f) return;
  if (C.open) hit_act(0, 0, SB_W, SB_H, on_click, 0, 0); /* a click anywhere closes it */
  int rtl = app_rtl(); int al = rtl ? ALIGN_RIGHT : ALIGN_LEFT;
  float x = SB_W / 2 - CW / 2, y = SB_H / 2 - CH / 2, P = 56, QS = 280, cx = SB_W / 2;
  float tx = rtl ? x + CW - P : x + P, tw = CW - 3 * P - QS, qx = rtl ? x + P : x + CW - P - QS, qy = y + (CH - QS) / 2 - 10;
  gfx_opacity_push(op); gfx_offset_push(0, dy); gfx_scale_push(sc, sc, cx, y + CH / 2);
  if (glass.ok && glass.a.v >= 0.999f) gfx_shadow_under(x, y + 30, CW, CH, 32, 80, BLACK_A(150), x, y, CW, CH, 32); else gfx_shadow(x, y + 30, CW, CH, 32, 80, BLACK_A(150)); sglass_draw(&glass, x, y, CW, CH, 32); gfx_ring(x, y, CW, CH, 32, 1, WHITE_A(22));
  float ty = y + P;
  text_draw(T("Share logs"), tx, ty, 40, 700, WHITE_A(255), -0.5f, tw, al); ty += text_line_height(40) + 14;
  const char *p = T("Scan the code and describe the issue.");
  float plh = 26 * 1.4f; if (C.state != -1) { int pn = text_wrap_count(p, tw, 4, 26, 400, 0); text_draw_wrapped(p, tx, ty, tw, 4, 26, 400, WHITE_A(210), 0, al, plh); ty += pn * plh + 26; } /* (failed: there is no code to scan, only the reason) */
  if (C.state == 1) { /* the code rises in; its letters stand apart, always left to right */
    float t = (float)(app_now() - C.doneAt), k = ease_q(t / 0.3f), w = text_width(C.out, 88, 700, 18);
    text_draw(T("Log code"), tx, ty, 22, 600, WHITE_A(150), 0.6f, tw, al);
    gfx_opacity_push(k); text_draw(C.out, rtl ? tx - w : tx, ty + text_line_height(22) + 4 + 8 * (1 - k), 88, 700, WHITE_A(255), 18, 0, ALIGN_LEFT); gfx_opacity_pop();
    gfx_opacity_push(k); qr_draw(C.url, qx, qy + 10 * (1 - k), QS, 28, 26); gfx_opacity_pop();
  } else if (C.state == -1) {
    text_draw_wrapped(C.err, tx, ty, tw, 3, 26, 500, WHITE_A(220), 0, al, plh);
  } else { /* the running line: a light that crosses a thin track; the code's place waits, quiet */
    float sx = tx - (rtl ? 220 : 0), sy = ty + 40; gfx_rrect(sx, sy, 220, 3, 1.5f, WHITE_A(31));
    float ph = (float)(app_now() - C.at) / 1.1f; ph -= (int)ph; ph = ph < 0.5f ? 2 * ph * ph : 1 - powf(-2 * ph + 2, 2) / 2;
    float lx = sx - 88 + ph * (88 + 220); gfx_clip_push(sx, sy, 220, 3);
    gfx_rrect_h(lx, sy, 44, 3, 0, col_alpha(ACC2, 0), ACC2); gfx_rrect_h(lx + 44, sy, 44, 3, 0, ACC2, col_alpha(ACC2, 0)); gfx_clip_pop();
    gfx_rrect(qx, qy, QS, QS, 28, WHITE_A(18)); gfx_ring(qx, qy, QS, QS, 28, 1, WHITE_A(26));
  }
  key_hint(rtl ? x + CW - P : x + P, y + CH - P + 6, K_OK, T("Close"), WHITE_A(200), rtl); /* the way out: the remote's key */
  gfx_scale_pop(); gfx_offset_pop(); gfx_opacity_pop();
}
Screen logCard = { NULL, NULL, key, tick, draw, NULL, NULL, NULL };
void logcard_show(void) {
  C.out[0] = 0; C.err[0] = 0; C.url[0] = 0; C.state = 0; C.started = 0; C.at = app_now(); C.open = 1;
  sglass_bake(&glass, SB_W / 2 - CW / 2, SB_H / 2 - CH / 2, CW, CH);
  app_overlay = &logCard;
}
