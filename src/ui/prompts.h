/* The player's prompts over the picture, drawn at any size: the next-up card and the skip pill. The player draws
   them at full size (s = 1); Settings draws them small over its preview of the player (s = its share of the screen),
   so what a setting will look like is the player's own drawing, never a copy of it. Every length, font size and
   corner is the full-size one times s: at s = 1 the drawing is the player's as it always was. */
#ifndef SB_PROMPTS_H
#define SB_PROMPTS_H
#include <stdint.h>

typedef struct {
  const char *label;   /* "Next episode" / "Next in the collection" (shown upper case, in the accent) */
  const char *key;     /* OK's words: "Next episode" / "Next film" */
  const char *se;      /* "S1 E2", or the film's part */
  const char *title;   /* the episode's name ("" none) */
  const char *ov;      /* its summary ("" none; never on the compact card) */
  const char *art;     /* its picture's address */
  float compact;       /* 0 the large card, 1 the compact one; between, the one becoming the other (Settings' preview) */
  float autoK;         /* < 0: no countdown; 0..1 the countdown's share gone (the bar under the picture) */
  int autoLeft;        /* the seconds the countdown still says */
} NextCardSpec;

/* the card, its right edge at `right` and its bottom at `bottom`; *x, *y, *w, *h (any may be NULL): where it went */
void ui_next_card(float right, float bottom, float s, const NextCardSpec *c, float *x, float *y, float *w, float *h);

/* the skip pill's width for its words and mark */
float ui_pill_width(float s, const char *label, const char *icon);
/* the pill at (x, y), w wide (ui_pill_width): lit = focused (the accent), fillK >= 0: counting down, the accent
   growing from its left end (0..1), nextK >= 0: Next episode's own countdown, a light fill (0..1) */
void ui_pill(float x, float y, float w, float s, const char *label, const char *icon, int lit, float fillK, float nextK);
float ui_pill_height(float s);
#endif
