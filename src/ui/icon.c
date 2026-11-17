#include "ui/icon.h"
#include "ui/gfx.h"
#include "ui/atlas.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dirent.h>

/* ---- a small SVG path rasteriser: flatten to polygons, scanline fill (non-zero winding), 4x4 supersampling ---- */
typedef struct { float x, y; } Pt;
typedef struct { Pt *p; int n, cap; } Poly;
typedef struct { Poly *polys; int n, cap; } Shape;
static void poly_add(Poly *p, float x, float y) { if (p->n == p->cap) { p->cap = p->cap ? p->cap * 2 : 32; p->p = realloc(p->p, p->cap * sizeof *p->p); } p->p[p->n].x = x; p->p[p->n].y = y; p->n++; }
static Poly *shape_new_poly(Shape *s) { if (s->n == s->cap) { s->cap = s->cap ? s->cap * 2 : 8; s->polys = realloc(s->polys, s->cap * sizeof *s->polys); } Poly *p = &s->polys[s->n++]; memset(p, 0, sizeof *p); return p; }
static void cubic(Poly *p, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3) {
  for (int i = 1; i <= 16; i++) { float t = i / 16.0f, u = 1 - t; poly_add(p, u*u*u*x0 + 3*u*u*t*x1 + 3*u*t*t*x2 + t*t*t*x3, u*u*u*y0 + 3*u*u*t*y1 + 3*u*t*t*y2 + t*t*t*y3); }
}
static void quad(Poly *p, float x0, float y0, float x1, float y1, float x2, float y2) {
  for (int i = 1; i <= 12; i++) { float t = i / 12.0f, u = 1 - t; poly_add(p, u*u*x0 + 2*u*t*x1 + t*t*x2, u*u*y0 + 2*u*t*y1 + t*t*y2); }
}
/* SVG arc -> centre parameterisation -> line segments */
static void arc(Poly *p, float x1, float y1, float rx, float ry, float phi, int large, int sweep, float x2, float y2) {
  if (rx == 0 || ry == 0) { poly_add(p, x2, y2); return; }
  float cs = cosf(phi * (float)M_PI / 180), sn = sinf(phi * (float)M_PI / 180);
  float dx = (x1 - x2) / 2, dy = (y1 - y2) / 2, x1p = cs * dx + sn * dy, y1p = -sn * dx + cs * dy;
  rx = fabsf(rx); ry = fabsf(ry); float lam = x1p * x1p / (rx * rx) + y1p * y1p / (ry * ry); if (lam > 1) { rx *= sqrtf(lam); ry *= sqrtf(lam); }
  float num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p, den = rx * rx * y1p * y1p + ry * ry * x1p * x1p;
  float co = den ? sqrtf(fmaxf(0, num / den)) : 0; if (large == sweep) co = -co;
  float cxp = co * rx * y1p / ry, cyp = -co * ry * x1p / rx, cx = cs * cxp - sn * cyp + (x1 + x2) / 2, cy = sn * cxp + cs * cyp + (y1 + y2) / 2;
  float a1 = atan2f((y1p - cyp) / ry, (x1p - cxp) / rx), a2 = atan2f((-y1p - cyp) / ry, (-x1p - cxp) / rx), da = a2 - a1;
  if (!sweep && da > 0) da -= 2 * (float)M_PI; else if (sweep && da < 0) da += 2 * (float)M_PI;
  int n = (int)(fabsf(da) * 8) + 4;
  for (int i = 1; i <= n; i++) { float a = a1 + da * i / n, ex = rx * cosf(a), ey = ry * sinf(a); poly_add(p, cs * ex - sn * ey + cx, sn * ex + cs * ey + cy); }
}
static int flag(const char **s, float *v) { while (**s == ' ' || **s == ',' || **s == '\n') (*s)++; if (**s != '0' && **s != '1') return 0; *v = **s - '0'; (*s)++; return 1; }
static int num(const char **s, float *v) { while (**s == ' ' || **s == ',' || **s == '\n') (*s)++; char *e; *v = strtof(*s, &e); if (e == *s) return 0; *s = e; return 1; }
static void parse_path(const char *d, Shape *sh) {
  const char *s = d; char cmd = 0; float cx = 0, cy = 0, sx = 0, sy = 0, lx = 0, ly = 0; Poly *p = NULL; char last = 0;
  while (*s) {
    while (*s == ' ' || *s == ',' || *s == '\n') s++; if (!*s) break;
    if ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z')) cmd = *s++;
    int rel = cmd >= 'a'; char c = rel ? cmd - 32 : cmd; float a, b, c2, d2, e2, f2, l, sw;
    switch (c) {
      case 'M': if (!num(&s, &a) || !num(&s, &b)) return; if (rel) { a += cx; b += cy; } p = shape_new_poly(sh); cx = sx = a; cy = sy = b; poly_add(p, cx, cy); cmd = rel ? 'l' : 'L'; break;
      case 'L': if (!num(&s, &a) || !num(&s, &b)) return; if (rel) { a += cx; b += cy; } if (!p) p = shape_new_poly(sh); cx = a; cy = b; poly_add(p, cx, cy); break;
      case 'H': if (!num(&s, &a)) return; if (rel) a += cx; if (!p) p = shape_new_poly(sh); cx = a; poly_add(p, cx, cy); break;
      case 'V': if (!num(&s, &a)) return; if (rel) a += cy; if (!p) p = shape_new_poly(sh); cy = a; poly_add(p, cx, cy); break;
      case 'C': if (!num(&s, &a) || !num(&s, &b) || !num(&s, &c2) || !num(&s, &d2) || !num(&s, &e2) || !num(&s, &f2)) return; if (rel) { a += cx; b += cy; c2 += cx; d2 += cy; e2 += cx; f2 += cy; }
        if (!p) p = shape_new_poly(sh); cubic(p, cx, cy, a, b, c2, d2, e2, f2); lx = c2; ly = d2; cx = e2; cy = f2; break;
      case 'S': if (!num(&s, &c2) || !num(&s, &d2) || !num(&s, &e2) || !num(&s, &f2)) return; if (rel) { c2 += cx; d2 += cy; e2 += cx; f2 += cy; }
        a = (last == 'C' || last == 'S') ? 2 * cx - lx : cx; b = (last == 'C' || last == 'S') ? 2 * cy - ly : cy; if (!p) p = shape_new_poly(sh); cubic(p, cx, cy, a, b, c2, d2, e2, f2); lx = c2; ly = d2; cx = e2; cy = f2; break;
      case 'Q': if (!num(&s, &a) || !num(&s, &b) || !num(&s, &c2) || !num(&s, &d2)) return; if (rel) { a += cx; b += cy; c2 += cx; d2 += cy; } if (!p) p = shape_new_poly(sh); quad(p, cx, cy, a, b, c2, d2); lx = a; ly = b; cx = c2; cy = d2; break;
      case 'T': if (!num(&s, &c2) || !num(&s, &d2)) return; if (rel) { c2 += cx; d2 += cy; } a = (last == 'Q' || last == 'T') ? 2 * cx - lx : cx; b = (last == 'Q' || last == 'T') ? 2 * cy - ly : cy; if (!p) p = shape_new_poly(sh); quad(p, cx, cy, a, b, c2, d2); lx = a; ly = b; cx = c2; cy = d2; break;
      case 'A': if (!num(&s, &a) || !num(&s, &b) || !num(&s, &c2) || !flag(&s, &l) || !flag(&s, &sw) || !num(&s, &e2) || !num(&s, &f2)) return; if (rel) { e2 += cx; f2 += cy; } if (!p) p = shape_new_poly(sh); arc(p, cx, cy, a, b, c2, l != 0, sw != 0, e2, f2); cx = e2; cy = f2; break;
      case 'Z': if (p) { poly_add(p, sx, sy); } cx = sx; cy = sy; p = NULL; break;
      default: return;
    }
    last = c;
  }
}
/* rasterise at px size: viewBox 0..vb maps to 0..px; 4x4 supersampling, non-zero winding */
static unsigned char *rasterise(Shape *sh, float vb, int px) {
  int S = 4, W = px * S; unsigned char *acc = calloc((size_t)W * W, 1); float k = (float)W / vb;
  float *xs = malloc(4096 * sizeof *xs); int *ws = malloc(4096 * sizeof *ws);
  for (int y = 0; y < W; y++) {
    float sy = y + 0.5f; int n = 0;
    for (int pi = 0; pi < sh->n; pi++) { Poly *p = &sh->polys[pi]; if (p->n < 2) continue;
      for (int i = 0; i < p->n; i++) { Pt a = p->p[i], b = p->p[(i + 1) % p->n]; float ax = a.x * k, ay = a.y * k, bx = b.x * k, by = b.y * k;
        if ((sy >= ay && sy < by) || (sy >= by && sy < ay)) { float t = (sy - ay) / (by - ay); if (n < 4096) { xs[n] = ax + (bx - ax) * t; ws[n] = by > ay ? 1 : -1; n++; } } } }
    /* sort crossings */
    for (int i = 1; i < n; i++) { float x = xs[i]; int w = ws[i]; int j = i - 1; while (j >= 0 && xs[j] > x) { xs[j + 1] = xs[j]; ws[j + 1] = ws[j]; j--; } xs[j + 1] = x; ws[j + 1] = w; }
    int wind = 0; for (int i = 0; i + 1 < n; i++) { wind += ws[i]; if (wind) { int x0 = (int)ceilf(xs[i] - 0.5f), x1 = (int)floorf(xs[i + 1] - 0.5f); if (x0 < 0) x0 = 0; if (x1 >= W) x1 = W - 1; for (int x = x0; x <= x1; x++) acc[y * W + x] = 1; } }
  }
  unsigned char *out = malloc((size_t)px * px);
  for (int y = 0; y < px; y++) for (int x = 0; x < px; x++) { int c = 0; for (int sy2 = 0; sy2 < S; sy2++) for (int sx2 = 0; sx2 < S; sx2++) c += acc[(y * S + sy2) * W + x * S + sx2]; out[y * px + x] = (unsigned char)(c * 255 / (S * S)); }
  free(acc); free(xs); free(ws); return out;
}

