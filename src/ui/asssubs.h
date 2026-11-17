/* Styled ASS / SSA subtitles (libass) for an embedded track of the film playing (its script header once, its events as
   the subtitle reader brings them, the fonts the file carries) or a whole subtitle file from any source. Drawn as tinted alpha pieces in the shared atlas. All
   rendering is on a thread of its own, made only when what shows changes (or at the picture's rate while an
   animation runs); the calls below are for the main thread and cost nothing while nothing is open. */
#ifndef SB_ASSSUBS_H
#define SB_ASSSUBS_H
#include "core/mkvsubs.h"
/* starts styled drawing of track n (url: the file, for its fonts; lang: the track's language, for the fallback font) */
void asssubs_open(MkvSession *mkv, int n, const char *url, const char *lang);
/* the same for a whole .ass / .ssa file (copied): its script info, styles and events; fonts: its own [Fonts], else the bundled ones */
void asssubs_open_script(const char *script, const char *lang);
void asssubs_close(void);                           /* before the reader is closed; the thread finishes on its own */
int  asssubs_on(void);
/* per frame while on: the subtitle time (seconds, on the script's own timeline: the viewer's delay and a sync applied),
   rate: script seconds per second of film (a sync's speed fix; 1 otherwise), whether it runs, the picture's rate and
   where it sits (layout units) with its own size; scale: the viewer's subtitle size (1 = 100 %) */
void asssubs_tick(double t, double rate, int playing, double fps, float vx, float vy, float vw, float vh, int videoW, int videoH, float scale);
void asssubs_draw(void);
void asssubs_sample(int on);  /* the style being set: with no line due, the file's nearest line shown in its own style */
int  asssubs_visible(void); /* a styled line is on screen now (none between lines) */
void asssubs_brightness(float k);   /* the subtitle brightness (0.4..1): the colours toward black */
void asssubs_set_line(double pct);                 /* raise the dialogue lines this % of the picture (0: where the script puts them) */
int  asssubs_too_slow(void);                        /* once: 1 renders stayed too slow, 2 the file could not be read: drawn plain from now on */
double asssubs_render_ms(void); /* the styled renders' running average (0 before the first) */
#endif
