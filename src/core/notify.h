/* New episodes and the ones coming: for the library's series (not removed, not temporary, notifications not turned
   off for them, not a live or single-video title), newest first, at most 100, the addons whose catalogs take
   lastVideosIds / calendarVideosIds are asked every 6 hours. A series with an episode out since it was last watched
   (or never watched) comes into Continue Watching as "New episode"; Home's Upcoming row lists the ones airing in the
   next 14 days, soonest first. What came is kept on disk, so a start shows it at once; whether an episode is still
   new is decided when the row is made, against the series' last watch then (watching it, here or in another app,
   takes it out). Main thread. */
#ifndef SB_NOTIFY_H
#define SB_NOTIFY_H
#include "core/stremio.h"
#include "cJSON.h"

void notify_tick(void (*changed)(void));    /* once a frame: asks when due (signed in, the library there); changed() when the answers changed what shows */
void notify_refresh(void);                  /* ask now (a series just added, the library pulled) */
/* a library item's new episodes: the first not yet watched (*vid, its season and episode), when the newest came out;
   0 = none */
int  notify_new(const cJSON *libItem, char *vid, size_t cap, double *newestAt);
int  notify_upcoming(ItemList *out);        /* Upcoming: soonest first; 0 = nothing coming */
void notify_dismiss(const char *id);        /* seen: the series' last watch is now (as the official apps dismiss) */
void notify_cw_dismiss(const Item *it);    /* out of Continue Watching until it is watched or marked again, or a newer episode comes out (this device) */
int  notify_cw_hidden(const cJSON *lib, double newestNew); /* that dismissal still holds (newestNew: the newest new episode's air time, s; 0 none) */
void notify_up_dismiss(const Item *it);    /* out of Upcoming until its next episode is another one (this device) */
#endif
