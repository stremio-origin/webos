/* What stremio.c asks of the kids profiles, the platforms, For you and Discover's TMDB lists, answered as
   with no TMDB key and no child's profile: the profile tests need none of them. */
#include "core/stremio.h"
int kids_on(void) { return 0; }
int kids_url(const char *url) { (void)url; return 0; }
int kids_defs(int kind, CatalogDef *out, int max) { (void)kind; (void)out; (void)max; return 0; }
int kids_disc_cats(const char *type, CatalogDef *out, int max) { (void)type; (void)out; (void)max; return 0; }
int kids_search_defs(const char *q, CatalogDef *out, int max) { (void)q; (void)out; (void)max; return 0; }
void kids_fetch(const char *url, const char *type, int n, void (*cb)(ItemList *, int, void *), void *ud) { (void)url; (void)type; (void)n; (void)cb; (void)ud; }
void kids_page(const char *url, const char *type, int skip, void (*cb)(ItemList *, int, void *), void *ud) { (void)url; (void)type; (void)skip; (void)cb; (void)ud; }
int foryou_defs(int kind, CatalogDef *out, int max) { (void)kind; (void)out; (void)max; return 0; }
int platforms_row(CatalogDef *out) { (void)out; return 0; }
int premieres_rows(CatalogDef *out, int max) { (void)out; (void)max; return 0; }
int tdisc_on(void) { return 0; }
int tdisc_cats(const char *type, CatalogDef *out, int max) { (void)type; (void)out; (void)max; return 0; }
int tdisc_genres(const CatalogDef *d, char (*out)[48], int max) { (void)d; (void)out; (void)max; return 0; }
void tdisc_with_genre(CatalogDef *d, const char *genre) { (void)d; (void)genre; }
