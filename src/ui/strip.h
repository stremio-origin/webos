/* The maths of a line of choices that can be longer than its room (Discover's open pill): where it is scrolled to,
   which ends fade, how much a point fades. No drawing and no state: the board draws with it and tests/strip checks it
   (no empty space in the room, the focused choice whole and clear of the fades, the scroll never past either end). */
#ifndef SB_STRIP_H
#define SB_STRIP_H

#define STRIP_ZONE 110.0f    /* how far a fade reaches into the room from an end with more beyond it */
#define STRIP_MARGIN 120.0f  /* the focused choice kept this far from an end that scrolls (clear of the fade) */

static inline float strip_clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
/* the furthest the line can scroll: none when it fits */
static inline float strip_max(float listW, float full) { return listW > full ? listW - full : 0; }
/* the scroll that shows the focused choice [l, l + w) whole and clear of the faded ends, moving as little as possible
   from cur; a choice too wide for that is shown from its start */
static inline float strip_target(float cur, float l, float w, float listW, float full) {
  float mx = strip_max(listW, full); if (mx <= 0) return 0;
  float want = cur, m = STRIP_MARGIN;
  if (w + 2 * m > full) m = (full - w) / 2 > 0 ? (full - w) / 2 : 0; /* (a wide one: as much margin as there is room for) */
  if (l - (l > 0 ? m : 0) < want) want = l - (l > 0 ? m : 0);       /* (the first one needs no margin before it) */
  if (l + w + (l + w < listW ? m : 0) > want + full) want = l + w + (l + w < listW ? m : 0) - full;
  if (w > full) want = l;                                            /* (wider than the room: its start) */
  return strip_clampf(want, 0, mx);
}
/* the scroll as drawn: whatever the glide is doing, never past an end (the room can shrink under it) */
static inline float strip_drawn(float sx, float listW, float full) { return strip_clampf(sx, 0, strip_max(listW, full)); }
/* whether an end fades: there is more of the line beyond it (room: as wide as it is drawn now) */
static inline int strip_more_before(float sx) { return sx > 0.5f; }
static inline int strip_more_after(float sx, float room, float listW) { return sx + room < listW - 0.5f; }
/* how much is seen at x (0..1) in a room [x0, x0 + room) whose ends fade where fl / fr say: a smooth ramp */
static inline float strip_fade_at(float x, float x0, float room, int fl, int fr) {
  float a = 1, z = STRIP_ZONE < room / 3 ? STRIP_ZONE : room / 3; if (z < 1) z = 1; /* (a room a few pixels wide, as it opens: still faded) */
  if (fl) { float t = strip_clampf((x - x0) / z, 0, 1); a *= t * t * (3 - 2 * t); }
  if (fr) { float t = strip_clampf((x0 + room - x) / z, 0, 1); a *= t * t * (3 - 2 * t); }
  return a;
}
#endif
