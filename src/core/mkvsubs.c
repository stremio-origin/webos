#include "platform/plat.h"
#include "core/mkvsubs.h"
volatile int mkvWantCues; /* (set by the player before a film opens: its seek previews want the keyframe index even with no text track) */
#include "core/net.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <zlib.h>

/* ---- tuning (the same numbers as the web app's reader) ---- */
#define HEAD_BYTES (64 * 1024)          /* first read: SeekHead, Info and Tracks live here */
#define CUES_FIRST (256 * 1024)         /* first read of the Cues; a 2 h film's index is ~100-400 KB */
#define CUES_MAX (16 * 1024 * 1024)   /* (a remux with many picture-subtitle tracks passes 8 MB) */
#define BLOCK_BYTES 512                 /* a BlockGroup header + one line of dialogue */
#define MERGE_BYTES (256 * 1024)        /* blocks this close in one Cluster share one Range */
#define AHEAD_S 90.0
#define BEHIND_S 8.0
#define P2P_AHEAD_S 120.0               /* torrents: only what playback downloads anyway... */
#define P2P_MIN_AHEAD_S 12.0            /* ...and no further than the player has buffered (plus this), once it says */
#define READY_AHEAD_S 20.0              /* a chosen track counts as ready once the file is read this far past the playhead */
#define RATE 3                          /* Ranges a second in the background... */
#define RATE_URGENT 8                   /* ...and while the window around the playhead is missing */
#define FULL_TRACK_MAX 8000             /* above this many lines, only around the playhead */
#define KEEP_BEHIND_S 60.0              /* tracks not chosen keep only this much around the playhead */
#define KEEP_AHEAD_S 150.0
#define SEL_MAX 20000                   /* the chosen track keeps everything up to this many lines... */
#define SEL_BEHIND_S 600.0              /* ...then this window */
#define SEL_AHEAD_S 900.0
#define PRUNE_EVERY 15.0
#define SCANNED_MAX 3000
#define DEFAULT_DUR_S 3.0
#define SCAN_CHUNK (512 * 1024)
#define SCAN_CHUNK_URGENT (2 * 1024 * 1024) /* the playhead's own lines: a high-bitrate file holds only a fraction of a second in 512 KB */
#define SCAN_AHEAD_S 60.0
#define SCAN_RATE 2
#define SCAN_MIN_BUFFER_S 15.0
#define TIMEOUT_MS 15000
#define P2P_CHAPTERS_MAX (64LL * 1024 * 1024) /* torrents: Chapters stored further in would fetch pieces there */

enum { ID_EBML = 0x1A45DFA3, ID_SEGMENT = 0x18538067, ID_SEEKHEAD = 0x114D9B74, ID_SEEK = 0x4DBB, ID_SEEKID = 0x53AB, ID_SEEKPOS = 0x53AC,
  ID_INFO = 0x1549A966, ID_TSCALE = 0x2AD7B1, ID_DURATION = 0x4489, ID_TRACKS = 0x1654AE6B, ID_TRACKENTRY = 0xAE,
  ID_TRACKNUM = 0xD7, ID_TRACKTYPE = 0x83, ID_CODECID = 0x86, ID_CODECPRIV = 0x63A2, ID_LANG = 0x22B59C, ID_LANGBCP = 0x22B59D,
  ID_NAME = 0x536E, ID_FLAGDEFAULT = 0x88, ID_FLAGFORCED = 0x55AA, ID_FLAGHI = 0x55AB,
  ID_ENCODINGS = 0x6D80, ID_ENCODING = 0x6240, ID_COMPRESSION = 0x5034, ID_COMPALGO = 0x4254, ID_COMPSETTINGS = 0x4255, ID_ENCRYPTION = 0x5035,
  ID_CUES = 0x1C53BB6B, ID_CUEPOINT = 0xBB, ID_CUETIME = 0xB3, ID_CUETRACKPOS = 0xB7, ID_CUETRACK = 0xF7, ID_CUECLUSTER = 0xF1,
  ID_CUEREL = 0xF0, ID_CUEDUR = 0xB2, ID_CLUSTER = 0x1F43B675, ID_TIMESTAMP = 0xE7,
  ID_BLOCKGROUP = 0xA0, ID_BLOCK = 0xA1, ID_SIMPLEBLOCK = 0xA3, ID_BLOCKDUR = 0x9B,
  ID_CHAPTERS = 0x1043A770, ID_EDITION = 0x45B9, ID_EDHIDDEN = 0x45BD, ID_EDDEFAULT = 0x45DB, ID_ATOM = 0xB6, ID_CHSTART = 0x91, ID_CHEND = 0x92,
  ID_CHHIDDEN = 0x98, ID_CHENABLED = 0x4598, ID_CHDISPLAY = 0x80, ID_CHSTRING = 0x85, ID_AUDIO = 0xE1, ID_CHANNELS = 0x9F, ID_ATTACHMENTS = 0x1941A469,
  ID_BLOCKADDMAP = 0x41E4, ID_BLOCKADDTYPE = 0x41E7, ID_BLOCKADDEXTRA = 0x41ED };

/* ---- EBML ---- */
typedef const unsigned char cu8;
static int read_id(cu8 *b, long n, long o, unsigned *id) {
  if (o >= n) return 0; unsigned c = b[o]; int l = c & 0x80 ? 1 : c & 0x40 ? 2 : c & 0x20 ? 3 : c & 0x10 ? 4 : 0;
  if (!l || o + l > n) return 0; unsigned v = 0; for (int i = 0; i < l; i++) v = v * 256 + b[o + i]; *id = v; return l;
}
static int read_size(cu8 *b, long n, long o, long long *size) { /* size -1 = unknown (all ones) */
  if (o >= n) return 0; unsigned c = b[o]; int l = 1; unsigned m = 0x80;
  while (l <= 8 && !(c & m)) { l++; m >>= 1; }
  if (l > 8 || o + l > n) return 0;
  long long v = c & (m - 1); int ones = v == (long long)(m - 1);
  for (int i = 1; i < l; i++) { v = v * 256 + b[o + i]; if (b[o + i] != 0xff) ones = 0; }
  *size = ones ? -1 : v; return l;
}
static unsigned long long read_uint(cu8 *b, long o, long n) { unsigned long long v = 0; for (long i = 0; i < n && i < 8; i++) v = v * 256 + b[o + i]; return v; }
static double read_float(cu8 *b, long o, long n) {
  if (n == 4) { uint32_t u = (uint32_t)read_uint(b, o, 4); float f; memcpy(&f, &u, 4); return f; }
  if (n == 8) { uint64_t u = read_uint(b, o, 8); double d; memcpy(&d, &u, 8); return d; }
  return 0;
}
static void read_str(cu8 *b, long o, long n, char *out, int cap) { int k = n < cap - 1 ? (int)n : cap - 1; memcpy(out, b + o, k); out[k] = 0; while (k > 0 && !out[k - 1]) k--; out[k] = 0; }
/* walks the children in b[start, end): fn returns 1 to stop; an element that doesn't fit ends the walk (size -1 passed once) */
typedef int (*EachFn)(void *ctx, unsigned id, long d, long long size, long at);
static long each(cu8 *b, long n, long start, long end, EachFn fn, void *ctx) {
  long o = start; if (end > n) end = n;
  while (o < end) {
    unsigned id; int il = read_id(b, n, o, &id); if (!il) break;
    long long sz; int sl = read_size(b, n, o + il, &sz); if (!sl) break;
    long d = o + il + sl;
    if (sz < 0 || d + sz > end) { if (fn(ctx, id, d, -1, o)) return o; break; }
    if (fn(ctx, id, d, sz, o)) return o;
    o = d + (long)sz;
  }
  return o;
}

/* ---- the file ---- */
typedef struct { char name[64]; int an, b, i; uint32_t c; } AssStyle;
typedef struct { int resX, resY, nst; AssStyle st[48]; char main[64]; } AssHdr;
typedef struct {
  int num, type, n, kind;            /* kind 0 = not text, 1 SubRip-like text, 2 ASS */
  char codec[32], lang[16], bcp[24], name[128]; int def, forced, sdh, encrypted, channels;
  int dvProfile, dvEl;               /* Dolby Vision (its BlockAdditionMapping): the profile (0 none), an enhancement layer */
  unsigned char *priv; long privLen; int compAlgo; unsigned char *compSet; long compSetLen; int hasComp;
} Track;
typedef struct { double t, d; long long cl; long rel; int num; long len; } Point; /* (len: an MP4 keyframe's exact size; cl its place) */
typedef struct { long long cl; double t; } ClusterT;
typedef struct {
  long long segStart, segEnd, size, cuesPos, tracksPos, chapPos, firstCluster, attachPos, seek2Pos; double tscale, duration; /* seek2Pos: a second SeekHead (near the end) */
  Track tr[64]; int ntr; int haveTracks;
  MkvChapter ch[64]; int nch; int haveChapters;
  Point *pts; int npts; ClusterT *cls; int ncls;
  Point *vk; int nvk; /* the video track's keyframes from the Cues (time, its Cluster, the block in it), in time order */
} Info;

/* ---- lines ---- */
typedef struct { double s, e; char *t; int an; float x, y; uint32_t color; unsigned char italic, bold, hidden; int layer; } Line; /* hidden: no text of its own (a vector sign), kept for the styled renderer only */
typedef struct { Track *tr; Line *l; int *pidx; long long *skey; int n, cap; AssHdr *hdr; int state; int total, done; int scan; char **raw; unsigned *rser, nser; int rawOn; } TState; /* raw / rser: the events as the file has them, with the order they came in (only while rawOn: shown styled) */ /* state: 0 pending 1 harvesting 2 complete */

struct MkvSession {
  char *url, *keyUrl; int p2p, mp4, keysOnly; pthread_t th; pthread_mutex_t mu; pthread_cond_t cv; int dead, refs;
  int state; char err[32];
  double pos, posAt, buffered; double jumpAt; int moving;
  int sel, wantN, listed;
  Info info; TState ts[64]; int nts;
  Point *all; unsigned char *status, *tries; int nall; /* tries: reads of that point that came back without its line */
  NetRange *net;
  double backoffUntil, backoffMs; int fails;
  int hdrLen[13], hdrLenBest;
  double sent[16]; int nsent; double scanSent[16]; int nscanSent;
  double prunedAt; int hasScan;
  /* scan cursor */
  struct { int on; long long pos, clStart, end, need; int inCl; long long raw; double ts; } cur;
  struct { long long cl; double t; } *scanned; int nscanned, capscanned;
  long long *seen; int nseen, capseen;
  struct { int tried, hit, miss, off, nd, ne; float dur[16]; long err[16]; long bytes; } aim; /* keyframes read where they most likely lie (no place in the index): how well that goes in this file */
};

