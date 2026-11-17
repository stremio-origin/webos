/* Seek previews from a lighter copy of the film (see prevsrc.h) */
#include "core/prevsrc.h"
#include "core/seekprev.h"
#include "core/prevspike.h"
#include "core/best.h"
#include "core/stremio.h"
#include "core/app.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NC 3 /* copies tried at most, best first */
static struct { char type[16], id[SB_ID], vid[SB_VID], film[SB_URL_MAX]; unsigned tok; int phase, nc, ci, durMin, hold; Stream c[NC]; double sc[NC]; MkvSession *m; double at, makeAt, filmSize; char key[300]; } PS;
/* phase 0 idle, 1 asking the addons, 2 reading a copy's index, 3 the copy makes pictures, 4 none to use */

static int has_w(const char *l, const char *w) { size_t n = strlen(w); for (const char *p = strstr(l, w); p; p = strstr(p + 1, w)) { int a = p == l || !isalnum((unsigned char)p[-1]), b = !isalnum((unsigned char)p[n]); if (a && b) return 1; } return 0; }
/* a copy fit to make the pictures: light, SDR, H.264 or HEVC in MKV or MP4, cached or direct; the lightest first */
static int light(const Stream *s, double *score) {
  if (s->p2p || strncmp(s->url, "http", 4)) return 0; if (PS.filmSize > 0 && !(s->size > 0 && s->size * 2.5 <= PS.filmSize)) return 0; /* (not clearly lighter than the film: its own keyframes are as cheap) */
  BestRate r; if (!best_rate(s, NULL, -1, PS.durMin > 0 ? PS.durMin : 100, &r)) return 0; /* (cams, files not cached yet: never) */
  char t[2048], l[2048]; best_stream_info(s, t, sizeof t); size_t i = 0; for (; t[i] && i < sizeof l - 1; i++) l[i] = (char)tolower((unsigned char)t[i]); l[i] = 0;
  for (char *p = l; *p; p++) if (strchr("._-[]()", *p)) *p = ' ';
  char hd[32]; best_hdr_of(t, hd, sizeof hd); if (hd[0] || has_w(l, "dv") || has_w(l, "dovi")) return 0; /* (HDR pictures come out grey: SDR only) */
  const char *rs = best_res_of(t); int res = rs ? atoi(rs) : 0; if (res >= 1440 || strstr(l, "2160") || has_w(l, "4k") || has_w(l, "uhd")) return 0;
  if (has_w(l, "av1") || has_w(l, "xvid") || has_w(l, "divx") || has_w(l, "mpeg2") || has_w(l, "vc1") || has_w(l, "remux")) return 0;
  const char *fn = s->filename[0] ? s->filename : ""; size_t fl = strlen(fn);
  if (fl > 4 && (!strcasecmp(fn + fl - 4, ".avi") || !strcasecmp(fn + fl - 3, ".ts") || !strcasecmp(fn + fl - 5, ".m2ts") || !strcasecmp(fn + fl - 4, ".wmv"))) return 0;
  double gb = s->size > 0 ? s->size / 1e9 : 0; if (gb > 8) return 0;
  double sc = res == 720 ? 30 : res == 1080 ? 22 : res == 480 || res == 576 ? 6 : 0;
  sc += has_w(l, "x264") || has_w(l, "h264") || has_w(l, "avc") ? 15 : has_w(l, "x265") || has_w(l, "h265") || has_w(l, "hevc") ? 4 : 0;
  sc += gb > 0 ? -gb * 8 : -35; if (gb > 0 && gb < 0.35) sc -= 25; /* (a tiny file: often a sample or a different cut) */
  *score = sc; return 1; }
static void reset(void) { sp_verify_reset(); sp_source_stop(); if (PS.m) { mkv_close(PS.m); PS.m = NULL; } PS.phase = 0; PS.nc = PS.ci = 0; PS.tok++; }
void ps_cancel(void) { if (PS.phase) fprintf(stderr, "[prevsrc] stopped\n"); reset(); PS.vid[0] = 0; PS.hold = 0; }
static void open_cur(void) {
  if (PS.ci >= PS.nc) { PS.phase = 4; fprintf(stderr, "[prevsrc] no lighter copy to use: the film's own keyframes make its pictures\n"); return; }
  const Stream *s = &PS.c[PS.ci]; char info[2048]; best_stream_info(s, info, sizeof info); if (strlen(info) > 120) info[120] = 0;
  fprintf(stderr, "[prevsrc] trying a lighter copy (%.1f GB, score %.0f): %s\n", s->size / 1e9, PS.sc[PS.ci], info);
  PS.m = mkv_open_keys(s->url); PS.phase = PS.m ? 2 : 4; PS.at = app_now(); }
