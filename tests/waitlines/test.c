/* tests/waitlines/run.sh: the loading lines' pick and slot rules, with a fake clock and fake dice */
#include "core/waitlines.h"
#include <stdio.h>
#include <string.h>
#include "core/waitlines_pools.h"
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static WlCtx ctx(WlStep st, int hour, int wday) { WlCtx c; memset(&c, 0, sizeof c); c.step = st; c.hour = hour; c.wday = wday; c.plat = WL_PLAT_OTHER; c.brand = ""; return c; }
static int pool_of_pick(WlCtx c, unsigned r) { WlHist h = { { 0 }, 0, 0 }; c.rnd = r; return wl_pool_of(wl_pick(&c, &h)); }
static const char *tr_fr(const char *s) { return !strcmp(s, "Warming up the pixels\xe2\x80\xa6") ? "Les pixels chauffent\xe2\x80\xa6" : s; } /* only one old line translated */
int main(void) {
  /* pool priority */
  { WlCtx c = ctx(WL_FIND, 23, 2); c.binge = 3; CHECK(pool_of_pick(c, 7) == WLP_LATEBINGE, "late night binge first"); }
  { WlCtx c = ctx(WL_FIND, 15, 6); c.binge = 2; CHECK(pool_of_pick(c, 7) == WLP_WEEKENDBINGE, "weekend binge"); }
  { WlCtx c = ctx(WL_FIND, 19, 5); CHECK(pool_of_pick(c, 7) == WLP_FRIDAY, "Friday 19:00"); c.hour = 3; c.wday = 6; CHECK(pool_of_pick(c, 7) == WLP_FRIDAY, "Saturday 03:00 is still Friday night"); c.hour = 6; CHECK(pool_of_pick(c, 7) != WLP_FRIDAY, "Saturday 06:00 is not"); c.hour = 16; c.wday = 5; CHECK(pool_of_pick(c, 7) != WLP_FRIDAY, "Friday 16:00 is not"); }
  { WlCtx c = ctx(WL_FIND, 15, 2); c.binge = 2; CHECK(pool_of_pick(c, 7) == WLP_BINGE, "a weekday afternoon binge: the binge's own"); }
  { WlCtx c = ctx(WL_FIND, 15, 2); c.act = WL_ACT_FILM; CHECK(pool_of_pick(c, 7) == WLP_FILM, "film"); c.act = WL_ACT_RESUME; CHECK(pool_of_pick(c, 7) == WLP_RESUME, "resume"); c.act = WL_ACT_FIRSTTODAY; CHECK(pool_of_pick(c, 7) == WLP_FIRSTTODAY, "first today"); }
  { WlCtx c = ctx(WL_FIND, 15, 2); c.act = WL_ACT_FILM; c.nth = 1; CHECK(pool_of_pick(c, 7) != WLP_FILM, "activity only for the first line"); }
  { WlCtx c = ctx(WL_PEERS, 23, 5); c.binge = 4; c.plat = WL_PLAT_LG; for (unsigned r = 0; r < 200; r++) CHECK(pool_of_pick(c, r) == WLP_PEERS, "peers: plain only (r %u)", r); }
  { WlCtx c = ctx(WL_PIECES, 12, 3); CHECK(pool_of_pick(c, 0) == WLP_PIECES, "pieces"); c.step = WL_FILES; CHECK(pool_of_pick(c, 0) == WLP_FILES, "files"); }
  { WlCtx c = ctx(WL_SIGNIN, 12, 3); c.binge = 5; CHECK(pool_of_pick(c, 1) != WLP_BINGE && pool_of_pick(c, 1) != WLP_LATEBINGE, "sign-in: no activity or combination"); }
  /* platform one in four, time one in three of the rest (rnd % 4 == 0: platform; (rnd/4) % 3 == 0: time) */
  { WlCtx c = ctx(WL_FIND, 12, 3); c.plat = WL_PLAT_LG; int pl = 0, tm = 0, pn = 0; for (unsigned r = 0; r < 1200; r++) { int p = pool_of_pick(c, r); if (p == WLP_LG) pl++; else if (p == WLP_AFTERNOON) tm++; else if (p == WLP_OLD) pn++; }
    CHECK(pl == 300, "platform 1 in 4: %d of 1200", pl); CHECK(tm == 300, "time 1 in 3 of the rest: %d", tm); CHECK(pn == 600, "plain the rest: %d", pn); }
  { WlCtx c = ctx(WL_FIND, 12, 3); for (unsigned r = 0; r < 400; r++) { int p = pool_of_pick(c, r); CHECK(p < WLP_LG || p > WLP_PHONE, "no platform pool off a known platform"); } }
  /* time of day */
  { struct { int h, wd, p; } T[] = { { 6, 2, WLP_MORNING }, { 12, 2, WLP_AFTERNOON }, { 18, 2, WLP_AFTERWORK }, { 21, 2, WLP_EVENING }, { 18, 0, WLP_EVENING }, { 23, 2, WLP_LATE }, { 4, 3, WLP_LATE } };
    for (int i = 0; i < 7; i++) { WlCtx c = ctx(WL_SIGNIN, T[i].h, T[i].wd); CHECK(pool_of_pick(c, 0) == T[i].p, "time %d:00 day %d -> pool %d, got %d", T[i].h, T[i].wd, T[i].p, pool_of_pick(c, 0)); } }
  { WlCtx c = ctx(WL_SIGNIN, 12, 6); CHECK(pool_of_pick(c, 12) == WLP_WEEKEND, "a weekend line at the weekend"); CHECK(pool_of_pick(c, 0) == WLP_AFTERNOON, "or the hour's"); }
  /* brands: exact match only, no guessing */
  { WlCtx c = ctx(WL_FIND, 12, 3); c.plat = WL_PLAT_LG; c.brand = "OLED"; CHECK(pool_of_pick(c, 0) == WLP_LG_OLED, "OLED"); c.brand = "QNED"; CHECK(pool_of_pick(c, 0) == WLP_LG_QNED, "QNED");
    c.brand = "NanoCell"; CHECK(pool_of_pick(c, 0) == WLP_LG_NANO, "NanoCell"); c.brand = "oled"; CHECK(pool_of_pick(c, 0) == WLP_LG, "no guessing: oled"); c.brand = ""; CHECK(pool_of_pick(c, 0) == WLP_LG, "generic LG");
    c.plat = WL_PLAT_ATV; c.brand = "Shield"; CHECK(pool_of_pick(c, 0) == WLP_ATV_SHIELD, "Shield"); c.brand = "Fire TV"; CHECK(pool_of_pick(c, 0) == WLP_ATV_FIRETV, "Fire TV"); c.brand = "Hisense"; CHECK(pool_of_pick(c, 0) == WLP_ATV, "unknown maker: generic");
    c.plat = WL_PLAT_SAMSUNG; c.brand = "QNED"; CHECK(pool_of_pick(c, 0) == WLP_SAMSUNG, "Samsung ignores another's family"); }
  { CHECK(wl_platform("LG webOS 4.4.0") == WL_PLAT_LG, "webOS"); CHECK(wl_platform("Mac") == WL_PLAT_OTHER, "Mac"); }
  /* no repeat, none of the last 20 while the pool allows */
  { WlHist h = { { 0 }, 0, 0 }; WlCtx c = ctx(WL_PEERS, 12, 3); int prev = -1; int seen[WL_NLINES] = { 0 };
    int np = WL_START[WLP_PEERS + 1] - WL_START[WLP_PEERS]; for (unsigned r = 0; r < (unsigned)np; r++) { c.rnd = r * 24; int id = wl_pick(&c, &h); CHECK(!seen[id], "peers %d: a line from the last 20 again while others were left", r); seen[id] = 1; CHECK(id != prev, "twice in a row"); prev = id; }
    for (unsigned r = 0; r < 40; r++) { c.rnd = r * 7; int id = wl_pick(&c, &h); CHECK(id != prev, "twice in a row (all used)"); prev = id; } }
  /* the slot: 4 s calm, 3.5 s each, gaps 5 and 6, at most 3, back at once on a change, never when not calm */
  { WlSlot s; memset(&s, 0, sizeof s); WlHist h = { { 0 }, 0, 0 }; WlCtx c = ctx(WL_PEERS, 12, 3); const char *real = "Finding peers\xe2\x80\xa6"; double t = 0; double on[8]; int non = 0; const char *was = real;
    for (t = 0; t < 60; t += 0.05) { c.rnd = (unsigned)(t * 1000); const char *l = wl_slot(&s, 1, t, real, 1, &c, &h, NULL, 1); if (l != was && strcmp(l, was)) { if (l != real && non < 8) on[non++] = t; } was = l; }
    CHECK(non == 3, "3 lines in a long calm wait, got %d", non); if (non == 3) { CHECK(on[0] >= 4 && on[0] < 4.1, "first at 4 s: %.2f", on[0]); CHECK(on[1] - (on[0] + 3.5) >= 5 - 0.06, "2nd 5 s after the 1st went: %.2f", on[1]); CHECK(on[2] - (on[1] + 3.5) >= 6 - 0.06, "3rd 6 s after: %.2f", on[2]); } }
  { WlSlot s; memset(&s, 0, sizeof s); WlHist h = { { 0 }, 0, 0 }; WlCtx c = ctx(WL_PEERS, 12, 3);
    const char *l = NULL; for (double t = 0; t < 5; t += 0.1) l = wl_slot(&s, 1, t, "Finding peers\xe2\x80\xa6", 1, &c, &h, NULL, 1); CHECK(strcmp(l, "Finding peers\xe2\x80\xa6"), "a line shows by 5 s");
    l = wl_slot(&s, 1, 5.1, "Slow source: 0.6 MB/s for a 4 MB/s video", 0, &c, &h, NULL, 1); CHECK(!strcmp(l, "Slow source: 0.6 MB/s for a 4 MB/s video"), "real information takes the place at once");
    for (double t = 5.2; t < 30; t += 0.1) { l = wl_slot(&s, 1, t, "Slow source: 0.6 MB/s for a 4 MB/s video", 0, &c, &h, NULL, 1); CHECK(!strncmp(l, "Slow source", 11), "never over information (%.1f)", t); } }
  { WlSlot s; memset(&s, 0, sizeof s); WlHist h = { { 0 }, 0, 0 }; WlCtx c = ctx(WL_PEERS, 12, 3); const char *l = NULL; char buf[64];
    int fun = 0; for (double t = 0; t < 6; t += 0.1) { snprintf(buf, sizeof buf, "Finding peers\xe2\x80\xa6 %ds", (int)t); l = wl_slot(&s, 1, t, buf, 1, &c, &h, NULL, 1); if (strncmp(l, "Finding peers", 13)) fun = 1; }
    CHECK(fun, "digits ticking don't restart the 4 s"); }
  { WlSlot s; memset(&s, 0, sizeof s); WlHist h = { { 0 }, 0, 0 }; WlCtx c = ctx(WL_PEERS, 12, 3); const char *l = NULL;
    for (double t = 0; t < 5; t += 0.1) l = wl_slot(&s, 1, t, "A", 1, &c, &h, NULL, 1); l = wl_slot(&s, 1, 5.1, "B", 1, &c, &h, NULL, 1); CHECK(!strcmp(l, "B"), "a calm change ends the line at once too");
    l = wl_slot(&s, 2, 5.2, "C", 1, &c, &h, NULL, 1); CHECK(!strcmp(l, "C") && s.shown == 0, "a new wait starts again"); }
  /* languages: a new line only once translated, else an old translated line */
  { WlSlot s; memset(&s, 0, sizeof s); WlHist h = { { 0 }, 0, 0 }; WlCtx c = ctx(WL_PEERS, 12, 3); const char *l = NULL;
    for (double t = 0; t < 4.2; t += 0.1) l = wl_slot(&s, 1, t, "X", 1, &c, &h, tr_fr, 0);
    int old = 0; for (int i = WL_START[WLP_OLD]; i < WL_START[WLP_OLD + 1]; i++) if (!strcmp(l, wl_text(i)) || !strcmp(l, tr_fr(wl_text(i)))) old = 1;
    CHECK(old, "an untranslated new line falls back to an old one: %s", l); }
  /* the peer step: nothing that is untrue with no peer yet (no count, nothing "connected") */
  for (int i = WL_START[WLP_PEERS]; i < WL_START[WLP_PEERS + 1]; i++) { const char *t = wl_text(i); CHECK(!strstr(t, "connected") && !strpbrk(t, "0123456789"), "peer line says peers are there: %s", t); }
  /* the truth rule: a pool that says what is going on keeps 3 lines at most; no line claims progress, speed, a count or a connection */
  { const int ST[] = { WLP_SIGNIN, WLP_CHOOSE, WLP_PEERS, WLP_FILES, WLP_PIECES, WLP_LIVE };
    for (int k = 0; k < 6; k++) { int n = WL_START[ST[k] + 1] - WL_START[ST[k]]; CHECK(n >= 1 && n <= 3, "a state pool (%d) has %d lines", ST[k], n); } }
  { const char *BAN[] = { "lmost", "onnected", "a few", "quick", "speeding", "nearly", "Probably", "buffer" };
    for (int i = 0; i < WL_NLINES; i++) for (int b = 0; b < 8; b++) CHECK(!strstr(wl_text(i), BAN[b]), "line %d claims something that can be false (%s): %s", i, BAN[b], wl_text(i));
    for (int i = 0; i < WL_NLINES; i++) CHECK(!strpbrk(wl_text(i), "0123456789"), "a number in line %d: %s", i, wl_text(i)); }
  /* a stream that is starting (not being chosen) takes humour only, never a line about choosing */
  { WlCtx c = ctx(WL_FIND, 12, 3); for (unsigned r = 0; r < 300; r++) { int p = pool_of_pick(c, r); CHECK(p != WLP_CHOOSE, "choosing lines while a chosen stream starts"); }
    c.step = WL_CHOOSE; int ch = 0; for (unsigned r = 0; r < 300; r++) if (pool_of_pick(c, r) == WLP_CHOOSE) ch++; CHECK(ch > 0, "choosing lines while choosing"); }
  /* the texts: 60 characters at most, none empty */
  for (int i = 0; i < WL_START[WLP_OLD]; i++) { int n = 0; for (const unsigned char *p = (const unsigned char *)wl_text(i); *p; p++) if ((*p & 0xc0) != 0x80) n++; CHECK(n > 0 && n <= 60, "line %d is %d characters", i, n); }
  printf(fails ? "%d failed\n" : "waitlines: all passed\n", fails); return fails != 0;
}
