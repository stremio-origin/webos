#include "platform/plat.h"
#include "core/subs.h"
#include "core/net.h"
#include "core/json.h"
#include "core/addons.h"
#include "core/stremio.h"
#include <zlib.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
static int tc(const char *s, double *out, const char **end) {
  int h = 0, m = 0, sec = 0; char ms[8] = ""; int n = 0; const char *p = s;
  if (sscanf(p, "%d:%d:%d%n", &h, &m, &sec, &n) == 3 && n) { p += n; }
  else if (sscanf(p, "%d:%d%n", &m, &sec, &n) == 2 && n) { h = 0; p += n; } else return 0;
  if (*p == ',' || *p == '.') { p++; int i = 0; while (*p >= '0' && *p <= '9' && i < 7) ms[i++] = *p++; ms[i] = 0; }
  double frac = ms[0] ? atof(ms) / pow(10, strlen(ms)) : 0; *out = h * 3600 + m * 60 + sec + frac; *end = p; return 1;
}
static int cmp_cue(const void *a, const void *b) { double x = ((const Cue *)a)->s, y = ((const Cue *)b)->s; return x < y ? -1 : x > y; }
static void cue_add(Cue **cues, int *n, int *cap, double s, double e, const char *t) {
  if (*n == *cap) { *cap = *cap ? *cap * 2 : 256; *cues = realloc(*cues, *cap * sizeof **cues); }
  (*cues)[*n].s = s; (*cues)[*n].e = e; (*cues)[*n].t = strdup(t); (*n)++;
}

/* ---- the bytes as UTF-8 text ---- */
static size_t put_utf8(char *o, unsigned c) {
  if (c < 0x80) { o[0] = (char)c; return 1; }
  if (c < 0x800) { o[0] = (char)(0xc0 | c >> 6); o[1] = (char)(0x80 | (c & 63)); return 2; }
  if (c < 0x10000) { o[0] = (char)(0xe0 | c >> 12); o[1] = (char)(0x80 | ((c >> 6) & 63)); o[2] = (char)(0x80 | (c & 63)); return 3; }
  o[0] = (char)(0xf0 | c >> 18); o[1] = (char)(0x80 | ((c >> 12) & 63)); o[2] = (char)(0x80 | ((c >> 6) & 63)); o[3] = (char)(0x80 | (c & 63)); return 4;
}
static int utf8_ok(const unsigned char *b, size_t n) {
  for (size_t i = 0; i < n;) { unsigned c = b[i]; int k = c < 0x80 ? 0 : (c & 0xe0) == 0xc0 ? 1 : (c & 0xf0) == 0xe0 ? 2 : (c & 0xf8) == 0xf0 ? 3 : -1;
    if (k < 0 || (k == 1 && c < 0xc2) || i + k >= n) return 0;
    for (int j = 1; j <= k; j++) if ((b[i + j] & 0xc0) != 0x80) return 0;
    i += k + 1; }
  return 1;
}
/* Windows-1252's 0x80-0x9F (the rest of the byte range is Latin-1 as it stands) */
static const unsigned short CP1252[32] = { 0x20ac, 0xfffd, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021, 0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0xfffd, 0x017d, 0xfffd,
  0xfffd, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0xfffd, 0x017e, 0x0178 };
/* Windows-1250 (Central European), 0x80-0xFF: its letters differ from Latin-1's (Hungarian ő ű are õ û there) */
static const unsigned short CP1250[128] = { 0x20ac, 0xfffd, 0x201a, 0xfffd, 0x201e, 0x2026, 0x2020, 0x2021, 0xfffd, 0x2030, 0x0160, 0x2039, 0x015a, 0x0164, 0x017d, 0x0179,
  0xfffd, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0xfffd, 0x2122, 0x0161, 0x203a, 0x015b, 0x0165, 0x017e, 0x017a,
  0x00a0, 0x02c7, 0x02d8, 0x0141, 0x00a4, 0x0104, 0x00a6, 0x00a7, 0x00a8, 0x00a9, 0x015e, 0x00ab, 0x00ac, 0x00ad, 0x00ae, 0x017b,
  0x00b0, 0x00b1, 0x02db, 0x0142, 0x00b4, 0x00b5, 0x00b6, 0x00b7, 0x00b8, 0x0105, 0x015f, 0x00bb, 0x013d, 0x02dd, 0x013e, 0x017c,
  0x0154, 0x00c1, 0x00c2, 0x0102, 0x00c4, 0x0139, 0x0106, 0x00c7, 0x010c, 0x00c9, 0x0118, 0x00cb, 0x011a, 0x00cd, 0x00ce, 0x010e,
  0x0110, 0x0143, 0x0147, 0x00d3, 0x00d4, 0x0150, 0x00d6, 0x00d7, 0x0158, 0x016e, 0x00da, 0x0170, 0x00dc, 0x00dd, 0x0162, 0x00df,
  0x0155, 0x00e1, 0x00e2, 0x0103, 0x00e4, 0x013a, 0x0107, 0x00e7, 0x010d, 0x00e9, 0x0119, 0x00eb, 0x011b, 0x00ed, 0x00ee, 0x010f,
  0x0111, 0x0144, 0x0148, 0x00f3, 0x00f4, 0x0151, 0x00f6, 0x00f7, 0x0159, 0x016f, 0x00fa, 0x0171, 0x00fc, 0x00fd, 0x0163, 0x02d9 };
