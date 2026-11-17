/* What the connected Trakt account has watched, shown on top of the Stremio library (Settings > Trakt > Watched marks
   from Trakt): a film or an episode watched on Trakt (anywhere: another app, another device) shows as watched here.
   Never written into the library, so the official apps and other devices see the library as it is.

   Kept small (an IMDb id and a time a film; a show's id and its episodes as season and number), on disk between
   launches, and asked again only when Trakt says it changed (its last activities: at start, every 15 minutes, a few
   seconds after a film is closed); a big answer is read on a thread of its own.

   By hand: marking a film or episodes watched adds to Trakt's history only what Trakt does not have yet (Trakt keeps
   every play: adding again would count it twice); unmarking never deletes Trakt's history (that removes every play,
   for good): the mark is only hidden here, until Trakt has a newer play of it. Main thread. */
#ifndef SB_TRAKTWATCH_H
#define SB_TRAKTWATCH_H
int      tw_on(void);                 /* connected and the setting on: the marks show */
unsigned tw_ver(void);                /* bumps when what shows changes (a fetch, a mark hidden or shown again) */
void     tw_tick(void);               /* once a frame: the checks */
/* watched on Trakt and not hidden here (0 when tw_on is off); ids are IMDb ("tt..."), others are never on Trakt here */
int      tw_movie(const char *id);
int      tw_show_known(const char *id);   /* any episode of it watched on Trakt */
int      tw_ep(const char *showId, int season, int episode);
/* by hand (tw_on and tracking on): a film, or a show's episodes (season, number pairs), watched or not */
void     tw_mark_movie(const char *id, int on);
void     tw_mark_eps(const char *showId, const int *se, int n, int on);
/* Continue Watching from Trakt (Settings > Trakt): the account's paused positions (another app, another device), one
   entry a title shown with the library's, the newer position winning; dismissed here: deleted on Trakt too */
typedef struct { long long pid; char id[16], vid[48], type[8], title[128]; float pct; long long at; } TwPlay; /* at: when paused (s); pct 0..1; vid: an episode's "tt..:s:e", a film's own id */
int      tw_cw_on(void);              /* connected and the setting on */
int      tw_plays(const TwPlay **out); /* newest first (0 when off) */
const TwPlay *tw_play_of(const char *id); /* a title's newest, NULL none */
void     tw_play_drop(const char *id);  /* dismissed: each of the title's entries deleted on Trakt, gone here at once */
void     tw_forget(void);             /* another connection: the account's marks and hidden ones go */
#endif
