/* Seek previews: small pictures of the film while scrubbing, made here from its own keyframes (no service).
   Platform-neutral. The film's keyframes are known up front (the MKV reader's Cues); a fixed grid of slots over the
   film, each taking the keyframe nearest its time, filled by priority: what the finger is on (or the paused place) >
   around the playhead > ~48 spread over the film > the rest of the grid. One keyframe read at a time through one
   fetch function, decoded on a separate low-priority worker (queue of 2), kept as small JPEGs in memory; a few
   pictures around the finger kept ready to draw, one small texture updated in place. Nothing of it runs with the
   Settings switch off, for a live broadcast, or without the decoder library; every failure is silent (the bubble
   stays as it was). */
#ifndef SB_SEEKPREV_H
#define SB_SEEKPREV_H
#include "core/mkvsubs.h"

/* the one place a keyframe's bytes come from (k: its index in the Cues). 0 and *frame (malloc'd) or < 0. LG's
   reader-thread version replaces this one function. */
int sp_fetch_key(MkvSession *m, int k, long maxBytes, unsigned char **frame, long *flen, double *t, long *bytes);

/* the player, about once a second (main thread): what plays and how it goes. A new session opens the previews, NULL
   closes them. key: the title and episode (the cache's name, never the link); partial: a source whose far parts are
   not here yet (a torrent, a download still coming): only around the playhead is read */
void sp_player_tick(MkvSession *m, const char *key, int partial, int live, double pos, double dur, double ahead, int buffering, int paused);
void sp_close(void);

typedef struct { unsigned tex; int w, h; int exact; double t; float u1, v0, v1; int k; } SpPic; /* v0..v1: the picture's rows in the texture (its black bars left out) */ /* the texture holds w x h at its top left (u1, v1 its share) */
void   sp_focus(double t, int dense); /* the finger on t (dense: the slide across the picture, every keyframe near it); t < 0: lifted */
int    sp_get(double t, SpPic *out);
double sp_shown_t(void); /* the time of the picture last shown for the scrub (-1 none) */
int    sp_made(void);
void   sp_dev_log(void);    /* development: SB_PREV_LOG, the pictures' progress every 2 s */   /* how many pictures this film has so far */
int    sp_get_cell(double t, double reach, int focus, SpPic *out); /* the TV row: the picture nearest t within reach (< 0 anywhere), in a texture of its own */
int    sp_get_at(double t, int slot, int skip, SpPic *out); /* slot 1 the picture for t, 0 / 2 a neighbour on either side (never the picture skip) */
double sp_slot(void);                 /* seconds between two pictures of the film's grid */  /* main thread, never blocks, at most one small texture update: 1 a picture (exact: t's own keyframe) */
double sp_snap(double t);             /* where a scrub let go at t lands: the shown picture's keyframe, else at or before within 1.5 s, else t */
void   sp_trim(void);                 /* memory is short: the ready pictures dropped, the decoder closed */
void   sp_mem_level(int lvl);         /* the app's memory guard (main.c, every 2 s): 2 the TV nearly out, nothing decoded meanwhile */
int    sp_active(void);
int    sp_progress(void);
double sp_progress_f(void);
int    sp_done(void);       /* nothing more will be made in the background (all there is, or its reading limit) */ /* the same, finer (the bar), -1 none */
void   sp_prepare(MkvSession *m, const char *key); /* the next episode's pictures made ahead from its own session (NULL: stop) */
int    sp_prepared(void);
int    sp_make_file(const char *url, const char *key); /* a downloaded file's previews made now (a file:// address; key "id|vid|..."): 1 started */
int    sp_make_busy(void);            /* that job still running */             /* that is done (the pictures kept on the phone) */             /* the share of the film with its pictures, 0..100 (-1: no previews for it) */
int    sp_torrents(void);             /* the viewer's "Previews for torrents" (off unless switched on) */
int    sp_may_make(void);             /* previews can be made here (or the decoder is still being looked for): a film's index worth reading */
extern int (*sp_metered)(void);     /* a platform's "is this link metered" (NULL: never): no background fill there, smaller frames */               /* previews are being made for what plays */
/* another copy of the film making the pictures (core/prevsrc): started from its keyframe index alone, adopted when
   the player passes that session to sp_player_tick after sp_verify said it is the same cut */
void sp_source_start(MkvSession *m, const char *key); /* key: the title and episode, the copy's size and length */
void sp_source_stop(void);           /* its pictures kept on this device */
int  sp_source_on(MkvSession *m);    /* that copy (NULL: any) is making pictures */
int  sp_source_made(void);
void sp_source_offset(double off);   /* the copy's times + off = the film's */
void sp_source_idle(void);           /* nothing plays now: it reads freely */
int  sp_verify_start(MkvSession *film, MkvSession *copy); /* the same cut? on a thread of its own */
int  sp_verify_state(double *off);   /* 0 running, 1 the same (off), -1 not, -2 none */
void sp_verify_reset(void);
#endif
