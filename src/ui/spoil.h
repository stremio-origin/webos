/* Spoilers: the stills and words of a series' episodes not watched yet, blurred (and their descriptions hidden) when
   the setting says so. The episode in progress and the next one to watch stay clear; films, posters and season art are
   never touched. A hidden episode is shown clear once asked (OK on it) until its page is left. */
#ifndef SB_SPOIL_H
#define SB_SPOIL_H
int  spoil_mode(void);                                 /* 0 off, 1 pictures blurred, 2 pictures blurred and descriptions hidden */
/* whether episode vid of a series is hidden: not watched, not the series' place, not nextVid (the next to watch, when
   the caller knows it), not shown clear on request */
int  spoil_hidden(const char *seriesId, const char *vid, int watched, const char *nextVid);
void spoil_reveal(const char *vid);                    /* OK on a hidden one: that one shown clear until spoil_forget (its blur fading off) */
void spoil_reveal_wait(const char *vid);               /* its clear still not in yet: the blur stays till it is */
void spoil_reveal_over(const char *vid, const char *src, float x, float y, float w, float h, float r, float mcx, float mcy, float d); /* over a still just shown clear: the blur fading off, the eye going */
void spoil_forget(void);                               /* the page is left: every episode hidden again */
/* a hidden still: a blurred copy of the picture (made once as it decodes) drawn up, soft and a little darker; the
   waiting glass while it comes. r: its corners */
int  spoil_blurable(const char *src);                  /* a still there to blur (else never shown hidden) */
void spoil_picture(const char *src, float x, float y, float w, float h, float r);
/* its mark: the crossed eye in a round glass of diameter d centred there; hint 0..1 opens it into OK to show and where the
   setting is, nudge 0..1 a small peek of the eye */
void spoil_mark(float cx, float cy, float d, float hint, float nudge);
#endif
