#include "ui/xline.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include "core/app.h"
#include "core/anim.h"
#include "core/i18n.h"
#include "platform/plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
void xline_draw_w(XLine *x, const char *t, float tx, float y, int px, int weight, float spacing, uint32_t col, float maxW, int al) {
#ifdef SB_PHONE
  /* the phone: a blank between two messages keeps the words (not cut as a stream is found), a softer fade, and a change
     mid-fade fades out whichever words are more visible */
  if (!t[0] && x->cur[0]) { if (!x->blankAt) x->blankAt = app_now(); if (app_now() - x->blankAt < 0.45) t = x->cur; } else x->blankAt = 0;
  float k = clampf((float)((app_now() - x->at) / 0.4), 0, 1); k = 1 - (1 - k) * (1 - k) * (1 - k);
  if (strcmp(t, x->cur)) { if (wl_same_but_digits(t, x->cur) && x->cur[0]) snprintf(x->cur, sizeof x->cur, "%s", t);
    else { if (k >= 0.5f || !x->old[0]) snprintf(x->old, sizeof x->old, "%s", x->cur); snprintf(x->cur, sizeof x->cur, "%s", t); x->at = app_now(); k = 0; } }
#else
  if (strcmp(t, x->cur)) { if (wl_same_but_digits(t, x->cur) && x->cur[0]) snprintf(x->cur, sizeof x->cur, "%s", t); /* a counter ticking: in place, no fade */
    else { snprintf(x->old, sizeof x->old, "%s", x->cur); snprintf(x->cur, sizeof x->cur, "%s", t); x->at = app_now(); } }
  float k = clampf((float)((app_now() - x->at) / 0.26), 0, 1); k = 1 - (1 - k) * (1 - k) * (1 - k);
#endif
  if (k < 1 && x->old[0]) { gfx_opacity_push(1 - k); text_draw(x->old, tx, y - 6 * k, px, weight, col, spacing, maxW, al); gfx_opacity_pop(); }
  if (x->cur[0]) { if (k < 1) gfx_opacity_push(k); text_draw(x->cur, tx, y + 6 * (1 - k), px, weight, col, spacing, maxW, al); if (k < 1) gfx_opacity_pop(); }
}
void xline_draw(XLine *x, const char *t, float tx, float y, int px, uint32_t col, float maxW, int al) { xline_draw_w(x, t, tx, y, px, 600, 0, col, maxW, al); }
static const char *tr(const char *s) { return T(s); }
/* SB_WAITWIDTH (development): every line, in the app's language, measured at each place's own size against its room;
   any that would not fit is logged ("[wait] too wide …"), then a summary */
static void widths(void) {
  struct { const char *where; int px, weight; float room; } W[] = { { "player", 25, 600, SB_W - 200 }, { "sign-in", 32, 700, SB_W - 160 }, { "board", 26, 500, SB_W * 0.52f - 160 }, { "live hub", 27, 600, SB_W - 200 } };
  int over = 0; float widest = 0; const char *wl = "";
  for (int i = 0; i < wl_count(); i++) { const char *t = T(wl_text(i)); for (int k = 0; k < 4; k++) { float w = text_width(t, W[k].px, W[k].weight, 0); if (k == 0 && w > widest) { widest = w; wl = t; }
      if (w > W[k].room) { over++; fprintf(stderr, "[wait] too wide for the %s (%.0f of %.0f): %s\n", W[k].where, w, W[k].room, t); } } }
  fprintf(stderr, "[wait] widths in \"%s\": %d lines, %d too wide; the widest %.0f px at the player's size: %s\n", i18n_lang(), wl_count(), over, widest, wl);
}
const char *xline_wait(WlSlot *s, unsigned gen, const char *real, int calm, WlStep step, WlAct act, int binge) {
  { static int checked; if (!checked && getenv("SB_WAITWIDTH")) { checked = 1; widths(); } }
  static WlHist hist; time_t tt = time(NULL); struct tm lt; localtime_r(&tt, &lt);
  WlCtx c; memset(&c, 0, sizeof c); c.step = step; c.act = act; c.binge = binge; c.plat = wl_platform(plat_device_name()); c.brand = plat_wait_brand();
  c.hour = lt.tm_hour; c.wday = lt.tm_wday; c.rnd = (unsigned)(app_now() * 1000) * 2654435761u;
  const char *lg = i18n_lang(); int was = s->shown; const char *r = wl_slot(s, gen, app_now(), real, calm, &c, &hist, tr, !lg || !*lg || !strcmp(lg, "en"));
  if (s->shown != was) fprintf(stderr, "[wait] line %d over \"%s\": %s\n", s->shown, real, r); /* (the words only: never an address) */
  return r;
}
