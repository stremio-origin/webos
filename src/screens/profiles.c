#include "screens/profiles.h"

#include "core/hit.h"
#include "core/i18n.h"
#include "core/keys.h"
#include "core/profiles.h"
#include "platform/plat.h"
#include "screens/splash.h"
#include "ui/gfx.h"
#include "ui/icon.h"
#include "ui/img.h"
#include "ui/glass.h"
#include "ui/widgets.h"
#include "ui/osk.h"
#include "ui/text.h"
#include "screens/board.h"
#include "screens/pinpad.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

enum { PICK, MANAGE, ADD, RENAME, CONFIRM_DELETE, AVATARS };
enum { COLUMNS = 5, AVATAR_COLUMNS = 6 };
enum { MENU_RENAME, MENU_AVATAR, MENU_KIDS, MENU_LIMIT, MENU_BED, MENU_LOCK, MENU_DELETE };
static struct {
  int tile, option, avatar_choice, mode, id, edit_error, after_login;
  int holding, held_id, fresh, unlocked; /* unlocked: the parent PIN given for this menu */ /* fresh: the avatar choice of a profile just added (Back or OK: the picker) */
  double hold_at;
  char name[PROFILE_NAME_CAP];
  Osk keyboard;
  Timer1 entrance;
  Tween shade, menu_focus;
  Nudge edge;
} P;

static const char *label(int id) {
  const char *name = profiles_name(id);
  return name && *name ? name : T("Default");
}

static void set_mode(int mode) {
  P.mode = mode;
  if (mode != PICK) timer_start(&P.entrance, 0.26f);
}

static void edit_close(void) {
  SDL_StopTextInput();
  osk_close(&P.keyboard);
  P.mode = PICK;
  P.holding = 0;
  P.edit_error = 0;
}

static void edit_open(int mode, int id) {
  set_mode(mode);
  P.id = id;
  if (mode == CONFIRM_DELETE) return;
  snprintf(P.name, sizeof P.name, "%s", mode == RENAME ? label(id) : "");
  if (plat_text_input()) SDL_StartTextInput();
  else { osk_open(&P.keyboard); P.keyboard.shift = !*P.name; }
}

static void save_name(void) {
  int adding = P.mode == ADD;
  int saved = adding ? profiles_add(P.name) : profiles_rename(P.id, P.name);
  if (!saved) { P.edit_error = 1; return; }
  edit_close();
  if (saved > 1) P.tile = profiles_count() - 1;
  if (adding && saved > 1) { P.id = saved; P.fresh = 1; P.avatar_choice = profiles_avatar(saved); set_mode(AVATARS); } /* then its picture */
}

static int can_delete(void) {
  return profiles_count() > 1 && P.id != profiles_active();
}

static void pick_id(int id);
static void on_pick_pin(int ok, void *ud) { if (ok) pick_id((int)(intptr_t)ud); }
static void select_tile(void) {
  int id = profiles_id(P.tile);
  if (!id) { edit_open(ADD, 0); return; }
  if (profiles_int(id, "lock", 0) && pin_is_set() && id != profiles_active()) { pin_ask(PIN_CHECK, label(id), on_pick_pin, (void *)(intptr_t)id); return; } /* (a locked profile) */
  pick_id(id);
}
static void pick_id(int id) {
  int launching = !profiles_active();
  if (!app_pick_profile(id)) return;
  if (launching && P.after_login) {
    app_splash_done = 0;
    splash_start(1, app_now());
  }
  app_go(SCR_HOME, NULL, 1);
  if (!P.after_login) app_reveal_start();
}

static void enter(const void *params) {
  memset(&P, 0, sizeof P);
  P.after_login = params != NULL;
  for (int i = 0; i < profiles_count(); i++)
    if (profiles_id(i) == profiles_active()) P.tile = i;
}

static void open_menu(void) {
  P.id = P.held_id;
  P.option = 0;
  set_mode(MANAGE);
  tween_set(&P.menu_focus, 0);
  P.holding = 0;
}

void profile_ok_up(void) {
  if (!P.holding) return;
  P.holding = 0;
  if (P.held_id && app_hold_k(P.hold_at) >= 1) open_menu();
  else select_tile();
}

