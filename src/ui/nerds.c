/* Stats for nerds (Settings > About): a small panel in the top corner, over everything, with where the memory and the
   time go: the app's RAM against the TV's, what the pictures and the GPU's textures hold, the cache on disk and the
   room left there, the frames (rate, typical and slow ones, late ones, the CPU per frame) and, while something plays,
   what plays and how. The numbers are read once a second; the disk is summed on a thread of its own every 15 s. */
#include "ui/nerds.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include "ui/img.h"
#include "ui/asssubs.h"
#include "core/app.h"
#include "core/store.h"
#include "core/net.h"
#include "core/update.h"
#include "screens/player.h"
#include "platform/plat.h"
#include "platform/mem.h"
#include <SDL2/SDL.h>
#include <pthread.h>
#include <dirent.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#define NL 18
static char lines[NL][2][120]; static int nlines;
static double cpuSum; static int cpuN, lastCalls, lastQuads; static Uint32 readAt;
static volatile long long diskData, diskFree, diskTotal; static volatile int diskBusy; static Uint32 diskAt;

int nerds_on(void) { return !strcmp(store_get("nerdStats", "false"), "true"); }
#define FR 240
static float fr[FR]; static int frN, frI; static Uint64 frLast; /* the last frames' periods, timed here (the same on every device) */
void nerds_frame(float cpuMs, int calls, int quads) {
  cpuSum += cpuMs; cpuN++; lastCalls = calls; lastQuads = quads;
  Uint64 n = SDL_GetPerformanceCounter(); if (frLast) { fr[frI] = (float)((double)(n - frLast) * 1000.0 / (double)SDL_GetPerformanceFrequency()); frI = (frI + 1) % FR; if (frN < FR) frN++; } frLast = n;
}
static void frames_live(float *fps, float *p50, float *p95, float *worst, float *latePct) {
  float s[FR]; int n = frN, lt = 0; double sum = 0; float w = 0; for (int i = 0; i < n; i++) { s[i] = fr[i]; sum += s[i]; if (s[i] > w) w = s[i]; if (s[i] > 25.0f) lt++; } /* late: a frame past 1.5 periods at 60 */
  for (int i = 1; i < n; i++) { float x = s[i]; int j = i - 1; while (j >= 0 && s[j] > x) { s[j + 1] = s[j]; j--; } s[j + 1] = x; }
  *fps = n && sum > 0 ? (float)(1000.0 * n / sum) : 0; *p50 = n ? s[n / 2] : 0; *p95 = n ? s[(int)(n * 0.95)] : 0; *worst = w; *latePct = n ? 100.0f * lt / n : 0;
}

static long long dir_bytes(const char *p, int depth) { /* everything under the app's data folder (cache, state, logs) */
  DIR *d = opendir(p); if (!d) return 0; long long sum = 0; struct dirent *e; char q[1400]; struct stat st;
  while ((e = readdir(d))) { if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue; snprintf(q, sizeof q, "%s/%s", p, e->d_name);
    if (lstat(q, &st)) continue; if (S_ISDIR(st.st_mode)) { if (depth < 4) sum += dir_bytes(q, depth + 1); } else sum += st.st_size; }
  closedir(d); return sum;
}
static void *disk_job(void *a) {
  (void)a; plat_thread_background(10); const char *dd = plat_data_dir();
  long long b = dir_bytes(dd, 0); struct statvfs v; long long fr = -1, tot = -1;
  if (!statvfs(dd, &v)) { fr = (long long)v.f_bavail * v.f_frsize; tot = (long long)v.f_blocks * v.f_frsize; }
  diskData = b; diskFree = fr; diskTotal = tot; diskBusy = 0; return NULL;
}
static const char *sz(long long b, char *o, size_t cap) { /* MB, or GB with a decimal from 10 GB */
  if (b >= 10240LL << 20) snprintf(o, cap, "%.1f GB", b / 1073741824.0); else snprintf(o, cap, "%lld MB", b >> 20); return o; }