/* the languages Windows writes in code page 1250, not 1252: a subtitle in one of them, not saved as Unicode, is read so */
static int cp1250_lang(const char *lang) {
  static const char *L[] = { "hun", "cze", "slo", "pol", "slv", "hrv", "bos", "bs", "srp", "rum", "alb", "sqi", "sq", NULL };
  char k[16]; if (!lang || !*lang) return 0; subs_lang_canon(lang, k, sizeof k);
  for (int i = 0; L[i]; i++) if (!strcmp(L[i], k)) return 1;
  return 0;
}
/* subtitle files come in whatever their authors saved: UTF-16 (with a BOM, or told by its zero bytes), UTF-8 (BOM or not),
   else the Windows code page most old SRT files use: 1250 for the Central European languages (cp1250 set: cp1250_lang), 1252
   otherwise. Line ends become "\n" whatever they were. malloc'd */
static char *to_text(const unsigned char *b, size_t n, int cp1250) {
  int be = -1; /* UTF-16: 0 little-endian, 1 big-endian */
  if (n >= 2 && b[0] == 0xff && b[1] == 0xfe) { be = 0; b += 2; n -= 2; }
  else if (n >= 2 && b[0] == 0xfe && b[1] == 0xff) { be = 1; b += 2; n -= 2; }
  else if (n >= 3 && b[0] == 0xef && b[1] == 0xbb && b[2] == 0xbf) { b += 3; n -= 3; }
  else if (n >= 4) { size_t lim = n < 512 ? n & ~(size_t)1 : 512, ze = 0, zo = 0; for (size_t i = 0; i < lim; i += 2) { ze += !b[i]; zo += !b[i + 1]; }
    if (zo * 3 > lim / 2 && ze * 10 < lim / 2) be = 0; else if (ze * 3 > lim / 2 && zo * 10 < lim / 2) be = 1; }
  char *out = malloc(n * 3 + 8); size_t o = 0;
  if (be >= 0) for (size_t i = 0; i + 1 < n; i += 2) { unsigned c = be ? (unsigned)(b[i] << 8 | b[i + 1]) : (unsigned)(b[i + 1] << 8 | b[i]);
      if (c >= 0xd800 && c < 0xdc00 && i + 3 < n) { unsigned d = be ? (unsigned)(b[i + 2] << 8 | b[i + 3]) : (unsigned)(b[i + 3] << 8 | b[i + 2]); if (d >= 0xdc00 && d < 0xe000) { c = 0x10000 + ((c - 0xd800) << 10) + (d - 0xdc00); i += 2; } }
      if (c) o += put_utf8(out + o, c); }
  else if (utf8_ok(b, n)) { for (size_t i = 0; i < n; i++) if (b[i]) out[o++] = (char)b[i]; }
  else for (size_t i = 0; i < n; i++) { unsigned c = b[i]; if (!c) continue; o += put_utf8(out + o, c < 0x80 ? c : cp1250 ? CP1250[c - 0x80] : c < 0xa0 ? CP1252[c - 0x80] : c); }
  out[o] = 0;
  size_t w = 0; for (size_t i = 0; i < o; i++) { if (out[i] == '\r') { out[w++] = '\n'; if (out[i + 1] == '\n') i++; } else out[w++] = out[i]; }
  out[w] = 0; return out;
}

/* ---- SubStation (.ass / .ssa): the [Events] Dialogue lines, override blocks dropped ---- */
static double ass_time(const char *s) { int h = 0, m = 0; double sec = 0; if (sscanf(s, " %d:%d:%lf", &h, &m, &sec) != 3) return -1; return h * 3600 + m * 60 + sec; }
static int parse_ass(const char *txt, Cue **out) {
  Cue *cues = NULL; int n = 0, cap = 0, inEv = 0, nf = 10, fs = 1, fe = 2, ft = 9;
  char *buf = strdup(txt);
  for (char *ln = buf, *nx; ln; ln = nx) {
    nx = strchr(ln, '\n'); if (nx) *nx++ = 0;
    while (*ln == ' ' || *ln == '\t') ln++;
    if (*ln == '[') { inEv = !strncasecmp(ln, "[events]", 8); continue; }
    if (!inEv) continue;
    if (!strncasecmp(ln, "Format:", 7)) { nf = 0; fs = fe = ft = -1; for (char *f = ln + 7, *c; f; f = c) { c = strchr(f, ','); if (c) *c++ = 0; while (*f == ' ') f++;
        if (!strncasecmp(f, "Start", 5)) fs = nf; else if (!strncasecmp(f, "End", 3)) fe = nf; else if (!strncasecmp(f, "Text", 4)) ft = nf; nf++; }
      if (fs < 0 || fe < 0 || ft != nf - 1) { nf = 10; fs = 1; fe = 2; ft = 9; } continue; }
    if (strncasecmp(ln, "Dialogue:", 9)) continue;
    char *f[32]; int k = 0; char *p = ln + 9; f[k++] = p;
    while (*p && k < nf && k < 32) { if (*p == ',') { *p = 0; f[k++] = p + 1; } p++; }
    if (k < nf) continue;
    double s = ass_time(f[fs]), e = ass_time(f[fe]); if (s < 0 || e <= s) continue;
    const char *t = f[ft]; char text[2048]; size_t o = 0; int drawing = 0;
    for (size_t i = 0; t[i] && o < sizeof text - 4;) {
      if (t[i] == '{') { const char *z = strchr(t + i, '}'); if (!z) break; for (const char *q = t + i; q < z; q++) if (q[0] == '\\' && q[1] == 'p' && q[2] >= '0' && q[2] <= '9') drawing = q[2] != '0'; i = (size_t)(z - t) + 1; continue; }
      if (t[i] == '\\' && (t[i + 1] == 'N' || t[i + 1] == 'n')) { while (o && text[o - 1] == ' ') o--; text[o++] = '\n'; i += 2; continue; }
      if (t[i] == '\\' && t[i + 1] == 'h') { text[o++] = ' '; i += 2; continue; }
      if (!drawing) text[o++] = t[i]; i++; } /* vector drawings are shapes, not words */
    text[o] = 0; char *a = text; while (*a == ' ' || *a == '\n') a++; size_t l = strlen(a); while (l && (a[l - 1] == ' ' || a[l - 1] == '\n')) a[--l] = 0;
    if (*a) cue_add(&cues, &n, &cap, s, e, a);
  }
  free(buf); if (n) qsort(cues, n, sizeof *cues, cmp_cue); *out = cues; return n;
}

