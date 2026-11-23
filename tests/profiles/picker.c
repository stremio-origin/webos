#include "screens/profiles.h"
#include "core/profiles.h"
#include "core/store.h"
#include "core/keys.h"
#include "core/hit.h"
#include "ui/osk.h"
#include <assert.h>
#include <string.h>

int app_splash_done;
static Scr opened = SCR_PROFILES;
static int quit;
static int reveals;
static double now = 1;
double app_now(void) { return now; }
float app_hold_k(double at) { return now - at >= 0.55 ? 1 : 0; }
void app_go(Scr screen, const void *params, int no_stack) { opened = screen; }
void splash_start(int wait, double since) {}
void app_reveal_start(void) { assert(opened == SCR_HOME); reveals++; }
void app_cancel_profiles(void) { quit = 1; }
int app_pick_profile(int id) { return profiles_select(id); }
int app_rtl(void) { return 0; }
int plat_text_input(void) { return 0; }
void plat_thread_background(int priority) {}
const char *T(const char *str) { return str; }
void osk_open(Osk *k) { k->open = 1; }
void osk_close(Osk *k) { k->open = 0; }
void osk_tick(Osk *k, float dt) {}
void osk_draw(Osk *k, float x, float y, float w) {}
int osk_key(Osk *k, int key, char *buf, size_t cap) { return key == K_OK ? 2 : 1; }
uint32_t ACC_FG;
unsigned app_ambient_glass_tex(void) { return 0; }
void nudge_start(Nudge *n, int k) {}
void nudge_tick(Nudge *n, float dt) {}
float nudge_x(const Nudge *n) { return 0; }
float nudge_y(const Nudge *n) { return 0; }
void gfx_rect(float x, float y, float w, float h, uint32_t c) {}
void gfx_opacity_push(float a) {}
void gfx_opacity_pop(void) {}
void gfx_offset_push(float x, float y) {}
void gfx_offset_pop(void) {}
void lglass_light(float at) {}
void lglass_liquid(unsigned t, float x, float y, float w, float h, float r,
                   float u0, float v0, float u1, float v1, float k, float dark, int seed, int small) {}
const char *plat_asset_dir(void) { return "/test"; }
unsigned img_get(const char *url, int w, int h, float *aspect) { return 1; }
void gfx_image(unsigned tex, float x, float y, float w, float h, float r,
               uint32_t tint, float u0, float v0, float u1, float v1) {}
void gfx_rrect(float x, float y, float w, float h, float r, uint32_t c) {}
void glass_rrect(float x, float y, float w, float h, float r, uint32_t c, float top, float bottom, float edge) {}
float osk_height(void) { return 342; }
void lglass_accent(float x, float y, float w, float h, float r, float a) {}
unsigned long long lift_key(const void *owner, int index) { return index; }
float lift_begin(unsigned long long key, int focused, float hold, float x, float y, float w, float h, float r) { return 0; }
void lift_end(unsigned long long key, float x, float y, float w, float h, float r) {}
void hold_trace_draw(float x, float y, float w, float h, float r, float progress) {}
void icon_draw(const char *name, float x, float y, float size, uint32_t color) {}
int text_line_height(int px) { return px; }
float text_draw(const char *s, float x, float y, int px, int weight, uint32_t c,
                float tracking, float max_width, int align) { return 0; }
void hit_add(float x, float y, float w, float h, HitFn focus, HitFn act, int a, int b) {}

static void tap(void) {
  profileScreen.key(K_OK);
  profile_ok_up();
}

static void hold(void) {
  profileScreen.key(K_OK);
  now += 0.6;
  profileScreen.tick(0.016f);
  profile_ok_up();
  assert(opened == SCR_PROFILES);
  profileScreen.draw();
}

int main(int argc, char **argv) {
  assert(argc == 2);
  store_init(argv[1]);
  profiles_init();
  profileScreen.enter(NULL);
  profileScreen.draw();
  profileScreen.key(K_RIGHT);
  tap();
  profileScreen.text("Alice");
  profileScreen.key(K_OK);
  assert(profiles_count() == 2 && !strcmp(profiles_name(2), "Alice"));

  hold();
  tap();
  profileScreen.text(" TV");
  profileScreen.key(K_OK);
  assert(!strcmp(profiles_name(2), "Alice TV"));

  hold();
  profileScreen.key(K_DOWN);
  tap();
  profileScreen.draw();
  profileScreen.key(K_RIGHT);
  tap();
  assert(profiles_avatar(2) == 2);
  profileScreen.key(K_DOWN);
  tap();
  profileScreen.key(K_BACK);
  assert(profiles_count() == 2 && !quit);
  tap();
  tap();
  assert(profiles_count() == 1);

  profileScreen.key(K_LEFT);
  tap();
  assert(opened == SCR_HOME && profiles_active() == PROFILE_DEFAULT && reveals == 1);
  profileScreen.leave();
  int second = profiles_add("Second");
  profileScreen.enter(NULL);
  profileScreen.draw();
  profileScreen.key(K_RIGHT);
  tap();
  assert(opened == SCR_HOME && profiles_active() == second && reveals == 2);
}
