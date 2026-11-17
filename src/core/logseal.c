/* Log text scrubbing and sealing, the same rules as the web app's helper so a bundle from either opens with
   the same tool. The patterns are those regular expressions written out by hand, pass after pass in the same
   order, with their exact matching rules (greedy runs, word boundaries on ASCII letters, digits and '_',
   lengths counted in UTF-16 units, JavaScript's whitespace set). */
#include "core/logseal.h"
#include "sbcrypto.h"
#include "uecc/uECC.h"
#include <zlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>

typedef struct { char *p; size_t n, cap; } Buf;
static void b_put(Buf *b, const char *s, size_t n) {
  if (b->n + n + 1 > b->cap) { b->cap = (b->n + n + 1) * 2 + 64; b->p = realloc(b->p, b->cap); }
  memcpy(b->p + b->n, s, n); b->n += n; b->p[b->n] = 0;
}
static void b_str(Buf *b, const char *s) { b_put(b, s, strlen(s)); }
static char *b_done(Buf *b) { if (!b->p) { b->p = malloc(1); b->p[0] = 0; } return b->p; }

static int is_alpha(int c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
static int is_digit(int c) { return c >= '0' && c <= '9'; }
static int is_alnum(int c) { return is_alpha(c) || is_digit(c); }
static int is_word(int c) { return is_alnum(c) || c == '_'; }
static int is_hex(int c) { return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
/* the byte length of the UTF-8 character at s (1 for a stray byte) and its code point */
static int u8(const unsigned char *s, uint32_t *cp) {
  if (s[0] < 0x80) { *cp = s[0]; return 1; }
  if ((s[0] & 0xe0) == 0xc0 && (s[1] & 0xc0) == 0x80) { *cp = ((s[0] & 0x1fu) << 6) | (s[1] & 0x3f); return 2; }
  if ((s[0] & 0xf0) == 0xe0 && (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80) { *cp = ((s[0] & 0x0fu) << 12) | ((s[1] & 0x3fu) << 6) | (s[2] & 0x3f); return 3; }
  if ((s[0] & 0xf8) == 0xf0 && (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80 && (s[3] & 0xc0) == 0x80) { *cp = ((s[0] & 0x07u) << 18) | ((s[1] & 0x3fu) << 12) | ((s[2] & 0x3fu) << 6) | (s[3] & 0x3f); return 4; }
  *cp = s[0]; return 1;
}
static int js_space(uint32_t c) {
  return c == ' ' || (c >= 9 && c <= 13) || c == 0xa0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000 || c == 0xfeff;
}
static int units(uint32_t cp) { return cp > 0xffff ? 2 : 1; } /* a JavaScript string's length counts UTF-16 units */
/* a character that may sit in a link: [^\s"'<>] (plus extra excluded ASCII bytes) */
static int link_ch(const unsigned char *s, const char *extra, int *n) {
  uint32_t cp; *n = u8(s, &cp);
  if (!s[0] || js_space(cp) || cp == '"' || cp == '\'' || cp == '<' || cp == '>') return 0;
  return !(cp < 128 && extra && strchr(extra, (int)cp));
}
static size_t scheme_len(const char *s) { if (!strncmp(s, "https://", 8)) return 8; if (!strncmp(s, "http://", 7)) return 7; return 0; }

/* [A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}: the end of a match starting at s, or 0 */
static int local_ch(int c) { return is_alnum(c) || c == '.' || c == '_' || c == '%' || c == '+' || c == '-'; }
static int dom_ch(int c) { return is_alnum(c) || c == '.' || c == '-'; }
static size_t email_at(const char *s) {
  size_t i = 0; while (local_ch((unsigned char)s[i])) i++;
  if (!i || s[i] != '@') return 0;
  const char *d = s + i + 1; size_t D = 0; while (dom_ch((unsigned char)d[D])) D++;
  for (size_t e = D ? D - 1 : 0; e >= 1 && e < D; e--) {
    if (d[e] != '.') continue;
    size_t L = 0; while (is_alpha((unsigned char)d[e + 1 + L])) L++;
    if (L >= 2) return i + 1 + e + 1 + L;
  }
  return 0;
}
static char *pass_email(const char *s, const char *rep) {
  Buf b = { 0 }; size_t i = 0, n = strlen(s);
  while (i < n) {
    if (local_ch((unsigned char)s[i])) { size_t m = email_at(s + i); if (m) { b_str(&b, rep); i += m; continue; }
      size_t j = i; while (local_ch((unsigned char)s[j])) j++; b_put(&b, s + i, j - i); i = j; continue; } /* every later start in this run meets the same '@' (or none): no match either */
    b_put(&b, s + i, 1); i++;
  }
  return b_done(&b);
}

/* ---- the app log line (kept on the TV) ---- */
char *logscrub_line(const char *line) {
  size_t n = strlen(line); if (n > 2000) { n = 2000; while (n && ((unsigned char)line[n] & 0xc0) == 0x80) n--; } /* .slice(0, 2000) */
  char *s = malloc(n + 1); memcpy(s, line, n); s[n] = 0;
  /* (https?:\/\/[^\s"'<>?#]+)[?#][^\s"'<>]* -> $1?… */
  { Buf b = { 0 }; size_t i = 0;
    while (i < n) { size_t sl = scheme_len(s + i), j = i + sl; int k, any = 0;
      if (sl) { while (link_ch((unsigned char *)s + j, "?#", &k)) { j += k; any = 1; } }
      if (sl && any && (s[j] == '?' || s[j] == '#')) { b_put(&b, s + i, j - i); b_str(&b, "?\xe2\x80\xa6"); j++; while (link_ch((unsigned char *)s + j, NULL, &k)) j += k; i = j; continue; }
      b_put(&b, s + i, 1); i++; }
    free(s); s = b_done(&b); n = strlen(s); }
  /* [A-Za-z0-9_\-]{24,} -> … */
  { Buf b = { 0 }; size_t i = 0;
    while (i < n) { size_t j = i; while (is_word((unsigned char)s[j]) || s[j] == '-') j++;
      if (j - i >= 24) { b_str(&b, "\xe2\x80\xa6"); i = j; continue; }
      if (j > i) { b_put(&b, s + i, j - i); i = j; continue; }
      b_put(&b, s + i, 1); i++; }
    free(s); s = b_done(&b); }
  { char *t = pass_email(s, "<email>"); free(s); s = t; }
  for (char *p = s; *p; p++) { unsigned char c = (unsigned char)*p; if (c <= 8 || (c >= 11 && c <= 31)) *p = ' '; }
  return s;
}

/* ---- the shared bundle ---- */
static const char *KW[] = { "authKey", "auth_key", "apikey", "api_key", "token", "access_token", "refresh_token", "password", "passwd", "secret", "bearer" };
static int g2_ch(const unsigned char *s, int *n) { uint32_t cp; *n = u8(s, &cp); return s[0] && (cp == '"' || cp == '\'' || cp == ':' || cp == '=' || js_space(cp)); }
static int g3_ch(const unsigned char *s, int *n, int *u) { uint32_t cp; *n = u8(s, &cp); *u = units(cp); return s[0] && !js_space(cp) && !strchr("\"'&,;<>", (int)(cp < 128 ? cp : 1)); }
/* (authKey|…|bearer)(["'\s:=]+)([^\s"'&,;<>]{4,}) gi -> $1$2[redacted] */
static char *pass_keys(const char *s) {
  Buf b = { 0 }; size_t i = 0, n = strlen(s);
  while (i < n) {
    int hit = 0;
    for (size_t w = 0; w < sizeof KW / sizeof KW[0] && !hit; w++) {
      size_t kl = strlen(KW[w]); if (strncasecmp(s + i, KW[w], kl)) continue;
      size_t ends[256]; int ne = 0; size_t j = i + kl; int k;
      while (ne < 256 && g2_ch((const unsigned char *)s + j, &k)) { j += k; ends[ne++] = j; }
      for (int e = ne - 1; e >= 0 && !hit; e--) { /* the greedy run first, then shorter */
        size_t g = ends[e]; int u, cnt = 0; while (g3_ch((const unsigned char *)s + g, &k, &u)) { g += k; cnt += u; }
        if (cnt >= 4) { b_put(&b, s + i, ends[e] - i); b_str(&b, "[redacted]"); i = g; hit = 1; }
      }
    }
    if (!hit) { b_put(&b, s + i, 1); i++; }
  }
  return b_done(&b);
}
static int seg_bad(const char *s, size_t n) {
  size_t u = 0; for (size_t i = 0; i < n;) { uint32_t cp; int k = u8((const unsigned char *)s + i, &cp); u += units(cp); i += k; }
  if (u >= 20) return 1;
  for (size_t i = 0; i < n; i++) if (s[i] == '=' || s[i] == '|' || s[i] == '%') return 1;
  if (n < 16) return 0;
  for (size_t i = 0; i < n; i++) if (!(is_alnum((unsigned char)s[i]) || s[i] == '+' || s[i] == '_' || s[i] == '-')) return 0;
  return 1;
}
/* a link keeps its scheme, host and plain path; credentials, query, fragment and token-like parts go */
static void link_clean(Buf *b, const char *u, size_t n) {
  size_t sl = scheme_len(u), h = sl, j = sl;
  while (j < n && u[j] != '@' && u[j] != '/') j++;
  if (j < n && u[j] == '@' && j + 1 < n && u[j + 1] != '/' && u[j + 1] != '?' && u[j + 1] != '#') h = j + 1; /* (?:[^@\/]*@)? when a host follows */
  size_t he = h; while (he < n && u[he] != '/' && u[he] != '?' && u[he] != '#') he++;
  if (he == h) { b_str(b, "[link]"); return; }
  size_t pe = he; while (pe < n && u[pe] != '?' && u[pe] != '#') pe++;
  b_put(b, u, sl); b_put(b, u + h, he - h);
  if (pe == he) return;
  for (size_t a = he;;) { /* split('/') of the path: its first part is the empty one before the leading '/' */
    size_t z = a; while (z < pe && u[z] != '/') z++;
    if (seg_bad(u + a, z - a)) b_str(b, "[\xe2\x80\xa6]"); else b_put(b, u + a, z - a);
    if (z >= pe) break; b_put(b, "/", 1); a = z + 1;
  }
}
static char *pass_links(const char *s) {
  Buf b = { 0 }; size_t i = 0, n = strlen(s);
  while (i < n) { size_t sl = scheme_len(s + i), j = i + sl; int k;
    if (sl && link_ch((const unsigned char *)s + j, NULL, &k)) { while (link_ch((const unsigned char *)s + j, NULL, &k)) j += k; link_clean(&b, s + i, j - i); i = j; continue; }
    b_put(&b, s + i, 1); i++; }
  return b_done(&b);
}
static int wb(const char *s, size_t i, size_t n) { int a = i > 0 && is_word((unsigned char)s[i - 1]), c = i < n && is_word((unsigned char)s[i]); return a != c; } /* \b */
/* \b(?:\d{1,3}\.){3}\d{1,3}\b -> [ip] */
static size_t ip_at(const char *s, size_t i, size_t n) {
  if (!wb(s, i, n)) return 0;
  size_t j = i;
  for (int g = 0; g < 4; g++) { size_t d = j; while (is_digit((unsigned char)s[j])) j++;
    if (j == d || j - d > 3) return 0;
    if (g < 3) { if (s[j] != '.') return 0; j++; } }
  return wb(s, j, n) ? j - i : 0;
}
static char *pass_ip(const char *s) {
  Buf b = { 0 }; size_t i = 0, n = strlen(s);
  while (i < n) { size_t m = is_digit((unsigned char)s[i]) ? ip_at(s, i, n) : 0; if (m) { b_str(&b, "[ip]"); i += m; continue; } b_put(&b, s + i, 1); i++; }
  return b_done(&b);
}
/* \b[0-9a-fA-F]{40}\b -> the first 8 and … */
static char *pass_hash(const char *s) {
  Buf b = { 0 }; size_t i = 0, n = strlen(s);
  while (i < n) {
    if (is_hex((unsigned char)s[i]) && wb(s, i, n) && i + 40 <= n) { size_t j = i; while (j < i + 40 && is_hex((unsigned char)s[j])) j++;
      if (j == i + 40 && wb(s, j, n)) { b_put(&b, s + i, 8); b_str(&b, "\xe2\x80\xa6"); i = j; continue; } }
    b_put(&b, s + i, 1); i++; }
  return b_done(&b);
}
/* \b(?=[A-Za-z0-9+_-]*\d)(?=[A-Za-z0-9+_-]*[A-Za-z])[A-Za-z0-9+_-]{24,}={0,2} -> [redacted] */
static int tok_ch(int c) { return is_alnum(c) || c == '+' || c == '_' || c == '-'; }
static char *pass_tokens(const char *s) {
  Buf b = { 0 }; size_t i = 0, n = strlen(s);
  while (i < n) {
    if (tok_ch((unsigned char)s[i]) && wb(s, i, n)) { size_t j = i; int dg = 0, al = 0;
      while (tok_ch((unsigned char)s[j])) { dg |= is_digit((unsigned char)s[j]); al |= is_alpha((unsigned char)s[j]); j++; }
      if (j - i >= 24 && dg && al) { if (s[j] == '=') j++; if (s[j] == '=') j++; b_str(&b, "[redacted]"); i = j; continue; } }
    b_put(&b, s + i, 1); i++; }
  return b_done(&b);
}
/* a user's own folder in a path (the Mac preview, a PC, a Linux box): its name goes, the rest stays.
   /Users/<name>, /home/<name>, C:\Users\<name> (either slash) -> [user]; /private/tmp/<dir>, /private/var/folders/<a>/<b> -> [tmp] */
static char *pass_paths(const char *s) {
  static const struct { const char *pre; int segs; const char *rep; } P[] = { { "/USERS/", 1, "[user]" }, { "/HOME/", 1, "[user]" }, { ":\\USERS\\", 1, "[user]" }, { ":/USERS/", 1, "[user]" }, { "/PRIVATE/TMP/", 1, "[tmp]" }, { "/PRIVATE/VAR/FOLDERS/", 2, "[tmp]" }, { "/CONTAINERS/DATA/APPLICATION/", 1, "[app]" }, { "/CONTAINERS/BUNDLE/APPLICATION/", 1, "[app]" }, { "/CONTAINERS/SHARED/APPGROUP/", 1, "[app]" } }; /* (iOS: a per-install folder id) (in capitals: matched without case, and the binary never holds a path-looking "/Users/" or "/private/tmp/" the privacy gates flag; the text kept is the log's own) */
  Buf b = { 0 }; size_t i = 0, n = strlen(s);
  while (i < n) { int hit = 0;
    for (size_t k = 0; k < sizeof P / sizeof P[0] && !hit; k++) { size_t pl = strlen(P[k].pre); if (strncasecmp(s + i, P[k].pre, pl)) continue;
      size_t j = i + pl; for (int g = 0; g < P[k].segs; g++) { size_t a = j; int win = P[k].pre[0] == ':'; /* (a Windows folder name may hold spaces: up to the next separator) */ while (j < n && s[j] != '/' && s[j] != '\\' && (win || s[j] != ' ') && s[j] != '"' && s[j] != '\'' && s[j] != '\n') j++; if (j == a) break; if (g + 1 < P[k].segs && j < n && (s[j] == '/' || s[j] == '\\')) j++; }
      if (j > i + pl) { b_put(&b, s + i, pl); b_str(&b, P[k].rep); i = j; hit = 1; } }
    if (!hit) { b_put(&b, s + i, 1); i++; } }
  return b_done(&b);
}
/* what was typed in Search, and the Trakt account's name, in lines kept by earlier builds ([search] "<words>" in N catalog(s), ... for "<words>"): the words go */
static char *pass_search(const char *s) {
  static const char *M[] = { "[search] \"", "collection(s) for \"" };
  Buf b = { 0 }; size_t i = 0, n = strlen(s);
  while (i < n) { int hit = 0;
    for (size_t k = 0; k < 2 && !hit; k++) { size_t ml = strlen(M[k]); if (strncmp(s + i, M[k], ml)) continue;
      const char *e = memchr(s + i + ml, '"', n - i - ml); const char *nl = memchr(s + i + ml, '\n', n - i - ml);
      if (e && (!nl || e < nl)) { b_put(&b, s + i, ml); b_str(&b, "[query]"); i = (size_t)(e - s); hit = 1; } }
    { static const char T[] = "[trakt] connected as "; size_t tl = sizeof T - 1; /* (and the Trakt account's name, in lines kept by earlier builds) */
      if (!hit && !strncmp(s + i, T, tl)) { const char *nl = memchr(s + i + tl, '\n', n - i - tl); b_str(&b, "[trakt] connected, its name known"); i = nl ? (size_t)(nl - s) : n; hit = 1; } }
    if (!hit) { b_put(&b, s + i, 1); i++; } }
  return b_done(&b);
}
char *logscrub_share(const char *text) {
  char *(*passes[])(const char *) = { pass_search, pass_paths, pass_keys, pass_links, NULL, pass_ip, pass_hash, pass_tokens };
  char *s = strdup(text);
  for (size_t p = 0; p < sizeof passes / sizeof passes[0]; p++) { char *t = passes[p] ? passes[p](s) : pass_email(s, "[email]"); free(s); s = t; }
  return s;
}

/* ---- the code and the seal ---- */
static int rnd(unsigned char *b, size_t n) { FILE *f = fopen("/dev/urandom", "rb"); if (!f) return 0; size_t g = fread(b, 1, n, f); fclose(f); return g == n; }
static int ecc_rng(uint8_t *dest, unsigned size) { return rnd(dest, size); }
int logseal_code(char out[5]) {
  static const char A[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"; unsigned char b[4];
  if (!rnd(b, 4)) return 0;
  for (int i = 0; i < 4; i++) out[i] = A[b[i] & 31]; /* 256 = 8 x 32: no bias */
  out[4] = 0; return 1;
}
static unsigned char *gzip_of(const char *text, size_t len, size_t *outLen) { /* as zlib.gzipSync */
  z_stream z; memset(&z, 0, sizeof z);
  if (deflateInit2(&z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) return NULL;
  uLong cap = deflateBound(&z, (uLong)len); unsigned char *gz = malloc(cap);
  z.next_in = (Bytef *)text; z.avail_in = (uInt)len; z.next_out = gz; z.avail_out = (uInt)cap;
  int rc = deflate(&z, Z_FINISH); *outLen = z.total_out; deflateEnd(&z);
  if (rc != Z_STREAM_END) { free(gz); return NULL; }
  return gz;
}
static char *b64(const unsigned char *d, size_t n) {
  static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  char *o = malloc((n + 2) / 3 * 4 + 1), *p = o;
  for (size_t i = 0; i < n; i += 3) { uint32_t v = (uint32_t)d[i] << 16 | (i + 1 < n ? (uint32_t)d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
    *p++ = A[v >> 18 & 63]; *p++ = A[v >> 12 & 63]; *p++ = i + 1 < n ? A[v >> 6 & 63] : '='; *p++ = i + 2 < n ? A[v & 63] : '='; }
  *p = 0; return o;
}
size_t logseal_unb64(const char *s, unsigned char *out, size_t cap) {
  static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; uint32_t v = 0; int bits = 0; size_t n = 0;
  for (; *s && n < cap; s++) { const char *k = *s == '=' ? NULL : strchr(A, *s); if (!k) continue; v = v << 6 | (uint32_t)(k - A); bits += 6; if (bits >= 8) { bits -= 8; out[n++] = (unsigned char)(v >> bits); } }
  return n;
}
char *logseal_seal(const char *code, const unsigned char devPub[65], const char *text, size_t len) {
  const struct uECC_Curve_t *cv = uECC_secp256r1(); uECC_set_rng(ecc_rng);
  if (devPub[0] != 4 || !uECC_valid_public_key(devPub + 1, cv)) return NULL;
  uint8_t eph[65], priv[32], shared[32]; eph[0] = 4;
  if (!uECC_make_key(eph + 1, priv, cv) || !uECC_shared_secret(devPub + 1, priv, shared, cv)) return NULL;
  char pre[64]; int pl = snprintf(pre, sizeof pre, "stremioorigin-logs-v1|%s|", code);
  size_t kl = (size_t)pl + 65 + 32; uint8_t *km = malloc(kl), key[32];
  memcpy(km, pre, (size_t)pl); memcpy(km + pl, eph, 65); memcpy(km + pl + 65, shared, 32); sbc_sha256(km, kl, key); free(km);
  memset(priv, 0, sizeof priv); memset(shared, 0, sizeof shared);
  size_t gl; unsigned char *gz = gzip_of(text, len, &gl); if (!gz) return NULL;
  if (gl > 40000) { /* the page holds ~64 KB: the header (version, build, device: it was cut off with the old lines)
                       and the newest whole lines, smaller again until it fits */
    static const char cut[] = "[older lines left out: the page holds about 64 KB]\n"; const size_t cl = sizeof cut - 1;
    const char *he = strstr(text, "\n== app ==\n"); size_t hl = he && (size_t)(he - text) < 4096 ? (size_t)(he - text) + 11 : 0;
    size_t keep = (size_t)((double)len * 38000 / gl);
    for (int t = 0; t < 8 && gl > 40000; t++, keep = keep * 4 / 5) {
      size_t from = len > keep ? len - keep : 0; if (from < hl) from = hl;
      while (from > 0 && from < len && text[from - 1] != '\n') from++;
      size_t nl = hl + cl + (len - from); char *t2 = malloc(nl + 1); if (!t2) { free(gz); return NULL; }
      memcpy(t2, text, hl); memcpy(t2 + hl, cut, cl); memcpy(t2 + hl + cl, text + from, len - from);
      free(gz); gz = gzip_of(t2, nl, &gl); free(t2); if (!gz) return NULL; }
    if (gl > 40000) { free(gz); return NULL; } }
  size_t n = 4 + 65 + 12 + 16 + gl; unsigned char *out = malloc(n);
  memcpy(out, "SBL3", 4); memcpy(out + 4, eph, 65);
  if (!rnd(out + 69, 12)) { free(gz); free(out); return NULL; }
  char aad[16]; int al = snprintf(aad, sizeof aad, "SBL3|%s", code);
  sbc_gcm_encrypt(key, 256, out + 69, (const uint8_t *)aad, (size_t)al, gz, gl, out + 97, out + 81);
  memset(key, 0, sizeof key); free(gz);
  char *r = b64(out, n); free(out); return r;
}
