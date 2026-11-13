/* The title page: cover art, logo, meta, the facts, the synopsis and the faces, the buttons, seasons and episodes.
   Buttons + seasons + episodes are one block that slides up over the synopsis as you go down. */
#include "screens/detail.h"
#include "screens/cwmenu.h"
#include "screens/streams.h"
#include "screens/sheet.h"
#include "screens/faces.h"
#include "screens/player.h"
#include "core/keys.h"
#include "core/i18n.h"
#include "core/json.h"
#include "core/best.h"
#include "core/tmdb.h"
#include "core/episodeart.h"
#include "core/trailer.h"
#include "ui/shared.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include "ui/img.h"
#include "ui/icon.h"
#include "ui/widgets.h"
#include "ui/sound.h"
#include "core/store.h"
#include "screens/revpanel.h"
#include "core/parental.h"
#include "core/rating.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include "core/hit.h"

#define EP_W 617.0f   /* episode card 589 + gap */
#define MAXBTN 7
#define MAXSEASON 64
#define MAXPPL 9
#define PP_STEP 154.0f   /* a face's column 140 + 14 */
#define BD 76.0f         /* a round button */
#define BG 16.0f         /* between them */
#define DESC_LH 42.0f    /* the synopsis: 28px at 1.5, at most 122px */
typedef struct { int n; const cJSON **eps; int neps; } Season;
typedef struct { char name[128], id[16], img[600]; int settled; Tween a, f; } Person; /* f: how focused (the face grows a little with the ring on it) */
typedef struct { int kind; QT x; Tween la, fb; double fAt; int f; } Btn;
static struct {
  DetailParams p; cJSON *meta, *pend; int zone; /* 0 btns, 1 seasons, 2 eps, 3 the people */
  Person ppl[MAXPPL]; int nppl, pIdx; Timer1 pRingPop; Tween pRingA, pRingX; int pRingOn, pRingEnter; /* enter: the ring's pop as the row takes the focus (it fades up); a move only scales */ float pplY, btnsY0; char facts[2][160]; int nfacts;
  Btn btn[MAXBTN]; int btnIdx, nbtn; FPill pill; Nudge nudge;
  Season seasons[MAXSEASON]; int nseasons, seasonIdx, epIdx; const cJSON **eps; int neps; int *watched; char resVid[80]; float resPct;
  double sT; int sPending; QT seasonsX; FPill sPill; int sPillOn; /* the season highlight glides between the pills, as the sheets' does */ Tween epsX; Timer1 epsIn; int epsStale; Tween epRingX, epRingA; Timer1 epRingPop; int epRingOn;
  int col; Tween lowerY; Tween moreA, moreY, btnsA, btnsY; Tween posterA, posterX, posterS;
  char logoUrl[512], bgKeep[512]; double openAt, bgT; int bgPending; int playHold, holdSwell; double playHoldAt; Tween hk, hintA, lblA, pgDim, revHintA; int pageZone, wasPage; /* the reviews risen: the zone they were entered from, back to it as they let down */ double zone0At; int zoneWas; int epHold, epSwell; double epHoldAt; float epK; char epVid[80]; /* Play held (best stream on): its pill fills and grows into Hold for streams (lblA: the words, quickly); an episode held: its streams; the film page's Reviews hint */
  Item focusedEp; unsigned gen; TmdbWait tw; int locDone; unsigned tgen;
  int moreWait; double moreUntil; Timer1 moreIn, pplIn, faceIn; int hadPpl, facesShown; /* faceIn: the faces come in from the side, one after another */   /* the synopsis and the faces come in together */
  int posterLate, logoLate, posterPh; Timer1 posterIn, logoIn; int posterSeen, logoSeen;
  const cJSON *src;
  double epRestAt; int epRestKey; /* the episode the focus rests on, since when (the "all streams" hint after 3 s) */
  unsigned ver;   /* bumps whenever what the page shows changes (details, translation, watched marks, the season): DC follows it */
} D;
static const char *ID(void) { return D.p.it.id; }
const char *detail_id(void) { return ID(); }
/* episodic: a series, or a title of another kind whose details list videos of their own (an addon's anime, a channel);
   a title whose details name a default video plays that one, as a film */
static int is_series(void) {
  if (!strcmp(D.p.it.type, "series")) return 1; if (!D.meta || !strcmp(D.p.it.type, "movie") || js_str(js_obj(D.meta, "behaviorHints"), "defaultVideoId", NULL)) return 0;
  const cJSON *v; cJSON_ArrayForEach(v, js_arr(D.meta, "videos")) { const char *id = js_str(v, "id", ""); if (*id && strcmp(id, D.p.it.id)) return 1; } return 0;
}
static const char *film_vid(void) { const char *dv = js_str(js_obj(D.meta, "behaviorHints"), "defaultVideoId", NULL); return dv && *dv ? dv : D.p.it.id; }
static const char *mname(void) { const char *n = js_str(D.meta, "locName", NULL); if (!n || !*n) n = js_str(D.meta, "name", NULL); return n ? n : D.p.it.name; }

/* ---- watched ---- */
static void load_watched(void) {
  D.ver++; free(D.watched); D.watched = NULL; D.resVid[0] = 0; D.resPct = 0; if (!is_series()) return;
  cJSON *lib = lib_get(ID()); const cJSON *st = js_obj(lib, "state"); const cJSON *videos = js_arr(D.meta, "videos");
  int n = videos ? cJSON_GetArraySize(videos) : 0;
  if (lib && n) { D.watched = calloc(n, sizeof(int)); lib_ep_watched_map(lib, videos, D.watched); }
  if (st) { const char *v = js_str(st, "video_id", ""); double d = js_num(st, "duration", 0); if (*v && d > 0) { snprintf(D.resVid, sizeof D.resVid, "%s", v); D.resPct = (float)(js_num(st, "timeOffset", 0) / d); } }
}
static int ep_watched(const cJSON *v) {
  if (!D.watched) return 0; const cJSON *videos = js_arr(D.meta, "videos"); int i = 0; const cJSON *x; cJSON_ArrayForEach(x, videos) { if (x == v) return D.watched[i]; i++; } return 0;
}
static int watched_now(void) { cJSON *it = lib_get(ID()); if (!it) return 0; return is_series() ? lib_series_watched(it, js_arr(D.meta, "videos")) : lib_movie_watched(it); }
static const char *first_unwatched(void) {
  for (int s = 0; s < D.nseasons; s++) { if (D.seasons[s].n == 0) continue; for (int e = 0; e < D.seasons[s].neps; e++) if (!ep_watched(D.seasons[s].eps[e])) return js_str(D.seasons[s].eps[e], "id", NULL); }
  return D.nseasons && D.seasons[0].neps ? js_str(D.seasons[0].eps[0], "id", NULL) : NULL;
}

