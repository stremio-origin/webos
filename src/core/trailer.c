#include "core/trailer.h"
#include "core/net.h"
#include "core/json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

/* YouTube extraction is dead in waves (every Piped mirror answers "Sign in to confirm you're not a bot",
   Innertube wants tokens), while IMDb hosts its own trailers on its CDN and every title shown already has an
   IMDb id: IMDb first, YouTube only as the fallback. IMDb's signed URLs live about two days. */
#define IMDB_GQL "https://api.graphql.imdb.com/"
#define YT_VER "20.10.38"
#define YT_UA "com.google.android.youtube/" YT_VER " (Linux; U; Android 14)"
#define WEB_UA "Mozilla/5.0 (Web0S; Linux) StremioOrigin"
static const char *PIPED[] = { "https://api.piped.private.coffee", "https://pipedapi.kavin.rocks", "https://pipedapi.ducks.party",
                               "https://pipedapi.reallyaweso.me", "https://pipedapi.adminforge.de" };
#define NPIPED 5
static int pipedGood = -1; static double pipedBad[NPIPED]; /* the mirror that worked last; failures cool down 3 min */
static int noImdb = -1;                                     /* SB_TRAILER_NOIMDB=1: tests the YouTube fallback */

static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static void cpy(char *d, size_t cap, const char *s) { snprintf(d, cap, "%s", s ? s : ""); }

/* ---- small caches: a key, when, and a JSON value (owned) ------------------------------------- */
#define MEMO_N 24
typedef struct { char key[96]; double at; cJSON *v; } Memo;
static Memo imdbCache[MEMO_N], imdbFail[MEMO_N], ytCache[MEMO_N], ytFail[MEMO_N], ytCap[MEMO_N], cineYt[MEMO_N], trCache[MEMO_N];
static cJSON *memo_get(Memo *m, const char *key, double maxAge) {
  double t = now_s();
  for (int i = 0; i < MEMO_N; i++) if (m[i].v && !strcmp(m[i].key, key)) return t - m[i].at < maxAge ? m[i].v : NULL;
  return NULL;
}
static void memo_put(Memo *m, const char *key, cJSON *v) {
  int k = -1; for (int i = 0; i < MEMO_N && k < 0; i++) if (m[i].v && !strcmp(m[i].key, key)) k = i;
  for (int i = 0; i < MEMO_N && k < 0; i++) if (!m[i].v) k = i;
  if (k < 0) { k = 0; for (int i = 1; i < MEMO_N; i++) if (m[i].at < m[k].at) k = i; }
  cJSON_Delete(m[k].v); cpy(m[k].key, sizeof m[k].key, key); m[k].at = now_s(); m[k].v = v;
}
static void memo_del(Memo *m, const char *key) { for (int i = 0; i < MEMO_N; i++) if (m[i].v && !strcmp(m[i].key, key)) { cJSON_Delete(m[i].v); m[i].v = NULL; } }

/* ---- languages --------------------------------------------------------------------------------- */
static const char *LANGS[][2] = { { "eng", "English" }, { "spa", "Spanish" }, { "fre", "French" }, { "fra", "French" }, { "ger", "German" }, { "deu", "German" },
  { "ita", "Italian" }, { "por", "Portuguese" }, { "rus", "Russian" }, { "ara", "Arabic" }, { "dut", "Dutch" }, { "nld", "Dutch" }, { "pol", "Polish" },
  { "tur", "Turkish" }, { "swe", "Swedish" }, { "rum", "Romanian" }, { "ron", "Romanian" }, { "gre", "Greek" }, { "heb", "Hebrew" }, { "hin", "Hindi" },
  { "kor", "Korean" }, { "jpn", "Japanese" }, { "chi", "Chinese" }, { "zho", "Chinese" }, { "cze", "Czech" }, { "dan", "Danish" }, { "fin", "Finnish" },
  { "nor", "Norwegian" }, { "hun", "Hungarian" }, { "bul", "Bulgarian" }, { "ukr", "Ukrainian" }, { "srp", "Serbian" }, { "hrv", "Croatian" },
  { "slo", "Slovak" }, { "slv", "Slovenian" }, { "spl", "Spanish (LA)" }, { "pob", "Portuguese (BR)" } };
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
/* a caption's language: the "/en-US-…" part of its URL, else its display name, else English */
static void guess_lang(const char *name, const char *url, char *out) {
  static const char *two[][2] = { { "en", "eng" }, { "es", "spa" }, { "fr", "fre" }, { "de", "ger" }, { "it", "ita" }, { "pt", "por" }, { "ru", "rus" },
    { "ar", "ara" }, { "nl", "dut" }, { "pl", "pol" }, { "tr", "tur" }, { "sv", "swe" }, { "ro", "rum" }, { "el", "gre" }, { "he", "heb" },
    { "hi", "hin" }, { "ko", "kor" }, { "ja", "jpn" }, { "zh", "chi" } };
  for (const char *p = url ? strchr(url, '/') : NULL; p; p = strchr(p + 1, '/')) {
    int n = 0; while (n < 3 && lower(p[1 + n]) >= 'a' && lower(p[1 + n]) <= 'z') n++;
    for (int k = n; k >= 2; k--) { char c = p[1 + k]; if (c != '-' && c != '_') continue;
      char code[4] = { 0 }; for (int i = 0; i < k; i++) code[i] = (char)lower(p[1 + i]);
      for (size_t i = 0; i < sizeof two / sizeof two[0]; i++) if (!strcmp(code, two[i][0])) { strcpy(out, two[i][1]); return; }
      if (k == 3) { strcpy(out, code); return; }
      goto byname; } /* the first match decides, as the pattern would */
  }
byname:
  for (size_t i = 0; name && i < sizeof LANGS / sizeof LANGS[0]; i++) if (!strcasecmp(name, LANGS[i][1])) { strcpy(out, LANGS[i][0]); return; }
  strcpy(out, "eng");
}

