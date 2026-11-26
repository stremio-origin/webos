#include "ui/osk.h"
#include "core/keys.h"
#include "core/i18n.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include "ui/icon.h"
#include "core/hit.h"
#include <stdio.h>
#include <string.h>

/* 10 keys a row: letters, digits, the address symbols; a last row of Shift, Space, Backspace, Done */
static const char *ROWS[4] = { "abcdefghij", "klmnopqrst", "uvwxyz1234", "567890@._-" };
#define KW 76.0f
#define KH 62.0f
#define GAP 8.0f
#define COLS 10
static float key_w(int r, int c) { if (r < 4) return KW; return c == 1 ? 3 * KW + 2 * GAP : c == 3 ? 2 * KW + GAP : KW * 1.5f + GAP / 2; }
static float key_x(int r, int c) { if (r < 4) return c * (KW + GAP); float x = 0; for (int i = 0; i < c; i++) x += key_w(r, i) + GAP; return x; }
static int ncols(int r) { return r < 4 ? COLS : 4; }
/* the pointer: pointing at a key moves the ring there, a click is OK on it (before, the keys had no place for
   the pointer, so with the Magic Remote out a click typed nothing) */
static Osk *hitOsk;
static void osk_hit(int r, int c) { if (hitOsk && hitOsk->open) { hitOsk->r = r; hitOsk->c = c; } }
void osk_open(Osk *k) { k->open = 1; k->r = 0; k->c = 0; tween_to(&k->a, 1, 0.25f, EASE_OUT); k->shown = 0; }
void osk_close(Osk *k) { k->open = 0; tween_to(&k->a, 0, 0.2f, EASE_OUT); }
float osk_height(void) { return 5 * (KH + GAP) - GAP; }
int osk_key(Osk *k, int key, char *buf, size_t cap) {
  if (!k->open) return 0; int r0 = k->r, c0 = k->c;
  if (key == K_LEFT) { if (k->c > 0) k->c--; }
  else if (key == K_RIGHT) { if (k->c < ncols(k->r) - 1) k->c++; }
  else if (key == K_UP) { if (k->r > 0) { int fx = (int)(key_x(k->r, k->c) + key_w(k->r, k->c) / 2); k->r--; k->c = fx / (int)(KW + GAP); if (k->c >= ncols(k->r)) k->c = ncols(k->r) - 1; } else return 0; }
  else if (key == K_DOWN) { if (k->r < 4) { int fx = (int)(key_x(k->r, k->c) + key_w(k->r, k->c) / 2); k->r++; if (k->r == 4) { k->c = 0; for (int i = 0; i < 4; i++) if (fx >= key_x(4, i)) k->c = i; } else if (k->c >= ncols(k->r)) k->c = ncols(k->r) - 1; } }
  else if (key == K_OK) { size_t n = strlen(buf);
    if (k->r < 4) { char ch = ROWS[k->r][k->c]; if (k->shift && ch >= 'a' && ch <= 'z') ch -= 32; if (n + 1 < cap) { buf[n] = ch; buf[n + 1] = 0; } k->shift = 0; }
    else if (k->c == 0) k->shift = !k->shift; else if (k->c == 1) { if (n + 1 < cap && n && buf[n - 1] != ' ') { buf[n] = ' '; buf[n + 1] = 0; } }
    else if (k->c == 2) { if (n) { n--; while (n && (buf[n] & 0xc0) == 0x80) n--; buf[n] = 0; } }
    else return 2; }
  else if (key == K_BACK) { size_t n = strlen(buf); if (n) { n--; while (n && (buf[n] & 0xc0) == 0x80) n--; buf[n] = 0; } else return 2; }
  else return 0;
  if (key != K_OK && key != K_BACK && r0 == k->r && c0 == k->c) nudge_start(&k->nudge, key);
  return 1;
}
void osk_tick(Osk *k, float dt) { tween_tick(&k->a, dt); nudge_tick(&k->nudge, dt); tween_tick(&k->fx, dt); tween_tick(&k->fy, dt); tween_tick(&k->fw, dt); }
void osk_draw(Osk *k, float x, float y, float w) {
  float a = k->a.v; if (a <= 0.001f) return; (void)w;
  gfx_opacity_push(a); gfx_offset_push(0, 12 * (1 - a));
  float tx = x + key_x(k->r, k->c), ty = y + k->r * (KH + GAP), tw = key_w(k->r, k->c);
  if (!k->shown) { tween_set(&k->fx, tx); tween_set(&k->fy, ty); tween_set(&k->fw, tw); k->shown = 1; } else { tween_to(&k->fx, tx, 0.14f, EASE_OUT); tween_to(&k->fy, ty, 0.14f, EASE_OUT); tween_to(&k->fw, tw, 0.14f, EASE_OUT); }
  gfx_offset_push(nudge_x(&k->nudge), nudge_y(&k->nudge)); gfx_rrect(k->fx.v, k->fy.v, k->fw.v, KH, 14, ACC); gfx_offset_pop();
  for (int r = 0; r < 5; r++) for (int c = 0; c < ncols(r); c++) { float kx = x + key_x(r, c), ky = y + r * (KH + GAP), kw = key_w(r, c); int f = r == k->r && c == k->c; float nx = f ? nudge_x(&k->nudge) : 0, ny = f ? nudge_y(&k->nudge) : 0;
    if (k->open) { hitOsk = k; hit_add(kx, ky, kw, KH, osk_hit, NULL, r, c); }
    if (!f) gfx_rrect(kx, ky, kw, KH, 14, WHITE_A(r == 4 && c == 0 && k->shift ? 46 : 15)); uint32_t fg = f ? ACC_FG : WHITE_A(219);
    if (r < 4) { char s[2] = { ROWS[r][c], 0 }; if (k->shift && s[0] >= 'a' && s[0] <= 'z') s[0] -= 32; text_draw(s, kx + kw / 2 + nx, ky + (KH - text_line_height(26)) / 2 + ny, 26, 700, fg, 0, 0, ALIGN_CENTER); }
    else if (c == 0) text_draw(k->shift ? "ABC" : "abc", kx + kw / 2 + nx, ky + (KH - text_line_height(22)) / 2 + ny, 22, 700, fg, 0.5f, 0, ALIGN_CENTER);
    else if (c == 1) text_draw(T("Space"), kx + kw / 2 + nx, ky + (KH - text_line_height(22)) / 2 + ny, 22, 700, fg, 0.5f, 0, ALIGN_CENTER);
    else if (c == 2) icon_draw("backspace", kx + kw / 2 - 14 + nx, ky + KH / 2 - 14 + ny, 28, fg);
    else text_draw(T("Done"), kx + kw / 2 + nx, ky + (KH - text_line_height(22)) / 2 + ny, 22, 700,
 fg, 0.5f, 0, ALIGN_CENTER); }
  gfx_offset_pop(); gfx_opacity_pop();
}
