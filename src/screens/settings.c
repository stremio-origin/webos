/* Settings: one grid 1600 wide, the categories as plain rows on the left (grouped by task, Log out on its own at
   the end), the focused category's settings on a see-through panel beside them; one accent highlight glides from
   row to row; switches flip with OK, choices drill in (the settings slide out to the left, the choices come in
   from the right); the accent blobs drift behind (app.c, shared with Search); a key-hint bar at the bottom. */
#include "screens/settings.h"
#include "screens/board.h"
#include "platform/plat.h"
#include "core/prevspike.h"
#include "core/scrobble.h"
#include "core/traktrows.h"
#include "core/traktwatch.h"
#include "ui/subfonts.h"
#include "core/trakt.h"
#include "core/keys.h"
#include "core/i18n.h"
#include "core/store.h"
#include "core/p2p.h"
#include "core/json.h"
#include "core/best.h"
#include "ui/glass.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include "ui/icon.h"
#include "ui/widgets.h"
#include "ui/prompts.h"
#include "ui/sound.h"
#include "ui/orbs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "core/hit.h"
#include "core/tmdb.h"
#include "core/trakt.h"
#include "screens/revpanel.h"
#include "ui/qrcard.h"
#include "core/update.h"
#include "screens/logcard.h"
#include "screens/welcome.h"
#include "ui/qr.h"
#include "ui/support.h"
#define SUP_H 520.0f
#define DISCORD_URL "https://discord.gg/SJEtyV3kW6"
#define LOOK_H 300.0f
#define KEYS_H 446.0f
#define ACCT_H 150.0f
#define TRAKT_H 250.0f      /* not connected: what it brings and the way in */
#define TRAKT_HC 144.0f     /* connected: who, since when, what was watched (its switch and Disconnect are the rows under it) */
static int wnBtn; /* About: What's new, beside the update pill, has the focus (Up from the first row) */
static Tween supK; static int supWant; /* the support row: which code shows (0 the coffee, 1 GitHub); OK turns it over */
#include "ui/img.h"
#include "core/usb.h"

typedef struct { const char *v, *lb; } Opt;
typedef struct { char k[160]; char lb[96], ds[200]; const Opt *opts; int nopts; const char *d; int action, info, raw, off, sub, danger; Opt dyn[40]; char dynS[40][64]; } Setting; /* off: greyed, not selectable; danger: an ending (Disconnect), as Log out: under a line, in its red, with its mark */
typedef struct { const char *g, *lb, *ic, *ds; const char *prev; Setting *opts; int n; int out; } Cat;
static const Opt OO[] = { { "true", "On" }, { "false", "Off" } };
static const Opt PWO[] = { { "false", "Off" }, { "50", "50%" }, { "60", "60%" }, { "70", "70%" }, { "80", "80%" }, { "90", "90%" }, { "100", "100%" } }; /* (previews before playing: how much of the film first) */
static const Opt UIVOL[] = { { "5", "5%" }, { "10", "10%" }, { "15", "15%" }, { "20", "20%" }, { "25", "25%" }, { "30", "30%" }, { "35", "35%" }, { "40", "40%" } }; /* (interface sounds: fine steps, never loud) */
static const Opt SVOL[] = { { "10", "10%" }, { "20", "20%" }, { "30", "30%" }, { "40", "40%" }, { "50", "50%" }, { "60", "60%" }, { "70", "70%" }, { "80", "80%" }, { "90", "90%" }, { "100", "100%" } };
static const Opt LANG_OPTS[] = { { "", "Off" }, { "eng", "English" }, { "ara", "Arabic" }, { "fre", "French" }, { "spa", "Spanish" }, { "spl", "Spanish (LA)" }, { "ger", "German" }, { "ita", "Italian" }, { "por", "Portuguese" }, { "pob", "Portuguese (BR)" }, { "rus", "Russian" }, { "tur", "Turkish" }, { "pol", "Polish" }, { "dut", "Dutch" }, { "gre", "Greek" }, { "rum", "Romanian" }, { "swe", "Swedish" }, { "heb", "Hebrew" }, { "hin", "Hindi" }, { "jpn", "Japanese" }, { "kor", "Korean" }, { "chi", "Chinese" }, { "zht", "Chinese (traditional)" },
  /* every other language the subtitles know, by name */
  { "alb", "Albanian" }, { "arm", "Armenian" }, { "baq", "Basque" }, { "ben", "Bengali" }, { "bos", "Bosnian" }, { "bul", "Bulgarian" }, { "cat", "Catalan" }, { "hrv", "Croatian" }, { "cze", "Czech" }, { "dan", "Danish" }, { "est", "Estonian" }, { "fin", "Finnish" },
  { "glg", "Galician" }, { "geo", "Georgian" }, { "hun", "Hungarian" }, { "ice", "Icelandic" }, { "ind", "Indonesian" }, { "kur", "Kurdish" }, { "lav", "Latvian" }, { "lit", "Lithuanian" }, { "mac", "Macedonian" }, { "may", "Malay" }, { "mal", "Malayalam" },
  { "mon", "Mongolian" }, { "nor", "Norwegian" }, { "per", "Persian" }, { "srp", "Serbian" }, { "sin", "Sinhala" }, { "slo", "Slovak" }, { "slv", "Slovenian" }, { "tgl", "Tagalog" }, { "tam", "Tamil" }, { "tel", "Telugu" }, { "tha", "Thai" }, { "ukr", "Ukrainian" }, { "urd", "Urdu" }, { "vie", "Vietnamese" } };
#define NLANGOPTS ((int)(sizeof LANG_OPTS / sizeof LANG_OPTS[0]))
static const Opt SKIPO[] = { { "auto", "Auto" }, { "show", "Show" }, { "off", "Off" } }; /* the intro / recap button: skips by itself, waits for OK, or none */
static const Opt CLOCKO[] = { { "24", "24-hour (15:30)" }, { "12", "12-hour (3:30 PM)" } };
static const Opt SEEK[] = { { "5", "5s" }, { "10", "10s" }, { "15", "15s" }, { "20", "20s" }, { "30", "30s" } };
static const Opt DTSM[] = { { "auto", "Auto" }, { "convert", "Convert" }, { "off", "Off" } }; /* DTS sound: found out by playing, always converted, or left to the TV */
static const Opt ASPECT[] = { { "fit", "Original" }, { "fill", "Fill the screen" }, { "zoom", "Zoom" } };
static const Opt SIZES[] = { { "0", "72%" }, { "1", "80%" }, { "2", "100%" }, { "3", "120%" }, { "4", "140%" }, { "5", "160%" }, { "6", "180%" } };
static const Opt COLORS[] = { { "#ffffff", "White" }, { "#ffe600", "Yellow" }, { "#00e5ff", "Cyan" } };
static const Opt OUTL[] = { { "#000000", "Black" }, { "#ffffff", "White" } };
static const Opt SPOILO[] = { { "off", "Off" }, { "blur", "Blur pictures" }, { "hide", "Blur pictures and text" } };
static const Opt BGS[] = { { "0", "None" }, { "1", "Solid" }, { ".6", "Transparent" } };
static const Opt ASSO[] = { { "styled", "Styled" }, { "plain", "Plain" } };
static const Opt SS_OPTS[] = { { "0", "Off" }, { "1", "1 min" }, { "2", "2 min" }, { "5", "5 min" }, { "10", "10 min" } };
static const Opt ACCENT_OPTS[] = { { "logo", "From the logo" }, { "adaptive", "From the artwork" }, { "purple", "Purple" }, { "violet", "Violet" }, { "fuchsia", "Fuchsia" }, { "pink", "Pink" }, { "rose", "Rose" }, { "red", "Red" }, { "orange", "Orange" }, { "amber", "Amber" }, { "gold", "Gold" }, { "lime", "Lime" }, { "green", "Green" }, { "emerald", "Emerald" }, { "teal", "Teal" }, { "cyan", "Cyan" }, { "sky", "Sky" }, { "blue", "Blue" }, { "indigo", "Indigo" }, { "graphite", "Graphite" } };
#define NCAT 11
#define MAXROWS 48
static Setting playBase[14], playS[10], seekS[6], subsS[11], audS[4], lookS[6], langS[44], langOpt, keyS[4], traktS[6], addonS[40], acctS[1], supS[10];
static Cat cats[NCAT]; static int ncat; /* (Keys leaves when this build carries every key) */

/* the layout, in the web's pixels: the grid is centred, 1600 wide, 150 from the top */
#define WX 160.0f            /* the grid's left edge */
#define WY 150.0f
#define CATW 500.0f          /* the categories */
#define COLH 810.0f          /* both columns end 120 above the bottom */
#define PNX (WX + 560)       /* the panel */
#define PNW 1040.0f
#define CLX (PNX + 30)       /* inside its padding */
#define CLY (WY + 34)
#define CLW 980.0f
#define CLH (COLH - 34)
#define PKY (WY + 98)        /* the choices under their heading */
#define PKH (COLH - 98)
#define ROWGAP 4.0f
#define HEADH 46.0f          /* the heading of the panel: 38 px on a 46 px line, 18 under it */

/* the focus highlight: one pill per column that glides between rows (rows of different heights) and fades its colour */
typedef struct { Tween y, h, a, in; int on; uint32_t from, to; Timer1 ct, pr; } Hl;
/* what a row of settings animates by itself: a switch's knob and track, a value sliding in, its words fading in */
typedef struct { Tween swK, swC, dy; double vAt; int vDir; double txAt; unsigned hash, kh; float dotP; int dotOn; Tween fk; } RowA; /* fk: how focused it is (0..1, eased): its colours turn with the highlight, never at once */ /* dotP: where the value's dots stand (gliding to the chosen one) */ /* kh: whose row it is (its key): the state follows the row when rows come and go; dy: its glide to a new place */
static struct {
  int zone; /* 0 cats, 1 opts, 2 pick */ int cat, idx, pick, shown; char acct[200]; Nudge nudge;
  Hl hc, ho, hp; Tween catsY, optsY, pickY, pkA, pkX, loA;
  double enterAt, optsAt, pickAt, keysAt; int optsDir, keysZone;
  /* the layout (measured when something changes, never per frame) */
  int dirty; float catY[NCAT], grpY[NCAT], catsH; int catK[NCAT], grpK[NCAT], gap[NCAT];
  float optY[MAXROWS], optH[MAXROWS], optTot, prevH; int optN, optN0; RowA ra[MAXROWS]; int raShown;
} S;
static void set_s(Setting *s, const char *k, const char *lb, const char *ds, const Opt *o, int n, const char *d) { memset(s, 0, sizeof *s); snprintf(s->k, sizeof s->k, "%s", k); snprintf(s->lb, sizeof s->lb, "%s", lb); snprintf(s->ds, sizeof s->ds, "%s", ds ? ds : ""); s->opts = o; s->nopts = n; s->d = d; }
static void set_act(Setting *s, const char *k, const char *lb, const char *ds) { set_s(s, k, lb, ds, NULL, 0, ""); s->action = 1; }
/* a switch per addon: on or off on this TV only */
static int addon_opts(void) {
  int n = 0; cJSON *l = sb_addons(); const cJSON *a;
  set_act(&addonS[n++], "__sync", "Sync addons now", "Fetch your addon list from your account");
  if (l) cJSON_ArrayForEach(a, l) { const cJSON *m = js_obj(a, "manifest"); const char *nm = js_str(m, "name", ""); if (!*nm || n >= 40) continue;
    char key[160], ak[150]; sb_addon_key(a, ak, sizeof ak); snprintf(key, sizeof key, "adOn_%s", ak);
    Setting *s = &addonS[n++]; set_s(s, key, nm, "", OO, 2, "true"); s->raw = 1;
    char ds[200] = ""; const char *ver = js_str(m, "version", ""); if (*ver) snprintf(ds, sizeof ds, "v%s", ver);
    const cJSON *ts = js_arr(m, "types"); const cJSON *t; int first = 1; cJSON_ArrayForEach(t, ts) { if (!cJSON_IsString(t)) continue; const char *x = t->valuestring; char cap[32]; snprintf(cap, sizeof cap, "%s", x); if (cap[0] >= 'a' && cap[0] <= 'z') cap[0] -= 32;
      const char *lb = !strcmp(x, "movie") ? T("Movies") : !strcmp(x, "series") ? T("Series") : T(cap);
      size_t L = strlen(ds); snprintf(ds + L, sizeof ds - L, "%s%s", first ? (L ? "   " : "") : " · ", lb); first = 0; }
    if (js_bool(js_obj(m, "behaviorHints"), "adult", 0)) { size_t L = strlen(ds); snprintf(ds + L, sizeof ds - L, "%s%s", L ? "   " : "", T("Adult content")); } /* the manifest's own warning */
    snprintf(s->ds, sizeof s->ds, "%s", sb_addon_on(a) ? ds : T("Off on this TV only")); }
  return n;
}
/* the user's own keys, added from the phone: TMDB (titles and descriptions in the app's language, its reviews) and
   Trakt (ratings, their spread, people's comments); Language points here */
static int tmdbOn, traktOn;
/* Language's page is the list itself: every language a row in its own words (Auto with the TV's), the one in use
   ticked, OK on one switches to it (the app redrawn in it, in the same place); Titles and descriptions last when the
   build has no TMDB key of its own */
static int lang_opts(void) {
  tmdbOn = tmdb_has_key(); int n = 0;
  for (int i = 0; i < langOpt.nopts && n < 42; i++) { Setting *s = &langS[n++]; char k[24], lb[96]; snprintf(k, sizeof k, "__lang:%s", langOpt.opts[i].v);
    if (!strcmp(langOpt.opts[i].v, "auto")) snprintf(lb, sizeof lb, "%s", T1("Auto ({lang})", "lang", i18n_lang_name(i18n_tv_lang()))); else snprintf(lb, sizeof lb, "%s", langOpt.opts[i].lb);
    set_act(s, k, lb, ""); s->raw = 1; }
  if (!tmdb_builtin()) set_act(&langS[n++], "__keys", "Titles and descriptions", tmdbOn ? "In the app\xe2\x80\x99s language, from TMDB (under Keys)" : "Only in English. Add a TMDB key under Keys");
  return n;
}
static int keys_opts(void) {
  int n = 0; tmdbOn = tmdb_has_key(); traktOn = trakt_has_key();
  if (!tmdb_builtin()) set_act(&keyS[n++], "__tmdb", "Titles and descriptions", tmdbOn ? "In the app\xe2\x80\x99s language, from TMDB. OK to change the key" : "Only in English. OK to add a TMDB key from your phone");
  if (tmdbOn && !tmdb_builtin()) set_act(&keyS[n++], "__tmdbOff", "Remove the TMDB key", "Titles and descriptions go back to English");
  if (!trakt_builtin()) set_act(&keyS[n++], "__trakt", "Ratings and reviews", traktOn ? "The rating, its spread and people\xe2\x80\x99s comments, from Trakt. OK to change the id" : "OK to add a Trakt client id from your phone");
  if (traktOn && !trakt_builtin()) set_act(&keyS[n++], "__traktOff", "Remove the Trakt id", "Ratings and comments from the other sources alone");
  return n;
}
/* Trakt: connect this TV with a code, then a switch for what you watch and a way out */
/* Trakt, at the end of Playback: one row to connect (OK: its code on a card); connected, a switch and Disconnect under it */
static int trakt_st(void) { return getenv("SB_TRAKT_DEMO") ? TRAKT_LINKED : scrobble_state(); } /* (SB_TRAKT_DEMO, development: the page as connected, with sample figures, for a picture of it) */
static int trakt_opts(void) {
  int n = 0, st = trakt_st();
  if (!trakt_client_id()[0] && st != TRAKT_LINKED) return 0; /* (no client id: the card says so, and OK adds one) */
  if (st == TRAKT_LINKED) { set_s(&traktS[n++], "traktScrobble", "Track what you watch on Trakt", "", OO, 2, "true");
    set_s(&traktS[n++], "traktMarks", "Watched marks from Trakt", "What you watched anywhere shows as watched here. Marking by hand adds it to Trakt", OO, 2, "true");
    set_s(&traktS[n++], "traktCw", "Continue Watching from Trakt", "What you paused in other apps, to carry on here", OO, 2, "true");
    if (!traktrows_official()) set_s(&traktS[n++], "traktRows", "Trakt rows on Home", "History, Watchlist and Recommendations, for films and series", OO, 2, "true"); /* (the account's own Trakt addon gives them: no second set, no switch) */
    set_act(&traktS[n], "__traktUnlink", "Disconnect Trakt", ""); traktS[n].danger = 1; n++; }
  /* (not connected, or signed out by Trakt: no rows; OK on Trakt pops the code) */
  return n;
}
static void play_layout(void);
static void trakt_relayout(void) { play_layout(); S.dirty = 1; }
static void trakt_go(void); /* (Trakt's card: further down) */
static int iUsb = 5; /* the USB row's place in Playback */
static int trakt_card_check(void *ud) { (void)ud; int r = scrobble_link_poll(); if (r == 1) { traktrows_forget(); tw_forget(); board_invalidate(); } /* (connected: Trakt's rows on Home, never another account's kept copy first) */ if (r < 0) { if (scrobble_link_error()[0]) toast(scrobble_link_error(), 3000, NULL); qrcard_close(); } return r == 1; } /* (cancelled: nothing to say) */
static void trakt_card_closed(void *ud) { (void)ud; scrobble_link_cancel(); trakt_relayout(); }
static void trakt_code_ready(int ok, void *ud) {
  (void)ud; if (!ok) { toast(T("Couldn’t reach Trakt. Try again in a moment"), 3000, NULL); return; }
  static char title[48]; snprintf(title, sizeof title, "%s", scrobble_link_code());
  QrCardOpts o; memset(&o, 0, sizeof o); o.title = title; o.sub = T("Scan with your phone and enter the code"); o.url = scrobble_link_url(); o.check = trakt_card_check;
  o.badge = "trakt"; o.badgeBg = RGB(0x64, 0x38, 0xd0); o.code = 1; /* Trakt's mark in its own colour */
  o.doneTitle = T("Connected to Trakt"); o.doneSub = NULL; o.doneHold = 1.8f; o.onClose = trakt_card_closed;
  qrcard_show(&o);
}
static void relang(void) { for (int i = 0; i < ncat; i++) if (cats[i].opts == langS) { cats[i].n = lang_opts(); if (S.cat == i && S.idx >= cats[i].n) S.idx = cats[i].n - 1; } for (int i = 0; i < ncat; i++) if (cats[i].opts == keyS) { cats[i].n = keys_opts(); if (S.cat == i && S.idx >= cats[i].n) S.idx = cats[i].n - 1; } S.dirty = 1; }
static void on_tmdb_card(int on, void *ud) { (void)ud; if (on == tmdbOn) return; relang(); if (on) toast(T("Titles and descriptions in your language"), 2000, NULL); }
static void on_trakt_card(int on, void *ud) { (void)ud; if (on == traktOn) return; relang(); if (on) toast(T("Ratings and comments now come from Trakt too"), 2000, NULL); }
/* About: the update rows (as the update stands now) and Share logs */
static void discord_card(void);
static unsigned aboutSeen;
static int about_opts(void) {
  int n = 0; const char *av = update_avail(); int st = update_state(); aboutSeen = update_serial();
  /* the rows say what they do and no more: only the support row has words under it (an install says how it goes, while it goes) */
  if (av) { Setting *s = &supS[n++]; set_act(s, "__upd", st == UPD_INSTALLING ? T1("Installing version {v}…", "v", av) : T1("Install version {v}", "v", av), update_msg()); s->raw = 1; }
  { Setting *s = &supS[n++]; set_act(s, "__check", T("Check for updates"), ""); s->raw = 1; } /* (its answer comes as a toast) */
  set_act(&supS[n++], "__logs", "Share logs", "");
  set_act(&supS[n++], "__discord", "Join our Discord", ""); /* Discord's mark beside it; OK: a card with its code (before the support row: that one takes the room left) */
  if (welcome_support_url()[0]) set_act(&supS[n++], "__support", "Support the project / Request a feature", "Your support keeps it going, and feature requests are welcome there. A star helps others find it"); /* its codes in view (sized as if last) */
  set_s(&supS[n++], "nerdStats", "Stats for nerds", "", OO, 2, "false"); /* under it: the list scrolls to it */
  return n;
}
/* Cache torrents on USB: a switch for the drive there (kept per drive), greyed with "No drive" when there is none */
static unsigned usbSeen;
/* the pages' rows as shown: Playback (the best stream, the next episode, the intro skip, the picture size, Dolby Vision
   for Blu-ray copies only on a TV with Dolby Vision); Seek bar (its step, chapters, previews and waiting for them while
   they are on); Appearance's last rows (Home's trailers, their volume under them while they are on); Trakt; the USB
   cache with About's rows (about_opts) */