static int valid_yt(const char *s) {
  size_t n = strlen(s); if (n < 6 || n > 16) return 0;
  for (size_t i = 0; i < n; i++) { char c = s[i]; if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return 0; }
  return 1;
}
static int valid_tt(const char *s, int minD, int maxD) {
  if (!s || s[0] != 't' || s[1] != 't') return 0; int n = 0; for (const char *p = s + 2; *p; p++, n++) if (*p < '0' || *p > '9') return 0;
  return n >= minD && n <= maxD;
}
static int is_yt_host(const char *url) {
  const char *h = strstr(url, "://"); if (!h) return 0; h += 3; const char *e = h + strcspn(h, "/:?#"); size_t n = (size_t)(e - h);
  static const char *d[] = { "youtube.com", "googlevideo.com", "ytimg.com" };
  for (int i = 0; i < 3; i++) { size_t l = strlen(d[i]); if (n >= l && !strncasecmp(e - l, d[i], l) && (n == l || e[-(long)l - 1] == '.')) return 1; }
  return 0;
}

/* ---- jobs: one trailer_find each; they live while any request or timer still points at them ---- */
typedef struct Job {
  unsigned tok; TrailerQuality quality; int cancelled, sent, refs; char imdb[24], key[200];
  char yt[8][20]; int nyt;
  TrailerCb cb; void *ud;
  double deadline;
  cJSON *out; int deliver;           /* the answer, handed over on the next tick when it came at once */
  int got; cJSON *imdbList, *caps;   /* IMDb and the captioned YouTube cuts, asked side by side */
  int imdbTry; double imdbRetryAt;
  int capsLeft, capsFired; double capsCapAt;
  int cineTi;
  struct Job *next;
} Job;
static Job *jobs; static unsigned nextTok = 1;
static void job_unref(Job *j) {
  if (--j->refs > 0) return;
  for (Job **pp = &jobs; *pp; pp = &(*pp)->next) if (*pp == j) { *pp = j->next; break; }
  cJSON_Delete(j->imdbList); cJSON_Delete(j->caps); cJSON_Delete(j->out); free(j);
}
static void deliver(Job *j) {
  cJSON *o = j->out; j->out = NULL; j->deliver = 0;
  if (j->cancelled || !j->cb) { cJSON_Delete(o); return; }
  const char *url = js_str(o, "url", "");
  if (!*url) { j->cb(NULL, js_str(o, "err", "unavailable"), j->ud); cJSON_Delete(o); return; }
  Trailer *t = calloc(1, sizeof *t); const char *hls = js_str(o, "hls", "");
  /* HLS for interactive playback (better seeking); background trailers use progressive MP4 to start quickly. */
  if (*hls && j->quality == TRAILER_QUALITY_BEST) { cpy(t->url, sizeof t->url, hls); cpy(t->alt, sizeof t->alt, url); cpy(t->mime, sizeof t->mime, "application/x-mpegurl"); }
  else { cpy(t->url, sizeof t->url, url); cpy(t->mime, sizeof t->mime, "video/mp4"); }
  cpy(t->name, sizeof t->name, js_str(o, "name", "Trailer")); t->runtime = (int)js_num(o, "runtime", 0); cpy(t->source, sizeof t->source, js_str(o, "source", ""));
  const cJSON *s; cJSON_ArrayForEach(s, js_arr(o, "subs")) { if (t->nsubs >= TRAILER_MAX_SUBS) break; TrailerSub *d = &t->subs[t->nsubs++];
    cpy(d->url, sizeof d->url, js_str(s, "url", "")); cpy(d->lang, sizeof d->lang, js_str(s, "lang", "eng")); cpy(d->name, sizeof d->name, js_str(s, "name", "English")); cpy(d->kind, sizeof d->kind, js_str(s, "kind", "srt")); }
  j->cb(t, NULL, j->ud); free(t); cJSON_Delete(o);
}
static void send(Job *j, cJSON *o) {
  if (j->sent) { cJSON_Delete(o); return; }
  j->sent = 1;
  if (*js_str(o, "url", "")) memo_put(trCache, j->key, cJSON_Duplicate(o, 1)); /* pressing Trailer again is instant */
  fprintf(stderr, "[trailer] %s -> %s %s\n", j->imdb[0] ? j->imdb : (j->nyt ? j->yt[0] : "?"), js_str(o, "source", js_str(o, "err", "")), js_str(o, "name", ""));
  j->out = o; j->deliver = 1; j->refs++; /* delivered on the next tick: never inside trailer_find */
}

/* ---- the best variant of an HLS master ----------------------------------------------------------- */
/* IMDb's masters list 360p first and the TV starts on the first variant (blurry for seconds, sometimes for
   good): the player gets the best variant's own playlist, resolution first, then bitrate */