static void on_part(StreamGroup *g, int gi, void *ud) { (void)gi;
  if ((unsigned)(uintptr_t)ud == PS.tok && PS.phase == 1) for (int i = 0; i < g->n; i++) { const Stream *s = &g->items[i]; double sc; if (!strcmp(s->url, PS.film) || !light(s, &sc)) continue; { int dup = 0; for (int q = 0; q < PS.nc; q++) if (!strcmp(PS.c[q].url, s->url) || (s->size > 0 && PS.c[q].size == s->size)) dup = 1; if (dup) continue; } /* (the same file from two addons: once) */
      int at = PS.nc; while (at > 0 && PS.sc[at - 1] < sc) at--; if (at >= NC) continue; int last = PS.nc < NC ? PS.nc : NC - 1;
      for (int k = last; k > at; k--) { PS.c[k] = PS.c[k - 1]; PS.sc[k] = PS.sc[k - 1]; } PS.c[at] = *s; PS.sc[at] = sc; if (PS.nc < NC) PS.nc++; }
  free(g->items); }
static void on_done(int total, void *ud) { (void)total; if ((unsigned)(uintptr_t)ud != PS.tok || PS.phase != 1) return; PS.ci = 0; open_cur(); }
void ps_want(const char *type, const char *id, const char *vid, int durMin, const char *filmUrl, double filmSize) {
  if (!prev_enabled() || !vid || !*vid) return;
  if (filmUrl) snprintf(PS.film, sizeof PS.film, "%s", filmUrl); PS.filmSize = filmUrl ? filmSize : 0;
  if (!strcmp(PS.vid, vid) && PS.phase) { if (PS.phase == 3 && PS.m && !sp_source_on(PS.m)) { sp_source_start(PS.m, PS.key); PS.makeAt = app_now(); } /* (stopped while idle: on again, its kept pictures at once) */
    if (filmUrl && PS.phase >= 2 && PS.phase <= 3 && (!strcmp(PS.c[PS.ci].url, filmUrl) || (filmSize > 0 && !(PS.c[PS.ci].size > 0 && PS.c[PS.ci].size * 2.5 <= filmSize)))) { fprintf(stderr, "[prevsrc] the film is about as light as the copy: its own keyframes\n"); ps_cancel(); } return; }
  reset(); snprintf(PS.type, sizeof PS.type, "%s", type); snprintf(PS.id, sizeof PS.id, "%s", id ? id : vid); snprintf(PS.vid, sizeof PS.vid, "%s", vid); PS.durMin = durMin; PS.phase = 1; PS.at = app_now();
  fprintf(stderr, "[prevsrc] looking for a lighter copy of %s\n", vid); sb_streams(type, vid, on_part, on_done, (void *)(uintptr_t)PS.tok); }
void ps_next(void) { if (PS.phase < 2) return; sp_verify_reset(); sp_source_stop(); if (PS.m) { mkv_close(PS.m); PS.m = NULL; } PS.ci++; open_cur(); }
void ps_hold(int playing) { PS.hold = playing; if (!playing) sp_source_idle(); }
MkvSession *ps_session(void) { return PS.phase == 3 ? PS.m : NULL; }
int ps_busy(void) { return PS.phase == 1 || PS.phase == 2; }
const char *ps_vid(void) { return PS.vid; }
void ps_tick(void) {
  double now = app_now();
  if (PS.phase == 1 && now - PS.at > 20) { fprintf(stderr, "[prevsrc] the addons took too long\n"); PS.phase = 4; }
  if (PS.phase == 2) { int nk = mkv_key_count(PS.m);
    if (nk >= 4) { double lt = 0; mkv_key_time(PS.m, nk - 1, &lt); snprintf(PS.key, sizeof PS.key, "%s|%s|%.0f|%.0f", PS.id, PS.vid, PS.c[PS.ci].size, lt + 5);
      PS.phase = 3; PS.makeAt = now; fprintf(stderr, "[prevsrc] the copy's index in after %.1f s: %d keyframes\n", now - PS.at, nk); sp_source_start(PS.m, PS.key); }
    else if (mkv_dead(PS.m) || (mkv_state(PS.m) == MKV_FALLBACK && now - PS.at > 2) || now - PS.at > 25) { fprintf(stderr, "[prevsrc] the copy's index can't be read after %.1f s (%s, state %d, %d keyframes)\n", now - PS.at, mkv_error(PS.m), mkv_state(PS.m), nk); ps_next(); } }
  if (PS.phase == 3 && !PS.hold && sp_source_on(PS.m) && (sp_source_made() >= 96 || now - PS.makeAt > 150)) { fprintf(stderr, "[prevsrc] nothing plays: the copy rests, its pictures kept\n"); sp_source_stop(); } }
