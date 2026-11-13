#include "core/bgquality.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
  /* Older 720p compositors must not allocate a 1080p buffer even with ample RAM. */
  assert(!bgquality_choose(4096, 1200, 1280, 720));
  assert(!bgquality_choose(4096, 1200, 1920, 720));
  assert(!bgquality_choose(4096, 1200, 1280, 1080));
  assert(bgquality_choose(4096, 1200, 1920, 1080));
  assert(bgquality_choose(4096, 1200, 3840, 2160));
  assert(bgquality_choose(1982, 384, 1920, 1080));
  assert(bgquality_choose(1792, 384, 1920, 1080));
  assert(!bgquality_choose(1791, 1200, 1920, 1080));
  assert(!bgquality_choose(2048, 383, 1920, 1080));
  assert(!bgquality_choose(1024, 900, 1920, 1080));
  assert(!bgquality_choose(0, 900, 1920, 1080));
  assert(!bgquality_choose(4096, -1, 1920, 1080));
  assert(bgquality_should_downgrade(255, 200, 4096));
  assert(!bgquality_should_downgrade(256, 200, 4096));
  assert(bgquality_should_downgrade(900, 1025, 4096));
  assert(!bgquality_should_downgrade(900, 1024, 4096));
  assert(!bgquality_should_downgrade(-1, -1, 0));
  int strikes = 0;
  for (int i = 0; i < 89; i++) strikes = bgquality_slow_frames(strikes, 0.049f);
  assert(strikes == 89); /* a short burst alone does not trigger the fallback */
  strikes = bgquality_slow_frames(strikes, 0.049f);
  assert(strikes == 90);
  strikes = bgquality_slow_frames(strikes, 0.016f);
  assert(strikes == 88);
  for (int i = 0; i < 90; i++) strikes = bgquality_slow_frames(strikes, 0.016f);
  assert(strikes == 0);
  for (int i = 0; i < 200; i++) strikes = bgquality_slow_frames(strikes, 0.033f);
  assert(strikes == 0); /* 30 FPS is not the emergency threshold */
  puts("PASS: adaptive backdrop memory, surface, and downgrade thresholds");
  return 0;
}
