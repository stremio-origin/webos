#include "ui/xline.h"
#include "screens/livehub.h"
#include "core/app.h"
#include "core/keys.h"
#include "core/i18n.h"
#include "core/live.h"
#include "ui/livewall.h"
#include "ui/shared.h"
#include "screens/player.h"
#include "screens/streams.h"
#include "screens/livepeek.h"
#include "core/video.h"
#include "core/hit.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include "ui/img.h"
#include "ui/widgets.h"
#include "ui/glass.h"
#include "ui/sound.h"
#include "ui/palette.h"
#include "ui/icon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>

#define MAXTAB 70
#define COLS 6
#define TW 252.0f
#define TH 142.0f
#define TGAP 26.0f
#define PITCH (TH + 26 + 30 + 22)       /* a tile, its name, the gap */
#define GRID_TOP 650.0f
#define PX 1048.0f                      /* the preview */
#define PY 76.0f
#define PW 776.0f
#define PH 436.5f

typedef struct { char name[96]; const LiveCh **p; LiveCh *own; int n; } Tab; /* p: the tab's channels (pointing at the catalogs', or at own: the pinned and recent ones) */
static struct {
  int open, inPlayer, zone; /* zone 0 the grid, 1 the tabs */ Tab tab[MAXTAB]; int ntab, cur, idx; double focusAt, openAt; int tunedFor; char playing[SB_ID];
  int liveN; Tween a, gridY, tabX, ring; Timer1 swap; double introAt; /* introAt: the switch-on, opened from Home */ int okDown, holdFired; double okAt; /* over the picture: OK held on a channel (the player's keys come here) */ int ptrHold; float tabPx[MAXTAB], tabW[MAXTAB]; int partial; Tween selX, selW; int selSet; int leaving; double leaveAt; /* leaving: a channel picked from Home, the hub stays until the player's art covers it */ /* sel: the chosen tab's pill, gliding */
} H;
/* each tile's colours, from its logo (once it is in) */
typedef struct { char url[512]; uint32_t c1, c2; int ok; double at; } Tint; /* at: when its colours were found (they fade in from then) */
static Tint tints[256];
static void tint_of(const char *url, uint32_t *c1, uint32_t *c2) {
  *c1 = RGB(42, 36, 62); *c2 = RGB(16, 14, 24); if (!url || !*url) return;
  unsigned h = 2166136261u; for (const char *p = url; *p; p++) h = (h ^ (unsigned char)*p) * 16777619u; Tint *t = &tints[h & 255];
  if (t->ok && !strcmp(t->url, url)) { *c1 = t->c1; *c2 = t->c2; return; }
  Palette p; int r = img_palette(url, 64, 64, 1, &p); if (r == 0) return;
  snprintf(t->url, sizeof t->url, "%s", url); t->ok = 1; t->at = app_now();
  if (r < 0) { t->c1 = *c1; t->c2 = *c2; return; }
  const Swatch *v = p.vibrant.ok ? &p.vibrant : p.muted.ok ? &p.muted : p.light.ok ? &p.light : NULL, *d = p.darkMuted.ok ? &p.darkMuted : p.dark.ok ? &p.dark : v;
  if (v) t->c1 = hsl2rgb(v->h, fminf(v->s, 0.62f), 0.30f); if (d) t->c2 = hsl2rgb(d->h, fminf(d->s, 0.5f), 0.11f);
  *c1 = t->c1; *c2 = t->c2;
}
/* how far a logo's colours have faded in (0..1 over 0.34 s from when they were found): the tile's colour comes with
   its logo, never a moment later */
static float tint_k(const char *url) { if (!url || !*url) return 1; unsigned h = 2166136261u; for (const char *p = url; *p; p++) h = (h ^ (unsigned char)*p) * 16777619u; Tint *t = &tints[h & 255];
  if (!t->ok || strcmp(t->url, url)) return 0; float k = (float)((app_now() - t->at) / 0.34); return k >= 1 ? 1 : k <= 0 ? 0 : k * k * (3 - 2 * k); }
/* a channel with no logo: a colour of its own from its name (light: how bright) */
static uint32_t name_hue(const char *n, float light) { unsigned h = 2166136261u; for (const char *p = n; p && *p; p++) h = (h ^ (unsigned char)*p) * 16777619u;
  static const uint32_t P[8] = { 0xff5b3f8cu, 0xff2f5d8au, 0xff2c7a6bu, 0xff8a4b2fu, 0xff7a2f55u, 0xff3f6f3au, 0xff6b3f8au, 0xff8a6b2fu }; return col_mix(P[h & 7] | 0xff000000u, 0xff0c0b11u, 1 - light); }
static const char *art_of(const LiveCh *it) { return it->logo[0] ? it->logo : it->poster; }

/* ---- the tabs ---- */
static void tab_free(Tab *t) { free(t->p); free(t->own); memset(t, 0, sizeof *t); }
static Tab *tab_new(const char *name, int n) { if (H.ntab >= MAXTAB || n <= 0) return NULL; Tab *t = &H.tab[H.ntab]; memset(t, 0, sizeof *t); t->p = malloc(sizeof(LiveCh *) * (size_t)n); if (!t->p) return NULL; snprintf(t->name, sizeof t->name, "%s", name); H.ntab++; return t; }
static void tab_items(const char *name, const ItemList *l) { /* the pinned or recent ones: their own small copies */
  Tab *t = tab_new(name, l->n); if (!t) return; t->own = malloc(sizeof(LiveCh) * (size_t)l->n); if (!t->own) { H.ntab--; free(t->p); return; }
  for (int i = 0; i < l->n; i++) { LiveCh *c = &t->own[i]; memset(c, 0, sizeof *c); snprintf(c->id, sizeof c->id, "%s", l->items[i].id); snprintf(c->type, sizeof c->type, "%s", l->items[i].type); snprintf(c->name, sizeof c->name, "%s", l->items[i].name);
    snprintf(c->logo, sizeof c->logo, "%s", l->items[i].logo); snprintf(c->poster, sizeof c->poster, "%s", l->items[i].poster[0] ? l->items[i].poster : l->items[i].background); t->p[t->n++] = c; } }
static int cmp_name(const void *a, const void *b) { return strcasecmp((*(const LiveCh *const *)a)->name, (*(const LiveCh *const *)b)->name); }
static const char *short_name(const char *t, char *buf, size_t cap) { /* "TvVoo • Italia · tv" -> "TvVoo • Italia" */
  snprintf(buf, cap, "%s", t); char *c = strstr(buf, " · "); if (c) *c = 0; return buf; }
