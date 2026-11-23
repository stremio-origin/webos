/* Updates: once a start (calm, at most every 6 hours unless asked) the newest release of THIS package
   (<appid>_<x.y.z>_arm.ipk on GitHub, the project's Homebrew listing as the fallback) is compared with the
   running version; a newer one shows the notice from the Settings icon, and Settings > About installs it
   through the Homebrew Channel, which checks the file's SHA-256. */
#ifndef SB_UPDATE_H
#define SB_UPDATE_H

enum { UPD_IDLE = 0, UPD_CHECKING, UPD_AVAIL, UPD_NONE, UPD_ERROR, UPD_INSTALLING };
void        update_tick(float dt);          /* every frame (the start check, the install's replies, the notice) */
void        update_check(int manual);       /* manual: now, with a toast for "latest" / "could not check" */
void        update_install(void);
const char *update_current(void);           /* the running version (appinfo.json) */
const char *update_avail(void);             /* the newer version, or NULL */
int         update_note_count(void);        /* "What's new": the release's first bullet points */
const char *update_note(int i);
int         update_state(void);
const char *update_msg(void);               /* the install's progress or error ("" = none) */
unsigned    update_serial(void);            /* changes whenever any of the above does */

/* the notice: "Update available <ver>" sliding out of the Settings icon for 10 s */
int   update_pill_focused(void);
float update_dot_hide(void);                /* 0..1: the Settings icon's green dot steps aside while the notice is out */
/* a key on the top bar: 0 = not taken (go on as usual), 1 = taken. onSettings: the Settings icon has the highlight */
int   update_pill_key(int k, int onSettings);
void  update_pill_draw(float setX, float y);
void  update_open(void);                    /* Settings > About */
/* another notice in the same capsule (a USB drive found): a question and its one answer; OK (or a click) calls ok.
   It waits while the update's notice is out; ignored, it slides back after 10 s and nothing changes */
void  update_notice(const char *label, const char *badge, void (*ok)(void));
int   update_notice_key(void);  /* the red key while a notice is out: it takes you there (1), else 0 */
void  update_notice_cancel(void (*ok)(void)); /* the question no longer stands (the drive went) */
#endif
