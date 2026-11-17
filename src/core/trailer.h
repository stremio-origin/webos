/* Trailers: IMDb's own trailer videos found by IMDb id (its GraphQL API), YouTube as the fallback,
   captions from IMDb or from the YouTube cut of the same length. One resolver feeds the title page's
   Trailer button, the card menu and the background trailer behind the boards. Answers come back on
   the main thread; trailer_tick() runs the deadlines and must be called once a frame. */
#ifndef SB_TRAILER_H
#define SB_TRAILER_H
#include <stddef.h>
#include "cJSON.h"
#include "core/subs.h"

#define TRAILER_MAX_SUBS 12
typedef struct { char url[1536], lang[8], name[64], kind[12]; } TrailerSub;  /* kind: "srt" (IMDb) or "timedtext" (YouTube) */
typedef struct {
  char url[2048];      /* what to load: the best HLS variant, or an MP4 (low quality: the sharpest MP4, 1080p / 720p / 480p) */
  char alt[2048];      /* the MP4 to try when the HLS stream fails ("" = none) */
  char down[2][2048];  /* the background trailer: the lower MP4s to fall back to, in order (720p then 480p; "" = none) */
  char mime[32];       /* "application/x-mpegurl" or "video/mp4" */
  char name[128];      /* the clip's own name ("Official Trailer"); "Trailer" when unknown */
  int  runtime;        /* seconds, 0 = unknown */
  char source[8];      /* "imdb" or "youtube" */
  TrailerSub subs[TRAILER_MAX_SUBS]; int nsubs;
} Trailer;
/* t is NULL on failure (err: "timeout", "unavailable", ...); it lives only for the call */
typedef void (*TrailerCb)(const Trailer *t, const char *err, void *ud);

/* imdbId: "tt…" or NULL; ytIds: YouTube ids, comma separated, or NULL (then Cinemeta is asked for them).
   lowQuality: the background trailer (an MP4, the sharpest there is unless the connection was measured slow; no caption matching). Returns a token for trailer_cancel. */
unsigned trailer_find(const char *imdbId, const char *ytIds, int lowQuality, TrailerCb cb, void *ud);
/* the same from a title's metadata (meta may be NULL: then the id alone) */
unsigned trailer_find_meta(const cJSON *meta, const char *id, int lowQuality, TrailerCb cb, void *ud);
void     trailer_cancel(unsigned tok);   /* its callback never runs */
void     trailer_tick(void);             /* once a frame: deadlines and retries */

/* a Trailer button / menu entry is offered: an IMDb id or YouTube ids exist */
int  trailer_available(const cJSON *meta, const char *id);
/* the title's IMDb id ("" when none) and its YouTube ids (up to 5, comma separated) */
const char *trailer_imdb_id(const cJSON *meta, const char *id, char *out, size_t cap);
const char *trailer_yt_ids(const cJSON *meta, char *out, size_t cap);

/* a caption track fetched (YouTube's URLs want its app's user agent) and parsed; cues belong to the
   callback (subs_free); n = 0 on failure */
unsigned trailer_sub_load(const TrailerSub *s, void (*cb)(Cue *cues, int n, void *ud), void *ud);
#endif
