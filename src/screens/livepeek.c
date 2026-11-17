#include "screens/livepeek.h"
#include "screens/btrailer.h"
#include "screens/player.h"
#include "core/app.h"
#include "core/bgtrailer.h"
#include "core/hproxy.h"
#include "core/live.h"
#include "core/video.h"
#include "ui/gfx.h"
#include "ui/img.h"
#include "ui/palette.h"
#include "ui/icon.h"
#include "ui/text.h"
#include "core/i18n.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define REST 0.5      /* the remote rests this long on a channel before its preview is looked for; and before the one playing gives way to it */
#define STEADY 0.35   /* playing this long before it shows (the TV says playing a few frames before the picture) */
#ifdef SB_ANDROIDTV
#define GIVEUP 20.0   /* loaded and still no picture: none (Android TV's player takes longer to its first picture on some channels) */
#else
#define GIVEUP 12.0   /* loaded and still no picture: none */
#endif

static struct { char id[SB_ID], src[SB_URL_MAX]; Stream st; Item it; int asked, own, quiet, shown, failed, reloads, out, handed; double t0, steadyAt; Tween a; } K; /* handed: given to the player (its own window grows from the box) */ /* out: the picture fading to black before the next channel */
static Item want; static int wanted; static float boxR = 22; /* boxR: the box's corner radius as last drawn */ static float wx, wy, ww, wh, bx, by, bw, bh; /* the box asked for, the one the picture is placed in */

void lpk_want(const Item *it, float x, float y, float w, float h) { if (it) { want = *it; wanted = 1; wx = x; wy = y; ww = w; wh = h; } }
int lpk_busy(void) { return K.id[0] != 0; }
int lpk_shown_now(void) { return K.id[0] && K.shown && !K.out && !K.failed && K.a.v > 0.99f; } /* (development: SB_SCRIPT "hunt") */
/* Back on a channel that grew from its preview: it can go back into the box, and takes its playback back there */
int lpk_can_return(const char *id) { return id && K.handed && !strcmp(K.it.id, id); }
int lpk_take_back(const char *id) { if (!lpk_can_return(id)) return 0; snprintf(K.id, sizeof K.id, "%s", id); K.own = 1; K.shown = 1; K.asked = 1; K.failed = 0; K.out = 0; K.handed = 0; K.quiet = 0;
  tween_set(&K.a, 1); if (bw >= SB_W - 1 && bh >= SB_H - 1) video_set_window(0, 0, SB_W, SB_H); /* (full screen behind All channels: there again, whatever the player did) */ want = K.it; wanted = 1; live_peek_set(id, &K.st); /* (its stream known again: OK takes it as it plays once more) */ fprintf(stderr, "[peek] the player gave it back as it plays\n"); return 1; }
/* OK on the channel previewing: the player takes the stream as it plays (no second load), the box it is in given */
int lpk_handoff(const char *id, char *src, size_t cap, float *r) {
  if (!id || strcmp(id, K.id) || !K.own || !K.shown || K.out || K.a.v < 0.5f) return 0;
  snprintf(src, cap, "%s", K.src); r[0] = bx; r[1] = by; r[2] = bw; r[3] = bh; r[4] = boxR;
  K.own = 0; K.shown = 0; K.asked = 0; K.id[0] = 0; K.handed = 1; tween_set(&K.a, 0); fprintf(stderr, "[peek] handed to the player as it plays\n"); return 1; }

/* OK on the channel previewing: the player loads it as any channel (its own loader); the preview lets go of it without
   unloading (the player's load takes the TV's pipeline), and starts again in its box once the player is gone */
