/* Unit & regression suite for Stremio Origin TV enhancements:
 * - Slice 3.1: sb_parse_video_id (Kitsu/MAL/AniDB/IMDb), addon_serves_subs for anime types,
 *   >1,024-episode watched bitfield safety (MAX_WATCH_VIDS=4096), season-less anime watched state,
 *   Home/Discover vs Anime catalog separation & ranking, and best_dur 24-min anime episode default.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <SDL2/SDL.h>
#include "cJSON.h"
#include "platform/plat.h"
#include "core/json.h"
#include "core/net.h"
#include "core/store.h"
#include "core/i18n.h"
#include "core/addons.h"
#include "core/stremio.h"
#include "core/subs.h"
#include "core/best.h"

/* ---- minimal test doubles for store, net, i18n, plat, SDL ---- */
static cJSON *mockRoot;
static unsigned mockSerial = 1;

static void mock_store_reset(void) {
  if (mockRoot) cJSON_Delete(mockRoot);
  mockRoot = cJSON_CreateObject();
  mockSerial++;
}
void store_init(const char *d) { (void)d; mock_store_reset(); }
const char *store_get(const char *k, const char *def) {
  const cJSON *v = mockRoot ? cJSON_GetObjectItemCaseSensitive(mockRoot, k) : NULL;
  return cJSON_IsString(v) && v->valuestring ? v->valuestring : def;
}
void store_set(const char *k, const char *v) {
  if (!mockRoot) mockRoot = cJSON_CreateObject();
  cJSON_DeleteItemFromObjectCaseSensitive(mockRoot, k);
  cJSON_AddStringToObject(mockRoot, k, v ? v : "");
  mockSerial++;
}
cJSON *store_node(const char *k) { return mockRoot ? cJSON_GetObjectItemCaseSensitive(mockRoot, k) : NULL; }
void store_set_node(const char *k, cJSON *n) {
  if (!mockRoot) mockRoot = cJSON_CreateObject();
  if (n && store_node(k) == n) { mockSerial++; return; }
  cJSON_DeleteItemFromObjectCaseSensitive(mockRoot, k);
  if (n) cJSON_AddItemToObject(mockRoot, k, n);
  mockSerial++;
}
void store_remove(const char *k) { if (mockRoot) cJSON_DeleteItemFromObjectCaseSensitive(mockRoot, k); mockSerial++; }
unsigned store_serial(void) { return mockSerial; }
void store_flush(void) {}
void store_pump(void) {}
unsigned char *cache_read(const char *k, long m, size_t *l) { (void)k; (void)m; if (l) *l = 0; return NULL; }
int cache_file(const char *k, long m, char *p, size_t c) { (void)k; (void)m; (void)p; (void)c; return 0; }
void cache_write(const char *k, const unsigned char *d, size_t l) { (void)k; (void)d; (void)l; }
const char *store_data_dir(void) { return "/tmp"; }

/* net stubs */
unsigned net_get(const char *u, NetCb cb, void *ud) { (void)u; (void)cb; (void)ud; return 1; }
unsigned net_get_pri(const char *u, NetCb cb, void *ud) { (void)u; (void)cb; (void)ud; return 1; }
unsigned net_file(const char *p, NetCb cb, void *ud) { (void)p; (void)cb; (void)ud; return 1; }
unsigned net_get_hdr(const char *u, const char *h, NetCb cb, void *ud) { (void)u; (void)h; (void)cb; (void)ud; return 1; }
unsigned net_get_hdr_pri(const char *u, const char *h, NetCb cb, void *ud) { (void)u; (void)h; (void)cb; (void)ud; return 1; }
unsigned net_post_json(const char *u, const char *j, NetCb cb, void *ud) { (void)u; (void)j; (void)cb; (void)ud; return 1; }
unsigned net_post_hdr_t(const char *u, const char *b, const char *ct, const char *h, int t, NetCb cb, void *ud) {
  (void)u; (void)b; (void)ct; (void)h; (void)t; (void)cb; (void)ud; return 1;
}
unsigned net_get_timeout(const char *u, int t, NetCb cb, void *ud) { (void)u; (void)t; (void)cb; (void)ud; return 1; }
void net_cancel(unsigned id) { (void)id; }
cJSON *net_json(const unsigned char *b) { return b ? cJSON_Parse((const char *)b) : NULL; }
void net_json_free(cJSON *j) { if (j) cJSON_Delete(j); }
unsigned char *net_fetch_sync(const char *u, size_t *l, int *st, int t) { (void)u; (void)t; if (l) *l = 0; if (st) *st = 0; return NULL; }
unsigned char *net_fetch_sync_hdr(const char *u, const char *h, size_t *l, int *st, int t) {
  (void)u; (void)h; (void)t; if (l) *l = 0; if (st) *st = 0; return NULL;
}