static void resolve_url(const char *base, const char *rel, char *out, size_t cap) {
  if (!strncmp(rel, "http://", 7) || !strncmp(rel, "https://", 8)) { cpy(out, cap, rel); return; }
  const char *sch = strstr(base, "://"); if (!sch) { cpy(out, cap, rel); return; }
  if (!strncmp(rel, "//", 2)) { snprintf(out, cap, "%.*s:%s", (int)(sch - base), base, rel); return; }
  const char *host = sch + 3, *path = host + strcspn(host, "/?#");
  if (rel[0] == '/') { snprintf(out, cap, "%.*s%s", (int)(path - base), base, rel); return; }
  const char *q = path + strcspn(path, "?#"), *slash = q; while (slash > path && slash[-1] != '/') slash--;
  if (slash == path) snprintf(out, cap, "%.*s/%s", (int)(path - base), base, rel);
  else snprintf(out, cap, "%.*s%s", (int)(slash - base), base, rel);
}
static int hls_top(const char *master, const char *txt, char *out, size_t cap) {
  double best = -1; out[0] = 0; const char *p = txt;
  while (p && *p) {
    const char *e = p + strcspn(p, "\r\n");
    if (!strncmp(p, "#EXT-X-STREAM-INF:", 18)) {
      char attrs[512]; snprintf(attrs, sizeof attrs, "%.*s", (int)(e - p - 18), p + 18);
      double bw = 0; long rw = 0, rh = 0; char *b = attrs;
      for (char *a = attrs; *a; a++) if ((a == attrs || a[-1] == ',') && !strncmp(a, "BANDWIDTH=", 10)) { bw = atof(a + 10); break; }
      if ((b = strstr(attrs, "RESOLUTION="))) sscanf(b + 11, "%ldx%ld", &rw, &rh);
      const char *n = e; char nx[1024] = "";
      while (*n) { n += strspn(n, "\r\n"); const char *ne = n + strcspn(n, "\r\n"); const char *s = n; while (s < ne && (*s == ' ' || *s == '\t')) s++;
        if (s < ne && *s != '#') { const char *t = ne; while (t > s && (t[-1] == ' ' || t[-1] == '\t')) t--; snprintf(nx, sizeof nx, "%.*s", (int)(t - s), s); break; } n = ne; }
      double score = (double)rw * rh * 1e5 + bw / 1000;
      if (nx[0] && score > best) { best = score; resolve_url(master, nx, out, cap); }
    }
    p = *e ? e + 1 : NULL;
  }
  return out[0] != 0;
}
typedef struct { Job *j; cJSON *o; } HlsReq;
static void on_hls(int st, const unsigned char *body, size_t len, void *ud) {
  HlsReq *r = ud; char top[2048] = "";
  if (st >= 200 && st < 300 && hls_top(js_str(r->o, "hls", ""), (const char *)body, top, sizeof top)) {
    cJSON_AddStringToObject(r->o, "master", js_str(r->o, "hls", "")); cJSON_ReplaceItemInObjectCaseSensitive(r->o, "hls", cJSON_CreateString(top)); }
  send(r->j, r->o); job_unref(r->j); free(r);
}
static void reply(Job *j, cJSON *o) {
  if (j->sent) { cJSON_Delete(o); return; }
  if (*js_str(o, "hls", "") && j->quality == TRAILER_QUALITY_BEST) { HlsReq *r = malloc(sizeof *r); r->j = j; r->o = o; j->refs++;
    net_get_timeout(js_str(o, "hls", ""), 4000, on_hls, r); return; }
  send(j, o);
}

