/* The subtitle fonts (see subfonts.h) */
#include "ui/subfonts.h"
#include "ui/text.h"
#include "core/store.h"
#include <stdlib.h>
const char *const SUBF_LAT[SUBF_N] = { "Default", "Roboto", "Open Sans", "Noto Sans", "Lato", "Montserrat", "Source Sans 3", "Fira Sans", "Atkinson Hyperlegible", "Merriweather", "Roboto Slab" };
const char *const SUBF_AR[SUBF_N] = { "Default", "Noto Naskh", "Noto Kufi", "Cairo", "Tajawal", "Almarai", "Amiri", "IBM Plex Arabic", "Changa", "Readex Pro", "El Messiri" };
static int pick(const char *k) { int i = atoi(store_get(k, "0")); return i < 0 || i >= SUBF_N ? 0 : i; }
int subfont_lat(void) { return pick("subsFont"); }
int subfont_ar(void) { return pick("subsFontAr"); }
int subfont_w(int weight) { return text_sub_weight(weight, subfont_lat(), subfont_ar()); }
