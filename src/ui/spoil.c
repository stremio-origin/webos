#include "ui/spoil.h"
#include "ui/img.h"
#include "ui/gfx.h"
#include "ui/widgets.h"
#include "ui/icon.h"
#include "ui/text.h"
#include "core/app.h"
#include "core/i18n.h"
#include <math.h>
#include "core/store.h"
#include "core/stremio.h"
#include "core/json.h"
#include <string.h>
#include <stdio.h>

int spoil_mode(void) { const char *m = store_get("spoilers", "off"); return !strcmp(m, "hide") ? 2 : !strcmp(m, "blur") ? 1 : 0; }

/* the episodes shown clear on request (OK on one), each with when: its blur fades off and its eye goes */
static struct { char vid[SB_VID]; double at; } shown[32]; static int nshown;
void spoil_reveal(const char *vid) { if (!vid || !*vid) return; for (int i = 0; i < nshown; i++) if (!strcmp(shown[i].vid, vid)) return; int k = nshown < 32 ? nshown++ : 31; snprintf(shown[k].vid, sizeof shown[k].vid, "%s", vid); shown[k].at = app_now(); }
void spoil_forget(void) { nshown = 0; }
void spoil_reveal_wait(const char *vid) { if (!vid) return; for (int i = 0; i < nshown; i++) if (!strcmp(shown[i].vid, vid) && app_now() - shown[i].at < 0.55) shown[i].at = app_now(); } /* (its clear still not in yet: the blur holds) */
static float reveal_k(const char *vid) { if (!vid) return 1; for (int i = 0; i < nshown; i++) if (!strcmp(shown[i].vid, vid)) { float t = (float)((app_now() - shown[i].at) / 0.55); return t < 0 ? 0 : t > 1 ? 1 : t; } return 1; }

int spoil_hidden(const char *seriesId, const char *vid, int watched, const char *nextVid) {
  if (!vid || !*vid || watched || !spoil_mode()) return 0;
  if (nextVid && !strcmp(nextVid, vid)) return 0;
  for (int i = 0; i < nshown; i++) if (!strcmp(shown[i].vid, vid)) return 0;
  if (seriesId) { cJSON *l = lib_get(seriesId); const char *place = js_str(js_obj(l, "state"), "video_id", ""); if (!strcmp(place, vid)) return 0; } /* the one in progress, or the next once one ended */
  return 1;
}

#define BW 160
#define BH 90
/* a still that cannot be blurred (none, or it never loads): never drawn as hidden, its plain placeholder instead */
int spoil_blurable(const char *src) { return src && *src && !img_shaped_failed(src, BW, BH, 0, 11); }
void spoil_picture(const char *src, float x, float y, float w, float h, float r) {
  GLuint t = src && *src ? img_get_shaped(src, BW, BH, 0, 11) : 0; /* blurred once as it decodes: drawn up, soft as a lens out of focus, a little darker */
  if (t) { gfx_image(t, x, y, w, h, r, RGB(178, 178, 186), 0, 0, 1, 1); return; }
  if (src && *src && !img_shaped_failed(src, BW, BH, 0, 11)) ph_picture(0, x, y, w, h, r, WHITE_A(255)); /* on its way: the waiting glass */
  else ph_surface(x, y, w, h, r);
}

/* the mark on a hidden still: a thin crossed eye, white, a soft shade under it so it reads on any blur; nudge (0..1) a
   small peek of it as the focus rests. (hint: no longer drawn) */
void spoil_mark(float cx, float cy, float d, float hint, float nudge) {
  (void)hint; float s = 1 + 0.12f * sinf(3.14159f * (nudge < 0 ? 0 : nudge > 1 ? 1 : nudge)), is = d * 0.62f;
  gfx_scale_push(s, s, cx, cy);
  icon_draw("spoilEye", cx - is / 2 + 1, cy - is / 2 + 2, is, BLACK_A(90)); /* (its shade) */
  icon_draw("spoilEye", cx - is / 2, cy - is / 2, is, WHITE_A(245));
  gfx_scale_pop();
}

/* an episode just shown clear (drawn over its clear still): the blurred copy fading off and the eye growing a little as
   it goes; nothing once done */
void spoil_reveal_over(const char *vid, const char *src, float x, float y, float w, float h, float r, float mcx, float mcy, float d) {
  float k = reveal_k(vid); if (k >= 1) return; float e = 1 - (1 - k) * (1 - k) * (1 - k);
  if (spoil_blurable(src)) { gfx_opacity_push(1 - e); spoil_picture(src, x, y, w, h, r); gfx_opacity_pop(); }
  float s = 1 + 0.35f * e; gfx_opacity_push(1 - e); gfx_scale_push(s, s, mcx, mcy); spoil_mark(mcx, mcy, d, 0, 0); gfx_scale_pop(); gfx_opacity_pop();
}
