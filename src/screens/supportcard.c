/* The support card (core/supcard.c says when): on Home once the app has been still a moment, never over a film, a
   loading screen, signing in, the guide or any other card, at most once a run. Home frosted behind (as the version's
   page), a glass sheet: a thank-you and one line on the left, the project's two codes on the right (the same drawing
   as Settings > About, larger). No buttons: the remote's keys say it under the words: OK turns the codes over
   between the coffee and GitHub (Left / Right pick a side too), Back closes (core/supcard.c walks the keys,
   tests/supcard checks them). The same on every platform: the phone draws it with its own scale. */
#include "screens/supportcard.h"
#include "screens/welcome.h"
#include "screens/login.h"
#include "screens/player.h"
#include "core/supcard.h"
#include "core/app.h"
#include "core/keys.h"
#include "core/hit.h"
#include "core/i18n.h"
#include "core/store.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include "ui/glass.h"
#include "ui/support.h"
#include "ui/icon.h"
#include "ui/widgets.h"
#include "core/anim.h"
#include "ui/orbs.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CW 1600.0f
#define CH 680.0f
#define TW 660.0f        /* the words' column */
#define Q 400.0f         /* the code's size: the most room on the card, to scan from a sofa */
#define QK 1.2f          /* the codes' pill and its words, larger than in proportion (support_draw_k) */
#define GAP 100.0f       /* at least this between the words' column and the codes */
#define HS 1.25f         /* the key hints, larger than the app's (their words 27 px) */
#define HEART 60.0f      /* the heart over the title */
/* the words read from a sofa: the title 52, the message 30 on 46 (1.5 lines), the hints 27; never smaller (the fit test checks) */
#define TPX 52
#define TLH 62.0f
#define MPX 30
#define MLH 46.0f
static struct { SupFocus fs; int open, closing, shown; double openAt, closeAt, forceAt, pressAt; Tween sel, in, ringA, nvA; Nudge nudge; Orbs co; char url[300], urlS[160], gh[300], ghS[160]; } C; /* fs: the focus and which code shows (core/supcard.c); sel: its turn; ringA: the codes' focus ring; nvA: Don't show again's; co: the glows, as Settings' */
static SGlass glass;
static Screen card;

static const char *shown_url(const char *u, char *out, size_t cap) { const char *p = u; if (!strncmp(p, "https://", 8)) p += 8; else if (!strncmp(p, "http://", 7)) p += 7; if (!strncmp(p, "www.", 4)) p += 4; snprintf(out, cap, "%s", p); size_t n = strlen(out); if (n && out[n - 1] == '/') out[n - 1] = 0; return out; }
static void open_card(void) {
  C.open = 1; C.closing = 0; C.shown = 1; C.pressAt = 0; C.openAt = app_now(); tween_set(&C.sel, 0); tween_set(&C.in, 0); tween_to(&C.in, 1, 0.5f, EASE_OUT); orbs_place(&C.co, 0, 1.8f);
  snprintf(C.url, sizeof C.url, "%s", welcome_support_url()); snprintf(C.gh, sizeof C.gh, "%s", welcome_github_url()); shown_url(C.url, C.urlS, sizeof C.urlS); shown_url(C.gh, C.ghS, sizeof C.ghS);
  C.fs = supc_focus_start(); tween_set(&C.ringA, 1); tween_set(&C.nvA, 0);
  if (getenv("SB_SUPCARD_ATNEVER")) { C.fs.never = 1; tween_set(&C.ringA, 0); tween_set(&C.nvA, 1); } /* (development: opens on Don't show again, for a picture of it) */
  sglass_bake(&glass, 0, 0, SB_W, SB_H); app_overlay = &card; fprintf(stderr, "[support] the card shows (start %d)\n", supc_store()->get("supStarts", 0)); }
