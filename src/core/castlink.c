/* The cast receiver (core/cast.c) joined to this app: a phone's play goes through the player (its subtitles, the
   moment to start at), its keys through the app's own keys; the pairing code shows on a small glass pill at the top,
   and a phone connecting or going is said once. */
#include "core/castlink.h"
#include "core/cast.h"
#include "core/app.h"
#include "core/keys.h"
#include "core/store.h"
#include "core/i18n.h"
#include "core/update.h"
#include "screens/player.h"
#include "core/live.h"
#include "screens/settings.h"
#include "screens/board.h"
#include "screens/btrailer.h"
#include "cJSON.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include "ui/glass.h"
#include "platform/plat.h"
#include <stdio.h>
#include <string.h>

static char codeNow[8], castTitle[256]; static Tween codeA;

static const char *tv_name(void) { return plat_device_name(); }
/* a channel a phone started that will not play: the phone told why (its remote leaves "loading"), in this TV's words */
static void on_live_fail(const char *id, const char *code, int back) { if (!cast_phones()) return; fprintf(stderr, "[cast] channel %s did not play (%s)%s: phones told\n", id, code, back ? ", the one before plays on" : "");
  cast_error(T("This channel isn't broadcasting right now"), code, id, back); }
static void on_play(const CastPlay *p) {
  app_activity();
  snprintf(castTitle, sizeof castTitle, "%s", p->title);
  if (live_type(p->type) && *p->id) { /* a channel from a phone: started as one picked here (its own stream found, the channel player, Up / Down along its line-up) */
    Item it; memset(&it, 0, sizeof it); snprintf(it.type, sizeof it.type, "%s", p->type); snprintf(it.id, sizeof it.id, "%s", p->id); snprintf(it.name, sizeof it.name, "%s", p->title); snprintf(it.poster, sizeof it.poster, "%s", p->poster); snprintf(it.logo, sizeof it.logo, "%s", p->logo);
    char ty[16], pid[96], pv[96]; player_cast_ident(ty, sizeof ty, pid, sizeof pid, pv, sizeof pv); live_start(&it, NULL, 0, player_open() && live_type(ty)); return; } /* (a channel already on: switched in its player, as Up / Down) */
  Stream s; memset(&s, 0, sizeof s); snprintf(s.url, sizeof s.url, "%s", p->url); snprintf(s.name, sizeof s.name, "%s", p->title); snprintf(s.title, sizeof s.title, "%s", p->title);
  const char *type = *p->type ? p->type : "movie", *id = *p->id ? p->id : "cast"; char vid[SB_VID];
  if (!strcmp(type, "series") && p->season > 0) snprintf(vid, sizeof vid, "%s:%d:%d", id, p->season, p->episode); else snprintf(vid, sizeof vid, "%s", id);
  const char *urls[16], *langs[16]; for (int i = 0; i < p->nsubs && i < 16; i++) { urls[i] = p->subs[i].url; langs[i] = p->subs[i].lang; }
  player_next_subs(urls, langs, p->nsubs); player_next_art(p->title, p->poster, p->background, p->logo);
  player_play_at(&s, type, id, vid, p->pos); /* pos 0: where it was left, as a play on the TV */
}
/* the phone names a title and this TV finds its stream (its own best-stream choice, as with best stream on here): the
   title's pictures at once, "choosing" told to the phone while it searches, then it plays from pos (0: from the start) */
static void on_playbest(const CastPlay *p) {
  app_activity();
  snprintf(castTitle, sizeof castTitle, "%s", p->title);
  const char *type = *p->type ? p->type : "movie", *id = p->id; char vid[SB_VID];
  if (*p->vid) snprintf(vid, sizeof vid, "%s", p->vid); else if (!strcmp(type, "series") && p->season > 0) snprintf(vid, sizeof vid, "%s:%d:%d", id, p->season, p->episode); else snprintf(vid, sizeof vid, "%s", id);
  player_next_subs(NULL, NULL, 0); player_next_art(p->title, p->poster, p->background, p->logo);
  player_cast_best(type, id, vid, p->pos);
}
static void on_cmd(int cmd, double pos) { static const char *N[] = { "", "pause", "resume", "seek", "stop", "playnow" }; if (cmd > 0 && cmd < 6) fprintf(stderr, "[cast] %s from the phone\n", N[cmd]); app_activity(); player_cast(cmd, pos); } /* (a phone's pause is activity: the screen saver never comes at once over it) */
static void on_track(const char *k, const char *id, int ms) { app_activity(); player_cast_track(k, id, ms); }
static int on_state(double *pos, double *dur, char *title, int cap) { int s = player_cast_state(pos, dur, title, cap); if (s && !title[0]) snprintf(title, (size_t)cap, "%s", castTitle); return s; } /* (no title known here: the phone's own) */
static void on_key(int k) {
  if (k == CAST_K_HOME) { if (player_open()) player_cast(4, 0); else app_go(SCR_HOME, NULL, 1); return; }
  static const int M[] = { 0, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_OK, K_BACK, 0, K_PLAY, K_PAUSE };
  int ak = k > 0 && k < 10 ? M[k] : 0; if (!ak) return; app_key(ak, 1, 0); app_key(ak, 0, 0);
}
/* the phone's account: this TV follows it (signed out, or another user: signed in as the phone is, its addons and
   library brought down and Home made again, as a sign-in here); the same user, or a phone signed out: left as it is */