/* ---- YouTube: Innertube's Android client, then Piped mirrors ------------------------------------- */
typedef void (*YtCb)(cJSON *res, void *ud);  /* res: {lo, hi, len, subs} (owned by the callee) or NULL */
typedef struct { char vid[20]; YtCb cb; void *ud; int k, order[NPIPED], n; } YtReq;
static void on_innertube(int st, const unsigned char *body, size_t len, void *ud) {
  YtReq *r = ud; cJSON *j = st > 0 ? net_json(body) : NULL, *out = NULL;
  if (j) {
    const char *lo = "", *hi = "", *mid = ""; const cJSON *f;
    cJSON_ArrayForEach(f, js_arr(js_obj(j, "streamingData"), "formats")) {
      const char *u = js_str(f, "url", ""); if (!*u || strncmp(js_str(f, "mimeType", ""), "video/mp4", 9)) continue;
      int itag = (int)js_num(f, "itag", 0);
      if (itag == 22) { hi = u; mid = u; } else if (itag == 18) lo = u; else if (!*lo) lo = u;
    }
    cJSON *subs = cJSON_CreateArray(); const cJSON *c;
    cJSON_ArrayForEach(c, js_arr(js_obj(js_obj(j, "captions"), "playerCaptionsTracklistRenderer"), "captionTracks")) {
      const char *bu = js_str(c, "baseUrl", ""); if (!*bu) continue;
      const cJSON *nm = js_obj(c, "name"), *runs = js_arr(nm, "runs"), *r0 = runs ? cJSON_GetArrayItem(runs, 0) : NULL;
      const char *snm = js_str(r0, "text", js_str(nm, "simpleText", "English")); if (!*snm) snm = "English";
      char lc[16]; cpy(lc, sizeof lc, js_str(c, "languageCode", "")); for (char *q = lc; *q; q++) *q = (char)lower(*q);
      char lang[8]; if (!strcmp(lc, "en")) strcpy(lang, "eng"); else if (strlen(lc) == 2) guess_lang(snm, "", lang); else cpy(lang, sizeof lang, lc);
      cJSON *s = cJSON_CreateObject(); cJSON_AddStringToObject(s, "name", snm); cJSON_AddStringToObject(s, "lang", lang); cJSON_AddStringToObject(s, "url", bu); cJSON_AddStringToObject(s, "kind", "timedtext");
      cJSON_AddItemToArray(subs, s);
    }
    if (*lo || *hi || cJSON_GetArraySize(subs)) {
      out = cJSON_CreateObject(); cJSON_AddStringToObject(out, "lo", lo); cJSON_AddStringToObject(out, "hi", hi); cJSON_AddStringToObject(out, "mid", mid);
      cJSON_AddNumberToObject(out, "len", js_numstr(js_obj(j, "videoDetails"), "lengthSeconds", 0)); /* to match it against IMDb's cuts */
      cJSON_AddItemToObject(out, "subs", subs);
    } else cJSON_Delete(subs);
    net_json_free(j);
  }
  r->cb(out, r->ud); free(r);
}
static void yt_innertube(const char *vid, YtCb cb, void *ud) {
  YtReq *r = calloc(1, sizeof *r); cpy(r->vid, sizeof r->vid, vid); r->cb = cb; r->ud = ud;
  char body[512]; snprintf(body, sizeof body, "{\"videoId\":\"%s\",\"context\":{\"client\":{\"clientName\":\"ANDROID\",\"clientVersion\":\"" YT_VER "\",\"androidSdkVersion\":34,\"hl\":\"en\",\"gl\":\"US\"}},\"contentCheckOk\":true,\"racyCheckOk\":true}", vid);
  net_post_hdr_t("https://www.youtube.com/youtubei/v1/player?prettyPrint=false", body, "application/json",
                 "User-Agent: " YT_UA "\nX-Youtube-Client-Name: 3\nX-Youtube-Client-Version: " YT_VER, 10000, on_innertube, r);
}
static void piped_try(YtReq *r);
static void on_piped(int st, const unsigned char *body, size_t len, void *ud) {
  YtReq *r = ud; int ii = r->order[r->k]; cJSON *j = st > 0 ? net_json(body) : NULL;
  const char *lo = "", *hi = "", *mid = ""; int midQ = 0; const cJSON *v;
  cJSON_ArrayForEach(v, js_arr(j, "videoStreams")) {
    if (js_bool(v, "videoOnly", 0) || !*js_str(v, "url", "") || strncmp(js_str(v, "mimeType", ""), "video/mp4", 9)) continue;
    int q = atoi(js_str(v, "quality", "0"));
    if (q >= 480) { if (!*hi) hi = js_str(v, "url", ""); if (q <= 720 && q > midQ) { mid = js_str(v, "url", ""); midQ = q; } }
    else if (!*lo) lo = js_str(v, "url", "");
  }
  if (*lo || *hi) {
    cJSON *out = cJSON_CreateObject(); cJSON_AddStringToObject(out, "lo", lo); cJSON_AddStringToObject(out, "hi", hi); cJSON_AddStringToObject(out, "mid", mid); cJSON_AddItemToObject(out, "subs", cJSON_CreateArray());
    pipedGood = ii; pipedBad[ii] = 0; fprintf(stderr, "[yt] piped ok via %s\n", PIPED[ii]);
    net_json_free(j); r->cb(out, r->ud); free(r); return;
  }
  if (!j) pipedBad[ii] = now_s(); /* network or parse failure: this mirror cools down */
  net_json_free(j); r->k++; piped_try(r);
}
static void piped_try(YtReq *r) {
  if (r->k >= r->n) { r->cb(NULL, r->ud); free(r); return; }
  char u[256]; snprintf(u, sizeof u, "%s/streams/%s", PIPED[r->order[r->k]], r->vid);
  net_get_hdr(u, "User-Agent: " WEB_UA "\nAccept: */*", on_piped, r);
}
static void yt_piped(const char *vid, YtCb cb, void *ud) {
  YtReq *r = calloc(1, sizeof *r); cpy(r->vid, sizeof r->vid, vid); r->cb = cb; r->ud = ud;
  double t = now_s(); int fresh[NPIPED], nf = 0, cool[NPIPED], nc = 0; /* throttled mirrors only as a last resort */
#define PUSH(i) do { if (pipedBad[i] && t - pipedBad[i] < 180) cool[nc++] = (i); else fresh[nf++] = (i); } while (0)
  if (pipedGood >= 0) PUSH(pipedGood);
  for (int i = 0; i < NPIPED; i++) if (i != pipedGood) PUSH(i);
#undef PUSH
  for (int i = 0; i < nf; i++) r->order[r->n++] = fresh[i];
  for (int i = 0; i < nc; i++) r->order[r->n++] = cool[i];
  piped_try(r);
}
/* one YouTube video: MP4 variants (cached 2 h; a dead one is left alone for 3 min) */
typedef void (*YtUrlCb)(Job *j, const char *url, const char *err);
typedef struct { Job *j; char vid[20]; YtUrlCb cb; } YtRes;
static const char *yt_pick(const cJSON *c, TrailerQuality quality) {
  const char *lo = js_str(c, "lo", ""), *hi = js_str(c, "hi", ""), *mid = js_str(c, "mid", "");
  if (quality == TRAILER_QUALITY_720P) return *mid ? mid : *lo ? lo : hi;
  return quality == TRAILER_QUALITY_LOW ? (*lo ? lo : hi) : (*hi ? hi : lo);
}
static void yt_res_done(YtRes *r, cJSON *res) {
  if (res) { const char *u = yt_pick(res, r->j->quality); memo_put(ytCache, r->vid, res); r->cb(r->j, u, *u ? "" : "no format"); }
  else { memo_put(ytFail, r->vid, cJSON_CreateTrue()); fprintf(stderr, "[yt] %s unresolved\n", r->vid); r->cb(r->j, "", "unavailable"); }
  job_unref(r->j); free(r);
}
static void on_res_piped(cJSON *res, void *ud) { yt_res_done(ud, res); }
static void on_res_inner(cJSON *res, void *ud) { YtRes *r = ud; if (res) yt_res_done(r, res); else yt_piped(r->vid, on_res_piped, r); }
static void yt_resolve(Job *j, const char *vid, YtUrlCb cb) {
  if (!valid_yt(vid)) { cb(j, "", "bad id"); return; }
  const cJSON *c = memo_get(ytCache, vid, 2 * 3600);
  if (c) { const char *u = yt_pick(c, j->quality); cb(j, u, *u ? "" : "no format"); return; }
  if (memo_get(ytFail, vid, 180)) { cb(j, "", "unavailable"); return; } /* mirror outages recover: asked again after 3 min */
  YtRes *r = calloc(1, sizeof *r); r->j = j; j->refs++; cpy(r->vid, sizeof r->vid, vid); r->cb = cb;
  yt_innertube(vid, on_res_inner, r);
}