/* blocks separated by blank lines; the line with "-->" times the text under it; tags and {..} are dropped */
int subs_parse(const char *txt, Cue **out) {
  *out = NULL; if (!txt) return 0; if (!strncmp(txt, "\xef\xbb\xbf", 3)) txt += 3;
  char *buf = to_text((const unsigned char *)txt, strlen(txt), 0); /* "\r\n" must not read as a blank line */
  if (strstr(buf, "[Events]") || strstr(buf, "[events]") || !strncmp(buf, "[Script Info]", 13)) { int k = parse_ass(buf, out); free(buf); return k; }
  Cue *cues = NULL; int n = 0, cap = 0;
  char *blk = buf;
  while (blk && *blk) {
    char *next = strstr(blk, "\n\n"); if (next) { *next = 0; next += 2; while (*next == '\n') next++; }
    char *arrow = strstr(blk, "-->");
    if (arrow) { char *ls = arrow; while (ls > blk && ls[-1] != '\n') ls--; double s, e; const char *p1, *p2; const char *a = ls; while (*a == ' ') a++;
      if (tc(a, &s, &p1)) { const char *b = arrow + 3; while (*b == ' ') b++; if (tc(b, &e, &p2)) {
        char *tl = strchr(arrow, '\n'); char text[2048] = ""; size_t o = 0;
        if (tl) { tl++; int tag = 0, brace = 0; for (const char *c = tl; *c && o < sizeof text - 1; c++) { if (*c == '<') { tag = 1; continue; } if (*c == '>') { tag = 0; continue; } if (*c == '{') { brace = 1; continue; } if (*c == '}') { brace = 0; continue; } if (!tag && !brace) text[o++] = *c; } text[o] = 0; }
        char *t0 = text; while (*t0 == ' ' || *t0 == '\n') t0++; char *t1 = t0 + strlen(t0); while (t1 > t0 && (t1[-1] == ' ' || t1[-1] == '\n')) *--t1 = 0;
        if (*t0 && e > s) cue_add(&cues, &n, &cap, s, e, t0); } } }
    blk = next;
  }
  free(buf); if (n) qsort(cues, n, sizeof *cues, cmp_cue); *out = cues; return n;
}
void subs_free(Cue *c, int n) { if (!c) return; for (int i = 0; i < n; i++) free(c[i].t); free(c); }

/* some subtitle hosts hand out the .gz file itself (no Content-Encoding): unpacked here, 16 MB at most; then the
   text's own encoding (UTF-16 has zero bytes, so it is decoded before anything reads it as a C string) */
int subs_parse_body(const unsigned char *body, size_t len, const char *lang, Cue **out) {
  *out = NULL; if (!body) return 0;
  unsigned char *buf = NULL; size_t n = 0;
  if (len >= 2 && body[0] == 0x1f && body[1] == 0x8b) {
    z_stream z; memset(&z, 0, sizeof z); if (inflateInit2(&z, 16 + MAX_WBITS) != Z_OK) return 0;
    size_t cap = len * 4 + 4096; buf = malloc(cap); int rc = Z_OK;
    z.next_in = (unsigned char *)body; z.avail_in = (uInt)len;
    while (rc == Z_OK) {
      if (cap - n < 4096) { if (cap >= 16u << 20) break; cap *= 2; buf = realloc(buf, cap); }
      z.next_out = buf + n; z.avail_out = (uInt)(cap - n - 1); rc = inflate(&z, Z_NO_FLUSH); n = cap - 1 - z.avail_out;
    }
    inflateEnd(&z); if (rc != Z_STREAM_END && !n) { free(buf); return 0; }
    body = buf; len = n;
  }
  char *txt = to_text(body, len, cp1250_lang(lang)); free(buf);
  int k = subs_parse_any(txt, out); free(txt); return k;
}

