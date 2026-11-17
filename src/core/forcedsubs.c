/* Forced subtitles by name: the name folded to plain lower-case words (accents off, Cyrillic lower-cased, anything not a
   letter or a digit a space), then looked up as whole words. From PR #50 by valentinBoch */
#include "core/forcedsubs.h"
#include "core/json.h"
#include <string.h>
#include <strings.h>
#include <stddef.h>

static unsigned utf8_next(const unsigned char **p) { /* a bad sequence: a space, its next byte read again */
  unsigned c = *(*p)++; if (c < 0x80) return c;
  int n = (c & 0xe0) == 0xc0 ? 1 : (c & 0xf0) == 0xe0 ? 2 : (c & 0xf8) == 0xf0 ? 3 : 0; if (!n) return ' ';
  c &= (1u << (6 - n)) - 1;
  for (int i = 0; i < n; i++) { if ((**p & 0xc0) != 0x80) return ' '; c = c << 6 | (*(*p)++ & 0x3f); }
  return c;
}
static char latin_fold(unsigned c) { /* a Latin letter without its accent, lower-case; 0 for any other */
  if (c < 128) return c >= 'A' && c <= 'Z' ? (char)(c + 32) : (char)c;
  switch (c) {
    case 0xc0: case 0xc1: case 0xc2: case 0xc3: case 0xc4: case 0xc5: case 0xe0: case 0xe1: case 0xe2: case 0xe3: case 0xe4: case 0xe5:
    case 0x100: case 0x101: case 0x102: case 0x103: case 0x104: case 0x105: return 'a';
    case 0xc7: case 0xe7: case 0x106: case 0x107: case 0x10c: case 0x10d: return 'c';
    case 0xc8: case 0xc9: case 0xca: case 0xcb: case 0xe8: case 0xe9: case 0xea: case 0xeb: case 0x118: case 0x119: case 0x11a: case 0x11b: return 'e';
    case 0xcc: case 0xcd: case 0xce: case 0xcf: case 0xec: case 0xed: case 0xee: case 0xef: case 0x130: case 0x131: return 'i';
    case 0xd1: case 0xf1: case 0x143: case 0x144: return 'n';
    case 0xd2: case 0xd3: case 0xd4: case 0xd5: case 0xd6: case 0xf2: case 0xf3: case 0xf4: case 0xf5: case 0xf6: return 'o';
    case 0xd9: case 0xda: case 0xdb: case 0xdc: case 0xf9: case 0xfa: case 0xfb: case 0xfc: return 'u';
    case 0xdd: case 0xfd: case 0xff: case 0x178: return 'y';
    case 0x141: case 0x142: return 'l';
    case 0x15a: case 0x15b: case 0x15e: case 0x15f: case 0x160: case 0x161: return 's';
    case 0x179: case 0x17a: case 0x17b: case 0x17c: case 0x17d: case 0x17e: return 'z';
    case 0x11e: case 0x11f: return 'g';
    case 0x162: case 0x163: case 0x21a: case 0x21b: case 0x164: case 0x165: return 't';
    case 0x218: case 0x219: return 's';
    case 0x158: case 0x159: return 'r';
    default: return 0;
  }
}
static void put_cp(char *out, size_t *n, unsigned c) {
  if (c < 0x800) { out[(*n)++] = (char)(0xc0 | (c >> 6)); out[(*n)++] = (char)(0x80 | (c & 63)); }
  else if (c < 0x10000) { out[(*n)++] = (char)(0xe0 | (c >> 12)); out[(*n)++] = (char)(0x80 | ((c >> 6) & 63)); out[(*n)++] = (char)(0x80 | (c & 63)); }
  else { out[(*n)++] = (char)(0xf0 | (c >> 18)); out[(*n)++] = (char)(0x80 | ((c >> 12) & 63)); out[(*n)++] = (char)(0x80 | ((c >> 6) & 63)); out[(*n)++] = (char)(0x80 | (c & 63)); }
}
/* " english forced srt ": words between single spaces, one at each end too (a word is then " w ") */
static void fold(const char *src, char *out, size_t cap) {
  size_t n = 0; const unsigned char *p = (const unsigned char *)(src ? src : ""); out[n++] = ' ';
  while (*p && n + 6 < cap) {
    unsigned c = utf8_next(&p); char f = latin_fold(c);
    if (f) c = (unsigned char)((f >= 'a' && f <= 'z') || (f >= '0' && f <= '9') ? f : ' ');
    else if (c >= 0x410 && c <= 0x42f) c += 0x20; /* Russian capitals */
    else if (c == 0x401) c = 0x451;
    else if ((c >= 0x80 && c < 0xc0) || (c >= 0x2000 && c <= 0x206f) || c == 0x3000 || c == 0xd7 || c == 0xf7) c = ' '; /* punctuation, spaces */
    if (c == ' ') { if (out[n - 1] != ' ') out[n++] = ' '; }
    else if (c < 128) out[n++] = (char)c; else put_cp(out, &n, c);
  }
  if (out[n - 1] != ' ') out[n++] = ' ';
  out[n] = 0;
}
static int has(const char *s, const char *words) { /* whole words: " forced " in " english forced " */
  char k[96]; size_t l = strlen(words); if (l + 3 > sizeof k) return 0; k[0] = ' '; memcpy(k + 1, words, l); k[l + 1] = ' '; k[l + 2] = 0;
  return strstr(s, k) != NULL;
}
static int named(const char *name, int strict) {
  if (!name || !*name) return 0;
  char s[768]; fold(name, s, sizeof s);
  static const char *const NOT[] = { "unforced", "notforced", "not forced", "non forced", "no forced", "sans forced", "non forces", "non forcees", NULL };
  for (int i = 0; NOT[i]; i++) if (has(s, NOT[i])) return 0;
  static const char *const WORDS[] = { "forced", "forcee", "forcees", /* English, French */
    "forzado", "forzada", "forzados", "forzadas", "forzato", "forzata", "forzati", "forzate", "forcado", "forcada", "forcados", "forcadas",
    "erzwungen", "erzwungene", "geforceerd", "geforceerde", "wymuszony", "wymuszona", "wymuszone", "wymuszonych", "pakotettu", "pakotetut",
    "tvungen", "tvungne", "zorunlu", "fortat", "fortate", "fortati", "vynucene", "kenyszeritett",
    "форсированный", "форсированная", "форсированные", "принудительные", "примусові", "إجباري", "اجباري", "إجبارية", "اجبارية",
    "foreign only", "foreign parts", "foreign dialogue", "foreign dialog", "signs and songs", "signs songs", NULL };
  for (int i = 0; WORDS[i]; i++) if (has(s, WORDS[i])) return 1;
  if (!strict) { /* in a track's own name only (a film's title can hold these): French without its accent, an anime's signs track */
    if (has(s, "full")) return 0;
    if (has(s, "force") || has(s, "forces") || has(s, "signs")) return 1; }
  return strstr(s, "強制") || strstr(s, "强制") || strstr(s, "강제"); /* (written right next to the word for subtitles) */
}
int forcedsubs_name(const char *name) { return named(name, 0); }
int forcedsubs_file(const char *name) { return named(name, 1); }

