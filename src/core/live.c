#include "core/live.h"
#include "core/app.h"
#include "core/i18n.h"
#include "core/json.h"
#include "core/net.h"
#include "core/store.h"
#include "screens/player.h"
#include "screens/livehub.h"
#include "screens/livepeek.h"
#include "platform/plat.h"
#include <SDL2/SDL.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int live_type(const char *type) { return type && (!strcmp(type, "tv") || !strcmp(type, "channel")); }

/* ---- the channels: every live catalog's list ---- */
#define MAXCAT 64
/* a channel kept small (thousands of them: a full card's fields would be megabytes), made into a card when it is used */
static struct { CatalogDef def; LiveCh *ch; int n, cap, skip; } cats[MAXCAT]; /* skip: where the next page starts */ static int ncat; static unsigned catGen; static double catAt = -1e9;
static double now_s(void) { return SDL_GetTicks() / 1000.0; }
#define MAXCH 1000 /* channels kept per catalog */
static void ch_of(const Item *it, LiveCh *c) { snprintf(c->id, sizeof c->id, "%s", it->id); snprintf(c->type, sizeof c->type, "%s", it->type); snprintf(c->name, sizeof c->name, "%s", it->name);
  snprintf(c->logo, sizeof c->logo, "%s", it->logo); snprintf(c->poster, sizeof c->poster, "%s", it->poster[0] ? it->poster : it->background); }
void live_item(const LiveCh *c, Item *it) { memset(it, 0, sizeof *it); snprintf(it->id, sizeof it->id, "%s", c->id); snprintf(it->type, sizeof it->type, "%s", c->type[0] ? c->type : "tv"); snprintf(it->name, sizeof it->name, "%s", c->name);
  snprintf(it->logo, sizeof it->logo, "%s", c->logo); snprintf(it->poster, sizeof it->poster, "%s", c->poster); snprintf(it->background, sizeof it->background, "%s", c->poster); }
static void *ctok(int k) { return (void *)(uintptr_t)(((uintptr_t)(catGen & 0x3ffffff) << 6) | (uintptr_t)k); }
static int cok(void *ud, int *k) { uintptr_t t = (uintptr_t)ud; *k = (int)(t & 63); return (unsigned)(t >> 6) == (catGen & 0x3ffffff) && *k < ncat; }
static int cat_add(int i, const ItemList *l) { /* the answer's channels, each once; how many were new */
  int add = 0; if (cats[i].n + l->n > cats[i].cap) { int nc = cats[i].n + l->n + 64; if (nc > MAXCH) nc = MAXCH; LiveCh *g = realloc(cats[i].ch, sizeof(LiveCh) * (size_t)nc); if (!g) return 0; cats[i].ch = g; cats[i].cap = nc; }
  /* each once: the ids already there in a hash (every one against every one was 250 000 comparisons for a page of 500) */
  int hn = 64; while (hn < (cats[i].n + l->n) * 2) hn <<= 1; int *H = malloc(sizeof(int) * (size_t)hn); if (!H) return 0; for (int k = 0; k < hn; k++) H[k] = -1;
  #define CH_H(s) ({ unsigned hh = 2166136261u; for (const char *q = (s); *q; q++) hh = (hh ^ (unsigned char)*q) * 16777619u; (int)(hh & (unsigned)(hn - 1)); })
  for (int b = 0; b < cats[i].n; b++) { int h = CH_H(cats[i].ch[b].id); while (H[h] >= 0) h = (h + 1) & (hn - 1); H[h] = b; }
  for (int a = 0; a < l->n && cats[i].n < cats[i].cap; a++) { const char *id = l->items[a].id; if (!id[0]) continue;
    int h = CH_H(id), dup = 0; while (H[h] >= 0) { if (!strcmp(cats[i].ch[H[h]].id, id)) { dup = 1; break; } h = (h + 1) & (hn - 1); }
    if (dup) continue; H[h] = cats[i].n; ch_of(&l->items[a], &cats[i].ch[cats[i].n++]); add++; }
  #undef CH_H
  free(H); return add; }
static void on_page(ItemList *l, int more, void *ud) { /* the next page of a catalog that pages: every channel of it, not its first page alone */
  int i; if (!cok(ud, &i) || !l) { if (l) itemlist_free(l); return; }
  int got = l->total > l->n ? l->total : l->n, add = cat_add(i, l); itemlist_free(l); cats[i].skip += got;
  if (got && add && more != 0 && cats[i].n < MAXCH) sb_catalog_page(&cats[i].def, cats[i].skip, on_page, ctok(i));
  else fprintf(stderr, "[live] %s: %d channels\n", cats[i].def.title, cats[i].n);
}
static void on_cat(ItemList *l, int cached, void *ud) {
  int i; if (!cok(ud, &i) || !l) { if (l) itemlist_free(l); return; }
  if (!l->n) { if (l->total == -2) cats[i].n = 0; itemlist_free(l); return; } /* (an empty answer leaves what was there; a list the addon no longer serves goes) */
  int got = l->total > l->n ? l->total : l->n; cats[i].n = 0; cat_add(i, l); itemlist_free(l); cats[i].skip = got;
  (void)cached; if (cats[i].def.skip && got) sb_catalog_page(&cats[i].def, cats[i].skip, on_page, ctok(i)); /* its next pages (the cached copy too: a fresh one comes only when it differs) */
}
void live_refresh(int force) {
  static CatalogDef all[128]; static unsigned lastSig; int n = sb_home_defs(all, 128), k = 0;
  unsigned sig = 2166136261u; for (int i = 0; i < n; i++) if (live_type(all[i].type)) for (const char *p = all[i].url; *p; p++) sig = (sig ^ (unsigned char)*p) * 16777619u; /* (the live catalogs the addons offer now) */
  if (!force && now_s() - catAt < 600 && sig == lastSig) return; /* (asked again every 10 minutes, or at once when the addons changed: one turned off or removed takes its channels with it) */
  lastSig = sig; catAt = now_s(); catGen++;
  for (int i = 0; i < n && k < MAXCAT; i++) if (live_type(all[i].type)) {
    if (k >= ncat || strcmp(cats[k].def.url, all[i].url)) cats[k].n = 0;
    cats[k].def = all[i]; if (ncat < k + 1) ncat = k + 1; /* (a cached answer comes back at once, inside the call) */ sb_catalog_url(all[i].url, all[i].type, 500, on_cat, ctok(k)); k++; }
  for (int i = k; i < ncat; i++) { free(cats[i].ch); memset(&cats[i], 0, sizeof cats[i]); }
  ncat = k;
}
int live_catalogs(void) { return ncat; }
int live_catalog(int i, const char **title, const LiveCh **ch) { if (i < 0 || i >= ncat) return -1; if (title) *title = cats[i].def.title; if (ch) *ch = cats[i].ch; return cats[i].n; }

