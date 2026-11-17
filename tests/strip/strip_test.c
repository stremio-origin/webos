/* cc -Isrc -o /tmp/strip_test tests/strip/strip_test.c -lm && /tmp/strip_test
   Checks ui/strip.h, the maths of an open pill's line of choices, the way the board uses it: thousands of lines (short,
   long, mixed, one choice, two hundred), rooms from narrow to wide, the remote moving along them one step at a time and
   jumping to the ends, the room shrinking while it glides, the line opening. At every frame:
   - no empty space in the room: the line covers it whenever it is at least as long, and never scrolls past an end;
   - once a glide has settled, the focused choice is whole in the room and clear of the faded ends (when there is room);
   - an end fades only where there is more beyond it: nothing there at its very edge, everything a zone inside;
   - a step that already shows the focused choice clearly does not scroll. */
#include "ui/strip.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define OPT_TW 520.0f
static unsigned rs = 12345; static float rnd(void) { rs = rs * 1103515245u + 12345u; return (float)((rs >> 8) & 0xffff) / 65535.0f; }
static int fails, checks; static char ctx[200];
#define CHECK(c, ...) do { checks++; if (!(c)) { if (fails < 25) { printf("FAIL %s: ", ctx); printf(__VA_ARGS__); printf("\n"); } fails++; } } while (0)