static void close_card(int never) { /* (never: "Don't show again") */
  if (!C.open || C.closing) return; C.closing = 1; C.closeAt = app_now(); tween_to(&C.in, 0, 0.32f, EASE_CSS_EASE); orbs_push(&C.co, ORB_BACK, 0.6f);
  if (never) supc_never(supc_store()); else supc_shown(supc_store());
  const SupStore *st = supc_store(); if (never) fprintf(stderr, "[support] closed: not again\n"); else fprintf(stderr, "[support] closed: again in %d starts\n", st->get("supNext", 0) - st->get("supStarts", 0)); }
static void finish(void) { C.open = C.closing = 0; if (app_overlay == &card) app_overlay = NULL; sglass_free(&glass); }

void supportcard_start(int autoReload) { supc_start(supc_store(), autoReload); if (getenv("SB_SUPPORTCARD")) C.forceAt = 3; } /* (development: SB_SUPPORTCARD=1, on Home after 3 s whatever is due) */
int  supportcard_open(void) { return C.open; }
/* fully in: its frosted picture of the page (solid, the whole screen) hides the page, which need not be drawn under it
   (on the TV the page under it cost ~19 ms a frame, the card 20 fps instead of 48) */
int  supportcard_covers(void) { return C.open && !C.closing && C.in.v >= 0.999f && glass.ok && glass.a.v >= 0.999f; }
/* development / test: SB_SUPCARD_SELFTEST=1 (with SB_SUPPORTCARD=1): the card shows, Back closes it, its fade must be
   drawn, and once it is closed drawing it must add no hit areas; prints the result and exits (0 passed, 3 failed) */
static int key(int k); static void draw(void); static int fadeDraws; /* (fadeDraws: the self-test's frames drawn while closing) */
static void selftest(void) {
  static int on = -1, phase; static double at; if (on < 0) on = getenv("SB_SUPCARD_SELFTEST") != NULL; if (!on) return;
  if (phase == 0 && C.open && !C.closing && app_now() - C.openAt > 1.0) { key(K_BACK); phase = 1; at = app_now(); }
  else if (phase == 1 && !C.open && app_now() - at > 0.6) {
    int n0 = hit_count(); draw(); int added = hit_count() - n0, ok = fadeDraws > 0 && added == 0;
    fprintf(stderr, "[supcard-selftest] %s: the close fade drew %d frames; closed, it added %d hit areas\n", ok ? "passed" : "FAILED", fadeDraws, added); exit(ok ? 0 : 3); }
  else if (app_now() > 40) { fprintf(stderr, "[supcard-selftest] FAILED: the card did not show and close\n"); exit(3); } }
void supportcard_tick(void) {
  selftest();
  if (C.open || C.shown) return;
  int due = supc_due(supc_store()) || (C.forceAt > 0 && app_now() >= C.forceAt);
  if (!due || !app_splash_done || SCR != SCR_HOME || app_overlay || welcome_is_open() || player_open() || app_idle() < 1.5f) return; /* (Home, still a moment, nothing else up) */
  if (!welcome_support_url()[0] && !welcome_github_url()[0]) return; /* (no codes in this build: nothing to show) */
  open_card(); }

static void turn(void) { tween_to(&C.sel, (float)C.fs.side, 0.5f, EASE_IN_OUT); orbs_push(&C.co, C.fs.side ? ORB_RIGHT : ORB_LEFT, 0.7f); } /* the pill's light glides, the code turns over */
static void hit_codes(int i, int b) { (void)i; (void)b; if (C.gh[0]) { C.fs.side ^= 1; turn(); } } /* (a tap or a click on the codes turns them over) */
static void hit_close(int i, int b) { (void)i; (void)b; close_card(0); }
static void hit_never(int i, int b) { (void)i; (void)b; close_card(1); }
static int key(int k) {
  if (C.closing) return 1;
  int sk = k == K_OK ? SUPK_OK : k == K_BACK ? SUPK_BACK : k == K_UP ? SUPK_UP : k == K_DOWN ? SUPK_DOWN : k == K_LEFT ? SUPK_LEFT : k == K_RIGHT ? SUPK_RIGHT : -1;
  if (sk < 0) return 1;
  int r = supc_key(&C.fs, sk, C.gh[0] != 0);
  if (k == K_OK) { C.pressAt = app_now(); orbs_push(&C.co, ORB_OK, 0.6f); }
  if (r == SUPA_CLOSE) close_card(0); else if (r == SUPA_NEVER) close_card(1); else if (r == SUPA_TURN) turn();
  else if (r == SUPA_MOVE) { tween_to(&C.ringA, C.fs.never ? 0 : 1, 0.2f, EASE_CSS_EASE); tween_to(&C.nvA, C.fs.never ? 1 : 0, 0.2f, EASE_CSS_EASE); orbs_push(&C.co, k == K_DOWN ? ORB_DOWN : ORB_UP, 0.35f); }
  else if (k != K_OK) nudge_start(&C.nudge, k);
  return 1; }
