/* Trakt scrobbling: what plays shows on the user's Trakt account (watching now, paused, watched), and their playback
   position is kept there. The TV is connected once with Trakt's device code (a short code entered on a phone or a
   computer at the address shown, no password on the TV); the tokens stay in the data dir (trakt.auth), never in
   the synced settings or the logs, and belong to the Stremio account signed in when it was connected (signing out,
   or another account, disconnects it), and are renewed in the background (Trakt's access lasts 7 days, each refresh
   token works once). The player says what plays every frame; this decides when Trakt is told (start after 10 s of
   steady playback, pause, a jump, stop at the end or the close; 80 % or more is watched on Trakt's side). Only
   films and episodes with an IMDb, TMDB or TVDB id; live broadcasts and trailers never. Nothing here blocks or
   shows anything during playback: a request that fails is tried again later, quietly. Main thread. */
#ifndef SB_SCROBBLE_H
#define SB_SCROBBLE_H
#include "core/net.h"

enum { TRAKT_OFF, TRAKT_LINKED, TRAKT_RELINK };   /* not connected; connected; Trakt signed this TV out */
void scrobble_init(void);              /* after store_init (reads trakt.auth, renews the access when due) */
int  scrobble_state(void);
const char *scrobble_user(void);       /* the Trakt username once known ("" before) */
int  scrobble_on(void);                /* connected and Settings › Trakt › Track what you watch on */
void scrobble_tick(void);              /* once a frame: the queue, the renewal */

/* connecting: the code first (cb(1, ud) once a code is there to show, cb(0, ud) when Trakt could not be reached),
   then scrobble_link_poll() while it is on screen: 1 = connected, 0 = still waiting, -1 = over (expired, refused) */
void scrobble_link_start(void (*cb)(int ok, void *ud), void *ud);
const char *scrobble_link_code(void);  /* the code to enter */
const char *scrobble_link_url(void);   /* where to enter it */
int  scrobble_link_poll(void);
const char *scrobble_link_error(void); /* why it is over, already translated ("" while it is not) */
void scrobble_link_cancel(void);
/* the account, for its card: username, name, VIP, joined (YYYY-MM-DD), films and episodes watched, minutes in all;
   haveUser / haveStats once each has come. scrobble_profile_ask() when the card shows (every 10 minutes at most) */
typedef struct { char user[96], name[96], joined[16]; int vip, movies, episodes, haveUser, haveStats; long minutes; } TraktProfile;
const TraktProfile *scrobble_profile(void);
void scrobble_profile_ask(void);
void scrobble_unlink(void);            /* disconnect: the access is revoked on Trakt and the tokens deleted */
/* a GET of the account's own data at API path ("/sync/history/movies?limit=100"), with the token: 0 when not connected */
int  scrobble_get(const char *path, NetCb cb, void *ud);
/* a POST to the account at API path with a JSON body (taken), with the token: 0 when not connected */
struct cJSON;
int  scrobble_post(const char *path, struct cJSON *body, NetCb cb, void *ud);
int  scrobble_delete(const char *path, NetCb cb, void *ud); /* a DELETE at API path, with the token: 0 when not connected */

/* the player, every frame it is open: what plays (type "movie" / "series", the title's id, season and episode of an
   episode, else -1), the state (0 loading or buffering, 1 playing, 2 paused by the viewer), the position and length
   in seconds; scrobble_player_end when it closes or the item ends (ended: it played to its end) */
typedef struct { const char *type, *id, *title, *year; int season, episode; } ScrobbleItem;
void scrobble_player(const ScrobbleItem *it, int state, double pos, double dur);
void scrobble_player_end(double pos, double dur, int ended);
#endif