typedef struct { int n; float ow[200], ox[200], listW; } Line;
static void make(Line *L, int kind, int n) {
  L->n = n; L->listW = 0;
  for (int i = 0; i < n; i++) { float tw;
    switch (kind) { case 0: tw = 50 + rnd() * 110; break;            /* genres: Action, Sci-Fi, Documentary */
      case 1: tw = 150 + rnd() * 260; break;                       /* catalogs: Popular, Netflix Top 10 · Cinemeta */
      case 2: tw = 400 + rnd() * 700; break;                       /* very long names (cut at OPT_TW) */
      case 3: tw = rnd() < 0.2f ? 600 + rnd() * 500 : 30 + rnd() * 200; break; /* mixed */
      default: tw = 20 + rnd() * 30; }                              /* very short (2, HD, 4K) */
    L->ow[i] = (tw < OPT_TW ? tw : OPT_TW) + 36; L->ox[i] = L->listW; L->listW += L->ow[i] + (i + 1 < n ? 6 : 0); }
}
/* every frame: the room covered, the scroll in range, the fades right */
static void frame_checks(const Line *L, float sxd, float room) {
  float mx = strip_max(L->listW, room);
  CHECK(sxd >= -0.01f && sxd <= mx + 0.01f, "scroll %.1f out of [0, %.1f]", sxd, mx);
  if (L->listW >= room) CHECK(sxd + room <= L->listW + 0.01f, "empty space at the end: %.1f + %.1f > %.1f", sxd, room, L->listW);
  int fl = strip_more_before(sxd), fr = strip_more_after(sxd, room, L->listW);
  float z = STRIP_ZONE < room / 3 ? STRIP_ZONE : room / 3;
  if (fl) CHECK(strip_fade_at(0, 0, room, fl, 0) < 0.001f, "start fade not 0 at its edge"); else CHECK(strip_fade_at(0, 0, room, fl, fr) > 0.999f || fr, "start fades with nothing before");
  if (fr) CHECK(strip_fade_at(room, 0, room, 0, fr) < 0.001f, "end fade not 0 at its edge");
  if (fl || fr) { float prev = -1; for (float x = 0; x <= z; x += 1) { float a = strip_fade_at(x, 0, room, fl, 0); CHECK(a >= prev - 1e-5f, "start fade not rising at %.0f", x); prev = a; } }
  CHECK(fabsf(strip_fade_at(room / 2, 0, room, fl, fr) - 1) < 1e-4f || room < 2 * z + 1, "the middle fades (room %.0f)", room);
  /* nothing drawn is a gap: some choice covers every point of the room (6 px gaps between them aside) */
  if (L->listW >= room) for (float x = sxd; x < sxd + room; x += 7) { int cov = 0; for (int i = 0; i < L->n && !cov; i++) cov = x >= L->ox[i] - 6.5f && x <= L->ox[i] + L->ow[i] + 0.5f; CHECK(cov, "nothing at %.0f (scroll %.0f)", x - sxd, sxd); }
}
/* the glide: the board's 0.24 s ease toward the target, 60 frames a second; the room can change under it */
static float glide(const Line *L, float from, float to, float room0, float room1, int frames) {
  float sx = from;
  for (int f = 1; f <= frames; f++) { float t = f / (0.24f * 60); if (t > 1) t = 1; float e = 1 - (1 - t) * (1 - t) * (1 - t);
    float room = room0 + (room1 - room0) * (f / (float)frames); float full = L->listW < room ? L->listW : room;
    sx = from + (to - from) * e; frame_checks(L, strip_drawn(sx, L->listW, full), full); }
  return sx;
}
static void settled_checks(const Line *L, int idx, float sxd, float full) {
  float l = L->ox[idx] - sxd, w = L->ow[idx]; int fl = strip_more_before(sxd), fr = strip_more_after(sxd, full, L->listW);
  if (w <= full) CHECK(l >= -0.01f && l + w <= full + 0.01f, "focused %d cut: [%.1f, %.1f] in %.1f", idx, l, l + w, full);
  else CHECK(fabsf(l) < 0.01f, "focused %d wider than the room not shown from its start (%.1f)", idx, l);
  if (w + 2 * STRIP_MARGIN <= full && w <= full) { float a0 = strip_fade_at(l + 2, 0, full, fl, fr), a1 = strip_fade_at(l + w - 2, 0, full, fl, fr);
    CHECK(a0 > 0.98f && a1 > 0.98f, "focused %d in a fade (%.2f, %.2f; at %.0f..%.0f of %.0f)", idx, a0, a1, l, l + w, full); }
}
int main(void) {
  int lines = 0;
  for (int kind = 0; kind < 5; kind++) for (int n = 1; n <= 200; n += (n < 12 ? 1 : 13)) for (int rr = 0; rr < 12; rr++) {
    Line L; make(&L, kind, n); float roomMax = 160 + rr * 120, full = L.listW < roomMax ? L.listW : roomMax; lines++;
    snprintf(ctx, sizeof ctx, "kind %d, %d choices, line %.0f, room %.0f", kind, n, L.listW, full);
    /* it opens on the choice in use: the room growing from nothing, the line scrolled to it at once */
    int idx = (int)(rnd() * n); if (idx >= n) idx = n - 1;
    float sx = strip_target(0, L.ox[idx], L.ow[idx], L.listW, full);
    for (int f = 0; f <= 20; f++) { float k = f / 20.0f, room = full * k; if (room < 2) continue; frame_checks(&L, strip_drawn(sx, L.listW, full), room); }
    settled_checks(&L, idx, strip_drawn(sx, L.listW, full), full);
    /* along it: one step at a time each way, then jumps to the ends, a glide for each */
    int steps = n * 2 + 6;
    for (int s = 0; s < steps; s++) {
      int ni = s < n ? idx + 1 : idx - 1; if (s == n + n / 2) ni = 0; if (s == steps - 2) ni = n - 1; if (ni < 0) ni = 0; if (ni >= n) ni = n - 1;
      float cur = strip_drawn(sx, L.listW, full), to = strip_target(cur, L.ox[ni], L.ow[ni], L.listW, full);
      /* already whole and clear: no scroll */
      { float l = L.ox[ni] - cur, w = L.ow[ni]; int fl = strip_more_before(cur), fr = strip_more_after(cur, full, L.listW);
        if (l >= 0 && l + w <= full && w + 2 * STRIP_MARGIN <= full && strip_fade_at(l, 0, full, fl, fr) > 0.999f && strip_fade_at(l + w, 0, full, fl, fr) > 0.999f && l >= (L.ox[ni] > 0 ? STRIP_MARGIN : 0) - 0.01f && full - (l + w) >= (L.ox[ni] + w < L.listW ? STRIP_MARGIN : 0) - 0.01f)
          CHECK(fabsf(to - cur) < 0.01f, "scrolled %.1f for a choice already in view (%d)", to - cur, ni); }
      sx = glide(&L, cur, to, full, full, 20); idx = ni; settled_checks(&L, idx, strip_drawn(sx, L.listW, full), full); }
    /* the room growing under a glide (the pills next to it narrowing): the furthest scroll gets shorter under it */
    { int ni = n - 1; float cur = strip_drawn(sx, L.listW, full), to = strip_target(cur, L.ox[ni], L.ow[ni], L.listW, full); float g2 = full * (1.2f + 0.6f * rnd());
      glide(&L, cur, to, full, g2, 24); float full2 = L.listW < g2 ? L.listW : g2; sx = glide(&L, strip_drawn(to, L.listW, full2), strip_target(strip_drawn(to, L.listW, full2), L.ox[ni], L.ow[ni], L.listW, full2), g2, g2, 20);
      settled_checks(&L, ni, strip_drawn(sx, L.listW, full2), full2); sx = strip_drawn(sx, L.listW, full); }
    /* the room shrinking under a glide (the pills next to it changing size), then the glide retargeted */
    { float f2 = full * (0.55f + 0.4f * rnd()); if (f2 < 120) f2 = 120; int ni = n - 1; float cur = strip_drawn(sx, L.listW, full), to = strip_target(cur, L.ox[ni], L.ow[ni], L.listW, full);
      glide(&L, cur, to, full, f2, 24); float full2 = L.listW < f2 ? L.listW : f2; float to2 = strip_target(strip_drawn(to, L.listW, full2), L.ox[ni], L.ow[ni], L.listW, full2);
      sx = glide(&L, strip_drawn(to, L.listW, full2), to2, f2, f2, 20); settled_checks(&L, ni, strip_drawn(sx, L.listW, full2), full2); }
  }
  printf("%d lines, %d checks, %d failed\n", lines, checks, fails);
  return fails ? 1 : 0;
}
