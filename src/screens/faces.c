#include "screens/faces.h"
#include "core/app.h"
#include "core/net.h"
#include "core/json.h"
#include "core/store.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#define MAXN 16 /* (a token carries the index in 4 bits) */
#define MEMN 120
typedef struct { char key[128], id[16], img[600]; } FaceMem;
static FaceMem mem[MEMN]; static int memN;
typedef struct { char name[128], id[16], img[600]; int state; /* 0 asking, 1 err, 2 found (maybe no photo), 3 settled */ } P;
static struct { P p[MAXN]; int n; unsigned gen; FaceCb cb; void *ud; int round, pend; double retryAt; char title[128]; } F; /* title: folded, to find who is known for it */

/* ---- names: lower case, accents folded ("Bagiński" = "baginski") ---- */
static int utf8_next(const unsigned char **s) {
  const unsigned char *p = *s; int c = *p; if (!c) return 0;
  if (c < 0x80) { *s = p + 1; return c; }
  if ((c & 0xe0) == 0xc0 && p[1]) { *s = p + 2; return ((c & 0x1f) << 6) | (p[1] & 0x3f); }
  if ((c & 0xf0) == 0xe0 && p[1] && p[2]) { *s = p + 3; return ((c & 0x0f) << 12) | ((p[1] & 0x3f) << 6) | (p[2] & 0x3f); }
  *s = p + 1; return 0xfffd; }
static int lower_cp(int c) {
  if (c >= 'A' && c <= 'Z') return c + 32;
  if (c >= 0xc0 && c <= 0xde && c != 0xd7) return c + 32;
  if (c >= 0x100 && c <= 0x17f && !(c >= 0x138 && c <= 0x148) && c != 0x178) return (c & 1) ? c : c + 1;
  if (c >= 0x139 && c <= 0x148) return (c & 1) ? c + 1 : c;
  if (c == 0x178) return 0xff;
  return c; }
static const char *FOLD[] = { "a\xc3\xa0\xc3\xa1\xc3\xa2\xc3\xa3\xc3\xa4\xc3\xa5\xc4\x81\xc4\x83\xc4\x85", "c\xc3\xa7\xc4\x87\xc4\x8d", "d\xc4\x8f\xc4\x91",
  "e\xc3\xa8\xc3\xa9\xc3\xaa\xc3\xab\xc4\x93\xc4\x97\xc4\x99\xc4\x9b", "i\xc3\xac\xc3\xad\xc3\xae\xc3\xaf\xc4\xab\xc4\xaf\xc4\xb1", "l\xc5\x82\xc4\xbe\xc4\xba", "n\xc3\xb1\xc5\x84\xc5\x88",
  "o\xc3\xb2\xc3\xb3\xc3\xb4\xc3\xb5\xc3\xb6\xc3\xb8\xc5\x8d\xc5\x91", "r\xc5\x95\xc5\x99", "s\xc5\x9b\xc5\xa1\xc5\x9f", "t\xc5\xa5\xc5\xa3", "u\xc3\xb9\xc3\xba\xc3\xbb\xc3\xbc\xc5\xab\xc5\xaf\xc5\xb1", "y\xc3\xbd\xc3\xbf", "z\xc5\xba\xc5\xbc\xc5\xbe" };
static int fold_cp(int c) {
  for (size_t k = 0; k < sizeof FOLD / sizeof *FOLD; k++) { const unsigned char *s = (const unsigned char *)FOLD[k] + 1; int x; while ((x = utf8_next(&s))) if (x == c) return FOLD[k][0]; }
  return 0; }
/* normName: lower case, anything but letters (Latin, accents too) and digits as one space; fold = 1 also folds the accents away */
static void norm(const char *in, char *out, size_t cap, int fold) {
  const unsigned char *s = (const unsigned char *)in; size_t o = 0; int sp = 0, c;
  while ((c = utf8_next(&s)) && o + 4 < cap) { c = lower_cp(c);
    int keep = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c >= 0xc0 && c <= 0x24f && c != 0xd7 && c != 0xf7);
    if (!keep) { sp = 1; continue; }
    if (sp && o) out[o++] = ' '; sp = 0;
    if (c < 0x80) out[o++] = (char)c;
    else if (fold) { if (c == 0xdf) { out[o++] = 's'; out[o++] = 's'; } else { int f = fold_cp(c); if (f) out[o++] = (char)f; } }
    else if (c < 0x800) { out[o++] = (char)(0xc0 | (c >> 6)); out[o++] = (char)(0x80 | (c & 0x3f)); } }
  out[o] = 0; }
