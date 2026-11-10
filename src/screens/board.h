/* Home / Discover / Library: one engine (a hero above rows of cards) with three data sources. */
#ifndef SB_BOARD_H
#define SB_BOARD_H
#include "core/app.h"
extern Screen homeScreen, discScreen, libScreen, animeScreen;
int  board_home_ready(void);
void tabs_warm_tick(void);    /* after start: the other tabs' first screens made ready in the background */    /* the first rows' posters are in, the hero is set, the backdrop is up */
void board_invalidate(void);    /* the library changed: Home and Library rebuild when shown */
void board_rebuild(void);       /* the board on screen reloads its rows in place, focus kept */
int  board_spot_up(void);
int  board_sample(Item *out, int max, char *rowTitle, int cap); /* titles from Home's first poster rows (Discover's when Home has none) and the first one's title, for the guide's pictures */       /* Discover's spotlight has the focus (it moves on by itself: no background trailer) */
void board_playing_mark(float x, float y, float h, float a); /* a poster's "its trailer plays" mark, at a (the melt) */
#endif