static int tile_count(void) {
  int count = profiles_count();
  return count + (count < PROFILE_MAX);
}

/* the menu's options for this profile: a child's has its limit and bedtime, the others a lock */
static int menu_items(int *ids) { int n = 0; ids[n++] = MENU_RENAME; ids[n++] = MENU_AVATAR; ids[n++] = MENU_KIDS;
  if (profiles_kids(P.id)) { ids[n++] = MENU_LIMIT; ids[n++] = MENU_BED; } else ids[n++] = MENU_LOCK;
  if (can_delete()) ids[n++] = MENU_DELETE; return n; }
static const int LIMITS[] = { 0, 30, 60, 90, 120, 180 }, BEDS[] = { 0, 1140, 1170, 1200, 1230, 1260, 1290, 1320 };
static void next_of(const char *key, const int *v, int n) { int cur = profiles_int(P.id, key, 0), i = 0; while (i < n && v[i] != cur) i++; profiles_set_int(P.id, key, v[(i + 1) % n]); }
static void do_option(int o) {
  if (o == MENU_KIDS) { if (profiles_set_kids(P.id, !profiles_kids(P.id)) && P.id == profiles_active()) board_profile_changed(); } /* only children's titles: the boards made again */
  else if (o == MENU_LIMIT) next_of("limit", LIMITS, 6);
  else if (o == MENU_BED) next_of("bed", BEDS, 8);
  else if (o == MENU_LOCK) profiles_set_int(P.id, "lock", !profiles_int(P.id, "lock", 0));
  else if (o == MENU_DELETE) edit_open(CONFIRM_DELETE, P.id);
  int ids[8], n = menu_items(ids); if (P.option >= n) P.option = n - 1;
}
static void on_option_pin(int ok, void *ud) { if (!ok) return; P.unlocked = 1; do_option((int)(intptr_t)ud); }
static int key(int k) {
  if (P.mode == MANAGE) {
    int ids[8], n = menu_items(ids);
    if (k == K_BACK) { P.unlocked = 0; edit_close(); }
    else if (k == K_UP && P.option > 0) P.option--;
    else if (k == K_DOWN && P.option < n - 1) P.option++;
    else if (k == K_OK) { int o = ids[P.option < n ? P.option : 0];
      if (o == MENU_AVATAR) { P.avatar_choice = profiles_avatar(P.id); set_mode(AVATARS); }
      else if (o == MENU_RENAME) edit_open(RENAME, P.id);
      else if (P.unlocked) do_option(o);
      else if (!pin_is_set() && o != MENU_DELETE) pin_ask(PIN_NEW, T("Parent PIN"), on_option_pin, (void *)(intptr_t)o); /* (the first guarded change makes the PIN: a child cannot undo it) */
      else pin_ask(PIN_CHECK, T("Parent PIN"), on_option_pin, (void *)(intptr_t)o);
    }
    return 1;
  }
  if (P.mode == AVATARS) {
    int i = P.avatar_choice;
    if (k == K_BACK) { if (P.fresh) { P.fresh = 0; P.mode = PICK; } else set_mode(MANAGE); }
    else if (k == K_OK && profiles_set_avatar(P.id, i)) { if (P.fresh) { P.fresh = 0; P.mode = PICK; } else set_mode(MANAGE); }
    else if (k == K_LEFT && i % AVATAR_COLUMNS) P.avatar_choice--;
    else if (k == K_RIGHT && i % AVATAR_COLUMNS < AVATAR_COLUMNS - 1 && i + 1 < PROFILE_AVATAR_COUNT) P.avatar_choice++;
    else if (k == K_UP && i >= AVATAR_COLUMNS) P.avatar_choice -= AVATAR_COLUMNS;
    else if (k == K_DOWN && i + AVATAR_COLUMNS < PROFILE_AVATAR_COUNT) P.avatar_choice += AVATAR_COLUMNS;
    return 1;
  }
  if (P.mode == CONFIRM_DELETE) {
    if (k == K_BACK) set_mode(MANAGE);
    else if (k == K_OK && profiles_delete(P.id)) {
      edit_close();
      if (P.tile >= tile_count()) P.tile = tile_count() - 1;
    }
    return 1;
  }
  if (P.mode != PICK) {
    if (k == K_BACK) {
      int renaming = P.mode == RENAME;
      edit_close();
      if (renaming) set_mode(MANAGE);
    }
    else if (P.keyboard.open) {
      size_t was = strlen(P.name);
      if (osk_key(&P.keyboard, k, P.name, sizeof P.name) == 2) save_name();
      else { size_t n = strlen(P.name); if (n != was && (!n || P.name[n - 1] == ' ')) P.keyboard.shift = 1; } /* a capital to start each word */
    } else if (k == K_OK || k == K_DOWN) save_name();
    return 1;
  }
  int total = tile_count();
  if (k == K_OK) {
    P.holding = 1;
    P.hold_at = app_now();
    P.held_id = profiles_id(P.tile);
    return 1;
  }
  P.holding = 0;
  int previous = P.tile;
  if (k == K_BACK) app_cancel_profiles();
  else if (k == K_LEFT && P.tile % COLUMNS) P.tile--;
  else if (k == K_RIGHT && P.tile % COLUMNS < COLUMNS - 1 && P.tile + 1 < total) P.tile++;
  else if (k == K_UP && P.tile >= COLUMNS) P.tile -= COLUMNS;
  else if (k == K_DOWN && P.tile / COLUMNS < (total - 1) / COLUMNS)
    P.tile = P.tile + COLUMNS < total ? P.tile + COLUMNS : total - 1;
  if (previous == P.tile && k >= K_LEFT && k <= K_DOWN) nudge_start(&P.edge, k);
  return 1;
}