/* an outlined icon (stroke-width in the SVG, round caps and joins): coverage from the distance to the path,
   one sample per pixel with a one-pixel soft edge */
static unsigned char *rasterise_stroke(Shape *sh, float vb, int px, float sw) {
  unsigned char *out = malloc((size_t)px * px); float k = px / vb, hw = sw * k / 2;
  for (int y = 0; y < px; y++) for (int x = 0; x < px; x++) {
    float X = x + .5f, Y = y + .5f, best = 1e9f;
    for (int pi = 0; pi < sh->n; pi++) { Poly *p = &sh->polys[pi];
      for (int i = 0; i + 1 < p->n; i++) { float ax = p->p[i].x * k, ay = p->p[i].y * k, bx = p->p[i + 1].x * k, by = p->p[i + 1].y * k;
        float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy, t = l2 > 0 ? ((X - ax) * dx + (Y - ay) * dy) / l2 : 0; t = t < 0 ? 0 : t > 1 ? 1 : t;
        float ex = ax + dx * t - X, ey = ay + dy * t - Y, d2 = ex * ex + ey * ey; if (d2 < best) best = d2; } }
    float c = hw - sqrtf(best) + .5f; out[y * px + x] = (unsigned char)(c <= 0 ? 0 : c >= 1 ? 255 : c * 255 + .5f); }
  return out;
}