/* ---- recent and pinned (kept on this TV: liveRecent, livePins; each an entry with what a card needs) ---- */
static cJSON *entry(const Item *it) { cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "id", it->id); cJSON_AddStringToObject(o, "type", it->type); cJSON_AddStringToObject(o, "name", it->name);
  cJSON_AddStringToObject(o, "poster", it->poster); cJSON_AddStringToObject(o, "background", it->background); cJSON_AddStringToObject(o, "logo", it->logo); cJSON_AddNumberToObject(o, "at", (double)time(NULL)); return o; }
static void from_entry(const cJSON *o, Item *it) { memset(it, 0, sizeof *it); snprintf(it->id, sizeof it->id, "%s", js_str(o, "id", "")); snprintf(it->type, sizeof it->type, "%s", js_str(o, "type", "tv"));
  snprintf(it->name, sizeof it->name, "%s", js_str(o, "name", "")); snprintf(it->poster, sizeof it->poster, "%s", js_str(o, "poster", "")); snprintf(it->background, sizeof it->background, "%s", js_str(o, "background", "")); snprintf(it->logo, sizeof it->logo, "%s", js_str(o, "logo", "")); }
static cJSON *list_of(const char *key) { cJSON *a = store_node(key); return cJSON_IsArray(a) ? a : NULL; }
static int index_in(const cJSON *a, const char *id) { int i = 0; const cJSON *o; cJSON_ArrayForEach(o, a) { if (!strcmp(js_str(o, "id", ""), id)) return i; i++; } return -1; }
static void put_first(const char *key, const Item *it, int max) { /* the entry to the front, once */
  cJSON *a = list_of(key); cJSON *n = cJSON_CreateArray(); cJSON_AddItemToArray(n, entry(it)); int k = 1; const cJSON *o;
  if (a) cJSON_ArrayForEach(o, a) { if (k >= max) break; if (strcmp(js_str(o, "id", ""), it->id)) { cJSON_AddItemToArray(n, cJSON_Duplicate(o, 1)); k++; } }
  store_set_node(key, n); }
static void take_out(const char *key, const char *id) { cJSON *a = list_of(key); if (!a) return; int i = index_in(a, id); if (i < 0) return; cJSON *n = cJSON_Duplicate(a, 1); cJSON_DeleteItemFromArray(n, i); store_set_node(key, n); }
int live_pinned(const char *id) { cJSON *a = list_of("livePins"); return a && index_in(a, id) >= 0; }
void live_pin(const Item *it, int on) { if (on) put_first("livePins", it, 30); else take_out("livePins", it->id); }
void live_forget(const char *id) { take_out("liveRecent", id); take_out("livePins", id); }
static void played(const Item *it) { put_first("liveRecent", it, 30); }
int live_used(void) {
  cJSON *p = list_of("livePins"); if (p && cJSON_GetArraySize(p)) return 1;
  cJSON *r = list_of("liveRecent"); double cut = (double)time(NULL) - 30 * 86400.0; const cJSON *o; if (r) cJSON_ArrayForEach(o, r) if (js_num(o, "at", 0) >= cut) return 1;
  return 0;
}
static int has_id(const ItemList *l, const char *id) { for (int i = 0; i < l->n; i++) if (!strcmp(l->items[i].id, id)) return 1; return 0; }
/* a channel's name as one: lower case, spaces and dots out, a quality tag at its end (HD, FHD, UHD, 4K, SD) left off */
static void name_key(const char *n, char *k, size_t cap) { size_t o = 0; for (; *n && o + 1 < cap; n++) { unsigned char c = (unsigned char)*n; if (c == ' ' || c == '.' || c == '-' || c == '_') continue; k[o++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c); } k[o] = 0;
  static const char *const tag[] = { "fhd", "uhd", "hd", "4k", "sd" }; for (int i = 0; i < 5; i++) { size_t l = strlen(tag[i]); if (o > l && !strcmp(k + o - l, tag[i])) { k[o - l] = 0; break; } } }
static int has_name(const ItemList *l, int from, const char *name) { char a[96], b[96]; name_key(name, a, sizeof a); for (int i = from; i < l->n; i++) { name_key(l->items[i].name, b, sizeof b); if (!strcmp(a, b)) return 1; } return 0; }
int live_pins(ItemList *out) { memset(out, 0, sizeof *out); cJSON *p = list_of("livePins"); int n = p ? cJSON_GetArraySize(p) : 0; out->items = calloc((size_t)(n ? n : 1), sizeof(Item)); if (!out->items) return 0; const cJSON *o; if (p) cJSON_ArrayForEach(o, p) from_entry(o, &out->items[out->n++]); out->total = out->n; return out->n; }
int live_recent(ItemList *out) { memset(out, 0, sizeof *out); cJSON *r = list_of("liveRecent"); int n = r ? cJSON_GetArraySize(r) : 0; out->items = calloc((size_t)(n ? n : 1), sizeof(Item)); if (!out->items) return 0; const cJSON *o; double cut = (double)time(NULL) - 30 * 86400.0;
  if (r) cJSON_ArrayForEach(o, r) if (js_num(o, "at", 0) >= cut) from_entry(o, &out->items[out->n++]); out->total = out->n; return out->n; }
/* Home's live row, whenever a live addon is installed: All channels first, the pinned ones beside it, then a few more
   (the recent ones, then the first of each catalog) */
