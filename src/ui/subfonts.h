/* The subtitle fonts the viewer picks (Settings > Subtitles, the player's subtitle tray): Default (the app's own) and
   ten each for Latin and Arabic script, drawn by the text engine where a line has every letter in them */
#ifndef SB_SUBFONTS_H
#define SB_SUBFONTS_H
#define SUBF_N 11
extern const char *const SUBF_LAT[SUBF_N], *const SUBF_AR[SUBF_N]; /* [0] "Default" (translated where shown) */
int subfont_lat(void);          /* the Latin choice 0-10 (subsFont) */
int subfont_ar(void);           /* the Arabic choice 0-10 (subsFontAr) */
int subfont_w(int weight);      /* that weight in the viewer's subtitle fonts */
#endif