/* ---- IMDb ----------------------------------------------------------------------------------------- */
static int score(const cJSON *v) {
  const char *nm = js_str(v, "name", ""); int run = (int)js_num(v, "run", 0);
  char low[256]; size_t i = 0; for (; nm[i] && i < sizeof low - 1; i++) low[i] = (char)lower(nm[i]); low[i] = 0;
  return (js_bool(v, "trailer", 0) ? 100 : 0) + (strstr(low, "official") ? 20 : 0) + (strstr(low, "trailer") ? 10 : 0) + (run >= 50 ? 5 : 0) + (cJSON_GetArraySize(js_arr(v, "subs")) ? 15 : 0);
}
/* the videos worth playing, best first: real trailers, "official" ones, captions, a sane length (no 15 s teasers) */
static cJSON *imdb_list(const cJSON *j) {
  const cJSON *e; cJSON *list = cJSON_CreateArray();
  cJSON_ArrayForEach(e, js_arr(js_obj(js_obj(js_obj(j, "data"), "title"), "primaryVideos"), "edges")) {
    const cJSON *n = js_obj(e, "node"); if (!n) continue;
    const char *ct = js_str(js_obj(n, "contentType"), "id", "");
    cJSON *v = cJSON_CreateObject(), *subs = cJSON_CreateArray(); const cJSON *t;
    cJSON_ArrayForEach(t, js_arr(n, "timedTextTracks")) {
      const char *u = js_str(t, "url", ""); if (!*u) continue;
      const char *snm = js_str(js_obj(t, "displayName"), "value", "English"); char lang[8]; guess_lang(snm, u, lang);
      cJSON *s = cJSON_CreateObject(); cJSON_AddStringToObject(s, "name", snm); cJSON_AddStringToObject(s, "lang", lang); cJSON_AddStringToObject(s, "url", u); cJSON_AddStringToObject(s, "kind", "srt");
      cJSON_AddItemToArray(subs, s);
    }
    const char *lo = "", *hi = "", *full = "", *hls = ""; const cJSON *pu;
    cJSON_ArrayForEach(pu, js_arr(n, "playbackURLs")) {
      const char *d = js_str(pu, "videoDefinition", ""), *m = js_str(pu, "mimeType", ""), *u = js_str(pu, "url", "");
      /* IMDb's newer MP4s mark only ~2 keyframes per trailer, so every seek restarted it; their HLS is cut in ~12 s
         segments that each start on a keyframe, and webOS plays HLS itself. HLS first, the MP4 as the fallback */
      if (!strcmp(m, "application/x-mpegurl") && *u) { hls = u; continue; }
      if (strcmp(m, "video/mp4") || !*u) continue;
      if (!strcmp(d, "DEF_480p") || (!strcmp(d, "DEF_SD") && !*lo)) lo = u;
      else if (!strcmp(d, "DEF_720p")) hi = u;
      else if (!strcmp(d, "DEF_1080p")) full = u;
      else if (!strcmp(d, "DEF_240p") && !*lo) lo = u;
    }
    cJSON_AddStringToObject(v, "name", js_str(js_obj(n, "name"), "value", "Trailer")); cJSON_AddNumberToObject(v, "run", js_num(js_obj(n, "runtime"), "value", 0));
    cJSON_AddBoolToObject(v, "trailer", strstr(ct, "trailer") != NULL);
    cJSON_AddStringToObject(v, "lo", lo); cJSON_AddStringToObject(v, "hi", hi); cJSON_AddStringToObject(v, "full", full); cJSON_AddStringToObject(v, "hls", hls);
    cJSON_AddItemToObject(v, "subs", subs);
    if (*lo || *hi || *full) cJSON_AddItemToArray(list, v); else cJSON_Delete(v);
  }
  /* a stable sort (at most 10) */
  int n = cJSON_GetArraySize(list); if (n < 2) return list;
  cJSON **a = malloc(n * sizeof *a); int *sc = malloc(n * sizeof *sc);
  for (int i = 0; i < n; i++) { a[i] = cJSON_DetachItemFromArray(list, 0); sc[i] = score(a[i]); }
  for (int i = 1; i < n; i++) { cJSON *x = a[i]; int s = sc[i], k = i - 1; while (k >= 0 && sc[k] < s) { a[k + 1] = a[k]; sc[k + 1] = sc[k]; k--; } a[k + 1] = x; sc[k + 1] = s; }
  for (int i = 0; i < n; i++) cJSON_AddItemToArray(list, a[i]);
  free(a); free(sc); return list;
}
static void decide(Job *j);
static void imdb_done(Job *j, cJSON *list) { j->imdbList = list; if (++j->got == 2) decide(j); }
static void imdb_ask(Job *j);
static void on_imdb(int st, const unsigned char *body, size_t len, void *ud) {
  Job *j = ud; cJSON *r = st > 0 ? net_json(body) : NULL;
  if (!r && ++j->imdbTry < 2) { j->imdbRetryAt = now_s() + 1.2; return; } /* a dropped request: asked once more (the ref stays for the retry) */
  cJSON *list = imdb_list(r); int n = cJSON_GetArraySize(list);
  if (n) { memo_put(imdbCache, j->imdb, cJSON_Duplicate(list, 1)); memo_del(imdbFail, j->imdb); }
  else if (js_obj(js_obj(r, "data"), "title")) memo_put(imdbFail, j->imdb, cJSON_CreateTrue()); /* a real "no videos", not a failed request */
  fprintf(stderr, "[trailer] imdb %s -> %d video(s)%s%s\n", j->imdb, n, n ? " best=" : "", n ? js_str(cJSON_GetArrayItem(list, 0), "name", "") : "");
  cJSON_Delete(r);
  if (!n) { cJSON_Delete(list); list = NULL; }
  imdb_done(j, list); job_unref(j);
}
static void imdb_ask(Job *j) {
  char q[512]; snprintf(q, sizeof q, "query{title(id:\"%s\"){primaryVideos(first:10){edges{node{name{value} runtime{value} contentType{id} playbackURLs{url mimeType videoDefinition} timedTextTracks{url type displayName{value}}}}}}}", j->imdb);
  cJSON *b = cJSON_CreateObject(); cJSON_AddStringToObject(b, "query", q); char *body = cJSON_PrintUnformatted(b); cJSON_Delete(b);
  net_post_hdr_t(IMDB_GQL, body, "application/json", "x-imdb-client-name: imdb-web-next\nUser-Agent: Mozilla/5.0", 10000, on_imdb, j);
  free(body);
}
static void imdb_trailers(Job *j) {
  if (noImdb < 0) { const char *e = getenv("SB_TRAILER_NOIMDB"); noImdb = e && *e && *e != '0'; }
  if (noImdb || !valid_tt(j->imdb, 5, 10)) { imdb_done(j, NULL); return; }
  const cJSON *c = memo_get(imdbCache, j->imdb, 3 * 3600);
  if (c) { imdb_done(j, cJSON_Duplicate(c, 1)); return; }
  if (memo_get(imdbFail, j->imdb, 600)) { imdb_done(j, NULL); return; }
  j->refs++; imdb_ask(j);
}

