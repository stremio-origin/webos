/* A title's people from TMDB: its whole cast with the part each plays (billing order), its directors and writers
   (a film's), each with a photo when TMDB has one. One find and one credits call per title, kept 3 days on disk.
   Main thread only; never blocks: a title not known yet is asked for and answered by a later frame. */
#ifndef SB_CREDITS_H
#define SB_CREDITS_H
enum { CRED_CAST = 0, CRED_DIRECTOR, CRED_WRITER };
typedef struct { char name[128], role[96], img[300]; int tid, kind; } Credit;   /* img: a TMDB photo address, "" none; tid: TMDB's person id */
/* the people of an IMDb title (type "movie" / "series"): their count when known (*out stays valid until the next call
   for another title), -1 while they are being asked for, 0 when TMDB has none (or there is no key) */
int  credits_get(const char *type, const char *imdbId, const Credit **out);
unsigned credits_gen(void);        /* moves when an answer comes in */
/* a TMDB person's IMDb id, asked for (cb on the main thread with "" when unknown) */
void credits_person_imdb(int tid, void (*cb)(const char *imdbId, void *ud), void *ud);
#endif
