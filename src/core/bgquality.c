#include "core/bgquality.h"

int bgquality_choose(int total_mb, int available_mb, int drawable_w, int drawable_h) {
  /* MemTotal can exclude the hardware's reserved RAM. at least 1.75 GiB of actually
     reported RAM plus >= 384 MiB available is our conservative cutoff. */
  if (drawable_w < SB_BG_HIGH_W || drawable_h < SB_BG_HIGH_H) return 0;
  if (total_mb < 1792 || available_mb < 384) return 0;
  return 1;
}

int bgquality_should_downgrade(int available_mb, int app_rss_mb, int total_mb) {
  /* Leave enough headroom for a stream's decoder and for webOS itself.
     Unknown metrics do not initiate downgrades; they already block upgrades. */
  if (available_mb >= 0 && available_mb < 256) return 1;
  if (total_mb > 0 && app_rss_mb >= 0 && (long)app_rss_mb * 4 > total_mb) return 1;
  return 0;
}

int bgquality_slow_frames(int strikes, float dt) {
  /* 40 ms means fewer than 25 FPS. Recover quickly if frame times improve.
     A transient network callback or a single slow bake must not downgrade. */
  if (dt > 0.040f) { if (strikes < 90) strikes++; }
  else if (dt < 0.030f) { strikes -= 2; if (strikes < 0) strikes = 0; }
  return strikes;
}
