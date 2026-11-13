#include "ui/imgquality.h"
int img_quality_covers(int haveW, int haveH, int wantW, int wantH) {
  if (haveW <= 0 || haveH <= 0 || wantW <= 0 || wantH <= 0) return 0;
  return (long)haveW * 108 >= (long)wantW * 100 && (long)haveH * 108 >= (long)wantH * 100;
}
int img_quality_room(long usedBytes, long budgetBytes, int w, int h, int variants) {
  if (budgetBytes <= 0 || w <= 0 || h <= 0 || variants >= 3) return 0;
  long bytes = (long)w * h * 4;
  return bytes <= budgetBytes / 4 && usedBytes + bytes < budgetBytes * 9 / 10;
}
