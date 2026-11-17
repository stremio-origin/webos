#include "screens/player.h"
#include "core/traktwatch.h"
#include "screens/board.h"
#include "core/scrobble.h"
#include "core/live.h"
#include "screens/livepeek.h"
#include "screens/livehub.h"
#include "ui/spoil.h"
#include "ui/sound.h"
#include "core/app.h"
#include "core/keys.h"
#include "core/i18n.h"
#include "core/json.h"
#include "core/net.h"
#include "core/store.h"
#include "core/subs.h"
#include "core/forcedsubs.h"
#include "core/video.h"
#include "ui/shared.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include "ui/img.h"
#include "ui/icon.h"
#include "ui/widgets.h"
#include "ui/prompts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>
#include <time.h>
#include "core/p2p.h"
#include "core/waitlines.h"
#include "platform/plat.h"
#include "ui/xline.h"
#include "core/prevspike.h"
#include "core/seekprev.h"
#include "core/mkvsubs.h"
#include "core/best.h"
#include "core/trailer.h"
#include "core/tmdb.h"
#include "core/audorig.h"
#include "core/parental.h"
#include "ui/glass.h"
#include "core/rating.h"
#include "core/hproxy.h"
#include "core/mp4chap.h"
#include "core/coll.h"
#include "core/addons.h"
#include "core/subsync.h"
#include "core/prevsrc.h"
#include "ui/subfonts.h"
#include "ui/asssubs.h"

/* ---- the stream's text, read for the info chips: resolution, HDR, audio ---- */
static void lower(const char *s, char *out, size_t cap) { size_t i = 0; for (; s[i] && i + 1 < cap; i++) out[i] = (char)tolower((unsigned char)s[i]); out[i] = 0; }
static int has_word(const char *s, const char *w) { const char *p = s; size_t n = strlen(w); while ((p = strstr(p, w))) { int a = p == s || !isalnum((unsigned char)p[-1]), b = !isalnum((unsigned char)p[n]); if (a && b) return 1; p += n; } return 0; }
static const char *res_of(const char *label) {
  char s[640]; lower(label, s, sizeof s);
  if (strstr(s, "2160p") || has_word(s, "4k") || has_word(s, "uhd")) return "4K"; if (strstr(s, "1440p") || has_word(s, "2k")) return "1440p"; if (strstr(s, "1080p") || has_word(s, "fhd")) return "1080p";
  if (strstr(s, "720p")) return "720p"; if (strstr(s, "576p") || strstr(s, "480p") || has_word(s, "sd")) return "SD"; return "";
}
static const char *res_of_video(int w, int h) { if (!w || !h) return ""; if (w >= 3200 || h >= 1800) return "4K"; if (w >= 2400 || h >= 1300) return "1440p"; if (w >= 1700 || h >= 950) return "1080p"; if (w >= 1150 || h >= 650) return "720p"; return "SD"; }
static void hdr_of(const char *label, char *out, size_t cap) {
  char s[640]; lower(label, s, sizeof s); for (char *c = s; *c; c++) if (*c == '.' || *c == '_') *c = ' ';
  int dv = has_word(s, "dv") || has_word(s, "dovi") || strstr(s, "dolby vision"); out[0] = 0;
  if (dv) snprintf(out, cap, "DV");
  const char *h = strstr(s, "hdr10+") || strstr(s, "hdr10 plus") || has_word(s, "hdr10p") ? "HDR10+" : has_word(s, "hdr10") ? "HDR10" : (has_word(s, "hdr") && !dv) ? "HDR" : has_word(s, "hlg") ? "HLG" : "";
  if (*h) { size_t l = strlen(out); snprintf(out + l, cap - l, "%s%s", l ? " · " : "", h); }
}
static void audio_of(const char *label, char *out, size_t cap) {
  char s[640]; lower(label, s, sizeof s); for (char *c = s; *c; c++) if (*c == '.' || *c == '_') *c = ' ';
  const char *au = "";
  if (strstr(s, "truehd")) au = "TrueHD"; else if (strstr(s, "dts-x") || strstr(s, "dts x") || strstr(s, "dtsx")) au = "DTS:X"; else if (strstr(s, "dts-hd ma") || strstr(s, "dts-hd-ma") || strstr(s, "dts hd ma") || strstr(s, "dts-ma") || strstr(s, "dtshd ma")) au = "DTS-HD MA";
  else if (strstr(s, "dts-hd") || strstr(s, "dts hd") || strstr(s, "dtshd")) au = "DTS-HD"; else if (has_word(s, "dts")) au = "DTS";
  else if (has_word(s, "ddp") || strstr(s, "dd+") || strstr(s, "e-ac-3") || strstr(s, "e-ac3") || strstr(s, "eac3") || strstr(s, "dolby digital plus")) au = "EAC3"; else if (has_word(s, "dd") || strstr(s, "ac-3") || has_word(s, "ac3") || strstr(s, "dolby digital")) au = "AC3";
  else if (has_word(s, "aac") || strstr(s, "aac2")) au = "AAC"; else if (has_word(s, "opus")) au = "Opus"; else if (has_word(s, "flac")) au = "FLAC"; else if (has_word(s, "mp3")) au = "MP3";
  char ch[8] = ""; for (const char *p = s; *p; p++) if (p[0] >= '1' && p[0] <= '9' && (p[1] == '.' || p[1] == ' ') && (p[2] == '0' || p[2] == '1') && !isdigit((unsigned char)p[3]) && (p == s || (!isdigit((unsigned char)p[-1]) && p[-1] != '.'))) { snprintf(ch, sizeof ch, "%c.%c", p[0], p[2]); break; }
  int atmos = strstr(s, "atmos") != NULL;
  if (*au && atmos) snprintf(out, cap, "%s Atmos%s%s", au, ch[0] ? " " : "", ch); else if (!*au && atmos) snprintf(out, cap, "Atmos"); else if (*au) snprintf(out, cap, "%s%s%s", au, ch[0] ? " " : "", ch); else out[0] = 0;
}
static void fmt_time(double s, char *out, size_t cap) { int t = s < 0 ? 0 : (int)s, h = t / 3600, m = (t % 3600) / 60, x = t % 60; if (h) snprintf(out, cap, "%d:%02d:%02d", h, m, x); else snprintf(out, cap, "%d:%02d", m, x); }
static const char *lang_name(const char *c) { const char *n = subs_lang_name(c); return n ? T(n) : ""; }
#define lang_match subs_lang_match

/* ---- motion: the page's CSS transitions (duration, cubic-bezier, delay), retargeted from where they are ---- */
static const float C_EASE[4] = { .25f, .1f, .25f, 1 }, C_OUT[4] = { .2f, .8f, .2f, 1 }, C_SEEK[4] = { .23f, 1, .32f, 1 }, C_TRIN[4] = { .2f, .9f, .25f, 1 },
  C_TROUT[4] = { .4f, 0, 1, 1 }, C_LGIN[4] = { .4f, 0, .2f, 1 }, C_INOUT[4] = { .42f, 0, .58f, 1 };
static float bez(const float *c, float x) {
  if (x <= 0) return 0; if (x >= 1) return 1; if (!c) return x;
  float t = x; for (int i = 0; i < 8; i++) { float u = 1 - t, b = 3 * u * u * t * c[0] + 3 * u * t * t * c[2] + t * t * t - x, d = 3 * u * u * c[0] + 6 * u * t * (c[2] - c[0]) + 3 * t * t * (1 - c[2]);
    if (fabsf(b) < 1e-4f || fabsf(d) < 1e-6f) break; t -= b / d; if (t < 0) t = 0; if (t > 1) t = 1; }
  float u = 1 - t; return 3 * u * u * t * c[1] + 3 * u * t * t * c[3] + t * t * t;
}
typedef struct { float from, to, v, t, dur; const float *c; int on; } Mo;
static void mo_set(Mo *m, float v) { m->from = m->to = m->v = v; m->on = 0; }
static void mo_to(Mo *m, float to, float dur, const float *c, float delay) {
  if (m->to == to && (m->on || m->v == to)) return;
  m->from = m->v; m->to = to; dur *= ANIM_SPEED; delay *= ANIM_SPEED; m->t = -delay; m->dur = dur > 0 ? dur : 1e-4f; m->c = c; m->on = 1; if (dur <= 0 && delay <= 0) { m->v = to; m->on = 0; }
}
static void mo_tick(Mo *m, float dt) { if (!m->on) return; m->t += dt; if (m->t < 0) return; float x = m->t / m->dur; if (x >= 1) { m->v = m->to; m->on = 0; return; } m->v = m->from + (m->to - m->from) * bez(m->c, x); }
/* a keyframe entrance started at `at` (0 = none running): 0..1 */
static float kf(double at, float dur, const float *c, float delay) { if (!at) return 1; return bez(c, (float)((app_now() - at - delay * ANIM_SPEED) / (dur * ANIM_SPEED))); }

/* ---- state ---- */
#define MAXBTN 8
#define MAXROW 64
typedef struct { int k; /* 0 note 1 skel 2 ep 3 src 4 chip; ls: a subtitle row's list, 1 OpenSubtitles 2 the addons' */ char lb[200], sub[120], ov[300], th[512], vl[48]; int on, spin, tool, hid, grey; int i, ls; const cJSON *v; Stream *st; char u[1024]; float d, hh, ww, vw; uint32_t sw; int vwt; } TItem; /* vwt: the value's weight (0: 700), a font row's in its own font */ /* hh, ww, vw: measured on first draw (items are made fresh, zeroed) */
typedef struct { char title[64], vl[32]; TItem *items; int n; } TCol;
static float fill_ease(double t);
typedef struct { int k; /* 0 none 1 intro 2 recap 3 outro */ int big, nbtn, idx, autoN; double t0, adur; struct { const char *id; char lb[48]; double to; int main; } b[2]; } PPrompt;
static struct {
  int open, closing, live, failed, started; double openedAt;
  char type[16], id[SB_ID], vid[SB_VID], name[256], logo[512], bg[512], poster[512], sname[600], saddon[96], url[SB_URL_MAX], alt[SB_URL_MAX]; int altTried, p2pRetried; double lastPos; int quiet, adopt, isTrailer, isHls, liveStream, hlsVod; /* liveStream: its HLS playlist has no end (a live broadcast); hlsVod: it has an end (a replay) */
  cJSON *meta; const cJSON *videos; int season, episode; char epTitle[256], year[8], genre[64];
  struct { int has; char vid[SB_VID], se[32], title[256], thumb[512], ov[600]; } next; int sim; double startAt;
  struct { int has, tried; double at; char id[16], name[160], poster[400], bg[400], part[64]; } coll; /* a film in a collection: the next one */
  int mode; /* 0 video 1 osd 2 tray 3 prompt */ int row; /* 0 btns 1 bar */ int bIdx;
  struct { const char *id; const char *icon; char lb[48]; int big, nx; float x0, x, y, w, lw; Mo dx; } btns[MAXBTN]; int nbtn, nL;
  /* the page's classes as motions: the title block, bar and buttons, the clock, the shades, the paused label, the spinner,
     the loading line, the seek pill, the knob, the prompt, the subtitles' lift */
  Mo in, artA, ttlA, ttlX, ttlY, barA, barY, btnA, btnY, topR, topY, dim, dimT, vig, pzA, bufA, lnA, skA, skS, knA, prA, prY, prB, subB, ccS, pgA, pgY, pwA, pwF;
  struct { ParGuide g; int age, certAge, guideIn, certIn, asked; unsigned sess; double at, askAt; char cert[24], desc[120]; } pg; /* the viewer notice: an age and what for, as the picture comes, gone in 5 s */
  Mo posA; double posAt; int styK; /* the subtitle style card (mode 5): its showing, when the value last changed, which one (0 position, 1 size, 2 colour, 3 background, 4 brightness) */
  Mo dlyA, dlyR; int dlyF; double dlyAt; /* the delay card (mode 4): its showing, its focus ring's place, the focused button, when the value last changed */
  double hideAt; int osd;
  /* the info chips: kept per place, each with its own entrance */
  struct { char t[100], old[100]; const char *icon; int bold, sep; double at, xAt; float delay, oldW; } chip[8]; int nchip; /* old, xAt, oldW: the words before a change, crossfading */
  double seekSnapAt, seekSnapT; int seekScrub; /* seekScrub: the target is a scrub of the viewer's (keys, the pointer), the only seek ever snapped */ /* (SB_SEEKSNAP: when the last seek was sent, for its time to a picture) */
  double seekT, seekCommitAt, lastKeyAt; int seekDir, rep, skm, skOn; char seekTxt[48]; double seekTxtUntil;
  /* the focus pill under the buttons and its name */
  Mo focX, focY, focW, focH, focA, tipA; char tip[48]; double tipAt, focFadeAt; int focSeen;
  int ldd, exitFade; char trName[128], p2pLine[160]; Mo p2pA; struct { int on, held, target, shown, ns, etaShown, made, slips; double heldAt, holdT, repAt, t0, eta, gainAt, lastF, shownAt, shownF, sAt[40], sF[40]; } pw; /* shown: the wait known to be needed (its line, bar and Play now up); sAt/sF: the progress every 0.5 s, for the pace of the last 15 s */ struct { int on, v; double t0; } ps; /* ps: the seek previews from a lighter copy (core/prevsrc); v 0 not checked, 1 checking, 2 the same cut */ /* pw: previews first (the setting): the film loaded and held on its first frame behind the art while most of its previews are made */
  
  /* prompts (shown: the one drawn, kept while it fades out) */ PPrompt prompt, shown; int dismissed[4];
  struct { int has; double s, e; } intro, recap, outro; int skipsAsked, skipsOk;
  /* the skip sources being asked (merged by rank when they have answered) */
  struct { unsigned tok; int pending, sent, nsrc, isEp, se, en, chapWait; double quickAt, endAt, d; struct { char url[440]; int kind, tries, live; double retryAt; } job[3]; struct { int rank; struct { int has; double s, e; } g[3]; } src[4]; } sk;
  /* next episode from the closest source */
  int nextTried; struct { unsigned tok; int done, has; double bs, until; char vid[SB_VID], info[700], url[SB_URL_MAX], addon[96], binge[96]; Stream best; } nx;
  int ended; double endGoAt; double metaRetryAt;
  /* the paused screensaver, the file size, the picture size */
  double liveT0, steadyAt; /* the first frame came / playing steadily since */
  int ssOn; double ssAt, ssCheckAt, lastAct; double size; char sizeTxt[24]; unsigned sizeTok; int httpSt, sizeAns, canNext; double errAt; int lastVw, lastVh; double aspRefreshAt;
  /* the pointer: the time bubble over the bar */ int hvOn, barDown; float hvP; Mo hvA;
  int extTry, embKnown, embWaitOver, sameWaitOver; double extRetryAt, embWaitAt, sameWaitAt; /* sameWait: a show's addon file, its addon's list awaited (10 s at most) */
  /* the stream as its addon gave it (headers, group, file facts); the subtitles addons installed for this video, the
     language chosen in their tab */
  Stream st; int nSubAddons; char subLang[24]; unsigned ytTok;
  /* the addons' subtitles (the stream's own, then each subtitles addon's), apart from the built-in list in ext */
  SubEntry *adx; int nAdx, adxLoading, adxTry; double adxRetryAt; unsigned adxReq;
  /* tray */ struct { int kind; /* 0 none 1 eps 2 src 3 aud 4 subs 5 epsrc */ char tabs[40][48]; int ntabs, tab, seasonList[40], season; int zone; /* 0 row 1 tabs 2 cols */ TItem row[MAXROW]; int n, idx, onBtn; TCol cols[4]; int ncols, c, r[4]; char title[300], epvid[SB_VID]; int backIdx, backTab; Mo open, openA; float ox, oy; Mo rowX, tabsX, colY[4], ringX, ringY, ringA, plY[4], plA[4]; float ringW, ringH; int loading; int armed; } tray;
  /* the sources tray's lists: one per addon serving the video (in the user's order), all asked as it opens; each stream
     read once as it comes (best_facts), the best fit for this TV among them all */
  struct { char vid[SB_VID]; struct { char name[96]; Stream *items; BestFacts *f; int n, st, nf, came; unsigned tok; Mo ia; } g[16]; int ng, on, bg, bi; } srcs; /* st: 0 not asked, 1 asked (or came, its streams still being read: nf of n, came set), 2 shown, 3 came empty; ia: its rows fading in as they come; bg/bi: the best fit (-1 none) */
  /* subs */ SubEntry *ext; int next_; int nExt, extLoading; char curSub[1100]; struct { int list; char addon[64], lang[24]; } pick; Cue *cues; int ncues; double subDelay; VidTrack emb[16]; int nEmb; int autoSubDone; unsigned subReq; unsigned trSubTok; int locDone;
  int curAud; int autoAudDone; int didResume;
  /* sound this TV can't decode (DTS, TrueHD): when the DTS track started playing, the codecs refused on this stream (error
     200), what was said */ struct { double playAt; char bad[4][16]; int nbad, said, gaveUp, switches;
    /* the sound converted on this device (hproxy): 0 no, 1 opening, 2 playing it; the file's audio track converted, the
       moment to come back to, since when, the picture is HEVC; a first load held back until the file's tracks are read */
    int conv, convOrd, hevc; double convAt, convT0, okAt, deferUntil; char outDesc[24]; /* outDesc: what the TV gets ("AC3 5.1"), for Stats for nerds */
    /* the same conversion for a Dolby Vision profile 7 picture: dv the picture goes out as 8.1, copyA the audio track as
       it is, dvTried started once already, dvSeen what the TV showed was checked */
    int dv, copyA, dvTried, dvSeen; double dvAt; } dts; /* dv: the Dolby Vision profile going through (7 as 8.1; 8, 5 as they are), 0 none */
  struct { double s; char nm[96]; } chap[64]; int nchap; int chLast; double chAt; int chapGen, mp4Asked; double chapReadyAt; /* the file's chapters (nm "" when only numbered); the one the pill last named, when it changed */
  int epState; double played, playTick, pzSince; /* the series' place: 0 not decided, 1 this episode is it, 2 an earlier one (it becomes the place once watched a while), 3 a special (never); played: seconds really watched */
  int assPlain; /* styled subtitles were too slow here: plain for the rest of this film */ double lastSave; int marked; char aspect[8]; const char *res; char infoKey[256]; int pausedShown; double pausedAt;
  /* torrents: resolved by the engine first; the file's own subtitles and chapters from the stream itself */
  int isP2p, resolving; double posTick; int mkvTracksOk, chapWant;
  Nudge nudge; double nameAt;
} P;
static MkvSession *mkv;            /* the subtitle reader of the stream playing (outside P: P starts over per source) */
static unsigned p2pTok;
static void close_player(int goNext, int goSim);
static int closeColl; static Item closeCollIt; /* closing for the next film of a collection: its page */
static void show_osd(int keepFocus);
static void hide_osd(void);
static void render_tray(int fresh);
static void tray_origin(void);
static double now_(void) { return app_now(); }
/* where a looping motion (a spinner, the loading line) is in its turn: worked out in double precision, as a float clock
   steps visibly once the app has been running a while */
static float clock_ph(double period) { return (float)fmod(now_() / period, 1.0); }
int player_open(void) { return P.open; }
int player_art_full(void) { return P.open && P.artA.v > 0.98f; } /* the loading art covers the screen */
float player_cover(void) { return P.open ? P.in.v : 0; }
/* the page under the player: while it covers it only partly, and while a logo flies between them over the art
   (the page's copy is the one that flies back) */
static double growAt, shrinkAt; static int keepVideo, adoptFull; /* adoptFull: taken full screen from behind All channels (no grow, no shrink) */
/* a preview growing to the screen (lead-and-cover): the page round it fades to black (never see-through: the plane
   can't show past the hole), the TV is told the picture's place ahead of the hole (leadMs, one call every stepMs,
   each edge only outward while it grows, inward while it shrinks) so the picture always covers the hole, and the
   hole is inset a little mid-move; the page's picture is taken before the move starts (growPending) */
static int growPending; static float growRad = 26, planeSent[4], growR[4]; /* growR: the box it grows from */ static double planeAt; static int growFrames, growCalls; static float growWorst;
#define GROW_D 0.62
#define SHRINK_D 0.55
static void grow_reverse(void); static float grow_k(void); static void finish_close(void);
int player_page_under(void) { return P.open && (P.in.v < 0.999f || (sx_flying() && !P.live) || growPending || growAt > 0 || shrinkAt > 0); } /* (and while a preview grows to the screen, or back into its box) */ /* (and while a preview grows to the screen) */
int player_live(void) { extern int app_bg_video(void); return (P.open && P.live && !P.closing && P.artA.v < 0.02f) || app_bg_video(); } /* (or the boards' background trailer) */
static int paused(void) { return video_state() == VID_PAUSED; }
static int awayHold; /* (#69) a film paused because the app went behind another app: held paused until the app is in front, or a play is asked (a phone's) */
int player_paused(void) { return P.open && !P.closing && P.live && paused(); } /* (the app's screensaver may come over a paused film) */
static double dur(void) { return video_duration(); }
/* a live broadcast (a channel, a match): no end to count down to, so the bar says LIVE instead of the time left */
/* live by its type: a channel always; a match or an event (sport, events) while its HLS playlist has not said it ends */
static int live_kind(void) { return !strcmp(P.type, "tv") || !strcmp(P.type, "channel") || ((!strcmp(P.type, "sport") || !strcmp(P.type, "events")) && P.isHls && !P.hlsVod); }
static int live_bcast(void) { double d = dur(); return P.live && (P.liveStream || !(d > 0) || d > 1e7 || live_kind()); }
static int live_title(void) { return P.liveStream || live_kind(); } /* known before it plays: nothing kept, nothing resumed */
/* whether an HLS stream is live: its playlist (the first variant of a master one) has no end. Read once as it starts;
   the TV's own length for a live stream is its window's, so a broadcast looked like a short film (no LIVE, a seek
   bar, a place in Continue Watching, a resume into a moment already gone) */
static unsigned liveTok; static char liveBase[SB_URL_MAX]; static int liveDepth;
static void on_live_pl(int st, const unsigned char *b, size_t n, void *ud) {
  (void)n; if ((unsigned)(uintptr_t)ud != liveTok || !P.open || st != 200 || !b) return; const char *s = (const char *)b;
  const char *inf = strstr(s, "#EXT-X-STREAM-INF");
  if (inf && liveDepth < 2) { const char *l = strchr(inf, '\n'); while (l && (l[1] == '#' || l[1] == '\r' || l[1] == '\n')) l = strchr(l + 1, '\n'); /* the variant under it */
    if (!l) return; l++; char v[2048]; size_t k = strcspn(l, "\r\n"); if (k >= sizeof v) return; memcpy(v, l, k); v[k] = 0;
    char abs[SB_URL_MAX]; if (strstr(v, "://")) snprintf(abs, sizeof abs, "%s", v);
    else { const char *q = strpbrk(liveBase, "?#"), *e = q ? q : liveBase + strlen(liveBase), *sl = e; while (sl > liveBase && sl[-1] != '/') sl--; snprintf(abs, sizeof abs, "%.*s%s", (int)(sl - liveBase), liveBase, v); }
    snprintf(liveBase, sizeof liveBase, "%s", abs); liveDepth++; net_get(abs, on_live_pl, ud); return; }
  if (!strstr(s, "#EXTM3U")) return;
  int vod = strstr(s, "#EXT-X-ENDLIST") || strstr(s, "PLAYLIST-TYPE:VOD");
  if (vod) P.hlsVod = 1; /* (a replay of a match: a film again) */
  else { P.liveStream = 1; video_set_live(1); fprintf(stderr, "[player] a live broadcast (its playlist has no end)\n"); }
}
/* a live broadcast that ends or errors while it plays: loaded again, at its live edge, instead of closing; six times
   in two minutes at most, then as any stream */
#define LIVE_RELOADS 6
static double liveRe[LIVE_RELOADS];
static int live_reload(const char *why) {
  if (!P.live || P.isTrailer || P.failed || !live_title()) return 0;
  double now = now_(); int old = 0, recent = 0;
  for (int i = 0; i < LIVE_RELOADS; i++) { if (now - liveRe[i] < 120) recent++; if (liveRe[i] < liveRe[old]) old = i; }
  if (recent >= LIVE_RELOADS) { fprintf(stderr, "[player] live: %s again, %d reloads in 2 min: giving up\n", why, recent); return 0; }
  liveRe[old] = now; fprintf(stderr, "[player] live: %s, back to its live edge (%d/%d)\n", why, recent + 1, LIVE_RELOADS);
  video_load(P.altTried && P.alt[0] ? P.alt : P.url); if (P.liveStream) video_set_live(1); P.errAt = 0;
  return 1;
}
static void live_probe(void) { if (!P.isHls || P.isTrailer) return; liveDepth = 0; snprintf(liveBase, sizeof liveBase, "%s", P.url); net_get(P.url, on_live_pl, (void *)(uintptr_t)++liveTok); }

/* ---- the art screen: the title's logo breathing over its cover, a loading line, a status ---- */
static char status[400];
/* a line of status words: it fades and slides a few px as it comes and goes, and crossfades when its words change */
static XLine stX[3];
/* a light line over the loading text, sparingly (core/waitlines: what fits the moment, at most 3 a wait, each only once
   the real text has stayed the same 4 s and only while it is a calm one; never once the picture runs, never on a
   failure, never over a number, an addon or a reason) */
static unsigned loadGen; /* bumps with each new stream (prepare) */
static struct { int binge; char vid[SB_VID]; int firstToday, film, resume; unsigned gen; } WA; /* the viewer's moment, in memory only */
static int calm_step(const char *st, WlStep *step) { /* the calm texts a line may cover, and the wait they are */
  if (!*st || !strcmp(st, T("Finding the closest source\xe2\x80\xa6")) || !strcmp(st, T("Tuning in\xe2\x80\xa6"))) { *step = WL_FIND; return 1; } /* (a chosen stream or a channel starting: humour only) */
  if (!strcmp(st, T("Choosing the best stream"))) { *step = WL_CHOOSE; return 1; }
  if (!strcmp(st, T("Finding peers\xe2\x80\xa6"))) { *step = WL_PEERS; return 1; }
  if (!strcmp(st, T("Getting the file list\xe2\x80\xa6"))) { *step = WL_FILES; return 1; }
  if (!strcmp(st, T("Waiting for the next pieces\xe2\x80\xa6"))) { *step = WL_PIECES; return 1; }
  return 0;
}
static void moment(void) { /* once a wait: a film, a resume, the nth episode in a row, the first episode today (one local date in the store) */
  if (WA.gen == loadGen) return; WA.gen = loadGen;
  int series = !strcmp(P.type, "series"); WA.film = !strcmp(P.type, "movie"); WA.resume = P.startAt > 1; WA.firstToday = 0;
  if (!series) { if (WA.film) { WA.binge = 0; WA.vid[0] = 0; } return; }
  if (strcmp(WA.vid, P.vid)) { WA.binge = WA.vid[0] ? WA.binge + 1 : 1; snprintf(WA.vid, sizeof WA.vid, "%s", P.vid); /* (another source of the same episode: not one more) */
    time_t tt = time(NULL); struct tm lt; localtime_r(&tt, &lt); char d[16]; strftime(d, sizeof d, "%Y-%m-%d", &lt);
    if (strcmp(store_get("wlDay", ""), d)) { WA.firstToday = 1; store_set("wlDay", d); } }
}
static const char *loading_line(const char *st) {
  static WlSlot slot; moment();
  WlStep step = WL_FIND; int calm = !P.live && !P.failed && calm_step(st, &step);
  return xline_wait(&slot, loadGen, st, calm, step, WA.resume ? WL_ACT_RESUME : WA.firstToday ? WL_ACT_FIRSTTODAY : WA.film ? WL_ACT_FILM : WL_ACT_NONE, WA.binge);
}
/* a status stays long enough to be read (1.2 s and a little more per letter, 3 s at most) before the next one takes its
   place; a failure, the same words with other digits (a counter) and a new play's first words come at once */
static char statusNext[400]; static double statusAt; static int statusPend;
static double status_hold(void) { double h = status[0] ? 1.2 + 0.035 * strlen(status) : 0; return h > 3 ? 3 : h; }
static void status_apply(const char *t) { snprintf(status, sizeof status, "%s", t); statusAt = now_(); statusPend = 0; }
static void set_status(const char *t) { const char *n = t ? t : ""; if (!strcmp(n, status)) { statusPend = 0; return; }
  if (P.failed || !P.open || P.live || wl_same_but_digits(n, status) || now_() - statusAt >= status_hold()) { status_apply(n); return; } /* (held only on the loading screen: never into the film) */
  snprintf(statusNext, sizeof statusNext, "%s", n); statusPend = 1; }
static void status_tick(void) { if (statusPend && (P.live || now_() - statusAt >= status_hold())) status_apply(statusNext); }
/* a heavy film (a remux, 4K, HDR or Dolby Vision, over 12 GB): its seek previews from a lighter copy of it, once
   that copy is proven the same cut; until then (25 s at most) none of the film's own are read, after that the film's
   own make them and a copy proven later still takes over */
static int film_heavy(void) { char t[2048], l[2048]; best_stream_info(&P.st, t, sizeof t); size_t i = 0; for (; t[i] && i < sizeof l - 1; i++) l[i] = (char)tolower((unsigned char)t[i]); l[i] = 0;
  char hd[32]; best_hdr_of(t, hd, sizeof hd); const char *rs = best_res_of(t); int res = rs ? atoi(rs) : 0;
  return hd[0] || res >= 1440 || strstr(l, "2160") || strstr(l, "remux") || P.st.size > 12e9 || P.size > 12e9; }
static int ps_wanted(void) { double fs = P.st.size > 0 ? P.st.size : P.size; return fs >= 20e9 && prev_enabled() && prev_available() && !P.adopt && !P.isTrailer && !P.isHls && !P.isP2p && !strncmp(P.url, "http", 4) && !live_title(); } /* (a film of 20 GB or more, a remux: a copy 2.5x lighter makes its pictures once proven the same cut; any other its own) */
static MkvSession *prev_src_pick(void) {
  if (!P.ps.on || !mkv) return mkv;
  MkvSession *cs = ps_session();
  if (P.ps.v == 2) { if (cs) return cs; P.ps.v = 0; }
  int calm = P.pw.on || paused() || (video_state() == VID_PLAYING && video_buffered_end() - video_time() >= 10); /* (the check reads a few of the film's own keyframes: never while it starts or buffers) */
  if (P.ps.v == 0 && cs && calm && mkv_key_count(mkv) >= 20 && mkv_key_count(cs) >= 20) { if (sp_verify_start(mkv, cs)) { P.ps.v = 1; fprintf(stderr, "[player] checking the lighter copy is the same cut\n"); } }
  if (P.ps.v == 1) { double off = 0; int r = sp_verify_state(&off); if (r == 1) { sp_source_offset(off); P.ps.v = 2; return cs; } if (r == -1 || r == -2) { P.ps.v = 0; ps_next(); } }
  if ((ps_busy() || P.ps.v == 1 || (cs && P.ps.v == 0)) && now_() - P.ps.t0 < 25) return NULL; /* (the copy coming or being checked: the film's own wait) */
  return mkv; }
/* previews first (Settings > Playback, Off or 50 % to 100 %): a direct or debrid film starts once that much of it has
   its seek previews; it loads meanwhile and waits on its first frame behind the art, so OK (Play now) or the share
   reached starts it at once. The time left is estimated from the pace so far. It gives up (the film starts) when no
   picture has come for 20 s, the film has none, or after 4 minutes. */
static int pw_target(void) { const char *v = store_get("seekPreviewsWait", "80"); /* (on by default: 80 %) */ if (!strcmp(v, "true")) return 70; int n = atoi(v); return n >= 50 && n <= 100 ? n : 0; }
static int pw_wanted(void) { return pw_target() > 0 && prev_enabled() && prev_available() && !P.adopt && !P.isTrailer && !P.isHls && !P.isP2p && !strncmp(P.url, "http", 4) && !live_title(); }
__attribute__((weak)) void video_start_silent(int on) { (void)on; } /* (a platform without it: as before) */
static void pw_end(const char *why) { if (!P.pw.on) return; P.pw.on = 0; video_start_silent(0); video_set_volume(100, 300); /* (its sound comes up softly as it starts) */ fprintf(stderr, "[player] previews first: %d %% of %d %% in %.1f s (%s), the film starts\n", sp_progress() < 0 ? 0 : sp_progress(), P.pw.target, now_() - P.pw.t0, why);
  status_apply(""); if (P.pw.held) { if (fabs(video_time() - P.pw.holdT) > 1) { fprintf(stderr, "[player] previews first: back to %.1f s where it waited (it had moved to %.1f s)\n", P.pw.holdT, video_time()); video_seek(P.pw.holdT); } video_play(); } } /* (it starts where it waited, never further on) */
static void pw_tick(void) { if (!P.pw.on) return; VidState pv = video_state(); double now = now_(), el = now - P.pw.t0, pf = sp_progress_f(); int tg = P.pw.target;
  if (P.pw.held && video_settling()) P.pw.holdT = video_time(); /* (the hold follows a jump made while it waits: the resume, which on webOS comes with the same "playing" that set the hold; before, the film went back to 0 s as it started) */
  if (!P.pw.held && pv == VID_PLAYING) { video_pause(); P.pw.held = 1; P.pw.heldAt = now; P.pw.holdT = video_time(); P.pw.repAt = now; fprintf(stderr, "[player] previews first: the film's first frame ready after %.1f s, held at %.1f s\n", el, P.pw.holdT); }
  else if (P.pw.held && now - P.pw.repAt > 0.5 && (pv == VID_PLAYING || video_time() > P.pw.holdT + 0.4)) { video_pause(); P.pw.repAt = now; if (pv != VID_PLAYING && !video_settling()) P.pw.holdT = video_time(); /* (a pause that landed late: it waits there, not paused again every 0.5 s) */ if (P.pw.slips++ < 5) fprintf(stderr, "[player] previews first: the film moved on (%.1f s, its pause not taken): paused again\n", video_time()); } /* (a pause the TV ignored while still starting: never a film running behind the art) */
  if (pv == VID_ERROR || P.failed) { P.pw.on = 0; video_start_silent(0); return; }
  int making = sp_active() || sp_source_on(NULL);
  if (ps_busy() || P.ps.v == 1) P.pw.gainAt = now; /* (a copy still being found or checked counts as coming) */
  { int mk = sp_made() + sp_source_made(); if (mk > P.pw.made) { P.pw.made = mk; P.pw.gainAt = now; } } if (pf > P.pw.lastF + 0.001) { P.pw.lastF = pf; P.pw.gainAt = now; } /* (a picture made counts, even where the share does not move) */
  if (making && pf >= tg) { pw_end(P.pw.shown ? "the share reached" : "already made: no wait"); return; }
  if (making && el > 3 && sp_done()) { pw_end("nothing more to make in the background"); return; } /* (kept on this device: straight in, nothing shown) */
  if (el >= 240) { pw_end("4 minutes"); return; }
  { double d = dur(); if (P.liveStream || (P.pw.held && (d >= 1e7 || (now - P.pw.heldAt > 8 && !(d > 0))))) { /* (live streams never have previews: at once when the TV says its length is endless; with no length at all, 8 s in, when a film's would be known) */ pw_end("a live stream: it has no previews to wait for"); return; } } /* (#81: a broadcast not known as one before it played, its first frame in and no length) */
  if (el > 20 && !P.pw.made && pf <= 0 && now - P.pw.gainAt > 20) { pw_end("no preview made in 20 s"); return; } /* (a lighter copy still looked for counts as coming: gainAt) */ /* (#81: none at all, this TV or this file: the film starts; they still come as it plays) */
  if (el > 10 && !making && !ps_busy() && P.ps.v != 1) { pw_end("no previews for this film"); return; }
  if (el > 12 && now - P.pw.gainAt > 45) { pw_end("no new picture for 45 s"); return; }
  if (!P.pw.shown) { if (!making || pf <= 0 || !P.pw.held) return; /* (once its first pictures are made: never a long 0 %; until then the usual loading screen) */ P.pw.shown = 1; P.pw.shownAt = now; P.pw.shownF = pf; P.pw.ns = 0; fprintf(stderr, "[player] previews first: %.1f %% made, waiting for %d %%\n", pf, tg); } /* (only once it is known a wait is needed: the film ready, its pictures counted) */
  if (!P.pw.ns || now - P.pw.sAt[(P.pw.ns - 1) % 40] >= 0.5) { int i = P.pw.ns++ % 40; P.pw.sAt[i] = now; P.pw.sF[i] = pf; }
  /* the time left from the pace of the last 15 s (the first pictures and the film's own loading left out), shown once
     that pace has held 6 s; it settles and only goes up when clearly wrong */
  double rate = 0; { int n = P.pw.ns < 40 ? P.pw.ns : 40, last = (P.pw.ns - 1) % 40; for (int q = 0; q < n; q++) { int i = (P.pw.ns - 1 - q) % 40; if (now - P.pw.sAt[i] > 15) break; if (now - P.pw.sAt[i] >= 4) rate = (P.pw.sF[last] - P.pw.sF[i]) / (P.pw.sAt[last] - P.pw.sAt[i]); } }
  { static double etaAt; if (rate > 0 && now - P.pw.shownAt >= 10 && pf - P.pw.shownF >= 2 && now - etaAt >= 0.5) { /* (once the pace has held 10 s and 2 points: the first, slow pictures never set it) */ double left = (tg - pf) / rate; /* (twice a second: the last estimate counted down, pulled a quarter towards the new one; up only when 20 % off) */
      if (P.pw.eta <= 0) P.pw.eta = left; else { double e = P.pw.eta - (now - etaAt), to = left < e || left > e * 1.2 ? left : e; P.pw.eta = e * 0.75 + to * 0.25; } etaAt = now; P.pw.etaShown = 1; } }
  char n[8], line[300]; snprintf(n, sizeof n, "%d", pf > 0 && pf < 1 ? 1 : (int)pf); snprintf(line, sizeof line, "%s", T1("Getting the seek bar\xe2\x80\x99s previews ready\xe2\x80\xa6 {n}%", "n", n));
  if (P.pw.etaShown && P.pw.eta > 0 && P.pw.eta <= 900) { double e = P.pw.eta; /* (over 15 minutes: no number at all, never an absurd one) */ char m[16], tl[80]; /* about 5 s, 10 s, ... 55 s; then minutes */
    if (e < 60) { snprintf(m, sizeof m, "%d", (int)(ceil(e / 5) * 5)); snprintf(tl, sizeof tl, "%s", T1("about {n} s left", "n", m)); }
    else { snprintf(m, sizeof m, "%d", (int)ceil(e / 60)); snprintf(tl, sizeof tl, "%s", T1("about {n} min left", "n", m)); }
    size_t l = strlen(line); snprintf(line + l, sizeof line - l, "  \xc2\xb7  %s", tl); }
  status_apply(line); }
static void fail(const char *t) { if (live_title() && !P.isTrailer && P.open && !P.closing && live_failover(P.id)) { fprintf(stderr, "[player] %s: the channel's next stream instead\n", t); video_unload(); P.live = 0; P.quiet = 1; return; } /* a channel: another of its streams first */
  fprintf(stderr, "[player] %s | %s %s %s\n", t, P.type, P.id, P.vid); P.failed = 1; if (live_title()) live_reach_set(P.id, -1); /* (a channel that would not play: off air) */ video_unload(); P.live = 0;
  P.canNext = P.st.url[0] && strcmp(P.type, "other") && !P.isTrailer; if (P.canNext) best_skip(P.st.url); /* OK picks the best of the others */
  char s[400]; snprintf(s, sizeof s, "%s  ·  %s", t, P.canNext ? T("OK: try the next stream  ·  Back: close") : T("Press Back to close")); set_status(s); }
/* why a stream did not play, in words: what its server answered, the torrent's peers, then the TV's own codes
   ("202 Media Not Found; 203 AV Type Not Founded") read with what the stream's text says it is */
static int tv_code(const char *tv, int lo, int hi) { for (const char *p = tv; p && *p; p = strstr(p, "; "), p = p ? p + 2 : NULL) { int k = atoi(p); if (k >= lo && k <= hi) return 1; } return 0; }
static const char *fail_reason(void) {
  const char *tv = video_error(); char lab[1100]; snprintf(lab, sizeof lab, "%s %s", P.sname, P.st.filename); for (char *c = lab; *c; c++) *c = (char)tolower((unsigned char)*c);
  const TvCaps *c = best_caps(); int http = !P.isP2p && !strncmp(P.url, "http", 4) && P.sizeTok;
  fprintf(stderr, "[player] why: tv \"%s\", %s %d, peers \"%s\"\n", tv, P.isP2p ? "torrent" : "http", P.httpSt, P.p2pLine);
  if (http && P.httpSt == 0) return T("The stream’s server didn’t answer");
  if (http && (P.httpSt == 401 || P.httpSt == 403)) return T("The stream’s server refused it: check your debrid account");
  if (http && (P.httpSt == 404 || P.httpSt == 410)) return T("The stream’s server can’t find this file");
  if (http && P.httpSt == 429) return T("The stream’s server is busy: try again in a moment");
  if (http && P.httpSt >= 500) return T("The stream’s server has a problem");
  if (http && P.httpSt >= 400) return T("The stream’s server didn’t send the video");
  /* the TV read the file and refused its format (201 / 203 its codec, 204 "Fail To Demultiplex" its container): said as that
     even on a torrent with few peers, which only waits for data, it does not end in these */
  int fmtRefused = tv_code(tv, 201, 201) || tv_code(tv, 203, 204) || strstr(tv, "Codec") || strstr(tv, "Demultiplex");
  if (P.isP2p && !fmtRefused && (!P.p2pLine[0] || atoi(P.p2pLine) < 2)) return T("Not enough peers yet");
  if (strstr(tv, "Network") || tv_code(tv, 300, 399)) return T("The connection to the stream dropped");
  int hevc = has_word(lab, "x265") || has_word(lab, "h265") || strstr(lab, "h.265") || strstr(lab, "h 265") || has_word(lab, "hevc"), ten = strstr(lab, "10bit") || strstr(lab, "10 bit") || strstr(lab, "10-bit") || has_word(lab, "hdr") || has_word(lab, "hdr10");
  char hdr[64]; best_hdr_of(P.sname, hdr, sizeof hdr); const char *res = best_res_of(P.sname);
  if (c && has_word(lab, "av1") && !c->av1) return T("This TV can’t play AV1 video");
  if (c && hevc && ten && !c->hdr) return T("This TV can’t play HEVC 10-bit video");
  if (c && strstr(hdr, "DV") && !strstr(hdr, "HDR") && !c->dv) return T("This TV can’t play Dolby Vision video");
  if (c && res && !strcmp(res, "4K") && c->maxRes && c->maxRes < 2160) return T("This TV can’t play 4K video");
  int hdrS = has_word(lab, "hdr") || has_word(lab, "hdr10") || strstr(lab, "hdr10+") || has_word(lab, "hlg");
  if (c && hdrS && !c->hdr) return T("This TV can’t show HDR video");
  if (video_audio_unsupported() || (c && ((strstr(lab, "truehd") && !c->truehd) || ((has_word(lab, "dts") || strstr(lab, "dts-hd") || strstr(lab, "dts-x")) && !c->dts))))
    if (tv_code(tv, 200, 200) || tv_code(tv, 203, 203) || video_audio_unsupported()) return T("This TV can’t play this video’s sound (DTS or TrueHD)");
  if (fmtRefused) return T("The video format isn’t supported by this TV");
  if (tv_code(tv, 202, 202)) return T("The TV couldn’t open the video");
  return T("Playback error");
}

/* ---- library / progress ---- */
/* episodes: a series, or any title whose metadata lists videos and where a video (not the title itself) plays (anime
   and channel addons give their own types: "anime", "tv"...; their ids may be "kitsu:123:5", with no season) */
static int episodic(void) {
  if (!strcmp(P.type, "series")) return 1;
  if (!strcmp(P.type, "movie") || !strcmp(P.type, "other") || !P.vid[0] || !strcmp(P.vid, P.id)) return 0;
  const cJSON *v = P.meta ? js_arr(P.meta, "videos") : NULL; return v && cJSON_GetArraySize(v) > 1;
}
static Item lib_item(void) { Item it; memset(&it, 0, sizeof it); snprintf(it.id, sizeof it.id, "%s", P.id); snprintf(it.type, sizeof it.type, "%s", P.type); snprintf(it.name, sizeof it.name, "%s", P.name); snprintf(it.poster, sizeof it.poster, "%s", P.poster); snprintf(it.background, sizeof it.background, "%s", P.bg); snprintf(it.logo, sizeof it.logo, "%s", P.logo); return it; }
/* the credits are reached: the file's or the show's own credits time when known, else the last stretch (about 2 minutes
   of an episode, 5 of a film) or 95 %: from there the title counts as watched */
static int at_credits(void) {
  double d = dur(), t = video_time(); if (!(d > 0) || live_title()) return 0;
  if (P.outro.has && P.outro.s > d * 0.5 && t >= P.outro.s) return 1;
  double tail = episodic() ? fmin(120, d * 0.1) : fmin(300, d * 0.06); return d - t <= tail || t / d > 0.95;
}
/* Continue Watching keeps a series at the latest episode watched: a newer one (or the same) takes the place as soon as
   it plays; an earlier one only once it is really watched (5 minutes, or 40 % of a short one), and then the episodes
   after it are unwatched (watching again from there); a special (season 0) never moves it */
#define BACK_S 300.0
static void ep_decide(void) {
  if (P.epState) return; if (!P.videos && P.vid[0] && strcmp(P.vid, P.id) && P.played < 20) return; /* (the episode list still coming: up to 20 s of playback, then as before) */
  P.epState = 1; if (!episodic() || !P.videos) return;
  int sn, sp; int rn = lib_ep_rank(P.videos, P.vid, &sn); if (sn == 0) { P.epState = 3; return; }
  cJSON *l = lib_get(P.id); const char *cur = js_str(js_obj(l, "state"), "video_id", ""); if (!*cur || !strcmp(cur, P.vid)) return;
  int rp = lib_ep_rank(P.videos, cur, &sp); if (rn >= 0 && rp >= 0 && sp != 0 && rn < rp) { P.epState = 2; fprintf(stderr, "[lib] an earlier episode: the series stays at its place until this one is watched a while\n"); }
}
static void ep_back_check(void) {
  if (P.epState != 2 || P.played < fmin(BACK_S, dur() > 0 ? dur() * 0.4 : BACK_S)) return;
  P.epState = 1; cJSON *l = lib_get(P.id); int n = l ? lib_unwatch_after(l, P.videos, P.vid) : 0;
  fprintf(stderr, "[lib] watching again from this episode: it is the series' place now, %d later episode(s) unwatched\n", n);
}
static void save_progress(int push) { double d = dur(); if (!P.id[0] || P.isTrailer || d <= 0 || live_title() || getenv("SB_PLAY_NOSAVE")) return; /* (a live broadcast: no place in Continue Watching, nothing to resume) */ /* (SB_PLAY_NOSAVE: development plays leave the library alone) */
  ep_decide(); if (P.epState != 1) return; /* an earlier episode not watched long enough yet, or a special: the series keeps its place */
  int ep = episodic() && P.next.has && P.next.vid[0];
  if (P.marked && !ep) return; /* watched to the credits, nothing after it: out of Continue Watching (marking it set its place to the start) */
  Item it = lib_item();
  /* an episode watched to its credits with the next one out: the series waits at the start of the next one, as the
     official apps keep it (video_id the next, offset 1), so it stays in Continue Watching on the next episode */
  if (ep && (P.marked || P.ended || at_credits())) lib_set_progress(P.id, &it, P.next.vid, 1, 0, push);
  else lib_set_progress(P.id, &it, P.vid, video_time() * 1000, d * 1000, push);
  if (push) fprintf(stderr, "[lib] place kept: %s at %.0f of %.0f s\n", P.vid, video_time(), d);
  P.lastSave = now_(); }
static void mark_watched(void) {
  if (P.isTrailer || P.marked || live_title() || getenv("SB_PLAY_NOSAVE")) return; P.marked = 1; Item it = lib_item(); cJSON *l = lib_get(P.id); if (!l) l = lib_new_item(&it);
  if (episodic() && P.videos && strcmp(P.vid, P.id)) { snprintf(it.videoId, sizeof it.videoId, "%s", P.vid); lib_episode_watched(&it, P.videos); } /* it and the aired ones before it; the show's place on to the next (a special: itself) */
  else if (!strcmp(P.type, "series") && P.vid[0] && strcmp(P.vid, P.id)) { snprintf(it.videoId, sizeof it.videoId, "%s", P.vid); lib_episode_watched(&it, NULL); } /* its episode list not here yet: from the cached one (never as a film) */
  else lib_set_movie_watched(l, 1);
  fprintf(stderr, "[lib] watched (%s)\n", P.ended ? "the end" : "the credits");
}
static char resumeMsg[80];
static void try_resume(void) {
  if (P.didResume || P.isTrailer) return; P.didResume = 1;
  if (live_title()) return; /* a broadcast plays from where it is now */
  if (P.startAt < 0) return; /* "Watch from the beginning" */
  if (P.startAt > 0) { video_seek(P.startAt); return; } /* another source: same moment */
  { float tp = lib_trakt_resume(P.id, P.vid); double d = dur(); if (tp > 0 && d > 0) { double sec = tp * d; /* Trakt's pause, the newer (another app, another device) */
      if (sec > 5 && sec < d - 30) { video_seek(sec); char ft[32]; fmt_time(sec, ft, sizeof ft); snprintf(resumeMsg, sizeof resumeMsg, "%s%s", T("Resuming from "), ft); fprintf(stderr, "[trakt] resumed at Trakt's pause, %.0f %%\n", tp * 100); } return; } }
  cJSON *l = lib_get(P.id); const cJSON *st = js_obj(l, "state"); if (!st) return;
  if (!strcmp(js_str(st, "video_id", ""), P.vid) && js_num(st, "timeOffset", 0) > 5000) { double sec = js_num(st, "timeOffset", 0) / 1000, d = js_num(st, "duration", 0) / 1000;
    if (!d || sec < d - 30) { video_seek(sec); char ft[32]; fmt_time(sec, ft, sizeof ft); snprintf(resumeMsg, sizeof resumeMsg, "%s%s", T("Resuming from "), ft); } } /* said as the picture comes up, not while it loads behind the art */
}

/* ---- subtitles ---- */
static const char *want_sub(void) { return store_get("subsLang", "eng"); }
/* Settings > Subtitles > Subtitle preference: Forced only = a film starts with a forced track in that language (only
   the lines in another language and the signs), or none; All subtitles = as always (PR #50) */
static int want_forced(void) { return !strcmp(store_get("subsMode", "all"), "forced"); }
static int emb_forced(const VidTrack *t) { return t->forced || forcedsubs_name(t->name); } /* the file's flag, else the track's name */
static void tag_forced(char *lb, size_t cap, int forced) { /* " · Forced" after a forced track's name, unless the name says it */
  if (!forced || forcedsubs_name(lb)) return; size_t l = strlen(lb); snprintf(lb + l, cap - l, " · %s", T("Forced")); }
/* how high the subtitles sit over the bottom of the picture (resting, the controls away), in % of the screen: 8 as before */
static const int SUB_POS[] = { 3, 8, 13, 18, 23, 28 };
static int pos_idx(void) { int i = atoi(store_get("subsPosition", "1")); return i < 0 || i > 5 ? 1 : i; }
/* the height in % (subsPosPct, set 1 % at a time while watching); before it, one of the six steps above */
#define POS_MIN 2
#define POS_MAX 30
static int pos_pct(void) { int p = atoi(store_get("subsPosPct", "0")); return p >= POS_MIN && p <= POS_MAX ? p : SUB_POS[pos_idx()]; }
static int size_idx(void) { int i = atoi(store_get("subtitleSize", "2")); return i < 0 || i > 6 ? 2 : i; }
static const int SUB_SIZES[] = { 72, 80, 100, 120, 140, 160, 180 };
static const char *SUB_COLORS[] = { "#ffffff", "#ffe600", "#00e5ff" }, *SUB_BGS[] = { "0", "1", ".6" };
/* the lines' brightness (from PR #39, gmarques33): white a little grey, softer on an OLED in a dark room; the colours go
   toward black, never see-through (readable over a bright scene) */
static const char *SUB_BRIGHT[] = { "100", "80", "60", "40" };
static int bright_idx(void) { const char *v = store_get("subsBright", "100"); for (int i = 0; i < 4; i++) if (!strcmp(SUB_BRIGHT[i], v)) return i; return 0; }
static uint32_t col_dim(uint32_t c, float k) { if (k >= 0.999f) return c; return (c & 0xff000000u) | ((uint32_t)(((c >> 16) & 255) * k) << 16) | ((uint32_t)(((c >> 8) & 255) * k) << 8) | (uint32_t)((c & 255) * k); }
static int bg_idx(void) { const char *v = store_get("subsBgStyle", "0"); for (int i = 0; i < 3; i++) if (!strcmp(SUB_BGS[i], v)) return i; return 0; }
static int color_idx(void) { const char *v = store_get("subsColor", "#ffffff"); for (int i = 0; i < 3; i++) if (!strcasecmp(SUB_COLORS[i], v)) return i; return -1; }
static uint32_t hexcol(const char *h) { return 0xff000000u | (unsigned)strtoul(h + 1, NULL, 16); }
static void refresh_tray(int kind) { if (P.tray.kind == kind) render_tray(0); }
static void sync_stop(int keepTimes);
static unsigned cueTimesGen; /* bumped when the lines' times change in place (Auto-sync): their index is made again */
/* The chosen subtitle's progress. A token per choice (an answer for an earlier choice is dropped); while its lines
   load, the tray shows a spinner on it; a file that cannot be read says so. Each choice and its first line on screen
   go to the log ([subs]), so the TV's log tells what happened. */
static struct { unsigned tok; double at, checkAt, waitAt; int wait, tv, mkv, failed, shown, retry, ready; } SL; /* waitAt: since when the reader is behind the playhead */
static char *extAss; static unsigned extAssTok; /* the downloaded subtitle when it is SubStation: the file whole, for styled drawing (valid for SL.tok == extAssTok) */
static int embKind[16];            /* the listed embedded tracks: 1 plain text, 2 SubStation, 0 unknown (the TV's list) */
/* an embedded track's name in the lists: its own title, else its language, else "Track 3" and what it is (some files
   name and tag none of their tracks: a bare "#" said nothing) */
static void emb_label(int i, char *out, size_t cap) {
  const VidTrack *t = &P.emb[i]; const char *ln = lang_name(t->lang); char num[8]; snprintf(num, sizeof num, "%d", i + 1);
  if (t->name[0] && strcmp(t->name, "#")) snprintf(out, cap, "%s", t->name);
  else if (*ln) snprintf(out, cap, "%s", ln);
  else if (t->lang[0] && strcmp(t->lang, "und")) snprintf(out, cap, "%s", t->lang);
  else snprintf(out, cap, "%s", T1("Track {n}", "n", num));
  size_t l0 = strlen(out); tag_forced(out, cap, t->forced);
  if (strlen(out) == l0 && !(t->name[0] && strcmp(t->name, "#")) && i < 16 && embKind[i]) { size_t l = strlen(out); snprintf(out + l, cap - l, " · %s", embKind[i] == 2 ? "ASS" : T("Text")); } /* (the format's own short name: the column is narrow; a forced track says that instead) */
}
static void sub_begin(void) { SL.tok++; SL.at = SL.waitAt = now_(); SL.checkAt = 0; SL.wait = 1; SL.tv = SL.mkv = SL.failed = SL.shown = SL.retry = SL.ready = 0; }
static void sub_reset(void) { unsigned t = SL.tok + 1; memset(&SL, 0, sizeof SL); SL.tok = t; memset(embKind, 0, sizeof embKind); }
static int tv_has_text(int n) { VidTrack t[24]; int k = video_text_tracks(t, 24); for (int i = 0; i < k; i++) if (t[i].idx == n) return 1; return 0; }
static void subs_off(void) { sync_stop(0); snprintf(P.curSub, sizeof P.curSub, "off"); subs_free(P.cues, P.ncues); P.cues = NULL; P.ncues = 0; sub_begin(); SL.wait = 0; video_select_text(-1); if (mkv) mkv_select(mkv, -1); fprintf(stderr, "[subs] off\n"); refresh_tray(4); }
static void set_ext_sub(int list, const SubEntry *e);
static void on_sub_cues(int status_, Cue *cues, int n, void *ud) {
  if (!P.open || (unsigned)(uintptr_t)ud != SL.tok || strncmp(P.curSub, "ext:", 4)) { subs_free(cues, n); return; } /* another choice since */
  if (n > 0) { subs_free(P.cues, P.ncues); P.cues = cues; P.ncues = n; SL.wait = 0; free(extAss); extAss = subs_take_script(); extAssTok = SL.tok; fprintf(stderr, "[subs] external: %d lines read in %.1f s\n", n, now_() - SL.at); refresh_tray(4); return; }
  subs_free(cues, n);
  if (status_ <= 0 && !SL.retry) { SL.retry = 1; fprintf(stderr, "[subs] external: no answer, asking once more\n"); subs_load_lang(P.curSub + 4, P.pick.lang, on_sub_cues, (void *)(uintptr_t)SL.tok); return; } /* one network hiccup */
  fprintf(stderr, "[subs] external: failed (HTTP %d, %s)\n", status_, status_ >= 200 && status_ < 300 ? "nothing readable in it" : "no file");
  toast(T("Couldn’t load these subtitles"), 3000, NULL);
  snprintf(P.curSub, sizeof P.curSub, "off"); SL.wait = 0; refresh_tray(4); /* the tray tells the truth: nothing is on */
}
/* a downloaded subtitle is known by the list it came from and its identity there (the same file can be in both lists,
   or twice in one from two addons): only that row is ticked, whatever the lists become after */
static int sub_is(int list, const SubEntry *e) { return list && P.pick.list == list && !strncmp(P.curSub, "ext:", 4) && !strcmp(e->url, P.curSub + 4) && !strcmp(e->addon, P.pick.addon) && !strcmp(e->lang, P.pick.lang); }
static int sub_is_arabic(void);
static int sub_row(int list, const SubEntry *l, int n) { for (int i = 0; i < n; i++) if (sub_is(list, &l[i])) return i; return -1; }
static void set_ext_sub(int list, const SubEntry *e) {
  const char *u = e->url; sync_stop(0); snprintf(P.curSub, sizeof P.curSub, "ext:%s", u); P.pick.list = list; snprintf(P.pick.addon, sizeof P.pick.addon, "%s", e->addon); snprintf(P.pick.lang, sizeof P.pick.lang, "%s", e->lang); subs_free(P.cues, P.ncues); P.cues = NULL; P.ncues = 0; video_select_text(-1); if (mkv) mkv_select(mkv, -1);
  sub_begin(); fprintf(stderr, "[subs] chose external %s\n", u); subs_load_lang(u, e->lang, on_sub_cues, (void *)(uintptr_t)SL.tok); refresh_tray(4);
}
static void set_emb_sub(int n) {
  sync_stop(0); snprintf(P.curSub, sizeof P.curSub, "emb:%d", n); subs_free(P.cues, P.ncues); P.cues = NULL; P.ncues = 0; sub_begin();
  if (P.mkvTracksOk && mkv) { SL.mkv = 1; video_select_text(-1); mkv_select(mkv, n); fprintf(stderr, "[subs] chose embedded #%d: read from the file\n", n); } /* shown once its lines are read */
  else { SL.wait = 0; SL.tv = 1; video_select_text(n); fprintf(stderr, "[subs] chose embedded #%d: drawn by the TV\n", n); }
  refresh_tray(4);
}
/* the reader's progress for the chosen embedded track, a few times a second: ready (the TV lets go if it was drawing
   meanwhile), still reading (a plain text track goes to the TV after 6 s, when the TV lists it), or given up (the TV
   draws it when it can, else the viewer is told) */
/* what the viewer's subtitle style reaches on the lines shown: 0 all of it (plain text drawn here), 1 size, place and
   brightness (styled lines keep their own colours and boxes), 2 nothing (pictures, or lines the TV draws) */
int player_sub_scope(void);
static void sub_check(void) {
  if (!SL.mkv || strncmp(P.curSub, "emb:", 4) || now_() < SL.checkAt) return; SL.checkAt = now_() + .25;
  int n = atoi(P.curSub + 4), st = mkv_track_status(mkv, n), k = -1; for (int i = 0; i < P.nEmb; i++) if (P.emb[i].idx == n) k = i;
  if (st > 0 && SL.wait) { SL.wait = 0; if (SL.tv && !SL.failed) { SL.tv = 0; video_select_text(-1); } if (!SL.ready) fprintf(stderr, "[subs] embedded #%d ready in %.1f s\n", n, now_() - SL.at); SL.ready = 1; refresh_tray(4); }
  else if (!st && !SL.wait && !SL.failed) { SL.wait = 1; SL.waitAt = now_(); refresh_tray(4); } /* after a seek, or the reader fell behind: reading the new spot */
  else if (st < 0 && !SL.failed) { SL.failed = 1; SL.wait = 0;
    if (tv_has_text(n)) { if (!SL.tv) video_select_text(n); SL.tv = 1; fprintf(stderr, "[subs] embedded #%d: the file can't be read here (%s): the TV draws it\n", n, mkv_error(mkv)); }
    else { fprintf(stderr, "[subs] embedded #%d: the file can't be read here (%s)\n", n, mkv_error(mkv)); toast(T("Subtitle extraction failed"), 3000, NULL); snprintf(P.curSub, sizeof P.curSub, "off"); mkv_select(mkv, -1); }
    refresh_tray(4); }
  else if (!st && SL.wait && !SL.tv && now_() - SL.waitAt > 6 && k >= 0 && embKind[k] == 1 && tv_has_text(n)) { SL.tv = 1; video_select_text(n); fprintf(stderr, "[subs] embedded #%d %s: the TV draws it meanwhile\n", n, SL.ready ? "fell behind the playhead (the server slow or busy)" : "still being read"); } /* (mid-film too: lines read fine for an hour, then a busy server held the reader back and they waited, loading, until it caught up) */
}
static int sub_pictures(void) { /* the chosen embedded track is pictures (PGS, VobSub, DVB) */
  if (strncmp(P.curSub, "emb:", 4)) return 0; int n = atoi(P.curSub + 4); for (int i = 0; i < P.nEmb; i++) if (P.emb[i].idx == n) return embKind[i] == 3; return 0; } /* (none known here yet: a picture track is the TV's, and says so as the TV's) */
int player_sub_scope(void) {
  if (asssubs_on()) return 1;
#ifdef SB_ANDROIDTV
  if (!strncmp(P.curSub, "emb:", 4) && SL.tv && sub_pictures()) return 2; /* (Android TV: its own subtitle view takes the colour, outline, brightness and position (video_atv.c), so only picture tracks take nothing) */
#else
  if (!strncmp(P.curSub, "emb:", 4) && SL.tv) return 2; /* (the TV draws them, for good or meanwhile: nothing of ours reaches them) */
#endif
  return 0; }
/* an ASS / SSA subtitle is drawn styled (libass) while the setting says so: an embedded track read from the file, or a
   downloaded file (any source) handed over whole, on its own timeline (the delay and a sync mapped onto it; its plain
   lines stay what the sync works on); plain otherwise, and from the moment renders get too slow here */
static double sync_map(double t, double *rate);
static void ass_sync(void) {
  int n = -1, k = -1, styled = !P.assPlain && strcmp(store_get("subsAssStyle", "styled"), "plain");
  if (!strncmp(P.curSub, "emb:", 4) && SL.mkv && !SL.tv && mkv && styled) { n = atoi(P.curSub + 4); for (int i = 0; i < P.nEmb; i++) if (P.emb[i].idx == n) k = i; }
  int ext = styled && !strncmp(P.curSub, "ext:", 4) && extAss && extAssTok == SL.tok && P.ncues;
  int want = ext || (k >= 0 && embKind[k] == 2);
  static int openN = -1; static const void *openMkv; static unsigned openTok;
  if (!want) { if (asssubs_on()) { asssubs_close(); openN = -1; } return; }
  if (ext) { if (!asssubs_on() || openN != -2 || openTok != SL.tok) { asssubs_open_script(extAss, P.pick.lang); openN = -2; openTok = SL.tok; } }
  else if (!asssubs_on() || openN != n || openMkv != mkv) { asssubs_open(mkv, n, P.url, P.emb[k].lang); openN = n; openMkv = mkv; }
  int slow = asssubs_too_slow();
  if (slow) { P.assPlain = 1; asssubs_close(); openN = -1; if (!ext) mkv_style(mkv, n, 0); if (slow == 1) toast(T("Styled subtitles are too heavy here: shown plain"), 4000, NULL); return; }
  /* the picture's rectangle: fitted inside the screen (Fill and Zoom: the screen) */
  float vx = 0, vy = 0, vw = SB_W, vh = SB_H; int w = video_width(), h = video_height();
  if (w > 0 && h > 0 && !strcmp(P.aspect, "fit")) { float a = (float)w / h; if (a > SB_W / SB_H) { vh = SB_W / a; vy = (SB_H - vh) / 2; } else { vw = SB_H * a; vx = (SB_W - vw) / 2; } }
  VidState vs = video_state(); double t = video_time() - P.subDelay, rate = 1; if (ext) t = sync_map(t, &rate);
  asssubs_set_line(pos_pct() > 8 ? pos_pct() - 8 : 0); /* the Position setting: 8 % (the default) leaves styled lines where the script puts them */
  asssubs_tick(t, rate, vs == VID_PLAYING && !video_settling(), video_fps(), vx, vy, vw, vh, w, h, SUB_SIZES[size_idx()] / 100.0f);
}
static const char *skip_mode(void) { return store_get("skipIntro", "auto"); } /* the intro / recap button: auto, show, off */
static void sync_auto_arm(void);
/* a language's regional variant, where subtitles name one: Brazilian Portuguese (pob), Latin American Spanish (spl);
   "" for the rest. The language match takes either for the other, so the variant asked for is looked for first */
static const char *lang_variant(const char *c) {
  char l[16]; size_t i = 0; for (; c && c[i] && i + 1 < sizeof l; i++) l[i] = (char)tolower((unsigned char)c[i]); l[i] = 0;
  if (!strcmp(l, "pob") || !strcmp(l, "pb") || !strcmp(l, "pt-br") || !strcmp(l, "pt_br") || !strcmp(l, "ptbr")) return "pob";
  if (!strcmp(l, "spl") || !strcmp(l, "es-419") || !strcmp(l, "es-la") || !strcmp(l, "es_la") || !strcmp(l, "es-mx")) return "spl";
  return ""; }
static int variant_ok(const char *have, const char *want) { return !strcmp(lang_variant(have), lang_variant(want)); }
/* ---- a show's own subtitles and audio: picked by hand in one of its episodes (other than what the settings would give),
   kept for the whole show and used in its other episodes; picked again, kept again. TV shows only ---- */
static int show_prefs_on(void) { return !strcmp(P.type, "series") && !P.isTrailer && P.id[0]; }
static const cJSON *show_pref(void) { if (!show_prefs_on()) return NULL; cJSON *a = store_node("showPrefs"); return cJSON_IsObject(a) ? cJSON_GetObjectItemCaseSensitive(a, P.id) : NULL; }
static void show_pref_put(const char *k, const char *v) { /* v NULL: that one forgotten (back to the settings') */
  cJSON *a = store_node("showPrefs"); cJSON *n = cJSON_IsObject(a) ? cJSON_Duplicate(a, 1) : cJSON_CreateObject(); cJSON *o = cJSON_GetObjectItemCaseSensitive(n, P.id); if (!o) { o = cJSON_CreateObject(); cJSON_AddItemToObject(n, P.id, o); }
  cJSON_DeleteItemFromObjectCaseSensitive(o, k); if (v) cJSON_AddStringToObject(o, k, v); if (!cJSON_GetArraySize(o)) cJSON_DeleteItemFromObjectCaseSensitive(n, P.id); store_set_node("showPrefs", n); }
static void show_pref_sub(void) { /* the subtitle on now, chosen by hand */
  P.autoSubDone = 1; /* (a list arriving later never overrides it: Off picked while they load stays off) */
  if (!show_prefs_on()) return; const char *w = want_sub(); char kind[8] = "", lang[24] = "", name[96] = "";
  if (!strcmp(P.curSub, "off")) snprintf(kind, sizeof kind, "off");
  else if (!strncmp(P.curSub, "emb:", 4)) { int n = atoi(P.curSub + 4); for (int i = 0; i < P.nEmb; i++) if (P.emb[i].idx == n) { snprintf(kind, sizeof kind, "emb"); snprintf(lang, sizeof lang, "%s", P.emb[i].lang); snprintf(name, sizeof name, "%s", P.emb[i].name); } }
  else if (!strncmp(P.curSub, "ext:", 4)) { snprintf(kind, sizeof kind, "ext"); snprintf(lang, sizeof lang, "%s", P.pick.lang); snprintf(name, sizeof name, "%s", P.pick.addon); }
  if (!kind[0]) return;
  int asSettings = !strcmp(kind, "off") ? !*w : (*w && lang_match(lang, w)); /* what the settings give anyway: nothing to keep */
  show_pref_put("sk", asSettings ? NULL : kind); show_pref_put("sl", asSettings ? NULL : lang); show_pref_put("sn", asSettings || !name[0] ? NULL : name);
  fprintf(stderr, "[subs] %s for this show\n", asSettings ? "as the settings say" : "kept");
}
/* Settings > Audio's language: "" for Original (the film's own track, left as it starts), else the language to pick */
static const char *aud_setting(void) { const char *v = store_get("defaultAudioTrack", ""); return strcmp(store_get("enableDefaultAudioTrack", "false"), "true") || !strcmp(v, "orig") ? "" : v; }
static void show_pref_aud(const char *lang, const char *name) {
  P.autoAudDone = 1; /* (picked by hand: the automatic pick, still trying while the tracks come, never overrides it) */
  if (!show_prefs_on() || !lang) return; const char *dw = aud_setting();
  int asSettings = *dw && lang_match(lang, dw); show_pref_put("al", asSettings || !*lang ? NULL : lang); show_pref_put("an", asSettings || !name || !*name ? NULL : name);
  fprintf(stderr, "[audio] %s for this show\n", asSettings ? "as the settings say" : "kept");
}
static const char *aud_want(void) { const char *al = js_str(show_pref(), "al", NULL); if (al && *al) return al; return aud_setting(); }
static void auto_sub(void) {
  if (P.autoSubDone || strcmp(P.curSub, "off")) return;
  const cJSON *sp = show_pref(); const char *pk = js_str(sp, "sk", ""), *pn = js_str(sp, "sn", "");
  if (!strcmp(pk, "off")) { P.autoSubDone = 1; return; } /* this show: none, as chosen */
  const char *w = *pk ? js_str(sp, "sl", "") : want_sub(); if (!*w) return;
  again:;
  if (!strcmp(pk, "ext") && (P.nExt || P.nAdx)) { /* this show's file from an addon: the same addon first, the language */
    for (int pass = 0; pass < 2; pass++) { if (pass && (*pn ? P.adxLoading : P.extLoading) && !P.sameWaitOver) { if (!P.sameWaitAt) P.sameWaitAt = now_() + 10; return; } /* (any addon's file in that language only once the list that may hold the same addon's is in, or 10 s on) */
      for (int i = 0; i < P.nAdx; i++) if (lang_match(P.adx[i].lang, w) && (pass || !strcmp(P.adx[i].addon, pn))) { P.autoSubDone = 1; set_ext_sub(2, &P.adx[i]); sync_auto_arm(); return; }
      for (int i = 0; i < P.nExt; i++) if (lang_match(P.ext[i].lang, w) && (pass || !strcmp(P.ext[i].addon, pn))) { P.autoSubDone = 1; set_ext_sub(1, &P.ext[i]); sync_auto_arm(); return; } }
    if (P.extLoading || P.adxLoading) return; } /* (the lists still coming) */
  if (!strcmp(pk, "emb") && *pn) for (int j = 0; j < P.nEmb; j++) if (lang_match(P.emb[j].lang, w) && !strcmp(P.emb[j].name, pn)) { P.autoSubDone = 1; set_emb_sub(P.emb[j].idx); return; } /* the same track by its name */
  int fo = want_forced(), em = -1;
  for (int j = 0; j < P.nEmb; j++) if (lang_match(P.emb[j].lang, w) && (!fo || emb_forced(&P.emb[j])) && (em < 0 || (!fo && P.emb[em].forced && !P.emb[j].forced))) em = j;
  if (em >= 0) { P.autoSubDone = 1; set_emb_sub(P.emb[em].idx); return; } /* embedded wins: it is in sync with the file */
  if (!P.embKnown && !P.embWaitOver) { if (!P.embWaitAt) P.embWaitAt = now_() + 8; return; } /* the file's own tracks get 8 s to show up first */
  for (int pass = 0; pass < 2; pass++) { /* the variant asked for first (Brazilian for Brazilian, European for European), then any */
    for (int i = 0; i < P.nExt; i++) if (lang_match(P.ext[i].lang, w) && (!fo || P.ext[i].forced) && (pass || variant_ok(P.ext[i].lang, w))) { P.autoSubDone = 1; set_ext_sub(1, &P.ext[i]); sync_auto_arm(); return; }
    for (int i = 0; i < P.nAdx; i++) if (lang_match(P.adx[i].lang, w) && (!fo || P.adx[i].forced) && (pass || variant_ok(P.adx[i].lang, w))) { P.autoSubDone = 1; set_ext_sub(2, &P.adx[i]); sync_auto_arm(); return; } }
  if (*pk && !P.extLoading && !P.adxLoading && *want_sub() && !lang_match(w, want_sub())) { /* this show's language not in this episode, every list in: the settings' */
    fprintf(stderr, "[subs] no %s for this show here: the settings' %s\n", w, want_sub()); pk = ""; pn = ""; w = want_sub(); goto again; }
}
/* a trailer's captions (IMDb's or YouTube's): handed over with the trailer, read when the CC button turns them on */
static TrailerSub trSubs[TRAILER_MAX_SUBS], trNext[TRAILER_MAX_SUBS]; static int trSubsN, trNextN;
void player_trailer_subs(const TrailerSub *subs, int n) { if (n > TRAILER_MAX_SUBS) n = TRAILER_MAX_SUBS; if (n < 0 || !subs) n = 0; if (n) memcpy(trNext, subs, n * sizeof *subs); trNextN = n; }
static void on_tr_cues(Cue *cues, int n, void *ud) {
  if (!P.open || (unsigned)(uintptr_t)ud != P.trSubTok || strncmp(P.curSub, "ext:", 4)) { subs_free(cues, n); return; }
  subs_free(P.cues, P.ncues); P.cues = cues; P.ncues = n; SL.wait = 0; fprintf(stderr, "[subs] trailer captions: %d lines\n", n);
  if (!n) toast(T("Couldn’t load these subtitles"), 3000, NULL);
}
/* one press on (the language the viewer reads, else the first), one press off */
static void cc_toggle(void) {
  if (!trSubsN) return;
  if (strcmp(P.curSub, "off")) { subs_off(); return; }
  const char *w = want_sub(); int pick = 0; for (int i = 0; i < trSubsN; i++) if (lang_match(trSubs[i].lang, w)) { pick = i; break; }
  snprintf(P.curSub, sizeof P.curSub, "ext:%s", trSubs[pick].url); P.pick.list = 0; subs_free(P.cues, P.ncues); P.cues = NULL; P.ncues = 0; sub_begin();
  P.trSubTok++; trailer_sub_load(&trSubs[pick], on_tr_cues, (void *)(uintptr_t)P.trSubTok);
}
/* the built-in OpenSubtitles list; no answer: asked again 15, 30 and 45 s later */
static void on_subs(SubEntry *subs, int n, int ok, void *ud) {
  if (!P.open || (unsigned)(uintptr_t)ud != P.subReq) { free(subs); return; }
  if (!ok && P.extTry < 3) { free(subs); P.extRetryAt = now_() + (P.extTry + 1) * 15; return; }
  free(P.ext); P.ext = subs; P.nExt = n; P.extLoading = 0; auto_sub(); refresh_tray(4); /* (an empty list too: what waited on it goes on) */
}
/* the addons' lists (the stream's own, every subtitles addon's): each answer replaces the list with all that came so
   far; nothing from anyone: asked again 15, 30 and 45 s later */
static void on_adx(SubEntry *subs, int n, int final, int ok, void *ud) {
  if (!P.open || (unsigned)(uintptr_t)ud != P.adxReq) { free(subs); return; }
  if (final && !ok && !n && P.adxTry < 3 && P.nSubAddons) { free(subs); P.adxRetryAt = now_() + (P.adxTry + 1) * 15; return; }
  free(P.adx); P.adx = subs; P.nAdx = n; P.adxLoading = !final; if (n || final) auto_sub(); refresh_tray(4);
}
static int hashable(const char *u) { return !strncmp(u, "http", 4) && !strstr(u, ".m3u8"); }
static char artName[256], artPoster[512], artBg[512], artLogo[512]; /* the next play's title and pictures as handed over (a phone's) */
static SubEntry castSubs[16]; static int castSubsN = -1; /* the next play's subtitles as a phone sent them (-1: none sent) */
static void load_ext_subs(int n) { if (!P.type[0] || !P.vid[0] || P.isTrailer) return; P.extLoading = 1;
  if (castSubsN >= 0) { int k = castSubsN; castSubsN = -1; SubEntry *l = calloc(16, sizeof *l); memcpy(l, castSubs, sizeof castSubs); P.subReq++; on_subs(l, k, 1, (void *)(uintptr_t)P.subReq); return; } /* cast: the phone's own list */
  if (getenv("SB_PLAY_SUBS")) { SubEntry *l = calloc(16, sizeof *l); int k = 0; char b[4096]; snprintf(b, sizeof b, "%s", getenv("SB_PLAY_SUBS")); /* development: "url,url,..." as the external list */
    for (char *u = strtok(b, ","); u && k < 16; u = strtok(NULL, ",")) { snprintf(l[k].lang, sizeof l[k].lang, "eng"); snprintf(l[k].name, sizeof l[k].name, "English"); snprintf(l[k].url, sizeof l[k].url, "%s", u); l[k].n = k + 1; l[k].from = SUB_FALLBACK; k++; }
    P.subReq++; on_subs(l, k, 1, (void *)(uintptr_t)P.subReq); return; }
  P.extTry = n; P.subReq++; subs_list(P.type, P.vid, on_subs, (void *)(uintptr_t)P.subReq); }
static void load_addon_subs(int n) { if (!P.type[0] || !P.vid[0] || P.isTrailer || getenv("SB_PLAY_SUBS")) return;
  P.adxTry = n; P.adxReq++; P.nSubAddons = subs_addons_for(P.type, P.vid);
  SubQuery q; memset(&q, 0, sizeof q); snprintf(q.type, sizeof q.type, "%s", P.type); snprintf(q.vid, sizeof q.vid, "%s", P.vid); snprintf(q.url, sizeof q.url, "%s", P.url);
  snprintf(q.filename, sizeof q.filename, "%s", P.st.filename); snprintf(q.hash, sizeof q.hash, "%s", P.st.hash); q.size = P.st.size > 0 ? P.st.size : P.size; q.hashable = hashable(P.url);
  SubEntry *own = NULL; int nown = sb_stream_subs(&P.st, &own);
  if (!P.nSubAddons && !nown) { free(own); return; } /* nobody to ask */
  P.adxLoading = 1; subs_find(&q, own, nown, on_adx, (void *)(uintptr_t)P.adxReq); free(own); }
/* the file's subtitle tracks: read from the stream itself when it is a Matroska file (named, styled, delayed like the
   others: listed as soon as its head is read, their lines follow); else the ones the TV's pipeline reports, which it
   draws itself (their numbers are the same: the order among the file's subtitle tracks). Asked every second until
   settled; the tray is redrawn only when the list changes */
static void load_emb_tracks(void) {
  int st = mkv ? mkv_state(mkv) : MKV_FALLBACK; unsigned sig = 0; for (int i = 0; i < P.nEmb; i++) sig = sig * 31 + (unsigned)P.emb[i].idx + 7; sig = sig * 3 + (unsigned)P.nEmb + (unsigned)P.embKnown * 101;
  if (mkv && st != MKV_FALLBACK && !mkv_listed(mkv)) return; /* the file's head is still being read */
  if (mkv && st != MKV_FALLBACK) { MkvTrack t[16]; int n = mkv_tracks(mkv, t, 16), k = 0;
    for (int i = 0; i < n && k < 16; i++) { if (!t[i].text) continue; embKind[k] = strstr(t[i].codec, "ASS") || strstr(t[i].codec, "SSA") ? 2 : 1; VidTrack *v = &P.emb[k++]; memset(v, 0, sizeof *v); snprintf(v->lang, sizeof v->lang, "%s", t[i].lang); snprintf(v->name, sizeof v->name, "%s", t[i].name); v->idx = t[i].n; v->forced = t[i].forced; }
    P.nEmb = k; P.embKnown = 1; P.mkvTracksOk = 1; }
  else { P.nEmb = video_text_tracks(P.emb, 16); memset(embKind, 0, sizeof embKind); P.mkvTracksOk = 0; if (P.nEmb || video_live()) P.embKnown = 1; }
  unsigned sig2 = 0; for (int i = 0; i < P.nEmb; i++) sig2 = sig2 * 31 + (unsigned)P.emb[i].idx + 7; sig2 = sig2 * 3 + (unsigned)P.nEmb + (unsigned)P.embKnown * 101;
  if (sig2 != sig) fprintf(stderr, "[subs] %d embedded track(s) %s\n", P.nEmb, P.mkvTracksOk ? "read from the file" : "from the TV");
  if (!P.autoSubDone) auto_sub(); if (sig2 != sig) refresh_tray(4);
}

/* ---- Auto-sync: the subtitle on (a downloaded one) lined up with the file's own subtitle tracks, read near the
   playhead by the Matroska reader (any language: only when lines are spoken matters). They are waited for while they
   are being read (a torrent brings them in as its pieces arrive), up to 45 s; the matching then runs on a thread of
   its own (core/subsync). The answer applies to the lines' original times (running it again never adds up), the delay
   goes back to 0, Reset undoes it. This TV hands its sound to the screen's own decoder, so there is no audio to match
   against: a file with no text tracks (most HTTP and debrid files) is matched against another subtitle made for this
   file instead, found and read by the job itself; nothing of the kind: nothing to sync against. ---- */
static struct { SubSync *job; int phase; /* 0 idle, 1 waiting for the reference, 2 matching */ int stage; double t0, nextAt, center; char st[160]; double *base; int nbase;
  int quiet, autoWant; unsigned autoTok; double autoAt; /* quiet: the check of a subtitle the app chose by itself, unseen unless it fixes something */
  double f, d; /* the sync applied: line times = original * f + d */ } SY;
/* a film time on the downloaded subtitle's own timeline (styled drawing): the sync applied undone; rate: its seconds per film second */
static double sync_map(double t, double *rate) { if (SY.base && SY.nbase == P.ncues && SY.f > 0) { *rate = 1 / SY.f; return (t - SY.d) / SY.f; } *rate = 1; return t; }
static void sync_stop(int keepTimes) {
  if (SY.job) { subsync_drop(SY.job); SY.job = NULL; } SY.phase = 0; SY.st[0] = 0; SY.quiet = 0;
  if (!keepTimes) { free(SY.base); SY.base = NULL; SY.nbase = 0; }
}
static void sync_end(const char *why) { if (SY.job) { subsync_drop(SY.job); SY.job = NULL; } SY.phase = 0; if (!SY.quiet) snprintf(SY.st, sizeof SY.st, "%s", why); fprintf(stderr, "[subsync] %s%s\n", SY.quiet ? "(by itself) " : "", why); SY.quiet = 0; refresh_tray(4); }
static void sync_undo(void) { /* Reset: the lines' own times again */
  if (SY.base && SY.nbase == P.ncues) { for (int i = 0; i < P.ncues; i++) { P.cues[i].s = SY.base[2 * i]; P.cues[i].e = SY.base[2 * i + 1]; } cueTimesGen++; }
  sync_stop(0);
}
static void sync_run(void) {
  if (SY.phase && SY.quiet) { SY.quiet = 0; snprintf(SY.st, sizeof SY.st, "%s", SY.phase == 2 ? (SY.stage ? T("Looking for a matching subtitle…") : T("Syncing…")) : T("Syncing…")); refresh_tray(4); return; } /* the check the app was making by itself: now shown */
  if (SY.phase) { sync_stop(1); refresh_tray(4); return; } /* pressed again while it runs: it stops */
  if (strncmp(P.curSub, "ext:", 4) || P.isTrailer) return;
  SY.phase = 1; SY.t0 = now_(); SY.nextAt = 0; SY.center = video_time(); snprintf(SY.st, sizeof SY.st, "%s", T("Syncing…")); refresh_tray(4);
  fprintf(stderr, "[subsync] asked at %.0f s\n", SY.center);
}
/* the subtitle's lines with their original times (not the ones a sync before set), for the job to copy */
static Cue *sync_xs(void) {
  Cue *xs = malloc((size_t)(P.ncues ? P.ncues : 1) * sizeof *xs); if (!xs) return NULL; int orig = SY.base && SY.nbase == P.ncues;
  for (int i = 0; i < P.ncues; i++) { xs[i] = P.cues[i]; if (orig) { xs[i].s = SY.base[2 * i]; xs[i].e = SY.base[2 * i + 1]; } }
  return xs;
}
static const char *sync_wait_txt(int stage) { return stage ? T("Looking for a matching subtitle…") : T("Syncing…"); }
/* no text track to match against: every other subtitle listed for this video goes to the job, which keeps those made
   for this file (by hash, carried by the stream, by file name) and asks OpenSubtitles with the file's hash */
static void sync_by_ref(const char *why) {
  int cap = P.nExt + P.nAdx, n = 0; SyncRef *rs = calloc((size_t)(cap ? cap : 1), sizeof *rs); if (!rs) { sync_end(T("Couldn’t sync")); return; }
  for (int l = 1; l <= 2; l++) { const SubEntry *L = l == 1 ? P.ext : P.adx; int nl = l == 1 ? P.nExt : P.nAdx;
    for (int i = 0; i < nl; i++) { const SubEntry *e = &L[i]; if (sub_is(l, e)) continue; SyncRef *r = &rs[n++];
      snprintf(r->url, sizeof r->url, "%s", e->url); snprintf(r->lang, sizeof r->lang, "%s", e->lang); snprintf(r->rel, sizeof r->rel, "%s", e->rel[0] ? e->rel : e->label);
      snprintf(r->from, sizeof r->from, "%s", e->addon); r->hm = e->hm; r->own = e->from == SUB_STREAM; r->fps = e->fps; } }
  SubQuery q; memset(&q, 0, sizeof q); snprintf(q.type, sizeof q.type, "%s", P.type); snprintf(q.vid, sizeof q.vid, "%s", P.vid); snprintf(q.url, sizeof q.url, "%s", P.url);
  snprintf(q.filename, sizeof q.filename, "%s", P.st.filename); snprintf(q.hash, sizeof q.hash, "%s", P.st.hash); q.size = P.st.size > 0 ? P.st.size : P.size; q.hashable = hashable(P.url);
  if (!q.hash[0]) subs_hash_of(P.url, q.hash, &q.size); q.fps = video_fps();
  int canHash = (q.hash[0] || q.hashable) && q.type[0] && q.vid[0];
  fprintf(stderr, "[sync] %s: another subtitle made for this file as the reference (%d listed, hash %s)\n", why, n, q.hash[0] ? q.hash : canHash ? "to work out" : "unknown");
  if (!n && !canHash) { free(rs); sync_end(T("Nothing to sync against")); return; }
  Cue *xs = sync_xs(); if (!xs) { free(rs); sync_end(T("Couldn’t sync")); return; }
  SY.job = subsync_start_ref(xs, P.ncues, SY.center, &q, P.curSub + 4, P.pick.list ? P.pick.lang : "", rs, n); free(xs); free(rs); /* (it keeps its own copies) */
  if (!SY.job) { sync_end(T("Couldn’t sync")); return; }
  SY.phase = 2; SY.stage = 1; if (!SY.quiet) snprintf(SY.st, sizeof SY.st, "%s", sync_wait_txt(1)); refresh_tray(4);
}
static void sync_auto_arm(void) { if (!P.nEmb && !P.isTrailer) { SY.autoWant = 1; SY.autoTok = SL.tok; SY.autoAt = now_(); } } /* (a file with subtitles of its own has them chosen instead) */
/* the subtitle the app chose by itself at the start, for a file with no subtitles of its own: checked once its lines are
   in and the lists have answered (or 15 s on), quietly; lined up only when clearly out (half a second or more, or the
   wrong speed) and the viewer has set no delay, then said in a toast */
static void sync_auto_tick(void) {
  if (!SY.autoWant || SY.phase) return;
  if (SL.tok != SY.autoTok || strncmp(P.curSub, "ext:", 4) || P.isTrailer || P.subDelay != 0) { SY.autoWant = 0; return; } /* another choice since, or a delay of the viewer's */
  if (!P.ncues || SL.wait || now_() - SY.autoAt < 3 || ((P.extLoading || P.adxLoading) && now_() - SY.autoAt < 15)) return;
  SY.autoWant = 0; SY.quiet = 1; SY.phase = 1; SY.t0 = now_(); SY.nextAt = 0; SY.center = video_time();
  fprintf(stderr, "[sync] checking the subtitle chosen at the start, by itself (at %.0f s)\n", SY.center);
}
static void sync_tick(void) {
  sync_auto_tick(); if (!SY.phase) return;
  if (SY.phase == 2) { SyncResult r; if (!subsync_poll(SY.job, &r)) { int sg = subsync_stage(SY.job); if (sg != SY.stage) { SY.stage = sg; if (!SY.quiet) { snprintf(SY.st, sizeof SY.st, "%s", sync_wait_txt(sg)); refresh_tray(4); } } return; }
    subsync_drop(SY.job); SY.job = NULL;
    if (r.st == SYNC_NOREF) { sync_end(T("Nothing to sync against")); return; }
    if (r.st == SYNC_NOSUB) { sync_end(T("Couldn’t sync")); return; }
    if (r.st != SYNC_OK) { sync_end(T("Not sure: unchanged")); return; }
    double at = video_time(), sh = at * (r.f - 1) + r.d; /* the shift where it is watched (a speed fix moves it along the film) */
    if (SY.quiet && (P.subDelay != 0 || (fabs(sh) < 0.5 && fabs(r.f - 1) < 0.0005))) { fprintf(stderr, "[sync] (by itself) %s: left as it is\n", P.subDelay != 0 ? "a delay was set meanwhile" : "already in sync"); sync_end(""); return; }
    if (!SY.base || SY.nbase != P.ncues) { free(SY.base); SY.base = malloc((size_t)(P.ncues ? P.ncues : 1) * 2 * sizeof *SY.base); SY.nbase = SY.base ? P.ncues : 0;
      for (int i = 0; i < SY.nbase; i++) { SY.base[2 * i] = P.cues[i].s; SY.base[2 * i + 1] = P.cues[i].e; } }
    if (!SY.base) { sync_end(T("Couldn’t sync")); return; }
    for (int i = 0; i < P.ncues; i++) { P.cues[i].s = SY.base[2 * i] * r.f + r.d; P.cues[i].e = SY.base[2 * i + 1] * r.f + r.d; } cueTimesGen++; SY.f = r.f; SY.d = r.d;
    P.subDelay = 0;
    char n[24]; snprintf(n, sizeof n, "%s%.1f", sh >= 0 ? "+" : "", sh); char st[160]; snprintf(st, sizeof st, "%s %s%s%s", T("Synced"), T1("{n} s", "n", n), fabs(r.f - 1) > 0.001 ? "  ·  " : "", fabs(r.f - 1) > 0.001 ? T("speed fixed") : "");
    int q = SY.quiet; SY.quiet = 0; sync_end(st); toast(q ? T1("Subtitles synced {n} s", "n", n) : SY.st, 3000, NULL); return; }
  if (now_() < SY.nextAt) return; SY.nextAt = now_() + 0.5;
  /* waiting for the file's own lines near the playhead */
  int st = mkv ? mkv_state(mkv) : MKV_FALLBACK, listed = mkv && st != MKV_FALLBACK && mkv_listed(mkv);
  if (st == MKV_FALLBACK) { sync_by_ref("not a file whose tracks can be read"); return; }
  MkvTrack tr[16]; int nt = listed ? mkv_tracks(mkv, tr, 16) : 0, ntext = 0;
  double lo = SY.center - SYNC_BEHIND_S, hi = SY.center + SYNC_AHEAD_S; int best = -1, bestN = 0;
  for (int i = 0; i < nt; i++) { if (!tr[i].text) continue; ntext++; int c = mkv_lines_in(mkv, tr[i].n, lo, hi, NULL, 0, NULL, 0); if (c > bestN) { bestN = c; best = tr[i].n; } }
  if (listed && !ntext) { sync_by_ref("no text track in the file"); return; } /* the file has no text tracks at all */
  if (bestN >= 12) {
    int cap = bestN < 3000 ? bestN : 3000, tcap = cap * 96; MkvLine *ml = malloc((size_t)cap * sizeof *ml); char *tb = malloc((size_t)tcap); SyncLine *rl = malloc((size_t)cap * sizeof *rl);
    Cue *xs = sync_xs();
    if (!ml || !tb || !rl || !xs) { free(ml); free(tb); free(rl); free(xs); sync_end(T("Couldn’t sync")); return; }
    int k = mkv_lines_in(mkv, best, lo, hi, ml, cap, tb, tcap); if (k > cap) k = cap;
    for (int i = 0; i < k; i++) { rl[i].s = ml[i].s; rl[i].e = ml[i].e; rl[i].t = ml[i].t; }
    fprintf(stderr, "[subsync] reference: track #%d, %d lines near %.0f s after %.1f s\n", best, bestN, SY.center, now_() - SY.t0);
    SY.job = subsync_start(xs, P.ncues, rl, k, SY.center); free(ml); free(tb); free(rl); free(xs); /* (it keeps its own copies) */
    if (!SY.job) { sync_end(T("Couldn’t sync")); return; }
    SY.phase = 2; return; }
  /* its own tracks are the best reference by far: while they are being read keep waiting, up to 45 s */
  double limit = listed && st == MKV_HARVESTING ? 45 : 12;
  if (now_() - SY.t0 > limit) { fprintf(stderr, "[subsync] reference: %d lines near %.0f s after %.0f s\n", bestN, SY.center, now_() - SY.t0);
    if (ntext) sync_end(T("Still reading the film’s subtitles: try again in a minute")); else sync_by_ref("the file's tracks were not read in time"); }
}

/* ---- audio ---- */
static void set_audio(int i) { P.curAud = i; video_select_audio(i); P.infoKey[0] = 0; }
/* ---- sound this TV can't decode: DTS where it was seen failing (or not yet seen playing), TrueHD always (passed
   through to a receiver only), and any codec the TV refused on this stream (error 200: the picture plays on) ---- */
static int aud_kind(const char *codec) { /* 1 DTS, 2 TrueHD, 0 another; the TV's names ("dts") and Matroska's ("A_DTS") alike */
  char c[32]; lower(codec, c, sizeof c); return strstr(c, "dts") ? 1 : (strstr(c, "truehd") || strstr(c, "mlp")) ? 2 : 0; }
__attribute__((weak)) int video_audio_plays(int idx) { (void)idx; return -1; } /* (a platform that can't say: the codec rules below) */
static int aud_bad(const VidTrack *t) {
  { int pl = video_audio_plays(t->idx); if (pl >= 0) { if (!pl) return 1; for (int i = 0; i < P.dts.nbad; i++) if (t->codec[0] && !strcasecmp(P.dts.bad[i], t->codec)) return 1; return 0; } } /* the device says (Android TV: ExoPlayer's own decoders, FFmpeg and passthrough): TrueHD or DTS that plays there is no worse than any other; a codec seen failing in this film still is */
  int k = aud_kind(t->codec), dm = best_dts_mode(); if (k == 2 || (k == 1 && (dm == 1 || (dm == 0 && best_learned("dts") == 0) || getenv("SB_DTS_CONVERT")))) return 1; /* DTS: Convert always; Auto once the TV was seen failing it; Off left to the TV */ /* (SB_DTS_CONVERT: development, a TV that plays DTS converts it anyway) */
  for (int i = 0; i < P.dts.nbad; i++) if (t->codec[0] && !strcasecmp(P.dts.bad[i], t->codec)) return 1;
  return 0;
}
/* the TV's list, its codecs and languages filled from the file's own tracks (same order) where the TV leaves them
   out (a dub it does not name shows by its language, not as Audio 2); every list of the audio tracks reads this one */
static int aud_list(VidTrack *ts, int max) {
  int n = video_audio_tracks(ts, max); if (!n || !mkv_listed(mkv)) return n;
  MkvAudio ma[16]; int m = mkv_audio_tracks(mkv, ma, 16);
  if (m == n) for (int i = 0; i < n; i++) {
    if (!ts[i].codec[0]) { snprintf(ts[i].codec, sizeof ts[i].codec, "%s", ma[i].codec); if (!ts[i].channels) ts[i].channels = ma[i].channels; }
    if ((!ts[i].lang[0] || !strcmp(ts[i].lang, "und")) && ma[i].lang[0] && strcmp(ma[i].lang, "und")) snprintf(ts[i].lang, sizeof ts[i].lang, "%s", ma[i].lang); }
  return n;
}
static void play_av(char *hdr, size_t hc, char *codec, size_t cc, char *chs, size_t sc); /* (below, by the info line) */
static void aud_label(const VidTrack *t, char *out, size_t cap) { /* "AC3 5.1" */
  char c[16]; size_t i = 0; const char *s = t->codec; if (!strncmp(s, "A_", 2)) s += 2; for (; s[i] && i + 1 < sizeof c; i++) c[i] = (char)toupper((unsigned char)s[i]); c[i] = 0;
  int ch = t->channels; snprintf(out, cap, "%s%s", c, ch == 6 ? " 5.1" : ch == 8 ? " 7.1" : ch == 2 ? " 2.0" : ch == 1 ? " 1.0" : "");
}
/* Original (Settings > Audio): the track in the title's original language (TMDB's), when the file has one and it is
   not the one playing; the file's own choice otherwise. Asked while the tracks come, until it is known */
static int aud_original_mode(void) { return !strcmp(store_get("defaultAudioTrack", "orig"), "orig") || strcmp(store_get("enableDefaultAudioTrack", "false"), "true"); }
static void auto_audio_original(void) {
  if (P.isTrailer || !aud_original_mode()) { P.autoAudDone = 1; return; }
  const char *ol = tmdb_orig_lang(P.id); if (!ol) return; /* (still asked: again in a moment) */
  if (!*ol) { P.autoAudDone = 1; fprintf(stderr, "[audio] Original: the title's language isn't known, the file's own track\n"); return; }
  VidTrack ts[16]; int n = aud_list(ts, 16); if (!n) return; /* (the tracks not listed yet) */
  AudInfo ai[16]; MkvAudio ma[16]; int m = mkv && mkv_listed(mkv) ? mkv_audio_tracks(mkv, ma, 16) : 0;
  for (int i = 0; i < n; i++) { ai[i].lang = ts[i].lang; ai[i].name = ts[i].name[0] ? ts[i].name : m == n ? ma[i].name : ""; ai[i].def = m == n ? ma[i].def : 0; ai[i].bad = aud_bad(&ts[i]); }
  int pick = aud_pick_original(ol, ai, n, subs_lang_match); P.autoAudDone = 1;
  if (pick < 0) { fprintf(stderr, "[audio] Original: no %s track in this file, its own stays\n", ol); return; }
  if (ts[pick].idx == video_audio_current()) { fprintf(stderr, "[audio] Original: %s already plays (%d of %d)\n", ol, pick + 1, n); return; }
  set_audio(ts[pick].idx); fprintf(stderr, "[audio] Original: the %s track (%d of %d: %s)\n", ol, pick + 1, n, ai[pick].name); }
static void auto_audio(void) {
  if (P.autoAudDone) return; const char *want = aud_want(), *an = js_str(show_pref(), "an", ""); if (!*want) { auto_audio_original(); return; } /* (this show's own, else the settings'; neither: Original) */
  VidTrack ts[16]; int n = aud_list(ts, 16); if (!n) return; int pick = -1;
  if (*an) for (int i = 0; i < n; i++) if (lang_match(ts[i].lang, want) && !strcmp(ts[i].name, an) && !aud_bad(&ts[i])) { pick = i; break; } /* the same track by its name */
  if (pick >= 0) { P.autoAudDone = 1; set_audio(ts[pick].idx); fprintf(stderr, "[audio] this show's track: %s\n", an); return; }
  for (int i = 0; i < n; i++) if (lang_match(ts[i].lang, want) && (pick < 0 || (aud_bad(&ts[pick]) && !aud_bad(&ts[i])))) pick = i; /* in that language, one whose sound plays here first */
  const char *dw = aud_setting();
  if (pick < 0 && *dw && !lang_match(dw, want)) { for (int i = 0; i < n; i++) if (lang_match(ts[i].lang, dw) && (pick < 0 || (aud_bad(&ts[pick]) && !aud_bad(&ts[i])))) pick = i; /* this show's not in this episode: the settings' */
    if (pick >= 0) fprintf(stderr, "[audio] no %s for this show here: the settings' %s\n", want, dw); }
  if (pick < 0 && strcmp(want, "eng")) for (int i = 0; i < n; i++) if (lang_match(ts[i].lang, "eng") && (pick < 0 || (aud_bad(&ts[pick]) && !aud_bad(&ts[i])))) pick = i; /* not in the file: English, when the file has it */
  if (pick >= 0) { P.autoAudDone = 1; set_audio(ts[pick].idx); fprintf(stderr, "[audio] %s track chosen: %s (%d of %d)\n", want, ts[pick].lang, pick + 1, n); }
}
/* No track this TV plays: the file is converted on the TV itself (video copied, the sound as Dolby Digital 5.1) and
   played from a local HLS stream through the same pipeline, from the same moment; the subtitles still read the file.
   Not for a stream that isn't a plain http file, nor HEVC where this TV showed it can't play it from HLS. tvIdx: the
   track in the TV's list (the same order as the file's), -1 the first DTS / TrueHD one. 1 started */
static int dv7_wanted(void);
static int dts_convert(int tvIdx, const char *why) {
  if (P.isTrailer || P.isHls || strncmp(P.url, "http", 4) || !hls_conv_available()) return 0;
  char vc[32] = ""; int hevc = mkv_video_codec(mkv, vc, sizeof vc) ? strstr(vc, "HEVC") != NULL : has_word(P.sname, "hevc") || has_word(P.sname, "x265") || has_word(P.sname, "h265");
  if (hevc && best_learned("hlsHevcOk") == 0) { fprintf(stderr, "[dts] no conversion: HEVC from local HLS failed on this TV before\n"); return 0; }
  int ord = tvIdx; MkvAudio ma[16]; int m = mkv_audio_tracks(mkv, ma, 16);
  if (ord >= m || (ord >= 0 && !aud_kind(ma[ord].codec))) ord = -1; /* (the TV's list and the file's disagree: the first DTS / TrueHD one) */
  P.dts.conv = 1; P.dts.convOrd = ord; P.dts.hevc = hevc; P.dts.convT0 = now_(); P.dts.convAt = P.live ? video_time() : P.startAt;
  P.dts.dv = dv7_wanted(); P.dts.copyA = 0; P.dts.dvTried = 1; /* a profile 7 picture: 8.1 in the same run */
  hls_conv_start(P.url, ord, (mkv_has_index(mkv) == 0 ? HC_NOINDEX : 0) | (P.dts.dv == 7 ? HC_DV81 : 0));
  fprintf(stderr, "[dts] path c: converting audio track %d (%s, %s%s) at %.0f s\n", ord, why, hevc ? "HEVC" : vc[0] ? vc : "video", P.dts.dv == 7 ? ", Dolby Vision 7 as 8.1" : P.dts.dv ? ", Dolby Vision kept" : "", P.dts.convAt);
  toast(T("Converting the sound for this TV…"), 4000, NULL);
  return 1;
}
/* ---- Dolby Vision profile 7 (Blu-ray remuxes: a base layer, an enhancement layer and the RPU in one track). A TV
   with Dolby Vision shows such a file as HDR10 only; converted here to single-layer profile 8.1 (the enhancement
   layer dropped, each RPU rewritten, the base layer as it is) it shows as Dolby Vision, through the same pipeline from
   local HLS, with the audio track copied (converted when this TV can't decode it). Only for an http file whose head
   says profile 7, on a TV with Dolby Vision, with the setting on, and unless this TV has shown that HEVC from local
   HLS fails or that the converted picture still shows as HDR10. Anything that fails plays the file as before.
   A profile 8 or 5 picture in Matroska (web copies) goes the same way with its RPU as it is: the TV showed it as HDR10 ---- */
static int dv7_wanted(void) {
  if (P.isTrailer || P.isHls || strncmp(P.url, "http", 4) || !hls_conv_available()) return 0;
  if (!strcmp(store_get("dv7Convert", "true"), "false") || best_learned("hlsHevcOk") == 0 || best_learned("dv81Hls") == 0) return 0;
  const TvCaps *c = best_caps(); if (!c || !c->dv) return 0;
  int el = 0, p = mkv_dovi(mkv, &el); return p == 7 || ((p == 8 || p == 5) && !el) ? p : 0; /* 8 and 5 (web copies) too: from a Matroska file the TV showed them as HDR10 */
}
static int aud_copyable(const char *codec) { /* the tracks the stream can carry as they are */
  static const char *const OK[] = { "A_AC3", "A_EAC3", "A_AAC", "A_DTS", "A_TRUEHD", "A_MPEG/L3", "A_MPEG/L2" };
  for (int i = 0; i < 7; i++) if (!strncmp(codec, OK[i], strlen(OK[i]))) return 1; return 0;
}
static int mkv_aud_bad(const MkvAudio *a) { VidTrack v; memset(&v, 0, sizeof v); snprintf(v.codec, sizeof v.codec, "%s", a->codec); return aud_bad(&v); }
/* the track that goes with the converted picture: the default-audio language, else the file's default, else the first;
   one this TV can't decode gives way to one it plays in the same language, else it is converted. -1 none fits */
static int dv7_audio(int *copy) {
  MkvAudio ma[16]; int m = mkv_audio_tracks(mkv, ma, 16), pick = -1; if (!m) return -1;
  const char *want = aud_want();
  for (int i = 0; i < m && pick < 0; i++) if (*want && lang_match(ma[i].lang, want)) pick = i;
  for (int i = 0; i < m && pick < 0; i++) if (ma[i].def) pick = i;
  if (pick < 0) pick = 0;
  if (mkv_aud_bad(&ma[pick]) || !aud_copyable(ma[pick].codec))
    for (int i = 0; i < m; i++) if (!mkv_aud_bad(&ma[i]) && aud_copyable(ma[i].codec) && !strcasecmp(ma[i].lang, ma[pick].lang)) { pick = i; break; }
  if (!mkv_aud_bad(&ma[pick]) && aud_copyable(ma[pick].codec)) { *copy = 1; return pick; }
  if (aud_kind(ma[pick].codec)) { *copy = 0; return pick; } /* DTS / TrueHD this TV can't decode: as Dolby Digital */
  return -1;
}
static int conv_flags(int ord) { /* another audio track while converting */
  MkvAudio ma[16]; int m = mkv_audio_tracks(mkv, ma, 16), copy = P.dts.dv && ord >= 0 && ord < m && !mkv_aud_bad(&ma[ord]) && aud_copyable(ma[ord].codec);
  return (mkv_has_index(mkv) == 0 ? HC_NOINDEX : 0) | (P.dts.dv == 7 ? HC_DV81 : 0) | (copy ? HC_COPYAUDIO : 0);
}
static int dv7_convert(const char *why) {
  int prof = P.dts.conv || P.dts.dvTried ? 0 : dv7_wanted(); if (!prof) return 0;
  P.dts.dvTried = 1; int copy = 1, ord = dv7_audio(&copy);
  if (ord < 0) { fprintf(stderr, "[dv] profile %d, but no audio track can go with it: played as it is\n", prof); return 0; }
  P.dts.conv = 1; P.dts.convOrd = ord; P.dts.dv = prof; P.dts.copyA = copy; P.dts.hevc = 1; P.dts.convT0 = now_(); P.dts.convAt = P.live ? video_time() : P.startAt;
  hls_conv_start(P.url, ord, (mkv_has_index(mkv) == 0 ? HC_NOINDEX : 0) | (prof == 7 ? HC_DV81 : 0) | (copy ? HC_COPYAUDIO : 0));
  fprintf(stderr, "[dv] profile %d (%s): the picture %s, audio track %d %s, from %.0f s\n", prof, why, prof == 7 ? "as 8.1" : "as it is, its Dolby Vision kept", ord, copy ? "copied" : "as Dolby Digital", P.dts.convAt);
  return 1;
}
/* A stream whose text says Dolby Vision from a Blu-ray (a remux, FEL / MEL) or in a Matroska file, on a TV with Dolby Vision:
   the first load waits for the file's head (a moment), so its picture starts converted instead of switching after a second */
static int dv7_defer_wanted(void) {
  if (P.isTrailer || P.isHls || strncmp(P.url, "http", 4) || !hls_conv_available()) return 0;
  if (!strcmp(store_get("dv7Convert", "true"), "false") || best_learned("hlsHevcOk") == 0 || best_learned("dv81Hls") == 0) return 0;
  const TvCaps *c = best_caps(); if (!c || !c->dv) return 0;
  char l[1100]; snprintf(l, sizeof l, "%s %s", P.sname, P.st.filename); for (char *q = l; *q; q++) *q = (char)tolower((unsigned char)*q);
  int dv = has_word(l, "dv") || has_word(l, "dovi") || strstr(l, "dolby vision") || has_word(l, "dolbyvision");
  return dv && (strstr(l, ".mkv") || strstr(l, "mkv") || strstr(l, "remux") || strstr(l, "bluray") || strstr(l, "blu-ray") || has_word(l, "bdmv") || has_word(l, "fel") || has_word(l, "mel") || has_word(l, "p7") || has_word(l, "dvp7")); /* (a Matroska web copy too: profile 8 goes the same way) */
}
/* while converting: the stream once it is ready, then whether the TV plays it (HEVC in HLS: learned once) */
static void dts_conv_tick(void) {
  if (P.dts.conv == 1) {
    char url[256], why[160]; HlsConvInfo ci; int st = hls_conv_poll(url, sizeof url, why, sizeof why, &ci);
    if (!st) return;
    if (st < 0 && P.dts.dv && P.dts.copyA) { /* the picture only: the file as it is, nothing said */
      P.dts.conv = 0; P.dts.dv = 0; fprintf(stderr, "[dv] the converter can't take this file (%s): played as it is\n", why);
      if (video_state() == VID_IDLE) { P.live = 0; P.liveT0 = 0; P.didResume = 0; video_load(P.url); }
      return; }
    if (st < 0 && P.dts.dv) { /* the picture couldn't be converted: the sound alone */
      fprintf(stderr, "[dv] the converter can't take this picture (%s): the sound alone\n", why); P.dts.dv = 0; hls_conv_start(P.url, P.dts.convOrd, conv_flags(P.dts.convOrd)); return; }
    if (st < 0) { P.dts.conv = 0; P.dts.gaveUp = 1; fprintf(stderr, "[dts] conversion failed: %s\n", why); toast(T("This TV can’t play this video’s sound (DTS or TrueHD)"), 6000, NULL);
      if (video_state() == VID_IDLE) { P.live = 0; P.liveT0 = 0; P.didResume = 0; video_load(P.url); } /* the picture as before, without its sound */
      return; }
    P.dts.conv = 2; P.dts.hevc = strstr(ci.vcodec, "hevc") != NULL; P.dts.convT0 = now_(); P.dts.okAt = 0;
    { const char *fc = strstr(ci.path, "E-AC3") || strstr(ci.path, "EAC3") ? "E-AC3" : strstr(ci.path, "AC3") ? "AC3" : strstr(ci.path, "AAC") ? "AAC" : ""; char chs[8] = ""; if (ci.outCh >= 3) snprintf(chs, sizeof chs, " %d.1", ci.outCh - 1); else if (ci.outCh > 0) snprintf(chs, sizeof chs, " %d.0", ci.outCh);
      snprintf(P.dts.outDesc, sizeof P.dts.outDesc, "%s%s", fc, *fc ? chs : ""); } /* (what the TV gets: "AC3 5.1") */
    double at = P.live && video_has_media() ? video_time() : P.dts.convAt; if (at < 1) at = P.startAt;
    P.live = 0; P.liveT0 = 0; P.didResume = 0; P.startAt = at > 1 ? at : P.startAt; P.curAud = -1; P.infoKey[0] = 0;
    fprintf(stderr, "[dts] playing the converted stream (%s, %s; %s) from %.0f s\n", ci.indexed ? "segments" : "one stream", ci.vcodec, ci.path, at);
    P.dts.dvSeen = 0; P.dts.dvAt = 0;
    if (ci.indexed) video_load(url); else video_load_timed(url, ci.dur);
    return;
  }
  if (P.dts.conv == 2 && P.dts.hevc && best_learned("hlsHevcOk") < 0) { /* HEVC in local HLS: plays here or not, learned once */
    if (video_state() == VID_PLAYING && video_live()) { if (!P.dts.okAt) P.dts.okAt = now_(); else if (now_() - P.dts.okAt >= 3) best_learn("hlsHevcOk", 1); } else P.dts.okAt = 0; }
  if (P.dts.conv == 2 && P.dts.dv && !P.dts.dvSeen) { /* the converted picture: Dolby Vision on screen, or HDR10 all the same (then not converted again here) */
    if (video_state() == VID_PLAYING && video_live()) { if (!P.dts.dvAt) P.dts.dvAt = now_();
      else if (now_() - P.dts.dvAt >= 3) { char h[32]; if (video_hdr(h, sizeof h)) { P.dts.dvSeen = 1; int yes = strcasestr(h, "dolby") != NULL;
          fprintf(stderr, "[dv] the TV shows the converted picture as %s\n", h); if (yes || strcasestr(h, "hdr")) best_learn("dv81Hls", yes); }
        else if (now_() - P.dts.dvAt >= 15) P.dts.dvSeen = 1; } } /* (this TV doesn't say) */
    else P.dts.dvAt = 0; }
}
static void dts_conv_failed(const char *why) { /* the converted stream didn't play: today's reason, and HEVC from HLS is not tried again here */
  fprintf(stderr, "[dts] the converted stream didn't play (%s)\n", why);
  video_unload(); /* first: the TV let go of the stream, so what it still asks of it (gone once the run stops) is not taken for this attempt's error */
  if (P.dts.dv) { /* the converted picture: not converted again on this TV; from here the file as it is, or (DTS) the sound alone converted */
    best_learn("dv81Hls", 0); hls_conv_stop(); P.dts.dv = 0;
    double at = P.lastPos > 1 ? P.lastPos : P.dts.convAt; P.startAt = at > 1 ? at : P.startAt; P.live = 0; P.liveT0 = 0; P.didResume = 0; P.curAud = -1;
    if (P.dts.copyA) { P.dts.conv = 0; video_load(P.url); return; }
    P.dts.conv = 1; P.dts.convAt = P.startAt; P.dts.convT0 = now_(); hls_conv_start(P.url, P.dts.convOrd, conv_flags(P.dts.convOrd));
    fprintf(stderr, "[dts] the sound alone converted from %.0f s\n", P.startAt); return; }
  if (P.dts.hevc) best_learn("hlsHevcOk", 0);
  hls_conv_stop(); P.dts.conv = 0; P.dts.gaveUp = 1; fail(T("This TV can’t play this video’s sound (DTS or TrueHD)"));
}
/* Once a second while playing: the track the TV plays is checked against what this TV decodes. DTS still unknown here
   is left to the TV (3 s of play without an error 200 teaches "yes", the error "no"); a track that can't play gives way
   to one that does, in the language of the default-audio setting, else the same language, else the first; with none,
   the reason is said once and the picture plays on */
static void dts_tick(void) {
  if (P.dts.conv) { dts_conv_tick(); return; }
  if (!P.dts.dvTried && mkv_listed(mkv) && dv7_convert("read in the file's head")) return;
  if (P.isTrailer || P.dts.gaveUp) return;
  VidTrack ts[16]; int n = aud_list(ts, 16); if (!n) return; /* only after the TV listed them: choosing earlier stalls it */
  int cur = P.curAud >= 0 ? P.curAud : video_audio_current(), ci = 0; for (int i = 0; i < n; i++) if (ts[i].idx == cur) ci = i;
  VidTrack *t = &ts[ci]; int kind = aud_kind(t->codec), unsup = video_audio_unsupported();
  if (kind == 1 && best_dts_mode() == 2) return; /* DTS sound Off: the TV does with it whatever it can, nothing here steps in */
  if (unsup) { if (kind == 1 && !best_dts_mode()) { best_learn("dts", 0); fprintf(stderr, "[dts] auto: this TV refused DTS, converting from now on\n"); } int known = 0; for (int i = 0; i < P.dts.nbad; i++) if (!strcasecmp(P.dts.bad[i], t->codec)) known = 1;
    if (!known && t->codec[0] && P.dts.nbad < 4) snprintf(P.dts.bad[P.dts.nbad++], sizeof P.dts.bad[0], "%s", t->codec); }
  if (kind == 1 && !unsup && !best_dts_mode() && best_learned("dts") < 0 && video_state() == VID_PLAYING) { if (!P.dts.playAt) P.dts.playAt = now_(); else if (now_() - P.dts.playAt >= 3) { best_learn("dts", 1); fprintf(stderr, "[dts] auto: this TV plays DTS itself\n"); } }
  else P.dts.playAt = 0;
  if (!unsup && !aud_bad(t)) return;
  const char *want = aud_want();
  int pick = -1, score = -1;
  for (int i = 0; i < n && P.dts.switches < 4; i++) { if (i == ci || aud_bad(&ts[i])) continue; /* (tracks the TV doesn't name: a few tries at most) */
    int sc = (*want && lang_match(ts[i].lang, want)) ? 2 : (t->lang[0] && !strcasecmp(ts[i].lang, t->lang)) ? 1 : 0; if (sc > score) { score = sc; pick = i; } }
  const char *what = kind == 2 ? "TrueHD" : "DTS";
  if (pick >= 0) {
    char lb[32], msg[160]; aud_label(&ts[pick], lb, sizeof lb);
    snprintf(msg, sizeof msg, "%s", kind == 2 ? T1("Playing the {t} track: this TV can’t play TrueHD", "t", lb) : T1("Playing the {t} track: this TV can’t play DTS", "t", lb));
    fprintf(stderr, "[dts] path b: %s track %d (%s) can't play here%s, track %d (%s %s) instead\n", what, ci, t->codec, unsup ? " (error 200)" : "", pick, ts[pick].codec, ts[pick].lang);
    P.autoAudDone = 1; set_audio(ts[pick].idx); P.dts.switches++; /* (the automatic pick, still retrying, never switches back to the track that can't play) */ if (!P.dts.said) { P.dts.said = 1; toast(msg, 4000, NULL); }
    return;
  }
  if (dts_convert(ci, unsup ? "error 200" : "known")) return;
  P.dts.gaveUp = 1;
  fprintf(stderr, "[dts] path c: only %s here (track %d, %s)%s: no track this TV plays\n", what, ci, t->codec, unsup ? ", error 200" : "");
  toast(T("This TV can’t play this video’s sound (DTS or TrueHD)"), 6000, NULL);
}

/* ---- skips: intro / recap / credits. Sources, best first, merged per segment: the file's own chapters (a reader
   hooks in below), TheIntroDB and IntroDB by IMDb id, AniSkip for anime from Kitsu-based addons. A segment that
   doesn't fit this file's length is dropped and the next source's is used. One source with something useful
   gives the others 2.5 s more; whatever answered by 14 s is used. API answers are kept 6 h; errors never ---- */
void (*player_chapters_hook)(const char *url, int p2p);
enum { SK_TIDB, SK_IDB, SK_KITSU, SK_ANI };
static struct { char url[440]; double at; char *body; } skCache[120]; static int skCacheN; static unsigned skTok;
static const char *sk_cached(const char *u) { for (int i = 0; i < skCacheN; i++) if (!strcmp(skCache[i].url, u)) return now_() - skCache[i].at < 6 * 3600 ? skCache[i].body : NULL; return NULL; }
static void sk_keep(const char *u, const char *body) {
  int i = 0; for (; i < skCacheN; i++) if (!strcmp(skCache[i].url, u)) break;
  if (i == skCacheN) { if (skCacheN < 120) skCacheN++; else { free(skCache[0].body); memmove(skCache, skCache + 1, sizeof skCache[0] * 119); i = 119; skCache[i].body = NULL; } }
  free(skCache[i].body); snprintf(skCache[i].url, sizeof skCache[i].url, "%s", u); skCache[i].body = strdup(body); skCache[i].at = now_();
}
static int sk_fits(int k, double s0, double e0) { double d = P.sk.d; if (!d) return 1; if (s0 >= d || (k != 2 && e0 > d + 30)) return 0; /* (credits "to the end", asked before the length was known, end later: trimmed to it) */
  if (k == 2 && P.sk.isEp && d < 4200 && d - s0 > 600) return 0; /* an episode's credits starting over 10 min before its end: not its credits */
  return k == 2 ? s0 >= d * 0.5 : s0 < d * 0.4 && e0 - s0 >= 3 && e0 - s0 <= 420; }
static void sk_seg(int *has, double *s, double *e, double a, double b) { *has = a >= 0 && b > a; if (*has) { *s = a; *e = b; } }
static void sk_add(int rank, const int has[3], const double ss[3], const double ee[3]) {
  if (P.sk.nsrc >= 4) return; int i = P.sk.nsrc++; P.sk.src[i].rank = rank; int any = 0;
  for (int k = 0; k < 3; k++) { P.sk.src[i].g[k].has = has[k]; P.sk.src[i].g[k].s = ss[k]; P.sk.src[i].g[k].e = ee[k]; any |= has[k]; }
  for (int j = i; j > 0 && P.sk.src[j - 1].rank > P.sk.src[j].rank; j--) { __typeof__(P.sk.src[0]) t = P.sk.src[j]; P.sk.src[j] = P.sk.src[j - 1]; P.sk.src[j - 1] = t; }
  if (any && !P.sk.quickAt) P.sk.quickAt = now_() + 2.5;
}
static void sk_finish(void) {
  if (P.sk.sent) return; P.sk.sent = 1; P.sk.tok = 0; double d = P.sk.d; int has[3] = { 0 }; double ss[3] = { 0 }, ee[3] = { 0 };
  for (int k = 0; k < 3; k++) for (int j = 0; j < P.sk.nsrc; j++) { const __typeof__(P.sk.src[0].g[0]) *g = &P.sk.src[j].g[k]; if (!g->has || !sk_fits(k, g->s, g->e)) continue;
      if (k == 2 && has[2] && (P.sk.src[j].rank == 0 || g->s <= ss[2])) continue; /* the credits: the file's own chapter, else the latest the sources give (two sources can be minutes apart for one episode: the card comes no earlier than the latest) */
      has[k] = 1; ss[k] = g->s; ee[k] = g->e; if (k != 2 || P.sk.src[j].rank == 0) break; }
  if (has[2] && d && ee[2] > d) ee[2] = d;
  /* credits learned from this show's other episodes: how long before the end they start */
  if (episodic()) { char lk[96]; snprintf(lk, sizeof lk, "creditsOff_%s", P.id);
    if (has[2] && d && d - ss[2] > 10 && d - ss[2] < 600) { char v[16]; snprintf(v, sizeof v, "%d", (int)(d - ss[2] + 0.5)); store_set(lk, v); }
    else if (!has[2] && d > 600) { double off = atof(store_get(lk, "0")); if (off > 90) off = 90; /* (another episode's: never earlier than with no credits known at all) */ if (off > 10) { has[2] = 1; ss[2] = d - off; ee[2] = d; } } }
  fprintf(stderr, "[player] skips from %d sources: intro %.0f-%.0f recap %.0f-%.0f credits %.0f-%.0f\n", P.sk.nsrc, has[0] ? ss[0] : -1, ee[0], has[1] ? ss[1] : -1, ee[1], has[2] ? ss[2] : -1, ee[2]);
  if (!has[0] && !has[1] && !has[2]) return;
  P.intro.has = has[0]; P.intro.s = ss[0]; P.intro.e = ee[0]; P.recap.has = has[1]; P.recap.s = ss[1]; P.recap.e = ee[1]; P.outro.has = has[2]; P.outro.s = ss[2]; P.outro.e = ee[2]; P.skipsOk = 1;
}
static void sk_done(void) { if (--P.sk.pending <= 0) sk_finish(); }
static void sk_ask(int i);
static double sk_ms(const cJSON *a, const char *k, double nul) { const cJSON *v = cJSON_GetObjectItemCaseSensitive(a, k); return !v || cJSON_IsNull(v) ? nul : cJSON_IsNumber(v) ? v->valuedouble / 1000 : -1; }
static double sk_sec(const cJSON *o, const char *k, double nul) { const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k); return !v || cJSON_IsNull(v) ? nul : cJSON_IsNumber(v) ? v->valuedouble : cJSON_IsString(v) ? atof(v->valuestring) : -1; }
/* one source's answer (j NULL: nothing usable) */
static void sk_answer(int i, const cJSON *j) {
  int kind = P.sk.job[i].kind, has[3] = { 0 }; double ss[3] = { 0 }, ee[3] = { 0 }, d = P.sk.d;
  if (kind == SK_TIDB) {
    if (!j || (P.sk.isEp && cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(j, "episode")) && ((int)js_num(j, "season", -1) != P.sk.se || (int)js_num(j, "episode", -1) != P.sk.en))) { sk_done(); return; } /* another episode: not ours */
    static const char *K[3] = { "intro", "recap", "credits" };
    for (int k = 0; k < 3; k++) { const cJSON *l = js_arr(j, K[k]), *a = l ? cJSON_GetArrayItem(l, 0) : NULL; if (!cJSON_IsObject(a)) continue; double s0 = sk_ms(a, "start_ms", 0); sk_seg(&has[k], &ss[k], &ee[k], s0, sk_ms(a, "end_ms", k == 2 ? (d ? d : s0 + 600) : -1)); }
    sk_add(1, has, ss, ee);
  } else if (kind == SK_IDB) {
    if (!j) { sk_done(); return; }
    static const char *K[3] = { "intro", "recap", "outro" };
    for (int k = 0; k < 3; k++) { const cJSON *o = js_obj(j, K[k]); if (!o) continue; double s0 = sk_sec(o, "start_sec", -1); sk_seg(&has[k], &ss[k], &ee[k], s0, sk_sec(o, "end_sec", k == 2 ? (d ? d : s0 + 600) : -1)); }
    sk_add(2, has, ss, ee);
  } else if (kind == SK_KITSU) { /* the MyAnimeList id, then its skip times (the same job) */
    const cJSON *dt = js_arr(j, "data"), *m0 = dt ? cJSON_GetArrayItem(dt, 0) : NULL; const char *mal = js_str(js_obj(m0, "attributes"), "externalId", NULL);
    if (!mal) { sk_done(); return; }
    char enc[64]; size_t o = 0; for (const char *c = mal; *c && o + 4 < sizeof enc; c++) { if (isalnum((unsigned char)*c) || strchr("-_.~", *c)) enc[o++] = *c; else o += snprintf(enc + o, sizeof enc - o, "%%%02X", (unsigned char)*c); } enc[o] = 0;
    P.sk.job[i].kind = SK_ANI; P.sk.job[i].tries = 0; snprintf(P.sk.job[i].url, sizeof P.sk.job[i].url, "https://api.aniskip.com/v2/skip-times/%s/%d?types=op&types=ed&types=recap&types=mixed-op&types=mixed-ed&episodeLength=0", enc, P.sk.en); sk_ask(i); return;
  } else {
    const cJSON *r = js_bool(j, "found", 0) ? js_arr(j, "results") : NULL, *x;
    if (r) cJSON_ArrayForEach(x, r) { const cJSON *iv = js_obj(x, "interval"); const char *t = js_str(x, "skipType", ""); int hs; double a, b; sk_seg(&hs, &a, &b, sk_sec(iv, "startTime", -1), sk_sec(iv, "endTime", -1)); if (!hs) continue;
      int k = (!strcmp(t, "op") || !strcmp(t, "mixed-op")) ? 0 : (!strcmp(t, "ed") || !strcmp(t, "mixed-ed")) ? 2 : !strcmp(t, "recap") ? 1 : -1; if (k >= 0 && !has[k]) { has[k] = 1; ss[k] = a; ee[k] = b; } }
    sk_add(3, has, ss, ee);
  }
  sk_done();
}
/* a real answer, or a real "nothing for this title" (kept too); anything else is asked again twice, 1.5 s apart */
static void on_sk(int st, const unsigned char *body, size_t len, void *ud) {
  (void)len; unsigned v = (unsigned)(uintptr_t)ud; int i = v & 3; if (!P.open || !P.sk.tok || v >> 2 != P.sk.tok || !P.sk.job[i].live) return;
  cJSON *j = st > 0 ? net_json(body) : NULL; const cJSON *err = cJSON_GetObjectItemCaseSensitive(j, "error");
  int real = cJSON_IsObject(j) && !err && (cJSON_GetObjectItemCaseSensitive(j, "imdb_id") || cJSON_GetObjectItemCaseSensitive(j, "tmdb_id") || cJSON_GetObjectItemCaseSensitive(j, "found") || (P.sk.job[i].kind == SK_KITSU && js_arr(j, "data")));
  int miss = 0; if (cJSON_IsString(err)) { char e[96]; lower(err->valuestring, e, sizeof e); miss = strstr(e, "not found") != NULL; }
  if (!real && !miss && ++P.sk.job[i].tries < 3) { cJSON_Delete(j); P.sk.job[i].retryAt = now_() + 1.5; return; }
  if (real || miss) sk_keep(P.sk.job[i].url, real ? (const char *)body : "{}");
  P.sk.job[i].live = 0; sk_answer(i, real ? j : NULL); cJSON_Delete(j);
}
static void sk_ask(int i) {
  const char *c = sk_cached(P.sk.job[i].url);
  if (c) { cJSON *j = cJSON_Parse(c); P.sk.job[i].live = 0; int ok = cJSON_IsObject(j) && cJSON_GetArraySize(j) > 0; sk_answer(i, ok ? j : NULL); cJSON_Delete(j); return; }
  P.sk.job[i].live = 1; P.sk.job[i].retryAt = 0; net_get(P.sk.job[i].url, on_sk, (void *)(uintptr_t)((P.sk.tok << 2) | (unsigned)i));
}
static void sk_job(const char *url, int kind) { int i = 0; while (i < 3 && P.sk.job[i].url[0]) i++; if (i >= 3) return; snprintf(P.sk.job[i].url, sizeof P.sk.job[i].url, "%s", url); P.sk.job[i].kind = kind; P.sk.pending++; sk_ask(i); }
static void sk_tick(void) {
  if (!P.sk.tok || P.sk.sent) return; double n = now_();
  for (int i = 0; i < 3; i++) if (P.sk.job[i].live && P.sk.job[i].retryAt && n >= P.sk.job[i].retryAt) sk_ask(i);
  if (!P.sk.sent && ((P.sk.quickAt && n >= P.sk.quickAt) || n >= P.sk.endAt)) sk_finish();
}
static void load_skips(void) {
  if (P.isTrailer || P.skipsAsked || !P.id[0]) return; P.skipsAsked = 1;
  const char *ts = getenv("SB_PLAY_SKIPS"); /* development: "intro:s-e,recap:s-e,outro:s-e" instead of the sources (the page's skipsForTest) */
  if (ts) { for (const char *c = ts; c && *c; c = strchr(c, ',') ? strchr(c, ',') + 1 : NULL) { double a = 0, b = 0; if (sscanf(strchr(c, ':') ? strchr(c, ':') + 1 : c, "%lf-%lf", &a, &b) < 2) continue;
      __typeof__(P.intro) *g = !strncmp(c, "intro", 5) ? &P.intro : !strncmp(c, "recap", 5) ? &P.recap : &P.outro; g->has = 1; g->s = a; g->e = b; } P.skipsOk = 1; return; }
  memset(&P.skOn, 0, sizeof P.skOn); P.sk.tok = ++skTok & 0x3fffffff; if (!P.sk.tok) P.sk.tok = ++skTok; P.sk.d = (int)(dur() + 0.5); P.sk.endAt = now_() + 14; P.sk.pending = 1; /* 1 = still adding jobs */
  /* 0. the file's own chapters, when a reader is there */
  if (player_chapters_hook && !P.isHls) { P.sk.chapWait = 1; P.sk.pending++; player_chapters_hook(P.url, P.isP2p); }
  char u[440]; const char *vid = P.vid; int ne = 0; const char *c = vid; while ((c = strchr(c, ':'))) { ne++; c++; }
  if (!strncmp(vid, "kitsu:", 6)) { const char *a = vid + 6; const char *b = strchr(a, ':'); P.sk.en = b ? atoi(b + 1) : 1; char kid[32]; snprintf(kid, sizeof kid, "%.*s", b ? (int)(b - a) : (int)strlen(a), a);
    if (atoi(kid) > 0) { snprintf(u, sizeof u, "https://kitsu.io/api/edge/anime/%d/mappings?filter%%5BexternalSite%%5D=myanimelist%%2Fanime", atoi(kid)); sk_job(u, SK_KITSU); } }
  const char *imdb = !strncmp(P.id, "tt", 2) ? P.id : !strncmp(vid, "tt", 2) ? vid : NULL;
  if (imdb) { char im[24]; int k = 2; while (k < 22 && isdigit((unsigned char)imdb[k])) k++; snprintf(im, sizeof im, "%.*s", k, imdb);
    P.sk.isEp = !strcmp(P.type, "series") && ne >= 2; if (P.sk.isEp) { const char *l = strrchr(vid, ':'), *pp = l - 1; while (pp > vid && *pp != ':') pp--; P.sk.se = atoi(pp + 1); P.sk.en = atoi(l + 1); }
    if (P.sk.isEp) snprintf(u, sizeof u, "https://api.theintrodb.org/v3/media?imdb_id=%s&season=%d&episode=%d", im, P.sk.se, P.sk.en); else snprintf(u, sizeof u, "https://api.theintrodb.org/v3/media?imdb_id=%s", im); sk_job(u, SK_TIDB);
    if (P.sk.isEp) snprintf(u, sizeof u, "https://api.introdb.app/segments?imdb_id=%s&season=%d&episode=%d", im, P.sk.se, P.sk.en); else snprintf(u, sizeof u, "https://api.introdb.app/segments?imdb_id=%s&is_movie=true", im); sk_job(u, SK_IDB); }
  if (P.sk.tok) sk_done();
}
/* the chapters a reader found in this file: the best skip source ("Opening", "Recap", "End Credits"...); n <= 0: none */
static int ch_word(const char *n, const char *const *w) {
  for (; *w; w++) { size_t l = strlen(*w); for (const char *p = n; (p = strstr(p, *w)); p++) { int a = p == n || !(isalnum((unsigned char)p[-1]) || p[-1] == '_'), b = !(isalnum((unsigned char)p[l]) || p[l] == '_'); if (a && b) return 1; } }
  return 0;
}
void player_chapters(const char *url, const PlayerChapter *list, int n) {
  if (!P.open || !P.sk.chapWait || P.sk.sent || strcmp(url, P.url)) return; P.sk.chapWait = 0;
  P.nchap = 0; P.chLast = -1; /* every chapter, for the seek bar: its start, and its name unless it is only a number */
  for (int i = 0; i < n && P.nchap < 64; i++) { if (list[i].s < 0) continue; int at = P.nchap; while (at > 0 && P.chap[at - 1].s > list[i].s) { P.chap[at] = P.chap[at - 1]; at--; }
    P.chap[at].s = list[i].s; char nm[128]; snprintf(nm, sizeof nm, "%s", list[i].name); char *a = nm; while (*a == ' ') a++; char *e = a + strlen(a); while (e > a && e[-1] == ' ') *--e = 0;
    char lw[128]; lower(a, lw, sizeof lw); const char *q = lw; static const char *const NUMW[] = { "chapter", "chapitre", "kapitel", "capítulo", "capitulo", "capitolo", "hoofdstuk", "chap", NULL };
    for (int k = 0; NUMW[k]; k++) if (!strncmp(q, NUMW[k], strlen(NUMW[k]))) { q += strlen(NUMW[k]); break; } while (*q == ' ' || *q == '.' || *q == '-') q++;
    const char *dg = q; while (isdigit((unsigned char)*dg) || *dg == ':' || *dg == '.') dg++; snprintf(P.chap[at].nm, sizeof P.chap[at].nm, "%s", *a && *dg ? a : ""); /* (only a number, or a time: unnamed) */
    P.nchap++; }
  if (P.nchap) fprintf(stderr, "[player] %d chapters\n", P.nchap);
  static const char *const IN[] = { "intro", "opening", "op", "title sequence", "main title", "main titles", "theme song", "générique", "generique", "genérique", "génerique", "vorspann", NULL };
  static const char *const RE[] = { "recap", "previously", "résumé", "resume", "résume", "resumé", "rückblick", "ruckblick", "summary", NULL };
  static const char *const CR[] = { "credit", "credits", "end credits", "endcredits", "ending", "ed", "outro", "closing", "end title", "end titles", "générique de fin", "generique de fin", "abspann", NULL };
  int has[3] = { 0 }; double ss[3] = { 0 }, ee[3] = { 0 };
  for (int i = 0; i < n; i++) { char nm[128]; lower(list[i].name, nm, sizeof nm); if (list[i].e <= list[i].s) continue;
    const char *q = nm; static const char *const NUM[] = { "chapter", "chapitre", "kapitel", "capítulo", "capitulo", "capitolo", "hoofdstuk", NULL };
    for (int k = 0; NUM[k]; k++) if (!strncmp(q, NUM[k], strlen(NUM[k]))) { q += strlen(NUM[k]); break; }
    while (*q == ' ' || *q == '\t') q++; if (*q) { const char *d = q; while (isdigit((unsigned char)*d)) d++; if (d > q && !*d) continue; } /* "Chapter 3": no name */
    if (strstr(nm, "cold open") || strstr(nm, "teaser")) continue;
    int k = ch_word(nm, IN) && !has[0] ? 0 : ch_word(nm, RE) && !has[1] ? 1 : ch_word(nm, CR) && !has[2] ? 2 : -1;
    if (k < 0) continue; has[k] = 1; ss[k] = list[i].s; ee[k] = list[i].e;
    if (k == 2 && i + 1 < n) { char nx[128]; lower(list[i + 1].name, nx, sizeof nx); const char *pc = strstr(nx, "post"); /* credits, then a preview / post-credits scene: stop there */
      if (strstr(nx, "preview") || strstr(nx, "next episode") || strstr(nx, "next time") || strstr(nx, "stinger") || strstr(nx, "bonus") || (pc && (!strncmp(pc + 4, "credits", 7) || (pc[4] && !strncmp(pc + 5, "credits", 7))))) ee[k] = list[i + 1].s; } }
  if (n > 0) sk_add(0, has, ss, ee);
  sk_done();
}

/* ---- buttons ---- */
static void build_btns(void) {
  const char *fid = P.nbtn && P.bIdx < P.nbtn ? P.btns[P.bIdx].id : NULL; /* the focus stays on the same button when the row changes */
  P.nbtn = 0; int i = 0;
  #define B(ID, IC, LB, BIG) do { P.btns[i].id = ID; P.btns[i].icon = IC; snprintf(P.btns[i].lb, 48, "%s", LB); P.btns[i].big = BIG; P.btns[i].nx = 0; i++; } while (0)
  if (live_title() && live_catalogs() > 0) B("chn", "wn_live", T("Channels"), 1); else B("pp", "p_pause", "", 1); /* a channel: its big button is the channels (play / pause on the remote's key) */
  if (!P.isTrailer && P.next.has) { B("nxt", "p_next", T("Next episode"), 0); P.btns[i - 1].nx = 1; }
  if (!P.isTrailer && !live_title()) { B("beg", "restart", T("Play from the beginning"), 0); P.btns[i - 1].nx = 1; } /* films and episodes alike (a broadcast has no beginning) */
  P.nL = i;
  if (!P.isTrailer && episodic() && P.videos && cJSON_GetArraySize(P.videos)) B("eps", "p_eps", T("Episodes"), 0);
  if (!P.isTrailer && !live_title()) B("src", "p_src", T("Sources"), 0); /* (a channel: the working stream is found for it) */
  if (P.isTrailer) { if (trSubsN) B("cc", "p_subs", T("Subtitles"), 0); } /* a trailer's captions: one press on, one off */
  else if (!live_title()) B("subs", "p_subs", T("Subtitles"), 0); /* (a live channel: none to choose) */
  if (!P.isTrailer) B("aud", "p_aud", T("Audio"), 0);
  const char *am = !strcmp(P.aspect, "fill") ? "Fill the screen" : !strcmp(P.aspect, "zoom") ? "Zoom" : "Original"; B("asp", "p_asp", T(am), 0);
  #undef B
  P.nbtn = i; if (P.bIdx >= i) P.bIdx = 0; if (fid) for (int k = 0; k < i; k++) if (!strcmp(P.btns[k].id, fid)) P.bIdx = k;
  for (int k = 0; k < i; k++) { P.btns[k].lw = P.btns[k].lb[0] ? text_width(P.btns[k].lb, 24, 700, 0) + 22 : 0; P.btns[k].w = P.btns[k].big ? 94 : 78; }
  /* where they sit before any slides aside (76 / 92 plus a 1 px edge): the left group from the left, the right group from the right */
  float x = 72; for (int k = 0; k < P.nL; k++) { if (P.btns[k].nx) x += 8; P.btns[k].x0 = x; P.btns[k].y = SB_H - 54 - 94 + (94 - P.btns[k].w) / 2; x += P.btns[k].w + (P.btns[k].big ? 20 : 0); }
  x = SB_W - 72; for (int k = i - 1; k >= P.nL; k--) { x -= P.btns[k].w; P.btns[k].x0 = x; P.btns[k].y = SB_H - 54 - 94 + 8; x -= 18; }
  for (int k = 0; k < i; k++) { P.btns[k].x = P.btns[k].x0 + P.btns[k].dx.v; }
}
static int btn_index(const char *id) { for (int i = 0; i < P.nbtn; i++) if (!strcmp(P.btns[i].id, id)) return i; return -1; }

/* ---- OSD ---- */
static void arm_hide(void) { P.hideAt = now_() + 5; }
static void chips_in(void);
static void show_osd(int keepFocus) {
  if (P.mode == 0) { P.mode = 1; if (!keepFocus) { P.row = 0; P.bIdx = 0; } }
  int was = P.osd; P.skm = 0; P.osd = 1; arm_hide();
  if (!was) chips_in(); /* the info chips come in again, one after the other */
}
static void hide_osd(void) { P.mode = 0; P.osd = 0; extern void app_pointer_rest(void); app_pointer_rest(); }
/* playing again: a live broadcast paused more than 3 s joins where the broadcast is now (it went on from the paused
   moment, no longer live); anything else goes on from where it stopped */
static void play_on(void) { awayHold = 0; /* (asked to play: a phone's, behind, too) */ if (live_bcast() && P.pzSince > 0 && now_() - P.pzSince > 3) { P.pzSince = 0; video_live_rejoin(); } else video_play(); }
static void toggle(void) { if (paused()) play_on(); else { video_pause(); save_progress(1); } }

/* ---- seeking: presses add up and commit 1.2 s after the last one ---- */
/* ---- chapters on the seek bar (Settings: Chapters): gaps at each start, the one under the knob named in the seek
   pill, a seek landing close to a start lands on it, Up / Down while seeking go from start to start ---- */
static int chapters_on(void) { return P.nchap >= 2 && !P.isTrailer && !live_title() && dur() > 0 && strcmp(store_get("showChapters", "true"), "false"); }
static int chap_at(double t) { int k = 0; for (int i = 0; i < P.nchap; i++) if (P.chap[i].s <= t + 0.25) k = i; return k; }
static const char *chap_name(int i, char *buf, size_t cap) { if (P.chap[i].nm[0]) return P.chap[i].nm; double cs = P.chap[i].s; /* unnamed: the opening, recap or credits it starts, else its number */
  if (P.intro.has && fabs(cs - P.intro.s) < 6) return T("Opening"); if (P.recap.has && fabs(cs - P.recap.s) < 6) return T("Recap"); if (P.outro.has && fabs(cs - P.outro.s) < 6) return T("End credits");
  char n[8]; snprintf(n, sizeof n, "%d", i + 1); snprintf(buf, cap, "%s", T1("Chapter {n}", "n", n)); return buf; }
static void seek_text(void) {
  int dd = (int)(P.seekT - video_time() + (P.seekT >= video_time() ? 0.5 : -0.5)), a = dd < 0 ? -dd : dd; char m[16], s[16]; snprintf(m, sizeof m, "%d", a / 60); snprintf(s, sizeof s, "%d", a % 60);
  if (a >= 60) snprintf(P.seekTxt, sizeof P.seekTxt, "%s%s%s%s", dd >= 0 ? "+" : "-", T1("{m}m", "m", m), a % 60 ? " " : "", a % 60 ? T1("{s}s", "s", s) : ""); else snprintf(P.seekTxt, sizeof P.seekTxt, "%s%s", dd >= 0 ? "+" : "-", T1("{s}s", "s", s));
}
static void chap_jump(int dir) { /* to the next chapter's start, or back to this one's (the one before, within its first 3 s) */
  double d = dur(), base = P.seekT >= 0 ? P.seekT : video_time(); int k = chap_at(base), to = -1;
  if (dir > 0) { if (k + 1 < P.nchap) to = k + 1; } else to = base - P.chap[k].s > 3 ? k : (k > 0 ? k - 1 : 0);
  if (to < 0) { nudge_start(&P.nudge, K_UP); return; }
  double n = now_(); P.seekT = P.chap[to].s < d - 1 ? P.chap[to].s : d - 1; P.seekDir = dir; P.rep = 0; P.lastKeyAt = n; P.seekScrub = 0;
  if (P.mode == 0) P.skm = 1; else arm_hide();
  seek_text(); P.skOn = 1; P.seekTxtUntil = 0; P.seekCommitAt = n + 1.6;
}
static double seekStartAt; static Tween seekK; static double stripT = -1, stripFrom; static int stripHold; static double stripMovedAt; /* stripHold: the strip up (a scrub, or just after one); stripMovedAt: its time last changed */ /* (the previews' strip: its motion, the time it shows, where the scrub began) */
static void seek_by(int dir) {
  if (live_bcast()) return; /* a broadcast: no moving in it (its window is the TV's, not ours) */
  double d = dur(); if (d <= 0) return; double n = now_(); int step = atoi(store_get("seekStep", "10")); if (!step) step = 10;
  P.rep = (n - P.lastKeyAt < 0.4 && dir == P.seekDir) ? P.rep + 1 : 0; P.lastKeyAt = n; P.seekDir = dir; step *= P.rep < 4 ? 1 : P.rep < 10 ? 3 : 6;
  if (P.seekT < 0) seekStartAt = n; /* (a new scrub: the previews' progress shows a moment) */
  double base = P.seekT >= 0 ? P.seekT : video_time(); P.seekT = base + dir * step; if (P.seekT < 0) P.seekT = 0; if (P.seekT > d - 1) P.seekT = d - 1; P.seekScrub = 1;
  if (chapters_on()) { double mg = step * 0.45 < 8 ? step * 0.45 : 8; for (int i = 1; i < P.nchap; i++) { double c = P.chap[i].s; if (fabs(c - P.seekT) <= mg && (dir > 0 ? c > base : c < base)) { P.seekT = c; break; } } } /* close to a chapter's start: on it */
  if (P.mode == 0) P.skm = 1; else arm_hide();
  seek_text();
  P.skOn = 1; /* the pill stays put while it is up: only its number changes */ P.seekTxtUntil = 0; P.seekCommitAt = n + 1.2;
}
static int stripQuick; /* OK on a picture: the strip goes at once (the film is on its way there), not with the slow fade of letting go */
static void commit_seek(void);
static void commit_ok(void) { if (P.seekT >= 0 && P.seekScrub) stripQuick = 1; commit_seek(); }
static void commit_seek(void) { if (P.seekT < 0) return; if (live_bcast()) { P.seekT = -1; return; } double t = P.seekT; P.seekT = -1;
  int scrub = P.seekScrub; P.seekScrub = 0;
  if (scrub && prev_enabled() && sp_active()) { double sh = sp_shown_t(); fprintf(stderr, "[seek] accuracy: the picture shown is %.1f s from where it lands (%.1f s; a picture every %.0f s, %d made)\n", sh >= 0 ? sh - t : 999, t, sp_slot(), sp_made()); double sn = sp_snap(t); if (fabs(sn - t) > 0.05) fprintf(stderr, "[seek] %.2f s lands on the shown picture's keyframe at %.2f s\n", t, sn); t = sn; scrub = 0; } /* (the picture shown: the film lands on it) */
  if (scrub && getenv("SB_SEEKSNAP") && mkv && atoi(getenv("SB_SEEKSNAP")) && prev_enabled()) { int atChap = 0; for (int i = 0; i < P.nchap; i++) if (fabs(P.chap[i].s - t) < 0.05) atChap = 1; double k; /* (SB_SEEKSNAP, development, off: a scrub of the viewer's lands on the keyframe at or before it within 1.5 s, as the previews will show it; never a chapter jump, resume, skip or a source's same moment; a chapter's start kept) */
    if (!atChap && mkv_key_before(mkv, t, 1.5, &k)) { fprintf(stderr, "[seek] %.2f s snapped to the keyframe at %.2f s (%d known)\n", t, k, mkv_key_count(mkv)); t = k; }
  }
  if (getenv("SB_SEEKSNAP")) { P.seekSnapAt = now_(); P.seekSnapT = t; } /* (SB_SEEKSNAP=0: the times only, for the comparison) */
  video_seek(t); P.seekTxtUntil = now_() + 0.9; }
static void cancel_seek(void) { P.seekT = -1; P.skm = 0; P.skOn = 0; P.seekTxtUntil = 0; }

/* ---- picture size, three states always in this order: Original (the whole picture), Fill (fills the screen: a file
   of another shape is cropped to it; a 16:9 file, which already fills it, gets a light crop) and Zoom (a 16:9 file
   with cinema bars baked in loses them; any other shape is cropped further). Chosen per title (a show keeps it from
   episode to episode), else the default from Settings ---- */
static void aspect_key(char *k, size_t cap) { snprintf(k, cap, "aspect_%.*s", (int)strcspn(P.id, ":"), P.id); }
static void aspect_load(void) { char k[96]; aspect_key(k, sizeof k); const char *a = store_get(k, ""); if (!*a) a = store_get("aspectMode", "fit"); snprintf(P.aspect, sizeof P.aspect, "%s", strcmp(a, "stretch") ? a : "fit"); }
/* the web sizes the picture's box (Fill / Zoom go past the screen); a TV's plane blanks for a rectangle that leaves
   the screen, so the part of that box on the screen is shown as a crop of the frame instead */
void player_reapply_window(void);
static void apply_aspect(void) {
  int vw = video_width(), vh = video_height(); float W = SB_W, H = SB_H; P.lastVw = vw; P.lastVh = vh;
  if (growPending || growAt > 0 || shrinkAt > 0) return; /* a preview growing or shrinking places the picture itself every frame (its framing comes as it ends): never a full-screen jump in the middle */
  if (!vw || !vh) { video_set_window(0, 0, W, H); return; }
  float fit = fminf(W / vw, H / vh), fill = fmaxf(W / vw, H / vh), k = fit; int wide = fabsf((float)vw / vh - W / H) < 0.03f;
  if (!strcmp(P.aspect, "fill")) k = wide ? fit * 1.12f : fill; else if (!strcmp(P.aspect, "zoom")) k = wide ? fit * 2.39f / (W / H) : fill * 1.15f;
  float fw = roundf(vw * fit), fh = roundf(vh * fit), fx = roundf((W - fw) / 2), fy = roundf((H - fh) / 2);
  if (k == fit) { video_set_window(fx, fy, fw, fh); return; }
  float w = roundf(vw * k), h = roundf(vh * k), x = roundf((W - w) / 2), y = roundf((H - h) / 2);
  float dx = fmaxf(0, x), dy = fmaxf(0, y), dw = fminf(W, x + w) - dx, dh = fminf(H, y + h) - dy;
  int sx = (int)roundf((dx - x) / k), sy = (int)roundf((dy - y) / k), sw = (int)roundf(dw / k), sh = (int)roundf(dh / k);
  if (sx + sw > vw) sw = vw - sx; if (sy + sh > vh) sh = vh - sy;
  if (!video_set_crop(sx, sy, sw, sh, dx, dy, dw, dh)) video_set_window(fx, fy, fw, fh); /* no crop on this TV: the picture stays whole */
}
static void next_aspect(void) {
  static const char *A[3] = { "fit", "fill", "zoom" }; static const char *N[3] = { "Original", "Fill the screen", "Zoom" }; int i = 0; for (int k = 0; k < 3; k++) if (!strcmp(A[k], P.aspect)) i = k;
  int n = (i + 1) % 3; char key[96]; aspect_key(key, sizeof key);
  snprintf(P.aspect, sizeof P.aspect, "%s", A[n]); store_set(key, P.aspect); apply_aspect();
  /* paused: the TV shows the new size with its next frame, when play resumes (asking for the same frame again was a
     seek: the stream buffered, a trailer jumped back) */
  int b = btn_index("asp"); if (b >= 0) { snprintf(P.btns[b].lb, 48, "%s", T(N[n])); P.btns[b].lw = text_width(P.btns[b].lb, 24, 700, 0) + 22; } /* the button names the mode it is in */
}

/* ---- prompts: skip intro / recap, the credits card ---- */
static int in_seg(int has, double s, double e, double t, double tail) { return has && t >= s && t < e - tail; }
static void paint_prompt(void) { if (P.prompt.k) P.shown = P.prompt; }
static void go_next(void);
static void next_auto(void);
static void dismiss_prompt(void) { if (P.prompt.k) P.dismissed[P.prompt.k] = 1; P.prompt.k = 0; if (P.mode == 3) P.mode = 0; paint_prompt(); }
static void prompt_go(int i) {
  if (!P.prompt.k || i >= P.prompt.nbtn) return; const char *id = P.prompt.b[i].id;
  if (!strcmp(id, "skip")) { P.dismissed[P.prompt.k] = 1; double to = P.prompt.b[i].to; P.prompt.k = 0; if (P.mode == 3) P.mode = 0; paint_prompt(); video_seek(to); }
  else if (!strcmp(id, "credits")) dismiss_prompt();
  else if (!strcmp(id, "coll")) { mark_watched(); save_progress(1); closeColl = 1; close_player(0, 0); } /* the next film: its page, its streams open */
  else go_next();
}
static void check_prompt(void) {
  double d = dur(); if (d <= 0 || P.seekT >= 0) return; double t = video_time(); int want = 0, big = 0;
  int skipOn = strcmp(skip_mode(), "off") != 0; /* Off: no intro / recap button at all (the next episode's card stays) */
  if (skipOn && in_seg(P.intro.has, P.intro.s, P.intro.e, t, 2) && !P.dismissed[1]) want = 1;
  else if (skipOn && in_seg(P.recap.has, P.recap.s, P.recap.e, t, 2) && !P.dismissed[2]) want = 2;
  else if (!P.dismissed[3] && (P.next.has || P.coll.has || P.sim)) { int credits = P.outro.has ? t >= P.outro.s : (d > 600 && d - t <= 90 && d - t > 2); if (credits) { want = 3; big = 1; } }
  if (want == P.prompt.k) return;
  P.prompt.k = want; P.prompt.big = big; P.prompt.autoN = 0; P.prompt.nbtn = 0;
  if (want == 1 || want == 2) { P.prompt.b[0].id = "skip"; snprintf(P.prompt.b[0].lb, 48, "%s", T(want == 1 ? "Skip intro" : "Skip recap")); P.prompt.b[0].to = want == 1 ? P.intro.e : P.recap.e; P.prompt.b[0].main = 0; P.prompt.nbtn = 1;
    /* the pill fills up and skips by itself when full (about 6 s, less when the part ends sooner); OK skips now, a pause stops it */
    if (!paused() && !strcmp(skip_mode(), "auto")) { double e = want == 1 ? P.intro.e : P.recap.e, dd = e - t - 1; P.prompt.autoN = 1; P.prompt.t0 = now_(); P.prompt.adur = dd < 2.5 ? 2.5 : dd > 6 ? 6 : dd; } }
  else if (want == 3) { P.prompt.b[0].id = "credits"; snprintf(P.prompt.b[0].lb, 48, "%s", T("Watch credits")); P.prompt.b[0].main = 0; P.prompt.b[1].id = P.next.has ? "next" : P.coll.has ? "coll" : "sim"; snprintf(P.prompt.b[1].lb, 48, "%s", P.next.has ? T("Next episode") : P.coll.has ? T("Next film") : T("Something similar")); P.prompt.b[1].main = 1; P.prompt.nbtn = 2;
    /* credits + autoplay on: "Next episode" fills up and plays by itself unless you pick something */
    if (P.next.has && strcmp(store_get("bingeWatch", "true"), "false") && strcmp(store_get("enableNextVideo", "true"), "false")) { P.prompt.autoN = 1; P.prompt.t0 = now_(); P.prompt.adur = 10; } }
  P.prompt.idx = P.prompt.nbtn ? P.prompt.nbtn - 1 : 0;
  if (want && big && P.mode == 0) P.mode = 3; /* the credits card takes the focus */
  if (!want && P.mode == 3) P.mode = 0;
  paint_prompt();
}

/* ---- next episode: played straight away from the source most like the current one (same addon, resolution, HDR,
   audio, release group, debrid vs torrent); the art covers the switch. With no source at all: the title page's list ---- */
static char closeVid[SB_VID];
static void play_stream(const Stream *s, const char *vid, double startAt);
static void prepare(const char *vid, double startAt, int keepSubs);
static int can_play(const Stream *s) { return !(s->p2p || !strncmp(s->url, "p2p://", 6)) || p2p_available(); } /* torrents need the engine library beside the app */
static void rel_group(const char *s, char *out, size_t cap) { /* "-GROUP" at the end of a word (before .mkv / .mp4 / .avi) */
  out[0] = 0; for (const char *p = s; (p = strchr(p, '-')); p++) { const char *q = p + 1; while (isalnum((unsigned char)*q)) q++; size_t n = q - p - 1; if (n < 2 || n > 12) continue;
    const char *r = q; if (*r == '.' && (!strncasecmp(r + 1, "mkv", 3) || !strncasecmp(r + 1, "mp4", 3) || !strncasecmp(r + 1, "avi", 3))) r += 4;
    if (*r && !isspace((unsigned char)*r)) { if (r == q) continue; r = q; if (*r && !isspace((unsigned char)*r)) continue; }
    snprintf(out, cap, "%.*s", (int)n, p + 1); for (char *c = out; *c; c++) *c = (char)tolower((unsigned char)*c); return; }
}
static const char *src_kind(const char *label) {
  char s[700]; lower(label, s, sizeof s); if (strstr(s, "remux")) return "remux"; if (strstr(s, "bluray") || strstr(s, "blu-ray") || strstr(s, "bdrip") || strstr(s, "brrip")) return "bluray";
  int nf = 0; for (const char *p = s; (p = strstr(p, "nf")); p++) if (!isalnum((unsigned char)p[2]) && p[2] != '_') nf = 1;
  if (strstr(s, "webdl") || strstr(s, "web-dl") || strstr(s, "webrip") || has_word(s, "web") || strstr(s, "amzn") || nf || strstr(s, "dsnp") || strstr(s, "hmax") || strstr(s, "atvp")) return "web";
  return strstr(s, "hdtv") ? "tv" : "";
}
static int cached_flag(const char *label) { char s[700]; lower(label, s, sizeof s); if (strstr(s, "cached") || strstr(s, "\xe2\x9a\xa1")) return 1;
  for (const char *p = s; (p = strchr(p, '[')); p++) { int n = 0; while (p[1 + n] >= 'a' && p[1 + n] <= 'z') n++; if (n >= 2 && n <= 3 && p[1 + n] == '+' && p[2 + n] == ']') return 1; } return 0; }
static void info_hash(const char *u, char *out) { out[0] = 0; const char *p = NULL; if (!strncmp(u, "p2p://", 6)) p = u + 6; else if ((p = strstr(u, "ih="))) p += 3; else if ((p = strstr(u, "/torrents/"))) p += 10;
  if (!p) return; int n = 0; while (n < 40 && isxdigit((unsigned char)p[n])) n++; if (n == 40) { for (int i = 0; i < 40; i++) out[i] = (char)tolower((unsigned char)p[i]); out[40] = 0; } }
static int likeness(const Stream *s) {
  char b[700]; sb_stream_info(s, b, sizeof b); const char *a = P.nx.info; int sc = 0;
  if (P.nx.addon[0] && !strcmp(s->addon, P.nx.addon)) sc += 20;
  static const char *RS[5] = { "SD", "720p", "1080p", "1440p", "4K" }; const char *r1 = res_of(a), *r2 = res_of(b);
  if (*r1 && !strcmp(r1, r2)) sc += 30; else if (*r1 && *r2) { int i1 = 0, i2 = 0; for (int i = 0; i < 5; i++) { if (!strcmp(RS[i], r1)) i1 = i; if (!strcmp(RS[i], r2)) i2 = i; } sc -= 12 * abs(i1 - i2); }
  char h1[32], h2[32]; hdr_of(a, h1, sizeof h1); hdr_of(b, h2, sizeof h2); if (!strcmp(h1, h2)) sc += 12;
  char a1[48], a2[48]; audio_of(a, a1, sizeof a1); audio_of(b, a2, sizeof a2); a1[strcspn(a1, " ")] = 0; a2[strcspn(a2, " ")] = 0; if (*a1 && !strcmp(a1, a2)) sc += 10;
  char g1[16], g2[16]; rel_group(a, g1, sizeof g1); rel_group(b, g2, sizeof g2); if (*g1 && !strcmp(g1, g2)) sc += 25;
  const char *k1 = src_kind(a), *k2 = src_kind(b); if (*k1 && !strcmp(k1, k2)) sc += 10;
  int c1 = cached_flag(a), c2 = cached_flag(b); if (c1 == c2) sc += c1 ? 15 : 5;
  int p1 = !strncmp(P.nx.url, "p2p://", 6), p2 = !strncmp(s->url, "p2p://", 6); sc += p1 == p2 ? 15 : -15;
  char x1[48], x2[48]; info_hash(P.nx.url, x1); info_hash(s->url, x2); if (*x1 && !strcmp(x1, x2)) sc += 60; /* the same season pack: the next file of the very same torrent */
  return sc;
}
static unsigned nxTok;
/* the next episode's seek previews made ahead: at 80 % of this one, its stream chosen as the next episode will choose
   it (the closest to this one: likeness), its index read alone (no subtitles, nothing played) and 48 pictures made
   from it, only while this one has a good buffer (seekprev's prep); kept on this device, so the next episode opens
   with them. A torrent never. */
static struct { char vid[SB_VID], key[300]; unsigned tok; int has, done, prepped; double bs, at; Stream best; MkvSession *m; } NP;
static void np_stop(void) { if (NP.m || NP.prepped) sp_prepare(NULL, NULL); if (NP.m) { mkv_close(NP.m); NP.m = NULL; } NP.tok++; NP.has = NP.done = NP.prepped = 0; }
static int likeness_to_this(const Stream *s) { __typeof__(P.nx) keep = P.nx; snprintf(P.nx.info, sizeof P.nx.info, "%s", P.sname); snprintf(P.nx.addon, sizeof P.nx.addon, "%s", P.saddon); snprintf(P.nx.url, sizeof P.nx.url, "%s", P.st.url[0] ? P.st.url : P.url); int v = likeness(s); P.nx = keep; return v; }
static void np_part(StreamGroup *g, int gi, void *ud) { (void)gi;
  if (P.open && (unsigned)(uintptr_t)ud == NP.tok && !NP.done) for (int i = 0; i < g->n; i++) { const Stream *s = &g->items[i]; if (!s->url[0] || s->p2p || strncmp(s->url, "http", 4)) continue;
      int v = P.st.binge[0] && !strcmp(s->binge, P.st.binge) ? 1000 : likeness_to_this(s); if (!NP.has || v > NP.bs) { NP.bs = v; NP.best = *s; NP.has = 1; } }
  free(g->items); }
static void np_done(int total, void *ud) { (void)total; if (!P.open || (unsigned)(uintptr_t)ud != NP.tok || NP.done) return; NP.done = 1;
  if (!NP.has) { fprintf(stderr, "[player] next episode's previews: no direct or debrid stream for it\n"); return; }
  snprintf(NP.key, sizeof NP.key, "%s|%s|%.0f|0", P.id, NP.vid, NP.best.size); NP.m = mkv_open_keys(NP.best.url); NP.at = now_();
  fprintf(stderr, "[player] next episode's previews: made ahead from %s (%.1f GB)\n", NP.best.addon, NP.best.size / 1e9); }
static void np_tick(void) {
  if (!P.open || !P.started || !P.live || P.isTrailer || P.isP2p || !P.next.has || !P.next.vid[0] || !prev_enabled() || !prev_available() || live_title()) return;
  double d = dur(); if (d < 600) return;
  if (strcmp(NP.vid, P.next.vid)) { if (video_time() < d * 0.8) return; np_stop(); snprintf(NP.vid, sizeof NP.vid, "%s", P.next.vid); sb_streams(P.type, NP.vid, np_part, np_done, (void *)(uintptr_t)NP.tok); return; } /* (at 80 %: once for that episode) */
  if (NP.m && !NP.prepped) { if (mkv_key_count(NP.m) >= 4) { sp_prepare(NP.m, NP.key); NP.prepped = 1; } else if (mkv_dead(NP.m) || now_() - NP.at > 30) { mkv_close(NP.m); NP.m = NULL; } } }
static void nx_finish(void) {
  if (!P.nx.tok || P.nx.done) return; P.nx.done = 1; P.nx.tok = 0;
  if (P.nx.has) { Stream b = P.nx.best; play_stream(&b, P.nx.vid, 0); }
  else { snprintf(closeVid, sizeof closeVid, "%s", P.nx.vid); close_player(1, 0); }
}
static void nx_part(StreamGroup *g, int gi, void *ud) {
  (void)gi; if (P.open && P.nx.tok && (unsigned)(uintptr_t)ud == P.nx.tok && !P.nx.done) {
    /* the same bingeGroup is the addon's own word that it is the same kind of file: taken at once (the official rule) */
    if (P.nx.binge[0]) for (int i = 0; i < g->n; i++) { const Stream *s = &g->items[i]; if (s->url[0] && can_play(s) && !strcmp(s->binge, P.nx.binge)) { P.nx.best = *s; P.nx.has = 1; P.nx.bs = 1000; free(g->items); fprintf(stderr, "[player] next episode: same binge group (%s)\n", P.nx.binge); nx_finish(); return; } }
    for (int i = 0; i < g->n; i++) { const Stream *s = &g->items[i]; if (!s->url[0] || !can_play(s)) continue; int v = likeness(s); if (!P.nx.has || v > P.nx.bs) { P.nx.bs = v; P.nx.best = *s; P.nx.has = 1; } }
    free(g->items); if (P.nx.has && P.nx.bs >= 95) nx_finish(); return; } /* same addon, quality, group: no need to wait for the slow ones */
  free(g->items);
}
static void nx_done(int total, void *ud) { (void)total; if (P.open && P.nx.tok && (unsigned)(uintptr_t)ud == P.nx.tok) nx_finish(); }
static void next_auto(void) {
  if (!P.next.has) { go_next(); return; }
  mark_watched(); save_progress(1);
  char nv[SB_VID], info[700], url[SB_URL_MAX], addon[96], binge[96]; snprintf(nv, sizeof nv, "%s", P.next.vid); snprintf(info, sizeof info, "%s", P.sname); snprintf(url, sizeof url, "%s", P.st.url[0] ? P.st.url : P.url); snprintf(addon, sizeof addon, "%s", P.saddon); snprintf(binge, sizeof binge, "%s", P.st.binge);
  prepare(nv, 0, 0);
  snprintf(P.nx.vid, sizeof P.nx.vid, "%s", nv); snprintf(P.nx.info, sizeof P.nx.info, "%s", info); snprintf(P.nx.binge, sizeof P.nx.binge, "%s", binge); snprintf(P.nx.url, sizeof P.nx.url, "%s", url); snprintf(P.nx.addon, sizeof P.nx.addon, "%s", addon);
  set_status(T("Finding the closest source\xe2\x80\xa6")); P.nx.tok = ++nxTok; if (!P.nx.tok) P.nx.tok = ++nxTok; P.nx.until = now_() + 15;
  sb_streams(P.type, nv, nx_part, nx_done, (void *)(uintptr_t)P.nx.tok);
}
static void go_next(void) {
  if (P.next.has && !P.nextTried) { P.nextTried = 1; next_auto(); }
  else if (P.next.has) { mark_watched(); snprintf(closeVid, sizeof closeVid, "%s", P.next.vid); close_player(1, 0); }
  else if (P.sim) close_player(0, 1);
}

/* ---- trays ---- */
static int focusable(const TItem *it) { return it && it->k != 0 && it->k != 1; }
static int first_on(const TItem *l, int n) { for (int i = 0; i < n; i++) if (l[i].on && focusable(&l[i])) return i; for (int i = 0; i < n; i++) if (focusable(&l[i])) return i; return -1; }
static int season_of(const char *vid) { const char *c = strrchr(vid, ':'); if (!c) return 1; const char *p = c - 1; while (p > vid && *p != ':') p--; return *p == ':' ? atoi(p + 1) : 1; }
static TItem *add_item(TItem *list, int *n, int max, int k) { if (*n >= max) return NULL; TItem *it = &list[(*n)++]; memset(it, 0, sizeof *it); it->k = k; return it; }
static TItem *col_add(TCol *c, int k) { c->items = realloc(c->items, (c->n + 1) * sizeof *c->items); TItem *it = &c->items[c->n++]; memset(it, 0, sizeof *it); it->k = k; return it; }
static void cols_clear(void) { for (int i = 0; i < 4; i++) { free(P.tray.cols[i].items); P.tray.cols[i].items = NULL; P.tray.cols[i].n = 0; } P.tray.ncols = 0; }
/* ---- the sources tray: tabs (All, then each addon serving the video, in the user's order, with how many it has),
   the stream playing and the best fit for this TV pinned on top of All, then every addon's streams under its name.
   A stream is one calm row: its facts in the accent (resolution, HDR, sound, languages), its size and source with the
   release's name, and the one thing that matters on this TV in coral when there is one (dimmed when it won't play
   here at all). Every addon is asked as the tray opens; everything is let go when it closes ---- */
typedef struct { int k; /* 0 a note, 1 waiting, 3 a stream, 5 an addon's name, 6 the one playing, 7 the best fit */ int g, i; float y, h; } SRow;
static struct { SRow *r; int n, cap, idx, moved; Mo y, plY, plH, plA; float tot; } SR; /* moved: the viewer has moved in this list (until then the focus stays on its top: what plays, the best fit) */
static unsigned srcAsk;
/* the trays' small motions, all tweens on quads already drawn: the tab pill gliding (and lighting up when focused),
   the contents swapping tabs (out and in, sliding the way of the move), the column's backdrop following the focus,
   a list fading in when what it lists changes, the tick fading in where the choice went, a value fading up when it
   changes, the lit dot of a value that cycles gliding to its place */
static const float C_GLIDE[4] = { .25f, 1, .5f, 1 }; /* GLIDE_EASE's curve, for the Mo motions */
static struct {
  Mo tabX, tabW, tabF; int tabSeen; Mo swA; int swDir, swPend; Mo colBX, colBA; int colBSeen; int quiet;
  unsigned colSig[4], rowSig; Mo colA[4], rowA; int onIdx[4], rowOn; Mo chkA[4], rowChk; int vkC; char vkU[8]; Mo vkA;
  Mo dot[5]; int dotSeen[5]; /* 0 the picture size, 1 subtitle size, 2 colour, 3 background, 4 subtitle position */
  Mo swK[2]; int swFrom[2], swCur[2], swSeen[2]; /* the colour and background swatches: the one before, the one now, how far it has come */
} TM;
static void tm_tick(float dt) {
  Mo *m[] = { &TM.tabX, &TM.tabW, &TM.tabF, &TM.swA, &TM.colBX, &TM.colBA, &TM.rowA, &TM.rowChk, &TM.vkA, &TM.dot[0], &TM.dot[1], &TM.dot[2], &TM.dot[3], &TM.dot[4], &TM.swK[0], &TM.swK[1] };
  for (size_t i = 0; i < sizeof m / sizeof *m; i++) mo_tick(m[i], dt); for (int i = 0; i < 4; i++) { mo_tick(&TM.colA[i], dt); mo_tick(&TM.chkA[i], dt); }
}
static void tm_reset(void) { /* a tray opening: everything where it is, nothing gliding in from an old place */
  TM.tabSeen = TM.colBSeen = TM.swPend = 0; mo_set(&TM.swA, 1); mo_set(&TM.colBA, 0); mo_set(&TM.tabF, 0); mo_set(&TM.vkA, 1); TM.vkU[0] = 0;
  for (int i = 0; i < 4; i++) { mo_set(&TM.colA[i], 1); mo_set(&TM.chkA[i], 1); TM.onIdx[i] = -2; TM.colSig[i] = 0; } mo_set(&TM.rowA, 1); mo_set(&TM.rowChk, 1); TM.rowOn = -2; TM.rowSig = 0;
  for (int i = 1; i < 5; i++) TM.dotSeen[i] = 0; TM.swSeen[0] = TM.swSeen[1] = 0;
}
static unsigned str_sig(unsigned h, const char *s) { while (*s) h = h * 31 + (unsigned char)*s++; return h * 31 + 7; }
static int item_on(const TItem *it) { return strncmp(it->u, "lang:", 5) ? it->on && !it->tool : it->d > 0; }
/* after each rebuild: a list whose items changed fades in, a tick that moved fades in at its new place */
static void tm_after(void) {
  for (int c = 0; c < 4; c++) { unsigned h = 1; int on = -1; const TCol *col = &P.tray.cols[c]; if (c < P.tray.ncols) for (int i = 0; i < col->n; i++) { h = str_sig(h * 13 + (unsigned)col->items[i].k, col->items[i].lb); if (!col->items[i].tool) h = str_sig(h, col->items[i].sub); /* (a tool's line under it is its state, not the list) */ if (on < 0 && item_on(&col->items[i])) on = i; }
    if (!TM.quiet && TM.colSig[c] && h != TM.colSig[c]) { mo_set(&TM.colA[c], 0); mo_to(&TM.colA[c], 1, .22f, C_OUT, 0); }
    if (!TM.quiet && TM.onIdx[c] != -2 && on != TM.onIdx[c] && on >= 0) { mo_set(&TM.chkA[c], 0); mo_to(&TM.chkA[c], 1, .2f, C_EASE, 0); }
    TM.colSig[c] = h; TM.onIdx[c] = on; }
  unsigned h = 1; int on = -1; for (int i = 0; i < P.tray.n; i++) { h = str_sig(h * 13 + (unsigned)P.tray.row[i].k, P.tray.row[i].lb); if (on < 0 && P.tray.row[i].on) on = i; }
  if (!TM.quiet && TM.rowSig && h != TM.rowSig) { mo_set(&TM.rowA, 0); mo_to(&TM.rowA, 1, .22f, C_OUT, 0); }
  if (!TM.quiet && TM.rowOn != -2 && on != TM.rowOn && on >= 0) { mo_set(&TM.rowChk, 0); mo_to(&TM.rowChk, 1, .2f, C_EASE, 0); }
  TM.rowSig = h; TM.rowOn = on;
}
/* a value that cycles (the picture size, the subtitles' style): one dot per choice, the lit one gliding to the current */
static void cycle_dots(int which, int n, int cur, float x, float y, float gap, float r, uint32_t dim, uint32_t lit, int alignRight) {
  /* the current choice is a short pill, the others dots; as the choice moves the pill flows from one into the next (each
     dot's length follows how near the gliding choice is to it), the row staying centred on x (or ending at x) */
  if (!TM.dotSeen[which]) { mo_set(&TM.dot[which], (float)cur); TM.dotSeen[which] = 1; } else mo_to(&TM.dot[which], (float)cur, GLIDE_DUR * .9f, C_GLIDE, 0);
  float d = 2 * r, L = 3.4f * d, sp = gap - d, w[8], tot = 0; if (n > 8) n = 8;
  for (int i = 0; i < n; i++) { float k = 1 - fabsf(i - TM.dot[which].v); k = k < 0 ? 0 : k; w[i] = d + (L - d) * k; tot += w[i] + (i ? sp : 0); }
  float xx = alignRight ? x - tot : x - tot / 2;
  for (int i = 0; i < n; i++) { float k = 1 - fabsf(i - TM.dot[which].v); k = k < 0 ? 0 : k;
    gfx_rrect(xx, y - r, w[i], d, r, k > 0.01f ? col_mix(dim, lit, k) : dim); xx += w[i] + sp; }
}
/* the subtitles' colour and background as a round swatch: the colour itself; no background a small checkerboard, the
   transparent one that checkerboard darkened, solid plain black. A new choice comes in over the old one, growing a
   little into place as the old one fades (one short ease, nothing that repeats) */
static GLuint checkTex;
static void swatch_one(int which, int v, float cx, float cy, float r, float a) {
  if (a <= 0.004f) return; uint32_t A = (uint32_t)(a * 255 + .5f) << 24;
  if (which == 0) gfx_circle(cx, cy, r, (hexcol(SUB_COLORS[v]) & 0xffffff) | A);
  else if (v == 1) gfx_circle(cx, cy, r, A);
  else { if (!checkTex) { unsigned char px[4 * 4 * 4]; for (int i = 0; i < 16; i++) { unsigned char g = ((i & 3) + (i >> 2)) & 1 ? 150 : 236; px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = g; px[i * 4 + 3] = 255; } checkTex = gfx_tex_create(px, 4, 4, 4, 0); }
    uint32_t tint = v == 2 ? RGB(102, 102, 102) : RGB(255, 255, 255); /* (.6 black over it) */
    gfx_image(checkTex, cx - r, cy - r, 2 * r, 2 * r, r, (tint & 0xffffff) | A, 0, 0, 1, 1); }
}
static void swatch_draw(int which, int v, float cx, float cy, float r) {
  if (!TM.swSeen[which]) { TM.swSeen[which] = 1; TM.swCur[which] = TM.swFrom[which] = v; mo_set(&TM.swK[which], 1); }
  else if (v != TM.swCur[which]) { TM.swFrom[which] = TM.swCur[which]; TM.swCur[which] = v; mo_set(&TM.swK[which], 0); mo_to(&TM.swK[which], 1, .22f, C_OUT, 0); }
  float k = TM.swK[which].v;
  if (k < 1) swatch_one(which, TM.swFrom[which], cx, cy, r, 1 - k);
  swatch_one(which, v, cx, cy, r * (.84f + .16f * k), k < 1 ? fminf(1, k * 1.6f) : 1);
  gfx_ring(cx - r, cy - r, 2 * r, 2 * r, r, 1.5f, WHITE_A(64)); /* its edge, so black shows on the dark tray */
}
#define SRC_W 1000.0f        /* the tray */
#define COL_LH 520.0f        /* the subtitle columns' window (about seven entries) */
#define TAB_FS 28            /* the trays' tabs: their words, and the pill around them */
#define TAB_PAD 60.0f
#define TAB_GAP 14.0f
#define SRC_RW (SRC_W - 62)  /* a row */
#define SRC_LH 630.0f        /* the list's window at most (the tray stays clear of the clock) */
#define SRC_VH (fminf(SRC_LH, fmaxf(150.0f, SR.tot - 10))) /* the list's window: as tall as what is in it (groups folded, it shrinks), up to SRC_LH */
static int src_tray(void) { int ak = P.tray.kind < 0 ? -P.tray.kind : P.tray.kind; return ak == 2 || ak == 5; }
static void srcs_free(void) { for (int i = 0; i < P.srcs.ng; i++) { free(P.srcs.g[i].items); free(P.srcs.g[i].f); } memset(&P.srcs, 0, sizeof P.srcs); P.srcs.bg = P.srcs.bi = -1; free(SR.r); SR.r = NULL; SR.n = SR.cap = 0; SR.idx = -1; }
static void src_rows(void);
static void src_tabs(void);
static void close_tray(void);
static int src_find(unsigned tok) { if (!tok) return -1; for (int i = 0; i < P.srcs.ng; i++) if (P.srcs.g[i].tok == tok) return i; return -1; }
static int src_on(const Stream *s) { char info[700]; sb_stream_info(s, info, sizeof info); return !strcmp(s->url, P.url) || !strcmp(s->url, P.st.url) || !strcmp(s->url, P.alt) || (P.sname[0] && !strncmp(info, P.sname, 200) && !strcmp(s->addon, P.saddon)); }
/* the best fit for this TV among everything that came (the same pick as Play's) */
static void src_best(void) { const Stream *all[600]; int gi[600], ii[600], n = 0;
  for (int g = 0; g < P.srcs.ng; g++) for (int i = 0; i < P.srcs.g[g].n && n < 600; i++) if (P.srcs.g[g].items[i].url[0]) { all[n] = &P.srcs.g[g].items[i]; gi[n] = g; ii[n] = i; n++; }
  P.srcs.bg = P.srcs.bi = -1; if (!n) return; BestRate br; const Stream *b = best_pick(all, n, best_dur(P.meta, P.type), NULL, -1, &br);
  for (int k = 0; k < n; k++) if (all[k] == b) { P.srcs.bg = gi[k]; P.srcs.bi = ii[k]; break; } }
static void on_src_part(StreamGroup *g, int gi, void *ud) { (void)gi; int k = P.open ? src_find((unsigned)(uintptr_t)ud) : -1; if (k < 0) { free(g->items); return; } /* a list for a tray since closed: dropped */
  free(P.srcs.g[k].items); free(P.srcs.g[k].f); P.srcs.g[k].items = g->items; P.srcs.g[k].n = g->n; P.srcs.g[k].nf = 0; P.srcs.g[k].f = calloc(g->n ? g->n : 1, sizeof(BestFacts)); /* the list is ours now; its streams are read a few a frame (src_tick) */
  if (!P.srcs.g[k].f) { free(g->items); P.srcs.g[k].items = NULL; P.srcs.g[k].n = 0; } }
/* an addon whose streams have all been read: shown (its rows fade in where the waiting ones were), the best fit again */
static void src_shown(int k) { int any = 0; for (int i = 0; i < P.srcs.g[k].n; i++) if (P.srcs.g[k].items[i].url[0]) any = 1; P.srcs.g[k].st = any ? 2 : 3;
  mo_set(&P.srcs.g[k].ia, 0); mo_to(&P.srcs.g[k].ia, 1, .24f, C_OUT, 0); src_best(); if (src_tray()) { src_tabs(); src_rows(); } }
static void on_src_done(int total, void *ud) { (void)total; int k = P.open ? src_find((unsigned)(uintptr_t)ud) : -1; if (k < 0) return; P.srcs.g[k].tok = 0; P.srcs.g[k].came = 1;
  if (P.srcs.g[k].nf >= P.srcs.g[k].n) src_shown(k); }
/* each stream read once (best_facts), 12 a frame at most: a big list never holds a frame on the TV */
static void src_tick(float dt) { int budget = 12, dm = -1;
  for (int k = 0; k < P.srcs.ng; k++) { mo_tick(&P.srcs.g[k].ia, dt); __typeof__(P.srcs.g[0]) *g = &P.srcs.g[k];
    while (budget > 0 && g->f && g->nf < g->n) { if (dm < 0) dm = best_dur(P.meta, P.type); best_facts(&g->items[g->nf], NULL, -1, dm, &g->f[g->nf]); g->nf++; budget--; }
    if (g->came && g->st == 1 && g->nf >= g->n) src_shown(k); } }
static void src_ask(int k) { if (k < 0 || k >= P.srcs.ng || P.srcs.g[k].st) return; P.srcs.g[k].st = 1; if (!++srcAsk) ++srcAsk; P.srcs.g[k].tok = srcAsk;
  sb_streams_one(P.type, P.srcs.vid, k, on_src_part, on_src_done, (void *)(uintptr_t)srcAsk); } /* (an addon that is gone answers at once: empty) */
static void srcs_begin(const char *vid) {
  if (P.srcs.on && !strcmp(P.srcs.vid, vid)) return; srcs_free(); snprintf(P.srcs.vid, sizeof P.srcs.vid, "%s", vid); P.srcs.on = 1;
  char names[16][96]; int n = sb_stream_addons(P.type, vid, names, 16);
  for (int i = 0; i < n; i++) { snprintf(P.srcs.g[i].name, sizeof P.srcs.g[i].name, "%s", names[i][0] ? names[i] : T("Addon")); mo_set(&P.srcs.g[i].ia, 1); } P.srcs.ng = n;
  for (int i = 0; i < n; i++) src_ask(i); /* every addon at once: All and the counts fill as they answer */
}
/* the tabs: All, then each addon with how many it has (a dot while it is asked) */
static int src_count(int g) { int n = 0; for (int i = 0; i < P.srcs.g[g].n; i++) if (P.srcs.g[g].items[i].url[0]) n++; return n; }
static void src_tabs(void) { int tot = 0; P.tray.ntabs = 0;
  for (int g = 0; g < P.srcs.ng; g++) tot += src_count(g);
  { char n[12]; snprintf(n, sizeof n, "%d", tot); snprintf(P.tray.tabs[P.tray.ntabs++], 48, "%s  %s", T("All"), tot ? n : "\xc2\xb7"); }
  for (int g = 0; g < P.srcs.ng && P.tray.ntabs < 40; g++) { char n[12]; if (P.srcs.g[g].st >= 2) snprintf(n, sizeof n, "%d", src_count(g)); else snprintf(n, sizeof n, "\xc2\xb7");
    snprintf(P.tray.tabs[P.tray.ntabs++], 48, "%.36s  %s", P.srcs.g[g].name, n); }
  if (P.tray.tab >= P.tray.ntabs) P.tray.tab = 0; }
/* the rows' heights: a stream two lines (three with its note), a pinned one the same, an addon's name a short line */
#define SN_PX 27      /* a stream's facts: 27 px on 34 */
#define SN_LH 34.0f
#define SD_PX 23      /* its size, source and release: 23 px on 31 */
#define SD_LH 31.0f
#define SRC_RH (16 + SN_LH + 4 + SD_LH + 16)
static SRow *sr_add(int k, int g, int i) { if (SR.n >= SR.cap) { int c = SR.cap ? SR.cap * 2 : 64; SRow *r = realloc(SR.r, c * sizeof *r); if (!r) return NULL; SR.r = r; SR.cap = c; } SRow *x = &SR.r[SR.n++]; memset(x, 0, sizeof *x); x->k = k; x->g = g; x->i = i; return x; }
static int src_focusable(int i) { return i >= 0 && i < SR.n && (SR.r[i].k == 3 || SR.r[i].k == 6 || SR.r[i].k == 7); }
static int src_url_on(const Stream *s) { return s->url[0] && (!strcmp(s->url, P.url) || !strcmp(s->url, P.st.url) || !strcmp(s->url, P.alt)); }
/* the one stream playing (an addon may list the same release twice: one only): its address first, its name when no address matches */
static int src_playing(int *g, int *i) { if (P.tray.kind != 2 && P.tray.kind != -2) return 0;
  for (int pass = 0; pass < 2; pass++) for (int a = 0; a < P.srcs.ng; a++) for (int b = 0; b < P.srcs.g[a].n; b++) { const Stream *s = &P.srcs.g[a].items[b];
    if (s->url[0] && (pass ? src_on(s) : src_url_on(s))) { *g = a; *i = b; return 1; } }
  return 0; }
static int srcPG = -1, srcPI = -1; /* (the one playing, as the rows were laid out: its tag goes on it alone) */
/* an addon's streams in its own order, the ones that won't play on this TV after the rest */
static void src_add_group(int g) { int all = P.tray.tab == 0; /* (on All, what is pinned on top is not listed again) */
  for (int pass = 0; pass < 2; pass++) for (int i = 0; i < P.srcs.g[g].n; i++) if (P.srcs.g[g].items[i].url[0] && !!P.srcs.g[g].f[i].out == pass) {
    if (all && ((g == srcPG && i == srcPI) || (g == P.srcs.bg && i == P.srcs.bi))) continue; sr_add(3, g, i); } }
static void src_rows(void) {
  int fk = -1, fg = -1, fi = -1; if (SR.moved && src_focusable(SR.idx)) { fk = SR.r[SR.idx].k; fg = SR.r[SR.idx].g; fi = SR.r[SR.idx].i; }
  SR.n = 0; int tab = P.tray.tab, pg = -1, pi = -1, playing = src_playing(&pg, &pi); srcPG = playing ? pg : -1; srcPI = playing ? pi : -1;
  if (tab == 0) { /* All: what plays now, the best fit, then every addon under its name */
    if (playing) sr_add(6, pg, pi);
    if (P.srcs.bg >= 0 && !(playing && pg == P.srcs.bg && pi == P.srcs.bi)) sr_add(7, P.srcs.bg, P.srcs.bi);
    for (int g = 0; g < P.srcs.ng; g++) { int st = P.srcs.g[g].st; if (st == 3) continue; sr_add(5, g, -1);
      if (st <= 1) { sr_add(1, g, 0); sr_add(1, g, 1); } else src_add_group(g); }
    int any = 0; for (int i = 0; i < SR.n; i++) if (SR.r[i].k != 0) any = 1; if (!any) sr_add(0, -1, 0); }
  else { int g = tab - 1; if (g < P.srcs.ng) { int st = P.srcs.g[g].st;
      if (st <= 1) { sr_add(1, g, 0); sr_add(1, g, 1); sr_add(1, g, 2); } else if (st == 3) sr_add(0, g, 0); else src_add_group(g); } }
  float y = 0;
  for (int i = 0; i < SR.n; i++) { SRow *r = &SR.r[i];
    if (r->k == 5) { y += i ? 18 : 4; r->h = 40; }
    else if (r->k == 3 || r->k == 6 || r->k == 7) r->h = SRC_RH + (P.srcs.g[r->g].f[r->i].warn[0] ? 4 + SD_LH : 0);
    else r->h = r->k == 1 ? SRC_RH : 64;
    r->y = y; y += r->h + (r->k == 5 ? 6 : 10); }
  SR.tot = y;
  SR.idx = -1; if (fk >= 0) for (int i = 0; i < SR.n; i++) if (SR.r[i].k == fk && SR.r[i].g == fg && SR.r[i].i == fi) { SR.idx = i; break; }
  if (SR.idx < 0 && fk >= 0) for (int i = 0; i < SR.n; i++) if (src_focusable(i) && SR.r[i].g == fg && SR.r[i].i == fi) { SR.idx = i; break; } /* the same stream, in its other place */
  if (SR.idx < 0) for (int i = 0; i < SR.n; i++) if (src_focusable(i)) { SR.idx = i; break; } /* fresh: the one playing, else the best fit, else the first */
}
static void src_choose(int i) {
  if (!src_focusable(i)) return; SRow r = SR.r[i];
  if (r.k == 6) { close_tray(); return; } /* (the one playing: nothing to change) */
  int same = P.tray.kind == 2; char v[SB_VID]; snprintf(v, sizeof v, "%s", same ? P.vid : P.tray.epvid); Stream s = P.srcs.g[r.g].items[r.i]; play_stream(&s, v, same ? video_time() : 0);
}
/* an episode not aired yet (its date after today): nothing to give away, and no still to blur */
static int ep_unaired(const cJSON *e) { const char *rel = js_str(e, "released", js_str(e, "firstAired", "")); int y, m, d; if (sscanf(rel, "%d-%d-%d", &y, &m, &d) != 3) return 0; time_t now = time(NULL); struct tm *t = localtime(&now); int today = (t->tm_year + 1900) * 10000 + (t->tm_mon + 1) * 100 + t->tm_mday; return y * 10000 + m * 100 + d > today; }
static void tray_data(void) {
  struct { int kind; } t = { P.tray.kind }; P.tray.ntabs = 0; P.tray.n = 0; cols_clear(); P.tray.title[0] = 0;
  if (t.kind == 4) { /* subtitles: four columns; with subtitles addons installed (or subtitles the stream carried), a second tab lists theirs by language */
    TCol *c1 = &P.tray.cols[0], *c2 = &P.tray.cols[1], *c3 = &P.tray.cols[2], *c4 = &P.tray.cols[3]; P.tray.ncols = 4;
    /* the tabs: Subtitles, the addons' when there are any, then Style (size, colour, background, position and the delay,
       for whichever subtitle is on): each tab two columns, the tray narrower for it. A trailer: one list, as before */
    int addonsTab = 0, styleTab = 0;
    if (!P.isTrailer) { int ad = P.nSubAddons > 0 || P.nAdx > 0; P.tray.ntabs = 0; snprintf(P.tray.tabs[P.tray.ntabs++], 48, "%s", T("Subtitles")); if (ad) snprintf(P.tray.tabs[P.tray.ntabs++], 48, "%s", T("Addons")); snprintf(P.tray.tabs[P.tray.ntabs++], 48, "%s/%s", T("Style"), T("Delay")); /* both on that tab: "Style/Delay", no wider than it needs */
      if (P.tray.tab >= P.tray.ntabs) P.tray.tab = 0; addonsTab = ad && P.tray.tab == 1; styleTab = P.tray.tab == P.tray.ntabs - 1; } else P.tray.tab = 0;
    snprintf(c3->title, 64, "%s", T("Style")); snprintf(c4->title, 64, "%s", T("Delay"));
    { char dl[24]; snprintf(dl, sizeof dl, "%s%.2f", P.subDelay > 0 ? "+" : "", P.subDelay); snprintf(c4->vl, 32, "%s", T1("{n} s", "n", dl)); }
    TItem *x; TCol *cs = c3, *cd = c4; /* where the style and the delay go: their own tab, or (a trailer) beside the list */
    if (styleTab) { cs = c1; cd = c2; snprintf(c1->title, 64, "%s", T("Style")); snprintf(c2->title, 64, "%s", T("Delay")); { char dl[24]; snprintf(dl, sizeof dl, "%s%.2f", P.subDelay > 0 ? "+" : "", P.subDelay); snprintf(c2->vl, 32, "%s", T1("{n} s", "n", dl)); } }
    else if (!addonsTab) {
      snprintf(c1->title, 64, "%s", P.isTrailer ? T("Subtitles") : T("In the file")); snprintf(c2->title, 64, "%s", P.isTrailer ? T("Languages") : "OpenSubtitles");
      x = col_add(c1, 4); snprintf(x->lb, 200, "%s", T("Off")); x->on = !strcmp(P.curSub, "off"); snprintf(x->u, 1024, "off");
      if (!P.isTrailer) { if (!P.embKnown && !P.nEmb) { col_add(c1, 1); col_add(c1, 1); } /* the file's head is still being read */ else if (!P.nEmb) { x = col_add(c1, 0); snprintf(x->lb, 200, "%s", T("None in this file")); } else for (int i = 0; i < P.nEmb; i++) { x = col_add(c1, 4); emb_label(i, x->lb, 200); x->i = P.emb[i].idx; char k[16]; snprintf(k, sizeof k, "emb:%d", P.emb[i].idx); x->on = !strcmp(P.curSub, k); x->spin = x->on && SL.wait; snprintf(x->u, 1024, "%s", k); } }
      /* the built-in OpenSubtitles list (the addons' are in their own tab) */
      int onExt = sub_row(1, P.ext, P.nExt);
      for (int i = 0; i < P.nExt; i++) { int same = 0; for (int j = 0; j < P.nExt; j++) if (!strcmp(P.ext[j].lang, P.ext[i].lang)) same++; x = col_add(c2, 4); if (same > 1) snprintf(x->lb, 200, "%s %d", T(P.ext[i].name), P.ext[i].n); /* the page's two spaces show as one */ else snprintf(x->lb, 200, "%s", T(P.ext[i].name)); tag_forced(x->lb, 200, P.ext[i].forced); x->on = i == onExt; x->spin = x->on && SL.wait; x->ls = 1; x->i = i; snprintf(x->u, 1024, "%s", P.ext[i].url); }
      if (!c2->n) { if (P.extLoading && !P.isTrailer) { col_add(c2, 1); col_add(c2, 1); col_add(c2, 1); } else { x = col_add(c2, 0); snprintf(x->lb, 200, "%s", T("None found")); } }
    } else {
      /* the addons' tab: their languages (the one you read first, then by name), and the chosen language's subtitles
         from every addon, each with its addon's name */
      const char *w = want_sub(); char keys[64][24], names[64][64], onKey[24] = ""; int cnt[64], nk = 0, onAdx = sub_row(2, P.adx, P.nAdx);
      if (onAdx >= 0) subs_lang_canon(P.adx[onAdx].lang, onKey, sizeof onKey);
      for (int i = 0; i < P.nAdx; i++) { const SubEntry *e = &P.adx[i]; char k[24]; subs_lang_canon(e->lang, k, sizeof k); int f = -1; for (int q = 0; q < nk; q++) if (!strcmp(keys[q], k)) f = q;
        if (f < 0 && nk < 64) { f = nk++; snprintf(keys[f], 24, "%s", k); snprintf(names[f], 64, "%s", T(e->name)); cnt[f] = 0; } if (f >= 0) cnt[f]++; }
      for (int a = 1; a < nk; a++) for (int b = a; b > 0; b--) { int pa = lang_match(keys[b - 1], w), pb = lang_match(keys[b], w); if (pb > pa || (pb == pa && strcasecmp(names[b - 1], names[b]) > 0)) { char t1[24], t2[64]; int tc = cnt[b]; snprintf(t1, 24, "%s", keys[b]); snprintf(t2, 64, "%s", names[b]); snprintf(keys[b], 24, "%s", keys[b - 1]); snprintf(names[b], 64, "%s", names[b - 1]); cnt[b] = cnt[b - 1]; snprintf(keys[b - 1], 24, "%s", t1); snprintf(names[b - 1], 64, "%s", t2); cnt[b - 1] = tc; } else break; }
      int have = 0; for (int q = 0; q < nk; q++) if (!strcmp(keys[q], P.subLang)) have = 1;
      if (!have) { int pick = -1; /* the language of the subtitle on, else the one you read, else the first */
        if (onKey[0]) for (int q = 0; q < nk; q++) if (!strcmp(keys[q], onKey)) pick = q;
        if (pick < 0 && nk) pick = 0; if (pick >= 0) snprintf(P.subLang, sizeof P.subLang, "%s", keys[pick]); }
      snprintf(c1->title, 64, "%s", T("Language")); c2->title[0] = 0;
      for (int q = 0; q < nk; q++) { x = col_add(c1, 4); snprintf(x->lb, 200, "%s", names[q]); snprintf(x->vl, 48, "%d", cnt[q]); snprintf(x->u, 1024, "lang:%s", keys[q]); x->on = !strcmp(keys[q], P.subLang); if (x->on) snprintf(c2->title, 64, "%s", names[q]);
        if (onKey[0] && !strcmp(onKey, keys[q])) x->d = 1; } /* d: the tick (a subtitle of this language is on); on: the language shown beside */
      if (!nk) { if (P.adxLoading) { col_add(c1, 1); col_add(c1, 1); col_add(c1, 1); } else { x = col_add(c1, 0); snprintf(x->lb, 200, "%s", T("None found")); } }
      if (!c2->title[0]) snprintf(c2->title, 64, "%s", T("Subtitles"));
      for (int i = 0; i < P.nAdx; i++) { const SubEntry *e = &P.adx[i]; char k[24]; if (strcmp(subs_lang_canon(e->lang, k, sizeof k), P.subLang)) continue;
        int same = 0; for (int j = 0; j < P.nAdx; j++) { char k2[24]; if (P.adx[j].order == e->order && !strcmp(subs_lang_canon(P.adx[j].lang, k2, sizeof k2), k)) same++; }
        x = col_add(c2, 4); if (e->label[0]) snprintf(x->lb, 200, "%s", e->label); else if (same > 1) snprintf(x->lb, 200, "%s %d", T(e->name), e->n); else snprintf(x->lb, 200, "%s", T(e->name));
        tag_forced(x->lb, 200, e->forced); snprintf(x->sub, 120, "%s", e->addon); x->on = i == onAdx; x->spin = x->on && SL.wait; x->ls = 2; x->i = i; snprintf(x->u, 1024, "%s", e->url); }
      if (!c2->n && nk) { x = col_add(c2, 0); snprintf(x->lb, 200, "%s", T("None found")); }
      else if (!c2->n && P.adxLoading) { col_add(c2, 1); col_add(c2, 1); }
    }
    if (!P.isTrailer) P.tray.ncols = 2; /* (the lists alone, or the style and the delay) */
    if (styleTab || P.isTrailer) {
    int ci = color_idx(); x = col_add(cs, 4); x->tool = 1; snprintf(x->lb, 200, "%s", T("Size")); snprintf(x->vl, 48, "%d%%", SUB_SIZES[size_idx()]); snprintf(x->u, 1024, "size");
    { int fl = subfont_lat(), fa = subfont_ar(); x = col_add(cs, 4); x->tool = 1; snprintf(x->lb, 200, "%s", T("Font")); snprintf(x->vl, 48, "%s", fl ? SUBF_LAT[fl] : T("Default")); x->vwt = text_sub_weight(700, fl, 0); snprintf(x->u, 1024, "font");
      if (sub_is_arabic()) { x = col_add(cs, 4); x->tool = 1; snprintf(x->lb, 200, "%s", T("Arabic font")); snprintf(x->vl, 48, "%s  \xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7", fa ? SUBF_AR[fa] : T("Default")); x->vwt = text_sub_weight(700, 0, fa); snprintf(x->u, 1024, "fontar"); } } /* (the Arabic font only while Arabic subtitles are on) */ /* (each in its own font; the Arabic one with a sample beside its name) */
    x = col_add(cs, 4); x->tool = 1; snprintf(x->lb, 200, "%s", T("Colour")); snprintf(x->u, 1024, "color"); /* colour and background: shown by their swatch (drawn with the column) */
    x = col_add(cs, 4); x->tool = 1; snprintf(x->lb, 200, "%s", T("Background")); snprintf(x->u, 1024, "bg"); (void)ci;
    x = col_add(cs, 4); x->tool = 1; snprintf(x->lb, 200, "%s", T("Brightness")); snprintf(x->vl, 48, "%s%%", SUB_BRIGHT[bright_idx()]); snprintf(x->u, 1024, "bright");
    x = col_add(cs, 4); x->tool = 1; snprintf(x->lb, 200, "%s", T("Position")); snprintf(x->vl, 48, "%d%%", pos_pct()); snprintf(x->u, 1024, "pos");
    { int sc = P.isTrailer ? 0 : player_sub_scope(); /* what the subtitles shown can't take: greyed, one line why (styled lines keep their colours and boxes; pictures, or lines the TV draws, take nothing) */
      if (sc) { for (int i = 0; i < cs->n; i++) { TItem *y = &cs->items[i]; if (y->tool && (sc == 2 || !strcmp(y->u, "color") || !strcmp(y->u, "bg") || !strcmp(y->u, "font") || !strcmp(y->u, "fontar"))) y->grey = 1; }
        x = col_add(cs, 0); snprintf(x->lb, 200, "%s", sc == 2 ? (sub_pictures() ? T("These subtitles are pictures: their look can\xe2\x80\x99t be changed.") : T("The TV draws these subtitles: their look can\xe2\x80\x99t be changed.")) : T("Styled subtitles keep their own fonts, colours and background.")); } }
    if (!P.isTrailer && !strncmp(P.curSub, "ext:", 4)) { x = col_add(cd, 4); x->tool = 1; snprintf(x->lb, 200, "%s", T("Auto-sync")); snprintf(x->sub, 120, "%s", SY.st); snprintf(x->u, 1024, "sync"); } /* a downloaded subtitle: lined up with the file's own */
    x = col_add(cd, 4); x->tool = 1; snprintf(x->lb, 200, "%s", T("Adjust")); snprintf(x->sub, 120, "%s", T("While you watch")); snprintf(x->u, 1024, "dlyopen"); /* the small card at the top: the film stays in view */
    x = col_add(cd, 4); x->tool = 1; x->d = 0; snprintf(x->lb, 200, "%s", T("Reset")); snprintf(x->u, 1024, "dly"); }
    if (P.isTrailer) { /* one list: Off + the languages */ TCol *m = c1; for (int i = 0; i < c2->n; i++) if (c2->items[i].k == 4) { TItem *y = col_add(m, 4); *y = c2->items[i]; } free(c2->items); P.tray.cols[1] = P.tray.cols[2]; P.tray.cols[2] = P.tray.cols[3]; memset(&P.tray.cols[3], 0, sizeof P.tray.cols[3]); P.tray.ncols = 3; }
  } else if (t.kind == 3 && P.dts.conv) { snprintf(P.tray.title, 300, "%s", T("Audio")); MkvAudio ma[16]; int m = mkv_audio_tracks(mkv, ma, 16), on = P.dts.convOrd;
    /* converted here: the file's own tracks (another one is converted from the same moment) */
    if (on < 0) for (int i = 0; i < m; i++) if (aud_kind(ma[i].codec)) { on = i; break; }
    /* (with the picture converted too: every track the stream can carry, the ones this TV can't decode as Dolby Digital) */
    for (int i = 0; i < m; i++) { int asIs = P.dts.dv && !mkv_aud_bad(&ma[i]) && aud_copyable(ma[i].codec); if (!asIs && !aud_kind(ma[i].codec)) continue;
      TItem *x = add_item(P.tray.row, &P.tray.n, MAXROW, 4); const char *ln = lang_name(ma[i].lang);
      if (*ln) snprintf(x->lb, 200, "%s", ln); else if (ma[i].lang[0]) snprintf(x->lb, 200, "%s", ma[i].lang); else { char k[8]; snprintf(k, sizeof k, "%d", i + 1); snprintf(x->lb, 200, "%s %s", T("Audio"), k); }
      if (asIs) { VidTrack v; memset(&v, 0, sizeof v); snprintf(v.codec, sizeof v.codec, "%s", ma[i].codec); v.channels = ma[i].channels; aud_label(&v, x->sub, 120); }
      else snprintf(x->sub, 120, "%s", ma[i].channels >= 6 ? T("Dolby Digital 5.1 (converted)") : T("Dolby Digital 2.0 (converted)"));
      x->i = 1000 + i; x->on = i == on; }
    if (!P.tray.n) { TItem *x = add_item(P.tray.row, &P.tray.n, MAXROW, 4); snprintf(x->lb, 200, "%s", T("Dolby Digital 5.1 (converted)")); x->i = 1000 + on; x->on = 1; }
  } else if (t.kind == 3) { snprintf(P.tray.title, 300, "%s", T("Audio")); VidTrack ts[16]; int n = aud_list(ts, 16); int sel = P.curAud >= 0 ? P.curAud : video_audio_current();
    /* the language is the name; the format (AC3 5.1, EAC3 Atmos...) the line under it */
    for (int i = 0; i < n; i++) { TItem *x = add_item(P.tray.row, &P.tray.n, MAXROW, 4); const char *ln = lang_name(ts[i].lang);
      if (*ln) snprintf(x->lb, 200, "%s", ln); else if (ts[i].lang[0]) snprintf(x->lb, 200, "%s", ts[i].lang); else { char k[8]; snprintf(k, sizeof k, "%d", i + 1); snprintf(x->lb, 200, "%s %s", T("Audio"), k); }
      if (ts[i].name[0] && strcmp(ts[i].name, x->lb)) snprintf(x->sub, 120, "%s", ts[i].name); x->i = ts[i].idx; x->on = ts[i].idx == sel; }
    if (!n) { TItem *x = add_item(P.tray.row, &P.tray.n, MAXROW, 0); snprintf(x->lb, 200, "%s", T("None")); }
  } else if (t.kind == 6) { snprintf(P.tray.title, 300, "%s", T("Channels")); /* the line-up: a card each, the one playing marked */
    for (int i = 0; i < live_lineup_n() && P.tray.n < MAXROW; i++) { const LiveCh *c = live_lineup_item(i); TItem *x = add_item(P.tray.row, &P.tray.n, MAXROW, 2);
      snprintf(x->lb, 200, "%s", c->name); snprintf(x->th, 512, "%s", c->poster); x->i = i; x->on = !strcmp(c->id, P.id); }
  } else if (t.kind == 1) { /* episodes: seasons on top, episode cards in a row */
    int ss[40], ns = 0; const cJSON *v; cJSON_ArrayForEach(v, P.videos) { int sn = cJSON_GetObjectItem(v, "season") ? (int)js_num(v, "season", 1) : 1; int f = 0; for (int i = 0; i < ns; i++) if (ss[i] == sn) f = 1; if (!f && ns < 40) ss[ns++] = sn; }
    for (int i = 1; i < ns; i++) for (int j = i; j > 0; j--) { int a = ss[j - 1], b = ss[j]; if ((a == 0 && b != 0) || (a != 0 && b != 0 && a > b)) { int tmp = ss[j]; ss[j] = ss[j - 1]; ss[j - 1] = tmp; } else break; }
    int found = 0; for (int i = 0; i < ns; i++) if (ss[i] == P.tray.season) found = 1; if (!found && ns) P.tray.season = ss[0];
    for (int i = 0; i < ns; i++) { P.tray.seasonList[i] = ss[i]; if (ss[i] == 0) snprintf(P.tray.tabs[i], 48, "%s", T("Specials")); else { char n[8]; snprintf(n, sizeof n, "%d", ss[i]); snprintf(P.tray.tabs[i], 48, "%s", T1("Season {n}", "n", n)); } if (ss[i] == P.tray.season) P.tray.tab = i; } P.tray.ntabs = ns;
    const cJSON *eps[400]; int ne = 0; cJSON_ArrayForEach(v, P.videos) { int sn = cJSON_GetObjectItem(v, "season") ? (int)js_num(v, "season", 1) : 1; if (sn == P.tray.season && ne < 400) eps[ne++] = v; }
    for (int i = 1; i < ne; i++) for (int j = i; j > 0 && js_num(eps[j - 1], "episode", 0) > js_num(eps[j], "episode", 0); j--) { const cJSON *tmp = eps[j]; eps[j] = eps[j - 1]; eps[j - 1] = tmp; }
    time_t nowT = time(NULL);
    static int wf[WATCH_MAX]; int sp = spoil_mode(); cJSON *wl = sp ? lib_get(P.id) : NULL; memset(wf, 0, sizeof wf); if (wl || (sp && tw_on())) lib_ep_watched_map(wl, P.videos, wf); /* (spoilers: what is watched, once per tray; a show not in the Library: nothing watched, hidden as on its page) */
    for (int i = 0; i < ne && P.tray.n < MAXROW; i++) { const cJSON *e = eps[i]; TItem *x = add_item(P.tray.row, &P.tray.n, MAXROW, 2); x->v = e; int en = (int)js_num(e, "episode", 0); const char *nm = js_str(e, "name", js_str(e, "title", NULL));
      if (nm) snprintf(x->lb, 200, "%d. %s", en, nm); else { char n[8]; snprintf(n, sizeof n, "%d", en); snprintf(x->lb, 200, "%d. %s", en, T1("Episode {n}", "n", n)); }
      const char *rel = js_str(e, "released", js_str(e, "firstAired", "")); int y, m, d; char ds[32] = ""; if (sscanf(rel, "%d-%d-%d", &y, &m, &d) == 3) snprintf(ds, sizeof ds, "%d/%d/%d", d, m, y);
      int up = 0; if (strlen(rel) >= 10) { struct tm tm; memset(&tm, 0, sizeof tm); if (sscanf(rel, "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) == 3) { tm.tm_year -= 1900; tm.tm_mon--; up = timegm(&tm) > nowT; } }
      if (up) snprintf(x->sub, 120, "%s", T1("Upcoming · {d}", "d", ds)); else snprintf(x->sub, 120, "%s", ds);
      snprintf(x->ov, 300, "%s", js_str(e, "overview", js_str(e, "description", "")));
      const char *th = js_str(e, "thumbnail", NULL); if (th && *th) snprintf(x->th, 512, "%s", th); else if (!strncmp(P.id, "tt", 2) && cJSON_GetObjectItem(e, "episode")) snprintf(x->th, 512, "https://episodes.metahub.space/%s/%d/%d/w300.jpg", P.id, P.tray.season, en);
      { char *p = strstr(x->th, "image.tmdb.org/t/p/"); if (p) { p += 19; char *q = strchr(p, '/'); if (q) { char rest[512]; snprintf(rest, sizeof rest, "%s", q); snprintf(p, 512 - (p - x->th), "w300%s", rest); } } }
      x->on = !strcmp(js_str(e, "id", ""), P.vid);
      if (sp) { int k = 0, w = 0; const cJSON *q; cJSON_ArrayForEach(q, P.videos) { if (q == e) { w = k < WATCH_MAX && wf[k]; break; } k++; } x->hid = !x->on && (!ep_unaired(e) || (*js_str(e, "thumbnail", "") && spoil_blurable(x->th))) && spoil_hidden(P.id, js_str(e, "id", ""), w, P.next.has ? P.next.vid : NULL); } } /* (no still: nothing to blur, never a grey card) */
  } else if (t.kind == 2 || t.kind == 5) {
    if (t.kind == 2) snprintf(P.tray.title, 300, "%s", T("Sources"));
    srcs_begin(t.kind == 5 ? P.tray.epvid : P.vid); src_tabs(); src_rows();
  }
}
static void render_tray(int fresh) {
  if (!P.tray.kind) return;
  if (src_tray()) { if (fresh) SR.idx = -1; int z = P.tray.zone; tray_data(); P.tray.zone = !fresh && z == 1 ? 1 : 0; return; } /* the sources: their own list (on the tabs: they keep it) */ char keepU[1024] = ""; char keepLb[200] = ""; int keepK = -1;
  { TItem *cur = NULL; if (P.tray.ncols) cur = P.tray.c < P.tray.ncols && P.tray.r[P.tray.c] >= 0 && P.tray.r[P.tray.c] < P.tray.cols[P.tray.c].n ? &P.tray.cols[P.tray.c].items[P.tray.r[P.tray.c]] : NULL; else cur = P.tray.idx >= 0 && P.tray.idx < P.tray.n ? &P.tray.row[P.tray.idx] : NULL;
    if (cur && !fresh) { snprintf(keepU, sizeof keepU, "%s", cur->u); snprintf(keepLb, sizeof keepLb, "%s", cur->lb); keepK = cur->k; } }
  tray_data();
  if (P.tray.ncols) {
    if (fresh) for (int i = 0; i < P.tray.ncols; i++) P.tray.r[i] = first_on(P.tray.cols[i].items, P.tray.cols[i].n);
    for (int i = 0; i < P.tray.ncols; i++) { if (P.tray.r[i] < 0 || P.tray.r[i] >= P.tray.cols[i].n || !focusable(&P.tray.cols[i].items[P.tray.r[i]])) P.tray.r[i] = first_on(P.tray.cols[i].items, P.tray.cols[i].n);
      if (!fresh && keepK >= 0) for (int j = 0; j < P.tray.cols[i].n; j++) if (P.tray.cols[i].items[j].k == keepK && !strcmp(P.tray.cols[i].items[j].u, keepU) && !strcmp(P.tray.cols[i].items[j].lb, keepLb)) { P.tray.c = i; P.tray.r[i] = j; } }
    if (fresh || P.tray.c >= P.tray.ncols || P.tray.r[P.tray.c] < 0) { P.tray.c = 0; for (int i = 0; i < P.tray.ncols; i++) if (P.tray.r[i] >= 0) { P.tray.c = i; break; } }
    if (P.tray.zone != 1 || !P.tray.ntabs) P.tray.zone = 2; /* (on the tabs: they keep it) */
  } else {
    int idx = -1; if (!fresh && keepK >= 0) for (int i = 0; i < P.tray.n; i++) if (P.tray.row[i].k == keepK && !strcmp(P.tray.row[i].lb, keepLb) && (keepK != 3 || (P.tray.row[i].st && !strcmp(P.tray.row[i].st->url, keepU)))) { idx = i; break; }
    if (idx < 0 && !fresh && P.tray.idx >= 0 && P.tray.idx < P.tray.n && focusable(&P.tray.row[P.tray.idx])) idx = P.tray.idx;
    if (idx < 0) idx = first_on(P.tray.row, P.tray.n);
    P.tray.idx = idx; if (P.tray.zone != 1 || !P.tray.ntabs) P.tray.zone = idx >= 0 ? 0 : (P.tray.ntabs ? 1 : 0);
  }
  tm_after();
}
static int subTab; /* the subtitle tab last used (kept while the app runs) */
static void open_tray(int kind) {
  if (kind != 2 && kind != 5) srcs_free(); else { mo_set(&SR.y, 0); mo_set(&SR.plA, 0); P.tray.tab = 0; SR.moved = 0; }
  P.mode = 2; memset(&P.tray.row, 0, sizeof P.tray.row); P.tray.kind = kind; P.tray.idx = -1; P.tray.zone = 0; P.tray.onBtn = 0; P.tray.n = 0; P.tray.ntabs = 0;
  if (kind == 1) P.tray.season = season_of(P.vid);
  if (kind == 4) { load_emb_tracks(); P.tray.tab = subTab; P.tray.zone = 1; } /* the subtitles open on their tabs (kept when there are any) */
  tm_reset(); TM.quiet = 1; render_tray(1); TM.quiet = 0;
  tray_origin();
  /* it starts small at its button and grows (the closing motion is set by the classes, see css()) */
  mo_set(&P.tray.open, 0.12f); mo_set(&P.tray.openA, 0); mo_set(&P.tray.rowX, 0); mo_set(&P.tray.tabsX, 0); for (int i = 0; i < 4; i++) mo_set(&P.tray.colY[i], 0); mo_set(&P.tray.ringA, 0); P.tray.ringW = 0; for (int i = 0; i < 4; i++) mo_set(&P.tray.plA[i], 0);
}
static void tray_origin(void) { int ak = P.tray.kind < 0 ? -P.tray.kind : P.tray.kind; int b = btn_index(ak == 1 || ak == 5 ? "eps" : ak == 6 ? "chn" : ak == 2 ? "src" : ak == 3 ? "aud" : "subs");
  P.tray.ox = b >= 0 ? P.btns[b].x0 + P.btns[b].w / 2 : SB_W - 72 - 39; P.tray.oy = SB_H - 54 - 47; } /* its button's centre, measured again on close: the tray may have grown since */
static void dly_open(void);
static void pos_open(void);
static void style_open(int kind);
static void close_tray(void) { if (P.tray.kind == 4) subTab = P.tray.tab; P.mode = 1; P.tray.kind = -P.tray.kind; tray_origin(); arm_hide(); }
static void tray_back(void) {
  if (P.tray.onBtn) { close_tray(); return; }
  if (P.tray.zone == 1 && P.tray.idx >= 0) { P.tray.zone = 0; return; }
  if (P.tray.kind == 5) { srcs_free(); P.tray.kind = 1; P.tray.tab = P.tray.backTab; P.tray.season = P.tray.seasonList[P.tray.tab]; P.tray.zone = 0; render_tray(1); P.tray.idx = P.tray.backIdx; return; }
  close_tray();
}
static void adjust(const char *k) {
  if (!strcmp(k, "size")) { char v[4]; snprintf(v, sizeof v, "%d", (size_idx() + 1) % 7); store_set("subtitleSize", v); }
  else if (!strcmp(k, "color")) { int ci = color_idx(); store_set("subsColor", SUB_COLORS[((ci < 0 ? 0 : ci) + 1) % 3]); }
  else if (!strcmp(k, "bg")) store_set("subsBgStyle", SUB_BGS[(bg_idx() + 1) % 3]);
  render_tray(0);
}
static void choose(TItem *it) {
  if (!it || it->grey) return; /* (greyed: these subtitles can't take it; the line under says why) */
  if (it->k == 4 && P.tray.kind == 4) {
    if (!strncmp(it->u, "lang:", 5)) { snprintf(P.subLang, sizeof P.subLang, "%s", it->u + 5); render_tray(0); if (P.tray.ncols > 1) { int r = first_on(P.tray.cols[1].items, P.tray.cols[1].n); if (r >= 0) { P.tray.c = 1; P.tray.r[1] = r; } } } /* its subtitles: the focus goes over */
    else if (!strcmp(it->u, "off")) { subs_off(); show_pref_sub(); } else if (!strncmp(it->u, "emb:", 4)) { set_emb_sub(it->i); show_pref_sub(); } else if (it->tool) { if (strcmp(it->u, "sync")) { TM.vkC = P.tray.c; snprintf(TM.vkU, sizeof TM.vkU, "%s", it->u); mo_set(&TM.vkA, 0); mo_to(&TM.vkA, 1, .2f, C_OUT, 0); } if (!strcmp(it->u, "sync")) sync_run(); else if (!strcmp(it->u, "dlyopen")) { dly_open(); return; } else if (!strcmp(it->u, "pos")) { pos_open(); return; } else if (!strcmp(it->u, "size") || !strcmp(it->u, "color") || !strcmp(it->u, "bg") || !strcmp(it->u, "bright") || !strcmp(it->u, "font") || !strcmp(it->u, "fontar")) { style_open(!strcmp(it->u, "size") ? 1 : !strcmp(it->u, "color") ? 2 : !strcmp(it->u, "bright") ? 4 : !strcmp(it->u, "font") ? 5 : !strcmp(it->u, "fontar") ? 6 : 3); return; } else if (!strcmp(it->u, "dly")) { P.subDelay = it->d ? (double)((int)((P.subDelay + it->d) * 10 + (P.subDelay + it->d >= 0 ? 0.5 : -0.5))) / 10 : 0; if (!it->d) sync_undo(); render_tray(0); } else adjust(it->u); }
    else if (it->ls) { const SubEntry *l = it->ls == 1 ? P.ext : P.adx; int n = it->ls == 1 ? P.nExt : P.nAdx, k = it->i; /* the row's entry (found again by its file if the list moved) */
      if (k < 0 || k >= n || strcmp(l[k].url, it->u)) for (k = 0; k < n && strcmp(l[k].url, it->u); k++) ;
      if (k < n) { set_ext_sub(it->ls, &l[k]); show_pref_sub(); } }
  } else if (it->k == 4 && P.tray.kind == 3 && it->i >= 1000) { int ord = it->i - 1000; /* another track converted, from here */
    { MkvAudio ma[16]; int m = mkv_audio_tracks(mkv, ma, 16); if (ord >= 0 && ord < m) show_pref_aud(ma[ord].lang, NULL); }
    if (P.dts.conv == 2 && ord != P.dts.convOrd && ord >= 0) { P.dts.convOrd = ord; P.dts.conv = 1; P.dts.convAt = video_time(); P.dts.convT0 = now_();
      int fl = conv_flags(ord); P.dts.copyA = !!(fl & HC_COPYAUDIO);
      video_unload(); P.live = 0; P.liveT0 = 0; hls_conv_start(P.url, ord, fl); fprintf(stderr, "[dts] audio track %d instead (%s)\n", ord, P.dts.copyA ? "copied" : "converted"); }
    render_tray(0); }
  else if (it->k == 4 && P.tray.kind == 3 && it->i == (P.curAud >= 0 ? P.curAud : video_audio_current())) { /* the track that already plays (the only one, often): nothing to switch, the tray closes; still the show's choice from now on (from Android TV's 10c87a66) */
    P.autoAudDone = 1; { VidTrack ts[16]; int n = video_audio_tracks(ts, 16); for (int i = 0; i < n; i++) if (ts[i].idx == it->i) show_pref_aud(ts[i].lang, ts[i].name); } fprintf(stderr, "[audio] the track playing chosen again: the tray closes\n"); close_tray(); }
  else if (it->k == 4 && P.tray.kind == 3) { P.autoAudDone = 1; set_audio(it->i); { VidTrack ts[16]; int n = video_audio_tracks(ts, 16); for (int i = 0; i < n; i++) if (ts[i].idx == it->i) show_pref_aud(ts[i].lang, ts[i].name); } render_tray(0); }
  else if (it->k == 2 && P.tray.kind == 6) { const LiveCh *c = live_lineup_item(it->i); if (c && strcmp(c->id, P.id)) { close_tray(); Item t; live_item(c, &t); live_start(&t, NULL, 0, 1); } else close_tray(); } /* a channel: switched to (the picture stays until it is found) */
  else if (it->k == 2 && it->hid) { spoil_reveal(js_str(it->v, "id", "")); it->hid = 0; sound_play(SND_TOGGLE); } /* a hidden episode: OK shows it first (its blur fading off) */
  else if (it->k == 2) { /* an episode: its sources, right here */
    const cJSON *v = it->v; P.tray.backIdx = P.tray.idx; P.tray.backTab = P.tray.tab; P.tray.kind = 5; snprintf(P.tray.epvid, sizeof P.tray.epvid, "%s", js_str(v, "id", "")); P.tray.idx = -1; P.tray.tab = 0; P.tray.zone = 0;
    snprintf(P.tray.title, 300, "S%d:E%d  ·  %s", (int)js_num(v, "season", 0), (int)js_num(v, "episode", 0), js_str(v, "name", js_str(v, "title", ""))); render_tray(1);
  } else if (it->k == 3) { int same = P.tray.kind == 2; const char *vid = same ? P.vid : P.tray.epvid; Stream s = *it->st; play_stream(&s, vid, same ? video_time() : 0); }
}
static TItem *tray_cur(void) { if (P.tray.ncols) return P.tray.c < P.tray.ncols && P.tray.r[P.tray.c] >= 0 && P.tray.r[P.tray.c] < P.tray.cols[P.tray.c].n ? &P.tray.cols[P.tray.c].items[P.tray.r[P.tray.c]] : NULL; return P.tray.idx >= 0 && P.tray.idx < P.tray.n ? &P.tray.row[P.tray.idx] : NULL; }
static int step_in(const TItem *l, int n, int from, int d) { for (int i = from + d; i >= 0 && i < n; i += d) if (focusable(&l[i])) return i; return -1; }
/* another tab: the pill glides there at once; what it shows slides out the other way, then the new one slides in */
static void tray_tab(int to) {
  if (to == P.tray.tab || to < 0 || to >= P.tray.ntabs) return;
  TM.swDir = to > P.tray.tab ? 1 : -1; P.tray.tab = to; if (P.tray.kind == 1) P.tray.season = P.tray.seasonList[to]; P.tray.idx = -1;
  TM.swPend = 1; mo_to(&TM.swA, 0, .1f, C_EASE, 0);
}
static void tray_swap_step(void) {
  if (!TM.swPend || TM.swA.on || TM.swA.v > 0.001f) return; TM.swPend = 0;
  if (P.tray.kind <= 0) { mo_set(&TM.swA, 1); return; }
  int z = P.tray.zone; TM.quiet = 1; render_tray(1); TM.quiet = 0; if (z == 1 && P.tray.ntabs) P.tray.zone = 1;
  mo_to(&TM.swA, 1, .18f, C_OUT, 0);
}
static int tray_key(int k) {
  if (P.tray.onBtn) { if (k == K_OK) close_tray(); else if (k == K_UP) P.tray.onBtn = 0; else if (k == K_LEFT || k == K_RIGHT) { close_tray(); P.bIdx = k == K_RIGHT ? (P.bIdx + 1 < P.nbtn ? P.bIdx + 1 : P.bIdx) : (P.bIdx > 0 ? P.bIdx - 1 : 0); } else if (k == K_BACK) close_tray(); return 1; }
  if (src_tray()) { /* the sources: Left / Right go from tab to tab wherever the focus is; Up from the first stream reaches the tabs */
    if (k == K_LEFT || k == K_RIGHT) { int to = P.tray.tab + (k == K_RIGHT ? 1 : -1); if (to >= 0 && to < P.tray.ntabs) { tray_tab(to); SR.moved = 0; } else nudge_start(&P.nudge, k); return 1; }
    if (k == K_BACK) { if (P.tray.kind == 5) { P.tray.zone = 0; tray_back(); } else close_tray(); return 1; } /* (Back: out at once, from the tabs or the list) */
    if (P.tray.zone == 1) { int any = 0; for (int i = 0; i < SR.n; i++) if (src_focusable(i)) any = 1;
      if ((k == K_DOWN || k == K_OK) && any) P.tray.zone = 0; else if (k == K_DOWN) P.tray.onBtn = 1; else nudge_start(&P.nudge, k); return 1; }
    if (k == K_UP || k == K_DOWN) { int d = k == K_DOWN ? 1 : -1, j = -1; for (int i = SR.idx + d; i >= 0 && i < SR.n; i += d) if (src_focusable(i)) { j = i; break; }
      if (j >= 0) { SR.idx = j; SR.moved = 1; } else if (k == K_DOWN) P.tray.onBtn = 1; else P.tray.zone = 1; }
    else if (k == K_OK) src_choose(SR.idx);
    else if (k == K_BACK) tray_back();
    return 1; }
  if (P.tray.zone == 1) {
    if ((k == K_LEFT && P.tray.tab > 0) || (k == K_RIGHT && P.tray.tab < P.tray.ntabs - 1)) tray_tab(P.tray.tab + (k == K_RIGHT ? 1 : -1));
    else if ((k == K_DOWN || k == K_OK) && P.tray.ncols) P.tray.zone = 2;
    else if ((k == K_DOWN || k == K_OK) && P.tray.idx >= 0) P.tray.zone = 0;
    else if (k == K_BACK) tray_back(); else if (k != K_OK) nudge_start(&P.nudge, k);
    return 1; }
  if (P.tray.zone == 2) { TCol *col = &P.tray.cols[P.tray.c];
    if (k == K_UP || k == K_DOWN) { int n = step_in(col->items, col->n, P.tray.r[P.tray.c], k == K_DOWN ? 1 : -1); if (n >= 0) P.tray.r[P.tray.c] = n; else if (k == K_DOWN) P.tray.onBtn = 1; else if (P.tray.ntabs) P.tray.zone = 1; else nudge_start(&P.nudge, k);
      if (n >= 0 && P.tray.kind == 4 && !strncmp(col->items[n].u, "lang:", 5)) { snprintf(P.subLang, sizeof P.subLang, "%s", col->items[n].u + 5); render_tray(0); } } /* the language under the focus: its subtitles beside it */
    else if (k == K_LEFT || k == K_RIGHT) { int c0 = P.tray.c; for (int c = P.tray.c + (k == K_RIGHT ? 1 : -1); c >= 0 && c < P.tray.ncols; c += k == K_RIGHT ? 1 : -1) if (P.tray.r[c] >= 0) { P.tray.c = c; break; } if (P.tray.c == c0) nudge_start(&P.nudge, k); }
    else if (k == K_OK) choose(tray_cur()); else if (k == K_BACK) tray_back();
    return 1; }
  if (k == K_LEFT) { int a = step_in(P.tray.row, P.tray.n, P.tray.idx, -1); if (a >= 0) P.tray.idx = a; else nudge_start(&P.nudge, k); }
  else if (k == K_RIGHT) { int b = step_in(P.tray.row, P.tray.n, P.tray.idx, 1); if (b >= 0) P.tray.idx = b; else nudge_start(&P.nudge, k); }
  else if (k == K_UP) { if (P.tray.ntabs) P.tray.zone = 1; else nudge_start(&P.nudge, k); }
  else if (k == K_DOWN) P.tray.onBtn = 1;
  else if (k == K_OK && tray_cur()) choose(tray_cur());
  else if (k == K_BACK) tray_back();
  return 1;
}

/* ---- keys ---- */
/* from the very start: the skips and the credits card are offered again */
static void from_start(void) { cancel_seek(); memset(P.dismissed, 0, sizeof P.dismissed); P.prompt.k = 0; paint_prompt(); P.ended = 0; P.endGoAt = 0; video_seek(0); if (paused()) video_play(); show_osd(1); }
static struct { float x[4], y, w[4], h; } DG; /* the card's buttons, for the pointer */
static void dly_draw(void);
static void pos_draw(void);
static void live_card_draw(void);
/* ---- the delay card: the subtitles' timing set while watching. A small card at the top with - and + (a quarter of a
   second each), the delay between them, play / pause and Done; the picture and the lines stay in view, so a change is
   seen at once. Back or Done closes it (mode 4) ---- */
#define DLY_STEP 0.25
static void dly_open(void) { close_tray(); P.osd = 0; P.mode = 4; P.dlyF = 0; mo_set(&P.dlyR, 0); mo_to(&P.dlyA, 1, .28f, C_OUT, 0); }
static void dly_close(void) { P.mode = 0; mo_to(&P.dlyA, 0, .2f, C_EASE, 0); }
static void dly_change(double d) { double v = P.subDelay + d; v = floor(v / DLY_STEP + 0.5) * DLY_STEP; if (v > 120) v = 120; if (v < -120) v = -120; P.subDelay = fabs(v) < 1e-6 ? 0 : v; P.dlyAt = now_(); }
static void dly_act(int i) { P.dlyF = i; mo_to(&P.dlyR, (float)i, .18f, C_OUT, 0);
  if (i == 0) dly_change(-DLY_STEP); else if (i == 1) dly_change(DLY_STEP); else if (i == 2) toggle(); else dly_close(); }
static void dly_key(int k) {
  if (k == K_LEFT) { if (P.dlyF > 0) { P.dlyF--; mo_to(&P.dlyR, (float)P.dlyF, .18f, C_OUT, 0); } else nudge_start(&P.nudge, k); }
  else if (k == K_RIGHT) { if (P.dlyF < 3) { P.dlyF++; mo_to(&P.dlyR, (float)P.dlyF, .18f, C_OUT, 0); } else nudge_start(&P.nudge, k); }
  else if (k == K_OK) dly_act(P.dlyF);
  else if (k == K_UP || k == K_DOWN) nudge_start(&P.nudge, k);
}
/* the position card (mode 5): the tray steps away so the subtitles sit where they will; Up and Down move them 1 % at a
   time, gliding there, a sample line standing in when none shows; OK or Back is done */
/* every subtitle style is set where it can be seen: the tray steps away, the subtitles (or a sample line) sit where they
   will, the card at the top holds that style's choices; size, colour and background move with Left / Right and show at
   once, the height with Up / Down */
static void style_open(int kind) { close_tray(); P.osd = 0; P.mode = 5; P.styK = kind; P.posAt = 0; mo_to(&P.posA, 1, .28f, C_OUT, 0); }
static void pos_open(void) { style_open(0); }
static void style_back(int kind) { /* the subtitle tray again, its Style tab, the row of the style just set */
  subTab = (P.nSubAddons > 0 || P.nAdx > 0) ? 2 : 1; show_osd(1); open_tray(4); P.tray.zone = 2; P.tray.c = 0; /* (the controls with it, as it was opened: a tray never floats alone) */
  static const char *const U[7] = { "pos", "size", "color", "bg", "bright", "font", "fontar" }; TCol *c = &P.tray.cols[0]; for (int i = 0; i < c->n; i++) if (!strcmp(c->items[i].u, U[kind >= 0 && kind < 7 ? kind : 0])) P.tray.r[0] = i; }
static int style_idx(void) { int c = color_idx(); return P.styK == 1 ? size_idx() : P.styK == 2 ? (c < 0 ? 0 : c) : P.styK == 4 ? bright_idx() : P.styK == 5 ? subfont_lat() : P.styK == 6 ? subfont_ar() : bg_idx(); }
static int style_n(void) { return P.styK == 1 ? 7 : P.styK == 4 ? 4 : P.styK >= 5 ? SUBF_N : 3; }
static void style_change(int d) {
  int i = style_idx() + d; if (i < 0 || i >= style_n()) { nudge_start(&P.nudge, d > 0 ? K_RIGHT : K_LEFT); return; }
  if (P.styK == 1) { char v[4]; snprintf(v, sizeof v, "%d", i); store_set("subtitleSize", v); } else if (P.styK == 2) store_set("subsColor", SUB_COLORS[i]); else if (P.styK == 4) store_set("subsBright", SUB_BRIGHT[i]); else if (P.styK >= 5) { char v[4]; snprintf(v, sizeof v, "%d", i); store_set(P.styK == 5 ? "subsFont" : "subsFontAr", v); } else store_set("subsBgStyle", SUB_BGS[i]);
  P.posAt = now_(); }
static void pos_close(void) { P.mode = 0; mo_to(&P.posA, 0, .2f, C_EASE, 0); }
static void pos_change(int d) { int v = pos_pct() + d; if (v < POS_MIN || v > POS_MAX) { nudge_start(&P.nudge, d > 0 ? K_UP : K_DOWN); return; } char b[8]; snprintf(b, sizeof b, "%d", v); store_set("subsPosPct", b); P.posAt = now_(); }
static void pos_key(int k) {
  if (P.styK) { if (k == K_LEFT || k == K_RIGHT) style_change(app_mirror_key(k) == K_RIGHT ? 1 : -1); else if (k == K_OK) pos_close(); else if (k == K_UP || k == K_DOWN) nudge_start(&P.nudge, k); return; }
  if (k == K_UP) pos_change(1); else if (k == K_DOWN) pos_change(-1); else if (k == K_OK) pos_close(); else if (k == K_LEFT || k == K_RIGHT) nudge_start(&P.nudge, k); }
static struct { float y, h, ux, uw, dx, dw, ox, ow; } PZ; /* the card's parts, for the pointer */
static void hub_from_player(void) { close_tray(); P.osd = 0; P.mode = 6; hub_open(1, P.id); } /* the live hub over the channel playing (mode 6) */
static void act(const char *id) {
  if (!strcmp(id, "pp")) toggle(); else if (!strcmp(id, "beg")) from_start(); else if (!strcmp(id, "asp")) next_aspect(); else if (!strcmp(id, "nxt")) next_auto(); else if (!strcmp(id, "cc")) cc_toggle();
  else if (!strcmp(id, "chn")) { hub_from_player(); return; }
  else open_tray(!strcmp(id, "eps") ? 1 : !strcmp(id, "src") ? 2 : !strcmp(id, "aud") ? 3 : 4);
  arm_hide();
}
static void back(void) {
  if (P.closing) return;
  if (P.mode == 2) { tray_back(); return; }
  if (P.mode == 4) { dly_close(); if (!P.isTrailer) { subTab = (P.nSubAddons > 0 || P.nAdx > 0) ? 2 : 1; open_tray(4); P.tray.zone = 2; P.tray.c = 1; TCol *c = &P.tray.cols[1]; for (int i = 0; i < c->n; i++) if (!strcmp(c->items[i].u, "dlyopen")) P.tray.r[1] = i; } return; } /* Back: the tray again, on Adjust */
  if (P.mode == 5) { int k = P.styK; pos_close(); if (!P.isTrailer) style_back(k); return; } /* Back: the tray again, on that style */
  if (P.mode == 6) { if (!hub_back()) { hub_close(); P.mode = 0; } return; }
  if (P.adopt && adoptFull && !P.failed && lpk_can_return(P.id) && (P.mode == 0 || P.mode == 1)) { hide_osd(); if (lpk_take_back(P.id)) { fprintf(stderr, "[player] back to All channels, the channel playing on behind it\n"); keepVideo = 1; adoptFull = 0; finish_close(); } else close_player(0, 0); return; } /* taken full screen from behind All channels: back there, playing on */
  if (P.adopt && !P.failed && shrinkAt <= 0 && (P.live || growPending || growAt > 0) && lpk_can_return(P.id) && (P.mode == 0 || P.mode == 1)) { hide_osd(); grow_reverse(); return; } /* grown from its preview: back into the box, playing on */ /* the hub: from the channels up to the catalogs, then away */
  if (P.prompt.k && P.prompt.autoN) { P.prompt.autoN = 0; paint_prompt(); return; } /* a countdown running (skip, next episode): Back stops it, the controls up or not; the prompt stays */
  if (P.mode == 3) { dismiss_prompt(); return; }
  if (P.seekT >= 0) { cancel_seek(); return; }
  if (P.mode == 1 && !paused()) { hide_osd(); return; }
  close_player(0, 0);
}
static void ss_exit(void) { P.ssOn = 0; P.lastAct = now_(); }
static void try_next(void);
void player_key(int k, int down) {
  if (!down) { if (P.open && P.mode == 6 && k == K_OK) hub_ok_up(); return; } /* (the hub over the picture: OK let go, a tap; held, its streams) */
  if (!P.open) return; if (shrinkAt > 0) return; /* (going back into its box) */ app_touch();
  if (P.pw.on && k != K_BACK) { if (k == K_OK) pw_end("Play now"); return; } /* (waiting for the previews: OK plays at once, Back closes, nothing else) */
  P.lastAct = now_(); if (P.ssOn) { ss_exit(); return; } /* the key that wakes the screensaver does nothing else */
  if (P.failed && P.canNext && k == K_OK) { try_next(); return; }
  if (!P.started || !P.live || P.failed) { if (k == K_BACK || k == K_EXIT) close_player(0, 0); return; } /* the art is up (resolving): only Back does anything */
  P.hvOn = 0;
  if ((P.mode == 4 || P.mode == 5) && (k == K_PLAY || k == K_PAUSE || k == K_PLAYPAUSE)) { if (k == K_PLAYPAUSE || (k == K_PLAY) == paused()) toggle(); return; } /* the card stays: the controls don't come */
  if (k == K_PLAY) { if (paused()) play_on(); show_osd(1); return; }
  if (k == K_PAUSE) { if (!paused()) { video_pause(); save_progress(1); } show_osd(1); return; }
  if (k == K_PLAYPAUSE) { toggle(); show_osd(1); return; }
  if (k == K_STOP || k == K_EXIT) { close_player(0, 0); return; }
  if (k == K_REW) { seek_by(-1); return; } if (k == K_FF) { seek_by(1); return; }
  if (k == K_BACK) { back(); return; }
  if (P.mode == 2) { tray_key(k); return; }
  if (P.mode == 4) { dly_key(k); return; }
  if (P.mode == 5) { pos_key(k); return; }
  if (P.mode == 3) {
    if (P.prompt.k == 3 && P.next.has && (k == K_LEFT || k == K_RIGHT)) { nudge_start(&P.nudge, k); return; } /* the card: OK or BACK */
    if ((k == K_LEFT && P.prompt.idx == 0) || (k == K_RIGHT && P.prompt.idx >= P.prompt.nbtn - 1)) nudge_start(&P.nudge, k);
    else if (k == K_LEFT) P.prompt.idx--; else if (k == K_RIGHT) P.prompt.idx++;
    else if (k == K_OK) prompt_go(P.prompt.idx);
    else if (k == K_UP || k == K_DOWN) { P.mode = 0; show_osd(0); }
    return; }
  if (k == K_YELLOW && live_title() && live_catalogs() > 0 && P.mode != 6 && P.mode != 2) { hub_from_player(); return; } /* the yellow key: the channels over the picture */
  if (P.mode == 6) { if (k == K_CH_UP || k == K_CH_DOWN) { live_zap(k == K_CH_UP ? 1 : -1); return; } hub_key(k); if (!hub_on()) P.mode = 0; return; } /* the hub has the keys */
  if (P.mode == 0 && live_title() && P.seekT < 0) { /* a channel: the channel keys switch (the yellow key or the Channels button: the hub), Left / Right bring the controls (nothing to seek); Down as ever */
    if ((k == K_CH_UP || k == K_CH_DOWN) && live_lineup_n() > 1) { live_zap(k == K_CH_UP ? 1 : -1); return; }
    if (k == K_OK && live_catalogs() > 0) { hub_from_player(); return; } /* OK: the channels */
    if (k == K_LEFT || k == K_RIGHT) { show_osd(0); return; } }
  if (P.mode == 0) {
    if (k == K_OK) { if (P.seekT >= 0) commit_ok(); else if (P.prompt.k) prompt_go(P.prompt.idx); else { toggle(); show_osd(0); } return; }
    if (k == K_LEFT) { seek_by(-1); return; } if (k == K_RIGHT) { seek_by(1); return; }
    if ((k == K_UP || k == K_DOWN) && P.seekT >= 0 && chapters_on()) { chap_jump(k == K_UP ? 1 : -1); return; } /* seeking: from chapter to chapter */
    if (k == K_UP || k == K_DOWN) { if (P.seekT >= 0) commit_seek(); show_osd(0); return; }
    return; }
  arm_hide();
  if (P.row == 1) {
    if ((k == K_UP || k == K_DOWN) && P.seekT >= 0 && chapters_on()) { chap_jump(k == K_UP ? 1 : -1); return; } /* seeking: from chapter to chapter */
    if (k == K_LEFT) seek_by(-1); else if (k == K_RIGHT) seek_by(1); else if (k == K_OK) { if (P.seekT >= 0) commit_ok(); else toggle(); }
    else if (k == K_DOWN) { if (P.seekT >= 0) commit_seek(); P.row = 0; }
    else if (k == K_UP) { if (P.prompt.k) { P.mode = 3; } else nudge_start(&P.nudge, k); }
    return; }
  if ((k == K_LEFT && P.bIdx == 0) || (k == K_RIGHT && P.bIdx == P.nbtn - 1)) nudge_start(&P.nudge, k); /* the row's end */
  else if (k == K_LEFT) P.bIdx--; else if (k == K_RIGHT) P.bIdx++;
  else if (k == K_UP) { if (live_title()) nudge_start(&P.nudge, k); else P.row = 1; } else if (k == K_DOWN) hide_osd(); /* (a channel: no bar to seek on) */ else if (k == K_OK) act(P.btns[P.bIdx].id);
}

/* ---- open / start / close ---- */
static void fill_from_meta(void) {
  const cJSON *m = P.meta; if (!m) return;
  snprintf(P.name, sizeof P.name, "%s", js_str(m, "name", P.name)); const char *lg = js_str(m, "logo", NULL); if (lg && *lg) snprintf(P.logo, sizeof P.logo, "%s", lg);
  if (!P.poster[0]) snprintf(P.poster, sizeof P.poster, "%s", js_str(m, "poster", "")); snprintf(P.year, sizeof P.year, "%.4s", js_str(m, "releaseInfo", js_str(m, "year", "")));
  const cJSON *gs = js_arr(m, "genres"); if (gs && cJSON_GetArraySize(gs) && cJSON_IsString(cJSON_GetArrayItem(gs, 0))) snprintf(P.genre, sizeof P.genre, "%s", cJSON_GetArrayItem(gs, 0)->valuestring);
  { const cJSON *vv = js_arr(m, "videos"); if (!strcmp(P.type, "series") || (vv && cJSON_GetArraySize(vv) > 1 && strcmp(P.type, "movie") && strcmp(P.type, "other"))) P.videos = (cJSON *)vv; }
  if (P.videos && strcmp(P.vid, P.id)) { P.season = P.episode = -1; /* from the video's own numbers; a series' id ("tt..:s:e") as the fallback (an anime id like "kitsu:123:5" is no season) */
    { const char *c = strrchr(P.vid, ':'); if (c && !strcmp(P.type, "series")) { const char *p = c - 1; while (p > P.vid && *p != ':') p--; if (*p == ':') { P.season = atoi(p + 1); P.episode = atoi(c + 1); } } }
    { const cJSON *v0; cJSON_ArrayForEach(v0, P.videos) if (!strcmp(js_str(v0, "id", ""), P.vid)) { if (js_num(v0, "season", -1) >= 0) P.season = (int)js_num(v0, "season", 0); if (js_num(v0, "episode", -1) >= 0) P.episode = (int)js_num(v0, "episode", 0); break; } }
    const cJSON *cur = NULL, *nxt = NULL, *first = NULL, *v; cJSON_ArrayForEach(v, P.videos) { if (!strcmp(js_str(v, "id", ""), P.vid)) cur = v; int s = (int)js_num(v, "season", -1), e = (int)js_num(v, "episode", -1); if (s == P.season && e == P.episode + 1) nxt = v; if (P.season >= 0 && s == P.season + 1 && (!first || e < (int)js_num(first, "episode", 0))) first = v; }
    if (!nxt && first) nxt = first; /* last episode of a season -> first of the next one */
    if (cur && !nxt && js_num(cur, "season", -1) < 0) { /* no seasons (anime, channels): the next in the list, by episode number when given, else in order */
      int ce = (int)js_num(cur, "episode", -1), seen = 0; const cJSON *after = NULL;
      cJSON_ArrayForEach(v, P.videos) { if (ce >= 0 && (int)js_num(v, "episode", -1) == ce + 1) { nxt = v; break; } if (seen && !after) after = v; if (v == cur) seen = 1; }
      if (!nxt) nxt = after; }
    if (nxt) { const char *rel = js_str(nxt, "released", ""); struct tm tm; memset(&tm, 0, sizeof tm); if (strlen(rel) >= 10 && sscanf(rel, "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) == 3) { tm.tm_year -= 1900; tm.tm_mon--; if (timegm(&tm) > time(NULL)) nxt = NULL; } } /* not out yet */
    if (cur) snprintf(P.epTitle, sizeof P.epTitle, "%s", js_str(cur, "name", js_str(cur, "title", "")));
    if (nxt) { P.next.has = 1; snprintf(P.next.vid, 80, "%s", js_str(nxt, "id", "")); if (js_num(nxt, "season", -1) >= 0) snprintf(P.next.se, 32, "S%d · E%d", (int)js_num(nxt, "season", 0), (int)js_num(nxt, "episode", 0)); else if (js_num(nxt, "episode", -1) >= 0) snprintf(P.next.se, 32, "E%d", (int)js_num(nxt, "episode", 0)); else P.next.se[0] = 0; snprintf(P.next.title, 256, "%s", js_str(nxt, "name", js_str(nxt, "title", ""))); snprintf(P.next.thumb, 512, "%s", js_str(nxt, "thumbnail", "")); snprintf(P.next.ov, 600, "%s", js_str(nxt, "overview", js_str(nxt, "description", ""))); }
  } else if (P.type[0] && strcmp(P.type, "other")) P.sim = 1;
}
/* the title's metadata: from memory at once, else from the disk cache (that answer can come back before the player
   is open) or the network; a series still missing its episodes or next episode asks again 20 s later */
static unsigned metaTok;
static void on_meta(cJSON *m, void *ud) {
  if ((unsigned)(uintptr_t)ud != metaTok || !m) return;
  if (P.meta && P.videos) return; /* nothing missing (and the open tray points into this copy) */
  if (P.meta) cJSON_Delete(P.meta); P.meta = cJSON_Duplicate(m, 1); fill_from_meta(); if (P.started) build_btns();
}
static void ask_meta(void) { metaTok++; if (!metaTok) metaTok++; sb_meta(P.type, P.id, on_meta, (void *)(uintptr_t)metaTok); }
static void on_size(int st, const unsigned char *body, size_t len, void *ud) {
  (void)len; if (!P.open || (unsigned)(uintptr_t)ud != P.sizeTok) return; P.sizeAns = 1; P.httpSt = st; if (st >= 400) fprintf(stderr, "[player] the stream's server answers %d\n", st); if (st <= 0) return; double b = atof((const char *)body); if (b <= 0) return; P.size = b;
  char n[16]; if (b >= 1e9) { snprintf(n, sizeof n, "%.*f", b >= 1e10 ? 0 : 1, b / 1e9); snprintf(P.sizeTxt, sizeof P.sizeTxt, "%s", T1("{n} GB", "n", n)); } else { snprintf(n, sizeof n, "%d", (int)(b / 1e6 + 0.5)); snprintf(P.sizeTxt, sizeof P.sizeTxt, "%s", T1("{n} MB", "n", n)); }
}
/* A stream whose text names only TrueHD, or only DTS on a TV seen failing it: the TV is not given it until the file's
   tracks are read (the subtitle reader's head read, a moment): a track it plays is chosen once it lists them, else the
   conversion starts without a silent first try. Every other stream loads at once, as always */
static int dts_defer_wanted(void) {
  if (P.isTrailer || P.isHls || strncmp(P.url, "http", 4) || !hls_conv_available()) return 0;
  BestRate r; if (!best_rate(&P.st, NULL, -1, 100, &r) || !r.mute) return 0;
  char au[48]; best_audio_of(P.sname, au, sizeof au); return strstr(au, "TrueHD") || best_learned("dts") == 0;
}
static void dts_defer_tick(void) {
  if (P.dts.conv) { P.dts.deferUntil = 0; return; } /* (started meanwhile, from the file's head) */
  int listed = mkv_listed(mkv); if (!listed && mkv_state(mkv) != MKV_FALLBACK && now_() < P.dts.deferUntil) return;
  P.dts.deferUntil = 0; MkvAudio ma[16]; int m = listed ? mkv_audio_tracks(mkv, ma, 16) : 0, playable = 0, first = -1;
  for (int i = 0; i < m; i++) { VidTrack v; memset(&v, 0, sizeof v); snprintf(v.codec, sizeof v.codec, "%s", ma[i].codec); if (!aud_bad(&v)) playable = 1; else if (first < 0) first = i; }
  fprintf(stderr, "[dts] the file's sound: %d track(s), %s\n", m, !m ? "not read: the TV tries it" : playable ? "one plays here: the TV loads it" : "none plays here");
  if (m && !playable && first >= 0) { const char *want = aud_want(); int ord = first; /* the track in the language wanted (this show's own, else the settings'), else the first */
    if (*want) for (int i = 0; i < m; i++) if (lang_match(ma[i].lang, want)) { ord = i; break; }
    if (dts_convert(ord, "known before loading")) return; }
  if (listed && dv7_convert("known before loading")) return;
  video_load(P.url);
}
static void start_video(void) {
  P.started = 1; P.isTrailer = !strcmp(P.type, "other"); P.isHls = strstr(P.url, ".m3u8") != NULL; P.liveStream = P.hlsVod = 0; liveTok++; for (int i = 0; i < LIVE_RELOADS; i++) liveRe[i] = -1e9;
  best_caps_probe(NULL, NULL); /* (once: kept) */
  video_start_silent(pw_wanted()); /* (to be held for its previews: it loads silent) */
  if (P.adopt) { fprintf(stderr, "[player] taking the preview's playback as it plays%s\n", adoptFull ? " (full screen already)" : ""); P.liveT0 = now_(); P.steadyAt = now_() - 1; growPending = !adoptFull; growAt = 0; planeAt = 0; memcpy(planeSent, growR, sizeof planeSent); } /* (it grows once the page under it is pictured) */ /* (already loaded and running) */
  else if (dts_defer_wanted() || dv7_defer_wanted()) P.dts.deferUntil = now_() + 4; else video_load(P.url);
  P.ps.on = ps_wanted(); P.ps.v = 0; P.ps.t0 = now_(); sp_verify_reset();
  if (P.ps.on) { double fs = P.st.size > 0 ? P.st.size : P.size; ps_want(P.type, P.id, P.vid, best_dur(P.meta, P.type), P.url, fs); ps_hold(1); fprintf(stderr, "[player] its seek previews from a copy 2.5x lighter if one proves the same cut (the film %.1f GB%s)\n", fs / 1e9, film_heavy() ? ", heavy" : ""); } else ps_cancel(); /* (a light film: its own keyframes; a copy made ahead stops, never beside it) */
  P.pw.on = pw_wanted(); P.pw.held = 0; P.pw.t0 = now_(); P.pw.target = pw_target(); P.pw.eta = 0; P.pw.shown = 0; P.pw.etaShown = 0; P.pw.lastF = -1; P.pw.made = 0; P.pw.slips = 0; P.pw.holdT = 0; P.pw.gainAt = now_(); if (P.pw.on) fprintf(stderr, "[player] previews first: waiting for %d %% of them, the film loading meanwhile (OK plays at once)\n", P.pw.target);
  live_probe(); /* (after the load: video_set_live holds for this source) */
  build_btns(); if (!P.adopt) apply_aspect(); P.res = res_of(P.sname);
  if ((episodic() || (strcmp(P.type, "movie") && strcmp(P.type, "other") && strcmp(P.vid, P.id))) && !P.isTrailer && (!P.next.has || !P.videos)) { ask_meta(); P.metaRetryAt = now_() + 20; }
  if (!P.isHls && !P.isTrailer && !strncmp(P.url, "http", 4)) { static unsigned szTok; P.sizeTok = ++szTok; net_size(P.url, on_size, (void *)(uintptr_t)P.sizeTok); } /* asked once per stream */
  asssubs_close(); if (mkv) { prevspike_stop(); sp_close(); mkv_close(mkv); mkv = NULL; }
  if (!P.isHls && !P.isTrailer && !strncmp(P.url, "http", 4)) { mkvWantCues = prev_enabled() && !P.isP2p && sp_may_make(); /* (seek previews want the index even with no text track; never for a torrent on a TV) */
    mkv = mkv_open(P.url, P.startAt > 0 ? P.startAt : 0, P.isP2p); } /* not Matroska: it gives up after one small read */
  load_ext_subs(0); load_addon_subs(0); load_emb_tracks();
}
static char nextAlt[SB_URL_MAX];
void player_fallback(const char *url) { snprintf(nextAlt, sizeof nextAlt, "%s", url ? url : ""); }
/* a new source / episode: the state starts over behind the title art (the art crossfades in when already playing) */
/* ---- the seek pill's time as a rolling clock: each character keeps a slot (digits as wide as the widest), only the
   ones that change roll: the old one slides down and fades, the new one comes in from above (the other way seeking
   back). A change coming faster than the roll shortens it rather than queueing ---- */
#define ROLL_MAX 24
typedef struct { char c[ROLL_MAX][5]; float w[ROLL_MAX]; int n; float tot; } RollStr;
static struct { char key[48]; RollStr cur, prev; Mo t; int dir; double at; float dw; } RL;
static void roll_split(const char *s, RollStr *o) {
  o->n = 0; o->tot = 0;
  while (*s && o->n < ROLL_MAX) { int k = 1; unsigned char b = (unsigned char)*s; if (b >= 0xf0) k = 4; else if (b >= 0xe0) k = 3; else if (b >= 0xc0) k = 2;
    char *c = o->c[o->n]; int i = 0; for (; i < k && s[i]; i++) c[i] = s[i]; c[i] = 0; s += i;
    o->w[o->n] = c[0] >= '0' && c[0] <= '9' && !c[1] ? RL.dw : text_width(c, 25, 600, 0) + .2f; o->tot += o->w[o->n]; o->n++; }
}
static float roll_layout(const char *txt, int dir) {
  if (RL.dw <= 0) for (char d = '0'; d <= '9'; d++) { char b[2] = { d, 0 }; float w = text_width(b, 25, 600, 0) + .2f; if (w > RL.dw) RL.dw = w; }
  if (strcmp(RL.key, txt)) {
    int first = !RL.key[0]; double now = now_(); float d = first ? 0 : (float)fmin(.18, fmax(.07, now - RL.at)); /* held keys: the roll keeps up */
    RL.prev = RL.cur; snprintf(RL.key, sizeof RL.key, "%s", txt); roll_split(txt, &RL.cur); RL.dir = dir < 0 ? -1 : 1; RL.at = now;
    if (first) { RL.prev = RL.cur; mo_set(&RL.t, 1); } else { mo_set(&RL.t, 0); mo_to(&RL.t, 1, d, C_OUT, 0); }
  }
  return RL.cur.tot;
}
static void roll_draw(float cx, float y, float lh, uint32_t col) {
  float x = cx + RL.cur.tot / 2, t = RL.t.v, dy = lh * .55f * RL.dir; int n = RL.cur.n > RL.prev.n ? RL.cur.n : RL.prev.n;
  for (int j = 0; j < n; j++) { int ic = RL.cur.n - 1 - j, ip = RL.prev.n - 1 - j; /* slots counted from the right: the seconds stay put */
    const char *a = ic >= 0 ? RL.cur.c[ic] : NULL, *b = ip >= 0 ? RL.prev.c[ip] : NULL; float w = a ? RL.cur.w[ic] : RL.prev.w[ip], mx = x - w / 2; x -= w;
    if (t >= 1 || (a && b && !strcmp(a, b))) { if (a) text_draw(a, mx, y, 25, 600, col, 0, 0, ALIGN_CENTER); continue; }
    if (b) text_draw(b, mx, y + dy * t, 25, 600, col_alpha(col, 1 - t), 0, 0, ALIGN_CENTER);
    if (a) text_draw(a, mx, y - dy * (1 - t), 25, 600, col_alpha(col, t), 0, 0, ALIGN_CENTER); }
}
static void draw_osd(void); static void draw_tray(void); static void live_card_draw(void);
/* another source or episode from inside the player: the controls as they are on screen at that moment, taken ONCE as
   a picture, fade off as the controls do (.2 s) while the art comes back over them (.45 s); freed once gone */
static GfxTarget handSnap; static Mo handA;
static void hand_snap(void) {
  if (!handSnap.fbo && !gfx_target_create(&handSnap, 1280, 720)) return;
  gfx_target_begin(&handSnap, RGBA(0, 0, 0, 0)); draw_osd(); draw_tray(); gfx_target_end();
  mo_set(&handA, 1); mo_to(&handA, 0, .2f, C_EASE, 0);
}
static void hand_tick(float dt) { mo_tick(&handA, dt); if (handSnap.fbo && !handA.on && handA.v <= 0.001f) gfx_target_free(&handSnap); }
static void hand_draw(void) { if (handSnap.fbo && handA.v > 0.001f) gfx_image(handSnap.tex, 0, 0, SB_W, SB_H, 0, WHITE_A((int)(255 * handA.v + .5f)), 0, 1, 1, 0); }
static void np_stop(void);
static void prepare(const char *vid, double startAt, int keepSubs) {
  resumeMsg[0] = 0; loadGen++; np_stop(); /* (the next episode's pictures made ahead: kept, this one opens with them) */
  int again = P.open; char type[16]; snprintf(type, sizeof type, "%s", P.type);
  float artWas = again ? P.artA.v : 1; /* the art comes back from where it is (the page's .45 s), never at once */
  if (again && P.live && artWas < 0.999f) hand_snap();
  if (again) { video_unload(); hls_conv_stop(); P.live = 0; asssubs_close(); if (mkv) { prevspike_stop(); sp_close(); mkv_close(mkv); mkv = NULL; } if (P.isP2p || P.resolving) p2p_player_switching(); } /* (another source inside the player: the torrent kept until the next one is known, p2p.c) */
  SubEntry *keep = NULL, *keepA = NULL; int nkeep = 0, nkeepA = 0; if (again && keepSubs && !strcmp(vid, P.vid)) { if (P.nExt) { keep = P.ext; nkeep = P.nExt; P.ext = NULL; } if (P.nAdx) { keepA = P.adx; nkeepA = P.nAdx; P.adx = NULL; } }
  cJSON *meta = P.meta; P.meta = NULL; subs_free(P.cues, P.ncues); free(P.ext); free(P.adx); srcs_free(); cols_clear();
  char id[SB_ID]; snprintf(id, sizeof id, "%s", P.id); char name[256]; snprintf(name, sizeof name, "%s", P.name); char poster[512]; snprintf(poster, sizeof poster, "%s", P.poster); char bg[512]; snprintf(bg, sizeof bg, "%s", P.bg); char logo[512]; snprintf(logo, sizeof logo, "%s", P.logo); unsigned subReq = P.subReq, adxReq = P.adxReq;
  char v[SB_VID]; snprintf(v, sizeof v, "%s", vid && *vid ? vid : id);
  sync_stop(0); memset(&P, 0, sizeof P); sub_reset(); P.subReq = subReq; P.adxReq = adxReq; P.open = 1; P.openedAt = now_(); P.seekT = -1; P.curAud = -1; snprintf(P.curSub, sizeof P.curSub, "off");
  snprintf(P.type, sizeof P.type, "%s", type); snprintf(P.id, sizeof P.id, "%s", id); snprintf(P.vid, sizeof P.vid, "%s", v); if (aud_original_mode()) (void)tmdb_orig_lang(P.id); /* (Original audio: its language on its way while the film loads) */ snprintf(P.name, sizeof P.name, "%s", name); snprintf(P.poster, sizeof P.poster, "%s", poster); snprintf(P.bg, sizeof P.bg, "%s", bg); snprintf(P.logo, sizeof P.logo, "%s", logo);
  P.meta = meta; P.ext = keep; P.nExt = nkeep; P.adx = keepA; P.nAdx = nkeepA; P.startAt = startAt; aspect_load(); set_status("");
  if (!strcmp(P.type, "series")) { const char *c = strrchr(P.vid, ':'); if (c && c > P.vid) { const char *q = c - 1; while (q > P.vid && *q != ':') q--; if (*q == ':') { P.season = atoi(q + 1); P.episode = atoi(c + 1); } } } /* known from the id, the metadata or not */
  memset(&P.pg.g, 0, sizeof P.pg.g); P.pg.age = 0; P.pg.certAge = -1; P.pg.guideIn = P.pg.certIn = 0; P.pg.asked = 0; P.pg.sess++; P.pg.at = P.pg.askAt = 0; mo_set(&P.pgA, 0); mo_set(&P.pgY, -10);
  mo_set(&P.artA, artWas); mo_set(&P.in, again ? 1 : 0); mo_set(&P.lnA, 1); mo_set(&P.pwA, 0); mo_set(&P.pwF, 0); P.pw.on = 0; mo_set(&P.prB, 104); mo_set(&P.subB, SB_H * pos_pct() / 100.0f); mo_set(&P.ttlX, -30); mo_set(&P.barY, 18); mo_set(&P.btnY, 24); mo_set(&P.topY, -12); mo_set(&P.prY, 20); mo_set(&P.skS, 0); mo_set(&P.ccS, 1);
  fill_from_meta(); if (!P.logo[0] && !strncmp(P.id, "tt", 2)) snprintf(P.logo, sizeof P.logo, "https://images.metahub.space/logo/medium/%s/img", P.id);
  if (!P.bg[0]) { Item it = lib_item(); sb_bg_mid(&it, P.bg, sizeof P.bg); }
}
static void on_p2p(void *ud, const char *url, const char *err, uint64_t size) {
  if (!P.open || P.closing || (unsigned)(uintptr_t)ud != p2pTok) return;
  P.resolving = 0;
  if (!url) { fail(err ? err : T("No peers found for this torrent")); return; }
  snprintf(P.url, sizeof P.url, "%s", url); P.isP2p = 1; set_status("");
  if (size) { P.size = (double)size; char n[16]; if (P.size >= 1e9) { snprintf(n, sizeof n, "%.*f", P.size >= 1e10 ? 0 : 1, P.size / 1e9); snprintf(P.sizeTxt, sizeof P.sizeTxt, "%s", T1("{n} GB", "n", n)); } else { snprintf(n, sizeof n, "%d", (int)(P.size / 1e6 + 0.5)); snprintf(P.sizeTxt, sizeof P.sizeTxt, "%s", T1("{n} MB", "n", n)); } }
  start_video();
}
static void on_yt(const Trailer *t, const char *err, void *ud) {
  if (!P.open || P.closing || (unsigned)(uintptr_t)ud != p2pTok) return; P.resolving = 0; P.ytTok = 0;
  if (!t) { fail(T("Couldn’t play this YouTube video")); fprintf(stderr, "[player] YouTube: %s\n", err ? err : "?"); return; }
  snprintf(P.url, sizeof P.url, "%s", t->url); snprintf(P.alt, sizeof P.alt, "%s", t->alt); start_video();
}
static int quietNext;
void player_quiet_next(void) { quietNext = 1; }
/* a preview playing in its box, picked: the player takes that very playback (nothing loaded again, no art), and the
   picture grows from the box to the screen while the page fades off around it */
static struct { int on; char src[SB_URL_MAX]; float x, y, w, h, r; } AD;
void player_adopt_next(const char *src, float x, float y, float w, float h, float r) { AD.on = 1; snprintf(AD.src, sizeof AD.src, "%s", src); AD.x = x; AD.y = y; AD.w = w; AD.h = h; AD.r = r; }
static float ease_io(double t) { t = t < 0 ? 0 : t > 1 ? 1 : t; return (float)(t < 0.5 ? 4 * t * t * t : 1 - (-2 * t + 2) * (-2 * t + 2) * (-2 * t + 2) / 2); }
static void grow_rect(float k, float *o) { o[0] = growR[0] * (1 - k); o[1] = growR[1] * (1 - k); o[2] = growR[2] + (SB_W - growR[2]) * k; o[3] = growR[3] + (SB_H - growR[3]) * k; } /* the box (k 0) to the screen (k 1) */
/* the plane told where the picture goes: at the pace this TV takes, never an edge moving back (out: growing) */
static void plane_step(const float *want, int out, int force) { VidPlaneCaps c = video_plane_caps(); double now = now_();
  if (!force && !c.perFrame && planeAt > 0 && (now - planeAt) * 1000 < c.stepMs) return;
  float x0 = want[0], y0 = want[1], x1 = want[0] + want[2], y1 = want[1] + want[3], s0 = planeSent[0], t0 = planeSent[1], s1 = planeSent[0] + planeSent[2], t1 = planeSent[1] + planeSent[3];
  if (!force) { if (out) { x0 = fminf(x0, s0); y0 = fminf(y0, t0); x1 = fmaxf(x1, s1); y1 = fmaxf(y1, t1); } else { x0 = fmaxf(x0, s0); y0 = fmaxf(y0, t0); x1 = fminf(x1, s1); y1 = fminf(y1, t1); } }
  if (x1 - x0 < 2 || y1 - y0 < 2) return;
  video_set_window(x0, y0, x1 - x0, y1 - y0); planeSent[0] = x0; planeSent[1] = y0; planeSent[2] = x1 - x0; planeSent[3] = y1 - y0; planeAt = now; growCalls++; }
/* Back while it grows (or before it started): the same window goes back the way it came, from where it is */
static void grow_reverse(void) {
  float kh = growPending ? 0 : growAt > 0 ? grow_k() : 1; growPending = 0; growAt = 0; /* (grown already: from the whole screen) */
  if (kh >= 1) { planeSent[0] = 0; planeSent[1] = 0; planeSent[2] = SB_W; planeSent[3] = SB_H; } /* (its framing since: the shrink starts from the screen) */
  double lo = 0, hi = SHRINK_D; for (int i = 0; i < 24; i++) { double m = (lo + hi) / 2; if (ease_io(m / SHRINK_D) < 1 - kh) lo = m; else hi = m; } /* (the shrink's moment whose window is this one) */
  shrinkAt = now_() - lo; planeAt = 0; growFrames = growCalls = 0; growWorst = 0; }
static float grow_k(void) { if (growPending) return 0; if (growAt <= 0) return 1; VidPlaneCaps c = video_plane_caps(); double t = now_() - growAt; if (c.cut) t -= 0.12; return ease_io(t / GROW_D); } /* (eased in and out: it leaves the box gently and lands gently) */
static float shrink_k(void) { if (shrinkAt <= 0) return 0; return ease_io((now_() - shrinkAt) / SHRINK_D); }
int player_page_morphing(void) { return P.open && (growPending || growAt > 0 || shrinkAt > 0); } /* a preview growing to the screen or shrinking back into its box */
float player_page_alpha(void) { return growAt > 0 ? 1 - grow_k() : shrinkAt > 0 ? shrink_k() : 1; }
/* switching channels full screen: the picture fades to black, stays black while the next one is found and loads (the
   corner pill says it), and the new picture fades up out of black once it runs; a switch that finds nothing fades
   the old one back */
static int zapOn, zapDown; static float zapF;
void player_zap_begin(void) { if (P.open && P.live && !P.closing) { zapOn = 1; zapDown = 0; } }
void player_zap_end(void) { zapOn = 0; }
static void zap_shade(void) {
  float dt = app_dt(); if (zapOn) { if (!P.live) zapDown = 1; else if (zapDown) zapOn = 0; } /* (the new picture runs: up it comes) */
  if (!P.open || P.closing || P.failed) zapOn = 0;
  zapF += zapOn ? dt / 0.15f : -dt / 0.3f; /* (down quickly at the switch, up as soon as the new picture runs) */ zapF = zapF < 0 ? 0 : zapF > 1 ? 1 : zapF;
  if (zapF > 0.003f) { float k = zapF * zapF * (3 - 2 * zapF); gfx_rect(0, 0, SB_W, SB_H, BLACK_A((int)(255 * k))); } }
static void play_stream(const Stream *s, const char *vid, double startAt) {
  { extern int app_behind(void); if (app_behind()) awayHold = 0; } /* (a play started while behind, a phone's: it plays on) */
  if (!s->url[0]) { if (s->ext[0]) app_open_external(s->ext, s->name); return; } /* a page to open elsewhere: never played here */
  if (!can_play(s)) { toast(T("Couldn’t start the torrent engine"), 2500, NULL); return; }
  int adopt = AD.on && !strcmp(AD.src, s->url) && !P.open; AD.on = 0;
  prepare(vid, startAt, 1); P.quiet = quietNext || adopt; quietNext = 0; P.adopt = adopt; adoptFull = adopt && AD.w >= SB_W - 1 && AD.h >= SB_H - 1; if (adopt) { mo_set(&P.artA, 0); growR[0] = AD.x; growR[1] = AD.y; growR[2] = AD.w; growR[3] = AD.h; growRad = AD.r > 0 ? AD.r : 26; } /* (switching channels: no art, the corner pill alone says it tunes) */
  if (!strcmp(P.type, "other")) { memcpy(trSubs, trNext, sizeof trSubs); trSubsN = trNextN; snprintf(P.trName, sizeof P.trName, "%.*s", (int)strcspn(s->name, "\n"), s->name); }
  else trSubsN = 0;
  trNextN = 0;
  P.st = *s; snprintf(P.url, sizeof P.url, "%s", s->url); snprintf(P.alt, sizeof P.alt, "%s", nextAlt); nextAlt[0] = 0;
  if ((s->hdrs[0] || s->rhdrs[0]) && !strncmp(s->url, "http", 4)) { hproxy_url(s->url, s->hdrs, s->rhdrs, P.url, sizeof P.url); fprintf(stderr, "[player] the stream wants its own headers: through %s\n", P.url); } /* every request for it carries them */
  char info[700]; sb_stream_info(s, info, sizeof info); snprintf(P.sname, sizeof P.sname, "%s", info); snprintf(P.saddon, sizeof P.saddon, "%s", s->addon);
  if (!P.isTrailer && strcmp(P.type, "other")) sb_remember_stream(P.id, P.vid, s); /* the torrent itself, resolved again next time */
  if (!strncmp(P.url, "ytid:", 5)) { /* a YouTube video: found as the trailers are, behind the title art */
    P.resolving = 1; set_status(""); p2pTok++; P.ytTok = trailer_find(NULL, P.url + 5, 0, on_yt, (void *)(uintptr_t)p2pTok); return; }
  if (!strncmp(P.url, "p2p://", 6)) { /* the engine finds the peers and the file while the title art shows how it goes */
    P.resolving = 1; set_status(""); p2pTok++; p2p_resolve(P.url, on_p2p, (void *)(uintptr_t)p2pTok); return; } /* the loading line alone while it is quick; a reason once it is slow */
  start_video();
}
/* "Play the best stream automatically": the title art comes up at once with one quiet line about the wait, Back
   cancels, and the pick plays through the normal path */
static unsigned bestTok, bestSess; static double bestPos = -1; /* a phone's playbest: where to start (0 from the start); -1: as a choice on the TV */
static void on_best_status(const char *t, void *ud) { if (P.open && !P.closing && !P.started && bestTok && (unsigned)(uintptr_t)ud == bestSess) set_status(t); }
static void on_best_done(const Stream *s, const BestRate *r, void *ud) {
  if (!P.open || P.closing || P.started || (unsigned)(uintptr_t)ud != bestSess) return; bestTok = 0;
  if (!s) { fail(T("No stream suits this TV. Pick one from the list")); return; }
  fprintf(stderr, "[best] %s\n", r ? r->sum : ""); if (r) set_status(r->sum);
  Stream c = *s; double at = bestPos; bestPos = -1; play_stream(&c, P.vid, at > 0 ? at : at == 0 ? -1 : 0); /* (a phone's: its moment, or from the start) */
}
/* the logo the page showed (its own address, which can differ from the details' one: a translated logo, a board's):
   the player shows that very picture, already loaded, so it always flies in and is never drawn again */
static char pageLogo[512], pageLogoId[SB_ID];
static void open_title(const char *type, const char *id) {
  if (bestTok) { best_choose_cancel(bestTok); bestTok = 0; }
  if (!P.open) { memset(&P, 0, sizeof P); sx_begin_quick(1); /* the page's logo moves into the player */
    char sk[80]; snprintf(sk, sizeof sk, "logo:%s", id); const char *u = img_src_of(sx_from_tex(sk));
    snprintf(pageLogo, sizeof pageLogo, "%s", u ? u : ""); snprintf(pageLogoId, sizeof pageLogoId, "%s", id); }
  if (P.open && strcmp(P.id, id)) { P.name[0] = P.poster[0] = P.bg[0] = P.logo[0] = P.epTitle[0] = P.year[0] = P.genre[0] = 0; P.season = P.episode = 0; if (P.meta) { cJSON_Delete(P.meta); P.meta = NULL; } P.videos = NULL; } /* another title while it is open (a phone casting): never the last one's art while this one loads */
  snprintf(P.type, sizeof P.type, "%s", type); snprintf(P.id, sizeof P.id, "%s", id);
  cJSON *m = sb_meta_cached(type, id); if (!m && !strcmp(type, "other")) { m = sb_meta_cached("movie", id); if (!m) m = sb_meta_cached("series", id); } /* a trailer: the title's own art */
  if (m) { if (P.meta) cJSON_Delete(P.meta); P.meta = cJSON_Duplicate(m, 1); } else if (strcmp(type, "other")) { if (P.meta) cJSON_Delete(P.meta); P.meta = NULL; P.videos = NULL; ask_meta(); }
  if (P.meta) { snprintf(P.name, sizeof P.name, "%s", js_str(P.meta, "name", "")); snprintf(P.poster, sizeof P.poster, "%s", js_str(P.meta, "poster", "")); Item it = lib_item(); snprintf(it.background, sizeof it.background, "%s", js_str(P.meta, "background", "")); sb_bg_mid(&it, P.bg, sizeof P.bg); }
  else { if (artBg[0]) snprintf(P.bg, sizeof P.bg, "%s", artBg); if (artLogo[0]) snprintf(P.logo, sizeof P.logo, "%s", artLogo); if (artPoster[0]) snprintf(P.poster, sizeof P.poster, "%s", artPoster); if (artName[0] && !P.name[0]) snprintf(P.name, sizeof P.name, "%s", artName); /* the pictures it was handed (a phone's) */
    Item it = lib_item(); if (!P.bg[0]) { it.background[0] = 0; sb_bg_mid(&it, P.bg, sizeof P.bg); } if (!P.logo[0]) sb_logo_of(&it, P.logo, sizeof P.logo); } /* else its pictures by its id while its details are on their way */
  artName[0] = artPoster[0] = artBg[0] = artLogo[0] = 0;
  { char pu[512], lu[512]; Item it = lib_item(); sb_poster_of(&it, pu, sizeof pu); sb_logo_of(&it, lu, sizeof lu); accent_scope(id, pu, lu); }
}
/* the title's art at once, before there is anything to play (a trailer being found): status lines on it, a failure on it,
   Back closes it; the play that follows starts behind the same art */
void player_prepare(const char *type, const char *id, const char *vid, const char *trailerName) {
  open_title(type, id); prepare(vid, 0, 0); snprintf(P.trName, sizeof P.trName, "%s", trailerName ? trailerName : "");
}
void player_status(const char *text) { if (P.open && !P.closing) set_status(text); }
void player_fail(const char *text) { if (P.open && !P.closing) fail(text); }
void player_close(void) { close_player(0, 0); }
int  player_waiting(void) { return P.open && !P.closing && !P.started && !P.failed; }
/* after a failure: the best of the other streams, behind the same art (the failed one is left out) */
static void try_next(void) {
  fprintf(stderr, "[player] trying the next stream\n"); prepare(P.vid, 0, 0);
  set_status(T("Choosing the best stream")); bestSess++;
  bestTok = best_choose(P.type, P.vid, best_dur(P.meta, P.type), on_best_status, on_best_done, (void *)(uintptr_t)bestSess);
}
void player_cast_best(const char *type, const char *id, const char *vid, double pos) { player_choose_best(type, id, vid); bestPos = pos > 0 ? pos : 0; }
void player_choose_best(const char *type, const char *id, const char *vid) {
  bestPos = -1; open_title(type, id); prepare(vid, 0, 0);
  set_status(T("Choosing the best stream")); bestSess++;
  bestTok = best_choose(type, P.vid, best_dur(P.meta, type), on_best_status, on_best_done, (void *)(uintptr_t)bestSess);
}
/* a channel's name and pictures from its card, for one no addon describes (its details, when they come, take over) */
void player_reapply_window(void) { if (P.open) apply_aspect(); } /* (the hub gives the picture back) */
void player_title_hint(const Item *it) {
  if (!P.open || strcmp(P.id, it->id)) return;
  if (!P.name[0]) snprintf(P.name, sizeof P.name, "%s", it->name); if (!P.poster[0]) snprintf(P.poster, sizeof P.poster, "%s", it->poster);
  if (!P.bg[0]) snprintf(P.bg, sizeof P.bg, "%s", it->background[0] ? it->background : it->poster); if (!P.logo[0]) snprintf(P.logo, sizeof P.logo, "%s", it->logo);
}
void player_play(const Stream *s, const char *type, const char *id, const char *vid, int fromStart) { open_title(type, id); play_stream(s, vid, fromStart ? -1 : 0); }
/* ---- cast (core/castlink.c): a phone plays a stream here and drives it ---- */
void player_next_subs(const char *const *urls, const char *const *langs, int n) {
  memset(castSubs, 0, sizeof castSubs); if (n > 16) n = 16;
  for (int i = 0; i < n; i++) { SubEntry *e = &castSubs[i]; snprintf(e->url, sizeof e->url, "%s", urls[i]); snprintf(e->lang, sizeof e->lang, "%s", langs[i] && *langs[i] ? langs[i] : "und"); snprintf(e->name, sizeof e->name, "%s", e->lang); e->n = i + 1; e->from = SUB_STREAM; }
  castSubsN = n > 0 ? n : -1; /* (none from the phone, a best-stream play among them: the built-in OpenSubtitles list as on the TV; 0 kept it empty, "None found") */
}
void player_next_art(const char *name, const char *poster, const char *bg, const char *logo) {
  snprintf(artName, sizeof artName, "%s", name ? name : ""); snprintf(artPoster, sizeof artPoster, "%s", poster ? poster : ""); snprintf(artBg, sizeof artBg, "%s", bg ? bg : ""); snprintf(artLogo, sizeof artLogo, "%s", logo ? logo : ""); }
void player_play_at(const Stream *s, const char *type, const char *id, const char *vid, double pos) { open_title(type, id); play_stream(s, vid, pos > 0 ? pos : 0); }
void player_cast(int cmd, double pos) {
  if (!P.open || P.closing) return;
  if (P.pw.on) { if (cmd == 2 || cmd == 5) pw_end("Play now, from the phone"); else if (cmd == 4) close_player(0, 0); return; } /* (waiting for the seek previews: play or Play now starts it, as OK; a pause changes nothing, it is held already) */
  if (cmd == 1 && !paused()) { video_pause(); save_progress(1); } else if (cmd == 2 && paused()) play_on();
  else if (cmd == 3 && P.live) video_seek(pos < 0 ? 0 : pos); else if (cmd == 4) close_player(0, 0);
}
int player_cast_previews(int *pct, int *target, int *eta) { *pct = *target = *eta = 0; if (!P.open || P.closing || P.isTrailer || !P.pw.on) return 0;
  int p = sp_progress(); *pct = p < 0 ? 0 : p > 100 ? 100 : p; *target = P.pw.target; *eta = P.pw.etaShown && P.pw.eta > 0 && P.pw.eta <= 900 ? (int)ceil(P.pw.eta) : 0; return 1; } /* (the same numbers as the TV's line: made of 100, the share it waits for, the time left once known) */
/* Sync, for a phone's remote: running or done, in the tray's words (nothing while it checks quietly by itself) */
int player_cast_sync(char *st, int cap) { st[0] = 0; if (!P.open || P.closing || (!SY.phase && !SY.st[0])) return -1; if (!SY.quiet) snprintf(st, (size_t)cap, "%s", SY.st); return SY.phase ? 1 : 0; }
/* the subtitles and audio as the trays list them, for a phone: ids it hands back as they are ("off", "emb:<track>",
   "ext1:<n>" the stream's and OpenSubtitles', "ext2:<n>" the addons', audio "<track>" or "conv:<n>" a converted one) */
char *player_cast_tracks(void) {
  if (!P.open || P.closing) return NULL;
  cJSON *j = cJSON_CreateObject(), *sl = cJSON_AddArrayToObject(j, "subs"), *al = cJSON_AddArrayToObject(j, "audio"); char id[48], lb[200];
  cJSON_AddStringToObject(j, "t", "tracks");
  for (int i = 0; i < P.nEmb; i++) { cJSON *o = cJSON_CreateObject(); snprintf(id, sizeof id, "emb:%d", P.emb[i].idx); emb_label(i, lb, sizeof lb);
    cJSON_AddStringToObject(o, "id", id); cJSON_AddStringToObject(o, "label", lb); cJSON_AddStringToObject(o, "lang", P.emb[i].lang); cJSON_AddStringToObject(o, "from", T("Embedded")); cJSON_AddItemToArray(sl, o); }
  char cur[48] = "off";
  if (!strncmp(P.curSub, "emb:", 4)) snprintf(cur, sizeof cur, "%s", P.curSub);
  for (int li = 1; li <= 2; li++) { const SubEntry *l = li == 1 ? P.ext : P.adx; int n = li == 1 ? P.nExt : P.nAdx;
    for (int i = 0; i < n; i++) { const SubEntry *e = &l[i]; cJSON *o = cJSON_CreateObject(); snprintf(id, sizeof id, "ext%d:%d", li, i); const char *ln = lang_name(e->lang);
      if (*ln) snprintf(lb, sizeof lb, "%s%s%s", ln, e->label[0] ? " · " : "", e->label); else snprintf(lb, sizeof lb, "%s", T(e->name));
      cJSON_AddStringToObject(o, "id", id); cJSON_AddStringToObject(o, "label", lb); cJSON_AddStringToObject(o, "lang", e->lang); cJSON_AddStringToObject(o, "from", e->addon[0] ? e->addon : "OpenSubtitles"); cJSON_AddItemToArray(sl, o);
      if (P.pick.list == li && !strncmp(P.curSub, "ext:", 4) && !strcmp(P.curSub + 4, e->url)) snprintf(cur, sizeof cur, "%s", id); } }
  cJSON_AddStringToObject(j, "sub", cur);
  if (P.dts.conv) { MkvAudio ma[16]; int m = mkv ? mkv_audio_tracks(mkv, ma, 16) : 0;
    for (int i = 0; i < m; i++) { cJSON *o = cJSON_CreateObject(); snprintf(id, sizeof id, "conv:%d", i); const char *ln = lang_name(ma[i].lang); snprintf(lb, sizeof lb, "%s", *ln ? ln : ma[i].lang[0] ? ma[i].lang : T("Audio"));
      cJSON_AddStringToObject(o, "id", id); cJSON_AddStringToObject(o, "label", lb); cJSON_AddStringToObject(o, "lang", ma[i].lang); cJSON_AddItemToArray(al, o); }
    snprintf(id, sizeof id, "conv:%d", P.dts.convOrd); cJSON_AddStringToObject(j, "aud", id); }
  else { VidTrack ts[16]; int n = aud_list(ts, 16), sel = P.curAud >= 0 ? P.curAud : video_audio_current();
    for (int i = 0; i < n; i++) { cJSON *o = cJSON_CreateObject(); snprintf(id, sizeof id, "%d", ts[i].idx); const char *ln = lang_name(ts[i].lang);
      if (*ln) snprintf(lb, sizeof lb, "%s", ln); else if (ts[i].lang[0]) snprintf(lb, sizeof lb, "%s", ts[i].lang); else snprintf(lb, sizeof lb, "%s %d", T("Audio"), i + 1);
      if (ts[i].name[0] && strcmp(ts[i].name, lb)) { size_t L = strlen(lb); snprintf(lb + L, sizeof lb - L, " · %s", ts[i].name); }
      cJSON_AddStringToObject(o, "id", id); cJSON_AddStringToObject(o, "label", lb); cJSON_AddStringToObject(o, "lang", ts[i].lang); cJSON_AddItemToArray(al, o); }
    snprintf(id, sizeof id, "%d", sel); cJSON_AddStringToObject(j, "aud", id); }
  cJSON_AddNumberToObject(j, "delay", (int)lround(P.subDelay * 1000));
  cJSON_AddNumberToObject(j, "canSync", !strncmp(P.curSub, "ext:", 4) && !P.isTrailer);
  { int sc = P.isTrailer ? 0 : player_sub_scope(); cJSON_AddNumberToObject(j, "scope", sc); if (sc == 2) cJSON_AddNumberToObject(j, "pics", sub_pictures()); cJSON_AddNumberToObject(j, "subAr", sub_is_arabic()); } /* (a phone's remote: what the subtitles shown can take (0 all, 1 styled, 2 nothing; pictures or drawn by the TV), whether they are Arabic (its Arabic font row)) */ /* (Sync: possible on an addon's subtitles; how it goes comes with the state) */
  char *s = cJSON_PrintUnformatted(j); cJSON_Delete(j); return s;
}
/* a phone's pick, as the trays make it: kind "sub" (id or "off"), "audio" (id), "subdelay" (ms, absolute) */
void player_cast_track(const char *kind, const char *id, int ms) {
  if (!P.open || P.closing) return;
  if (!strcmp(kind, "subsync")) { if (!SY.phase || SY.quiet) sync_run(); return; } /* (a phone's Sync: as the tray's; pressed while it runs, nothing) */
  if (!strcmp(kind, "subdelay")) { double v = ms / 1000.0; if (v > 120) v = 120; if (v < -120) v = -120; P.subDelay = fabs(v) < 1e-6 ? 0 : v; P.dlyAt = now_(); refresh_tray(4); return; }
  if (!strcmp(kind, "sub")) {
    if (!strcmp(id, "off")) subs_off();
    else if (!strncmp(id, "emb:", 4)) set_emb_sub(atoi(id + 4));
    else if (!strncmp(id, "ext1:", 5) || !strncmp(id, "ext2:", 5)) { int li = id[3] - '0', k = atoi(id + 5); const SubEntry *l = li == 1 ? P.ext : P.adx; int n = li == 1 ? P.nExt : P.nAdx; if (k >= 0 && k < n) set_ext_sub(li, &l[k]); }
    show_pref_sub(); refresh_tray(4); return; }
  if (!strcmp(kind, "audio")) {
    if (!strncmp(id, "conv:", 5)) { int ord = atoi(id + 5);
      { MkvAudio ma[16]; int m = mkv ? mkv_audio_tracks(mkv, ma, 16) : 0; if (ord >= 0 && ord < m) show_pref_aud(ma[ord].lang, NULL); } /* (as the tray's pick) */
      if (P.dts.conv == 2 && ord != P.dts.convOrd && ord >= 0) { P.dts.convOrd = ord; P.dts.conv = 1; P.dts.convAt = video_time(); P.dts.convT0 = now_();
        int fl = conv_flags(ord); P.dts.copyA = !!(fl & HC_COPYAUDIO); video_unload(); P.live = 0; P.liveT0 = 0; hls_conv_start(P.url, ord, fl); fprintf(stderr, "[dts] audio track %d instead (phone)\n", ord); } }
    else { int ai = atoi(id); P.autoAudDone = 1; set_audio(ai); VidTrack ts[16]; int n = video_audio_tracks(ts, 16); for (int i = 0; i < n; i++) if (ts[i].idx == ai) show_pref_aud(ts[i].lang, ts[i].name); }
    refresh_tray(3); }
}
/* a torrent's peers and speed as the player's corner shows them ("" when none), for a phone's remote */
const char *player_p2p_line(void) { return P.open && !P.closing && !P.isTrailer && P.isP2p ? P.p2pLine : ""; }
/* the info line as the player shows it (resolution, its picture and sound, the size | the sound playing | the subtitles on), for a phone's remote */
void player_cast_chips(void *arr) { cJSON *a = arr; if (!a || !P.open || P.closing || P.isTrailer) return;
  for (int i = 0; i < P.nchip && i < 8; i++) { cJSON *o = cJSON_CreateObject(); if (P.chip[i].sep) cJSON_AddNumberToObject(o, "sep", 1); else { cJSON_AddStringToObject(o, "t", P.chip[i].t); if (P.chip[i].icon) cJSON_AddStringToObject(o, "i", P.chip[i].icon); if (P.chip[i].bold) cJSON_AddNumberToObject(o, "b", 1); } cJSON_AddItemToArray(a, o); } }
/* the stream in a few words, for a phone's remote ("1080p · HDR10 · DDP 5.1"): the decoder's HDR and the playing track's sound once known, the stream's name before */
void player_cast_info(char *out, int cap) { out[0] = 0; if (!P.open || P.closing || P.isTrailer || cap < 2) return; char hdr[32], codec[48], chs[8], au[64]; int n = 0;
  play_av(hdr, sizeof hdr, codec, sizeof codec, chs, sizeof chs); snprintf(au, sizeof au, "%s%s%s%s", !strncmp(codec, "EAC3", 4) ? "DDP" : codec, !strncmp(codec, "EAC3", 4) ? codec + 4 : "", codec[0] && chs[0] ? " " : "", codec[0] ? chs : ""); /* (the phone says DDP) */
  const char *parts[3] = { P.res && *P.res ? P.res : "", hdr, au }; for (int i = 0; i < 3; i++) if (parts[i][0] && n < cap - 1) n += snprintf(out + n, (size_t)(cap - n), "%s%s", n ? " \xc2\xb7 " : "", parts[i]); }
/* what plays, for a phone's remote: its type, id and episode ("" when nothing) */
void player_cast_ident(char *type, int tc, char *id, int ic, char *vid, int vc) { type[0] = id[0] = vid[0] = 0; if (!P.open || P.closing || P.isTrailer) return; snprintf(type, (size_t)tc, "%s", P.type); snprintf(id, (size_t)ic, "%s", P.id); snprintf(vid, (size_t)vc, "%s", P.vid); }
int player_cast_state(double *pos, double *du, char *title, int cap) {
  *pos = *du = 0; if (title && cap) title[0] = 0;
  if (!P.open || P.closing) return 0;
  if (title && cap) snprintf(title, (size_t)cap, "%s", P.name);
  if (P.failed) return 5;
  if (bestTok && !P.started) return 6; /* (CAST_CHOOSING: finding the best stream) */
  if (P.pw.on) return 1; /* (held on its first frame for the seek previews: loading, never "paused", to a phone) */
  VidState vs = video_state(); if (!P.live) return vs == VID_ERROR ? 5 : 1;
  *pos = video_time(); *du = dur();
  return vs == VID_ENDED ? 4 : vs == VID_ERROR ? 5 : paused() ? 3 : vs == VID_LOADING || vs == VID_BUFFERING ? 1 : 2;
}
static void finish_close(void) { if (hub_on() && hub_player_mode()) hub_close(); /* (one from Home stays: it is the page) */ { extern void detail_refresh_watched(void); detail_refresh_watched(); } /* the title page under it: its ticks as they are now */ sync_stop(0); gfx_target_free(&handSnap); mo_set(&handA, 0); if (!keepVideo) video_unload(); keepVideo = 0; /* (back into its preview: it plays on there) */ asssubs_close(); if (mkv) { prevspike_stop(); sp_close(); mkv_close(mkv); mkv = NULL; } p2p_player_closed(); p2pTok++; metaTok++; P.open = 0; P.closing = 0; if (P.meta) cJSON_Delete(P.meta); P.meta = NULL; subs_free(P.cues, P.ncues); free(P.ext); free(P.adx); srcs_free(); cols_clear(); memset(&P, 0, sizeof P); sub_reset(); free(extAss); extAss = NULL; app_refresh_boards(); }
static int closeNext, closeSim, closeArt; static double closeAt, closeT0; /* closeArt: closed on the loading screen (its line and words fade as the logo flies back) */
static void close_player(int goNext, int goSim) {
  if (!P.open || P.closing) return; if (bestTok) { best_choose_cancel(bestTok); bestTok = 0; } P.closing = 1; ps_hold(0); sp_verify_reset(); np_stop(); NP.vid[0] = 0; /* (the next episode's pictures made so far: kept) */ /* (a lighter copy not taken yet: it reads on freely, nothing plays) */ closeArt = !P.live && P.artA.v > 0.999f; closeT0 = now_(); sx_begin_quick(0); /* the player's logo moves back to the page */ closeNext = goNext; closeSim = goSim; P.ssOn = 0; P.nx.tok = 0; P.sk.tok = 0;
  scrobble_player_end(video_time(), dur(), 0); save_progress(1); video_pause(); /* (Test playback: nothing told, nothing kept) */
  /* the title's own picture fades back over the paused frame, the video goes, then the cover fades onto the app */
  closeAt = now_() + (closeArt ? 0.01 : 0.26); /* the cover fades back over the paused frame (see css()); from the loading screen the art fades at once, under the logo on its way back */
}
static void close_step(void) {
  if (!P.closing || now_() < closeAt) return;
  if (closeAt > 0) { closeAt = 0; video_unload(); hls_conv_stop(); P.live = 0; P.exitFade = 1;
    if (closeNext && closeVid[0]) { DetailParams p; memset(&p, 0, sizeof p); p.it = lib_item(); snprintf(p.it.videoId, sizeof p.it.videoId, "%s", closeVid); p.openStreams = 1; closeVid[0] = 0; app_go(SCR_DETAIL, &p, 0); }
    else if (closeSim) { DetailParams p; memset(&p, 0, sizeof p); p.it = lib_item(); app_go(SCR_SIM, &p, 0); }
    else if (closeColl) { closeColl = 0; DetailParams p; memset(&p, 0, sizeof p); p.it = closeCollIt; p.openStreams = 1; app_go(SCR_DETAIL, &p, 0); }
    else accent_scope(SCR == SCR_DETAIL ? P.id : NULL, P.poster, P.logo); }
  if (P.exitFade && P.in.v <= 0.001f && !P.in.on) finish_close();
}

/* ---- per frame ---- */
/* the info chips, grouped as the page groups them: [resolution] [HDR · audio format] [size] | [audio language + channels]
   [subtitles]. Kept per place: only a new chip comes in, each 0.05 s after the one before it (the subtitles' last); a
   chip whose words change (another subtitle, another audio track) crossfades them in place, its width gliding */
static float chip_delay(int i) { return i >= 1 ? 0.05f * (i < 6 ? i : 6) : 0; }
static void chips_in(void) { double n = now_(); for (int i = 0; i < P.nchip; i++) { P.chip[i].at = n; P.chip[i].delay = chip_delay(i); P.chip[i].xAt = 0; } }
/* the picture and sound playing, for the info line and a phone's remote: the HDR the TV decodes once it says (the
   stream's name before: "Dolby Vision · HDR10" as that names it), the sound of the track playing by its codec and
   channels (a track's name may be its language alone), else the name's; codec in audio_of's words ("EAC3", "DTS-HD MA"), chs "5.1" */
static void play_av(char *hdr, size_t hc, char *codec, size_t cc, char *chs, size_t sc) {
  hdr[0] = codec[0] = chs[0] = 0;
  { char vh[32]; if (video_hdr(vh, sizeof vh) && vh[0] && strcasecmp(vh, "none")) snprintf(hdr, hc, "%s", !strncasecmp(vh, "dolby", 5) || !strncmp(vh, "DV", 2) ? "Dolby Vision" : vh); }
  if (!hdr[0]) { char h2[32]; hdr_of(P.sname, h2, sizeof h2); snprintf(hdr, hc, "%s%s", !strncmp(h2, "DV", 2) ? "Dolby Vision" : h2, !strncmp(h2, "DV", 2) ? h2 + 2 : ""); }
  { VidTrack ts[16]; int nt = aud_list(ts, 16), sel = P.curAud >= 0 ? P.curAud : video_audio_current(); /* (aud_list: the codec and channels from the file where the TV leaves them out) */
    for (int i = 0; i < nt; i++) if (ts[i].idx == sel) { const char *c = ts[i].codec; if (!strncmp(c, "A_", 2)) c += 2; /* (the file's names: A_EAC3, A_DTS...) */
      const char *nm = !strcasecmp(c, "eac3") || !strcasecmp(c, "ec-3") || !strcasecmp(c, "e-ac-3") ? "EAC3" : !strcasecmp(c, "ac3") || !strcasecmp(c, "ac-3") ? "AC3" : !strcasecmp(c, "dts-hd") || !strncasecmp(c, "dts/ma", 6) ? "DTS-HD MA" : !strcasecmp(c, "dts-x") ? "DTS:X" : !strncasecmp(c, "dts", 3) ? "DTS" : !strcasecmp(c, "truehd") ? "TrueHD" : !strncasecmp(c, "aac", 3) || !strncasecmp(c, "mp4a", 4) ? "AAC" : !strcasecmp(c, "opus") ? "Opus" : !strcasecmp(c, "flac") ? "FLAC" : "";
      if (*nm) { int ch = ts[i].channels; snprintf(codec, cc, "%s%s", nm, strcasestr(ts[i].name, "atmos") ? " Atmos" : ""); snprintf(chs, sc, "%s", ch >= 8 ? "7.1" : ch >= 6 ? "5.1" : ch == 2 ? "2.0" : ""); } break; } }
  if (!codec[0]) { char au[48]; audio_of(P.sname, au, sizeof au); snprintf(codec, cc, "%s", au); char *sp = strrchr(codec, ' '); if (sp && sp[1] >= '0' && sp[1] <= '9' && sp[2] == '.') { snprintf(chs, sc, "%s", sp + 1); *sp = 0; } }
}
static void info_update(void) {
  if (P.isTrailer) { P.nchip = 0; return; }
  char key[256]; static char hdr[32], codec[48], chs[8]; static double avAt; double tn = now_(); /* (the decoder tells its HDR and track after the first picture: asked twice a second, not every frame) */
  if (!P.infoKey[0] || tn - avAt > 0.5 || tn < avAt) { avAt = tn; play_av(hdr, sizeof hdr, codec, sizeof codec, chs, sizeof chs); }
  snprintf(key, sizeof key, "%s|%d|%d%s|%s|%s|%d|%s|%s%s", P.res ? P.res : "", P.curAud, P.pick.list, P.pick.addon, P.curSub, P.sizeTxt, P.nEmb, hdr, codec, chs); if (!strcmp(key, P.infoKey)) return;
  int first = !P.infoKey[0]; snprintf(P.infoKey, sizeof P.infoKey, "%s", key);
  char fmt[96]; snprintf(fmt, sizeof fmt, "%s%s%s", hdr, hdr[0] && codec[0] ? " · " : "", codec); /* the page's "  ·  " collapses to one space each side */
  char aud[96] = ""; { VidTrack ts[16]; int nt = aud_list(ts, 16); int sel = P.curAud >= 0 ? P.curAud : video_audio_current(); for (int i = 0; i < nt; i++) if (ts[i].idx == sel) { const char *ln = lang_name(ts[i].lang); snprintf(aud, sizeof aud, "%s", *ln ? ln : ts[i].name); } }
  if (chs[0]) { size_t l = strlen(aud); snprintf(aud + l, sizeof aud - l, "%s%s", l ? " " : "", chs); }
  char sub[140] = "";
  if (!strncmp(P.curSub, "emb:", 4)) { int nn = atoi(P.curSub + 4); for (int i = 0; i < P.nEmb; i++) if (P.emb[i].idx == nn) { char lb[120]; emb_label(i, lb, sizeof lb); snprintf(sub, sizeof sub, "%s · %s", lb, T("In the file")); } }
  else if (!strncmp(P.curSub, "ext:", 4)) { int i = sub_row(1, P.ext, P.nExt); if (i >= 0) { const char *ln = lang_name(P.ext[i].lang); snprintf(sub, sizeof sub, "%s · %s", *ln ? ln : T(P.ext[i].name), T("External")); }
    if ((i = sub_row(2, P.adx, P.nAdx)) >= 0) { const char *ln = lang_name(P.adx[i].lang); snprintf(sub, sizeof sub, "%s · %s", *ln ? ln : T(P.adx[i].name), P.adx[i].addon); } }
  struct { const char *t, *icon; int bold, sep; } nw[8]; int n = 0;
  if (P.res && *P.res) { nw[n].t = P.res; nw[n].icon = NULL; nw[n].bold = 1; nw[n++].sep = 0; }
  if (fmt[0]) { nw[n].t = fmt; nw[n].icon = NULL; nw[n].bold = 0; nw[n++].sep = 0; }
  if (P.sizeTxt[0]) { nw[n].t = P.sizeTxt; nw[n].icon = NULL; nw[n].bold = 0; nw[n++].sep = 0; }
  if (n && (aud[0] || sub[0])) { nw[n].t = "|"; nw[n].icon = NULL; nw[n].bold = 0; nw[n++].sep = 1; }
  if (aud[0]) { nw[n].t = aud; nw[n].icon = "p_spk"; nw[n].bold = 0; nw[n++].sep = 0; }
  if (sub[0]) { nw[n].t = sub; nw[n].icon = "p_cc"; nw[n].bold = 0; nw[n++].sep = 0; }
  double now = now_();
  for (int i = 0; i < n; i++) {
    int kept = i < P.nchip && P.chip[i].sep == nw[i].sep && P.chip[i].icon == nw[i].icon && !strcmp(P.chip[i].t, nw[i].t);
    int same = i < P.nchip && P.chip[i].sep == nw[i].sep && P.chip[i].icon == nw[i].icon && !nw[i].sep && P.chip[i].at && now - P.chip[i].at > P.chip[i].delay + .42; /* there and settled: only its words change */
    if (!kept && same) { int b = P.chip[i].bold; snprintf(P.chip[i].old, sizeof P.chip[i].old, "%s", P.chip[i].t); P.chip[i].oldW = text_width(P.chip[i].old, 24, b ? 800 : 600, b ? 0.5f : 0); P.chip[i].xAt = now; snprintf(P.chip[i].t, sizeof P.chip[i].t, "%s", nw[i].t); }
    else if (!kept) { snprintf(P.chip[i].t, sizeof P.chip[i].t, "%s", nw[i].t); P.chip[i].icon = nw[i].icon; P.chip[i].bold = nw[i].bold; P.chip[i].sep = nw[i].sep; P.chip[i].at = now; P.chip[i].delay = chip_delay(i); P.chip[i].xAt = 0; }
  }
  P.nchip = n; if (first) chips_in();
}
/* names and stories of the episodes in the app's language (TMDB), once they are known */
static void loc_apply(void) {
  if (P.locDone || P.isTrailer || strcmp(P.type, "series")) return;
  int st = tmdb_state(P.type, P.id, 0); if (st == TMDB_PENDING) return; P.locDone = 1; if (st != TMDB_READY) return;
  const TmdbLoc *L = tmdb_loc(P.type, P.id, 0); if (!L) return; const char *nm, *ov;
  if (P.season > 0 && P.episode > 0 && tmdb_episode(L, P.season, P.episode, &nm, &ov) && *nm) snprintf(P.epTitle, sizeof P.epTitle, "%s", nm);
  if (P.next.has) { int s = 0, e = 0; const char *c = strrchr(P.next.vid, ':'); if (c && c > P.next.vid) { const char *q = c - 1; while (q > P.next.vid && *q != ':') q--; s = atoi(q + 1); e = atoi(c + 1); }
    if (tmdb_episode(L, s, e, &nm, &ov)) { if (*nm) snprintf(P.next.title, sizeof P.next.title, "%s", nm); if (*ov) snprintf(P.next.ov, sizeof P.next.ov, "%s", ov); } }
}
/* a picture that stopped while the TV still says "playing": the time runs on between the TV's reports, so a frozen
   playhead shows as the time pulled back by each report (or, where it is not run on, as a time that does not move) */
static int stalled(VidState vs) {
  static double lastT = -1, backAt, sameSince; double t = video_time(), n = now_();
  if (vs != VID_PLAYING || P.seekT >= 0) { lastT = -1; backAt = 0; sameSince = 0; return 0; }
  if (lastT >= 0) { if (t < lastT - 0.25 && t > lastT - 5) backAt = n; /* (a bigger jump is a seek) */
    if (fabs(t - lastT) < 0.001) { if (!sameSince) sameSince = n; } else sameSince = 0; }
  lastT = t;
  return (backAt && n - backAt < 0.9) || (sameSince && n - sameSince > 0.5);
}
/* the viewer notice, asked as the picture comes (a trailer has none): IMDb's Parents Guide votes read as an age
   (any severe 16+, three severe 18+, moderate 13+, mild only 7+) and the reasons with how much; when the guide has
   nothing (or cannot be reached), the official rating's age alone (TMDB). Nothing usable within 5 s: no notice. */
static void pg_decide(void) {
  if (P.pg.at || !P.pg.asked || now_() - P.pg.askAt > 5) return; /* (shown once; never late) */
  if (P.pg.guideIn && P.pg.g.n) { int sev = 0, mod = 0; for (int i = 0; i < P.pg.g.n; i++) { sev += P.pg.g.lvl[i] == PG_SEVERE; mod += P.pg.g.lvl[i] == PG_MODERATE; }
    P.pg.age = sev >= 3 ? 18 : sev ? 16 : mod ? 13 : 7; P.pg.at = now_(); return; }
  if (P.pg.guideIn && P.pg.certIn && P.pg.certAge >= 0) { P.pg.age = P.pg.certAge; P.pg.at = now_(); } /* (the age alone, no reasons) */
}
static void on_pg(const ParGuide *g, void *ud) { if (!P.open || P.closing || (unsigned)(uintptr_t)ud != P.pg.sess) return; if (g) P.pg.g = *g; P.pg.guideIn = 1; fprintf(stderr, "[notice] guide: %d reasons\n", g ? g->n : -1); pg_decide(); }
static void on_age(const Rating *r, void *ud) { if (!P.open || P.closing || (unsigned)(uintptr_t)ud != P.pg.sess) return; P.pg.certAge = rating_age(r); P.pg.certIn = 1; snprintf(P.pg.cert, sizeof P.pg.cert, "%s", r ? r->rating : ""); P.pg.desc[0] = 0; for (int i = 0; r && i < r->ndesc && i < 3; i++) { size_t l = strlen(P.pg.desc); snprintf(P.pg.desc + l, sizeof P.pg.desc - l, "%s%s", l ? ", " : "", r->desc[i]); } /* (the rating's own reasons, when it gives them) */ fprintf(stderr, "[notice] rating \"%s\" -> age %d\n", r ? r->rating : "", P.pg.certAge); pg_decide(); }
static void pg_ask(void) { if (P.pg.asked || P.isTrailer || strncmp(P.id, "tt", 2)) return; P.pg.asked = 1; P.pg.askAt = now_();
  parental_get(P.id, on_pg, (void *)(uintptr_t)P.pg.sess); rating_get(P.type, P.id, on_age, (void *)(uintptr_t)P.pg.sess); }
/* the page's classes, as targets: each motion goes there with the duration, curve and delay the page gives that state */
static void css(void) {
  int tr = P.mode == 2 && P.tray.kind > 0, ss = P.ssOn, osd = P.osd && !ss, pz = P.pausedShown, skm = P.skm && !osd;
  if (osd && !tr) { mo_to(&P.ttlA, 1, .42f, C_OUT, 0); mo_to(&P.ttlX, 0, .42f, C_OUT, 0); mo_to(&P.ttlY, 0, .42f, C_OUT, 0); }
  else if (osd) { mo_to(&P.ttlA, 0, .42f, C_OUT, 0); mo_to(&P.ttlX, 0, .42f, C_OUT, 0); mo_to(&P.ttlY, 10, .42f, C_OUT, 0); } /* a tray takes its place: it sinks a little */
  else { mo_to(&P.ttlA, 0, .2f, C_EASE, 0); mo_to(&P.ttlX, -30, .2f, C_EASE, 0); mo_to(&P.ttlY, 0, .2f, C_EASE, 0); }
  if (osd) { mo_to(&P.barA, tr ? 0 : 1, .36f, C_EASE, .06f); mo_to(&P.barY, tr ? 10 : 0, .36f, C_OUT, .06f); mo_to(&P.btnA, 1, .36f, C_EASE, .12f); mo_to(&P.btnY, 0, .36f, C_OUT, .12f); }
  else { float bd = stripQuick ? .12f : seekK.v > 0.02f ? .45f : .2f; mo_to(&P.barA, skm ? 1 : 0, bd, C_EASE, 0); mo_to(&P.barY, skm ? 0 : 18, bd, C_EASE, 0); mo_to(&P.btnA, 0, .2f, C_EASE, 0); mo_to(&P.btnY, 24, .2f, C_EASE, 0); }
  if (ss) mo_to(&P.topR, 0, 1, C_EASE, 0); else { mo_to(&P.topR, osd ? 1 : 0, .3f, C_EASE, 0); mo_to(&P.topY, osd ? 0 : -12, .3f, C_EASE, 0); }
  if (ss) { mo_to(&P.dim, 0, 1, C_EASE, 0); mo_to(&P.dimT, 0, 1, C_EASE, 0); }
  else { mo_to(&P.dim, osd || pz ? 1 : skm ? .6f : 0, .3f, C_EASE, 0); mo_to(&P.dimT, osd || pz ? 1 : 0, .3f, C_EASE, 0); }
  mo_to(&P.vig, pz ? 1 : 0, .9f, C_EASE, 0); mo_to(&P.pzA, pz ? 1 : 0, .3f, C_EASE, 0);
  if (!P.closing && !ss && (!P.live || P.artA.v > 0.02f)) { mo_set(&P.dim, 0); mo_set(&P.dimT, 0); mo_set(&P.vig, 0); } /* the title's art fades straight into the picture: the shades come after it */
  if (P.closing) mo_to(&P.artA, 1, .2f, C_EASE, 0); else if (ss) mo_to(&P.artA, 1, 1.4f, C_EASE, 0); else mo_to(&P.artA, (P.live && !P.pw.on) || (P.quiet && !P.failed && !P.pw.on) ? 0 : 1, .45f, C_EASE, 0);
  if (P.exitFade) mo_to(&P.in, 0, .35f, C_EASE, 0); else mo_to(&P.in, 1, .22f, C_EASE, 0);
  mo_to(&P.lnA, P.live && !P.closing && !P.pw.on ? 0 : 1, .16f, C_EASE, 0);
  mo_to(&P.pwA, P.pw.on && P.pw.shown && !P.closing && !P.failed ? 1 : 0, P.pw.on ? .35f : .2f, C_EASE, P.pw.on ? .25f : 0); /* previews first: the bar's fill and Play now */
  if (P.pw.on && P.pw.shown) { double pc = sp_progress_f(); float f = pc <= 0 ? 0 : pc >= P.pw.target ? 1 : (float)(pc / P.pw.target); if (fabsf(f - P.pwF.to) > .004f) mo_to(&P.pwF, f, .5f, C_EASE, 0); } /* the line runs until the picture runs, then goes with the art */
  /* the spinner: loading, a seek on its way, or a picture that stopped moving; it comes after 0.3 s (a quick seek shows
     none) and goes as soon as the time moves again */
  VidState vs = video_state(); int wait = P.live && !paused() && (vs == VID_BUFFERING || vs == VID_LOADING || video_settling() || stalled(vs));
  if (wait) mo_to(&P.bufA, 1, .2f, C_EASE, .3f); else mo_to(&P.bufA, 0, .2f, C_EASE, 0);
  mo_to(&P.skA, P.skOn ? 1 : 0, stripQuick ? .12f : seekK.v > 0.02f ? .45f : .16f, C_EASE, 0); mo_to(&P.skS, P.skOn ? 1 : 0, .2f, C_SEEK, 0);
  mo_to(&P.knA, (P.mode == 1 && P.row == 1) || P.skOn ? 1 : 0, .15f, C_EASE, 0);

  mo_to(&P.prA, P.prompt.k ? 1 : 0, .3f, C_EASE, 0); mo_to(&P.prY, P.prompt.k ? 0 : 20, .3f, C_OUT, 0); mo_to(&P.prB, P.osd ? 440 : 104, .3f, C_EASE, 0);
  { float rest = SB_H * pos_pct() / 100.0f; mo_to(&P.subB, tr ? SB_H * 0.62f : P.osd ? fmaxf(420, rest) : P.skm ? fmaxf(200, rest) : rest, P.mode == 5 ? .16f : .3f, C_EASE, 0); } /* (the chosen height, never under the controls) */
  mo_to(&P.hvA, P.hvOn ? 1 : 0, .15f, C_EASE, 0); mo_to(&P.ccS, strcmp(P.curSub, "off") ? 0 : 1, .26f, C_OUT, 0);
  mo_to(&P.p2pA, P.isP2p && P.p2pLine[0] && osd ? 1 : 0, .3f, C_EASE, 0);
  { int on = P.pg.at > 0 && now_() - P.pg.at < 6.0; /* 6 s from when it comes, controls or not, then it fades; once only */
    mo_to(&P.pgA, on ? 1 : 0, on ? .5f : .55f, C_EASE, 0); mo_to(&P.pgY, on ? 0 : -14, on ? .6f : .55f, C_OUT, 0); }
  if (P.tray.kind > 0) { mo_to(&P.tray.open, 1, .38f, C_TRIN, 0); mo_to(&P.tray.openA, 1, .18f, C_EASE, 0); }
  else { mo_to(&P.tray.open, .12f, .26f, C_TROUT, 0); mo_to(&P.tray.openA, 0, .2f, C_EASE, .04f); }
}
static void foc_update(void) {
  int fi = P.bIdx; if (P.mode == 2 && P.tray.kind) { int ak = P.tray.kind < 0 ? -P.tray.kind : P.tray.kind; int b = btn_index(ak == 1 || ak == 5 ? "eps" : ak == 6 ? "chn" : ak == 2 ? "src" : ak == 3 ? "aud" : "subs"); if (b >= 0) fi = b; }
  if (fi >= P.nbtn) fi = P.nbtn - 1; if (fi < 0) return;
  /* the focused button opens into a pill with its name: the others slide aside, the pill and the name follow (0.32 s) */
  int show = P.live && ((P.mode == 1 && P.row == 0) || P.mode == 2); float lw = show && P.btns[fi].lb[0] ? P.btns[fi].lw : 0;
  for (int k = 0; k < P.nbtn; k++) { float dx = 0; if (fi < P.nL && k < P.nL && k > fi) dx = lw; if (fi >= P.nL && k >= P.nL && k <= fi) dx = -lw; mo_to(&P.btns[k].dx, dx, .32f, C_OUT, 0); }
  float x = P.btns[fi].x0 + (fi >= P.nL ? -lw : 0), y = P.btns[fi].y, sz = P.btns[fi].w / 76;
  float fd = P.focSeen && fabsf(x - P.focX.to) > 260 ? .42f : .32f; /* across the gap between the two groups: the same glide, a little longer (never a jump) */
  if (!P.focSeen) { mo_set(&P.focX, x); mo_set(&P.focY, y); mo_set(&P.focW, (76 + lw) * sz); mo_set(&P.focH, 76 * sz); }
  else { mo_to(&P.focX, x, fd, C_OUT, 0); mo_to(&P.focY, y, fd, C_OUT, 0); mo_to(&P.focW, (76 + lw) * sz, fd, C_OUT, 0); mo_to(&P.focH, 76 * sz, fd, C_OUT, 0); }
  P.focSeen = 1; mo_to(&P.focA, show ? 1 : 0, .18f, C_EASE, 0);
  if (strcmp(P.tip, P.btns[fi].lb)) { snprintf(P.tip, sizeof P.tip, "%s", P.btns[fi].lb); P.tipAt = now_(); } /* a new name: its entrance again */
  mo_to(&P.tipA, lw > 0 ? 1 : 0, .14f, C_EASE, 0);
}
/* an MP4's own chapters, read (mp4chap.c): as the MKV reader's (n <= 0: none, the skips go on without them) */
static void on_mp4_chapters(const char *url, const Mp4Chapter *l, int n, void *ud) { (void)ud;
  if (!P.open || strcmp(url, P.url)) return; PlayerChapter pc[64]; int k = 0;
  for (int i = 0; i < n && k < 64; i++) { snprintf(pc[k].name, sizeof pc[k].name, "%s", l[i].name); pc[k].s = l[i].s; pc[k].e = i + 1 < n ? l[i + 1].s : dur(); k++; }
  player_chapters(url, pc, k); }
/* a film or episode whose file brings fewer than two chapters gets them made (Settings: Chapters on), so every one has
   them: the opening, recap and end credits where the skip sources put them (named), and marks about every 10 minutes
   between (numbered), none closer than 90 s to another. Made once the file and the skip sources have answered */
static void chap_generate(void) {
  if (P.chapGen || P.nchap >= 2 || P.isTrailer || live_title() || P.liveStream || !P.started) return;
  double d = dur(); if (d < 600) return; if (!P.chapReadyAt) P.chapReadyAt = now_(); double w = now_() - P.chapReadyAt;
  if ((P.sk.chapWait || P.chapWant) && w < 20) return; /* (the file's own, still being read) */
  if (!P.sk.sent && w < 16) return; /* (the skip sources: 14 s at most) */
  P.chapGen = 1; double m[24]; int n = 0; m[n++] = 0;
  if (P.intro.has) m[n++] = P.intro.s; if (P.intro.has && P.intro.e < d - 60) m[n++] = P.intro.e;
  if (P.recap.has) m[n++] = P.recap.s; if (P.recap.has && P.recap.e < d - 60) m[n++] = P.recap.e;
  if (P.outro.has && P.outro.s > 60) m[n++] = P.outro.s;
  int parts = (int)(d / 600 + 0.5); if (parts < 2) parts = 2; if (parts > 16) parts = 16;
  for (int k = 1; k < parts && n < 24; k++) { double at = d * k / parts; int near = 0; for (int i = 0; i < n; i++) if (fabs(m[i] - at) < 90) near = 1; if (!near && (!P.outro.has || at < P.outro.s - 90)) m[n++] = at; }
  for (int i = 1; i < n; i++) for (int j = i; j > 0 && m[j] < m[j - 1]; j--) { double t = m[j]; m[j] = m[j - 1]; m[j - 1] = t; }
  P.nchap = 0; for (int i = 0; i < n && P.nchap < 64; i++) { if (P.nchap && m[i] - P.chap[P.nchap - 1].s < 20) continue; if (m[i] < 0 || m[i] >= d - 5) continue; P.chap[P.nchap].s = m[i]; P.chap[P.nchap].nm[0] = 0; P.nchap++; }
  P.chLast = -1; fprintf(stderr, "[player] %d chapters made (the file had none)\n", P.nchap); }
/* a film in a collection: the next one found, for the credits card (checked once a second until known) */
static void coll_next_check(void) {
  if (P.coll.tried || !P.started || P.isTrailer || strcmp(P.type, "movie") || now_() < P.coll.at) return; P.coll.at = now_() + 1;
  char key[24]; const char *nm; int r = coll_of("movie", P.id, key, sizeof key, &nm); if (r < 0) { P.coll.tried = 1; return; } if (r == 0) return;
  const Coll *c = coll_get(key); if (!c) return; P.coll.tried = 1; int ix = coll_index(c, P.id), nx = -1; if (ix < 0) return;
  for (int i = ix + 1; i < c->n; i++) if (!strcmp(c->part[i].type, "movie")) { nx = i; break; } if (nx < 0) return; /* (the next film: a franchise's shows passed over) */
  if (!c->part[nx].id[0]) { P.coll.tried = 0; return; } /* (its IMDb id on its way: asked again in a second) */
  const CollPart *x = &c->part[nx]; P.coll.has = 1; snprintf(P.coll.id, sizeof P.coll.id, "%s", x->id); snprintf(P.coll.name, sizeof P.coll.name, "%s", x->name); snprintf(P.coll.poster, sizeof P.coll.poster, "%s", x->poster);
  snprintf(P.coll.bg, sizeof P.coll.bg, "https://images.metahub.space/background/medium/%s/img", x->id);
  char a[16], b[16]; snprintf(a, sizeof a, "%d", nx + 1); snprintf(b, sizeof b, "%d", c->n); snprintf(P.coll.part, sizeof P.coll.part, "%s", T2("Part {n} of {m}", "n", a, "m", b));
  memset(&closeCollIt, 0, sizeof closeCollIt); snprintf(closeCollIt.type, sizeof closeCollIt.type, "movie"); snprintf(closeCollIt.id, sizeof closeCollIt.id, "%s", x->id); snprintf(closeCollIt.name, sizeof closeCollIt.name, "%s", x->name); snprintf(closeCollIt.poster, sizeof closeCollIt.poster, "%s", x->poster);
  fprintf(stderr, "[player] next in its collection: %s\n", x->id); }
/* behind another app (#69, core/away.c): a film playing is paused where it is (kept, Trakt told by the pause, as the
   remote's Pause does); one still loading is held as it starts. A play started while behind (a phone's) plays on.
   Back in front: a film paused for it stays paused, its controls up */
void player_away(int behind) {
  if (!behind) { if (awayHold && P.open && !P.closing && paused()) show_osd(1); awayHold = 0; return; }
  if (!P.open || P.closing) return;
  awayHold = 1; if (!paused() && video_state() == VID_PLAYING) { video_pause(); save_progress(1); fprintf(stderr, "[player] behind another app: paused at %.0f s\n", video_time()); } }
void player_tick(float dt) {
  if (awayHold && (!P.open || P.closing)) awayHold = 0;
  if (awayHold && video_state() == VID_PLAYING) { extern int app_behind(void); if (app_behind()) { video_pause(); save_progress(1); fprintf(stderr, "[player] behind another app: held as it started\n"); } }
  status_tick(); pw_tick(); ps_tick(); if (getenv("SB_PREV_LOG")) sp_dev_log(); { static double npAt; if (now_() - npAt > 1) { npAt = now_(); np_tick(); } }
  { int sk = P.open && P.seekT >= 0 && P.seekScrub && prev_enabled() && sp_active() && sp_made() > 0 && !live_bcast(); /* (no picture yet: the seek pill alone, never a row of loaders) */ /* a scrub of the viewer's with previews: in 0.18 s, out 0.45 s where it was let go */
    static double skLast; if (sk) { skLast = now_(); stripQuick = 0; } else if (stripQuick) skLast = -1e9; else if (stripT >= 0 && P.open && now_() - skLast < 0.6) sk = 1; /* (between two presses the strip stays: it never closes and opens again; OK on a picture: it goes at once) */
    if (sk) { if (stripT < 0 || seekK.v <= 0.01f) stripFrom = video_time(); if (P.seekT >= 0 && fabs(P.seekT - stripT) > 0.01) { stripT = P.seekT; stripMovedAt = now_(); } } stripHold = sk; tween_to(&seekK, sk, sk ? 0.18f : stripQuick ? 0.08f : 0.45f, EASE_CSS_EASE); tween_tick(&seekK, dt); if (!sk && seekK.v <= 0.001f) { stripT = -1; stripQuick = 0; } }
  coll_next_check();
  hub_tick(dt);
  hand_tick(dt);
  subs_pump(); /* subtitle files and lists, fetched on their own threads */
  if (!P.open) return; video_pump(); sync_tick();
  css();
  Mo *mo[] = { &P.pgA, &P.pgY, &P.in, &P.artA, &P.ttlA, &P.ttlX, &P.ttlY, &P.barA, &P.barY, &P.btnA, &P.btnY, &P.topR, &P.topY, &P.dim, &P.dimT, &P.vig, &P.pzA, &P.bufA, &P.lnA, &P.pwA, &P.pwF, &P.skA, &P.skS, &P.knA,
    &P.prA, &P.prY, &P.prB, &P.subB, &P.ccS, &P.dlyA, &P.dlyR, &P.posA, &P.p2pA, &P.focX, &P.focY, &P.focW, &P.focH, &P.focA, &P.tipA, &P.hvA, &P.tray.open, &P.tray.openA, &P.tray.rowX, &P.tray.tabsX, &P.tray.ringX, &P.tray.ringY, &P.tray.ringA, &RL.t };
  if (P.skA.v <= 0.001f && !P.skOn) RL.key[0] = 0; /* the pill gone: its next time shows at once */
  for (size_t i = 0; i < sizeof mo / sizeof *mo; i++) mo_tick(mo[i], dt);
  for (int i = 0; i < 4; i++) { mo_tick(&P.tray.colY[i], dt); mo_tick(&P.tray.plY[i], dt); mo_tick(&P.tray.plA[i], dt); }
  for (int i = 0; i < P.nbtn; i++) { mo_tick(&P.btns[i].dx, dt); P.btns[i].x = P.btns[i].x0 + P.btns[i].dx.v; }
  nudge_tick(&P.nudge, dt);
  if (P.tray.kind < 0 && !P.tray.openA.on && P.tray.openA.v <= 0.001f) { P.tray.kind = 0; cols_clear(); srcs_free(); } /* put away: its lists go too */
  mo_tick(&SR.y, dt); mo_tick(&SR.plY, dt); mo_tick(&SR.plH, dt); mo_tick(&SR.plA, dt); tm_tick(dt); tray_swap_step(); src_tick(dt);
  if (!P.prompt.k && P.prA.v <= 0.001f) P.shown.k = 0;
  close_step(); if (P.closing) return;
  if (P.resolving) { const char *st = p2p_status(); if (strcmp(st, status)) set_status(st); return; } /* the art: nothing for a few seconds, then why it is slow ("Finding peers…", "Getting the file list…") */
  VidState vs = video_state();
  /* Trakt: what plays, every frame (it decides when to tell; trailers and live broadcasts never) */
  if (!P.isTrailer && !live_bcast()) { int ep = episodic(); ScrobbleItem si = { P.type, P.id, P.name, P.year, ep ? P.season : -1, ep ? P.episode : -1 };
    scrobble_player(&si, vs == VID_PLAYING && video_live() ? 1 : paused() ? 2 : 0, video_time(), dur()); }
  if (P.started && mkv && !P.isTrailer && !P.isP2p && !sp_active()) { static double spAt; if (now_() - spAt > 0.1) { spAt = now_(); double t0 = video_time(), b0 = video_buffered_end(); char sk[300]; snprintf(sk, sizeof sk, "%s|%s|%.0f|%.0f", P.id, P.vid, P.size, dur()); sp_player_tick(prev_src_pick(), sk, 0, live_bcast() || !strcmp(P.type, "tv") || !strcmp(P.type, "channel"), t0, dur(), b0 > t0 ? b0 - t0 : -1, (vs == VID_BUFFERING || vs == VID_LOADING) && !P.pw.on, paused() || P.pw.on); } } /* (seek previews not started yet: asked ten times a second, so they start the moment the index is in) */
  { static double last = -9; static int was; double d = dur(); double f = P.seekT >= 0 ? P.seekT : P.hvOn && d > 0 ? P.hvP * d : -1; int on = f >= 0; /* the remote's scrub, or the pointer over the bar: its pictures first (told as it moves) */
    if (on != was || (on && fabs(f - last) > 0.4)) { sp_focus(f, 0); last = f; was = on; } }
  if (P.started && now_() - P.posTick >= 1.0) { P.posTick = now_(); double t = video_time(), b = video_buffered_end(); if (t > 0) P.lastPos = t;
    if (mkv) mkv_set_pos(mkv, t, b > t ? b - t : -1);
    if (mkv && !P.isTrailer) prevspike_tick(mkv, vs == VID_PLAYING && P.live, t, b > t ? b - t : -1); /* (SB_PREVSPIKE only: seek previews measured) */
    { char sk[300]; snprintf(sk, sizeof sk, "%s|%s|%.0f|%.0f", P.id, P.vid, P.size, dur()); sp_player_tick(P.isTrailer || P.isP2p ? NULL : prev_src_pick(), sk, 0, live_bcast() || !strcmp(P.type, "tv") || !strcmp(P.type, "channel"), t, dur(), b > t ? b - t : -1, (vs == VID_BUFFERING || vs == VID_LOADING) && !P.pw.on, paused() || P.pw.on); } /* seek previews: what plays and how it goes (a debrid or direct file only: never a torrent on a TV) */
    dts_tick();
    if (!P.isTrailer && (!P.embKnown || !P.mkvTracksOk || mkv_state(mkv) == MKV_FALLBACK)) load_emb_tracks(); /* until the file's list settles (the TV's own list may come late) */
    if (P.isP2p) { p2p_pace(vs == VID_PLAYING, vs == VID_BUFFERING || vs == VID_LOADING, dur()); char ln[160]; int has = p2p_peer_line(ln, sizeof ln); snprintf(P.p2pLine, sizeof P.p2pLine, "%s", has ? ln : "");
      int waiting = (vs == VID_BUFFERING || vs == VID_LOADING) && !paused(); const char *why = p2p_wait_reason(waiting); /* one short reason under the ring once the wait is long */
      if (waiting && P.live && !strcmp(why, T("Waiting for the next pieces…"))) why = ""; /* mid-film the spinner says it alone */
      if (waiting && !P.failed) { if (strcmp(why, status)) set_status(why); } else if (vs == VID_PLAYING && status[0] && !P.failed) set_status(""); } }
  if (P.chapWant && P.sk.chapWait) { MkvChapter mc[64]; int n = mkv ? mkv_chapters(mkv, mc, 64) : 0; /* the file's chapters, once its head is read */
    if (n == 0 && !P.mp4Asked && (!mkv || mkv_state(mkv) == MKV_FALLBACK) && !strncmp(P.url, "http", 4) && !P.isHls && !P.isTrailer) { /* no MKV: an MP4's own chapters */
      P.chapWant = 0; P.mp4Asked = 1; mp4_chapters(P.url, on_mp4_chapters, NULL); }
    else if (n >= 0) { P.chapWant = 0; PlayerChapter pc[64]; for (int i = 0; i < n; i++) { snprintf(pc[i].name, sizeof pc[i].name, "%s", mc[i].name); pc[i].s = mc[i].s; pc[i].e = mc[i].e < 0 ? dur() : mc[i].e; } player_chapters(P.url, pc, n); } }
  chap_generate();
  if (P.dts.conv == 2 && !P.failed && (vs == VID_ERROR || (!video_live() && now_() - P.dts.convT0 > 25))) { dts_conv_failed(vs == VID_ERROR ? video_error() : "no picture after 25 s"); return; }
  if (P.dts.deferUntil) dts_defer_tick();
  if (vs == VID_ERROR && !P.failed && P.started && P.isP2p && !P.p2pRetried && p2p_moved_recently()) { /* the torrent's storage moved (a USB drive pulled out): the same stream once more, same moment */
    P.p2pRetried = 1; P.startAt = P.lastPos > 0 ? P.lastPos : 0; P.didResume = 0; P.live = 0; fprintf(stderr, "[player] stream error after the torrent storage moved: again at %.0f s\n", P.startAt); video_load(P.url); vs = video_state(); }
  if (vs == VID_ERROR && !P.failed && P.started && live_reload("stream error")) vs = video_state();
  if (vs == VID_ERROR && !P.failed && P.started) { if (P.alt[0] && !P.altTried) { P.altTried = 1; fprintf(stderr, "[player] error, the fallback URL\n"); video_load(P.alt); } else { if (!P.errAt) P.errAt = now_(); if (!P.sizeTok || P.sizeAns || now_() - P.errAt > 4) fail(fail_reason()); } } /* the first URL failed: the same file the other way; the reason waits for the server's answer (4 s at most) */
  if (P.nx.tok && !P.nx.done && now_() >= P.nx.until) nx_finish();
  if (P.metaRetryAt && now_() >= P.metaRetryAt) { P.metaRetryAt = 0; if (!P.next.has || !P.videos) ask_meta(); }
  if (P.extRetryAt && now_() >= P.extRetryAt) { P.extRetryAt = 0; load_ext_subs(P.extTry + 1); }
  if (P.adxRetryAt && now_() >= P.adxRetryAt) { P.adxRetryAt = 0; load_addon_subs(P.adxTry + 1); }
  if (P.embWaitAt && !P.embWaitOver && now_() >= P.embWaitAt) { P.embWaitOver = 1; auto_sub(); }
  if (P.sameWaitAt && !P.sameWaitOver && now_() >= P.sameWaitAt) { P.sameWaitOver = 1; auto_sub(); }
  sub_check(); ass_sync();
  sk_tick(); loc_apply();
  if (P.started && !P.ldd && dur() > 0) P.ldd = 1; /* the length is known: the loading line fades before the picture comes */
  /* the art stays until the picture really runs: the resume jump is made behind it, and it fades only once playing has
     been steady for a moment (never a frozen first frame, then a spinner) */
  if (P.liveT0 && !P.autoAudDone && now_() - P.liveT0 < 15) { static double aAt; if (now_() - aAt > 0.5) { aAt = now_(); auto_audio(); } } /* (the player may list the file's tracks a moment after the first picture: asked again until it has, 15 s at most) */
  if (!P.live && P.started && video_live()) { /* (P.started: this title's own stream asked for, as start_video says; before it, a picture still running is another's: Home's trailer while the best stream is chosen) */
    if (!P.liveT0) { P.liveT0 = now_(); P.steadyAt = 0; apply_aspect(); try_resume(); auto_audio(); load_emb_tracks(); }
    int steady = (video_state() == VID_PLAYING || paused()) && !video_settling(); if (!steady) P.steadyAt = 0; else if (!P.steadyAt) P.steadyAt = now_();
    if ((P.steadyAt && now_() - P.steadyAt > (P.quiet ? 0.06 : 0.35)) || now_() - P.liveT0 > 15) { /* (a channel switched to: up at its first frames) */ P.live = 1; pg_ask(); if (live_title()) live_reach_set(P.id, 1); /* (a channel: it really plays) */ fprintf(stderr, "[player] picture running after %.1f s\n", now_() - P.liveT0); if (resumeMsg[0]) { toast(resumeMsg, 2500, NULL); resumeMsg[0] = 0; }
      { static int tr; const char *e = getenv("SB_PLAY_TRAY"); if (e && !tr) { tr = 1; open_tray(!strcmp(e, "src") ? 2 : !strcmp(e, "eps") ? 1 : !strcmp(e, "aud") ? 3 : 4); } } } } /* (development: SB_PLAY_TRAY=src|eps|aud|subs opens that tray once the picture runs, for pictures of it) */
  if (!video_live()) P.liveT0 = 0;
  if (P.live && !P.skipsAsked && dur() > 0) load_skips(); /* once the length is known: segments are checked against it */
  if (P.live) {
    if (video_width() != P.lastVw || video_height() != P.lastVh) apply_aspect(); /* the picture's size became known or changed */
    if (P.aspRefreshAt && now_() >= P.aspRefreshAt) { P.aspRefreshAt = 0; if (paused()) video_seek(video_time()); }
    if (paused() && P.prompt.autoN && (P.prompt.k == 1 || P.prompt.k == 2)) { P.prompt.autoN = 0; paint_prompt(); } /* paused: no skipping by itself */
    /* the screensaver: paused and untouched for the time chosen in Settings, the controls fade away and the title's cover takes the screen */
    if (P.ssOn && !paused()) ss_exit();
    /* (the player has no screensaver of its own any more: the app's one comes over a paused film) */
    if (paused()) { if (!P.pzSince) P.pzSince = now_(); } else P.pzSince = 0; /* (how long it has been paused, any way) */
    int pz = paused() && P.mode != 2 && !P.pw.on; if (pz && !P.pausedShown) { if (!P.pausedAt) P.pausedAt = now_(); if (now_() - P.pausedAt > 0.35) { P.pausedShown = 1; show_osd(1); } }
    else if (!pz) { P.pausedAt = 0; P.pausedShown = 0; }
    if (P.seekT >= 0 && now_() >= P.seekCommitAt) commit_seek();
    if (P.seekSnapAt) { static int stalled; double tn = now_(); /* (SB_SEEKSNAP: a committed seek's time to playing again, its picture there) */
      if (vs != VID_PLAYING) stalled = 1;
      else if ((stalled || tn - P.seekSnapAt > 1.5) && fabs(video_time() - P.seekSnapT) < 5) { fprintf(stderr, "[seek] playing again %.0f ms after the seek to %.2f s (%s, at %.2f s)\n", (tn - P.seekSnapAt) * 1000, P.seekSnapT, stalled ? "after loading" : "no stall seen", video_time()); P.seekSnapAt = 0; stalled = 0; }
      else if (tn - P.seekSnapAt > 20) { P.seekSnapAt = 0; stalled = 0; } }
    if (P.seekTxtUntil && now_() >= P.seekTxtUntil && !stripHold) { P.seekTxtUntil = 0; P.skOn = 0; P.skm = 0; } /* (the previews up: the bar stays with them, and goes with them) */
    if (P.mode == 1 && now_() >= P.hideAt && !paused() && P.seekT < 0) hide_osd();
    { double nw = now_(); if (vs == VID_PLAYING && !video_settling() && P.playTick > 0 && nw - P.playTick < 1) P.played += nw - P.playTick; P.playTick = nw; ep_back_check(); } /* (seconds really watched: an earlier episode's place) */
    if (vs == VID_PLAYING && at_credits() && !P.marked) { mark_watched(); save_progress(1); } /* the credits: watched, everywhere, at once */
    if (vs == VID_PLAYING && now_() - P.lastSave >= 10) save_progress(0);
    if (vs == VID_ENDED && !P.ended && live_reload("the stream ended")) vs = video_state();
    if (vs == VID_ENDED && !P.ended) { P.ended = 1; scrobble_player_end(video_time(), dur(), 1); save_progress(1); mark_watched(); if (strcmp(store_get("bingeWatch", "true"), "false") && strcmp(store_get("enableNextVideo", "true"), "false") && P.next.has) P.endGoAt = now_() + 0.6; else close_player(0, 0); }
    if (P.endGoAt && now_() >= P.endGoAt) { P.endGoAt = 0; go_next(); if (!P.open || P.closing) return; }
    check_prompt();
    if (P.prompt.autoN && (P.prompt.k == 1 || P.prompt.k == 2) && now_() - P.prompt.t0 >= P.prompt.adur && !paused()) prompt_go(0);
    else if (P.prompt.autoN && P.prompt.k == 3 && now_() - P.prompt.t0 >= 10) { P.prompt.autoN = 0; next_auto(); }
    if (!P.live) return; /* a new episode is being found */
    const char *r = res_of_video(video_width(), video_height()); if (*r && !P.isTrailer) P.res = r;
    info_update();
  }
  foc_update();
}

/* ---- drawing ---- */
/* the drawing's words measured, upper-cased or wrapped once per text (and language), not every frame */
typedef struct { char s[640]; int px, wt, n, lines; float tr, w, mw; unsigned lg; } TMemo;
static int tm_same(TMemo *m, const char *s, int px, int wt, float tr) { if (m->px == px && m->wt == wt && m->tr == tr && !strcmp(m->s, s)) return 1; snprintf(m->s, sizeof m->s, "%s", s); m->px = px; m->wt = wt; m->tr = tr; return 0; }
static float tm_width(TMemo *m, const char *s, int px, int wt, float tr) { if (!tm_same(m, s, px, wt, tr) || m->n != -1) { m->n = -1; m->w = text_width(s, px, wt, tr); } return m->w; }
static int tm_wrap(TMemo *m, const char *s, float maxW, int lines, int px, int wt, float tr) { if (!tm_same(m, s, px, wt, tr) || m->mw != maxW || m->lines != lines) { m->mw = maxW; m->lines = lines; m->n = text_wrap_count(s, maxW, lines, px, wt, tr); } return m->n; }
typedef struct { char src[200], up[200]; unsigned lg; } UMemo;
static const char *um_upper(UMemo *m, const char *s) { if (m->lg != i18n_gen() || strcmp(m->src, s)) { m->lg = i18n_gen(); snprintf(m->src, sizeof m->src, "%s", s); snprintf(m->up, sizeof m->up, "%s", s); text_upper(m->up, sizeof m->up); } return m->up; }
/* where the last frame put what the pointer can click (the tray's tabs, cards and columns, the prompt) */
static struct { int tray; float x, y, w, h; int ntab; float tabX[40], tabW[40], tabY, tabH; int nrow; float rowX[MAXROW], rowW[MAXROW], rowY, rowH;
  float colX[4], colW, colTop, colBot; int nit[4]; float itY[4][300], itH[4][300]; int prompt, card, npr; float prX[2], prY[2], prW[2], prH[2]; int src; float srcX, srcTop; } PG;
/* the paused vignette and the art's shade, baked once into small alpha pictures the GPU stretches, as the page draws them
   into tiny canvases: one quad each, no rings or bands */
static GLuint vigTex, shTex;
static void bake(void) {
  if (vigTex) return;
  int W = 384, H = 216; unsigned char *a = malloc((size_t)W * H);
  for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { float px = x + .5f, py = y + .5f, dx = (px - .6f * W) / (.744f * W), dy = (py - .4f * H) / (.62f * H), r = sqrtf(dx * dx + dy * dy);
    float ra = r <= .45f ? 0 : r >= 1 ? .6f : .6f * (r - .45f) / .55f, u = px / W, la = u < .55f ? .42f * (1 - u / .55f) : 0;
    a[y * W + x] = (unsigned char)((ra + la - ra * la) * 255 + .5f); }
  vigTex = gfx_tex_create(a, W, H, 1, 1);
  W = 256; H = 144; /* radial-gradient(ellipse at center): .28 in the middle, .92 from 78 % of the way to the corners */
  for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { float dx = (x + .5f - W / 2.0f) / (W / 2.0f * 1.41421f), dy = (y + .5f - H / 2.0f) / (H / 2.0f * 1.41421f), r = sqrtf(dx * dx + dy * dy);
    a[y * W + x] = (unsigned char)((r >= .78f ? .92f : .28f + .64f * r / .78f) * 255 + .5f); }
  shTex = gfx_tex_create(a, W, H, 1, 1); free(a);
}
static float txt_sh(const char *t, float x, float y, int px, int wt, uint32_t col, float ls, float maxW, int align, int sa) { /* text-shadow: 0 1px 0 */
  text_draw(t, x, y + 1, px, wt, BLACK_A(sa), ls, maxW, align); return text_draw(t, x, y, px, wt, col, ls, maxW, align); }
static const char *logo_url(void) { return pageLogo[0] && !strcmp(pageLogoId, P.id) ? pageLogo : P.logo; }
static int name_shown(GLuint logoTex) { /* the name only when there is no text art by 2.5 s (never for a few frames first) */
  const char *lu = logo_url(); if (logoTex) return 0; if (lu[0] && !img_failed(lu) && now_() - P.openedAt < 2.5) return 0;
  if (!P.nameAt) P.nameAt = now_(); return 1; }
static GLuint logo_fit(float mw, float mh, float *w, float *h) {
  const char *lu = logo_url(); float asp = 1; GLuint t = lu[0] ? img_get(lu, (int)mw, (int)mh, &asp) : 0; if (!t) { *w = *h = 0; return 0; }
  *w = mw; *h = mw / asp; if (*h > mh) { *h = mh; *w = mh * asp; } return t; }
static const char *trailer_name(void) { return P.trName[0] ? P.trName : T("Trailer"); }
/* a channel loading: a deep gradient in its logo's colours, a slow glow drifting across it */
static void live_backdrop(void) {
  static char url[512]; static uint32_t c1 = RGB(40, 30, 64), c2 = RGB(14, 12, 22); static int done;
  const char *u = P.logo[0] ? P.logo : P.poster; if (strcmp(url, u)) { snprintf(url, sizeof url, "%s", u); done = 0; c1 = RGB(40, 30, 64); c2 = RGB(14, 12, 22); }
  if (!done && u[0]) { Palette p; int r = img_palette(u, 64, 64, 1, &p); if (r) { done = 1; if (r > 0) { const Swatch *v = p.vibrant.ok ? &p.vibrant : p.muted.ok ? &p.muted : NULL, *d = p.darkMuted.ok ? &p.darkMuted : p.dark.ok ? &p.dark : v;
      if (v) c1 = hsl2rgb(v->h, fminf(v->s, 0.6f), 0.26f); if (d) c2 = hsl2rgb(d->h, fminf(d->s, 0.45f), 0.08f); } } }
  gfx_rrect_v(0, 0, SB_W, SB_H, 0, c1, c2);
  float ph = (float)fmod(now_() / 12, 1) * 6.2832f; gfx_alpha_tex(shTex, -200 + 140 * sinf(ph), 0, SB_W + 400, SB_H, BLACK_A(120)); /* (the page's own shade, drifting) */
}
static void draw_art(void) {
  float a = P.artA.v; if (a <= 0.001f) return; gfx_opacity_push(a); bake();
  int ss = P.ssOn && !P.closing; /* the screensaver: the cover comes back brighter and drifts very slowly (26 s each way); the logo alone */
  gfx_rect(0, 0, SB_W, SB_H, PAGE);
  if (live_title()) live_backdrop(); /* a channel: its logo's own colours, never its logo blown up behind itself */
  float asp; GLuint bg = P.bg[0] && !live_title() ? img_get(P.bg, 1280, 720, &asp) : 0; if (bg) { float u0, v0, u1, v1; img_cover_uv(asp, SB_W / SB_H, 1, &u0, &v0, &u1, &v1);
    if (ss) { float ph = fmodf((float)(now_() - P.ssAt) / 26, 2), k = bez(C_INOUT, ph < 1 ? ph : 2 - ph), sc = 1.02f + 0.07f * k;
      gfx_scale_push(sc, sc, SB_W / 2, SB_H / 2); gfx_offset_push(SB_W * (-0.01f + 0.022f * k), SB_H * (-0.008f * k)); gfx_image(bg, 0, 0, SB_W, SB_H, 0, WHITE_A(199), u0, v0, u1, v1); gfx_offset_pop(); gfx_scale_pop(); }
    else gfx_image(bg, 0, 0, SB_W, SB_H, 0, WHITE_A(107), u0, v0, u1, v1); }
  gfx_alpha_tex(shTex, 0, 0, SB_W, SB_H, PAGE);
  float bk = !P.closing ? 1 : closeArt ? 1 - (float)((now_() - closeT0) / 0.22) : 0; /* closed while loading: the rest of the block fades as the logo leaves */
  if (!P.closing && sx_flying() && sx_since() < 0.5) { float q = (float)((sx_since() - 0.12) / 0.3); bk = q < 0 ? 0 : q > 1 ? 1 : q; bk = bk * bk * (3 - 2 * bk); } /* opened: it fades into place as the logo lands */
  if (!P.closing || bk > 0.001f) { /* the title block, centred as one column: art, trailer tag, loading line, status */
    float lw, lh; GLuint t = logo_fit(760, 260, &lw, &lh); int nm = name_shown(t), nl = 0;
    static TMemo nmW; if (nm) { nl = tm_wrap(&nmW, P.name, SB_W * 0.8f, 3, 78, 800, -1.5f); lh = nl * 78 * 1.05f; }
    int tag = P.isTrailer || !strcmp(P.type, "other"), ln = !ss && !P.failed; float L24 = text_line_height(24), L25 = text_line_height(25);
    float pwh = ss ? 0 : 92 * bez(C_INOUT, P.pwA.v); /* (Play now: 36 apart and 56 tall, its room opening smoothly) */
    float tot = lh + (tag && !ss ? 28 + L24 : 0) + (ln ? 49 : 0) + (ss ? 0 : 20 + fmaxf(34, L25)) + pwh, y = SB_H / 2 - tot / 2;
    float o = 1; if ((!P.live || P.pw.on) && !ss) { float p = fmodf((float)(now_() - P.openedAt) / 3.6f + .5f, 1); /* from full strength: it lands as it flew */ o = p < .5f ? .9f + .1f * bez(C_INOUT, p * 2) : 1 - .1f * bez(C_INOUT, (p - .5f) * 2); } /* breathing */
    if (P.closing) {} /* (the page's copy flies back) */
    else if (t) { char sk[80]; snprintf(sk, sizeof sk, "logo:%s", P.id); sx_image(sk, t, SB_W / 2 - lw / 2, y, lw, lh, 0, WHITE_A((int)(255 * o)), 0, 0, 1, 1); }
    else if (nm) { float k = kf(P.nameAt, .45f, C_LGIN, 0); { char sk[80]; snprintf(sk, sizeof sk, "logo:%s", P.id); sx_text_wrapped(sk, P.name, SB_W / 2, y, SB_W * 0.8f, 3, 78, 800, WHITE_A((int)(255 * o * k)), -1.5f, ALIGN_CENTER, 78 * 1.05f); } }
    y += lh; if (bk < 1) gfx_opacity_push(bk);
    if (tag && !ss) { y += 28; static UMemo tgU; static TMemo tgW; const char *tg = um_upper(&tgU, trailer_name()); float tw = tm_width(&tgW, tg, 24, 700, 8), totW = 84 + 22 + tw + 14 + 84, x0 = SB_W / 2 - totW / 2, ly = y + L24 / 2 - 1;
      gfx_rrect_h(x0, ly, 84, 2, 0, col_alpha(ACC, 0), ACC2); text_draw(tg, x0 + 84 + 22, y, 24, 700, WHITE_A(255), 8, 0, ALIGN_LEFT); gfx_rrect_h(x0 + 84 + 22 + tw + 14, ly, 84, 2, 0, ACC2, col_alpha(ACC, 0)); y += L24; }
    if (ln) { y += 46; float pa = P.pwA.v; /* previews first: the running bar becomes the previews' own (70 % fills it) */
      if (pa > 0.001f) { float x0 = SB_W / 2 - 130; gfx_opacity_push(pa * P.lnA.v); gfx_rrect(x0, y, 260, 3, 1.5f, WHITE_A(46)); if (P.pwF.v > 0.004f) gfx_rrect_h(x0, y, 260 * P.pwF.v, 3, 1.5f, ACC, ACC2); gfx_opacity_pop(); }
      if (P.lnA.v * (1 - pa) > 0.001f) { float x0 = SB_W / 2 - 130, p = bez(C_INOUT, clock_ph(1.5)), sx = x0 - 117 + 380.25f * p; /* 45 % of the line, from -100 % to 225 % of itself */
        gfx_opacity_push(P.lnA.v * (1 - pa)); gfx_clip_push(x0, y, 260, 3); gfx_rrect_h(sx, y, 58.5f, 3, 2, col_alpha(ACC, 0), ACC2); gfx_rrect_h(sx + 58.5f, y, 58.5f, 3, 2, ACC2, col_alpha(ACC, 0)); gfx_clip_pop(); gfx_opacity_pop(); }
      y += 3; }
    if (!ss) { y += 20; xline_draw(&stX[0], loading_line(status), SB_W / 2, y, 25, P.failed ? RGB(0xff, 0xb9, 0xb9) : WHITE_A(168), SB_W - 200, ALIGN_CENTER); y += fmaxf(34, L25); }
    if (pwh > 0.5f) { /* Play now: one quiet pill, the OK key and its words, rising into place */
      float a = bez(C_EASE, P.pwA.v), kw = key_hint(-9999, 0, K_OK, T("Play now"), WHITE_A(255), 0), w = kw + 2 * 26, h = 56, x = SB_W / 2 - w / 2, py = y + 36 + 10 * (1 - a);
      gfx_opacity_push(a); gfx_rrect(x, py, w, h, h / 2, WHITE_A(26)); gfx_ring(x, py, w, h, h / 2, 1.5f, WHITE_A(56));
      key_hint(x + 26, py + h / 2, K_OK, T("Play now"), WHITE_A(240), 0); gfx_opacity_pop(); }
    if (bk < 1) gfx_opacity_pop(); }
  gfx_opacity_pop();
}
/* the subtitle style, read once while the settings stay as they are */
typedef struct { unsigned ser; int px; uint32_t col, ol; float bo; } SubStyle;
static const SubStyle *sub_style(void) {
  static SubStyle st; if (st.ser == store_serial() && st.ser) return &st;
  st.ser = store_serial(); st.px = (int)(38 * SUB_SIZES[size_idx()] / 100.0f + 0.5f); st.col = hexcol(store_get("subsColor", "#ffffff")); st.ol = hexcol(store_get("subsOutlineColor", "#000000")); st.bo = (float)atof(SUB_BGS[bg_idx()]);
  { float k = atoi(SUB_BRIGHT[bright_idx()]) / 100.0f; st.col = col_dim(st.col, k); st.ol = col_dim(st.ol, k); asssubs_brightness(k); } /* (the brightness: plain, embedded and styled lines alike) */
  return &st;
}
/* the cue list's index, made once per list: whether it runs in start order, and the latest end so far at each cue */
typedef struct { const Cue *c; int n; const char *t0, *t1; int sorted; unsigned gen, tg; double *maxE; int cap; } CueIx;
static const CueIx *cue_ix(void) {
  static CueIx ix; int n = P.ncues;
  if (ix.c == P.cues && ix.n == n && n && ix.t0 == P.cues[0].t && ix.t1 == P.cues[n - 1].t && ix.tg == cueTimesGen) return &ix;
  ix.c = P.cues; ix.n = n; ix.t0 = n ? P.cues[0].t : NULL; ix.t1 = n ? P.cues[n - 1].t : NULL; ix.gen++; ix.tg = cueTimesGen; ix.sorted = 0;
  if (n > ix.cap) { double *m = realloc(ix.maxE, (size_t)n * sizeof *m); if (!m) return &ix; ix.maxE = m; ix.cap = n; }
  ix.sorted = 1; for (int i = 0; i < n; i++) { if (i && P.cues[i].s < P.cues[i - 1].s) ix.sorted = 0; ix.maxE[i] = i && ix.maxE[i - 1] > P.cues[i].e ? ix.maxE[i - 1] : P.cues[i].e; }
  return &ix;
}
static int sub_is_arabic(void) { const char *l = ""; /* the subtitles on are Arabic (a file's, an addon's or the film's own) */
  if (!strncmp(P.curSub, "ext:", 4)) l = P.pick.lang; else if (!strncmp(P.curSub, "emb:", 4)) { int n = atoi(P.curSub + 4); for (int i = 0; i < P.nEmb; i++) if (P.emb[i].idx == n) l = P.emb[i].lang; }
  return !strncasecmp(l, "ar", 2) && (!l[2] || l[2] == '-' || l[2] == '_' || !strcasecmp(l, "ara") || !strncasecmp(l, "arab", 4)); }
static int subw(void) { return subfont_w(600); } /* (the subtitles' weight in the viewer's fonts) */
static void draw_mkv_line(const MkvLine *l, float bx, float by, int px, uint32_t col, uint32_t ol, float bo, int align) {
  float lh = px * 1.35f; char buf[1024]; snprintf(buf, sizeof buf, "%s", l->t); char *lp[12]; int n = 0; for (char *c = strtok(buf, "\n"); c && n < 12; c = strtok(NULL, "\n")) lp[n++] = c;
  int an = l->an ? l->an : 2; float y = an >= 7 ? by : an >= 4 ? by - n * lh / 2 : by - n * lh; /* numpad: 7-9 hang from the point, 1-3 stand on it */
  if (l->color) col = 0xff000000u | (l->color & 0xffffff); /* a sign keeps its own colour */
  for (int i = 0; i < n; i++) { float tw = text_width(lp[i], px, subw(), 0), x0 = align == ALIGN_CENTER ? bx - tw / 2 : align == ALIGN_RIGHT ? bx - tw : bx;
    if (bo > 0) gfx_rrect(x0 - 16, y - 2, tw + 32, lh + 4, 8, BLACK_A((int)(bo * 255)));
    for (int d = 0; d < 8; d++) { static const float ox[8] = { -2, 2, 0, 0, -1, 1, -1, 1 }, oy[8] = { 0, 0, -2, 2, -1, 1, 1, -1 }; text_draw(lp[i], bx + ox[d], y + oy[d] + (lh - text_line_height(px)) / 2, px, subw(), ol, 0, 0, align); }
    text_draw(lp[i], bx, y + (lh - text_line_height(px)) / 2, px, subw(), col, 0, 0, align); y += lh; }
}
static int subDrew; /* draw_subs put a line on screen this frame */
static void draw_subs_lines(void) {
  float bottom = P.subB.v; /* the page's bottom: 8 %, above the controls (420), the seek bar (200) or an open tray (62 %), gliding 0.3 s */
  if (asssubs_on()) { (void)sub_style(); asssubs_draw(); subDrew = asssubs_visible(); return; } /* styled (libass): the viewer's brightness handed to its lines too (sub_style); between its lines the style's sample may show */
  if (!strncmp(P.curSub, "emb:", 4) && SL.mkv && !SL.tv && mkv) { /* the file's own lines (the TV draws them while it holds the track) */
    MkvLine ls[8]; static char tb[4096]; int n = mkv_lines_at(mkv, atoi(P.curSub + 4), video_time() - P.subDelay, ls, 8, tb, sizeof tb); if (!n) return; subDrew = 1;
    if (!SL.shown) { SL.shown = 1; fprintf(stderr, "[subs] first line on screen %.1f s after the choice: \"%.40s\"\n", now_() - SL.at, ls[0].t); }
    const SubStyle *sst = sub_style(); int px = sst->px; uint32_t col = sst->col, ol = sst->ol; float bo = sst->bo;
    float lh = px * 1.35f, stackY = SB_H - bottom;
    for (int i = n - 1; i >= 0; i--) { const MkvLine *l = &ls[i]; int an = l->an ? l->an : 2, al = an % 3 == 1 ? ALIGN_LEFT : an % 3 == 0 ? ALIGN_RIGHT : ALIGN_CENTER;
      if (l->x >= 0 && l->y >= 0) draw_mkv_line(l, l->x / 100 * SB_W, l->y / 100 * SB_H, px, col, ol, bo, al); /* placed by the file */
      else if (an >= 7) draw_mkv_line(l, al == ALIGN_LEFT ? 80 : al == ALIGN_RIGHT ? SB_W - 80 : SB_W / 2, 70, px, col, ol, bo, al);
      else if (an >= 4) draw_mkv_line(l, al == ALIGN_LEFT ? 80 : al == ALIGN_RIGHT ? SB_W - 80 : SB_W / 2, SB_H / 2, px, col, ol, bo, al);
      else { int nl = 1; for (const char *c = l->t; *c; c++) if (*c == '\n') nl++; draw_mkv_line(l, al == ALIGN_LEFT ? 80 : al == ALIGN_RIGHT ? SB_W - 80 : SB_W / 2, stackY, px, col, ol, bo, al); stackY -= nl * lh; } } /* the dialogue stacks up from the bottom */
    return; }
  if (!P.ncues || !strncmp(P.curSub, "emb", 3)) return; double t = video_time() - P.subDelay; const Cue *show[2]; int ns = 0;
  const CueIx *ix = cue_ix();
  if (!ix->sorted) { for (int i = 0; i < P.ncues; i++) if (t >= P.cues[i].s && t <= P.cues[i].e) { if (ns < 2) show[ns++] = &P.cues[i]; else { show[0] = show[1]; show[1] = &P.cues[i]; } } }
  else { /* the last two cues showing at t: those starting by t (a binary search), back from there while an earlier one can still last until t */
    int lo = 0, hi = P.ncues; while (lo < hi) { int mid = (lo + hi) / 2; if (P.cues[mid].s <= t) lo = mid + 1; else hi = mid; }
    const Cue *f[2]; int nf = 0;
    for (int i = lo - 1; i >= 0 && nf < 2; i--) { if (ix->maxE[i] < t) break; if (t >= P.cues[i].s && t <= P.cues[i].e) f[nf++] = &P.cues[i]; }
    if (nf == 2) { show[0] = f[1]; show[1] = f[0]; ns = 2; } else if (nf == 1) { show[0] = f[0]; ns = 1; } }
  if (!ns) return; subDrew = 1;
  if (!SL.shown) { SL.shown = 1; fprintf(stderr, "[subs] first line on screen %.1f s after the choice: \"%.40s\"\n", now_() - SL.at, show[0]->t); }
  const SubStyle *st = sub_style(); int px = st->px; uint32_t col = st->col, ol = st->ol; float bo = st->bo;
  float lh = px * 1.35f;
  /* the shown cues' lines, split and measured once while they stay up */
  static struct { const Cue *a, *b; unsigned gen; int px, lines; char buf[4096]; char *lp[12]; float mw; } SC;
  const Cue *ca = show[0], *cb = ns > 1 ? show[1] : NULL;
  if (SC.a != ca || SC.b != cb || SC.gen != ix->gen || SC.px != px) { SC.a = ca; SC.b = cb; SC.gen = ix->gen; SC.px = px;
    char all[4096] = ""; for (int i = 0; i < ns; i++) { size_t l = strlen(all); snprintf(all + l, sizeof all - l, "%s%s", i ? "\n" : "", show[i]->t); }
    SC.lines = 0; snprintf(SC.buf, sizeof SC.buf, "%s", all); for (char *c = strtok(SC.buf, "\n"); c && SC.lines < 12; c = strtok(NULL, "\n")) SC.lp[SC.lines++] = c;
    SC.mw = 0; for (int i = 0; i < SC.lines; i++) SC.mw = fmaxf(SC.mw, text_width(SC.lp[i], px, subw(), 0)); }
  int lines = SC.lines; char **lp = SC.lp;
  float y = SB_H - bottom - lines * lh;
  if (bo > 0) { float mw = SC.mw; gfx_rrect(SB_W / 2 - mw / 2 - 16, y - 2, mw + 32, lines * lh + 4, 8, BLACK_A((int)(bo * 255))); } /* one box around the lines, as the page's span */
  for (int i = 0; i < lines; i++) {
    for (int d = 0; d < 8; d++) { static const float ox[8] = { -2, 2, 0, 0, -1, 1, -1, 1 }, oy[8] = { 0, 0, -2, 2, -1, 1, 1, -1 }; text_draw(lp[i], SB_W / 2 + ox[d], y + oy[d] + (lh - text_line_height(px)) / 2, px, subw(), ol, 0, 0, ALIGN_CENTER); }
    text_draw(lp[i], SB_W / 2, y + (lh - text_line_height(px)) / 2, px, subw(), col, 0, 0, ALIGN_CENTER); y += lh; }
}
/* while the position is set: a sample line in the viewer's style where the subtitles will sit, when none shows */
static void draw_subs_(void);
/* the subtitles */
static void draw_subs(void) { draw_subs_(); } /* (at their own brightness under every tray: an open tray lifts them over it, never dims them) */
static void draw_subs_(void) {
  subDrew = 0; draw_subs_lines(); float a = P.posA.v;
  if (asssubs_on()) { asssubs_sample(a > 0.01f); return; }
  if (player_sub_scope() == 2) return; /* (the TV draws them: no sample of ours over its lines) */ /* a styled track: its own lines show the style being set (the file's nearest one when none is due), never a plain sample over them */
  if (a <= 0.01f || subDrew) return;
  const SubStyle *st = sub_style(); int px = st->px; float lh = px * 1.35f, y = SB_H - P.subB.v - lh; const char *t = P.styK == 6 || sub_is_arabic() ? "\xd8\xb3\xd8\xaa\xd8\xb8\xd9\x87\xd8\xb1\x20\xd8\xa7\xd9\x84\xd8\xaa\xd8\xb1\xd8\xac\xd9\x85\xd8\xa9\x20\xd9\x87\xd9\x86\xd8\xa7" : T("Subtitles appear here"); /* (Arabic subtitles, or their font being set: the sample in Arabic) */ float tw = text_width(t, px, subw(), 0);
  gfx_opacity_push(a * 0.9f);
  if (st->bo > 0) gfx_rrect(SB_W / 2 - tw / 2 - 16, y - 2, tw + 32, lh + 4, 8, BLACK_A((int)(st->bo * 255)));
  for (int d = 0; d < 8; d++) { static const float ox[8] = { -2, 2, 0, 0, -1, 1, -1, 1 }, oy[8] = { 0, 0, -2, 2, -1, 1, 1, -1 }; text_draw(t, SB_W / 2 + ox[d], y + oy[d] + (lh - text_line_height(px)) / 2, px, subw(), st->ol, 0, 0, ALIGN_CENTER); }
  text_draw(t, SB_W / 2, y + (lh - text_line_height(px)) / 2, px, subw(), st->col, 0, 0, ALIGN_CENTER);
  gfx_opacity_pop();
}
/* seek previews on the TV (core/seekprev): a scrub with the remote shows the picture at its time over the seek pill,
   its neighbours either side smaller and fainter; the pointer over the bar, one picture over its time. Its own keyframe
   sharp, else the nearest made (drawn the same), none yet: the pill alone, as before. How much of the film has its
   pictures shows a moment as a scrub starts ("Previews 64%", "Previews ready") */
/* scrubbing with the remote: the film steps back (a little darker), the picture at the scrub's time large in the middle,
   its two neighbours either side smaller and fainter, sliding out from behind it as it opens; the time under it and
   how far from where the scrub began. In 0.18 s, out 0.45 s (where it was, after the seek is let go). No picture yet
   for that time: the same card waiting, a ring filling with how much of the film has its pictures (as the phone) */
/* the videos whose "Previews ready" has been seen (the store, the last 200, newest last): it is said once a video */
static int ready_seen(const char *vid) { const char *l = store_get("prevReadySeen", ""); size_t n = strlen(vid); if (!n) return 0;
  for (const char *p = l; (p = strstr(p, vid)); p += n) if ((p == l || p[-1] == ' ') && (p[n] == ' ' || !p[n])) return 1; return 0; }
static void ready_mark(const char *vid) { if (!vid || !*vid || ready_seen(vid)) return; const char *l = store_get("prevReadySeen", ""); char b[8192]; snprintf(b, sizeof b, "%s%s%s", l, *l ? " " : "", vid);
  int words = 1; for (const char *p = b; *p; p++) words += *p == ' '; char *q = b; while (words > 200 && (q = strchr(q, ' '))) { q++; words--; } /* (the oldest go) */
  store_set("prevReadySeen", q && words <= 200 ? q : b); }
/* under the controls while the previews are up: the film a little darker, a light vignette (one stretched quad) */
static void prev_shade(void) { if (stripT < 0 || seekK.v <= 0.01f || live_bcast()) return; float k = ease_apply(EASE_OUT, clampf(seekK.v, 0, 1));
  gfx_rect(0, 0, SB_W, SB_H, BLACK_A((int)(56 * k))); bake(); if (vigTex) gfx_alpha_tex(vigTex, 0, 0, SB_W, SB_H, BLACK_A((int)(96 * k))); }
static void prev_strip(double t, double from, float a) {
  if (a <= 0.01f || !prev_enabled() || !sp_active()) return;
  /* a conveyor, one place a press: whatever the film, its length or its previews' spacing, each press slides the row
     exactly one picture over (0.24 s, ease out), the one reaching the middle growing into it, the one leaving it
     shrinking back; nothing else moves or starts again while the scrub goes on. The places are the presses: the middle
     the scrub's time, each neighbour one step before or after it. Presses quicker than the slide catch it up, never
     more than 1.5 places behind. */
  double sl = sp_slot(); if (sl <= 0) sl = 10;
  float k = ease_apply(EASE_OUT, clampf(seekK.v, 0, 1)), cw = fminf(SB_W * 0.40f, 800);
  static float asp = 0.5625f; static double cvFrom, cvAt, lastT, stepT; static int cvN, cvOn;
  double now = now_();
  #define CV_POS() (cvFrom + (cvN - cvFrom) * ease_apply(EASE_OUT, clampf((float)((now - cvAt) / 0.24), 0, 1)))
  if (!cvOn || seekK.v < 0.05f) { cvN = 0; cvFrom = 0; cvAt = now - 1; lastT = t; stepT = atof(store_get("seekStep", "10")); if (stepT < 1) stepT = 10; cvOn = 1; } /* (opening: where it is, no slide in) */
  else if (fabs(t - lastT) > 0.05) { int dir = t > lastT ? 1 : -1; double cur = CV_POS(); stepT = fabs(t - lastT); lastT = t; cvN += dir;
    if (fabs(cvN - cur) > 1.5) cur = cvN - dir * 1.5; cvFrom = cur; cvAt = now; }
  double pos = CV_POS();
  #undef CV_POS
  SpPic fm; if (sp_get_cell(t, -1, 1, &fm) && fm.w > 0) { float r = (float)fm.h / fm.w; asp = r > 0.62f ? 0.62f : r; }
  float ch = cw * asp, cx = SB_W / 2, cy = SB_H * 0.40f + 18 * (1 - k), pitch = cw * 0.89f + 26, rc = 30; /* (the neighbours 0.78 of the middle, 26 px apart) */
  int pc = sp_progress(); if (pc < 0) pc = 0;
  static struct { int i; double at, ct; SpPic pic, was; } seen[12]; /* each place: the picture it shows, and the one it shows less of as a nearer one comes in (0.3 s) */
  if (seekK.v < 0.05f) for (int q = 0; q < 12; q++) seen[q].i = INT32_MIN;
  gfx_opacity_push(a * k); /* (the whole strip, its middle picture and time too, in and out together) */
  int lo = (int)floor(pos) - 2, hi = (int)ceil(pos) + 2, ord[8], no = 0;
  for (int i = lo; i <= hi && no < 8; i++) if (fabs(i - pos) < 1.75) ord[no++] = i;
  for (int x = 0; x < no; x++) for (int y = x + 1; y < no; y++) if (fabs(ord[y] - pos) > fabs(ord[x] - pos)) { int w = ord[x]; ord[x] = ord[y]; ord[y] = w; } /* the farthest first, the middle on top */
  for (int o = 0; o < no; o++) { int i = ord[o]; double ct = t + (i - cvN) * stepT; if (ct < -stepT * 0.5 || ct > dur() + stepT * 0.5) continue; if (ct < 0) ct = 0;
    float d = (float)(i - pos), ad = fabsf(d), g = ad > 1 ? 1 : ad, e = g * g * (3 - 2 * g); /* 0 the middle .. 1 a neighbour */
    float sc = 1 - 0.22f * e, al = ad > 1 ? clampf((1.75f - ad) / 0.75f, 0, 1) : 1;
    int tint = 255 - (int)(78 * e); /* (the neighbours a little darker, never see-through) */
    float w = cw * sc, h = ch * sc, x = cx + d * pitch * (0.75f + 0.25f * k) - w / 2, y = cy - h / 2, r = rc * (1 - 0.2f * e);
    if (x + w < 0 || x > SB_W || al <= 0.005f) continue;
    /* the picture made nearest the place: always a real one (never a loader); a nearer one coming in crossfades over it */
    SpPic m; int has = sp_get_cell(ct, -1, i == cvN, &m) && m.w > 0;
    int q = 0, free_ = -1; for (; q < 12; q++) { if (seen[q].i == i && fabs(seen[q].ct - ct) < 0.5) break; if (free_ < 0 && (seen[q].i == INT32_MIN || abs(seen[q].i - cvN) > 3)) free_ = q; }
    if (!has && (q == 12 || seen[q].pic.w <= 0 || !(sp_get_cell(seen[q].pic.t, 0.05, i == cvN, &m) && m.w > 0 && m.k == seen[q].pic.k))) continue; /* (its picture still being unpacked: the place keeps the one it showed; a place that never had one waits, the middle first) */
    if (q == 12) { q = free_ >= 0 ? free_ : 0; seen[q].i = i; seen[q].ct = ct; seen[q].pic = m; seen[q].at = 0; }
    else if (seen[q].pic.k != m.k && stripHold && now - stripMovedAt < 0.35) { seen[q].was = seen[q].pic; seen[q].pic = m; seen[q].at = now; } /* (a nearer one while the row moves; stopped, what shows stays) */
    else if (seen[q].pic.k != m.k) { SpPic o; if (sp_get_cell(seen[q].pic.t, 0.05, i == cvN, &o) && o.k == seen[q].pic.k) m = o; }
    float pa = seen[q].at == 0 ? 1 : clampf((float)((now - seen[q].at) / 0.3), 0, 1); pa = pa * pa * (3 - 2 * pa);
    gfx_opacity_push(al);
    if (ad < 0.5f) gfx_shadow(x, y + 16 * sc, w, h, r, 56, BLACK_A((int)(170 * (1 - e))));
    gfx_rrect(x, y, w, h, r, RGBA(14, 12, 20, 255)); /* (solid behind: nothing of the film through a picture as it crossfades) */
    uint32_t tc = RGBA(tint, tint, tint, 255);
    if (pa < 0.999f) { SpPic o; if (sp_get_cell(seen[q].was.t, 0.05, 0, &o) && o.k == seen[q].was.k) gfx_image(o.tex, x, y, w, h, r, tc, 0, o.v0, o.u1, o.v1); } /* (asked again each frame: its texture kept, never another picture's) */
    if (pa < 0.999f) gfx_opacity_push(pa); gfx_image(m.tex, x, y, w, h, r, tc, 0, m.v0, m.u1, m.v1); if (pa < 0.999f) gfx_opacity_pop();
    gfx_ring(x, y, w, h, r, 1.2f, WHITE_A((int)(34 * (1 - 0.4f * e))));
    gfx_opacity_pop(); }
  float fx = cx - cw / 2, fy = cy - ch / 2;
  char tt[24]; fmt_time(t, tt, sizeof tt); float ty = fy + ch + 20; (void)fx; text_draw(tt, cx, ty, 42, 800, WHITE_A(250), -0.5f, 0, ALIGN_CENTER); ty += text_line_height(42);
  { double dt = t - from; char dl[24], sg[32]; fmt_time(fabs(dt), dl, sizeof dl); snprintf(sg, sizeof sg, "%s%s", dt < 0 ? "\xe2\x88\x92" : "+", dl); text_draw(sg, cx, ty + 2, 24, 700, WHITE_A(170), 0, 0, ALIGN_CENTER); ty += text_line_height(24) + 4; }
  { static double at90, scrubAt; static int doneHere; if (scrubAt != seekStartAt) { scrubAt = seekStartAt; at90 = pc >= 90 ? 1 : 0; doneHere = pc >= 100 && ready_seen(P.vid); } if (pc >= 90 && !at90) at90 = now; else if (pc < 90) at90 = 0; /* (under 90 %: it stays while the scrub goes on; from 90 %: it goes after a moment, 1.5 s after reaching it at most) */
    if (pc >= 100 && !doneHere) ready_mark(P.vid); /* ("Previews ready" shows once for a video, the scrub it is first seen on; never again after, this run or the next) */
    double goAt = doneHere ? -1e18 : at90 == 0 ? 1e18 : fmax(seekStartAt + 3.5, at90 == 1 ? 0 : at90 + 1.5);
    float ha = clampf(1 - (float)(now - goAt) / 0.6f, 0, 1); if (ha > 0.01f) { /* how much has its pictures ("Previews 64%", "Previews ready"); it leaves with the strip (inside its fade) */
      char n[48], pn[8]; snprintf(pn, sizeof pn, "%d", pc); snprintf(n, sizeof n, "%s", pc >= 100 ? T("Previews ready") : T1("Previews {n}%", "n", pn)); float tw = text_width(n, 28, 700, 0), r = 13, w = 2 * r + 12 + tw + 36, h = 50, gx = cx - w / 2, gy = fy - 40 - h; (void)ty; /* (above the middle picture: clear of the time, the bar and the subtitles) */
      gfx_opacity_push(ha); gfx_rrect(gx, gy, w, h, h / 2, BLACK_A(120)); gfx_ring(gx, gy, w, h, h / 2, 1, WHITE_A(30)); float rx = gx + 18, ry = gy + h / 2;
      gfx_ring(rx, ry - r, 2 * r, 2 * r, r, 3, WHITE_A(45)); int seg = 48; for (int q = 0; q < seg * pc / 100; q++) { float an = -1.5708f + 6.2832f * q / seg; gfx_circle(rx + r + cosf(an) * (r - 1.5f), ry + sinf(an) * (r - 1.5f), 1.8f, ACC2 | 0xff000000u); }
      text_draw(n, rx + 2 * r + 12, ry - text_line_height(28) / 2.0f, 28, 700, WHITE_A(235), 0, 0, ALIGN_LEFT); gfx_opacity_pop(); } }
  gfx_opacity_pop(); }
static void prev_pic(double t, float cx, float bottom, float a) { /* the pointer over the bar: one picture over its time */
  SpPic m; if (a <= 0.01f || !prev_enabled() || !sp_get(t, &m) || m.w <= 0) return; float w = 320, h = w * m.h / m.w; if (h > w * 0.62f) h = w * 0.62f;
  float x = clampf(cx - w / 2, 72, SB_W - 72 - w), y = bottom - 12 - h; gfx_opacity_push(a); gfx_shadow(x, y + 10, w, h, 16, 34, BLACK_A(150)); gfx_image(m.tex, x, y, w, h, 16, WHITE_A(255), 0, m.v0, m.u1, m.v1); gfx_ring(x, y, w, h, 16, 1, WHITE_A(30)); gfx_opacity_pop(); }
/* the CC button of a trailer: the subtitles icon, and when they are off a slash drawn from its top-left (0.26 s) with a gap cut around it */
static void cc_icon(float x, float y, float sz, uint32_t col, uint32_t gap) {
  float k = P.ccS.v, u = sz / 24; icon_draw("p_subs", x, y, sz, col_alpha(col, k > 0.5f && col == WHITE_A(255) ? 0.8f : 1));
  if (k <= 0.001f) return;
  float g = (2.6f + 17.68f * k) * u; gfx_clip_push(x, y, g, g); icon_draw("p_ccgap", x, y, sz, gap); gfx_clip_pop();
  float c = (3.4f + 17.68f * k + 1) * u; gfx_clip_push(x, y, c, c); icon_draw("p_ccslash", x, y, sz, col); gfx_clip_pop();
}
static void draw_osd(void) {
  { float t = (float)now_(); lglass_light(0.018f * sinf(t * 0.157f) + 0.012f * sinf(t * 0.263f + 1.3f) + 0.008f * sinf(t * 0.411f + 4.1f)); } /* the glass's light wandering, slowly and a little */
  float W = SB_W, H = SB_H, bx = 72, rowTop = H - 54 - 94, L25 = text_line_height(25), barTop = rowTop - 30 - L25, trkC = barTop + L25 / 2, ttlBot = barTop - 32;
  bake();
  /* the shades: the bottom for the controls, the top for the clock, the vignette when paused */
  if (P.dim.v > 0.001f) { gfx_opacity_push(P.dim.v); gfx_rrect_v(0, H * 0.44f, W, H * 0.56f * 0.54f, 0, BLACK_A(0), BLACK_A(140)); gfx_rrect_v(0, H * 0.44f + H * 0.56f * 0.54f, W, H * 0.56f * 0.46f, 0, BLACK_A(140), BLACK_A(224)); gfx_opacity_pop(); }
  if (P.dimT.v > 0.001f) { gfx_opacity_push(P.dimT.v); gfx_rrect_v(0, 0, W, 240, 0, BLACK_A(140), BLACK_A(0)); gfx_opacity_pop(); }
  if (P.vig.v > 0.001f) gfx_alpha_tex(vigTex, 0, 0, W, H, BLACK_A((int)(255 * P.vig.v)));
  draw_subs();
  /* buffering: a quarter of the ring turning */
  if (P.bufA.v > 0.001f) { gfx_opacity_push(P.bufA.v); float cx = W / 2, cy = H / 2 - 62 + 40; gfx_ring(cx - 40, cy - 40, 80, 80, 40, 5, WHITE_A(36));
    float ph = clock_ph(0.9) * 6.2832f; for (int i = 0; i <= 20; i++) { float an = ph - 2.356f + 1.5708f * i / 20; gfx_circle(cx + cosf(an) * 37.5f, cy + sinf(an) * 37.5f, 2.5f, ACC2); }
    xline_draw(&stX[1], status, cx, cy + 40 + 22, 24, WHITE_A(204), 0, ALIGN_CENTER); gfx_opacity_pop(); }
  /* top left, as the film starts, for 6 s (under the peers chip while the controls show it): one glass pill like the peers chip, the age in a small
     accent badge, then what for, each with three small dots for how much; the reasons come in one after the other */
  if (P.pgA.v > 0.001f) { float x = 72 + P.pgY.v, y = 52 + (46 + 14) * P.p2pA.v, h = 52, t = (float)(now_() - P.pg.at); char ag[40]; if (P.pg.age > 0) snprintf(ag, sizeof ag, "%d+", P.pg.age); else snprintf(ag, sizeof ag, "%s", T("All ages"));
    static float nw[PG_CATS]; static unsigned lg; if (lg != i18n_gen() || !nw[0]) { lg = i18n_gen(); for (int c = 0; c < PG_CATS; c++) nw[c] = text_width(parental_cat_name(c), 22, 600, 0); }
    float bw = text_width(ag, 25, 800, 0) + 22, /* (the age a step above the rest) */ w = 9 + bw + 16; for (int i = 0; i < P.pg.g.n; i++) w += nw[P.pg.g.cat[i]] + 9 + 2 * 10 + 7 + (i + 1 < P.pg.g.n ? 20 : 18); if (!P.pg.g.n) w = 9 + bw + 9; /* (the age alone) */
    gfx_opacity_push(P.pgA.v);
    if (!P.pg.g.n) { /* the age alone: one pill, the official rating beside it when there is one ("TV-MA · 16+") */
      char one[200]; int same = !P.pg.cert[0] || !strcmp(P.pg.cert, ag) || (atoi(P.pg.cert) == P.pg.age && strchr(P.pg.cert, '+')); snprintf(one, sizeof one, same ? "%s" : "%s  ·  %s", same ? ag : P.pg.cert, ag);
      if (P.pg.desc[0]) { size_t l = strlen(one); snprintf(one + l, sizeof one - l, "  ·  %s", P.pg.desc); } /* and what for, as the rating says */
      float ow = text_width(one, 25, 800, 0) + 48; lglass_shell(x, y, ow, h, h / 2, RGBA(10, 12, 22, 150), 1); text_draw(one, x + ow / 2, y + (h - text_line_height(25)) / 2, 25, 800, WHITE_A(250), 0, 0, ALIGN_CENTER);
      gfx_opacity_pop(); }
    else {
    lglass_shell(x, y, w, h, h / 2, RGBA(10, 12, 22, 150), 1);
    float bx = x + 7, by = y + 7; gfx_rrect(bx, by, bw, h - 14, (h - 14) / 2, acc_a(0.35f)); text_draw(ag, bx + bw / 2, by + (h - 14 - text_line_height(25)) / 2, 25, 800, WHITE_A(255), 0, 0, ALIGN_CENTER);
    float rx = bx + bw + 16, ry = y + (h - text_line_height(22)) / 2, cy = y + h / 2;
    for (int i = 0; i < P.pg.g.n; i++) { float k = (t - 0.25f - i * 0.12f) / 0.35f; k = k < 0 ? 0 : k > 1 ? 1 : k; k = 1 - (1 - k) * (1 - k); int c = P.pg.g.cat[i], lv = P.pg.g.lvl[i];
      if (k > 0.004f) { gfx_opacity_push(k); text_draw(parental_cat_name(c), rx, ry, 22, 600, WHITE_A(225), 0, 0, ALIGN_LEFT);
        for (int d = 0; d < 3; d++) gfx_circle(rx + nw[c] + 9 + 3.5f + d * 10, cy, 3.5f, d < lv ? (lv == PG_SEVERE ? RGB(0xf0, 0xb9, 0x4a) : WHITE_A(220)) : WHITE_A(55));
        gfx_opacity_pop(); }
      rx += nw[c] + 9 + 2 * 10 + 7 + 20; }
    gfx_opacity_pop(); } }
  /* top left, with the controls: a torrent's peers and speed */
  if (P.p2pA.v > 0.001f) { gfx_opacity_push(P.p2pA.v); static TMemo p2W; float tw = tm_width(&p2W, P.p2pLine, 24, 600, 0), w = 16 + 10 + 12 + tw + 20 + 4;
    lglass_shell(72, 52, w, 46, 23, RGBA(10, 12, 22, 150), 1); gfx_circle(72 + 2 + 16 + 5, 52 + 23, 5, RGB(0x4a, 0xde, 0x80));
    xline_draw(&stX[2], P.p2pLine, 72 + 2 + 16 + 10 + 12, 52 + (46 - text_line_height(24)) / 2, 24, WHITE_A(255), 0, ALIGN_LEFT); gfx_opacity_pop(); }
  /* top right: the clock, the pages' own (a new minute: only its changed digits crossfade), and when it ends */
  if (P.topR.v > 0.001f) { gfx_opacity_push(P.topR.v); gfx_offset_push(0, P.topY.v);
    app_clock_draw(W - 64, 46, 1);
    double d = dur(); if (d > 0 && !live_bcast()) { time_t t = time(NULL) + (time_t)(d - video_time()); /* (live: nothing ends) */
      static char s[64]; static long sMin = -1; static unsigned sLg; /* made again when its minute or the language changes */
      if ((long)(t / 60) != sMin || sLg != i18n_gen()) { sMin = (long)(t / 60); sLg = i18n_gen(); struct tm tm; localtime_r(&t, &tm); char e[16]; app_fmt_hm(tm.tm_hour, tm.tm_min, e, sizeof e); snprintf(s, sizeof s, "%s", T1("Ends at {t}", "t", e)); text_upper(s, sizeof s); }
      txt_sh(s, W - 64, 46 + text_line_height(38) / 2 + 38 + 14, 20, 600, WHITE_A(178), 1.6f, 0, ALIGN_RIGHT, 128); } /* a quiet line just under the clock's glass, in line with the time */
    gfx_offset_pop(); gfx_opacity_pop(); }
  /* bottom left: the title's art, S:E and its name, the info chips, year · genre · via addon (laid out from the bar up) */
  if (P.ttlA.v > 0.001f) { gfx_opacity_push(P.ttlA.v); gfx_offset_push(P.ttlX.v, P.ttlY.v);
    static char viaSrc[96], mt2[96]; static unsigned viaLg; /* "via addon": made when the addon or the language changes */
    if (viaLg != i18n_gen() || strcmp(viaSrc, P.saddon)) { viaLg = i18n_gen(); snprintf(viaSrc, sizeof viaSrc, "%s", P.saddon); char via[120]; snprintf(via, sizeof via, "%s", P.saddon); char *v = via; while (*v && !isalnum((unsigned char)*v)) v++; snprintf(mt2, sizeof mt2, "%s", *v ? T1("via {a}", "a", v) : ""); }
    const char *mt[3] = { P.isTrailer ? "" : P.year, P.isTrailer || !P.genre[0] ? "" : T(P.genre), P.isTrailer || !mt2[0] ? "" : mt2 };
    int hasMt = mt[0][0] || mt[1][0] || mt[2][0], hasSe = P.isTrailer || P.episode > 0; /* (an anime episode: "E5", no season) */ float L30 = text_line_height(30);
    float y = ttlBot, mtTop = 0, infTop = 0, seTop = 0;
    if (hasMt) { mtTop = y - L25; y = mtTop - 14; }
    if (P.nchip) { infTop = y - 46; y = infTop - 18; }
    if (hasSe) { seTop = y - 36; y = seTop - 18; }
    float lw, lh; GLuint t = logo_fit(560, 170, &lw, &lh); int nm = name_shown(t), nl = 0; static TMemo nmW; if (nm) { nl = tm_wrap(&nmW, P.name, 1100, 3, 60, 800, -1); lh = nl * 60 * 1.05f; }
    float lgTop = y - lh;
    if (P.pzA.v > 0.001f) { static UMemo pzU; const char *pz = um_upper(&pzU, T("Paused")); text_draw(pz, bx, lgTop - 40, 24, 700, WHITE_A((int)(199 * P.pzA.v)), 6, 0, ALIGN_LEFT); }

    if (t) { char sk[80]; snprintf(sk, sizeof sk, "logo:%s", P.id); sx_image(sk, t, bx, lgTop, lw, lh, 0, WHITE_A(255), 0, 0, 1, 1); } else if (nm) text_draw_wrapped(P.name, bx, lgTop, 1100, 3, 60, 800, WHITE_A((int)(255 * kf(P.nameAt, .45f, C_LGIN, 0))), -1, ALIGN_LEFT, 60 * 1.05f);
    if (hasSe) { float ty = seTop + (36 - L30) / 2;
      if (P.isTrailer) { static UMemo tgU; const char *tg = um_upper(&tgU, trailer_name()); text_draw(tg, bx, seTop + (36 - text_line_height(22)) / 2, 22, 700, ACC2, 6, 0, ALIGN_LEFT); }
      else { char se[16]; if (P.season >= 0) snprintf(se, sizeof se, "S%d:E%d", P.season, P.episode); else snprintf(se, sizeof se, "E%d", P.episode); float w = txt_sh(se, bx, ty, 30, 800, WHITE_A(255), 0.5f, 0, ALIGN_LEFT, 153);
        if (P.epTitle[0]) { gfx_circle(bx + w + 16 + 3.5f, seTop + 18, 3.5f, ACC2); txt_sh(P.epTitle, bx + w + 16 + 7 + 16, ty, 30, 700, WHITE_A(255), 0, 1000, ALIGN_LEFT, 153); } } }
    if (P.nchip) { float cx = bx, L24 = text_line_height(24);
      for (int i = 0; i < P.nchip; i++) { float k = kf(P.chip[i].at, .42f, C_OUT, P.chip[i].delay); if (P.chip[i].at && now_() - P.chip[i].at < P.chip[i].delay) k = 0; /* the entrance keeps its first frame through the delay */
        if (P.chip[i].sep) { cx += 4; gfx_rect(cx, infTop + 6 + 8 * (1 - k), 2, 34, WHITE_A((int)(115 * k))); cx += 2 + 16; continue; }
        static TMemo chW[8]; int b = P.chip[i].bold; float tw = tm_width(&chW[i], P.chip[i].t, 24, b ? 800 : 600, b ? 0.5f : 0), cy = infTop + 8 * (1 - k);
        float xk = P.chip[i].xAt ? clampf((float)((now_() - P.chip[i].xAt) / 0.26), 0, 1) : 1; xk = 1 - (1 - xk) * (1 - xk) * (1 - xk); if (xk >= 1) P.chip[i].xAt = 0; /* its words changing */
        if (xk < 1) tw = P.chip[i].oldW + (tw - P.chip[i].oldW) * xk;
        float w = 40 + tw + (P.chip[i].icon ? 36 : 0);
        if (k > 0.001f) { gfx_opacity_push(k); gfx_rrect(cx, cy, w, 46, 23, RGBA(10, 12, 22, 140)); gfx_ring(cx, cy, w, 46, 23, 2, WHITE_A(56)); float tx = cx + 20, ty = cy + (46 - L24) / 2;
          if (P.chip[i].icon) { icon_draw(P.chip[i].icon, tx, cy + 10, 26, WHITE_A(230)); tx += 36; }
          if (xk < 1) { gfx_clip_push(cx, cy, w, 46); text_draw(P.chip[i].old, tx, ty - 6 * xk, 24, b ? 800 : 600, WHITE_A((int)(255 * (1 - xk))), b ? 0.5f : 0, 0, ALIGN_LEFT); text_draw(P.chip[i].t, tx, ty + 6 * (1 - xk), 24, b ? 800 : 600, WHITE_A((int)(255 * xk)), b ? 0.5f : 0, 0, ALIGN_LEFT); gfx_clip_pop(); }
          else text_draw(P.chip[i].t, tx, ty, 24, b ? 800 : 600, WHITE_A(255), b ? 0.5f : 0, 0, ALIGN_LEFT); gfx_opacity_pop(); }
        cx += w + 12; } }
    if (hasMt) { float x = bx; int any = 0; static TMemo dotW; float bw = tm_width(&dotW, "•", 25, 400, 0);
      for (int i = 0; i < 3; i++) { if (!mt[i][0]) continue; if (any) { txt_sh("•", x + 14, mtTop, 25, 400, WHITE_A(128), 0, 0, ALIGN_LEFT, 128); x += 14 + bw + 14; } x += txt_sh(mt[i], x, mtTop, 25, 500, WHITE_A(191), 0, 0, ALIGN_LEFT, 128); any = 1; } }
    gfx_offset_pop(); gfx_opacity_pop(); }
  /* the bar: time, buffered, the intro / recap / credits marks, the progress, the knob, the pointer's time and the seek pill */
  if (P.barA.v > 0.001f && live_title()) { gfx_opacity_push(P.barA.v); gfx_offset_push(0, P.barY.v); /* a channel: no time and no bar, LIVE alone (the Channels button wears the yellow key's colour) */
    const char *lt = T("LIVE"); float lw = text_width(lt, 22, 800, 1) + 44, lh = 10 + text_line_height(22), ly = barTop + (L25 - lh) / 2, pu = 0.5f + 0.5f * sinf((float)now_() * 3.0f);
    gfx_rrect(bx, ly, lw, lh, lh / 2, RGB(0xe0, 0x2b, 0x2b)); gfx_circle(bx + 16, ly + lh / 2, 4.5f + 3 * pu, WHITE_A((int)(80 * (1 - pu)))); gfx_circle(bx + 16, ly + lh / 2, 4.5f, WHITE_A(255)); text_draw(lt, bx + 28, ly + 5, 22, 800, WHITE_A(255), 1, 0, ALIGN_LEFT);
    gfx_offset_pop(); gfx_opacity_pop(); }
  else if (P.barA.v > 0.001f) { gfx_opacity_push(P.barA.v); gfx_offset_push(0, P.barY.v); double d = dur(); double t = P.seekT >= 0 ? P.seekT : video_time(); float p = d > 0 ? (float)(t / d) : 0; if (p < 0) p = 0; if (p > 1) p = 1;
    char c1[16], c2[18]; fmt_time(t, c1, sizeof c1); char r[16]; fmt_time(d - t, r, sizeof r); snprintf(c2, sizeof c2, "-%s", r); int f = P.mode == 1 && P.row == 1, sk = P.skOn; float th = f || sk ? 10 : 6;
    float tx = bx + 132 + 20, tw = W - 2 * bx - 2 * (132 + 20), ty = trkC - th / 2;
    int lv = live_bcast(); if (lv) p = 1; /* live: the bar full, the LIVE badge where the time left was */
    text_draw(c1, bx, barTop, 25, 600, WHITE_A(255), 0, 0, ALIGN_LEFT);
    if (lv) { const char *lt = T("LIVE"); float lw = text_width(lt, 22, 800, 1) + 44, lh = 10 + text_line_height(22), lx = W - bx - lw, ly = barTop + (L25 - lh) / 2;
      gfx_rrect(lx, ly, lw, lh, lh / 2, RGB(0xe0, 0x2b, 0x2b)); gfx_circle(lx + 16, ly + lh / 2, 4.5f, WHITE_A(255)); text_draw(lt, lx + 28, ly + 5, 22, 800, WHITE_A(255), 1, 0, ALIGN_LEFT); }
    else text_draw(c2, W - bx, barTop, 25, 600, WHITE_A(184), 0, 0, ALIGN_RIGHT);
    int chs = chapters_on() && !lv, nseg = chs ? P.nchap : 1, kc = chs ? chap_at(t) : 0;
    for (int sg = 0; sg < nseg; sg++) { /* chapters: the line in pieces, a small gap at each start; the piece being seeked through a little brighter */
      float s0 = sg && chs ? tx + tw * (float)(P.chap[sg].s / d) + 2 : tx - 1, s1 = chs && sg + 1 < nseg ? tx + tw * (float)(P.chap[sg + 1].s / d) - 2 : tx + tw + 1; if (s1 - s0 < 1) continue;
      if (chs) gfx_clip_push(s0, ty - 4, s1 - s0, th + 8);
      gfx_rrect(tx, ty, tw, th, 3, WHITE_A(51)); float be = d > 0 ? (float)(video_buffered_end() / d) : 0; if (be > p) gfx_rrect(tx, ty, tw * (be > 1 ? 1 : be), th, 3, WHITE_A(66));
      if (chs && sg == kc && (f || sk)) gfx_rrect(tx, ty, tw, th, 3, WHITE_A(34));
      if (P.skipsOk && d > 0) { struct { int has; double s, e; } segs[3] = { { P.intro.has, P.intro.s, P.intro.e }, { P.recap.has, P.recap.s, P.recap.e }, { P.outro.has, P.outro.s, P.outro.e } }; for (int i = 0; i < 3; i++) if (segs[i].has) { float a = (float)(segs[i].s / d), w = (float)((segs[i].e - segs[i].s) / d); if (w < 0.004f) w = 0.004f; gfx_rrect(tx + tw * a, ty, tw * w, th, 3, WHITE_A(128)); } }
      gfx_rrect_h(tx, ty, tw * p, th, 3, ACC, ACC2);
      if (chs) gfx_clip_pop(); }
    float kx = tx + roundf(tw * p);
    if (P.knA.v > 0.001f && !lv) { gfx_opacity_push(P.knA.v); if (f || sk) gfx_circle(kx, trkC, 15, WHITE_A(255)); else gfx_circle(kx, trkC, 9, WHITE_A(255)); gfx_opacity_pop(); }
    if (P.hvA.v > 0.001f && d > 0) { char ht[16]; fmt_time(P.hvP * d, ht, sizeof ht); float bw = text_width(ht, 24, 700, 0) + 28, bh = 14 + text_line_height(24), bx2 = tx + roundf(tw * P.hvP) - bw / 2, by2 = ty + th - 26 - bh;
      gfx_opacity_push(P.hvA.v); gfx_rrect(bx2, by2, bw, bh, 12, RGBA(14, 12, 22, 219)); gfx_ring(bx2, by2, bw, bh, 12, 1, WHITE_A(36)); text_draw(ht, bx2 + 14, by2 + 7, 24, 700, WHITE_A(255), 0, 0, ALIGN_LEFT); gfx_opacity_pop();
      if (P.seekT < 0) prev_pic(P.hvP * d, bx2 + bw / 2, by2, P.hvA.v); } /* (its picture over it) */
    if (P.skA.v > 0.001f && P.seekTxt[0]) { /* the pill rises and grows from the knob; the number rolls the way it moved */
      int chs2 = chapters_on() && !lv; char cb[64]; const char *cn = ""; float cw = 0, L20 = text_line_height(20);
      if (chs2) { int ci = chap_at(t); if (ci != P.chLast) { P.chLast = ci; P.chAt = now_(); } cn = chap_name(ci, cb, sizeof cb); cw = text_width(cn, 20, 600, 0.2f); if (cw > 520) cw = 520; } /* the chapter, faint, over the time */
      float pw = roll_layout(P.seekTxt, P.seekDir) + 32, ph = 12 + L25, pb = ty + th - 34, s = .88f + .12f * P.skS.v; if (chs2) { if (cw + 36 > pw) pw = cw + 36; ph += L20 + 2; }
      float rr = chs2 ? 18 : 20;
      gfx_opacity_push(P.skA.v * (1 - seekK.v)); gfx_scale_push(s, s, kx, pb); gfx_offset_push(0, 10 * (1 - P.skS.v)); /* (previews showing: the pill steps back, the picture tells the time) */
      gfx_rrect(kx - pw / 2, pb - ph, pw, ph, rr, RGBA(18, 15, 26, 199)); gfx_ring(kx - pw / 2, pb - ph, pw, ph, rr, 1, WHITE_A(31));
      gfx_clip_push(kx - pw / 2, pb - ph, pw, ph);
      if (chs2) { float ca = fminf(1, (float)((now_() - P.chAt) / 0.18)), ce = ca * ca * (3 - 2 * ca); /* a new chapter: its name fades up into place */
        text_draw(cn, kx, pb - ph + 7 + 4 * (1 - ce), 20, 600, WHITE_A((int)(150 * ce)), 0.2f, pw - 36, ALIGN_CENTER); roll_draw(kx, pb - ph + 7 + L20 + 2, L25, WHITE_A(240)); }
      else roll_draw(kx, pb - ph + 6, L25, WHITE_A(240));
      gfx_clip_pop();
      gfx_offset_pop(); gfx_scale_pop(); gfx_opacity_pop();
      } /* (with previews, the pictures in the middle take over: drawn after the controls) */
    gfx_offset_pop(); gfx_opacity_pop(); }
  /* the buttons: one accent pill slides under the focused one and its name fades in */
  if (P.btnA.v > 0.001f) { gfx_opacity_push(P.btnA.v); gfx_offset_push(nudge_x(&P.nudge) * (P.mode == 1 && P.row == 0), P.btnY.v);
    int fi = P.mode == 1 && P.row == 0 ? P.bIdx : -1; if (P.mode == 2 && P.tray.kind) { int ak = P.tray.kind < 0 ? -P.tray.kind : P.tray.kind; fi = btn_index(ak == 1 || ak == 5 ? "eps" : ak == 6 ? "chn" : ak == 2 ? "src" : ak == 3 ? "aud" : "subs"); }
    float fa = P.focA.v * fminf(1, kf(P.focFadeAt, .24f, C_EASE, 0));
    { static float sv, lx; lglass_stretch(&sv, &lx, P.focX.v, app_dt()); /* the lit button: accent glass that stretches as it glides and settles round */
      if (fa > 0.001f) { float w0 = P.focW.v, h0 = P.focH.v, sw = w0 * (1 + 0.16f * sv), sh = h0 * (1 - 0.08f * sv), px = P.focX.v + w0 / 2 - sw / 2, py = P.focY.v + h0 / 2 - sh / 2; lglass_accent(px, py, sw, sh, sh / 2, fa);
      } }
    if (P.tipA.v > 0.001f && P.tip[0]) { float ta = P.tipA.v * (P.tipAt && now_() - P.tipAt < .08 ? 0 : fminf(1, kf(P.tipAt, .3f, C_EASE, .08f)));
      int cyc = fi >= 0 && !strcmp(P.btns[fi].id, "asp") && !strcmp(P.tip, P.btns[fi].lb); /* a button that cycles: one dot per choice under its name, the lit one gliding on each press */
      if (cyc) { /* the name and its dots centred together in the pill's open part, whatever the name's length */
        float cx = P.focX.v + 70 + (P.focW.v - 70 - 24) / 2; int ai = !strcmp(P.aspect, "fill") ? 1 : !strcmp(P.aspect, "zoom") ? 2 : 0;
        text_draw(P.tip, cx, P.focY.v + 38 - text_line_height(24) / 2.0f - 8, 24, 700, col_alpha(ACC_FG, ta), 0, 0, ALIGN_CENTER);
        cycle_dots(0, 3, ai, cx, P.focY.v + 38 + text_line_height(24) / 2.0f + 2, 11, 2.5f, col_alpha(ACC_FG, .3f * ta), col_alpha(ACC_FG, ta), 0); }
      else { float ks = P.focH.v / 76.f; text_draw(P.tip, P.focX.v + 70 * ks, P.focY.v + 38 * ks - text_line_height(24) / 2.0f, 24, 700, col_alpha(ACC_FG, ta), 0, 0, ALIGN_LEFT); } } /* (the big button's name: past its bigger icon) */
    for (int i = 0; i < P.nbtn; i++) { float sz = P.btns[i].w, x = P.btns[i].x, y = P.btns[i].y; int f = i == fi;
      if (!f) lglass_shell(x, y, sz, sz, sz / 2, RGBA(16, 16, 24, 90), 0.85f); /* glass at rest */
      float isz = P.btns[i].big ? 48 : 34, ix = x + sz / 2 - isz / 2, iy = y + sz / 2 - isz / 2; uint32_t ic = f ? ACC_FG : WHITE_A(255);
      if (!strcmp(P.btns[i].id, "cc")) { cc_icon(ix, iy, isz, ic, f ? ACC : RGBA(30, 30, 34, 235)); continue; }
      icon_draw(!strcmp(P.btns[i].id, "pp") ? (paused() ? "p_play" : "p_pause") : P.btns[i].icon, ix, iy, isz, ic); }
    gfx_offset_pop(); gfx_opacity_pop(); }
  /* the prompts: the skip pill, the credits buttons, the next-up card */
  PG.prompt = 0; const PPrompt *pr = P.prompt.k ? &P.prompt : &P.shown;
  if (P.prA.v > 0.001f && pr->k) { float a = P.prA.v; gfx_opacity_push(a); gfx_offset_push(nudge_x(&P.nudge) * (P.mode == 3), P.prY.v); float pb = P.prB.v, rm = 72 + 40 * fminf(1, fmaxf(0, (440 - pb) / 336)); /* off the edges: further in alone, in line with the buttons when they show */ PG.prompt = a > 0.5f && P.prompt.k; PG.card = 0; PG.npr = 0;
    if (pr->k == 3 && (P.next.has || P.coll.has)) { int nxC = !P.next.has; const char *nxOv = nxC ? "" : P.next.ov, *nxSe = nxC ? P.coll.part : P.next.se, *nxTi = nxC ? P.coll.name : P.next.title, *nxTh = nxC ? P.coll.bg : P.next.thumb, *nxLab = nxC ? T("Next in the collection") : T("Next episode"), *nxKey = nxC ? T("Next film") : T("Next episode"); /* (a film in a collection: the next one) */ int cmp = !strcmp(store_get("nextCard", "large"), "compact"); /* Settings > Playback: a smaller card, without the episode's summary */
      const char *th = nxTh[0] ? nxTh : P.bg; NextCardSpec nc = { nxLab, nxKey, nxSe, nxTi, nxOv, th, cmp ? 1.f : 0.f, -1, 0 };
      if (pr->autoN) { nc.autoK = fill_ease((now_() - pr->t0) / 10); nc.autoLeft = (int)(10 - (now_() - pr->t0) + 0.999); } /* (counting down to it: the bar under its picture, the seconds beside its name) */
      float cx, cy, cw, ch; ui_next_card(W - rm, H - pb, 1, &nc, &cx, &cy, &cw, &ch); /* ui/prompts.c: the same drawing Settings shows small */
      PG.card = 1; PG.prX[0] = cx; PG.prY[0] = cy; PG.prW[0] = cw; PG.prH[0] = ch; }

    else { float x = W - rm; int focus = P.mode == 3 || (P.mode == 0 && !pr->big); for (int i = pr->nbtn - 1; i >= 0; i--) { const char *ic = !strcmp(pr->b[i].id, "next") ? "p_next" : !strcmp(pr->b[i].id, "skip") ? "p_skip" : NULL; float w = ui_pill_width(1, pr->b[i].lb, ic), h = ui_pill_height(1); x -= w; float y = H - pb - h; int f = focus && i == pr->idx;
        int cnt = (pr->k == 1 || pr->k == 2) && pr->autoN; /* skip intro / recap counting down: the accent fills it from the left behind the words, then it skips */
        ui_pill(x, y, w, 1, pr->b[i].lb, ic, f && !cnt, cnt ? fill_ease((now_() - pr->t0) / pr->adur) : -1, pr->autoN && !strcmp(pr->b[i].id, "next") ? fill_ease((now_() - pr->t0) / 10) : -1); /* ui/prompts.c: glass, lit in the accent when focused; counting down, the accent growing from its left end */
        if (i < 2) { PG.prX[i] = x; PG.prY[i] = y; PG.prW[i] = w; PG.prH[i] = h; if (PG.npr < i + 1) PG.npr = i + 1; }
        x -= 16; } }
    gfx_offset_pop(); gfx_opacity_pop(); }
  dly_draw(); pos_draw(); if (P.mode != 6) live_card_draw(); if (hub_player_mode()) hub_draw(); /* (the hub opened from Home is the page's: never drawn again over the picture) */ lglass_light(0);
}
/* a countdown's fill: eases in and out (sine), so it starts and lands softly */
static float fill_ease(double t) { if (t <= 0) return 0; if (t >= 1) return 1; return (float)(0.5 - 0.5 * cos(t * 3.14159265358979)); }
/* ---- the tray: sized by what it shows (never more than 60 % of the width), glass, over the buttons' side ---- */
static float item_w(TItem *it, int ak) {
  if (it->ww > 0) return it->ww; /* measured on the first frame it shows */
  if (it->k == 0) return it->ww = 20 + text_width(it->lb, 24, 400, 0) + (it->spin ? 34 : 0);
  if (it->k == 2) return 336; if (it->k == 3) return 400;
  float tw = text_width(it->lb, 27, 700, 0), sw = it->sub[0] ? text_width(it->sub, 23, 600, 0) : 0; /* (a chip's two lines: the wider one) */
  float w = 20 + (it->tool ? 0 : 36) + (tw > sw ? tw : sw) + (it->vl[0] ? 16 + text_width(it->vl, 22, it->vwt ? it->vwt : 700, 0) : 0) + (it->sw ? 14 + 22 : 0) + 28; (void)ak;
  return it->ww = w < 240 ? 240 : w;
}
/* the "Playing" tag, upper-cased and measured once per language */
static const char *playing_tag(float *nw) { static char now[32]; static float w; static unsigned lg; if (lg != i18n_gen()) { lg = i18n_gen(); snprintf(now, sizeof now, "%s", T("Playing")); text_upper(now, sizeof now); w = 26 + text_width(now, 20, 800, 1); } *nw = w; return now; }
static float item_h(TItem *it) {
  if (it->k == 0) return 36 + text_line_height(24);
  if (it->k == 3) return 196;
  if (it->k == 2) { if (!it->hh) { int n = it->ov[0] ? text_wrap_count(it->ov, 300, 2, 22, 400, 0) : 0; it->hh = 189 + 14 + 30 + (it->sub[0] ? 4 + text_line_height(22) : 0) + (n ? 8 + n * 30 : 0) + 16; } return it->hh; }
  return 22 + text_line_height(27) + 2 + text_line_height(23) + 20; /* a chip: every one as tall as one with two lines */
}
static void draw_card(TItem *it, float x, float y, float w, float h, int f) {
  int on = it->on; uint32_t base = f ? ((it->k == 2 || (it->k == 3 && on) || it->k == 4) ? WHITE_A(18) : 0) : WHITE_A(11); /* rows: .045; focused: the plate shows through (.07 on some kinds) */
  if (it->k == 0) { float tx = x + 10; if (it->spin) { float cx = x + 10 + 11, cy = y + 18 + 13; gfx_ring(cx - 11, cy - 11, 22, 22, 11, 3, WHITE_A(51)); float ph = clock_ph(0.9) * 6.2832f; for (int i = 0; i <= 8; i++) { float an = ph - 2.356f + 1.5708f * i / 8; gfx_circle(cx + cosf(an) * 9.5f, cy + sinf(an) * 9.5f, 1.5f, ACC2); } tx += 34; } text_draw(it->lb, tx, y + 18, 24, 400, WHITE_A(184), 0, 0, ALIGN_LEFT); return; }
  if (base) gfx_rrect(x, y, w, h, 20, base); if (it->k == 3 && on && !f) gfx_ring(x, y, w, h, 20, 2, acc_a(0.7f));
  if (it->k == 2 && it->hid && (spoil_blurable(it->th) || spoil_blurable(P.bg))) { float th = 189; gfx_clip_push(x, y, w, th); spoil_picture(spoil_blurable(it->th) ? it->th : P.bg, x, y, w, th + 20, 20); /* (no still of its own: the show's backdrop blurred) */ gfx_clip_pop(); spoil_mark(x + w / 2, y + th / 2, 54, 0, 0); /* not watched yet: its still soft and dark, its words kept back when asked */
    float ty = y + th + 14; text_draw(it->lb, x + 18, ty + (30 - text_line_height(24)) / 2, 24, 700, WHITE_A(255), 0, w - 36, ALIGN_LEFT); ty += 30; if (it->sub[0]) { text_draw(it->sub, x + 18, ty + 4, 22, 600, WHITE_A(184), 0, w - 36, ALIGN_LEFT); ty += 4 + text_line_height(22); }
    if (it->ov[0] && spoil_mode() < 2) text_draw_wrapped(it->ov, x + 18, ty + 8 + (30 - text_line_height(22)) / 2, w - 36, 2, 22, 400, WHITE_A(191), 0, ALIGN_LEFT, 30); return; }
  if (it->k == 2) { float th = 189; GLuint t = it->th[0] ? img_get_shaped(it->th, (int)w, (int)th + 20, 20, 1) : 0; int noStill = !t && (!it->th[0] || img_shaped_failed(it->th, (int)w, (int)th + 20, 20, 1));
    int upBlur = noStill && it->v && ep_unaired(it->v) && spoil_blurable(P.bg); /* not aired, no still: the show's backdrop blurred, Upcoming on it (as the title page) */
    if (noStill && !upBlur && P.bg[0]) t = img_get_shaped(P.bg, (int)w, (int)th + 20, 20, 1);
    gfx_clip_push(x, y, w, th); if (upBlur) { spoil_picture(P.bg, x, y, w, th + 20, 20); spoil_mark(x + w / 2, y + th / 2, 54, 0, 0); } else ph_picture(t, x, y, w, th + 20, 20, WHITE_A(255)); /* (the eye as on hidden ones) */
    if (upBlur) { static char ut[64]; static float uw; static unsigned lg; if (lg != i18n_gen() || !ut[0]) { lg = i18n_gen(); snprintf(ut, sizeof ut, "%s", T("Upcoming")); uw = text_width(ut, 20, 600, 0.2f) + 46; }
      float uh = 12 + text_line_height(20), ux = x + 12, uy = y + 12; gfx_rrect(ux, uy, uw, uh, uh / 2, RGBA(14, 12, 20, 178)); gfx_rrect_v(ux, uy, uw, uh, uh / 2, WHITE_A(26), WHITE_A(6)); gfx_ring(ux, uy, uw, uh, uh / 2, 1, WHITE_A(46));
      gfx_circle(ux + 19, uy + uh / 2, 4.5f, ACC2); text_draw(ut, ux + 32, uy + 6, 20, 600, WHITE_A(245), 0.2f, 0, ALIGN_LEFT); } /* the title page's tag, a size down */ if (it->v && !t) spoil_reveal_wait(js_str(it->v, "id", "")); if (it->v) spoil_reveal_over(js_str(it->v, "id", ""), it->th, x, y, w, th + 20, 20, x + w / 2, y + th / 2, 54); gfx_clip_pop(); /* rounded at the top only: the lower corners are cut square (just shown clear: its blur fading off) */
    if (on) { float nw; const char *now = playing_tag(&nw); gfx_rrect(x + 12, y + 12, nw, 8 + text_line_height(20), 11, ACC); text_draw(now, x + 12 + 13, y + 12 + 4, 20, 800, ACC_FG, 1, 0, ALIGN_LEFT); }
    float ty = y + th + 14; text_draw(it->lb, x + 18, ty + (30 - text_line_height(24)) / 2, 24, 700, WHITE_A(255), 0, w - 36, ALIGN_LEFT); ty += 30; if (it->sub[0]) { text_draw(it->sub, x + 18, ty + 4, 22, 600, WHITE_A(184), 0, w - 36, ALIGN_LEFT); ty += 4 + text_line_height(22); }
    if (it->ov[0]) text_draw_wrapped(it->ov, x + 18, ty + 8 + (30 - text_line_height(22)) / 2, w - 36, 2, 22, 400, WHITE_A(191), 0, ALIGN_LEFT, 30); return; }
  if (it->k == 3) { gfx_clip_push(x, y, w, h); int n = text_draw_wrapped(it->lb, x + 22, y + 20, w - 44, 2, 24, 700, WHITE_A(255), 0, ALIGN_LEFT, 24 * 1.25f); if (it->ov[0]) text_draw_wrapped(it->ov, x + 22, y + 20 + n * 24 * 1.25f + 8, w - 44, 3, 22, 400, WHITE_A(191), 0, ALIGN_LEFT, 30);
    if (on) { float nw; const char *now = playing_tag(&nw); gfx_rrect(x + w - 16 - nw, y + h - 14 - 8 - text_line_height(20), nw, 8 + text_line_height(20), 11, ACC); text_draw(now, x + w - 16 - nw + 13, y + h - 14 - 4 - text_line_height(20), 20, 800, ACC_FG, 1, 0, ALIGN_LEFT); } gfx_clip_pop(); return; }
  /* a chip (audio): tick, name, its language, value */
  float tx = x + 20; if (!it->tool) { if (on) icon_draw("check", tx + 1, y + h / 2 - 11, 22, col_alpha(ACC2, TM.rowChk.v)); tx += 24 + 12; }
  float right = x + w - 26; if (it->vl[0]) { if (it->vw <= 0) it->vw = text_width(it->vl, 22, it->vwt ? it->vwt : 700, 0); float vw = it->vw; text_draw(it->vl, right, y + h / 2 - text_line_height(22) / 2.0f, 22, it->vwt ? it->vwt : 700, ACC3, 0, 0, ALIGN_RIGHT); right -= vw + 16; }
  if (it->sw) { gfx_circle(right - 11, y + h / 2, 11, it->sw); gfx_ring(right - 22, y + h / 2 - 11, 22, 22, 11, 2, BLACK_A(77)); right -= 22 + 14; }
  float L1 = text_line_height(27), L2 = text_line_height(23);
  if (it->sub[0]) { float ty = y + (h - L1 - 2 - L2) / 2; text_draw(it->lb, tx, ty, 27, 700, WHITE_A(255), 0, right - tx - 4, ALIGN_LEFT); text_draw(it->sub, tx, ty + L1 + 2, 23, 600, WHITE_A(173), 0, right - tx - 4, ALIGN_LEFT); }
  else text_draw(it->lb, tx, y + h / 2 - L1 / 2, 27, 700, WHITE_A(255), 0, right - tx - 4, ALIGN_LEFT);
}
/* the sources: an addon's header (its name, how many, or the spinner while it is asked; plus / minus folds it), its rows
   as tall as their words need (up to 3 lines of name, 8 of words, then an ellipsis), the lit plate gliding under the
   focus; an addon whose header comes into view is asked then */
static void draw_srcs(float ix, float top, float W) {
  (void)W; float RW = SRC_RW, vx = ix + 30, vy = top + 8, vh = SRC_VH; int fi = !P.tray.onBtn && P.tray.kind > 0 && P.tray.zone == 0 && src_focusable(SR.idx) ? SR.idx : -1;
  float want = 0; if (src_focusable(SR.idx)) { want = SR.r[SR.idx].y - 120; float mx = SR.tot - 10 - vh; if (want > mx) want = mx; if (want < 0) want = 0; } mo_to(&SR.y, want, .28f, C_OUT, 0);
  PG.src = 1; PG.srcX = vx; PG.srcTop = vy;
  if (fi >= 0) { SRow *r = &SR.r[fi]; if (SR.plA.v <= 0.001f && !SR.plA.on) { mo_set(&SR.plY, r->y); mo_set(&SR.plH, r->h); } else { mo_to(&SR.plY, r->y, .26f, C_OUT, 0); mo_to(&SR.plH, r->h, .26f, C_OUT, 0); } mo_to(&SR.plA, 1, .18f, C_EASE, 0); }
  else mo_to(&SR.plA, 0, .18f, C_EASE, 0);
  gfx_clip_push(ix, vy, RW + 60, vh); gfx_offset_push(vx, vy - SR.y.v);
  if (SR.plA.v > 0.001f) { float py = SR.plY.v, ph = SR.plH.v; gfx_opacity_push(SR.plA.v); gfx_rrect(0, py, RW, ph, 20, WHITE_A(38)); gfx_clip_push(0, py, 4, ph); gfx_rrect(0, py, RW, ph, 20, ACC2); gfx_clip_pop(); gfx_opacity_pop(); }
  const uint32_t CORAL = RGB(0xff, 0x9a, 0x7a); float LN = text_line_height(SN_PX), LD = text_line_height(SD_PX);
  for (int i = 0; i < SR.n; i++) { SRow *r = &SR.r[i]; float y = r->y, h = r->h; int f = i == fi;
    if (y - SR.y.v > vh || y + h - SR.y.v < 0) continue;
    float al = (r->k == 3 || r->k == 1) && r->g >= 0 ? P.srcs.g[r->g].ia.v : 1; if (al <= 0.004f) continue; gfx_opacity_push(al);
    if (r->k == 5) { /* an addon's name over its streams, and how many */
      char up[120]; snprintf(up, sizeof up, "%s", P.srcs.g[r->g].name); text_upper(up, sizeof up); float ty = y + (h - text_line_height(20)) / 2;
      text_draw(up, 22, ty, 20, 800, WHITE_A(160), 1.6f, RW - 160, ALIGN_LEFT);
      if (P.srcs.g[r->g].st == 2) { char n[12]; snprintf(n, sizeof n, "%d", src_count(r->g)); text_draw(n, RW - 22, ty, 20, 700, WHITE_A(120), 0, 0, ALIGN_RIGHT); } }
    else if (r->k == 1) { gfx_rrect(0, y, RW, h, 20, WHITE_A(9)); ph_text(22, y + 16, (RW - 44) * (r->i ? 0.34f : 0.46f), 1, SN_PX, SN_LH, ALIGN_LEFT); ph_text(22, y + 16 + SN_LH + 4, (RW - 44) * (r->i ? 0.62f : 0.78f), 1, SD_PX, SD_LH, ALIGN_LEFT); }
    else if (r->k == 0) text_draw(T("No streams found."), 22, y + (h - text_line_height(25)) / 2, 25, 400, WHITE_A(191), 0, RW - 44, ALIGN_LEFT);
    else { /* a stream: its facts in the accent and what it is on the right; its release under it; what matters here in coral */
      const Stream *st = &P.srcs.g[r->g].items[r->i]; const BestFacts *bf = &P.srcs.g[r->g].f[r->i]; int on = r->g == srcPG && r->i == srcPI;
      if (bf->out && !f) gfx_opacity_push(0.5f);
      if (!f) gfx_rrect(0, y, RW, h, 20, WHITE_A(11)); if (on && !f && r->k != 6) gfx_ring(0, y, RW, h, 20, 2, acc_a(0.7f));
      float l1 = y + 16 + (SN_LH - LN) / 2, l2 = y + 16 + SN_LH + 4 + (SD_LH - LD) / 2, right = RW - 22;
      if (on && r->k != 6) { float nw; const char *now = playing_tag(&nw); gfx_rrect(right - nw, y + 16 + SN_LH / 2 - (8 + text_line_height(20)) / 2, nw, 8 + text_line_height(20), 11, ACC); text_draw(now, right - nw + 13, y + 16 + SN_LH / 2 - (8 + text_line_height(20)) / 2 + 4, 20, 800, ACC_FG, 1, 0, ALIGN_LEFT); right -= nw + 16; }
      const char *nm = st->name[0] ? st->name : st->addon; char first[200]; { const char *e = strchr(nm, '\n'); snprintf(first, sizeof first, "%.*s", e ? (int)(e - nm) : (int)strlen(nm), nm); }
      if (r->k == 6 || r->k == 7) { /* pinned: what it is, then its facts and size */
        const char *lb = r->k == 6 ? T("Now playing") : T("Best fit for this TV"); icon_draw("play", 22, y + 16 + (SN_LH - 26) / 2, 26, ACC3);
        text_draw(lb, 22 + 36, l1, SN_PX, 700, ACC3, 0.2f, right - 22 - 36 - 10, ALIGN_LEFT);
        char ln[300]; snprintf(ln, sizeof ln, "%s%s%s", bf->facts[0] ? bf->facts : first, bf->meta[0] ? "  \xc2\xb7  " : "", bf->meta); text_draw(ln, 22, l2, SD_PX, 500, WHITE_A(210), 0, RW - 44, ALIGN_LEFT); }
      else {
        float mw = bf->meta[0] ? text_width(bf->meta, SD_PX, 600, 0) : 0; if (mw) text_draw(bf->meta, right, l1 + (LN - LD) / 2, SD_PX, 600, WHITE_A(185), 0, 0, ALIGN_RIGHT);
        text_draw(bf->facts[0] ? bf->facts : first, 22, l1, SN_PX, 700, bf->facts[0] ? ACC3 : WHITE_A(240), 0.2f, right - 22 - (mw ? mw + 24 : 0), ALIGN_LEFT);
        char rl[300]; snprintf(rl, sizeof rl, "%s", bf->rel[0] ? bf->rel : bf->facts[0] ? first : st->addon); text_draw(rl, 22, l2, SD_PX, 400, WHITE_A(155), 0, RW - 44, ALIGN_LEFT); }
      if (bf->warn[0]) { float wy = y + 16 + SN_LH + 4 + SD_LH + 4; icon_draw("minus", 22, wy + (SD_LH - 22) / 2, 22, CORAL); text_draw(bf->warn, 22 + 32, wy + (SD_LH - text_line_height(22)) / 2, 22, 700, CORAL, 0, RW - 44 - 32, ALIGN_LEFT); }
      if (bf->out && !f) gfx_opacity_pop(); }
    gfx_opacity_pop(); }
  gfx_offset_pop(); gfx_clip_pop();
  /* more above or below: the list's edges fade */
  if (SR.y.v > 2) gfx_rrect_v(ix, vy, RW + 60, 36, 0, RGBA(18, 16, 28, 200), RGBA(18, 16, 28, 0));
  if (SR.tot - 10 - SR.y.v > vh + 2) gfx_rrect_v(ix, vy + vh - 36, RW + 60, 36, 0, RGBA(18, 16, 28, 0), RGBA(18, 16, 28, 200));
}
static float CP = 282, CPW = 232; /* the subtitle columns' pitch and their entries' width: wider when there are only two */
static float col_item_h(TItem *it) { if (it->tool && it->sub[0]) { if (it->hh <= 0) it->hh = 24 + 23 * 1.25f + 2 + 26 * text_wrap_count(it->sub, 6 + CPW - 18 - 24, 2, 21, 600, 0); return it->hh; }
  if (it->k == 0) { if (it->hh <= 0) it->hh = 24 + 28 * text_wrap_count(it->lb, CPW - 8, 4, 23, 400, 0); return it->hh; }
  return it->k == 1 ? 58 : it->tool ? 30 + 23 * 1.25f : it->sub[0] ? 24 + 24 * 1.25f + 2 + text_line_height(21) : 30 + 24 * 1.25f; }
static void draw_tray(void) {
  int ak = P.tray.kind < 0 ? -P.tray.kind : P.tray.kind; PG.tray = 0; PG.ntab = 0; PG.nrow = 0; PG.src = 0; if (!ak || P.tray.openA.v <= 0.001f) return;
  float L24 = text_line_height(24), L30 = text_line_height(30), L18 = text_line_height(21); /* (the columns' headings) */
  /* the header: tabs (an episode's sources: its name above them), a title, or nothing */
  float hdH = 0, hdW = 0, tabW[40], tabsW = 0;
  static TMemo tabM[40], ttlM;
  if (P.tray.ntabs) { for (int i = 0; i < P.tray.ntabs; i++) { tabW[i] = TAB_PAD + tm_width(&tabM[i], P.tray.tabs[i], TAB_FS, 700, 0); tabsW += tabW[i] + TAB_GAP; } hdW = tabsW; hdH = 28 + text_line_height(TAB_FS) + 22; if (ak == 5 || ak == 2) { hdH += L24 + 14; hdW = fmaxf(hdW, tm_width(&ttlM, P.tray.title, 24, 800, 0)); } }
  else if (P.tray.title[0]) { hdH = L30 + 18; hdW = tm_width(&ttlM, P.tray.title, 30, 800, 0); }
  float xs[MAXROW], ws[MAXROW], hs[MAXROW], rowH = 0, rowW = 0;
  if (ak != 4 && ak != 2 && ak != 5) for (int i = 0; i < P.tray.n; i++) { ws[i] = item_w(&P.tray.row[i], ak); hs[i] = item_h(&P.tray.row[i]); xs[i] = rowW; rowW += ws[i] + (P.tray.row[i].k == 0 ? 0 : 18); /* notes have no margin */ if (hs[i] > rowH) rowH = hs[i]; }
  int srcs = ak == 2 || ak == 5;
  if (ak == 4) { CP = P.tray.ncols && P.tray.ncols <= 2 ? 340 : 282; CPW = CP - 50; }
  float colH = 6 + (6 + L18 + 14) + COL_LH + 4, listH = ak == 4 ? colH : srcs ? SRC_VH : rowH;
  float W = ak == 4 ? 56 + (P.tray.ncols ? P.tray.ncols : 4) * CP - 10 /* the last column as far from the edge as the first */ : srcs ? SRC_W : fminf(SB_W * 0.6f, 2 + 60 + fmaxf(rowW, hdW)), H = 2 + 22 + hdH + 8 + listH + 8 + 20, x = SB_W - 72 - W, y;
  if (P.tray.ntabs && W < tabsW - TAB_GAP + 62) W = tabsW - TAB_GAP + 62; /* the tabs all in view: never cut */
  /* every tray takes the size of what is in it, and glides there when that changes (a group folded, a tab switched) */
  { static Mo tH, tW; static int tk; if (tk != ak || P.tray.openA.v < 0.02f) { mo_set(&tH, H); mo_set(&tW, W); tk = ak; } else { if (fabsf(tH.to - H) > 0.5f) mo_to(&tH, H, .26f, C_OUT, 0); if (fabsf(tW.to - W) > 0.5f) mo_to(&tW, W, .26f, C_OUT, 0); } static double lt; double nw = now_(); float fdt = lt ? (float)(nw - lt) : 0; lt = nw; if (fdt > 0.05f) fdt = 0.05f; mo_tick(&tH, fdt); mo_tick(&tW, fdt); H = tH.v; W = tW.v; }
  x = SB_W - 72 - W; y = SB_H - 178 - H;
  PG.tray = P.tray.kind > 0 && P.tray.openA.v > 0.5f; PG.x = x; PG.y = y; PG.w = W; PG.h = H;
  float sc = P.tray.open.v; gfx_opacity_push(P.tray.openA.v); gfx_scale_push(sc, sc, P.tray.ox, P.tray.oy);
  gfx_rrect(x, y, W, H, 28, RGBA(18, 16, 28, 204)); gfx_rrect_v(x, y, W, H, 28, WHITE_A(20), WHITE_A(5)); gfx_ring(x, y, W, H, 28, 1, WHITE_A(31));
  gfx_offset_push(nudge_x(&P.nudge) * (P.mode == 2), nudge_y(&P.nudge) * (P.mode == 2));
  float ix = x + 1, cy = y + 1 + 22;
  if (P.tray.ntabs) { if (ak == 5 || ak == 2) { text_draw(P.tray.title, ix + 30, cy, 24, 800, WHITE_A(209), 0, W - 62, ALIGN_LEFT); cy += L24 + 14; }
    float vw = W - 2 - 60, xt[40], tx = 0; for (int i = 0; i < P.tray.ntabs; i++) { xt[i] = tx; tx += tabW[i] + TAB_GAP; }
    float want = 0; if (P.tray.tab < P.tray.ntabs) { want = xt[P.tray.tab] + tabW[P.tray.tab] / 2 - vw / 2; if (want > tabsW - vw) want = tabsW - vw; if (want < 0) want = 0; } mo_to(&P.tray.tabsX, want, .25f, C_EASE, 0); /* the chosen tab stays in view */
    float bh = 28 + text_line_height(TAB_FS); gfx_clip_push(ix + 30, cy - 4, vw, bh + 8);
    for (int i = 0; i < P.tray.ntabs; i++) { float bx = ix + 30 + xt[i] - P.tray.tabsX.v; gfx_rrect(bx, cy, tabW[i], bh, bh / 2, WHITE_A(16)); gfx_ring(bx, cy, tabW[i], bh, bh / 2, 1.5f, WHITE_A(46)); } /* every tab its own pill: plainly something to choose */
    /* one pill under the chosen tab, gliding from tab to tab; lit with the accent while the tabs have the focus */
    if (P.tray.tab < P.tray.ntabs) { int tf = P.tray.zone == 1 && !P.tray.onBtn && P.tray.kind > 0;
      if (!TM.tabSeen) { mo_set(&TM.tabX, xt[P.tray.tab]); mo_set(&TM.tabW, tabW[P.tray.tab]); mo_set(&TM.tabF, tf); TM.tabSeen = 1; }
      else { mo_to(&TM.tabX, xt[P.tray.tab], GLIDE_DUR, C_GLIDE, 0); mo_to(&TM.tabW, tabW[P.tray.tab], GLIDE_DUR, C_GLIDE, 0); mo_to(&TM.tabF, tf, .2f, C_EASE, 0); }
      float px = ix + 30 + TM.tabX.v - P.tray.tabsX.v, pw = TM.tabW.v, k = TM.tabF.v;
      if (k < 0.999f) { gfx_rrect(px, cy, pw, bh, bh / 2, WHITE_A((int)(56 * (1 - k)))); gfx_ring(px, cy, pw, bh, bh / 2, 2, WHITE_A((int)(150 * (1 - k)))); }
      if (k > 0.001f) { gfx_opacity_push(k); gfx_rrect(px, cy, pw, bh, bh / 2, ACC); gfx_rrect_h(px, cy, pw, bh, bh / 2, WHITE_A(0), WHITE_A(56));  gfx_opacity_pop(); } }
    float pc = ix + 30 + TM.tabX.v - P.tray.tabsX.v + TM.tabW.v / 2;
    for (int i = 0; i < P.tray.ntabs; i++) { float bx = ix + 30 + xt[i] - P.tray.tabsX.v;
      PG.tabX[i] = bx; PG.tabW[i] = tabW[i]; PG.tabY = cy; PG.tabH = bh; PG.ntab = i + 1;
      float u = 1 - fabsf(pc - (bx + tabW[i] / 2)) / tabW[i]; if (u < 0) u = 0; /* the words under the pill take its colours as it passes */
      text_draw(P.tray.tabs[i], bx + tabW[i] / 2, cy + 14, TAB_FS, 700, col_mix(WHITE_A(224), col_mix(WHITE_A(255), ACC_FG, TM.tabF.v), u), 0, 0, ALIGN_CENTER); }
    gfx_clip_pop(); cy += bh + 22; }
  else if (P.tray.title[0]) { text_draw(P.tray.title, ix + 30, cy, 30, 800, WHITE_A(255), 0, W - 62, ALIGN_LEFT); cy += L30 + 18; }
  float lstTop = cy;
  /* changing tabs: what was shown slides out the other way and fades, the new one comes in from the side of the move */
  float swa = TM.swA.v, swx = TM.swPend ? -TM.swDir * 28 * (1 - swa) : TM.swDir * 28 * (1 - swa); int sw = swa < 0.999f;
  if (sw) { gfx_opacity_push(swa); gfx_offset_push(swx, 0); }
  if (ak == 4) { /* subtitles: columns side by side (the page's last one runs a little past the tray's edge) */
    gfx_clip_push(ix, lstTop, W - 2, listH + 16);
    /* the focused column's backdrop glides from column to column */
    { int on = P.tray.zone == 2 && P.tray.c < P.tray.ncols; float tx = ix + 22 + 5 + P.tray.c * CP;
      if (on && (!TM.colBSeen || TM.colBA.v <= 0.001f)) { mo_set(&TM.colBX, tx); TM.colBSeen = 1; } else if (on) mo_to(&TM.colBX, tx, GLIDE_DUR, C_GLIDE, 0);
      mo_to(&TM.colBA, on, .2f, C_EASE, 0);
      if (TM.colBA.v > 0.001f) gfx_rrect(TM.colBX.v, lstTop + 8, CP - 10, colH, 22, WHITE_A((int)(10 * TM.colBA.v + .5f))); }
    for (int c = 0; c < P.tray.ncols; c++) { TCol *col = &P.tray.cols[c]; int cur = c == P.tray.c && P.tray.zone == 2; float colX = ix + 22 + 5 + c * CP, colTop = lstTop + 8;
      float near = 1 - fabsf(TM.colBX.v - colX) / CP; if (near < 0) near = 0; near *= TM.colBA.v;
      static UMemo colU[4][2]; const char *h2 = um_upper(&colU[c][0], col->title); text_draw(h2, colX + 18, colTop + 12, 21, 800, col_mix(WHITE_A(184), ACC2, near), 1.8f, CP - 46, ALIGN_LEFT);
      if (col->vl[0]) { const char *v2 = um_upper(&colU[c][1], col->vl); float va = TM.vkC == c && !strcmp(TM.vkU, "dly") ? TM.vkA.v : 1; text_draw(v2, colX + CP - 28, colTop + 12 + 6 * (1 - va), 21, 800, WHITE_A((int)(255 * va)), 0.5f, 0, ALIGN_RIGHT); }
      float clTop = colTop + 6 + 6 + L18 + 14, cinX = colX + 10 + 4, cinY = clTop + 2; int cn = col->n < 300 ? col->n : 300; float iy = 0, ys[300], hh[300];
      for (int i = 0; i < cn; i++) { ys[i] = iy; hh[i] = col_item_h(&col->items[i]); iy += hh[i] + (col->items[i].k == 0 ? 0 : 10); }
      int fi = P.tray.r[c]; if (fi >= cn) fi = -1; float want = 0; if (fi >= 0 && iy > COL_LH - 4) { want = ys[fi] - COL_LH / 2 + hh[fi] / 2; if (want > iy - (COL_LH - 4)) want = iy - (COL_LH - 4); if (want < 0) want = 0; } mo_to(&P.tray.colY[c], -want, .25f, C_EASE, 0);
      PG.colX[c] = cinX + 6; PG.colW = CPW; PG.colTop = clTop; PG.colBot = clTop + COL_LH; PG.nit[c] = cn; for (int i = 0; i < cn; i++) { PG.itY[c][i] = cinY + P.tray.colY[c].v + ys[i]; PG.itH[c][i] = hh[i]; }
      gfx_clip_push(colX + 10, clTop, CP - 30, COL_LH); gfx_offset_push(cinX, cinY + P.tray.colY[c].v);
      int lfa = TM.colA[c].v < 0.999f; if (lfa) { gfx_opacity_push(TM.colA[c].v); gfx_offset_push(0, 10 * (1 - TM.colA[c].v)); } /* what it lists changed: it fades in */
      /* the column's plate glides to its focused item; moving across columns it fades out here and in there */
      int pon = cur && fi >= 0 && !P.tray.onBtn && col->items[fi].k != 0;
      if (pon) { if (P.tray.plA[c].v <= 0.001f && !P.tray.plA[c].on) mo_set(&P.tray.plY[c], ys[fi]); else mo_to(&P.tray.plY[c], ys[fi], .24f, C_OUT, 0); mo_to(&P.tray.plA[c], 1, .2f, C_EASE, 0); } else mo_to(&P.tray.plA[c], 0, .2f, C_EASE, 0);
      if (P.tray.plA[c].v > 0.001f && fi >= 0) { float ph = hh[fi]; gfx_opacity_push(P.tray.plA[c].v); gfx_rrect(6, P.tray.plY[c].v, CPW, ph, 20, WHITE_A(41)); gfx_clip_push(6, P.tray.plY[c].v, 4, ph); gfx_rrect(6, P.tray.plY[c].v, CPW, ph, 20, ACC2); gfx_clip_pop(); gfx_opacity_pop(); }
      for (int i = 0; i < cn; i++) { if (ys[i] + P.tray.colY[c].v > COL_LH || ys[i] + hh[i] + P.tray.colY[c].v < 0) continue; /* only what shows */ TItem *it = &col->items[i]; int f = cur && i == fi && !P.tray.onBtn; float h = hh[i], yy = ys[i];
        if (it->k == 0) { text_draw_wrapped(it->lb, 14, yy + 12, CPW - 8, 4, 23, 400, WHITE_A(184), 0, ALIGN_LEFT, 28); continue; } /* (a note: on up to four lines) */
        if (it->k == 1) { ph_box(6, yy, CPW, 58, 20); continue; } /* still being found: the waiting look */
        if (it->grey) gfx_opacity_push(0.38f); /* (what these subtitles can't take) */
        if (!f) gfx_rrect(6, yy, CPW, h, 20, WHITE_A(18));
        float tx = 6 + (it->tool ? 18 : 14); if (!it->tool) { if (it->spin) { float cx = tx + 12, cy = yy + h / 2; gfx_ring(cx - 11, cy - 11, 22, 22, 11, 3, WHITE_A(51)); float ph = clock_ph(0.9) * 6.2832f; for (int q = 0; q <= 8; q++) { float an = ph - 2.356f + 1.5708f * q / 8; gfx_circle(cx + cosf(an) * 9.5f, cy + sinf(an) * 9.5f, 1.5f, ACC2); } } /* chosen, its lines still loading: the notes' spinner in place of the tick */
          else if (item_on(it)) icon_draw("check", tx + 1, yy + h / 2 - 11, 22, col_alpha(ACC2, i == TM.onIdx[c] ? TM.chkA[c].v : 1)); tx += 24 + 12; }
        /* a value that cycles: its dots under it, the lit one gliding along; a value just changed fades up */
        int dk = !it->tool ? 0 : !strcmp(it->u, "size") ? 1 : !strcmp(it->u, "color") ? 2 : !strcmp(it->u, "bg") ? 3 : 0; float va = TM.vkC == c && it->tool && !strcmp(TM.vkU, it->u) ? TM.vkA.v : 1, vy0 = dk ? -5 : 0;
        float right = 6 + CPW - 18; if (dk == 2 || dk == 3) { swatch_draw(dk - 2, dk == 2 ? (color_idx() < 0 ? 0 : color_idx()) : bg_idx(), right - 13, yy + h / 2, 13); right -= 26 + 10; }
        if (it->vl[0]) { if (it->vw <= 0) it->vw = text_width(it->vl, 22, it->vwt ? it->vwt : 700, 0); float vw = it->vw; text_draw(it->vl, right, yy + h / 2 - text_line_height(22) / 2.0f + vy0 + 6 * (1 - va), 22, it->vwt ? it->vwt : 700, col_alpha(ACC3, va), 0, 0, ALIGN_RIGHT);
          if (dk == 1) cycle_dots(dk, 7, size_idx(), right, yy + h / 2 + 14, 9, 2.5f, WHITE_A(64), ACC3, 1);
          right -= vw + 10; } if (it->sw) { gfx_circle(right - 9, yy + h / 2, 9, it->sw); gfx_ring(right - 18, yy + h / 2 - 9, 18, 18, 9, 2, BLACK_A(77)); right -= 18 + 10; }
        int px = it->tool ? 23 : 24;
        if (it->sub[0] && it->tool) { float ly = yy + 12; text_draw(it->lb, tx, ly, px, 700, WHITE_A(255), 0, right - tx - 4, ALIGN_LEFT); text_draw_wrapped(it->sub, tx, ly + px * 1.25f + 2, 6 + CPW - 18 - tx, 2, 21, 600, WHITE_A(184), 0, ALIGN_LEFT, 26); } /* Auto-sync: what it did, on up to two lines */
        else if (it->sub[0]) { float ly = yy + 12; text_draw(it->lb, tx, ly, px, 700, WHITE_A(255), 0, right - tx - 4, ALIGN_LEFT); text_draw(it->sub, tx, ly + px * 1.25f + 2, 21, 600, WHITE_A(184), 0, 6 + CPW - 18 - tx, ALIGN_LEFT); }
 /* an addon's subtitle: its addon under it */
        else text_draw(it->lb, tx, yy + h / 2 - text_line_height(px) / 2.0f, px, 700, WHITE_A(255), 0, right - tx - 4, ALIGN_LEFT);
        if (it->grey) gfx_opacity_pop(); }
      gfx_offset_pop(); gfx_clip_pop(); if (lfa) { gfx_offset_pop(); gfx_opacity_pop(); } }
    gfx_clip_pop(); }
  else if (srcs) draw_srcs(ix, lstTop, W);
  else { /* a row: the focused card second from the left, a lit plate (and an accent line) behind it */
    gfx_clip_push(ix, lstTop, W - 2, listH + 16);
    float want = P.tray.idx > 0 && P.tray.idx < P.tray.n ? 30 + xs[P.tray.idx - 1] - 8 : 0, mx = 60 + rowW - (W - 2); if (want > mx) want = mx; if (want < 0) want = 0; mo_to(&P.tray.rowX, want, .3f, C_OUT, 0);
    float ox = ix - P.tray.rowX.v, oy = lstTop + 8;
    PG.nrow = P.tray.n; PG.rowY = oy; PG.rowH = rowH; for (int i = 0; i < P.tray.n; i++) { PG.rowX[i] = ox + 30 + xs[i]; PG.rowW[i] = ws[i]; }
    int fi = P.tray.zone == 0 && !P.tray.onBtn ? P.tray.idx : -1;
    if (fi >= 0 && fi < P.tray.n && P.tray.row[fi].k != 0) { if (!P.tray.ringW) { mo_set(&P.tray.ringX, 0); mo_set(&P.tray.ringY, 0); } mo_to(&P.tray.ringX, 30 + xs[fi], .2f, C_OUT, 0); P.tray.ringW = ws[fi]; P.tray.ringH = hs[fi]; mo_to(&P.tray.ringA, 1, .15f, C_EASE, 0); }
    else mo_to(&P.tray.ringA, 0, .15f, C_EASE, 0);
    gfx_offset_push(ox, oy);
    if (P.tray.ringA.v > 0.001f && P.tray.ringW) { float rx = P.tray.ringX.v, rw = P.tray.ringW, rh = P.tray.ringH; gfx_opacity_push(P.tray.ringA.v); gfx_rrect(rx, 0, rw, rh, 20, WHITE_A(33)); gfx_rrect(rx + rw * 0.22f, rh + 5, rw * 0.56f, 4, 2, ACC2); gfx_opacity_pop(); }
    int rf = TM.rowA.v < 0.999f; if (rf) { gfx_opacity_push(TM.rowA.v); gfx_offset_push(0, 10 * (1 - TM.rowA.v)); } /* what it lists changed: it fades in */
    for (int i = 0; i < P.tray.n; i++) { if (30 + xs[i] - P.tray.rowX.v > W || 30 + xs[i] + ws[i] - P.tray.rowX.v < 0) continue; draw_card(&P.tray.row[i], 30 + xs[i], 0, ws[i], hs[i], i == fi); }
    if (rf) { gfx_offset_pop(); gfx_opacity_pop(); }
    gfx_offset_pop(); gfx_clip_pop(); }
  if (sw) { gfx_offset_pop(); gfx_opacity_pop(); }
  gfx_offset_pop(); gfx_scale_pop(); gfx_opacity_pop();
}
void player_draw(void) {
  if (!P.open) return; float a = P.in.v; gfx_opacity_push(a);
  if (P.mode == 6 && !hub_on()) P.mode = 0; /* (the hub closed from elsewhere: the player has its keys again) */
  if (growPending) { extern int app_snapshot_ready(void); /* the move waits for the page's picture (taking it is a slow frame: never one of the move's) */
    if (!P.open || P.closing) growPending = 0;
    else { gfx_hole(growR[0], growR[1], growR[2], growR[3], growRad, 1); gfx_ring(growR[0], growR[1], growR[2], growR[3], growRad, 1.5f, WHITE_A(34));
      if (app_snapshot_ready()) { growPending = 0; growAt = now_(); planeAt = 0; growFrames = growCalls = 0; growWorst = 0; } } }
  if (growAt > 0) { VidPlaneCaps c = video_plane_caps(); double t = now_() - growAt; float kh = grow_k();
    if (!P.open || P.closing || kh >= 1) { growAt = 0; if (P.open && !P.closing) apply_aspect();
      fprintf(stderr, "[grow] %d frames, %d plane calls, worst frame %.0f ms (lead %d ms, every %d ms%s)\n", growFrames, growCalls, growWorst, c.leadMs, c.stepMs, c.cut ? ", cut" : ""); }
    else { float H[4], W[4];
      if (c.cut) { /* a TV whose plane can't move smoothly: a dip to black in the box, one move behind it, then the window opens */
        if (t >= 0.06 && planeSent[2] < SB_W - 1) { float full[4] = { 0, 0, SB_W, SB_H }; plane_step(full, 1, 1); } }
      else { grow_rect(ease_io((t + c.leadMs / 1000.0) / GROW_D), W); plane_step(W, 1, 0); }
      grow_rect(kh, H); float in = 2 * sinf(3.14159f * kh), rad = growRad * (1 - kh); /* (inset a touch mid-move: a pixel of rounding never shows a line) */
#ifdef SB_MAC
      { extern void video_mac_radius(float); video_mac_radius(rad); } /* (the preview's round corners opening as it grows) */
#endif
      gfx_hole(H[0] + in, H[1] + in, H[2] - 2 * in, H[3] - 2 * in, rad, 1); /* the one window it plays in, round-cornered, cut through the page fading round it */
      { float ra = 1 - kh / 0.4f; if (ra > 0) gfx_ring(H[0] + in, H[1] + in, H[2] - 2 * in, H[3] - 2 * in, rad, 1.5f, WHITE_A((int)(34 * ra))); } /* (the box's thin edge, going as it opens) */
      if (c.cut) { float d = t < 0.12 ? (float)(t / 0.12) : 1 - (float)((t - 0.12) / 0.15); if (d > 0) gfx_rrect(H[0] + in, H[1] + in, H[2] - 2 * in, H[3] - 2 * in, rad, BLACK_A((int)(255 * fminf(1, d)))); }
      growFrames++; { float ms = app_dt() * 1000; if (growFrames > 1 && ms > growWorst) growWorst = ms; static int gl = -1; if (gl < 0) gl = getenv("SB_GROWLOG") != NULL; static double lastT; double nt = now_(); if (gl) fprintf(stderr, "[grow] frame %d: dt %.1f ms, real %.1f ms, k %.3f, at %.3f\n", growFrames, ms, (nt - lastT) * 1000, grow_k(), nt - growAt); lastT = nt; } }
  }
  if (shrinkAt > 0) { VidPlaneCaps c = video_plane_caps(); double t = now_() - shrinkAt; float k = shrink_k(), H[4], W[4];
    grow_rect(1 - k, H); float in = 2 * sinf(3.14159f * k), rad = growRad * k;
    if (c.cut) { if (k >= 0.999f) plane_step(growR, 0, 1); }
    else { grow_rect(1 - ease_io((t - c.leadMs / 1000.0) / SHRINK_D), W); plane_step(W, 0, 0); } /* (the picture behind the hole: it shrinks after it, so it always covers it) */
#ifdef SB_MAC
    { extern void video_mac_radius(float); video_mac_radius(rad); }
#endif
    gfx_hole(H[0] + in, H[1] + in, H[2] - 2 * in, H[3] - 2 * in, rad, 1); /* the window shrinking back into the box, the page coming back round it */
    { float ra = (k - 0.6f) / 0.4f; if (ra > 0) gfx_ring(H[0] + in, H[1] + in, H[2] - 2 * in, H[3] - 2 * in, rad, 1.5f, WHITE_A((int)(34 * fminf(1, ra)))); }
    growFrames++; { float ms = app_dt() * 1000; if (growFrames > 1 && ms > growWorst) growWorst = ms; }
    if (k >= 1) { shrinkAt = 0; growAt = 0; plane_step(growR, 0, 1); /* (the box exactly, at the end) */
      fprintf(stderr, "[grow] back into the box: %d frames, %d plane calls, worst frame %.0f ms\n", growFrames, growCalls, growWorst);
#ifdef SB_MAC
      { extern void video_mac_radius(float); video_mac_radius(0); }
#endif
      if (lpk_take_back(P.id)) { keepVideo = 1; finish_close(); return; } close_player(0, 0); }
    return; }
  zap_shade();
  if (P.live && P.artA.v < 0.999f) { prev_shade(); draw_osd(); if (stripT >= 0 && seekK.v > 0.01f && !live_bcast()) prev_strip(stripT, stripFrom, 1); draw_tray(); } /* (the previews over the controls, under a tray) */
  else if (P.quiet && P.mode != 6) live_card_draw(); /* switching channels, nothing running yet: the corner pill alone */
  else if (P.artA.v < 0.999f) hand_draw();
  draw_art();
  gfx_opacity_pop();
}
static void chapters_hook(const char *url, int p2p) { (void)url; (void)p2p; P.chapWant = 1; }
void player_init(void) { prev_available(); /* (the previews' decoder loading in the background from the start: the first film knows it is there) */ video_init(SB_APP_ID); app_play_hook = player_play; player_chapters_hook = chapters_hook; if (getenv("SB_PLAY_ALT")) player_fallback(getenv("SB_PLAY_ALT")); /* development: the fallback of the SB_PLAY stream */
  if (getenv("SB_PLAY_TRSUB")) { TrailerSub t; memset(&t, 0, sizeof t); snprintf(t.url, sizeof t.url, "%s", getenv("SB_PLAY_TRSUB")); snprintf(t.lang, sizeof t.lang, "eng"); snprintf(t.name, sizeof t.name, "English"); snprintf(t.kind, sizeof t.kind, "srt"); player_trailer_subs(&t, 1); } /* development: a caption track for an SB_PLAY trailer */ }

/* ---- the pointer in the player: moving brings the controls; pointing at a button, the bar, a prompt or a tray
   card highlights it; a click acts there as OK does: on the bar it seeks to that spot on release (a bubble shows the
   time under the pointer), on the picture it plays / pauses, beside an open tray it puts the tray away (on another
   button: that one acts) ---- */
static int in_box(float x, float y, float bx, float by, float bw, float bh) { return x >= bx && x <= bx + bw && y >= by && y <= by + bh; }
static void ptr_hv(int on, float x) {
  double d = dur(); on = on && d > 0; if (on) { float tx = 72 + 132 + 20, tw = SB_W - 2 * 72 - 2 * (132 + 20), p = (x - tx) / tw; P.hvP = p < 0 ? 0 : p > 1 ? 1 : p; }
  P.hvOn = on;
}
static void ptr_tray(float x, float y, int down) {
  for (int i = 0; i < PG.ntab && i < P.tray.ntabs; i++) if (in_box(x, y, PG.tabX[i], PG.tabY, PG.tabW[i], PG.tabH) && x >= PG.x && x <= PG.x + PG.w) {
    if (down && i != P.tray.tab) { tray_tab(i); P.tray.zone = 1; }
    return; }
  if (PG.src && src_tray()) { if (y < PG.srcTop || y > PG.srcTop + SRC_VH || x < PG.srcX || x > PG.srcX + SRC_RW) return; float ly = y - PG.srcTop + SR.y.v;
    for (int i = 0; i < SR.n; i++) if (ly >= SR.r[i].y && ly <= SR.r[i].y + SR.r[i].h) { if (!src_focusable(i)) return; if (SR.idx != i || P.tray.onBtn || P.tray.zone != 0) { SR.idx = i; SR.moved = 1; P.tray.onBtn = 0; P.tray.zone = 0; } if (down) src_choose(i); return; }
    return; }
  if (P.tray.ncols) {
    for (int c = 0; c < P.tray.ncols; c++) if (x >= PG.colX[c] && x <= PG.colX[c] + PG.colW) {
      if (y < PG.colTop || y > PG.colBot) return;
      for (int i = 0; i < PG.nit[c] && i < P.tray.cols[c].n; i++) if (y >= PG.itY[c][i] && y <= PG.itY[c][i] + PG.itH[c][i]) {
        TItem *it = &P.tray.cols[c].items[i]; if (!focusable(it)) return;
        if (P.tray.c != c || P.tray.r[c] != i || P.tray.onBtn) { P.tray.c = c; P.tray.r[c] = i; P.tray.onBtn = 0; P.tray.zone = 2; }
        if (down) choose(it); return; }
      return; }
    return; }
  if (y < PG.rowY || y > PG.rowY + PG.rowH || x < PG.x || x > PG.x + PG.w) return;
  for (int i = 0; i < PG.nrow && i < P.tray.n; i++) if (x >= PG.rowX[i] && x <= PG.rowX[i] + PG.rowW[i]) {
    TItem *it = &P.tray.row[i]; if (!focusable(it)) return;
    if (P.tray.idx != i || P.tray.zone != 0 || P.tray.onBtn) { P.tray.idx = i; P.tray.zone = 0; P.tray.onBtn = 0; }
    if (down) choose(it); return; }
}
void player_ptr(int type, float x, float y) {
  if (!P.open || P.closing || !P.live) return; /* the art is up (resolving): nothing to point at */
  float L25 = text_line_height(25), barTop = SB_H - 54 - 94 - 30 - L25; /* the bar's row, its times included (the track alone is a few pixels high) */
  int onBar = y >= barTop && y <= barTop + L25, onBtns = y >= SB_H - 54 - 94 && y <= SB_H - 54;
  if (type == 2) { if (P.barDown && onBar) { ptr_hv(1, x); double d = dur(); if (d > 0) { P.seekT = P.hvP * d; P.seekScrub = 1; commit_seek(); } } P.barDown = 0; return; } /* the bar seeks on release, where the pointer is then */
  int down = type == 1;
  if (P.mode == 5 && P.styK) return; /* (the style card: the remote's keys) */
  if (P.mode == 5) { if (down && PZ.h > 0 && y >= PZ.y && y <= PZ.y + PZ.h) { if (x >= PZ.ux && x <= PZ.ux + PZ.uw) pos_change(y < PZ.y + PZ.h / 2 ? 1 : -1); else if (x >= PZ.ox && x <= PZ.ox + PZ.ow) pos_close(); } return; } /* the position card: its keys and Done */
  if (P.mode == 4) { /* the delay card: its buttons; elsewhere the picture stays as it is */
    if (DG.h > 0 && y >= DG.y && y <= DG.y + DG.h) for (int i = 0; i < 4; i++) if (x >= DG.x[i] && x <= DG.x[i] + DG.w[i]) { if (P.dlyF != i) { P.dlyF = i; mo_to(&P.dlyR, (float)i, .18f, C_OUT, 0); } if (down) dly_act(i); return; }
    return; }
  if (P.mode == 2) {
    if (PG.tray && in_box(x, y, PG.x, PG.y, PG.w, PG.h)) { ptr_tray(x, y, down); return; }
    if (down) { int ak = P.tray.kind < 0 ? -P.tray.kind : P.tray.kind, ob = btn_index(ak == 1 || ak == 5 ? "eps" : ak == 6 ? "chn" : ak == 2 ? "src" : ak == 3 ? "aud" : "subs"); close_tray();
      if (onBtns) for (int i = 0; i < P.nbtn; i++) if (i != ob && x >= P.btns[i].x && x <= P.btns[i].x + P.btns[i].w) { P.row = 0; P.bIdx = i; act(P.btns[i].id); break; } }
    return; }
  if (P.prompt.k && PG.prompt) { /* skip intro / recap, the next-up card (all of it) */
    int pi = -1; if (PG.card) { if (in_box(x, y, PG.prX[0], PG.prY[0], PG.prW[0], PG.prH[0])) pi = P.prompt.nbtn - 1; }
    else for (int i = 0; i < PG.npr && i < P.prompt.nbtn; i++) if (in_box(x, y, PG.prX[i], PG.prY[i], PG.prW[i], PG.prH[i])) pi = i;
    if (pi >= 0) { if (P.mode != 3 || P.prompt.idx != pi) { if (P.mode == 0) show_osd(1); P.mode = 3; P.prompt.idx = pi; } ptr_hv(0, x); if (down) prompt_go(pi); return; } }
  if (P.mode == 0 || P.mode == 3) { if (P.mode == 3) P.mode = 0; show_osd(1); } else arm_hide(); /* moving brings the controls */
  if (onBtns) for (int i = 0; i < P.nbtn; i++) if (x >= P.btns[i].x && x <= P.btns[i].x + P.btns[i].w) { if (P.row != 0 || P.bIdx != i) { P.row = 0; P.bIdx = i; } ptr_hv(0, x); if (down) act(P.btns[i].id); return; }
  if (onBar) { if (P.row != 1) P.row = 1; ptr_hv(1, x); if (down) P.barDown = 1; return; } /* the bar, its times included */
  ptr_hv(0, x);
  if (down) { if (P.seekT >= 0) commit_ok(); else toggle(); show_osd(1); } /* the picture: play / pause */
}

/* Stats for nerds: what plays and how, a few short lines (n of them filled) */
int player_nerd_lines(char (*out)[120], int max) {
  if (!P.open || max < 1) return 0; int n = 0; double t = video_time(), d = dur(), buf = video_buffered_end() - t;
  const char *st = P.closing ? "closing" : !P.started ? "starting" : paused() ? "paused" : video_state() == VID_PLAYING ? "playing" : "waiting";
  snprintf(out[n++], 120, "player  %s  %d:%02d / %d:%02d", st, (int)t / 60, (int)t % 60, (int)d / 60, (int)d % 60);
  if (n < max) { char hdr[32] = "", fp[24] = ""; video_hdr(hdr, sizeof hdr); double fps = video_fps(); if (fps > 0) snprintf(fp, sizeof fp, " @ %.3g fps", fps);
    snprintf(out[n++], 120, "video  %dx%d%s%s%s", video_width(), video_height(), fp, hdr[0] && strcmp(hdr, "none") ? " · " : "", hdr[0] && strcmp(hdr, "none") ? hdr : ""); }
  if (n < max) { VidTrack a[16]; int na = aud_list(a, 16); const VidTrack *c = P.curAud >= 0 && P.curAud < na ? &a[P.curAud] : NULL; char ch[16] = "";
    if (c && c->channels > 0) snprintf(ch, sizeof ch, " · %d ch", c->channels);
    MkvAudio ma[16]; int m = P.dts.conv && mkv ? mkv_audio_tracks(mkv, ma, 16) : 0; const MkvAudio *o = NULL; for (int i = 0; i < m; i++) if (ma[i].n == P.dts.convOrd) o = &ma[i]; if (!o && P.dts.convOrd >= 0 && P.dts.convOrd < m) o = &ma[P.dts.convOrd];
    if (P.dts.conv && o) { /* converted here: the file's own sound, then what the TV gets ("DTS 7.1 → AC3 5.1") */
      const char *k = o->codec, *nm = !strncmp(k, "A_DTS", 5) ? "DTS" : !strncmp(k, "A_TRUEHD", 8) ? "TrueHD" : !strncmp(k, "A_EAC3", 6) ? "E-AC3" : !strncmp(k, "A_AC3", 5) ? "AC3" : !strncmp(k, "A_AAC", 5) ? "AAC" : !strncmp(k, "A_OPUS", 6) ? "Opus" : !strncmp(k, "A_FLAC", 6) ? "FLAC" : k + (strncmp(k, "A_", 2) ? 0 : 2);
      char oc[8] = ""; if (o->channels >= 3) snprintf(oc, sizeof oc, " %d.1", o->channels - 1); else if (o->channels > 0) snprintf(oc, sizeof oc, " %d.0", o->channels);
      snprintf(out[n++], 120, "audio  %s%s \xe2\x86\x92 %s", nm, oc, P.dts.outDesc[0] ? P.dts.outDesc : "AC3"); }
    else snprintf(out[n++], 120, "audio  %s%s", c && c->codec[0] ? c->codec : "not reported yet", ch); }
  if (n < max) snprintf(out[n++], 120, "buffer  %.0f s ahead  ·  %s", buf > 0 ? buf : 0, P.isP2p ? "torrent" : P.isHls ? "HLS" : "direct");
  if (n < max && P.isP2p) { char pl[96]; if (p2p_peer_line(pl, sizeof pl)) snprintf(out[n++], 120, "p2p  %s", pl); }
  if (n < max) { double am = asssubs_render_ms(); if (asssubs_on()) snprintf(out[n++], 120, "subtitles  styled, %.0f ms a render", am); else if (P.curSub[0]) snprintf(out[n++], 120, "subtitles  plain"); }
  return n;
}

/* switching channels: a pill in the top corner hugging the channel's logo in its round end. While something is said
   (tuning in, nothing on, its place in the line-up, Back again to leave) it is open with the name and the words; a new
   channel's logo crossfades in its circle as the words change; tuned, it pops once, closes to the logo alone and fades */
static void lc_logo(const char *u, float cx, float cy, float s, float a) {
  if (a <= 0.003f || !u || !*u) return; float asp = 0; GLuint t = img_get(u, 128, 128, &asp); if (!t || asp <= 0) return;
  float lw = asp >= 1 ? s : s * asp, lh = asp >= 1 ? s / asp : s; gfx_image(t, cx - lw / 2, cy - lh / 2, lw, lh, 0, WHITE_A((int)(255 * a)), 0, 0, 1, 1); }
static char shN[256], shL[120]; static int shT, shF; static float ta; /* the pill's words as shown (they change only once faded out) */
static void lc_reset(void) { shN[0] = shL[0] = 0; ta = 0; }
static void live_card_draw(void) {
  const LiveCard *c = live_card(); static float a; static Tween w; static char cur[512], prev[512], curId[SB_ID]; static double swapAt, popAt; static int wasTuning;
  float want = c->on && P.open && !P.closing && P.artA.v < 0.5f && growAt <= 0 && shrinkAt <= 0 ? 1 : 0; /* (and not while a preview grows: once it fills the screen) */ a += (want - a) * fminf(1, app_dt() * 8); /* (over the art: it says it there) */
  if (a < 0.01f || !c->it) { if (a < 0.01f) { cur[0] = prev[0] = curId[0] = 0; tween_set(&w, 0); wasTuning = 0; lc_reset(); } return; }
  const Item *it = c->it; double now = now_(); int rtl = app_rtl(); char ln[120];
  const char *src = it->logo[0] ? it->logo : it->poster[0] ? it->poster : it->background;
  if (strcmp(curId, it->id)) { snprintf(prev, sizeof prev, "%s", cur); snprintf(cur, sizeof cur, "%s", src); snprintf(curId, sizeof curId, "%s", it->id); swapAt = now; }
  if (wasTuning && !c->tuning && !c->failed) popAt = now; wasTuning = c->tuning;
  if (c->tuning) snprintf(ln, sizeof ln, "%s", T("Tuning in…")); else if (c->failed) snprintf(ln, sizeof ln, "%s", T("This channel isn't broadcasting right now"));
  else if (c->backHint) snprintf(ln, sizeof ln, "%s", T("Back again to leave"));
  else { int k = live_lineup_cur(), n = live_lineup_n(); if (k >= 0 && n > 1) { char a1[12], b1[12]; snprintf(a1, sizeof a1, "%d", k + 1); snprintf(b1, sizeof b1, "%d", n); snprintf(ln, sizeof ln, "%s", T2("{n} of {total}", "n", a1, "total", b1)); } else ln[0] = 0; }
  const float H = 88, R = H / 2, D = 66; int open = c->tuning || c->failed || c->backHint || now - c->at < 1.6;
  /* the words shown: when they change, the old ones fade out, the pill takes the new ones' size, then they fade in
     (nothing is ever cut by its edge) */
  float dt = app_dt();
  int same = !strcmp(shN, it->name) && !strcmp(shL, ln) && shT == c->tuning && shF == c->failed;
  if (!same && (ta <= 0.01f || !shN[0])) { snprintf(shN, sizeof shN, "%s", it->name); snprintf(shL, sizeof shL, "%s", ln); shT = c->tuning; shF = c->failed; ta = 0; same = 1; }
  float nw = text_width(shN, 28, 800, -0.3f), lw = text_width(shL, 21, 600, 0), lead = shT ? 30 : shF ? 20 : 0, mw = fmaxf(nw, lw + lead); int capd = 0; if (mw > SB_W - 64 * 2 - 140) { mw = SB_W - 64 * 2 - 140; capd = 1; } /* (cut only at the widest pill: else it is the words' own size) */
  float full = (H - D) / 2 + D + 18 + mw + 30, tgt = open || ta > 0.05f ? full : H;
  if (w.v <= 0) tween_set(&w, tgt); else if (fabsf(w.to - tgt) > 0.5f) tween_to(&w, tgt, 0.36f, EASE_CSS_EASEOUT); tween_tick(&w, dt);
  if (!same || !open) ta -= dt * 7; else if (fabsf(w.v - full) < 2) ta += dt * 5; ta = ta < 0 ? 0 : ta > 1 ? 1 : ta;
  float W = w.v, x = rtl ? SB_W - 64 - W : 64, y = 52 - 10 * (1 - a), cx = rtl ? x + W - R : x + R, cy = y + R;
  float pk = now - popAt < 0.4 ? sinf((float)((now - popAt) / 0.4) * 3.14159f) : 0, sc = (0.94f + 0.06f * a) * (1 + 0.05f * pk);
  gfx_opacity_push(a); gfx_scale_push(sc, sc, cx, cy);
  lglass_shell(x, y, W, H, R, RGBA(14, 12, 22, 214), 1);
  gfx_circle(cx, cy, D / 2, WHITE_A(16));
  float xf = fminf(1, (float)((now - swapAt) / 0.4)); xf = xf * xf * (3 - 2 * xf); /* the logos crossfade */
  lc_logo(prev, cx, cy, D * 0.74f, 1 - xf); lc_logo(cur, cx, cy, D * 0.74f, xf);
  if (ta > 0.01f) { gfx_opacity_push(ta); int al = rtl ? ALIGN_RIGHT : ALIGN_LEFT;
    float lx = rtl ? cx - D / 2 - 18 : cx + D / 2 + 18, ty = shL[0] ? cy - (text_line_height(28) + 2 + text_line_height(21)) / 2 : cy - text_line_height(28) / 2;
    text_draw(shN, lx, ty, 28, 800, WHITE_A(255), -0.3f, capd ? mw : 0, al);
    float ly = ty + text_line_height(28) + 2, sx = lx;
    if (shT) { float qx = rtl ? lx - 10 : lx + 10, qy = ly + text_line_height(21) / 2; float ph = clock_ph(0.9) * 6.2832f; for (int q = 0; q <= 8; q++) { float an = ph - 2.356f + 1.5708f * q / 8; gfx_circle(qx + cosf(an) * 8, qy + sinf(an) * 8, 1.5f, ACC2); } sx = rtl ? lx - 30 : lx + 30; }
    if (shF) { float qx = rtl ? lx - 6 : lx + 6; gfx_circle(qx, ly + text_line_height(21) / 2, 5, RGB(0xef, 0x3b, 0x3b)); sx = rtl ? lx - 20 : lx + 20; }
    if (shL[0]) text_draw(shL, sx, ly, 21, 600, WHITE_A(shF ? 220 : 190), 0, capd ? mw - lead : 0, al);
    gfx_opacity_pop(); }
  gfx_scale_pop(); gfx_opacity_pop();
}
/* the fonts' card: the font in use by its name, written in it; five around it as samples (Aa, or an Arabic word), the
   one in use lit; Left / Right through them */
static void font_card_draw(float a) {
  int ar = P.styK == 6, n = SUBF_N, cur = style_idx(); const float h = 120, VW = 330, B = 76, CW = 104; static TMemo dW; const char *dn = T("Done"); float dw = tm_width(&dW, dn, 26, 700, 0) + 64;
  int lo = cur - 2; if (lo < 0) lo = 0; if (lo > n - 5) lo = n - 5; float tot = 5 * CW + 4 * 12;
  float w = 28 + VW + 24 + tot + 28 + 2 + 28 + dw + 28, x = SB_W / 2 - w / 2, y = 44 - 24 * (1 - a), cy = y + h / 2;
  gfx_opacity_push(a); gfx_offset_push(nudge_x(&P.nudge), nudge_y(&P.nudge));
  gfx_shadow(x, y + 14, w, h, 30, 40, BLACK_A(110)); gfx_rrect(x, y, w, h, 30, RGBA(18, 16, 28, 222)); gfx_rrect_v(x, y, w, h, 30, WHITE_A(18), WHITE_A(4)); gfx_ring(x, y, w, h, 30, 1, WHITE_A(36));
  { const char *nm = cur ? (ar ? SUBF_AR[cur] : SUBF_LAT[cur]) : T("Default"); float e = (float)((now_() - P.posAt) / 0.22); e = e < 0 ? 0 : e > 1 ? 1 : e; float lift = 6 * (1 - ease_apply(EASE_OUT, e)), vx = x + 28;
    char nb[96]; snprintf(nb, sizeof nb, ar ? "%s  \xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7" : "%s", nm); text_draw(nb, vx + VW / 2, cy - 30 - lift, 32, text_sub_weight(600, ar ? 0 : cur, ar ? cur : 0), WHITE_A(255), 0, VW, ALIGN_CENTER); text_draw(T(ar ? "Arabic subtitle font" : "Subtitle font"), vx + VW / 2, cy + 18, 20, 600, WHITE_A(160), 0.3f, VW, ALIGN_CENTER); }
  float ox = x + 28 + VW + 24; const char *smp = ar ? "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7" : "Aa"; /* (the Arabic sample: the same word everywhere, مرحبا) */
  for (int k = 0; k < 5; k++) { int i = lo + k, on = i == cur; float bh = 58, by = cy - bh / 2; int fw = text_sub_weight(600, ar ? 0 : i, ar ? i : 0);
    gfx_rrect_v(ox, by, CW, bh, 14, RGB(46, 48, 60), RGB(14, 14, 20)); text_draw(smp, ox + CW / 2, cy - text_line_height(28) / 2, 28, fw, WHITE_A(240), 0, CW - 12, ALIGN_CENTER);
    if (on) gfx_ring_out(ox, by, CW, bh, 14, 3, ACC2); else gfx_ring(ox, by, CW, bh, 14, 1, WHITE_A(40)); ox += CW + 12; }
  float sep = ox - 12 + 28; gfx_rect(sep, cy - 30, 1.5f, 60, WHITE_A(40));
  float dx = sep + 2 + 28; lglass_accent(dx, cy - B / 2, dw, B, B / 2, 1); text_draw(dn, dx + dw / 2, cy - text_line_height(26) / 2.0f, 26, 700, ACC_FG, 0, 0, ALIGN_CENTER);
  { float hy = y + h + 22; key_hint(SB_W / 2 - 60, hy, K_LEFT, NULL, WHITE_A(150), 0); key_hint(SB_W / 2 + 22, hy, K_RIGHT, NULL, WHITE_A(150), 0); }
  gfx_offset_pop(); gfx_opacity_pop(); }
void player_dev_fontcard(int k) { P.styK = k; P.posAt = 0; font_card_draw(1); } /* (development: SB_FONTCARD=5|6 draws the fonts' card over any screen) */
static void style_draw(float a) { /* [ value · what it is ]  [ the choices, the one in use lit ]  |  [ Done ] */
  if (P.styK >= 5) { font_card_draw(a); return; }
  static const char *const CN[3] = { "White", "Yellow", "Cyan" }, *const BN[3] = { "None", "Solid", "Transparent" }, *const TT[5] = { "", "Subtitle size", "Subtitle colour", "Subtitle background", "Subtitle brightness" };
  int n = style_n(), cur = style_idx(); const float h = 120, VW = 250, B = 76; static TMemo dW; const char *dn = T("Done"); float dw = tm_width(&dW, dn, 26, 700, 0) + 64;
  float ow[7], tot = 0; for (int i = 0; i < n; i++) { if (P.styK == 1) { char v[8]; snprintf(v, sizeof v, "%d%%", SUB_SIZES[i]); ow[i] = text_width(v, 22, 700, 0) + 34; } else ow[i] = P.styK == 2 ? 56 : 96; tot += ow[i] + (i ? 12 : 0); }
  float w = 28 + VW + 24 + tot + 28 + 2 + 28 + dw + 28, x = SB_W / 2 - w / 2, y = 44 - 24 * (1 - a), cy = y + h / 2;
  gfx_opacity_push(a); gfx_offset_push(nudge_x(&P.nudge), nudge_y(&P.nudge));
  gfx_shadow(x, y + 14, w, h, 30, 40, BLACK_A(110)); gfx_rrect(x, y, w, h, 30, RGBA(18, 16, 28, 222)); gfx_rrect_v(x, y, w, h, 30, WHITE_A(18), WHITE_A(4)); gfx_ring(x, y, w, h, 30, 1, WHITE_A(36));
  { char v[32]; if (P.styK == 1) snprintf(v, sizeof v, "%d %%", SUB_SIZES[cur]); else if (P.styK == 4) snprintf(v, sizeof v, "%s %%", SUB_BRIGHT[cur]); else snprintf(v, sizeof v, "%s", T(P.styK == 2 ? CN[cur] : BN[cur]));
    float e = (float)((now_() - P.posAt) / 0.22); e = e < 0 ? 0 : e > 1 ? 1 : e; float lift = 6 * (1 - ease_apply(EASE_OUT, e)), vx = x + 28;
    text_draw(v, vx + VW / 2, cy - 30 - lift, 36, 800, WHITE_A(255), -0.4f, VW, ALIGN_CENTER); text_draw(T(TT[P.styK]), vx + VW / 2, cy + 18, 20, 600, WHITE_A(160), 0.3f, VW, ALIGN_CENTER); }
  float ox = x + 28 + VW + 24; const SubStyle *st = sub_style();
  for (int i = 0; i < n; i++) { int on = i == cur; float bw = ow[i], bh = P.styK == 2 ? 56 : 58, by = cy - bh / 2;
    if (P.styK == 1) { char v[8]; snprintf(v, sizeof v, "%d%%", SUB_SIZES[i]); if (on) { lglass_accent(ox, by, bw, bh, bh / 2, 1); } else gfx_rrect(ox, by, bw, bh, bh / 2, WHITE_A(22));
      text_draw(v, ox + bw / 2, cy - text_line_height(22) / 2, 22, 700, on ? ACC_FG : WHITE_A(225), 0, 0, ALIGN_CENTER); }
    else if (P.styK == 2) { float r = 22, ccx = ox + bw / 2; if (on) gfx_ring_out(ccx - r - 5, cy - r - 5, 2 * r + 10, 2 * r + 10, r + 5, 3, ACC2); gfx_circle(ccx, cy, r, hexcol(SUB_COLORS[i])); gfx_ring(ccx - r, cy - r, 2 * r, 2 * r, r, 1.5f, BLACK_A(70));
      if (on) icon_draw("check", ccx - 11, cy - 11, 22, RGB(20, 18, 28)); }
    else if (P.styK == 4) { float k = atoi(SUB_BRIGHT[i]) / 100.0f; uint32_t base = hexcol(store_get("subsColor", "#ffffff")), bol = hexcol(store_get("subsOutlineColor", "#000000")); gfx_rrect_v(ox, by, bw, bh, 14, RGB(46, 48, 60), RGB(14, 14, 20)); /* a dark scene behind, where the glare shows */
      for (int dd = 0; dd < 8; dd++) { static const float qx[8] = { -2, 2, 0, 0, -1, 1, -1, 1 }, qy[8] = { 0, 0, -2, 2, -1, 1, 1, -1 }; text_draw("Aa", ox + bw / 2 + qx[dd], cy - text_line_height(26) / 2 + qy[dd], 26, 600, col_dim(bol, k), 0, 0, ALIGN_CENTER); }
      text_draw("Aa", ox + bw / 2, cy - text_line_height(26) / 2, 26, 600, col_dim(base, k), 0, 0, ALIGN_CENTER);
      if (on) gfx_ring_out(ox, by, bw, bh, 14, 3, ACC2); else gfx_ring(ox, by, bw, bh, 14, 1, WHITE_A(40)); }
    else { float bo = (float)atof(SUB_BGS[i]); gfx_rrect_v(ox, by, bw, bh, 14, RGB(120, 128, 150), RGB(60, 64, 82)); /* a scene behind, so the background shows as it would */
      float tw = text_width("Aa", 26, 600, 0); if (bo > 0) gfx_rrect(ox + bw / 2 - tw / 2 - 10, cy - 18, tw + 20, 36, 7, BLACK_A((int)(bo * 255)));
      for (int dd = 0; dd < 8; dd++) { static const float qx[8] = { -2, 2, 0, 0, -1, 1, -1, 1 }, qy[8] = { 0, 0, -2, 2, -1, 1, 1, -1 }; text_draw("Aa", ox + bw / 2 + qx[dd], cy - text_line_height(26) / 2 + qy[dd], 26, 600, st->ol, 0, 0, ALIGN_CENTER); }
      text_draw("Aa", ox + bw / 2, cy - text_line_height(26) / 2, 26, 600, st->col, 0, 0, ALIGN_CENTER);
      if (on) gfx_ring_out(ox, by, bw, bh, 14, 3, ACC2); else gfx_ring(ox, by, bw, bh, 14, 1, WHITE_A(40)); }
    ox += bw + 12; }
  float sep = ox - 12 + 28; gfx_rect(sep, cy - 30, 1.5f, 60, WHITE_A(40));
  float dx = sep + 2 + 28; lglass_accent(dx, cy - B / 2, dw, B, B / 2, 1); text_draw(dn, dx + dw / 2, cy - text_line_height(26) / 2.0f, 26, 700, ACC_FG, 0, 0, ALIGN_CENTER);
  { float hy = y + h + 22; key_hint(SB_W / 2 - 60, hy, K_LEFT, NULL, WHITE_A(150), 0); key_hint(SB_W / 2 + 22, hy, K_RIGHT, NULL, WHITE_A(150), 0); } /* (the keys that change it) */
  gfx_offset_pop(); gfx_opacity_pop();
}
static void pos_draw(void) {
  float a = P.posA.v; if (a <= 0.004f) { PZ.h = 0; return; }
  if (P.styK) { PZ.h = 0; style_draw(a); return; }
  /* [ 12 % · Subtitle position ]  [Up Higher / Down Lower]  |  [ Done ]: the height large, the keys beside it */
  const float h = 120, VW = 250; static TMemo dW, hW1, hW2; const char *dn = T("Done"), *hu = T("Higher"), *hd = T("Lower");
  float dw = tm_width(&dW, dn, 26, 700, 0) + 64, hw = 38 + 10 + fmaxf(tm_width(&hW1, hu, 22, 600, 0), tm_width(&hW2, hd, 22, 600, 0));
  float w = 28 + VW + 24 + hw + 28 + 2 + 28 + dw + 28, x = SB_W / 2 - w / 2, y = 44 - 24 * (1 - a), cy = y + h / 2;
  gfx_opacity_push(a); gfx_offset_push(nudge_x(&P.nudge), nudge_y(&P.nudge));
  gfx_shadow(x, y + 14, w, h, 30, 40, BLACK_A(110)); gfx_rrect(x, y, w, h, 30, RGBA(18, 16, 28, 222)); gfx_rrect_v(x, y, w, h, 30, WHITE_A(18), WHITE_A(4)); gfx_ring(x, y, w, h, 30, 1, WHITE_A(36));
  { char v[16]; snprintf(v, sizeof v, "%d %%", pos_pct()); float vx = x + 28, e = (float)((now_() - P.posAt) / 0.22); e = e < 0 ? 0 : e > 1 ? 1 : e; float lift = 6 * (1 - ease_apply(EASE_OUT, e));
    text_draw(v, vx + VW / 2, cy - 30 - lift, 40, 800, WHITE_A(255), -0.4f, 0, ALIGN_CENTER);
    text_draw(T("Subtitle position"), vx + VW / 2, cy + 18, 20, 600, WHITE_A(160), 0.3f, VW, ALIGN_CENTER); }
  float hx = x + 28 + VW + 24; key_hint(hx, cy - 22, K_UP, hu, WHITE_A(pos_pct() < POS_MAX ? 235 : 110), 0); key_hint(hx, cy + 22, K_DOWN, hd, WHITE_A(pos_pct() > POS_MIN ? 235 : 110), 0);
  float sep = hx + hw + 28; gfx_rect(sep, cy - 30, 1.5f, 60, WHITE_A(40));
  float ox = sep + 2 + 28, B = 76; lglass_accent(ox, cy - B / 2, dw, B, B / 2, 1); text_draw(dn, ox + dw / 2, cy - text_line_height(26) / 2.0f, 26, 700, ACC_FG, 0, 0, ALIGN_CENTER);
  PZ.y = y; PZ.h = h; PZ.ux = hx; PZ.uw = hw; PZ.ox = ox; PZ.ow = dw;
  gfx_offset_pop(); gfx_opacity_pop();
}
static void dly_draw(void) {
  float a = P.dlyA.v; if (a <= 0.004f) { DG.h = 0; return; }
  /* [ - ]  +0.25 s  [ + ]  |  [ play ]  [ Done ]: one row, the value large between its two keys */
  const float B = 76, G = 18, VW = 230, h = 120; static TMemo dW; const char *dn = T("Done"); float dw = tm_width(&dW, dn, 26, 700, 0) + 64;
  float w = 28 + B + G + VW + G + B + 28 + 2 + 28 + B + G + dw + 28, x = SB_W / 2 - w / 2, y = 44 - 24 * (1 - a);
  gfx_opacity_push(a); gfx_offset_push(nudge_x(&P.nudge), 0);
  gfx_shadow(x, y + 14, w, h, 30, 40, BLACK_A(110)); gfx_rrect(x, y, w, h, 30, RGBA(18, 16, 28, 222)); gfx_rrect_v(x, y, w, h, 30, WHITE_A(18), WHITE_A(4)); gfx_ring(x, y, w, h, 30, 1, WHITE_A(36));
  float cy = y + h / 2, bx[4], bw[4]; bx[0] = x + 28; bw[0] = B; float vx = bx[0] + B + G; bx[1] = vx + VW + G; bw[1] = B;
  float sep = bx[1] + B + 28; bx[2] = sep + 2 + 28; bw[2] = B; bx[3] = bx[2] + B + G; bw[3] = dw;
  gfx_rect(sep, cy - 30, 1.5f, 60, WHITE_A(40)); /* the timing apart from the playing */
  for (int i = 0; i < 4; i++) { DG.x[i] = bx[i]; DG.w[i] = bw[i]; } DG.y = cy - B / 2; DG.h = B;
  /* the focus: one accent shape gliding between the buttons (a circle, stretching into the Done pill) */
  { float k = P.dlyR.v; int i0 = (int)floorf(k); if (i0 < 0) i0 = 0; if (i0 > 2) i0 = 2; float f = k - i0; if (f < 0) f = 0; if (f > 1) f = 1;
    float fx = bx[i0] + (bx[i0 + 1] - bx[i0]) * f, fw = bw[i0] + (bw[i0 + 1] - bw[i0]) * f; if (k >= 3) { fx = bx[3]; fw = bw[3]; }
    lglass_accent(fx, cy - B / 2, fw, B, B / 2, 1); }
  for (int i = 0; i < 4; i++) { int f = (int)floorf(P.dlyR.v + 0.5f) == i; uint32_t c = f ? ACC_FG : WHITE_A(240);
    if (!f) gfx_rrect(bx[i], cy - B / 2, bw[i], B, B / 2, WHITE_A(22));
    if (i < 2) { float s = 26, t = 4; gfx_rrect(bx[i] + B / 2 - s / 2, cy - t / 2, s, t, 2, c); if (i == 1) gfx_rrect(bx[i] + B / 2 - t / 2, cy - s / 2, t, s, 2, c); } /* - and + drawn: even strokes, centred */
    else if (i == 2) icon_draw(paused() ? "p_play" : "p_pause", bx[i] + B / 2 - 17, cy - 17, 34, c);
    else text_draw(dn, bx[i] + bw[i] / 2, cy - text_line_height(26) / 2.0f, 26, 700, c, 0, 0, ALIGN_CENTER); }
  /* the delay: large, a small lift when it changes; under it what it means */
  { char v[32]; if (P.subDelay == 0) snprintf(v, sizeof v, "0.00 s"); else snprintf(v, sizeof v, "%s%.2f s", P.subDelay > 0 ? "+" : "\xe2\x88\x92", fabs(P.subDelay));
    float e = (float)((now_() - P.dlyAt) / 0.22); e = e < 0 ? 0 : e > 1 ? 1 : e; float lift = 6 * (1 - ease_apply(EASE_OUT, e));
    text_draw(v, vx + VW / 2, cy - 30 - lift, 40, 800, WHITE_A(255), -0.4f, 0, ALIGN_CENTER);
    const char *m = P.subDelay > 0 ? T("Subtitles later") : P.subDelay < 0 ? T("Subtitles earlier") : T("Subtitle delay");
    text_draw(m, vx + VW / 2, cy + 18, 20, 600, WHITE_A(160), 0.3f, VW, ALIGN_CENTER); }
  gfx_offset_pop(); gfx_opacity_pop();
}