static void build(void) {
  char keepTab[96] = "", keepId[SB_ID] = ""; if (H.ntab && H.cur < H.ntab) { snprintf(keepTab, sizeof keepTab, "%s", H.tab[H.cur].name); if (H.idx < H.tab[H.cur].n) snprintf(keepId, sizeof keepId, "%s", H.tab[H.cur].p[H.idx]->id); }
  for (int i = 0; i < H.ntab; i++) tab_free(&H.tab[i]); H.ntab = 0;
  int tot = 0; for (int c = 0; c < live_catalogs(); c++) { int n = live_catalog(c, NULL, NULL); if (n > 0) tot += n; }
  Tab *all = tab_new(T("All channels"), tot); /* every catalog's channels, catalog after catalog (pointers: nothing copied) */
  if (all) for (int c = 0; c < live_catalogs(); c++) { const LiveCh *ch; int n = live_catalog(c, NULL, &ch); for (int i = 0; i < n; i++) all->p[all->n++] = &ch[i]; }
  int allAt = all ? H.ntab - 1 : -1;
  ItemList p; if (live_pins(&p)) tab_items(T("Favorites"), &p); itemlist_clear(&p);
  ItemList r; if (live_recent(&r)) tab_items(T("Recent"), &r); itemlist_clear(&r);
  for (int c = 0; c < live_catalogs(); c++) { const char *tt; const LiveCh *ch; char b[96]; int n = live_catalog(c, &tt, &ch); if (n <= 0) continue; Tab *t = tab_new(short_name(tt, b, sizeof b), n); if (t) for (int i = 0; i < n; i++) t->p[t->n++] = &ch[i]; }
  if (allAt >= 0 && H.tab[allAt].n > 1) { Tab *az = tab_new("A–Z", H.tab[allAt].n); if (az) { memcpy(az->p, H.tab[allAt].p, sizeof(LiveCh *) * (size_t)H.tab[allAt].n); az->n = H.tab[allAt].n; qsort(az->p, (size_t)az->n, sizeof(LiveCh *), cmp_name); } }
  H.liveN = tot;
  H.cur = 0; H.idx = 0; for (int i = 0; i < H.ntab; i++) if (keepTab[0] && !strcmp(H.tab[i].name, keepTab)) H.cur = i;
  if (keepId[0] && H.cur < H.ntab) for (int i = 0; i < H.tab[H.cur].n; i++) if (!strcmp(H.tab[H.cur].p[i]->id, keepId)) { H.idx = i; break; }
  if (!keepId[0] && H.playing[0] && H.cur < H.ntab) for (int i = 0; i < H.tab[H.cur].n; i++) if (!strcmp(H.tab[H.cur].p[i]->id, H.playing)) { H.idx = i; break; } /* opened over a channel: on it */
}
static const Tab *cur_tab(void) { return H.cur < H.ntab ? &H.tab[H.cur] : NULL; }
static const LiveCh *focused(void) { const Tab *t = cur_tab(); return t && H.idx < t->n ? t->p[H.idx] : NULL; }

/* ---- open / close ---- */
static Screen over = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
static const Item *over_focused(void) { static Item one; const LiveCh *f = focused(); if (!H.open || H.leaving || H.zone != 0 || !f) return NULL; live_item(f, &one); return &one; } /* held: the channel's menu */
static void watch(void);
static void over_open(const Item *it) { (void)it; if (H.open && !H.leaving) watch(); } /* a tap: watch it */
/* the keys while it is Home's page. Closed (or turned into the one over a channel and closed there), it is no page
   any more: it lets go of the keys and this one goes to Home, never swallowed by a page that draws nothing */
static int over_key(int k) {
  if (!H.open && !H.leaving) { if (app_overlay == &over) app_overlay = NULL; return 1; } /* (the safety in hub_over_tick lets go a frame after it closes: this is only a key in that frame) */
  if (H.leaving) return 1; if (!hub_key(k) && k == K_BACK) hub_close(); return 1; }
static void preview_place(int on) {
  if (!H.inPlayer || !H.partial) return;
  if (on) video_set_window(PX, PY, PW, PH);
  else { extern void player_reapply_window(void); player_reapply_window(); }
}
void hub_open(int inPlayer, const char *playingId) {
  H.leaving = 0; live_refresh(0); memset(&H.tabPx, 0, sizeof H.tabPx); H.open = 1; H.inPlayer = inPlayer; H.zone = 0; H.ptrHold = 0; H.openAt = app_now(); H.tunedFor = -1;
  snprintf(H.playing, sizeof H.playing, "%s", playingId ? playingId : ""); build(); H.focusAt = app_now();
  tween_set(&H.a, 0); tween_to(&H.a, 1, 0.32f, EASE_CSS_EASEOUT); tween_set(&H.gridY, 0); tween_set(&H.tabX, 0); timer_start(&H.swap, 0.6f);
  H.partial = 0; H.selSet = 0; /* (over a channel: its picture stays full screen behind the hub) */ H.introAt = inPlayer ? 0 : app_now();
  if (!inPlayer) { over.key = over_key; over.focused = over_focused; over.open = over_open; extern void hub_over_tick(float); extern void hub_over_draw(void); over.tick = hub_over_tick; over.draw = hub_over_draw; app_overlay = &over; }
  if (!inPlayer) app_pace_again(); /* (the TV's rate may have changed with the last video) */
  sound_play(SND_TOGGLE); fprintf(stderr, "[hub] open (%s): %d tabs, %d channels\n", inPlayer ? "over a channel" : "from Home", H.ntab, H.ntab ? H.tab[0].n : 0);
}
void hub_close(void) {
  if (!H.open) return; H.open = 0; preview_place(0); tween_to(&H.a, 0, 0.34f, EASE_CSS_EASE); /* (it sinks a little as it fades: hub_draw_body) */
  if (app_overlay == &over) app_overlay = NULL; /* (whichever way it was opened last: the one from Home may have become the one over a channel) */
}
int hub_on(void) { return H.open; }
/* opened from Home and fully in: it is opaque from edge to edge, so the page under it is not drawn at all (on these
   TVs the chip that draws is the limit: the page under it doubled every frame's cost for nothing) */
