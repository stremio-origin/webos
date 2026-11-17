#include "core/mp4chap.h"
#include "core/net.h"
#include "platform/plat.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXCH 64
#define WIN (64 * 1024)          /* each read takes at least this much: the headers walked one after another share it */
#define TIMEOUT_MS 8000
#define MAX_PART (2 * 1024 * 1024) /* a chapter part read whole (udta, the chapter track's mdia): never more than this */

typedef struct {
  char url[4096]; void (*cb)(const char *, const Mp4Chapter *, int, void *); void *ud;
  NetRange *net; long long total; unsigned char *buf; long long bufAt; long bufN; int reads;
  Mp4Chapter ch[MAXCH]; int n;
} Job;

/* bytes [at, at+len) of the file: from the window when it holds them, else one ranged read (at least WIN) */
static const unsigned char *get(Job *j, long long at, long len) {
  if (len <= 0 || at < 0 || (j->total > 0 && at + len > j->total)) return NULL;
  if (j->buf && at >= j->bufAt && at + len <= j->bufAt + j->bufN) return j->buf + (at - j->bufAt);
  if (j->reads >= 40) return NULL; /* (a file laid out strangely: given up rather than read in pieces for ever) */
  long want = len > WIN ? len : WIN; if (j->total > 0 && at + want > j->total) want = (long)(j->total - at);
  unsigned char *acc = malloc(want); long have = 0; int empty = 0;
  while (acc && have < want && empty < 3) { unsigned char *b = NULL; long g = 0; long long t = -1; int st = 0; j->reads++;
    int e = net_range_get(j->net, j->url, at + have, want - have, &b, &g, &t, &st, TIMEOUT_MS);
    if (t > 0) j->total = t;
    if (g > 0) { memcpy(acc + have, b, g); have += g; empty = 0; } else empty++;
    free(b); if (e != NR_OK && e != NR_NET && e != NR_TIMEOUT) break; }
  if (!acc || have < len) { free(acc); return NULL; }
  free(j->buf); j->buf = acc; j->bufAt = at; j->bufN = have; return acc;
}
static unsigned be32(const unsigned char *p) { return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3]; }
static unsigned long long be64(const unsigned char *p) { return (unsigned long long)be32(p) << 32 | be32(p + 4); }
/* the box at `at` (inside [.., end)): its type, where its body starts and its whole size; 0 when there is none */
static int box(Job *j, long long at, long long end, char type[5], long long *body, long long *size) {
  if (at + 8 > end) return 0; const unsigned char *h = get(j, at, 16 <= end - at ? 16 : 8); if (!h) return 0;
  long long sz = be32(h); memcpy(type, h + 4, 4); type[4] = 0; long long hl = 8;
  if (sz == 1) { if (end - at < 16) return 0; sz = (long long)be64(h + 8); hl = 16; } else if (sz == 0) sz = end - at;
  if (sz < hl || at + sz > end) return 0; *body = at + hl; *size = sz; return 1;
}
/* the child box of [from, to) with this type: its body and the end of it */
static int child(Job *j, long long from, long long to, const char *want, long long *body, long long *bend) {
  for (long long at = from; at < to;) { char t[5]; long long b, s; if (!box(j, at, to, t, &b, &s)) return 0;
    if (!memcmp(t, want, 4)) { *body = b; *bend = at + s; return 1; } at += s; }
  return 0;
}
static void add(Job *j, double s, const unsigned char *t, long len) {
  if (j->n >= MAXCH || s < 0) return; Mp4Chapter *c = &j->ch[j->n++]; c->s = s; long k = 0;
  if (len >= 2 && ((t[0] == 0xfe && t[1] == 0xff) || (t[0] == 0xff && t[1] == 0xfe))) { int le = t[0] == 0xff; /* UTF-16 with its mark: to UTF-8 */
    for (long i = 2; i + 1 < len && k + 4 < (long)sizeof c->name; i += 2) { unsigned u = le ? (unsigned)t[i] | (unsigned)t[i + 1] << 8 : (unsigned)t[i] << 8 | t[i + 1];
      if (u < 0x80) c->name[k++] = (char)u; else if (u < 0x800) { c->name[k++] = (char)(0xc0 | u >> 6); c->name[k++] = (char)(0x80 | (u & 63)); }
      else { c->name[k++] = (char)(0xe0 | u >> 12); c->name[k++] = (char)(0x80 | (u >> 6 & 63)); c->name[k++] = (char)(0x80 | (u & 63)); } } }
  else for (long i = 0; i < len && k + 1 < (long)sizeof c->name; i++) if (t[i] >= 0x20) c->name[k++] = (char)t[i]; /* (UTF-8 as it is, control bytes left out) */
  c->name[k] = 0;
}
/* Nero chapters: version, flags, (4 more bytes from version 1), a count, then each start in 100 ns and its name */
static void read_chpl(Job *j, long long b, long long e) {
  long len = (long)(e - b); if (len < 5 || len > MAX_PART) return; const unsigned char *p = get(j, b, len); if (!p) return;
  long o = 4 + (p[0] ? 4 : 0); if (o >= len) return; int cnt = p[o++];
  for (int i = 0; i < cnt && o + 9 <= len; i++) { double s = be64(p + o) / 1e7; o += 8; int l = p[o++]; if (o + l > len) break; add(j, s, p + o, l); o += l; }
}
/* the chapter track: its timescale (mdhd), the samples' durations (stts), sizes (stsz), chunks (stsc) and where the
   chunks are (stco / co64); each sample is a 16-bit length and the name */