static const char *last_word(const char *folded) { const char *s = strrchr(folded, ' '); return s ? s + 1 : folded; }
static void urlenc(const char *s, char *out, size_t cap) { size_t o = 0; for (; *s && o + 4 < cap; s++) { unsigned char c = *s; if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') out[o++] = c; else o += snprintf(out + o, cap - o, "%%%02X", c); } out[o] = 0; }
static void imdb_img(const char *u, char *out, size_t cap) { /* IMDb's own headshot, at twice the size it is shown */
  const char *v = strstr(u, "._V1_"); size_t l = strlen(u);
  if (v && l >= 4 && !strcmp(u + l - 4, ".jpg")) snprintf(out, cap, "%.*s._V1_QL90_UX400_.jpg", (int)(v - u), u); else snprintf(out, cap, "%s", u); }

static FaceMem *mem_find(const char *key) { for (int i = 0; i < MEMN && i < memN; i++) if (!strcmp(mem[i].key, key)) return &mem[i]; return NULL; }
static void settle(int i) {
  P *p = &F.p[i]; if (p->state == 3) return; p->state = 3;
  char k[128]; norm(p->name, k, sizeof k, 1); if (!mem_find(k)) { FaceMem *m = &mem[memN++ % MEMN]; snprintf(m->key, sizeof m->key, "%s", k); snprintf(m->id, sizeof m->id, "%s", p->id); snprintf(m->img, sizeof m->img, "%s", p->img); }
  { char ck[160], v[700]; snprintf(ck, sizeof ck, "face2:%s", k); int n = snprintf(v, sizeof v, "%s\n%s", p->id, p->img); cache_write(ck, (const unsigned char *)v, (size_t)n); } /* kept on disk: the next visit has every face at once */
  if (F.cb) F.cb(i, p->id, p->img, F.ud); }
#define TOK(i) ((void *)(uintptr_t)(((F.gen & 0xffffff) << 4) | (unsigned)(i)))
#define OK_TOK(ud) ((((unsigned)(uintptr_t)(ud)) >> 4) == (F.gen & 0xffffff))
#define IDX(ud) ((int)((uintptr_t)(ud) & 15))
static void stage_free(void);
static void done_one(void) { if (--F.pend > 0) return; stage_free(); }

/* ---- 3: Wikipedia's photo, when the page says they work in film or television ---- */
static int film_job(const char *d) {
  static const char *W[] = { "actor", "actress", "director", "writer", "screenwriter", "producer", "filmmaker", "comedian", "voice", "animator", "manga", "author", "novelist", "cinematographer", "composer", "television", "presenter", "model", "singer", "musician", "rapper", "athlete", "dancer", "stunt" };
  char low[512]; size_t i = 0; for (; d[i] && i + 1 < sizeof low; i++) low[i] = (d[i] >= 'A' && d[i] <= 'Z') ? d[i] + 32 : d[i]; low[i] = 0;
  for (size_t k = 0; k < sizeof W / sizeof *W; k++) if (strstr(low, W[k])) return 1; return 0; }
static void on_wiki(int st, const unsigned char *body, size_t len, void *ud) {
  (void)len; if (!OK_TOK(ud)) return; int i = IDX(ud); P *p = &F.p[i]; cJSON *j = st == 200 ? net_json(body) : NULL;
  if (!j && st != 404) p->state = 1;
  else if (j && !strcmp(js_str(j, "type", ""), "standard") && film_job(js_str(j, "description", ""))) {
    const char *u = js_str(js_obj(j, "thumbnail"), "source", ""); char b[600]; snprintf(b, sizeof b, "%s", u);
    if (js_num(js_obj(j, "originalimage"), "width", 0) >= 400) { char *px = strstr(b, "px-"); if (px) { char *q = px; while (q > b && q[-1] >= '0' && q[-1] <= '9') q--; if (q > b && q[-1] == '/') { char rest[600]; snprintf(rest, sizeof rest, "%s", px); snprintf(q, sizeof b - (q - b), "400%s", rest); } } }
    if (!strncmp(b, "https://upload.wikimedia.org/", 29)) snprintf(p->img, sizeof p->img, "%s", b); }
  net_json_free(j); if (p->state != 1) settle(i); done_one();
}
static void stage_wiki(void) {
  int n = 0; for (int i = 0; i < F.n; i++) if (F.p[i].state == 2 && !F.p[i].img[0]) n++;
  for (int i = 0; i < F.n; i++) if (F.p[i].state == 2 && F.p[i].img[0]) settle(i);
  if (!n) { stage_free(); return; }
  F.pend = n;
  for (int i = 0; i < F.n; i++) { P *p = &F.p[i]; if (p->state != 2 || p->img[0]) continue; char t[128], e[400], u[512]; snprintf(t, sizeof t, "%s", p->name); for (char *c = t; *c; c++) if (*c == ' ') *c = '_';
    urlenc(t, e, sizeof e); snprintf(u, sizeof u, "https://en.wikipedia.org/api/rest_v1/page/summary/%s", e); net_get(u, on_wiki, TOK(i)); }
}
/* ---- 2: Wikidata's image of each IMDb id, one query for everyone missing ---- */
static void on_commons(int st, const unsigned char *body, size_t len, void *ud) {
  (void)len; if (!OK_TOK(ud)) return; cJSON *j = st == 200 ? net_json(body) : NULL; const cJSON *b = js_arr(js_obj(j, "results"), "bindings");
  for (int i = 0; i < F.n; i++) { P *p = &F.p[i]; if (p->state != 2 || p->img[0] || !p->id[0]) continue;
    if (!b) { p->state = 1; continue; }
    const cJSON *r; cJSON_ArrayForEach(r, b) { if (strcmp(js_str(js_obj(r, "id"), "value", ""), p->id)) continue; const char *u = js_str(js_obj(r, "im"), "value", "");
      const char *fp = strstr(u, "commons.wikimedia.org/wiki/Special:FilePath/"); if (fp && (!strncmp(u, "http://", 7) || !strncmp(u, "https://", 8))) snprintf(p->img, sizeof p->img, "https://%s?width=400", fp); break; } }
  net_json_free(j); stage_wiki();
}
static void stage_commons(void) {
  char vals[600] = ""; size_t o = 0; int n = 0;
  for (int i = 0; i < F.n; i++) { P *p = &F.p[i]; if (p->state == 2 && !p->img[0] && p->id[0] && o + 20 < sizeof vals) { o += snprintf(vals + o, sizeof vals - o, "\"%s\" ", p->id); n++; } }
  if (!n) { stage_wiki(); return; }
  char q[1000]; snprintf(q, sizeof q, "SELECT ?id (SAMPLE(?img) AS ?im) WHERE { VALUES ?id { %s} ?p wdt:P345 ?id . ?p wdt:P18 ?img } GROUP BY ?id", vals);
  char u[3200] = "https://query.wikidata.org/sparql?format=json&query="; size_t l = strlen(u); urlenc(q, u + l, sizeof u - l);
  net_get(u, on_commons, TOK(0));
}
/* ---- 1: IMDb's name suggestions ---- */
static int known_for(const cJSON *e) { if (!F.title[0]) return 0; char s[300]; norm(js_str(e, "s", ""), s, sizeof s, 1); return strstr(s, F.title) != NULL; } /* "Actor, <title> (year)" */
static void on_imdb(int st, const unsigned char *body, size_t len, void *ud) {
  (void)len; if (!OK_TOK(ud)) return; int i = IDX(ud); P *p = &F.p[i];
  cJSON *j = st == 200 ? net_json(body) : NULL; const cJSON *d = js_arr(j, "d");
  if (!d) p->state = 1;
  else { char fn[128]; norm(p->name, fn, sizeof fn, 1); const cJSON *top = NULL, *exact = NULL, *e;
    /* of the people with exactly that name, the one known for this title (IMDb says what each is known for), else the
       most popular; someone with only a like name (same surname and initial) only when known for this title: a
       popular namesake's face and films came up for the actor */
    const cJSON *exactHere = NULL;
    cJSON_ArrayForEach(e, d) { if (strncmp(js_str(e, "id", ""), "nm", 2)) continue; if (!top) top = e; char n2[128]; norm(js_str(e, "l", ""), n2, sizeof n2, 1);
      if (!strcmp(n2, fn)) { if (!exact) exact = e; if (!exactHere && known_for(e)) exactHere = e; } }
    if (exactHere) exact = exactHere;
    int same = 0; if (top) { char tl[128]; norm(js_str(top, "l", ""), tl, sizeof tl, 1); same = !strcmp(last_word(tl), last_word(fn)) && tl[0] == fn[0] && known_for(top); }
    const cJSON *pick = exact ? exact : same ? top : NULL;
    #define HASIMG(x) (*js_str(js_obj(x, "i"), "imageUrl", ""))
    if (pick && !HASIMG(pick) && same && top != pick && HASIMG(top)) pick = top; /* an exact entry without a photo gives way to the plainly same popular one */
    p->id[0] = p->img[0] = 0; if (pick) { snprintf(p->id, sizeof p->id, "%s", js_str(pick, "id", "")); imdb_img(js_str(js_obj(pick, "i"), "imageUrl", ""), p->img, sizeof p->img); }
    p->state = 2; if (p->img[0]) settle(i); } /* a photo from IMDb shows at once, not after the slowest name */
  net_json_free(j); if (--F.pend == 0) stage_commons();
}
static void round_start(void) {
  F.pend = 0; for (int i = 0; i < F.n; i++) if (F.p[i].state == 0) F.pend++;
  if (!F.pend) { stage_commons(); return; }
  int n = F.pend; (void)n;
  for (int i = 0; i < F.n; i++) { P *p = &F.p[i]; if (p->state != 0) continue; char nn[128], enc[400], u[512]; norm(p->name, nn, sizeof nn, 0);
    if (!nn[0]) { p->state = 2; if (--F.pend == 0) stage_commons(); continue; }
    urlenc(nn, enc, sizeof enc); snprintf(u, sizeof u, "https://v3.sg.media-imdb.com/suggestion/x/%s.json", enc); net_get(u, on_imdb, TOK(i)); }
}
/* the end of a round: what failed is asked again later (2.5 s, 5 s, 7.5 s); after three rounds it rests without a photo */
static void stage_free(void) {
  int again = 0; for (int i = 0; i < F.n; i++) if (F.p[i].state == 1) again++;
  if (!again) return;
  if (F.round < 3) { F.retryAt = app_now() + 2.5 * (F.round + 1); return; }
  for (int i = 0; i < F.n; i++) if (F.p[i].state == 1) { F.p[i].img[0] = 0; settle(i); }
}
void faces_tick(void) {
  if (!F.retryAt || app_now() < F.retryAt) return; F.retryAt = 0; F.round++; F.gen++;
  for (int i = 0; i < F.n; i++) if (F.p[i].state == 1) { F.p[i].state = 0; F.p[i].img[0] = 0; }
  round_start();
}
void faces_cancel(void) { F.gen++; F.cb = NULL; F.n = 0; F.retryAt = 0; }
void faces_find(const char *const *names, int n, const char *title, FaceCb cb, void *ud) {
  norm(title ? title : "", F.title, sizeof F.title, 1);
  F.gen++; F.cb = cb; F.ud = ud; F.round = 0; F.retryAt = 0; F.n = n > MAXN ? MAXN : n;
  for (int i = 0; i < F.n; i++) { P *p = &F.p[i]; memset(p, 0, sizeof *p); snprintf(p->name, sizeof p->name, "%s", names[i]); }
  for (int i = 0; i < F.n; i++) { char k[128]; norm(F.p[i].name, k, sizeof k, 1); FaceMem *m = mem_find(k);
    if (m) { snprintf(F.p[i].id, sizeof F.p[i].id, "%s", m->id); snprintf(F.p[i].img, sizeof F.p[i].img, "%s", m->img); F.p[i].state = 3; if (cb) cb(i, m->id, m->img, ud); continue; }
    char ck[160]; snprintf(ck, sizeof ck, "face2:%s", k); size_t len = 0; char *t = (char *)cache_read(ck, 30 * 86400, &len); /* a few hundred bytes each */
    if (t) { char *nl = strchr(t, '\n'); if (nl) { *nl = 0; snprintf(F.p[i].id, sizeof F.p[i].id, "%s", t); snprintf(F.p[i].img, sizeof F.p[i].img, "%s", nl + 1); F.p[i].state = 3;
        FaceMem *mm = &mem[memN++ % MEMN]; snprintf(mm->key, sizeof mm->key, "%s", k); snprintf(mm->id, sizeof mm->id, "%s", F.p[i].id); snprintf(mm->img, sizeof mm->img, "%s", F.p[i].img); if (cb) cb(i, F.p[i].id, F.p[i].img, ud); } free(t); } }
  round_start();
}
