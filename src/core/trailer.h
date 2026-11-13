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
  char url[2048];      /* best HLS variant or an MP4 at the requested quality */
  char alt[2048];      /* the MP4 to try when the HLS stream fails ("" = none) */
  char mime[32];       /* "application/x-mpegurl" or "video/mp4" */
  char name[128];      /* the clip's own name ("Official Trailer"); "Trailer" when unknown */
  int  runtime;        /* seconds, 0 = unknown */
  char source[8];      /* "imdb" or "youtube" */
  TrailerSub subs[TRAILER_MAX_SUBS]; int nsubs;
} Trailer;
/* t is NULL on failure (err: "timeout", "unavailable", ...); it lives only for the call */
typedef void (*TrailerCb)(const Trailer *t, const char *err, void *ud);

/* Explicit quality policy: normal playback keeps its best HLS/MP4 path; background playback
   uses a progressive MP4 without subtitle lookups to start smoothly on the TV. */
typedef enum { TRAILER_QUALITY_BEST = 0, TRAILER_QUALITY_LOW = 1, TRAILER_QUALITY_720P = 2 } TrailerQuality;
/* imdbId: "tt…" or NULL; ytIds: YouTube ids, comma separated, or NULL (then Cinemeta is asked for them).
   Quality 720p falls back to lower MP4 variants when unavailable. Returns a cancellation token. */
unsigned trailer_find(const char *imdbId, const char *ytIds, TrailerQuality quality, TrailerCb cb, void *ud);
/* the same from a title's metadata (meta may be NULL: then the id alone) */
unsigned trailer_find_meta(const cJSON *meta, const char *id, TrailerQuality quality, TrailerCb cb, void *ud);
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