#define BAND_MORE 10
#define BAND_MAX 15
int live_band(ItemList *out) {
  memset(out, 0, sizeof *out); live_refresh(0); if (!ncat) return 0; /* (no live addon on: no row, whatever was played or pinned before) */
  ItemList p, r; live_pins(&p); live_recent(&r); int cap = 1 + BAND_MAX; /* 15 channels at most: the favourites, then the recent, then some to discover */
  out->items = calloc((size_t)cap, sizeof(Item)); if (!out->items) { itemlist_clear(&p); itemlist_clear(&r); return 0; }
  Item *a = &out->items[out->n++]; snprintf(a->id, sizeof a->id, "%s", LIVE_ALL_ID); snprintf(a->type, sizeof a->type, "tv"); snprintf(a->name, sizeof a->name, "%s", T("All channels"));
  for (int i = 0; i < p.n && out->n < cap; i++) out->items[out->n++] = p.items[i];
  int pinsEnd = out->n; for (int i = 0; i < r.n && out->n < cap; i++) if (!has_id(out, r.items[i].id) && !has_name(out, 1, r.items[i].name)) { out->items[out->n++] = r.items[i]; } (void)pinsEnd; /* (each channel once; the pinned are there as pinned) */
  for (int k = 0; out->n < cap && k < 40; k++) for (int c = 0; c < ncat && out->n < cap; c++) if (k < cats[c].n && !has_id(out, cats[c].ch[k].id) && !has_name(out, 1, cats[c].ch[k].name)) { live_item(&cats[c].ch[k], &out->items[out->n++]); } /* (the catalogs take turns; a channel already there, by its name, left out) */
  itemlist_clear(&p); itemlist_clear(&r); out->total = out->n; return out->n;
}
/* ---- the line-up (kept small, as the catalogs) ---- */
static LiveCh *lineup; static int nline, curLine = -1; static LiveCh prevCh; static int hasPrev;
static void line_own(LiveCh *c, int n, const char *id) { free(lineup); lineup = c; nline = n; curLine = -1; for (int i = 0; i < nline; i++) if (!strcmp(lineup[i].id, id)) { curLine = i; break; } }
static void lineup_set(const Item *from, int n, const char *id) {
  if (from && n > 0) { LiveCh *c = malloc(sizeof(LiveCh) * (size_t)n); if (!c) return; int k = 0; for (int i = 0; i < n; i++) if (strcmp(from[i].id, LIVE_ALL_ID)) ch_of(&from[i], &c[k++]); line_own(c, k, id); }
  else { curLine = -1; for (int i = 0; i < nline; i++) if (!strcmp(lineup[i].id, id)) { curLine = i; break; } }
  if (curLine >= 0) return;
  /* not in the kept one: the band, else the catalog that lists it */
  ItemList b; if (live_band(&b)) { int in = 0; for (int i = 0; i < b.n; i++) if (!strcmp(b.items[i].id, id)) in = 1; if (in) { lineup_set(b.items, b.n, id); itemlist_clear(&b); return; } itemlist_clear(&b); }
  for (int c = 0; c < ncat; c++) for (int i = 0; i < cats[c].n; i++) if (!strcmp(cats[c].ch[i].id, id)) { LiveCh *k = malloc(sizeof(LiveCh) * (size_t)cats[c].n); if (k) { memcpy(k, cats[c].ch, sizeof(LiveCh) * (size_t)cats[c].n); line_own(k, cats[c].n, id); } return; }
}
int live_lineup_n(void) { return nline; }
const LiveCh *live_lineup_item(int i) { return i >= 0 && i < nline ? &lineup[i] : NULL; }
int live_lineup_cur(void) { return curLine; }

/* ---- starting a channel: its streams tried in order on a thread of their own ---- */
#define MAXS 24
#define GIVEUP 8.0
static struct {
  unsigned tok; Item it; int zap, peek, art, fo; double t0; /* fo: the stream playing failed, the next one of the channel is being found */ int asked, done; /* done: every addon answered; peek: for Home's preview, nothing opens */
  pthread_mutex_t mu; Stream s[MAXS]; int ns, next, busy, found; /* found: the index that works, +1 */
} L = { .mu = PTHREAD_MUTEX_INITIALIZER };
static LiveCard card; static Item cardIt;
/* switching over a picture playing: found, the old one fades to black (0.16 s) and only then the new one loads */
static struct { int on; Stream s; Item it; double at; } SW;
static void switch_to(const Stream *s, const Item *it) { SW.on = 1; SW.s = *s; SW.it = *it; SW.at = SDL_GetTicks() / 1000.0 + 0.16; player_zap_begin(); }
static int waitPic; static double waitAt; /* waitPic: handed to the player; the card tunes on until the picture runs */
static void switch_tick(void) { if (!SW.on || SDL_GetTicks() / 1000.0 < SW.at) return; SW.on = 0; player_quiet_next(); app_play(&SW.s, SW.it.type, SW.it.id, SW.it.id, 0); player_title_hint(&SW.it); hub_playing(SW.it.id); card.tuning = 1; waitPic = 1; waitAt = SDL_GetTicks() / 1000.0; } /* (the pill tunes on until the new picture runs) */
static struct { char id[SB_ID]; int st; Stream s; } PK;
/* what was last found of a channel's streams (1 one sends a picture, -1 none does), kept ten minutes */
static struct { char id[SB_ID]; int st; double at; } RC[128]; static int rcNext;
/* the stream that sent a picture, per channel (the background check's, the preview's, the player's): a preview asked
   for a channel found live opens that one at once instead of trying its streams again from the first (dead ones ahead of
   it ran the search out of time: "off air" on a channel that plays). Kept 3 minutes, as the live mark */
static struct { char id[SB_ID]; Stream s; double at; } FS[24]; static int fsNext;
static void found_set(const char *id, const Stream *st) { int k = -1; for (int i = 0; i < 24; i++) if (!strcmp(FS[i].id, id)) { k = i; break; } if (k < 0) { k = fsNext; fsNext = (fsNext + 1) % 24; }
  snprintf(FS[k].id, sizeof FS[k].id, "%s", id); FS[k].s = *st; FS[k].at = now_s(); }
