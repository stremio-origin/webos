/* Subtitles inside Matroska files, without reading the film: the head of the file (tracks,
   chapters), the Cues index, then one small Range per subtitle block nearest the playhead first
   (about 0.1 % of the file). Files without an index are scanned near the playhead, gently. One
   session per stream harvests every text track at once (muxers keep the same line in all
   languages side by side), so choosing a track shows its lines at once. Works over debrid links
   and the torrent engine's local stream alike. Calls are for the main thread; the reading runs on
   the session's own thread. */
#ifndef SB_MKVSUBS_H
#define SB_MKVSUBS_H
#include <stdint.h>
#include "core/net.h"
typedef struct MkvSession MkvSession;
enum { MKV_STARTING = 0, MKV_HARVESTING = 1, MKV_FALLBACK = -1 };
typedef struct { int n; char lang[16], name[128], codec[32]; int def, forced, sdh, text; } MkvTrack;   /* n = order among the file's subtitle tracks */
typedef struct { double s, e; const char *t; int an; float x, y; uint32_t color; int italic, bold, layer; } MkvLine; /* an 1-9 (0 = bottom centre), x/y in % of the frame (-1 = none), color 0 = the viewer's */
typedef struct { double s, e; char name[96]; } MkvChapter;          /* seconds; e -1 when unknown */
typedef struct { int n, num; char lang[16], name[128], codec[32]; int channels, def; } MkvAudio;  /* n = order among the file's audio tracks, num its track number */

MkvSession *mkv_open(const char *url, double startPos, int p2p);
MkvSession *mkv_open_keys(const char *url); /* another copy of a film: its keyframe index and video track only (seek previews from it) */
void mkv_close(MkvSession *s);                       /* the thread stops and frees itself */
int  mkv_state(MkvSession *s);
const char *mkv_error(MkvSession *s);
int  mkv_listed(MkvSession *s);                      /* 1 once the file's tracks are read (they can be listed and chosen) */
int  mkv_tracks(MkvSession *s, MkvTrack *out, int max);
int  mkv_audio_tracks(MkvSession *s, MkvAudio *out, int max);  /* once listed: codec (A_DTS, A_AC3...), language, channels */
int  mkv_video_codec(MkvSession *s, char *out, int cap);
int  mkv_dovi(MkvSession *s, int *el);               /* once listed: the first video track's Dolby Vision profile (0 none; *el: an enhancement layer), -1 not known yet */
int  mkv_has_index(MkvSession *s);
/* styled ASS: mkv_style keeps track n's events as the file has them (its lines read so far are read again); new ones
   come from mkv_events_since (cursor starts at 0; fn is called under the session's lock: quick), the script header from
   mkv_track_header (malloc'd, NULL none); the Attachments element (fonts) is at mkv_attachments_at (-1 none) */
void mkv_style(MkvSession *s, int n, int on);
int  mkv_events_since(MkvSession *s, int n, unsigned *cursor, void (*fn)(void *ud, const char *ev, double s, double e), void *ud);
char *mkv_track_header(MkvSession *s, int n);
long long mkv_attachments_at(MkvSession *s);                              /* once listed: 1 the file has Cues, 0 none; -1 not known */        /* once listed: the first video track's CodecID (V_MPEGH/ISO/HEVC...) */
void mkv_select(MkvSession *s, int n);               /* -1 = none; any time (kept until the index is read) */
void mkv_set_pos(MkvSession *s, double t, double bufferedAhead);   /* the player, every second or so (bufferedAhead -1 unknown) */
int  mkv_lines_at(MkvSession *s, int n, double t, MkvLine *out, int max, char *textBuf, int textCap); /* the lines showing at t */
int  mkv_lines_in(MkvSession *s, int n, double lo, double hi, MkvLine *out, int max, char *textBuf, int textCap); /* the lines between lo and hi: count of all, the first max copied */
int  mkv_key_near(MkvSession *s, double t, double tol, double *out); /* the video keyframe nearest t within tol (from the Cues): 1 and *out, else 0 */
int  mkv_key_before(MkvSession *s, double t, double tol, double *out); /* the keyframe at or before t within tol: 1 and *out, else 0 */
extern volatile int mkvWantCues; /* the keyframe index read even without text tracks (seek previews on) */
int  mkv_key_count(MkvSession *s);
/* seek previews (development: SB_PREVSPIKE) */
int  mkv_video_track(MkvSession *s, char *codec, int cap, unsigned char **priv, long *privLen); /* 1 frames usable as stored, 0 not, -1 no video yet */
int  mkv_key_read(MkvSession *s, NetRange *net, int k, long guess, long maxBytes, unsigned char **frame, long *flen, double *t, long *bytes, int *reqs);
int  mkv_key_time(MkvSession *s, int k, double *t);
const char *mkv_url(MkvSession *s);
void mkv_set_key_url(MkvSession *s, const char *url); /* the keyframe reads' own address (a torrent engine's preview read), NULL: the film's */
int  mkv_key_where(MkvSession *s, int k, long long *start, long *slack); /* 1: the keyframe's block is at start (+ up to slack): ask for it with others */
int  mkv_key_from(MkvSession *s, NetRange *net, int k, const unsigned char *b, long n, long maxBytes, unsigned char **frame, long *flen, long *bytes, int *reqs); /* the keyframe from bytes read at that start */
int  mkv_key_from_part(MkvSession *s, NetRange *net, int k, const unsigned char *b0, long n, long maxBytes, unsigned char **frame, long *flen, long *bytes, int *reqs); /* a batch's part from mkv_key_where's start (slack -1: its Cluster walked in those bytes) */
/* a keyframe with no place in its Cluster, read where it most likely lies (a batch asks for many at once): start says
   what to ask for (a->at, a->len; 0: not for this one), feed takes those bytes: 0 the frame, 1 ask again, < 0 failed
   (-5: not found this way, mkv_key_read then walks its Cluster); end frees what it holds */
typedef struct { int k, stage, tries, tc, num; long long at; long len; long long cl, dataAt, clEnd, clTs, minAt; double t, rate; long est, partLen; unsigned char *part; } MkvAim;
int  mkv_aim_start(MkvSession *s, int k, MkvAim *a);
int  mkv_aim_feed(MkvSession *s, MkvAim *a, const unsigned char *b, long n, long maxBytes, unsigned char **frame, long *flen);
void mkv_aim_end(MkvAim *a);
int  mkv_range(MkvSession *s, NetRange *net, long long start, long len, unsigned char **out, long *got); /* one plain ranged read of the film (NR_OK or why not) */
int  mkv_net_warm(MkvSession *s, NetRange *net);
void mkv_retain(MkvSession *s); void mkv_release(MkvSession *s); int mkv_dead(MkvSession *s);                    /* the video keyframes known (0: no index, or not read yet) */
int  mkv_chapters(MkvSession *s, MkvChapter *out, int max);        /* -1 not read yet, else the count */
int  mkv_track_status(MkvSession *s, int n);         /* track n: 1 its lines are here (or none near the playhead), 0 still reading, -1 it cannot be read */
#endif
