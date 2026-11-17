/* The support card's timing: a count of the app's real starts (a TV relaunching it on its own, webOS's
   "autoReload", is not one). Due on the 5th start; each close puts the next one further: +10 starts, then +30,
   +50, then +100 every time (the table GAPS in supcard.c). It stays due until it has shown; "Don't show again" ends
   it for good. Kept in the local store only (supStarts, supNext, supCloses, supNever): nothing sent anywhere.
   The store is reached through two functions so a test can stand in for it. */
#ifndef SB_SUPCARD_H
#define SB_SUPCARD_H
typedef struct { int (*get)(const char *key, int def); void (*set)(const char *key, int v); } SupStore;
int  supc_gap(int closes);                          /* the starts to wait after this many closes (0: the first time) */
void supc_start(const SupStore *s, int autoReload); /* once per run, at start */
int  supc_due(const SupStore *s);                   /* due now (and not ended for good) */
void supc_shown(const SupStore *s);                 /* it showed and was closed: the next 5th start */
void supc_never(const SupStore *s);                 /* "Don't show again" */
const SupStore *supc_store(void);                   /* the app's own store */

/* The card's keys, apart from the drawing so a test can walk them. The codes have the focus first: OK turns them
   over (the coffee first, side 0; GitHub side 1), Left / Right pick a side (the codes' pill is drawn the same way in
   both directions, the coffee on the left). Down reaches "Don't show again" (never 1), Up comes back, OK there ends
   it for good. Back closes from anywhere. Returns what the key did. */
enum { SUPK_OK, SUPK_BACK, SUPK_UP, SUPK_DOWN, SUPK_LEFT, SUPK_RIGHT };
enum { SUPA_NUDGE, SUPA_TURN, SUPA_CLOSE, SUPA_MOVE, SUPA_NEVER };
typedef struct { int side, never; } SupFocus;
SupFocus supc_focus_start(void);
int  supc_key(SupFocus *s, int key, int hasGh);
#endif