/* ---- the icon set ---- */
typedef struct { char name[32]; char *d; float vb, stroke; } IconDef;
#define MAXDEFS 256 /* (every assets/icons*.json together; one past it is logged, never dropped quietly) */
static IconDef defs[MAXDEFS]; static int ndefs;
int icon_exists(const char *name);
typedef struct { int def, px, rot; AtlasSlot slot; GLuint own; unsigned use; } Cached; /* in the shared pages, or a texture of its own when they are full */
static Cached cache[192]; static int ncache; static unsigned useTick;
static void icon_load(const char *path) {
  FILE *f = fopen(path, "rb"); if (!f) return;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); char *t = malloc(n + 1); size_t g = fread(t, 1, n, f); t[g] = 0; fclose(f);
  cJSON *j = cJSON_Parse(t); free(t); if (!j) { fprintf(stderr, "[icon] bad %s\n", path); return; }
  cJSON *e; cJSON_ArrayForEach(e, j) { if (!cJSON_IsString(e) || icon_exists(e->string)) continue;
    if (ndefs >= MAXDEFS) { fprintf(stderr, "[icon] too many icons: %s from %s left out\n", e->string, path); continue; }
    const char *svg = e->valuestring; const char *d = strstr(svg, " d=\""); if (!d) continue; d += 4; const char *end = strchr(d, '"'); if (!end) continue;
    IconDef *ic = &defs[ndefs++]; snprintf(ic->name, sizeof ic->name, "%s", e->string); ic->d = strndup(d, end - d); ic->vb = 24;
    const char *vbp = strstr(svg, "viewBox=\""); if (vbp) { float a, b, c, dd; if (sscanf(vbp + 9, "%f %f %f %f", &a, &b, &c, &dd) == 4) ic->vb = c; }
    const char *swp = strstr(svg, "stroke-width=\""); ic->stroke = swp ? strtof(swp + 14, NULL) : 0; }
  cJSON_Delete(j);
}
/* icons.json, then every icons.<part>.json beside it (each feature ships its own icons) */
int icon_init(const char *dir) {
  char p[1400]; snprintf(p, sizeof p, "%s/icons.json", dir); icon_load(p); if (!ndefs) return 0;
  DIR *dd = opendir(dir); struct dirent *e;
  while (dd && (e = readdir(dd))) { size_t n = strlen(e->d_name);
    if (n <= 11 || strncmp(e->d_name, "icons.", 6) || strcmp(e->d_name + n - 5, ".json")) continue;
    snprintf(p, sizeof p, "%s/%s", dir, e->d_name); icon_load(p); }
  if (dd) closedir(dd);
  return 1;
}
int icon_exists(const char *name) { for (int i = 0; i < ndefs; i++) if (!strcmp(defs[i].name, name)) return 1; return 0; }
static Cached *icon_get(int def, int px, int rot) {
  useTick++;
  Cached *c = NULL;
  for (int i = 0; i < ncache; i++) if (cache[i].def == def && cache[i].px == px && cache[i].rot == rot) { c = &cache[i]; break; }
  if (c && (c->own || atlas_valid(&c->slot))) { c->use = useTick; atlas_touch(&c->slot); return c; }
  if (!c) { /* a free entry, or the one drawn longest ago */
    if (ncache < 192) c = &cache[ncache++];
    else { c = &cache[0]; for (int i = 1; i < 192; i++) if (cache[i].use < c->use) c = &cache[i]; }
    if (c->own) gfx_tex_free(c->own);
    memset(c, 0, sizeof *c); c->def = def; c->px = px; c->rot = rot; c->slot.page = -1; }
  Shape sh = { 0 }; parse_path(defs[def].d, &sh);
  if (rot) { float cs = cosf(rot * (float)M_PI / 180), sn = sinf(rot * (float)M_PI / 180), m = defs[def].vb / 2;
    for (int i = 0; i < sh.n; i++) for (int j = 0; j < sh.polys[i].n; j++) { Pt *p = &sh.polys[i].p[j]; float x = p->x - m, y = p->y - m; p->x = m + x * cs - y * sn; p->y = m + x * sn + y * cs; } }
  unsigned char *bmp = defs[def].stroke > 0 ? rasterise_stroke(&sh, defs[def].vb, px, defs[def].stroke) : rasterise(&sh, defs[def].vb, px);
  if (!atlas_put(bmp, px, px, -1, 0, &c->slot)) { c->slot.page = -1; c->own = gfx_tex_create(bmp, px, px, 1, 1); }
  free(bmp); for (int i = 0; i < sh.n; i++) free(sh.polys[i].p); free(sh.polys);
  c->use = useTick;
  return c;
}
void icon_draw_rot(const char *name, float x, float y, float size, uint32_t color, float deg) {
  int def = -1; for (int i = 0; i < ndefs; i++) if (!strcmp(defs[i].name, name)) { def = i; break; }
  if (def < 0) return;
  int px = (int)(size * gfx_scale() + 0.5f); if (px < 4) px = 4; if (px > 512) px = 512;
  /* one picture per size, turned by the GPU (a picture per 6-degree step was a rasterised icon every few frames of a turn) */
  /* a size on the move (a zoom, a press) would be a new picture every frame, each made on the CPU: while it moves, the
     nearest one made already is scaled by the GPU; once it settles (10 frames on one size), it is made sharp at that size */
  static unsigned lastMade[MAXDEFS]; static int lastPx[MAXDEFS]; unsigned fr = gfx_frame();
  Cached *c = NULL;
  for (int i = 0; i < ncache; i++) if (cache[i].def == def && cache[i].px == px && !cache[i].rot && (cache[i].own || atlas_valid(&cache[i].slot))) { c = &cache[i]; break; }
  if (!c && lastMade[def] && fr - lastMade[def] < 10) { Cached *n = NULL; int bd = 1 << 30;
    for (int i = 0; i < ncache; i++) if (cache[i].def == def && !cache[i].rot && (cache[i].own || atlas_valid(&cache[i].slot))) { int d = abs(cache[i].px - px); if (d < bd) { bd = d; n = &cache[i]; } }
    if (n && bd * 3 <= px) { c = n; c->use = ++useTick; atlas_touch(&c->slot); if (lastPx[def] != px) { lastPx[def] = px; lastMade[def] = fr; } } }
  if (!c) { c = icon_get(def, px, 0); lastMade[def] = fr; lastPx[def] = px; }
  float u0 = 0, v0 = 0, u1 = 1, v1 = 1; GLuint t = c->own; if (!t) { atlas_uv(&c->slot, &u0, &v0, &u1, &v1); t = atlas_tex(c->slot.page); }
  if (gfx_mirrored() && (!strcmp(name, "ph_back") || !strcmp(name, "ph_chev"))) { float t2 = u0; u0 = u1; u1 = t2; } /* (a mirrored page: an arrow points the other way too) */
  float r = fmodf(deg, 360); if (fabsf(r) < 0.01f) gfx_alpha_tex_uv(t, x, y, size, size, color, u0, v0, u1, v1); else gfx_alpha_tex_rot(t, x, y, size, size, r, color, u0, v0, u1, v1);
}
void icon_draw(const char *name, float x, float y, float size, uint32_t color) { icon_draw_rot(name, x, y, size, color, 0); }