/* i18n stubs */
static char tbuf[8][256];
static int tbi;
const char *T(const char *en) { return en; }
const char *T1(const char *en, const char *k1, const char *v1) {
  char *o = tbuf[tbi++ & 7], pat[48];
  snprintf(pat, sizeof pat, "{%s}", k1);
  const char *p = strstr(en, pat);
  if (!p) snprintf(o, 256, "%s", en);
  else snprintf(o, 256, "%.*s%s%s", (int)(p - en), en, v1 ? v1 : "", p + strlen(pat));
  return o;
}
const char *T2(const char *en, const char *k1, const char *v1, const char *k2, const char *v2) {
  char tmp[256]; snprintf(tmp, sizeof tmp, "%s", T1(en, k1, v1));
  return T1(tmp, k2, v2);
}
const char *i18n_lang(void) { return "en"; }
const char *i18n_tv_lang(void) { return "en"; }
int i18n_rtl(void) { return 0; }

/* SDL + plat + app + net extra stubs */
Uint32 SDL_GetTicks(void) { return 1000; }
void SDL_Delay(Uint32 ms) { (void)ms; }
double app_now(void) { return 1.0; }
unsigned app_frame(void) { return 1; }
char *plat_luna_call(const char *u, const char *p, int timeoutMs) { (void)u; (void)p; (void)timeoutMs; return NULL; }
const char *plat_device_name(void) { return "test"; }
int plat_panel_lines(void) { return 2160; }
void plat_thread_background(int niceInc) { (void)niceInc; }
int net_stream_sync(const char *u, int t, NetSink s, void *ud) { (void)u; (void)t; (void)s; (void)ud; return 0; }
NetRange *net_range_new(void) { return NULL; }
int net_range_get(NetRange *r, const char *u, long long st, long l, unsigned char **o, long *g, long long *tot, int *hs, int t) {
  (void)r; (void)u; (void)st; (void)l; (void)t;
  if (o) *o = NULL; if (g) *g = 0; if (tot) *tot = -1; if (hs) *hs = 0;
  return NR_NET;
}
void net_range_free(NetRange *r) { (void)r; }

/* Pull in production modules directly */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include "../third_party/cjson/cJSON.c"
#pragma clang diagnostic pop
#include "../src/core/addons.c"
#include "../src/core/stremio.c"
#include "../src/core/subs.c"
#include "../src/core/best.c"

static void test_video_id_parsing(void) {
  int s = -1, e = -1;

  /* Standard IMDb series */
  assert(sb_parse_video_id("tt0903747", "tt0903747:5:14", &s, &e) == 1);
  assert(s == 5 && e == 14);

  /* Kitsu season-less anime episode (previously parsed as S43806 E7!; s=-1 distinguishes from Season 0 Specials) */
  assert(sb_parse_video_id("kitsu:43806", "kitsu:43806:7", &s, &e) == 2);
  assert(s == -1 && e == 7);

  /* Kitsu with explicit season + episode */
  assert(sb_parse_video_id("kitsu:43806", "kitsu:43806:2:3", &s, &e) == 1);
  assert(s == 2 && e == 3);

  /* Kitsu standalone movie where videoId == itemId (contains ':' but is NOT an episode!) */
  assert(sb_parse_video_id("kitsu:12345", "kitsu:12345", &s, &e) == 0);
  assert(sb_parse_video_id("tt0133093", "tt0133093", &s, &e) == 0);

  /* NULL itemId fallbacks */
  assert(sb_parse_video_id(NULL, "kitsu:43806:7", &s, &e) == 2 && s == -1 && e == 7);
  assert(sb_parse_video_id(NULL, "kitsu:43806", &s, &e) == 0);
  assert(sb_parse_video_id(NULL, "mal:52991:12", &s, &e) == 2 && s == -1 && e == 12);
  assert(sb_parse_video_id(NULL, "anidb:999:1", &s, &e) == 2 && s == -1 && e == 1);
  assert(sb_parse_video_id(NULL, "tt0903747:1:2", &s, &e) == 1 && s == 1 && e == 2);

  /* Special season 0 */
  assert(sb_parse_video_id("tt0903747", "tt0903747:0:3", &s, &e) == 1 && s == 0 && e == 3);
  printf("  [PASS] sb_parse_video_id across IMDb, Kitsu, MAL, AniDB & movies\n");
}

