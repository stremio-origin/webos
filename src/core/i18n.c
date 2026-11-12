#include "core/i18n.h"
#include "core/store.h"
#include "platform/plat.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

static cJSON *dict; static char lang[8] = "en"; static int rtl;
/* T() runs for every label in every frame: a hash index over the dictionary (a cJSON object is a list walked with
   strcmp, thousands of entries in a translated build) */
static const cJSON **idx; static unsigned idxCap;
static unsigned hash_str(const char *s) { unsigned h = 2166136261u; while (*s) h = (h ^ (unsigned char)*s++) * 16777619u; return h; }
static void index_build(void) {
  free(idx); idx = NULL; idxCap = 0; if (!dict) return;
  unsigned n = (unsigned)cJSON_GetArraySize(dict); idxCap = 64; while (idxCap < n * 2) idxCap <<= 1;
  idx = calloc(idxCap, sizeof *idx); const cJSON *it;
  cJSON_ArrayForEach(it, dict) if (it->string) { unsigned i = hash_str(it->string) & (idxCap - 1); while (idx[i] && strcmp(idx[i]->string, it->string)) i = (i + 1) & (idxCap - 1); idx[i] = it; }
}
static const char *codes[] = { "en", "ar", "fr", "es", "de", "it", "pt", "ru", "tr", "pl", "nl", "hu" };
static const char *names[] = { "English", "العربية", "Français", "Español", "Deutsch", "Italiano", "Português", "Русский", "Türkçe", "Polski", "Nederlands", "Magyar" };
#define NCODES (int)(sizeof codes / sizeof codes[0])

static char *read_all(const char *p) { FILE *f = fopen(p, "rb"); if (!f) return NULL; fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); char *b = malloc(n + 1); size_t g = fread(b, 1, n, f); b[g] = 0; fclose(f); return b; }
static int known(const char *c) { for (int i = 0; i < NCODES; i++) if (!strcmp(codes[i], c)) return 1; return 0; }
/* the TV's own language, two letters: its settings service (asked once, the answer kept for the next start),
   else the last answer it gave, else the environment */
static char tvLang[8];
const char *i18n_tv_lang(void) {
  if (tvLang[0]) return tvLang;
  const char *loc = plat_ui_locale(); if (loc && *loc) store_set("tvUiLocale", loc); else loc = store_get("tvUiLocale", NULL);
  if (!loc || !*loc) loc = getenv("LANG");
  if (!loc || strlen(loc) < 2) loc = "en";
  tvLang[0] = (char)(loc[0] | 32); tvLang[1] = (char)(loc[1] | 32); tvLang[2] = 0;
  return tvLang;
}
/* first run: the subtitle and audio languages follow the TV's language too (only while never chosen: a choice
   made in Settings is never overridden) */
