/* A line of status words that never jumps: it fades and slides a few px as it comes and goes, crossfades when its
   words change, and stays put when only its digits change (a counter ticking). The player's loading lines, signing
   in and the channel list draw their one line through it. */
#ifndef SB_XLINE_H
#define SB_XLINE_H
#include <stdint.h>
#include "core/waitlines.h"
typedef struct { char cur[400], old[400]; double at, blankAt; } XLine; /* (blankAt: the phone holds the words through a brief blank) */
void xline_draw(XLine *x, const char *t, float tx, float y, int px, uint32_t col, float maxW, int al);              /* weight 600 */
void xline_draw_w(XLine *x, const char *t, float tx, float y, int px, int weight, float spacing, uint32_t col, float maxW, int al);
/* the words for a waiting place: its real text, or a light loading line over it while it is calm (core/waitlines),
   with this device's clock, platform, make and language; the last 20 lines are shared by every place, in memory */
const char *xline_wait(WlSlot *s, unsigned gen, const char *real, int calm, WlStep step, WlAct act, int binge);
#endif
