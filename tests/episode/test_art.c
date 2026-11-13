#include "core/episodeart.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void check(const char *input, unsigned width, const char *expected) {
 char b[512]; snprintf(b, sizeof b, "%s", input);
 episode_art_upgrade_tmdb(b, sizeof b, width);
 if (strcmp(b, expected)) { fprintf(stderr, "%s -> %s (expected %s)\n", input, b, expected); assert(0); }
}
int main(void) {
 check("https://image.tmdb.org/t/p/w300/abc.jpg",780,"https://image.tmdb.org/t/p/w780/abc.jpg");
 check("https://image.tmdb.org/t/p/w400/abc.jpg",780,"https://image.tmdb.org/t/p/w780/abc.jpg");
 check("https://image.tmdb.org/t/p/w500/abc.jpg",780,"https://image.tmdb.org/t/p/w780/abc.jpg");
 check("https://image.tmdb.org/t/p/w300/abc.jpg",500,"https://image.tmdb.org/t/p/w500/abc.jpg");
 check("https://image.tmdb.org/t/p/w780/abc.jpg",500,"https://image.tmdb.org/t/p/w780/abc.jpg");
 check("https://image.tmdb.org/t/p/w1280/abc.jpg",780,"https://image.tmdb.org/t/p/w1280/abc.jpg");
 check("https://image.tmdb.org/t/p/original/abc.jpg",780,"https://image.tmdb.org/t/p/original/abc.jpg");
 check("https://image.tmdb.org/t/p/h632/abc.jpg",780,"https://image.tmdb.org/t/p/h632/abc.jpg");
 check("https://image.tmdb.org/t/p/w3000/abc.jpg",780,"https://image.tmdb.org/t/p/w3000/abc.jpg");
 check("https://image.tmdb.org/t/p/w300/abc.jpg",400,"https://image.tmdb.org/t/p/w300/abc.jpg");
 check("https://example.org/image.tmdb.org/t/p/w300/abc.jpg",780,"https://example.org/image.tmdb.org/t/p/w300/abc.jpg");
 check("https://image.tmdb.org/t/p/w300x/abc.jpg",780,"https://image.tmdb.org/t/p/w300x/abc.jpg");
 check("https://image.tmdb.org/t/p/w300/abc.jpg?quality=80",780,"https://image.tmdb.org/t/p/w780/abc.jpg?quality=80");
 char tooSmall[41]="https://image.tmdb.org/t/p/w300/x.jpg";
 char original[41]; strcpy(original, tooSmall);
 episode_art_upgrade_tmdb(tooSmall, strlen(tooSmall)+1, 780); /* enough for this replacement; no overrun */
 assert(strstr(tooSmall,"w780/"));
 strcpy(tooSmall,original);episode_art_upgrade_tmdb(tooSmall,strlen(tooSmall),780);
 assert(!strcmp(tooSmall,original));
 puts("PASS: episode artwork URL upgrade, no downgrades, malformed and bounded inputs"); return 0;
}
