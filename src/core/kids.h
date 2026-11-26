/* kids profiles. A profile marked for a child sees only children's titles in every tab, Discover and
   Search (TMDB's lists with an age limit; Cinemeta's family and animation ones without a TMDB key). */
#ifndef SB_KIDS_H
#define SB_KIDS_H
#include "core/stremio.h"
int  kids_on(void);                     /* the profile watching is a child's */
int  kids_url(const char *url);         /* a row of ours ("tmdbk:...") */
int  kids_defs(int kind, CatalogDef *out, int max);   /* a tab's rows: 0 Home, 1 Discover, 2 Films, 3 Series, 4 Anime */
int  kids_disc_cats(const char *type, CatalogDef *out, int max);
int  kids_search_defs(const char *q, CatalogDef *out, int max);
void kids_feat(char *movieUrl, size_t mc, char *seriesUrl, size_t sc);  /* Discover's spotlight */
void kids_fetch(const char *url, const char *type, int n, void (*cb)(ItemList *, int, void *), void *ud);
void kids_page(const char *url, const char *type, int skip, void (*cb)(ItemList *, int, void *), void *ud);
int  foryou_defs(int kind, CatalogDef *out, int max);  /* For you (and Because you watched X on Home): 0 Home, 2 Films, 3 Series */
int  kids_open(const Item *it);         /* a "tmdb:" title: its IMDb id asked, then its page; 0 for any other */
#endif
