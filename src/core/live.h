/* Live TV: channels and broadcasts from the addons whose catalogs are of type "tv" or "channel".
   - The channels: every live catalog's list, asked on its own (cached), for Home's Live now band, the All channels
     page and switching channels in the player.
   - Starting one: its streams are asked for and tried in the addons' order, a few seconds at most, quietly; the first
     that really sends a picture plays. None does: the viewer is told, nothing opens.
   - Recent and pinned channels, kept on this TV; the band shows them first.
   - The line-up: the channels the one playing came from, Up / Down in the player goes along it. */
#ifndef SB_LIVE_H
#define SB_LIVE_H
#include "core/stremio.h"

#define LIVE_ALL_ID "__allchannels" /* the band's first card: opens All channels */
#define LIVE_TOAST "\002live" /* a toast key: a red LIVE chip before the words */

/* a channel as the catalogs keep it (small: there can be thousands); live_item makes it a card */
typedef struct { char id[SB_ID], type[16], name[96], logo[300], poster[300]; } LiveCh;
void live_item(const LiveCh *c, Item *out);
extern void (*live_fail_hook)(const char *id, const char *code, int back); /* a channel started that will not play (code "offair": known off air, not tried; "nostream": none of its streams sent a picture; back: the channel before plays on), for a phone casting; NULL: no one told */
extern void (*live_remote_play)(const Item *it); /* a phone casting: a channel started is handed to the TV (it finds its stream there and opens its channel player); NULL on a TV */
extern int (*live_remote_on)(void); /* a phone casting (set by the phone; NULL on a TV): a channel started plays on the TV, so no player art here and the preview stays silent */

int  live_type(const char *type);            /* "tv" or "channel" */
void live_refresh(int force);                /* the live catalogs asked again (at most every 10 minutes unless forced) */
int  live_catalogs(void);                    /* how many live catalogs the installed addons have */
int  live_catalog(int i, const char **title, const LiveCh **ch); /* one of them as it stands: its channel count (-1: no such catalog) */

/* starting a channel: from (the list it was picked in) becomes the line-up (NULL: the one kept, else the band, else
   its catalog); zap: from the player (the picture playing stays until the new one is found) */
void live_start(const Item *it, const Item *from, int nfrom, int zap);
void live_start_list(const LiveCh *const *list, int n, int idx, int zap); /* one of a list of channels (the hub's tab, its line-up) */
void live_zap(int dir);                      /* the next / previous channel of the line-up */
int  live_back(void);                        /* the channel played before this one, started; 0 when none */
void live_tick(void);                        /* every frame (the main thread) */
void live_cancel(void);
int  live_failover(const char *id);           /* the channel's stream failed in the player: its next working one plays (0: none left) */
/* Home's preview of a focused channel: its stream looked for quietly (OK then opens it at once) */
void live_peek(const Item *it);
int  live_peek_stream(const char *id, Stream *out);
void live_peek_set(const char *id, const Stream *s); /* the preview has that stream playing again (back from the player) */ /* 1 found (out), 0 looking or not this one, -1 none works */
void live_peek_stop(void);
int  live_reach(const char *id);
void live_reach_set(const char *id, int st);  /* what a try found (the player: none of it played) */
void live_probe_visible(const char *const *ids, const char *const *types, int n); /* every frame: the channels on screen, tried in the background one after another */              /* a channel last tried: 1 it sends a picture, -1 nothing does, 0 not known */
const char *live_catalog_of(const char *id);  /* the title of the catalog the channel is in, NULL when unknown */
/* the corner card in the player: what is being tuned, or what was just tuned (shown for a few seconds) */
typedef struct { int on; const Item *it; int tuning, failed, backHint; double at; } LiveCard;
const LiveCard *live_card(void);
int  live_lineup_n(void);
const LiveCh *live_lineup_item(int i);
int  live_lineup_cur(void);

/* recent and pinned */
int  live_used(void);                        /* pinned channels, or one played in the last 30 days */
int  live_pinned(const char *id);
void live_pin(const Item *it, int on);
void live_forget(const char *id);            /* out of the recent ones (and unpinned) */
int  live_band(ItemList *out);               /* Home's live row: All channels, the pinned, a few more; 0 with no live addon */
int  live_pins(ItemList *out);               /* the pinned channels, newest pin first */
int  live_recent(ItemList *out);             /* the channels played in the last 30 days, latest first */
#endif