static int found_get(const char *id, Stream *out) { for (int i = 0; i < 24; i++) if (FS[i].id[0] && !strcmp(FS[i].id, id)) { if (now_s() - FS[i].at > 180) return 0; *out = FS[i].s; return 1; } return 0; }
void live_found_drop(const char *id) { if (id) for (int i = 0; i < 24; i++) if (!strcmp(FS[i].id, id)) FS[i].id[0] = 0; }
static void reach_set(const char *id, int st) { int k = -1; for (int i = 0; i < 128; i++) if (!strcmp(RC[i].id, id)) { k = i; break; } if (k < 0) { k = rcNext; rcNext = (rcNext + 1) % 128; }
  snprintf(RC[k].id, sizeof RC[k].id, "%s", id); RC[k].st = st; RC[k].at = now_s(); } /* the preview's channel: 0 looking, 1 found (s), -1 none sends a picture */
const LiveCard *live_card(void) { return &card; }
typedef struct { unsigned char b[2048]; size_t n; int status; } Probe;
static int probe_sink(const unsigned char *p, size_t n, void *ud) { Probe *r = ud; if (!p) return 1; size_t k = n < sizeof r->b - 1 - r->n ? n : sizeof r->b - 1 - r->n; memcpy(r->b + r->n, p, k); r->n += k; r->b[r->n] = 0; return r->n < 1500; }
static void probe_hdr(const char *line, void *ud) { Probe *r = ud; int s; if (!strncmp(line, "HTTP/", 5) && sscanf(line, "HTTP/%*s %d", &s) == 1) r->status = s; }
static int fetch(const char *url, const char *hdrs, Probe *r) { memset(r, 0, sizeof *r); double ft0 = now_s(); int st = net_pipe_sync(url, hdrs, 0, probe_hdr, probe_sink, r, 4);
  { static int lg = -1; if (lg < 0) lg = getenv("SB_LIVELOG") != NULL; if (lg) { char h[80] = ""; const char *p = strstr(url, "://"); if (p) { p += 3; size_t k = strcspn(p, "/:?"); if (k > 70) k = 70; memcpy(h, p, k); h[k] = 0; } fprintf(stderr, "[livelog] %s: st %d http %d, %d bytes, %.1f s\n", h, st, r->status, (int)r->n, now_s() - ft0); } } /* (development) */ if (st > 0) r->status = st; return r->n > 0 && (r->status == 200 || r->status == 206 || (st < 0 && (r->status == 0 || r->status == 200 || r->status == 206))); }
static void resolve(const char *base, const char *ref, char *out, size_t cap) {
  if (strstr(ref, "://")) { snprintf(out, cap, "%s", ref); return; } const char *q = strpbrk(base, "?#"), *e = q ? q : base + strlen(base), *sl = e;
  if (ref[0] == '/') { const char *h = strstr(base, "://"); const char *p = h ? strchr(h + 3, '/') : NULL; snprintf(out, cap, "%.*s%s", p ? (int)(p - base) : (int)strlen(base), base, ref); return; }
  while (sl > base && sl[-1] != '/') sl--; snprintf(out, cap, "%.*s%s", (int)(sl - base), base, ref); }
/* a media playlist plays here: no DRM the TV cannot open, not a stand-in loop, and its first segment answers with
   media (a transport stream, fragmented MP4, AAC); a playlist that loads while its segments are refused (a region,
   an expired token) is caught here, not in the player */
static int seg_ok(const char *pl, const char *plUrl, const char *hdrs) {
  if (strstr(pl, "METHOD=SAMPLE-AES") || strstr(pl, "com.widevine") || strstr(pl, "com.microsoft.playready") || strstr(pl, "com.apple.streamingkeydelivery")) return 0;
  const char *l = strstr(pl, "#EXTINF"); if (!l) return strstr(pl, "#EXT-X-TARGETDURATION") != NULL;
  const char *p = strchr(l, '\n'); while (p && (p[1] == '#' || p[1] == '\r' || p[1] == '\n')) p = strchr(p + 1, '\n'); if (!p) return 1; /* (past what was read: the playlist alone) */
  char v[1024], abs[SB_URL_MAX]; size_t k = strcspn(p + 1, "\r\n"); if (!k || k >= sizeof v) return 1; memcpy(v, p + 1, k); v[k] = 0;
  if (strcasestr(v, "offline") || strcasestr(v, "not_available") || strcasestr(v, "notavailable") || strcasestr(v, "geoblock") || strcasestr(v, "unavailable")) return 0; /* a "not on now" loop */
  resolve(plUrl, v, abs, sizeof abs);
  Probe r; if (!fetch(abs, hdrs, &r)) return 0;
  const unsigned char *d = r.b; size_t n = r.n;
  if (d[0] == 0x47 || (n > 188 && d[188] == 0x47)) return 1;                                  /* MPEG-TS */
  if (n >= 3 && !memcmp(d, "ID3", 3)) return 1;                                                 /* AAC behind an ID3 tag */
  if (n >= 2 && d[0] == 0xff && (d[1] & 0xf0) == 0xf0) return 1;                                 /* ADTS */
  if (memmem(d, n, "ftyp", 4) || memmem(d, n, "styp", 4) || memmem(d, n, "moof", 4) || memmem(d, n, "sidx", 4)) return 1; /* fragmented MP4 */
  return !(d[0] == '<' || d[0] == '{' || d[0] == '['); /* (a page or an error in its place: no) */
}
/* a stream sends a picture: a playlist (a master one: its first variant) whose first segment is media, or a body that
   is no web page or error text */
static int works(const Stream *s) {
  if (!strncmp(s->url, "ytid:", 5)) return 1; /* (YouTube: played by the TV's own way) */
  if (strncmp(s->url, "http", 4)) return 0;
  const char *hd = s->hdrs[0] ? s->hdrs : NULL;
  Probe r; if (!fetch(s->url, hd, &r)) return 0;
  const char *b = (const char *)r.b; while (*b == ' ' || *b == '\r' || *b == '\n' || (unsigned char)*b == 0xef || (unsigned char)*b == 0xbb || (unsigned char)*b == 0xbf) b++;
  if (!strncmp(b, "#EXTM3U", 7)) {
    if (strstr(b, "#EXTINF") || strstr(b, "#EXT-X-TARGETDURATION")) return seg_ok(b, s->url, hd);
    if (strstr(b, "SAMPLE-AES") || strstr(b, "com.widevine")) return 0;
    const char *inf = strstr(b, "#EXT-X-STREAM-INF"); if (!inf) return 0; const char *l = strchr(inf, '\n'); while (l && (l[1] == '#' || l[1] == '\r' || l[1] == '\n')) l = strchr(l + 1, '\n'); if (!l) return 0;
    char v[1024], abs[SB_URL_MAX]; size_t k = strcspn(l + 1, "\r\n"); if (k >= sizeof v) return 0; memcpy(v, l + 1, k); v[k] = 0; resolve(s->url, v, abs, sizeof abs);
    Probe r2; if (!fetch(abs, hd, &r2)) return 0; const char *b2 = (const char *)r2.b;
    return (strstr(b2, "#EXTINF") || strstr(b2, "#EXT-X-TARGETDURATION")) && seg_ok(b2, abs, hd); }
  if (*b == '<' || *b == '{' || *b == '[') return 0; /* a page, an error in JSON */
  return 1;
}
/* a preview's streams that loaded and never showed a picture (this TV's player does not start some): the next try
   for the preview passes over them, as the player's failover does (a channel the preview called off air played on OK) */
