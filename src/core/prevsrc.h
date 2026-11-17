/* Seek previews from a lighter copy of the film: for a heavy film (a remux, 4K, HDR), another stream of the same
   title from the user's addons, light and SDR (a 1080p or 720p H.264 file, cached on debrid or direct), makes the
   seek bar's pictures: a tenth of the reads, a fraction of the decoding, and the right colours. It starts on the
   title page (after a few seconds there) or as the film opens, from the copy's keyframe index alone; the player uses
   it only once seekprev's check proves it the same cut (sp_verify), else the film's own keyframes as before. Never a
   torrent, never a file not yet in the debrid cloud, never anything downloaded to keep. */
#ifndef SB_PREVSRC_H
#define SB_PREVSRC_H
#include "core/mkvsubs.h"
void        ps_want(const char *type, const char *id, const char *vid, int durMin, const char *filmUrl, double filmSize); /* filmSize: bytes (0 unknown): a copy only when 2.5x lighter */ /* that title / episode (filmUrl: the film itself, never its own copy) */
void        ps_cancel(void);          /* everything stopped (its pictures kept) */
void        ps_tick(void);            /* once a frame */
MkvSession *ps_session(void);         /* the copy whose index is in (it makes pictures), else NULL */
int         ps_busy(void);            /* a copy still being found or opened */
void        ps_next(void);            /* the copy failed its check: the next one */
void        ps_hold(int playing);     /* a film plays with it (no idle stop) */
const char *ps_vid(void);
#endif
