/* libsbdts: a file whose only sound the TV can't decode (DTS, TrueHD) as MPEG-TS the TV's own player takes, made as
   it is asked for. The video is copied packet for packet (Matroska's length-prefixed H.264 / HEVC turned into start
   codes); one audio track is decoded (DTS: the core only, never the lossless extension; TrueHD: its 5.1 downmix) and
   encoded as AC3, 5.1 at 640 kb/s (2.0 at 256 kb/s from a stereo source).

   - Segments come from the keyframe index (Matroska's Cues): keyframes grouped to about 2 s or more. A segment that
     follows the one made last continues the same reading, encoders and muxer; any other starts over with a seek.
   - Timestamps: the video keeps the file's own. Matroska stores presentation times only, and MPEG-TS needs decode
     times too: with no video decoder to learn the reorder depth, the n-th packet in decode order takes the n-th
     smallest presentation time seen over a window of LOOK packets, LOOK frames earlier (always at or before its own).
     The audio runs on its own clock from the first decoded frame kept (1536 samples a frame), so encoded frames
     never drift or overlap; a jump in the source of more than 250 ms starts that clock again.
   - Memory: packets waiting for the other stream are capped (QMAX, about 8 MiB); past the cap the stream that is
     there is written without waiting. The reading itself is the app's (its ring of 4 MiB).
   - Each segment ends with the muxer flushed, so a segment is complete when its response ends.
   - Dolby Vision (SBD_DV81): a profile 7 picture (a base layer, an enhancement layer and the RPU in each frame) goes
     out as single-layer profile 8.1, which TVs with Dolby Vision take from a stream: the enhancement layer's NAL units
     are left out, each RPU is rewritten by libdovi (sbdovi_to81, tools/dovi), the base layer is copied as it is. The
     stream table says so: the video's entry in the PMT gets the DOVI registration and the Dolby Vision video stream
     descriptor (the TS writer has neither), written into each PMT as it goes out. A profile 8 or 5 picture keeps
     its RPU as it is and gets the same descriptor. An RPU that can't be converted is left out of its frame (counted).
   - SBD_COPYAUDIO: the audio track goes out as the file has it (AC3, E-AC3, AAC, DTS...): no decoder, no encoder. */
#include "core/sbdts.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libswresample/swresample.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/avstring.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/pixdesc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define API __attribute__((visibility("default")))
#define QMAX (8u << 20)
#define IOBUF (64 << 10)
#define LOOK 4
#define EPS 0.0015
#define PMT_PID 0x1000 /* the TS writer's first PMT */

extern ptrdiff_t sbdovi_to81(const uint8_t *src, size_t len, uint8_t *dst, size_t cap); /* tools/dovi: one RPU for 8.1, its size or -1 */

typedef struct QE { AVPacket *p; double t; int seg, has; struct QE *next; } QE;   /* t: decode time (s); has: decode time known */
typedef struct { QE *h, *t; size_t bytes; int n; } Q;

struct SbdSrc {
  sbd_read rd; void *ud; long long size, pos; volatile int *stop; double readWait; long long readCnt, budget; /* budget: reads allowed (0 = any) */
  AVFormatContext *ic; AVIOContext *io; int vi, ai; AVRational vtb, atb;
  AVBSFContext *bsf; AVCodecContext *dec, *enc; SwrContext *swr; int swrChecked; AVAudioFifo *fifo; AVFrame *fr, *ef, *cv; AVPacket *pk;
  int outRate;
  double *seg; int nseg; sbd_info info;
  /* the run */
  int next, curVSeg, runSeg; double segEnd;
  AVFormatContext *oc; unsigned char *obuf; sbd_write w; void *wud; int werr;
  Q vq, aq; int64_t pend[LOOK + 2]; int npend; int64_t lastDts; int64_t fd;
  int vEof, aEof, needKey; double keyAt;
  int64_t grid; int gridOn; double audioFrom;
  size_t peak; sbd_stats *st;
  int copyA, dv81; uint8_t desc[16]; int descLen; /* desc: the video's PMT descriptors (Dolby Vision), descLen 0 none */
  uint8_t *nal; size_t nalCap; uint8_t tsb[188 * 2]; int tsc; uint8_t *wb; int wbCap;
};

