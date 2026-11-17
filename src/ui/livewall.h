/* The live wall: the channels' own logos on a staggered grid, slowly panning. It dresses the All channels card
   (Home's round badge, the wide tile), Home's live hero and the live hub. It is baked once, when its logos are in,
   into one texture that fades in; every surface pans a window of it. */
#ifndef SB_LIVEWALL_H
#define SB_LIVEWALL_H
void livewall_card(float x, float y, float w, float h, float r, int focused); /* the All channels card, whole */
void livewall_backdrop(float alpha);                                          /* the grid over the whole screen */
void livewall_hero(float alpha);                                              /* Home's hero behind a live title */
float livewall_word(float x, float y, int px, float maxW, int align);             /* the All channels wordmark; its height */
#endif