static void tick(float dt) { tween_tick(&C.sel, dt); tween_tick(&C.in, dt); tween_tick(&C.ringA, dt); tween_tick(&C.nvA, dt); nudge_tick(&C.nudge, dt); sglass_tick(&glass, dt);
  orbs_step(&C.co, dt, 0, C.closing ? 1.6f : 1 + (1 - C.in.v) * 0.8f, 2.6f); if (C.closing && app_now() - C.closeAt > 0.34) finish(); }
/* the key hints, one row: OK names the other code, Back closes, then "Don't show again" (quieter: Down reaches it,
   OK there ends it); their row's width (also the fit test's) */
#define HGAP 30.0f
static const char *ok_label(int side) { return side ? T("Support") : "GitHub"; }
static float never_w(void) { return key_hint(-9999, -9999, K_DOWN, T("Don't show again"), 0, 0) + 24; } /* (room for its focus pill) */
static float hints_w(int hasGh) { float w = key_hint(-9999, -9999, K_BACK, T("Close"), 0, 0) + HGAP + never_w(); if (hasGh) w += HGAP + fmaxf(key_hint(-9999, -9999, K_OK, ok_label(0), 0, 0), key_hint(-9999, -9999, K_OK, ok_label(1), 0, 0)); return w * HS; }
/* the narrowest width that keeps a text on as many lines as the column gives it: the lines even, never one lonely word */
static float balanced_w(const char *s, float maxW, int px, int wt, float tr) { int n = text_wrap_count(s, maxW, 9, px, wt, tr); if (n <= 1) return maxW;
  float lo = maxW * 0.5f, hi = maxW; for (int i = 0; i < 9; i++) { float m = (lo + hi) / 2; if (text_wrap_count(s, m, 9, px, wt, tr) <= n) hi = m; else lo = m; } return hi + 2; }
