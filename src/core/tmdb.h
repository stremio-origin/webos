/* Titles, descriptions, genres, logos, posters and episode names in the app's language, from TMDB with
   the user's own key (added from the phone: the TV keyboard is no place for 32 characters). Cinemeta is
   English only. One find + one details call per title (+ one per season for a series' page), kept 3 days
   on disk so a title costs nothing the second time. What TMDB has no translation for stays empty: the
   English stays. English UI = no lookups.

   Everything here is for the main thread and never blocks: a lookup that is not known yet is queued for
   the workers and answered by a later frame (draw code reads tmdb_loc() every frame; nothing here does
   disk or network work on the caller's thread). Pointers handed out stay valid until the next frame. */
#ifndef SB_TMDB_H
#define SB_TMDB_H
#include "cJSON.h"

typedef struct { int s, e; const char *name, *overview; } TmdbEp;
typedef struct {
  const char *name, *overview, *logo, *poster;   /* "" when TMDB has none in the app's language */
  const char *genres[8]; int ngenres;             /* TMDB's genre names, in the app's language */
  int light;                                      /* a row's lookup: no episode list (the page asks again) */
  const TmdbEp *eps; int neps;                    /* sorted by season, episode; TMDB's stand-in names left out */
} TmdbLoc;

enum { TMDB_OFF = 0, TMDB_PENDING, TMDB_READY, TMDB_NONE };
/* how long a screen holds its English art back for the translation, as the page does */
#define TMDB_WAIT_CARD 0.7f   /* a card near the cursor: its translated poster */
#define TMDB_WAIT_HERO 0.7f   /* the hero */
#define TMDB_WAIT_PAGE 1.5f   /* a title page */

void tmdb_init(void);                 /* after store_init and i18n_init; otherwise done on first use */
int  tmdb_has_key(void);
const char *tmdb_orig_lang(const char *imdbId); /* the title's original language ("ja"; "tt123:1:2" takes the show's): "" unknown or no key, NULL while asked (asked once, kept in the store) */
int  tmdb_call_any(const char *path, char *url, size_t ucap, char *hdr, size_t hcap); /* the same from any thread */
int  tmdb_call(const char *path, char *url, size_t ucap, char *hdr, size_t hcap); /* (a path such as "/collection/10?language=fr"): its URL and header; 0 with no key */
int  tmdb_builtin(void);            /* this build carries the app's own key: nobody needs to add one */
void tmdb_key_clear(void);            /* "Remove the TMDB key" */
/* changes whenever lookups start again (a key added or removed, another language): screens drop what they
   applied */
unsigned tmdb_generation(void);
int  tmdb_wanted(const char *id);     /* the UI is not English, there is a key, and it is an IMDb id */

/* the title in the app's language: NULL while unknown (then it is asked for), when TMDB has nothing for it
   or when lookups are off. light = 1 for rows (no episodes); 0 for a title page (a series' episodes too). */
const TmdbLoc *tmdb_loc(const char *type, const char *id, int light);
int  tmdb_state(const char *type, const char *id, int light);   /* TMDB_OFF / PENDING / READY / NONE; asks too */
void tmdb_first(const char *type, const char *id, int light);   /* the focused title goes ahead of the queue */

/* the wait pattern: a screen keeps one TmdbWait per thing it holds back (the focused card, the hero, the page).
   1 = go ahead (the translation is in, there is none, lookups are off, or maxSec went by since this id was
   first asked here); 0 = keep waiting (draw the placeholder). A new id restarts the clock and goes first. */
typedef struct { char id[24]; int light; double at; } TmdbWait;
int  tmdb_wait(TmdbWait *w, const char *type, const char *id, int light, float maxSec);

/* one episode's name / overview ("" when there is none); 0 when the episode is unknown */
int  tmdb_episode(const TmdbLoc *L, int season, int episode, const char **name, const char **overview);
/* applies a lookup to a meta (once per language, idempotent): description, genres (the English kept in
   "_genresEn"), episode titles and overviews in place; the name, logo and poster beside the English ones
   ("locName", "locLogo", "locPoster") because the library takes its name and poster from the meta */
void tmdb_apply_meta(cJSON *meta, const TmdbLoc *L);

/* for the other TMDB users (the age rating): a GET with the user's key, the answer on the main thread.
   0 when there is no key. path: "/movie/123/release_dates" */
#include "core/net.h"
unsigned tmdb_request(const char *path, NetCb cb, void *ud);
#endif
