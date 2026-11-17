#ifndef SB_WAITLINES_H
#define SB_WAITLINES_H
/* Loading lines: a light line over a calm loading text (starting a stream, a live channel, signing in, the channel
   list), never in place of real information. The pools are constant tables (waitlines_pools.h, from the approved
   texts); the pick is pure (the caller gives the moment, the clock and the dice); a slot keeps one place's timing:
   the first line once the real text has stayed the same 4 s, each shown 3.5 s, at most 3 a wait (the 2nd at least
   5 s after the 1st went, the 3rd 6 s after the 2nd), back to the real text at once when it changes. No drawing, no
   storage, no network: the last 20 lines are kept in memory only. */

typedef enum { WL_SIGNIN, WL_FIND, WL_CHOOSE, WL_PEERS, WL_FILES, WL_PIECES, WL_LIVE } WlStep; /* what the wait is: FIND a chosen stream (or a channel) starting, CHOOSE the best stream really being chosen, LIVE the channel list */
typedef enum { WL_ACT_NONE, WL_ACT_FILM, WL_ACT_RESUME, WL_ACT_FIRSTTODAY } WlAct;          /* what the viewer is doing */
typedef enum { WL_PLAT_OTHER, WL_PLAT_LG, WL_PLAT_SAMSUNG, WL_PLAT_ATV, WL_PLAT_PHONE } WlPlat;

typedef struct {
  WlStep step; WlAct act;
  int binge;          /* episodes started in a row in this run of the app (2 or more: a binge) */
  WlPlat plat; const char *brand; /* brand: plat_wait_brand() ("OLED", "QNED", "NanoCell"; "Shield", "Sony", "Chromecast", "Fire TV", "Xiaomi", "Philips", "TCL"), "" none */
  int hour, wday;     /* the device's local hour 0-23 and weekday (0 Sunday .. 6 Saturday), never shown */
  int nth;            /* which line of this wait (0 the first) */
  unsigned rnd;       /* the dice */
} WlCtx;
typedef struct { short id[20]; int n, at; } WlHist;  /* the last 20 lines shown, in memory only */

int         wl_pick(const WlCtx *c, WlHist *h);   /* a line's id (kept in h); the pool the moment fits best */
const char *wl_text(int id);                      /* its English words (the key its translations use) */
int         wl_count(void);                       /* how many lines there are (ids 0 .. n-1) */
int         wl_pool_of(int id);                   /* its pool (tests) */
WlPlat      wl_platform(const char *deviceName);  /* from plat_device_name() */

/* one place's line: the real text, or a light line over it while the real text is calm. gen: the wait (a new value
   starts again), now: seconds, calm: the real text may be covered (no number, no addon, no failure), tr: the
   translator (T), english: the app is in English (another language takes a new line only once it is translated,
   else one of the old translated lines) */
typedef struct { unsigned gen; int shown, showing; double since, showAt, endAt; char last[400]; const char *line; } WlSlot;
const char *wl_slot(WlSlot *s, unsigned gen, double now, const char *real, int calm, WlCtx *c, WlHist *h, const char *(*tr)(const char *), int english);
int         wl_same_but_digits(const char *a, const char *b); /* "Finding peers… 4s" and "… 5s": the same text */

/* the platform's own: its make or family when it can be read exactly, "" otherwise (a weak default here) */
const char *plat_wait_brand(void);
#endif
