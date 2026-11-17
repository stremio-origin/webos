/* "Original" audio: the track in the title's original language (TMDB's original_language), when the file has one.
   Pure: the caller gives the tracks and how to compare two language codes, so a test can stand in. */
#ifndef SB_AUDORIG_H
#define SB_AUDORIG_H
typedef struct { const char *lang, *name; int def, bad; } AudInfo; /* def: flagged default in the file; bad: its sound won't play on this TV */
/* the track to play for orig ("ja", "es"...): among those in that language, never a commentary, one whose sound plays
   here before one whose doesn't, one named "Original" first, then the file's default, then the first; -1 when no track
   is in that language (the file's own choice stays) */
int aud_pick_original(const char *orig, const AudInfo *t, int n, int (*match)(const char *a, const char *b));
#endif
