/* what Stremio Origin 1.1.1 added to the platform, on Android TV. The pointer is the system's, live
   broadcasts reload as any stream, and the picture is on a SurfaceView the app never reads */
#include "platform/plat.h"
#include "core/video.h"
#include <stdint.h>

void plat_pointer_visible(int on) { (void)on; }
unsigned long plat_luna_subscribe_app(const char *uri, const char *payload, PlatLunaCb cb, void *ud) { (void)uri; (void)payload; (void)cb; (void)ud; return 0; }
int plat_luna_send_app(const char *uri, const char *payload) { (void)uri; (void)payload; return 0; }
void video_set_live(int on) { (void)on; }
void video_live_rejoin(void) { video_play(); }
int video_edge_colors(uint32_t *out4) { (void)out4; return 0; }
VidPlaneCaps video_plane_caps(void) { VidPlaneCaps c = { 0, 0, 0, 1 }; return c; } /* (the SurfaceView moves through the Java side: one move behind a dip to black) */
int video_mac_real(void) { return 1; } /* (livepeek.c asks it off webOS: Android plays real video) */
