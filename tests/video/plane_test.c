#include <assert.h>
#include <stdlib.h>
#include "platform/webos/video_webos.c"

static const char *device;
const char *plat_device_name(void) { return device; }

int main(int argc, char **argv) {
  assert(argc == 4);
  device = argv[1];
  int width = atoi(argv[2]), height = atoi(argv[3]);
  SDL_Rect full = to_plane(0, 0, 1920, 1080);
  assert(full.x == 0 && full.y == 0 && full.w == width && full.h == height);
  SDL_Rect cinema = to_plane(0, 140, 1920, 800);
  assert(cinema.x == 0 && cinema.y == 140 * height / 1080);
  assert(cinema.w == width && cinema.h == 800 * height / 1080);
  SDL_Rect inset = to_plane(480, 270, 960, 540);
  assert(inset.x == width / 4 && inset.y == height / 4);
  assert(inset.w == width / 2 && inset.h == height / 2);
  puts("video plane geometry OK");
  return 0;
}
