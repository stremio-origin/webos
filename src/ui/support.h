/* The project's two codes in one place: a coffee (Ko-fi) and the star on GitHub, one at a time (some phones read only
   one code in view), with a two-part pill above that says which; switching turns the code over (it narrows to its
   edge and opens as the other) while the pill's light glides across. Settings > About and the guide's last page. */
#ifndef SB_SUPPORT_H
#define SB_SUPPORT_H
/* at x, y (the pill's top left; it is support_w wide), the code Q wide; sel 0..1: which one shows (0 the coffee, 1 GitHub, between: turning);
   f: its row has the focus (the pill in the accent). Returns the height taken */
float support_draw(float x, float y, float Q, float sel, int f, const char *coffee, const char *coffeeShown, const char *gh, const char *ghShown);
float support_h(float Q);
/* the picture behind the next pill (full screen, upside down as targets are), for its liquid glass; 0 the plain shell */
void  support_glass(unsigned int tex);
void  support_glass_picture(unsigned int tex); /* the same, a plain picture the right way up (a backdrop) */
float support_w(float Q);   /* the width taken: the pill's (the code is centred under it) */
/* the same with the pill and its words k times larger than in proportion (the support card; the others use 1) */
float support_draw_k(float x, float y, float Q, float k, float sel, int f, const char *coffee, const char *coffeeShown, const char *gh, const char *ghShown);
float support_w_k(float Q, float k);
float support_h_k(float Q, float k);
#endif
