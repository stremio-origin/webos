#ifndef SB_RESAMPLE_H
#define SB_RESAMPLE_H
/* Area-average when shrinking; centre-aligned, alpha-aware interpolation when
   any source axis needs enlargement. Result is a new allocated RGBA buffer. */
unsigned char *sb_resample_rgba(const unsigned char *src, int w, int h,
                                float cx, float cy, float cw, float ch,
                                int nw, int nh);
#endif