int hub_covers_page(void) { return H.open && !H.inPlayer && app_overlay == &over && H.a.v >= 0.999f; }
/* the frame's clear: covering it all, its base colour (a clear is free on these chips; a full-screen fill is not) */
static int baseCleared; int gfx_in_target(void);
uint32_t hub_frame_clear(uint32_t page, int live) { if (!live && hub_covers_page() && lpk_full_a() >= 0.999f) { baseCleared = 0; return RGBA(0, 0, 0, 0); } /* (the channel full screen behind it: see-through) */
  baseCleared = !live && hub_covers_page(); return baseCleared ? RGB(10, 9, 15) : page; } /* (every frame: a film playing under it is cleared see-through, and its base is drawn then) */
int hub_player_mode(void) { return H.inPlayer; }
int hub_back(void) { if (H.open && H.zone == 0 && H.ntab) { H.zone = 1; H.okDown = 0; sound_focus(); return 1; } return 0; }
static void open_streams(void) { const LiveCh *f = focused(); if (!f) return; Item it; live_item(f, &it); streams_show_over_player(it.type, it.id, it.name); } /* (held over the picture: its streams' sheet over it all, the picture playing on) */
void hub_ok_up(void) { if (!H.okDown) return; H.okDown = 0; if (!H.holdFired && H.open && H.zone == 0) watch(); H.holdFired = 0; }
void hub_playing(const char *id) { snprintf(H.playing, sizeof H.playing, "%s", id ? id : ""); }
void hub_over_tick(float dt) { hub_tick(dt); if (!H.open && !H.leaving && H.a.v <= 0.004f && app_overlay == &over) { app_overlay = NULL; fprintf(stderr, "[hub] closed elsewhere: the keys back to the page\n"); } } /* (a safety: never a page that holds the keys and draws nothing) */
void hub_over_draw(void) { hub_draw(); }

/* ---- keys ---- */
static void focus_moved(void) { H.focusAt = app_now(); H.tunedFor = -1; H.ptrHold = 0; }
static void watch(void) {
  const Tab *t = cur_tab(); const LiveCh *it = focused(); if (!it || !t) return;
  if (H.inPlayer && !strcmp(it->id, H.playing) && live_card()->tuning == 0) { hub_close(); return; } /* (it is the one playing: full screen) */
  if (live_reach(it->id) < 0) { live_start_list(t->p, t->n, H.idx, H.inPlayer); return; } /* off air: said at once, the hub stays (OK again tries it) */
  live_start_list(t->p, t->n, H.idx, H.inPlayer);
  if (H.inPlayer) hub_close(); else { H.leaving = 1; H.leaveAt = app_now(); } /* (from Home: the loader comes over it, its logo flying from the tile) */
}
int hub_key(int k) {
  if (!H.open) return 0; const Tab *tb = cur_tab(); int n = tb ? tb->n : 0;
  if (k == K_BACK && H.zone == 0 && H.ntab) { H.zone = 1; H.okDown = 0; sound_focus(); return 1; } /* Back on the channels: up to the catalogs first */
  if (k == K_BACK || (k == K_YELLOW && H.inPlayer)) { hub_close(); return 1; } /* (on the catalogs: away; the yellow key brought it over the picture: it takes it away too) */
  if (k == K_GREEN) { const LiveCh *f = focused(); if (f) { Item one; live_item(f, &one); const Item *it = &one; int on = !live_pinned(it->id); live_pin(it, on); toast(T(on ? "Added to favorites" : "Removed from favorites"), 1600, NULL); sound_play(SND_TOGGLE); build(); } return 1; }
  if (H.zone == 1) { /* the tabs */
    if (k == K_LEFT || k == K_RIGHT) { int d = app_mirror_key(k) == K_RIGHT ? 1 : -1; if (H.cur + d >= 0 && H.cur + d < H.ntab) { H.cur += d; H.idx = 0; tween_set(&H.gridY, 0); timer_start(&H.swap, 0.6f); focus_moved(); sound_focus(); } return 1; }
    if (k == K_DOWN || k == K_OK) { if (n) { H.zone = 0; focus_moved(); } return 1; }
    return 1; }
  int i0 = H.idx;
  if (k == K_RIGHT) { if ((H.idx % COLS) < COLS - 1 && H.idx + 1 < n) H.idx++; }
  else if (k == K_LEFT) { if (H.idx % COLS) H.idx--; }
  else if (k == K_DOWN) { if (H.idx + COLS < n) H.idx += COLS; else if (H.idx / COLS < (n - 1) / COLS) H.idx = n - 1; }
  else if (k == K_UP) { if (H.idx >= COLS) H.idx -= COLS; else { H.zone = 1; sound_focus(); return 1; } }
  else if (k == K_OK) { if (H.inPlayer) { if (!H.okDown) { H.okDown = 1; H.holdFired = 0; H.okAt = app_now(); } } else watch(); return 1; } /* (over the picture: a tap or a hold, decided as OK is let go) */
  else if (k == K_CH_UP || k == K_CH_DOWN) return 0; /* (the player switches) */
  else return 1;
  if (H.idx != i0) { focus_moved(); sound_focus(); }
  return 1;
}
static void tile_focus(int i, int b) { (void)b; if (!H.open) return; if (H.zone != 0 || H.idx != i) { H.zone = 0; H.idx = i; H.focusAt = app_now(); H.tunedFor = -1; H.ptrHold = 1; } }
static void tab_pick(int i, int b) { (void)b; if (!H.open || i >= H.ntab) return; if (H.cur != i) { H.cur = i; H.idx = 0; tween_set(&H.gridY, 0); timer_start(&H.swap, 0.6f); } H.zone = 1; }

