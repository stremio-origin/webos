/* The app behind another app (#69). The TV tells an app it lost the focus the same way when another app comes to the
   front and when one of its own overlays opens over it (the Home bar, quick settings, its screen saver), and the app
   stays visible under an overlay. So two states:
   - not focused (either): the background trailer stops and none starts; everything else as in front;
   - behind (not focused, and the TV says another app is in front): nothing is drawn, a film playing is paused (where
     it was kept, Trakt told), the loop turns ten times a second for the network, a phone, the TV's own messages.
   Which app is in front comes from the TV (getForegroundAppInfo, subscribed); where it is refused, never answers, or
   does not name this app while it is in front, the app is never taken as behind (a film is never paused). Back in
   front: drawn again at once; a film paused for it stays paused, its controls up. Main thread. */
#ifndef SB_AWAY_H
#define SB_AWAY_H
int  app_focused(void);
int  app_behind(void);
void away_focus(int lifecycle);   /* plat_lifecycle's answer for an event: -1 to the back, 1 back in front, 0 none */
void away_tick(void);             /* once a frame (in front and behind) */
#endif
