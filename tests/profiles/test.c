#include "core/profiles.h"
#include "core/store.h"
#include "core/stremio.h"
#include "core/traktwatch.h"
#include "core/net.h"
#include "core/notify.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>

void plat_thread_background(int priority) { (void)priority; }

static int pushes;
unsigned net_post_json(const char *url, const char *body, NetCb cb, void *ud) {
  (void)body; (void)cb; (void)ud;
  assert(strstr(url, "/datastorePut"));
  return ++pushes;
}
const TwPlay *tw_play_of(const char *id) { (void)id; return NULL; }

int tw_movie(const char *id) { (void)id; return 0; }
int tw_plays(const TwPlay **out) { *out = NULL; return 0; }

static Item movie = { .id = "tt222", .type = "movie", .name = "Test Film" };
static Item episode = { .id = "tt111", .type = "series", .name = "Test Series" };

static void check_progress(float expected) {
  CwInfo info;
  assert(lib_cw_info(&episode, &info));
  assert(!strcmp(info.vid, "tt111:1:1"));
  assert(info.pct == expected);
}

int main(int argc, char **argv) {
  assert(argc == 3);
  store_init(argv[1]);
  if (!strcmp(argv[2], "setup")) {
    store_set("authKey", "account");
    assert(profiles_active() == 0);
    profiles_init();
    profiles_init();
    assert(profiles_count() == 1);
    assert(!profiles_delete(PROFILE_DEFAULT));
    assert(!profiles_add("   "));
    assert(profiles_add("  Alice ") == 2);
    assert(profiles_rename(2, "Alice Renamed"));
    assert(profiles_set_avatar(2, 7));
    assert(!profiles_set_avatar(2, PROFILE_AVATAR_COUNT));
    assert(profiles_add("Bob") == 3);
    assert(profiles_delete(3));
    assert(profiles_select(PROFILE_DEFAULT));
    lib_set_progress(episode.id, &episode, "tt111:1:1", 600000, 2400000, 1);
    assert(pushes == 1);
    store_flush();
  } else if (!strcmp(argv[2], "reload")) {
    profiles_init();
    assert(profiles_count() == 2);
    assert(profiles_id(0) == PROFILE_DEFAULT);
    assert(profiles_id(1) == 2);
    assert(!strcmp(profiles_name(2), "Alice Renamed"));
    assert(profiles_avatar(2) == 7);
    assert(!strcmp(store_get("authKey", ""), "account"));

    assert(!profiles_select(3));
    assert(profiles_add("Bob") == 4);
    assert(profiles_select(2));
    assert(!profiles_select(40));
    assert(!lib_get(episode.id));
    lib_set_progress(episode.id, &episode, "tt111:1:1", 1800000, 2400000, 1);
    check_progress(0.75f);
    assert(!pushes);
    lib_set_movie_watched(lib_new_item(&movie), 1);
    notify_cw_dismiss(&episode);
    store_flush();
  } else if (!strcmp(argv[2], "default")) {
    profiles_init();
    assert(profiles_select(PROFILE_DEFAULT));
    check_progress(0.25f);
    assert(!lib_movie_watched(lib_get(movie.id)));
    ItemList cw;
    assert(lib_cw_items(&cw) == 1);
    free(cw.items);
  } else if (!strcmp(argv[2], "secondary")) {
    profiles_init();
    assert(profiles_select(2));
    check_progress(0.75f);
    assert(lib_movie_watched(lib_get(movie.id)));
    ItemList cw;
    assert(lib_cw_items(&cw) == 0);
    free(cw.items);
    unsigned session = profiles_session();
    assert(profiles_select(PROFILE_DEFAULT));
    check_progress(0.25f);
    assert(profiles_session() != session);
    assert(profiles_select(2));
    check_progress(0.75f);
  } else {
    profiles_init();
    assert(profiles_delete(2));
    assert(!store_node("profile-2-lib"));
  }
}
