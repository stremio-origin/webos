/* Forced subtitles (only the lines in another language and the signs): known by a track's flag, else by its name in
   any of the app's languages (from PR #50 by valentinBoch) */
#ifndef SB_FORCEDSUBS_H
#define SB_FORCEDSUBS_H
struct cJSON;
/* a track's or a list entry's own name or label says forced ("English Forced", "Signs & Songs", "Forcés", "強制") */
int forcedsubs_name(const char *name);
/* the same in a file's or a release's name, where a film's title can hold the word: "force" / "forces" alone don't count */
int forcedsubs_file(const char *name);
/* a subtitles answer's entry (an addon's, or the stream's own): a forced flag it carries, else its label, its name, its
   file's name, and only when there is none, its release's name */
int forcedsubs_entry(const struct cJSON *entry, const char *label);
#endif