static unsigned pkSkip[4]; static int npkSkip, pkKeep; static char pkSkipId[SB_ID];
static unsigned url_h(const char *u) { unsigned h = 2166136261u; for (; *u; u++) h = (h ^ (unsigned char)*u) * 16777619u; return h ? h : 1; }
static int pk_skipped(const char *u) { unsigned h = url_h(u); for (int i = 0; i < npkSkip; i++) if (pkSkip[i] == h) return 1; return 0; }
/* streams with the ads put in by the server (Google DAI, Pluto's stitcher, Uplynk): webOS 3/4's player loads them and
   never starts (measured on a 43LK5900, webOS 4.4): there they are tried after the channel's other streams, so a
   channel with a plain stream shows it first (CBS: DAI and Pluto first, its own stream third) */
static int ssai_last(void) {
#ifdef SB_WEBOS
  static int v = -1; if (v < 0) { v = 0; FILE *f = fopen("/var/run/nyx/os_info.json", "r"); char b[8192] = ""; if (f) { size_t n = fread(b, 1, sizeof b - 1, f); b[n] = 0; fclose(f); }
    const char *r = strstr(b, "\"webos_release\""); if (r && (r = strchr(r + 15, '"'))) { int major = atoi(r + 1); v = major > 0 && major < 5; } } return v;
#else
  return 0;
#endif
}
static int ssai(const char *u) { if (!ssai_last()) return 0; const char *h = strstr(u, "://"); if (!h) return 0; h += 3; char host[128]; size_t k = strcspn(h, "/:?"); if (k >= sizeof host) return 0; memcpy(host, h, k); host[k] = 0;
  size_t hl = strlen(host); return !strcmp(host, "dai.google.com") || (strstr(host, "stitcher") && strstr(host, "pluto.tv")) || (hl >= 10 && !strcmp(host + hl - 10, "uplynk.com")) || !strncmp(h, "jmp2.uk/plu-", 12); }
/* a found stream into a channel's list (its lock held): on webOS 3/4 a plain one goes before the untried ad-inserted ones */
static void add_stream(Stream *a, int *ns, int max, int next, const Stream *x) { if (*ns >= max) return;
  int at = *ns; if (!ssai(x->url)) for (int i = next; i < *ns; i++) if (ssai(a[i].url)) { at = i; break; }
  for (int i = *ns; i > at; i--) a[i] = a[i - 1]; a[at] = *x; (*ns)++; }
static void kick(void);
static void *probe_run(void *arg) {
  unsigned tok = (unsigned)(uintptr_t)arg; plat_thread_background(5);
  for (;;) {
    pthread_mutex_lock(&L.mu); if (L.tok != tok || L.found) { int stale = L.tok != tok; L.busy = 0; pthread_mutex_unlock(&L.mu); if (stale) kick(); return NULL; } /* (a newer search asked while this one was busy: it starts now, it was never started) */
    if (L.next >= L.ns) { L.busy = 0; pthread_mutex_unlock(&L.mu); return NULL; }
    int i = L.next++; Stream s = L.s[i]; int skip = L.peek && pk_skipped(s.url); pthread_mutex_unlock(&L.mu);
    if (skip) { fprintf(stderr, "[live] stream %d of the channel: passed over (no picture in the preview)\n", i + 1); continue; }
    int ok = works(&s);
    pthread_mutex_lock(&L.mu); if (L.tok == tok && ok && !L.found) L.found = i + 1; pthread_mutex_unlock(&L.mu);
    fprintf(stderr, "[live] stream %d of the channel: %s\n", i + 1, ok ? "sends a picture" : "no picture");
  }
}
static void kick(void) { pthread_mutex_lock(&L.mu); int go = !L.busy && !L.found && L.next < L.ns; if (go) L.busy = 1; unsigned tok = L.tok; pthread_mutex_unlock(&L.mu);
  if (go) { pthread_t th; if (pthread_create(&th, NULL, probe_run, (void *)(uintptr_t)tok)) { pthread_mutex_lock(&L.mu); L.busy = 0; pthread_mutex_unlock(&L.mu); } else pthread_detach(th); } }
