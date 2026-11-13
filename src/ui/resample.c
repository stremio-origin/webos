#include "ui/resample.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

/* Keep the old area reduction path unchanged: it is the common operation on
   low-end webOS TVs, and its output must remain byte-for-byte identical. */
static unsigned char *area_average(const unsigned char *src, int w, int h,
                                   float cx, float cy, float cw, float ch,
                                   int nw, int nh, unsigned char *out) {
  for (int y = 0; y < nh; y++) { int y0 = (int)(cy + y * ch / nh), y1 = (int)(cy + (y + 1) * ch / nh); if (y1 <= y0) y1 = y0 + 1; if (y0 >= h) y0 = h - 1; if (y1 > h) y1 = h;
    for (int x = 0; x < nw; x++) { int x0 = (int)(cx + x * cw / nw), x1 = (int)(cx + (x + 1) * cw / nw); if (x1 <= x0) x1 = x0 + 1; if (x0 >= w) x0 = w - 1; if (x1 > w) x1 = w;
      unsigned r = 0, g = 0, b = 0, a = 0, n = 0;
      for (int yy = y0; yy < y1; yy++) { const unsigned char *p = src + ((size_t)yy * w + x0) * 4; for (int xx = x0; xx < x1; xx++, p += 4) { r += p[0]; g += p[1]; b += p[2]; a += p[3]; n++; } }
      unsigned char *o = out + ((size_t)y * nw + x) * 4; if (!n) n = 1; o[0] = (unsigned char)(r / n); o[1] = (unsigned char)(g / n); o[2] = (unsigned char)(b / n); o[3] = (unsigned char)(a / n); } }
  return out;
}

/* Axis uses an integer pixel span while shrinking and two neighbouring pixels
   with 8-bit fractional weights while enlarging. Mixed stretch/shrink thereby
   keeps area filtering along the shrinking axis rather than introducing aliasing. */
typedef struct { int first, end, secondWeight, enlarge; } Axis;
static Axis axis_at(float start, float length, int limit, int dst, int count, int enlarge) {
  Axis a = { 0, 1, 0, enlarge };
  if (!enlarge) {
    int lo = (int)(start + dst * length / count), hi = (int)(start + (dst + 1) * length / count);
    if (hi <= lo) hi = lo + 1;
    if (lo < 0) lo = 0;
    if (lo >= limit) lo = limit - 1;
    if (hi > limit) hi = limit;
    if (hi <= lo) hi = lo + 1;
    a.first = lo; a.end = hi;
    return a;
  }
  float centre = start + (dst + 0.5f) * length / count - 0.5f;
  /* The interpolation must not bleed pixels outside a cropped source region. */
  float lastCentre = start + length - 1.0f;
  if (lastCentre < start) lastCentre = start;
  if (centre < start) centre = start;
  if (centre > lastCentre) centre = lastCentre;
  if (centre <= 0) { a.first = 0; a.end = 1; return a; }
  if (centre >= limit - 1) { a.first = limit - 1; a.end = limit; return a; }
  int lo = (int)centre;
  a.first = lo; a.end = lo + 2;
  a.secondWeight = (int)((centre - lo) * 256.0f + 0.5f);
  return a;
}

