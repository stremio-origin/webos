/* The TV's screen-saver request (tvpower registerScreenSaverRequest), read: whether it says the screen saver is about
   to start ("state" "Active", at the top level) and its timestamp, copied as it came (a number or a string, never
   rounded), whatever the spacing. The TV sends it pretty-printed ("state": "Active"); the literal "state":"Active"
   this looked for never matched, nothing was answered and the screen saver came over the film. Pure: tests/ssreq. */
#ifndef SB_SSREQ_H
#define SB_SSREQ_H
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* the value of a top-level key: where it starts and its length (a string with its quotes); 0 = not there */
static inline int ssreq_value(const char *p, const char *key, const char **v, size_t *n) {
  size_t kl = strlen(key); int depth = 0, inStr = 0;
  for (const char *s = p; s && *s; s++) {
    if (inStr) { if (*s == '\\' && s[1]) s++; else if (*s == '"') inStr = 0; continue; }
    if (*s == '{' || *s == '[') { depth++; continue; } if (*s == '}' || *s == ']') { depth--; continue; }
    if (*s != '"') continue;
    if (depth == 1 && !strncmp(s + 1, key, kl) && s[1 + kl] == '"') { /* a key of the object itself */
      const char *c = s + 2 + kl; while (isspace((unsigned char)*c)) c++; if (*c != ':') { inStr = 1; continue; }
      c++; while (isspace((unsigned char)*c)) c++;
      const char *e = c; if (*e == '"') { e++; while (*e && *e != '"') { if (*e == '\\' && e[1]) e++; e++; } if (*e != '"') return 0; e++; }
      else while (*e && *e != ',' && *e != '}' && !isspace((unsigned char)*e)) e++;
      if (e == c) return 0; *v = c; *n = (size_t)(e - c); return 1; }
    inStr = 1; }
  return 0;
}
/* 1: the screen saver is about to start; *ts its timestamp token to send back (a number, or a string with quotes) */
static inline int ssreq_active(const char *p, char *ts, size_t cap) {
  const char *v; size_t n;
  if (!p || !ssreq_value(p, "state", &v, &n) || n != 8 || strncmp(v, "\"Active\"", 8)) return 0;
  if (!ssreq_value(p, "timestamp", &v, &n) || n + 1 > cap) return 0;
  if (*v != '"') { char tmp[64]; if (n >= sizeof tmp) return 0; memcpy(tmp, v, n); tmp[n] = 0; char *end; strtod(tmp, &end); if (*end) return 0; } /* a number, whole */
  memcpy(ts, v, n); ts[n] = 0; return 1;
}
#endif