static float stag(double t, int k) { float e = (float)((t - 0.06 - 0.07 * k) / 0.38); e = e < 0 ? 0 : e > 1 ? 1 : e; return ease_apply(EASE_OUT, e); } /* (each piece in after the one before) */
static void draw(void) {
  if (!C.open) return; /* (closed: nothing drawn and no hit areas left behind; the app keeps drawing an overlay it has shown, and the closed card's invisible buttons ate taps on the page under them. C.open lasts until the fade has played) */
  if (C.closing) fadeDraws++;
  float a = ease_apply(EASE_OUT, C.in.v); int rtl = app_rtl(), al = rtl ? ALIGN_RIGHT : ALIGN_LEFT; double t = C.closing ? 9 : app_now() - C.openAt;
  /* Home frosted behind, Settings' glows over it, leaning with every press */
  gfx_opacity_push(a); sglass_draw(&glass, 0, 0, SB_W, SB_H, 0); gfx_rect(0, 0, SB_W, SB_H, col_alpha(PAGE, 0.86f)); orbs_draw(&C.co, 0.9f); gfx_opacity_pop();
  float x = SB_W / 2 - CW / 2, y = SB_H / 2 - CH / 2, sc = 0.965f + 0.035f * a;
  gfx_opacity_push(a); gfx_scale_push(sc, sc, SB_W / 2, SB_H / 2);
  gfx_shadow(x, y + 24, CW, CH, 44, 60, BLACK_A(120)); gfx_rrect(x, y, CW, CH, 44, RGBA(30, 26, 44, 220)); orbs_draw_in(&C.co, 0.35f, x, y, CW, CH, 44); gfx_rrect_v(x, y, CW, CH, 44, WHITE_A(22), WHITE_A(4)); gfx_ring(x, y, CW, CH, 44, 1.2f, WHITE_A(40));
  /* the words and the keys' hints: one block, centred beside the codes; a soft shade behind them (the glows stay as they
     are elsewhere, never under the words) */
  float pad = 80, tx = rtl ? x + CW - pad : x + pad; const char *tt = T("Enjoying Stremio Origin?"), *ln = T("One person makes it in their spare time. Support or a star keeps it going.");
  { uint32_t sh = RGBA(18, 15, 28, 150), tr0 = RGBA(18, 15, 28, 0); if (rtl) gfx_rrect_h(x + CW * 0.42f, y, CW * 0.58f, CH, 44, tr0, sh); else gfx_rrect_h(x, y, CW * 0.58f, CH, 44, sh, tr0); }
  static const char *mt, *ml; static unsigned mg; static float tw1, tw2; static int n1, n2; /* (the balanced widths, measured once per words and language) */
  if (mt != tt || ml != ln || mg != i18n_gen()) { mt = tt; ml = ln; mg = i18n_gen(); tw1 = balanced_w(tt, TW, TPX, 800, -0.5f); tw2 = balanced_w(ln, TW, MPX, 500, 0);
    n1 = text_wrap_count(tt, tw1, 2, TPX, 800, -0.5f); n2 = text_wrap_count(ln, tw2, 3, MPX, 500, 0); if (n1 < 1) n1 = 1; if (n2 < 1) n2 = 1; }
  float HH = 38 * HS, blk = HEART + 22 + n1 * TLH + 20 + n2 * MLH + 48 + HH, ty = y + CH / 2 - blk / 2;
  static int noText = -1; if (noText < 0) noText = getenv("SB_SUPCARD_NOTEXT") != NULL; /* (development: the card without its words, heart and buttons, to measure what lies behind them) */
  if (!noText) { float e = stag(t, 0), pr = C.pressAt > 0 ? (float)((app_now() - C.pressAt) / 0.5) : 1, beat = pr < 1 ? 1 + 0.14f * sinf(pr * 3.14159f) : 1; /* (it beats once with each OK) */
    gfx_opacity_push(e); gfx_scale_push(beat, beat, rtl ? tx - HEART / 2 : tx + HEART / 2, ty + HEART / 2); icon_draw("g_heart", rtl ? tx - HEART : tx, ty + 8 * (1 - e), HEART, ACC2 | 0xff000000u); gfx_scale_pop(); gfx_opacity_pop(); } ty += HEART + 22; /* (what it is about, in the accent) */
  if (!noText) { float e = stag(t, 1); gfx_opacity_push(e); text_draw_wrapped(tt, tx, ty + 10 * (1 - e), tw1, 2, TPX, 800, WHITE_A(255), -0.5f, al, TLH); gfx_opacity_pop(); } ty += n1 * TLH + 20;
  if (!noText) { float e = stag(t, 2); gfx_opacity_push(e); text_draw_wrapped(ln, tx, ty + 10 * (1 - e), tw2, 3, MPX, 500, WHITE_A(232), 0, al, MLH); gfx_opacity_pop(); } ty += n2 * MLH + 52;
  /* the keys' hints: OK turns the codes over (it names the other one), Back closes, Don't show again */
  if (!noText) { float e = stag(t, 3); gfx_opacity_push(e); float hy = ty + HH / 2 + 10 * (1 - e), hx = tx;
    gfx_scale_push(HS, HS, tx, hy); float w = 0;
    if (C.gh[0]) { const char *l0 = ok_label(0), *l1 = ok_label(1); float s = C.sel.v; /* (the label turns with the code) */
      float w0 = key_hint(-9999, -9999, K_OK, l0, 0, 0), w1 = key_hint(-9999, -9999, K_OK, l1, 0, 0); w = w0 + (w1 - w0) * s;
      if (s < 0.99f) { gfx_opacity_push(1 - s); key_hint(hx, hy, K_OK, l0, WHITE_A(225), rtl); gfx_opacity_pop(); }
      if (s > 0.01f) { gfx_opacity_push(s); key_hint(hx, hy, K_OK, l1, WHITE_A(225), rtl); gfx_opacity_pop(); }
      gfx_opacity_push(1 - 0.45f * C.nvA.v); /* (quieter while Don't show again has the focus: OK is its there) */
      if (s < 0.99f) { gfx_opacity_push(1 - s); key_hint(hx, hy, K_OK, l0, WHITE_A(225), rtl); gfx_opacity_pop(); }
      if (s > 0.01f) { gfx_opacity_push(s); key_hint(hx, hy, K_OK, l1, WHITE_A(225), rtl); gfx_opacity_pop(); }
      gfx_opacity_pop();
      hit_add(rtl ? hx - w * HS : hx, hy - HH / 2, w * HS, HH, hit_codes, NULL, 0, 0); w += HGAP; }
    float bxh = rtl ? hx - w : hx + w, bw = key_hint(bxh, hy, K_BACK, T("Close"), WHITE_A(200), rtl);
    hit_add(rtl ? tx - (w + bw) * HS : tx + w * HS, hy - HH / 2, bw * HS, HH, hit_close, NULL, 0, 0); w += bw + HGAP;
    { /* Don't show again: the same hint, quieter; with the focus a soft accent pill under it, and its key turns to OK */
      const char *nl = T("Don't show again"); float nw = never_w(), nv = C.nvA.v, px0 = rtl ? hx - w - nw : hx + w, ph = 38 + 14, nx = nudge_x(&C.nudge) * (C.fs.never ? 1 : 0) / HS;
      if (nv > 0.01f) { gfx_opacity_push(nv); gfx_rrect(px0 + nx, hy - ph / 2, nw, ph, ph / 2, acc_a(0.22f)); gfx_ring(px0 + nx, hy - ph / 2, nw, ph, ph / 2, 1.5f, ACC2 | 0xff000000u); gfx_opacity_pop(); }
      float kx = rtl ? px0 + nw - 12 : px0 + 12; uint32_t c = col_mix(WHITE_A(150), WHITE_A(255), nv);
      if (nv < 0.99f) { gfx_opacity_push(1 - nv); key_hint(kx + nx, hy, K_DOWN, nl, c, rtl); gfx_opacity_pop(); }
      if (nv > 0.01f) { gfx_opacity_push(nv); key_hint(kx + nx, hy, K_OK, nl, c, rtl); gfx_opacity_pop(); }
      hit_add(rtl ? tx - (w + nw) * HS : tx + w * HS, hy - ph / 2 * HS, nw * HS, ph * HS, hit_never, NULL, 0, 0); }
    gfx_scale_pop(); gfx_opacity_pop(); }
  /* the codes, on the other side, in the focus' ring (they are all there is to choose) */
  if (C.gh[0] || C.url[0]) { float e = stag(t, 2), sw = support_w_k(Q, QK), sh = support_h_k(Q, QK), qx = rtl ? x + pad : x + CW - pad - sw, qy = y + CH / 2 - sh / 2 + 12 * (1 - e);
    qx += C.fs.never ? 0 : nudge_x(&C.nudge); /* (a key that goes nowhere gives a little) */
    gfx_opacity_push(e);
    if (C.ringA.v > 0.01f) { gfx_opacity_push(C.ringA.v); gfx_ring_out(qx - 16, qy - 14, sw + 32, sh + 28, 30, 3, ACC2); gfx_opacity_pop(); }
    support_glass(0); support_draw_k(qx, qy, Q, QK, C.sel.v, !C.closing && !C.fs.never, C.url, C.urlS, C.gh, C.ghS); hit_add(qx, qy, sw, sh, hit_codes, NULL, 0, 0);
    gfx_opacity_pop(); }
  gfx_scale_pop(); gfx_opacity_pop(); }