static void test_addon_serves_subs_anime(void) {
  mock_store_reset();
  /* Mirrors OpenSubtitles PRO from the user's TV state.json */
  const char *osProJson =
    "{"
    "  \"transportUrl\": \"https://opensubtitlesv3-pro.dexter21767.com/manifest.json\","
    "  \"manifest\": {"
    "    \"id\": \"community.opensubtitlesv3.pro\","
    "    \"name\": \"OpenSubtitles v3 PRO\","
    "    \"types\": [\"movie\", \"series\"],"
    "    \"idPrefixes\": [\"tt\", \"kitsu\"],"
    "    \"resources\": [\"subtitles\"]"
    "  }"
    "}";
  cJSON *ad = cJSON_Parse(osProJson);
  assert(ad != NULL);
  assert(addon_serves_subs(ad, "movie", "tt0133093") == 1);
  assert(addon_serves_subs(ad, "series", "tt0903747") == 1);
  assert(addon_serves_subs(ad, "anime", "kitsu:43806") == 1);
  assert(addon_serves_subs(ad, "Anime Series", "kitsu:43806") == 1);
  assert(addon_serves_subs(ad, "Anime Movies", "kitsu:12345") == 1);
  /* Prefix filter still enforced */
  assert(addon_serves_subs(ad, "anime", "yt_id:xyz") == 0);
  /* Unrelated type still rejected */
  assert(addon_serves_subs(ad, "tv", "tt0133093") == 0);
  cJSON_Delete(ad);
  printf("  [PASS] addon_serves_subs serves subtitles for anime / Kitsu items\n");
}

static void test_long_seasonless_anime_watched_bitfield(void) {
  mock_store_reset();
  /* Build a 1,200-episode season-less Kitsu anime (exceeds old 1,024 limit -> caught BSS overflow) */
  const int N = 1200;
  cJSON *videos = cJSON_CreateArray();
  for (int i = 1; i <= N; i++) {
    cJSON *v = cJSON_CreateObject();
    char vid[32];
    snprintf(vid, sizeof vid, "kitsu:12:%d", i);
    cJSON_AddStringToObject(v, "id", vid);
    cJSON_AddNumberToObject(v, "number", i); /* Kitsu style: 'number' without 'season' */
    cJSON_AddStringToObject(v, "released", "2020-01-01T00:00:00.000Z");
    cJSON_AddItemToArray(videos, v);
  }

  /* Populate in-memory metadata cache (metaMem) as sb_meta does */
  cJSON *metaObj = cJSON_CreateObject();
  cJSON_AddStringToObject(metaObj, "id", "kitsu:12");
  cJSON_AddStringToObject(metaObj, "type", "anime");
  cJSON_AddItemToObject(metaObj, "videos", cJSON_Duplicate(videos, 1));
  meta_put("anime_kitsu:12", metaObj);

  Item it = {0};
  snprintf(it.id, sizeof it.id, "kitsu:12");
  snprintf(it.type, sizeof it.type, "anime");
  snprintf(it.name, sizeof it.name, "One Piece");

  /* Mark episode 1 and episode 1150 watched */
  cJSON *lit = lib_new_item(&it);
  const char *someEps[2] = { "kitsu:12:1", "kitsu:12:1150" };
  lib_set_eps_watched(lit, videos, someEps, 2, 1);
  lib_save(lit);

  int *marks = calloc(N, sizeof *marks);
  cJSON *saved = lib_get("kitsu:12");
  assert(saved != NULL);
  lib_ep_watched_map(saved, videos, marks);
  assert(marks[0] == 1);
  assert(marks[1] == 0);
  assert(marks[1149] == 1);
  assert(marks[1199] == 0);
  assert(lib_series_watched(saved, videos) == 0);

  /* Toggle whole 1,200-ep season-less anime watched via lib_toggle_watched */
  lib_toggle_watched(&it, videos);
  saved = lib_get("kitsu:12");
  lib_ep_watched_map(saved, videos, marks);
  for (int i = 0; i < N; i++) assert(marks[i] == 1);
  assert(lib_series_watched(saved, videos) == 1);
  assert(lib_is_marked_watched(&it) == 1);

  /* Kitsu standalone movie watched toggle must NOT confuse 'kitsu:999' colon for an episode */
  Item movie = {0};
  snprintf(movie.id, sizeof movie.id, "kitsu:999");
  snprintf(movie.type, sizeof movie.type, "anime");
  snprintf(movie.name, sizeof movie.name, "Spirited Away");
  snprintf(movie.videoId, sizeof movie.videoId, "kitsu:999");
  assert(lib_is_marked_watched(&movie) == 0);
  lib_toggle_watched(&movie, NULL);
  assert(lib_is_marked_watched(&movie) == 1);

  free(marks);
  cJSON_Delete(videos);
  printf("  [PASS] >1,024-ep season-less Kitsu watched bitfield + Kitsu movie toggle\n");
}

