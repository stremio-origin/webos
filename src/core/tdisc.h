/* Discover in order (TMDB's categories and genres first, the addons' catalogs after) */
#ifndef SB_TDISC_H
#define SB_TDISC_H
#include "core/stremio.h"
int  tdisc_on(void);                                   /* a TMDB key, and not a child's profile */
int  tdisc_cats(const char *type, CatalogDef *out, int max);
int  tdisc_genres(const CatalogDef *d, char (*out)[48], int max);
void tdisc_with_genre(CatalogDef *d, const char *genre);
#endif