/* ---- language codes ---- */
static const char *ALIAS[][2] = { { "en", "eng" }, { "ar", "ara" }, { "fr", "fre" }, { "fra", "fre" }, { "es", "spa" }, { "spl", "spa" }, { "de", "ger" }, { "deu", "ger" }, { "it", "ita" }, { "pt", "por" }, { "pob", "por" },
  { "ru", "rus" }, { "tr", "tur" }, { "pl", "pol" }, { "nl", "dut" }, { "nld", "dut" }, { "sv", "swe" }, { "he", "heb" }, { "iw", "heb" }, { "hi", "hin" }, { "ja", "jpn" }, { "ko", "kor" }, { "zh", "chi" }, { "zho", "chi" },
  { "ro", "rum" }, { "ron", "rum" }, { "el", "gre" }, { "ell", "gre" }, { "cs", "cze" }, { "ces", "cze" }, { "fa", "per" }, { "fas", "per" }, { "da", "dan" }, { "fi", "fin" }, { "no", "nor" }, { "nb", "nor" }, { "nob", "nor" },
  { "hu", "hun" }, { "bg", "bul" }, { "uk", "ukr" }, { "sr", "srp" }, { "hr", "hrv" }, { "sk", "slo" }, { "slk", "slo" }, { "sl", "slv" }, { "id", "ind" }, { "ms", "may" }, { "msa", "may" }, { "th", "tha" }, { "vi", "vie" }, { NULL, NULL } };
/* a track's language as a name (the player's chips and lists) */
static const char *LANGS[][2] = { { "en", "English" }, { "eng", "English" }, { "fr", "French" }, { "fre", "French" }, { "fra", "French" }, { "es", "Spanish" }, { "spa", "Spanish" }, { "de", "German" }, { "ger", "German" }, { "deu", "German" },
  { "it", "Italian" }, { "ita", "Italian" }, { "pt", "Portuguese" }, { "por", "Portuguese" }, { "ru", "Russian" }, { "rus", "Russian" }, { "ar", "Arabic" }, { "ara", "Arabic" }, { "tr", "Turkish" }, { "tur", "Turkish" },
  { "pl", "Polish" }, { "pol", "Polish" }, { "nl", "Dutch" }, { "dut", "Dutch" }, { "nld", "Dutch" }, { "sv", "Swedish" }, { "swe", "Swedish" }, { "ja", "Japanese" }, { "jpn", "Japanese" }, { "ko", "Korean" }, { "kor", "Korean" },
  { "zh", "Chinese" }, { "chi", "Chinese" }, { "zho", "Chinese" }, { "hi", "Hindi" }, { "hin", "Hindi" }, { "he", "Hebrew" }, { "heb", "Hebrew" }, { "el", "Greek" }, { "gre", "Greek" }, { "ell", "Greek" }, { "ro", "Romanian" }, { "rum", "Romanian" }, { "ron", "Romanian" },
  { "hu", "Hungarian" }, { "hun", "Hungarian" }, { "cs", "Czech" }, { "cze", "Czech" }, { "ces", "Czech" }, { "da", "Danish" }, { "dan", "Danish" }, { "fi", "Finnish" }, { "fin", "Finnish" }, { "no", "Norwegian" }, { "nor", "Norwegian" }, { "nb", "Norwegian" }, { "nob", "Norwegian" },
  { "uk", "Ukrainian" }, { "ukr", "Ukrainian" }, { "th", "Thai" }, { "tha", "Thai" }, { "vi", "Vietnamese" }, { "vie", "Vietnamese" }, { "id", "Indonesian" }, { "ind", "Indonesian" }, { "fa", "Persian" }, { "per", "Persian" }, { "fas", "Persian" }, { NULL, NULL } };
/* OpenSubtitles' own codes, ahead of the list above */
static const char *SUBNAMES[][2] = { { "spl", "Spanish (LA)" }, { "pob", "Portuguese (BR)" }, { "hrv", "Croatian" }, { "bul", "Bulgarian" }, { "bos", "Bosnian" }, { "srp", "Serbian" }, { "slo", "Slovak" }, { "slk", "Slovak" },
  { "slv", "Slovenian" }, { "est", "Estonian" }, { "lav", "Latvian" }, { "lit", "Lithuanian" }, { "alb", "Albanian" }, { "sqi", "Albanian" }, { "mac", "Macedonian" }, { "mkd", "Macedonian" }, { "ice", "Icelandic" },
  { "isl", "Icelandic" }, { "may", "Malay" }, { "msa", "Malay" }, { "cat", "Catalan" }, { "baq", "Basque" }, { "eus", "Basque" }, { "glg", "Galician" }, { "geo", "Georgian" }, { "kat", "Georgian" }, { "arm", "Armenian" },
  { "hye", "Armenian" }, { "ben", "Bengali" }, { "tam", "Tamil" }, { "tel", "Telugu" }, { "urd", "Urdu" }, { "mal", "Malayalam" }, { "sin", "Sinhala" }, { "tgl", "Tagalog" }, { "kur", "Kurdish" }, { "mon", "Mongolian" },
  { "ze", "Chinese (bilingual)" }, { "zht", "Chinese (traditional)" }, { "zhe", "Chinese (bilingual)" }, { "ell", "Greek" }, { "ron", "Romanian" }, { "ces", "Czech" }, { "nld", "Dutch" }, { NULL, NULL } };
