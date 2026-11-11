#include "core/forcedsubs.h"
#include "core/json.h"
#include <strings.h>
#include <stddef.h>
#include <string.h>

/* Fold common Latin diacritics and Cyrillic case, retaining other Unicode scripts.
   Treat filename separators as word boundaries; no locale or external library needed. */
static unsigned utf8_next(const unsigned char **p) {
  unsigned c = *(*p)++;
  if (c < 0x80) return c;
  int n = (c & 0xe0) == 0xc0 ? 1 : (c & 0xf0) == 0xe0 ? 2 : (c & 0xf8) == 0xf0 ? 3 : 0;
  if (!n) return ' ';
  c &= (1u << (6 - n)) - 1;
  for (int i = 0; i < n; i++) { if ((**p & 0xc0) != 0x80) return ' '; c = c << 6 | (*(*p)++ & 0x3f); }
  return c;
}
static char ascii_fold(unsigned c) {
  if (c < 128) return c >= 'A' && c <= 'Z' ? (char)(c + 32) : (char)c;
  switch (c) {
    case 0xc0: case 0xc1: case 0xc2: case 0xc3: case 0xc4: case 0xc5:
    case 0xe0: case 0xe1: case 0xe2: case 0xe3: case 0xe4: case 0xe5:
    case 0x100: case 0x101: case 0x102: case 0x103: case 0x104: case 0x105: return 'a';
    case 0xc7: case 0xe7: case 0x106: case 0x107: case 0x10c: case 0x10d: return 'c';
    case 0xc8: case 0xc9: case 0xca: case 0xcb: case 0xe8: case 0xe9: case 0xea: case 0xeb:
    case 0x118: case 0x119: return 'e';
    case 0xcc: case 0xcd: case 0xce: case 0xcf: case 0xec: case 0xed: case 0xee: case 0xef: return 'i';
    case 0xd1: case 0xf1: case 0x143: case 0x144: return 'n';
    case 0xd2: case 0xd3: case 0xd4: case 0xd5: case 0xd6:
    case 0xf2: case 0xf3: case 0xf4: case 0xf5: case 0xf6: return 'o';
    case 0xd9: case 0xda: case 0xdb: case 0xdc: case 0xf9: case 0xfa: case 0xfb: case 0xfc: return 'u';
    case 0xdd: case 0xfd: case 0xff: case 0x178: return 'y';
    case 0x141: case 0x142: return 'l';
    case 0x15a: case 0x15b: case 0x160: case 0x161: return 's';
    case 0x179: case 0x17a: case 0x17b: case 0x17c: case 0x17d: case 0x17e: return 'z';
    case 0x130: case 0x131: return 'i';
    default: return 0;
  }
}
static void add_cp(char *out, size_t *n, size_t cap, unsigned c) {
  if (*n + 5 >= cap) return;
  if (c < 128) out[(*n)++] = (char)c;
  else if (c < 0x800) { out[(*n)++] = (char)(0xc0 | (c >> 6)); out[(*n)++] = (char)(0x80 | (c & 63)); }
  else if (c < 0x10000) { out[(*n)++] = (char)(0xe0 | (c >> 12)); out[(*n)++] = (char)(0x80 | ((c >> 6) & 63)); out[(*n)++] = (char)(0x80 | (c & 63)); }
  else { out[(*n)++] = (char)(0xf0 | (c >> 18)); out[(*n)++] = (char)(0x80 | ((c >> 12) & 63)); out[(*n)++] = (char)(0x80 | ((c >> 6) & 63)); out[(*n)++] = (char)(0x80 | (c & 63)); }
}
static void normalise(const char *src, char *out, size_t cap) {
  size_t n = 0;
  const unsigned char *p = (const unsigned char *)(src ? src : "");
  while (*p && n + 5 < cap) {
    unsigned c = utf8_next(&p); char f = ascii_fold(c);
    if (f) c = (unsigned char)f;
    else if (c >= 0x410 && c <= 0x42f) c += 0x20; /* Russian capitals */
    else if (c == 0x401) c = 0x451;
    else if (c < 128 || (c >= 0x2000 && c <= 0x206f) || c == 0x3000) c = ' ';
    if (c < 128 && !(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9')) c = ' ';
    if (c != ' ' || (n && out[n - 1] != ' ')) add_cp(out, &n, cap, c);
  }
  out[n] = 0;
}
static int token(const char *s, const char *word) {
  size_t len = strlen(word);
  while (*s) {
    while (*s == ' ') s++;
    if (!strncmp(s, word, len) && (s[len] == ' ' || s[len] == 0)) return 1;
    while (*s && *s != ' ') s++;
  }
  return 0;
}
static int phrase(const char *s, const char *words) {
  size_t len = strlen(words);
  for (const char *p = s; (p = strstr(p, words)); p++) {
    if ((p == s || p[-1] == ' ') && (!p[len] || p[len] == ' ')) return 1;
  }
  return 0;
}
int forcedsubs_name(const char *name) {
  if (!name || !*name) return 0;
  char s[768]; normalise(name, s, sizeof s);
  if (token(s, "unforced") || token(s, "notforced") || phrase(s, "not forced") ||
      phrase(s, "non forced") || phrase(s, "non forces") || phrase(s, "non forcees") ||
      phrase(s, "no forced") || phrase(s, "sans forced")) return 0;
  static const char *words[] = {
    "forced", "force", "forces", "forcee", "forcees",
    "forzado", "forzada", "forzados", "forzadas",
    "forzato", "forzata", "forzati", "forzate",
    "forcado", "forcada", "forcados", "forcadas",
    "erzwungen", "erzwungene", "zwang", "geforceerd", "geforceerde",
    "wymuszony", "wymuszona", "wymuszone", "wymuszonych",
    "zorunlu", "pakotettu", "pakotetut", "tvungen", "tvang",
    "форсированный", "форсированная", "форсированные", "принудительные",
    "إجباري", "اجباري", "إجبارية", "अनिवार्य", NULL
  };
  for (int i = 0; words[i]; i++) if (token(s, words[i])) return 1;
  if (phrase(s, "foreign only") || phrase(s, "foreign parts") ||
      phrase(s, "foreign dialogue") || phrase(s, "foreign dialog") ||
      phrase(s, "signs and songs") || phrase(s, "signs songs")) return 1;
  /* These scripts often put the word directly next to "subtitle" with no delimiter. */
  return strstr(s, "強制") || strstr(s, "强制") || strstr(s, "강제");
}

/* Stremio add-ons do not all expose the same fields. Prefer explicit flags,
   then actual subtitle filenames and visible labels; a release name is only a
   fallback when no subtitle-specific filename was returned. */
int forcedsubs_metadata(const struct cJSON *entry, const char *label, const char *name) {
  const cJSON *f = entry ? cJSON_GetObjectItemCaseSensitive(entry, "forced") : NULL;
  const cJSON *disp = js_obj(entry, "disposition");
  if (cJSON_IsTrue(f) || (cJSON_IsNumber(f) && f->valuedouble > 0) ||
      (cJSON_IsString(f) && (!strcasecmp(f->valuestring, "true") || !strcmp(f->valuestring, "1"))) ||
      js_bool(entry, "isForced", 0) || js_bool(entry, "is_forced", 0) ||
      js_bool(entry, "forcedSubtitle", 0) || js_bool(disp, "forced", 0) ||
      js_num(disp, "forced", 0) > 0) return 1;
  const cJSON *flags = entry ? cJSON_GetObjectItemCaseSensitive(entry, "flags") : NULL;
  if (cJSON_IsString(flags) && forcedsubs_name(flags->valuestring)) return 1;
  if (cJSON_IsArray(flags)) { const cJSON *v;
    cJSON_ArrayForEach(v, flags) if (cJSON_IsString(v) && forcedsubs_name(v->valuestring)) return 1;
  }
  if (forcedsubs_name(js_str(entry, "subtitleFileName", "")) ||
      forcedsubs_name(js_str(entry, "fileName", js_str(entry, "filename", ""))) ||
      forcedsubs_name(js_str(entry, "title", "")) ||
      forcedsubs_name(label) || forcedsubs_name(name)) return 1;
  if (!js_str(entry, "subtitleFileName", NULL) &&
      !js_str(entry, "fileName", NULL) && !js_str(entry, "filename", NULL))
    return forcedsubs_name(js_str(entry, "releaseName", js_str(entry, "release", "")));
  return 0;
}
