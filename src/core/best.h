/* "Play the best stream automatically" (autoBest): what this TV can show and decode (asked once, kept as
   tvCaps2), how fast the line is (measured once when the setting is switched on, kept as netMbps), one score
   per stream read from its own text, and the wait for the addons before the pick. Results come back on the
   main thread from best_tick(). */
#ifndef SB_BEST_H
#define SB_BEST_H
#include "core/stremio.h"

/* ---- what the TV can show and decode ---- */
typedef struct { int maxRes, hdr, dv, av1, dts, truehd; } TvCaps;   /* dts: 1 only once this TV was seen playing DTS */
const TvCaps *best_caps(void);         /* the kept answer, or NULL when never asked */
/* what playback taught about this TV, kept for good in tvCaps2: "dts" (its decoder plays DTS: an error 200 on a
   DTS track says no, a DTS track playing 3 s without one says yes), "hlsHevcOk" (HEVC inside the local HLS
   stream plays), "dv81Hls" (a profile 7 picture converted to 8.1 shows as Dolby Vision, not HDR10). -1 while not known */
int  best_learned(const char *what);
void best_learn(const char *what, int yes);
int  best_dts_mode(void);   /* Settings > Audio > DTS sound: 0 auto, 1 convert, 2 off (the TV plays it) */
/* asks once (two luna calls on a worker, then kept); cb on the main thread, at once when already known */
void best_caps_probe(void (*cb)(const TvCaps *c, void *ud), void *ud);

/* ---- the line ---- */
double best_net_mbps(void);            /* the last measured speed, 0 = never measured */
int  best_speed_busy(void);
double best_speed_live(void);          /* the running test's speed so far in Mbps (0 before it has one) */
double best_speed_since(void);         /* seconds since the running test began (-1 when none runs) */
/* three parallel downloads for about 7 s on workers; cb(ok, mbps) on the main thread. Does nothing (and never
   calls cb) while a test runs, as the helper's endpoint did */
void best_speed_run(void (*cb)(int ok, double mbps, void *ud), void *ud);
void best_speed_cancel(void);          /* stops the downloads; cb is not called */

int  best_on(void);                    /* the setting */

/* ---- the score (pure: no state, unit-testable) ---- */
typedef struct { const Stream *s; double sc, need, cap; int mute, nodec; char sum[192]; } BestRate;   /* mute: its text names only DTS / TrueHD and this TV can't decode it (converted: a soft preference); nodec: its picture won't decode here (AV1 without the decoder): left out while anything else can play */
/* 0 = the stream is left out (a cam, an uncached debrid file, a thin swarm, above the panel) */
int  best_rate(const Stream *s, const TvCaps *c, double mbps, double durMin, BestRate *out);
/* the pick among n streams: the panel's own resolution first, then lower ones, each only among streams the line
   carries, and in each the ones whose sound plays here before the DTS / TrueHD-only ones; NULL when nothing is left. c NULL = the kept caps (or a 1080p panel), mbps < 0 = the measured speed */
const Stream *best_pick(const Stream *const *list, int n, double durMin, const TvCaps *c, double mbps, BestRate *out);
/* the running time used for bitrates: the title's own, else 45 min an episode, 110 a film */
int  best_dur(const cJSON *meta, const char *type);
/* the text a stream is read from (name or addon, title, file name), as the player reads it */
void best_stream_info(const Stream *s, char *out, size_t cap);
/* the tags read from that text, as the player's info chips read them ("4K", "DV · HDR10", "EAC3 Atmos 5.1") */
const char *best_res_of(const char *label);
void best_hdr_of(const char *label, char *out, size_t cap);
void best_audio_of(const char *label, char *out, size_t cap);

/* what a list shows of a stream: facts ("4K  ·  DV HDR10  ·  DTS-HD MA 7.1  ·  EN RU"), meta ("38.2 GB  ·  Direct",
   "12.1 GB  ·  484 seeds"), the release's name (the file's, else the title's line most like one; may be empty), and
   the one thing that matters most on this TV (warn, may be empty; out: it won't play here at all). c NULL = the kept
   caps; mbps < 0 = the measured speed; durMin <= 0 = a film's */
typedef struct { char facts[128], meta[96], rel[256], warn[160]; int out; } BestFacts;
void best_facts(const Stream *s, const TvCaps *c, double mbps, double durMin, BestFacts *f);

/* ---- choosing for Play ---- */
/* Fetches the streams of type/vid and picks one: at once when every addon answered, else after up to 12 s
   (30 s while nothing usable came). status(text) gets the quiet line for the player ("Choosing the best stream
   · 12 found · waiting for 2 addons") and, at the end, the pick's summary. done(s, r) gets the pick, or s NULL
   when nothing suits this TV ("No stream suits this TV. Pick one from the list"); s and r live only during
   the call. Returns a token for best_choose_cancel. */
typedef void (*BestStatusCb)(const char *text, void *ud);
typedef void (*BestDoneCb)(const Stream *s, const BestRate *r, void *ud);
void best_set_langs(const char *audio, const char *subs); /* the user's languages: streams in them first, ones naming only others last */
unsigned best_choose(const char *type, const char *vid, int durMin, BestStatusCb status, BestDoneCb done, void *ud);
void best_choose_cancel(unsigned tok);
void best_skip(const char *url);       /* a stream that failed to play: left out of the next choices (until the app restarts) */

void best_tick(void);                  /* once a frame: hands back worker results, runs the waits */
#endif
