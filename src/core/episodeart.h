#ifndef SB_EPISODEART_H
#define SB_EPISODEART_H
#include <stddef.h>
/* Raise only undersized TMDB episode stills to the requested width.
   Larger/original sources and non-TMDB addon artwork are left intact. */
void episode_art_upgrade_tmdb(char *url, size_t cap, unsigned min_width);
#endif
