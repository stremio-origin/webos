/* What the side sheets and the title page share: the strong ease-out the page uses for sheets, pills and rings
   (cubic-bezier(.23,1,.32,1), close to a quintic ease-out), and the gliding accent highlight under the focus. */
#ifndef SB_SHEET_H
#define SB_SHEET_H
#include "ui/glass.h"
#include <math.h>
#include "core/anim.h"
#include "core/app.h"
#include "ui/gfx.h"

static inline float ease_quint(float x) { return x <= 0 ? 0 : x >= 1 ? 1 : 1 - powf(1 - x, 5); }
/* a value gliding on that curve */
typedef struct { float from, to, v, t, dur; int on; } QT;
static inline void qt_set(QT *q, float v) { q->from = q->to = q->v = v; q->on = 0; q->t = 0; }
static inline void qt_to(QT *q, float to, float dur) {
  if (q->to == to && (q->on || q->v == to)) return;
  if (dur <= 0) { qt_set(q, to); return; }
  q->from = q->v; q->to = to; q->t = 0; q->dur = dur * ANIM_SPEED; q->on = 1; }
static inline void qt_tick(QT *q, float dt) { if (!q->on) return; if (dt > 0.034f) dt = 0.034f; /* a slow frame (a panel's glass made as it opens) never skips the start of a motion */ q->t += dt; float x = q->t / q->dur; if (x >= 1) { q->v = q->to; q->on = 0; } else q->v = q->from + (q->to - q->from) * ease_apply(GLIDE_EASE, x); }

/* the accent highlight: appears in place (never glides in from a stale spot), fades in 0.16 s, then glides; OK gives
   it a small press (0.09 s down, back on the curve) */
typedef struct { QT x, y, w, h; Tween a; int shown; float pt; int pressing; } FPill;
static inline void fpill_to(FPill *p, float x, float y, float w, float h, float dur) {
  if (!p->shown) { qt_set(&p->x, x); qt_set(&p->y, y); qt_set(&p->w, w); qt_set(&p->h, h); p->shown = 1; tween_to(&p->a, 1, 0.16f, EASE_IN_OUT); return; }
  qt_to(&p->x, x, dur); qt_to(&p->y, y, dur); qt_to(&p->w, w, dur); qt_to(&p->h, h, dur); tween_to(&p->a, 1, 0.16f, EASE_IN_OUT); }
static inline void fpill_hide(FPill *p) { if (p->shown) { p->shown = 0; tween_to(&p->a, 0, 0.16f, EASE_IN_OUT); } }
static inline void fpill_snap(FPill *p) { p->shown = 0; tween_set(&p->a, 0); }
static inline void fpill_press(FPill *p) { p->pressing = 1; p->pt = 0; }
static inline void fpill_tick(FPill *p, float dt) { qt_tick(&p->x, dt); qt_tick(&p->y, dt); qt_tick(&p->w, dt); qt_tick(&p->h, dt); tween_tick(&p->a, dt); if (p->pressing) { p->pt += dt; if (p->pt > 0.25f) p->pressing = 0; } }
/* the press as a scale: down to k in 0.09 s, back in 0.16 s */
static inline float fpill_scale(const FPill *p, float k) {
  if (!p->pressing) return 1; float t = p->pt;
  return t < 0.09f ? 1 - (1 - k) * ease_quint(t / 0.09f) : k + (1 - k) * ease_quint((t - 0.09f) / 0.16f); }
static inline void fpill_draw(const FPill *p, float radius, float k) {
  if (p->a.v <= 0.001f) return; float s = fpill_scale(p, k), cx = p->x.v + p->w.v / 2, cy = p->y.v + p->h.v / 2;
  gfx_opacity_push(p->a.v); if (s != 1) gfx_scale_push(s, s, cx, cy);
  lglass_accent(p->x.v, p->y.v, p->w.v, p->h.v, radius < 0 ? p->h.v / 2 : radius, 1); /* (the lit glass, as everywhere) */
  if (s != 1) gfx_scale_pop(); gfx_opacity_pop(); }

/* the accent's soft halo (4 px outside) on the gliding highlight itself, so it never sits on the target while the
   highlight is still on its way there */
static inline void fpill_halo(const FPill *p, float radius, float k, uint32_t color) {
  if (p->a.v <= 0.001f) return; float s = fpill_scale(p, k), cx = p->x.v + p->w.v / 2, cy = p->y.v + p->h.v / 2;
  gfx_opacity_push(p->a.v); if (s != 1) gfx_scale_push(s, s, cx, cy);
  gfx_ring_out(p->x.v, p->y.v, p->w.v, p->h.v, radius < 0 ? p->h.v / 2 : radius, 4, color);
  if (s != 1) gfx_scale_pop(); gfx_opacity_pop(); }

/* a ring landing on its place: from 1.05 and 0.3 opacity to rest (the rings of the grids and the faces) */
static inline float ring_pop_k(float x) { return ease_quint(x); }
#endif
