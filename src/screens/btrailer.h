/* The background trailer behind the boards, drawn: bgtrailer.c decides when it plays, this plays it through
   the video backend and gives the look its numbers. When it starts, the backdrop pushes in 2.5% and fades off
   the video (1.2 s), which is framed at that same zoom, while the page melts (0.9 s); when it stops, the picture
   fades back over it (0.45 s), the video goes and the picture eases back to its own framing. */
#ifndef SB_BTRAILER_H
#define SB_BTRAILER_H
void  btr_init(void);
void  btr_tick(float dt);     /* once a frame, from the app */
void  btr_handover(void);     /* play pressed: the player's art comes over the trailer, then it is let go */
int   btr_handing(void);
void  btr_key(int arrow);     /* any key or pointer action (arrow: a move to another title) */
void  btr_release(void);      /* the player is about to load: the decoder is let go now */
int   btr_owns(void);         /* the decoder holds the trailer */
int   btr_under(void);        /* the video shows through: the window is cleared transparent, the backdrop over it */
int btr_playing(void);  /* a background trailer is up and playing (not on its way out) */
unsigned btr_hold_gen(void); /* a key stopped the trailer: the backdrop generation it stopped at (that one is not shown again), else 0 */
float btr_cover(void);        /* 1 = the picture covers the video, 0 = the video is uncovered */
float btr_zoom(void);         /* the backdrop's scale (1 .. 1.025), around (960, 400) */
float btr_melt(void);         /* 0..1: the page's dim while it plays */
void  btr_cast_hold(double secs); /* a phone is about to play here: no background trailer for that long (one playing gives way), until the player opens or a key is pressed */
#endif
