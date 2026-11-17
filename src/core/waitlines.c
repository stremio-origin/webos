/* Loading lines: see waitlines.h */
#include "core/waitlines.h"
#include <stdio.h>
#include <string.h>
#include "core/waitlines_pools.h"

__attribute__((weak)) const char *plat_wait_brand(void) { return ""; } /* (a platform that reads its make defines its own) */

int wl_count(void) { return WL_NLINES; }
const char *wl_text(int id) { return id >= 0 && id < WL_NLINES ? WL_LINES[id] : ""; }
int wl_pool_of(int id) { for (int p = 0; p < WLP_N; p++) if (id >= WL_START[p] && id < WL_START[p + 1]) return p; return -1; }
WlPlat wl_platform(const char *d) {
  if (!d) return WL_PLAT_OTHER;
  if (strstr(d, "webOS")) return WL_PLAT_LG; if (strstr(d, "Tizen") || strstr(d, "Samsung")) return WL_PLAT_SAMSUNG;
  if (strstr(d, "Android TV")) return WL_PLAT_ATV; if (strstr(d, "Android")) return WL_PLAT_PHONE; return WL_PLAT_OTHER;
}
static int late(int h) { return h >= 22 || h < 5; }
static int weekend(int wd) { return wd == 0 || wd == 6; }
/* the time of day's pool: morning 5-11, afternoon 11-17, after work 17-21 on weekdays, evening 21-22 (17-22 at weekends), late 22-5 */
static int time_pool(const WlCtx *c) {
  int h = c->hour; if (late(h)) return WLP_LATE; if (h < 11) return WLP_MORNING; if (h < 17) return WLP_AFTERNOON;
  if (!weekend(c->wday) && h < 21) return WLP_AFTERWORK; return WLP_EVENING;
}
static int plat_pool(const WlCtx *c) { /* the brand only on an exact match; anything else the platform's own lines */
  const char *b = c->brand ? c->brand : "";
  switch (c->plat) {
    case WL_PLAT_LG: return !strcmp(b, "OLED") ? WLP_LG_OLED : !strcmp(b, "QNED") ? WLP_LG_QNED : !strcmp(b, "NanoCell") ? WLP_LG_NANO : WLP_LG;
    case WL_PLAT_SAMSUNG: return WLP_SAMSUNG;
    case WL_PLAT_ATV: return !strcmp(b, "Shield") ? WLP_ATV_SHIELD : !strcmp(b, "Sony") ? WLP_ATV_SONY : !strcmp(b, "Chromecast") ? WLP_ATV_CHROMECAST : !strcmp(b, "Fire TV") ? WLP_ATV_FIRETV
                        : !strcmp(b, "Xiaomi") ? WLP_ATV_XIAOMI : !strcmp(b, "Philips") ? WLP_ATV_PHILIPS : !strcmp(b, "TCL") ? WLP_ATV_TCL : WLP_ATV;
    case WL_PLAT_PHONE: return WLP_PHONE;
    default: return -1; }
}
static int combo_pool(const WlCtx *c) { /* the moments that fit best, first */
  int binge = c->binge >= 2;
  if (binge && late(c->hour)) return WLP_LATEBINGE;
  if (binge && weekend(c->wday)) return WLP_WEEKENDBINGE;
  if ((c->wday == 5 && c->hour >= 17) || (c->wday == 6 && c->hour < 5)) return WLP_FRIDAY; /* Friday 17:00 to 05:00 */
  return -1;
}
static int plain_pool(WlStep s) { /* a starting stream has no lines of its own (none would always be true): the old humour */
  return s == WL_SIGNIN ? WLP_SIGNIN : s == WL_CHOOSE ? WLP_CHOOSE : s == WL_PEERS ? WLP_PEERS : s == WL_FILES ? WLP_FILES : s == WL_PIECES ? WLP_PIECES : s == WL_LIVE ? WLP_LIVE : WLP_OLD; }
