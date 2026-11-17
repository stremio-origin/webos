/* Collections: the films of one series (TMDB's collections: Harry Potter, Dune...), and for TV shows (and films TMDB
   puts in none) the titles of one franchise (Wikidata's "media franchise": Breaking Bad, Better Call Saul, El Camino).
   Which one a title is in: TMDB for a film (its details name its collection), Wikidata for the rest, many titles in
   one question. A collection's titles: TMDB's parts (each one's IMDb id asked of TMDB, so the addons find its
   streams), or the franchise's films and shows from Wikidata; in the order they came out. Kept on disk (which one:
   30 days, none: 14, a collection: 7). Main thread only, never blocks: what is not known yet is asked and answered on
   a later frame. Wikidata refusing (busy, rate limited): asked again later, never taken as "none". */
#ifndef SB_COLL_H
#define SB_COLL_H
#include <stddef.h>

#define COLL_MAXPART 60
typedef struct { char id[16], type[8], name[160], poster[400]; int year, tmdb; } CollPart; /* id "" while its IMDb id is on its way (tmdb: TMDB's id, a film of a TMDB collection) */
typedef struct { char key[24], name[160], overview[1200], poster[400], backdrop[400]; CollPart part[COLL_MAXPART]; int n; } Coll;

/* the collection a title (its type and IMDb id) is in: 1 (its key, and its name when known), 0 not known yet
   (asked), -1 none */
int  coll_of(const char *type, const char *imdb, char *key, size_t cap, const char **name);
/* a collection's details by its key: NULL while they come (asked); a collection of fewer than two titles is none */
const Coll *coll_get(const char *key);
int  coll_known(const char *key);                   /* 1 ready, -1 not a collection (fewer than two titles), 0 coming (asked) */
int  coll_index(const Coll *c, const char *imdb);   /* a title's place in it, -1 when it is not there */
unsigned coll_version(void);
void coll_hint_tmdb(const char *imdb, int tmdb);     /* a film's TMDB id already known (its details): no look-up for it */                        /* bumps whenever something new is known */
#endif
