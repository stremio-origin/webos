#include "core/episodeart.h"
#include <stdio.h>
#include <string.h>

void episode_art_upgrade_tmdb(char *url, size_t cap, unsigned min_width) {
  static const char prefix[] = "https://image.tmdb.org/t/p/";
  if (!url || !cap || strncmp(url, prefix, sizeof prefix - 1)) return;
  char *size = url + sizeof prefix - 1;
  /* TMDB's size segment is the first path segment, not a substring of the filename.
     Only known width sizes are rewritten: h632 and original must be preserved. */
  if (*size != 'w') return;
  char *end = size + 1;
  unsigned long current = 0;
  while (*end >= '0' && *end <= '9') {
    current = current * 10 + (unsigned long)(*end - '0');
    if (current > 10000) return;
    end++;
  }
  if (*end != '/' || end == size + 1 || current >= min_width) return;
  if (min_width != 500 && min_width != 780) return; /* sizes actually served by TMDB */
  char replacement[12]; int n = snprintf(replacement, sizeof replacement, "w%u", min_width);
  size_t off = (size_t)(size - url), rest = strlen(end) + 1;
  if (off + (size_t)n + rest > cap) return;
  memmove(size + n, end, rest);
  memcpy(size, replacement, (size_t)n);
}