static const char *lookup(const char *t[][2], const char *k) { for (int i = 0; t[i][0]; i++) if (!strcmp(t[i][0], k)) return t[i][1]; return NULL; }
static void base_code(const char *c, char *k, size_t cap) { size_t i = 0; for (; c && c[i] && c[i] != '-' && c[i] != '_' && i + 1 < cap; i++) k[i] = (char)(c[i] >= 'A' && c[i] <= 'Z' ? c[i] + 32 : c[i]); k[i] = 0; }
const char *subs_lang_canon(const char *code, char *out, size_t cap) { char k[16]; base_code(code, k, sizeof k); const char *a = lookup(ALIAS, k); snprintf(out, cap, "%s", a ? a : k); return out; }
int subs_lang_match(const char *a, const char *b) { if (!a || !b || !*a || !*b) return 0; char x[16], y[16]; return !strcmp(subs_lang_canon(a, x, sizeof x), subs_lang_canon(b, y, sizeof y)); }
const char *subs_lang_name(const char *code) { char k[16]; base_code(code, k, sizeof k); return *k ? lookup(LANGS, k) : NULL; }
const char *subs_sub_name(const char *code) { char k[16]; base_code(code, k, sizeof k); if (!*k) return NULL; const char *n = lookup(SUBNAMES, k); return n ? n : lookup(LANGS, k); }

/* ---- fetching on a thread of their own ----
   The shared request queue can sit behind six stream addons for 45 s each: a subtitle the viewer just picked, or the
   lists, must not wait there. Each fetch runs on its own short thread (files are also read there, off the drawing
   thread); subs_pump hands the answers over on the main thread. */
typedef struct FindQ FindQ;
typedef struct SubJob { struct SubJob *next; char *url, lang[24]; int list, status; unsigned char *body; size_t len; Cue *cues; int n;
  void (*cb)(int, Cue *, int, void *); void *ud;
  FindQ *fq; int src; SubEntry *got; int ngot; int os; } SubJob;   /* list: one addon's answer for a FindQ (src: which); os: the built-in OpenSubtitles list */
static pthread_mutex_t jmu = PTHREAD_MUTEX_INITIALIZER; static SubJob *jdone;
static void job_finish(SubJob *j) { pthread_mutex_lock(&jmu); j->next = jdone; jdone = j; pthread_mutex_unlock(&jmu); }
static void list_parse(SubJob *j);
static void *job_main(void *a) { plat_thread_background(5);
  SubJob *j = a; j->body = net_fetch_sync(j->url, &j->len, &j->status, 20000); /* one addon's limit: the others never wait for it */
  if (j->os) {} /* read on the main thread (on_list) */
  else if (j->list) list_parse(j);
  else if (j->body) { j->n = subs_parse_body(j->body, j->len, j->lang, &j->cues); free(j->body); j->body = NULL; }
  job_finish(j);
  return NULL;
}
static void job_start(SubJob *j) { pthread_t t; if (pthread_create(&t, NULL, job_main, j)) job_main(j); else pthread_detach(t); }
static void find_answer(SubJob *j);
static void on_list(int st, const unsigned char *b, size_t n, void *ud);
void subs_pump(void) {
  pthread_mutex_lock(&jmu); SubJob *l = jdone; jdone = NULL; pthread_mutex_unlock(&jmu);
  SubJob *rev = NULL; while (l) { SubJob *nx = l->next; l->next = rev; rev = l; l = nx; } /* in the order they finished */
  while (rev) { SubJob *j = rev; rev = j->next;
    if (j->os) on_list(j->status, j->body ? j->body : (const unsigned char *)"", j->len, j->ud);
    else if (j->list) find_answer(j);
    else j->cb(j->status, j->cues, j->n, j->ud);
    free(j->body); free(j->url); free(j->got); free(j); }
}
void subs_load(const char *url, const char *lang, void (*cb)(int status, Cue *cues, int n, void *ud), void *ud) {
  SubJob *j = calloc(1, sizeof *j); j->url = strdup(url); snprintf(j->lang, sizeof j->lang, "%s", lang ? lang : ""); j->cb = cb; j->ud = ud; job_start(j);
}

/* ---- the OpenSubtitles hash: the size plus the 64-bit little-endian words of the first and the last 64 KB ---- */
int subs_os_hash(const char *url, double *size, char out[17]) {
  out[0] = 0; NetRange *r = net_range_new(); if (!r) return 0; unsigned char *a = NULL, *b = NULL; long ga = 0, gb = 0; long long tot = -1; int st, ok = 0;
  if (net_range_get(r, url, 0, 65536, &a, &ga, &tot, &st, 8000) == NR_OK && ga == 65536) {
    if (tot <= 0 && *size > 0) tot = (long long)*size;
    if (tot >= 131072 && net_range_get(r, url, tot - 65536, 65536, &b, &gb, NULL, &st, 8000) == NR_OK && gb == 65536) {
      unsigned long long h = (unsigned long long)tot;
      for (int k = 0; k < 65536; k += 8) { unsigned long long x = 0, y = 0; for (int q = 7; q >= 0; q--) { x = x << 8 | a[k + q]; y = y << 8 | b[k + q]; } h += x + y; }
      snprintf(out, 17, "%016llx", h); *size = (double)tot; ok = 1; } }
  free(a); free(b); net_range_free(r); return ok;
}

/* what an entry says of the file it was made for: its name, and whether the video's hash found it ("m": "h" on
   OpenSubtitles' answers; other addons' own words for it) */