/* ---- tick ---- */
void hub_tick(float dt) {
  tween_tick(&H.a, dt); tween_tick(&H.gridY, dt); tween_tick(&H.tabX, dt); timer_tick(&H.swap, dt);
  if (!H.open) return;
  if (H.okDown && !H.holdFired && H.open && app_hold_k(H.okAt) >= 1) { H.holdFired = 1; H.okDown = 0; open_streams(); return; }
  if (H.leaving && lpk_can_return(H.tab[H.cur].n > H.idx ? H.tab[H.cur].p[H.idx]->id : "")) H.leaving = 0; /* grown from its preview: the hub stays under it, its box to come back to */
  if (H.leaving && player_open() && (player_art_full() || app_now() - H.leaveAt > 3)) { H.leaving = 0; } /* covered by the player: it stays under it, so Back from the channel comes back here */
  else if (H.leaving && !player_open() && app_now() - H.leaveAt > 0.4) { H.leaving = 0; H.a.v = 0; tween_set(&H.a, 0); hub_close(); return; } /* (the channel did not open: gone, as before) */
  { int tot = 0; for (int c = 0; c < live_catalogs(); c++) { int n = live_catalog(c, NULL, NULL); if (n > 0) tot += n; } if (tot != H.liveN) build(); } /* the catalogs came in */
  int row = H.idx / COLS; if (!H.ptrHold) tween_to(&H.gridY, -row * PITCH, 0.28f, EASE_ROW); /* the focused row right under the pills (the one above gone, never half under them) */
  { const LiveCh *f = focused(); if (H.inPlayer && f && H.zone == 0 && strcmp(f->id, H.playing) && !live_reach(f->id) && app_now() - H.focusAt > 0.6 && !live_card()->tuning) { Item it; live_item(f, &it); live_peek(&it); } }
  /* (over a channel: OK switches to another; from Home: the preview box plays the focused one, see hub_draw_body) */
}

/* ---- drawing ---- */
static GLuint glow_tex(void) { static GLuint t; if (t) return t; enum { N = 64 }; static unsigned char px[N * N];
  for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) { float dx = (x + .5f) / N * 2 - 1, dy = (y + .5f) / N * 2 - 1, d = sqrtf(dx * dx + dy * dy); float a = d < 1 ? (1 - d) * (1 - d) : 0; px[y * N + x] = (unsigned char)(a * 255); }
  t = gfx_tex_create(px, N, N, 1, 1); return t; }
static float fit_logo(const char *url, float mw, float mh, float *w, float *h, GLuint *t) {
  float asp; *t = url && *url ? img_get(url, (int)mw, (int)mh, &asp) : 0; if (!*t || asp <= 0) return 0;
  *w = mw; *h = mw / asp; if (*h > mh) { *h = mh; *w = mh * asp; } return 1; }