void lpk_release(void) { if (!K.id[0]) return; K.own = 0; K.shown = 0; K.asked = 0; K.id[0] = 0; K.steadyAt = 0; K.quiet = 0; tween_set(&K.a, 0); fprintf(stderr, "[peek] let go: the player opens the channel\n"); }
static void let_go(void) { if (K.own && !player_open()) { video_set_volume(0, 0); video_unload(); } K.own = 0; K.shown = 0; K.quiet = 0; K.steadyAt = 0; tween_set(&K.a, 0); }
/* behind another app (#69): a channel previewing goes, its sound with it; resting on it again starts it afresh */
void lpk_away(void) { if (!K.id[0]) return; let_go(); K.asked = 0; K.id[0] = 0; fprintf(stderr, "[peek] let go: behind another app\n"); }
static void load(const Stream *s) {
  char u[SB_URL_MAX]; snprintf(u, sizeof u, "%s", s->url);
  if (strncmp(u, "http", 4)) { K.failed = 1; return; } /* (YouTube, a torrent: the player's own ways, not here) */
  if (s->hdrs[0] || s->rhdrs[0]) hproxy_url(s->url, s->hdrs, s->rhdrs, u, sizeof u);
  fprintf(stderr, "[peek] loading the channel's preview\n");
  snprintf(K.src, sizeof K.src, "%s", s->url); K.st = *s; video_load(u); video_set_live(1); K.own = 1; K.quiet = 1; K.t0 = app_now(); K.steadyAt = 0;
}
static void place(void) { bx = wx; by = wy; bw = ww; bh = wh; video_set_window(bx, by, bw, bh); }
/* All channels from Home: the channel plays full screen behind it. How much of its picture is up (0..1), and the one
   it is; what it is doing for a channel (2 playing, 1 on its way, -1 no picture, 0 nothing), with its quality */
static int is_full(void) { return ww >= SB_W - 1 && wh >= SB_H - 1; }
float lpk_full_a(void) { return is_full() && (K.own || K.out) && !K.handed ? K.a.v : 0; }
const char *lpk_full_id(void) { return is_full() && K.own && K.shown && !K.out ? K.id : ""; }
int lpk_fullscreen(const char *id) { return id && is_full() && !strcmp(id, K.id) && K.own && K.shown && !K.out && K.a.v >= 0.5f; }
int lpk_state(const char *id, char *q, size_t cap) { if (q && cap) q[0] = 0; if (!id || strcmp(id, K.id)) return 0;
  if (K.failed) return -1; if (K.own && K.shown && !K.out) { if (q) { const char *n = K.st.name, *t = K.st.title; int hh = video_height();
      snprintf(q, cap, "%s", hh >= 2000 || strstr(n, "4K") || strstr(t, "4K") ? "4K" : hh >= 700 || strstr(n, "HD") || strstr(t, "HD") ? "HD" : hh > 0 || strstr(n, "SD") ? "SD" : ""); } return 2; }
  return K.asked ? 1 : 0; }

void lpk_tick(float dt) {
  tween_tick(&K.a, dt);
  if (!player_open()) K.handed = 0;
  if (player_open()) { if (K.own) { K.own = 0; K.shown = 0; tween_set(&K.a, 0); } K.id[0] = 0; K.asked = 0; wanted = 0; return; } /* the player has the decoder now */
  const char *w = wanted ? want.id : ""; wanted = 0;
  if (!strcmp(w, K.id) && K.out) { K.out = 0; tween_to(&K.a, 1, 0.3f, EASE_CSS_EASEOUT); video_set_volume(live_remote_on && live_remote_on() ? 0 : 100, 300); } /* back on it while it faded: it comes back */
  if (strcmp(w, K.id) && *w && K.own && K.id[0] && !K.out && app_idle() < REST) { video_pump(); return; } /* moving over the others: the one playing plays on until the remote rests on another */
  if (strcmp(w, K.id)) { /* another channel, or none: the picture playing fades to black first (and its sound), then it goes */
    if (K.own && K.shown && !K.out && K.a.v > 0.01f) { K.out = 1; tween_to(&K.a, 0, 0.3f, EASE_CSS_EASEIN); video_set_volume(0, 300); }
    if (K.out && K.a.v > 0.01f) { video_pump(); return; } /* (its events still delivered as it fades: no frozen frame) */
    K.out = 0; let_go(); if (K.asked) live_peek_stop(); K.asked = 0; K.failed = 0; K.reloads = 0;
    snprintf(K.id, sizeof K.id, "%s", w); if (*w) K.it = want; }
  if (!K.id[0] || K.failed) return;
  if (K.own && (wx != bx || wy != by || ww != bw || wh != bh)) place(); /* (Home's box, then the hub's: the picture follows) */
  if (!K.asked) { if (app_idle() >= REST) { live_peek(&K.it); K.asked = 1; } return; }
  if (!K.own) { Stream s; int st = live_peek_stream(K.id, &s);
    if (st < 0) { K.failed = 1; return; }
    if (st == 1 && !btr_owns() && !btr_under() && (bgt_state() == BGT_OFF || bgt_state() == BGT_GONE)) { load(&s); place(); } /* the decoder free: a trailer lets go first */
    return; }
  video_pump(); VidState vs = video_state(); double now = app_now();
  { static int rem; int r = live_remote_on && live_remote_on(); if (K.shown && !K.out && r != rem) video_set_volume(r ? 0 : 100, 300); rem = r; } /* (casting: the preview silent, the TV has the sound; back on the phone: it comes back) */
  if (K.quiet && video_has_media()) { K.quiet = 0; video_set_volume(0, 0); } /* silent until it shows */
  if (!K.shown) {
    if (vs == VID_PLAYING && video_live()) { if (!K.steadyAt) { K.steadyAt = now; place(); } } else K.steadyAt = 0;
    if (K.steadyAt && now - K.steadyAt > STEADY) { K.shown = 1; place(); if (is_full()) app_pace_again(); /* (the TV may have set its panel to this video's rate) */ tween_to(&K.a, 1, 0.5f, EASE_CSS_EASEOUT); video_set_volume(live_remote_on && live_remote_on() ? 0 : 100, 900); fprintf(stderr, "[peek] showing %dx%d\n", video_width(), video_height()); }
    else if (now - K.t0 > GIVEUP || vs == VID_ERROR) { extern int live_peek_next(const Item *, const char *); let_go();
      if (live_peek_next(&K.it, K.src)) fprintf(stderr, "[peek] no picture: the channel's next stream\n"); /* (as the player's failover) */
      else { fprintf(stderr, "[peek] no picture: none\n"); K.failed = 1; } }
    return; }
  if (vs == VID_ENDED || vs == VID_ERROR) { /* a live stream that stops: loaded again at its edge, twice at most */
    Stream s; if (K.reloads < 2 && live_peek_stream(K.id, &s) == 1) { K.reloads++; let_go(); load(&s); place(); fprintf(stderr, "[peek] stopped: loaded again\n"); }
    else { let_go(); K.failed = 1; } }
}

