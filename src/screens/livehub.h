/* The live hub: every channel of the live addons in one place, as a TV's own guide. Opened from Home's live row (All
   channels) or over a channel playing (Up, or Channels in the controls). A preview at the top right (over a channel
   playing: that channel, its picture in the preview window or seen through it), the focused channel's logo, name and
   where it comes from beside it; tabs under them (All, Favorites, Recent, each addon catalog, A–Z) and a grid of channel
   tiles, each its logo on its own colours, a LIVE light breathing on it. Over a channel playing, resting on another
   for a moment tunes the preview to it; OK watches it full screen; the green key makes it a favourite (a star marks it); Back closes. */
#ifndef SB_LIVEHUB_H
#define SB_LIVEHUB_H
void hub_open(int inPlayer, const char *playingId);   /* inPlayer: drawn by the player (its picture under it) */
void hub_close(void);
int  hub_on(void);
int  hub_covers_page(void);                          /* from Home, fully in: opaque edge to edge (the page under it is not drawn) */
int  hub_key(int k);                                  /* the player's keys while it is open (1: taken) */
void hub_tick(float dt);
void hub_draw(void);
void hub_playing(const char *id);
int  hub_player_mode(void);                           /* opened over the picture (drawn by the player), not from Home */
int  hub_back(void);                                  /* Back: on the channels, up to the catalogs (1); else 0 (it closes) */
void hub_ok_up(void);                                 /* over the picture: OK let go (a tap watches; held, the channel's streams opened) */                     /* the channel the player now plays (the preview follows it) */
#endif