static void text(const char *utf8) {
  if (P.mode != ADD && P.mode != RENAME) return;
  size_t len = strlen(P.name), added = strlen(utf8);
  if (len + added < sizeof P.name) memcpy(P.name + len, utf8, added + 1);
}

static void focus(int tile, int unused) {
  (void)unused;
  if (P.tile != tile) P.holding = 0;
  P.tile = tile;
}
static void tick(float dt) {
  if (!profiles_active() && profiles_count() == 1 && P.mode == PICK) { P.tile = 0; select_tile(); return; } /* (one viewer, nobody to ask: straight to Home, as before profiles) */
  osk_tick(&P.keyboard, dt);
  timer_tick(&P.entrance, dt);
  nudge_tick(&P.edge, dt);
  tween_to(&P.shade, P.mode != PICK, 0.2f, EASE_CSS_EASE);
  tween_tick(&P.shade, dt);
  tween_to(&P.menu_focus, P.option, 0.23f, EASE_CSS_EASEOUT);
  tween_tick(&P.menu_focus, dt);
  if (P.holding && P.held_id && app_hold_k(P.hold_at) >= 1) open_menu();
}

static void surface(float x, float y, float w, float h, float r, float dark, int seed) {
  lglass_liquid(app_ambient_glass_tex(), x, y, w, h, r,
                x / SB_W, 1 - y / SB_H, (x + w) / SB_W, 1 - (y + h) / SB_H,
                0.9f, dark, seed, 1);
  lglass_light(0);
}

static void menu_focus(int option, int unused) { (void)unused; P.option = option; }
static void menu_button(const char *title, float y, int option) {
  float x = SB_W / 2 - 230;
  text_draw(title, SB_W / 2, y + (62 - text_line_height(27)) / 2, 27, 600,
            P.option == option ? ACC_FG : WHITE_A(217), 0, 420, ALIGN_CENTER);
  hit_add(x, y, 460, 62, menu_focus, NULL, option, 0);
}

void profile_draw_avatar(int choice, float x, float y, float size) {
  float radius = size / 2;
  char url[600];
  snprintf(url, sizeof url, "file://%s/avatars/%d.webp", plat_asset_dir(), choice);
  GLuint image = img_get(url, 288, 288, NULL);
  if (image) gfx_image(image, x, y, size, size, radius, WHITE_A(255), 0, 0, 1, 1);
  else {
    gfx_rrect(x, y, size, size, radius, PAGE);
    icon_draw("nav_profiles", x + size * .25f, y + size * .25f, size * .5f, WHITE_A(180));
  }
}