/* a soft halo round a rounded box (white, its light falling off smoothly outside the box's outline; inside, none): the
   box sits at 40..88 of its 128 px with a 22 px corner, the light spreads 40 px out */
static GLuint halo_tex(void) { static GLuint t; if (t) return t; enum { N = 128 }; static unsigned char px[N * N * 4]; const float B0 = 40, B1 = 88, R = 22;
  for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) { float fx = x + .5f, fy = y + .5f, qx = fmaxf(fmaxf(B0 + R - fx, fx - (B1 - R)), 0), qy = fmaxf(fmaxf(B0 + R - fy, fy - (B1 - R)), 0);
    float d = sqrtf(qx * qx + qy * qy) - R; float a = d <= 0 ? 0 : expf(-(d * d) / (2 * 13.f * 13.f)) * (1 - fminf(1, d / 40)); unsigned char *p = &px[(y * N + x) * 4]; p[0] = p[1] = p[2] = 255; p[3] = (unsigned char)(a * 255 + 0.5f); }
  t = gfx_tex_create(px, N, N, 4, 1); return t; }
/* a soft round light, smooth to its edge (a gaussian falloff at 128 px, filtered when stretched) */
static GLuint glow_tex(void) { static GLuint t; if (t) return t; enum { N = 128 }; static unsigned char px[N * N];
  for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) { float dx = (x + .5f) / N * 2 - 1, dy = (y + .5f) / N * 2 - 1, d2 = dx * dx + dy * dy; float a = d2 < 1 ? expf(-d2 * 3.2f) * (1 - d2) : 0; px[y * N + x] = (unsigned char)(a * 255 + 0.5f); }
  t = gfx_tex_create(px, N, N, 1, 1); return t; }