static double mono(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static int net_err(int e) { return e == NR_NET || e == NR_TIMEOUT || e == NR_BUSY; }

/* reads exactly [start, start+len): some CDNs end a response early, so the rest is asked for while each follow-up brings bytes (3 empty ones end it) */
static int range_with(MkvSession *s, NetRange *net, long long start, long len, unsigned char **out, long *got, long long *total) {
  unsigned char *acc = malloc(len); long have = 0; int empty = 0, err = NR_OK; long long tot = -1;
  while (have < len && !s->dead) {
    unsigned char *b; long g; long long t; int st;
    int kr = net != s->net && s->keyUrl; /* (a preview's read: the engine's low-priority address, a piece may take a while) */
    err = net_range_get(net, kr ? s->keyUrl : s->url, start + have, len - have, &b, &g, &t, &st, kr ? 60000 : TIMEOUT_MS);
    if (t > 0) tot = t;
    if (g > 0) { memcpy(acc + have, b, g); have += g; empty = 0; free(b); } else empty++;
    if (have >= len || (tot > 0 && start + have >= tot)) { err = NR_OK; break; }
    if (err != NR_OK && err != NR_NET && err != NR_TIMEOUT) break;
    if (empty >= 3) break;
  }
  if (total) *total = tot;
  if (!have) { if (getenv("SB_MKVDBG") || mkvWantCues) fprintf(stderr, "[mkv] range %lld+%ld: nothing (err %d, file %lld bytes)\n", start, len, err, tot); free(acc); *out = NULL; *got = 0; return err ? err : NR_NET; }
  *out = acc; *got = have; return NR_OK;
}
static int range_full(MkvSession *s, long long start, long len, unsigned char **out, long *got, long long *total) { return range_with(s, s->net, start, len, out, got, total); }
static int range_once(MkvSession *s, long long start, long len, unsigned char **out, long *got) {
  long long t; int st; return net_range_get(s->net, s->url, start, len, out, got, &t, &st, TIMEOUT_MS);
}

/* ---- head: Segment, SeekHead, Info, Tracks, Chapters ---- */
typedef struct { cu8 *b; long n; Info *info; Track *t; void *x; int which; long long pos; } Ctx;
static uint32_t ass_color(const char *s) { /* "&H00FFFFFF" (AABBGGRR) -> 0xRRGGBB | 0xff000000; 0 when unparsable */
  while (*s == '&' || *s == 'H' || *s == 'h') s++;
  char hex[9]; int k = 0; while (isxdigit((unsigned char)*s) && k < 8) hex[k++] = *s++; hex[k] = 0; if (!k) return 0;
  unsigned long v = strtoul(hex, NULL, 16); unsigned r = v & 255, g = (v >> 8) & 255, b = (v >> 16) & 255;
  return 0xff000000u | (r << 16) | (g << 8) | b;
}
static int legacy_align(int a) { int col = a & 3 ? a & 3 : 2; if (a & 4) return col + 6; if (a & 8) return col + 3; return col; }
static int each_comp(void *c_, unsigned id, long d, long long sz, long at) { Ctx *c = c_; (void)at; if (sz < 0) return 0;
  if (id == ID_COMPALGO) c->t->compAlgo = (int)read_uint(c->b, d, sz);
  else if (id == ID_COMPSETTINGS) { free(c->t->compSet); c->t->compSet = malloc(sz); memcpy(c->t->compSet, c->b + d, sz); c->t->compSetLen = (long)sz; }
  return 0; }
static int each_encoding(void *c_, unsigned id, long d, long long sz, long at) { Ctx *c = c_; (void)at; if (sz < 0) return 0;
  if (id == ID_ENCRYPTION) c->t->encrypted = 1;
  else if (id == ID_COMPRESSION) { c->t->hasComp = 1; c->t->compAlgo = 0; each(c->b, c->n, d, d + (long)sz, each_comp, c); }
  return 0; }
static int each_encodings(void *c_, unsigned id, long d, long long sz, long at) { Ctx *c = c_; (void)at; if (id == ID_ENCODING && sz >= 0) each(c->b, c->n, d, d + (long)sz, each_encoding, c); return 0; }
static int each_audio(void *c_, unsigned id, long d, long long sz, long at) { Ctx *c = c_; (void)at; if (id == ID_CHANNELS && sz >= 0) c->t->channels = (int)read_uint(c->b, d, sz); return 0; }
/* a BlockAdditionMapping: Dolby Vision's configuration record (dvcC / dvvC / dvwC): version, then profile 7 bits, level
   6, RPU, enhancement layer and base layer present */
static int each_addmap(void *c_, unsigned id, long d, long long sz, long at) { Ctx *c = c_; (void)at; if (sz < 0) return 0;
  if (id == ID_BLOCKADDTYPE) c->which = (int)read_uint(c->b, d, sz);
  else if (id == ID_BLOCKADDEXTRA && sz >= 4) c->pos = d;
  return 0; }
static int each_track(void *c_, unsigned id, long d, long long sz, long at) { Ctx *c = c_; Track *t = c->t; (void)at; if (sz < 0) return 0;
  switch (id) {
    case ID_BLOCKADDMAP: { Ctx k = *c; k.which = 0; k.pos = -1; each(c->b, c->n, d, d + (long)sz, each_addmap, &k);
      if (k.pos >= 0 && (k.which == 0x64766343 || k.which == 0x64767643 || k.which == 0x64767743)) { cu8 *r = c->b + k.pos;
        t->dvProfile = r[2] >> 1; t->dvEl = (r[3] >> 1) & 1; } } break;
    case ID_AUDIO: each(c->b, c->n, d, d + (long)sz, each_audio, c); break; /* the channels, for the audio choice */
    case ID_TRACKNUM: t->num = (int)read_uint(c->b, d, sz); break;
    case ID_TRACKTYPE: t->type = (int)read_uint(c->b, d, sz); break;
    case ID_CODECID: read_str(c->b, d, sz, t->codec, sizeof t->codec); break;
    case ID_CODECPRIV: free(t->priv); t->priv = malloc(sz + 1); memcpy(t->priv, c->b + d, sz); t->priv[sz] = 0; t->privLen = (long)sz; break;
    case ID_LANG: read_str(c->b, d, sz, t->lang, sizeof t->lang); break;
    case ID_LANGBCP: read_str(c->b, d, sz, t->bcp, sizeof t->bcp); break;
    case ID_NAME: read_str(c->b, d, sz, t->name, sizeof t->name); break;
    case ID_FLAGDEFAULT: t->def = read_uint(c->b, d, sz) != 0; break;
    case ID_FLAGFORCED: t->forced = read_uint(c->b, d, sz) != 0; break;
    case ID_FLAGHI: t->sdh = read_uint(c->b, d, sz) != 0; break;
    case ID_ENCODINGS: each(c->b, c->n, d, d + (long)sz, each_encodings, c); break; /* zlib (old mkvmerge default) or header stripping; encrypted tracks are skipped */
  }
  return 0; }
static int each_tracks(void *c_, unsigned id, long d, long long sz, long at) { Ctx *c = c_; (void)at;
  if (id != ID_TRACKENTRY || sz < 0 || c->info->ntr >= 64) return 0;
  Track *t = &c->info->tr[c->info->ntr]; memset(t, 0, sizeof *t); strcpy(t->lang, "eng"); t->def = 1;
  Ctx k = *c; k.t = t; each(c->b, c->n, d, d + (long)sz, each_track, &k);
  if (t->bcp[0] && (!t->lang[0] || !strcmp(t->lang, "und"))) { char *dash = strchr(t->bcp, '-'); if (dash) *dash = 0; snprintf(t->lang, sizeof t->lang, "%s", t->bcp); }
  if (!strcmp(t->lang, "und")) t->lang[0] = 0;
  c->info->ntr++; return 0; }
static void parse_tracks(Info *info, cu8 *b, long n, long d, long long sz) { Ctx c = { b, n, info, NULL, NULL, 0, 0 }; info->ntr = 0; each(b, n, d, d + (long)sz, each_tracks, &c); info->haveTracks = 1; }

typedef struct { int hidden, def, n; MkvChapter l[64]; } Edition;
typedef struct { double s, e; char name[96]; int hidden, on; } Atom;
static int each_chdisplay(void *c_, unsigned id, long d, long long sz, long at) { Ctx *c = c_; Atom *a = c->x; (void)at; if (id == ID_CHSTRING && sz >= 0 && !a->name[0]) read_str(c->b, d, sz, a->name, sizeof a->name); return 0; }
static int each_atom(void *c_, unsigned id, long d, long long sz, long at) { Ctx *c = c_; Atom *a = c->x; (void)at; if (sz < 0) return 0;
  if (id == ID_CHSTART) a->s = read_uint(c->b, d, sz) / 1e9; else if (id == ID_CHEND) a->e = read_uint(c->b, d, sz) / 1e9;
  else if (id == ID_CHHIDDEN) a->hidden = read_uint(c->b, d, sz) != 0; else if (id == ID_CHENABLED) a->on = read_uint(c->b, d, sz) != 0;
  else if (id == ID_CHDISPLAY && !a->name[0]) each(c->b, c->n, d, d + (long)sz, each_chdisplay, c);
  return 0; }
static int each_edition(void *c_, unsigned id, long d, long long sz, long at) { Ctx *c = c_; Edition *e = c->x; (void)at; if (sz < 0) return 0;
  if (id == ID_EDHIDDEN) e->hidden = read_uint(c->b, d, sz) != 0; else if (id == ID_EDDEFAULT) e->def = read_uint(c->b, d, sz) != 0;
  else if (id == ID_ATOM) { Atom a; memset(&a, 0, sizeof a); a.e = -1; a.on = 1; Ctx k = *c; k.x = &a; each(c->b, c->n, d, d + (long)sz, each_atom, &k);
    if (!a.hidden && a.on && e->n < 64) { e->l[e->n].s = a.s; e->l[e->n].e = a.e; snprintf(e->l[e->n].name, sizeof e->l[e->n].name, "%s", a.name); e->n++; } }
  return 0; }
typedef struct { Edition ed[8]; int n; } Editions;
static int each_chapters(void *c_, unsigned id, long d, long long sz, long at) { Ctx *c = c_; Editions *E = c->x; (void)at;
  if (id != ID_EDITION || sz < 0 || E->n >= 8) return 0;
  Edition *e = &E->ed[E->n]; memset(e, 0, sizeof *e); Ctx k = *c; k.x = e; each(c->b, c->n, d, d + (long)sz, each_edition, &k);
  if (!e->hidden && e->n) E->n++;
  return 0; }
static int cmp_ch(const void *a, const void *b) { double x = ((const MkvChapter *)a)->s, y = ((const MkvChapter *)b)->s; return x < y ? -1 : x > y; }
/* the default (else first visible) edition, sorted; a missing end is the next chapter's start */
static void parse_chapters(Info *info, cu8 *b, long n, long d, long long sz) {
  Editions *E = calloc(1, sizeof *E); Ctx c = { b, n, info, NULL, E, 0, 0 }; each(b, n, d, d + (long)sz, each_chapters, &c);
  Edition *pick = NULL; for (int i = 0; i < E->n; i++) if (E->ed[i].def) { pick = &E->ed[i]; break; }
  if (!pick && E->n) pick = &E->ed[0];
  info->nch = 0;
  if (pick) { memcpy(info->ch, pick->l, pick->n * sizeof *pick->l); info->nch = pick->n; qsort(info->ch, info->nch, sizeof *info->ch, cmp_ch);
    for (int j = 0; j + 1 < info->nch; j++) if (info->ch[j].e < 0) info->ch[j].e = info->ch[j + 1].s; }
  info->haveChapters = 1; free(E);
}
static int each_seek(void *c_, unsigned id, long d, long long sz, long at) { Ctx *c = c_; (void)at; if (sz < 0) return 0;
  if (id == ID_SEEKID) c->which = (int)read_uint(c->b, d, sz); else if (id == ID_SEEKPOS) c->pos = (long long)read_uint(c->b, d, sz); return 0; }
static int each_seekhead(void *c_, unsigned id, long d, long long sz, long at) { Ctx *c = c_; (void)at; if (id != ID_SEEK || sz < 0) return 0;
  Ctx k = *c; k.which = 0; k.pos = -1; each(c->b, c->n, d, d + (long)sz, each_seek, &k); if (k.pos < 0) return 0;
  Info *i = c->info; long long abs = i->segStart + k.pos;
  if ((unsigned)k.which == ID_SEEKHEAD && abs != i->segStart && i->seek2Pos < 0) i->seek2Pos = abs; /* (another SeekHead: its entries may hold the index) */
  if ((unsigned)k.which == ID_CUES) i->cuesPos = abs; else if ((unsigned)k.which == ID_TRACKS && i->tracksPos < 0) i->tracksPos = abs; else if ((unsigned)k.which == ID_CHAPTERS && i->chapPos < 0) i->chapPos = abs; else if ((unsigned)k.which == ID_ATTACHMENTS && i->attachPos < 0) i->attachPos = abs;
  return 0; }
static int each_info(void *c_, unsigned id, long d, long long sz, long at) { Ctx *c = c_; (void)at; if (sz < 0) return 0;
  if (id == ID_TSCALE) { c->info->tscale = (double)read_uint(c->b, d, sz); if (c->info->tscale <= 0) c->info->tscale = 1000000; }
  else if (id == ID_DURATION) c->info->duration = read_float(c->b, d, sz);
  return 0; }
static int each_segment(void *c_, unsigned id, long d, long long sz, long at) { Ctx *c = c_; Info *i = c->info;
  if (id == ID_CLUSTER) { i->firstCluster = at; return 1; }
  if (sz < 0) { if (id == ID_TRACKS && i->tracksPos < 0) i->tracksPos = at; return 1; }
  if (id == ID_SEEKHEAD) each(c->b, c->n, d, d + (long)sz, each_seekhead, c);
  else if (id == ID_INFO) each(c->b, c->n, d, d + (long)sz, each_info, c);
  else if (id == ID_TRACKS) parse_tracks(i, c->b, c->n, d, sz);
  else if (id == ID_CHAPTERS) parse_chapters(i, c->b, c->n, d, sz);
  else if (id == ID_CUES && i->cuesPos < 0) i->cuesPos = at; /* the index written at the front */
  else if (id == ID_ATTACHMENTS && i->attachPos < 0) i->attachPos = at;
  return 0; }
static int parse_head(Info *i, cu8 *b, long n) {
  unsigned id; long long sz; int il = read_id(b, n, 0, &id); if (!il || id != ID_EBML) return 0;
  int sl = read_size(b, n, il, &sz); if (!sl || sz < 0) return 0;
  long o = il + sl + (long)sz; il = read_id(b, n, o, &id); if (!il || id != ID_SEGMENT) return 0;
  sl = read_size(b, n, o + il, &sz); if (!sl) return 0;
  i->segStart = o + il + sl; i->segEnd = sz < 0 ? -1 : i->segStart + sz; i->tscale = 1000000; i->cuesPos = i->tracksPos = i->chapPos = i->firstCluster = i->attachPos = i->seek2Pos = -1;
  Ctx c = { b, n, i, NULL, NULL, 0, 0 }; each(b, n, (long)i->segStart, n, each_segment, &c);
  i->duration = i->duration * i->tscale / 1e9;
  return 1;
}

/* ---- Cues ---- */
typedef struct { Ctx c; long long time; struct { int trk; long long cl; long rel; long long dur; } p[8]; int np; Point *pts; int npts, cap; ClusterT *cls; int ncls, capc; const unsigned char *want; int vtrk; Point *vk; int nvk, vcap; } CueCtx;
static int each_ctp(void *c_, unsigned id, long d, long long sz, long at) { CueCtx *q = c_; (void)at; if (sz < 0) return 0;
  int k = q->np - 1;
  if (id == ID_CUETRACK) q->p[k].trk = (int)read_uint(q->c.b, d, sz); else if (id == ID_CUECLUSTER) q->p[k].cl = (long long)read_uint(q->c.b, d, sz);
  else if (id == ID_CUEREL) q->p[k].rel = (long)read_uint(q->c.b, d, sz); else if (id == ID_CUEDUR) q->p[k].dur = (long long)read_uint(q->c.b, d, sz);
  return 0; }
static int each_cp(void *c_, unsigned id, long d, long long sz, long at) { CueCtx *q = c_; (void)at; if (sz < 0) return 0;
  if (id == ID_CUETIME) q->time = (long long)read_uint(q->c.b, d, sz);
  else if (id == ID_CUETRACKPOS && q->np < 8) { q->np++; q->p[q->np - 1].trk = 0; q->p[q->np - 1].cl = -1; q->p[q->np - 1].rel = -1; q->p[q->np - 1].dur = -1; each(q->c.b, q->c.n, d, d + (long)sz, each_ctp, q); }
  return 0; }
static int each_cues(void *c_, unsigned id, long d, long long sz, long at) { CueCtx *q = c_; (void)at;
  if (id != ID_CUEPOINT || sz < 0) return 0;
  q->time = -1; q->np = 0; each(q->c.b, q->c.n, d, d + (long)sz, each_cp, q);
  if (q->time < 0) return 0;
  double scale = q->c.info->tscale / 1e9;
  for (int j = 0; j < q->np; j++) {
    if (q->p[j].cl >= 0) { /* every Cluster's time, for scan mode */
      if (!q->ncls || q->cls[q->ncls - 1].cl != q->c.info->segStart + q->p[j].cl) { if (q->ncls == q->capc) { q->capc = q->capc ? q->capc * 2 : 1024; q->cls = realloc(q->cls, q->capc * sizeof *q->cls); } q->cls[q->ncls].cl = q->c.info->segStart + q->p[j].cl; q->cls[q->ncls].t = q->time * scale; q->ncls++; } }
    if (q->vtrk > 0 && q->p[j].trk == q->vtrk && q->p[j].cl >= 0) { /* the video's keyframes, kept apart (seeks land on one) */
      if (q->nvk == q->vcap) { int nc = q->vcap ? q->vcap * 2 : 1024; Point *np = realloc(q->vk, nc * sizeof *np); if (np) { q->vk = np; q->vcap = nc; } }
      if (q->nvk < q->vcap) { Point *v = &q->vk[q->nvk++]; v->t = q->time * scale; v->d = -1; v->cl = q->c.info->segStart + q->p[j].cl; v->rel = q->p[j].rel; v->num = q->vtrk; } }
    if (q->p[j].trk <= 0 || q->p[j].trk > 255 || !q->want[q->p[j].trk] || q->p[j].cl < 0) continue;
    if (q->npts == q->cap) { q->cap = q->cap ? q->cap * 2 : 1024; q->pts = realloc(q->pts, q->cap * sizeof *q->pts); }
    Point *p = &q->pts[q->npts++]; p->t = q->time * scale; p->d = q->p[j].dur >= 0 ? q->p[j].dur * scale : -1; p->cl = q->c.info->segStart + q->p[j].cl; p->rel = q->p[j].rel; p->num = q->p[j].trk;
  }
  return 0; }
static int cmp_pt(const void *a, const void *b) { const Point *x = a, *y = b; if (x->t != y->t) return x->t < y->t ? -1 : 1; if (x->cl != y->cl) return x->cl < y->cl ? -1 : 1; return x->rel < y->rel ? -1 : x->rel > y->rel; }
static int cmp_cl(const void *a, const void *b) { const ClusterT *x = a, *y = b; return x->cl < y->cl ? -1 : x->cl > y->cl; }
/* NR_OK with info->pts filled (may be 0 points), 1 = no usable index, or a net error */
/* where the index is, when the head didn't say: a second SeekHead (near the end), else the file's last 4 MB searched for
   it; and an index said to be past the server's size read anyway (some servers report a size cut short) */
static void find_cues(MkvSession *s) {
  Info *i = &s->info; long long end = i->segEnd > 0 ? i->segEnd : i->size;
  fprintf(stderr, "[mkv] index: at %lld, second SeekHead %lld, Segment end %lld, server size %lld\n", i->cuesPos, i->seek2Pos, i->segEnd, i->size);
  if (i->cuesPos < 0 && i->seek2Pos >= 0) { unsigned char *b; long n; if (!range_full(s, i->seek2Pos, HEAD_BYTES, &b, &n, NULL)) { unsigned id; long long sz; int il = read_id(b, n, 0, &id), sl = il ? read_size(b, n, il, &sz) : 0;
      if (il && sl && id == ID_SEEKHEAD && sz > 0) { Ctx c; memset(&c, 0, sizeof c); c.b = b; c.n = n; c.info = i; long d = il + sl; each(b, n, d, d + sz < n ? d + (long)sz : n, each_seekhead, &c); } free(b); }
    if (i->cuesPos >= 0) fprintf(stderr, "[mkv] index found through the second SeekHead\n"); }
  if (i->cuesPos < 0 && end > (8 << 20)) { long want = 4 << 20; unsigned char *b; long n; if (!range_full(s, end - want, want, &b, &n, NULL)) { /* the last 4 MB: the index's ID with a plausible size, its first child a CuePoint */
      for (long o = n - 8; o >= 0 && i->cuesPos < 0; o--) { if (b[o] != 0x1C || b[o + 1] != 0x53 || b[o + 2] != 0xBB || b[o + 3] != 0x6B) continue; long long sz; int sl = read_size(b, n, o + 4, &sz); if (!sl || sz <= 0 || sz > CUES_MAX) continue;
        unsigned c; if (read_id(b, n, o + 4 + sl, &c) && c == ID_CUEPOINT && end - want + o + 4 + sl + sz <= end + 16) i->cuesPos = end - want + o; }
      free(b); }
    if (i->cuesPos >= 0) fprintf(stderr, "[mkv] index found at the file's end (%lld)\n", i->cuesPos); } }
static int load_cues(MkvSession *s) {
  if (s->info.cuesPos < 0 && !getenv("SB_MKV_NOCUES")) find_cues(s);
  Info *i = &s->info; if (i->cuesPos < 0 || getenv("SB_MKV_NOCUES")) return 1; /* SB_MKV_NOCUES: development, try the scan path */
  if (i->size > 0 && i->cuesPos >= i->size) { fprintf(stderr, "[mkv] the index is said to be at %lld, past the server's size (%lld, Segment end %lld): read anyway\n", i->cuesPos, i->size, i->segEnd); i->size = i->segEnd > i->cuesPos ? i->segEnd : -1; } /* (the server's size may be cut short: the header's own end, else none) */ /* (a header that doesn't match what the server sends: never a read of a negative length) */
  long first = CUES_FIRST; if (i->size > 0 && i->cuesPos + first > i->size) first = (long)(i->size - i->cuesPos);
  unsigned char *b; long n; int e = range_full(s, i->cuesPos, first, &b, &n, NULL); if (e) return e;
  unsigned id; long long sz; int il = read_id(b, n, 0, &id);
  if (!il || id != ID_CUES) { free(b); return il && id == ID_EBML ? NR_NORANGE : 1; }
  int sl = read_size(b, n, il, &sz); if (!sl || sz <= 0 || sz > CUES_MAX) { free(b); return 1; }
  long d = il + sl;
  if (d + sz > n) { free(b); e = range_full(s, i->cuesPos, d + (long)sz, &b, &n, NULL); if (e) return e; if (n < d + sz) { free(b); return 1; } }
  unsigned char want[256] = { 0 }; for (int k = 0; k < i->ntr; k++) if (i->tr[k].type == 17 && i->tr[k].kind && i->tr[k].num > 0 && i->tr[k].num < 256) want[i->tr[k].num] = 1;
  CueCtx q; memset(&q, 0, sizeof q); q.c.b = b; q.c.n = n; q.c.info = i; q.want = want;
  for (int k = 0; k < i->ntr && !q.vtrk; k++) if (i->tr[k].type == 1 && i->tr[k].num > 0 && i->tr[k].num < 256) q.vtrk = i->tr[k].num; /* (the first video track) */
  each(b, n, d, d + (long)sz, each_cues, &q); free(b);
  qsort(q.pts, q.npts, sizeof *q.pts, cmp_pt); qsort(q.cls, q.ncls, sizeof *q.cls, cmp_cl);
  /* one entry per Cluster (the first time seen wins, as the points come in time order per Cluster) */
  int w = 0; for (int k = 0; k < q.ncls; k++) if (!w || q.cls[w - 1].cl != q.cls[k].cl) q.cls[w++] = q.cls[k]; q.ncls = w;
  i->pts = q.pts; i->npts = q.npts; i->cls = q.cls; i->ncls = q.ncls;
  if (q.nvk) { qsort(q.vk, q.nvk, sizeof *q.vk, cmp_pt); int u = 0; for (int k = 0; k < q.nvk; k++) if (!u || q.vk[u - 1].t != q.vk[k].t) q.vk[u++] = q.vk[k]; q.nvk = u; }
  pthread_mutex_lock(&s->mu); free(i->vk); i->vk = q.vk; i->nvk = q.nvk; pthread_mutex_unlock(&s->mu); /* (read by the player) */
  return NR_OK;
}

/* ---- ASS ---- */
static void parse_ass_header(AssHdr *h, const char *priv) {
  memset(h, 0, sizeof *h); if (!priv) goto defaults;
  char sec[64] = ""; char fmt[24][24]; int nf = 0;
  const char *p = priv;
  while (*p) {
    const char *e = p; while (*e && *e != '\n') e++;
    char line[1024]; int l = (int)(e - p); if (l > 1023) l = 1023; memcpy(line, p, l); line[l] = 0; p = *e ? e + 1 : e;
    char *t = line; while (isspace((unsigned char)*t)) t++; int tl = (int)strlen(t); while (tl && isspace((unsigned char)t[tl - 1])) t[--tl] = 0;
    if (t[0] == '[' && tl && t[tl - 1] == ']') { snprintf(sec, sizeof sec, "%s", t); for (char *q = sec; *q; q++) *q = (char)tolower((unsigned char)*q); continue; }
    if (!strncasecmp(t, "PlayResX", 8)) { char *c = strchr(t, ':'); if (c) h->resX = atoi(c + 1); }
    else if (!strncasecmp(t, "PlayResY", 8)) { char *c = strchr(t, ':'); if (c) h->resY = atoi(c + 1); }
    else if (strstr(sec, "styles") && !strncasecmp(t, "Format", 6)) { char *c = strchr(t, ':'); nf = 0; if (c) { char *tok = strtok(c + 1, ","); while (tok && nf < 24) { while (isspace((unsigned char)*tok)) tok++; snprintf(fmt[nf], 24, "%s", tok); for (char *q = fmt[nf]; *q; q++) *q = (char)tolower((unsigned char)*q); int fl = (int)strlen(fmt[nf]); while (fl && isspace((unsigned char)fmt[nf][fl - 1])) fmt[nf][--fl] = 0; nf++; tok = strtok(NULL, ","); } } }
    else if (strstr(sec, "styles") && !strncasecmp(t, "Style", 5) && nf && h->nst < 48) {
      char *c = strchr(t, ':'); if (!c) continue; AssStyle *st = &h->st[h->nst]; memset(st, 0, sizeof *st); int align = 2, k = 0; char *q = c + 1;
      while (q && k < nf) { char *comma = strchr(q, ','); if (comma && k < nf - 1) *comma = 0; while (isspace((unsigned char)*q)) q++;
        if (!strcmp(fmt[k], "name")) { snprintf(st->name, sizeof st->name, "%s", q); for (char *z = st->name; *z; z++) *z = (char)tolower((unsigned char)*z); }
        else if (!strcmp(fmt[k], "alignment")) align = atoi(q);
        else if (!strcmp(fmt[k], "bold")) st->b = atoi(q) != 0;
        else if (!strcmp(fmt[k], "italic")) st->i = atoi(q) != 0;
        else if (!strcmp(fmt[k], "primarycolour") || !strcmp(fmt[k], "primarycolor")) st->c = ass_color(q);
        k++; q = comma && k < nf ? comma + 1 : NULL; }
      if (!strstr(sec, "v4+")) align = legacy_align(align); /* SSA v4 numbering */
      st->an = align >= 1 && align <= 9 ? align : 2;
      if (!h->main[0]) snprintf(h->main, sizeof h->main, "%s", st->name);
      h->nst++;
    }
  }
defaults: /* libass defaults when only one or neither is given */
  if (!h->resX && !h->resY) { h->resX = 384; h->resY = 288; }
  else if (!h->resY) h->resY = h->resX == 1280 ? 1024 : h->resX * 3 / 4;
  else if (!h->resX) h->resX = h->resY == 1024 ? 1280 : h->resY * 4 / 3;
}
static const AssStyle *ass_style(const AssHdr *h, const char *name) {
  static const AssStyle def = { "", 2, 0, 0, 0 };
  for (int i = 0; i < h->nst; i++) if (!strcmp(h->st[i].name, name)) return &h->st[i];
  for (int i = 0; i < h->nst; i++) if (!strcmp(h->st[i].name, "default")) return &h->st[i];
  return &def;
}
/* one ASS event (ReadOrder,Layer,Style,Name,MarginL,MarginR,MarginV,Effect,Text) -> a line: plain text, alignment, position, and the
   colour/italic/bold that hold for the whole line (the dialogue style follows the viewer's colour; signs keep theirs) */
static int ass_line(const char *payload, double s, double e, const AssHdr *h, Line *out) {
  const char *f[9]; int nf = 0; const char *p = payload; f[nf++] = p;
  while (*p && nf < 9) { if (*p == ',') f[nf++] = p + 1; p++; }
  if (nf < 9) return 0;
  char sname[64]; { const char *a = f[2], *b = f[3] - 1; while (a < b && isspace((unsigned char)*a)) a++; while (b > a && isspace((unsigned char)b[-1])) b--; int l = (int)(b - a); if (l > 63) l = 63; memcpy(sname, a, l); sname[l] = 0; for (char *z = sname; *z; z++) *z = (char)tolower((unsigned char)*z); }
  const AssStyle *st = ass_style(h, sname);
  uint32_t baseCol = sname[0] && strcmp(sname, "default") && strcmp(sname, h->main) && st->c && (st->c & 0xffffff) != 0xffffff ? st->c : 0;
  int an = st->an, it = st->i, bd = st->b, drawing = 0, anyStyleChange = 0; uint32_t col = baseCol; float x = -1, y = -1;
  const char *text = f[8]; size_t tl = strlen(text);
  char *plain = malloc(tl + 2); size_t o = 0;
  for (size_t i = 0; i < tl;) {
    char ch = text[i];
    if (ch == '{') {
      const char *j = strchr(text + i, '}'); size_t end = j ? (size_t)(j - text) : tl;
      for (size_t k = i + 1; k < end; k++) {
        if (text[k] != '\\') continue; const char *t = text + k + 1;
        if (!strncmp(t, "an", 2) && isdigit((unsigned char)t[2])) { int n = t[2] - '0'; if (n >= 1 && n <= 9) an = n; }
        else if (t[0] == 'a' && isdigit((unsigned char)t[1])) { int n = atoi(t + 1); if (n > 0) an = legacy_align(n); }
        else if (!strncmp(t, "pos(", 4) || !strncmp(t, "move(", 5)) { const char *q = strchr(t, '(') + 1; float a, b; if (sscanf(q, "%f , %f", &a, &b) == 2 || sscanf(q, "%f,%f", &a, &b) == 2) { x = a; y = b; } }
        else if (t[0] == 'i' && (t[1] == '0' || t[1] == '1')) { it = t[1] == '1'; anyStyleChange = 1; }
        else if (t[0] == 'b' && isdigit((unsigned char)t[1])) { int w = atoi(t + 1); bd = w == 1 || w >= 700; anyStyleChange = 1; }
        else if ((t[0] == 'c' && (t[1] == '&' || t[1] == 'H')) || (!strncmp(t, "1c", 2) && (t[2] == '&' || t[2] == 'H'))) { col = ass_color(t + (t[0] == '1' ? 2 : 1)); anyStyleChange = 1; }
        else if (t[0] == 'p' && isdigit((unsigned char)t[1])) drawing = atoi(t + 1) > 0;
        else if (t[0] == 'r') { it = st->i; bd = st->b; col = baseCol; anyStyleChange = 1; }
      }
      i = end + 1; continue;
    }
    if (drawing) { i++; continue; } /* vector drawing commands, not text */
    if (ch == '\\' && i + 1 < tl) { char nx = text[i + 1];
      if (nx == 'N' || nx == 'n') { while (o && plain[o - 1] == ' ') o--; plain[o++] = '\n'; i += 2; continue; }
      if (nx == 'h') { plain[o++] = ' '; i += 2; continue; } }
    plain[o++] = ch; i++;
  }
  plain[o] = 0;
  /* trim */
  char *a = plain; while (*a && isspace((unsigned char)*a)) a++; size_t l = strlen(a); while (l && isspace((unsigned char)a[l - 1])) a[--l] = 0;
  if (!*a) { free(plain); return 0; }
  memset(out, 0, sizeof *out); out->s = s; out->e = e; out->t = strdup(a); free(plain);
  out->layer = atoi(f[1]); out->italic = (unsigned char)it; out->bold = (unsigned char)bd; out->color = col; out->x = out->y = -1; (void)anyStyleChange;
  if (x >= 0 && y >= 0 && h->resX && h->resY) { out->x = roundf(x / h->resX * 1000) / 10; out->y = roundf(y / h->resY * 1000) / 10; out->an = an; }
  else if (an != 2) out->an = an;
  return 1;
}
/* S_TEXT/UTF8 (a SubRip body with <i>/<b>/<font color>, sometimes ASS override blocks) -> a line */
static int text_line(const char *payload, double s, double e, Line *out) {
  size_t n = strlen(payload); char *body = malloc(n + 1); size_t o = 0; int an = 0, italicAll = 0; uint32_t col = 0;
  const char *am = strstr(payload, "{\\an"); if (am && isdigit((unsigned char)am[4])) an = am[4] - '0';
  { const char *t = payload; while (isspace((unsigned char)*t)) t++; if (!strncasecmp(t, "<i>", 3)) { const char *z = payload + n; while (z > payload && isspace((unsigned char)z[-1])) z--; if (z - payload >= 4 && !strncasecmp(z - 4, "</i>", 4)) italicAll = 1; }
    const char *fc = strcasestr(payload, "<font"); if (fc && fc - t < 2) { const char *c = strcasestr(fc, "color"); if (c) { c = strchr(c, '='); if (c) { c++; while (*c == '"' || *c == '\'' || *c == ' ') c++; if (*c == '#') c++; char hex[7]; int k = 0; while (isxdigit((unsigned char)c[k]) && k < 6) { hex[k] = c[k]; k++; } hex[k] = 0; if (k == 6) col = 0xff000000u | (uint32_t)strtoul(hex, NULL, 16); } } } }
  for (size_t i = 0; i < n;) {
    if (payload[i] == '\r') { i++; continue; }
    if (payload[i] == '{' && payload[i + 1] == '\\') { const char *z = strchr(payload + i, '}'); if (z) { i = (size_t)(z - payload) + 1; continue; } }
    if (payload[i] == '<') { const char *z = strchr(payload + i, '>'); if (z) { i = (size_t)(z - payload) + 1; continue; } }
    body[o++] = payload[i++];
  }
  body[o] = 0;
  char *a = body; while (*a && isspace((unsigned char)*a)) a++; size_t l = strlen(a); while (l && isspace((unsigned char)a[l - 1])) a[--l] = 0;
  if (!*a) { free(body); return 0; }
  memset(out, 0, sizeof *out); out->s = s; out->e = e; out->t = strdup(a); free(body);
  out->x = out->y = -1; out->italic = (unsigned char)italicAll; out->color = col; if (an && an != 2) out->an = an;
  return 1;
}

/* ---- blocks ---- */
static unsigned char *decode_payload(const Track *t, cu8 *data, long n, long *outLen) {
  if (!t->hasComp) { unsigned char *c = malloc(n + 1); memcpy(c, data, n); c[n] = 0; *outLen = n; return c; }
  if (t->compAlgo == 3) { long m = t->compSetLen + n; unsigned char *c = malloc(m + 1); if (t->compSetLen) memcpy(c, t->compSet, t->compSetLen); memcpy(c + t->compSetLen, data, n); c[m] = 0; *outLen = m; return c; } /* header stripping */
  if (t->compAlgo == 0) { /* zlib */
    uLongf cap = (uLongf)(n * 8 + 1024); for (int tries = 0; tries < 4; tries++, cap *= 4) { unsigned char *c = malloc(cap + 1); uLongf got = cap;
      int r = uncompress(c, &got, data, (uLong)n); if (r == Z_OK) { c[got] = 0; *outLen = (long)got; return c; } free(c); if (r != Z_BUF_ERROR) break; }
  }
  return NULL;
}
typedef struct { long len, total; cu8 *payload; long plen; long long dur; int ts; } Block;
static int block_dur_cb(void *c_, unsigned id, long d, long long sz, long at) { Ctx *c = c_; (void)at; if (sz < 0) return 0;
  long long *v = c->x; if (id == ID_BLOCK) { v[0] = d; v[1] = sz; } else if (id == ID_BLOCKDUR) v[2] = (long long)read_uint(c->b, d, sz); return 0; }
/* the BlockGroup/SimpleBlock at b[o]: 1 = a block of this track (len < 0: it needs `total` bytes from o), 0 = not one */
static int read_block(cu8 *b, long n, long o, int trackNum, Block *out) {
  unsigned id; int il = read_id(b, n, o, &id); if (!il || (id != ID_BLOCKGROUP && id != ID_SIMPLEBLOCK)) return 0;
  long long sz; int sl = read_size(b, n, o + il, &sz); if (!sl || sz <= 0 || sz > 4 * 1024 * 1024) return 0;
  long d = o + il + sl, end = d + (long)sz;
  memset(out, 0, sizeof *out);
  if (end > n) { out->len = -1; out->total = end - o; return 1; }
  long long v[3] = { -1, 0, -1 };
  if (id == ID_BLOCKGROUP) { Ctx c = { b, n, NULL, NULL, v, 0, 0 }; each(b, n, d, end, block_dur_cb, &c); } else { v[0] = d; v[1] = sz; }
  long bl = (long)v[0], blN = (long)v[1]; if (bl < 0 || blN < 4) return 0;
  long long tn; int tl = read_size(b, n, bl, &tn); if (!tl || tn != trackNum) return 0;
  int flags = b[bl + tl + 2]; out->len = end - o; out->dur = v[2];
  if (flags & 0x06) { out->payload = NULL; return 1; } /* laced subtitle blocks: not seen in practice */
  out->ts = (int16_t)((b[bl + tl] << 8) | b[bl + tl + 1]);
  long pe = end > bl + blN ? bl + blN : end; out->payload = b + bl + tl + 3; out->plen = pe - (bl + tl + 3);
  return 1;
}

/* ---- the session ---- */
static void lock(MkvSession *s) { pthread_mutex_lock(&s->mu); }
static void unlock(MkvSession *s) { pthread_mutex_unlock(&s->mu); }
static double now_pos(MkvSession *s) { if (!s->moving) return s->pos; double el = mono() - s->posAt; if (el > 30) el = 30; return s->pos + el; } /* the playhead between the player's reports (still while paused or loading) */
static TState *ts_of(MkvSession *s, int num) { for (int i = 0; i < s->nts; i++) if (s->ts[i].tr->num == num) return &s->ts[i]; return NULL; }
static TState *ts_by_n(MkvSession *s, int n) { for (int i = 0; i < s->nts; i++) if (s->ts[i].tr->n == n) return &s->ts[i]; return NULL; }
/* lines are kept sorted by start (the player looks them up by time) */
static void raw_fit(TState *t) { if (t->rawOn) { t->raw = realloc(t->raw, (t->cap ? t->cap : 1) * sizeof *t->raw); t->rser = realloc(t->rser, (t->cap ? t->cap : 1) * sizeof *t->rser); } }
static void ts_add(TState *t, Line *ln, int pidx, long long skey, char *raw) {
  if (t->n == t->cap) { t->cap = t->cap ? t->cap * 2 : 256; t->l = realloc(t->l, t->cap * sizeof *t->l); t->pidx = realloc(t->pidx, t->cap * sizeof *t->pidx); t->skey = realloc(t->skey, t->cap * sizeof *t->skey); raw_fit(t); }
  int a = 0, z = t->n; while (a < z) { int m = (a + z) / 2; if (t->l[m].s <= ln->s) a = m + 1; else z = m; }
  memmove(t->l + a + 1, t->l + a, (t->n - a) * sizeof *t->l); memmove(t->pidx + a + 1, t->pidx + a, (t->n - a) * sizeof *t->pidx); memmove(t->skey + a + 1, t->skey + a, (t->n - a) * sizeof *t->skey);
  t->l[a] = *ln; t->pidx[a] = pidx; t->skey[a] = skey;
  if (t->rawOn) { memmove(t->raw + a + 1, t->raw + a, (t->n - a) * sizeof *t->raw); memmove(t->rser + a + 1, t->rser + a, (t->n - a) * sizeof *t->rser); t->raw[a] = raw; t->rser[a] = ++t->nser; raw = NULL; }
  free(raw); t->n++;
}
static int make_line(MkvSession *s, TState *t, const Block *blk, double st, double cueDur, Line *out, char **raw) {
  double scale = s->info.tscale / 1e9; long n; unsigned char *data = decode_payload(t->tr, blk->payload, blk->plen, &n); *raw = NULL;
  if (!data) return 0;
  double dur = blk->dur >= 0 ? blk->dur * scale : (cueDur >= 0 ? cueDur : DEFAULT_DUR_S);
  double e = st + (dur > 0 ? dur : 0.5);
  int ok = t->tr->kind == 2 ? ass_line((const char *)data, st, e, t->hdr, out) : text_line((const char *)data, st, e, out);
  if (t->rawOn && t->tr->kind == 2) { /* the styled renderer takes the event as it is, signs without text too */
    if (!ok) { memset(out, 0, sizeof *out); out->s = st; out->e = e; out->t = strdup(""); out->x = out->y = -1; out->hidden = 1; ok = 1; }
    *raw = (char *)data; data = NULL; }
  free(data);
  if (ok) { out->s = round(out->s * 1000) / 1000; out->e = round(out->e * 1000) / 1000; }
  return ok;
}
/* a point read: its line kept (2), or not found in what came back (a short read, a torrent piece not there yet): read
   again up to twice later (0) before it counts as empty (3); an empty one is never taken as covered (see track_status) */
static void mark_done(MkvSession *s, int i, int got) { if (s->status[i] >= 2) return;
  if (!got && s->tries && s->tries[i] < 2) { s->tries[i]++; s->status[i] = 0; return; }
  s->status[i] = got ? 2 : 3; TState *t = ts_of(s, s->all[i].num); if (t) t->done++; }
static void add_line(MkvSession *s, int i, const Block *blk) {
  Point *p = &s->all[i]; TState *t = ts_of(s, p->num); Line ln;
  char *raw = NULL; int ok = t && blk->payload && make_line(s, t, blk, p->t, p->d, &ln, &raw);
  mark_done(s, i, ok);
  if (ok) ts_add(t, &ln, i, 0, raw); else free(raw);
}
/* drops lines far from the playhead: their index points become pending again, scan keys are forgotten */
static int seen_has(MkvSession *s, long long k) { for (int i = 0; i < s->nseen; i++) if (s->seen[i] == k) return 1; return 0; }
static void seen_add(MkvSession *s, long long k) { if (s->nseen == s->capseen) { s->capseen = s->capseen ? s->capseen * 2 : 1024; s->seen = realloc(s->seen, s->capseen * sizeof *s->seen); } s->seen[s->nseen++] = k; }
static void prune(MkvSession *s) {
  s->prunedAt = mono(); double now = now_pos(s); s->nseen = 0;
  for (int k = 0; k < s->nts; k++) {
    TState *t = &s->ts[k]; int sel = t->tr->num == s->sel; double lo, hi;
    if (sel) { if (t->n <= SEL_MAX) { for (int a = 0; a < t->n; a++) if (t->skey[a]) seen_add(s, t->skey[a]); continue; } lo = now - SEL_BEHIND_S; hi = now + SEL_AHEAD_S; }
    else { lo = now - KEEP_BEHIND_S; hi = now + KEEP_AHEAD_S; }
    int w = 0, dropped = 0;
    for (int a = 0; a < t->n; a++) {
      Line *c = &t->l[a];
      if (c->e >= lo && c->s <= hi) { t->l[w] = *c; t->pidx[w] = t->pidx[a]; t->skey[w] = t->skey[a]; if (t->rawOn) { t->raw[w] = t->raw[a]; t->rser[w] = t->rser[a]; } if (t->skey[w]) seen_add(s, t->skey[w]); w++; continue; }
      dropped++; free(c->t); if (t->rawOn) free(t->raw[a]);
      int i = t->pidx[a]; if (i >= 0 && s->status[i] == 2) { s->status[i] = 0; t->done--; }
    }
    t->n = w; if (dropped && t->state == 2) t->state = 1;
  }
  if (s->hasScan || s->nscanned) { /* Clusters whose lines went must be read again when the playhead returns */
    double klo = now - KEEP_BEHIND_S, khi = now + KEEP_AHEAD_S; int w = 0;
    if (s->nscanned <= SCANNED_MAX) for (int i = 0; i < s->nscanned; i++) if (s->scanned[i].t >= klo && s->scanned[i].t <= khi) s->scanned[w++] = s->scanned[i];
    s->nscanned = w;
  }
}
/* the next pending point: the window around the playhead for every text track; then (not on torrents) the rest of the chosen one, ahead first */
static int pick(MkvSession *s, int *urgent) {
  double now = now_pos(s), lo = now - BEHIND_S, hi = now + (s->p2p ? P2P_AHEAD_S : AHEAD_S);
  if (s->p2p && s->buffered >= 0) hi = now + fmin(P2P_AHEAD_S, s->buffered + P2P_MIN_AHEAD_S); /* a piece not downloaded yet blocks the read (and pulls the engine away from the playhead) */
  int a = 0, z = s->nall; while (a < z) { int m = (a + z) >> 1; if (s->all[m].t < lo) a = m + 1; else z = m; }
  int i; for (i = a; i < s->nall && s->all[i].t <= hi; i++) if (!s->status[i]) { *urgent = 1; return i; }
  *urgent = 0;
  if (s->p2p || s->sel < 0) return -1;
  TState *sel = ts_of(s, s->sel); if (!sel || sel->total > FULL_TRACK_MAX) return -1;
  for (; i < s->nall; i++) if (!s->status[i] && s->all[i].num == s->sel) return i;
  for (i = a - 1; i >= 0; i--) if (!s->status[i] && s->all[i].num == s->sel) return i;
  return -1;
}
/* points sharing the Range of point i: the same Cluster, any track, within MERGE_BYTES */
static int group(MkvSession *s, int i, int *g) {
  int n = 0; g[n++] = i; Point *p0 = &s->all[i];
  for (int dir = -1; dir <= 1; dir += 2)
    for (int j = i + dir; j >= 0 && j < s->nall && n < 96; j += dir) {
      Point *p = &s->all[j]; if (fabs(p->t - p0->t) > 30) break;
      if (s->status[j] || p->cl != p0->cl) continue;
      if (p0->rel < 0 || p->rel < 0) { if (p0->rel < 0 && p->rel < 0) g[n++] = j; continue; }
      if (labs(p->rel - p0->rel) <= MERGE_BYTES) g[n++] = j;
    }
  return n;
}
static double rate_wait(double *sent, int *n, int lim) { double now = mono(); int w = 0; for (int i = 0; i < *n; i++) if (now - sent[i] <= 1.0) sent[w++] = sent[i]; *n = w; return *n < lim ? 0 : 1.0 - (now - sent[0]) + 0.005; }
static void sent_push(double *sent, int *n) { if (*n < 16) sent[(*n)++] = mono(); }
/* the block of a point begins at buf[base + h - 5] for the file's Cluster header length h (5..12): the learned length first */
static int find_block(MkvSession *s, cu8 *b, long n, long base, int num, long *offOut, Block *blk) {
  int order[9], no = 0; if (s->hdrLenBest) order[no++] = s->hdrLenBest; for (int h = 5; h <= 12; h++) if (h != s->hdrLenBest) order[no++] = h;
  for (int k = 0; k < no; k++) { int h = order[k]; long off = base + (h - 5); if (off < 0) continue;
    if (read_block(b, n, off, num, blk)) { s->hdrLen[h]++; if (!s->hdrLenBest || s->hdrLen[h] > s->hdrLen[s->hdrLenBest]) s->hdrLenBest = h; *offOut = off; return 1; } }
  return 0;
}
/* no CueRelativePosition: read the Cluster (bounded) and take the text blocks matching the points */
typedef struct { MkvSession *s; int *g, ng; cu8 *b; long n; long long clTs; } ClCtx;
static int each_cluster(void *c_, unsigned id, long d, long long sz, long at) { ClCtx *c = c_; MkvSession *s = c->s;
  if (sz < 0) return 1;
  if (id == ID_TIMESTAMP) { c->clTs = (long long)read_uint(c->b, d, sz); return 0; }
  if (id != ID_BLOCKGROUP && id != ID_SIMPLEBLOCK) return 0;
  double scale = s->info.tscale / 1e9;
  for (int j = 0; j < c->ng; j++) { int i = c->g[j]; if (s->status[i] != 1) continue; Block b;
    if (!read_block(c->b, c->n, at, s->all[i].num, &b) || !b.payload) continue;
    if (fabs((c->clTs + b.ts) * scale - s->all[i].t) < 0.002) { lock(s); add_line(s, i, &b); unlock(s); break; } }
  return 0; }
static int fetch_cluster(MkvSession *s, int *g, int ng) {
  long long cl = s->all[g[0]].cl; unsigned char *h; long hn; int e = range_once(s, cl, 16, &h, &hn); if (e) return e;
  unsigned id; long long sz; int il = read_id(h, hn, 0, &id), sl = il ? read_size(h, hn, il, &sz) : 0; free(h);
  if (!il || id != ID_CLUSTER || !sl || sz < 0 || sz > 16 * 1024 * 1024) { lock(s); for (int k = 0; k < ng; k++) mark_done(s, g[k], 0); unlock(s); return NR_OK; }
  long d = il + sl; unsigned char *b; long n; e = range_full(s, cl, d + (long)sz, &b, &n, NULL); if (e) return e;
  ClCtx c = { s, g, ng, b, n, 0 }; each(b, n, d, n, each_cluster, &c); free(b);
  lock(s); for (int j = 0; j < ng; j++) if (s->status[g[j]] == 1) mark_done(s, g[j], 0); unlock(s);
  return NR_OK;
}
static int fetch_group(MkvSession *s, int *g, int ng) {
  Point *p0 = &s->all[g[0]]; if (p0->rel < 0) return fetch_cluster(s, g, ng);
  long lo = p0->rel, hi = p0->rel; for (int k = 1; k < ng; k++) { if (s->all[g[k]].rel < lo) lo = s->all[g[k]].rel; if (s->all[g[k]].rel > hi) hi = s->all[g[k]].rel; }
  /* CueRelativePosition counts from the Cluster's data start (ID 4 + size 1-8): read from 7 bytes early and find the header length */
  long long start = p0->cl + 5 + lo; long slack = 7;
  unsigned char *b; long n; int e = range_full(s, start, (hi - lo) + slack + BLOCK_BYTES, &b, &n, NULL); if (e) return e; /* a CDN that ends early would lose these lines for good */
  struct { int i; long long abs; long total; } pend[96]; int np = 0;
  lock(s);
  for (int k = 0; k < ng; k++) { int i = g[k]; long off; Block blk;
    if (!find_block(s, b, n, s->all[i].rel - lo, s->all[i].num, &off, &blk)) { mark_done(s, i, 0); continue; }
    if (blk.len < 0) { pend[np].i = i; pend[np].abs = start + off; pend[np].total = blk.total; np++; continue; }
    add_line(s, i, &blk); }
  unlock(s); free(b);
  for (int j = 0; j < np && !s->dead; j++) { /* a long event (karaoke, vector sign) that didn't fit: one more read each */
    unsigned char *b2; long n2; Block blk; int ok = 0;
    if (!range_full(s, pend[j].abs, pend[j].total, &b2, &n2, NULL)) { if (read_block(b2, n2, 0, s->all[pend[j].i].num, &blk) && blk.len > 0) { lock(s); add_line(s, pend[j].i, &blk); unlock(s); ok = 1; } free(b2); }
    if (!ok) { lock(s); mark_done(s, pend[j].i, 0); unlock(s); } }
  return NR_OK;
}
static void check_complete(MkvSession *s) { for (int k = 0; k < s->nts; k++) { TState *t = &s->ts[k]; if (t->state == 1 && t->total && t->done >= t->total) t->state = 2; } }

/* ---- scan mode (no index for a text track, or none at all) ---- */
static int find_cluster_in(cu8 *b, long n) { /* a plausible Cluster start: ID, a size, then a Timestamp (or CRC-32) child */
  for (long i = 0; i + 12 < n; i++) { if (b[i] != 0x1F || b[i + 1] != 0x43 || b[i + 2] != 0xB6 || b[i + 3] != 0x75) continue;
    long long sz; int sl = read_size(b, n, i + 4, &sz); if (!sl) continue; unsigned id; if (read_id(b, n, i + 4 + sl, &id) && (id == ID_TIMESTAMP || id == 0xBF)) return (int)i; }
  return -1;
}
static int scanned_has(MkvSession *s, long long cl) { for (int i = 0; i < s->nscanned; i++) if (s->scanned[i].cl == cl) return 1; return 0; }
static void scanned_add(MkvSession *s, long long cl, double t) { if (scanned_has(s, cl)) return; if (s->nscanned == s->capscanned) { s->capscanned = s->capscanned ? s->capscanned * 2 : 512; s->scanned = realloc(s->scanned, s->capscanned * sizeof *s->scanned); } s->scanned[s->nscanned].cl = cl; s->scanned[s->nscanned].t = t; s->nscanned++; }
static int block_track(cu8 *b, long n, long o, unsigned id, int il, int sl) {
  long d = o + il + sl;
  if (id == ID_BLOCKGROUP) { unsigned c; int cl = read_id(b, n, d, &c); long long cs; int csl = cl ? read_size(b, n, d + cl, &cs) : 0; if (!cl || c != ID_BLOCK || !csl) return -1; d += cl + csl; }
  long long tv; int tl = read_size(b, n, d, &tv); return tl ? (int)tv : -1;
}
/* a block's header alone (enough of it is in b even when its payload runs past the end): its track, its timestamp
   relative to the Cluster, and for a SimpleBlock its keyframe flag (-1 not said: a BlockGroup); 0 the header not here */
static int block_head(cu8 *b, long n, long o, unsigned id, int il, int sl, int *trk, int *tc, int *key) {
  long d = o + il + sl;
  if (id == ID_BLOCKGROUP) { unsigned c; int cl = read_id(b, n, d, &c); long long cs; int csl = cl ? read_size(b, n, d + cl, &cs) : 0; if (!cl || c != ID_BLOCK || !csl) return 0; d += cl + csl; }
  long long tv; int tl = read_size(b, n, d, &tv); if (!tl || d + tl + 3 > n) return 0;
  *trk = (int)tv; *tc = (int16_t)((b[d + tl] << 8) | b[d + tl + 1]); *key = id == ID_SIMPLEBLOCK ? (b[d + tl + 2] & 0x80) != 0 : -1; return 1; }
static int scan_locate(MkvSession *s, double target) {
  Info *i = &s->info;
  if (i->ncls) { int a = 0, z = i->ncls; while (a < z) { int m = (a + z) >> 1; if (i->cls[m].t <= target) a = m + 1; else z = m; }
    ClusterT *c = &i->cls[a > 0 ? a - 1 : 0]; s->cur.on = 1; s->cur.pos = c->cl; s->cur.inCl = 0; s->cur.ts = c->t; return NR_OK; }
  long long segEnd = i->segEnd > 0 ? i->segEnd : i->size, first = i->firstCluster;
  if (first < 0) { /* the head was all attachments (fonts): walk the top-level elements to the first Cluster */
    long long o = i->segStart; for (int steps = 0; steps < 32 && !s->dead; steps++) {
      unsigned char *b; long n; int e = range_once(s, o, 16, &b, &n); if (e) return e;
      unsigned id; long long sz; int il = read_id(b, n, 0, &id), sl = il ? read_size(b, n, il, &sz) : 0; free(b);
      if (!il || !sl || sz < 0) return NR_NET;
      if (id == ID_CLUSTER) { first = i->firstCluster = o; break; }
      o += il + sl + sz; }
    if (first < 0) return NR_NET;
  }
  if (!(i->duration > 0) || !(segEnd > first) || target < 30) { s->cur.on = 1; s->cur.pos = first; s->cur.inCl = 0; s->cur.ts = -1; return NR_OK; }
  double rate = (double)(segEnd - first) / i->duration; long long byte = first + (long long)((target - 10) * rate);
  for (int tries = 0; tries < 6 && !s->dead; tries++) { /* jump by bitrate, then find the next Cluster in the bytes */
    if (byte < first) byte = first; if (byte > segEnd - 1024) byte = segEnd - 1024;
    unsigned char *b; long n; int e = range_once(s, byte, 256 * 1024, &b, &n); if (e) return e;
    int hit = find_cluster_in(b, n);
    if (hit < 0) { byte += n - 16; free(b); continue; }
    long long sz; int sl = read_size(b, n, hit + 4, &sz); double ts = -1;
    long o = hit + 4 + sl; unsigned id; int il = read_id(b, n, o, &id); long long csz; int csl = il ? read_size(b, n, o + il, &csz) : 0;
    if (il && csl && id == ID_TIMESTAMP && csz > 0 && o + il + csl + csz <= n) ts = read_uint(b, o + il + csl, csz) * i->tscale / 1e9;
    free(b);
    long long pos = byte + hit;
    if (ts < 0 || tries >= 4) { s->cur.on = 1; s->cur.pos = pos; s->cur.inCl = 0; s->cur.ts = -1; return NR_OK; }
    if (ts > target + 1) { byte = pos - (long long)((ts - target) * rate) - 512 * 1024; continue; }
    if (ts < target - 90) { byte = pos + (long long)((target - ts) * rate) - 256 * 1024; continue; }
    s->cur.on = 1; s->cur.pos = pos; s->cur.inCl = 0; s->cur.ts = ts; return NR_OK;
  }
  s->cur.on = 1; s->cur.pos = first; s->cur.inCl = 0; s->cur.ts = -1; return NR_OK;
}
/* one chunk from the cursor: the text blocks of the scanned tracks, through as many Clusters as it holds */
static int scan_read(MkvSession *s, double hi, int urgent) {
  Info *i = &s->info; double scale = i->tscale / 1e9;
  long long start = s->cur.pos; long len = s->cur.need ? (long)s->cur.need : urgent ? SCAN_CHUNK_URGENT : SCAN_CHUNK; s->cur.need = 0;
  if (i->size > 0 && start + len > i->size) len = (long)(i->size - start);
  if (len <= 0) { s->cur.on = 0; s->hasScan = 0; return NR_OK; } /* the end of the file */
  unsigned char *b; long n; int e = range_once(s, start, len, &b, &n); if (e) return e;
  long o = 0;
  while (o < n) {
    long long abs = start + o;
    if (s->cur.inCl && abs >= s->cur.end) { lock(s); scanned_add(s, s->cur.clStart, s->cur.ts); unlock(s); s->cur.inCl = 0; s->cur.ts = -1; continue; }
    unsigned id; long long sz; int il = read_id(b, n, o, &id), sl = il ? read_size(b, n, o + il, &sz) : 0;
    if (!il || !sl) break; /* a header cut by the chunk: the next read starts here */
    long hdr = il + sl; long long total = hdr + sz; long d = o + hdr;
    if (!s->cur.inCl) {
      if (id == ID_CLUSTER && sz >= 0) {
        if (scanned_has(s, abs)) { o += (long)total; continue; } /* read before: skip it */
        s->cur.inCl = 1; s->cur.clStart = abs; s->cur.end = abs + total; s->cur.raw = -1; s->cur.ts = -1; o = d; continue; }
      if (sz < 0 || id == ID_EBML || id == ID_SEGMENT) { s->cur.on = 0; free(b); return NR_OK; } /* lost: locate again */
      o += (long)total; continue; /* Cues, Tags, Void... between Clusters */
    }
    if (sz < 0) { s->cur.on = 0; free(b); return NR_OK; }
    if (id == ID_TIMESTAMP) {
      if (d + sz > n) break;
      s->cur.raw = (long long)read_uint(b, d, sz); s->cur.ts = s->cur.raw * scale;
      if (s->cur.ts > hi) { s->cur.pos = s->cur.clStart; s->cur.inCl = 0; free(b); return NR_OK; } /* past the window: wait at this Cluster */
    } else if (id == ID_BLOCKGROUP || id == ID_SIMPLEBLOCK) {
      int num = block_track(b, n, o, id, il, sl); TState *t = num > 0 ? ts_of(s, num) : NULL;
      if (t && t->scan) {
        if (o + total > n) { if (total <= 1024 * 1024) { s->cur.pos = abs; s->cur.need = total; free(b); return NR_OK; } } /* a text block cut by the chunk: read exactly it next */
        else { Block blk; if (read_block(b, n, o, num, &blk) && blk.payload && s->cur.raw >= 0) {
            double st = (s->cur.raw + blk.ts) * scale; unsigned hh = 2166136261u; for (long q = 0; q < blk.plen; q++) { hh ^= blk.payload[q]; hh *= 16777619u; }
            long long key = (((long long)num * 100000000LL + (long long)llround(st * 1000)) << 16) | (hh & 0xffff); /* track, start and content: two lines starting together are both kept */
            lock(s); if (!seen_has(s, key)) { seen_add(s, key); Line ln; char *raw = NULL; if (make_line(s, t, &blk, st, -1, &ln, &raw)) ts_add(t, &ln, -1, key, raw); else free(raw); } unlock(s); } }
      }
    }
    o += (long)total; /* a video/audio block running past the chunk is skipped: the next read starts after it */
  }
  s->cur.pos = start + o; free(b);
  return NR_OK;
}

static void wait_for(MkvSession *s, double secs) {
  if (secs <= 0) return;
  struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); double t = ts.tv_sec + ts.tv_nsec / 1e9 + secs; ts.tv_sec = (time_t)t; ts.tv_nsec = (long)((t - ts.tv_sec) * 1e9);
  lock(s); if (!s->dead) pthread_cond_timedwait(&s->cv, &s->mu, &ts); unlock(s);
}
static const char *errname(int e) { return e == NR_NORANGE ? "norange" : e == NR_HTTP ? "http" : e == NR_BUSY ? "busy" : e == NR_TIMEOUT ? "timeout" : "net"; }
static void fallback(MkvSession *s, const char *why) { lock(s); s->state = MKV_FALLBACK; snprintf(s->err, sizeof s->err, "%s", why); unlock(s); fprintf(stderr, "[mkv] no embedded subtitles from the file here (%s)\n", why); }
static int probe(MkvSession *s) {
  unsigned char *b; long n; long long total; int e = range_full(s, 0, HEAD_BYTES, &b, &n, &total); if (e) return e;
  Info *i = &s->info; memset(i, 0, sizeof *i);
  if (!parse_head(i, b, n)) { free(b); return -1; }
  i->size = total;
  if (!i->haveTracks && i->tracksPos >= 0) { /* Tracks past the head: read it where the SeekHead says */
    unsigned char *b2; long n2; e = range_full(s, i->tracksPos, HEAD_BYTES, &b2, &n2, NULL); if (e) { free(b); return e; }
    unsigned id; long long sz; int il = read_id(b2, n2, 0, &id), sl = il ? read_size(b2, n2, il, &sz) : 0;
    if (!il || id != ID_TRACKS || !sl || sz < 0) { free(b); free(b2); return -2; }
    long d = il + sl; if (d + sz > n2) { free(b2); e = range_full(s, i->tracksPos, d + (long)sz, &b2, &n2, NULL); if (e) { free(b); return e; } }
    parse_tracks(i, b2, n2, d, sz); free(b2);
  }
  free(b);
  if (!i->haveTracks) return -2;
  int sub = 0;
  for (int k = 0; k < i->ntr; k++) { Track *t = &i->tr[k]; if (t->type != 17) continue; t->n = sub++;
    const char *c = t->codec; t->kind = t->encrypted ? 0 : (!strcmp(c, "S_TEXT/UTF8") || !strcmp(c, "S_TEXT/ASCII") || !strcmp(c, "S_TEXT/WEBVTT")) ? 1 : (!strcmp(c, "S_TEXT/ASS") || !strcmp(c, "S_TEXT/SSA") || !strcmp(c, "S_ASS") || !strcmp(c, "S_SSA")) ? 2 : 0; }
  { char l[300] = ""; size_t o = 0; for (int k = 0; k < i->ntr && o + 40 < sizeof l; k++) { Track *t = &i->tr[k]; if (t->type != 17) continue; o += snprintf(l + o, sizeof l - o, "%s%s %s%s", o ? ", " : "", t->codec, t->lang, t->encrypted ? " (encrypted)" : t->hasComp ? " (compressed)" : ""); }
    fprintf(stderr, "[mkv] %d tracks; subtitles: %s\n", i->ntr, l[0] ? l : "none"); } /* (what the file carries, for a text track missed) */
  return NR_OK;
}
static void read_chapters(MkvSession *s) {
  Info *i = &s->info; if (i->haveChapters || i->chapPos < 0) { i->haveChapters = 1; return; }
  if (s->p2p && i->chapPos > P2P_CHAPTERS_MAX) { i->haveChapters = 1; return; }
  unsigned char *b; long n; if (range_full(s, i->chapPos, HEAD_BYTES, &b, &n, NULL)) return;
  unsigned id; long long sz; int il = read_id(b, n, 0, &id), sl = il ? read_size(b, n, il, &sz) : 0;
  if (il && id == ID_CHAPTERS && sl && sz >= 0) { long d = il + sl; lock(s); parse_chapters(i, b, n, d, sz < n - d ? sz : n - d); unlock(s); }
  else i->haveChapters = 1;
  free(b);
}
static void session_free(MkvSession *s) {
  for (int k = 0; k < s->nts; k++) { for (int a = 0; a < s->ts[k].n; a++) { free(s->ts[k].l[a].t); if (s->ts[k].rawOn) free(s->ts[k].raw[a]); } free(s->ts[k].l); free(s->ts[k].pidx); free(s->ts[k].skey); free(s->ts[k].hdr); free(s->ts[k].raw); free(s->ts[k].rser); }
  for (int k = 0; k < s->info.ntr; k++) { free(s->info.tr[k].priv); free(s->info.tr[k].compSet); }
  free(s->info.pts); free(s->info.cls); free(s->info.vk); free(s->all); free(s->status); free(s->tries); free(s->scanned); free(s->seen);
  net_range_free(s->net); pthread_mutex_destroy(&s->mu); pthread_cond_destroy(&s->cv); free(s->url); free(s->keyUrl); free(s);
}
/* ---- MP4: the video track's keyframes from its moov (stss, stts, stsc, stsz, stco/co64, the edit list), for the seek
   previews: each keyframe's exact place and size, its codec's setup (avcC/hvcC) as Matroska's CodecPrivate ---- */