static void on_part(StreamGroup *g, int gi, void *ud) {
  (void)gi; if ((unsigned)(uintptr_t)ud != L.tok) { free(g->items); return; }
  pthread_mutex_lock(&L.mu); for (int i = 0; i < g->n && L.ns < MAXS; i++) { const Stream *s = &g->items[i]; if (!s->url[0] || s->ext[0] || s->p2p) continue; add_stream(L.s, &L.ns, MAXS, L.next, s); } pthread_mutex_unlock(&L.mu);
  free(g->items); kick();
}
static void on_done(int total, void *ud) { (void)total; if ((unsigned)(uintptr_t)ud == L.tok) L.done = 1; }
void live_cancel(void) { pthread_mutex_lock(&L.mu); L.tok++; pthread_mutex_unlock(&L.mu); L.asked = 0; if (card.tuning) { card.tuning = 0; card.on = 0; } }
/* from a card: the player's art comes up at once (the channel's logo on its own colours) while its streams are tried */
int (*live_remote_on)(void); void (*live_remote_play)(const Item *it); void (*live_fail_hook)(const char *id, const char *code, int back);
static void failed(const char *id, const char *code, int back) { if (live_fail_hook) live_fail_hook(id, code, back); }
static int remote(void) { return live_remote_on && live_remote_on(); }
static void art_up(const Item *it) { if (remote()) return; /* (casting: it plays on the TV, nothing opens here) */ player_prepare(it->type, it->id, it->id, NULL); player_title_hint(it); player_status(T("Tuning in…")); L.art = 1; }
void live_start(const Item *it, const Item *from, int nfrom, int zap) {
  if (!it || !it->id[0]) return;
  if (!strcmp(it->id, LIVE_ALL_ID)) return; /* (the caller opens the page) */
  if (live_reach(it->id) < 0) { toast(T("This channel isn't broadcasting right now"), 2600, LIVE_TOAST); reach_set(it->id, 0); fprintf(stderr, "[live] off air (found before): said, not tried\n"); failed(it->id, "offair", 0); return; } /* (pressed again: tried again) */
  live_refresh(0); lineup_set(from, nfrom, it->id);
  if (remote() && live_remote_play) { played(it); for (int i = 0; i < nline; i++) if (!strcmp(lineup[i].id, it->id)) curLine = i; fprintf(stderr, "[live] to the TV: %s\n", it->name); live_remote_play(it); return; } /* (casting: the TV finds its stream and plays it in its channel player; nothing looked for here) */
  if (PK.st == 1 && !strcmp(PK.id, it->id)) { /* Home's preview found it already: it opens at once */
    L.asked = 0; PK.id[0] = 0; played(it); for (int i = 0; i < nline; i++) if (!strcmp(lineup[i].id, it->id)) curLine = i;
    cardIt = *it; card.it = &cardIt; card.on = 1; card.failed = 0; card.backHint = 0; card.at = now_s();
    Stream s = PK.s; fprintf(stderr, "[live] the preview's stream opens\n");
    card.tuning = 1; if (zap && player_open()) { switch_to(&s, it); return; } if (!zap) { float r[5]; char src[SB_URL_MAX]; if (lpk_fullscreen(it->id) && !remote() && lpk_handoff(it->id, src, sizeof src, r) && !strcmp(src, s.url)) player_adopt_next(src, r[0], r[1], r[2], r[3], r[4]); /* full screen behind All channels: it carries on as it plays (no reload, no grow) */
      else { extern void lpk_release(void); lpk_release(); art_up(it); } } L.art = 0; /* (a box: the player's own loader, as any channel; the box no longer grows into the player: 2026-11-08, the user) */ waitPic = 1; waitAt = now_s(); app_play(&s, it->type, it->id, it->id, 0); player_title_hint(it); hub_playing(it->id); return; }
  if (L.asked && L.peek && !strcmp(L.it.id, it->id)) { /* the preview is still looking for it: that search goes on, for the player now */
    L.peek = 0; L.zap = zap; PK.id[0] = 0; cardIt = *it; card.it = &cardIt; card.on = 1; card.tuning = 1; card.failed = 0; card.backHint = 0; card.at = now_s();
    L.art = 0; if (!zap) art_up(it); return; }
  PK.id[0] = 0;
  pthread_mutex_lock(&L.mu); L.tok++; L.ns = L.next = L.found = 0; unsigned tok = L.tok; pthread_mutex_unlock(&L.mu);
  L.it = *it; L.zap = zap; L.peek = 0; L.art = 0; L.t0 = now_s(); L.asked = 1; L.done = 0; /* (switching: the channel playing plays on while this one is found) */
  cardIt = *it; card.it = &cardIt; card.on = 1; card.tuning = 1; card.failed = 0; card.backHint = 0; card.at = now_s();
  if (!zap) art_up(it);
  fprintf(stderr, "[live] tuning in to a channel (%s)\n", zap ? "switching" : "from a card");
  sb_streams(it->type, it->id, on_part, on_done, (void *)(uintptr_t)tok);
}
static void probe_tick(void);
/* each live list asked once a launch even inside the addon's cacheMaxAge (a server that went away no longer leaves
   its channels): from 15 s after the first frame, one every 2 s, never at the start's memory peak */
static void recheck_tick(void) { static double t0, lastAt; static int next; double now = now_s(); if (!t0) t0 = now;
  if (now - t0 < 15 || now - lastAt < 2 || next >= ncat) return;
  { extern int hub_on(void); extern float app_idle(void); if (player_open() || hub_on() || app_idle() < 3) return; } /* (only when nothing moves: a list's items are 100-450 ms of work on a TV, never under the grid, a preview or a film) */
  lastAt = now; if (cats[next].def.url[0]) sb_catalog_recheck(cats[next].def.url, cats[next].def.type, 500, on_cat, ctok(next)); next++; }
