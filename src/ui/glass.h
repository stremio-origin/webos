/* The glass of the side sheets and pop-up cards: the part of the page a sheet covers, drawn once small and soft
   when it opens (the page as it shows there: backdrop, posters, words; a dark neutral tint, a faint accent at the edges, a light sheen at the top),
   then stretched over the sheet by the GPU. One small bake per opening, one textured quad per frame: no blur is
   ever drawn per frame. */
#ifndef SB_GLASS_H
#define SB_GLASS_H
#include "ui/gfx.h"
#include "core/anim.h"
typedef struct { GfxTarget t; int ok; Tween a; } SGlass;
/* bakes the page region (layout units) the sheet will cover; the glass then fades in over 0.3 s */
void sglass_bake(SGlass *g, float x, float y, float w, float h);
void sglass_tick(SGlass *g, float dt);
/* the sheet's surface: its opaque base and the glass over it, corners rounded by r */
void sglass_draw(const SGlass *g, float x, float y, float w, float h, float r);
void sglass_free(SGlass *g);
/* liquid glass, drawn: a see-through shell (body: its colour, 0 none; k 1 at rest, more when lit), the lit accent one,
   its rim alone, and a moving pill's stretch (0..1, kept in s and last) */
void  lglass_tint(float x, float y, float w, float h, float r, uint32_t col, float a); /* a selection's glass in any colour */
void  lglass_shell(float x, float y, float w, float h, float r, uint32_t body, float k);
void  lglass_accent(float x, float y, float w, float h, float r, float a);
void  lglass_rim(float x, float y, float w, float h, float r, float a0, float a1, int topLeft);
void  lglass_light(float at);  /* the light on the next pieces of glass, along their top: 0 the top-left corner (until set again) */
void  lglass_switch(float x, float y, float w, float h, float k, float c, int lit); /* a switch: knob at k, colours turned c, on a lit row */
float lglass_stretch(float *s, float *last, float x, float dt);
/* the bar's liquid glass over a picture `t` (uv of the box's part of it; t 0: the plain shell), its light wandering on
   its own timing (seed), dimmed by `dark` (the page's shading there, 0..1); small: the softer touch of a little pill */
void  lglass_soft(float k); /* the next lglass_liquid bends less (k of its usual bend; then back to 1) */
void  lglass_liquid(GLuint t, float x, float y, float w, float h, float r, float u0, float v0, float u1, float v1, float k, float dark, int seed, int small);
#endif
