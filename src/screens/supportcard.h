/* The support card: a thank-you with the project's two codes, on Home every 5th start (see supportcard.c) */
#ifndef SB_SUPPORTCARD_H
#define SB_SUPPORTCARD_H
void supportcard_start(int autoReload); /* once, at start: counts a real start */
void supportcard_tick(void);            /* every frame: shows it when due and Home is still */
int  supportcard_open(void);
int  supportcard_covers(void);      /* fully in: the page under it is hidden (not drawn) */
int  supportcard_fit_check(const char *assetDir); /* development: every language measured against the card; returns how many don't fit */
#endif