int forcedsubs_entry(const struct cJSON *e, const char *label) {
  if (!e) return 0;
  const cJSON *f = cJSON_GetObjectItemCaseSensitive(e, "forced"), *d = js_obj(e, "disposition");
  if (cJSON_IsTrue(f) || (cJSON_IsNumber(f) && f->valuedouble > 0) || (cJSON_IsString(f) && (!strcasecmp(f->valuestring, "true") || !strcmp(f->valuestring, "1")))
      || js_bool(e, "isForced", 0) || js_bool(e, "is_forced", 0) || js_bool(e, "forcedSubtitle", 0) || js_bool(d, "forced", 0) || js_num(d, "forced", 0) > 0) return 1;
  const cJSON *fl = cJSON_GetObjectItemCaseSensitive(e, "flags"), *v;
  if (cJSON_IsString(fl) && forcedsubs_name(fl->valuestring)) return 1;
  if (cJSON_IsArray(fl)) cJSON_ArrayForEach(v, fl) if (cJSON_IsString(v) && forcedsubs_name(v->valuestring)) return 1;
  if (forcedsubs_name(label) || forcedsubs_name(js_str(e, "title", "")) || forcedsubs_name(js_str(e, "name", ""))) return 1;
  const char *file = js_str(e, "subtitleFileName", js_str(e, "fileName", js_str(e, "filename", NULL)));
  if (file) return forcedsubs_file(file);
  return forcedsubs_file(js_str(e, "releaseName", js_str(e, "release", ""))); /* (a release's name only when the entry has no file name) */
}