static void first_run_defaults(void) {
  static int done; if (done) return; done = 1;
  if (store_get("subsLang", NULL) && store_get("defaultAudioTrack", NULL)) return;
  static const char *M[][2] = { { "en", "eng" }, { "ar", "ara" }, { "fr", "fre" }, { "es", "spa" }, { "de", "ger" }, { "it", "ita" }, { "pt", "por" }, { "ru", "rus" }, { "tr", "tur" }, { "pl", "pol" },
    { "nl", "dut" }, { "hu", "hun" }, { "sv", "swe" }, { "he", "heb" }, { "hi", "hin" }, { "ja", "jpn" }, { "ko", "kor" }, { "zh", "chi" } };
  const char *tv = i18n_tv_lang(), *c = NULL; for (size_t i = 0; i < sizeof M / sizeof M[0]; i++) if (!strcmp(M[i][0], tv)) c = M[i][1];
  if (!c) return;
  if (!store_get("subsLang", NULL)) store_set("subsLang", c);
  if (!store_get("defaultAudioTrack", NULL)) store_set("defaultAudioTrack", c);
}
static unsigned langGen = 1;
unsigned i18n_gen(void) { return langGen; }
int i18n_init(const char *assetDir, const char *want) {
  langGen++;
  /* "auto" (or a language this build lacks): the TV's language when the app has it, else English */
  if (want && (!strcmp(want, "en") || known(want))) snprintf(lang, sizeof lang, "%s", want);
  else { const char *tv = i18n_tv_lang(); snprintf(lang, sizeof lang, "%s", known(tv) ? tv : "en"); }
  first_run_defaults();
  if (dict) { cJSON_Delete(dict); dict = NULL; }
  /* <lang>.json, then every <lang>.<part>.json beside it (each feature ships its own strings) */
  if (strcmp(lang, "en")) {
    char dir[1100], p[1400]; snprintf(dir, sizeof dir, "%s/i18n", assetDir);
    snprintf(p, sizeof p, "%s/%s.json", dir, lang); char *t = read_all(p); if (t) { dict = cJSON_Parse(t); free(t); }
    if (!dict) dict = cJSON_CreateObject();
    DIR *d = opendir(dir); struct dirent *e; size_t ll = strlen(lang);
    while (d && (e = readdir(d))) {
      size_t n = strlen(e->d_name);
      if (n <= ll + 6 || strncmp(e->d_name, lang, ll) || e->d_name[ll] != '.' || strcmp(e->d_name + n - 5, ".json") || n == ll + 5) continue;
      snprintf(p, sizeof p, "%s/%s", dir, e->d_name); char *x = read_all(p); if (!x) continue;
      cJSON *part = cJSON_Parse(x); free(x); if (!part) { fprintf(stderr, "[i18n] bad %s\n", e->d_name); continue; }
      cJSON *it; cJSON_ArrayForEach(it, part) if (cJSON_IsString(it) && it->string) { cJSON_DeleteItemFromObjectCaseSensitive(dict, it->string); cJSON_AddStringToObject(dict, it->string, it->valuestring); }
      cJSON_Delete(part);
    }
    if (d) closedir(d);
  }
  index_build();
  rtl = !strcmp(lang, "ar");
  return 1;
}
const char *i18n_lang(void) { return lang; }
int i18n_rtl(void) { return rtl; }
const char *T(const char *en) {
  if (!idx || !en) return en;
  unsigned i = hash_str(en) & (idxCap - 1); const cJSON *v;
  while ((v = idx[i]) && strcmp(v->string, en)) i = (i + 1) & (idxCap - 1);
  return cJSON_IsString(v) && v->valuestring && v->valuestring[0] ? v->valuestring : en;
}
const char *i18n_key_of(const char *tr) {
  if (!dict || !tr) return NULL; const cJSON *it;
  cJSON_ArrayForEach(it, dict) if (cJSON_IsString(it) && it->valuestring && !strcmp(it->valuestring, tr)) return it->string;
  return NULL;
}
static char bufs[8][1024]; static int bi;
static void subst(char *out, size_t cap, const char *s, const char *k1, const char *v1, const char *k2, const char *v2) {
  size_t o = 0;
  while (*s && o + 1 < cap) {
    if (*s == '{') { const char *e = strchr(s, '}');
      if (e) { size_t kl = e - s - 1; const char *v = NULL;
        if (k1 && strlen(k1) == kl && !strncmp(s + 1, k1, kl)) v = v1; else if (k2 && strlen(k2) == kl && !strncmp(s + 1, k2, kl)) v = v2;
        if (v) { size_t vl = strlen(v); if (vl > cap - 1 - o) vl = cap - 1 - o; memcpy(out + o, v, vl); o += vl; s = e + 1; continue; } } }
    out[o++] = *s++;
  }
  out[o] = 0;
}
const char *T1(const char *en, const char *k1, const char *v1) { char *b = bufs[bi++ & 7]; subst(b, 1024, T(en), k1, v1 ? v1 : "", NULL, NULL); return b; }
const char *T2(const char *en, const char *k1, const char *v1, const char *k2, const char *v2) { char *b = bufs[bi++ & 7]; subst(b, 1024, T(en), k1, v1 ? v1 : "", k2, v2 ? v2 : ""); return b; }
const char *i18n_lang_name(const char *code) { for (int i = 0; i < NCODES; i++) if (!strcmp(codes[i], code)) return names[i]; return code; }
int i18n_lang_count(void) { return NCODES; }
const char *i18n_lang_code(int i) { return i >= 0 && i < NCODES ? codes[i] : "en"; }
