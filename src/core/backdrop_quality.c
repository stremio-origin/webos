#include "core/backdrop_quality.h"
#include <stdio.h>
#include <string.h>
void backdrop_tmdb_upgrade(char *url, size_t cap) {
  static const char prefix[] = "https://image.tmdb.org/t/p/";
  if (!url || !cap || strncmp(url, prefix, sizeof prefix - 1)) return;
  char *size = url + sizeof prefix - 1;
  if (!strncmp(size, "original/", 9)) { /* preserve prior behavior's 1280 px memory bound */
    size_t suffix = strlen(size + 8) + 1;
    if ((size_t)(size - url) + 5 + suffix > cap) return;
    memmove(size + 5, size + 8, suffix); memcpy(size, "w1280", 5); return;
  }
  if (*size != 'w') return;
  char *end = size + 1;
  unsigned long current = 0;
  while (*end >= '0' && *end <= '9') {
    current = current * 10 + (unsigned long)(*end - '0');
    if (current > 10000) return;
    end++;
  }
  if (*end != '/' || end == size + 1 || current >= 1280) return;
  /* Keep this limited to actual TMDB width variants, not arbitrary CDN filenames. */
  if (current != 92 && current != 154 && current != 185 && current != 300 &&
      current != 342 && current != 400 && current != 500 && current != 780) return;
  size_t off = (size_t)(size - url), suffix = strlen(end) + 1;
  if (off + 5 + suffix > cap) return;
  memmove(size + 5, end, suffix); memcpy(size, "w1280", 5);
}
