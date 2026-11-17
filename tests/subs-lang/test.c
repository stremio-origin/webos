/* Exercise the real body parser: reference subtitles must use their language's code page. */
#include "core/subs.h"
#include <stdio.h>
#include <string.h>

static int check(const char *body, const char *lang, const char *want) {
  Cue *c = NULL; int n = subs_parse_body((const unsigned char *)body, strlen(body), lang, &c);
  int ok = n == 1 && !strcmp(c[0].t, want) && c[0].s == 1 && c[0].e == 2;
  if (!ok) fprintf(stderr, "FAIL %s: %s\n", lang ? lang : "unknown", n ? c[0].t : "no cue");
  subs_free(c, n); return !ok;
}
int main(void) {
  const char *hu = "1\n00:00:01,000 --> 00:00:02,000\n\xf5 \xfb \xd5 \xdb\n";
  int fails = 0;
  fails += check(hu, "hun", "ő ű Ő Ű");
  fails += check(hu, "hu", "ő ű Ő Ű");
  fails += check(hu, "hu-HU", "ő ű Ő Ű");
  fails += check("1\n00:00:01,000 --> 00:00:02,000\nUnknown language\n", NULL, "Unknown language");
  fails += check("1\n00:00:01,000 --> 00:00:02,000\nő ű\n", "", "ő ű");
  fails += check("1\n00:00:01,000 --> 00:00:02,000\n\xb3\xb9\x9c\x9f\xbf\n", "pl-PL", "łąśźż");
  fails += check("1\n00:00:01,000 --> 00:00:02,000\n\xc7" "a s\xfbr\n", "fre", "Ça sûr");
  fails += check("1\n00:00:01,000 --> 00:00:02,000\nő ű Ő Ű\n", "hun", "ő ű Ő Ű");
  printf("subs-lang: %s (8 cases)\n", fails ? "FAILED" : "all passed"); return !!fails;
}