unsigned char *sb_resample_rgba(const unsigned char *src, int w, int h,
                                float cx, float cy, float cw, float ch,
                                int nw, int nh) {
  if (!src || w <= 0 || h <= 0 || nw <= 0 || nh <= 0 ||
      !isfinite(cx) || !isfinite(cy) || !isfinite(cw) || !isfinite(ch) ||
      cx < 0 || cy < 0 || cw <= 0 || ch <= 0 ||
      cx + cw > w + 0.001f || cy + ch > h + 0.001f ||
      (size_t)nw > SIZE_MAX / 4 / (size_t)nh ||
      (size_t)nw > SIZE_MAX / sizeof(Axis) ||
      (size_t)nh > SIZE_MAX / sizeof(Axis)) return NULL;
  unsigned char *out = malloc((size_t)nw * (size_t)nh * 4);
  if (!out) return NULL;
  if (cw >= nw && ch >= nh) return area_average(src,w,h,cx,cy,cw,ch,nw,nh,out);

  /* Transparent edges (PNG/WebP): interpolate premultiplied colours, then
     unpremultiply. Fully opaque images use the fast no-alpha-weight branch. */
  int opaque = 1;
  for (size_t i = 3, n = (size_t)w * h * 4; i < n; i += 4) {
    if (src[i] != 255) { opaque = 0; break; }
  }
  int xup = cw < nw, yup = ch < nh;
  /* These sampling coordinates do not depend on the other axis. Building them
     once also avoids floating-point arithmetic in the inner pixel loops. */
  Axis *cols = malloc((size_t)nw * sizeof *cols);
  Axis *rows = malloc((size_t)nh * sizeof *rows);
  if (!cols || !rows) { free(cols); free(rows); free(out); return NULL; }
  for (int x = 0; x < nw; x++) cols[x] = axis_at(cx, cw, w, x, nw, xup);
  for (int y = 0; y < nh; y++) rows[y] = axis_at(cy, ch, h, y, nh, yup);

  if (opaque && xup && yup) {
    /* The common enlarged JPG case: four texels, fixed-point bilinear,
       exactly normalized by 2^16 (no division or extra alpha math). */
    for (int y = 0; y < nh; y++) {
      Axis ay = rows[y];
      unsigned fy = (unsigned)ay.secondWeight;
      int y1 = ay.end == ay.first + 2 ? ay.first + 1 : ay.first;
      const unsigned char *line0 = src + (size_t)ay.first * w * 4;
      const unsigned char *line1 = src + (size_t)y1 * w * 4;
      for (int x = 0; x < nw; x++) {
        Axis ax = cols[x];
        unsigned fx = (unsigned)ax.secondWeight;
        int x1 = ax.end == ax.first + 2 ? ax.first + 1 : ax.first;
        const unsigned char *p00 = line0 + (size_t)ax.first * 4, *p01 = line0 + (size_t)x1 * 4;
        const unsigned char *p10 = line1 + (size_t)ax.first * 4, *p11 = line1 + (size_t)x1 * 4;
        unsigned w00 = (256u - fx) * (256u - fy), w01 = fx * (256u - fy);
        unsigned w10 = (256u - fx) * fy, w11 = fx * fy;
        unsigned char *o = out + ((size_t)y * nw + x) * 4;
        for (int c = 0; c < 3; c++)
          o[c] = (unsigned char)((p00[c] * w00 + p01[c] * w01 + p10[c] * w10 + p11[c] * w11 + 32768u) >> 16);
        o[3] = 255;
      }
    }
    free(cols); free(rows);
    return out;
  }
  for (int y = 0; y < nh; y++) {
    Axis ay = rows[y];
    for (int x = 0; x < nw; x++) {
      Axis ax = cols[x];
      uint64_t red = 0, green = 0, blue = 0, alpha = 0, total = 0;
      for (int sy = ay.first; sy < ay.end; sy++) {
        unsigned wy = yup ? (sy == ay.first ? 256u - (unsigned)ay.secondWeight : (unsigned)ay.secondWeight) : 1u;
        const unsigned char *line = src + (size_t)sy * (size_t)w * 4;
        for (int sx = ax.first; sx < ax.end; sx++) {
          unsigned wx = xup ? (sx == ax.first ? 256u - (unsigned)ax.secondWeight : (unsigned)ax.secondWeight) : 1u;
          unsigned weight = wx * wy;
          const unsigned char *p = line + (size_t)sx * 4;
          unsigned a = opaque ? 1u : p[3];
          total += weight; alpha += (uint64_t)weight * a;
          red += (uint64_t)weight * a * p[0];
          green += (uint64_t)weight * a * p[1];
          blue += (uint64_t)weight * a * p[2];
        }
      }
      unsigned char *o = out + ((size_t)y * nw + x) * 4;
      o[3] = opaque ? 255 : (unsigned char)((alpha + total / 2) / total);
      if (alpha) {
        o[0] = (unsigned char)((red + alpha / 2) / alpha);
        o[1] = (unsigned char)((green + alpha / 2) / alpha);
        o[2] = (unsigned char)((blue + alpha / 2) / alpha);
      } else o[0] = o[1] = o[2] = 0;
    }
  }
  free(cols); free(rows);
  return out;
}