void subs_entry_facts(SubEntry *e, const struct cJSON *s) {
  const char *rel = js_str(s, "subtitleFileName", js_str(s, "releaseName", js_str(s, "release", ""))); snprintf(e->rel, sizeof e->rel, "%s", rel);
  const char *m = js_str(s, "m", ""); const cJSON *hm = cJSON_GetObjectItemCaseSensitive(s, "hashMatch"); const char *mb = js_str(s, "matchedBy", "");
  e->hm = !strcmp(m, "h") || cJSON_IsTrue(hm) || (cJSON_IsNumber(hm) && hm->valuedouble > 0) || strstr(mb, "hash") != NULL;
  const cJSON *fm = cJSON_GetObjectItemCaseSensitive(s, "fpsMilli"); double fp = cJSON_IsNumber(fm) ? fm->valuedouble : cJSON_IsString(fm) ? atof(fm->valuestring) : 0;
  e->fps = fp > 10000 && fp < 61000 ? fp / 1000 : 0;
}
/* the hash worked out for the stream last asked about (the reference Auto-sync uses asks again with it) */
static pthread_mutex_t hmu = PTHREAD_MUTEX_INITIALIZER; static char hUrl[4096], hHash[24]; static double hSize;
static void hash_keep(const char *url, const char *h, double size) { pthread_mutex_lock(&hmu); snprintf(hUrl, sizeof hUrl, "%s", url); snprintf(hHash, sizeof hHash, "%s", h); hSize = size; pthread_mutex_unlock(&hmu); }
int subs_hash_of(const char *url, char out[24], double *size) {
  pthread_mutex_lock(&hmu); int ok = hHash[0] && !strcmp(url, hUrl); if (ok) { snprintf(out, 24, "%s", hHash); if (size) *size = hSize; } pthread_mutex_unlock(&hmu); return ok;
}