static void add(const char *k, const char *fmt, ...) {
  if (nlines >= NL) return; snprintf(lines[nlines][0], 120, "%s", k);
  va_list ap; va_start(ap, fmt); vsnprintf(lines[nlines][1], 120, fmt, ap); va_end(ap); nlines++;
}
static void refresh(void) {
  nlines = 0;
  int rss, swp; plat_mem_self(&rss, &swp); int tot = plat_mem_total_mb(), fr = plat_mem_free_mb();
  char totS[24]; if (tot > 0) snprintf(totS, sizeof totS, "%d MB", tot); else snprintf(totS, sizeof totS, "?");
  add("RAM", "app %d MB of %s", rss, totS);
  if (swp > 0) add("swap", "%d MB of the app's", swp);
  add("TV free", "%d MB", fr);
  add("pictures", "%d of %d MB", img_used_mb(), img_budget_mb());
  long tb, pb; int tn; gfx_mem(&tb, &tn, &pb); add("GPU", "%ld MB in %d textures (%ld kept)", tb >> 20, tn, pb >> 20);
  if (diskData >= 0 && diskTotal > 0) { char a[24], f[24], t[24]; add("storage", "%s the app's · %s free of %s", sz(diskData, a, sizeof a), sz(diskFree, f, sizeof f), sz(diskTotal, t, sizeof t)); }
  else add("storage", "reading…");
  float fps, p50, p95, worst, late; frames_live(&fps, &p50, &p95, &worst, &late);
  add("frames", "%.0f fps · %.1f / %.1f ms · worst %.0f", fps, p50, p95, worst);
  add("late", "%.1f %% of the last 240", late);
  add("work", "%.1f ms a frame · %d calls · %d quads", cpuN ? cpuSum / cpuN : 0, lastCalls, lastQuads); cpuSum = 0; cpuN = 0;
  static const char *SN[] = { "sign-in", "home", "discover", "library", "anime", "title", "similar", "search", "settings" };
  add("screen", "%s · %d requests", (unsigned)SCR < SCR_N ? SN[SCR] : "?", net_pending());
  Uint32 up = SDL_GetTicks() / 1000; add("app", "%s · up %u:%02u:%02u", update_current(), up / 3600, up / 60 % 60, up % 60);
  if (player_open()) { char pl[6][120]; int n = player_nerd_lines(pl, 6);
    for (int i = 0; i < n; i++) { char *sp = strstr(pl[i], "  "); if (sp) { *sp = 0; add(pl[i], "%s", sp + 2); } else add("", "%s", pl[i]); } }
}
void nerds_draw(void) {
  if (!nerds_on()) return;
  Uint32 now = SDL_GetTicks();
  if (!diskBusy && (!diskAt || now - diskAt > 15000)) { diskAt = now; diskBusy = 1; diskData = diskData ? diskData : -1; pthread_t t; if (pthread_create(&t, NULL, disk_job, NULL)) diskBusy = 0; else pthread_detach(t); }
  if (!readAt || now - readAt >= 1000) { readAt = now; refresh(); }
  const float W = 560, LW = 120, PAD = 18, LH = 30; float h = PAD * 2 + 34 + nlines * LH, x = SB_W - 40 - W, y = 104;
  int rtl = app_rtl(); if (rtl) x = 40;
  if (player_open()) { x = rtl ? SB_W - 40 - W : 40; y = 40; } /* (in the player: the free top corner, away from its clock and its trays) */
  gfx_rrect(x, y, W, h, 18, RGBA(8, 8, 12, 214)); gfx_ring(x, y, W, h, 18, 1, WHITE_A(36));
  text_draw("STATS FOR NERDS", x + PAD, y + PAD, 17, 800, ACC3, 1.6f, 0, ALIGN_LEFT);
  for (int i = 0; i < nlines; i++) { float ly = y + PAD + 34 + i * LH;
    text_draw(lines[i][0], x + PAD, ly, 19, 600, WHITE_A(140), 0, LW - 8, ALIGN_LEFT);
    text_draw(lines[i][1], x + PAD + LW, ly, 19, 500, WHITE_A(240), 0, W - 2 * PAD - LW, ALIGN_LEFT); }
}