/* ---- seasons / episodes ---- */
static int cmp_ep(const void *a, const void *b) { const cJSON *x = *(const cJSON **)a, *y = *(const cJSON **)b; double ex = js_num(x, "episode", js_num(x, "number", 0)), ey = js_num(y, "episode", js_num(y, "number", 0)); return ex < ey ? -1 : ex > ey; }
static void build_eps(void) {
  D.ver++; Season *se = D.seasonIdx < D.nseasons ? &D.seasons[D.seasonIdx] : NULL; D.eps = se ? se->eps : NULL; D.neps = se ? se->neps : 0; D.epIdx = 0;
  timer_start(&D.epsIn, 0.34f); D.epsStale = 0; tween_set(&D.epsX, 0); tween_set(&D.epRingX, 0);
}
static void build_seasons(void) {
  D.ver++; for (int i = 0; i < D.nseasons; i++) free(D.seasons[i].eps); D.nseasons = 0;
  const cJSON *videos = js_arr(D.meta, "videos"); const cJSON *v;
  cJSON_ArrayForEach(v, videos) { int sn = (int)js_num(v, "season", 0); int k = -1; for (int i = 0; i < D.nseasons; i++) if (D.seasons[i].n == sn) { k = i; break; }
    if (k < 0) { if (D.nseasons >= MAXSEASON) continue; k = D.nseasons++; D.seasons[k].n = sn; D.seasons[k].eps = NULL; D.seasons[k].neps = 0; }
    Season *s = &D.seasons[k]; s->eps = realloc(s->eps, (s->neps + 1) * sizeof *s->eps); s->eps[s->neps++] = v; }
  /* seasons in order, specials (0) last */
  for (int i = 1; i < D.nseasons; i++) for (int j = i; j > 0; j--) { int a = D.seasons[j - 1].n, b = D.seasons[j].n; if ((a == 0 && b != 0) || (a != 0 && b != 0 && a > b)) { Season t = D.seasons[j]; D.seasons[j] = D.seasons[j - 1]; D.seasons[j - 1] = t; } else break; }
  for (int i = 0; i < D.nseasons; i++) qsort(D.seasons[i].eps, D.seasons[i].neps, sizeof *D.seasons[i].eps, cmp_ep);
  D.seasonIdx = 0; D.epIdx = 0; build_eps();
}
static int select_episode(const char *vid, int focus) {
  for (int s = 0; s < D.nseasons; s++) for (int e = 0; e < D.seasons[s].neps; e++) if (!strcmp(js_str(D.seasons[s].eps[e], "id", ""), vid)) {
    if (s != D.seasonIdx || D.eps != D.seasons[s].eps) { D.seasonIdx = s; build_eps(); }
    D.epIdx = e; if (focus) D.zone = 2; return 1; }
  return 0;
}
static int unreleased(const cJSON *v) {
  time_t now = time(NULL); const char *keys[2] = { "released", "firstAired" };
  for (int i = 0; i < 2; i++) { const char *r = js_str(v, keys[i], NULL); if (r && strlen(r) >= 10) { struct tm tm; memset(&tm, 0, sizeof tm); if (sscanf(r, "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) == 3) { tm.tm_year -= 1900; tm.tm_mon -= 1; if (timegm(&tm) > now) return 1; } } }
  const char *n = js_str(v, "name", ""); while (*n == ' ') n++; if (!strncasecmp(n, "TBA", 3) && (n[3] == 0 || n[3] == ' ')) return 1;
  return 0;
}
static void rel_date(const cJSON *v, char *out, size_t cap) {
  out[0] = 0; const char *r = js_str(v, "released", NULL); if (!r) r = js_str(v, "firstAired", NULL); if (!r || strlen(r) < 10) return;
  int y, m, d; if (sscanf(r, "%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12) return;
  static const char *M[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" }; char ds[8], ys[8]; snprintf(ds, sizeof ds, "%d", d); snprintf(ys, sizeof ys, "%d", y);
  char mon[32]; snprintf(mon, sizeof mon, "%s", T(M[m - 1]));
  const char *t = T2("{mon} {d}, {y}", "mon", mon, "d", ds); char tmp[64]; snprintf(tmp, sizeof tmp, "%s", t);
  /* T2 fills two: the year by hand */
  char *yp = strstr(tmp, "{y}"); if (yp) { *yp = 0; snprintf(out, cap, "%s%s%s", tmp, ys, yp + 3); } else snprintf(out, cap, "%s", tmp);
}
static void queue_season(void) { D.epsStale = 1; D.sPending = 1; D.sT = app_now() + 0.22; }
static void flush_season(void) { if (D.sPending) { D.sPending = 0; build_eps(); } }

/* ---- trailers: resolved first, then the player in trailer mode ---- */
static struct { unsigned tok; char type[16], id[64]; int busy; } TR;
static void on_trailer(const Trailer *t, const char *err, void *ud) {
  (void)err; (void)ud; TR.busy = 0; TR.tok = 0;
  if (!player_waiting()) return; /* Back while it was being found */
  if (!t) { player_close(); toast(T("No trailer available"), 2600, NULL); return; }
  Stream s; memset(&s, 0, sizeof s); snprintf(s.url, sizeof s.url, "%s", t->url); snprintf(s.name, sizeof s.name, "%s", T(t->name[0] ? t->name : "Trailer"));
  if (t->alt[0]) player_fallback(t->alt); /* HLS first (IMDb's MP4s mark too few keyframes to seek), the MP4 when it fails */
  player_trailer_subs(t->subs, t->nsubs);
  app_play(&s, "other", TR.id, TR.id, 0);
}
static void trailer_go(const cJSON *m) { TR.tok = trailer_find_meta(m, TR.id, 0, on_trailer, NULL); }
static void on_trailer_meta(cJSON *m, void *ud) { if ((unsigned)(uintptr_t)ud != TR.tok + 1 || !TR.busy) return; trailer_go(m); }
void detail_trailer(const char *type, const char *id, const char *name) {
  (void)name; if (TR.busy && TR.tok) trailer_cancel(TR.tok);
  snprintf(TR.type, sizeof TR.type, "%s", type); snprintf(TR.id, sizeof TR.id, "%s", id); TR.busy = 1; TR.tok = 0;
  player_prepare("other", id, id, T("Trailer")); /* the title's art at once, the trailer found behind it */
  cJSON *m = sb_meta_cached(type, id); if (m) trailer_go(m); else sb_meta(type, id, on_trailer_meta, (void *)(uintptr_t)1);
}

/* ---- the best stream for this TV (Play, and OK on an episode when the setting is on): chosen behind the player's art ---- */
static void play_best_vid(const char *vid) { player_choose_best(D.p.it.type, ID(), vid); }
/* Play: a film, or the episode in view (else where you left off / the first unwatched); a title being resumed keeps
   the exact stream it was playing */
/* what Play plays: the film, or the episode in view, the one being watched, the first not yet seen; 0 none */
static int play_target(char *vid, size_t cap) {
  snprintf(vid, cap, "%s", film_vid()); if (!is_series()) return 1; const cJSON *st = js_obj(lib_get(ID()), "state");
  const char *v = D.eps && D.epIdx < D.neps ? js_str(D.eps[D.epIdx], "id", NULL) : NULL; if (!v || !*v) v = js_str(st, "video_id", NULL); if (!v || !*v) v = first_unwatched(); if (!v) return 0; snprintf(vid, cap, "%s", v); return 1; }
static float hold_w(void) { static float w; static unsigned lg; if (!w || lg != i18n_gen()) { lg = i18n_gen(); w = text_width(T("Hold for streams"), 26, 600, 0); } return w; }
static void play_best(void) {
  char vid[80]; cJSON *lb = lib_get(ID()); const cJSON *st = js_obj(lb, "state");
  if (!play_target(vid, sizeof vid)) return;
  Stream r; if (st && js_num(st, "timeOffset", 0) > 60000 && (!is_series() || !strcmp(js_str(st, "video_id", ""), vid)) && sb_resume_stream(ID(), vid, &r)) { app_play(&r, D.p.it.type, ID(), vid, 0); return; }
  play_best_vid(vid);
}

/* ---- buttons: Play (best stream on), Trailer, Similar, Library, Reviews, the watched eye ---- */
enum { B_PLAY, B_TRAILER, B_SIMILAR, B_LIB, B_EYE, B_REVIEWS };
static void btn_label_now(int kind, char *out, size_t cap, const char **icon) {
  switch (kind) {
    case B_PLAY: { cJSON *lb = lib_get(ID()); int rs = js_num(js_obj(lb, "state"), "timeOffset", 0) > 60000; *icon = "play"; snprintf(out, cap, "%s", T(rs ? "Resume" : "Play")); break; }
    case B_TRAILER: *icon = "trailer"; snprintf(out, cap, "%s", T("Trailer")); break;
    case B_SIMILAR: *icon = "similar"; snprintf(out, cap, "%s", T("Similar")); break;
    case B_REVIEWS: *icon = "reviews"; snprintf(out, cap, "%s", T("Reviews")); break;
    case B_LIB: { int on = lib_has(ID()); *icon = on ? "check" : "plus"; snprintf(out, cap, "%s", T(on ? "In Library" : "Library")); break; }
    default: { int w = watched_now(); *icon = w ? "eyeOff" : "eye"; snprintf(out, cap, "%s", T(w ? "Mark as unwatched" : "Mark as watched")); }
  }
}
/* the title's IMDb id (an episode id or another addon's id has none) */
static const char *imdb_of(char *buf, size_t cap) {
  const char *x = js_str(D.meta, "imdb_id", NULL); if (!x || !*x) x = ID(); snprintf(buf, cap, "%s", x); char *c = strchr(buf, ':'); if (c) *c = 0;
  if (strncmp(buf, "tt", 2) || !buf[2]) { buf[0] = 0; return buf; } for (const char *q = buf + 2; *q; q++) if (*q < '0' || *q > '9') { buf[0] = 0; break; } return buf;
}
static void make_btns(void) {
  char im[32]; int kinds[MAXBTN], n = 0;
  kinds[n++] = B_PLAY; /* always: the best stream on, it plays it (held: all of them); off, it opens the list */
  if (trailer_available(D.meta, ID())) kinds[n++] = B_TRAILER;
  kinds[n++] = B_SIMILAR; kinds[n++] = B_LIB; if (imdb_of(im, sizeof im)[0]) kinds[n++] = B_REVIEWS; kinds[n++] = B_EYE;
  int same = n == D.nbtn; for (int i = 0; same && i < n; i++) same = D.btn[i].kind == kinds[i];
  if (!same) { /* the buttons change (a trailer or the reviews became known): laid out again in place */
    for (int i = 0; i < n; i++) { Btn *b = &D.btn[i]; int keep = i < D.nbtn ? 1 : 0; float x = keep ? b->x.v : (i ? D.btn[i - 1].x.v + BD + BG : 0); memset(b, 0, sizeof *b); b->kind = kinds[i]; qt_set(&b->x, x); }
    D.nbtn = n; }
  if (D.btnIdx >= D.nbtn) D.btnIdx = D.nbtn - 1;
}
static Item meta_item(void) { Item it; memset(&it, 0, sizeof it); if (D.meta) sb_fill_item(&it, D.meta, D.p.it.type); else it = D.p.it; snprintf(it.id, sizeof it.id, "%s", ID()); snprintf(it.type, sizeof it.type, "%s", D.p.it.type); if (!it.name[0]) snprintf(it.name, sizeof it.name, "%s", D.p.it.name); return it; }
static void toggle_watched_all(void) {
  if (!D.meta) { toast(T("Could not load title"), 2000, NULL); return; }
  int on = !watched_now(); Item it = meta_item(); cJSON *l = lib_get(ID()); if (!l) l = lib_new_item(&it);
  if (is_series()) { const cJSON *vs = js_arr(D.meta, "videos"); const char *ids[2048]; int n = 0; const cJSON *v;
    cJSON_ArrayForEach(v, vs) { if (!js_num(v, "season", 0)) continue; if (on && unreleased(v)) continue; if (n < 2048) ids[n++] = js_str(v, "id", ""); } /* only what has aired */
    if (!n) return; lib_set_eps_watched(l, vs, ids, n, on); }
  else lib_set_movie_watched(l, on);
  app_refresh_boards(); detail_refresh_watched();
}
/* reviews: the panel, grown out of the button (for a series, with a switch between the show and the season in view) */
static void show_reviews(void) { if (revpanel_page_k() > 0) { revpanel_page_close(); return; } /* (in them already: Reviews again scrolls back up) */ char im[32]; if (!imdb_of(im, sizeof im)[0]) { toast(T("No reviews for this title"), 2600, NULL); return; } Item it = meta_item(); int sl[40], n = 0; for (int i = 0; i < D.nseasons && n < 40; i++) if (D.seasons[i].n > 0) sl[n++] = D.seasons[i].n; revpanel_seasons(sl, n); /* the whole series first, then each season */
  D.pageZone = 0; revpanel_show_page(&it); } /* (it slides the page down into them, as Down does) */
/* in an episode's reviews the Watched button is that episode's: its words, icon and width */
static int eye_ep(const char **lbl, const char **icon, float *w) {
  Item ep; if (!revpanel_page_episode(&ep)) return 0; int on = lib_is_marked_watched(&ep);
  *lbl = T(on ? "Mark episode as unwatched" : "Mark episode as watched"); *icon = on ? "eyeOff" : "eye"; *w = text_width(*lbl, 26, 600, 0); return 1; }
static void btn_act(int kind) {
  if (kind == B_PLAY) { if (best_on()) play_best(); else { char vid[80]; if (play_target(vid, sizeof vid)) detail_open_streams(vid); } }
  else if (kind == B_TRAILER) detail_trailer(D.p.it.type, ID(), mname());
  else if (kind == B_SIMILAR) { DetailParams p; memset(&p, 0, sizeof p); p.it = meta_item(); app_go(SCR_SIM, &p, 0); }
  else if (kind == B_LIB) { Item it = meta_item(); int was = lib_has(it.id); sound_play(SND_TOGGLE); cwmenu_fx_lib(!was, &it); lib_toggle(&it); app_refresh_boards(); } /* the chip, as from a card's menu */
  else if (kind == B_EYE) { Item ep; if (revpanel_page_episode(&ep)) { sound_play(SND_TOGGLE); lib_toggle_watched(&ep, js_arr(D.meta, "videos")); app_refresh_boards(); detail_refresh_watched(); } else toggle_watched_all(); } /* (in an episode's reviews: that episode) */
  else if (kind == B_REVIEWS) show_reviews();
}
void detail_paint_lib(void) {}
void detail_refresh_watched(void) { if (!D.meta) return; load_watched(); }

/* ---- the people: a strip of faces (directors, writers, then the cast in billing order); the names show at once,
   the faces and IMDb ids arrive one by one ---- */
static void ppl_add(const char *n) { char t[128]; snprintf(t, sizeof t, "%s", n); char *a = t; while (*a == ' ') a++; char *e = a + strlen(a); while (e > a && e[-1] == ' ') *--e = 0;
  if (!*a || D.nppl >= MAXPPL) return; for (int i = 0; i < D.nppl; i++) if (!strcmp(D.ppl[i].name, a)) return;
  Person *p = &D.ppl[D.nppl++]; memset(p, 0, sizeof *p); snprintf(p->name, sizeof p->name, "%s", a); }
static void on_face(int i, const char *id, const char *img, void *ud) {
  if ((unsigned)(uintptr_t)ud != D.gen || i >= D.nppl) return; Person *p = &D.ppl[i];
  snprintf(p->id, sizeof p->id, "%s", id); snprintf(p->img, sizeof p->img, "%s", img); p->settled = 1;
}
static void ppl_build(void) {
  int keep = D.pIdx; char old[MAXPPL][128]; int nold = D.nppl; for (int i = 0; i < nold; i++) snprintf(old[i], 128, "%s", D.ppl[i].name);
  Person kept[MAXPPL]; memcpy(kept, D.ppl, sizeof kept);
  D.nppl = 0; const char *keys[3] = { "director", "writer", "cast" }; int maxn[3] = { 2, 2, 8 };
  for (int k = 0; k < 3; k++) { const cJSON *x = cJSON_GetObjectItem(D.meta, keys[k]), *e; int n = 0;
    if (cJSON_IsArray(x)) cJSON_ArrayForEach(e, x) { if (n >= maxn[k]) break; if (cJSON_IsString(e)) { ppl_add(e->valuestring); n++; } }
    else if (cJSON_IsString(x)) { char buf[512]; snprintf(buf, sizeof buf, "%s", x->valuestring); char *sv; for (char *t = strtok_r(buf, ",", &sv); t && n < maxn[k]; t = strtok_r(NULL, ",", &sv)) { ppl_add(t); n++; } } }
  /* the details' links when the old name lists are missing (the protocol's newer form: {name, category, url}) */
  if (!D.nppl) { static const char *cats[3] = { "Directors", "Writers", "Cast" }; int maxl[3] = { 2, 2, 8 };
    for (int k = 0; k < 3; k++) { const cJSON *l; int n = 0; cJSON_ArrayForEach(l, js_arr(D.meta, "links")) { if (n >= maxl[k]) break; const char *c = js_str(l, "category", ""); if (strcasecmp(c, cats[k]) && !(k < 2 && !strncasecmp(c, cats[k], strlen(cats[k]) - 1))) continue; const char *nm = js_str(l, "name", ""); if (*nm) { ppl_add(nm); n++; } } } }
  D.pIdx = keep < D.nppl ? keep : (D.nppl ? D.nppl - 1 : 0);
  int same = nold == D.nppl; for (int i = 0; same && i < D.nppl; i++) same = !strcmp(old[i], D.ppl[i].name);
  if (same) { memcpy(D.ppl, kept, sizeof kept); return; } /* the same people: their faces stay */
  const char *names[MAXPPL]; for (int i = 0; i < D.nppl; i++) names[i] = D.ppl[i].name;
  if (D.nppl) faces_find(names, D.nppl, on_face, (void *)(uintptr_t)D.gen); else faces_cancel();
}
static void ppl_open(void) { if (D.pIdx >= D.nppl) return; Person *p = &D.ppl[D.pIdx]; if (!p->id[0]) { toast(T("No films found for this person"), 2600, NULL); return; }
  DetailParams pp; memset(&pp, 0, sizeof pp); snprintf(pp.it.type, sizeof pp.it.type, "person"); snprintf(pp.it.id, sizeof pp.it.id, "%s", p->id); snprintf(pp.it.name, sizeof pp.it.name, "%s", p->name); snprintf(pp.it.poster, sizeof pp.it.poster, "%s", p->img);
  app_go(SCR_SIM, &pp, 0); }
/* the facts under the meta line: the awards in gold (their first phrase), then where it comes from */
static int num_after(const char *s, const char *pre, const char *post) { /* "won 3 oscars" -> 3 */
  size_t pl = strlen(pre); for (const char *q = s; *q; q++) { if (strncasecmp(q, pre, pl)) continue; const char *r = q + pl; while (*r == ' ') r++; if (*r < '0' || *r > '9') continue; int n = atoi(r); while (*r >= '0' && *r <= '9') r++; while (*r == ' ') r++; if (!strncasecmp(r, post, strlen(post))) return n; }
  return -1; }
static void facts_build(void) {
  D.nfacts = 0; D.facts[0][0] = D.facts[1][0] = 0; const char *as = js_str(D.meta, "awards", ""); int n; char ns[16];
  if ((n = num_after(as, "won", "oscar")) >= 0) { snprintf(ns, sizeof ns, "%d", n); snprintf(D.facts[0], sizeof D.facts[0], "%s", n == 1 ? T("Won 1 Oscar") : T1("Won {n} Oscars", "n", ns)); }
  else if ((n = num_after(as, "nominated for", "oscar")) >= 0) { snprintf(ns, sizeof ns, "%d", n); snprintf(D.facts[0], sizeof D.facts[0], "%s", n == 1 ? T("Nominated for 1 Oscar") : T1("Nominated for {n} Oscars", "n", ns)); }
  else { for (const char *q = as; *q; q++) if (*q >= '0' && *q <= '9' && (q == as || q[-1] < '0' || q[-1] > '9')) { int v = atoi(q); const char *r = q; while (*r >= '0' && *r <= '9') r++; while (*r == ' ') r++; if (!strncasecmp(r, "win", 3)) { snprintf(ns, sizeof ns, "%d", v); snprintf(D.facts[0], sizeof D.facts[0], "%s", T1("{n} awards", "n", ns)); break; } } }
  const cJSON *cj = cJSON_GetObjectItem(D.meta, "country"); char co[200] = "";
  if (cJSON_IsString(cj)) { char buf[400]; snprintf(buf, sizeof buf, "%s", cj->valuestring); char *sv; int k = 0; for (char *t = strtok_r(buf, ",", &sv); t && k < 2; t = strtok_r(NULL, ",", &sv)) { while (*t == ' ') t++; char *e = t + strlen(t); while (e > t && e[-1] == ' ') *--e = 0; if (!*t) continue; size_t l = strlen(co); snprintf(co + l, sizeof co - l, "%s%s", k ? ", " : "", t); k++; } }
  else if (cJSON_IsArray(cj)) { const cJSON *e; int k = 0; cJSON_ArrayForEach(e, cj) { if (k >= 2 || !cJSON_IsString(e)) break; size_t l = strlen(co); snprintf(co + l, sizeof co - l, "%s%s", k ? ", " : "", e->valuestring); k++; } }
  snprintf(D.facts[1], sizeof D.facts[1], "%s", co); D.nfacts = (D.facts[0][0] || D.facts[1][0]) ? 1 : 0;
}
static const char *desc_text(void) { return js_str(D.meta, "description", D.p.it.description); }
static float desc_h_now(void) { const char *d = desc_text(); if (!*d) return 0; float h = text_wrap_count(d, (SB_W - 96) * 0.68f, 3, 28, 400, 0) * DESC_LH; return h > 122 ? 122 : h; }
static float names_h_now(void) { for (int i = 0; i < D.nppl; i++) if (text_wrap_count(D.ppl[i].name, 140, 2, 24, 600, 0) > 1) return 58; return D.nppl ? 29 : 0; }

/* ---- what the page draws, made once: again when the details, the library, the language or the minute move ---- */
/* one episode card's words and picture address, as draw_ep shows them */
typedef struct { int unrel, w, res, epNum; float tagW; const char *ov; char rel[64], src[512], tag[96], title[300], info[160], ovb[300]; } EpRec;
static struct {
  int ok; unsigned ver, lv, lg, frame; long mn, mnNow;
  char btnLbl[6][64]; const char *btnIcon[6]; float btnW[6]; int wNow;   /* the buttons by kind */
  char cover[512], fb[512]; const char *name, *desc; int nameN; float descH, namesH;
  MetaParts mp[2];                                                        /* the meta line: without / with the translated name */
  char sLbl[MAXSEASON][48]; float sW[MAXSEASON][2];                       /* the seasons, at 600 and 700 */
  EpRec *ep; unsigned char *epOk; int nep; const cJSON **eps;             /* the season's episodes, made as they show */
} DC;
static void dc_check(void) {
  unsigned f = app_frame(); if (f != DC.frame || !DC.ok) { DC.frame = f; DC.mnNow = (long)(time(NULL) / 60); }
  if (DC.ok && DC.ver == D.ver && DC.lv == lib_version() && DC.lg == i18n_gen() && DC.mn == DC.mnNow) return;
  DC.ok = 1; DC.ver = D.ver; DC.lv = lib_version(); DC.lg = i18n_gen(); DC.mn = DC.mnNow;
  for (int k = 0; k < 6; k++) { btn_label_now(k, DC.btnLbl[k], sizeof DC.btnLbl[k], &DC.btnIcon[k]); DC.btnW[k] = text_width(DC.btnLbl[k], 26, 600, 0); }
  DC.wNow = watched_now();
  { Item *it = malloc(sizeof *it); DC.cover[0] = DC.fb[0] = 0;
    if (it) { *it = meta_item(); char pu[512]; const char *lp = js_str(D.meta, "locPoster", NULL); const char *src = lp && *lp ? lp : sb_poster_of(it, pu, sizeof pu); snprintf(DC.cover, sizeof DC.cover, "%s", src ? src : "");
      char b[512]; const char *bu = it->background[0] ? sb_bg_small(it, b, sizeof b) : sb_poster_of(it, b, sizeof b); snprintf(DC.fb, sizeof DC.fb, "%s", bu ? bu : ""); free(it); } }
  float W = SB_W - 96; DC.name = mname(); DC.nameN = text_wrap_count(DC.name, W * 0.7f, 2, 78, 800, -1.6f);
  { char ml[200]; const char *rel = js_str(D.meta, "releaseInfo", js_str(D.meta, "year", "")); const char *rt = js_str(D.meta, "runtime", ""); const char *ir = js_str(D.meta, "imdbRating", ""); char irb[16] = "";
    /* the rating as a number too (some addons send 7.5, not "7.5"), else the one the card had (its line from the catalog) */
    { const cJSON *n = cJSON_GetObjectItemCaseSensitive(D.meta, "imdbRating"); if (cJSON_IsNumber(n) && n->valuedouble > 0) { snprintf(irb, sizeof irb, "%.1f", n->valuedouble); ir = irb; } }
    if (!*ir) { const char *q = strstr(D.p.it.metaLine, "IMDb "); if (q) { q += 5; size_t k = 0; while (k + 1 < sizeof irb && ((q[k] >= '0' && q[k] <= '9') || q[k] == '.')) { irb[k] = q[k]; k++; } irb[k] = 0; ir = irb; } }
    if (D.meta) snprintf(ml, sizeof ml, "%s %s %s%s", rel, rt, *ir ? "IMDb " : "", ir); else snprintf(ml, sizeof ml, "%s", D.p.it.metaLine);
    char gen[200] = ""; const cJSON *gs = js_arr(D.meta, "genres"); if (gs) { int n = 0; const cJSON *g; cJSON_ArrayForEach(g, gs) { if (n >= 3 || !cJSON_IsString(g)) break; size_t l = strlen(gen); snprintf(gen + l, sizeof gen - l, "%s%s", n ? T(", ") : "", T(g->valuestring)); n++; } }
    const char *ln = js_str(D.meta, "locName", ""), *ll = js_str(D.meta, "locLogo", ""); /* a translated name beside an English logo */
    meta_prep(&DC.mp[0], ml, gen, NULL, 27); meta_prep(&DC.mp[1], ml, gen, *ln && !*ll ? ln : NULL, 27); }
  for (int i = 0; i < D.nseasons; i++) { char *lbl = DC.sLbl[i]; if (D.seasons[i].n == 0) snprintf(lbl, sizeof DC.sLbl[i], "%s", T("Specials")); else { char n[8]; snprintf(n, sizeof n, "%d", D.seasons[i].n); snprintf(lbl, sizeof DC.sLbl[i], "%s", T1("Season {n}", "n", n)); }
    DC.sW[i][0] = text_width(lbl, 27, 600, 0); DC.sW[i][1] = text_width(lbl, 27, 700, 0); }
  DC.desc = desc_text(); DC.descH = desc_h_now(); DC.namesH = names_h_now();
  if (DC.eps != D.eps || DC.nep != D.neps) { free(DC.ep); free(DC.epOk); DC.ep = NULL; DC.epOk = NULL; DC.eps = D.eps; DC.nep = D.neps;
    if (D.neps > 0) { DC.ep = malloc((size_t)D.neps * sizeof *DC.ep); DC.epOk = calloc((size_t)D.neps, 1); if (!DC.ep || !DC.epOk) { free(DC.ep); free(DC.epOk); DC.ep = NULL; DC.epOk = NULL; DC.nep = 0; } } }
  else if (DC.epOk) memset(DC.epOk, 0, (size_t)DC.nep);
}
static float desc_h(void) { dc_check(); return DC.descH; }
static float names_h(void) { dc_check(); return DC.namesH; }
static void ep_make(EpRec *r, int i) {
  const cJSON *v = D.eps[i];
  r->unrel = unreleased(v); r->w = ep_watched(v); rel_date(v, r->rel, sizeof r->rel);
  r->epNum = (int)js_num(v, "episode", js_num(v, "number", i + 1)); int season = (int)js_num(v, "season", -1);
  r->src[0] = 0; const char *th = js_str(v, "thumbnail", NULL);
  if (th && *th) snprintf(r->src, sizeof r->src, "%s", th); else if (sb_is_imdb(ID()) && season >= 0) snprintf(r->src, sizeof r->src, "https://episodes.metahub.space/%s/%d/%d/w780.jpg", ID(), season, r->epNum);
  /* The 589 px episode card must not enlarge a 300/400 px TMDB thumbnail. */
  episode_art_upgrade_tmdb(r->src, sizeof r->src, 780);
  r->tag[0] = 0; r->tagW = 0;
  if (r->unrel) { if (r->rel[0]) snprintf(r->tag, sizeof r->tag, "%s", T1("Upcoming · {d}", "d", r->rel)); else snprintf(r->tag, sizeof r->tag, "%s", T("Upcoming")); r->tagW = text_width(r->tag, 22, 600, 0.2f) + 50; } /* (sentence case, on dark glass: see draw_ep) */
  r->res = !strcmp(js_str(v, "id", ""), D.resVid);
  const char *nm = js_str(v, "name", js_str(v, "title", NULL)); if (nm && *nm) snprintf(r->title, sizeof r->title, "%d. %s", r->epNum, nm); else { char n[16]; snprintf(n, sizeof n, "%d", r->epNum); snprintf(r->title, sizeof r->title, "%d. %s", r->epNum, T1("Episode {n}", "n", n)); }
  { char rt[32] = ""; int mn = sb_runtime_min(js_str(v, "runtime", "")); if (mn) sb_fmt_runtime(mn, rt, sizeof rt); else snprintf(rt, sizeof rt, "%s", js_str(v, "runtime", "")); snprintf(r->info, sizeof r->info, "%s%s%s", r->rel, r->rel[0] && rt[0] ? "  ·  " : "", rt); }
  r->ov = js_str(v, "overview", js_str(v, "description", ""));
  if (!*r->ov && r->unrel) { if (r->rel[0]) snprintf(r->ovb, sizeof r->ovb, "%s", T1("Expected air date: {d}", "d", r->rel)); else snprintf(r->ovb, sizeof r->ovb, "%s", T("Air date to be announced.")); r->ov = r->ovb; }
}
static const EpRec *ep_rec(int i) { dc_check(); if (!DC.ep || i < 0 || i >= DC.nep) return NULL; if (!DC.epOk[i]) { ep_make(&DC.ep[i], i); DC.epOk[i] = 1; } return &DC.ep[i]; }
static float ppl_h(void) { return 124 + 12 + names_h(); }
static float more_h(void) { float dh = desc_h(); return 20 + (dh ? 22 + dh : 0) + (D.nppl ? 16 + ppl_h() : 0); } /* dMore: the synopsis, then the faces */
/* the faces' bottom margin (or the empty credits' top one) sits below the block, outside what the lower block slides over */
static float more_after(void) { return D.nppl ? 8 : 16; }

/* ---- the page ---- */
static void collapse(int c, int snap) {
  if (c == D.col) return; D.col = c;
  float y = 0;
  if (c >= 1) y += more_h();
  if (c >= 2) y += BD + 30; /* the buttons' height + the seasons' top margin */
  float dur = snap ? 0 : 0.34f;
  tween_to(&D.lowerY, -y, dur, EASE_OUT);
  tween_to(&D.moreA, c >= 1 ? 0 : 1, snap ? 0 : (c >= 1 ? 0.2f : 0.45f), EASE_OUT); tween_to(&D.moreY, c >= 1 ? -22 : 0, snap ? 0 : (c >= 1 ? 0.3f : 0.55f), EASE_OUT);
  tween_to(&D.btnsA, c >= 2 ? 0 : 1, snap ? 0 : (c >= 2 ? 0.2f : 0.45f), EASE_OUT); tween_to(&D.btnsY, c >= 2 ? -22 : 0, snap ? 0 : (c >= 2 ? 0.3f : 0.55f), EASE_OUT);
  int deps = c >= 1; tween_to(&D.posterA, deps ? 0 : 1, snap ? 0 : (deps ? 0.22f : 0.5f), EASE_OUT); tween_to(&D.posterX, deps ? 30 : 0, snap ? 0 : (deps ? 0.3f : 0.6f), EASE_OUT); tween_to(&D.posterS, deps ? 0.97f : 1, snap ? 0 : (deps ? 0.3f : 0.6f), EASE_OUT);
}
static void bg_follow(void) { D.bgPending = 1; D.bgT = app_now() + (D.zone == 0 ? 0 : 0.32); }
static void bg_apply(void) {
  D.bgPending = 0; char u[512], alt[512]; const char *src = D.bgKeep[0] ? D.bgKeep : NULL;
  if (!src) { Item it = meta_item(); if (!it.background[0] && !it.poster[0]) return; src = sb_bg_mid(&it, u, sizeof u); sb_poster_of(&it, alt, sizeof alt); } else alt[0] = 0;
  if (!src || !*src || !strcmp(src, bg_current())) return;
  bg_set(src, 0, alt);
}
/* the episode ring: appears in place with a pop, then glides between the cards */
static void ep_ring(void) {
  int on = D.zone == 2 && D.epIdx < D.neps; float x = D.epIdx * EP_W;
  if (on && !D.epRingOn) { tween_set(&D.epRingX, x); timer_start(&D.epRingPop, 0.32f); }
  else if (on && D.epRingX.to != x) { static double last; int pop = focus_pops(&last); tween_to(&D.epRingX, x, pop ? GLIDE_DUR : 0.14f, GLIDE_EASE); if (pop) timer_start(&D.epRingPop, 0.32f); } /* held: it glides, lit, with the focus */
  if (on != D.epRingOn) { D.epRingOn = on; tween_to(&D.epRingA, on ? 1 : 0, 0.2f, EASE_IN_OUT); if (on) tween_set(&D.epRingA, 1); }
}
static void paint(void) {
  collapse(D.zone == 2 ? 2 : D.zone == 1 ? 1 : 0, 0);
  if (D.zone == 2) { float x = (D.epIdx > 1 ? D.epIdx - 1 : 0) * EP_W; tween_to(&D.epsX, -x, 0.26f, EASE_OUT); } /* the focused card sits second, the previous one stays in view */
  ep_ring();
  int pr = D.zone == 3; tween_to(&D.pRingA, pr ? 1 : 0, 0.16f, EASE_IN_OUT); if (pr && D.pRingA.v < 0.01f) tween_set(&D.pRingA, 1);
  bg_follow();
}
static void show_more(void) { if (!D.moreWait) return; D.moreWait = 0; timer_start(&D.moreIn, 0.42f); }
static void logo_pick(void) { const char *l = js_str(D.meta, "locLogo", NULL); if (!l || !*l) l = NULL;
  if (l && strcmp(D.logoUrl, l)) snprintf(D.logoUrl, sizeof D.logoUrl, "%s", l);
  else if (!l && !D.logoUrl[0]) { const char *lg = js_str(D.meta, "logo", NULL); if (lg) snprintf(D.logoUrl, sizeof D.logoUrl, "%s", lg); } }
static void render(void) {
  D.ver++; int hadPpl = D.nppl; make_btns(); load_watched(); ppl_build(); facts_build(); logo_pick();
  if (D.moreWait) show_more(); else if (D.nppl && !hadPpl && !D.hadPpl) timer_start(&D.pplIn, 0.42f); /* credits that came late fade up on their own */
  D.hadPpl = D.nppl > 0; D.col = -1;
  if (is_series()) build_seasons();
  int snap = app_now() - D.openAt < 3;
  if (is_series()) {
    const char *av = D.p.it.videoId[0] && strcmp(D.p.it.videoId, ID()) ? D.p.it.videoId : NULL;
    if (!av) { cJSON *lib = lib_get(ID()); const cJSON *st = js_obj(lib, "state"); if (st && js_num(st, "timeOffset", 0) > 60000 && *js_str(st, "video_id", "")) av = js_str(st, "video_id", ""); }
    if (!av) av = first_unwatched();
    char avb[80] = ""; if (av) snprintf(avb, sizeof avb, "%s", av); av = avb[0] ? avb : NULL; /* av may point into the params cleared next */
    int oe = D.p.openStreams; D.p.openStreams = 0; D.p.it.videoId[0] = 0;
    /* landing on the episode list as the page opens: laid out that way from the first frame */
    /* the page opens on its words and buttons; the episode row already rests on where you left off (only a play
       request for an episode lands on the episodes, with its streams) */
    if (av) { char vid[80]; snprintf(vid, sizeof vid, "%s", av); if (select_episode(vid, oe)) { tween_set(&D.epsX, -(D.epIdx > 1 ? D.epIdx - 1 : 0) * EP_W); if (oe && snap) { D.col = -1; collapse(2, 1); } paint(); if (oe) detail_open_streams(vid); return; } }
    paint();
  } else paint(); /* (Play is the way in, the best stream on or off) */
}
/* the details as they come: held back while their translation is on its way (1.5 s at most) */
static void adopt(void) {
  if (D.meta) cJSON_Delete(D.meta); D.meta = D.pend; D.pend = NULL; D.locDone = 0;
  const TmdbLoc *L = tmdb_loc(D.p.it.type, ID(), 0); if (L) { tmdb_apply_meta(D.meta, L); D.locDone = 1; }
  render();
}
static void on_meta(cJSON *m, void *ud) {
  if ((unsigned)(uintptr_t)ud != D.gen || SCR != SCR_DETAIL) return;
  if (!m) { if (!D.meta) toast(T("Could not load title"), 2000, NULL); return; }
  if (m == D.src && (D.meta || D.pend)) return; /* the copy on the page is this one */
  D.src = m;
  if (D.pend) cJSON_Delete(D.pend); D.pend = cJSON_Duplicate(m, 1);
  if (tmdb_wait(&D.tw, D.p.it.type, ID(), 0, TMDB_WAIT_PAGE)) adopt();
}
static void rating_ahead(const Rating *r, void *ud) { (void)r; (void)ud; } /* (kept by the rating module for when the film starts) */
static void enter(const void *pp) {
  if (pp) D.p = *(const DetailParams *)pp;
  if (!strncmp(D.p.it.id, "tt", 2)) { parental_get(D.p.it.id, NULL, NULL); rating_get(D.p.it.type, D.p.it.id, rating_ahead, NULL); } /* (the viewer notice's data, ahead of Play) */
  D.gen++; D.ver++; D.openAt = app_now(); D.zone = 0; D.btnIdx = 0; D.nbtn = 0; D.nppl = 0; D.pIdx = 0; D.nfacts = 0; D.hadPpl = 0; faces_cancel();
  if (D.meta) cJSON_Delete(D.meta); D.meta = NULL; if (D.pend) cJSON_Delete(D.pend); D.pend = NULL; free(D.watched); D.watched = NULL;
  memset(&D.tw, 0, sizeof D.tw); D.tgen = tmdb_generation(); D.locDone = 0;
  for (int i = 0; i < D.nseasons; i++) free(D.seasons[i].eps); D.nseasons = 0; D.eps = NULL; D.neps = 0; D.sPending = 0;
  D.bgKeep[0] = 0; if (bg_current()[0]) { /* opened from a board showing this title: keep that exact picture */
    char u[512]; sb_bg_mid(&D.p.it, u, sizeof u); if (!strcmp(u, bg_current())) snprintf(D.bgKeep, sizeof D.bgKeep, "%s", u); }
  { char pu[512], lu[512]; sb_poster_of(&D.p.it, pu, sizeof pu); sb_logo_of(&D.p.it, lu, sizeof lu); accent_scope(ID(), pu, lu); snprintf(D.logoUrl, sizeof D.logoUrl, "%s", lu); }
  D.col = -1; tween_set(&D.lowerY, 0); tween_set(&D.moreA, 1); tween_set(&D.moreY, 0); tween_set(&D.btnsA, 1); tween_set(&D.btnsY, 0); tween_set(&D.posterA, 1); tween_set(&D.posterX, 0); tween_set(&D.posterS, 1); tween_set(&D.epsX, 0); qt_set(&D.seasonsX, 0);
  fpill_snap(&D.pill); D.epRingOn = 0; tween_set(&D.epRingA, 0); tween_set(&D.pRingA, 0);
  D.posterSeen = D.logoSeen = 0; D.posterLate = D.logoLate = 0; D.posterPh = 0; D.moreWait = 0; D.moreIn.on = 0; D.pplIn.on = 0; D.facesShown = 0; D.faceIn.on = 0;
  cJSON *mm = sb_meta_cached(D.p.it.type, ID());
  D.src = mm; if (mm) { D.pend = cJSON_Duplicate(mm, 1); adopt(); }
  else { D.moreWait = !D.p.it.description[0]; D.moreUntil = app_now() + 0.7; /* the card's words come in with the page (one motion); without them the synopsis waits for the details (0.7 s at most) */
    make_btns(); if (!D.bgKeep[0] && (D.p.it.background[0] || D.p.it.poster[0])) { char u[512], a[512]; bg_set(sb_bg_mid(&D.p.it, u, sizeof u), 0, sb_poster_of(&D.p.it, a, sizeof a)); } paint(); }
  sb_meta(D.p.it.type, ID(), on_meta, (void *)(uintptr_t)D.gen);
}
static void leave(void) { streams_close(1); faces_cancel(); }
static void ep_label(const char *vid, char *out, size_t cap) {
  if (!is_series()) { char rt[32] = ""; int mn = sb_runtime_min(js_str(D.meta, "runtime", "")); if (mn) sb_fmt_runtime(mn, rt, sizeof rt); const char *y = js_str(D.meta, "releaseInfo", js_str(D.meta, "year", "")); snprintf(out, cap, "%s%s%s", y, *y && *rt ? "  " : "", rt); return; }
  const char *c = strrchr(vid, ':'); out[0] = 0;
  if (c) { const char *p = c - 1; while (p > vid && *p != ':') p--; if (*p == ':') snprintf(out, cap, "S%d · E%d", atoi(p + 1), atoi(c + 1)); }
  for (int i = 0; i < D.neps; i++) if (!strcmp(js_str(D.eps[i], "id", ""), vid)) { const char *n = js_str(D.eps[i], "name", js_str(D.eps[i], "title", "")); if (*n) { size_t l = strlen(out); snprintf(out + l, cap - l, "%s%s", l ? "  ·  " : "", n); } break; }
}
void detail_open_streams(const char *vid) { if (revpanel_page_k() > 0) revpanel_page_close(); /* (the reviews risen: they let down first, the list is the page's) */ char lbl[400]; ep_label(vid, lbl, sizeof lbl); streams_show(D.p.it.type, ID(), vid, mname(), lbl); }
static const Item *focused(void) {
  if (D.zone != 2 || D.epIdx >= D.neps) return NULL; const cJSON *v = D.eps[D.epIdx];
  Item *it = &D.focusedEp; *it = meta_item(); snprintf(it->videoId, sizeof it->videoId, "%s", js_str(v, "id", "")); snprintf(it->epName, sizeof it->epName, "%s", js_str(v, "name", js_str(v, "title", ""))); it->ep = 1; it->cw = 0; /* (its kind stays the title's: an anime's episode is asked as one) */
  return it;
}
static void not_aired(const char *vid);
static void open_ep(const Item *it) { /* a short OK on an episode (the app's press path): as the key does */
  for (int i = 0; i < D.neps; i++) if (!strcmp(js_str(D.eps[i], "id", ""), it->videoId) && unreleased(D.eps[i])) { not_aired(it->videoId); return; }
  if (best_on()) play_best_vid(it->videoId); else detail_open_streams(it->videoId); }
static void nudge_btn(int k) { nudge_start(&D.nudge, k); }
/* OK on an episode that has not aired: its streams open as usual, and where they would say none was found, the first time ever one light line (how many still come before it, its
   date, or that nobody knows yet); after that, just its date */
static void not_aired(const char *vid) {
  const cJSON *v = D.eps[D.epIdx]; char d[64]; rel_date(v, d, sizeof d); int ahead = 0;
  for (int i = 0; i < D.epIdx; i++) if (unreleased(D.eps[i])) ahead++;
  char m[200]; char n[12]; snprintf(n, sizeof n, "%d", ahead);
  if (strcmp(store_get("eggUnaired", ""), "1")) { store_set("eggUnaired", "1");
    if (ahead >= 2) snprintf(m, sizeof m, "%s", T1("Easy there. {n} episodes still have to air before this one.", "n", n));
    else if (d[0]) snprintf(m, sizeof m, "%s", T1("This one airs on {d}. We checked twice.", "d", d));
    else snprintf(m, sizeof m, "%s", T("Not even the writers know when this one airs.")); }
  else if (d[0]) snprintf(m, sizeof m, "%s", T1("Expected air date: {d}", "d", d));
  else snprintf(m, sizeof m, "%s", T("Air date to be announced."));
  detail_open_streams(vid); streams_empty_note(m); /* the list opens as usual; if nothing is there, these words take its place */
}
static int key_in(int k);
int detail_page_btn_key(int k) { if (k != K_LEFT && k != K_RIGHT && k != K_OK) return 0; D.zone = 0; return key_in(k); }
static int key_in(int k) {
  if (D.zone == 0) { int b0 = D.btnIdx;
    if (k == K_RIGHT) D.btnIdx = D.btnIdx + 1 < D.nbtn ? D.btnIdx + 1 : D.btnIdx;
    else if (k == K_LEFT) D.btnIdx = D.btnIdx > 0 ? D.btnIdx - 1 : 0;
    else if (k == K_UP) { if (D.nppl) { D.zone = 3; D.pRingEnter = 1; timer_start(&D.pRingPop, 0.2f); paint(); return 1; } topnav_focus(); return 1; } /* Up: the people, then the top tabs */
    else if (k == K_OK) { if (D.btnIdx < D.nbtn && D.btn[D.btnIdx].kind == B_PLAY && best_on()) { if (!D.playHold) { D.playHold = 1; D.holdSwell = 0; D.playHoldAt = app_now(); } } /* Play: a tap plays, a hold opens all the streams (on release or when full) */
      else { fpill_press(&D.pill); if (D.btnIdx < D.nbtn) btn_act(D.btn[D.btnIdx].kind); } }
    else if (k == K_DOWN) { if (is_series()) { if (D.nseasons) D.zone = 1; else nudge_btn(K_DOWN); } else { char im[32]; if (imdb_of(im, sizeof im)[0]) { Item it = meta_item(); sound_focus(); D.pageZone = 0; revpanel_show_page(&it); } else nudge_btn(K_DOWN); return 1; } } /* (best stream on: Play holds the streams, Down brings the reviews up) */ /* film: Down re-opens the streams sheet */
    else if (k == K_BACK) return 0;
    if ((k == K_LEFT || k == K_RIGHT) && D.btnIdx == b0) nudge_btn(k);
    paint(); return 1; }
  if (D.zone == 3) { int p0 = D.pIdx;
    if (k == K_RIGHT) D.pIdx = D.pIdx + 1 < D.nppl ? D.pIdx + 1 : D.pIdx; else if (k == K_LEFT) D.pIdx = D.pIdx > 0 ? D.pIdx - 1 : 0;
    else if (k == K_DOWN) D.zone = 0; else if (k == K_UP) { topnav_focus(); return 1; } else if (k == K_OK) { ppl_open(); return 1; } else if (k == K_BACK) return 0;
    if ((k == K_LEFT || k == K_RIGHT) && D.pIdx == p0) nudge_start(&D.nudge, k); /* (a move: the ring travels, swelling a little on the way: drawn from its glide) */
    paint(); return 1; }
  if (D.zone == 1) {
    if (k == K_RIGHT) { if (D.seasonIdx < D.nseasons - 1) { D.seasonIdx++; queue_season(); } else nudge_start(&D.nudge, k); }
    else if (k == K_LEFT) { if (D.seasonIdx > 0) { D.seasonIdx--; queue_season(); } else nudge_start(&D.nudge, k); }
    else if (k == K_UP) { flush_season(); D.zone = 0; }
    else if (k == K_DOWN || k == K_OK) { flush_season(); if (D.neps) D.zone = 2; else if (k == K_DOWN) nudge_start(&D.nudge, K_DOWN); }
    else if (k == K_BACK) return 0;
    paint(); return 1; }
  int e0 = D.epIdx;
  if (k == K_RIGHT) D.epIdx = D.epIdx + 1 < D.neps ? D.epIdx + 1 : D.epIdx;
  else if (k == K_LEFT) D.epIdx = D.epIdx > 0 ? D.epIdx - 1 : 0;
  else if (k == K_UP) D.zone = 1;
  else if (k == K_OK) { if (D.epIdx < D.neps) { const char *v = js_str(D.eps[D.epIdx], "id", ""); if (unreleased(D.eps[D.epIdx])) { not_aired(v); return 1; } if (best_on()) { if (!D.epHold) { D.epHold = 1; D.epSwell = 0; D.epHoldAt = app_now(); snprintf(D.epVid, sizeof D.epVid, "%s", v); } } else detail_open_streams(v); return 1; } } /* best stream on: a tap plays it, a hold opens its streams */
  else if ((k == K_YELLOW || k == K_BLUE) && D.epIdx < D.neps) { const Item *fi = focused(); if (fi) { Item it = *fi; sound_play(SND_TOGGLE); lib_toggle_watched(&it, js_arr(D.meta, "videos")); app_refresh_boards(); detail_refresh_watched();
      toast(T(lib_is_marked_watched(&it) ? "Marked as watched" : "Marked as unwatched"), 1800, NULL); } return 1; } /* the yellow or blue key: this episode watched, or not */
  else if (k == K_DOWN && D.epIdx < D.neps) { char im[32]; if (imdb_of(im, sizeof im)[0]) { const Item *fi = focused(); if (fi) { Item it = *fi; sound_focus(); D.pageZone = 2; revpanel_show_page(&it); return 1; } } } /* Down: this episode's reviews, risen into the page as a film's */
  else if (k == K_BACK) { D.zone = 1; paint(); return 1; }
  if (((k == K_LEFT || k == K_RIGHT) && D.epIdx == e0) || k == K_DOWN) nudge_start(&D.nudge, k);
  paint(); return 1;
}
static void tick(float dt) {
  nudge_tick(&D.nudge, dt); fpill_tick(&D.pill, dt); faces_tick();
  tween_tick(&D.lowerY, dt); tween_tick(&D.moreA, dt); tween_tick(&D.moreY, dt); tween_tick(&D.btnsA, dt); tween_tick(&D.btnsY, dt);
  timer_tick(&D.pRingPop, dt); tween_tick(&D.pRingA, dt); tween_tick(&D.pRingX, dt); if (D.zone != 3) D.pRingOn = 0; for (int i = 0; i < D.nppl; i++) { Person *p = &D.ppl[i]; tween_tick(&p->a, dt); float f = D.zone == 3 && i == D.pIdx ? 1.f : 0.f; if (p->f.to != f) tween_to(&p->f, f, GLIDE_DUR, GLIDE_EASE); tween_tick(&p->f, dt); }
  tween_tick(&D.posterA, dt); tween_tick(&D.posterX, dt); tween_tick(&D.posterS, dt); tween_tick(&D.epsX, dt); qt_tick(&D.seasonsX, dt); fpill_tick(&D.sPill, dt); timer_tick(&D.epsIn, dt);
  tween_tick(&D.epRingX, dt); tween_tick(&D.epRingA, dt); timer_tick(&D.epRingPop, dt);
  timer_tick(&D.moreIn, dt); timer_tick(&D.pplIn, dt); timer_tick(&D.faceIn, dt); timer_tick(&D.posterIn, dt); timer_tick(&D.logoIn, dt);
  if (D.moreWait && app_now() >= D.moreUntil) show_more();
  /* the buttons: round, the focused one opens into a pill with its name and the others slide aside */
  { int pg = revpanel_page_k() > 0; if (D.wasPage && !pg && D.zone != D.pageZone) { D.zone = D.pageZone; paint(); } D.wasPage = pg; } /* (let down: the focus back where Down was pressed) */
  /* an episode held: as Play, its line running round it */
  if (D.epHold) { float k = (float)((app_now() - D.epHoldAt - 0.2) / 0.6); k = k < 0 ? 0 : k > 1 ? 1 : k; D.epK = k;
    if (k > 0 && !D.epSwell) { D.epSwell = 1; sound_play(SND_HOLD); }
    if (D.zone != 2 || D.epIdx >= D.neps || strcmp(js_str(D.eps[D.epIdx], "id", ""), D.epVid)) { D.epHold = 0; D.epK = 0; }
    else if (!app_ok_down()) { D.epHold = 0; D.epK = 0; if (k < 0.35f) play_best_vid(D.epVid); }
    else if (k >= 1) { D.epHold = 0; D.epK = 0; detail_open_streams(D.epVid); } }
  /* Play held: 0.2 s is still a tap, then its pill fills over 0.6 s and grows into "Hold for streams"; full, the list opens */
  if (D.playHold) { float k = (float)((app_now() - D.playHoldAt - 0.2) / 0.6); k = k < 0 ? 0 : k > 1 ? 1 : k;
    if (k > 0 && !D.holdSwell) { D.holdSwell = 1; sound_play(SND_HOLD); }
    if (D.zone != 0 || D.btnIdx >= D.nbtn || D.btn[D.btnIdx].kind != B_PLAY) D.playHold = 0;
    else if (!app_ok_down()) { D.playHold = 0; if (k < 0.35f) { fpill_press(&D.pill); play_best(); } } /* let go early: Play; past a third: let go, nothing */
    else if (k >= 1) { char vid[80]; D.playHold = 0; fpill_press(&D.pill); if (play_target(vid, sizeof vid)) detail_open_streams(vid); }
    tween_set(&D.hk, k); } else { tween_to(&D.hk, 0, 0.22f, EASE_IN_OUT); tween_tick(&D.hk, dt); }
  tween_to(&D.lblA, D.playHold && app_now() - D.playHoldAt > 0.2 ? 1 : 0, 0.1f, EASE_IN_OUT); tween_tick(&D.lblA, dt); /* the words the moment the hold is one */
  { double now = app_now(); float x = 0, fx = 0, fw = BD; int fi = D.zone == 0 || revpanel_page_btns() ? D.btnIdx : -1; /* (the reviews risen: it keeps its place, only dimmed while the reviews hold the focus) */
    dc_check();
    for (int i = 0; i < D.nbtn; i++) { Btn *b = &D.btn[i];
      int f = i == fi; if (f != b->f) { b->f = f; b->fAt = now; } float lw = DC.btnW[b->kind]; { const char *l_, *i_; float w_; if (b->kind == B_EYE && eye_ep(&l_, &i_, &w_)) lw = w_; } if (b->kind == B_PLAY && D.lblA.v > 0) lw += (hold_w() - lw) * D.lblA.v; float w = f ? BD + lw + 26 : BD;
      qt_to(&b->x, x, 0.3f); qt_tick(&b->x, dt); if (f) { fx = x; fw = w; } x += w + BG;
      if (f && now - b->fAt >= 0.06) tween_to(&b->la, 1, 0.18f, EASE_IN_OUT); else if (!f) tween_to(&b->la, 0, 0.18f, EASE_IN_OUT); /* the name: after a beat, quicker away */
      tween_to(&b->fb, f ? 1 : 0, 0.2f, EASE_IN_OUT); tween_tick(&b->la, dt); tween_tick(&b->fb, dt); }
    if (fi >= 0) fpill_to(&D.pill, fx, 0, fw, BD, GLIDE_DUR); else fpill_hide(&D.pill); }
  if (D.pend && tmdb_wait(&D.tw, D.p.it.type, ID(), 0, TMDB_WAIT_PAGE)) adopt();
  /* a translation that came late: applied in place (the details, the logo, the episodes' names) */
  if (D.meta && !D.locDone) { int st = tmdb_state(D.p.it.type, ID(), 0); const TmdbLoc *L = st == TMDB_READY ? tmdb_loc(D.p.it.type, ID(), 0) : NULL;
    if (L) { tmdb_apply_meta(D.meta, L); D.locDone = 1; D.ver++; logo_pick(); make_btns(); D.col = -1; paint(); } else if (st == TMDB_NONE || st == TMDB_OFF) D.locDone = 1; }
  if (tmdb_generation() != D.tgen) { D.tgen = tmdb_generation(); cJSON *mm = sb_meta_cached(D.p.it.type, ID()); if (mm && D.meta) { D.src = mm; if (D.pend) cJSON_Delete(D.pend); D.pend = cJSON_Duplicate(mm, 1); adopt(); } } /* a key added or removed: the details again */
  if (D.sPending && app_now() >= D.sT) { D.sPending = 0; build_eps(); paint(); }
  if (D.bgPending && app_now() >= D.bgT) bg_apply();
  { int rk = D.zone == 2 ? D.seasonIdx * 10000 + D.epIdx + 1 : 0; if (rk != D.epRestKey) { D.epRestKey = rk; D.epRestAt = app_now(); } }
}
/* ---- drawing ---- */
static void ptr_off(void) { if (topNav.active) { topNav.active = 0; } }
static void ppl_focus(int i, int b) { (void)b; ptr_off(); if (D.col >= 1) return; if (D.zone != 3 || D.pIdx != i) { flush_season(); if (D.zone != 3) { D.pRingEnter = 1; timer_start(&D.pRingPop, 0.2f); } D.zone = 3; D.pIdx = i; paint(); } }
static void btn_focus(int i, int b) { (void)b; ptr_off(); if (D.col >= 2) return; if (D.zone != 0 || D.btnIdx != i) { flush_season(); D.btnIdx = i; D.zone = 0; paint(); } }
static void season_focus(int i, int b) { (void)b; ptr_off(); if (D.zone != 1 || D.seasonIdx != i) { D.zone = 1; paint(); } (void)i; }
static void season_act(int i, int b) { (void)b; ptr_off(); D.zone = 1; if (i != D.seasonIdx) { flush_season(); D.seasonIdx = i; build_eps(); } paint(); }
static void ep_focus(int i, int b) { (void)b; ptr_off(); if (D.zone != 2 || D.epIdx != i) { D.zone = 2; D.epIdx = i; paint(); } }
static float bx_of(int i, int rtl, float lx) { return rtl ? lx - D.btn[i].x.v - BD : lx + D.btn[i].x.v; }
static void draw_btns(float lx, int rtl) {
  float nx = nudge_x(&D.nudge) * (D.zone == 0), ny = nudge_y(&D.nudge) * (D.zone == 0);
  /* the accent pill under the focused button (mirrored for right-to-left) */
  tween_to(&D.pgDim, revpanel_page_k() > 0 && !revpanel_page_btns() ? 1 : 0, 0.2f, EASE_IN_OUT); tween_tick(&D.pgDim, app_dt()); /* down in the reviews: the pill stays, dimmed */
  if (D.pgDim.v > 0.001f) gfx_opacity_push(1 - 0.6f * D.pgDim.v);
  if (D.pill.a.v > 0.001f) { float px = rtl ? lx - D.pill.x.v - D.pill.w.v : lx + D.pill.x.v; gfx_offset_push(px - D.pill.x.v + nx, ny); fpill_draw(&D.pill, -1, 0.96f); gfx_offset_pop();
    float hk = D.hk.v; if (hk > 0.001f && D.zone == 0) { float pw = D.pill.w.v, sw = pw * hk; gfx_clip_push(rtl ? px + nx + pw - sw : px + nx, ny, sw, BD); gfx_rrect(px + nx, ny, pw, BD, BD / 2, WHITE_A(70)); gfx_clip_pop(); } } /* (the hold filling it from the start: the pill's own shape, uncovered as it goes) */
  if (D.pgDim.v > 0.001f) gfx_opacity_pop();
  dc_check();
  for (int i = 0; i < D.nbtn; i++) { const Btn *b = &D.btn[i]; const char *lbl = DC.btnLbl[b->kind], *icon = DC.btnIcon[b->kind];
    float x = bx_of(i, rtl, lx), fo = b->f ? nx : 0, fy = b->f ? ny : 0, lw = DC.btnW[b->kind];
    if (b->kind == B_EYE) { float w_; if (eye_ep(&lbl, &icon, &w_)) lw = w_; } /* (an episode's reviews: its own Watched) */
    if (D.col < 2) { float hw = b->f ? BD + lw + 26 : BD; hit_add(rtl ? x + BD - hw : x, 0, hw, BD, btn_focus, NULL, i, 0); }
    int eyeOn = DC.wNow; { Item ep; if (b->kind == B_EYE && revpanel_page_episode(&ep)) eyeOn = lib_is_marked_watched(&ep); }
    uint32_t base = b->kind == B_PLAY ? acc_a(0.55f) : (b->kind == B_EYE && eyeOn) ? acc_a(0.4f) : WHITE_A(46);
    if (b->fb.v < 0.999f) gfx_circle(x + BD / 2 + fo, BD / 2 + fy, BD / 2, col_alpha(base, 1 - b->fb.v)); /* its own glass; the pill shows through once focused */
    uint32_t fg = b->f ? ACC_FG : WHITE_A(255);
    icon_draw(icon, rtl ? x + BD - 21 - 34 + fo : x + 21 + fo, 21 + fy, 34, fg);
    if (b->la.v > 0.001f) { float hk = b->kind == B_PLAY ? D.lblA.v : 0; gfx_opacity_push(b->la.v * (1 - hk)); text_draw(lbl, rtl ? x + BD - 66 + fo : x + 66 + fo, (BD - text_line_height(26)) / 2 + fy, 26, 600, fg, 0, 0, rtl ? ALIGN_RIGHT : ALIGN_LEFT); gfx_opacity_pop();
      if (hk > 0.001f) { gfx_opacity_push(b->la.v * hk); text_draw(T("Hold for streams"), rtl ? x + BD - 66 + fo : x + 66 + fo, (BD - text_line_height(26)) / 2 + fy, 26, 600, fg, 0, 0, rtl ? ALIGN_RIGHT : ALIGN_LEFT); gfx_opacity_pop(); } } }
  /* resting on Play with the best stream on: how to reach every stream, after the row */
  { int on = D.zone == 0 && (revpanel_page_k() <= 0 || revpanel_page_btns()) && D.btnIdx < D.nbtn && D.btn[D.btnIdx].kind == B_PLAY && best_on() && !D.playHold && D.hk.v < 0.01f && app_now() - D.btn[D.btnIdx].fAt > 1.2;
    tween_to(&D.hintA, on ? 1 : 0, on ? 0.4f : 0.2f, EASE_IN_OUT); tween_tick(&D.hintA, app_dt()); /* in slowly, away quickly, never cut */
    if (D.hintA.v > 0.002f) { const Btn *lb = &D.btn[D.nbtn - 1]; float end = lb->x.v + (lb->f ? BD + DC.btnW[lb->kind] + 26 : BD) + 28;
      gfx_opacity_push(D.hintA.v); gfx_offset_push((rtl ? 1 : -1) * 10 * (1 - D.hintA.v), 0); key_hint(rtl ? lx - end : lx + end, BD / 2, K_OK, T("Hold for streams"), WHITE_A(170), rtl); gfx_offset_pop(); gfx_opacity_pop(); } }
}
/* a film's page: Down for its reviews, at the foot in the middle (where the streams' peek was): a soft shade, the
   word spaced out, a thin chevron breathing downwards under it */
static void film_hint(void) {
  if (D.zone != D.zoneWas) { D.zoneWas = D.zone; D.zone0At = app_now(); }
  char im[32]; int on = !is_series() && D.zone == 0 && revpanel_page_k() <= 0 && !D.playHold && D.hk.v < 0.01f && !streams_open() && app_now() - D.zone0At > 1.0 && imdb_of(im, sizeof im)[0];
  tween_to(&D.revHintA, on ? 1 : 0, on ? 0.5f : 0.2f, EASE_IN_OUT); tween_tick(&D.revHintA, app_dt()); float a = D.revHintA.v; if (a <= 0.002f) return;
  static char tx[64]; static unsigned lg; if (lg != i18n_gen() || !tx[0]) { lg = i18n_gen(); snprintf(tx, sizeof tx, "%s", T("Reviews")); text_upper(tx, sizeof tx); }
  float t = (float)app_now(), b = 0.5f - 0.5f * cosf(t * 6.2832f / 1.6f), cx = SB_W / 2, y = SB_H - 112 + 10 * (1 - a);
  gfx_opacity_push(a);
  gfx_rrect_v(0, SB_H - 220, SB_W, 220, 0, BLACK_A(0), BLACK_A(150)); /* the foot darkened a little, for the words */
  text_draw(tx, cx, y, 20, 700, WHITE_A(225), 3.2f, 0, ALIGN_CENTER);
  float cy = y + text_line_height(20) + 12 + 6 * b; uint32_t c = WHITE_A((int)(150 + 105 * b)); /* the chevron: two strokes, down a little and brighter on each breath */
  for (int k = 0; k < 2; k++) { float s0 = k ? 1 : -1; for (int d = 0; d <= 10; d++) { float f = d / 10.0f; gfx_circle(cx + s0 * 14 * (1 - f), cy + 9 * f, 1.6f, c); } }
  gfx_opacity_pop();
}
static void draw_ep(int i, float x, float y) {
  const EpRec *r = ep_rec(i); if (!r) return;
  int unrel = r->unrel, w = r->w; const char *src = r->src;
  float TW = 589, TH = 331;
  /* stills cut to the card with their corners once (the plain image program draws them) */
  GLuint t = src[0] ? img_get_shaped(src, (int)TW, (int)TH, 18, 1) : 0; int fb = !src[0] || img_shaped_failed(src, (int)TW, (int)TH, 18, 1);
  if (ph_hold() && !fb) t = 0;
  float tf = t ? gfx_tex_fade(t) : 0, shade = 1;
  if (fb && !t) ph_surface(x, y, TW, TH, 18);
  else { ph_picture(t, x, y, TW, TH, 18, WHITE_A(255)); shade = tf; } /* waiting: the app's waiting look; the lower shade waits too */
  if (!t && fb) { const char *bu = DC.fb; GLuint bt = *bu ? img_get_shaped(bu, (int)TW, (int)TH, 18, 1) : 0;
    if (bt) gfx_image(bt, x, y, TW, TH, 0, WHITE_A(107), 0, 0, 1, 1); } /* the show's backdrop, dimmed */
  if (shade > 0.001f) gfx_rrect_v(x, y + TH * 0.55f, TW, TH * 0.45f, 0, BLACK_A(0), BLACK_A((int)(128 * shade)));
  if (unrel) { const char *tag = r->tag;
    /* the same dark glass as the episode hint: a small accent dot, then the words, readable on any still */
    float tw = r->tagW, th = 14 + text_line_height(22), tx = app_rtl() ? x + TW - 14 - tw : x + 14, ty0 = y + 14;
    gfx_shadow(tx, ty0 + 4, tw, th, th / 2, 18, BLACK_A(110)); gfx_rrect(tx, ty0, tw, th, th / 2, RGBA(14, 12, 20, 178)); gfx_rrect_v(tx, ty0, tw, th, th / 2, WHITE_A(26), WHITE_A(6)); gfx_ring(tx, ty0, tw, th, th / 2, 1, WHITE_A(46));
    gfx_circle(tx + 22, ty0 + th / 2, 5, ACC2); text_draw(tag, tx + 36, ty0 + 7, 22, 600, WHITE_A(245), 0.2f, 0, ALIGN_LEFT); }
  if (w) { float bx = app_rtl() ? x + 12 : x + TW - 12 - 42; gfx_shadow(bx, y + 14, 42, 42, 21, 10, BLACK_A(140)); gfx_circle(bx + 21, y + 12 + 21, 21, acc_a(0.96f)); icon_draw("check", bx + 8, y + 12 + 8, 26, WHITE_A(255)); }
  else if (r->res && D.resPct > 0.01f && D.resPct < 0.97f) { float bw = TW - 28, pw = bw * D.resPct; gfx_rrect(x + 14, y + TH - 13 - 6, bw, 6, 3, WHITE_A(61)); gfx_rrect(app_rtl() ? x + 14 + bw - pw : x + 14, y + TH - 13 - 6, pw, 6, 3, ACC3); } /* inset and rounded */
  const char *title = r->title;
  float ty = y + TH + 18; int al = app_rtl() ? ALIGN_RIGHT : ALIGN_LEFT; float tx = app_rtl() ? x + TW : x;
  text_draw(title, tx, ty, 31, 700, w ? ACC2 : WHITE_A(255), 0, TW, al); ty += text_line_height(31) + 5;
  const char *info = r->info;
  if (info[0]) { text_draw(info, tx, ty, 24, 600, WHITE_A(184), 0, TW, al); ty += text_line_height(24); }
  const char *ov = r->ov;
  if (*ov) text_draw_wrapped(ov, tx, ty + 8 + (34 - text_line_height(25)) / 2, TW, 3, 25, 400, WHITE_A(196), 0, al, 34);
}
/* best stream on, the focus resting 3 s on an episode: a small glass chip on its still says Down lists all its streams;
   it fades and rises in softly, then breathes very gently and a faint sheen crosses it now and then, for as long as it shows */
static void ep_hint(float x, float y, int rtl) {
  double t = app_now() - D.epRestAt - 1; if (t <= 0) return; /* (a second on it: Down shows its reviews) */ float a = (float)(t / 0.5); a = a > 1 ? 1 : 1 - (1 - a) * (1 - a) * (1 - a);
  static char tx[2][96]; static float tw[2]; static unsigned lg; if (lg != i18n_gen() || !tx[0][0]) { lg = i18n_gen(); snprintf(tx[0], sizeof tx[0], "%s", T("Episode reviews")); snprintf(tx[1], sizeof tx[1], "%s", T("Hold for streams"));
    for (int i = 0; i < 2; i++) tw[i] = text_width(tx[i], 22, 600, 0.2f) + 30; } /* (the key is drawn on its own, moving) */
  /* best stream on: the two ways on, in turn every 3.5 s (OK held for its streams first, then Down for its reviews), crossfading */
  float m = 0; if (best_on()) { float ph = (float)fmod(t, 7.0); m = ph < 3.15f ? 0 : ph < 3.5f ? (ph - 3.15f) / 0.35f : ph < 6.65f ? 1 : 1 - (ph - 6.65f) / 0.35f; m = m * m * (3 - 2 * m); m = 1 - m; } /* (Hold for streams first, then Episode reviews) */
  float h = 14 + text_line_height(22), w = tw[0] + (tw[1] - tw[0]) * m + 30, bx = rtl ? x + 16 : x + 589 - 16 - w, by = y + 331 - 30 - h + 6 * (1 - a);
  float br = 0.5f - 0.5f * cosf((float)(t * 6.2832 / 3.6)); /* the breath: 3.6 s, slow */
  gfx_opacity_push(a * (0.9f + 0.1f * br));
  gfx_shadow(bx, by + 4, w, h, h / 2, 18, BLACK_A(110)); gfx_rrect(bx, by, w, h, h / 2, RGBA(14, 12, 20, 178)); gfx_rrect_v(bx, by, w, h, h / 2, WHITE_A(26), WHITE_A(6)); gfx_ring(bx, by, w, h, h / 2, 1, WHITE_A(46)); /* dark enough to read on any still */
  { float sp = (float)fmod(t, 5.0) / 1.4f; if (sp < 1) { float sx = bx - 40 + (w + 80) * sp; gfx_clip_push(bx, by, w, h); gfx_rrect_h(sx - 30, by, 30, h, 0, WHITE_A(0), WHITE_A(22)); gfx_rrect_h(sx, by, 30, h, 0, WHITE_A(22), WHITE_A(0)); gfx_clip_pop(); } } /* the sheen: every 5 s */
  gfx_clip_push(bx, by, w, h);
  if (m < 0.999f) { gfx_opacity_push(1 - m); float bob = 3.5f * sinf((float)(t * 6.2832 / 1.4)); icon_draw("k_down", bx + 14, by + h / 2 - 11 + bob, 22, WHITE_A(255)); /* the arrow: a soft bob downwards, 1.4 s */
    text_draw(tx[0], bx + 15 + 30, by + 7, 22, 600, WHITE_A(245), 0.2f, 0, ALIGN_LEFT); gfx_opacity_pop(); }
  if (m > 0.001f) { gfx_opacity_push(m); float pr = 0.92f + 0.08f * (0.5f + 0.5f * cosf((float)(t * 6.2832 / 1.2))); gfx_scale_push(pr, pr, bx + 25, by + h / 2); icon_draw("k_ok", bx + 14, by + h / 2 - 11, 22, WHITE_A(255)); gfx_scale_pop(); /* OK: a slow press */
    text_draw(tx[1], bx + 15 + 30, by + 7, 22, 600, WHITE_A(245), 0.2f, 0, ALIGN_LEFT); gfx_opacity_pop(); }
  gfx_clip_pop();
  gfx_opacity_pop();
}
/* a face that is still on its way: the app's waiting look, round */
static void face_wait(float cx, float cy) { ph_circle(cx, cy, 62); }
/* no photo anywhere: a quiet silhouette */
static void face_none(float x, float y) {
  gfx_circle(x + 62, y + 62, 62, RGB(0x24, 0x21, 0x2e)); gfx_circle(x + 62, y + 48, 21, WHITE_A(41));
  gfx_clip_push(x, y, 124, 112); gfx_rrect(x + 22, y + 78, 80, 70, 40, WHITE_A(41)); gfx_clip_pop();
}
static float OUTK; /* Back: how far the page is through leaving (0 while it is the page) */
static void draw(void) {
  int rtl = app_rtl(); float X0 = 48, X1 = SB_W - 48, W = X1 - X0; int al = rtl ? ALIGN_RIGHT : ALIGN_LEFT; float lx = rtl ? X1 : X0;
  float y = 100; double now = app_now();
  float PK = revpanel_page_k(), pbY = 0; if (PK > 0.001f) { gfx_opacity_push(1 - PK); gfx_offset_push(0, -220 * PK); } /* the reviews rising: the page goes up and away, its buttons stay */
  /* the cover: nothing in its place while it loads, then it fades in (one already in memory is simply there) */
  dc_check();
  if (D.posterA.v > 0.001f) { const char *src = DC.cover;
    GLuint t = src && *src ? img_get_shaped(src, 330, 495, 16, 0) : 0; if (ph_hold()) t = 0;
    if (t && !D.posterSeen) { D.posterSeen = 1; D.posterLate = 1; timer_start(&D.posterIn, 0.35f); } /* the cover always fades in (it is not the card's poster, so it does not move across) */
    float px = rtl ? X0 : X1 - 330, py = 130, pin = !t ? 0 : OUTK > 0 ? 1 - OUTK : D.posterIn.on ? timer_x(&D.posterIn) : 1; pin = pin < 0.5f ? 2 * pin * pin : 1 - powf(-2 * pin + 2, 2) / 2;
    /* late (half a second in): it will fade in in place, no slide */
    if (!t && src && *src && now - D.openAt > 0.5 && !img_shaped_failed(src, 330, 495, 16, 0)) D.posterPh = 1;
    int wait = 0; /* (no waiting look: a late cover simply fades in, in its place) */
    if (t || wait) { gfx_opacity_push(D.posterA.v); gfx_offset_push(D.posterX.v * (rtl ? -1 : 1), 0); gfx_scale_push(D.posterS.v, D.posterS.v, px + 165, py + 247);
      float slide = D.posterPh ? 0 : 40; /* late (its placeholder was up): it fades in place over it; on time: it slides in a little as the page opens */
      if (t) { gfx_opacity_push(pin); gfx_shadow(px + (rtl ? -1 : 1) * slide * (1 - pin), py + 18, 330, 495, 16, 50, BLACK_A(191)); gfx_opacity_pop(); } /* under the placeholder: it never darkens it */
      if (wait) { if (t) gfx_opacity_push(1 - pin); ph_box(px, py, 330, 495, 16); if (t) gfx_opacity_pop(); }
      if (t) { float cdx = (rtl ? -1 : 1) * slide * (1 - pin); gfx_opacity_push(pin); gfx_image(t, px + cdx, py, 330, 495, 0, WHITE_A(255), 0, 0, 1, 1); gfx_opacity_pop(); } /* its own picture: it slides in a little from the edge as it fades in */
      gfx_scale_pop(); gfx_offset_pop(); gfx_opacity_pop(); } }
  /* head: the logo or the name (which waits unseen up to 0.9 s for the logo), the meta line */
  int drewLogo = 0; float nameH = (DC.nameN ? DC.nameN : 1) * 78 * 1.04f;
  if (D.logoUrl[0]) { float asp; GLuint t = img_get(D.logoUrl, 520, 175, &asp);
    if (t) { if (!D.logoSeen) { D.logoSeen = 1; if (now - D.openAt > 0.05) timer_start(&D.logoIn, 0.3f); } /* a logo that came late fades in */
      float w = 520, h = w / asp; if (h > 175) { h = 175; w = h * asp; } float la = D.logoIn.on ? 1 - powf(1 - timer_x(&D.logoIn), 3) : 1; { char ak[80]; snprintf(ak, sizeof ak, "logo:%s", ID()); if (sx_arrived(ak)) la = 1; } /* it came over from the last page: no fade of its own */
      { char sk[80]; snprintf(sk, sizeof sk, "logo:%s", ID()); sx_image(sk, t, rtl ? lx - w : lx, y, w, h, 0, WHITE_A((int)(255 * la)), 0, 0, 1, 1); } y += h; drewLogo = 1; }
    else if (img_failed(D.logoUrl)) D.logoUrl[0] = 0;
    else if (now - D.openAt < 0.9) { y += nameH; drewLogo = 1; } } /* waiting for the text art: the name's place is kept, empty (it fades in when it comes) */
  if (!drewLogo) { char sk[80]; snprintf(sk, sizeof sk, "logo:%s", ID()); int n = sx_text_wrapped(sk, DC.name, lx, y, W * 0.7f, 2, 78, 800, WHITE_A(255), -1.6f, al, 78 * 1.04f); y += n * 78 * 1.04f; }
  y += 22;
  meta_draw_p(&DC.mp[drewLogo ? 1 : 0], lx, y, WHITE_A(230), al); y += text_line_height(27);
  /* the facts: the awards in gold, the country */
  if (D.nfacts) { y += 12; float fx = lx, fy = y, flh = text_line_height(24);
    for (int i = 0; i < 2; i++) { if (!D.facts[i][0]) continue; uint32_t c = i == 0 ? RGB(0xf5, 0xc5, 0x18) : WHITE_A(204); float iw = 24 + 8;
      icon_draw(i == 0 ? "d_award" : "d_country", rtl ? fx - 24 : fx, fy + flh / 2 - 12, 24, i == 0 ? c : WHITE_A(166));
      float tw = text_draw(D.facts[i], rtl ? fx - iw : fx + iw, fy, 24, i == 0 ? 600 : 500, c, 0, 0, al); fx += (rtl ? -1 : 1) * (iw + tw + 30); }
    y += flh; }
  /* more: the synopsis, then the people; they wait for the details and fade up together */
  float moreTop = y;
  if (D.moreA.v > 0.001f && !D.moreWait) { float mk = OUTK > 0 ? (1 - OUTK) * (1 - OUTK) : D.moreIn.on ? 1 - powf(1 - timer_x(&D.moreIn), 3) : 1;
    gfx_opacity_push(D.moreA.v * mk); gfx_offset_push(0, D.moreY.v + 10 * (1 - mk)); float yy = y + 20;
    const char *desc = DC.desc; float dh = DC.descH; if (*desc) { text_draw_wrapped(desc, lx, yy + 22 + (DESC_LH - text_line_height(28)) / 2, W * 0.68f, 3, 28, 400, WHITE_A(219), 0, al, DESC_LH); yy += 22 + dh; }
    if (D.nppl) { float py = yy + 16; D.pplY = py + D.moreY.v; float rk = D.pRingPop.on ? ring_pop_k(timer_x(&D.pRingPop)) : 1;
      float pk = 1; /* (the faces come in one by one now, below) */
      if (!D.facesShown) { D.facesShown = 1; timer_start(&D.faceIn, 0.5f); }
      int nv = D.nppl < 10 ? D.nppl : 10; /* leaving: the same sweep backwards, the last face in view first */
      float FK[MAXPPL]; for (int i = 0; i < D.nppl && i < MAXPPL; i++) { float t = OUTK > 0 ? ((1 - OUTK) * (0.24f + 0.035f * (nv - 1)) - (i < nv ? i : nv - 1) * 0.035f) / 0.24f : D.faceIn.on ? (timer_x(&D.faceIn) * 0.5f - i * 0.035f) / 0.24f : 1; t = t < 0 ? 0 : t > 1 ? 1 : t; FK[i] = 1 - (1 - t) * (1 - t) * (1 - t); } /* one quick sweep from the side, 35 ms apart */
      /* in passes (the placeholders, the faces, the names, the ring): each pass is one batch */
      float nx = nudge_x(&D.nudge), ny = nudge_y(&D.nudge);
      #define PX(i) ((rtl ? lx - (i) * PP_STEP - 140 : lx + (i) * PP_STEP) + (rtl ? 1 : -1) * 36 * (1 - FK[i]))
      #define NX(i) (D.zone == 3 && (i) == D.pIdx ? nx : 0)
      #define NY(i) (D.zone == 3 && (i) == D.pIdx ? ny : 0)
      /* each face: its placeholder or photo, grown a little and lit with the focus (one state per face), then its name */
      int pf = D.zone == 3 && D.pIdx < D.nppl ? D.pIdx : -1; (void)rk;
      for (int pass = 0; pass < 2; pass++) for (int i = 0; i < D.nppl; i++) { if ((i == pf) != pass) continue; Person *p = &D.ppl[i];
        float fx = PX(i) + 8 + NX(i), fy = py + NY(i); unsigned long long lk = lift_key(D.ppl, i);
        if (FK[i] < 0.999f) gfx_opacity_push(FK[i]);
        lift_begin(lk, pass, 0, fx, fy, 124, 124, 62);
        int failed = p->img[0] && img_shaped_failed(p->img, 124, 124, 62, 2); GLuint t = p->img[0] && !failed ? img_get_shaped(p->img, 124, 124, 62, 2) : 0;
        if (p->settled && (!p->img[0] || failed)) face_none(fx, fy);
        else if (p->a.v < 0.999f || !t || ph_hold()) face_wait(fx + 62, fy + 62);
        if (t && !ph_hold()) { if (!p->a.on && p->a.to < 1) tween_to(&p->a, 1, 0.3f, EASE_IN_OUT); /* the photo fades in */
          gfx_image(t, fx, fy, 124, 124, 0, WHITE_A((int)(255 * p->a.v)), 0, 0, 1, 1); }
        lift_end(lk, fx, fy, 124, 124, 62);
        if (FK[i] < 0.999f) gfx_opacity_pop();
        if (D.col < 1) hit_add(PX(i), py, 140, 124 + 12 + 58, ppl_focus, NULL, i, 0); }
      for (int i = 0; i < D.nppl; i++) { float ny_ = 0;
        text_draw_wrapped(D.ppl[i].name, PX(i) + 70 + NX(i), py + 124 + 12 + NY(i) + ny_, 140, 2, 24, 600, i == pf ? WHITE_A((int)(255 * FK[i])) : WHITE_A((int)(219 * FK[i])), 0, ALIGN_CENTER, 29); }

      #undef PX
      #undef NX
      #undef NY
      if (pk < 1) { gfx_offset_pop(); gfx_opacity_pop(); }
    }
    gfx_offset_pop(); gfx_opacity_pop(); }
  /* the lower block: buttons, seasons, episodes; it slides up over the synopsis */
  float ly = moreTop + more_h() + more_after() + 34 + D.lowerY.v; D.btnsY0 = ly;
  gfx_offset_push(0, ly);
  pbY = ly + D.btnsY.v - 220 * PK;
  if (D.btnsA.v > 0.001f && (PK <= 0.001f || D.pageZone == 2)) { gfx_opacity_push(D.btnsA.v); gfx_offset_push(0, D.btnsY.v); draw_btns(lx, rtl); gfx_offset_pop(); gfx_opacity_pop(); } /* (risen from an episode: the row also in its place, coming back with the page) */
  if (is_series() && D.nseasons) {
    float sy = BD + 30, sh = 60; float cx = 0, xs[MAXSEASON], ws[MAXSEASON], tot = 0;
    for (int i = 0; i < D.nseasons; i++) { int f = D.zone == 1 && i == D.seasonIdx; ws[i] = DC.sW[i][f] + 60; xs[i] = cx; cx += ws[i] + 12; } tot = cx - 12;
    float vw = W < 1060 ? W : 1060, want = 0; if (tot > vw) { float c = xs[D.seasonIdx] + ws[D.seasonIdx] / 2 - vw / 2; want = c < 0 ? 0 : c > tot - vw ? tot - vw : c; }
    qt_to(&D.seasonsX, roundf(want), 0.24f);
    gfx_clip_push(rtl ? X1 - vw : X0, sy, vw, sh); gfx_offset_push(nudge_x(&D.nudge) * (D.zone == 1), nudge_y(&D.nudge) * (D.zone == 1));
    float h = 13 * 2 + text_line_height(27);
    /* the highlight: it appears in place as the row takes the focus, then glides (and widens) from pill to pill */
    if (D.zone == 1) { if (!D.sPillOn) { fpill_snap(&D.sPill); D.sPillOn = 1; } fpill_to(&D.sPill, xs[D.seasonIdx], 0, ws[D.seasonIdx], h, GLIDE_DUR); } else if (D.sPillOn) { D.sPillOn = 0; fpill_hide(&D.sPill); }
    /* pills running past either end fade out there (soft edges, not a cut), only on a side with more beyond */
    float vx0 = rtl ? X1 - vw : X0, vx1 = vx0 + vw, more0 = D.seasonsX.v > 2, more1 = D.seasonsX.v < tot - vw - 2;
    #define EDGE_A(x, w) ((more0 ? clampf(((x) + (w) - vx0) / 110.0f, 0, 1) : 1) * (more1 ? clampf((vx1 - (x)) / 110.0f, 0, 1) : 1))
    for (int i = 0; i < D.nseasons; i++) { /* the plates first */
      int cur = i == D.seasonIdx; float x = rtl ? X1 - (xs[i] - D.seasonsX.v) - ws[i] : X0 + xs[i] - D.seasonsX.v, ea = EDGE_A(x, ws[i]); if (ea < 0.01f) continue;
      hit_add(x, sy, ws[i], h, season_focus, season_act, i, 0);
      if (ea < 1) gfx_opacity_push(ea); gfx_rrect(x, sy, ws[i], h, 30, cur && D.zone != 1 ? acc_a(0.38f) : WHITE_A(26)); gfx_ring(x, sy, ws[i], h, 30, 1, WHITE_A(23)); if (ea < 1) gfx_opacity_pop(); }
    if (D.sPill.a.v > 0.001f) { float pw = D.sPill.w.v, px = rtl ? X1 - (D.sPill.x.v - D.seasonsX.v) - pw : X0 + D.sPill.x.v - D.seasonsX.v;
      gfx_opacity_push(D.sPill.a.v); gfx_rrect(px, sy, pw, h, 30, ACC); gfx_rrect_h(px, sy, pw, h, 30, WHITE_A(0), WHITE_A(56)); gfx_ring_out(px, sy, pw, h, 30, 4, acc_a(0.34f)); gfx_opacity_pop(); }
    for (int i = 0; i < D.nseasons; i++) { const char *lbl = DC.sLbl[i];
      int f = D.zone == 1 && i == D.seasonIdx, cur = i == D.seasonIdx; float x = rtl ? X1 - (xs[i] - D.seasonsX.v) - ws[i] : X0 + xs[i] - D.seasonsX.v, ea = EDGE_A(x, ws[i]); if (ea < 0.01f) continue;
      if (ea < 1) gfx_opacity_push(ea); text_draw(lbl, x + ws[i] / 2, sy + 13, 27, f ? 700 : 600, f ? ACC_FG : cur ? WHITE_A(255) : WHITE_A(209), 0, 0, ALIGN_CENTER); if (ea < 1) gfx_opacity_pop(); }
    #undef EDGE_A
    gfx_offset_pop(); gfx_clip_pop();
    /* more seasons that way: arrows at the edges */
    float ml = D.seasonsX.v > 2 ? 1 : 0, mr = D.seasonsX.v < tot - vw - 2 ? 1 : 0;
    if (ml) text_draw("\xe2\x80\xb9", rtl ? X1 - 2 : X0 + 2, sy - 2, 44, 300, WHITE_A(255), 0, 0, rtl ? ALIGN_RIGHT : ALIGN_LEFT);
    if (mr) text_draw("\xe2\x80\xba", rtl ? X1 - vw + 2 : X0 + vw - 2, sy - 2, 44, 300, WHITE_A(255), 0, 0, rtl ? ALIGN_LEFT : ALIGN_RIGHT);
    /* the episodes */
    float ey = sy + sh + 24; gfx_clip_push(X0 - 40, ey - 30, W + 80, 604); /* room for the focused still to grow, and for the hold line around it */
    float ia = D.epsIn.on ? 1 - powf(1 - timer_x(&D.epsIn), 3) : 1; float ix = 34 * (1 - ia) * (rtl ? -1 : 1);
    gfx_opacity_push(D.epsStale ? 0.25f : ia); gfx_offset_push(ix + nudge_x(&D.nudge) * (D.zone == 2), nudge_y(&D.nudge) * (D.zone == 2));
    int i0 = (int)(-D.epsX.v / EP_W) - 1, i1 = i0 + 6; if (i0 < 0) i0 = 0; if (i1 > D.neps - 1) i1 = D.neps - 1;
    int fe = D.zone == 2 ? D.epIdx : -1; float hp = D.zone == 2 ? (D.epHold ? D.epK : app_hold_progress()) : 0;
    for (int pass = 0; pass < 2; pass++) for (int i = i0; i <= i1; i++) { if ((i == fe) != pass) continue; /* the focused episode last, over its neighbours as it grows */
      float x = X0 + i * EP_W + D.epsX.v; if (rtl) x = SB_W - x - 589; unsigned long long lk = lift_key(D.eps, i);
      lift_begin_z(lk, pass, pass ? hp : 0, x, ey + 12, 589, 331, 18, 0.1f); draw_ep(i, x, ey + 12); /* episodes sit close together: a clearer zoom */
      if (pass && hp > 0) hold_trace_draw(x, ey + 12, 589, 331, 18, ease_apply(EASE_IN_OUT, hp));
      if (pass && hp <= 0) ep_hint(x, ey + 12, rtl); /* (Down: its reviews) */
      lift_end(lk, x, ey + 12, 589, 331, 18); hit_add(x, ey + 12, 589, 331 + 18 + text_line_height(31), ep_focus, NULL, i, 0); }
    gfx_offset_pop(); gfx_opacity_pop(); gfx_clip_pop();
  }
  gfx_offset_pop();
  film_hint();
  if (PK > 0.001f) { gfx_offset_pop(); gfx_opacity_pop(); /* the buttons glide up to the top, over a darker page for the reviews */
    gfx_rect(0, 0, SB_W, SB_H, BLACK_A((int)(150 * PK)));
    if (D.pageZone == 2) { gfx_opacity_push(PK); gfx_offset_push(0, 56 - 12 * (1 - PK)); draw_btns(lx, rtl); gfx_offset_pop(); gfx_opacity_pop(); } /* from an episode: the row at the top fades in and out in place (the page's own one comes back below) */
    else { float by = pbY + 220 * PK; by += (56 - by) * PK; gfx_offset_push(0, by); draw_btns(lx, rtl); gfx_offset_pop(); } }
}
/* where the next screen grows out of: the focused button, or the focused face */
static int origin(float *x, float *y) {
  if (topNav.active) return 0;
  int rtl = app_rtl(); float lx = rtl ? SB_W - 48 : 48;
  if (D.zone == 3 && D.pIdx < D.nppl) { float cx = rtl ? lx - D.pIdx * PP_STEP - 140 : lx + D.pIdx * PP_STEP; *x = cx + 70; *y = D.pplY + 62; return 1; }
  if (D.zone == 0 && D.btnIdx < D.nbtn) { float cx = D.pill.x.v + D.pill.w.v / 2; *x = rtl ? lx - cx : lx + cx; *y = D.btnsY0 + BD / 2; return 1; }
  return 0;
}
/* Back: the page once more over the one it returns to, its entrance backwards: the cover slides and fades out, the
   faces sweep out, the words fade, all of it fading as it came (the logo flies back on its own) */
static void out(float k) { OUTK = k > 0.0001f ? k : 0.0001f; gfx_opacity_push(ease_apply(EASE_CSS_EASEOUT, 1 - k)); draw(); gfx_opacity_pop(); OUTK = 0; }
/* every move on the page taps: the buttons, the faces, the seasons, the episodes (the focus point does not always move) */
static int key(int k) {
  int z = D.zone, b = D.btnIdx, p = D.pIdx, se = D.seasonIdx, e = D.epIdx, r = key_in(k);
  if (z != D.zone || b != D.btnIdx || p != D.pIdx || se != D.seasonIdx || e != D.epIdx) sound_focus();
  return r;
}
static const Item *focused_menu(void) { return best_on() && D.zone == 2 ? NULL : focused(); } /* best stream on: holding an episode is its streams, not its menu */
Screen detailScreen = { enter, leave, key, tick, draw, focused_menu, open_ep, NULL, origin, out };