/* ---- the built-in OpenSubtitles list: asked by id alone, English first, then by name, 12 per language ---- */
typedef struct { void (*cb)(SubEntry *, int, int, void *); void *ud; } ListReq;
static int cmp_os(const void *a, const void *b) { const SubEntry *x = a, *y = b; int xe = !strcmp(x->lang, "eng"), ye = !strcmp(y->lang, "eng"); if (xe != ye) return ye - xe; int c = strcmp(x->name, y->name); return c ? c : x->n - y->n; }
static void on_list(int st, const unsigned char *b, size_t n, void *ud) {
  (void)n; ListReq *r = ud; cJSON *j = st == 200 ? net_json(b) : NULL; const cJSON *arr = js_arr(j, "subtitles"), *s;
  SubEntry *out = calloc(256, sizeof *out); int no = 0; char langs[64][8]; int per[64], nl = 0;
  if (arr) cJSON_ArrayForEach(s, arr) { if (no >= 256) break; const char *url = js_str(s, "url", NULL); char lang[8]; snprintf(lang, sizeof lang, "%s", js_str(s, "lang", "")); for (char *p = lang; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32; if (!*lang || !url || strlen(url) >= sizeof out->url) continue;
    int li = -1; for (int i = 0; i < nl; i++) if (!strcmp(langs[i], lang)) li = i; if (li < 0 && nl < 64) { li = nl++; snprintf(langs[li], 8, "%s", lang); per[li] = 0; } if (li < 0) continue;
    if (++per[li] > 12) continue; SubEntry *e = &out[no++]; snprintf(e->lang, sizeof e->lang, "%s", lang); const char *nm = subs_sub_name(lang);
    if (nm) snprintf(e->name, sizeof e->name, "%s", nm); else { snprintf(e->name, sizeof e->name, "%s", lang); for (char *p = e->name; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32; }
    snprintf(e->url, sizeof e->url, "%s", url); snprintf(e->addon, sizeof e->addon, "OpenSubtitles"); e->from = SUB_FALLBACK; e->order = 999; e->n = per[li]; subs_entry_facts(e, s); }
  if (no) qsort(out, no, sizeof *out, cmp_os);
  r->cb(out, no, j != NULL, r->ud); net_json_free(j); free(r);
}
void subs_list(const char *type, const char *vid, void (*cb)(SubEntry *, int, int, void *), void *ud) {
  char t[96], enc[400], url[600]; addon_urlenc(type, t, sizeof t); addon_urlenc(vid, enc, sizeof enc);
  snprintf(url, sizeof url, "https://opensubtitles-v3.strem.io/subtitles/%s/%s.json", t, enc);
  ListReq *r = malloc(sizeof *r); r->cb = cb; r->ud = ud; SubJob *j = calloc(1, sizeof *j); j->url = strdup(url); j->list = 1; j->os = 1; j->ud = r; job_start(j);
}

/* ---- the addons' lists: every installed subtitles addon for this video, asked with the file's hash, size and name
   when known (/subtitles/{type}/{id}/videoHash=..&videoSize=..&filename=...json); the subtitles the stream carries first.
   The built-in list above is never mixed in. ---- */
typedef struct { char name[64], base[512]; int from, order; } FindSrc;
struct FindQ { unsigned tok; SubQuery q; FindSrc src[16]; int nsrc, pending, nok; SubEntry *all; int n, cap; void (*cb)(SubEntry *, int, int, int, void *); void *ud; };
static void list_parse(SubJob *j) {
  cJSON *js = j->body && j->status == 200 ? cJSON_ParseWithLength((const char *)j->body, j->len) : NULL; const cJSON *arr = js_arr(js, "subtitles"), *s;
  if (j->body && j->status == 200 && !js) fprintf(stderr, "[subs] %s: the answer is not JSON\n", j->fq->src[j->src].name);
  if (js) j->status = 1000; /* read */
  int n = arr ? cJSON_GetArraySize(arr) : 0; if (n > 400) n = 400; j->got = calloc(n ? n : 1, sizeof *j->got);
  char langs[64][16]; int per[64], nl = 0; const FindSrc *fs = &j->fq->src[j->src];
  if (arr) cJSON_ArrayForEach(s, arr) { if (j->ngot >= n) break; const char *url = js_str(s, "url", NULL); if (!url || !*url || strlen(url) >= 1024) continue;
    char lang[24]; snprintf(lang, sizeof lang, "%s", js_str(s, "lang", "und")); for (char *p = lang; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32; if (!*lang) snprintf(lang, sizeof lang, "und");
    char key[16]; subs_lang_canon(lang, key, sizeof key);
    int li = -1; for (int i = 0; i < nl; i++) if (!strcmp(langs[i], key)) li = i; if (li < 0 && nl < 64) { li = nl++; snprintf(langs[li], sizeof langs[li], "%s", key); per[li] = 0; } if (li < 0) continue;
    if (++per[li] > 12) continue; /* 12 per language from one source: enough to choose from */
    SubEntry *e = &j->got[j->ngot++]; snprintf(e->lang, sizeof e->lang, "%s", lang); snprintf(e->url, sizeof e->url, "%s", url); snprintf(e->label, sizeof e->label, "%s", js_str(s, "label", ""));
    snprintf(e->addon, sizeof e->addon, "%s", fs->name); e->from = fs->from; e->order = fs->order; e->n = per[li]; subs_entry_facts(e, s); }
  cJSON_Delete(js); free(j->body); j->body = NULL;
}
static void entry_name(SubEntry *e) {
  const char *nm = subs_sub_name(e->lang); if (nm) snprintf(e->name, sizeof e->name, "%s", nm);
  else { snprintf(e->name, sizeof e->name, "%s", e->lang); size_t l = strlen(e->name); /* "a valid ISO 639-2 code, else the text itself": a short code in capitals, words as they are */
    if (l <= 3) { for (char *p = e->name; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32; } else if (e->name[0] >= 'a' && e->name[0] <= 'z') e->name[0] -= 32; }
}
static int cmp_entry(const void *a, const void *b) { const SubEntry *x = a, *y = b; if (x->from != y->from) return x->from - y->from; if (x->order != y->order) return x->order - y->order; return x->n - y->n; }
static void find_deliver(FindQ *f) {
  SubEntry *out = malloc((f->n ? f->n : 1) * sizeof *out); memcpy(out, f->all, f->n * sizeof *out);
  f->cb(out, f->n, f->pending <= 0, f->nok, f->ud);
  if (f->pending <= 0) { free(f->all); free(f); }
}
static void find_add(FindQ *f, const SubEntry *e, int n) {
  for (int i = 0; i < n; i++) { int dup = 0; for (int k = 0; k < f->n && !dup; k++) dup = !strcmp(f->all[k].url, e[i].url); if (dup) continue;
    if (f->n == f->cap) { f->cap = f->cap ? f->cap * 2 : 64; f->all = realloc(f->all, f->cap * sizeof *f->all); }
    f->all[f->n] = e[i]; entry_name(&f->all[f->n]); f->n++; }
  qsort(f->all, f->n, sizeof *f->all, cmp_entry);
}
static void find_answer(SubJob *j) {
  FindQ *f = j->fq; f->pending--; if (j->status == 1000) f->nok++;
  if (j->status != 1000) fprintf(stderr, "[subs] %s: %s\n", f->src[j->src].name, j->status > 0 ? "HTTP error" : "no answer");
  else fprintf(stderr, "[subs] %s: %d subtitles\n", f->src[j->src].name, j->ngot);
  find_add(f, j->got, j->ngot); find_deliver(f);
}
/* the hash (when the file can be read in ranges), then one request per source */
static void *find_main(void *a) { plat_thread_background(5);
  FindQ *f = a; SubQuery *q = &f->q;
  if (!q->hash[0] && q->hashable) { double sz = q->size; char h[17]; if (subs_os_hash(q->url, &sz, h)) { snprintf(q->hash, sizeof q->hash, "%s", h); q->size = sz; } }
  if (q->hash[0]) hash_keep(q->url, q->hash, q->size);
  char ex[600] = "", n[32]; if (q->hash[0]) addon_extra_add(ex, sizeof ex, "videoHash", q->hash);
  if (q->size > 0) { snprintf(n, sizeof n, "%.0f", q->size); addon_extra_add(ex, sizeof ex, "videoSize", n); }
  if (q->filename[0]) addon_extra_add(ex, sizeof ex, "filename", q->filename);
  fprintf(stderr, "[subs] asking %d source(s) for %s %s (%s)\n", f->nsrc, q->type, q->vid, ex[0] ? ex : "no file facts");
  int ns = f->nsrc; /* f may be gone once the last request is out */
  for (int i = 0; i < ns; i++) { SubJob *j = calloc(1, sizeof *j); char url[2400]; addon_url(url, sizeof url, f->src[i].base, "subtitles", q->type, q->vid, ex);
    j->url = strdup(url); j->list = 1; j->fq = f; j->src = i; job_start(j); }
  return NULL;
}
int subs_addons_for(const char *type, const char *vid) { return addon_count_subs(type, vid); }
void subs_find(const SubQuery *q, const SubEntry *own, int nown, void (*cb)(SubEntry *, int, int, int, void *), void *ud) {
  FindQ *f = calloc(1, sizeof *f); f->q = *q; f->cb = cb; f->ud = ud;
  cJSON *a = sb_addons(); const cJSON *ad; int order = 0;
  if (a) cJSON_ArrayForEach(ad, a) { order++; if (f->nsrc >= 16 || !addon_serves_subs(ad, q->type, q->vid)) continue; FindSrc *s = &f->src[f->nsrc++];
    snprintf(s->name, sizeof s->name, "%s", addon_name(ad)); addon_base(ad, s->base, sizeof s->base); s->from = SUB_ADDON; s->order = order; }
  if (nown) find_add(f, own, nown);
  f->pending = f->nsrc;
  if (!f->nsrc) { find_deliver(f); return; } /* only the stream's own: final at once */
  if (nown) { SubEntry *out = malloc(f->n * sizeof *out); memcpy(out, f->all, f->n * sizeof *out); cb(out, f->n, 0, 0, ud); } /* the stream's own at once */
  pthread_t t; if (pthread_create(&t, NULL, find_main, f)) find_main(f); else pthread_detach(t);
}

/* ---- YouTube timed text (trailer captions) ------------------------------------------------------ */
/* the entities these files use; &amp; first, so the classic format's doubly escaped "&amp;#39;" comes out whole */
static void tt_text(const char *s, size_t n, int stripTags, char *out, size_t cap) {
  char *raw = malloc(n + 1); size_t o = 0; int tag = 0;
  for (size_t i = 0; i < n; i++) { char c = s[i]; if (stripTags) { if (c == '<') { tag = 1; continue; } if (tag) { if (c == '>') tag = 0; continue; } } raw[o++] = c; }
  raw[o] = 0;
  static const char *ent[][2] = { { "&amp;", "&" }, { "&lt;", "<" }, { "&gt;", ">" }, { "&quot;", "\"" }, { "&#39;", "'" } };
  for (int k = 0; k < 5; k++) { size_t el = strlen(ent[k][0]); char *w = raw; for (char *r = raw; *r;) { if (!strncmp(r, ent[k][0], el)) { *w++ = ent[k][1][0]; r += el; } else *w++ = *r++; } *w = 0; }
  /* line breaks become one space; trimmed */
  o = 0; int nl = 0; for (char *r = raw; *r && o < cap - 1; r++) { if (*r == '\n' || *r == '\r') { if (!nl) out[o++] = ' '; nl = 1; continue; } nl = 0; out[o++] = *r; }
  out[o] = 0; free(raw);
  char *a = out; while (*a == ' ' || *a == '\t') a++; if (a != out) memmove(out, a, strlen(a) + 1);
  size_t l = strlen(out); while (l && (out[l - 1] == ' ' || out[l - 1] == '\t')) out[--l] = 0;
}
static const char *ws(const char *p) { while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++; return p; }
/* attr="..." right here (after white space); the value's span */
static int tt_attr(const char **pp, const char *name, const char **v, size_t *vl) {
  const char *p = *pp, *q = ws(p); if (q == p) return 0; size_t nl = strlen(name);
  if (strncmp(q, name, nl) || q[nl] != '=' || q[nl + 1] != '"') return 0;
  q += nl + 2; const char *e = strchr(q, '"'); if (!e) return 0; *v = q; *vl = (size_t)(e - q); *pp = e + 1; return 1;
}
/* srv3 (<p t="ms" d="ms">, words in <s> tags) first, then the classic <text start="s" dur="s"> */
int subs_parse_timedtext(const char *x, Cue **out) {
  *out = NULL; if (!x) return 0;
  Cue *cues = NULL; int n = 0, cap = 0; char text[2048];
  for (int pass = 0; pass < 2 && !n; pass++) {
    const char *open = pass ? "<text" : "<p", *close = pass ? "</text>" : "</p>", *a1 = pass ? "start" : "t", *a2 = pass ? "dur" : "d";
    size_t ol = strlen(open);
    for (const char *p = strstr(x, open); p; p = strstr(p, open)) {
      const char *q = p + ol, *v1, *v2; size_t l1, l2; p += ol;
      if (!tt_attr(&q, a1, &v1, &l1) || !tt_attr(&q, a2, &v2, &l2)) continue;
      const char *gt = strchr(q, '>'); if (!gt) break;
      const char *body = gt + 1, *end = strstr(body, close); if (!end) break;
      char b1[32], b2[32]; snprintf(b1, sizeof b1, "%.*s", (int)(l1 < 31 ? l1 : 31), v1); snprintf(b2, sizeof b2, "%.*s", (int)(l2 < 31 ? l2 : 31), v2);
      double s = atof(b1), d = atof(b2); if (!pass) { s /= 1000; d /= 1000; }
      tt_text(body, (size_t)(end - body), !pass, text, sizeof text);
      if (pass) { char *w = text; int tag = 0; for (char *r = text; *r; r++) { if (*r == '<') tag = 1; else if (tag) { if (*r == '>') tag = 0; } else *w++ = *r; } *w = 0; } /* the classic format escapes its markup: dropped once decoded (no markup on screen) */
      if (*text) cue_add(&cues, &n, &cap, s, s + d, text);
      p = end + strlen(close);
    }
  }
  if (n) qsort(cues, n, sizeof *cues, cmp_cue); *out = cues; return n;
}
/* whatever a subtitle URL answered: timed text when it looks like it (and parses), else SRT / WebVTT */
int subs_parse_any(const char *txt, Cue **out) {
  *out = NULL; if (!txt) return 0;
  if (strstr(txt, "<timedtext") || strstr(txt, "<transcript") || strstr(txt, "<text ") || strstr(txt, "<p ")) { int n = subs_parse_timedtext(txt, out); if (n) return n; }
  return subs_parse(txt, out);
}
