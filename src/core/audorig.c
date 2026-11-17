/* "Original" audio (see audorig.h) */
#include "core/audorig.h"
#include <string.h>
#include <ctype.h>
static int has_ci(const char *s, const char *w) { if (!s) return 0; size_t n = strlen(w); for (; *s; s++) { size_t i = 0; while (i < n && s[i] && tolower((unsigned char)s[i]) == w[i]) i++; if (i == n) return 1; } return 0; }
static int commentary(const char *name) { return has_ci(name, "comment") || has_ci(name, "\xd0\xba\xd0\xbe\xd0\xbc\xd0\xbc\xd0\xb5\xd0\xbd\xd1\x82") /* "коммент" */ || has_ci(name, "director"); }
int aud_pick_original(const char *orig, const AudInfo *t, int n, int (*match)(const char *a, const char *b)) {
  if (!orig || !*orig || !t || n <= 0) return -1; int best = -1, bs = -1;
  for (int i = 0; i < n; i++) { if (!t[i].lang || !match(t[i].lang, orig) || commentary(t[i].name)) continue;
    int sc = (t[i].bad ? 0 : 8) + (has_ci(t[i].name, "original") ? 4 : 0) + (t[i].def ? 2 : 0); /* (ties: the first) */
    if (sc > bs) { bs = sc; best = i; } }
  return best; }
