/* the streaming platforms (Netflix, Disney+, Max, Prime Video...): their icons in a row on Home, and a
   page for each with its own Top 10, news, best rated and genres, all asked of TMDB for this country. */
#ifndef SB_PLATFORMS_H
#define SB_PLATFORMS_H
#include "core/stremio.h"
typedef struct { int id; char name[96], logo[512]; } PlatformParams;
int  platforms_row(CatalogDef *out);              /* Home's row of platforms (0 without a TMDB key) */
int  platform_defs(CatalogDef *out, int max);
int  premieres_rows(CatalogDef *out, int max);     /* Home's premieres: films coming to streaming, new series */     /* the open platform's rows */
int  platform_open(const Item *it);               /* a "plat:" card: its page; 0 for any other */
int  platform_enter(const PlatformParams *p);     /* 1 when it is another platform than last time */
const PlatformParams *platform_current(void);
#endif