static void live_dot(float x, float y, float r) { float pu = 0.5f + 0.5f * sinf((float)app_now() * 3.2f); gfx_circle(x, y, r + 5 * pu, RGBA(0xe5, 0x2b, 0x2b, (int)(70 * (1 - pu)))); gfx_circle(x, y, r, RGB(0xef, 0x3b, 0x3b)); }
static void eq_bars(float x, float y, float h, uint32_t c) { double t = app_now(); for (int i = 0; i < 3; i++) { float k = 0.35f + 0.65f * (0.5f + 0.5f * sinf((float)t * (5.5f + i * 1.7f) + i)); gfx_rrect(x + i * 7, y + h - h * k, 4, h * k, 2, c); } }
static void tile(const LiveCh *it, float x, float y, int f, float k, int playing) {
  const char *art = art_of(it);
  float s = 1 + 0.07f * k, cx = x + TW / 2, cy = y + TH / 2; gfx_scale_push(s, s, cx, cy);
  if (k > 0.01f) gfx_shadow(x, y + 12, TW, TH, 18, 34, BLACK_A((int)(150 * k)));
  /* its face: the logo on its own colours, the two fading in together; while the logo comes, the app's waiting
     shimmer with the name soft on it; no logo (or one that would not load): a colour of its own from its name */
  int has = art && *art, failed = has && img_failed(art); float lw, lh; GLuint t = 0; int got = has && !failed && fit_logo(art, TW * 0.62f, TH * 0.56f, &lw, &lh, &t);
  if (got) { uint32_t c1, c2; tint_of(art, &c1, &c2); float fa = fminf(gfx_tex_fade(t), tint_k(art)); if (fa < 1) ph_box(x, y, TW, TH, 18);
    gfx_opacity_push(fa); gfx_rrect_v(x, y, TW, TH, 18, c1, c2); gfx_rrect_v(x, y, TW, TH * 0.5f, 18, WHITE_A(18), WHITE_A(0)); /* a soft light from above */
    if (f) { char sk[80]; snprintf(sk, sizeof sk, "logo:%s", it->id); sx_image(sk, t, cx - lw / 2, cy - lh / 2, lw, lh, 0, WHITE_A(255), 0, 0, 1, 1); } /* (picked: it flies into the loader) */
    else gfx_image(t, cx - lw / 2, cy - lh / 2, lw, lh, 0, WHITE_A(255), 0, 0, 1, 1);
    gfx_opacity_pop(); }
  else if (has && !failed) { ph_box(x, y, TW, TH, 18); text_draw(it->name, cx, cy - text_line_height(24) / 2, 24, 700, WHITE_A(110), 0, TW - 24, ALIGN_CENTER); } /* (coming) */
  else { gfx_rrect_v(x, y, TW, TH, 18, name_hue(it->name, 0.62f), name_hue(it->name, 0.28f)); gfx_rrect_v(x, y, TW, TH * 0.5f, 18, WHITE_A(20), WHITE_A(0)); gfx_ring(x, y, TW, TH, 18, 1, WHITE_A(22));
    text_draw(it->name, cx, cy - text_line_height(24) / 2, 24, 800, WHITE_A(240), 0, TW - 24, ALIGN_CENTER); }
  { int st = live_reach(it->id); if (playing) st = 1; /* its dot only once tried: red and breathing, or struck through, fading in as it comes */
    static struct { char id[SB_ID]; int st; double at; } MK[256]; unsigned hh = 2166136261u; for (const char *p = it->id; *p; p++) hh = (hh ^ (unsigned char)*p) * 16777619u;
    int mi = hh & 255; if (strcmp(MK[mi].id, it->id) || MK[mi].st != st) { snprintf(MK[mi].id, sizeof MK[mi].id, "%s", it->id); MK[mi].st = st; MK[mi].at = app_now(); }
    float mk = (float)((app_now() - MK[mi].at) / 0.35); mk = mk < 0 ? 0 : mk > 1 ? 1 : mk; mk = mk * mk * (3 - 2 * mk); gfx_opacity_push(mk);
    if (st > 0) live_dot(x + 18, y + 18, 5);
    else if (st < 0) { gfx_circle(x + 18, y + 18, 9, RGBA(12, 11, 17, 170)); gfx_ring(x + 18 - 6, y + 18 - 6, 12, 12, 6, 1.6f, WHITE_A(150)); for (int q = 0; q <= 6; q++) { float t = q / 6.f; gfx_circle(x + 18 - 5 + 10 * t, y + 18 + 5 - 10 * t, 1.0f, WHITE_A(170)); } }
    gfx_opacity_pop(); }
  if (live_pinned(it->id)) { gfx_circle(x + TW - 22, y + 22, 15, RGBA(12, 11, 17, 190)); icon_draw("star", x + TW - 32, y + 12, 20, RGB(0xf5, 0xc5, 0x18)); } /* a favourite: its star */
  if (playing) { gfx_rrect(x + TW - 46, y + TH - 34, 34, 24, 8, RGBA(12, 11, 17, 190)); eq_bars(x + TW - 40, y + TH - 30, 16, ACC2); }
  if (f) { gfx_ring_out(x, y, TW, TH, 18, 3, ACC2); float hp = H.inPlayer ? (H.okDown && !H.holdFired ? app_hold_k(H.okAt) : 0) : app_hold_progress(); if (hp > 0) hold_trace_draw(x, y, TW, TH, 18, ease_apply(EASE_IN_OUT, hp)); } /* (held: the line runs round it, as on Home) */
  gfx_scale_pop();
  text_draw(it->name, x + 4, y + TH + 14 + 8 * k, 23, f ? 700 : 600, f ? WHITE_A(255) : WHITE_A(190), 0, TW - 8, ALIGN_LEFT);
}
static void hub_draw_body(void);
/* where "Live TV" was: the focused channel's state (live_mark) */
static void reach_mark(float x, float y, const char *id) {
  static float k3[3]; static char kid[SB_ID]; if (id && strcmp(kid, id)) { snprintf(kid, sizeof kid, "%s", id); k3[0] = k3[1] = k3[2] = 0; } /* (from nothing for each channel) */
  int st = id ? live_reach(id) : 0; if (id && (H.inPlayer ? !strcmp(id, H.playing) : !strcmp(id, lpk_full_id()))) st = 1;
  live_mark(x, y, 1, 0, k3, st);
}
/* opened from Home: a channel switching on, a bright line across the middle, then it opens up and the hub is in it */
#define IN_LINE 0.2
#define IN_OPEN 0.3
void hub_draw(void) {
  hub_draw_body(); return; /* (no entrance: it comes in with its own fade) */
  double t = H.introAt > 0 ? app_now() - H.introAt : 1e9; if (t >= IN_LINE + IN_OPEN || !H.open) { hub_draw_body(); return; }
  float k; gfx_rect(0, 0, SB_W, SB_H, RGB(4, 4, 7));
  if (t < IN_LINE) { k = (float)(t / IN_LINE); k = 1 - (1 - k) * (1 - k) * (1 - k); float w = SB_W * k, cy = SB_H / 2;
    gfx_rrect_h(SB_W / 2 - w / 2, cy - 14, w / 2, 28, 0, col_alpha(ACC2 | 0xff000000u, 0), col_alpha(ACC2 | 0xff000000u, 0.35f)); gfx_rrect_h(SB_W / 2, cy - 14, w / 2, 28, 0, col_alpha(ACC2 | 0xff000000u, 0.35f), col_alpha(ACC2 | 0xff000000u, 0)); /* its glow */
    gfx_rect(SB_W / 2 - w / 2, cy - 1.5f, w, 3, WHITE_A(240)); return; }
  k = (float)((t - IN_LINE) / IN_OPEN); k = k * k * (3 - 2 * k); float h = SB_H * k, y0 = SB_H / 2 - h / 2;
  gfx_clip_push(0, y0, SB_W, h); hub_draw_body(); gfx_clip_pop();
  gfx_rect(0, y0 - 1.5f, SB_W, 3, WHITE_A((int)(230 * (1 - k)))); gfx_rect(0, y0 + h - 1.5f, SB_W, 3, WHITE_A((int)(230 * (1 - k)))); /* its edges, fading as it opens */
}
/* the entrance: each part fades up and rises in its turn after the open (0 .. 1) */
static float in_k(double delay, double dur) { double t = (app_now() - H.openAt - delay) / dur; if (t <= 0) return 0; if (t >= 1) return 1; float k = (float)t; return 1 - (1 - k) * (1 - k) * (1 - k); }
static int hskip(const char *w) { static const char *sk = (const char *)1; if (sk == (const char *)1) sk = getenv("SB_HUBSKIP"); return sk && strstr(sk, w); } /* development: SB_HUBSKIP=wall,grad,glow,peek,tiles times what each costs */
static void hub_draw_body(void) {
  static float hideK = 1; hideK += ((player_open() && !H.inPlayer ? 0.f : 1.f) - hideK) * fminf(1, app_dt() * 12); /* from Home, the player taking the channel as it plays: it fades away over the picture, and back after */
  float a = H.a.v * hideK; if (a <= 0.004f) return; double now = app_now();
  { static int pw; int po = player_open(); if (pw && !po && H.open) { H.focusAt = now; } pw = po; } /* back from the player: the focused one previews again */
  int out = !H.open && !H.leaving; /* leaving: its content sinks a touch as it fades (the backdrop and its shades stay whole: see below) */
  const LiveCh *f = focused(); uint32_t c1, c2; tint_of(f ? art_of(f) : NULL, &c1, &c2); uint32_t ac = ACC2 | 0xff000000u;
  gfx_opacity_push(a);
  /* the backdrop. Over a channel: its picture full screen behind, dark where the words, the tabs and the channels sit.
     From Home: the channels' wall, dark again under them */
  uint32_t dk = RGB(10, 9, 15);
  if (H.inPlayer) { gfx_rrect_h(0, 0, 1240, SB_H, 0, col_alpha(dk, 0.92f), col_alpha(dk, 0)); gfx_rrect_v(0, 0, SB_W, 220, 0, col_alpha(dk, 0.5f), col_alpha(dk, 0));
    gfx_rrect_v(0, 380, SB_W, 200, 0, col_alpha(dk, 0), col_alpha(dk, 0.94f)); gfx_rect(0, 580, SB_W, SB_H - 580, col_alpha(dk, 0.94f)); }
  else { /* from Home: the channels' wall, panning slowly (baked once: one picture), dark under the words and the row;
       once the focused channel plays, its picture full screen behind instead, dark where the words and the row are */
    float v = lpk_full_a();
    if (v < 0.999f) { if (!(baseCleared && a >= 0.999f && !gfx_in_target())) gfx_rect(0, 0, SB_W, SB_H, dk); /* (the frame was cleared to it) */ if (!hskip("wall")) livewall_backdrop(0.24f);
      if (!hskip("grad")) { gfx_rrect_h(0, 0, 760, 560, 0, col_alpha(dk, 0.85f), col_alpha(dk, 0)); gfx_rrect_v(0, 330, SB_W, 220, 0, col_alpha(dk, 0), col_alpha(dk, 0.96f)); gfx_rect(0, 550, SB_W, 620 - 550, col_alpha(dk, 0.96f)); } }
    if (v > 0.003f) { if (v < 0.999f) gfx_hole(0, 0, SB_W, SB_H, 0, v); gfx_opacity_push(v);
      gfx_rrect_h(0, 0, 1240, SB_H, 0, col_alpha(dk, 0.92f), col_alpha(dk, 0)); gfx_rrect_v(0, 0, SB_W, 220, 0, col_alpha(dk, 0.5f), col_alpha(dk, 0));
      gfx_rrect_v(0, 380, SB_W, 200, 0, col_alpha(dk, 0), col_alpha(dk, 0.94f)); gfx_rect(0, 580, SB_W, SB_H - 580, col_alpha(dk, 0.94f)); gfx_opacity_pop(); } }
  if (H.inPlayer) { float ph = (float)fmod(now / 14, 1) * 6.2832f, gk = 0.45f; /* over a channel: its two slow glows in the channel's colours, as before */
    gfx_alpha_tex(glow_tex(), -200 + 120 * sinf(ph), -260 + 60 * cosf(ph), 1300, 1000, col_alpha(c1 | 0xff000000u, 0.55f * gk));
    gfx_alpha_tex(glow_tex(), 900 + 100 * cosf(ph * 0.7f), 520 + 80 * sinf(ph), 1400, 900, col_alpha(c2 | 0xff000000u, 0.7f * gk)); }
  /* from Home: the focused channel plays full screen behind it once the remote rests on it (the square box is gone) */
  if (!H.inPlayer && !hskip("peek") && f && H.open) { Item it; live_item(f, &it); lpk_want(&it, 0, 0, SB_W, SB_H); }
  if (out) { float s = 0.985f + 0.015f * a; gfx_scale_push(s, s, SB_W / 2, SB_H / 2); gfx_offset_push(0, 26 * (1 - a)); }
  /* the focused channel */
  { float k = in_k(0, 0.45); gfx_opacity_push(k); gfx_offset_push(0, 22 * (1 - k)); float lx = 120, ly = 96, mw = 1100 - lx - 60;
    reach_mark(lx, ly, f ? f->id : NULL); ly += 64 + 14;
    static LiveCh curC, prevC; static double swapAt; static int hasPrev;
    if (f && strcmp(curC.id, f->id)) { if (curC.id[0]) { prevC = curC; hasPrev = 1; } curC = *f; swapAt = now; } /* a move: the old channel's logo and words go as the new ones come */
    float xf = (float)((now - swapAt) / 0.3); xf = xf < 0 ? 0 : xf > 1 ? 1 : xf; xf = xf * xf * (3 - 2 * xf); if (xf >= 1) hasPrev = 0;
    float ly0 = ly;
    for (int w = hasPrev ? 0 : 1; w < 2 && f; w++) { const LiveCh *c = w ? &curC : &prevC; float k = w ? xf : 1 - xf; if (k <= 0.003f) continue; float y2 = ly0 + (w ? 10 * (1 - xf) : -6 * xf);
      gfx_opacity_push(k); float lw, lh; GLuint t; if (fit_logo(art_of(c), 360, 130, &lw, &lh, &t)) { float fa = gfx_tex_fade(t); gfx_image(t, lx, y2 + 130 - lh, lw, lh, 0, WHITE_A((int)(255 * fa)), 0, 0, 1, 1); } y2 += 130 + 24; /* (its place kept while it comes: the words never jump; it fades in, at the place's foot) */
      text_draw(c->name, lx, y2, 52, 800, WHITE_A(255), -0.8f, mw, ALIGN_LEFT); y2 += text_line_height(52) + 10;
      const char *where = H.cur < H.ntab ? H.tab[H.cur].name : ""; text_draw(where, lx, y2, 25, 600, WHITE_A(170), 0, mw, ALIGN_LEFT); y2 += text_line_height(25) + 22;
      gfx_opacity_pop(); if (w) ly = y2; }
    if (f) { /* the line under it fades in whenever what it says changes */
      { int sk = lpk_state(f->id, NULL, 0) * 4 + (live_reach(f->id) < 0) + (H.inPlayer && live_card()->tuning) * 2; static int lastK = -99; static char lastId[SB_ID]; static double kAt;
        if (sk != lastK || strcmp(lastId, f->id)) { lastK = sk; snprintf(lastId, sizeof lastId, "%s", f->id); kAt = now; } float lk = (float)((now - kAt) / 0.25); lk = lk < 0 ? 0 : lk > 1 ? 1 : lk * lk * (3 - 2 * lk); gfx_opacity_push(lk); }
      const LiveCard *c = live_card(); int playing = H.inPlayer && !strcmp(f->id, H.playing);
      if (H.inPlayer && c->tuning && c->it && !strcmp(c->it->id, f->id)) { float ph = (float)fmod(now / 0.9, 1) * 6.2832f; for (int q = 0; q <= 8; q++) { float an = ph - 2.356f + 1.5708f * q / 8; gfx_circle(lx + 10 + cosf(an) * 8, ly + 14 + sinf(an) * 8, 1.5f, ac); } text_draw(T("Tuning in…"), lx + 32, ly, 25, 700, WHITE_A(230), 0, 0, ALIGN_LEFT); }
      else if (playing) { eq_bars(lx, ly + 4, 20, ac); text_draw(T("Now playing"), lx + 32, ly, 25, 700, WHITE_A(230), 0, 0, ALIGN_LEFT); }
      else if (!H.inPlayer && lpk_state(f->id, NULL, 0) == 2) { char q[8]; lpk_state(f->id, q, sizeof q); eq_bars(lx, ly + 4, 20, ac); float tw = text_draw(T("Now playing"), lx + 32, ly, 25, 700, WHITE_A(230), 0, 0, ALIGN_LEFT);
        if (q[0]) { float qw = text_width(q, 19, 800, 0.8f) + 20, qh = text_line_height(19) + 6, qx = lx + 32 + tw + 16; gfx_rrect(qx, ly + text_line_height(25) / 2 - qh / 2, qw, qh, 7, WHITE_A(36)); gfx_ring(qx, ly + text_line_height(25) / 2 - qh / 2, qw, qh, 7, 1.2f, WHITE_A(70)); text_draw(q, qx + 10, ly + text_line_height(25) / 2 - text_line_height(19) / 2, 19, 800, WHITE_A(235), 0.8f, 0, ALIGN_LEFT); } }
      else if (!H.inPlayer && lpk_state(f->id, NULL, 0) == 1 && live_reach(f->id) >= 0) { float ph = (float)fmod(now / 0.9, 1) * 6.2832f; for (int q = 0; q <= 8; q++) { float an = ph - 2.356f + 1.5708f * q / 8; gfx_circle(lx + 10 + cosf(an) * 8, ly + 14 + sinf(an) * 8, 1.5f, ac); } text_draw(T("Tuning in…"), lx + 32, ly, 25, 700, WHITE_A(230), 0, 0, ALIGN_LEFT); }
      else if (!H.inPlayer && (lpk_state(f->id, NULL, 0) < 0 || live_reach(f->id) < 0)) { gfx_circle(lx + 6, ly + 14, 5, RGB(0xef, 0x3b, 0x3b)); text_draw(T("This channel isn't broadcasting right now"), lx + 22, ly, 25, 600, WHITE_A(220), 0, mw - 20, ALIGN_LEFT); }
      else if (H.inPlayer && c->failed && c->it && !strcmp(c->it->id, f->id)) { gfx_circle(lx + 6, ly + 14, 5, RGB(0xef, 0x3b, 0x3b)); text_draw(T("This channel isn't broadcasting right now"), lx + 22, ly, 25, 600, WHITE_A(220), 0, mw - 20, ALIGN_LEFT); }
      gfx_opacity_pop(); }
    gfx_offset_pop(); gfx_opacity_pop(); }
  /* the keys: under the preview (over a channel: at the right, the yellow key closing it) */
  { float k = in_k(0.2, 0.4); gfx_opacity_push(k); float hx = PX + PW, hy = 536;
    { /* Back: on the channels it goes up to the catalogs, on them it closes; the two words crossfade where it stands */
      static float bz; bz += ((H.zone == 1 ? 1.f : 0.f) - bz) * fminf(1, app_dt() * 8); const char *l0 = T("Catalogs"), *l1 = T("Close");
      float w0 = key_hint(-9999, hy, K_BACK, l0, 0, 1), w1 = key_hint(-9999, hy, K_BACK, l1, 0, 1), bw = w0 > w1 ? w0 : w1; /* (measured: drawn off screen) */
      if (bz < 0.997f) { gfx_opacity_push(1 - bz); key_hint(hx, hy, K_BACK, l0, WHITE_A(200), 1); gfx_opacity_pop(); }
      if (bz > 0.003f) { gfx_opacity_push(bz); key_hint(hx, hy, K_BACK, l1, WHITE_A(200), 1); gfx_opacity_pop(); }
      hx -= bw + 22; }
    hx -= key_hint(hx, hy, K_GREEN, T(f && live_pinned(f->id) ? "Unfavorite" : "Favorite"), WHITE_A(200), 1) + 22;
    hx -= key_hint(hx, hy, K_OK, T("Hold: Streams"), WHITE_A(200), 1) + 22; key_hint(hx, hy, K_OK, T("Play"), WHITE_A(200), 1); /* (short: the keys say it) */
    gfx_opacity_pop(); }
  /* the catalogs: quiet words, the chosen one on a pill tinted with the accent that glides to it; with the focus on
     them, that pill fills with the accent. Each name carries its count, fainter */
  { float x = 120 + H.tabX.v, ty = 574, th = 50; gfx_clip_push(80, ty - 24, SB_W - 160, th + 48); int fz = H.zone == 1; float dt = app_dt(); (void)dt;
    float wsel = 0, xsel = 0;
    for (int i = 0; i < H.ntab; i++) { char cn[16]; snprintf(cn, sizeof cn, "%d", H.tab[i].n); float nw = text_width(H.tab[i].name, 22, 700, 0), cw = text_width(cn, 19, 600, 0.4f), w = 26 + nw + 10 + cw + 26;
      H.tabPx[i] = x - H.tabX.v; H.tabW[i] = w; if (i == H.cur) { xsel = x; wsel = w; } x += w + 8; }
    /* each pill keeps its own place and width from frame to frame, found again by its name (in a copy of last frame's:
       the list is rewritten as it goes): one just added (Favorites, Recent) opens from nothing where it goes, its
       words fading in as it widens, while the others glide aside; one taken away goes at once */
    static char pvName[MAXTAB][96]; static float pvX[MAXTAB], pvW[MAXTAB]; static int pvN; static double pvOpen = -1;
    static float tX[MAXTAB], tW[MAXTAB], tA[MAXTAB];
    { char (*oN)[96] = malloc(sizeof pvName); float oX[MAXTAB], oW[MAXTAB]; int oN0 = pvN, snap = pvOpen != H.openAt || !oN; pvOpen = H.openAt;
      if (oN) memcpy(oN, pvName, sizeof pvName); memcpy(oX, pvX, sizeof oX); memcpy(oW, pvW, sizeof oW);
      float kk = fminf(1, app_dt() * 14), cx = 0;
      for (int i = 0; i < H.ntab; i++) { int f = -1; if (!snap) for (int j = 0; j < oN0; j++) if (!strcmp(oN[j], H.tab[i].name)) { f = j; break; }
        float tw = H.tabW[i];
        if (snap) { tX[i] = H.tabPx[i]; tW[i] = tw; }
        else if (f < 0) { tX[i] = cx; tW[i] = 0; } /* new: from nothing, where the row has it now */
        else { tX[i] = oX[f] + (H.tabPx[i] - oX[f]) * kk; tW[i] = oW[f] + (tw - oW[f]) * kk; if (fabsf(tX[i] - H.tabPx[i]) < 0.3f) tX[i] = H.tabPx[i]; if (fabsf(tW[i] - tw) < 0.3f) tW[i] = tw; }
        if (f < 0 && !snap) { tW[i] = tw * kk; } /* (its first step open) */
        tA[i] = tw > 0 ? tW[i] / tw : 1; tA[i] = tA[i] * tA[i]; cx = tX[i] + tW[i] + 8;
        snprintf(pvName[i], sizeof pvName[i], "%s", H.tab[i].name); pvX[i] = tX[i]; pvW[i] = tW[i]; }
      pvN = H.ntab; free(oN); }
    if (H.cur < H.ntab) { if (!H.selSet) { tween_set(&H.selX, xsel - H.tabX.v); tween_set(&H.selW, wsel); H.selSet = 1; }
      else { if (fabsf(H.selX.to - (xsel - H.tabX.v)) > 0.5f) tween_to(&H.selX, xsel - H.tabX.v, 0.3f, EASE_ROW); if (fabsf(H.selW.to - wsel) > 0.5f) tween_to(&H.selW, wsel, 0.3f, EASE_ROW); }
      tween_tick(&H.selX, app_dt()); tween_tick(&H.selW, app_dt());
      float k = in_k(0.1, 0.4), sx = H.selX.v + H.tabX.v, sw = H.selW.v; gfx_opacity_push(k);
      if (fz) { gfx_rrect_v(sx, ty, sw, th, th / 2, col_mix(ac, WHITE_A(255), 0.10f), ac); }
      else { gfx_rrect(sx, ty, sw, th, th / 2, col_alpha(ac, 0.16f)); gfx_ring(sx, ty, sw, th, th / 2, 1.5f, col_alpha(ac, 0.42f)); }
      gfx_opacity_pop(); }
    for (int i = 0; i < H.ntab; i++) { float d = 0.1 + 0.03 * i, k = in_k(d > 0.5 ? 0.5 : d, 0.4) * tA[i]; if (k <= 0.003f) continue; float px = tX[i] + H.tabX.v, w = tW[i];
      if (px > SB_W || px + w < 0) { hit_add(px, ty, w, th, tab_pick, NULL, i, 0); continue; }
      int sel = i == H.cur; uint32_t tc = sel && fz ? ACC_FG : sel ? WHITE_A(255) : WHITE_A(150); char cn[16]; snprintf(cn, sizeof cn, "%d", H.tab[i].n);
      gfx_opacity_push(k); gfx_offset_push(0, 12 * (1 - k)); float ly = ty + th / 2 - text_line_height(22) / 2;
      float nw = text_draw(H.tab[i].name, px + 26, ly, 22, 700, tc, 0, 0, ALIGN_LEFT);
      text_draw(cn, px + 26 + nw + 10, ty + th / 2 - text_line_height(19) / 2, 19, 600, col_alpha(tc, sel ? 0.62f : 0.5f), 0.4f, 0, ALIGN_LEFT);
      gfx_offset_pop(); gfx_opacity_pop(); hit_add(px, ty, w, th, tab_pick, NULL, i, 0); }
    gfx_clip_pop();
    if (H.cur < H.ntab) { float r = H.tabPx[H.cur] + H.tabW[H.cur], want = r > SB_W - 160 ? -(r - (SB_W - 160)) : 0; if (H.tabPx[H.cur] + want < 120) want = 120 - H.tabPx[H.cur]; tween_to(&H.tabX, want, 0.25f, EASE_ROW); } }
  /* the grid */
  const Tab *l = cur_tab(); float gx = (SB_W - (COLS * TW + (COLS - 1) * TGAP)) / 2;
  gfx_clip_push(0, GRID_TOP - 24, SB_W, SB_H - GRID_TOP + 24); /* (under the pills, never behind them; room for a held tile's line) */
  if (l) { static float fk[1024]; float dt = app_dt();
    { const char *ids[32], *tys[32]; int nv = 0; const LiveCh *fc = focused(); if (fc) { ids[nv] = fc->id; tys[nv++] = fc->type; } /* the channels on screen, the focused one first: tried in the background */
      for (int i = 0; i < l->n && nv < 32; i++) { float y = GRID_TOP + (i / COLS) * PITCH + H.gridY.v; if (y > SB_H || y + PITCH < GRID_TOP - 40) continue; if (l->p[i] == fc) continue; ids[nv] = l->p[i]->id; tys[nv++] = l->p[i]->type; }
      if (H.open && !H.leaving) live_probe_visible(ids, tys, nv); }
    for (int pass = 0; pass < 2; pass++) for (int i = 0; i < l->n; i++) { int isf = i == H.idx && H.zone == 0; if (isf != pass) continue;
      int row = i / COLS, col = i % COLS; float y = GRID_TOP + row * PITCH + H.gridY.v; if (y > SB_H || y + PITCH < GRID_TOP - 40) { if (i < 1024) fk[i] = 0; continue; }
      int vr = row - (int)floorf(-H.gridY.v / PITCH + 0.01f); if (vr < 0) vr = 0; float d = (vr * COLS + col) * 0.018f; d = d > 0.3f ? 0.3f : d; /* (in one after another, by where they sit on screen) */
      float e = ((H.swap.on ? H.swap.t : 99) - d) / 0.25f; e = e < 0 ? 0 : e > 1 ? 1 : e; e = ease_apply(EASE_CSS_EASEOUT, e); /* (every one ends whole: never left part-faded) */
      float k = 0; if (i < 1024) { fk[i] += ((isf ? 1.f : 0.f) - fk[i]) * fminf(1, dt * 12); k = fk[i]; }
      float x = gx + col * (TW + TGAP); gfx_opacity_push(e); gfx_offset_push(0, 16 * (1 - e));
      if (!hskip("tiles")) tile(l->p[i], x, y, isf, k, H.inPlayer ? !strcmp(l->p[i]->id, H.playing) : !strcmp(l->p[i]->id, lpk_full_id()));
      gfx_offset_pop(); gfx_opacity_pop(); hit_add(x, y, TW, TH, tile_focus, NULL, i, 0); } }
  { static XLine xl; static WlSlot ws; static unsigned gen; static int had; int empty = !l || !l->n; if (had && empty) gen++; had = !empty; /* (a light line over it while the list comes, core/waitlines) */
    if (empty) xline_draw_w(&xl, xline_wait(&ws, gen, T("Loading channels…"), 1, WL_LIVE, WL_ACT_NONE, 0), SB_W / 2, GRID_TOP + 80, 27, 600, 0, WHITE_A(170), 0, ALIGN_CENTER); else xl.cur[0] = xl.old[0] = 0; }
  gfx_clip_pop();
  if (out) { gfx_offset_pop(); gfx_scale_pop(); }
  gfx_opacity_pop();
}