static void avatar_focus(int choice, int unused) { (void)unused; P.avatar_choice = choice; }

static void heading(const char *title, float y) {
  text_draw(title, SB_W / 2, y, 48, 700, WHITE_A(255), 0, 1000, ALIGN_CENTER);
}

static void draw_panel(void) {
  if (P.mode == AVATARS) {
    const float size = 150, step = 172;
    glass_rrect(SB_W / 2 - 560, 140, 1120, 740, 28, RGB(21,18,31), .14f, .02f, .19f);
    heading(T("Choose an avatar"), 168);
    for (int i = 0; i < PROFILE_AVATAR_COUNT; i++) {
      int row = i / AVATAR_COLUMNS, left = PROFILE_AVATAR_COUNT - row * AVATAR_COLUMNS;
      int cols = left < AVATAR_COLUMNS ? left : AVATAR_COLUMNS;
      int col = app_rtl() ? cols - 1 - i % AVATAR_COLUMNS : i % AVATAR_COLUMNS;
      float x = (SB_W - (cols - 1) * step - size) / 2 + col * step, y = 262 + 200 * row;
      unsigned long long focus_key = lift_key(&P.avatar_choice, i);
      lift_begin(focus_key, P.avatar_choice == i, 0, x, y, size, size, size / 2);
      profile_draw_avatar(i, x, y, size);
      hit_add(x, y, size, size, avatar_focus, NULL, i, 0);
      lift_end(focus_key, x, y, size, size, size / 2);
    }
  } else if (P.mode == MANAGE) {
    float x = SB_W / 2 - 270;
    int ids[8], n = menu_items(ids);
    glass_rrect(x, 228, 540, 238 + 70 * n, 28, RGB(21,18,31), .14f, .02f, .19f);
    profile_draw_avatar(profiles_avatar(P.id), SB_W / 2 - 52, 254, 104);
    text_draw(label(P.id), SB_W / 2, 373, 38, 700, WHITE_A(255), 0, 460, ALIGN_CENTER);
    lglass_accent(SB_W / 2 - 230, 442 + 70 * P.menu_focus.v, 460, 62, 16, 1);
    for (int i = 0; i < n; i++) { char t[96], v[24]; int o = ids[i];
      if (o == MENU_RENAME) snprintf(t, sizeof t, "%s", T("Rename profile"));
      else if (o == MENU_AVATAR) snprintf(t, sizeof t, "%s", T("Change avatar"));
      else if (o == MENU_KIDS) snprintf(t, sizeof t, "%s", T(profiles_kids(P.id) ? "Kids profile: on" : "Kids profile: off"));
      else if (o == MENU_LOCK) snprintf(t, sizeof t, "%s", T(profiles_int(P.id, "lock", 0) ? "Locked with PIN: on" : "Locked with PIN: off"));
      else if (o == MENU_LIMIT) { int m = profiles_int(P.id, "limit", 0); if (!m) snprintf(t, sizeof t, "%s", T("Daily limit: off")); else { if (m % 60) snprintf(v, sizeof v, m >= 60 ? "%d h %d min" : "%d min", m >= 60 ? m / 60 : m, m % 60); else snprintf(v, sizeof v, "%d h", m / 60); snprintf(t, sizeof t, "%s", T1("Daily limit: {t}", "t", v)); } }
      else if (o == MENU_BED) { int b = profiles_int(P.id, "bed", 0); if (!b) snprintf(t, sizeof t, "%s", T("Bedtime: off")); else { snprintf(v, sizeof v, "%02d:%02d", b / 60, b % 60); snprintf(t, sizeof t, "%s", T1("Bedtime: {t}", "t", v)); } }
      else snprintf(t, sizeof t, "%s", T("Delete profile"));
      menu_button(t, 442 + 70 * i, i); }
  } else if (P.mode == CONFIRM_DELETE) {
    surface(SB_W / 2 - 360, 279, 720, 330, 28, 0.42f, 31);
    heading(T("Delete profile?"), 326);
    text_draw(label(P.id), SB_W / 2, 423, 32, 600, WHITE_A(240), 0, 630, ALIGN_CENTER);
    text_draw(T("OK to delete · Back to cancel"), SB_W / 2, 520, 25, 500, WHITE_A(198), 0, 630, ALIGN_CENTER);
  } else {
    glass_rrect(SB_W / 2 - 449, 193, 898, P.keyboard.open ? 313 + osk_height() : 259,
                28, RGB(21,18,31), .14f, .02f, .19f);
    heading(T(P.mode == ADD ? "Add profile" : "Rename profile"), 226);
    lglass_accent(SB_W / 2 - 400, 322, 800, 68, 16, 1);
    text_draw(*P.name ? P.name : T("Profile name"), SB_W / 2, 337, 32, 600,
              *P.name ? ACC_FG : WHITE_A(150), 0, 760, ALIGN_CENTER);
    if (P.edit_error) text_draw(T("Enter a profile name"), SB_W / 2, 408, 23, 500, WHITE_A(220), 0, 760, ALIGN_CENTER);
    if (P.keyboard.a.v > 0.001f) osk_draw(&P.keyboard, SB_W / 2 - 416, 476, 832);
  }
}