/* ---- YouTube cuts that have captions: [{id, len, subs}] (side by side, 6 s at most, cached 1 h: the
   caption URLs are signed and expire) ----------------------------------------------------------------- */
static void caps_fin(Job *j) {
  if (j->capsFired) return; j->capsFired = 1;
  if (++j->got == 2) decide(j);
  job_unref(j); /* the 6 s timer's ref */
}
typedef struct { Job *j; char id[20]; } CapReq;
static void on_cap(cJSON *yd, void *ud) {
  CapReq *r = ud; Job *j = r->j; cJSON *v = NULL;
  if (yd && cJSON_GetArraySize(js_arr(yd, "subs")) && js_num(yd, "len", 0) > 0) {
    v = cJSON_CreateObject(); cJSON_AddStringToObject(v, "id", r->id); cJSON_AddNumberToObject(v, "len", js_num(yd, "len", 0)); cJSON_AddItemToObject(v, "subs", cJSON_Duplicate(js_arr(yd, "subs"), 1)); }
  memo_put(ytCap, r->id, v ? cJSON_Duplicate(v, 1) : cJSON_CreateNull());
  cJSON_Delete(yd);
  if (v) { if (!j->capsFired) cJSON_AddItemToArray(j->caps, v); else cJSON_Delete(v); }
  if (--j->capsLeft == 0) caps_fin(j);
  job_unref(j); free(r);
}
static void yt_captioned(Job *j) {
  int n = j->nyt < 5 ? j->nyt : 5; j->caps = cJSON_CreateArray(); j->capsLeft = n; j->capsFired = 0;
  j->refs++; j->capsCapAt = now_s() + 6;
  for (int i = 0; i < n; i++) {
    const cJSON *c = memo_get(ytCap, j->yt[i], 3600);
    if (c) { if (!cJSON_IsNull(c)) cJSON_AddItemToArray(j->caps, cJSON_Duplicate(c, 1)); j->capsLeft--; continue; }
    CapReq *r = calloc(1, sizeof *r); r->j = j; j->refs++; cpy(r->id, sizeof r->id, j->yt[i]);
    yt_innertube(j->yt[i], on_cap, r);
  }
  if (j->capsLeft == 0) caps_fin(j);
}

