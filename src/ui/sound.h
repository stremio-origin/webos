/* Interface sounds: short soft taps as you move around (Settings > Audio > Interface sounds). */
#ifndef SB_SOUND_H
#define SB_SOUND_H
enum { SND_FOCUS, SND_TAB, SND_FILTER, SND_TOGGLE, SND_HOLD, SND_PLAY, SND_START, SND_N };
void sound_play(int which);  /* cheap: the work is on the sound thread; a sound asked twice within 60 ms plays once */
void sound_focus(void);      /* sound_play(SND_FOCUS) */
double sound_last(void);     /* when a sound was last asked for (app_now's clock), so a general one can step aside */
void sound_keep(int on);     /* 1 while the channel grid is up on its own: the device stays open (no opening as a preview grows) */
void sound_quiet(int on);    /* 1 while the player is up: what is playing finishes, then the device is let go */
#endif