static double cpu_now(void) { struct timespec t; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static double wall_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static void say(char *err, int n, const char *what, int code) {
  char e[96] = ""; if (code) av_strerror(code, e, sizeof e); snprintf(err, n, "%s%s%s", what, code ? ": " : "", e);
}

/* ---- reading: through the app ---- */
static int io_read(void *o, uint8_t *buf, int n) {
  SbdSrc *s = o; if (s->stop && *s->stop) return AVERROR_EXIT;
  if (s->budget && s->readCnt > s->budget) return AVERROR_EXIT;
  double t0 = wall_now(); int r = s->rd(s->ud, s->pos, buf, n); s->readWait += wall_now() - t0;
  if (r > 0) { s->pos += r; s->readCnt += r; return r; }
  return r == 0 ? AVERROR_EOF : (s->stop && *s->stop) ? AVERROR_EXIT : AVERROR(EIO);
}
static int64_t io_seek(void *o, int64_t off, int whence) {
  SbdSrc *s = o; whence &= ~AVSEEK_FORCE;
  if (whence == AVSEEK_SIZE) return s->size > 0 ? s->size : AVERROR(ENOSYS);
  int64_t p = whence == SEEK_SET ? off : whence == SEEK_CUR ? s->pos + off : whence == SEEK_END && s->size > 0 ? s->size + off : -1;
  if (p < 0) return AVERROR(EINVAL); s->pos = p; return p;
}
static int interrupted(void *o) { SbdSrc *s = o; return (s->stop && *s->stop) || (s->budget && s->readCnt > s->budget); }

/* ---- queues ---- */
static void q_push(SbdSrc *s, Q *q, AVPacket *p, double t, int seg, int has) {
  QE *e = av_mallocz(sizeof *e); if (!e) { av_packet_free(&p); return; }
  e->p = p; e->t = t; e->seg = seg; e->has = has; if (q->t) q->t->next = e; else q->h = e; q->t = e; q->bytes += p->size; q->n++;
  size_t tot = s->vq.bytes + s->aq.bytes; if (tot > s->peak) s->peak = tot;
}
static AVPacket *q_pop(Q *q) { QE *e = q->h; if (!e) return NULL; q->h = e->next; if (!q->h) q->t = NULL; q->bytes -= e->p->size; q->n--; AVPacket *p = e->p; av_free(e); return p; }
static void q_clear(Q *q) { AVPacket *p; while ((p = q_pop(q))) av_packet_free(&p); }

/* ---- Dolby Vision 7 -> 8.1: one frame in start-code form ----
   Every NAL unit of the base layer is copied; type 63 (the enhancement layer, as profile 7 interleaves it in one track)
   and anything with a nonzero layer id are left out; type 62 (the RPU) is rewritten. 1 done (*out, *on: the frame, its
   size; 0 when nothing is left), 0 not start-code data (the frame goes as it is) */
static const uint8_t *next_sc(const uint8_t *p, const uint8_t *e, int *len) {
  for (; p + 3 <= e; p++) { if (p[0] || p[1]) continue; if (p[2] == 1) { *len = 3; return p; } if (p + 4 <= e && !p[2] && p[3] == 1) { *len = 4; return p; } }
  *len = 0; return e;
}
static int dv_frame(SbdSrc *s, const uint8_t *b, int n, int *on) {
  const uint8_t *e = b + n; int sl; const uint8_t *p = next_sc(b, e, &sl); if (!sl) return 0;
  size_t need = (size_t)n + 4096; if (need > s->nalCap) { uint8_t *nb = av_realloc(s->nal, need); if (!nb) return 0; s->nal = nb; s->nalCap = need; }
  size_t o = 0;
  while (p < e) {
    const uint8_t *u = p + sl; int nl; const uint8_t *q = next_sc(u, e, &nl); size_t ul = (size_t)(q - u);
    if (ul >= 2) {
      int type = (u[0] >> 1) & 63, layer = ((u[0] & 1) << 5) | (u[1] >> 3);
      if (type == 63 || layer) s->st->elDropped++;
      else if (type == 62) {
        if (s->nalCap - o < 4) break; memcpy(s->nal + o, "\0\0\0\1", 4);
        ptrdiff_t k = sbdovi_to81(u, ul, s->nal + o + 4, s->nalCap - o - 4);
        if (k > 0) { o += 4 + (size_t)k; s->st->rpuOk++; } else s->st->rpuBad++;  /* (the frame goes without it) */
      } else { if (s->nalCap - o < ul + 4) break; memcpy(s->nal + o, "\0\0\0\1", 4); memcpy(s->nal + o + 4, u, ul); o += 4 + ul; }
    }
    p = q; sl = nl;
  }
  *on = (int)o; return 1;
}
/* the descriptors for the video's entry: the DOVI registration, then the Dolby Vision video stream descriptor (version
   1.0, profile, level, RPU / enhancement layer / base layer present, the base layer's compatibility) */
static void dv_desc(SbdSrc *s, int profile, int level, int compat) {
  uint8_t *d = s->desc; int k = 0; unsigned v = (unsigned)(profile & 0x7f) << 9 | (unsigned)(level & 0x3f) << 3 | 1u << 2 | 0u << 1 | 1u;
  d[k++] = 0x05; d[k++] = 4; d[k++] = 'D'; d[k++] = 'O'; d[k++] = 'V'; d[k++] = 'I';
  d[k++] = 0xB0; d[k++] = 5; d[k++] = 1; d[k++] = 0; d[k++] = (uint8_t)(v >> 8); d[k++] = (uint8_t)v; d[k++] = (uint8_t)((compat & 15) << 4);
  s->descLen = k;
}
static uint32_t crc32_mpeg(const uint8_t *p, int n) {
  uint32_t c = 0xFFFFFFFFu; while (n--) { c ^= (uint32_t)*p++ << 24; for (int i = 0; i < 8; i++) c = c & 0x80000000u ? c << 1 ^ 0x04C11DB7u : c << 1; } return c;
}
/* one TS packet: a PMT that starts in it gets the descriptors in the HEVC entry (its length, the section's and the
   CRC again); a PMT already carrying them, or one with no room, is left as it is */
static void pmt_fix(SbdSrc *s, uint8_t *t) {
  if (t[0] != 0x47 || !(t[1] & 0x40) || (((t[1] & 0x1f) << 8) | t[2]) != PMT_PID || (t[3] & 0x30) != 0x10) return;
  uint8_t *sec = t + 5 + t[4]; if (sec + 12 > t + 188 || sec[0] != 0x02) return;
  int sl = ((sec[1] & 0x0f) << 8) | sec[2], end = 3 + sl; if (sec + end + s->descLen > t + 188 || sl < 13) return;
  int pil = ((sec[10] & 0x0f) << 8) | sec[11], at = 12 + pil;
  while (at + 5 <= end - 4) {
    int type = sec[at], eil = ((sec[at + 3] & 0x0f) << 8) | sec[at + 4], de = at + 5 + eil;
    if (de > end - 4) return;
    if (type == 0x24) {
      for (int i = at + 5; i + 1 < de; i += 2 + sec[i + 1]) if (sec[i] == 0xB0) return; /* (done already) */
      memmove(sec + de + s->descLen, sec + de, (size_t)(end - de)); memcpy(sec + de, s->desc, (size_t)s->descLen);
      eil += s->descLen; sec[at + 3] = (uint8_t)(0xf0 | eil >> 8); sec[at + 4] = (uint8_t)eil;
      sl += s->descLen; end += s->descLen; sec[1] = (uint8_t)((sec[1] & 0xf0) | sl >> 8); sec[2] = (uint8_t)sl;
      uint32_t c = crc32_mpeg(sec, end - 4); sec[end - 4] = (uint8_t)(c >> 24); sec[end - 3] = (uint8_t)(c >> 16); sec[end - 2] = (uint8_t)(c >> 8); sec[end - 1] = (uint8_t)c;
      return;
    }
    at = de;
  }
}

/* ---- video decode times ---- */
static void dts_assign(SbdSrc *s, int final) {
  while (s->npend > (final ? 0 : LOOK)) {
    QE *e = s->vq.h; while (e && e->has) e = e->next; if (!e) { s->npend = 0; return; }
    int mi = 0; for (int i = 1; i < s->npend; i++) if (s->pend[i] < s->pend[mi]) mi = i;
    int64_t d = s->pend[mi] - LOOK * s->fd; s->pend[mi] = s->pend[--s->npend];
    if (e->p->pts != AV_NOPTS_VALUE && d > e->p->pts) d = e->p->pts;
    if (s->lastDts != AV_NOPTS_VALUE && d <= s->lastDts) d = s->lastDts + 1;
    s->lastDts = d; e->p->dts = d; e->t = d * av_q2d(s->vtb); e->has = 1;
  }
}
static int seg_at(SbdSrc *s, double t) { int a = 0, z = s->nseg - 1; while (a < z) { int m = (a + z + 1) / 2; if (s->seg[m] <= t + EPS) a = m; else z = m - 1; } return a; }
static void video_out(SbdSrc *s, AVPacket *p) {
  if (p->pts == AV_NOPTS_VALUE) p->pts = p->dts; if (p->pts == AV_NOPTS_VALUE) { av_packet_free(&p); return; }
  if (s->dv81) { int on = 0; /* profile 7 -> 8.1: the frame rewritten into a packet of its own */
    if (dv_frame(s, p->data, p->size, &on)) { if (!on) { av_packet_free(&p); return; }
      if (on > p->size) { if (av_grow_packet(p, on - p->size) < 0) { av_packet_free(&p); return; } } else av_shrink_packet(p, on);
      if (av_packet_make_writable(p) < 0) { av_packet_free(&p); return; } memcpy(p->data, s->nal, (size_t)on); } }
  double t = p->pts * av_q2d(s->vtb);
  if (s->runSeg >= 0 && (p->flags & AV_PKT_FLAG_KEY)) while (s->curVSeg + 1 < s->nseg && t >= s->seg[s->curVSeg + 1] - EPS) s->curVSeg++; /* a keyframe opening the next segment */
  p->dts = AV_NOPTS_VALUE;
  if (s->npend < LOOK + 2) s->pend[s->npend++] = p->pts;
  q_push(s, &s->vq, p, t, s->runSeg >= 0 ? s->curVSeg : 0, 0); s->st->vpkts++;
  dts_assign(s, 0);
}
static int video_in(SbdSrc *s, AVPacket *pk) {
  double t = (pk->pts != AV_NOPTS_VALUE ? pk->pts : pk->dts) * av_q2d(s->vtb);
  if (s->needKey) { if (!(pk->flags & AV_PKT_FLAG_KEY) || t < s->keyAt - EPS) { av_packet_unref(pk); return 0; } s->needKey = 0; if (s->audioFrom < -1e8) s->audioFrom = t; }
  int r = av_bsf_send_packet(s->bsf, pk); if (r < 0) { av_packet_unref(pk); return 0; }
  for (;;) { AVPacket *o = av_packet_alloc(); if (!o) return AVERROR(ENOMEM); r = av_bsf_receive_packet(s->bsf, o); if (r < 0) { av_packet_free(&o); break; } video_out(s, o); }
  return 0;
}

/* ---- audio ---- */
static int encode_frame(SbdSrc *s, AVFrame *f) {
  int r = avcodec_send_frame(s->enc, f); if (r < 0 && r != AVERROR_EOF) return r;
  for (;;) { AVPacket *o = av_packet_alloc(); if (!o) return AVERROR(ENOMEM); r = avcodec_receive_packet(s->enc, o); if (r < 0) { av_packet_free(&o); break; }
    double t = o->pts / (double)s->outRate; o->dts = o->pts; int sg = s->runSeg >= 0 ? seg_at(s, t) : 0; if (sg < s->runSeg) sg = s->runSeg;
    q_push(s, &s->aq, o, t, sg, 1); s->st->apkts++; }
  return 0;
}
static int drain_fifo(SbdSrc *s, int final) {
  int fs = s->enc->frame_size;
  while (av_audio_fifo_size(s->fifo) >= fs || (final && av_audio_fifo_size(s->fifo) > 0)) {
    AVFrame *f = s->ef; av_frame_unref(f); f->nb_samples = fs; f->format = s->enc->sample_fmt; f->sample_rate = s->outRate; av_channel_layout_copy(&f->ch_layout, &s->enc->ch_layout);
    if (av_frame_get_buffer(f, 0) < 0) return AVERROR(ENOMEM);
    int got = av_audio_fifo_read(s->fifo, (void **)f->data, fs); if (got < fs) av_samples_set_silence(f->data, got, fs - got, f->ch_layout.nb_channels, f->format); /* the last one padded */
    f->pts = s->grid; s->grid += fs;
    int r = encode_frame(s, f); if (r < 0) return r;
  }
  return 0;
}
static int frame_in(SbdSrc *s, AVFrame *fr) {
  int64_t ts = fr->best_effort_timestamp != AV_NOPTS_VALUE ? fr->best_effort_timestamp : fr->pts; if (ts == AV_NOPTS_VALUE) return 0;
  double t = ts * av_q2d(s->atb), d = fr->nb_samples / (double)(fr->sample_rate > 0 ? fr->sample_rate : 48000);
  if (s->audioFrom < -1e8 || t + d <= s->audioFrom + EPS) return 0;   /* before the picture starts (one stream from a moment: known once its keyframe came) */
  int64_t at = llrint(t * s->outRate);
  if (!s->gridOn || llabs(at - (s->grid + av_audio_fifo_size(s->fifo))) > s->outRate / 4) { /* the first frame kept (or a jump in the source): the clock from here */
    if (s->gridOn) { av_audio_fifo_reset(s->fifo); }
    s->grid = at; s->gridOn = 1; }
  if (!s->swrChecked) { s->swrChecked = 1;
    if (fr->format != s->enc->sample_fmt || fr->sample_rate != s->outRate || av_channel_layout_compare(&fr->ch_layout, &s->enc->ch_layout)) {
      if (swr_alloc_set_opts2(&s->swr, &s->enc->ch_layout, s->enc->sample_fmt, s->outRate, &fr->ch_layout, fr->format, fr->sample_rate, 0, NULL) < 0 || swr_init(s->swr) < 0) return AVERROR(EINVAL); } }
  if (!s->swr) return av_audio_fifo_write(s->fifo, (void **)fr->extended_data, fr->nb_samples) < 0 ? AVERROR(ENOMEM) : drain_fifo(s, 0);
  int out = swr_get_out_samples(s->swr, fr->nb_samples); AVFrame *c = s->cv; av_frame_unref(c);
  c->nb_samples = out; c->format = s->enc->sample_fmt; c->sample_rate = s->outRate; av_channel_layout_copy(&c->ch_layout, &s->enc->ch_layout);
  if (av_frame_get_buffer(c, 0) < 0) return AVERROR(ENOMEM);
  int got = swr_convert(s->swr, c->data, out, (const uint8_t **)fr->extended_data, fr->nb_samples); if (got < 0) return got;
  if (av_audio_fifo_write(s->fifo, (void **)c->data, got) < 0) return AVERROR(ENOMEM);
  return drain_fifo(s, 0);
}
static int audio_copy(SbdSrc *s, AVPacket *pk) { /* SBD_COPYAUDIO: the packet as the file has it, on the file's clock */
  if (pk->pts == AV_NOPTS_VALUE) pk->pts = pk->dts; if (pk->pts == AV_NOPTS_VALUE) { av_packet_unref(pk); return 0; }
  double t = pk->pts * av_q2d(s->atb), d = pk->duration > 0 ? pk->duration * av_q2d(s->atb) : 0;
  if (s->audioFrom < -1e8 || t + d <= s->audioFrom + EPS) { av_packet_unref(pk); return 0; } /* before the picture starts */
  AVPacket *o = av_packet_alloc(); if (!o) { av_packet_unref(pk); return AVERROR(ENOMEM); }
  av_packet_move_ref(o, pk); o->dts = o->pts;
  int sg = s->runSeg >= 0 ? seg_at(s, t) : 0; if (sg < s->runSeg) sg = s->runSeg;
  q_push(s, &s->aq, o, t, sg, 1); s->st->apkts++; return 0;
}
static int audio_in(SbdSrc *s, AVPacket *pk) {
  if (s->copyA) return audio_copy(s, pk);
  int r = avcodec_send_packet(s->dec, pk); av_packet_unref(pk); if (r < 0 && r != AVERROR(EAGAIN) && r != AVERROR_EOF) return 0; /* a damaged frame: skipped */
  for (;;) { r = avcodec_receive_frame(s->dec, s->fr); if (r < 0) break; int e = frame_in(s, s->fr); av_frame_unref(s->fr); if (e < 0) return e; }
  return 0;
}

/* ---- writing ---- */
static int out_pass(SbdSrc *s, const uint8_t *b, int n) { if (!n) return 0; int r = s->w(s->wud, b, n); if (r < 0) { s->werr = 1; return AVERROR(EPIPE); } s->st->bytes += n; return n; }
static int out_write(void *o, const uint8_t *b, int n) {
  SbdSrc *s = o; if (s->werr) return AVERROR(EPIPE);
  if (!s->descLen) { int r = out_pass(s, b, n); return r < 0 ? r : n; }
  /* with the Dolby Vision descriptors: whole TS packets, each PMT mended on its way (a packet cut between two writes
     waits for its end) */
  if (s->wbCap < n + 188) { uint8_t *nb = av_realloc(s->wb, (size_t)n + 188); if (!nb) return AVERROR(ENOMEM); s->wb = nb; s->wbCap = n + 188; }
  int k = 0; memcpy(s->wb, s->tsb, (size_t)s->tsc); k = s->tsc; memcpy(s->wb + k, b, (size_t)n); k += n;
  int whole = k / 188 * 188; for (int i = 0; i < whole; i += 188) pmt_fix(s, s->wb + i);
  s->tsc = k - whole; memcpy(s->tsb, s->wb + whole, (size_t)s->tsc);
  int r = out_pass(s, s->wb, whole); return r < 0 ? r : n;
}
static void mux_close(SbdSrc *s) {
  s->tsc = 0;
  if (!s->oc) return; if (s->oc->pb) { avio_flush(s->oc->pb); av_freep(&s->oc->pb->buffer); avio_context_free(&s->oc->pb); }
  avformat_free_context(s->oc); s->oc = NULL;
}
static int mux_open(SbdSrc *s, char *err, int errn) {
  const AVOutputFormat *of = av_guess_format("mpegts", NULL, NULL); int r;
  if ((r = avformat_alloc_output_context2(&s->oc, of, NULL, NULL)) < 0) { say(err, errn, "MPEG-TS writer", r); return -1; }
  unsigned char *b = av_malloc(32 * 1024); if (!b) return -1;
  s->oc->pb = avio_alloc_context(b, 32 * 1024, 1, s, NULL, out_write, NULL); if (!s->oc->pb) { av_free(b); return -1; }
  AVStream *v = avformat_new_stream(s->oc, NULL), *a = avformat_new_stream(s->oc, NULL); if (!v || !a) return -1;
  avcodec_parameters_copy(v->codecpar, s->bsf->par_out); v->codecpar->codec_tag = 0; v->time_base = (AVRational){ 1, 90000 };
  if (s->copyA) avcodec_parameters_copy(a->codecpar, s->ic->streams[s->ai]->codecpar); else avcodec_parameters_from_context(a->codecpar, s->enc);
  a->codecpar->codec_tag = 0; a->time_base = (AVRational){ 1, 90000 };
  { const AVDictionaryEntry *l = av_dict_get(s->ic->streams[s->ai]->metadata, "language", NULL, 0); if (l) av_dict_set(&a->metadata, "language", l->value, 0); }
  /* every time 10 s on, the same in every segment: the first decode times (LOOK frames before the first picture) never go
     below zero, so the muxer never shifts a run by its own first packet */
  s->oc->output_ts_offset = 10 * AV_TIME_BASE; s->oc->avoid_negative_ts = AVFMT_AVOID_NEG_TS_DISABLED;
  AVDictionary *o = NULL; av_dict_set(&o, "mpegts_flags", "+resend_headers", 0);
  r = avformat_write_header(s->oc, &o); av_dict_free(&o); if (r < 0) { say(err, errn, "MPEG-TS header", r); return -1; }
  return 0;
}
static int write_pkt(SbdSrc *s, AVPacket *p, int video) {
  AVStream *os = s->oc->streams[video ? 0 : 1]; AVRational tb = video ? s->vtb : s->copyA ? s->atb : (AVRational){ 1, s->outRate };
  av_packet_rescale_ts(p, tb, os->time_base); p->stream_index = os->index; p->pos = -1;
  if (video) { double t = p->pts * av_q2d(os->time_base); if (s->st->first < 0) s->st->first = t; if (t > s->st->last) s->st->last = t; }
  int r = av_write_frame(s->oc, p); av_packet_free(&p); return r;
}

/* ---- opening ---- */
static int enc_open(SbdSrc *s, char *err, int errn) { /* AC3 (its float encoder: planar float in), 5.1 at 640 kb/s or 2.0 at 256 */
  avcodec_free_context(&s->enc); const AVCodec *ec = avcodec_find_encoder(AV_CODEC_ID_AC3); if (!ec || !(s->enc = avcodec_alloc_context3(ec))) { snprintf(err, errn, "no AC3 encoder"); return -1; }
  s->enc->sample_rate = s->outRate; s->enc->sample_fmt = AV_SAMPLE_FMT_FLTP; s->enc->time_base = (AVRational){ 1, s->outRate };
  AVChannelLayout five = AV_CHANNEL_LAYOUT_5POINT1, two = AV_CHANNEL_LAYOUT_STEREO; av_channel_layout_copy(&s->enc->ch_layout, s->info.outCh >= 6 ? &five : &two);
  s->enc->bit_rate = s->info.outCh >= 6 ? 640000 : 256000;
  int r = avcodec_open2(s->enc, ec, NULL); if (r < 0) { say(err, errn, "AC3 encoder", r); return -1; }
  return 0;
}
static void src_free(SbdSrc *s) {
  mux_close(s); q_clear(&s->vq); q_clear(&s->aq);
  av_bsf_free(&s->bsf); avcodec_free_context(&s->dec); avcodec_free_context(&s->enc); swr_free(&s->swr); if (s->fifo) av_audio_fifo_free(s->fifo);
  av_frame_free(&s->fr); av_frame_free(&s->ef); av_frame_free(&s->cv); av_packet_free(&s->pk); av_freep(&s->nal); av_freep(&s->wb);
  if (s->ic) avformat_close_input(&s->ic); if (s->io) { av_freep(&s->io->buffer); avio_context_free(&s->io); }
  free(s->seg); free(s);
}
static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
static SbdSrc *sbd_open(sbd_read rd, void *ud, long long size, int audioOrd, int flags, double segTarget, volatile int *stop, char *err, int errn) {
  SbdSrc *s = calloc(1, sizeof *s); if (!s) { snprintf(err, errn, "out of memory"); return NULL; }
  s->rd = rd; s->ud = ud; s->size = size; s->stop = stop; s->next = -1; s->vi = s->ai = -1; av_log_set_level(getenv("SB_DTS_AVLOG") ? AV_LOG_WARNING : AV_LOG_FATAL); /* (SB_DTS_AVLOG=1: the library's own warnings) */
  int r; unsigned char *b = av_malloc(IOBUF);
  s->io = b ? avio_alloc_context(b, IOBUF, 0, s, io_read, NULL, io_seek) : NULL; s->ic = avformat_alloc_context();
  if (!s->io || !s->ic) { av_free(b); snprintf(err, errn, "out of memory"); goto fail; }
  s->ic->pb = s->io; s->ic->flags |= AVFMT_FLAG_CUSTOM_IO; s->ic->interrupt_callback.callback = interrupted; s->ic->interrupt_callback.opaque = s;
  if ((r = avformat_open_input(&s->ic, NULL, NULL, NULL)) < 0) { say(err, errn, "not a file this reads", r); goto fail; } /* (no stream-info pass: the container's head describes every track) */
  for (unsigned i = 0, na = 0; i < s->ic->nb_streams; i++) { AVCodecParameters *p = s->ic->streams[i]->codecpar;
    if (p->codec_type == AVMEDIA_TYPE_VIDEO && s->vi < 0 && (p->codec_id == AV_CODEC_ID_H264 || p->codec_id == AV_CODEC_ID_HEVC)) s->vi = (int)i;
    if (p->codec_type == AVMEDIA_TYPE_AUDIO) { int conv = p->codec_id == AV_CODEC_ID_DTS || p->codec_id == AV_CODEC_ID_TRUEHD || p->codec_id == AV_CODEC_ID_MLP;
      if (s->ai < 0 && ((audioOrd >= 0 && (int)na == audioOrd) || (audioOrd < 0 && (conv || (flags & SBD_COPYAUDIO))))) s->ai = (int)i; na++; } }
  if (s->vi < 0) { snprintf(err, errn, "no H.264 / HEVC picture"); goto fail; }
  if (s->ai < 0) { snprintf(err, errn, "no such audio track"); goto fail; }
  for (unsigned i = 0; i < s->ic->nb_streams; i++) if ((int)i != s->vi && (int)i != s->ai) s->ic->streams[i]->discard = AVDISCARD_ALL; /* other tracks are not even queued */
  AVStream *V = s->ic->streams[s->vi], *A = s->ic->streams[s->ai]; s->vtb = V->time_base; s->atb = A->time_base;
  AVRational fr = V->avg_frame_rate.num ? V->avg_frame_rate : V->r_frame_rate.num ? V->r_frame_rate : (AVRational){ 24, 1 };
  s->fd = av_rescale_q(1, av_inv_q(fr), s->vtb); if (s->fd < 1) s->fd = 1;
  /* the video: start codes for MPEG-TS */
  const AVBitStreamFilter *bf = av_bsf_get_by_name(V->codecpar->codec_id == AV_CODEC_ID_HEVC ? "hevc_mp4toannexb" : "h264_mp4toannexb");
  if (!bf || av_bsf_alloc(bf, &s->bsf) < 0) { snprintf(err, errn, "no bitstream filter"); goto fail; }
  avcodec_parameters_copy(s->bsf->par_in, V->codecpar); s->bsf->time_base_in = s->vtb;
  if ((r = av_bsf_init(s->bsf)) < 0) { say(err, errn, "bitstream filter", r); goto fail; }
  /* the picture's Dolby Vision, from the container (Matroska's BlockAdditionMapping, MP4's dvcC / dvvC) */
  { const AVPacketSideData *sd = av_packet_side_data_get(V->codecpar->coded_side_data, V->codecpar->nb_coded_side_data, AV_PKT_DATA_DOVI_CONF);
    const AVDOVIDecoderConfigurationRecord *dv = sd && sd->size >= sizeof *dv ? (const void *)sd->data : NULL;
    if (dv && V->codecpar->codec_id == AV_CODEC_ID_HEVC && dv->rpu_present_flag && dv->bl_present_flag) {
      s->info.dvIn = dv->dv_profile;
      if (dv->dv_profile == 7 && (flags & SBD_DV81)) { s->dv81 = 1; s->info.dvOut = 8; dv_desc(s, 8, dv->dv_level, 1); }   /* 8.1: an HDR10 base */
      else if ((dv->dv_profile == 8 || dv->dv_profile == 5) && !dv->el_present_flag) { s->info.dvOut = dv->dv_profile; dv_desc(s, dv->dv_profile, dv->dv_level, dv->dv_bl_signal_compatibility_id); } } }
  if ((flags & SBD_DV81) && !s->dv81) { snprintf(err, errn, "not a Dolby Vision profile 7 picture"); goto fail; }
  if (flags & SBD_COPYAUDIO) { /* the track as it is: one the TS writer can carry */
    enum AVCodecID c = A->codecpar->codec_id;
    if (c != AV_CODEC_ID_AC3 && c != AV_CODEC_ID_EAC3 && c != AV_CODEC_ID_AAC && c != AV_CODEC_ID_DTS && c != AV_CODEC_ID_TRUEHD && c != AV_CODEC_ID_MP3 && c != AV_CODEC_ID_MP2) {
      snprintf(err, errn, "%s can't go in the stream as it is", avcodec_get_name(c)); goto fail; }
    s->copyA = 1; s->info.outCh = A->codecpar->ch_layout.nb_channels; s->outRate = A->codecpar->sample_rate > 0 ? A->codecpar->sample_rate : 48000;
    s->pk = av_packet_alloc(); if (!s->pk) { snprintf(err, errn, "out of memory"); goto fail; }
    goto index;
  }
  /* the sound: decoded, then AC3 */
  const AVCodec *dc = avcodec_find_decoder(A->codecpar->codec_id);
  if (!dc) { snprintf(err, errn, "no decoder for this sound"); goto fail; }
  s->dec = avcodec_alloc_context3(dc); avcodec_parameters_to_context(s->dec, A->codecpar); s->dec->pkt_timebase = s->atb;
  AVDictionary *o = NULL; int six = A->codecpar->ch_layout.nb_channels >= 6 || A->codecpar->codec_id != AV_CODEC_ID_DTS || !A->codecpar->ch_layout.nb_channels;
  if (A->codecpar->codec_id == AV_CODEC_ID_DTS) av_dict_set(&o, "core_only", "1", 0); /* the extension's lossless layers cost most of the CPU and AC3 can't carry them */
  else av_dict_set(&o, "downmix", "5.1(side)", 0);                                    /* TrueHD: its own 5.1 presentation, not the 7.1 / objects one */
  if ((r = avcodec_open2(s->dec, dc, &o)) < 0) { av_dict_free(&o); say(err, errn, "sound decoder", r); goto fail; } av_dict_free(&o);
  int ir = A->codecpar->sample_rate; s->outRate = ir == 48000 || ir == 44100 || ir == 32000 ? ir : 48000; s->info.outCh = six ? 6 : 2;
  if (enc_open(s, err, errn) < 0) goto fail;
  s->fifo = av_audio_fifo_alloc(s->enc->sample_fmt, s->enc->ch_layout.nb_channels, s->enc->frame_size * 4);
  s->fr = av_frame_alloc(); s->ef = av_frame_alloc(); s->cv = av_frame_alloc(); s->pk = av_packet_alloc();
  if (!s->fifo || !s->fr || !s->ef || !s->cv || !s->pk) { snprintf(err, errn, "out of memory"); goto fail; }
index:
  /* the keyframe index: Matroska reads its Cues on the first seek */
  s->info.dur = s->ic->duration > 0 ? s->ic->duration / (double)AV_TIME_BASE : 0;
  /* (Cues at the end are reached through the SeekHead; with none, Matroska's seek would read the whole file looking for
     keyframes: 12 MiB of reads at most, then the file counts as having no index; not tried at all when the caller has
     read the head already and found no Cues) */
  s->readCnt = 0; s->budget = 12 << 20; int over = 1;
  if (!(flags & SBD_NOINDEX)) { av_seek_frame(s->ic, s->vi, V->start_time != AV_NOPTS_VALUE ? V->start_time : 0, AVSEEK_FLAG_BACKWARD); over = s->readCnt > s->budget; }
  s->budget = 0;
  int ne = avformat_index_get_entries_count(V), nk = 0; double *kf = ne > 0 ? malloc(ne * sizeof *kf) : NULL;
  for (int i = 0; i < ne && kf; i++) { const AVIndexEntry *e = avformat_index_get_entry(V, i); if (e && (e->flags & AVINDEX_KEYFRAME)) kf[nk++] = e->timestamp * av_q2d(s->vtb); }
  if (nk) qsort(kf, nk, sizeof *kf, cmp_d);
  int indexed = !over && nk >= 2 && kf[0] < 10 && s->info.dur > 0 && kf[nk - 1] >= s->info.dur - fmax(30, s->info.dur * 0.05);
  s->seg = malloc((nk + 1) * sizeof *s->seg); s->nseg = 0;
  if (indexed && s->seg) { for (int i = 0; i < nk; i++) if (!s->nseg || kf[i] - s->seg[s->nseg - 1] >= segTarget - EPS) s->seg[s->nseg++] = kf[i]; }
  else if (s->seg) { s->seg[0] = 0; s->nseg = 1; indexed = 0; }
  free(kf);
  s->info.indexed = indexed; s->info.nseg = s->nseg; s->info.width = V->codecpar->width; s->info.height = V->codecpar->height;
  s->info.inCh = A->codecpar->ch_layout.nb_channels; s->info.outKbps = s->enc ? (int)(s->enc->bit_rate / 1000) : (int)(A->codecpar->bit_rate / 1000);
  snprintf(s->info.vcodec, sizeof s->info.vcodec, "%s", avcodec_get_name(V->codecpar->codec_id)); snprintf(s->info.acodec, sizeof s->info.acodec, "%s", avcodec_get_name(A->codecpar->codec_id));
  { char pic[40] = ""; if (s->dv81) snprintf(pic, sizeof pic, "Dolby Vision 7 as 8.1, "); else if (s->info.dvOut) snprintf(pic, sizeof pic, "Dolby Vision %d, ", s->info.dvOut);
    if (s->copyA) snprintf(s->info.path, sizeof s->info.path, "%s%s %d ch copied", pic, avcodec_get_name(A->codecpar->codec_id), s->info.outCh);
    else snprintf(s->info.path, sizeof s->info.path, "%s%s decoded, AC3 %s %d kb/s", pic, A->codecpar->codec_id == AV_CODEC_ID_DTS ? "DTS core" : "TrueHD 5.1", s->info.outCh >= 6 ? "5.1" : "2.0", s->info.outKbps); }
  return s;
fail:
  src_free(s); return NULL;
}
static const sbd_info *sbd_info_of(SbdSrc *s) { return &s->info; }
static double sbd_seg_start(SbdSrc *s, int i) { return i <= 0 || i >= s->nseg ? (i <= 0 ? 0 : s->info.dur) : s->seg[i]; }
static double sbd_seg_dur(SbdSrc *s, int i) { if (i < 0 || i >= s->nseg) return 0; double e = i + 1 < s->nseg ? s->seg[i + 1] : s->info.dur; return e > s->seg[i] ? e - (i ? s->seg[i] : 0) : 0.1; }

/* ---- a run ---- */
/* no index: the Cluster nearest a byte position read straight from the file (its ID, then its Timestamp), corrected from
   the time found there a few times; the demuxer is then pointed at it as a one-entry index, which its own seek resets
   to properly (a byte seek would leave it parsing from the middle of an element). 0 found, -1 not */
static int find_cluster(SbdSrc *s, long long from, long long *pos, double *t) {
  enum { N = 1 << 20 }; unsigned char *b = av_malloc(N); if (!b) return -1; int got = 0, r = -1;
  while (got < N) { int k = s->rd(s->ud, from + got, b + got, N - got); if (k <= 0) break; got += k; }
  for (int i = 0; i + 12 < got && r < 0; i++) {
    if (b[i] != 0x1F || b[i + 1] != 0x43 || b[i + 2] != 0xB6 || b[i + 3] != 0x75) continue;
    int o = i + 4, l = 1; unsigned m = 0x80; while (l <= 8 && !(b[o] & m)) { l++; m >>= 1; } if (l > 8) continue; o += l;   /* the Cluster's size */
    if (b[o] == 0xBF && b[o + 1] == 0x84) o += 6;                                                                       /* a CRC-32 first */
    if (o + 2 >= got || b[o] != 0xE7) continue; int n = b[o + 1] & 0x7F; if (!(b[o + 1] & 0x80) || n < 1 || n > 8 || o + 2 + n > got) continue;
    unsigned long long v = 0; for (int j = 0; j < n; j++) v = v << 8 | b[o + 2 + j];
    *pos = from + i; *t = v * av_q2d(s->vtb); r = 0;
  }
  av_free(b); return r;
}
static int locate(SbdSrc *s, double at) {
  long long size = s->size > 0 ? s->size : avio_size(s->io), pos = 0, cp = 0; double dur = s->info.dur > 0 ? s->info.dur : 1, ct = 0;
  if (at > 1 && size > 0) {
    long long guess = (long long)(at / dur * size);
    for (int tries = 0; tries < 6; tries++) {
      if (find_cluster(s, guess, &cp, &ct) < 0) { guess -= 1 << 20; if (guess < 0) break; continue; }
      pos = cp; if ((ct <= at && at - ct < 6) || tries == 5) break;          /* a little before the moment: its keyframes follow */
      long long np = guess + (long long)((at - ct - 2) / dur * size); if (np < 0) np = 0; if (np > size - (2 << 20)) np = size - (2 << 20);
      if (llabs(np - guess) < (256 << 10)) { if (ct > at && guess > (1 << 20)) np = guess - (1 << 20); else break; }
      guess = np;
    }
  }
  if (!pos) return av_seek_frame(s->ic, s->vi, 0, AVSEEK_FLAG_BYTE) < 0 ? -1 : 0;
  AVStream *V = s->ic->streams[s->vi]; int64_t ts = (int64_t)llround(ct / av_q2d(s->vtb));
  av_add_index_entry(V, pos, ts, 0, 0, AVINDEX_KEYFRAME);
  return av_seek_frame(s->ic, s->vi, ts, AVSEEK_FLAG_BACKWARD) < 0 ? -1 : 0;
}
static int fresh(SbdSrc *s, double at, char *err, int errn) {
  mux_close(s); q_clear(&s->vq); q_clear(&s->aq); s->npend = 0; s->lastDts = AV_NOPTS_VALUE;
  av_bsf_flush(s->bsf); if (s->dec) avcodec_flush_buffers(s->dec); if (s->fifo) av_audio_fifo_reset(s->fifo); s->gridOn = 0;
  if (!s->copyA && enc_open(s, err, errn) < 0) return -1; /* the encoder again: nothing of the last place leaks into the new one */
  int r;
  s->vEof = s->aEof = 0; s->needKey = 1;
  AVStream *V = s->ic->streams[s->vi]; int64_t ts = (int64_t)llround(at / av_q2d(s->vtb)); if (V->start_time != AV_NOPTS_VALUE && ts < V->start_time) ts = V->start_time;
  if (!s->info.indexed) { if (locate(s, at) < 0) { snprintf(err, errn, "can't find %.0f s in the file", at); return -1; } }
  else if ((r = av_seek_frame(s->ic, s->vi, ts, AVSEEK_FLAG_BACKWARD)) < 0 && at > 0) { say(err, errn, "seek", r); return -1; }
  return mux_open(s, err, errn);
}
static int read_one(SbdSrc *s, char *err, int errn) {
  int r = av_read_frame(s->ic, s->pk);
  if (r == AVERROR_EXIT && !(s->ic->pb && s->ic->pb->eof_reached && !(s->stop && *s->stop))) return -2;
  if (r < 0) { /* the end (or a read that keeps failing): everything left goes out */
    if (r != AVERROR_EOF && r != AVERROR_EXIT && !(s->ic->pb && s->ic->pb->eof_reached)) { say(err, errn, "read", r); return -1; }
    av_bsf_send_packet(s->bsf, NULL); for (;;) { AVPacket *o = av_packet_alloc(); if (!o || av_bsf_receive_packet(s->bsf, o) < 0) { av_packet_free(&o); break; } video_out(s, o); }
    dts_assign(s, 1); s->vEof = 1;
    if (!s->copyA) { avcodec_send_packet(s->dec, NULL); for (;;) { if (avcodec_receive_frame(s->dec, s->fr) < 0) break; frame_in(s, s->fr); av_frame_unref(s->fr); }
      drain_fifo(s, 1); encode_frame(s, NULL); }
    s->aEof = 1;
    return 0;
  }
  if (s->pk->stream_index == s->vi) return video_in(s, s->pk);
  if (s->pk->stream_index == s->ai) return audio_in(s, s->pk);
  av_packet_unref(s->pk); return 0;
}
static int sbd_run(SbdSrc *s, int seg, double from, sbd_write w, void *ud, volatile int *stop, sbd_stats *st, char *err, int errn) {
  sbd_stats dummy; if (!st) st = &dummy; memset(st, 0, sizeof *st); st->first = -1; s->st = st; s->stop = stop; s->w = w; s->wud = ud; s->werr = 0; s->readWait = 0; s->peak = 0;
  double c0 = cpu_now(), w0 = wall_now(); int r = 0;
  if (seg >= s->nseg) { snprintf(err, errn, "no segment %d", seg); return -1; }
  int cont = seg >= 0 && seg == s->next && s->oc;
  if (!cont) { st->fresh = 1; s->runSeg = seg; s->curVSeg = seg >= 0 ? seg : 0; double at = seg >= 0 ? s->seg[seg] : from; s->keyAt = seg >= 0 ? s->seg[seg] : -1e9; s->audioFrom = seg >= 0 ? s->seg[seg] : -1e9;
    if (fresh(s, at, err, errn) < 0) { s->next = -1; mux_close(s); return -1; } }
  else { s->runSeg = seg; av_opt_set(s->oc->priv_data, "mpegts_flags", "+resend_headers", 0); }
  for (;;) {
    if (stop && *stop) { r = -2; break; }
    if (s->werr) { r = -2; break; }
    int cs = s->runSeg >= 0 ? seg : 0;
    QE *vh = s->vq.h, *ah = s->aq.h;
    int vDone = (vh && vh->seg > cs) || (!vh && s->vEof), aDone = (ah && ah->seg > cs) || (!ah && s->aEof);
    int over = s->vq.bytes + s->aq.bytes > QMAX; /* the other stream is not coming: what is there goes out */
    int vOk = vh && vh->seg == cs && vh->has, aOk = ah && ah->seg == cs;
    if (vOk && (aOk ? vh->t <= ah->t : (aDone || over))) { if ((r = write_pkt(s, q_pop(&s->vq), 1)) < 0) break; continue; }
    if (aOk && (vOk ? ah->t < vh->t : (vDone || over))) { if ((r = write_pkt(s, q_pop(&s->aq), 0)) < 0) break; continue; }
    if (vDone && aDone) { r = 0; break; }
    if (s->vEof && s->aEof) { r = 0; break; }
    if ((r = read_one(s, err, errn)) < 0) break;
  }
  if (r >= 0 || r == AVERROR(EPIPE)) { av_write_frame(s->oc, NULL); avio_flush(s->oc->pb); } /* the segment complete in itself */
  if (r == AVERROR(EPIPE) || s->werr) r = -2;
  else if (r < -2) { say(err, errn, "write", r); r = -1; }
  s->next = r == 0 && seg >= 0 && seg + 1 < s->nseg ? seg + 1 : -1;
  if (r != 0) mux_close(s);   /* stopped half-way: the next run starts over */
  st->cpu = cpu_now() - c0; st->wall = wall_now() - w0; st->readWait = s->readWait; st->peakQueue = s->peak;
  return r;
}
static void sbd_close(SbdSrc *s) { if (s) src_free(s); }
static int sbd_api_v(void) { return SBD_API_VERSION; }
static const char *sbd_version(void) { static char v[64]; snprintf(v, sizeof v, "FFmpeg %s", av_version_info()); return v; }
static const sbd_api API_ = { sbd_api_v, sbd_version, sbd_open, sbd_info_of, sbd_seg_start, sbd_seg_dur, sbd_run, sbd_close };
API const sbd_api *sbd_get(void) { return &API_; }

#ifdef SBD_PREVIEW
/* ---- seek previews: one keyframe in, one small RGB picture out (see sbdts.h) ---- */
typedef struct { AVCodecContext *c; AVPacket *pk; AVFrame *f; } PrevDec;
static int prev_api(void) { return SBD_PREV_VERSION; }
static void prev_close(void *h) { PrevDec *d = h; if (!d) return; avcodec_free_context(&d->c); av_packet_free(&d->pk); av_frame_free(&d->f); free(d); }
static void *prev_open(const char *codecId, const unsigned char *extra, int extraLen) {
  enum AVCodecID id = !strcmp(codecId, "V_MPEG4/ISO/AVC") ? AV_CODEC_ID_H264 : !strcmp(codecId, "V_MPEGH/ISO/HEVC") ? AV_CODEC_ID_HEVC : AV_CODEC_ID_NONE;
  if (id == AV_CODEC_ID_NONE) id = !strcmp(codecId, "h264") ? AV_CODEC_ID_H264 : !strcmp(codecId, "hevc") ? AV_CODEC_ID_HEVC : AV_CODEC_ID_NONE; /* (a platform's own names: start-code frames, another player's extractor) */
  const AVCodec *cd = id != AV_CODEC_ID_NONE ? avcodec_find_decoder(id) : NULL; if (!cd) return NULL;
  PrevDec *d = calloc(1, sizeof *d); if (!d) return NULL;
  d->c = avcodec_alloc_context3(cd); d->pk = av_packet_alloc(); d->f = av_frame_alloc(); if (!d->c || !d->pk || !d->f) { prev_close(d); return NULL; }
  if (extra && extraLen > 0) { /* avcC / hvcC (frames length-prefixed) or start-code parameter sets (frames with start codes): the decoder tells them apart; none: the parameter sets come inside the frame */
    d->c->extradata = av_mallocz(extraLen + AV_INPUT_BUFFER_PADDING_SIZE); if (!d->c->extradata) { prev_close(d); return NULL; }
    memcpy(d->c->extradata, extra, extraLen); d->c->extradata_size = extraLen; }
  d->c->thread_count = 1; d->c->skip_loop_filter = AVDISCARD_ALL; d->c->flags |= AV_CODEC_FLAG_OUTPUT_CORRUPT; d->c->flags2 |= AV_CODEC_FLAG2_SHOW_ALL; /* (a keyframe that is not an IDR: open GOP, CRA) */
  if (avcodec_open2(d->c, cd, NULL) < 0) { prev_close(d); return NULL; }
  return d;
}
/* the picture shrunk straight from its planes: each output pixel the mean of its box (integer sums), BT.709 limited range to RGB on the small picture only */
static int prev_frame(void *h, const unsigned char *data, int len, int w, int hh, unsigned char *rgb, int *srcW, int *srcH, float *dar, int *dark) {
  PrevDec *d = h; if (!d || !data || len <= 0 || w <= 0 || hh <= 0) return -1;
  if (av_new_packet(d->pk, len) < 0) return -1; memcpy(d->pk->data, data, len); d->pk->flags |= AV_PKT_FLAG_KEY;
  int r = avcodec_send_packet(d->c, d->pk); av_packet_unref(d->pk); if (r < 0 && r != AVERROR(EAGAIN)) { avcodec_flush_buffers(d->c); return -1; }
  avcodec_send_packet(d->c, NULL); /* (drained: a decoder holds its frames for reordering) */
  int got = 0; while (!got) { r = avcodec_receive_frame(d->c, d->f); if (r < 0) break; got = 1; }
  if (!got) { avcodec_flush_buffers(d->c); return -1; }
  AVFrame *f = d->f; const AVPixFmtDescriptor *ds = av_pix_fmt_desc_get(f->format); int ok = ds && !(ds->flags & AV_PIX_FMT_FLAG_RGB) && ds->nb_components >= 3 && f->width > 0 && f->height > 0;
  int deep = ok && ds->comp[0].depth > 8, sh = deep ? ds->comp[0].depth - 8 : 0, cw = ok ? ds->log2_chroma_w : 0, ch = ok ? ds->log2_chroma_h : 0;
  long long sumY = 0;
  if (ok) for (int oy = 0; oy < hh; oy++) { int y0 = (int)((long long)oy * f->height / hh), y1 = (int)((long long)(oy + 1) * f->height / hh); if (y1 <= y0) y1 = y0 + 1;
    for (int ox = 0; ox < w; ox++) { int x0 = (int)((long long)ox * f->width / w), x1 = (int)((long long)(ox + 1) * f->width / w); if (x1 <= x0) x1 = x0 + 1;
      long sy = 0, su = 0, sv = 0; int ny = 0, nc = 0;
      for (int y = y0; y < y1; y += 2) { const unsigned char *ry = f->data[0] + (long)y * f->linesize[0]; /* (every other line and column: the box's mean, a quarter of the reads) */
        for (int x = x0; x < x1; x += 2) { sy += deep ? (((const uint16_t *)ry)[x] >> sh) : ry[x]; ny++; } }
      for (int y = y0 >> ch; y < ((y1 + (1 << ch) - 1) >> ch); y += 2) { const unsigned char *ru = f->data[1] + (long)y * f->linesize[1], *rv = f->data[2] + (long)y * f->linesize[2];
        for (int x = x0 >> cw; x < ((x1 + (1 << cw) - 1) >> cw); x += 2) { su += deep ? (((const uint16_t *)ru)[x] >> sh) : ru[x]; sv += deep ? (((const uint16_t *)rv)[x] >> sh) : rv[x]; nc++; } }
      int Y = ny ? (int)(sy / ny) : 16, U = nc ? (int)(su / nc) : 128, V = nc ? (int)(sv / nc) : 128; sumY += Y;
      int c = Y - 16, dd = U - 128, e = V - 128; /* BT.709 limited range, fixed point (x256) */
      int R = (298 * c + 459 * e + 128) >> 8, G = (298 * c - 55 * dd - 136 * e + 128) >> 8, B = (298 * c + 541 * dd + 128) >> 8;
      unsigned char *o = rgb + ((long)oy * w + ox) * 3; o[0] = R < 0 ? 0 : R > 255 ? 255 : R; o[1] = G < 0 ? 0 : G > 255 ? 255 : G; o[2] = B < 0 ? 0 : B > 255 ? 255 : B; } }
  if (srcW) *srcW = f->width; if (srcH) *srcH = f->height;
  if (dar) { AVRational sar = f->sample_aspect_ratio; *dar = f->height ? (float)f->width / f->height * (sar.num > 0 && sar.den > 0 ? (float)sar.num / sar.den : 1.0f) : 0; }
  if (dark) *dark = ok && sumY / ((long long)w * hh) < 24;
  av_frame_unref(f); avcodec_flush_buffers(d->c);
  return ok ? 0 : -1;
}
static const sbd_prev_api PREV_ = { prev_api, prev_open, prev_frame, prev_close };
API const sbd_prev_api *sbd_prev_get(void) { return &PREV_; }
#endif