void live_tick(void) { recheck_tick();
  { static int lg = -1; static double at; if (lg < 0) lg = getenv("SB_LIVELOG") != NULL; if (lg && now_s() - at > 2) { at = now_s(); pthread_mutex_lock(&L.mu); fprintf(stderr, "[livelog] search: asked %d peek %d streams %d next %d found %d busy %d done %d tok %u | preview %d\n", L.asked, L.peek, L.ns, L.next, L.found, L.busy, L.done, L.tok, PK.st); pthread_mutex_unlock(&L.mu); } } /* (development) */
  probe_tick(); switch_tick();
  if (waitPic) { if (player_live() || !player_open() || now_s() - waitAt > 15) { waitPic = 0; card.tuning = 0; card.at = now_s(); } } /* the picture runs: tuned */
  if (!L.asked) { if (card.on && !card.tuning && now_s() - card.at > (card.backHint ? 3.5 : 4.0)) card.on = 0; return; }
  if (L.art && !player_waiting()) { fprintf(stderr, "[live] tuning given up (the art was closed)\n"); L.asked = 0; L.art = 0; live_cancel(); card.on = 0; return; } /* Back on the art */
  pthread_mutex_lock(&L.mu); int f = L.found, ns = L.ns, next = L.next, busy = L.busy; Stream s; if (f) s = L.s[f - 1]; pthread_mutex_unlock(&L.mu);
  if (f) found_set(L.it.id, &s);
  if (f && L.peek) { L.asked = 0; reach_set(L.it.id, 1); if (!strcmp(PK.id, L.it.id)) { PK.s = s; PK.st = 1; } fprintf(stderr, "[live] preview: stream %d\n", f); return; }
  if (f) { L.asked = 0; L.art = 0; L.fo = 0; reach_set(L.it.id, 1); card.tuning = 0; card.at = now_s();
    played(&L.it); for (int i = 0; i < nline; i++) if (!strcmp(lineup[i].id, L.it.id)) curLine = i;
    card.tuning = 1;
    if (L.zap && player_open()) { switch_to(&s, &L.it); return; }
    waitPic = 1; waitAt = now_s(); /* (over the picture: it goes only now, briefly through black) */
    app_play(&s, L.it.type, L.it.id, L.it.id, 0); player_title_hint(&L.it); hub_playing(L.it.id); return; }
  int out = (L.done && next >= ns && !busy) || now_s() - L.t0 > GIVEUP;
  if (out && L.peek) { L.asked = 0; live_cancel(); reach_set(L.it.id, -1); if (!strcmp(PK.id, L.it.id)) PK.st = -1; return; }
  if (out && L.fo) { L.asked = 0; L.fo = 0; reach_set(L.it.id, -1); live_cancel(); card.on = 1; card.tuning = 0; card.failed = 1; card.at = now_s(); player_fail(T("This channel isn't broadcasting right now")); failed(L.it.id, "nostream", 0); return; } /* none of its streams played */
  if (out) { L.asked = 0; reach_set(L.it.id, -1); live_cancel(); card.on = 1; card.tuning = 0; card.failed = 1; card.at = now_s();
    fprintf(stderr, "[live] no stream of the channel sends a picture (%d tried)\n", next);
    if (L.art) { L.art = 0; player_fail(T("This channel isn't broadcasting right now")); }
    else if (!L.zap) toast(T("This channel isn't broadcasting right now"), 3000, LIVE_TOAST);
    else player_zap_end();
    failed(L.it.id, "nostream", L.zap && !L.art && player_open()); } /* (switching: nothing found, the channel playing comes back) */
}
static void remember_prev(void) { if (curLine >= 0 && curLine < nline) { prevCh = lineup[curLine]; hasPrev = 1; } }
void live_zap(int dir) {
  if (nline < 2) return; int base = curLine >= 0 ? curLine : 0; int i = ((base + dir) % nline + nline) % nline;
  remember_prev(); Item t; live_item(&lineup[i], &t); live_start(&t, NULL, 0, 1);
}
int live_back(void) {
  if (!hasPrev) return 0; Item p; live_item(&prevCh, &p); remember_prev(); live_start(&p, NULL, 0, 1); card.backHint = 1; return 1;
}
/* a channel picked in a list of channels (the hub's tab): the list becomes the line-up */
void live_start_list(const LiveCh *const *list, int n, int idx, int zap) {
  if (!list || idx < 0 || idx >= n) return; LiveCh *c = malloc(sizeof(LiveCh) * (size_t)n); if (!c) return;
  for (int i = 0; i < n; i++) c[i] = *list[i]; line_own(c, n, list[idx]->id); Item t; live_item(list[idx], &t); live_start(&t, NULL, 0, zap);
}

/* Home's preview: the channel's streams tried as a start would, quietly; what works waits for live_peek_stream (and
   for OK: the channel then opens with it at once). A real start in progress is never taken over */
void live_peek(const Item *it) {
  if (!it || !it->id[0] || !strcmp(it->id, LIVE_ALL_ID) || !strcmp(PK.id, it->id)) return;
  if (L.asked && !L.peek) return;
  if (!pkKeep || strcmp(pkSkipId, it->id)) { npkSkip = 0; snprintf(pkSkipId, sizeof pkSkipId, "%s", it->id); } /* (a channel focused again: its streams all tried again) */
  snprintf(PK.id, sizeof PK.id, "%s", it->id); PK.st = 0;
  { Stream fs; if (live_reach(it->id) > 0 && found_get(it->id, &fs)) { PK.s = fs; PK.st = 1; fprintf(stderr, "[live] preview: the stream found before\n"); return; } } /* (found live a moment ago: no search) */
  pthread_mutex_lock(&L.mu); L.tok++; L.ns = L.next = L.found = 0; unsigned tok = L.tok; pthread_mutex_unlock(&L.mu);
  L.it = *it; L.zap = 1; L.peek = 1; L.t0 = now_s(); L.asked = 1; L.done = 0;
  fprintf(stderr, "[live] preview: looking for a stream\n");
  sb_streams(it->type, it->id, on_part, on_done, (void *)(uintptr_t)tok);
}
/* the preview's stream loaded and showed no picture: its channel's next stream that sends one (three at most); 0 when
   there is none left (the preview then says so) */
int live_peek_next(const Item *it, const char *failedUrl) {
  if (!it || !failedUrl || strcmp(PK.id, it->id) || (L.asked && !L.peek)) return 0;
  if (strcmp(pkSkipId, it->id)) { npkSkip = 0; snprintf(pkSkipId, sizeof pkSkipId, "%s", it->id); }
  if (npkSkip >= 3) return 0; pkSkip[npkSkip++] = url_h(failedUrl); live_found_drop(it->id);
  if (!strcmp(L.it.id, it->id) && L.peek && !L.asked && L.ns > 0) { /* the search that found it: on from the next stream */
    pthread_mutex_lock(&L.mu); int more = L.next < L.ns || !L.done; if (more) L.found = 0; pthread_mutex_unlock(&L.mu);
    if (!more) return 0; PK.st = 0; L.asked = 1; L.t0 = now_s(); kick(); return 1; }
  PK.id[0] = 0; pkKeep = 1; live_peek(it); pkKeep = 0; return PK.id[0] != 0; } /* (it came from a check: its streams looked through again, that one passed over) */
void live_peek_set(const char *id, const Stream *s) { snprintf(PK.id, sizeof PK.id, "%s", id); PK.s = *s; PK.st = 1; }
int live_peek_stream(const char *id, Stream *out) { if (!id || strcmp(PK.id, id)) return 0; if (PK.st == 1 && out) *out = PK.s; return PK.st; }
void live_peek_stop(void) { if (L.asked && L.peek) { pthread_mutex_lock(&L.mu); L.tok++; pthread_mutex_unlock(&L.mu); L.asked = 0; } PK.id[0] = 0; PK.st = 0; }
const char *live_catalog_of(const char *id) { /* the catalog a channel is in (its title): kept once found, looked for again a second later when not */
  static char last[SB_ID]; static const char *t; static unsigned g; static double at; if (!id) return NULL;
  if (g == catGen && !strcmp(last, id) && (t || now_s() - at < 1)) return t; g = catGen; snprintf(last, sizeof last, "%s", id); t = NULL; at = now_s();
  for (int c = 0; c < ncat && !t; c++) for (int i = 0; i < cats[c].n; i++) if (!strcmp(cats[c].ch[i].id, id)) { t = cats[c].def.title; break; }
  return t; }