static void set_count(const Setting *opts, int n) { for (int i = 0; i < ncat; i++) if (cats[i].opts == opts) { cats[i].n = n; if (S.cat == i && S.idx >= n) S.idx = n > 0 ? n - 1 : 0; } }
static void play_layout(void) {
  int on = strcmp(store_get("bgTrailer", "true"), "false") != 0, n = 0;
  playS[n++] = playBase[4]; playS[n++] = playBase[1]; playS[n++] = playBase[12]; playS[n++] = playBase[8]; playS[n++] = playBase[2]; { const TvCaps *tc = best_caps(); if (tc && tc->dv) playS[n++] = playBase[7]; }
  playS[n++] = playBase[13]; /* torrents: how they connect, then their USB cache, last */
  iUsb = n; playS[n++] = playBase[5]; /* the USB cache, last */
  set_count(playS, n);
  n = 0; seekS[n++] = playBase[0]; seekS[n++] = playBase[9];
  if (prev_available()) { seekS[n++] = playBase[10]; /* pictures on the seek bar: only where this app can make them */
    if (strcmp(store_get("seekPreviews", "true"), "false")) seekS[n++] = playBase[11]; } /* (and, while they are on, waiting for them first) */
  set_count(seekS, n);
  n = 4; lookS[n++] = playBase[3]; if (on) lookS[n++] = playBase[6]; set_count(lookS, n);
  set_count(traktS, trakt_opts());
  set_count(supS, about_opts());
}
static void usb_row(void) {
  Setting *s = &playBase[5]; UsbDrive d; usbSeen = usb_serial();
  if (usb_drive(&d)) { char k[128], gb[16]; snprintf(k, sizeof k, "usbCache_%s", d.id); set_s(s, k, T("Cache torrents on USB"), "", OO, 2, "false"); s->raw = 1;
    snprintf(gb, sizeof gb, "%.*f", d.freeMb >= 10240 ? 0 : 1, d.freeMb / 1024.0);
    snprintf(s->ds, sizeof s->ds, "%s · %s. %s", d.name[0] ? d.name : T("USB drive"), T1("{n} GB free", "n", gb), T("Faster seeks and less memory used")); }
  else { set_s(s, "__usbNone", T("Cache torrents on USB"), usb_unreachable(NULL, 0) ? T("USB drive plugged in after the app started: restart the TV to use it") : T("Plug in a USB drive to keep torrents on it"), NULL, 0, ""); s->raw = 1; s->off = 1; }
}
static void caps_known(const TvCaps *c, void *ud) { (void)c; (void)ud; play_layout(); S.dirty = 1; }
static void build(void) {
  set_s(&playBase[0], "seekStep", "Seek step", "How far Left / Right jumps in the player", SEEK, 5, "10");
  set_s(&playBase[1], "bingeWatch", "Auto-play next episode", "When an episode ends, the next one starts", OO, 2, "true");
  { static const Opt NC[] = { { "large", "Large" }, { "compact", "Compact" } }; set_s(&playBase[12], "nextCard", "Next episode card", "Compact: a smaller picture and no summary, more of the film shows", NC, 2, "large"); }
  { static const Opt TC[] = { { "auto", "Automatic" }, { "tcp", "TCP only" } }; set_s(&playBase[13], "p2pTcpOnly", "Torrent connections", "TCP only can help on mobile (4G / 5G) routers; a torrent may find fewer peers", TC, 2, "auto"); }
  set_s(&playBase[9], "showChapters", "Chapters", "", OO, 2, "true");
  prev_available(); /* (its library looked at in the background now, so the row is there when Settings opens) */
  set_s(&playBase[10], "seekPreviews", "Previews on the seek bar", "Pictures from the film over the bar as you move through it. Off: none are made", OO, 2, "true"); /* the file's chapters on the seek bar and in the seek pill */
  set_s(&playBase[11], "seekPreviewsWait", "Load previews before playing", "The film starts once this much of it has its previews; OK starts it at once", PWO, 7, "80"); playBase[11].sub = 1;
  set_s(&playBase[8], "skipIntro", "Skip intro and recap", "Auto skips by itself after a moment; Show waits for OK; Off shows nothing", SKIPO, 3, "auto");
  set_s(&playBase[2], "aspectMode", "Picture size", "Original, fill the screen or zoom", ASPECT, 3, "fit");
  set_s(&playBase[3], "bgTrailer", "Background trailers", "A trailer plays behind Home after a few seconds", OO, 2, "true");
  set_s(&playBase[4], "autoBest", "Play the best stream automatically", "", OO, 2, "false");
  set_s(&playBase[7], "dv7Convert", "Dolby Vision for Blu-ray copies", "Their picture is converted on this TV so it shows in Dolby Vision, not HDR10", OO, 2, "true");
  set_s(&playBase[6], "bgTrailerVol", "Volume", "The sound comes up softly to this level", SVOL, 10, "70"); playBase[6].sub = 1; /* (shown right under the trailers while they are on: play_layout; drawn as theirs, set in) */
  usb_row();
  set_s(&subsS[0], "subsLang", "Default subtitles", "Chosen by itself when a film starts", LANG_OPTS, NLANGOPTS, "eng");
  { static const Opt SM[] = { { "all", "All subtitles" }, { "forced", "Forced only" } }; /* (PR #50: forced tracks are the lines in another language and the signs) */
    set_s(&subsS[1], "subsMode", "Subtitle preference", "Forced only: just the lines in another language and the signs, from a forced track", SM, 2, "all"); }
  set_s(&subsS[2], "subtitleSize", "Size", "Also in the player, under Subtitles", SIZES, 7, "2");
  { static Opt FL[SUBF_N], FA[SUBF_N]; static char fv[SUBF_N][4]; for (int i = 0; i < SUBF_N; i++) { snprintf(fv[i], sizeof fv[i], "%d", i); FL[i].v = FA[i].v = fv[i]; FL[i].lb = SUBF_LAT[i]; FA[i].lb = SUBF_AR[i]; } /* (the fonts by name; Default the app's own) */
    set_s(&subsS[3], "subsFont", "Font", "Also in the player, under Subtitles", FL, SUBF_N, "0"); set_s(&subsS[4], "subsFontAr", "Arabic font", "For subtitles in Arabic", FA, SUBF_N, "0"); }
  set_s(&subsS[5], "subsColor", "Colour", "", COLORS, 3, "#ffffff");
  set_s(&subsS[6], "subsOutlineColor", "Outline", "", OUTL, 2, "#000000");
  set_s(&subsS[7], "subsBgStyle", "Background", "", BGS, 3, "0");
  /* the height in %, 1 % at a time as in the player (where it can be seen while set); a height kept as one of the old six steps carries over */
  { static Opt PP[29]; static char pv[29][4], pl[29][8]; for (int i = 0; i < 29; i++) { snprintf(pv[i], 4, "%d", i + 2); snprintf(pl[i], 8, "%d%%", i + 2); PP[i].v = pv[i]; PP[i].lb = pl[i]; }
    if (!store_get("subsPosPct", "")[0]) { static const int OLD[6] = { 3, 8, 13, 18, 23, 28 }; int i = atoi(store_get("subsPosition", "1")); char b[8]; snprintf(b, sizeof b, "%d", OLD[i < 0 || i > 5 ? 1 : i]); store_set("subsPosPct", b); }
    set_s(&subsS[9], "subsPosPct", "Position", "How high over the bottom of the picture; also in the player", PP, 29, "8"); }
  { static const Opt BR[] = { { "100", "100%" }, { "80", "80%" }, { "60", "60%" }, { "40", "40%" } }; set_s(&subsS[8], "subsBright", "Brightness", "", BR, 4, "100"); } /* (PR #39: softer on an OLED in the dark; under Background) */
  set_s(&subsS[10], "subsAssStyle", "Styled subtitles", "Signs, fonts and effects as the film\xe2\x80\x99s own ASS / SSA subtitles draw them", ASSO, 2, "styled");
  /* the audio language: Original (the film's own track, as it starts) first and by default, then a language, picked by
     itself when a film has it (one row: the old "Auto audio track" switch is folded in: off was Original) */
  { static Opt AUDO[NLANGOPTS]; AUDO[0].v = "orig"; AUDO[0].lb = "Original"; for (int i = 1; i < NLANGOPTS; i++) AUDO[i] = LANG_OPTS[i];
    if (!store_get("audOrigDone", NULL)) { if (strcmp(store_get("enableDefaultAudioTrack", "false"), "true")) store_set("defaultAudioTrack", "orig"); store_set("audOrigDone", "1"); } /* (once: the switch off was the original track) */
    set_s(&audS[0], "defaultAudioTrack", "Default audio language", "", AUDO, NLANGOPTS, "orig"); }
  set_s(&audS[1], "dtsMode", "DTS sound", "Auto finds out what this TV plays. Convert if you are not sure. Off leaves it to the TV", DTSM, 3, "auto");
  set_s(&audS[2], "uiSound", "Interface sounds", "A soft click as you move between titles", OO, 2, "true");
  { int v = atoi(store_get("uiSoundVol", "10")); if (v > 40) store_set("uiSoundVol", "40"); else if (v % 5 || v < 5) { char b[8]; snprintf(b, sizeof b, "%d", v < 5 ? 5 : (v + 2) / 5 * 5); store_set("uiSoundVol", b); } } /* (a level from before, or a phone's: the nearest one here, 40 at most) */
  set_s(&audS[3], "uiSoundVol", "Interface sound level", "Of the TV\xe2\x80\x99s volume", UIVOL, 8, "10");
  set_s(&lookS[0], "accent", "Accent colour", "Dynamic: the colours of the title you open", ACCENT_OPTS, 20, "logo"); /* first, right under its picture */
  set_s(&lookS[2], "clockFormat", "Clock", "24-hour or 12-hour time, everywhere the app shows it", CLOCKO, 2, "24");
  set_s(&lookS[3], "spoilers", "Spoilers", "", SPOILO, 3, "off"); /* the episodes not watched yet: their stills blurred, and their descriptions too if asked */
  set_s(&lookS[1], "ssDelay", "Screensaver", "After a while without a key, the titles of the moment fill the screen", SS_OPTS, 5, "5");
  /* language names are shown in their own language, never translated */
  set_s(&langOpt, "uiLang", "Language", "The app restarts in the new language", NULL, 0, "auto");
  { Setting *s = &langOpt; int n = 0; s->dyn[n].v = "auto"; s->dyn[n].lb = "Auto"; n++; s->dyn[n].v = "en"; s->dyn[n].lb = "English"; n++;
    static const char *ORDER[] = { "fr", "es", "de", "it", "pt", "pt-BR", "nl", "pl", "ro", "hu", "cs", "sk", "hr", "sr", "sv", "da", "no", "fi", "tr", "id", "vi", "ru", "uk", "bg", "el", "ar", "fa" }; /* the web's order, then the later ones near their neighbours */
    for (int j = 0; j < (int)(sizeof ORDER / sizeof *ORDER) && n < 40; j++) { int have = 0; for (int i = 0; i < i18n_lang_count(); i++) if (!strcmp(i18n_lang_code(i), ORDER[j])) have = 1; if (!have) continue;
      snprintf(s->dynS[n], 64, "%s", ORDER[j]); s->dyn[n].v = s->dynS[n]; s->dyn[n].lb = i18n_lang_name(ORDER[j]); n++; }
    s->opts = s->dyn; s->nopts = n; }
  int nl = lang_opts(), nk = keys_opts(), na = addon_opts();
  set_act(&acctS[0], "__logout", "Log out", ""); /* (who it is: the card above it) */
  int nab = about_opts();
  /* grouped by task: what you watch, what the screens look like, account and system; Log out on its own, last */
  Cat c[NCAT] = {
    { "Watch", "Playback", "set_play", "Best stream, next episode, picture size", NULL, playS, 6, 0 }, /* (the counts: play_layout) */
    { "Watch", "Seek bar", "set_seek", "Seek step, chapters, previews", NULL, seekS, 4, 0 },
    { "Watch", "Subtitles", "set_subs", "Language, size, font, colour", "subs", subsS, 11, 0 },
    { "Watch", "Audio", "set_aud", "Default audio language, DTS, interface sounds", NULL, audS, 4, 0 },
    { "Screens", "Appearance", "set_look", "Accent colour, screensaver, clock, trailers", "look", lookS, 6, 0 },
    { "Screens", "Language", "set_lang", "The language of the app", NULL, langS, nl, 0 },
    { "Account and system", "Addons", "set_addon", "", NULL, addonS, na, 0 },
    { "Account and system", "Trakt", "set_trakt", "Track what you watch", "trakt", traktS, 1, 0 },
    { "Account and system", "Keys", "set_keys", "Titles in your language, ratings and reviews", "keys", keyS, nk, 0 },
    { "Account and system", "About", "info", "Version, updates, logs and support", "about", supS, nab, 0 },
    { "", "Log out", "set_out", "", "acct", acctS, 0, 1 } }; /* (no row of its own: the category itself is the button, the page shows who is signed in) */
  ncat = 0; for (int i = 0; i < NCAT; i++) if (c[i].opts != keyS || nk) cats[ncat++] = c[i];
  play_layout(); S.dirty = 1;
  best_caps_probe(caps_known, NULL); /* (the Dolby Vision row, once the TV has said) */
}
static const char *cur_val(const Setting *o) { return store_get(o->k, o->d); }
static int is_switch(const Setting *o) { return !o->action && !o->info && o->nopts == 2 && !strcmp(o->opts[0].v, "true") && !strcmp(o->opts[1].v, "false"); }
static int font_wt(const Setting *o, const char *v, int w) { if (!strcmp(o->k, "subsFont")) return text_sub_weight(w, atoi(v), 0); if (!strcmp(o->k, "subsFontAr")) return text_sub_weight(w, 0, atoi(v)); return w; } /* a font's own name drawn in it */
static const char *val_text(const Setting *o, char *buf, size_t cap) {
  if (o->off) return T("No drive");
  const char *cur = cur_val(o);
  if (!strcmp(o->k, "uiLang")) { if (!strcmp(cur, "auto")) { snprintf(buf, cap, "%s", T1("Auto ({lang})", "lang", i18n_lang_name(i18n_tv_lang()))); return buf; } return i18n_lang_name(cur); }
  for (int i = 0; i < o->nopts; i++) if (!strcmp(o->opts[i].v, cur)) { if (!strcmp(o->k, "subsFontAr")) { snprintf(buf, cap, "%s  \xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7", T(o->opts[i].lb)); return buf; } return T(o->opts[i].lb); } /* (an Arabic font: a sample beside its name) */
  return o->nopts ? T(o->opts[0].lb) : "";
}
/* a new category's settings come in from the side the focus moved to */
static void show_cat(int i, int dir) { S.shown = i; S.optsAt = app_now(); S.optsDir = dir; S.dirty = 1; S.raShown = -1; }
static void enter(const void *p) { (void)p;
  const char *email = sb_user_email(); char prem[96] = ""; /* (a Premium account says until when; any other, nothing) */
  cJSON *u = store_node("user"); const char *pe = js_str(u, "premium_expire", NULL);
  if (pe && strncmp(pe, "000", 3)) { struct tm tm; memset(&tm, 0, sizeof tm); if (sscanf(pe, "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) == 3) { tm.tm_year -= 1900; tm.tm_mon--; time_t t = timegm(&tm); if (t > time(NULL)) { char d[64]; strftime(d, sizeof d, "%a %b %d %Y", &tm); snprintf(prem, sizeof prem, "%s", T1("Premium until {d}", "d", d)); } } }
  snprintf(S.acct, sizeof S.acct, "%s%s%s", email && *email ? email : T("Signed in"), prem[0] ? "  ·  " : "", prem);
  build(); S.zone = 0; S.cat = 0; S.idx = 0; S.pick = -1; show_cat(0, 1);
  tween_set(&S.catsY, 0); tween_set(&S.optsY, 0); tween_set(&S.pickY, 0); tween_set(&S.pkA, 0); tween_set(&S.pkX, 0); tween_set(&S.loA, 1);
  memset(&S.hc, 0, sizeof S.hc); memset(&S.ho, 0, sizeof S.ho); memset(&S.hp, 0, sizeof S.hp);
  S.enterAt = app_now(); S.keysAt = -10; S.keysZone = 0;
}
static void logout(void) { toast(T("Signing out…"), 1500, NULL); sb_logout(); board_invalidate(); app_go(SCR_LOGIN, NULL, 1); }
static void on_sync_addons(int changed, void *ud) { (void)ud; /* what happened, briefly: added and removed counted, a failure said as one */
  int ad = 0, rm = 0; sb_addon_sync_counts(&ad, &rm); char na[12], nr[12]; snprintf(na, sizeof na, "%d", ad); snprintf(nr, sizeof nr, "%d", rm);
  if (changed == -2) toast(T("Sign in to sync addons"), 2600, NULL);
  else if (changed < 0) toast(T("Couldn’t reach your account: addons not synced"), 3000, NULL);
  else if (!changed) toast(T("Addons already match your account"), 2200, NULL);
  else if (ad && rm) toast(T2("Addons updated: {a} added, {r} removed", "a", na, "r", nr), 2600, NULL);
  else if (ad) toast(T1("Addons updated: {a} added", "a", na), 2600, NULL);
  else if (rm) toast(T1("Addons updated: {r} removed", "r", nr), 2600, NULL);
  else toast(T("Addons updated"), 2200, NULL); /* the same addons, their order or settings changed */
  if (changed > 0) board_invalidate(); cats[5].n = addon_opts(); if (S.cat == 5 && S.idx >= cats[5].n) S.idx = cats[5].n - 1; S.dirty = 1; }
static void on_sync_lib(int changed, void *ud) { (void)ud; if (changed) board_invalidate(); }
static void sync_now(void) { toast(T("Syncing…"), 1500, NULL); sb_pull_library(on_sync_lib, NULL); sb_sync_addons(on_sync_addons, NULL); }
/* the best stream: switched on only once the line is measured (once, kept); the row says why meanwhile */
static int ab_busy(const Setting *o) { return !strcmp(o->k, "autoBest") && best_speed_busy(); }
static void on_speed(int ok, double mbps, void *ud) { (void)mbps; (void)ud;
  if (!ok) { toast(T("Could not check the connection. Try again in a moment"), 2600, NULL); return; }
  store_set("autoBest", "true"); toast(T1("Your connection: {n} Mbps. Play now starts the best stream", "n", store_get("netMbps", "0")), 3800, NULL); }
int settings_apply_pref(const char *k, const char *v) {
  if (!strcmp(store_get(k, ""), v)) return 0;
  if (!strcmp(k, "autoBest") && !strcmp(v, "true") && !best_on()) { if (!best_speed_busy()) { best_caps_probe(NULL, NULL); best_speed_run(on_speed, NULL); } return 1; } /* (on only once this TV's line is measured, as here) */
  if (!strcmp(k, "uiLang")) { store_set("uiLang", v); i18n_init(plat_asset_dir(), v); text_set_rtl(i18n_rtl()); text_gc(); board_invalidate(); /* the whole app in the phone's language, at once */
    if (SCR == SCR_SET) { int c = S.cat, i = S.idx, z = S.zone; enter(NULL); S.cat = c; S.idx = i; S.zone = z == 2 ? 1 : z; show_cat(c, 0); S.enterAt = app_now() - 10; } return 1; }
  store_set(k, v); S.dirty = 1;
  if (!strcmp(k, "bingeWatch")) store_set("enableNextVideo", v);
  if (!strcmp(k, "p2pTcpOnly")) p2p_connections_changed();
  if (!strcmp(k, "bgTrailer") || !strcmp(k, "seekPreviews")) play_layout();
  if (!strcmp(k, "accent")) accent_apply();
  return 1;
}
static void ab_tick(void) {
  { static int prevRow; if (!prevRow && prev_available()) { prevRow = 1; play_layout(); S.dirty = 1; } } /* (its library was looked at in the background: the row comes in once that is known) */
  { const char *m = store_get("accent", "logo"), *d = !strcmp(m, "logo") ? "Dynamic: the colours of the logo of the title you open" : !strcmp(m, "adaptive") ? "Dynamic: the colours of the poster of the title you open" : "One colour everywhere in the app"; /* the accent row says what its choice does */
    if (strcmp(lookS[0].ds, d)) { snprintf(lookS[0].ds, sizeof lookS[0].ds, "%s", d); S.dirty = 1; } }
  best_tick();
  /* (the row's words are made here, translated, with the kept speed in them) */
  char d[192]; if (best_speed_busy()) snprintf(d, sizeof d, "%s", T("Testing your speed. Stay here a few seconds"));
  else if (best_on()) snprintf(d, sizeof d, "%s", T1("Set for {n} Mbps. New network? Switch off and on again", "n", store_get("netMbps", "0")));
  else snprintf(d, sizeof d, "%s", T("Play starts the best stream for this TV and your internet speed"));
  if (strcmp(playBase[4].ds, d)) { snprintf(playBase[4].ds, sizeof playBase[4].ds, "%s", d); playBase[4].raw = 0; play_layout(); S.dirty = 1; }
}
static void set_val(Setting *o, const char *v, int dir) {
  if (strcmp(store_get(o->k, o->d ? o->d : ""), v)) { sound_play(SND_TOGGLE); orbs_pulse(ORB_DONE, is_switch(o) ? 0.45f : 0.3f); } /* a switch or a choice: its own tone, and the glows feel it */
  if (!strcmp(o->k, "autoBest") && !strcmp(v, "true") && !best_on()) { if (!best_speed_busy()) { best_caps_probe(NULL, NULL); best_speed_run(on_speed, NULL); } return; }
  if (!strcmp(o->k, "uiLang")) { if (strcmp(v, store_get("uiLang", "auto"))) { store_set("uiLang", v); i18n_init(plat_asset_dir(), v); text_set_rtl(i18n_rtl()); text_gc(); board_invalidate();
      int c = S.cat, i = S.idx, z = S.zone; enter(NULL); S.cat = c; S.idx = i; S.zone = z == 2 ? 1 : z; show_cat(c, 0); S.enterAt = app_now() - 10; } return; } /* the same place, in the new language */
  store_set(o->k, v); S.dirty = 1;
  if (!strcmp(o->k, "defaultAudioTrack")) store_set("enableDefaultAudioTrack", strcmp(v, "orig") ? "true" : "false"); /* (a language: picked by itself; Original: the film's own. Kept for the phone's remote and older builds) */
  if (!strcmp(o->k, "bgTrailer") || !strcmp(o->k, "seekPreviews")) play_layout(); /* its volume row comes and goes with it, right under it */
  if (!strcmp(o->k, "traktRows") || !strcmp(o->k, "traktMarks") || !strcmp(o->k, "traktCw")) board_invalidate(); /* Trakt's rows come and go on Home */
  if (!strncmp(o->k, "adOn_", 5)) { board_invalidate(); cats[5].n = addon_opts(); return; } /* an addon on/off here: rows, metadata and streams follow */
  if (!strcmp(o->k, "bingeWatch")) store_set("enableNextVideo", v);
  if (!strcmp(o->k, "p2pTcpOnly")) p2p_connections_changed();
  if (!strcmp(o->k, "accent")) accent_apply();
  /* only the row changes: a value slides in from the side pressed */
  if (S.idx < MAXROWS && !is_switch(o)) { RowA *r = &S.ra[S.idx]; r->vAt = app_now(); r->vDir = dir; }
}

/* ---- layout ---- */
static float about_h(void);
#define PVR_W 178.0f                         /* the window: about the row's own height, 16:9 */
#define PVR_H 100.0f
#define PVR_R 14.0f                          /* its corners */
static int pv_kind(const Setting *o) { return !strcmp(o->k, "nextCard") ? 1 : !strcmp(o->k, "skipIntro") ? 2 : 0; }
static float right_w(const Setting *o, int focused) { /* what sits right of the words: a switch, a value with its arrows */
  if (ab_busy(o)) return 24 + 300; /* the live speed and its ring */
  if (is_switch(o) && !o->action && !o->info) return 24 + 64;
  if (o->action || o->info) return 0;
  char vb[128]; const char *vt = val_text(o, vb, sizeof vb);
  (void)focused; float dw = o->nopts >= 2 ? (o->nopts <= 7 ? (o->nopts - 1) * 11 + 16 : 96) : 0; /* (the dots under the value: as wide as they need) */
  return 24 + fmaxf(text_width(vt, 26, font_wt(o, cur_val(o), 600), 0), dw) + (pv_kind(o) ? PVR_W + 24 : 0); /* (a preview row: its window on the far side) */
}
#define DS_PX 26     /* a setting's description: 26 px on 34 px lines, at most 3 lines */
#define DS_LH 34.0f
/* the words under a row: only About's rows and the best stream's (its connection measured) keep them */
static int keep_ds(const Setting *o) { return (o >= supS && o < supS + sizeof supS / sizeof *supS) || !strcmp(o->k, "autoBest"); }
static int ds_lines(const Setting *o) { if (!o->ds[0] || !keep_ds(o)) return 0; int n = text_wrap_count(o->raw ? o->ds : T(o->ds), CLW - 48 - right_w(o, 0), 3, DS_PX, 400, 0); return n < 1 ? 1 : n; }
static unsigned hash_s(const char *s, unsigned h) { while (*s) h = h * 31 + (unsigned char)*s++; return h; }
/* what a row draws beside its words, measured once: again when the layout is redone, its value or the language
   changes (a value set from elsewhere is caught by the comparison) */
typedef struct { const Setting *o; unsigned lay, lg; int busy, nl; char v[256], vt[128]; float rw[2], aw, bw, vw; } RowC;
static RowC rowC[MAXROWS]; static unsigned layVer;
static const RowC *row_c(const Setting *o, int i) {
  RowC *c = &rowC[i]; const char *cur = cur_val(o); int busy = ab_busy(o);
  if (c->o == o && c->lay == layVer && c->lg == i18n_gen() && c->busy == busy && !strcmp(c->v, cur ? cur : "")) return c;
  c->o = o; c->lay = layVer; c->lg = i18n_gen(); c->busy = busy; snprintf(c->v, sizeof c->v, "%s", cur ? cur : "");
  c->rw[0] = right_w(o, 0); c->rw[1] = right_w(o, 1); c->nl = ds_lines(o);
  char vb[128]; snprintf(c->vt, sizeof c->vt, "%s", val_text(o, vb, sizeof vb));
  c->aw = text_width(" \xe2\x80\xba", 26, 600, 0); c->bw = text_width("\xe2\x80\xb9 ", 26, 600, 0); c->vw = text_width(c->vt, 26, font_wt(o, cur_val(o), 600), 0);
  return c;
}
/* ---- a setting's own preview: a small screen on the right of its row ----
   Next episode card and Skip intro and recap each show, on the right of their row, the player as that setting makes it
   look, drawn by the player's own code at the screen's scale (ui/prompts.c). The rows keep their height whatever has
   the focus: moving between them moves nothing. The focused row's screen plays its scene; the others hold a still of
   it, a little dimmer. The value (Large, Auto...) sits left of the screen and changes the scene at once */
static double pvFocusAt, pvKindFocusAt[3]; static int pvFocusRow = -1; /* the focused preview row, since when; each scene's own start (kept while it fades back to its still) */
static void layout(void) {
  S.dirty = 0; layVer++;
  /* the categories: a group's name (22 px on 28, 30 above, 10 below), rows of 64 with 4 between; Log out under a line */
  float y = 0; const char *g = NULL; int k = 2; /* child 1 is the highlight: the entrance delays count from there */
  for (int i = 0; i < ncat; i++) { S.grpY[i] = -1; S.gap[i] = 0;
    if (!g || strcmp(cats[i].g, g)) { if (g) y += cats[i].g[0] ? 30 - ROWGAP : 22 - ROWGAP; g = cats[i].g;
      if (g[0]) { S.grpY[i] = y; S.grpK[i] = k++; y += 28 + 10; } else { S.gap[i] = 1; S.grpY[i] = y; S.grpK[i] = k++; y += 1 + 18; } }
    S.catY[i] = y; S.catK[i] = k++; y += 64 + ROWGAP; }
  S.catsH = y - ROWGAP;
  /* the shown category's settings: the heading, a preview, then the rows (68 at least, 16 above and below the words) */
  const Cat *c = &cats[S.shown]; S.prevH = !c->prev ? 0 : !strcmp(c->prev, "about") ? about_h() + 14 : !strcmp(c->prev, "look") ? LOOK_H + 6 : !strcmp(c->prev, "keys") ? KEYS_H + 14 : !strcmp(c->prev, "acct") ? ACCT_H + 14 : !strcmp(c->prev, "trakt") ? (trakt_st() == TRAKT_LINKED ? TRAKT_HC : TRAKT_H) + 18 : 210 + 14; /* (the colour's picture sits right on its row) */
  y = HEADH + 18 + S.prevH; S.optN = c->n < MAXROWS ? c->n : MAXROWS;
  int same = S.raShown == S.shown;
  static RowA old[MAXROWS]; static float oldY[MAXROWS]; int oldN = S.optN0; memcpy(old, S.ra, sizeof old); memcpy(oldY, S.optY, sizeof oldY);
  for (int i = 0; i < S.optN; i++) { const Setting *o = &c->opts[i]; int nl = ds_lines(o); float h = 16 + 34 + (nl ? 4 + nl * DS_LH : 0) + 16; if (h < 68) h = 68;
    if (pv_kind(o) && h < PVR_H + 16) h = PVR_H + 16; /* (a preview row: room for its window, a little more than a row at most) */
    if (!strcmp(o->k, "__support")) { h = CLH - 8 - y; if (h > SUP_H) h = SUP_H; if (h < 320) h = 320; } /* the support row: the panel's room left, so its codes are in view (a row after it is reached by scrolling) */
    if (o->danger) y += 19; /* (under a line, as Log out) */
    S.optY[i] = y; S.optH[i] = h; y += h + ROWGAP;
    RowA *r = &S.ra[i]; unsigned hs = hash_s(o->ds, hash_s(o->lb, 7)), kh = hash_s(o->k, 3); int on = is_switch(o) && !strcmp(cur_val(o), "true");
    if (!same) { memset(r, 0, sizeof *r); tween_set(&r->swK, on); tween_set(&r->swC, on); r->vAt = r->txAt = -10; }
    else { int j = -1; for (int q = 0; q < oldN && q < MAXROWS; q++) if (old[q].kh == kh) { j = q; break; } /* the same row, maybe moved */
      if (j >= 0) { *r = old[j]; if (j != i || oldY[j] != S.optY[i]) { tween_set(&r->dy, oldY[j] + old[j].dy.v - S.optY[i]); tween_to(&r->dy, 0, 0.28f, GLIDE_EASE); } } /* it glides to its new place */
      else { memset(r, 0, sizeof *r); tween_set(&r->swK, on); tween_set(&r->swC, on); r->vAt = -10; r->txAt = app_now(); tween_set(&r->dy, -14); tween_to(&r->dy, 0, 0.28f, GLIDE_EASE); } /* a new row: in from just above, its words fading in */
      if (r->hash != hs && j >= 0) r->txAt = app_now(); tween_to(&r->swK, on, 0.26f, EASE_SNAP); tween_to(&r->swC, on, 0.22f, EASE_CSS_EASE); } /* changed words fade in, a knob glides */
    r->hash = hs; r->kh = kh; }
  S.optN0 = S.optN;
  S.optTot = y - ROWGAP + 28; S.raShown = S.shown;
}

/* ---- the highlight ---- */
#define SUBIN 52.0f /* a sub-setting: set in under the one it belongs to */
static float hlIn; /* the inset of the row the highlight goes to (set before hl_to) */
static uint32_t hl_col(const Hl *p) { return p->ct.on ? col_mix(p->from, p->to, ease_apply(EASE_CSS_EASE, timer_x(&p->ct))) : p->to; }
static void hl_to(Hl *p, float y, float h, int on, uint32_t col) {
  int snap = on && !p->on; p->on = on; /* it appears in place, and glides from then on */
  tween_to(&p->a, on ? 1 : 0, 0.16f, EASE_CSS_EASE);
  if (on) { if (snap) { tween_set(&p->y, y); tween_set(&p->h, h); tween_set(&p->in, hlIn); } else { tween_to(&p->y, y, 0.24f, EASE_SNAP); tween_to(&p->h, h, 0.24f, EASE_SNAP); tween_to(&p->in, hlIn, 0.24f, EASE_SNAP); } }
  if (col != p->to) { p->from = snap || p->a.v <= 0.001f ? col : hl_col(p); p->to = col; if (p->from != col) timer_start(&p->ct, 0.2f); }
}
static void hl_tick(Hl *p, float dt) { tween_tick(&p->in, dt); tween_tick(&p->y, dt); tween_tick(&p->h, dt); tween_tick(&p->a, dt); timer_tick(&p->ct, dt); timer_tick(&p->pr, dt); }
static void hl_press(Hl *p) { timer_start(&p->pr, 0.25f); } /* OK: the highlight gives under the press */
static void hl_draw(const Hl *p, float x, float w, float oy) {
  if (p->a.v <= 0.001f) return;
  float s = 1; if (p->pr.on) { float t = p->pr.t; s = t < 0.09f ? 1 - 0.03f * ease_apply(EASE_SNAP, t / 0.09f) : 0.97f + 0.03f * ease_apply(EASE_SNAP, (t - 0.09f) / 0.16f); }
  float y = oy + p->y.v, h = p->h.v; if (s != 1) gfx_scale_push(s, s, x + w / 2, y + h / 2);
  uint32_t c = hl_col(p); if ((c >> 24) > 160) lglass_tint(x, y, w, h, 16, c, p->a.v); /* the lit one: glass, corner to corner, a thin glassy edge */
  else { gfx_rrect(x, y, w, h, 16, col_alpha(c, p->a.v)); gfx_ring(x, y, w, h, 16, 1, WHITE_A((int)(38 * p->a.v))); } /* (the section you are in: a faint glass mark) */
  if (s != 1) gfx_scale_pop();
}

/* ---- the drill-in choice list ---- */
static void open_pick(void) {
  const Setting *o = &cats[S.cat].opts[S.idx]; const char *cur = cur_val(o); S.pick = 0; for (int j = 0; j < o->nopts; j++) if (!strcmp(o->opts[j].v, cur)) S.pick = j;
  S.zone = 2; tween_to(&S.pkA, 1, 0.22f, EASE_CSS_EASE); tween_to(&S.pkX, 1, 0.26f, EASE_SNAP); tween_set(&S.pickY, 0); S.pickAt = app_now(); memset(&S.hp, 0, sizeof S.hp);
}
static void close_pick(void) { S.zone = 1; S.pick = -1; tween_to(&S.pkA, 0, 0.22f, EASE_CSS_EASE); tween_to(&S.pkX, 0, 0.26f, EASE_SNAP); }
static int key(int k) {
  if (k == K_OK) hl_press(S.zone == 0 ? &S.hc : S.zone == 2 ? &S.hp : &S.ho);
  if (S.zone == 2) { Setting *o = &cats[S.cat].opts[S.idx];
    if (k == K_UP) { if (S.pick > 0) S.pick--; else nudge_start(&S.nudge, k); }
    else if (k == K_DOWN) { if (S.pick < o->nopts - 1) S.pick++; else nudge_start(&S.nudge, k); }
    else if (k == K_RIGHT) nudge_start(&S.nudge, k);
    else if (k == K_OK) { const char *v = o->opts[S.pick].v; close_pick(); set_val(o, v, 0); return 1; }
    else if (k == K_LEFT || k == K_BACK) { close_pick(); return 1; }
    return 1; }
  if (S.zone == 0) { int c = S.cat;
    int toPage = k == (app_rtl() ? K_LEFT : K_RIGHT), trk = cats[c].opts == traktS && trakt_st() != TRAKT_LINKED;
    if (trk && k == K_OK) { trakt_go(); return 1; } /* Trakt, not connected: OK pops the code at once (the right side only tells) */
    else if (trk && toPage) { nudge_start(&S.nudge, k); return 1; }
    else if (cats[c].out && (k == K_OK || toPage)) { if (k == K_OK) logout(); else nudge_start(&S.nudge, k); return 1; } /* Log out: OK signs out, nothing to open */
    if (k == K_DOWN) { if (c < ncat - 1) S.cat++; else nudge_start(&S.nudge, k); }
    else if (k == K_LEFT) nudge_start(&S.nudge, k);
    else if (k == K_UP) { if (c > 0) S.cat--; else { topnav_focus(); return 1; } }
    else if (k == K_RIGHT || k == K_OK) { S.zone = 1; S.idx = 0; }
    else if (k == K_BACK) return 0;
    if (S.cat != c) { S.idx = 0; show_cat(S.cat, S.cat > c ? 1 : -1); } /* (on Log out: who is signed in, its card) */
    return 1; }
  Cat *cat = &cats[S.cat]; Setting *op = S.idx < cat->n ? &cat->opts[S.idx] : NULL;
  if (best_speed_busy()) { if (k == K_BACK) { best_speed_cancel(); toast(T("Speed test stopped"), 1800, NULL); } else nudge_start(&S.nudge, k); return 1; } /* the test: held on its row, in view, till it lands; Back stops it */
  if (wnBtn) { if (cat->opts != supS) wnBtn = 0;
    else { if (k == K_OK) welcome_show_whatsnew(); else if (k == K_DOWN) wnBtn = 0; else if (k == K_LEFT || k == K_BACK) { wnBtn = 0; S.zone = 0; } else nudge_start(&S.nudge, k); return 1; } }
  if (k == K_UP && S.idx == 0 && cat->opts == supS) { wnBtn = 1; return 1; } /* up from About's first row: What's new */
  if (op && op->off && (k == K_OK || k == K_RIGHT)) { nudge_start(&S.nudge, k); return 1; } /* a greyed row: read, not changed */
  if (op && op->off && k == K_LEFT) { S.zone = 0; return 1; } /* (and Left goes back to the list: it has no values to flick through) */
  if (k == K_DOWN) { int j = S.idx + 1; if (j < cat->n) S.idx = j; else nudge_start(&S.nudge, k); } /* greyed rows can be reached (and read), they just do nothing */
  else if (k == K_UP) { int j = S.idx - 1; if (j >= 0) S.idx = j; else nudge_start(&S.nudge, k); }
  else if (k == K_BACK) { S.zone = 0; return 1; }
  else if ((k == K_LEFT || k == K_RIGHT) && op && op->info) { if (k == K_LEFT) S.zone = 0; else nudge_start(&S.nudge, k); }
  else if (k == K_RIGHT && op && op->action) nudge_start(&S.nudge, k);
  else if ((k == K_LEFT || k == K_RIGHT) && op && !op->action && op->nopts > 0) { /* flick through the values: applied at once */
    if (!strcmp(op->k, "uiLang")) { open_pick(); return 1; } /* a new language restarts the app: choose it in the list */
    if (is_switch(op)) { set_val(op, k == K_RIGHT ? "true" : "false", 0); return 1; }
    const char *cur = cur_val(op); int ci = 0; for (int i = 0; i < op->nopts; i++) if (!strcmp(op->opts[i].v, cur)) ci = i;
    ci = (ci + (k == K_RIGHT ? 1 : -1) + op->nopts) % op->nopts; set_val(op, op->opts[ci].v, app_mirror_key(k) == K_RIGHT ? 1 : -1); return 1; }
  else if (k == K_LEFT) S.zone = 0;
  else if (k == K_OK && op) {
    if (op->info) return 1;
    if (op->action && !strncmp(op->k, "__lang:", 7)) { set_val(&langOpt, op->k + 7, 0); return 1; } /* a language: at once */
    if (op->action) { if (!strcmp(op->k, "__tmdb")) { qrcard_show_tmdb(on_tmdb_card, NULL); return 1; }
      if (!strcmp(op->k, "__tmdbOff")) { tmdb_key_clear(); relang(); toast(T("Titles and descriptions in English"), 2000, NULL); return 1; }
      if (!strcmp(op->k, "__trakt")) { qrcard_show_trakt(on_trakt_card, NULL); return 1; }
      if (!strcmp(op->k, "__traktOff")) { trakt_key_clear(); relang(); toast(T("The Trakt id is gone"), 2000, NULL); return 1; }
      if (!strcmp(op->k, "__keys")) { settings_open_keys(); return 1; }
      if (!strcmp(op->k, "__traktUnlink")) { scrobble_unlink(); board_invalidate(); trakt_relayout(); toast(T("Trakt is disconnected"), 2000, NULL); return 1; }
      if (!strcmp(op->k, "__sync")) sync_now(); else if (!strcmp(op->k, "__logs")) logcard_show(); else if (!strcmp(op->k, "__guide")) welcome_show_guide();
      else if (!strcmp(op->k, "__support")) { if (welcome_github_url()[0]) { supWant = !supWant; tween_to(&supK, (float)supWant, 0.5f, EASE_IN_OUT); orbs_pulse(supWant ? ORB_RIGHT : ORB_LEFT, 0.6f); } } /* the other code, turned over */ else if (!strcmp(op->k, "__upd")) update_install(); else if (!strcmp(op->k, "__check")) { if (update_state() != UPD_CHECKING) toast(T("Checking for updates…"), 1500, NULL); update_check(1); } else if (!strcmp(op->k, "__discord")) discord_card(); else if (!strcmp(op->k, "__logout")) logout(); return 1; } /* (only Log out signs out: a row with no action of its own does nothing) */
    if (is_switch(op)) { set_val(op, !strcmp(cur_val(op), "true") ? "false" : "true", 0); return 1; } /* a switch: OK flips it */
    open_pick(); return 1; }
  return 1;
}
static Tween catFk[32], catCk[32]; /* a category: focused / the one shown while the rows have the focus (eased) */
static void tick(float dt) {
  ab_tick();
  if (usb_serial() != usbSeen) { usb_row(); play_layout(); S.dirty = 1; } /* a drive came or went, or its switch moved */
  if (update_serial() != aboutSeen) { for (int i = 0; i < ncat; i++) if (cats[i].opts == supS) { cats[i].n = about_opts(); if (S.cat == i && S.idx >= cats[i].n) S.idx = cats[i].n - 1; } S.dirty = 1; } /* the update moved on: its rows follow */
  { int fr = (S.zone == 1 || S.zone == 2) && S.cat == S.shown && S.idx < cats[S.shown].n && S.idx < MAXROWS && pv_kind(&cats[S.shown].opts[S.idx]) ? S.idx : -1; if (fr != pvFocusRow) { pvFocusRow = fr; pvFocusAt = app_now(); if (fr >= 0) pvKindFocusAt[pv_kind(&cats[S.shown].opts[fr])] = pvFocusAt; } } /* (a preview row focused: its scene plays from here) */
  if (S.dirty) layout();
  nudge_tick(&S.nudge, dt); tween_tick(&S.catsY, dt); tween_tick(&S.optsY, dt); tween_tick(&S.pickY, dt); tween_tick(&S.pkA, dt); tween_tick(&S.pkX, dt); tween_tick(&S.loA, dt);
  hl_tick(&S.hc, dt); hl_tick(&S.ho, dt); hl_tick(&S.hp, dt);
  { int inZ = S.zone == 1 && S.cat == S.shown; for (int i = 0; i < S.optN; i++) { tween_tick(&S.ra[i].swK, dt); tween_tick(&S.ra[i].swC, dt); tween_tick(&S.ra[i].dy, dt); tween_to(&S.ra[i].fk, inZ && i == S.idx ? 1 : 0, 0.18f, EASE_CSS_EASE); tween_tick(&S.ra[i].fk, dt); } }
  for (int i = 0; i < ncat && i < 32; i++) { tween_to(&catFk[i], S.zone == 0 && i == S.cat ? 1 : 0, 0.18f, EASE_CSS_EASE); tween_to(&catCk[i], S.zone != 0 && i == S.cat ? 1 : 0, 0.18f, EASE_CSS_EASE); tween_tick(&catFk[i], dt); tween_tick(&catCk[i], dt); } /* (the categories' colours too) */
  { int kz = S.zone + (best_speed_busy() ? 10 : 0); if (kz != S.keysZone) { S.keysZone = kz; S.keysAt = app_now(); } } /* each new set of key hints fades in */
  /* the categories scroll only as far as the focus needs; the panel keeps the focused row clear of its bottom edge */
  float cb = S.catY[S.cat] + 64, cy = cb > COLH - 60 ? cb - COLH + 60 : 0; tween_to(&S.catsY, -cy, 0.22f, EASE_CSS_EASE);
  hl_to(&S.hc, S.catY[S.cat], 64, 1, S.zone == 0 ? (cats[S.cat].out ? RGB(0xc7, 0x5a, 0x5a) : ACC) : WHITE_A(24)); /* the section you are in: a light glass mark, not a dark one */
  tween_to(&S.loA, 1, 0.22f, EASE_CSS_EASE); /* (Log out shows who is signed in, at full strength) */
  int oz = S.zone != 0 && S.cat == S.shown && S.idx < S.optN; float oy = 0;
  if (oz) { float b = S.optY[S.idx] + S.optH[S.idx]; if (b > CLH - 72) { oy = b - CLH + 72; float mx = S.optTot - CLH; if (mx < 0) mx = 0; if (oy > mx) oy = mx; } }
  tween_to(&S.optsY, -oy, 0.22f, EASE_CSS_EASE);
  if (S.zone != 1) wnBtn = 0;
  hlIn = oz && S.idx < cats[S.cat].n && cats[S.cat].opts[S.idx].sub ? SUBIN : 0; hl_to(&S.ho, oz ? S.optY[S.idx] : 0, oz ? S.optH[S.idx] : 68, oz && !wnBtn, oz && S.idx < cats[S.cat].n && cats[S.cat].opts[S.idx].off ? WHITE_A(34) : oz && S.idx < cats[S.cat].n && cats[S.cat].opts[S.idx].danger ? RGB(0xc7, 0x5a, 0x5a) : ACC); /* a greyed row: a quiet glass highlight, not the accent */
  if (S.zone == 2) { const Setting *o = &cats[S.cat].opts[S.idx]; float pb = S.pick * 72.0f + 68, pt = o->nopts * 72.0f - ROWGAP + 28, py = 0;
    if (pb > PKH - 72) { py = pb - PKH + 72; float mx = pt - PKH; if (mx < 0) mx = 0; if (py > mx) py = mx; }
    tween_to(&S.pickY, -py, 0.22f, EASE_SNAP); hl_to(&S.hp, S.pick * 72.0f, 68, 1, ACC); }
}
/* ---- drawing ---- */
static float stag(int k) { return k <= 2 ? 0 : k <= 7 ? (k - 2) * 0.03f : k <= 10 ? 0.18f : 0.21f; } /* the entrance stagger by child */
static float since(double at) { return (float)(app_now() - at); }
static float MX(float x, float w) { return app_rtl() ? SB_W - x - w : x; }  /* mirrored for right-to-left */
static uint32_t hexcol(const char *h) { unsigned v = (unsigned)strtoul(h + 1, NULL, 16); return 0xff000000u | v; }
/* previews are drawn scenes: the subtitle line as it will look over a film-like dusk; the accent on a small mock of
   the player and its controls */
/* the previews: a title from Home behind them (its backdrop, framed as the app frames pictures), so they read as the
   app itself */
/* the previews' picture: the same film everywhere (every TV, every build, every run), a title from Home if it cannot load */
/* the app's own demo pictures (assets/guide), for the pictures of its screens */
static const char *demo_art(const char *file) { static char b[4][640]; static int k; k = (k + 1) & 3; snprintf(b[k], sizeof b[k], "file://%s/guide/%s", plat_asset_dir(), file); return b[k]; }
#define PREV_BG demo_art("demo1.bg.webp")
static GLuint prev_backdrop(float w, float h, float *u0, float *v0, float *u1, float *v1) {
  float asp = 0; GLuint t = img_get(PREV_BG, 1280, 720, &asp);
  if (!t && img_failed(PREV_BG)) { static Item it; static int have = -1; static double triedAt;
    if (have <= 0 && app_now() - triedAt > 2) { char rn[8]; triedAt = app_now(); have = board_sample(&it, 1, rn, 0) > 0; }
    char b[512]; if (have > 0 && sb_bg_mid(&it, b, sizeof b)[0]) t = img_get(b, 1280, 720, &asp); }
  if (!t || asp <= 0) return 0; img_cover_uv(asp, w / h, 0, u0, v0, u1, v1); return t;
}
/* the preview's picture, lightly blurred, for its glass (as the bar's: made again only when the picture changes) */
static GfxTarget pvT[3]; static GLuint pvSrc; static float pvW, pvH;
static GLuint prev_glass(float w, float h) {
  float u0, v0, u1, v1; GLuint bt = prev_backdrop(w, h, &u0, &v0, &u1, &v1); if (!bt) return 0;
  if (bt == pvSrc && pvW == w && pvH == h && pvT[1].fbo) return pvT[1].tex;
  int W = 480, H = (int)(480 * h / w), bw = W / 2, bh = H / 2; if (H < 8) return 0;
  for (int i = 0; i < 3; i++) { int tw = i ? bw : W, th = i ? bh : H; if (pvT[i].fbo && (pvT[i].w != tw || pvT[i].h != th)) gfx_target_free(&pvT[i]); if (!pvT[i].fbo && !gfx_target_create(&pvT[i], tw, th)) return 0; }
  float LH = SB_W * h / w;
  gfx_target_begin(&pvT[0], PAGE); gfx_image(bt, 0, 0, SB_W, LH, 0, WHITE_A(255), u0, v0, u1, v1); gfx_target_end();
  gfx_target_begin(&pvT[1], PAGE); gfx_image(pvT[0].tex, 0, 0, SB_W, LH, 0, WHITE_A(255), 0, 1, 1, 0); gfx_target_end();
  gfx_blur_pass(&pvT[2], pvT[1].tex, bw, bh, 0, 1.0f); gfx_blur_pass(&pvT[1], pvT[2].tex, bw, bh, 1, 1.0f);
  pvSrc = bt; pvW = w; pvH = h; return pvT[1].tex;
}
/* liquid glass over the preview's picture: its part under a box (x0, y0, w0, h0 the preview) */
static void prev_liquid(GLuint t, float x0, float y0, float w0, float h0, float x, float y, float w, float h, float pull, float lag) {
  if (!t) return; float u0 = (x - x0) / w0 + lag, u1 = (x + w - x0) / w0 + lag, v0 = 1 - (y - y0) / h0, v1 = 1 - (y + h - y0) / h0; gfx_liquid(t, x, y, w, h, h / 2, pull, 1, u0, v0, u1, v1); }
static void prev_frame(float x, float y, float w, float h, float dim) { /* the picture, or the page's own dark when it has none */
  float u0, v0, u1, v1; GLuint t = prev_backdrop(w, h, &u0, &v0, &u1, &v1);
  if (t) gfx_image(t, x, y, w, h, 18, WHITE_A(255), u0, v0, u1, v1); else gfx_rrect(x, y, w, h, 18, RGB(0x1c, 0x1a, 0x24));
  if (dim > 0) gfx_rrect(x, y, w, h, 18, col_alpha(PAGE, dim));
}
/* ---- the settings' own previews (Next episode card, Skip intro and recap): the player, small ---- */
static float pv_ease(double t) { if (t <= 0) return 0; if (t >= 1) return 1; return (float)(0.5 - 0.5 * cos(t * 3.14159265358979)); }
/* the corner of the player where the thing comes (bottom right): a demo picture's matching part, framed in the window */
static void pv_corner(const char *file, float x, float y, float w, float h, float a) {
  float asp = 0; GLuint t = img_get(demo_art(file), 1280, 720, &asp);
  if (!t || asp <= 0) { gfx_rrect(x, y, w, h, PVR_R, col_alpha(RGB(0x1c, 0x1a, 0x24), a)); return; }
  float u0, v0, u1, v1; img_cover_uv(asp, 16.0f / 9, 0, &u0, &v0, &u1, &v1); /* the whole frame as the player shows it, then its lower right 40 % */
  float fu = 0.58f, fv = 0.58f; gfx_image(t, x, y, w, h, PVR_R, WHITE_A((int)(255 * a)), u0 + (u1 - u0) * fu, v0 + (v1 - v0) * fv, u1, v1); }
static void pv_frame_edge(float x, float y, float w, float h) { gfx_rrect_v(x, y + h * 0.45f, w, h * 0.55f, PVR_R, BLACK_A(0), BLACK_A(110)); gfx_ring(x, y, w, h, PVR_R, 1, WHITE_A(46)); } /* the lower part a touch darker, as under the player's prompts; a hairline edge */
/* the card's size: its own glide from one to the other when the value changes */
static float pv_compact(void) {
  static float from, to = -1; static double at; float want = !strcmp(store_get("nextCard", "large"), "compact");
  float e = pv_ease((app_now() - at) / 0.42), cur = to < 0 ? want : from + (to - from) * e;
  if (to < 0) { from = to = want; at = -10; } else if (want != to) { from = cur; to = want; at = app_now(); e = 0; cur = from; }
  return cur; }
/* Next episode card: the credits' corner, darker, with the card as the player draws it (small); Large / Compact glide
   into each other; counting down when the next episode starts by itself */
static void pv_next(float x, float y, float w, float h, double t) {
  float s = 0.16f; gfx_clip_push(x, y, w, h);
  pv_corner("demo1.bg.webp", x, y, w, h, 1); gfx_rrect(x, y, w, h, PVR_R, BLACK_A(118)); pv_frame_edge(x, y, w, h);
  NextCardSpec nc = { T("Next episode"), T("Next episode"), "S1 E2", "Paper Moons", "Two rival illustrators share one small studio for a summer, and one very large deadline.", demo_art("demo2.bg.webp"), pv_compact(), -1, 0 };
  int autoNext = strcmp(store_get("bingeWatch", "true"), "false") && strcmp(store_get("enableNextVideo", "true"), "false");
  double ct = fmod(t, 13.0); if (autoNext) { nc.autoK = pv_ease((ct - 0.8) / 10); nc.autoLeft = (int)(10 - (ct - 0.8) + 0.999); if (nc.autoLeft > 10) nc.autoLeft = 10; }
  float in = pv_ease(t / 0.55); gfx_opacity_push(in); gfx_offset_push(14 * (1 - in), 0);
  ui_next_card(x + w - 10, y + h - 8, s, &nc, NULL, NULL, NULL, NULL); gfx_offset_pop(); gfx_opacity_pop();
  gfx_clip_pop(); }
/* Skip intro and recap: the opening's corner, the pill as the player draws it, and a hairline marking the intro under
   it. Auto: the accent fills the pill, then the film jumps past the intro; Show: the pill lit, OK pressed, then the
   jump; Off: nothing comes, the opening plays through */
static void pv_skip_scene(float x, float y, float w, float h, double t, int live, int mode, double t0) {
  float s = 0.3f;
  double L = 7.6, lt = live ? fmod(fmin(t, app_now() - t0) + 0.95, L) : 0.95, skipT = mode == 1 ? 3.7 : 3.9; /* (the still: the pill just come; focused, the scene goes on from there) */
  float jump = mode ? pv_ease((lt - skipT) / 0.35) : 0, back = pv_ease((lt - (L - 0.5)) / 0.5);
  gfx_clip_push(x, y, w, h);
  pv_corner("demo1.bg.webp", x, y, w, h, 1); if (jump > 0) pv_corner("demo3.bg.webp", x, y, w, h, jump * (1 - back)); pv_frame_edge(x, y, w, h);
  float bx = x + 12, bw = w - 24, by = y + h - 11, bh = 3, i0 = 0.14f, i1 = 0.62f; /* the line: the intro marked, where it plays */
  float p = mode ? (float)(i0 + 0.04f + (lt < skipT ? lt : skipT) * 0.03) : (float)(i0 + 0.04f + lt * 0.075);
  if (mode) p = p + (i1 + 0.03f - p) * jump; p = p + (i0 + 0.04f - p) * back; if (p > 1) p = 1;
  gfx_rrect(bx, by, bw, bh, bh / 2, WHITE_A(70)); gfx_rrect(bx + bw * i0, by, bw * (i1 - i0), bh, bh / 2, col_alpha(ACC2 | 0xff000000u, 0.8f)); gfx_rrect(bx, by, bw * p, bh, bh / 2, WHITE_A(225));
  gfx_circle(bx + bw * p, by + bh / 2, 3.5f, WHITE_A(255));
  if (mode) { float ap = pv_ease((lt - 0.6) / 0.3) * (1 - pv_ease((lt - skipT) / 0.22));
    if (ap > 0.001f) { const char *lb = T("Skip intro"); float pw = ui_pill_width(s, lb, "p_skip"), ph = ui_pill_height(s), px = x + w - 10 - pw, py = by - 8 - ph + 4 * (1 - pv_ease((lt - 0.6) / 0.3));
      gfx_opacity_push(ap);
      if (mode == 1) ui_pill(px, py, pw, s, lb, "p_skip", 0, pv_ease((lt - 0.95) / (skipT - 0.95)), -1); /* Auto: the accent fills it, then it skips */
      else { ui_pill(px, py, pw, s, lb, "p_skip", 1, -1, -1); /* Show: lit, waiting for OK, then OK pressed */
        float press = pv_ease((lt - (skipT - 0.3)) / 0.12) * (1 - pv_ease((lt - (skipT - 0.1)) / 0.15)); if (press > 0) gfx_rrect(px, py, pw, ph, ph / 2, WHITE_A((int)(90 * press))); }
      gfx_opacity_pop(); } }
  gfx_clip_pop(); }
/* the way chosen changed: the new scene from its start, fading in over the old one (which goes on meanwhile) */
static void pv_skip(float x, float y, float w, float h, double t, int live) {
  const char *m = store_get("skipIntro", "auto"); int mode = !strcmp(m, "off") ? 0 : !strcmp(m, "show") ? 2 : 1;
  static int cur = -1, prev = -1; static double curT0, prevT0, at = -10; double now = app_now();
  if (cur < 0) { cur = mode; curT0 = now; } else if (mode != cur) { prev = cur; prevT0 = curT0; cur = mode; curT0 = at = now; }
  float e = pv_ease((now - at) / 0.32);
  if (e < 1 && prev >= 0) pv_skip_scene(x, y, w, h, t, live, prev, prevT0);
  if (e < 1) gfx_opacity_push(e); pv_skip_scene(x, y, w, h, t, live, cur, curT0); if (e < 1) gfx_opacity_pop(); }
static void pv_draw(int kind, float x, float y, float w, float h, float a, int live) {
  if (a <= 0.001f || !kind) return; gfx_opacity_push(a); double t = live ? app_now() - pvKindFocusAt[kind] : 0;
  if (kind == 1) pv_next(x, y, w, h, t + 0.6); /* (the still: the card come in; focused, it counts down from there) */
  else pv_skip(x, y, w, h, t, live);
  gfx_opacity_pop(); }
/* Subtitles: a film from Home, slowly moving, with two soft glows drifting over it; the line drawn as the player draws
   it, in the chosen size, colour, edge and background. A change: the glows lean, the line settles into its new look */
static Orbs subOrbs; static unsigned subSig; static double subAt;
static void preview_subs(float x, float y, float w, float h) {
  float t = (float)app_now(), kb = 0.5f - 0.5f * cosf(t * 6.2832f / 24), u0, v0, u1, v1; GLuint bt = prev_backdrop(w, h, &u0, &v0, &u1, &v1);
  if (bt) { float z = 0.06f + 0.05f * kb, du = (u1 - u0) * z, dv = (v1 - v0) * z, px = (u1 - u0) * 0.04f * sinf(t * 0.13f); /* a slow drift across the still */
    gfx_image(bt, x, y, w, h, 18, WHITE_A(255), u0 + du + px, v0 + dv, u1 - du + px, v1 - dv); } else gfx_rrect(x, y, w, h, 18, RGB(0x1c, 0x1a, 0x24));
  gfx_rrect(x, y, w, h, 18, col_alpha(PAGE, 0.25f));
  int sz = atoi(store_get("subtitleSize", "2")); static const int SZ[] = { 72, 80, 100, 120, 140, 160, 180 }; if (sz < 0 || sz > 6) sz = 2; int px = (int)(34 * SZ[sz] / 100.0f + 0.5f);
  uint32_t col = hexcol(store_get("subsColor", "#ffffff")), ol = hexcol(store_get("subsOutlineColor", "#000000")); float bo = (float)atof(store_get("subsBgStyle", "0"));
  { float k = atoi(store_get("subsBright", "100")) / 100.0f; if (k > 0 && k < 1) { col = (col & 0xff000000u) | ((uint32_t)(((col >> 16) & 255) * k) << 16) | ((uint32_t)(((col >> 8) & 255) * k) << 8) | (uint32_t)((col & 255) * k); ol = (ol & 0xff000000u) | ((uint32_t)(((ol >> 16) & 255) * k) << 16) | ((uint32_t)(((ol >> 8) & 255) * k) << 8) | (uint32_t)((ol & 255) * k); } } /* (the brightness, as the player draws it) */
  unsigned sig = (unsigned)px * 2654435761u ^ col ^ (ol << 1) ^ (unsigned)(bo * 1000); if (sig != subSig) { if (subSig) { subAt = app_now(); orbs_push(&subOrbs, ORB_DONE, 0.5f); } subSig = sig; }
  orbs_step(&subOrbs, app_dt(), 1, 1, 2.6f); orbs_draw_in(&subOrbs, 0.5f, x, y, w, h, 18); /* the app's glows, over the picture, inside its corners */
  gfx_rrect_v(x, y, w, h, 18, BLACK_A(0), BLACK_A(170)); /* the player's shade at the foot */
  int arF = S.cat == S.shown && S.zone >= 1 && S.idx < cats[S.cat].n && !strcmp(cats[S.cat].opts[S.idx].k, "subsFontAr"); /* (the Arabic font being set: the line in Arabic) */
  const char *line = arF ? "\xd8\xb9\xd8\xaf\xd8\xaa\xd9\x8f\x20\xd9\x84\xd8\xa3\xd8\xae\xd8\xa8\xd8\xb1\xd9\x83\x20\xd8\xa8\xd8\xb4\xd9\x8a\xd8\xa1\x2e" : T("I came back to tell you something."); float maxW = w - 80, lh = text_line_height(px) * 1.1f;
  int sfw = subfont_w(600); static int cPx, cN, cFw; static float cW, cMax; static unsigned cLg; static const char *cLine; /* measured once per size, font, line and language */
  if (cPx != px || cFw != sfw || cLine != line || cLg != i18n_gen() || cMax != maxW) { cPx = px; cFw = sfw; cLine = line; cLg = i18n_gen(); cMax = maxW; cN = text_wrap_count(line, maxW, 2, px, sfw, 0); cW = text_width(line, px, sfw, 0); }
  int n = cN; float ty; { int pp = atoi(store_get("subsPosPct", "8")); if (pp < 2 || pp > 30) pp = 8; static Tween pg; static int pgSet; float want = h * pp / 100.0f + 8; /* the Position: the same share of the picture's height as in the player, gliding there */
    if (!pgSet) { tween_set(&pg, want); pgSet = 1; } else if (pg.to != want) tween_to(&pg, want, 0.25f, EASE_OUT); tween_tick(&pg, app_dt()); ty = y + h - pg.v - n * lh; if (ty < y + 8) ty = y + 8; } float ch = subAt > 0 ? ease_apply(EASE_OUT, (float)(app_now() - subAt) / 0.35f) : 1; /* a change: the line settles in */
  gfx_clip_push(x, y, w, h); gfx_opacity_push(0.4f + 0.6f * ch); gfx_offset_push(0, 8 * (1 - ch));
  if (bo > 0) { float tw = cW > maxW ? maxW : cW; gfx_rrect(x + w / 2 - tw / 2 - 18, ty - 4, tw + 36, n * lh + 8, 10, BLACK_A((int)(bo * 220))); }
  for (int d = 0; d < 8; d++) { float an = d * 0.785f, ox = cosf(an) * 2.2f, oy = sinf(an) * 2.2f; text_draw_wrapped(line, x + w / 2 + ox, ty + oy, maxW, 2, px, sfw, col_alpha(ol, 0.9f), 0, ALIGN_CENTER, lh); }
  text_draw_wrapped(line, x + w / 2, ty, maxW, 2, px, sfw, col, 0, ALIGN_CENTER, lh);
  gfx_offset_pop(); gfx_opacity_pop(); gfx_clip_pop();
  gfx_ring(x, y, w, h, 18, 1.5f, WHITE_A(40));
}
/* Language: what a TMDB key brings, at a glance. One film as the app shows it without a key (English) and with
   one (the app's language; French shows the idea when the app is in English); every few seconds the right card's
   words sweep from English into the language, an accent line running ahead of them */
static void tmdb_card(float x, float y, float w, float h, const char *tag, const char *ti, const char *ds, int lit, float a) {
  int rtl = app_rtl(), al = rtl ? ALIGN_RIGHT : ALIGN_LEFT; float pw = 104, ph = 156, px = rtl ? x + w - 16 - pw : x + 16, tx = rtl ? px - 18 : px + pw + 18, tw = w - pw - 50;
  gfx_rrect(x, y, w, h, 16, lit ? RGBA(16, 14, 24, 214) : RGBA(16, 14, 24, 170)); gfx_ring(x, y, w, h, 16, lit ? 2 : 1, lit ? col_alpha(ACC, 0.85f) : WHITE_A(36));
  const char *pu = demo_art("demo3.poster.webp"); GLuint pt = img_get_shaped(pu, (int)pw, (int)ph, 10, 0);
  if (!ph_picture(pt, px, y + (h - ph) / 2, pw, ph, 10, WHITE_A(lit ? 255 : 150))) ph_box(px, y + (h - ph) / 2, pw, ph, 10);
  float ty = y + 16; text_draw(tag, tx, ty, 18, 700, lit ? ACC3 : WHITE_A(150), 1.0f, tw, al); ty += text_line_height(18) + 8;
  gfx_opacity_push(a); int tn = text_draw_wrapped(ti, tx, ty, tw, 2, 24, 800, WHITE_A(lit ? 255 : 190), -0.3f, al, 29); ty += tn * 29 + 2; /* a long title: two lines, the words give one */
  { char m[64]; snprintf(m, sizeof m, "2001 \xc2\xb7 %s", lit ? T("Animation") : "Animation"); text_draw(m, tx, ty, 20, 600, WHITE_A(lit ? 170 : 120), 0, tw, al); } ty += text_line_height(20) + 8;
  text_draw_wrapped(ds, tx, ty, tw, tn > 1 ? 2 : 3, 19, 400, WHITE_A(lit ? 215 : 140), 0, al, 25); gfx_opacity_pop();
}
/* Log out's page: who is signed in, to read only (Log out under it is the one thing to press): the letter on a clean
   disc, the email, the account (Premium until when, or a Stremio account) and since when, the library and the addons */
static void figure(const char *v, const char *cap, float x, float y, int al, float vpx) { text_draw(v, x, y, (int)vpx, 800, WHITE_A(250), -0.6f, 0, al); char u[64]; snprintf(u, sizeof u, "%s", cap); text_upper(u, sizeof u); text_draw(u, x, y + text_line_height((int)vpx) + 2, 18, 700, WHITE_A(140), 1.2f, 0, al); }
static void month_year(const char *iso, char *out, size_t cap) { static const char *M[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" }; int y, m, d; out[0] = 0; if (iso && sscanf(iso, "%d-%d-%d", &y, &m, &d) == 3 && m >= 1 && m <= 12) snprintf(out, cap, "%s %d", T(M[m - 1]), y); }
static void thousands(long v, char *out, size_t cap) { char b[32]; snprintf(b, sizeof b, "%ld", v); int n = (int)strlen(b), k = 0; size_t o = 0; for (int i = 0; i < n && o + 2 < cap; i++) { out[o++] = b[i]; k = n - 1 - i; if (k && k % 3 == 0) out[o++] = ','; } out[o] = 0; }
static void preview_acct(float x, float y, float w) {
  int rtl = app_rtl(), al = rtl ? ALIGN_RIGHT : ALIGN_LEFT, ar = rtl ? ALIGN_LEFT : ALIGN_RIGHT; float pad = 8;
  const char *em = sb_user_email(); cJSON *u = store_node("user");
  /* the person */
  float R = 60, ax = rtl ? x + w - pad - 2 * R : x + pad, ay = y + 14, cx = ax + R, cy = ay + R;
  gfx_shadow(ax, ay + 10, 2 * R, 2 * R, R, 36, BLACK_A(110)); gfx_rrect_v(ax, ay, 2 * R, 2 * R, R, ACC2 | 0xff000000u, ACC | 0xff000000u); /* (one clean disc: the accent, lighter at the top; no rings round it) */
  char ini[8] = "?"; if (em && *em) { ini[0] = (char)toupper((unsigned char)em[0]); ini[1] = 0; }
  text_draw(ini, cx, cy - text_line_height(58) / 2.0f, 58, 800, WHITE_A(255), 0, 0, ALIGN_CENTER);
  char libn[16], adn[16]; { int n = 0; const cJSON *it; cJSON *l = store_node("lib"); if (cJSON_IsObject(l)) cJSON_ArrayForEach(it, l) if (!cJSON_IsTrue(cJSON_GetObjectItem(it, "removed")) && !cJSON_IsTrue(cJSON_GetObjectItem(it, "temp"))) n++; thousands(n, libn, sizeof libn); }
  { cJSON *a = sb_addons(); snprintf(adn, sizeof adn, "%d", a ? cJSON_GetArraySize(a) : 0); }
  /* the two figures on the far side, then the words in what is left */
  float fw0 = fmaxf(text_width(libn, 46, 800, -0.6f), text_width(T("Library"), 18, 700, 1.2f) + 8), fw1 = fmaxf(text_width(adn, 46, 800, -0.6f), text_width(T("Addons"), 18, 700, 1.2f) + 8), gap = 44, fy = cy - (text_line_height(46) + 2 + text_line_height(18)) / 2;
  float f1x = rtl ? x + pad : x + w - pad, f0x = rtl ? f1x + fw1 + gap : f1x - fw1 - gap;
  figure(adn, T("Addons"), f1x, fy, ar, 46); figure(libn, T("Library"), f0x, fy, ar, 46);
  { float lx = rtl ? f1x + fw1 + gap / 2 : f1x - fw1 - gap / 2; gfx_rect(lx, fy + 8, 1, text_line_height(46) + 6, WHITE_A(30)); }
  float tx = rtl ? ax - 28 : ax + 2 * R + 28, tw = w - pad * 2 - 2 * R - 28 - fw0 - fw1 - gap - 40;
  char prem[96] = ""; { const char *pe = js_str(u, "premium_expire", NULL); if (pe && strncmp(pe, "000", 3)) { struct tm tm; memset(&tm, 0, sizeof tm); if (sscanf(pe, "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) == 3) { tm.tm_year -= 1900; tm.tm_mon--; if (timegm(&tm) > time(NULL)) { char d[64]; snprintf(d, sizeof d, "%d/%d/%d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900); snprintf(prem, sizeof prem, "%s", T1("Premium until {d}", "d", d)); } } } }
  float lh = text_line_height(36), bh = 36, ty = cy - (lh + 12 + bh) / 2;
  text_draw(em && *em ? em : T("Signed in"), tx, ty, 36, 800, WHITE_A(255), -0.6f, tw, al);
  { const char *bl = prem[0] ? prem : T("Stremio account"), *bi = prem[0] ? "g_star" : "set_acct"; float is = 18, bw = 14 + is + 8 + text_width(bl, 19, 700, 0.3f) + 16, bx = rtl ? tx - bw : tx, by = ty + lh + 12;
    gfx_rrect(bx, by, bw, bh, bh / 2, prem[0] ? acc_a(0.32f) : WHITE_A(14)); gfx_ring(bx, by, bw, bh, bh / 2, 1, prem[0] ? acc_a(0.75f) : WHITE_A(30));
    uint32_t bc = prem[0] ? ACC3 : WHITE_A(215); float ix = rtl ? bx + bw - 14 - is : bx + 14; icon_draw(bi, ix, by + bh / 2 - is / 2, is, bc);
    text_draw(bl, rtl ? ix - 8 : ix + is + 8, by + bh / 2 - text_line_height(19) / 2.0f, 19, 700, bc, 0.3f, 0, al);
    char since[64]; month_year(js_str(u, "dateRegistered", ""), since, sizeof since);
    if (since[0]) { char sl[128]; snprintf(sl, sizeof sl, "%s %s", T("Member since"), since); float sx = rtl ? bx - 18 : bx + bw + 18; text_draw(sl, sx, by + bh / 2 - text_line_height(20) / 2.0f, 20, 600, WHITE_A(150), 0, tw - bw - 18, al); } }
}
/* Trakt's page, on top: what it is and does, in the page's own glass and the accent (Trakt's red only in its mark).
   Connected: the name, @user, VIP, what has been watched there, whether this TV tells it, since when; its switch and
   Disconnect under it (the page's rows). Not connected: what it brings; OK on Trakt itself pops the code (no client id:
   the card to add one), nothing on the right to focus */
static void trakt_hit(int a, int b) { (void)a; (void)b; trakt_go(); }
static void trakt_hover(int a, int b) { (void)a; (void)b; } /* (the card takes no focus: a pointer over it changes nothing) */
static void preview_trakt(float x, float y, float w) {
  int rtl = app_rtl(), al = rtl ? ALIGN_RIGHT : ALIGN_LEFT, ar = rtl ? ALIGN_LEFT : ALIGN_RIGHT; scrobble_profile_ask();
  int st0 = trakt_st(); float kh = st0 == TRAKT_LINKED ? TRAKT_HC : TRAKT_H, kr = 22; int st = st0, hasId = trakt_client_id()[0] != 0; const TraktProfile *tp = scrobble_profile();
  static TraktProfile demo = { "moviebuff", "Sam Carter", "2019-03-14", 1, 412, 3210, 1, 1, 136500 }; if (getenv("SB_TRAKT_DEMO")) tp = &demo; /* (development: the connected card, for a picture of it) */
  gfx_rrect(x, y, w, kh, kr, WHITE_A(10)); gfx_rrect_v(x, y, w, kh * 0.6f, kr, WHITE_A(12), WHITE_A(0)); gfx_ring(x, y, w, kh, kr, 1, WHITE_A(26)); /* (the page's own glass) */
  if (st != TRAKT_LINKED) hit_add(x, y, w, kh, trakt_hover, trakt_hit, 0, 0);
  float L = 30, D = 64, lx = rtl ? x + w - L - D : x + L, ly = st0 == TRAKT_LINKED ? y + kh / 2 - D / 2 : y + 28;
  gfx_circle(lx + D / 2, ly + D / 2, D / 2, RGB(0xed, 0x1c, 0x24)); icon_draw("trakt", lx + D / 2 - 21, ly + D / 2 - 21, 42, WHITE_A(255)); /* (Trakt's mark: its own red, the only red) */
  float hx = rtl ? lx - 22 : lx + D + 22, hw = w - L * 2 - D - 22, t1 = ly + D / 2 - (text_line_height(30) + 4 + text_line_height(21)) / 2;
  if (st == TRAKT_LINKED) {
    const char *nm = tp->name[0] ? tp->name : tp->user[0] ? tp->user : scrobble_user(); char at[120]; snprintf(at, sizeof at, "@%s", tp->user[0] ? tp->user : scrobble_user());
    char jn[64], jl[120] = ""; month_year(tp->joined, jn, sizeof jn); if (jn[0]) snprintf(jl, sizeof jl, "%s %s", T("Member since"), jn);
    float lh1 = text_line_height(30), lh2 = text_line_height(21), lh3 = jl[0] ? text_line_height(19) + 4 : 0, ty = y + kh / 2 - (lh1 + 4 + lh2 + lh3) / 2;
    text_draw(nm && *nm ? nm : "Trakt", hx, ty, 30, 800, WHITE_A(255), -0.5f, hw * 0.5f, al);
    { float aw = text_width(at, 21, 600, 0), vy = ty + lh1 + 4; text_draw(at, hx, vy, 21, 600, WHITE_A(160), 0, hw * 0.5f, al);
      if (tp->vip) { float vx = rtl ? hx - aw - 10 - 46 : hx + aw + 10; gfx_rrect(vx, vy, 46, 26, 13, acc_a(0.3f)); gfx_ring(vx, vy, 46, 26, 13, 1, acc_a(0.7f)); text_draw("VIP", vx + 23, vy + 13 - text_line_height(15) / 2.0f, 15, 800, ACC3, 1, 0, ALIGN_CENTER); } }
    if (jl[0]) text_draw(jl, hx, ty + lh1 + 4 + lh2 + 4, 19, 600, WHITE_A(130), 0, hw * 0.5f, al);
    if (tp->haveStats) { char a[24], b[24], c2[24], h[24]; thousands(tp->movies, a, sizeof a); thousands(tp->episodes, b, sizeof b); thousands(tp->minutes / 60, h, sizeof h); snprintf(c2, sizeof c2, "%s h", h);
      const char *v[3] = { a, b, c2 }, *cp[3] = { T("Films"), T("Episodes"), T("Watched") }; float fx = rtl ? x + L : x + w - L, fy = y + kh / 2 - (text_line_height(38) + 2 + text_line_height(18)) / 2;
      for (int i = 2; i >= 0; i--) { float fwd = fmaxf(text_width(v[i], 38, 800, -0.6f), text_width(cp[i], 18, 700, 1.2f) + 8); figure(v[i], cp[i], fx, fy, ar, 38); fx += rtl ? fwd + 34 : -(fwd + 34); } }
  } else {
    text_draw("Trakt", hx, t1, 30, 800, WHITE_A(255), -0.5f, hw, al);
    const char *sub = st == TRAKT_RELINK ? T("Trakt signed this TV out. OK shows a new code") : !hasId ? T("Needs a Trakt client id under Keys") : T("OK shows a code for your phone or computer");
    text_draw(sub, hx, t1 + text_line_height(30) + 4, 21, 600, WHITE_A(160), 0, hw, al);
    const char *bl[2] = { T("Films and episodes show on Trakt as you watch"), T("Ratings and reviews from Trakt") }; float by = ly + D + 24;
    for (int i = 0; i < 2; i++) { float bx = rtl ? x + w - L - 24 : x + L; icon_draw("check", bx, by + 3, 24, ACC3);
      text_draw(bl[i], rtl ? bx - 14 : bx + 24 + 14, by + 15 - text_line_height(23) / 2.0f, 23, 600, WHITE_A(220), 0, w - 2 * L - 320, al); by += 42; }
    /* the way in: a glass pill with OK's key (the card takes no focus: OK on Trakt itself) */
    const char *cta = st == TRAKT_RELINK ? T("Connect Trakt again") : T("Connect Trakt"); float cw = 28 + 30 + 12 + text_width(cta, 23, 700, 0) + 28, ch = 58, ccx = rtl ? x + L : x + w - L - cw, ccy = y + kh - 28 - ch;
    gfx_rrect(ccx, ccy, cw, ch, ch / 2, WHITE_A(16)); gfx_ring(ccx, ccy, cw, ch, ch / 2, 1, WHITE_A(46));
    uint32_t cc = WHITE_A(240); float kx = rtl ? ccx + cw - 28 - 30 : ccx + 28; gfx_ring(kx, ccy + ch / 2 - 15, 30, 30, 15, 1.5f, col_alpha(cc, 0.8f)); icon_draw("k_ok", kx + 5, ccy + ch / 2 - 10, 20, cc);
    text_draw(cta, rtl ? kx - 12 : kx + 30 + 12, ccy + ch / 2 - text_line_height(23) / 2.0f, 23, 700, cc, 0, 0, al); }
}
/* OK on Trakt's card (not connected): the code to connect; no client id: the card to add one */
static void trakt_go(void) { if (trakt_st() == TRAKT_LINKED) return;
  if (!trakt_client_id()[0]) { qrcard_show_trakt(on_trakt_card, NULL); return; }
  toast(T("Getting a code from Trakt\xe2\x80\xa6"), 1500, NULL); scrobble_link_start(trakt_code_ready, NULL); }
static void preview_keys(float x, float y, float w, float h) {
  float u0, v0, u1, v1, t = (float)app_now(); int rtl = app_rtl(), al = rtl ? ALIGN_RIGHT : ALIGN_LEFT;
  { float asp = 0; GLuint bt = img_get(demo_art("demo3.bg.webp"), 1280, 720, &asp); /* the demo film's own still */
    if (bt && asp > 0) { img_cover_uv(asp, w / h, 0, &u0, &v0, &u1, &v1); gfx_image(bt, x, y, w, h, 18, WHITE_A(255), u0, v0, u1, v1); } else gfx_rrect(x, y, w, h, 18, RGB(0x1c, 0x1a, 0x24));
    gfx_rrect(x, y, w, h, 18, col_alpha(PAGE, 0.78f)); }
  const char *EN_T = "Spirited Away", *EN_D = "A girl wanders into a world of spirits and must work in a bathhouse to save her parents.";
  int en = !strcmp(i18n_lang(), "en"); const char *LT = en ? "Le Voyage de Chihiro" : T(EN_T), *LD = en ? "Une fillette s\xe2\x80\x99\xc3\xa9gare dans un monde d\xe2\x80\x99" "esprits et doit travailler dans des bains pour sauver ses parents." : T(EN_D);
  float cw = (w - 28 * 2 - 40) / 2, lx = rtl ? x + w - 28 - cw : x + 28, rx = rtl ? x + 28 : x + w - 28 - cw, ty = y + 22; /* two halves: TMDB, Trakt */
  for (int k = 0; k < 2; k++) { float cx = k ? rx : lx, tx = rtl ? cx + cw : cx; const char *nm = k ? "Trakt" : "TMDB";
    float nw = text_width(nm, 18, 700, 1.0f) + 20; gfx_rrect(rtl ? tx - nw : tx, ty, nw, 28, 14, k ? acc_a(0.3f) : RGBA(0x0d, 0x25, 0x3f, 220)); text_draw(nm, rtl ? tx - nw / 2 : tx + nw / 2, ty + 14 - text_line_height(18) / 2.0f, 18, 700, k ? ACC3 : RGB(0x1e, 0xd5, 0xa3), 1.0f, 0, ALIGN_CENTER);
    text_draw(T(k ? "Ratings and comments" : "Titles in your language, reviews"), rtl ? tx - nw - 14 : tx + nw + 14, ty + 14 - text_line_height(20) / 2.0f, 20, 600, WHITE_A(200), 0, cw - nw - 14, al); }
  float by = ty + 28 + 14;
  /* TMDB: the card in English, then the language sweeping in (5 s loop); its review under it */
  float ph = fmodf(t, 5.0f) / 5.0f, k = ph < 0.18f ? 0 : ph < 0.42f ? ease_apply(EASE_IN_OUT, (ph - 0.18f) / 0.24f) : ph < 0.92f ? 1 : 1 - ease_apply(EASE_IN_OUT, (ph - 0.92f) / 0.08f), ch = 192;
  if (k < 1) { gfx_clip_push(rtl ? lx : lx + cw * k, by, cw * (1 - k), ch); tmdb_card(lx, by, cw, ch, T("Without a key"), EN_T, EN_D, 0, ph > 0.92f ? 1 - (ph - 0.92f) / 0.08f : 1); gfx_clip_pop(); }
  if (k > 0) { gfx_clip_push(rtl ? lx + cw * (1 - k) : lx, by, cw * k, ch); tmdb_card(lx, by, cw, ch, T("With your key"), LT, LD, 1, 1); gfx_clip_pop(); }
  if (k > 0.01f && k < 0.99f) { float sx = rtl ? lx + cw * (1 - k) : lx + cw * k; gfx_rrect(sx - 1.5f, by + 10, 3, ch - 20, 1.5f, ACC); gfx_glow_round(sx - 40, by, 80, ch, col_alpha(ACC, 0.5f), lx, by, cw, ch, 16); } /* the line running ahead */
  revpanel_draw_comment(lx, by + ch + 12, cw, revpanel_sample(1), 0, revpanel_comment_lines(revpanel_sample(1), cw, 2), 1);
  /* Trakt: the score with its spread, a comment; the bars grow in as the loop starts */
  float g = ease_apply(EASE_OUT, fmodf(t, 5.0f) / 0.9f); int d[10]; const int *sd = revpanel_sample_dist(); for (int i = 0; i < 10; i++) d[i] = (int)(sd[i] * (0.05f + 0.95f * g) + 0.5f);
  float sh = revpanel_draw_score(rx, by + 2, cw, 8.2f, "Trakt", 23140, d, 0);
  revpanel_draw_comment(rx, by + sh + 14, cw, revpanel_sample(0), 0, revpanel_comment_lines(revpanel_sample(0), cw, 2), 1);
  { const char *pv = T("Preview"); float tw2 = text_width(pv, 16, 700, 1.2f) + 22, tx2 = rtl ? x + 12 : x + w - 12 - tw2; gfx_rrect(tx2, y + 8, tw2, 26, 13, BLACK_A(130)); gfx_ring(tx2, y + 8, tw2, 26, 13, 1, WHITE_A(50)); text_draw(pv, tx2 + tw2 / 2, y + 21 - text_line_height(16) / 2.0f, 16, 700, WHITE_A(220), 1.2f, 0, ALIGN_CENTER); }
  gfx_ring(x, y, w, h, 18, 1.5f, WHITE_A(40));
}
/* Appearance: the page in the chosen colour (its two glows, a lit row, the player's bar) over a title from Home, and the
   colours to choose from on a glass strip, the ring gliding to the one chosen. "From the logo" and "From the artwork":
   titles from Home in turn, each giving its own colour, a few drops of it flowing from its picture into the page */
static Tween lookShift; /* the page's row moving aside for the title's picture (the dynamic modes) */
static Item lookIt[4]; static char lookLogo[4][512]; static int lookN = -1; static Tween lookSel; static int lookSelSet; static uint32_t lookFrom[2], lookTo[2]; static double lookAt; static int lookCur = -1;
static void preview_look(float x, float y, float w, float h) {
  const char *a = store_get("accent", "logo"); int rtl = app_rtl(), dyn = !strcmp(a, "logo") || !strcmp(a, "adaptive"), logoMode = !strcmp(a, "logo"); float t = (float)app_now();
  if (lookN <= 0) { static double tried; if (app_now() - tried > 1.5) { char rn[8]; tried = app_now(); lookN = board_sample(lookIt, 4, rn, 0); for (int i = 0; i < lookN; i++) sb_logo_of(&lookIt[i], lookLogo[i], sizeof lookLogo[i]); } }
  /* the colour shown: the accent, or (dynamic) each title's own in turn; a title still being read keeps the last one */
  static const uint32_t STAND[4][2] = { { 0xffe0634a, 0xffffa38f }, { 0xff3d8bfd, 0xff9cc4ff }, { 0xff19b38a, 0xff7fe3c6 }, { 0xffd4a017, 0xfff3d27a } }; /* (while a title's own colour is read) */
  uint32_t c1 = ACC | 0xff000000u, c2 = ACC2 | 0xff000000u; int ti = -1; float ph = 0;
  if (dyn && lookN > 0) { ti = (int)(t / 3.4f) % lookN; ph = fmodf(t, 3.4f) / 3.4f; uint32_t tc[4];
    if (ti != lookCur) { lookFrom[0] = lookTo[0] ? lookTo[0] : c1; lookFrom[1] = lookTo[1] ? lookTo[1] : c2; lookCur = ti; lookAt = app_now(); }
    if (accent_tint_of(lookIt[ti].id, lookIt[ti].poster, lookLogo[ti], tc)) { lookTo[0] = 0xff000000u | tc[0]; lookTo[1] = 0xff000000u | tc[1]; }
    else { lookTo[0] = STAND[ti % 4][0]; lookTo[1] = STAND[ti % 4][1]; }
    float e = ease_apply(EASE_IN_OUT, (float)(app_now() - lookAt - 0.6f) / 0.8f); c1 = col_mix(lookFrom[0], lookTo[0], e); c2 = col_mix(lookFrom[1], lookTo[1], e); }
  prev_frame(x, y, w, h, 0.72f);
  gfx_clip_push(x, y, w, h);
  gfx_glow_round(x - w * 0.35f + 30 * sinf(t * 0.6f), y - h * 0.9f, w * 0.9f, h * 2.2f, col_alpha(col_mix(c1, 0xffffffffu, 0.1f), 0.42f), x, y, w, h, 18); /* (inside the corners) */
  gfx_glow_round(x + w * 0.45f - 30 * cosf(t * 0.5f), y - h * 0.2f, w * 0.9f, h * 2.2f, col_alpha(c1, 0.3f), x, y, w, h, 18);
  float sh = 64, sy = y + h - 16 - sh, mid = y + (sy - y) / 2, rh = 56, top = mid - rh / 2, artW = 100, ah = artW * 1.5f;
  tween_to(&lookShift, ti >= 0 ? 1.0f : 0.0f, 0.4f, GLIDE_EASE); tween_tick(&lookShift, app_dt());
  float mx = x + 28 + (artW + 30) * lookShift.v * (rtl ? 0 : 1);
  if (ti >= 0) { /* the title whose colour it is (its poster, or its logo), coming in, then its colour flowing out of it */
    const Item *it = &lookIt[ti]; float axp = rtl ? x + w - 28 - artW : x + 28, atop = mid - ah / 2, in = ease_apply(EASE_OUT, ph / 0.1f), out2 = ph > 0.95f ? (ph - 0.95f) / 0.05f : 0, fl = 4 * sinf(t * 1.6f);
    float top0 = top; top = atop; /* (the picture's own top) */
    gfx_opacity_push(in * (1 - out2) * lookShift.v); gfx_offset_push((rtl ? 1 : -1) * 24 * (1 - in), fl);
    if (logoMode) { float asp; GLuint lt = lookLogo[ti][0] ? img_get(lookLogo[ti], 300, 120, &asp) : 0; float lw = artW + 40, lh2 = 0;
      if (lt && asp > 0) { lh2 = lw / asp; if (lh2 > ah) { lh2 = ah; lw = lh2 * asp; } gfx_image(lt, axp + artW / 2 - lw / 2, top + ah / 2 - lh2 / 2, lw, lh2, 0, WHITE_A(255), 0, 0, 1, 1); }
      else text_draw_wrapped(it->name, axp + artW / 2, top + ah / 2 - 30, artW + 30, 2, 26, 800, WHITE_A(255), -0.4f, ALIGN_CENTER, 30); }
    else { char pu[512]; GLuint pt = sb_poster_of(it, pu, sizeof pu)[0] ? img_get_shaped(pu, (int)artW, (int)ah, 10, 0) : 0; /* the poster alone: no badges */
      gfx_shadow(axp, top + 8, artW, ah, 10, 18, BLACK_A(120)); if (!ph_picture(pt, axp, top, artW, ah, 10, WHITE_A(255))) ph_box(axp, top, artW, ah, 10); gfx_ring(axp, top, artW, ah, 10, 2, col_alpha(c1, 0.9f)); }
    gfx_offset_pop(); gfx_opacity_pop();
    float u = (ph - 0.2f) / 0.35f; if (u > 0 && u < 1.3f) for (int k = 0; k < 6; k++) { float uu = u - k * 0.07f; if (uu <= 0 || uu >= 1) continue; float e = ease_apply(EASE_IN_OUT, uu);
        float sx = axp + artW / 2, sy2 = top + ah / 2, ex = rtl ? axp - 70 : axp + artW + 70, ey = mid, px2 = sx + (ex - sx) * e, py2 = sy2 + (ey - sy2) * e - 44 * sinf(3.14159f * e);
        gfx_circle(px2, py2, 8 - k, col_alpha(lookTo[0], 1 - 0.14f * k)); }
    top = top0; }
  /* a small copy of the app in the colour (a picture only: tagged Preview, its pieces small): the tabs with the
     accent under the current one, then a Play button, a switch, a season chip and the player's bar */
  float mw = x + w - 28 - mx - (rtl ? (artW + 30) * lookShift.v : 0), pulse = ti >= 0 ? 1 + 0.05f * sinf(3.14159f * (ph < 0.5f ? 0 : (ph - 0.5f) / 0.3f > 1 ? 1 : (ph - 0.5f) / 0.3f)) : 1;
  { const char *pv = T("Preview"); float tw2 = text_width(pv, 18, 700, 1.2f) + 24, tx2 = rtl ? x + 14 : x + w - 14 - tw2; /* (the free corner) */
    lglass_shell(tx2, y + 12, tw2, 30, 15, BLACK_A(120), 0.9f); text_draw(pv, tx2 + tw2 / 2, y + 27 - text_line_height(18) / 2.0f, 18, 700, WHITE_A(220), 1.2f, 0, ALIGN_CENTER); }
  /* the bar, as the app draws it: one piece of liquid glass behind the three tabs, a light wandering it and bending the
     picture under it, and the focus gliding from tab to tab as accent glass that stretches as it goes */
  static const char *TB[3] = { "Discover", "Home", "Library" }; float tbw[3], tot = 0; for (int i = 0; i < 3; i++) { tbw[i] = text_width(T(TB[i]), 20, 700, 0) + 28; tot += tbw[i]; }
  float txx = mx + mw / 2 - tot / 2, tyy = y + 30, ch = 40, cx0 = txx - 6, cwid = tot + 12; GLuint gt = prev_glass(w, h);
  { float su = 0.5f + 0.4f * (0.55f * sinf(t * 0.27f) + 0.3f * sinf(t * 0.43f + 2.0f) + 0.15f * sinf(t * 0.71f + 4.4f)), svv = 0.32f + 0.1f * sinf(t * 0.33f + 1.0f);
    gfx_shadow(cx0, tyy - ch / 2 + 6, cwid, ch, ch / 2, 20, BLACK_A(64));
    if (gt) { gfx_liquid_spot(su, svv, 1.0f); prev_liquid(gt, x, y, w, h, cx0, tyy - ch / 2, cwid, ch, fminf(ch * 0.5f, 16), 0); gfx_rrect(cx0, tyy - ch / 2, cwid, ch, ch / 2, RGBA(10, 10, 16, 18)); }
    lglass_light(su - 0.12f); lglass_shell(cx0, tyy - ch / 2, cwid, ch, ch / 2, 0, 0.85f); lglass_light(0);
    gfx_rrect(cx0, tyy - ch / 2, cwid, ch, ch / 2, col_alpha(c1, 0.08f)); }
  { static Tween fx, fw; static float sv, lx; static int seen; int fi = (int)(t / 2.2f) % 3; float px = txx; for (int i = 0; i < fi; i++) px += tbw[i]; /* the focus moves on every 2.2 s */
    if (!seen) { tween_set(&fx, px); tween_set(&fw, tbw[fi]); seen = 1; } else { if (fx.to != px) tween_to(&fx, px, 0.32f, GLIDE_EASE); if (fw.to != tbw[fi]) tween_to(&fw, tbw[fi], 0.32f, GLIDE_EASE); } tween_tick(&fx, app_dt()); tween_tick(&fw, app_dt());
    float x0 = lx; lglass_stretch(&sv, &lx, fx.v, app_dt()); float dir = fx.v > x0 ? 1 : fx.v < x0 ? -1 : 0, sw = fw.v * (1 + 0.22f * sv), sh = 34 * (1 - 0.10f * sv), ppx = fx.v + fw.v / 2 - sw / 2, ppy = tyy - sh / 2;
    if (gt) prev_liquid(gt, x, y, w, h, ppx, ppy, sw, sh, fminf(sh * 0.6f, 20) * (1 + 0.25f * sv), -dir * sv * 4.0f / w);
    gfx_rrect_v(ppx, ppy, sw, sh, sh / 2, col_alpha(c2, 0.46f), col_alpha(c1, 0.36f)); gfx_rrect_v(ppx, ppy, sw, sh, sh / 2, WHITE_A(40), WHITE_A(0)); gfx_ring(ppx, ppy, sw, sh, sh / 2, 1.2f, WHITE_A(70));
    float tx3 = txx; for (int i = 0; i < 3; i++) { int f = i == fi; text_draw(T(TB[i]), tx3 + tbw[i] / 2, tyy - text_line_height(20) / 2.0f, 20, f ? 700 : 500, f ? WHITE_A(250) : WHITE_A(170), 0, 0, ALIGN_CENTER); tx3 += tbw[i]; } }
  const char *pl = T("Play"), *sn = T1("Season {n}", "n", "1"); float plw = 40 + text_width(pl, 20, 700, 0) + 18, snw = text_width(sn, 18, 600, 0) + 30;
  float ey = mid - 6, eh = 40, ex = mx + mw / 2 - (plw + 18 + 46 + 18 + snw) / 2; /* centred under the tabs */
  gfx_scale_push(pulse, pulse, mx + mw / 2, ey);
  { float pw = plw; gfx_rrect(ex, ey - eh / 2, pw, eh, eh / 2, c1); icon_draw("play", ex + 14, ey - 10, 20, WHITE_A(255)); text_draw(pl, ex + 40, ey - text_line_height(20) / 2.0f, 20, 700, WHITE_A(255), 0, 0, ALIGN_LEFT); ex += pw + 18; }
  { gfx_rrect(ex, ey - 13, 46, 26, 13, c1); gfx_circle(ex + 46 - 13, ey, 10, WHITE_A(255)); ex += 46 + 18; } /* a switch, on */
  { float cw2 = snw; gfx_rrect(ex, ey - 16, cw2, 32, 16, WHITE_A(24)); gfx_ring(ex, ey - 16, cw2, 32, 16, 2, c2); text_draw(sn, ex + cw2 / 2, ey - text_line_height(18) / 2.0f, 18, 600, WHITE_A(240), 0, 0, ALIGN_CENTER); ex += cw2 + 22; }
  gfx_scale_pop();
  /* the player's bar, small: its play mark, the times, the progress in the colour */
  float by = ey + 46, bx0 = mx, bw0 = mw, bp = 0.3f + 0.5f * (0.5f + 0.5f * sinf(t * 0.8f));
  icon_draw("p_pause", bx0, by - 9, 18, WHITE_A(220)); text_draw("1:02:15", bx0 + 26, by - text_line_height(16) / 2.0f, 16, 600, WHITE_A(200), 0, 0, ALIGN_LEFT);
  float tbx = bx0 + 100, tbw2 = bw0 - 100 - 64; gfx_rrect(tbx, by - 2, tbw2, 4, 2, WHITE_A(51)); gfx_rrect_h(tbx, by - 2, tbw2 * bp, 4, 2, c1, c2); gfx_circle(tbx + tbw2 * bp, by, 6, WHITE_A(255));
  text_draw("-38:41", bx0 + bw0, by - text_line_height(16) / 2.0f, 16, 600, WHITE_A(170), 0, 0, ALIGN_RIGHT);
  /* the colours: the ring glides to the one chosen, the chosen one grows */
  int sel = 0; for (int i = 0; i < 20; i++) if (!strcmp(ACCENT_OPTS[i].v, a)) sel = i;
  if (!lookSelSet) { tween_set(&lookSel, (float)sel); lookSelSet = 1; } else if (lookSel.to != sel) tween_to(&lookSel, (float)sel, 0.32f, GLIDE_EASE); tween_tick(&lookSel, app_dt());
  gfx_rrect(x + 14, sy, w - 28, sh, 32, RGBA(18, 16, 26, 200)); gfx_ring(x + 14, sy, w - 28, sh, 32, 1, WHITE_A(36));
  float step = (w - 28 - 40) / 20.0f, ccy = sy + sh / 2;
  #define SWX(i) (rtl ? x + w - 34 - step / 2 - (i) * step : x + 34 + step / 2 + (i) * step)
  for (int i = 0; i < 20; i++) { float near = 1 - fabsf(i - lookSel.v); near = near < 0 ? 0 : near; float ccx = SWX(i), r = 12 + 4 * near;
    if (i == 0) gfx_rrect_h(ccx - r, ccy - r, 2 * r, 2 * r, r, RGB(0xff, 0xc3, 0x71), RGB(0xff, 0x5f, 0x6d));
    else if (i == 1) gfx_rrect_h(ccx - r, ccy - r, 2 * r, 2 * r, r, RGB(0xff, 0x8e, 0x6f), RGB(0x76, 0xa8, 0xe8));
    else if (i == 19) { gfx_rrect_v(ccx - r, ccy - r, 2 * r, 2 * r, r, RGB(0xd8, 0xdd, 0xe6), RGB(0x6a, 0x72, 0x85)); gfx_rrect_v(ccx - r * 0.8f, ccy - r * 0.9f, r * 1.6f, r * 0.9f, r * 0.45f, WHITE_A(90), WHITE_A(0)); gfx_ring(ccx - r, ccy - r, 2 * r, 2 * r, r, 1, WHITE_A(110)); } /* graphite: a little glass */
    else { extern const uint32_t *settings_accent_rgb(int); gfx_circle(ccx, ccy, r, 0xff000000u | settings_accent_rgb(i - 2)[0]); } }
  { float rx = SWX(0) + (SWX(19) - SWX(0)) * lookSel.v / 19.0f, r = 16; gfx_ring_out(rx - r, ccy - r, 2 * r, 2 * r, r, 3, WHITE_A(255)); } /* the ring, gliding */
  #undef SWX
  gfx_clip_pop();
  gfx_ring(x, y, w, h, 18, 1.5f, WHITE_A(40));
}
/* About: the app's icon, name, version and update state, then what the new version brings */
#define AB_ICO 140.0f
static float about_hd(void) { float b = text_line_height(46) + 4 + text_line_height(28) + 14 + 16 + text_line_height(26); return b > AB_ICO ? b : AB_ICO; } /* the head: icon and words, centred */
static float about_h(void) {
  static float h; static unsigned ser = ~0u; static const char *lang; if (ser == update_serial() && lang == i18n_lang()) return h; /* measured once per change, not per frame */
  ser = update_serial(); lang = i18n_lang(); int n = update_note_count(); h = 28 + about_hd() + 28; if (n) { h += 22 + 20 + text_line_height(26) + 12; for (int i = 0; i < n; i++) h += text_wrap_count(update_note(i), CLW - 60 - 24, 3, 26, 400, 0) * 34 + 8; } return h; }
static void wn_focus(int a, int b) { (void)a; (void)b; S.zone = 1; wnBtn = 1; }
static void wn_act(int a, int b) { (void)a; (void)b; welcome_show_whatsnew(); }
static void preview_about(float x, float y, float w) {
  int rtl = app_rtl(), al = rtl ? ALIGN_RIGHT : ALIGN_LEFT; float h = about_h(); const char *av = update_avail(); int st = update_state();
  gfx_rrect(x, y, w, h, 18, RGB(0x1c, 0x1a, 0x24));
  static char icon[1100]; if (!icon[0]) snprintf(icon, sizeof icon, "file://%s/icon.png", plat_asset_dir());
  float ix = rtl ? x + w - 30 - AB_ICO : x + 30, asp; GLuint t = img_get(icon, 256, 256, &asp); if (t) gfx_image(t, ix, y + 28 + (about_hd() - AB_ICO) / 2, AB_ICO, AB_ICO, 0, WHITE_A(255), 0, 0, 1, 1);
  const char *stt = av ? T1("Version {v} is available", "v", av) : st == UPD_CHECKING ? T("Checking for updates…") : st == UPD_ERROR ? T("Could not check for updates") : T("Up to date");
  float sh = 16 + text_line_height(26), bh = text_line_height(46) + 4 + text_line_height(28) + 14 + sh, hd = about_hd(), ty = y + 28 + hd / 2 - bh / 2, tx = rtl ? ix - 30 : ix + AB_ICO + 30;
  text_draw("Stremio Origin", tx, ty, 46, 800, WHITE_A(255), -0.8f, 0, al); ty += text_line_height(46) + 4;
  text_draw(T1("Version {v}", "v", update_current()[0] ? update_current() : "…"), tx, ty, 28, 500, WHITE_A(200), 0, 0, al); ty += text_line_height(28) + 14;
  float sw = 40 + text_width(stt, 26, 700, 0), sx = rtl ? tx - sw : tx; gfx_rrect(sx, ty, sw, sh, sh / 2, av ? ACC : WHITE_A(24)); text_draw(stt, sx + 20, ty + sh / 2 - text_line_height(26) / 2.0f, 26, 700, av ? ACC_FG : WHITE_A(230), 0, 0, ALIGN_LEFT);
  { const char *wl = T("What\xe2\x80\x99s new"); float ww = 40 + 26 + 10 + text_width(wl, 26, 700, 0), wx = rtl ? sx - 14 - ww : sx + sw + 14; int f = wnBtn; /* the version's page again */
    if (f) { lglass_accent(wx, ty, ww, sh, sh / 2, 1); } else { gfx_rrect(wx, ty, ww, sh, sh / 2, WHITE_A(14)); gfx_ring(wx, ty, ww, sh, sh / 2, 1, WHITE_A(46)); }
    icon_draw("g_spark", wx + 18, ty + sh / 2 - 13, 26, f ? ACC_FG : ACC2); text_draw(wl, wx + 18 + 26 + 10, ty + sh / 2 - text_line_height(26) / 2.0f, 26, 700, f ? ACC_FG : WHITE_A(240), 0, 0, ALIGN_LEFT);
    hit_add(wx, ty, ww, sh, wn_focus, wn_act, 0, 0); }
  int n = update_note_count(); if (!n) return;
  float ny = y + 28 + about_hd() + 22; gfx_rect(x + 30, ny, w - 60, 1, WHITE_A(20)); ny += 20;
  text_draw(T1("What’s new in {v}", "v", av), rtl ? x + w - 30 : x + 30, ny, 26, 600, WHITE_A(230), 0, w - 60, al); ny += text_line_height(26) + 12;
  for (int i = 0; i < n; i++) { const char *nt = update_note(i); float lw = w - 60 - 24; int k = text_wrap_count(nt, lw, 3, 26, 400, 0);
    gfx_circle(rtl ? x + w - 30 - 6 : x + 30 + 6, ny + 17, 3.5f, WHITE_A(140));
    text_draw_wrapped(nt, rtl ? x + w - 30 - 24 : x + 30 + 24, ny + (34 - text_line_height(26)) / 2, lw, 3, 26, 400, WHITE_A(204), 0, al, 34); ny += k * 34 + 8; }
}
/* a USB notice: Playback, on the USB drive's row (there it is switched on) */
void settings_open_usb(void) { for (int i = 0; i < ncat; i++) if (cats[i].opts == playS) { int c = S.cat; S.cat = i; S.zone = 1; S.idx = iUsb; if (S.idx >= cats[i].n) S.idx = cats[i].n - 1; show_cat(i, i >= c ? 1 : -1); return; } }
void settings_open_keys(void) { for (int i = 0; i < ncat; i++) if (cats[i].opts == keyS) { int c = S.cat; S.cat = i; S.zone = 1; S.idx = 0; show_cat(i, i >= c ? 1 : -1); return; } }
void settings_dev_open(const char *spec) { /* (development: SB_OPEN="settings:<page>[:key=value;...]") */
  char b[300]; snprintf(b, sizeof b, "%s", spec); char *kv = strchr(b, ':'); if (kv) { *kv++ = 0; for (char *t = strtok(kv, ";"); t; t = strtok(NULL, ";")) { char *e = strchr(t, '='); if (e) { *e = 0; store_set(t, e + 1); } } }
  for (int i = 0; i < ncat; i++) if (!strcmp(cats[i].lb, b)) { S.cat = i; S.zone = cats[i].out ? 0 : 1; S.idx = 0; show_cat(i, 1); return; } }
void settings_open_about(void) { for (int i = 0; i < ncat; i++) if (cats[i].opts == supS) { int c = S.cat; S.cat = i; S.zone = 1; S.idx = 0; show_cat(i, i >= c ? 1 : -1); return; } }
static void cat_focus(int i, int b) { (void)b; int w = topNav.active; topNav.active = 0; if (S.cat != i) { int c = S.cat; S.cat = i; S.idx = 0; show_cat(i, i > c ? 1 : -1); w = 1; } if (w || S.zone != 0) S.zone = 0; }
static void opt_focus(int i, int b) { (void)b; int w = topNav.active; topNav.active = 0; if (S.cat != S.shown) return; if (w || S.zone != 1 || S.idx != i) { S.zone = 1; S.idx = i; } }
static void pick_focus(int i, int b) { (void)b; if (S.pick != i) S.pick = i; }
static void pick_outside(int a, int b) { (void)a; (void)b; close_pick(); }
/* text centred on a CSS line box of height L */
static void line(const char *s, float x, float y, float L, int px, int wt, uint32_t c, float tr, float maxW, int al) { text_draw(s, x, y + (L - text_line_height(px)) / 2, px, wt, c, tr, maxW, al); }
/* the edges of a scrolled list fade where there is more to see: the panel's own colour laid over them */
static void edge_fades(float x, float y, float w, float h, int top, int bottom) {
  if (top) gfx_rrect_v(x, y, w, 40, 0, RGBA(21, 19, 28, 170), RGBA(21, 19, 28, 0));
  if (bottom) gfx_rrect_v(x, y + h - 72, w, 72, 0, RGBA(21, 19, 28, 0), RGBA(21, 19, 28, 170));
}
/* the support row: its words on the start side, larger, and the code to scan on a white tile on the other, always
   there (OK shows it bigger, on a card) */
static void draw_support(const Setting *o, float x, float y, float h, int f, float nx, float ny) {
  int rtl = app_rtl(), al = rtl ? ALIGN_RIGHT : ALIGN_LEFT; const char *gh = welcome_github_url(); tween_tick(&supK, app_dt());
  float Q = (h - 48 - 56 - 10) / 1.06f; if (Q > 340) Q = 340; /* (no address under it here) */ float PW = support_w(Q); float qx = rtl ? x + 24 : x + CLW - 24 - PW, qy = y + 24, tw = CLW - 48 - PW - 40, L = rtl ? x + CLW - 24 : x + 24;
  x += nx; y += ny; qx += nx; qy += ny; L += nx;
  float TLH = 30 * 1.18f; int tn = text_wrap_count(T(o->lb), tw - 42, 2, 30, 800, -0.4f); if (tn < 1) tn = 1; /* (the title on two lines when it needs them: Support the project / Request a feature) */
  float th = tn * TLH, hd = th + 10 + 3 * DS_LH + (gh[0] ? 16 + 40 : 0), ty = y + h / 2 - hd / 2;
  icon_draw("g_heart", rtl ? L - 30 : L, ty + (TLH - 30) / 2 + 2, 30, f ? ACC_FG : ACC2); { const char *tl = T(o->lb), *sl = strstr(tl, " / "); float tx2 = rtl ? L - 42 : L + 42, ty2 = ty + (TLH - text_line_height(30)) / 2; uint32_t tc = f ? ACC_FG : WHITE_A(255);
    if (tn > 1 && sl) { char a[200]; snprintf(a, sizeof a, "%.*s /", (int)(sl - tl), tl); text_draw(a, tx2, ty2, 30, 800, tc, -0.4f, tw - 42, al); text_draw(sl + 3, tx2, ty2 + TLH, 30, 800, tc, -0.4f, tw - 42, al); } /* (two lines: split where its two halves meet) */
    else text_draw_wrapped(tl, tx2, ty2, tw - 42, 2, 30, 800, tc, -0.4f, al, TLH); }
  int nl = text_draw_wrapped(T(o->ds), L, ty + th + 10, tw, 3, DS_PX, 500, f ? col_alpha(ACC_FG, 0.9f) : WHITE_A(210), 0, al, DS_LH);
  if (gh[0]) key_hint(L, ty + th + 10 + nl * DS_LH + 16 + 20, K_OK, supWant ? T("Support on Ko-fi") : T("Star on GitHub"), f ? ACC_FG : WHITE_A(200), rtl);
  support_glass(0); support_draw(qx, qy, Q, gh[0] ? supK.v : 0, f, welcome_support_url(), NULL, gh, NULL);
}
/* the community: Discord's mark in its colour beside the row's words; OK opens its code on a card like the others */
#define DISC_B 36.0f /* (inside an ordinary row: Join our Discord is as tall as the others, only Support stays big) */
static void disc_badge(float x, float y, int f) { gfx_rrect(x, y, DISC_B, DISC_B, DISC_B * 0.28f, RGB(0x58, 0x65, 0xf2)); if (f) gfx_ring(x, y, DISC_B, DISC_B, DISC_B * 0.28f, 2, WHITE_A(200)); icon_draw("discord", x + DISC_B * 0.18f, y + DISC_B * 0.18f, DISC_B * 0.64f, WHITE_A(255)); }
static void discord_card(void) {
  QrCardOpts o; memset(&o, 0, sizeof o); o.title = T("Join our Discord"); o.sub = T("News, help and requests: scan with your phone"); o.url = DISCORD_URL;
  o.badge = "discord"; o.badgeBg = RGB(0x58, 0x65, 0xf2); qrcard_show(&o);
}
static void draw_row(const Setting *o, int i, float x, float y, float h, int f, float nx, float ny) {
  if (!strcmp(o->k, "__support")) { draw_support(o, x, y, h, f, nx, ny); return; }
  int rtl = app_rtl(), al = rtl ? ALIGN_RIGHT : ALIGN_LEFT, ar = rtl ? ALIGN_LEFT : ALIGN_RIGHT; RowA *r = &S.ra[i];
  float L = rtl ? x + CLW - 24 : x + 24, R = rtl ? x + 24 : x + CLW - 24; L += nx; R += nx; y += ny;
  float fk = r->fk.v; /* (how focused: the colours turn with the highlight) */
  if (pv_kind(o)) { /* a preview row: its screen on the far side (playing while focused, a still a little dimmer otherwise), its value left of it */
    float sx = rtl ? R : R - PVR_W, sy = y + (h - PVR_H) / 2; gfx_opacity_push(0.8f + 0.2f * fk); /* (the still, then the scene playing over it as the focus comes; going, it fades back to the still) */
    if (fk < 0.999f) pv_draw(pv_kind(o), sx, sy, PVR_W, PVR_H, 1, 0); if (fk > 0.001f) pv_draw(pv_kind(o), sx, sy, PVR_W, PVR_H, fk, 1); gfx_opacity_pop();
    R += rtl ? PVR_W + 24 : -(PVR_W + 24); }
  const RowC *rc = i < MAXROWS ? row_c(o, i) : NULL; if (!rc) return;
  int busy = rc->busy, sw = is_switch(o) && !busy; float rw = rc->rw[f ? 1 : 0], tw = CLW - 48 - rw;
  uint32_t lc = col_mix(o->action ? ACC3 : WHITE_A(255), ACC_FG, fk), dc = col_mix(WHITE_A(199), col_alpha(ACC_FG, 0.85f), fk);
  int trk = !strcmp(o->k, "__traktUnlink") || !strcmp(o->k, "__discord") || !strncmp(o->k, "__lang:", 7);
  if (!strncmp(o->k, "__lang:", 7) && !strcmp(o->k + 7, store_get("uiLang", "auto"))) icon_draw("check", rtl ? R : R - 28, y + h / 2 - 14, 28, col_mix(ACC3, ACC_FG, fk)); /* (the language in use) */ if (trk) lc = col_mix(WHITE_A(255), ACC_FG, fk); /* Trakt's rows read like any setting (not the action colour); its mark says whose they are */
  if (o->off) { lc = col_mix(WHITE_A(110), WHITE_A(235), fk); dc = col_mix(WHITE_A(96), WHITE_A(200), fk); }
  if (o->danger) { lc = col_mix(RGB(0xef, 0x8f, 0x8a), WHITE_A(255), fk); gfx_rect(x + 24, y - ROWGAP / 2.0f - 9, CLW - 48, 1, WHITE_A(40)); /* (a line above it, as over Log out) */
    icon_draw("set_out", rtl ? L - 30 : L, y + h / 2 - 15, 30, lc); L += rtl ? -46 : 46; tw -= 46; } /* greyed: dim, but plain to read when focused (on a neutral highlight) */
  /* the words: changed ones fade in where they stand */
  float tk = since(r->txAt) < 0.3f ? ease_apply(EASE_SNAP, since(r->txAt) / 0.3f) : 1; if (tk < 1) { gfx_opacity_push(tk); gfx_offset_push(0, 5 * (1 - tk)); }
  if (o->sub) { /* joined to the row above: a thin line down from it that turns in towards the words */
    float d = rtl ? -1 : 1, gx = L + d * 12, ey = y + 16 + 17, rr = 12, top = y - 2, ex = L + d * (SUBIN - 10); uint32_t jc = col_mix(WHITE_A(92), col_alpha(ACC, 0.95f), fk);
    gfx_rrect(gx - 1, top, 2, ey - rr - top, 1, jc);
    for (int k = 0; k <= 14; k++) { float t = k * 1.5708f / 14; gfx_circle(gx + d * (rr - rr * cosf(t)), ey - rr + rr * sinf(t), 1, jc); } /* the turn */
    gfx_rrect(rtl ? ex : gx + rr, ey - 1, fabsf(ex - (gx + d * rr)), 2, 1, jc);
    L += rtl ? -SUBIN : SUBIN; tw -= SUBIN; }
  if (!strcmp(o->k, "__discord")) { disc_badge(rtl ? L - DISC_B : L, y + h / 2 - DISC_B / 2, f); L += rtl ? -(DISC_B + 16) : DISC_B + 16; tw -= DISC_B + 16; } /* Discord's mark before its words */
  line(o->raw ? o->lb : T(o->lb), L, y + 16, 34, o->sub ? 26 : 28, 600, lc, -0.2f, tw, al);
  if (o->ds[0] && keep_ds(o)) { text_draw_wrapped(o->raw ? o->ds : T(o->ds), L, y + 16 + 34 + 4 + (DS_LH - text_line_height(DS_PX)) / 2, tw, 3, DS_PX, 400, dc, 0, al, DS_LH); }
  if (tk < 1) { gfx_offset_pop(); gfx_opacity_pop(); }
  if (busy) { /* testing: the speed so far, large, counting as it comes, and a ring filling over the ~7.6 s with a bright head */
    static float shown; float live = (float)best_speed_live(), k = clampf((float)best_speed_since() / 7.6f, 0, 1); if (live <= 0) shown = 0; else shown += (live - shown) * fminf(1, app_dt() * 5);
    uint32_t ac = f ? ACC_FG : ACC3, dim = f ? col_alpha(ACC_FG, 0.25f) : WHITE_A(36); float r = 22, cx = rtl ? R + r : R - r, cy = y + h / 2;
    for (int d = 0; d < 48; d++) { float an = -1.5708f + d * 6.2832f / 48; gfx_circle(cx + cosf(an) * r, cy + sinf(an) * r, 2.2f, dim); } /* the track */
    float ke = ease_apply(EASE_IN_OUT, k); int nd = (int)(ke * 96); for (int d = 0; d <= nd; d++) { float an = -1.5708f + d * 6.2832f / 96; gfx_circle(cx + cosf(an) * r, cy + sinf(an) * r, 2.6f, ac); }
    { float an = -1.5708f + ke * 6.2832f, pu = 0.5f + 0.5f * sinf((float)app_now() * 7); gfx_circle(cx + cosf(an) * r, cy + sinf(an) * r, 4.5f + 1.5f * pu, ac); } /* its head, breathing */
    float ux = rtl ? cx + r + 18 : cx - r - 18; const char *un = T("Mbps"); float uw = text_width(un, 22, 600, 0);
    text_draw(un, ux, cy + 4, 22, 600, f ? col_alpha(ACC_FG, 0.75f) : WHITE_A(160), 0, 0, rtl ? ALIGN_LEFT : ALIGN_RIGHT);
    char nb[16]; if (shown > 0) snprintf(nb, sizeof nb, shown < 10 ? "%.1f" : "%.0f", shown); else snprintf(nb, sizeof nb, "%s", (int)(app_now() * 3) % 3 == 0 ? "\xc2\xb7" : (int)(app_now() * 3) % 3 == 1 ? "\xc2\xb7\xc2\xb7" : "\xc2\xb7\xc2\xb7\xc2\xb7");
    float nx2 = rtl ? ux + uw + 8 : ux - uw - 8; text_draw(nb, nx2, cy - text_line_height(44) / 2 - 2, 44, 800, f ? ACC_FG : WHITE_A(255), -0.5f, 0, rtl ? ALIGN_LEFT : ALIGN_RIGHT); }
  else if (sw) { /* a glass switch: on a focused (accent) row the track turns white glass and the knob takes the accent */
    float sx = rtl ? R : R - 64, sy = y + h / 2 - 18, kk = r->swK.v, kc = r->swC.v; /* the knob goes right when on, in either direction (as the page) */
    lglass_switch(sx, sy, 64, 36, kk, kc, f); /* glass, as the bar and the player */
    if (!strcmp(o->k, "seekPreviews")) { /* the previews' hint, in front of the switch: where they come from (debrid links, never a torrent on a TV) */
      const char *hn = T("Debrid only"); float used = text_width(o->raw ? o->lb : T(o->lb), 28, 600, -0.2f), room = CLW - 48 - rw - used - 32, hx = rtl ? R + 64 + 22 : R - 64 - 22;
      if (room > 120) line(hn, hx, y + h / 2 - 17, 34, 22, 500, col_mix(WHITE_A(150), col_alpha(ACC_FG, 0.8f), fk), 0, room, rtl ? ALIGN_LEFT : ALIGN_RIGHT); } }
  else if (!o->action && !o->info) { /* the value, and under it where it stands among its choices: a dot each, the chosen one long (a short track for a long list); one changed with Left / Right slides in from that side and the long dot glides to it */
    const char *vt = rc->vt; int n = o->nopts, ci = -1; for (int j = 0; j < n && ci < 0; j++) if (o->opts && o->opts[j].v && !strcmp(o->opts[j].v, cur_val(o))) ci = j;
    int dots = !o->off && n >= 2 && ci >= 0; float vy = y + h / 2 - 17 - (dots ? 8 : 0), ve = since(r->vAt), vw = rc->vw;
    uint32_t vc = o->off ? col_mix(WHITE_A(87), WHITE_A(200), fk) : col_mix(ACC3, ACC_FG, fk);
    float vx = R; float va = 1, vd = 0;
    if (r->vDir && ve < 0.18f) { float e = ease_apply(EASE_SNAP, ve / 0.18f); va = e; vd = 16 * r->vDir * (1 - e) * (rtl ? -1 : 1); } else if (!r->vDir && ve < 0.2f) va = ease_apply(EASE_CSS_EASE, ve / 0.2f);
    if (va < 1) gfx_opacity_push(va); line(vt, vx + vd, vy, 34, 26, font_wt(o, cur_val(o), 600), vc, 0, 0, ar); if (va < 1) gfx_opacity_pop();
    if (dots) { if (!r->dotOn) { r->dotOn = 1; r->dotP = (float)ci; } r->dotP += (ci - r->dotP) * fminf(1, app_dt() * 16); if (fabsf(ci - r->dotP) < 0.002f) r->dotP = (float)ci;
      uint32_t on = col_mix(ACC3, ACC_FG, fk), offc = col_mix(WHITE_A(56), col_alpha(ACC_FG, 0.3f), fk); float dy = y + h / 2 + 13;
      if (n <= 7) { float dw = 6, lw = 16, g = 5, tot = (n - 1) * (dw + g) + lw, x0 = rtl ? R : R - tot, xx = 0;
        for (int j = 0; j < n; j++) { float k = fmaxf(0, 1 - fabsf(j - r->dotP)), w = dw + (lw - dw) * k, dx = rtl ? x0 + tot - xx - w : x0 + xx; gfx_rrect(dx, dy, w, 6, 3, col_mix(offc, on, k)); xx += w + g; } } /* (right to left: the first choice on the right) */
      else { float tw2 = 96, x0 = rtl ? R : R - tw2, th = fmaxf(14, tw2 / n), k = r->dotP / (n - 1), tx = rtl ? x0 + (tw2 - th) * (1 - k) : x0 + (tw2 - th) * k;
        gfx_rrect(x0, dy + 1.5f, tw2, 3, 1.5f, offc); gfx_rrect(tx, dy + 0.5f, th, 5, 2.5f, on); } } /* (a long list: a short track, its place on it) */
    { /* a brief hint in front of the value: Audio's Original (what it does) */
      const char *hn = !strcmp(o->k, "defaultAudioTrack") && !strcmp(cur_val(o), "orig") ? T("Each film in its own language") : NULL;
      if (hn) { float used = text_width(o->raw ? o->lb : T(o->lb), 28, 600, -0.2f), room = CLW - 48 - rw - used - 32, hx = rtl ? vx + vw + 22 : vx - vw - 22;
      if (room > 120) { if (va < 1) gfx_opacity_push(va); line(hn, hx, vy, 34, 22, 500, col_mix(WHITE_A(150), col_alpha(ACC_FG, 0.8f), fk), 0, room, ar); if (va < 1) gfx_opacity_pop(); } } } /* (it comes and goes with the value) */
    }
}
static void draw(void) {
  int rtl = app_rtl(), al = rtl ? ALIGN_RIGHT : ALIGN_LEFT; double now = app_now();
  /* the entrance: the pieces rise in once the screen is up (the panel itself stays) */
  float et = (float)(now - S.enterAt), fa = et < 0.42f ? ease_apply(EASE_CSS_EASE, et / 0.42f) : 1, fy = 22 * (1 - fa);
  /* ---- the categories ---- */
  float cx = MX(WX, CATW);
  gfx_offset_push(0, fy); gfx_clip_push(cx, WY, CATW, COLH); gfx_opacity_push(fa); gfx_offset_push(0, WY + S.catsY.v);
  hl_draw(&S.hc, cx, CATW, 0);
  for (int i = 0; i < ncat; i++) { const Cat *c = &cats[i];
    if (S.grpY[i] >= 0) { float d = et - stag(S.grpK[i]), e = d < 0.3f ? ease_apply(EASE_SNAP, d / 0.3f) : 1; if (e > 0.001f) { gfx_opacity_push(e);
      if (S.gap[i]) gfx_rect(cx + 20, S.grpY[i] + 10 * (1 - e), CATW - 40, 1, WHITE_A(26));
      else line(T(c->g), rtl ? cx + CATW - 20 : cx + 20, S.grpY[i] + 10 * (1 - e), 28, 22, 600, WHITE_A(158), 0, CATW - 40, al);
      gfx_opacity_pop(); } }
    float d = et - stag(S.catK[i]), e = d < 0.3f ? ease_apply(EASE_SNAP, d / 0.3f) : 1; if (e <= 0.001f) continue;
    int f = S.zone == 0 && i == S.cat, cur = i == S.cat && !f; float y = S.catY[i] + 10 * (1 - e), nx = f ? nudge_x(&S.nudge) : 0, ny = f ? nudge_y(&S.nudge) : 0;
    if (S.zone != 2) hit_add(cx, S.catY[i], CATW, 64, cat_focus, NULL, i, 0);
    float cf = i < 32 ? catFk[i].v : f, cc2 = i < 32 ? catCk[i].v : cur; (void)cur; /* (eased: the colours turn as the highlight moves) */
    uint32_t tc = c->out ? col_mix(RGB(0xef, 0x8f, 0x8a), WHITE_A(255), cf) : col_mix(col_mix(WHITE_A(204), WHITE_A(255), cc2), ACC_FG, cf);
    uint32_t ic = c->out ? tc : col_mix(col_mix(WHITE_A(140), ACC3, cc2), ACC_FG, cf);
    gfx_opacity_push(e);
    icon_draw(c->ic, (rtl ? cx + CATW - 20 - 30 : cx + 20) + nx, y + 17 + ny, 30, ic);
    line(T(c->lb), (rtl ? cx + CATW - 70 : cx + 70) + nx, y + ny, 64, 27, 600, tc, -0.2f, CATW - 90 - (c->opts == supS && update_avail() ? 30 : 0), al);
    if (c->opts == supS && update_avail()) gfx_circle((rtl ? cx + 22 + 5 : cx + CATW - 22 - 5) + nx, y + 32 + ny, 5, f ? ACC_FG : ACC2); /* an update is waiting */
    gfx_opacity_pop(); }
  gfx_offset_pop(); gfx_opacity_pop(); gfx_clip_pop(); gfx_offset_pop();
  /* ---- the panel: see-through, the drifting light shows faintly ---- */
  float px = MX(PNX, PNW), clx = MX(CLX, CLW);
  gfx_rrect(px, WY, PNW, COLH, 24, RGBA(21, 19, 28, 150)); gfx_rrect_v(px, WY, PNW, COLH, 24, WHITE_A(10), WHITE_A(0)); gfx_ring(px, WY, PNW, COLH, 24, 1, WHITE_A(22)); /* glassy: the glows show a little through it */
  float oa = fa * S.loA.v * (1 - S.pkA.v), ox = -56 * S.pkX.v * (rtl ? -1 : 1);
  if (oa > 0.001f) { const Cat *c = &cats[S.shown]; int inZ = S.zone != 0 && S.cat == S.shown;
    /* the subtitles' preview stays in view (its heading too) while the rows under it scroll: what is set is seen as it changes */
    int stick = c->prev && !strcmp(c->prev, "subs"); float top0 = HEADH + 18 + S.prevH;
    gfx_offset_push(ox, fy); gfx_clip_push(clx, CLY, CLW, CLH); gfx_opacity_push(oa);
    float ot = since(S.optsAt), oe = ot < 0.24f ? ease_apply(EASE_SNAP, ot / 0.24f) : 1; /* the settings come in from the side the focus moved to */
    if (stick) { gfx_offset_push(0, CLY); if (oe < 1) { gfx_opacity_push(oe); gfx_offset_push(0, 14 * S.optsDir * (1 - oe)); }
      line(T(c->lb), rtl ? clx + CLW - 24 : clx + 24, 0, HEADH, 38, 700, WHITE_A(255), -0.5f, CLW - 48, al); preview_subs(clx, HEADH + 18, CLW, 210);
      if (oe < 1) { gfx_offset_pop(); gfx_opacity_pop(); } gfx_offset_pop(); gfx_clip_push(clx, CLY + top0 + 4, CLW, CLH - top0 - 4); } /* (the rows: their own window under it) */
    gfx_offset_push(0, CLY + S.optsY.v);
    { float in = S.ho.in.v; hl_draw(&S.ho, app_rtl() ? clx : clx + in, CLW - in, 0); }
    if (oe < 1) { gfx_opacity_push(oe); gfx_offset_push(0, 14 * S.optsDir * (1 - oe)); }
    if (!stick) { line(T(c->lb), rtl ? clx + CLW - 24 : clx + 24, 0, HEADH, 38, 700, WHITE_A(255), -0.5f, CLW - 48, al);
      float y = HEADH + 18;
      if (c->prev) { if (!strcmp(c->prev, "about")) preview_about(clx, y, CLW); else if (!strcmp(c->prev, "acct")) preview_acct(clx, y, CLW); else if (!strcmp(c->prev, "trakt")) preview_trakt(clx, y, CLW); else if (!strcmp(c->prev, "keys")) preview_keys(clx, y, CLW, KEYS_H); else preview_look(clx, y, CLW, LOOK_H); } }
    float vis0 = -S.optsY.v - 120, vis1 = -S.optsY.v + CLH + 20;
    for (int i = 0; i < S.optN; i++) { if (S.optY[i] + S.optH[i] < vis0 || S.optY[i] > vis1) continue; /* only what can be seen */
      float ry = S.optY[i] + S.ra[i].dy.v; int f = inZ && i == S.idx; if (S.zone != 2 && S.cat == S.shown) hit_add(clx, ry, CLW, S.optH[i], opt_focus, NULL, i, 0);
      float nx = f && S.zone == 1 ? nudge_x(&S.nudge) : 0, ny = f && S.zone == 1 ? nudge_y(&S.nudge) : 0;
      draw_row(&c->opts[i], i, clx, ry, S.optH[i], f, nx, ny); }
    if (oe < 1) { gfx_offset_pop(); gfx_opacity_pop(); }
    gfx_offset_pop();
    if (stick) { gfx_clip_pop(); edge_fades(clx, CLY + top0 + 4, CLW, CLH - top0 - 4, S.optsY.v < -0.5f, S.optTot + S.optsY.v > CLH + 2); }
    else edge_fades(clx, CLY, CLW, CLH, S.optsY.v < -0.5f, S.optTot + S.optsY.v > CLH + 2);
    gfx_opacity_pop(); gfx_clip_pop(); gfx_offset_pop(); }
  /* ---- the choices of one setting: drilled into, in the panel's place ---- */
  if (S.pkA.v > 0.001f && S.idx < cats[S.cat].n) { const Setting *o = &cats[S.cat].opts[S.idx]; float a = S.pkA.v, dx = 56 * (1 - S.pkX.v) * (rtl ? -1 : 1);
    if (S.zone == 2) { hit_act(0, 0, SB_W, SB_H, pick_outside, 0, 0); hit_shield(px, WY, PNW, COLH); }
    gfx_opacity_push(a * fa); gfx_offset_push(dx, 0);
    gfx_rrect(px, WY, PNW, COLH, 24, RGBA(21, 19, 28, 196));
    line(o->raw ? o->lb : T(o->lb), rtl ? clx + CLW - 24 : clx + 24, CLY, HEADH, 38, 700, WHITE_A(255), -0.5f, CLW - 48, al);
    gfx_clip_push(clx, PKY, CLW, PKH); gfx_offset_push(0, PKY + S.pickY.v);
    hl_draw(&S.hp, clx, CLW, 0);
    const char *cur = cur_val(o); float pt = since(S.pickAt);
    for (int j = 0; j < o->nopts; j++) { float y = j * 72.0f; if (y + 68 < -S.pickY.v - 10 || y > -S.pickY.v + PKH + 10) continue;
      float d = pt - stag(j + 2), e = d < 0.26f ? ease_apply(EASE_SNAP, d / 0.26f) : 1; if (e <= 0.001f) continue; /* each opening: they come in from the right, one after another */
      int f = j == S.pick, on = !strcmp(o->opts[j].v, cur); float nx = f ? nudge_x(&S.nudge) : 0, ny = f ? nudge_y(&S.nudge) : 0, ex = 18 * (1 - e) * (rtl ? -1 : 1);
      if (S.zone == 2) hit_add(clx, y, CLW, 68, pick_focus, NULL, j, 0);
      gfx_opacity_push(e);
      const char *lb = !strcmp(o->k, "uiLang") ? o->opts[j].lb : T(o->opts[j].lb); char lbb[128]; if (!strcmp(o->k, "subsFontAr")) { snprintf(lbb, sizeof lbb, "%s  \xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7", lb); lb = lbb; } /* (an Arabic font: its sample beside its name) */
      line(lb, (rtl ? clx + CLW - 24 : clx + 24) + nx + ex, y + ny, 68, 28, font_wt(o, o->opts[j].v, on ? 600 : 400), f ? ACC_FG : on ? WHITE_A(255) : WHITE_A(209), 0, CLW - 48 - 40, al);
      if (on) icon_draw("check", (rtl ? clx + 24 + 2 : clx + CLW - 24 - 2 - 28) + nx + ex, y + 20 + ny, 28, f ? ACC_FG : ACC3);
      gfx_opacity_pop(); }
    gfx_offset_pop();
    edge_fades(clx, PKY, CLW, PKH, S.pickY.v < -0.5f, o->nopts * 72.0f - ROWGAP + 28 + S.pickY.v > PKH + 2);
    gfx_clip_pop(); gfx_offset_pop(); gfx_opacity_pop(); }
  /* ---- the key hints: each new set fades in ---- */
  { struct { int k, k2; const char *l; } ks[3]; int n = 0; /* the remote's keys, as everywhere */
    if (S.zone == 0) { ks[n].k = K_OK; ks[n].k2 = 0; ks[n++].l = cats[S.cat].out ? T("Log out") : cats[S.cat].opts == traktS && trakt_st() != TRAKT_LINKED ? T("Connect Trakt") : T("Open"); /* (on Log out, OK signs out; on Trakt not connected, it shows the code) */ ks[n].k = K_BACK; ks[n].k2 = 0; ks[n++].l = T("Leave settings"); }
    else if (S.zone == 1 && best_speed_busy()) { ks[n].k = K_BACK; ks[n].k2 = 0; ks[n++].l = T("Stop the test"); }
    else if (S.zone == 1) { ks[n].k = K_LEFT; ks[n].k2 = K_RIGHT; ks[n++].l = T("Change"); ks[n].k = K_OK; ks[n].k2 = 0; ks[n++].l = T("All choices"); ks[n].k = K_BACK; ks[n].k2 = 0; ks[n++].l = T("Go to the categories"); }
    else { ks[n].k = K_OK; ks[n].k2 = 0; ks[n++].l = T("Confirm"); ks[n].k = K_BACK; ks[n].k2 = 0; ks[n++].l = T("Cancel"); }
    float kt = since(S.keysAt), ka = kt < 0.2f ? ease_apply(EASE_CSS_EASE, kt / 0.2f) : 1;
    gfx_opacity_push(fa * ka); gfx_offset_push(0, fy);
    float x = rtl ? SB_W - WX - 20 : WX + 20, cy = SB_H - 44 - 20;
    for (int i = 0; i < n; i++) { if (ks[i].k2) x += (rtl ? -1 : 1) * (key_hint(x, cy, ks[i].k, NULL, WHITE_A(220), rtl) + 8);
      x += (rtl ? -1 : 1) * (key_hint(x, cy, ks[i].k2 ? ks[i].k2 : ks[i].k, ks[i].l, WHITE_A(220), rtl) + 40); }
    gfx_offset_pop(); gfx_opacity_pop(); }
}
Screen setScreen = { enter, NULL, key, tick, draw, NULL, NULL, NULL };
