/* a child's time. A child's profile can have a daily limit and a bedtime: the time with the app in front
   is counted each day (on this TV), five minutes before the end a notice says so, and at the end the film stops and a card
   says the TV is over for today. From there: another profile (a locked one asks the parent PIN), or half an hour more
   with the parent PIN. Bedtime runs until six in the morning. */
#include "screens/kidsgate.h"
#include "screens/pinpad.h"
#include "screens/player.h"
#include "core/app.h"
#include "core/profiles.h"
#include "core/store.h"
#include "core/i18n.h"
#include "core/keys.h"
#include "core/hit.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include "ui/glass.h"
#include "ui/widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static struct { int open, sel, warned, id; double used, savedAt; char day[16]; double at; } G;
#define GW 820.0f
#define GH 420.0f
#define BW 320.0f
#define BH 76.0f

static void today(char *out, size_t cap) { time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm); strftime(out, cap, "%Y-%m-%d", &tm); }
static void load(int id) {
  char k[48], d[16]; snprintf(k, sizeof k, "kidsUsed-%d", id); today(d, sizeof d); const char *v = store_get(k, "");
  G.id = id; snprintf(G.day, sizeof G.day, "%s", d); G.used = !strncmp(v, d, 10) ? atof(v + 11) : 0; G.warned = 0; }
static void save(void) { char k[48], v[48]; snprintf(k, sizeof k, "kidsUsed-%d", G.id); snprintf(v, sizeof v, "%s %.0f", G.day, G.used); store_set(k, v); G.savedAt = app_now(); }
static int extra_min(int id) { char k[48], d[16]; snprintf(k, sizeof k, "kidsExtra-%d", id); today(d, sizeof d); const char *v = store_get(k, ""); return !strncmp(v, d, 10) ? atoi(v + 11) : 0; }
static void add_extra(int id, int min) { char k[48], d[16], v[48]; snprintf(k, sizeof k, "kidsExtra-%d", id); today(d, sizeof d); snprintf(v, sizeof v, "%s %d", d, extra_min(id) + min); store_set(k, v); store_flush(); }

/* 0 still time; 1 the day's time is over; 2 bedtime */
static int over(int id, double *leftSec) {
  int lim = profiles_int(id, "limit", 0), bed = profiles_int(id, "bed", 0); *leftSec = 1e9;
  int extra = extra_min(id);
  if (lim > 0) { double left = (lim + extra) * 60.0 - G.used; *leftSec = left; if (left <= 0) return 1; }
  if (bed > 0) { time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm); int now = tm.tm_hour * 60 + tm.tm_min, end = bed + extra;
    if (now >= end || now < 6 * 60) return 2; double left = (end - now) * 60.0 - tm.tm_sec; if (left < *leftSec) *leftSec = left; }
  return 0;
}

static void close_gate(void) { if (!G.open) return; G.open = 0; if (app_overlay == &kidsGate) app_overlay = NULL; }
static void on_more(int ok, void *ud) { (void)ud; if (!ok) return; add_extra(G.id, 30); G.warned = 0; close_gate(); toast(T("Half an hour more"), 2200, NULL); }
static void choose(int i) {
  if (i == 0) { close_gate(); app_go(SCR_PROFILES, NULL, 0); return; }
  pin_ask(PIN_CHECK, T("More time"), on_more, NULL);
}
static int key(int k) {
  if (!G.open) { app_overlay = NULL; return 0; }
  if (k == K_LEFT || k == K_RIGHT) G.sel = k == K_RIGHT ? 1 : 0;
  else if (k == K_OK) choose(G.sel);
  return 1; /* Back and the rest: nothing (the time is over) */
}
static void tick(float dt) { (void)dt; }
static void btn_xy(int i, float *x, float *y) { *x = SB_W / 2 - BW - 12 + i * (BW + 24); *y = SB_H / 2 + GH / 2 - 44 - BH; }
static void on_focus(int a, int b) { (void)b; if (G.open) G.sel = a; }
static void on_btn(int a, int b) { (void)b; if (G.open) { G.sel = a; choose(a); } }
static void draw(void) {
  if (!G.open) return; float k = (float)((app_now() - G.at) / 0.3); if (k > 1) k = 1; float x = SB_W / 2 - GW / 2, y = SB_H / 2 - GH / 2;
  gfx_opacity_push(k); gfx_rect(0, 0, SB_W, SB_H, BLACK_A(200));
  glass_rrect(x, y, GW, GH, 34, RGB(21, 18, 31), .14f, .02f, .19f);
  double left; int why = over(G.id, &left);
  text_draw(why == 2 ? T("Bedtime!") : T("That's all the TV for today"), SB_W / 2, y + 60, 46, 800, WHITE_A(255), 0, GW - 80, ALIGN_CENTER);
  text_draw(why == 2 ? T("Time to sleep. See you tomorrow") : T("Time to play something else. See you tomorrow"), SB_W / 2, y + 140, 28, 500, WHITE_A(200), 0, GW - 80, ALIGN_CENTER);
  for (int i = 0; i < 2; i++) { float bx, by; btn_xy(i, &bx, &by); int on = G.sel == i; hit_add(bx, by, BW, BH, on_focus, on_btn, i, 0);
    gfx_rrect(bx, by, BW, BH, BH / 2, on ? WHITE_A(255) : WHITE_A(26));
    text_draw(i ? T("More time (PIN)") : T("Change profile"), bx + BW / 2, by + (BH - text_line_height(28)) / 2, 28, 700, on ? RGB(17, 17, 17) : WHITE_A(230), 0, BW - 24, ALIGN_CENTER); }
  gfx_opacity_pop();
}
Screen kidsGate = { NULL, NULL, key, tick, draw, NULL, NULL, NULL };

/* every frame: the child's time counted, the notice, the card */
void kids_tick(float dt) {
  int id = profiles_active();
  if (id <= 0 || !profiles_kids(id) || (!profiles_int(id, "limit", 0) && !profiles_int(id, "bed", 0))) { if (G.open) close_gate(); if (G.id && G.id != id) { save(); G.id = 0; } return; }
  char d[16]; today(d, sizeof d); if (G.id != id || strcmp(G.day, d)) { if (G.id && G.id != id) save(); load(id); }
  if (!G.open) G.used += dt; if (app_now() - G.savedAt > 30) save();
  double left; int why = over(id, &left);
  if (!why && left <= 300 && !G.warned) { G.warned = 1; toast(T("5 minutes of TV left"), 4000, NULL); }
  if (!why || SCR == SCR_PROFILES || pin_open()) return;
  if (!G.open) { if (player_open()) player_close(); G.open = 1; G.sel = 0; G.at = app_now(); app_overlay = &kidsGate; save(); fprintf(stderr, "[kids] time is up (%s)\n", why == 2 ? "bedtime" : "the day's limit"); }
  else if (app_overlay != &kidsGate) app_overlay = &kidsGate;
}
