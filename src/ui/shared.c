/* Shared elements across a hand-over: a title's logo or poster that is on the page you leave and on the page you
   open moves from where it was to where it goes (size and place), instead of vanishing with one page and being
   drawn again with the other. Pages name what they draw ("logo:tt123", "poster:tt123") through sx_image(); the
   last frame's places are kept, and when a hand-over starts (sx_begin: a page switch, the player opening or
   closing) the new page's copy flies from the old place. While it flies it is drawn after the page, outside the
   page's own fade and move, at full strength. */
#include "ui/shared.h"
#include "core/stremio.h"
#include "core/anim.h"
#include "core/app.h"
#include "ui/text.h"
#include "ui/widgets.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#define SXN 48
typedef struct { char key[SB_ID + 16]; float x, y, w, h; GLuint tex; int layer; } Mark; /* key: "poster:<id>" and the like */
static Mark cur[SXN], last[SXN], from[SXN]; static int ncur, nlast, nfrom;
static double t0; static int active, layer, pref, quick; /* layer: 0 the page, 1 the player (drawn over it); pref: the one the elements fly to */
typedef struct { GLuint tex; float x, y, w, h, r; uint32_t tint; float u0, v0, u1, v1; } Late;
static Late late[16]; static int nlate;
#define SX_DUR 0.2 /* the shortest move; a long one takes up to SX_DUR_MAX (sx_k) */
#define SX_DUR_MAX 0.38
#define SX_HOLD 0.5 /* it stays drawn over the page, at full strength, until the page's own fade-in is over: handing back earlier dimmed it and brought it back (a pulse) */

extern void gfx_xform_point(float *x, float *y);
static Mark *find(Mark *t, int n, const char *k) { Mark *any = NULL; for (int i = 0; i < n; i++) if (!strcmp(t[i].key, k)) { if (t[i].layer != pref) return &t[i]; if (!any) any = &t[i]; } return any; } /* from the other layer first (the page under the player, the player over the page) */
void sx_frame_begin(void) {
  memcpy(last, cur, sizeof cur); nlast = ncur; ncur = 0;
  if (active && app_now() - t0 > SX_HOLD) active = 0;
}
void sx_begin(int toLayer) {
  if (!ncur) return; /* (between frames: cur holds the whole of the last frame) */
  memcpy(from, cur, sizeof cur); nfrom = ncur; pref = toLayer; t0 = app_now(); active = 1; quick = 0;
}
void sx_begin_quick(int toLayer) { sx_begin(toLayer); quick = 1; }
void sx_layer(int l) { layer = l; }
int sx_flying(void) { return active; }
double sx_since(void) { return app_now() - t0; }
/* a page drawn once more as it leaves (its exit over the next page): it names nothing, and what flies from it to the
   next page is not drawn by it */
static int ghost;
void sx_ghost(int on) { ghost = on; }
static int ghost_hides(const char *key) { return key && *key && active && find(from, nfrom, key) != NULL; }
/* how far along a move is: a quick eased dash with a light trail, everywhere */
#define SX_QUICK 0.18
static float sx_k_at(const Mark *f, float x0, float y0, float w, float h, double t);
static float sx_k(const Mark *f, float x0, float y0, float w, float h) { return sx_k_at(f, x0, y0, w, h, app_now() - t0); }
static float sx_k_at(const Mark *f, float x0, float y0, float w, float h, double t) {
  if (quick) { float x = (float)(t / SX_QUICK); x = x < 0 ? 0 : x > 1 ? 1 : x; return ease_bezier(.25f, .8f, .3f, 1, x); } /* a quick ease-out */
  /* page to page: the same quick eased dash, a touch longer the farther it goes (0.2 s near, 0.28 s across the screen) */
  float dx = (x0 + w / 2) - (f->x + f->w / 2), dy = (y0 + h / 2) - (f->y + f->h / 2), d = sqrtf(dx * dx + dy * dy) / 1100.0f;
  float dur = 0.2f + 0.08f * (d > 1 ? 1 : d);
  float x = (float)(t / dur); x = x < 0 ? 0 : x > 1 ? 1 : x; return ease_bezier(.3f, .7f, .25f, 1, x); }
