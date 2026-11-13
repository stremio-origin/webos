#ifndef SB_BACKDROP_QUALITY_H
#define SB_BACKDROP_QUALITY_H
#include <stddef.h>
/* Normalize known undersized TMDB backdrop URL variants to w1280.
   Preserves larger, height-based and non-TMDB URLs. */
void backdrop_tmdb_upgrade(char *url, size_t cap);
#endif