static void on_account(const char *ak, const char *userJson) {
  if (!ak || !*ak) return; cJSON *u = cJSON_Parse(userJson ? userJson : ""); const cJSON *id = cJSON_GetObjectItemCaseSensitive(u, "_id");
  int same = sb_signed_in() && cJSON_IsString(id) && sb_user_id() && !strcmp(sb_user_id(), id->valuestring); cJSON_Delete(u);
  if (same || (sb_signed_in() && !strcmp(sb_auth_key(), ak))) return;
  fprintf(stderr, "[cast] account from the phone: %s\n", sb_signed_in() ? "another user" : "signing in");
  if (player_open()) player_cast(4, 0);
  if (sb_signed_in()) { sb_forget_local(); board_invalidate(); }
  toast(T("Signing in with your phone\xe2\x80\x99s account"), 2500, NULL);
  extern void login_finish(const char *); if (SCR != SCR_LOGIN) app_go(SCR_LOGIN, NULL, 1); login_finish(ak); /* the sign-in's own loading, then Home */
}
static void on_pref(const char *k, const char *v) { if (settings_apply_pref(k, v) && !player_open()) toast(T("Settings updated from your phone"), 2000, NULL); } /* (one short notice, no key cap before it; none while a title plays: the change shows itself, the subtitles moving or changing) */
static void on_pairing(const char *code) { snprintf(codeNow, sizeof codeNow, "%s", code); }
static void on_connected(int n) { static int was; if (n > was && !player_open()) btr_cast_hold(20); /* a phone just connected: its play comes next, no trailer flashes before it */ if (n > was) toast(T("Phone connected"), 2500, NULL); else if (n < was && !n) toast(T("Phone disconnected"), 2500, NULL); was = n; }

void castlink_start(void) {
  static CastHooks h; const char *dn = plat_device_name();
  h.plat = strstr(dn, "webOS") ? "webos" : strstr(dn, "Android") ? "androidtv" : strstr(dn, "Tizen") ? "tizen" : "mac"; h.ver = update_current(); h.name = tv_name;
  h.get = store_get; h.set = store_set; h.play = on_play; h.playbest = on_playbest; h.cmd = on_cmd; h.key = on_key; h.state = on_state; h.ident = player_cast_ident; h.info = player_cast_info; h.chips = player_cast_chips; h.p2p = player_p2p_line; h.sync = player_cast_sync; h.pairing = on_pairing; h.connected = on_connected; h.account = on_account; h.pref = on_pref; h.tracks = player_cast_tracks; h.track = on_track; h.previews = player_cast_previews;
  live_fail_hook = on_live_fail;
  cast_start(&h);
}
void castlink_tick(float dt) { cast_tick(); tween_to(&codeA, codeNow[0] ? 1 : 0, 0.3f, EASE_CSS_EASE); tween_tick(&codeA, dt); }
/* the code to type on the phone: a glass pill at the top middle, over everything, while it is asked for */
void castlink_draw(void) {
  float a = codeA.v; if (a <= 0.004f) return; static char shown[8]; if (codeNow[0]) snprintf(shown, sizeof shown, "%s", codeNow);
  const char *msg = T("A phone wants to connect"), *sub = T("Only phones that enter this code can control the TV, on your home network"); /* (plain WebSocket on the LAN: the code is what protects it) */
  float tw = text_width(msg, 26, 600, 0), cw = text_width(shown, 40, 800, 8), w = 44 + tw + 30 + cw + 44, h = 84, x = SB_W / 2 - w / 2, y = 120 - 16 * (1 - a);
  gfx_opacity_push(a); lglass_light(0); lglass_shell(x, y, w, h, h / 2, RGBA(14, 12, 22, 200), 1);
  text_draw(msg, x + 44, y + h / 2 - text_line_height(26) / 2.0f, 26, 600, WHITE_A(220), 0, 0, ALIGN_LEFT);
  text_draw(shown, x + 44 + tw + 30, y + h / 2 - text_line_height(40) / 2.0f, 40, 800, ACC2 | 0xff000000u, 8, 0, ALIGN_LEFT);
  text_draw(sub, SB_W / 2, y + h + 14, 21, 500, WHITE_A(190), 0, 0, ALIGN_CENTER);
  gfx_opacity_pop();
}