void live_reach_set(const char *id, int st) { if (id && *id) reach_set(id, st); }
int live_reach(const char *id) { if (!id || !*id) return 0;
  for (int i = 0; i < 128; i++) if (!strcmp(RC[i].id, id)) return now_s() - RC[i].at < (RC[i].st > 0 ? 180 : 600) ? RC[i].st : 0; /* (live: tried again after 3 min) */
  return 0; }

/* ---- the channels on screen, tried one after another in the background (their streams asked for, the first that
   sends a picture marks the channel live, none: off air), so the marks are true before one is picked ---- */
static struct { char id[SB_ID], type[16]; } PV[32]; static int npv;
static struct { unsigned tok; char id[SB_ID]; double t0; int on, done; pthread_mutex_t mu; Stream s[MAXS]; int ns, next, busy, found; } PR = { .mu = PTHREAD_MUTEX_INITIALIZER };
void live_probe_visible(const char *const *ids, const char *const *types, int n) {
  if (n > 32) n = 32; npv = n; for (int i = 0; i < n; i++) { snprintf(PV[i].id, sizeof PV[i].id, "%s", ids[i]); snprintf(PV[i].type, sizeof PV[i].type, "%s", types[i] && *types[i] ? types[i] : "tv"); } }
static void pr_kick(void);
static void *pr_run(void *arg) {
  unsigned tok = (unsigned)(uintptr_t)arg; plat_thread_background(5);
  for (;;) { pthread_mutex_lock(&PR.mu); if (PR.tok != tok || PR.found || PR.next >= PR.ns) { int stale = PR.tok != tok; PR.busy = 0; pthread_mutex_unlock(&PR.mu); if (stale) pr_kick(); return NULL; } /* (the next channel's check: started now) */
    int i = PR.next++; Stream st = PR.s[i]; pthread_mutex_unlock(&PR.mu);
    int ok = works(&st); pthread_mutex_lock(&PR.mu); if (PR.tok == tok && ok && !PR.found) PR.found = i + 1; pthread_mutex_unlock(&PR.mu); }
}
static void pr_kick(void) { pthread_mutex_lock(&PR.mu); int go = !PR.busy && !PR.found && PR.next < PR.ns; if (go) PR.busy = 1; unsigned tok = PR.tok; pthread_mutex_unlock(&PR.mu);
  if (go) { pthread_t th; if (pthread_create(&th, NULL, pr_run, (void *)(uintptr_t)tok)) { pthread_mutex_lock(&PR.mu); PR.busy = 0; pthread_mutex_unlock(&PR.mu); } else pthread_detach(th); } }
static void pr_part(StreamGroup *g, int gi, void *ud) { (void)gi; if ((unsigned)(uintptr_t)ud != PR.tok) { free(g->items); return; }
  pthread_mutex_lock(&PR.mu); for (int i = 0; i < g->n && PR.ns < MAXS; i++) { const Stream *x = &g->items[i]; if (!x->url[0] || x->ext[0] || x->p2p) continue; add_stream(PR.s, &PR.ns, MAXS, PR.next, x); } pthread_mutex_unlock(&PR.mu);
  free(g->items); pr_kick(); }
static void pr_done(int total, void *ud) { (void)total; if ((unsigned)(uintptr_t)ud == PR.tok) PR.done = 1; }
static void probe_tick(void) {
  if (PR.on) { pthread_mutex_lock(&PR.mu); int f = PR.found, out = (PR.done && PR.next >= PR.ns && !PR.busy) || now_s() - PR.t0 > GIVEUP; pthread_mutex_unlock(&PR.mu);
    if (f) { Stream fs; pthread_mutex_lock(&PR.mu); fs = PR.s[f - 1]; pthread_mutex_unlock(&PR.mu); found_set(PR.id, &fs); }
    if (f || out) { reach_set(PR.id, f ? 1 : -1); fprintf(stderr, "[live] checked a channel: %s\n", f ? "live" : "off air"); pthread_mutex_lock(&PR.mu); PR.tok++; pthread_mutex_unlock(&PR.mu); PR.on = 0; } return; }
  if (player_open() && !player_live()) return; /* (a channel starting: it goes first) */
  for (int i = 0; i < npv; i++) { if (live_reach(PV[i].id)) continue; if (!strcmp(PK.id, PV[i].id) || (L.asked && !strcmp(L.it.id, PV[i].id))) continue; /* (being tried already) */
    pthread_mutex_lock(&PR.mu); PR.tok++; PR.ns = PR.next = PR.found = 0; PR.done = 0; unsigned tok = PR.tok; pthread_mutex_unlock(&PR.mu);
    snprintf(PR.id, sizeof PR.id, "%s", PV[i].id); PR.t0 = now_s(); PR.on = 1;
    sb_streams(PV[i].type, PV[i].id, pr_part, pr_done, (void *)(uintptr_t)tok); break; }
  npv = 0; /* (asked again each frame by whoever shows them) */
}

/* the stream playing failed in the player: the channel's next stream that sends a picture plays instead, quietly (the
   corner pill tunes); 0 when none is left to try (the player then says it) */
int live_failover(const char *id) {
  if (!id || strcmp(L.it.id, id) || L.asked) return 0;
  pthread_mutex_lock(&L.mu); int more = L.next < L.ns || !L.done; if (more) L.found = 0; pthread_mutex_unlock(&L.mu);
  if (!more) return 0;
  L.asked = 1; L.zap = 1; L.peek = 0; L.art = 0; L.fo = 1; L.t0 = now_s(); waitPic = 0;
  cardIt = L.it; card.it = &cardIt; card.on = 1; card.tuning = 1; card.failed = 0; card.at = now_s();
  fprintf(stderr, "[live] the stream failed in the player: the next one\n"); kick(); return 1;
}