/* ---- the answer ------------------------------------------------------------------------------------ */
static void imdb_reply(Job *j, const cJSON *v, const cJSON *subs) {
  const char *lo = js_str(v, "lo", ""), *hi = js_str(v, "hi", ""), *full = js_str(v, "full", "");
  const char *u = j->quality == TRAILER_QUALITY_BEST ? (*full ? full : *hi ? hi : lo)
    : j->quality == TRAILER_QUALITY_720P ? (*hi ? hi : *lo ? lo : full)
    : (*lo ? lo : *hi ? hi : full);
  cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "url", u); cJSON_AddStringToObject(o, "hls", js_str(v, "hls", ""));
  cJSON_AddStringToObject(o, "name", js_str(v, "name", "Trailer")); cJSON_AddNumberToObject(o, "runtime", js_num(v, "run", 0)); cJSON_AddStringToObject(o, "source", "imdb");
  cJSON_AddItemToObject(o, "subs", subs ? cJSON_Duplicate(subs, 1) : cJSON_CreateArray());
  reply(j, o);
}
static cJSON *err_obj(const char *e) { cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "err", e); return o; }
static void on_yt_url(Job *j, const char *url, const char *err) {
  if (!*url) { reply(j, err_obj(*err ? err : "unavailable")); return; }
  const cJSON *c = memo_get(ytCache, j->yt[0], 1e9), *subs = js_arr(c, "subs");
  cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "url", url); cJSON_AddStringToObject(o, "name", "Trailer"); cJSON_AddStringToObject(o, "source", "youtube");
  cJSON_AddItemToObject(o, "subs", subs ? cJSON_Duplicate(subs, 1) : cJSON_CreateArray());
  reply(j, o);
}
static void decide(Job *j) {
  const cJSON *list = j->imdbList; int nl = cJSON_GetArraySize(list), nc = cJSON_GetArraySize(j->caps);
  if (nl) {
    const cJSON *v0 = cJSON_GetArrayItem(list, 0), *s0 = js_arr(v0, "subs");
    if (cJSON_GetArraySize(s0) || !nc) { imdb_reply(j, v0, s0); return; }
    /* IMDb's trailer has no captions: a YouTube cut's captions fit only the IMDb trailer of the same length (±2 s) */
    const cJSON *v;
    cJSON_ArrayForEach(v, list) {
      int run = (int)js_num(v, "run", 0); if (!js_bool(v, "trailer", 0) || !run) continue;
      const cJSON *c; cJSON_ArrayForEach(c, j->caps) { double d = run - js_num(c, "len", 0); if (d <= 2 && d >= -2) { imdb_reply(j, v, js_arr(c, "subs")); return; } }
    }
    imdb_reply(j, v0, NULL); return;
  }
  if (!j->nyt) { reply(j, err_obj("unavailable")); return; }
  yt_resolve(j, j->yt[0], on_yt_url);
}
static void with_yt(Job *j) {
  j->got = 0;
  if (j->nyt && j->quality == TRAILER_QUALITY_BEST) yt_captioned(j); else { j->caps = cJSON_CreateArray(); j->capsFired = 1; j->got++; } /* no captions to match: no timer either */
  imdb_trailers(j);
}

/* ---- every YouTube trailer Cinemeta knows for a title (movie, then series) ------------------------- */
static int add_ids(Job *j, const cJSON *arr) {
  const cJSON *t; cJSON_ArrayForEach(t, arr) { if (j->nyt >= 5) break; const char *s = js_str(t, "source", ""); if (!*s) s = js_str(t, "ytId", "");
    if (!*s || !valid_yt(s)) continue; int dup = 0; for (int i = 0; i < j->nyt; i++) if (!strcmp(j->yt[i], s)) dup = 1;
    if (!dup) cpy(j->yt[j->nyt++], sizeof j->yt[0], s); }
  return j->nyt;
}
static void cine_next(Job *j);
static void on_cine(int st, const unsigned char *body, size_t len, void *ud) {
  Job *j = ud; cJSON *r = st > 0 ? net_json(body) : NULL; const cJSON *m = js_obj(r, "meta");
  j->nyt = 0; add_ids(j, js_arr(m, "trailers")); add_ids(j, js_arr(m, "trailerStreams"));
  cJSON_Delete(r);
  if (j->nyt) { cJSON *ids = cJSON_CreateArray(); for (int i = 0; i < j->nyt; i++) cJSON_AddItemToArray(ids, cJSON_CreateString(j->yt[i])); memo_put(cineYt, j->imdb, ids); with_yt(j); }
  else cine_next(j);
  job_unref(j);
}
static void cine_next(Job *j) {
  static const char *types[] = { "movie", "series" };
  if (j->cineTi >= 2) { with_yt(j); return; }
  char u[256]; snprintf(u, sizeof u, "https://v3-cinemeta.strem.io/meta/%s/%s.json", types[j->cineTi++], j->imdb);
  j->refs++; net_get_hdr(u, "User-Agent: " WEB_UA "\nAccept: */*", on_cine, j);
}
static void cinemeta_yt(Job *j) {
  if (!valid_tt(j->imdb, 1, 20)) { with_yt(j); return; }
  const cJSON *c = memo_get(cineYt, j->imdb, 1e12), *s;
  if (c) { j->nyt = 0; cJSON_ArrayForEach(s, c) if (cJSON_IsString(s) && j->nyt < 8) cpy(j->yt[j->nyt++], sizeof j->yt[0], s->valuestring); with_yt(j); return; }
  cine_next(j);
}