static uint32_t mp_u32(const unsigned char *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint64_t mp_u64(const unsigned char *p) { return (uint64_t)mp_u32(p) << 32 | mp_u32(p + 4); }
/* the first child box of this type inside b..n: its payload (after the header) */
static int mp_find(const unsigned char *b, long n, const char *type, const unsigned char **out, long *on) {
  long o = 0;
  while (o + 8 <= n) { uint64_t sz = mp_u32(b + o); long h = 8; if (sz == 1) { if (o + 16 > n) return 0; sz = mp_u64(b + o + 8); h = 16; } else if (sz == 0) sz = (uint64_t)(n - o);
    if (sz < (uint64_t)h || sz > (uint64_t)(n - o)) return 0;
    if (!memcmp(b + o + 4, type, 4)) { *out = b + o + h; *on = (long)sz - h; return 1; }
    o += (long)sz; }
  return 0; }
static int mp_cmp_t(const void *a, const void *b) { double x = ((const Point *)a)->t, y = ((const Point *)b)->t; return x < y ? -1 : x > y; }
/* one trak: 1 and the keyframes when it is H.264/HEVC video */
static int mp_trak(MkvSession *s, const unsigned char *tk, long tn) {
  const unsigned char *md, *hd, *mh, *mi, *st, *x; long mdn, hdn, mhn, min, stn, xn;
  if (!mp_find(tk, tn, "mdia", &md, &mdn) || !mp_find(md, mdn, "hdlr", &hd, &hdn) || hdn < 12 || memcmp(hd + 8, "vide", 4)) return 0;
  if (!mp_find(md, mdn, "mdhd", &mh, &mhn) || mhn < 24) return 0;
  uint32_t scale = mh[0] == 1 ? (mhn >= 24 ? mp_u32(mh + 20) : 0) : mp_u32(mh + 12); if (!scale) return 0;
  if (!mp_find(md, mdn, "minf", &mi, &min) || !mp_find(mi, min, "stbl", &st, &stn)) return 0;
  /* the codec and its setup */
  if (!mp_find(st, stn, "stsd", &x, &xn) || xn < 16) return 0;
  const unsigned char *en = x + 8; long enn = xn - 8; uint32_t esz = mp_u32(en); if (esz < 8 + 78 || esz > (uint32_t)enn) return 0;
  const char *codec = !memcmp(en + 4, "avc1", 4) || !memcmp(en + 4, "avc3", 4) ? "V_MPEG4/ISO/AVC" : !memcmp(en + 4, "hvc1", 4) || !memcmp(en + 4, "hev1", 4) ? "V_MPEGH/ISO/HEVC" : NULL; if (!codec) return 0;
  const unsigned char *cc; long ccn; if (!mp_find(en + 8 + 78, (long)esz - 8 - 78, codec[6] == '4' ? "avcC" : "hvcC", &cc, &ccn) || ccn <= 0) return 0;
  /* the tables */
  const unsigned char *tts, *tss, *tsc, *tsz, *tco; long ttn, tsn, tcn, tzn, ton; int co64 = 0;
  if (!mp_find(st, stn, "stts", &tts, &ttn) || !mp_find(st, stn, "stss", &tss, &tsn) || !mp_find(st, stn, "stsc", &tsc, &tcn) || !mp_find(st, stn, "stsz", &tsz, &tzn)) return 0;
  if (!mp_find(st, stn, "stco", &tco, &ton)) { if (!mp_find(st, stn, "co64", &tco, &ton)) return 0; co64 = 1; }
  if (ttn < 8 || tsn < 8 || tcn < 8 || tzn < 12 || ton < 8) return 0;
  uint32_t nts = mp_u32(tts + 4), nss = mp_u32(tss + 4), nsc = mp_u32(tsc + 4), fixed = mp_u32(tsz + 4), nsamp = mp_u32(tsz + 8), nch = mp_u32(tco + 4);
  if ((uint64_t)nts * 8 + 8 > (uint64_t)ttn || (uint64_t)nss * 4 + 8 > (uint64_t)tsn || (uint64_t)nsc * 12 + 8 > (uint64_t)tcn || (!fixed && (uint64_t)nsamp * 4 + 12 > (uint64_t)tzn) ||
      (uint64_t)nch * (co64 ? 8 : 4) + 8 > (uint64_t)ton || !nss || !nsc || !nch || !nsamp) return 0;
  const unsigned char *cts = NULL; long ctn = 0; uint32_t nct = 0; if (mp_find(st, stn, "ctts", &cts, &ctn) && ctn >= 8) { nct = mp_u32(cts + 4); if ((uint64_t)nct * 8 + 8 > (uint64_t)ctn) nct = 0; } /* (display times: a B-frame film's keyframes shown later than decoded) */
  /* where it starts: the edit list's first media time */
  double t0 = 0; { const unsigned char *ed, *el; long edn, eln; if (mp_find(tk, tn, "edts", &ed, &edn) && mp_find(ed, edn, "elst", &el, &eln) && eln >= 8 && mp_u32(el + 4) >= 1) {
      int64_t mt = el[0] == 1 ? (eln >= 24 ? (int64_t)mp_u64(el + 16) : -1) : (eln >= 16 ? (int32_t)mp_u32(el + 12) : -1); if (mt > 0) t0 = (double)mt / scale; } }
  Point *vk = malloc(sizeof *vk * nss); if (!vk) return 0; int nvk = 0;
  uint32_t ci2 = 0, cleft = nct ? mp_u32(cts + 8) : 0; uint32_t ti = 0, tleft = nts ? mp_u32(tts + 8) : 0, si = 0, sci = 0, ci = 0, left; uint64_t dts = 0, off = co64 ? mp_u64(tco + 8) : mp_u32(tco + 8);
  left = mp_u32(tsc + 8 + 4); if (nsc > 1 && mp_u32(tsc + 8 + 12) <= 1) left = mp_u32(tsc + 8 + 12 + 4); if (!left) { free(vk); return 0; }
  for (uint32_t i = 0; i < nsamp && si < nss; i++) {
    uint32_t sz = fixed ? fixed : mp_u32(tsz + 12 + 4 * (uint64_t)i);
    if (mp_u32(tss + 8 + 4 * (uint64_t)si) == i + 1) { Point *p = &vk[nvk++]; memset(p, 0, sizeof *p); p->t = ((double)dts + (ci2 < nct ? (double)(int32_t)mp_u32(cts + 8 + 8 * (uint64_t)ci2 + 4) : 0)) / scale - t0; if (p->t < 0) p->t = 0; p->cl = (long long)off; p->rel = 0; p->len = (long)sz; p->num = 1; si++; }
    else while (si < nss && mp_u32(tss + 8 + 4 * (uint64_t)si) < i + 1) si++;
    if (ci2 < nct && (cleft == 0 || --cleft == 0)) { while (++ci2 < nct && !(cleft = mp_u32(cts + 8 + 8 * (uint64_t)ci2))) {} }
    off += sz; dts += ti < nts ? mp_u32(tts + 8 + 8 * (uint64_t)ti + 4) : 0; if (ti < nts && (tleft == 0 || --tleft == 0)) { while (++ti < nts && !(tleft = mp_u32(tts + 8 + 8 * (uint64_t)ti))) {} } /* (an empty run skipped) */
    if (--left == 0) { if (++ci >= nch) break; while (sci + 1 < nsc && mp_u32(tsc + 8 + 12 * (uint64_t)(sci + 1)) <= ci + 1) sci++; left = mp_u32(tsc + 8 + 12 * (uint64_t)sci + 4); if (!left) break;
      off = co64 ? mp_u64(tco + 8 + 8 * (uint64_t)ci) : mp_u32(tco + 8 + 4 * (uint64_t)ci); } }
  if (nvk < 4) { free(vk); return 0; }
  qsort(vk, nvk, sizeof *vk, mp_cmp_t);
  lock(s); Info *f = &s->info; Track *t = &f->tr[0]; memset(t, 0, sizeof *t); t->num = 1; t->type = 1; snprintf(t->codec, sizeof t->codec, "%s", codec);
  t->priv = malloc(ccn); if (t->priv) { memcpy(t->priv, cc, ccn); t->privLen = ccn; } f->ntr = 1; f->haveTracks = 1; free(f->vk); f->vk = vk; f->nvk = nvk; s->mp4 = 1; unlock(s);
  return 1; }
/* the file as MP4: its moov found among the top boxes (a few small reads past the media data), read whole, its video
   track's keyframes listed. 1 done */
static int mp4_load(MkvSession *s) {
  long long o = 0, total = -1; unsigned char *b = NULL; long n = 0;
  for (int k = 0; k < 64 && !s->dead; k++) {
    if (range_full(s, o, 16, &b, &n, &total) || n < 8) { free(b); return 0; } uint64_t sz = mp_u32(b); char ty[5]; memcpy(ty, b + 4, 4); ty[4] = 0;
    if (sz == 1) { if (n < 16) { free(b); return 0; } sz = mp_u64(b + 8); } else if (sz == 0 && total > o) sz = (uint64_t)(total - o); free(b); b = NULL;
    if (k == 0 && strcmp(ty, "ftyp") && strcmp(ty, "moov") && strcmp(ty, "free") && strcmp(ty, "mdat")) return 0; /* (not MP4) */
    if (sz < 8) return 0;
    if (!strcmp(ty, "moov")) { if (sz > (32u << 20)) return 0; /* (a long film's moov is a few MB; more is not worth a small phone's memory) */ if (range_full(s, o, (long)sz, &b, &n, NULL) || n < (long)sz) { free(b); return 0; }
      const unsigned char *bd = b + 8; long bn = n - 8; if (mp_u32(b) == 1) { bd += 8; bn -= 8; } int ok = 0;
      for (long q = 0; q + 8 <= bn && !ok;) { uint64_t bs = mp_u32(bd + q); if (bs < 8 || bs > (uint64_t)(bn - q)) break; if (!memcmp(bd + q + 4, "trak", 4)) ok = mp_trak(s, bd + q + 8, (long)bs - 8); q += (long)bs; }
      free(b); if (ok) fprintf(stderr, "[mkv] MP4: %d video keyframes from its moov (%llu KB at %lld)\n", s->info.nvk, (unsigned long long)(sz >> 10), o); return ok; }
    o += (long long)sz; if (total > 0 && o >= total) return 0; }
  return 0; }
static void *session_main(void *arg) { plat_thread_background(5);
  MkvSession *s = arg; int e = 0;
  static const double retry[] = { 0.5, 1.5, 3, 5 };
  s->net = net_range_new(); if (!s->net) { fallback(s, "net"); goto out; }
  for (int a = 0;; a++) { e = probe(s); if (s->dead) goto out; if (e > 0 && net_err(e) && a < 4) { wait_for(s, retry[a]); continue; } break; } /* a network hiccup (debrid CDN, the engine still starting) is retried */
  if (e == -1 && (mkvWantCues || s->keysOnly) && !s->dead) mp4_load(s); /* (an MP4: its keyframes for the seek previews; its subtitles stay the player's) */
  if (e) { fallback(s, e == -1 ? "notmkv" : e == -2 ? "notracks" : errname(e)); goto out; }
  int any = 0;
  lock(s);
  int local = !strncmp(s->url, "file://", 7) || s->keysOnly; /* (keysOnly: another copy of the film, read for its keyframes alone) */ /* (a file on the phone, a download: its index and chapters only; its subtitles stay the player's own, as before) */
  for (int k = 0; k < s->info.ntr; k++) { Track *t = &s->info.tr[k]; if (local || t->type != 17 || !t->kind || s->nts >= 64) continue; any = 1;
    TState *x = &s->ts[s->nts++]; memset(x, 0, sizeof *x); x->tr = t; if (t->kind == 2) { x->hdr = malloc(sizeof *x->hdr); parse_ass_header(x->hdr, (const char *)t->priv); } }
  if (s->wantN >= 0) { TState *w = ts_by_n(s, s->wantN); s->sel = w ? w->tr->num : -1; }
  s->listed = 1; /* the tracks can be listed and chosen now: their lines follow once the index is read */
  unlock(s);
  if (!any) { read_chapters(s); if (getenv("SB_SEEKSNAP") || getenv("SB_PREVSPIKE") || mkvWantCues || s->keysOnly) { int ce; for (int a = 0;; a++) { ce = load_cues(s); if (s->dead) goto out; if (net_err(ce) && a < 4) { wait_for(s, retry[a]); continue; } break; } /* (a network hiccup reading far into a big file: tried again, as the subtitles' index is) */ if (ce == NR_OK) { int nr = 0; for (int j = 0; j < s->info.nvk; j++) if (s->info.vk[j].rel < 0) nr++; fprintf(stderr, "[mkv] no text tracks; %d video keyframes from the index (%d without a place in their Cluster)\n", s->info.nvk, nr); } else fprintf(stderr, "[mkv] no text tracks; the index not read (%d, at %lld)\n", ce, (long long)s->info.cuesPos); } fallback(s, "notext"); goto out; } /* (SB_SEEKSNAP, development: the keyframes even with no text to read; one read of the index) */
  read_chapters(s);
  for (int a = 0;; a++) { e = load_cues(s); if (s->dead) goto out; if (net_err(e) && a < 4) { wait_for(s, retry[a]); continue; } break; }
  if (e != NR_OK && e != 1) { fallback(s, errname(e)); goto out; }
  lock(s);
  s->nall = 0; s->all = malloc((s->info.npts + 1) * sizeof *s->all);
  for (int k = 0; k < s->nts; k++) { TState *t = &s->ts[k]; int cnt = 0;
    for (int j = 0; j < s->info.npts; j++) if (s->info.pts[j].num == t->tr->num) { s->all[s->nall++] = s->info.pts[j]; cnt++; }
    t->state = 1; if (!cnt) { t->scan = 1; s->hasScan = 1; } else t->total = cnt; }
  qsort(s->all, s->nall, sizeof *s->all, cmp_pt);
  free(s->info.pts); s->info.pts = NULL; s->info.npts = 0; /* each line knows its point now */
  s->status = calloc(s->nall + 1, 1); s->tries = calloc(s->nall + 1, 1);
  s->state = MKV_HARVESTING; s->prunedAt = mono();
  unlock(s);
  { int nr = 0; for (int j = 0; j < s->info.nvk; j++) if (s->info.vk[j].rel < 0) nr++; fprintf(stderr, "[mkv] %d text track(s), %d index points, %d video keyframes (%d without a place in their Cluster)%s\n", s->nts, s->nall, s->info.nvk, nr, s->hasScan ? ", scanning near the playhead for the rest" : ""); }
  int g[96];
  while (!s->dead) {
    lock(s); if (mono() - s->prunedAt > PRUNE_EVERY) prune(s); unlock(s);
    double w = s->backoffUntil - mono(); if (w > 0) { wait_for(s, w); continue; }
    lock(s); int urgent = 0, i = pick(s, &urgent); unlock(s);
    if (i < 0) {
      if (!s->hasScan) { wait_for(s, 1.0); continue; } /* wait for the playhead */
      /* scan: the lines at the playhead are urgent; beyond that gently, never while the player's buffer is low */
      double now = now_pos(s), lo = now - BEHIND_S, hi = now + SCAN_AHEAD_S;
      if (s->cur.on && ((s->cur.ts >= 0 && s->cur.ts < lo - 120) || s->cur.ts > hi + 120)) s->cur.on = 0;
      if (s->cur.on && !s->cur.inCl && s->cur.ts > hi) { wait_for(s, 1.0); continue; }
      int urg = !s->cur.on || s->cur.ts < 0 || s->cur.ts < now + 10;
      if (!urg && s->buffered >= 0 && s->buffered < SCAN_MIN_BUFFER_S) { wait_for(s, 1.0); continue; }
      double rw = rate_wait(s->scanSent, &s->nscanSent, urg ? RATE_URGENT : SCAN_RATE); if (rw > 0) { wait_for(s, rw); continue; }
      sent_push(s->scanSent, &s->nscanSent); double askedAt = mono();
      e = s->cur.on ? scan_read(s, hi, urg && !s->p2p) : scan_locate(s, now - 4 > 0 ? now - 4 : 0);
      if (s->dead) break;
      if (s->jumpAt >= askedAt) { lock(s); prune(s); unlock(s); }
      if (e && (e == NR_NORANGE || e == NR_HTTP)) { /* a server that will never answer these reads: given up as the index path does, not retried for ever */
        lock(s); int have = 0; for (int k = 0; k < s->nts; k++) have += s->ts[k].n; unlock(s);
        if (!have || ++s->fails > 6) { fallback(s, errname(e)); break; } }
      if (e) { s->backoffMs = s->backoffMs * 2; if (s->backoffMs < 1000) s->backoffMs = 1000; if (s->backoffMs > 8000) s->backoffMs = 8000; s->backoffUntil = mono() + s->backoffMs / 1000; } else { s->backoffMs = 0; s->fails = 0; }
      continue;
    }
    double rw = rate_wait(s->sent, &s->nsent, urgent ? RATE_URGENT : RATE); if (rw > 0) { wait_for(s, rw); continue; }
    lock(s); int ng = group(s, i, g); for (int k = 0; k < ng; k++) s->status[g[k]] = 1; unlock(s);
    sent_push(s->sent, &s->nsent); double askedAt = mono();
    e = fetch_group(s, g, ng);
    if (s->dead) break;
    lock(s);
    if (s->jumpAt >= askedAt) prune(s); /* the playhead moved while this was in flight */
    if (e) {
      for (int k = 0; k < ng; k++) if (s->status[g[k]] == 1) s->status[g[k]] = 0;
      s->fails++;
      if (e == NR_NORANGE || e == NR_HTTP) { int have = 0; for (int k = 0; k < s->nts; k++) have += s->ts[k].n; if (!have || s->fails > 6) { unlock(s); fallback(s, errname(e)); break; } }
      int calm = e == NR_BUSY || (e == NR_TIMEOUT && !s->p2p); /* the server asks for calm: 10 s doubling to 60; else 0.5..8 s (a torrent's timeout is a piece still coming) */
      s->backoffMs = calm ? fmin(15000, fmax(4000, s->backoffMs * 2)) : fmin(8000, fmax(500, s->backoffMs * 2)); /* (a busy server: 4 s doubling to 15, was 10 to 60: the lines at the playhead waited a minute) */
      s->backoffUntil = mono() + s->backoffMs / 1000;
    } else { s->fails = 0; s->backoffMs = 0; }
    check_complete(s);
    unlock(s);
  }
out:
  lock(s); int last = --s->refs == 0; unlock(s);
  if (last) session_free(s);
  return NULL;
}

/* ---- the player's side ---- */
static MkvSession *open_as(const char *url, double startPos, int p2p, int keysOnly) {
  MkvSession *s = calloc(1, sizeof *s); if (!s) return NULL; s->keysOnly = keysOnly;
  s->url = strdup(url); s->p2p = p2p; s->pos = startPos > 0 ? startPos : 0; s->posAt = mono(); s->buffered = -1; s->sel = -1; s->wantN = -1; s->refs = 2;
  pthread_mutex_init(&s->mu, NULL); pthread_cond_init(&s->cv, NULL);
  if (pthread_create(&s->th, NULL, session_main, s)) { s->refs = 1; s->state = MKV_FALLBACK; snprintf(s->err, sizeof s->err, "thread"); return s; }
  pthread_detach(s->th);
  return s;
}
MkvSession *mkv_open(const char *url, double startPos, int p2p) { return open_as(url, startPos, p2p, 0); }
MkvSession *mkv_open_keys(const char *url) { return open_as(url, 0, 0, 1); } /* (its keyframe index and video track alone: no subtitle lines read) */
void mkv_close(MkvSession *s) {
  if (!s) return;
  lock(s); s->dead = 1; pthread_cond_signal(&s->cv); int last = --s->refs == 0; unlock(s);
  if (last) session_free(s); /* else the thread frees it after its current read */
}
int mkv_state(MkvSession *s) { if (!s) return MKV_FALLBACK; lock(s); int v = s->state; unlock(s); return v; }
const char *mkv_error(MkvSession *s) { return s ? s->err : "none"; }
int mkv_tracks(MkvSession *s, MkvTrack *out, int max) {
  if (!s) return 0; lock(s); int n = 0;
  if (s->listed) for (int k = 0; k < s->info.ntr; k++) { Track *t = &s->info.tr[k]; if (t->type != 17) continue;
    if (n < max) { MkvTrack *o = &out[n]; memset(o, 0, sizeof *o); o->n = t->n; snprintf(o->lang, sizeof o->lang, "%s", t->lang); snprintf(o->name, sizeof o->name, "%s", t->name); snprintf(o->codec, sizeof o->codec, "%s", t->codec); o->def = t->def; o->forced = t->forced; o->sdh = t->sdh; o->text = t->kind != 0; }
    n++; }
  unlock(s); return n < max ? n : max;
}
void mkv_select(MkvSession *s, int n) {
  if (!s) return; lock(s); s->wantN = n; TState *t = n >= 0 ? ts_by_n(s, n) : NULL; s->sel = t ? t->tr->num : -1; pthread_cond_signal(&s->cv); unlock(s);
}
void mkv_set_pos(MkvSession *s, double t, double buffered) {
  if (!s || !(t >= 0)) return;
  lock(s); int jumped = fabs(t - now_pos(s)) > 20; s->moving = t > s->pos && t - s->pos < 5; s->pos = t; s->posAt = mono(); s->buffered = buffered >= 0 ? buffered : -1;
  if (jumped) { s->jumpAt = mono(); s->cur.on = 0; if (s->state == MKV_HARVESTING) prune(s); pthread_cond_signal(&s->cv); }
  unlock(s);
}
/* the audio tracks in the file's order, from the head already read for the subtitles (no read of their own) */
int mkv_audio_tracks(MkvSession *s, MkvAudio *out, int max) {
  if (!s) return 0; lock(s); int n = 0;
  if (s->listed) for (int k = 0; k < s->info.ntr && n < max; k++) { Track *t = &s->info.tr[k]; if (t->type != 2) continue;
    MkvAudio *o = &out[n]; memset(o, 0, sizeof *o); o->n = n; o->num = t->num; snprintf(o->lang, sizeof o->lang, "%s", t->lang); snprintf(o->name, sizeof o->name, "%s", t->name);
    snprintf(o->codec, sizeof o->codec, "%s", t->codec); o->channels = t->channels ? t->channels : 2; o->def = t->def; n++; }
  unlock(s); return n;
}
int mkv_dovi(MkvSession *s, int *el) {
  if (!s) return -1; lock(s); int p = -1;
  if (s->listed) { p = 0; for (int k = 0; k < s->info.ntr; k++) if (s->info.tr[k].type == 1) { p = s->info.tr[k].dvProfile; if (el) *el = s->info.tr[k].dvEl; break; } }
  unlock(s); return p;
}
int mkv_video_codec(MkvSession *s, char *out, int cap) {
  if (!s || cap <= 0) return 0; lock(s); out[0] = 0;
  if (s->listed) for (int k = 0; k < s->info.ntr; k++) if (s->info.tr[k].type == 1) { snprintf(out, cap, "%s", s->info.tr[k].codec); break; }
  unlock(s); return out[0] != 0;
}
/* ---- for the styled renderer ---- */
void mkv_style(MkvSession *s, int n, int on) { /* the events of track n kept as they are; the lines read so far are read again with them */
  if (!s) return; lock(s); TState *t = ts_by_n(s, n);
  if (t && t->tr->kind == 2 && t->rawOn != !!on) {
    for (int a = 0; a < t->n; a++) { free(t->l[a].t); if (t->rawOn) free(t->raw[a]); }
    for (int i = 0; i < s->nall && s->status; i++) if (s->all[i].num == t->tr->num && s->status[i] >= 2) { s->status[i] = 0; if (s->tries) s->tries[i] = 0; t->done--; } /* (signs with no text were read as empty) */
    t->n = 0; if (t->state == 2) t->state = 1; free(t->raw); free(t->rser); t->raw = NULL; t->rser = NULL; t->rawOn = !!on; raw_fit(t);
    s->nseen = 0; s->nscanned = 0; s->cur.on = 0; /* (scanned Clusters: read again too) */
    pthread_cond_signal(&s->cv); }
  unlock(s);
}
int mkv_events_since(MkvSession *s, int n, unsigned *cursor, void (*fn)(void *ud, const char *ev, double s, double e), void *ud) {
  if (!s) return 0; lock(s); TState *t = ts_by_n(s, n); int k = 0; unsigned top = *cursor;
  if (t && t->rawOn) for (int a = 0; a < t->n; a++) if (t->rser[a] > *cursor && t->raw[a]) { fn(ud, t->raw[a], t->l[a].s, t->l[a].e); k++; if (t->rser[a] > top) top = t->rser[a]; }
  *cursor = top; unlock(s); return k;
}
char *mkv_track_header(MkvSession *s, int n) {
  if (!s) return NULL; lock(s); TState *t = ts_by_n(s, n); char *h = t && t->tr->priv ? strdup((const char *)t->tr->priv) : NULL; unlock(s); return h;
}
long long mkv_attachments_at(MkvSession *s) { if (!s) return -1; lock(s); long long p = s->listed ? s->info.attachPos : -1; unlock(s); return p; }
int mkv_has_index(MkvSession *s) { if (!s) return -1; lock(s); int r = s->listed ? s->info.cuesPos >= 0 : -1; unlock(s); return r; }
int mkv_listed(MkvSession *s) { if (!s) return 0; lock(s); int l = s->listed; unlock(s); return l; }
/* ready: it has lines near the playhead, or everything there has been read (a quiet stretch has none to show) */
int mkv_track_status(MkvSession *s, int n) {
  if (!s) return -1; lock(s); int r = 0; TState *t = ts_by_n(s, n);
  if (s->state == MKV_FALLBACK) r = -1;
  else if (s->listed && !t) r = -1; /* not a text track */
  else if (t && s->state == MKV_HARVESTING) {
    double now = now_pos(s), lo = now - BEHIND_S, hi = now + READY_AHEAD_S;
    for (int i = 0; i < t->n && !r; i++) if (t->l[i].e >= lo && t->l[i].s <= hi) r = 1;
    if (!r && t->scan) r = s->cur.on && s->cur.ts > now + 3; /* scanned past the playhead (no index): nothing before it can be missing; the scan reads on ahead (it waited for the whole look-ahead, which a big file without an index took minutes to cover: the subtitle seemed to load for ever) */
    else if (!r) { int a = 0, z = s->nall; while (a < z) { int m = (a + z) >> 1; if (s->all[m].t < lo) a = m + 1; else z = m; }
      r = 1; for (int i = a; i < s->nall && s->all[i].t <= hi; i++) if (s->all[i].num == t->tr->num && s->status[i] != 2) { r = 0; break; } } /* a point read without its line is not covered: the TV keeps drawing there */
  }
  unlock(s); return r;
}
/* the video keyframe nearest t within tol s (the Cues' own): 1 and *out, else 0 (no index yet, none near, no video cues) */
int mkv_key_near(MkvSession *s, double t, double tol, double *out) {
  if (!s) return 0; lock(s); int r = 0; const Point *v = s->info.vk; int n = s->info.nvk;
  if (n) { int lo = 0, hi = n - 1; while (lo < hi) { int m = (lo + hi) / 2; if (v[m].t < t) lo = m + 1; else hi = m; } /* first at or after t */
    double best = -1, bd = tol + 1; for (int k = lo - 1; k <= lo; k++) if (k >= 0 && k < n) { double d = fabs(v[k].t - t); if (d < bd) { bd = d; best = v[k].t; } }
    if (best >= 0 && bd <= tol) { *out = best; r = 1; } }
  unlock(s); return r;
}
/* the video keyframe at or before t, within tol s: 1 and *out, else 0 */
int mkv_key_before(MkvSession *s, double t, double tol, double *out) {
  if (!s) return 0; lock(s); int r = 0; const Point *v = s->info.vk; int n = s->info.nvk;
  if (n) { int lo = 0, hi = n; while (lo < hi) { int m = (lo + hi) / 2; if (v[m].t <= t + 1e-6) lo = m + 1; else hi = m; } /* lo: the first after t */
    if (lo > 0 && t - v[lo - 1].t <= tol) { *out = v[lo - 1].t; r = 1; } }
  unlock(s); return r;
}
/* seek previews: the first video track's codec and CodecPrivate (a copy, freed by the caller): 1 when its frames can be
   handed on as stored (no compression, or header stripping put back here), 0 not (zlib, encryption), -1 no video / not read yet */
int mkv_video_track(MkvSession *s, char *codec, int cap, unsigned char **priv, long *privLen) {
  if (!s) return -1; lock(s); int r = -1; *priv = NULL; *privLen = 0;
  if (s->listed || s->info.haveTracks) for (int k = 0; k < s->info.ntr; k++) { const Track *t = &s->info.tr[k]; if (t->type != 1) continue;
    snprintf(codec, cap, "%s", t->codec); if (t->priv && t->privLen > 0) { *priv = malloc(t->privLen); if (*priv) { memcpy(*priv, t->priv, t->privLen); *privLen = t->privLen; } }
    r = t->encrypted ? 0 : (!t->hasComp || t->compAlgo == 3) ? 1 : 0; break; }
  unlock(s); return r;
}
/* keyframe k's frame bytes, read through the caller's own handle: the block at its Cluster + CueRelativePosition (the
   first read `guess` bytes, a second for the rest of a bigger frame), checked a key block of the video track, header
   stripping put back. 0 and *frame (malloc'd), < 0: -1 a read failed, -2 no relative position (not read: a Cluster
   scan is a later step), -3 not a key block / laced / unreadable, -4 over maxBytes */
/* a cue with no relative position (many remuxes): its Cluster's headers walked from its start to the video keyframe at
   the cue's time, payloads skipped, never downloaded (each read starts at the next header); at most 4 reads. 0 and
   *rel (the block's offset from the Cluster's data, as a cue gives it), -1 a read failed, -3 not found */
static int walk_cluster(MkvSession *s, NetRange *net, const Point *v, long guess, long *rel, long *bytes, int *reqs, const unsigned char *pre, long pren) { /* pre: the cluster's first bytes, already read (a batch's part) */
  double scale = s->info.tscale / 1e9; long long bufAt = v->cl, dataAt = -1, clTs = -1; unsigned char *b = NULL; long n = 0, o = 0; int clHdr = 0;
  for (int rq = 0; rq < 8; rq++) {
    free(b); b = NULL; if (rq == 0 && pre && pren > 0) { b = malloc(pren); if (!b) return -1; memcpy(b, pre, pren); n = pren; } else { int e = range_with(s, net, bufAt, guess, &b, &n, NULL); (*reqs)++; if (e) return -1; *bytes += n; } o = 0;
    if (!clHdr) { unsigned id; int il = read_id(b, n, 0, &id); long long csz; int sl = il ? read_size(b, n, il, &csz) : 0; if (!il || id != ID_CLUSTER || !sl) { free(b); return -3; } o = il + sl; dataAt = bufAt + o; clHdr = 1; }
    for (;;) {
      unsigned id; int il = read_id(b, n, o, &id); long long sz; int sl = il ? read_size(b, n, o + il, &sz) : 0;
      if (!il || !sl) break;                                   /* (its header cut: the next read starts at it) */
      if (sz < 0 || id == ID_CLUSTER) { free(b); return -3; }  /* (the next Cluster, or a size we can't step over) */
      long d = o + il + sl; long long end = d + sz;
      if (id == ID_TIMESTAMP) { if (end > n) break; clTs = (long long)read_uint(b, d, sz); }
      else if (id == ID_SIMPLEBLOCK || id == ID_BLOCKGROUP) { int trk, tc, key; if (!block_head(b, n, o, id, il, sl, &trk, &tc, &key)) break; /* (its header cut: the next read starts at it) */
        if (trk == v->num && clTs >= 0) { double bt = (clTs + tc) * scale; /* the cue's own block: its time within a tick, and a keyframe where the block says (never a block taken unchecked because its payload runs past what was read) */
          if (fabs(bt - v->t) <= scale + 0.002 && key != 0) { *rel = (long)(bufAt + o - dataAt); free(b); return 0; }
          if (bt > v->t + 1) { free(b); return -3; } } } /* (past its time: not in this Cluster) */
      if (end > n && end - n <= (1L << 20)) { long need = (long)(end - o) + 262144; if (guess < need) guess = need < (2L << 20) ? need : (2L << 20); break; } /* (a block running past what was read: read on from it, one longer read, rather than a request per block) */
      o = (long)end; if (o >= n) break; } /* (a big payload: stepped over, never downloaded) */
    bufAt += o; if (guess < (1L << 20)) guess *= 2; }
  free(b); return -3; }
/* ---- a keyframe the index gives no place for, read where it most likely lies ----
   Walking its Cluster from the start costs the whole stretch before it (about 1 MB a picture where keyframes sit deep).
   Instead: the Cluster's first bytes give its time and size (256 bytes); the keyframe's offset is estimated from how
   far into the Cluster its time is (the local byte rate from the index around it, and, once learnt in this file, the
   Cluster's size over how long its Clusters last); a modest window there is searched for the block itself: the
   element's ID, its size, the video track's number and the exact timestamp relative to the Cluster (and the keyframe
   flag of a SimpleBlock), with the element after it checked when it is in hand. A window without it still holds
   other blocks, found the same way and proven by the blocks that follow them: their timestamps say which way and
   how far, for one more window. Then the block's own bytes are read exactly. Anything else: the old way (-5). */
static int aim_hdr(cu8 *b, long n, long o, const unsigned char *trks, int *t, int *tc, int *key, long *total) {
  if (o >= n || (b[o] != ID_SIMPLEBLOCK && b[o] != ID_BLOCKGROUP)) return 0; long long sz; int sl = read_size(b, n, o + 1, &sz); if (!sl || sz < 5 || sz > 4 * 1024 * 1024) return 0;
  if (!block_head(b, n, o, b[o], 1, sl, t, tc, key)) return 0; if (*t <= 0 || *t > 255 || !trks[*t]) return 0; *total = 1 + sl + (long)sz; return 1; }
static int aim_next_ok(cu8 *b, long n, long p) { if (p >= n) return 1; unsigned c = b[p]; /* what may follow a block in a Cluster (or the Cluster itself) */
  return c == ID_SIMPLEBLOCK || c == ID_BLOCKGROUP || c == ID_TIMESTAMP || c == 0xAB || c == 0xA7 || c == 0xBF || c == 0xEC || c == 0x1F || c == 0x1C || c == 0x12 || c == 0x11 || c == 0x16 || c == 0x10 || c == 0x19; }
static int aim_frame(const Track *tr, int num, cu8 *b, long n, unsigned char **frame, long *flen) {
  Block blk; if (!read_block(b, n, 0, num, &blk) || blk.len < 0 || !blk.payload || blk.plen <= 0) return -3;
  long ol; unsigned char *d = decode_payload(tr, blk.payload, blk.plen, &ol); if (!d) return -3; *frame = d; *flen = ol; return 0; }
static int cmp_long(const void *a, const void *b) { long x = *(const long *)a, y = *(const long *)b; return x < y ? -1 : x > y; }
static int cmp_float(const void *a, const void *b) { float x = *(const float *)a, y = *(const float *)b; return x < y ? -1 : x > y; }
static void aim_give_up(MkvSession *s, MkvAim *a) { lock(s); if (a->k < s->info.nvk && s->info.vk[a->k].cl == a->cl && s->info.vk[a->k].rel < 0) s->info.vk[a->k].rel = -2; /* (this one: the old way from now on) */
  s->aim.miss++; if (!s->aim.off && s->aim.tried >= 12 && s->aim.miss * 2 > s->aim.tried) { s->aim.off = 1; fprintf(stderr, "[mkv] keyframes not found where expected (%d of %d): their Clusters read from the start in this file\n", s->aim.miss, s->aim.tried); } unlock(s); }
int mkv_aim_start(MkvSession *s, int k, MkvAim *a) { memset(a, 0, sizeof *a); if (!s) return 0; lock(s);
  int ok = k >= 0 && k < s->info.nvk && !s->dead && !s->mp4 && !s->aim.off && s->info.vk[k].rel == -1;
  if (ok) { const Point *v = s->info.vk; int n = s->info.nvk, i0 = k >= 2 ? k - 2 : 0, i1 = k + 2 < n ? k + 2 : n - 1; a->k = k; a->cl = v[k].cl; a->num = v[k].num; a->t = v[k].t; a->at = a->cl; a->len = 256;
    double dt = v[i1].t - v[i0].t; long long db = v[i1].cl - v[i0].cl; a->rate = dt > 0.5 && db > 0 ? (double)db / dt : s->info.duration > 0 && s->info.size > 0 ? s->info.size / s->info.duration : 750000; /* (bytes a second around it) */
    s->aim.tried++; }
  unlock(s); return ok; }
void mkv_aim_end(MkvAim *a) { free(a->part); a->part = NULL; }
/* the bytes asked for (a->at, a->len): 0 the frame, 1 ask again (a->at, a->len), -1 a read came short, -3 / -4 as mkv_key_read, -5 not found this way */
int mkv_aim_feed(MkvSession *s, MkvAim *a, const unsigned char *b, long n, long maxBytes, unsigned char **frame, long *flen) {
  *frame = NULL; *flen = 0; double scale; unsigned char trks[256]; const Track *tr = NULL; memset(trks, 0, sizeof trks);
  lock(s); scale = s->info.tscale / 1e9; for (int j = 0; j < s->info.ntr; j++) { int nu = s->info.tr[j].num; if (nu > 0 && nu < 256) trks[nu] = 1; if (nu == a->num) tr = &s->info.tr[j]; } s->aim.bytes += n; unlock(s);
  if (!tr || !b || n <= 0) return -1; long o = 0, fo = -1, ftot = 0;
  if (a->stage == 0) { /* the Cluster's first bytes: its time, its size, where its blocks start */
    unsigned id; int il = read_id(b, n, 0, &id); long long csz; int sl = il ? read_size(b, n, il, &csz) : 0; if (!il || id != ID_CLUSTER || !sl) { aim_give_up(s, a); return -5; }
    o = il + sl; a->dataAt = a->cl + o; a->clEnd = csz > 0 ? a->dataAt + csz : -1; a->clTs = -1;
    for (;;) { long long sz; il = read_id(b, n, o, &id); sl = il ? read_size(b, n, o + il, &sz) : 0; if (!il || !sl || sz < 0) break;
      if (id == ID_TIMESTAMP) { if (o + il + sl + sz > n || sz > 8) break; a->clTs = (long long)read_uint(b, o + il + sl, sz); }
      else if (id == ID_SIMPLEBLOCK || id == ID_BLOCKGROUP) break; else if (id == ID_CLUSTER) { o = n; break; }
      o += il + sl + (long)sz; if (o >= n) break; }
    if (a->clTs < 0 || o >= n) { aim_give_up(s, a); return -5; } /* (o: its first block) */
    long long ticks = llround(a->t / scale) - a->clTs; if (ticks < 0 || ticks > 32767) { aim_give_up(s, a); return -5; } a->tc = (int)ticks; a->minAt = a->cl + o;
    lock(s); double e1 = ticks * scale * a->rate, est = e1; long half = 96 * 1024;
    if (s->aim.nd >= 4 && csz > 0) { float d[16]; int nd = s->aim.nd < 16 ? s->aim.nd : 16; memcpy(d, s->aim.dur, nd * sizeof *d); qsort(d, nd, sizeof *d, cmp_float); double e2 = (double)csz * ticks / d[nd / 2]; est = (e1 + e2) / 2; } /* (and by the Cluster's own size, once it is known how long this file's Clusters last) */
    a->est = (long)est;
    if (s->aim.ne >= 5) { long er[16], ab[16]; int ne = s->aim.ne < 16 ? s->aim.ne : 16; memcpy(er, s->aim.err, ne * sizeof *er); qsort(er, ne, sizeof *er, cmp_long); long bias = er[ne / 2]; for (int i = 0; i < ne; i++) ab[i] = labs(er[i] - bias); qsort(ab, ne, sizeof *ab, cmp_long);
      est += bias; half = ab[ne / 2] * 3 / 2; if (half < 64 * 1024) half = 64 * 1024; if (half > 192 * 1024) half = 192 * 1024; } /* (how far off the last ones were: the window as wide as half of them need) */
    unlock(s);
    if (csz > 0 && est > csz - 1024) est = (double)csz - 1024; if (est < 0) est = 0;
    a->at = a->dataAt + (long long)est - half; if (a->at < a->minAt) a->at = a->minAt; a->len = 2 * half + 4096;
    if (a->clEnd > 0 && a->at + a->len > a->clEnd) a->len = (long)(a->clEnd - a->at); if (a->len < 16) { aim_give_up(s, a); return -5; }
    a->stage = 1; return 1; }
  if (a->stage == 2) { /* the rest of its block */
    if (n < a->len) return -1; unsigned char *w = realloc(a->part, a->partLen + n); if (!w) return -3; a->part = w; memcpy(w + a->partLen, b, n); a->partLen += n;
    return aim_frame(tr, a->num, a->part, a->partLen, frame, flen); }
  /* a window: the block itself */
  for (o = 0; o + 8 < n && fo < 0; o++) { if (b[o] != ID_SIMPLEBLOCK && b[o] != ID_BLOCKGROUP) continue; int t, tc, key; long tot;
    if (!aim_hdr(b, n, o, trks, &t, &tc, &key, &tot) || t != a->num || tc != a->tc || key == 0) continue; if (o + tot < n && !aim_next_ok(b, n, o + tot)) continue; fo = o; ftot = tot; }
  if (fo >= 0) { long rel = (long)(a->at + fo - a->dataAt);
    lock(s); s->aim.hit++; s->aim.err[s->aim.ne++ % 16] = rel - a->est; if (a->tc > 150 && rel > 4096 && a->clEnd > 0) s->aim.dur[s->aim.nd++ % 16] = (float)((double)a->tc * (a->clEnd - a->dataAt) / rel);
    if (a->k < s->info.nvk && s->info.vk[a->k].cl == a->cl) s->info.vk[a->k].rel = rel; unlock(s); /* (its place kept: the next read goes straight to it) */
    if (ftot > maxBytes) return -4; if (fo + ftot <= n) return aim_frame(tr, a->num, b + fo, ftot, frame, flen);
    a->part = malloc(n - fo); if (!a->part) return -3; memcpy(a->part, b + fo, n - fo); a->partLen = n - fo; a->at += n; a->len = ftot - a->partLen; a->stage = 2; return 1; }
  if (a->tries++ >= 1) { aim_give_up(s, a); return -5; }
  /* not in it: the blocks it does hold (each proven by the two after it) say which way the keyframe lies */
  long lk = -1; for (o = 0; o + 8 < n && lk < 0; o++) { if (b[o] != ID_SIMPLEBLOCK && b[o] != ID_BLOCKGROUP) continue; int t, tc, key; long tot, tot2;
    if (!aim_hdr(b, n, o, trks, &t, &tc, &key, &tot) || o + tot + 8 >= n) continue; long e = o + tot;
    if (aim_hdr(b, n, e, trks, &t, &tc, &key, &tot2)) { if (e + tot2 < n && !aim_next_ok(b, n, e + tot2)) continue; if (e + tot2 + 8 < n && !aim_hdr(b, n, e + tot2, trks, &t, &tc, &key, &tot) && b[e + tot2] != 0x1F) continue; lk = o; }
    else if (b[e] == 0x1F && e + 4 <= n && b[e + 1] == 0x43 && b[e + 2] == 0xB6 && b[e + 3] == 0x75) lk = o; }
  if (lk < 0) { aim_give_up(s, a); return -5; }
  long off0 = -1, offL = -1; int tc0 = 0, tcL = 0, first = 0, top = -40000, vtop = -40000, past = 0;
  for (o = lk; o + 8 < n;) { int t, tc, key; long tot; if (aim_hdr(b, n, o, trks, &t, &tc, &key, &tot)) { if (off0 < 0) { off0 = o; tc0 = tc; first = tc; } offL = o; tcL = tc; if (tc > top) top = tc; if (t == a->num && tc > vtop) vtop = tc; o += tot; continue; }
    if (b[o] == 0x1F) { past = 1; break; } unsigned id; long long sz; int il = read_id(b, n, o, &id), sl = il ? read_size(b, n, o + il, &sz) : 0; if (!il || !sl || sz < 0 || !aim_next_ok(b, n, o)) break; o += il + sl + (long)sz; }
  double rpt = a->rate * scale; if (offL > off0 && tcL > tc0) { double r2 = (double)(offL - off0) / (tcL - tc0); if (r2 < rpt / 4) r2 = rpt / 4; if (r2 > rpt * 4) r2 = rpt * 4; rpt = r2; } /* (bytes a tick, as this stretch goes) */
  int hi = vtop > -40000 ? vtop : top; long long at2; long len2; /* (the video's own blocks tell best: all before a keyframe are earlier than it) */
  if (hi > a->tc) { if (a->at + off0 <= a->minAt) { aim_give_up(s, a); return -5; } double back = (first > a->tc ? (first - a->tc) * rpt * 1.5 : 0) + 160 * 1024; if (back > 1024 * 1024) back = 1024 * 1024;
    at2 = a->at + off0 - (long long)back; if (at2 < a->minAt) at2 = a->minAt; len2 = (long)(a->at + off0 - at2) + 64; } /* (before this window) */
  else if (!past) { double fwd = (a->tc - hi) * rpt * 1.5 + 160 * 1024; if (fwd > 1024 * 1024) fwd = 1024 * 1024; at2 = a->at + offL; len2 = (long)fwd; if (a->clEnd > 0 && at2 + len2 > a->clEnd) len2 = (long)(a->clEnd - at2); if (len2 < 16) { aim_give_up(s, a); return -5; } } /* (after it: from its last block on) */
  else { aim_give_up(s, a); return -5; }
  a->at = at2; a->len = len2; return 1; }
/* the same, one keyframe on its own connection: 0 the frame, -1 a read failed, -3 / -4, -5 not found this way */
static int aim_read(MkvSession *s, NetRange *net, int k, long maxBytes, unsigned char **frame, long *flen, long *bytes, int *reqs) {
  MkvAim a; if (!mkv_aim_start(s, k, &a)) return -5; int r = 1;
  for (int i = 0; i < 5 && r == 1; i++) { unsigned char *b = NULL; long n = 0; int e = range_with(s, net, a.at, a.len, &b, &n, NULL); (*reqs)++; if (e) { free(b); r = -1; break; } *bytes += n; r = mkv_aim_feed(s, &a, b, n, maxBytes, frame, flen); free(b); }
  mkv_aim_end(&a); if (r == 1) { aim_give_up(s, &a); r = -5; } return r; }
int mkv_range(MkvSession *s, NetRange *net, long long start, long len, unsigned char **out, long *got) { return range_with(s, net, start, len, out, got, NULL); }
int mkv_key_read(MkvSession *s, NetRange *net, int k, long guess, long maxBytes, unsigned char **frame, long *flen, double *t, long *bytes, int *reqs) {
  *frame = NULL; *flen = 0; *bytes = 0; *reqs = 0; if (!s || !net) return -1;
  lock(s); if (k < 0 || k >= s->info.nvk || s->dead) { unlock(s); return -1; } Point v = s->info.vk[k]; const Track *tr = NULL;
  for (int j = 0; j < s->info.ntr; j++) if (s->info.tr[j].num == v.num) tr = &s->info.tr[j]; unlock(s);
  if (t) *t = v.t; if (!tr) return -3;
  if (s->mp4) { if (v.len <= 0) return -3; if (v.len > maxBytes) return -4; unsigned char *b; long n; int e = range_with(s, net, v.cl, v.len, &b, &n, NULL); (*reqs)++; if (e) return -1; *bytes += n;
    if (n < v.len) { free(b); return -1; } *frame = b; *flen = n; return 0; } /* (an MP4: its exact bytes, one read) */
  if (guess < 64 * 1024) guess = 64 * 1024;
  if (v.rel == -1) { int ar = aim_read(s, net, k, maxBytes, frame, flen, bytes, reqs); if (ar != -5) return ar; } /* (no place in the index: read where it most likely lies; not found there, its Cluster walked as before) */
  if (v.rel < 0) { long r2 = -1; int w = walk_cluster(s, net, &v, guess, &r2, bytes, reqs, NULL, 0); if (w) return w; v.rel = r2; lock(s); if (k < s->info.nvk && s->info.vk[k].cl == v.cl) s->info.vk[k].rel = r2; unlock(s); } /* (no position in the index: found by walking its Cluster, and kept for the next time) */
  long long start = v.cl + 5 + v.rel; long slack = 7; unsigned char *b; long n; /* (the Cluster's data starts 5 to 12 bytes in: read from its earliest and find the block) */
  int e = range_with(s, net, start, slack + guess, &b, &n, NULL); (*reqs)++; if (e) return -1; *bytes += n;
  int r = mkv_key_from(s, net, k, b, n, maxBytes, frame, flen, bytes, reqs); free(b); return r;
}
/* a keyframe from bytes read at mkv_key_where's start (a batch's part): the block found in its first bytes, its tail read
   when the window cut it; 0 and *frame, -1 a read failed, -3 not a usable key block, -4 over maxBytes (b stays the caller's) */
int mkv_key_from(MkvSession *s, NetRange *net, int k, const unsigned char *b0, long n, long maxBytes, unsigned char **frame, long *flen, long *bytes, int *reqs) {
  *frame = NULL; *flen = 0; lock(s); if (k < 0 || k >= s->info.nvk || s->dead || s->info.vk[k].rel < 0) { unlock(s); return -1; } Point v = s->info.vk[k]; const Track *tr = NULL;
  for (int j = 0; j < s->info.ntr; j++) if (s->info.tr[j].num == v.num) tr = &s->info.tr[j]; unlock(s); if (!tr) return -3;
  if (s->mp4) { if (v.len <= 0) return -3; if (v.len > maxBytes) return -4; unsigned char *d = malloc(v.len); if (!d) return -3; /* (an MP4: its bytes from the batch, the rest read when the window cut it) */
    long have = n < v.len ? n : v.len; memcpy(d, b0, have);
    if (have < v.len) { unsigned char *r; long rn; int e = range_with(s, net, v.cl + have, v.len - have, &r, &rn, NULL); (*reqs)++; if (e || rn < v.len - have) { free(r); free(d); return -1; } memcpy(d + have, r, v.len - have); *bytes += rn; free(r); }
    *frame = d; *flen = v.len; return 0; }
  long long start = v.cl + 5 + v.rel; long slack = 7; const unsigned char *b = b0; unsigned char *own = NULL; int e;
  Block blk; long off = -1; for (int h = 0; h <= (int)slack && off < 0; h++) if (read_block(b, n, h, v.num, &blk)) off = h;
  if (off < 0) return -3;
  if (blk.len < 0) { if (blk.total > maxBytes) return -4; e = range_with(s, net, start + off, blk.total, &own, &n, NULL); (*reqs)++; if (e) return -1; *bytes += n; off = 0; b = own;
    if (!read_block(b, n, 0, v.num, &blk) || blk.len < 0) { free(own); return -3; } }
  unsigned id; read_id(b, n, off, &id); int key = id != ID_SIMPLEBLOCK || (blk.payload && (blk.payload[-1] & 0x80)); /* (a SimpleBlock says it; a BlockGroup the Cues point at is taken as one) */
  if (!blk.payload || !key || blk.plen <= 0) { free(own); return -3; }
  long ol; unsigned char *d = decode_payload(tr, blk.payload, blk.plen, &ol); free(own); if (!d) return -3;
  *frame = d; *flen = ol; return 0;
}
/* a batch's part for keyframe k, read from mkv_key_where's start: its block found there, its Cluster walked first when
   the index gave no place in it (that place kept); 0 and *frame, -1 a read failed, -3 unusable, -4 over maxBytes */
int mkv_key_from_part(MkvSession *s, NetRange *net, int k, const unsigned char *b0, long n, long maxBytes, unsigned char **frame, long *flen, long *bytes, int *reqs) {
  *frame = NULL; *flen = 0; lock(s); if (k < 0 || k >= s->info.nvk || s->dead) { unlock(s); return -1; } Point v = s->info.vk[k]; int mp = s->mp4; unlock(s);
  if (mp || v.rel >= 0) return mkv_key_from(s, net, k, b0, n, maxBytes, frame, flen, bytes, reqs);
  long r2 = -1; int q0 = *reqs; long b0n = *bytes; int w = walk_cluster(s, net, &v, 65536, &r2, bytes, reqs, b0, n); if (getenv("SB_SPDBG")) fprintf(stderr, "[mkv] walk k%d: %s at %ld in its Cluster, %d more reads, %ld KB\n", k, w ? "not found" : "found", r2, *reqs - q0, (*bytes - b0n) / 1024); if (w) return w;
  lock(s); if (k < s->info.nvk && s->info.vk[k].cl == v.cl) s->info.vk[k].rel = r2; unlock(s);
  long off = 5 + r2; if (off + 16 < n) return mkv_key_from(s, net, k, b0 + off, n - off, maxBytes, frame, flen, bytes, reqs);
  long b2 = 0; int q2 = 0; int r = mkv_key_read(s, net, k, 196608, maxBytes, frame, flen, NULL, &b2, &q2); *bytes += b2; *reqs += q2; return r; } /* (its block past the part: read on its own, its place known now) */
/* a hold on the session for a thread of the caller's (it stays readable until released; mkv_close only marks it dead) */
int mkv_key_where(MkvSession *s, int k, long long *start, long *slack) { lock(s); int ok = k >= 0 && k < s->info.nvk && !s->dead; if (ok) { const Point *v = &s->info.vk[k]; if (s->mp4) { *start = v->cl; *slack = 0; } else if (v->rel >= 0) { *start = v->cl + 5 + v->rel; *slack = 7; } else { *start = v->cl; *slack = -1; } } /* (no place in its Cluster: the Cluster's start, walked in the bytes a batch brings) */ unlock(s); return ok; } /* (where a keyframe's block starts, its position known: a batch can ask for it) */
const char *mkv_url(MkvSession *s) { return s ? s->url : ""; }
void mkv_set_key_url(MkvSession *s, const char *url) { if (!s) return; lock(s); free(s->keyUrl); s->keyUrl = url && url[0] ? strdup(url) : NULL; unlock(s); }
int mkv_net_warm(MkvSession *s, NetRange *net) { unsigned char *b = NULL; long n = 0; int e = range_with(s, net, 0, 1, &b, &n, NULL); free(b); return e; } /* (a connection opened ahead: its first read pays the handshake now) */
void mkv_retain(MkvSession *s) { if (!s) return; lock(s); s->refs++; unlock(s); }
void mkv_release(MkvSession *s) { if (!s) return; lock(s); int last = --s->refs == 0; unlock(s); if (last) session_free(s); }
int mkv_dead(MkvSession *s) { if (!s) return 1; lock(s); int d = s->dead; unlock(s); return d; }
int mkv_key_time(MkvSession *s, int k, double *t) { if (!s) return 0; lock(s); int ok = k >= 0 && k < s->info.nvk; if (ok) *t = s->info.vk[k].t; unlock(s); return ok; }
int mkv_key_count(MkvSession *s) { if (!s) return 0; lock(s); int n = s->info.nvk; unlock(s); return n; }
int mkv_chapters(MkvSession *s, MkvChapter *out, int max) {
  if (!s) return 0; lock(s); int r = -1;
  if (s->info.haveChapters) { r = s->info.nch < max ? s->info.nch : max; memcpy(out, s->info.ch, r * sizeof *out); }
  else if (s->state == MKV_FALLBACK) r = 0;
  unlock(s); return r;
}
int mkv_lines_at(MkvSession *s, int n, double t, MkvLine *out, int max, char *buf, int cap) {
  if (!s) return 0; lock(s); TState *x = ts_by_n(s, n); int k = 0, used = 0;
  if (x && x->n) {
    int a = 0, z = x->n; while (a < z) { int m = (a + z) / 2; if (x->l[m].s <= t) a = m + 1; else z = m; } /* the first line starting after t */
    int from = a - 1, look = 0;
    for (int i = from; i >= 0 && look < 64 && k < max; i--, look++) { Line *l = &x->l[i]; if (l->e <= t || l->hidden) continue;
      int len = (int)strlen(l->t) + 1; if (used + len > cap) break;
      memcpy(buf + used, l->t, len); MkvLine *o = &out[k++]; o->s = l->s; o->e = l->e; o->t = buf + used; used += len;
      o->an = l->an; o->x = l->x; o->y = l->y; o->color = l->color; o->italic = l->italic; o->bold = l->bold; o->layer = l->layer; }
    /* oldest first, the way they appeared */
    for (int i = 0; i < k / 2; i++) { MkvLine tmp = out[i]; out[i] = out[k - 1 - i]; out[k - 1 - i] = tmp; }
  }
  unlock(s); return k;
}
/* the lines of track n that show between lo and hi (Auto-sync's reference): copied into out/buf up to max / cap, the
   count of all of them returned (out NULL: only counted) */
int mkv_lines_in(MkvSession *s, int n, double lo, double hi, MkvLine *out, int max, char *buf, int cap) {
  if (!s) return 0; lock(s); TState *x = ts_by_n(s, n); int k = 0, used = 0, all = 0;
  if (x && x->n) {
    int a = 0, z = x->n; double from = lo - 30; while (a < z) { int m = (a + z) / 2; if (x->l[m].s < from) a = m + 1; else z = m; } /* lines rarely stay up 30 s */
    for (int i = a; i < x->n && x->l[i].s < hi; i++) { Line *l = &x->l[i]; if (l->e <= lo || l->hidden) continue; all++;
      if (!out || k >= max) continue; int len = (int)strlen(l->t) + 1; if (used + len > cap) continue;
      memcpy(buf + used, l->t, len); MkvLine *o = &out[k++]; memset(o, 0, sizeof *o); o->s = l->s; o->e = l->e; o->t = buf + used; used += len; }
  }
  unlock(s); return all;
}