static void test_catalog_separation_and_ranking(void) {
  mock_store_reset();
  const char *addonsJson =
    "["
    "  {\"transportUrl\":\"https://v3-cinemeta.strem.io/manifest.json\",\"manifest\":{\"id\":\"com.linvo.cinemeta\",\"name\":\"Cinemeta\",\"catalogs\":["
    "    {\"type\":\"movie\",\"id\":\"top\",\"name\":\"Popular\"}"
    "  ]}},"
    "  {\"transportUrl\":\"https://7a82163c306e-stremio-netflix-catalog-addon.baby-beamup.club/manifest.json\",\"manifest\":{\"id\":\"pw.ers.netflix-catalog\",\"name\":\"Streaming Catalogs\",\"catalogs\":["
    "    {\"type\":\"movie\",\"id\":\"nfx\",\"name\":\"Netflix\"},"
    "    {\"type\":\"series\",\"id\":\"cru\",\"name\":\"Crunchyroll\"}"
    "  ]}},"
    "  {\"transportUrl\":\"https://94c8cb9f702d-tmdb-addon.baby-beamup.club/manifest.json\",\"manifest\":{\"id\":\"tmdb-addon\",\"name\":\"The Movie Database Addon\",\"catalogs\":["
    "    {\"type\":\"movie\",\"id\":\"tmdb.top\",\"name\":\"Popular\"},"
    "    {\"type\":\"series\",\"id\":\"streaming.cru\",\"name\":\"Crunchyroll\"}"
    "  ]}},"
    "  {\"transportUrl\":\"https://1fe84bc728af-stremio-anime-catalogs.baby-beamup.club/manifest.json\",\"manifest\":{\"id\":\"org.stremio.animecatalogs\",\"name\":\"Anime Catalogs\",\"catalogs\":["
    "    {\"type\":\"anime\",\"id\":\"kitsu-anime-trending\",\"name\":\"Trending\"}"
    "  ]}},"
    "  {\"transportUrl\":\"https://local.example/manifest.json\",\"manifest\":{\"id\":\"com.local.anime-latest\",\"name\":\"أحدث الحلقات\",\"catalogs\":["
    "    {\"type\":\"series\",\"id\":\"kitsu\",\"name\":\"أحدث الحلقات\"}"
    "  ]}}"
    "]";
  cJSON *ads = cJSON_Parse(addonsJson);
  assert(ads != NULL);
  store_set_node("addons", ads);

  CatalogDef home[64], disc[64], anime[64];
  int nh = sb_home_defs(home, 64);
  int nd = sb_disc_defs(disc, 64);
  int na = sb_anime_defs(anime, 64);

  assert(nh == 3); /* Cinemeta Popular, Streaming Catalogs Netflix, TMDB Popular */
  assert(nd == 8); /* 3 Cinemeta featured + 5 Cinemeta genres, 0 addon overflow */
  for (int i = 0; i < nh; i++) {
    assert(strstr(home[i].title, "Crunchyroll") == NULL);
    assert(strstr(home[i].title, "Anime") == NULL);
    assert(strcmp(home[i].type, "anime") != 0);
  }
  for (int i = 0; i < nd; i++) {
    assert(strstr(disc[i].title, "Crunchyroll") == NULL);
    assert(strstr(disc[i].title, "Anime") == NULL);
    assert(strcmp(disc[i].type, "anime") != 0);
  }

  assert(na == 4); /* 2 dedicated anime catalogs first, then 2 Crunchyroll catalogs */
  /* Pass 1 (dedicated anime catalogs) come BEFORE Pass 2 (Crunchyroll sub-catalogs) */
  assert(!strcmp(anime[0].title, "Trending"));
  assert(strstr(anime[0].title, "· anime") == NULL); /* clean localized row title */
  assert(strstr(anime[1].title, "أحدث الحلقات") != NULL);
  assert(strstr(anime[2].title, "Crunchyroll") != NULL);
  assert(strstr(anime[3].title, "Crunchyroll") != NULL);
  printf("  [PASS] Home/Discover vs Anime catalog separation, Crunchyroll routing & ranking\n");
}

