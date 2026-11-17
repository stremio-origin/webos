/* Ratings and reviews of a title, a season or an episode, from every source the TV can reach at once: the IMDb
   rating its details carry, Rotten Tomatoes and Metacritic from Wikidata (no key), Trakt's rating, its spread and
   people's comments (the user's client id), TMDB's vote and its reviews (the user's key). Everything is asked on
   the net workers and lands on the main thread; what came is kept in memory for the session. */
#ifndef SB_REVIEWS_H
#define SB_REVIEWS_H
#define REV_MAXC 30     /* comments kept per title */
#define REV_TEXT 600    /* bytes of one comment */
typedef struct { char src[20], val[12]; int rank; } RevTile;   /* one source's score as it shows it ("8.0", "85%", "78") */
typedef struct { char user[40], from[12], text[REV_TEXT]; int days, score, likes, replies, spoiler; } RevComment; /* days: ago (-1 unknown); score 0 = none */
typedef struct {
  char key[192]; unsigned ver; int pending, asked;      /* ver bumps as answers land; pending: answers still to come */
  float score; int votes; char scoreSrc[12]; int rank; /* the main score: the source with a spread first (Trakt), then TMDB, then IMDb */
  int dist[10], hasDist;                               /* how many gave 1..10 */
  RevTile tiles[6]; int ntiles;
  RevComment *c; int nc;
  char poster[160], overview[1000];    /* poster, overview: a season's own (TMDB) */                                  /* a title's backdrop with no words on it (TMDB), when there is one */
} Reviews;
/* the entry for a title (season 0, episode 0), a season (episode 0) or an episode: fetched when first asked, then
   filled in as the sources answer (watch ver); never NULL */
const Reviews *reviews_get(const char *type, const char *id, int season, int episode);
#endif
