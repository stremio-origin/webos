/* The background trailer's decisions: after 5 s without a key on Home, Discover or the Library with a card
   focused, the focused title's trailer (low quality, sound on) plays behind the art; any key stops it at
   once. This module decides when to start and stop; the boards draw it and the player's video backend
   plays it (one hardware decoder: bgt_release_now() before the player loads anything). */
#ifndef SB_BGTRAILER_H
#define SB_BGTRAILER_H
#include "core/stremio.h"

typedef enum {
  BGT_OFF = 0,   /* nothing */
  BGT_WAIT,      /* idle: looking the trailer up */
  BGT_LOAD,      /* loading behind the art (not shown yet) */
  BGT_SHOW,      /* playing: the art uncovers it, the boards dim */
  BGT_GONE       /* stopped and off screen; the decoder is let go in a pause of the remote */
} BgtState;

/* what the boards see, once a frame */
typedef struct {
  float idle;           /* seconds since the last key (app_idle()) */
  int   browsing;       /* Home, Discover or the Library is the screen */
  const Item *focused;  /* the card under the highlight, NULL when none (or the top bar has the focus) */
  int   blocked;        /* the top bar, a sheet or menu, the player or the splash is up */
  int   spotlight;      /* Discover's spotlight is showing: it moves on by itself */
  int   shedding;       /* frames run late (the pace stepped down): no trailer for now */
} BgtView;

typedef struct {
  void (*load)(const char *url, const char *mime, const char *titleId, void *ud); /* start playing it behind the art */
  void (*hide)(int now, void *ud);  /* pause it at once; now = 0: it was showing, the art fades back over it first (0.46 s) */
  void (*unload)(void *ud);         /* release the decoder */
  void *ud;
} BgtHooks;

void bgt_init(const BgtHooks *h);
void bgt_tick(const BgtView *v);    /* once a frame, from whatever screen is up */
void bgt_key(void);                 /* any key or pointer action: it stops at once */
void bgt_release_now(void);         /* the player is about to load: hidden and unloaded now, no fade */
void bgt_playing(void);             /* the backend shows its first frame */
void bgt_ended(void);               /* it ended (or failed for good) */
void bgt_failed(void);              /* it failed: before it shows, the next lower MP4 is tried (1080p -> 720p -> 480p); else as ended */
BgtState bgt_state(void);
#endif