static void draw_grid(int interactive) {
  int total = tile_count();
  int rows = (total + COLUMNS - 1) / COLUMNS;
  float size = rows == 1 ? 220 : rows == 2 ? 187 : 145;
  float step = size + 64, row_step = size + 118;
  float y0 = rows == 1 ? 360 : rows == 2 ? 263 : 214;
  heading(T("Who's watching?"), y0 - 119);
  for (int i = 0; i < total; i++) {
    int row = i / COLUMNS, col = i % COLUMNS;
    int remaining = total - row * COLUMNS;
    int cols = remaining < COLUMNS ? remaining : COLUMNS;
    if (app_rtl()) col = cols - 1 - col;
    float x = (SB_W - (cols - 1) * step - size) / 2 + col * step;
    float y = y0 + row * row_step;
    int id = profiles_id(i), focused = interactive && P.tile == i;
    float hold = focused && P.holding && id ? app_hold_k(P.hold_at) : 0;
    unsigned long long key = lift_key(&P, id);
    if (focused) gfx_offset_push(nudge_x(&P.edge), nudge_y(&P.edge));
    if (interactive) lift_begin(key, focused, hold, x, y, size, size, size / 2);
    if (id) profile_draw_avatar(profiles_avatar(id), x, y, size);
    else {
      if (interactive) surface(x, y, size, size, size / 2, 0.07f, 1);
      else gfx_rrect(x, y, size, size, size / 2, PAGE);
      icon_draw("plus", x + size * .31f, y + size * .31f, size * .38f, WHITE_A(216));
    }
    if (hold > 0) hold_trace_draw(x, y, size, size, size / 2, hold);
    if (interactive) hit_add(x, y, size, size + 48, focus, NULL, i, 0);
    if (interactive) lift_end(key, x, y, size, size, size / 2);
    if (focused) gfx_offset_pop();
    text_draw(id ? label(id) : T("Add profile"), x + size / 2, y + size + 12, 28,
              focused ? 700 : 500, WHITE_A(focused ? 255 : 186), 0, size + 8, ALIGN_CENTER);
    if (id && profiles_kids(id)) text_draw(T("Kids"), x + size / 2, y + size + 48, 22, 600, WHITE_A(150), 0, size + 8, ALIGN_CENTER);
  }
  if (interactive && profiles_id(P.tile))
    text_draw(T("Hold OK to manage profile"), SB_W / 2, rows == 3 ? 930 : 795,
              23, 500, WHITE_A(155), 0, 760, ALIGN_CENTER);

}

static void draw(void) {
  int picking = P.mode == PICK;
  draw_grid(picking);
  if (P.shade.v > 0.001f)
    gfx_rect(0, 0, SB_W, SB_H, BLACK_A((int)(180 * P.shade.v)));
  if (picking) return;
  float k = ease_apply(EASE_CSS_EASEOUT, timer_x(&P.entrance));
  gfx_opacity_push(k);
  gfx_offset_push(0, 22 * (1 - k));
  draw_panel();
  gfx_offset_pop();
  gfx_opacity_pop();
}

Screen profileScreen = { .enter = enter, .leave = edit_close, .key = key,
                         .tick = tick, .draw = draw, .text = text };