void lpk_draw(const char *id, const char *logo, float x, float y, float W, float H, float R, float a, uint32_t top, uint32_t bot) {
  if (a <= 0.003f) return; double now = app_now(); boxR = R; /* (handed to the player: its light and shadow stay in the page, the player's window takes the box's place, see below) */
  /* the box stays black while a channel's picture is on its way (the last one faded into it, the next fades out of
     it); the channel's logo and colours come only when it is taking long (1.2 s) or nothing plays */
  float mine0v = K.own || K.out ? K.a.v : 0; /* (the picture in the box as it was) */
  static char dId[SB_ID], dLogo[512], pLogo[512]; static double dAt; static float lks, pVis; if (id && strcmp(dId, id)) { pVis = 1 - (mine0v); /* (how much of the last logo showed: none under a picture) */ snprintf(dId, sizeof dId, "%s", id); snprintf(pLogo, sizeof pLogo, "%s", dLogo); snprintf(dLogo, sizeof dLogo, "%s", logo ? logo : ""); dAt = now; }
  if (logo && *logo && !dLogo[0]) snprintf(dLogo, sizeof dLogo, "%s", logo);
  int mine = id && !strcmp(id, K.id); float v = K.handed ? 1 : K.own || K.out ? K.a.v : 0; /* (the picture in the box whatever is focused: on the very frame of a move it stays, then fades) */
#ifndef SB_WEBOS
  { extern int video_mac_real(void); if (!video_mac_real()) v = 0; } /* (the Mac preview's simulated playback has no picture: the logo stays) */
#endif
  { /* the light round it, as a TV's backlight follows its picture: the picture's own edge colours where they can be
       seen (the Mac preview), the channel's logo colours on a TV (its plane cannot be read); eased as they change */
    static float cur[4][3]; static int seeded; uint32_t e[4]; int have = v > 0.05f && video_edge_colors(e);
    if (!have) { static char pu[512]; static uint32_t pc[2]; static int pok; if (logo && strcmp(pu, logo)) { snprintf(pu, sizeof pu, "%s", logo); pok = 0; }
      if (!pok && logo && *logo) { Palette pp; int r = img_palette(logo, 64, 64, 1, &pp); if (r) { pok = 1; const Swatch *sv = pp.vibrant.ok ? &pp.vibrant : pp.muted.ok ? &pp.muted : NULL, *sd = pp.dark.ok ? &pp.dark : pp.darkMuted.ok ? &pp.darkMuted : sv;
          pc[0] = sv ? hsl2rgb(sv->h, fminf(sv->s, 0.8f), 0.45f) : RGB(90, 80, 140); pc[1] = sd ? hsl2rgb(sd->h, fminf(sd->s, 0.8f), 0.32f) : pc[0]; } }
      e[0] = e[1] = pok ? pc[0] : RGB(90, 80, 140); e[2] = e[3] = pok ? pc[1] : RGB(60, 52, 100); }
    float sp = seeded ? fminf(1, app_dt() * 3.5f) : 1; seeded = 1;
    for (int i = 0; i < 4; i++) { float t[3] = { (float)((e[i] >> 16) & 255), (float)((e[i] >> 8) & 255), (float)(e[i] & 255) }; for (int c = 0; c < 3; c++) cur[i][c] += (t[c] - cur[i][c]) * sp; }
    float gk = a * v * (have ? 1.0f : 0.45f), cx = x + W / 2, cy = y + H / 2; /* (only with the picture up: never a logo's colour on trust) */
    if (gk > 0.003f) { GLuint gt = halo_tex(); float avg[3]; for (int c = 0; c < 3; c++) avg[c] = (cur[0][c] + cur[1][c] + cur[2][c] + cur[3][c]) / 4;
      /* the light round it: one soft halo of the box's own shape in the picture's colour (a picture of it, its corners
         and edges stretched apart: smooth everywhere), and each edge's colour a touch where it is */
      const float P = 54, TB = 40, TC = 62, sc = P / TB, cs = TC * sc; uint32_t hc = RGBA((int)avg[0], (int)avg[1], (int)avg[2], (int)(255 * 0.42f * gk));
      float X0 = x - P, Y0 = y - P, X1 = x + W + P, Y1 = y + H + P, u0 = 0, u1 = TC / 128.f, u2 = 1 - TC / 128.f;
      gfx_image(gt, X0, Y0, cs, cs, 0, hc, u0, u0, u1, u1); gfx_image(gt, X1 - cs, Y0, cs, cs, 0, hc, u2, u0, 1, u1); gfx_image(gt, X0, Y1 - cs, cs, cs, 0, hc, u0, u2, u1, 1); gfx_image(gt, X1 - cs, Y1 - cs, cs, cs, 0, hc, u2, u2, 1, 1);
      gfx_image(gt, X0 + cs, Y0, X1 - X0 - 2 * cs, cs, 0, hc, u1, u0, u2, u1); gfx_image(gt, X0 + cs, Y1 - cs, X1 - X0 - 2 * cs, cs, 0, hc, u1, u2, u2, 1);
      gfx_image(gt, X0, Y0 + cs, cs, Y1 - Y0 - 2 * cs, 0, hc, u0, u1, u1, u2); gfx_image(gt, X1 - cs, Y0 + cs, cs, Y1 - Y0 - 2 * cs, 0, hc, u2, u1, 1, u2);
      GLuint st = glow_tex(); float ew = W * 0.10f, eh = H * 0.18f;
      #define GA(v, k) RGBA((int)(v)[0], (int)(v)[1], (int)(v)[2], (int)(255 * (k) * gk))
      gfx_alpha_tex(st, x - ew, y + H * 0.15f, ew * 2, H * 0.7f, GA(cur[0], 0.16f)); gfx_alpha_tex(st, x + W - ew, y + H * 0.15f, ew * 2, H * 0.7f, GA(cur[2], 0.16f));
      gfx_alpha_tex(st, x + W * 0.15f, y - eh, W * 0.7f, eh * 2, GA(cur[1], 0.14f)); gfx_alpha_tex(st, x + W * 0.15f, y + H - eh, W * 0.7f, eh * 2, GA(cur[3], 0.14f));
      #undef GA
      (void)cx; (void)cy; }
  }
  gfx_shadow(x, y + 14, W, H, R, 40, BLACK_A((int)(150 * a)));
  if (K.handed) return; /* (handed to the player: its window, its corners and its ring are the player's now) */
  lks = 1; /* the logo lies under the picture always: a picture fading out shows it, one fading in covers it (a crossfade, never a dip) */
  float xf = (float)((now - dAt) / 0.35); xf = xf < 0 ? 0 : xf > 1 ? 1 : xf; xf = xf * xf * (3 - 2 * xf); /* a move: the last channel's logo fades out as this one's fades in */
  gfx_rrect(x, y, W, H, R, RGBA(8, 8, 11, (int)(255 * a)));
  gfx_rrect_v(x, y, W, H, R, col_alpha(top | 0xff000000u, a), col_alpha(bot | 0xff000000u, a));
  if (lks > 0.003f || pVis * (1 - xf) > 0.003f) for (int w = 0; w < 2; w++) { /* no picture up: the channel's logo, breathing softly (the last one going as it comes) */
    const char *lu = w ? dLogo : pLogo; float lka = w ? xf * lks : (1 - xf) * pVis; if (lka <= 0.003f || !lu[0]) continue; /* the last one only as it was seen, this one as the picture lets it */
    float asp = 0; GLuint t = img_get(lu, 360, 200, &asp); int loading = !(mine && K.failed) && !(id && live_reach(id) < 0);
    float ph = (float)(now * 6.2832 / 2.2), pu = loading ? 0.5f - 0.5f * cosf(ph) : 0.5f, sc = loading ? 0.985f + 0.03f * pu : 1; /* loading: a slow soft breath, its light and size rising and falling */
    if (t && asp > 0) { float lw = W * 0.40f, lh = lw / asp; if (lh > H * 0.36f) { lh = H * 0.36f; lw = lh * asp; } float lcx = x + W / 2, lcy = y + H / 2 + 6 * (1 - lka) * (w ? 1 : -1);
      gfx_scale_push(sc, sc, lcx, lcy); gfx_image(t, lcx - lw / 2, lcy - lh / 2, lw, lh, 0, WHITE_A((int)(255 * a * lka * (loading ? 0.5f + 0.4f * pu : 0.75f))), 0, 0, 1, 1); gfx_scale_pop(); } }
  { static float ok; int off = id && (live_reach(id) < 0 || (mine && K.failed)); ok += ((off ? 1.f : 0.f) - ok) * fminf(1, app_dt() * 6); /* off air: said in the box too, under the logo */
    if (ok > 0.003f) { gfx_opacity_push(a * ok); const float is = 40; float tw = text_width(T("Off air"), 24, 700, 0.3f), tot = is + 12 + tw, ox = x + W / 2 - tot / 2, oy = y + H * 0.78f;
      uint32_t g = WHITE_A(170); icon_draw("wn_live", ox, oy - is / 2, is, g);
      for (int i = 0; i <= 14; i++) { float t = i / 14.f; gfx_circle(ox + 5 + (is - 10) * t, oy + is / 2 - 5 - (is - 10) * t, 3.6f, RGB(12, 11, 17)); }
      for (int i = 0; i <= 14; i++) { float t = i / 14.f; gfx_circle(ox + 5 + (is - 10) * t, oy + is / 2 - 5 - (is - 10) * t, 1.7f, g); }
      text_draw(T("Off air"), ox + is + 12, oy - text_line_height(24) / 2, 24, 700, WHITE_A(200), 0.3f, 0, ALIGN_LEFT); gfx_opacity_pop(); } }
  if (v > 0.003f) gfx_hole(x, y, W, H, R, v); /* the video plane shows through, under the window (the Mac preview: its picture, drawn there) */
  gfx_ring(x, y, W, H, R, 1.5f, WHITE_A((int)(34 * a)));
}
