#ifndef SB_BGQUALITY_H
#define SB_BGQUALITY_H

/* Backdrop source image and GPU bake dimensions (the rest of the UI is unchanged). */
#define SB_BG_LOW_W 1280
#define SB_BG_LOW_H 720
#define SB_BG_HIGH_W 1920
#define SB_BG_HIGH_H 1080

/* Conservative, hardware-independent policy. A 1080p rendering surface alone
   does not imply that the TV has the free memory for two full-screen fades. */
int bgquality_choose(int total_mb, int available_mb, int drawable_w, int drawable_h);
/* One-way downgrade: never re-upgrade during the same session, preventing
   quality/memory oscillation while the launcher and video decoder allocate RAM. */
int bgquality_should_downgrade(int available_mb, int app_rss_mb, int total_mb);
/* Counts sustained very slow frames (>40 ms); the caller checks >=90 and
   samples only on an already-visible static backdrop without video playing. */
int bgquality_slow_frames(int strikes, float dt);

#endif