/* ---- the API ----------------------------------------------------------------------------------------- */
unsigned trailer_find(const char *imdbId, const char *ytIds, TrailerQuality quality, TrailerCb cb, void *ud) {
  Job *j = calloc(1, sizeof *j); j->tok = nextTok++; j->quality = quality; j->cb = cb; j->ud = ud; j->refs = 1; j->next = jobs; jobs = j;
  cpy(j->imdb, sizeof j->imdb, imdbId); snprintf(j->key, sizeof j->key, "%s|%s|%d", j->imdb, ytIds ? ytIds : "", j->quality);
  j->deadline = now_s() + 20; /* the TV can be busy (app start): slow beats failed, but never a hang on a dead upstream */
  const cJSON *tc = memo_get(trCache, j->key, 1800);
  if (tc) { j->sent = 1; j->out = cJSON_Duplicate(tc, 1); j->deliver = 1; j->refs++; }
  else {
    char buf[200]; cpy(buf, sizeof buf, ytIds);
    for (char *s = strtok(buf, ","); s && j->nyt < 8; s = strtok(NULL, ",")) if (valid_yt(s)) cpy(j->yt[j->nyt++], sizeof j->yt[0], s);
    if (j->nyt || !j->imdb[0]) with_yt(j); else cinemeta_yt(j);
  }
  unsigned tok = j->tok; job_unref(j); /* the start's own ref */
  return tok;
}
unsigned trailer_find_meta(const cJSON *meta, const char *id, TrailerQuality quality, TrailerCb cb, void *ud) {
  /* an addon's trailer given as a plain video address (a stream with a url, not a YouTube id): played as it is */
  { const cJSON *ts = js_arr(meta, "trailerStreams"), *t; if (ts) cJSON_ArrayForEach(t, ts) { const char *u = js_str(t, "url", "");
      if (strncmp(u, "http://", 7) && strncmp(u, "https://", 8)) continue;
      Job *j = calloc(1, sizeof *j); j->tok = nextTok++; j->cb = cb; j->ud = ud; j->sent = 1; j->deliver = 1; j->refs = 1; j->next = jobs; jobs = j;
      cJSON *o = cJSON_CreateObject(); int hls = strstr(u, ".m3u8") != NULL;
      cJSON_AddStringToObject(o, hls ? "hls" : "url", u); if (hls) cJSON_AddStringToObject(o, "url", u);
      cJSON_AddStringToObject(o, "name", js_str(t, "title", js_str(t, "name", "Trailer"))); cJSON_AddStringToObject(o, "source", "addon");
      j->out = o; fprintf(stderr, "[trailer] %s -> the addon's own address\n", id ? id : ""); return j->tok; } }
  char imdb[24], yt[128]; trailer_imdb_id(meta, id, imdb, sizeof imdb); trailer_yt_ids(meta, yt, sizeof yt);
  if (!imdb[0] && !yt[0]) { /* nothing to look up: the answer still comes on a tick */
    Job *j = calloc(1, sizeof *j); j->tok = nextTok++; j->cb = cb; j->ud = ud; j->sent = 1; j->out = err_obj("unavailable"); j->deliver = 1; j->refs = 1; j->next = jobs; jobs = j;
    return j->tok; }
  return trailer_find(imdb, yt, quality, cb, ud);
}
void trailer_cancel(unsigned tok) { for (Job *j = jobs; j; j = j->next) if (j->tok == tok) j->cancelled = 1; }
void trailer_tick(void) {
  if (!jobs) return;
  double t = now_s();
  for (Job *j = jobs, *n; j; j = n) {
    j->refs++; /* nothing below may free it while it is looked at */
    if (j->deliver) { deliver(j); j->refs--; }  /* the ref send() took */
    if (!j->sent && t >= j->deadline) { send(j, err_obj("timeout")); deliver(j); j->refs--; }
    if (j->imdbRetryAt && t >= j->imdbRetryAt) { j->imdbRetryAt = 0; imdb_ask(j); }
    if (j->caps && !j->capsFired && t >= j->capsCapAt) caps_fin(j);
    n = j->next; job_unref(j);
  }
}

/* ---- what a title offers ------------------------------------------------------------------------------ */
const char *trailer_imdb_id(const cJSON *meta, const char *id, char *out, size_t cap) {
  const char *x = js_str(meta, "imdb_id", ""); if (!*x) x = id ? id : ""; if (!*x) x = js_str(meta, "id", "");
  char b[64]; cpy(b, sizeof b, x); char *c = strchr(b, ':'); if (c) *c = 0;
  cpy(out, cap, valid_tt(b, 1, 20) ? b : ""); return out;
}
const char *trailer_yt_ids(const cJSON *meta, char *out, size_t cap) {
  Job tmp; memset(&tmp, 0, sizeof tmp); add_ids(&tmp, js_arr(meta, "trailers")); add_ids(&tmp, js_arr(meta, "trailerStreams"));
  out[0] = 0; for (int i = 0; i < tmp.nyt; i++) { size_t l = strlen(out); snprintf(out + l, cap - l, "%s%s", i ? "," : "", tmp.yt[i]); }
  return out;
}
int trailer_available(const cJSON *meta, const char *id) {
  char b[24];
  return cJSON_GetArraySize(js_arr(meta, "trailers")) > 0 || cJSON_GetArraySize(js_arr(meta, "trailerStreams")) > 0 || *trailer_imdb_id(meta, id, b, sizeof b);
}

/* ---- captions ------------------------------------------------------------------------------------------ */
typedef struct { void (*cb)(Cue *, int, void *); void *ud; } SubReq;
static void on_sub(int st, const unsigned char *body, size_t len, void *ud) {
  SubReq *r = ud; Cue *c = NULL; int n = 0;
  if (st >= 200 && st < 300 && len) n = subs_parse_any((const char *)body, &c);
  if (st < 0) { free(r); return; }
  r->cb(c, n, r->ud); free(r);
}
unsigned trailer_sub_load(const TrailerSub *s, void (*cb)(Cue *, int, void *), void *ud) {
  SubReq *r = malloc(sizeof *r); r->cb = cb; r->ud = ud;
  return net_get_hdr(s->url, is_yt_host(s->url) ? "User-Agent: " YT_UA "\nAccept: */*" : "User-Agent: " WEB_UA "\nAccept: */*", on_sub, r);
}
