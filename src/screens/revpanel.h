/* The reviews panel: one glass panel over the page with a title's, a season's or an episode's ratings and comments
   from every source the TV can reach (core/reviews.c). It grows out of the still or poster it was asked from and
   folds back into it. Hold OK on an episode, the title page's Reviews button, or Reviews in a card's menu. */
#ifndef SB_REVPANEL_H
#define SB_REVPANEL_H
#include "core/stremio.h"
#include "core/reviews.h"
/* it: the title, or an episode card (ep set, videoId "tt..:s:e"); stackSeason: the season the title page shows (a
   switch between the show and that season; 0 = none); fx..fr: the box it grows out of (fw 0 = the middle) */
void revpanel_show(const Item *it, int stackSeason, float fx, float fy, float fw, float fh, float fr);
void revpanel_seasons(const int *list, int n); /* before revpanel_show on a series: the seasons its switch steps through */
void revpanel_from_button(void); /* before revpanel_show: it grows out of a menu option (the box given) as that option's highlight */
float revpanel_card_k(void);     /* 0..1: how far the panel (not the page one) is open: what it came from steps back */
void revpanel_show_page(const Item *it); /* the title page's Down: it rises from the page's foot and Up from its top row lets it down */
float revpanel_page_k(void);   /* 0..1: how far it has risen into the title page (0 not there) */
int  revpanel_page_btns(void);
void revpanel_page_close(void);
int  revpanel_page_episode(Item *out); /* risen for an episode: 1, and that episode (the page's Watched button acts on it) */  /* risen: let it down again (the Reviews button pressed a second time) */  /* risen, with the title page's buttons in focus */
/* its parts, for the previews that show what a key brings (Settings, the guide): drawn as the panel draws them */
float revpanel_draw_score(float x, float y, float w, float score, const char *src, int votes, const int *dist, int pending); /* the big score, its votes, the spread; the height taken */
float revpanel_comment_h(const RevComment *c, float w, int lines);
int   revpanel_comment_lines(const RevComment *c, float w, int max);   /* the lines its words take, at most max */
float revpanel_draw_comment(float x, float y, float w, const RevComment *c, int lit, int lines, float when);      /* when: the "n days ago" at k (0 none) */
void  revpanel_draw_ring(float cx, float y, float d, const char *src, const char *val, float g); /* a source's ring round its mark, filled g of the way to its score, the score under it */
float revpanel_badge(float x, float y, float h, const char *src, float share, int rtl); /* the source's mark (share: its score of 1, for its colour); the width */
const RevComment *revpanel_sample(int i);   /* 0, 2: Trakt comments, 1: a TMDB review (in the app's language) */
const int *revpanel_sample_dist(void);      /* a spread of ratings, 1..10 */
#endif