static Screen card = { NULL, NULL, key, tick, draw, NULL, NULL, NULL };

/* development: SB_SUPCARD_FIT=1: every language's words measured against the card: the title in 2 lines, the line in
   3, the three keys' hints (OK, Back, Don't show again) in one row in the words' column; prints each that does not fit,
   then the count */
int supportcard_fit_check(const char *assetDir) {
  int bad = 0, n = i18n_lang_count();
  if (Q < 380 || QK < 1.15f) { fprintf(stderr, "[fit] the codes are smaller than a sofa can scan (code %.0f px, pill words x%.2f)\n", Q, QK); bad++; }
  if (TPX < 44 || MPX < 28 || 22 * HS < 26 || HEART < 48) { fprintf(stderr, "[fit] the words are smaller than a sofa can read (title %d, message %d, hints %.0f px, heart %.0f)\n", TPX, MPX, 22 * HS, HEART); bad++; } /* (10-foot floor) */
  for (int i = 0; i < n; i++) { const char *lc = i18n_lang_code(i); i18n_init(assetDir, lc);
    const char *tt = T("Enjoying Stremio Origin?"), *ln = T("One person makes it in their spare time. Support or a star keeps it going.");
    int l1 = text_wrap_count(tt, TW, 9, TPX, 800, -0.5f), l2 = text_wrap_count(ln, TW, 9, MPX, 500, 0); float bw = hints_w(1);
    int miss = (!strcmp(lc, "en") ? 0 : (!strcmp(tt, "Enjoying Stremio Origin?") || !strcmp(T("Close"), "Close") || !strcmp(T("Support"), "Support") || !strcmp(T("Don't show again"), "Don't show again") || !strncmp(ln, "One person makes", 16)));
    float qw = support_w_k(Q, QK), qh = support_h_k(Q, QK), room = CW - 2 * 80 - TW - GAP; /* the codes beside the words, their pill as wide as its words need */
    float hroom = CW - 2 * 80 - qw - 60; /* (the hints' row may run past the words' column, up to the codes) */
    if (getenv("SB_SUPCARD_FIT_ALL")) fprintf(stderr, "[fit] %s: hints %.0f of %.0f px\n", lc, bw, hroom);
    if (l1 > 2 || l2 > 3 || bw > hroom || miss || qw > room || qh > CH - 2 * 40) { bad++; fprintf(stderr, "[fit] %s: title %d lines, line %d lines, hints %.0f of %.0f px, codes %.0f x %.0f in %.0f x %.0f%s\n", lc, l1, l2, bw, hroom, qw, qh, room, CH - 80, miss ? ", not translated" : ""); } }
  { /* the text engine keeps what reaches past the line box: a final Arabic yeh's dots (U+064A) sit below it and were
       once cut off, so it read as a dotless U+0649; Persian's own yeh (U+06CC) has no dots at the end of a word */
    int ab, be, cut = text_ink_check("\xd9\x8a\xd9\x88\xd9\x85\xd9\x8a", MPX, 500, &ab, &be); /* "يومي" */
    int ab2, be2; text_ink_check("\xd9\x8a\xd9\x88\xd9\x85\xd9\x89", MPX, 500, &ab2, &be2);  /* "يومى": the same word, dotless */
    if (cut || be <= be2) { bad++; fprintf(stderr, "[fit] a final Arabic yeh loses its dots (%d rows below the line, %d without the dots%s)\n", be, be2, cut ? ", cut" : ""); } }
  for (int i = 0; i < n; i++) { const char *lc = i18n_lang_code(i); i18n_init(assetDir, lc); /* nothing in any language is cut at the top or bottom */
    const char *w[] = { T("Enjoying Stremio Origin?"), T("One person makes it in their spare time. Support or a star keeps it going."), T("Close"), T("Support"), T("Don't show again") };
    for (int k = 0; k < 5; k++) { int ab, be; if (text_ink_check(w[k], k ? MPX : TPX, k ? 500 : 800, &ab, &be)) { bad++; fprintf(stderr, "[fit] %s: \"%s\" is cut at the top or bottom\n", lc, w[k]); } } }
  fprintf(stderr, "[fit] %d of %d languages do not fit\n", bad, n); return bad; }