static void test_best_dur_anime(void) {
  mock_store_reset();
  assert(best_dur(NULL, "anime") == 24);
  assert(best_dur(NULL, "Anime Series") == 24);
  assert(best_dur(NULL, "series") == 45);
  assert(best_dur(NULL, "movie") == 110);

  cJSON *singleFilm = cJSON_Parse("{\"videos\":[{\"id\":\"kitsu:999\"}]}");
  assert(best_dur(singleFilm, "anime") == 100);
  cJSON_Delete(singleFilm);
  printf("  [PASS] best_dur defaults anime episodes to 24m and single-video anime films to 100m\n");
}

static void test_cp1256_arabic_subs_vs_cp1252(void) {
  /* 1. Classic OpenSubtitles Windows-1256 Arabic SRT:
   *    "مرحبا يا صديقي، هل رأيت الحلقة؟" in CP1256 bytes:
   *    مرحبا = E3 D1 CD C8 C7
   *    يا = ED C7
   *    صديقي، = D5 CF ED DE ED A1
   *    هل = E5 E1
   *    رأيت = D1 C3 ED CA
   *    الحلقة؟ = C7 E1 CD E1 DE C9 BF
   */
  static const unsigned char cp1256_srt[] =
    "1\r\n"
    "00:00:01,000 --> 00:00:04,000\r\n"
    "\xe3\xd1\xcd\xc8\xc7 \xed\xc7 \xd5\xcf\xed\xde\xed\xa1 \xe5\xe1 \xd1\xc3\xed\xca \xc7\xe1\xcd\xe1\xde\xc9\xbf\r\n\r\n"
    "2\r\n"
    "00:00:05,000 --> 00:00:07,000\r\n"
    "- \xc3\xe5\xe1\xc7\xf0!\r\n\r\n";
  Cue *cues = NULL;
  int n = subs_parse_body(cp1256_srt, sizeof(cp1256_srt) - 1, &cues);
  assert(n == 2);
  assert(strcmp(cues[0].t, "مرحبا يا صديقي، هل رأيت الحلقة؟") == 0);
  assert(strcmp(cues[1].t, "- أهلاً!") == 0);
  subs_free(cues, n);

  /* 2. Windows-1252 French SRT must NOT be misclassified as CP1256:
   *    "C’était déjà l’été à Noël — garçon !"
   *    ’ = 0x92, é = 0xE9, è = 0xE8, à = 0xE0, ë = 0xEB, — = 0x97, ç = 0xE7
   */
  static const unsigned char cp1252_srt[] =
    "1\r\n"
    "00:00:01,000 --> 00:00:04,000\r\n"
    "C\x92\xe9tait d\xe9j\xe0 l\x92\xe9t\xe9 \xe0 No\xebl \x97 gar\xe7on !\r\n\r\n";
  cues = NULL;
  n = subs_parse_body(cp1252_srt, sizeof(cp1252_srt) - 1, &cues);
  assert(n == 1);
  assert(strcmp(cues[0].t, "C’était déjà l’été à Noël — garçon !") == 0);
  subs_free(cues, n);
  printf("  [PASS] Windows-1256 Arabic SRT auto-decoded to UTF-8 without breaking Windows-1252 Western SRTs\n");
}