static void read_chap_track(Job *j, long long mb, long long me) {
  long len = (long)(me - mb); if (len < 8 || len > MAX_PART) return;
  const unsigned char *m = get(j, mb, len); if (!m) return; unsigned char *mdia = malloc(len); if (!mdia) return; memcpy(mdia, m, len);
  /* the boxes needed, found in the copy (the window moves as the samples are read) */
  const unsigned char *mdhd = NULL, *stts = NULL, *stsz = NULL, *stsc = NULL, *stco = NULL; int co64 = 0; long lens[5] = { 0 };
  struct { long a, e; } stack[8]; int sp = 0; stack[sp].a = 0; stack[sp].e = len; sp++;
  while (sp) { sp--; long a = stack[sp].a, e = stack[sp].e;
    while (a + 8 <= e) { long sz = (long)be32(mdia + a); if (sz < 8 || a + sz > e) break; const unsigned char *t = mdia + a + 4, *bd = mdia + a + 8; long bl = sz - 8;
      if (!memcmp(t, "minf", 4) || !memcmp(t, "stbl", 4)) { if (sp < 8) { stack[sp].a = a + 8; stack[sp].e = a + sz; sp++; } }
      else if (!memcmp(t, "mdhd", 4)) { mdhd = bd; lens[0] = bl; }
      else if (!memcmp(t, "stts", 4)) { stts = bd; lens[1] = bl; }
      else if (!memcmp(t, "stsz", 4)) { stsz = bd; lens[2] = bl; }
      else if (!memcmp(t, "stsc", 4)) { stsc = bd; lens[3] = bl; }
      else if (!memcmp(t, "stco", 4) || !memcmp(t, "co64", 4)) { stco = bd; lens[4] = bl; co64 = t[1] == 'o'; }
      a += sz; } }
  if (!mdhd || !stts || !stsz || !stsc || !stco || lens[0] < 24) { free(mdia); return; }
  double scale = mdhd[0] == 1 ? (lens[0] >= 32 ? be32(mdhd + 20) : 0) : be32(mdhd + 12); if (scale <= 0) { free(mdia); return; }
  unsigned nts = be32(stts + 4), fixed = be32(stsz + 4), nsz = be32(stsz + 8), nsc = be32(stsc + 4), nco = be32(stco + 4);
  if ((long)(8 + nts * 8ul) > lens[1] || (!fixed && (long)(12 + nsz * 4ul) > lens[2]) || (long)(8 + nsc * 12ul) > lens[3] || (long)(8 + nco * (co64 ? 8ul : 4ul)) > lens[4]) { free(mdia); return; }
  unsigned total = nsz > MAXCH ? MAXCH : nsz;
  double t = 0; unsigned ti = 0, tleft = nts ? be32(stts + 8) : 0, si = 0; /* t: this sample's start; ti / tleft: its stts entry and what is left of it */
  for (unsigned c = 0; c < nco && si < total; c++) { /* each chunk: its samples, one after another */
    unsigned per = 1; for (unsigned k = 0; k < nsc; k++) { unsigned first = be32(stsc + 8 + k * 12) - 1; if (first <= c) per = be32(stsc + 8 + k * 12 + 4); else break; }
    long long off = co64 ? (long long)be64(stco + 8 + c * 8ul) : (long long)be32(stco + 8 + c * 4ul);
    for (unsigned k = 0; k < per && si < total; k++, si++) {
      unsigned sz = fixed ? fixed : be32(stsz + 12 + si * 4ul);
      const unsigned char *p = sz >= 2 && sz < 4096 ? get(j, off, sz) : NULL;
      if (p) { unsigned l = (unsigned)p[0] << 8 | p[1]; if (l > sz - 2) l = sz - 2; add(j, t / scale, p + 2, l); }
      off += sz;
      while (ti < nts && !tleft) { ti++; if (ti < nts) tleft = be32(stts + 8 + ti * 8); } /* (entries of no samples passed over) */
      if (ti < nts) { t += be32(stts + 8 + ti * 8 + 4); tleft--; } } }
  free(mdia);
}
static int read_file(Job *j) {
  const unsigned char *h = get(j, 0, 16); if (!h || (memcmp(h + 4, "ftyp", 4) && memcmp(h + 4, "moov", 4) && memcmp(h + 4, "free", 4) && memcmp(h + 4, "wide", 4))) return -1; /* no MP4 */
  long long end = j->total > 0 ? j->total : (1ll << 62), mb = 0, me = 0, at = 0; int found = 0;
  for (int hops = 0; hops < 12 && at < end; hops++) { if (j->total > 0) end = j->total; char t[5]; long long b, s; if (!box(j, at, end, t, &b, &s)) break; if (!memcmp(t, "moov", 4)) { mb = b; me = at + s; found = 1; break; } at += s; }
  if (!found) return -1;
  long long ub, ue; if (child(j, mb, me, "udta", &ub, &ue)) { long long cb, ce; if (child(j, ub, ue, "chpl", &cb, &ce)) read_chpl(j, cb, ce); }
  if (j->n) return j->n;
  /* the QuickTime chapter track: the track a tref "chap" names */
  unsigned chapId = 0; struct { unsigned id; long long mb, me; } tr[16]; int ntr = 0;
  for (long long a = mb; a < me && ntr < 16;) { char t[5]; long long b, s; if (!box(j, a, me, t, &b, &s)) break;
    if (!memcmp(t, "trak", 4)) { unsigned id = 0; long long xb, xe; long long mdb = 0, mde = 0;
      if (child(j, b, a + s, "tkhd", &xb, &xe)) { const unsigned char *k = get(j, xb, 24); if (k) id = k[0] == 1 ? be32(k + 20) : be32(k + 12); }
      if (child(j, b, a + s, "tref", &xb, &xe)) { long long cb, ce; if (child(j, xb, xe, "chap", &cb, &ce) && ce - cb >= 4) { const unsigned char *k = get(j, cb, 4); if (k && !chapId) chapId = be32(k); } }
      if (child(j, b, a + s, "mdia", &mdb, &mde)) {}
      tr[ntr].id = id; tr[ntr].mb = mdb; tr[ntr].me = mde; ntr++; }
    a += s; }
  for (int i = 0; i < ntr && chapId; i++) if (tr[i].id == chapId && tr[i].mb) { read_chap_track(j, tr[i].mb, tr[i].me); break; }
  return j->n;
}
static int cmp(const void *a, const void *b) { double d = ((const Mp4Chapter *)a)->s - ((const Mp4Chapter *)b)->s; return d < 0 ? -1 : d > 0; }
static void on_done(int st, const unsigned char *b, size_t n, void *ud) { (void)st; (void)b; (void)n; Job *j = ud;
  j->cb(j->url, j->ch, j->n, j->ud); free(j->buf); free(j); }
static void *run(void *a) { Job *j = a; plat_thread_background(5); j->net = net_range_new(); j->total = -1;
  int r = j->net ? read_file(j) : -1; if (j->net) net_range_free(j->net); j->net = NULL;
  if (r > 1) qsort(j->ch, j->n, sizeof j->ch[0], cmp); if (r < 0) j->n = -1;
  fprintf(stderr, "[mp4] chapters: %d (%d reads)\n", j->n, j->reads);
  net_file("", on_done, j); return NULL; }
void mp4_chapters(const char *url, void (*cb)(const char *, const Mp4Chapter *, int, void *), void *ud) {
  Job *j = calloc(1, sizeof *j); if (!j) { cb(url, NULL, -1, ud); return; } snprintf(j->url, sizeof j->url, "%s", url); j->cb = cb; j->ud = ud;
  pthread_t t; if (pthread_create(&t, NULL, run, j)) { free(j); cb(url, NULL, -1, ud); return; } pthread_detach(t);
}