static int sx_image_(const char *key, GLuint tex, float x, float y, float w, float h, float radius, uint32_t tint, float u0, float v0, float u1, float v1, int fade);
int sx_image(const char *key, GLuint tex, float x, float y, float w, float h, float radius, uint32_t tint, float u0, float v0, float u1, float v1) { return sx_image_(key, tex, x, y, w, h, radius, tint, u0, v0, u1, v1, 0); }
/* the same, crossfading on the way: the old picture fades out over the second half of the move while the new one fades in where it lands (a poster into a cover of another shape) */
static float sstep(float a, float b, float x) { x = (x - a) / (b - a); x = x < 0 ? 0 : x > 1 ? 1 : x; return x * x * (3 - 2 * x); }
static int sx_image_(const char *key, GLuint tex, float x, float y, float w, float h, float radius, uint32_t tint, float u0, float v0, float u1, float v1, int fade) {
  if (ghost) { if (ghost_hides(key)) return 1; if (tex) gfx_image(tex, x, y, w, h, radius, tint, u0, v0, u1, v1); return 0; }
  /* where it is on the screen, through the page's offsets and scales */
  float x0 = x, y0 = y, x1 = x + w, y1 = y + h; gfx_xform_point(&x0, &y0); gfx_xform_point(&x1, &y1);
  if (ncur < SXN && key && *key) { Mark *m = &cur[ncur++]; snprintf(m->key, sizeof m->key, "%s", key); m->x = x0; m->y = y0; m->w = x1 - x0; m->h = y1 - y0; m->tex = tex; m->layer = layer; }
  Mark *f = active && key ? find(from, nfrom, key) : NULL;
  if (f && layer != pref) return 1; /* the copy on the other layer stays hidden while its twin flies */
  if (!f) { if (tex) gfx_image(tex, x, y, w, h, radius, tint, u0, v0, u1, v1); return 0; }
  float k = sx_k(f, x0, y0, x1 - x0, y1 - y0); GLuint t = fade ? f->tex : tex ? tex : f->tex;
  if (fade && tex) { uint32_t a = (uint32_t)(((tint >> 24) & 255) * sstep(0.3f, 0.8f, k) + 0.5f); gfx_image(tex, x, y, w, h, radius, (tint & 0xFFFFFFu) | (a << 24), u0, v0, u1, v1); } /* the new one, in place, fading up */
  if (!t || nlate >= 16) return 1;
  /* a quick dash leaves a light trail: four faint copies 11 ms apart behind it, along the same path (plain quads) */
  if (!fade && k < 0.98f && nlate <= 11) for (int g = 4; g >= 1; g--) { /* (every flight: the player's and page to page) */ float kg = sx_k_at(f, x0, y0, x1 - x0, y1 - y0, app_now() - t0 - 0.011 * g); if (k - kg < 0.006f) continue;
    Late *q = &late[nlate++]; q->tex = t; q->x = f->x + (x0 - f->x) * kg; q->y = f->y + (y0 - f->y) * kg; q->w = f->w + ((x1 - x0) - f->w) * kg; q->h = f->h + ((y1 - y0) - f->h) * kg;
    q->r = radius; q->tint = 0x00FFFFFFu | ((uint32_t)(g == 1 ? 96 : g == 2 ? 66 : g == 3 ? 40 : 20) << 24); /* four copies, fainter further back: a smooth smear */ q->u0 = u0; q->v0 = v0; q->u1 = u1; q->v1 = v1; }
  Late *l = &late[nlate++]; l->tex = t; l->x = f->x + (x0 - f->x) * k; l->y = f->y + (y0 - f->y) * k; l->w = f->w + ((x1 - x0) - f->w) * k; l->h = f->h + ((y1 - y0) - f->h) * k;
  l->r = radius; l->tint = fade ? 0x00FFFFFFu | ((uint32_t)(255 * (1 - sstep(0.4f, 0.95f, k)) + 0.5f) << 24) : tint | 0xFF000000u; l->u0 = u0; l->v0 = v0; l->u1 = u1; l->v1 = v1; /* in flight: at full strength, whatever the page's own fade */
  return 1;
}
static void flush_text(void); static void flush_meta(void);
void sx_flush(void) {
  flush_text(); flush_meta();
  for (int i = 0; i < nlate; i++) { Late *l = &late[i]; gfx_image(l->tex, l->x, l->y, l->w, l->h, l->r, l->tint, l->u0, l->v0, l->u1, l->v1); }
  nlate = 0;
}
int sx_arrived(const char *key) { return key && find(from, nfrom, key) != NULL && (active || app_now() - t0 < SX_DUR + 2); }
unsigned sx_from_tex(const char *key) { Mark *f = key ? find(from, nfrom, key) : NULL; return f ? f->tex : 0; }
/* a paragraph (a title's description): it moves from where it was on the last page to where it goes, drawn at the
   new page's size and width (what it wraps to there), after the page, at full strength while it moves */