static int choose_pool(const WlCtx *c) {
  if (c->step == WL_PEERS || c->step == WL_FILES || c->step == WL_PIECES) return plain_pool(c->step); /* the peer steps say what is happening, plainly */
  unsigned r = c->rnd;
  int find = c->step == WL_FIND || c->step == WL_CHOOSE;
  if (c->nth == 0 && (find || c->step == WL_LIVE)) { int p = combo_pool(c); if (p >= 0) return p; }
  if (c->nth == 0 && find) { /* what the viewer is doing (a binge's own lines when no moment fits it) */
    if (c->binge >= 2) return WLP_BINGE;
    if (c->act == WL_ACT_FILM) return WLP_FILM; if (c->act == WL_ACT_RESUME) return WLP_RESUME; if (c->act == WL_ACT_FIRSTTODAY) return WLP_FIRSTTODAY; }
  int pp = plat_pool(c); if (pp >= 0 && r % 4 == 0) return pp; /* the platform: one line in four */
  if ((r / 4) % 3 == 0) { if (weekend(c->wday) && (r / 12) % 2) return WLP_WEEKEND; return time_pool(c); } /* the time of day: one in three */
  return plain_pool(c->step);
}
static int in_hist(const WlHist *h, int id) { for (int i = 0; i < h->n; i++) if (h->id[i] == id) return 1; return 0; }
static void remember(WlHist *h, int id) { h->id[h->at] = (short)id; h->at = (h->at + 1) % 20; if (h->n < 20) h->n++; }
static int last_of(const WlHist *h) { return h->n ? h->id[(h->at + 19) % 20] : -1; }
static int pick_in(int p, unsigned r, WlHist *h) { /* not one of the last 20 when the pool allows, never the last one again */
  int a = WL_START[p], b = WL_START[p + 1], el[64], n = 0, last = last_of(h);
  for (int i = a; i < b && n < 64; i++) if (!in_hist(h, i)) el[n++] = i;
  if (!n) for (int i = a; i < b && n < 64; i++) if (i != last) el[n++] = i;
  if (!n) el[n++] = a;
  int id = el[(r / 24) % (unsigned)n]; remember(h, id); return id;
}
int wl_pick(const WlCtx *c, WlHist *h) { return pick_in(choose_pool(c), c->rnd, h); }

int wl_same_but_digits(const char *a, const char *b) {
  while (*a || *b) { while (*a >= '0' && *a <= '9') a++; while (*b >= '0' && *b <= '9') b++; if (*a != *b) return 0; if (*a) { a++; b++; } } return 1; }

const char *wl_slot(WlSlot *s, unsigned gen, double now, const char *real, int calm, WlCtx *c, WlHist *h, const char *(*tr)(const char *), int english) {
  if (s->gen != gen) { s->gen = gen; s->shown = s->showing = 0; s->since = now; s->showAt = s->endAt = 0; s->last[0] = 0; s->line = NULL; }
  if (!wl_same_but_digits(real, s->last)) { snprintf(s->last, sizeof s->last, "%s", real); s->since = now; /* the real text changed: it takes the place at once */
    if (s->showing) { s->showing = 0; s->endAt = now; } }
  if (!calm) { if (s->showing) { s->showing = 0; s->endAt = now; } return real; }
  if (s->showing) { if (now - s->showAt < 3.5) return s->line; s->showing = 0; s->endAt = now; return real; }
  if (s->shown >= 3 || now - s->since < 4) return real;
  if (s->shown == 1 && now - s->endAt < 5) return real; /* the 2nd at least 5 s after the 1st went */
  if (s->shown == 2 && now - s->endAt < 6) return real; /* the 3rd at least 6 s after the 2nd */
  c->nth = s->shown; int id = wl_pick(c, h); const char *key = wl_text(id), *t = tr ? tr(key) : key;
  if (!english && (!t || !strcmp(t, key))) { WlCtx o = *c; int oid = pick_in(WLP_OLD, o.rnd, h); key = wl_text(oid); t = tr ? tr(key) : key; } /* not translated yet: an old translated line */
  s->line = t; s->showing = 1; s->showAt = now; s->shown++; return s->line;
}
