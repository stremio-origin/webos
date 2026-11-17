#include "ui/qrcard.h"
#include "ui/widgets.h"
#include "core/keys.h"
#include "ui/qr.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include "ui/icon.h"
#include "ui/glass.h"
#include "core/app.h"
#include "core/hit.h"
#include "core/i18n.h"
#include "core/httpd.h"
#include "core/tmdb.h"
#include "core/trakt.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define PAD_X 48.0f
#define CW (680.0f + 2 * PAD_X)  /* 680 of content inside its padding */
#define PAD_T 48.0f
#define PAD_B 42.0f
#define POLL 1.5
#define BADGE 76.0f    /* a service's mark */
#define CODE_PX 64     /* a code to type */
static Screen *under; /* the overlay it opened over (the streams sheet) */
static struct {
  int open, closing, done; QrCardOpts o; char title[256], sub[400], url[600], shown[600], doneTitle[200], doneSub[300];
  Timer1 in, out, tick; double nextPoll, closeAt; float h, subLines, doneSubLines;
} Q;

static Screen card;
static SGlass glass;   /* the page behind the card, baked once per card */
static float quint(float x) { return 1 - powf(1 - x, 5); } /* cubic-bezier(.23,1,.32,1) */
static float p_lh(void) { return 24 * 1.45f; }
static void layout(void) { /* heights once per state: never measured per frame */
  float w = CW - 2 * PAD_X;
  Q.subLines = Q.sub[0] ? (float)text_wrap_count(Q.sub, w, 6, 24, 400, 0) : 0;
  Q.doneSubLines = Q.doneSub[0] ? (float)text_wrap_count(Q.doneSub, w, 6, 24, 400, 0) : 0;
  float h = PAD_T;
  if (Q.done) { h += 96 + 22 + text_line_height(38); if (Q.doneSub[0]) h += 12 + Q.doneSubLines * p_lh() + 28; }
  else if (Q.o.code) h += (Q.o.badge ? BADGE + 26 : 0) + text_line_height(CODE_PX) + 10 + Q.subLines * p_lh() + 26 + 280 + 18 + text_line_height(22); /* the mark, the code, one line, the QR, the way out */
  else h += text_line_height(38) + 12 + Q.subLines * p_lh() + 28 + 280 + 18 + text_line_height(23) + 18 + text_line_height(22);
  Q.h = h + PAD_B;
}
static void finish(void) {
  Q.open = 0; Q.closing = 0; if (app_overlay == &card) app_overlay = under; under = NULL; /* a sheet it was opened from has the keys again */
  void (*cb)(void *) = Q.o.onClose; void *ud = Q.o.ud; Q.o.onClose = NULL; if (cb) cb(ud);
}
void qrcard_close(void) { if (!Q.open || Q.closing) return; Q.closing = 1; timer_start(&Q.out, 0.22f); } /* fades away, a little quicker than it came */
static int key(int k) { (void)k; if (Q.closing) finish(); else qrcard_close(); return 1; } /* any key, Back too */
static void tick(float dt) {
  if (!Q.open) return; /* (gone: the app keeps ticking overlays it has shown; a closed card asks nothing more) */
  timer_tick(&Q.in, dt); timer_tick(&Q.tick, dt); sglass_tick(&glass, dt);
  if (Q.closing) { if (timer_tick(&Q.out, dt) >= 1) finish(); return; }
  double now = app_now();
  if (!Q.done && Q.o.check && now >= Q.nextPoll) { Q.nextPoll = now + POLL;
    if (Q.o.check(Q.o.ud)) { Q.done = 1; layout(); timer_start(&Q.tick, 0.3f); Q.closeAt = now + Q.o.doneHold; } }
  if (Q.done && now >= Q.closeAt) qrcard_close();
}
static void outside(int a, int b) { (void)a; (void)b; qrcard_close(); } /* a click anywhere closes it too */
static void draw(void) {
  if (!Q.open) return;
  float ia = Q.in.on ? quint(timer_x(&Q.in)) : 1, a = ia, dy = 18 * (1 - ia), sc = 0.97f + 0.03f * ia;
  if (Q.closing) { float x = timer_x(&Q.out), o = x / (0.18f / 0.22f); a = 1 - (o > 1 ? 1 : o); float t = quint(x); dy = 14 * t; sc = 1 - 0.03f * t; }
  if (!Q.closing) hit_act(0, 0, SB_W, SB_H, outside, 0, 0);
  float x = SB_W / 2 - CW / 2, y = SB_H / 2 - Q.h / 2, cx = SB_W / 2, w = CW - 2 * PAD_X;
  gfx_opacity_push(a); gfx_offset_push(0, dy); gfx_scale_push(sc, sc, cx, SB_H / 2);
  if (glass.ok && glass.a.v >= 0.999f) gfx_shadow_under(x, y + 30, CW, Q.h, 32, 80, BLACK_A(153), x, y, CW, Q.h, 32); else gfx_shadow(x, y + 30, CW, Q.h, 32, 80, BLACK_A(153));
  sglass_draw(&glass, x, y, CW, Q.h, 32); gfx_ring(x, y, CW, Q.h, 32, 1, WHITE_A(20));
  float yy = y + PAD_T;
  if (Q.done) {
    float ta = Q.tick.on ? timer_x(&Q.tick) : 1; /* the tick fades in */
    gfx_opacity_push(1 - powf(1 - ta, 2)); gfx_circle(cx, yy + 48, 48, Q.o.badgeBg ? Q.o.badgeBg : ACC); icon_draw("check", cx - 28, yy + 48 - 28, 56, WHITE_A(255)); gfx_opacity_pop();
    yy += 96 + 22; text_draw(Q.doneTitle, cx, yy, 38, 700, WHITE_A(255), -0.4f, w, ALIGN_CENTER); yy += text_line_height(38);
    if (Q.doneSub[0]) text_draw_wrapped(Q.doneSub, cx, yy + 12, w, 6, 24, 400, WHITE_A(214), 0, ALIGN_CENTER, p_lh());
  } else if (Q.o.code) { /* a code to type: the service's mark, the code large and spaced, one short line, the QR */
    if (Q.o.badge) { gfx_rrect(cx - BADGE / 2, yy, BADGE, BADGE, BADGE * 0.225f, Q.o.badgeBg); icon_draw(Q.o.badge, cx - BADGE * 0.3f, yy + BADGE * 0.2f, BADGE * 0.6f, WHITE_A(255)); yy += BADGE + 26; }
    text_draw(Q.title, cx, yy, CODE_PX, 800, WHITE_A(255), 6.0f, w, ALIGN_CENTER); yy += text_line_height(CODE_PX) + 10;
    if (Q.sub[0]) text_draw_wrapped(Q.sub, cx, yy, w, 6, 24, 400, WHITE_A(199), 0, ALIGN_CENTER, p_lh());
    yy += Q.subLines * p_lh() + 26;
    float qx = cx - 140;
    if (!Q.url[0] || !qr_draw(Q.url, qx, yy, 280, 18, 10)) { gfx_rrect(qx, yy, 280, 280, 18, WHITE_A(255)); text_draw_wrapped(T("No internet connection"), cx, yy + 140 - text_line_height(24), 240, 2, 24, 600, RGB(0x16, 0x14, 0x1d), 0, ALIGN_CENTER, p_lh()); }
    yy += 280;
    { const char *cl = T("Close"); float kw = 38 + 10 + text_width(cl, 22, 600, 0); key_hint(cx - kw / 2, yy + 18 + 19, K_OK, cl, WHITE_A(209), 0); }
  } else {
    text_draw(Q.title, cx, yy, 38, 700, WHITE_A(255), -0.4f, w, ALIGN_CENTER); yy += text_line_height(38) + 12;
    if (Q.sub[0]) text_draw_wrapped(Q.sub, cx, yy, w, 6, 24, 400, WHITE_A(214), 0, ALIGN_CENTER, p_lh());
    yy += Q.subLines * p_lh() + 28;
    float qx = cx - 140;
    if (!Q.url[0] || !qr_draw(Q.url, qx, yy, 280, 18, 10)) { /* no network address: said in the code's place */
      gfx_rrect(qx, yy, 280, 280, 18, WHITE_A(255)); text_draw_wrapped(T("No internet connection"), cx, yy + 140 - text_line_height(24), 240, 2, 24, 600, RGB(0x16, 0x14, 0x1d), 0, ALIGN_CENTER, p_lh()); }
    yy += 280;
    if (Q.shown[0]) text_draw(Q.shown, cx, yy + 18, 23, 400, WHITE_A(191), 0, w, ALIGN_CENTER); yy += 18 + text_line_height(23);
    { const char *cl = T("Close"); float kw = 38 + 10 + text_width(cl, 22, 600, 0); key_hint(cx - kw / 2, yy + 18 + 19, K_OK, cl, WHITE_A(209), 0); } /* the way out: the remote's OK */
  }
  gfx_scale_pop(); gfx_offset_pop(); gfx_opacity_pop();
}
static Screen card = { NULL, NULL, key, tick, draw, NULL, NULL, NULL };
void qrcard_show(const QrCardOpts *o) {
  if (Q.open) finish(); /* one card at a time: the one there is replaced */
  memset(&Q, 0, sizeof Q); Q.o = *o; Q.open = 1;
  snprintf(Q.title, sizeof Q.title, "%s", o->title ? o->title : ""); snprintf(Q.sub, sizeof Q.sub, "%s", o->sub ? o->sub : "");
  snprintf(Q.url, sizeof Q.url, "%s", o->url ? o->url : ""); if (Q.url[0]) httpd_url_shown(Q.url, Q.shown, sizeof Q.shown);
  snprintf(Q.doneTitle, sizeof Q.doneTitle, "%s", o->doneTitle ? o->doneTitle : ""); snprintf(Q.doneSub, sizeof Q.doneSub, "%s", o->doneSub ? o->doneSub : "");
  Q.o.title = Q.o.sub = Q.o.url = Q.o.doneTitle = Q.o.doneSub = NULL; /* the caller's strings may not live on */
  layout(); timer_start(&Q.in, 0.3f); Q.nextPoll = app_now() + POLL;
  sglass_bake(&glass, SB_W / 2 - CW / 2, SB_H / 2 - Q.h / 2, CW, Q.h); /* where it settles */
  if (app_overlay != &card) under = app_overlay; app_overlay = &card;
}

