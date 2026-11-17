#ifndef SB_PREVSPIKE_H
#define SB_PREVSPIKE_H
/* seek previews, the measuring spike (development only, SB_PREVSPIKE): see prevspike.c */
#include "core/mkvsubs.h"
void prevspike_tick(MkvSession *mkv, int playing, double pos, double ahead); /* the player, every second while a film plays (ahead -1: not known) */
void prevspike_stop(void);
int  prev_available(void); /* the converter library beside the app carries the preview decoder (loaded once, on first ask) */
int  prev_enabled(void);   /* the viewer's setting (Settings > Playback): on unless switched off */                                                   /* the film closes or changes */
#endif
