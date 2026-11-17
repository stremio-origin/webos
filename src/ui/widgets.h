/* The pieces every screen draws: cards, the floating focus copy, the focus ring, the sliding
   pill, glass panels, the meta line (year · runtime · IMDb). */
#ifndef SB_WIDGETS_H
#define SB_WIDGETS_H
#include <stdint.h>
#include "core/anim.h"
#include "core/stremio.h"
#include "ui/gl.h"

/* a glass panel: a dark base, a soft white sheen from the top, a 1px inner edge */
void glass_rrect(float x, float y, float w, float h, float r, uint32_t base, float sheenTop, float sheenBottom, float edge);

/* year · runtime · IMDb with the yellow badge, left/centre/right aligned at x; returns the width */
float meta_draw(const char *metaLine, const char *genres, const char *name, float x, float y, int px, uint32_t color, int align);
/* the same taken apart and measured once, when its words change (the parse and the measuring are not for every frame):
   screens keep one in their view and prepare it again when meta_stale() says the language moved */
typedef struct { char name[128], imdb[16], year[24], rt[32], rest[96], genres[128]; int px; float w; unsigned lg; } MetaParts;
void  meta_prep(MetaParts *p, const char *metaLine, const char *genres, const char *name, int px);
int   meta_stale(const MetaParts *p);
float meta_draw_p(const MetaParts *p, float x, float y, uint32_t color, int align);

/* a poster card (264x396) or a wide Continue Watching card (430x242). label: the title under the art (library rows) */
#define CARD_W 264
#define CARD_H 396
#define CARD_R 14
#define WIDE_W 430
#define WIDE_H 242
#define CARD_GAP 24
void card_draw(const Item *it, float x, float y, int wide, int label, int loadArt);
/* what a card shows that only changes with its data, resolved once per item (rows keep one beside each item): the
   picture's address (artOverride: a translated poster), the address to fetch ahead, the wide card's label and time left */
typedef struct { char art[512], pre[512], lbl[300], left[64]; float leftW; unsigned lg; } CardPrep;
void card_prep(CardPrep *cp, const Item *it, int wide, const char *artOverride);
int  card_draw_prep(const Item *it, const CardPrep *cp, float x, float y, float w, float h, int wide, int label, int loadArt);
/* the same at any size (the boards' rows: 300x450 and 500x281); returns 1 when the art is in, 0 while it waits, -1 when it failed */
int  card_draw_wh(const Item *it, float x, float y, float w, float h, int wide, int label, int loadArt);
/* the one waiting look, used for every picture or text still on its way: the app's surface with one soft light
   that sweeps the whole screen on one clock (neighbours glow as parts of the same light). Only the shape changes.
   All of it comes from the shared alpha pages: placeholders, their light and nearby text batch together. */
void ph_box(float x, float y, float w, float h, float r);             /* any rounded box */
void ph_circle(float cx, float cy, float rad);
void ph_text(float x, float y, float w, int n, int px, float lineH, int align); /* n lines of text on their way (bars; the last shorter) */
void ph_surface(float x, float y, float w, float h, float r);         /* the surface alone, still: nothing is coming */
/* the picture (its corners baked in) over the waiting look, which stays until the picture has faded in; returns 1 when drawn */
int  ph_picture(GLuint tex, float x, float y, float w, float h, float r, uint32_t tint);
void card_standin(const Item *it, float x, float y, float w, float h, float r, int wide, float foot); /* a title with no art to show: its backdrop (wide: its poster) or a colour of its name, the title over it (foot: room kept clear under it, for a tag) */
void card_draw_round(const Item *it, float x, float y, float d); /* a live channel's round badge (All channels: the one marked LIVE) */
int  ph_hold(void);   /* development (SB_PH_HOLD=1): pictures are held back, every placeholder stays */

/* the floating focus copy over a card: lifted 6px, 1.06 from its top edge, a 5px white ring, popping up on each move */
typedef struct { Timer1 pop; int which; } FocusPop;
void card_mark_pop(const char *id);   /* that title was just marked watched: its badge pops in on its card */
void focus_pop(FocusPop *f);                      /* restart the pop (a new card) */
void focus_tick(FocusPop *f, float dt);
/* the boards' floating copy at any size: the white 5px ring only, no shadow (as the page draws it) */

/* a focus ring around a box with corners of radius r: its inner edge on the box's outline (it reaches 1 px under
   the art's soft edge, so no seam shows between them at any scale), t wide outside */
/* whether a move should restart its ring's landing pop: not for held keys or quick successive presses (the pop
   starts dim and large: restarted on every step it hid the ring); *last = when the previous move came */
int  focus_pops(double *last);
float key_hint(float x, float y, int key, const char *label, uint32_t col, int alignRight); /* a remote key (K_OK, K_BACK, K_UP / K_DOWN, K_LEFT / K_RIGHT, K_RED, K_GREEN, K_YELLOW, K_BLUE) as on the remote, then its words; y the middle */
float key_hint_s(float x, float y, int key, const char *label, uint32_t col, int alignRight, float s); /* the same, every length times s (a screen drawn small) */
/* focus: the card or face with the focus grows a little, lifts off with a soft shadow, and a faint glare of glass
   crosses it once (with a click, if Interface sounds is on); the one it left settles back. One state per key (any
   stable number for the card: lift_key(owner, index)); a card no one draws for two frames is forgotten.
     lift_begin(key, focused, hold, x, y, w, h, r)   before the card's art (its scale, about the card's centre)
     lift_end(key, x, y, w, h, r)                     after it (the glass, then the scale goes) */
unsigned long long lift_key(const void *owner, int index);
float lift_begin(unsigned long long key, int focused, float hold, float x, float y, float w, float h, float r);
float lift_begin_z(unsigned long long key, int focused, float hold, float x, float y, float w, float h, float r, float zoom); /* the same, growing by `zoom` (0.06 by default) */
void  lift_fade(float a);   /* the shadow and glass at this strength until set back to 1 */
void  lift_end(unsigned long long key, float x, float y, float w, float h, float r);
void  lift_tick(float dt);  /* once a frame, from the app */

/* the hold-OK line: runs once around a rounded box, a few px outside its edge, over a faint track */
void hold_trace_draw(float x, float y, float w, float h, float r, float progress);
int  hold_trace_rect(float *x, float *y, float *w, float *h, float *r); /* the box it last ran around, as it sat on screen: what the hold opens grows out of it */

/* a focus ring that glides to its place and lands with a small pop (grids) */
typedef struct { Tween x, y; Timer1 pop; int shown; double moved; } Ring;
void ring_to(Ring *r, float x, float y, int instant);
void ring_hide(Ring *r);
void ring_tick(Ring *r, float dt);

/* one pill per row of choices that glides to the focused one */
typedef struct { Tween x, y, w, h, a; int shown; float dur; } Pill;
void pill_to(Pill *p, float x, float y, float w, float h);
void pill_hide(Pill *p);
void pill_tick(Pill *p, float dt);
float live_mark(float x, float y, float sc, int rtl, float *k3, int st); /* a channel's state: LIVE, faint (not known) or Off air; its top row's width */
#endif