/* ---- the ready-made cards ---- */
static struct { void (*cb)(int, void *); void *ud; unsigned gen; } tm;
/* the TV notices the key by itself: one saved from the phone since the card opened (a key that was there
   already does not close the card before the new one is in) */
static int tmdb_check(void *ud) { (void)ud; return tmdb_has_key() && tmdb_generation() != tm.gen; }
static void tmdb_closed(void *ud) { (void)ud; void (*cb)(int, void *) = tm.cb; tm.cb = NULL; if (cb) cb(tmdb_has_key(), tm.ud); }
void qrcard_show_tmdb(void (*onClose)(int hasKey, void *ud), void *ud) {
  tmdb_has_key(); /* the TMDB side ready to take the key from the phone */
  char url[600]; httpd_url("/tmdb", url, sizeof url);
  QrCardOpts o; memset(&o, 0, sizeof o);
  o.title = T("Titles and descriptions"); o.sub = T("Scan with your phone and paste your TMDB key. Get one at themoviedb.org"); o.url = url;
  o.check = tmdb_check; o.doneTitle = T("Key saved"); o.doneSub = T("Titles and descriptions now come in your language"); o.doneHold = 1.6f;
  o.onClose = tmdb_closed;
  qrcard_show(&o); tm.cb = onClose; tm.ud = ud; tm.gen = tmdb_generation();
}
/* Settings > Keys > Ratings and reviews: the Trakt client id page; onClose(hasKey, ud) once it is closed */
static struct { void (*cb)(int, void *); void *ud; unsigned gen; } tk;
static int trakt_check(void *ud) { (void)ud; return trakt_has_key() && trakt_generation() != tk.gen; }
static void trakt_closed(void *ud) { (void)ud; void (*cb)(int, void *) = tk.cb; tk.cb = NULL; if (cb) cb(trakt_has_key(), tk.ud); }
void qrcard_show_trakt(void (*onClose)(int hasKey, void *ud), void *ud) {
  trakt_has_key(); /* the Trakt side ready to take the id from the phone */
  char url[600]; httpd_url("/trakt", url, sizeof url);
  QrCardOpts o; memset(&o, 0, sizeof o);
  o.title = T("Ratings and reviews"); o.sub = T("Scan with your phone and paste your Trakt Client ID. The page says where to get it"); o.url = url;
  o.check = trakt_check; o.doneTitle = T("Id saved"); o.doneSub = T("Ratings, their spread and people\xe2\x80\x99s comments now come from Trakt"); o.doneHold = 1.6f;
  o.onClose = trakt_closed;
  qrcard_show(&o); tk.cb = onClose; tk.ud = ud; tk.gen = trakt_generation();
}