static void test_torbox_debrid_scoring(void) {
  mock_store_reset();
  TvCaps oled = { 2160, 1, 1, 1, 0, 0 }; /* 4K HDR + DV OLED */

  Stream uncachedTb = { 0 };
  snprintf(uncachedTb.addon, sizeof uncachedTb.addon, "Torrentio TB");
  snprintf(uncachedTb.name, sizeof uncachedTb.name, "[TB-] Torrentio\n4k DV | HDR");
  snprintf(uncachedTb.title, sizeof uncachedTb.title, "Show.S01E01.2160p.WEB-DL.DDP5.1.DV.HDR.H265\n💾 4.2 GB");
  snprintf(uncachedTb.url, sizeof uncachedTb.url, "https://torrentio.strem.fun/resolve/tb/1");

  Stream uncachedTorz = { 0 };
  snprintf(uncachedTorz.addon, sizeof uncachedTorz.addon, "StremThru Torz");
  snprintf(uncachedTorz.name, sizeof uncachedTorz.addon, "StremThru ⬇️ 4K");
  snprintf(uncachedTorz.title, sizeof uncachedTorz.title, "Show.S01E01.2160p.WEB-DL.DDP5.1.DV.HDR.H265\n💾 4.5 GB");
  snprintf(uncachedTorz.url, sizeof uncachedTorz.url, "https://stremthru.example/torz/2");

  Stream uncachedParen = { 0 };
  snprintf(uncachedParen.addon, sizeof uncachedParen.addon, "Comet");
  snprintf(uncachedParen.name, sizeof uncachedParen.name, "Comet (Download)\n2160p");
  snprintf(uncachedParen.title, sizeof uncachedParen.title, "Show.S01E01.2160p.WEB-DL.DDP5.1.H265\n💾 4.1 GB");
  snprintf(uncachedParen.url, sizeof uncachedParen.url, "https://comet.example/dl/3");

  Stream plainDirect = { 0 };
  snprintf(plainDirect.addon, sizeof plainDirect.addon, "SomeHTTP");
  snprintf(plainDirect.name, sizeof plainDirect.name, "Direct\n2160p");
  snprintf(plainDirect.title, sizeof plainDirect.title, "Show.S01E01.2160p.WEB-DL.DDP5.1.H265\n💾 3.8 GB");
  snprintf(plainDirect.url, sizeof plainDirect.url, "https://cdn.example/4.mkv");

  Stream cachedTb = { 0 };
  snprintf(cachedTb.addon, sizeof cachedTb.addon, "Torrentio TB");
  snprintf(cachedTb.name, sizeof cachedTb.name, "[TB+] Torrentio\n2160p");
  snprintf(cachedTb.title, sizeof cachedTb.title, "Show.S01E01.2160p.WEB-DL.DDP5.1.H265\n💾 3.8 GB");
  snprintf(cachedTb.url, sizeof cachedTb.url, "https://torrentio.strem.fun/resolve/tb/5");

  BestRate r;
  /* Uncached TorBox / StremThru / Comet variants must all be rejected */
  assert(best_rate(&uncachedTb, &oled, 100, 24, &r) == 0);
  assert(best_rate(&uncachedTorz, &oled, 100, 24, &r) == 0);
  assert(best_rate(&uncachedParen, &oled, 100, 24, &r) == 0);

  /* Cached [TB+] wins over unmarked plain link of identical quality */
  const Stream *list[5] = { &uncachedTb, &uncachedTorz, &uncachedParen, &plainDirect, &cachedTb };
  const Stream *picked = best_pick(list, 5, 24, &oled, 100, &r);
  assert(picked == &cachedTb);
  printf("  [PASS] TorBox / StremThru uncached markers rejected and [TB+] cached streams prioritized\n");
}

int main(void) {
  printf("Running Stremio Origin enhancement unit tests...\n");
  test_video_id_parsing();
  test_addon_serves_subs_anime();
  test_long_seasonless_anime_watched_bitfield();
  test_catalog_separation_and_ranking();
  test_best_dur_anime();
  test_cp1256_arabic_subs_vs_cp1252();
  test_torbox_debrid_scoring();
  for (int i = 0; i < 40; i++) if (metaMem[i].meta) { cJSON_Delete(metaMem[i].meta); metaMem[i].meta = NULL; }
  if (mockRoot) cJSON_Delete(mockRoot);
  printf("ALL UNIT TESTS PASSED.\n");
  return 0;
}
