/* The code card: a card over the page with a QR to open on the phone (the TMDB key page, the Trakt id page, an
   addon's outside link). Any key or click closes it. When the phone did what the card was for (the code was
   opened, a key arrived) it shows a tick and goes away by itself. It is an overlay (app_overlay): keys go
   to it first. */
#ifndef SB_QRCARD_H
#define SB_QRCARD_H
#include <stdint.h>

typedef struct {
  const char *title, *sub;      /* already translated */
  const char *url;              /* what the code opens; NULL or "" = no network address (the card says so) */
  int (*check)(void *ud);       /* asked every 1.5 s: 1 = done (the tick); NULL = never */
  const char *doneTitle, *doneSub; /* the tick's words (doneSub may be NULL) */
  float doneHold;               /* seconds the tick stays before the card goes */
  void (*onClose)(void *ud);    /* once it is closed, however that happened */
  void *ud;
  const char *badge;            /* a service's mark (an icon's name) over the title, white on badgeBg; NULL = none */
  uint32_t badgeBg;             /* the service's own colour: the badge, the tick when it is done */
  int code;                     /* the title is a code to type: large and spaced; the address under the QR is left out */
} QrCardOpts;

void qrcard_show(const QrCardOpts *o);
void qrcard_close(void);

/* the ready-made cards */
/* Settings > Language > Titles and descriptions: the TMDB key page; onClose(hasKey, ud) once it is closed */
void qrcard_show_tmdb(void (*onClose)(int hasKey, void *ud), void *ud);
/* Settings > Keys > Ratings and reviews: the Trakt client id page */
void qrcard_show_trakt(void (*onClose)(int hasKey, void *ud), void *ud);
#endif