typedef struct { char *s; float x, y, maxW, lh, tracking; int lines, px, weight, align; uint32_t color; } LateText;
static LateText lateT[4]; static int nlateT;
int sx_text_wrapped(const char *key, const char *utf8, float x, float y, float maxW, int maxLines, int px, int weight, uint32_t color, float tracking, int align, float lh) {
  if (!utf8 || !*utf8) return 0;
  if (ghost) { if (ghost_hides(key)) return text_wrap_count(utf8, maxW, maxLines, px, weight, tracking); return text_draw_wrapped(utf8, x, y, maxW, maxLines, px, weight, color, tracking, align, lh); }
  float bl = align == ALIGN_CENTER ? x - maxW / 2 : align == ALIGN_RIGHT ? x - maxW : x; /* the block's left edge, whatever the anchor */
  float x0 = bl, y0 = y, x1 = bl + maxW, y1 = y + (lh > 0 ? lh : px * 1.3f) * maxLines; gfx_xform_point(&x0, &y0); gfx_xform_point(&x1, &y1);
  if (ncur < SXN && key && *key) { Mark *m = &cur[ncur++]; snprintf(m->key, sizeof m->key, "%s", key); m->x = x0; m->y = y0; m->w = x1 - x0; m->h = y1 - y0; m->tex = 0; m->layer = layer; }
  Mark *f = active && key ? find(from, nfrom, key) : NULL;
  if (f && layer != pref) return text_wrap_count(utf8, maxW, maxLines, px, weight, tracking);
  if (!f || nlateT >= 4) return text_draw_wrapped(utf8, x, y, maxW, maxLines, px, weight, color, tracking, align, lh);
  float k = sx_k(f, x0, y0, x1 - x0, y1 - y0); LateText *t = &lateT[nlateT++]; free(t->s); t->s = strdup(utf8);
  /* the anchor x as the page gave it (left, centre or right edge): moved by as much as the block's left edge moves */
  float ax0 = x, ay0 = y; gfx_xform_point(&ax0, &ay0);
  t->x = ax0 + (f->x - x0) * (1 - k); t->y = ay0 + (f->y - y0) * (1 - k); t->maxW = maxW; t->lines = maxLines; t->px = px; t->weight = weight; t->color = color | 0xFF000000u; t->tracking = tracking; t->align = align; t->lh = lh;
  return text_wrap_count(utf8, maxW, maxLines, px, weight, tracking); /* the lines it takes, as text_draw_wrapped says */
}
static void flush_text(void) { for (int i = 0; i < nlateT; i++) { LateText *t = &lateT[i]; text_draw_wrapped(t->s, t->x, t->y, t->maxW, t->lines, t->px, t->weight, t->color, t->tracking, t->align, t->lh); } nlateT = 0; }
/* the line of facts under a title (year, runtime, IMDb...): moves across like the words */
typedef struct { MetaParts p; float x, y; uint32_t color; int align; } LateMeta;
static LateMeta lateM[3]; static int nlateM;
static void flush_meta(void) { for (int i = 0; i < nlateM; i++) meta_draw_p(&lateM[i].p, lateM[i].x, lateM[i].y, lateM[i].color, lateM[i].align); nlateM = 0; }
