/* Subtitle files (SRT / WebVTT / SubStation, YouTube timed text; any of the usual encodings) as timed lines, the OpenSubtitles list, and language codes. */
#ifndef SB_SUBS_H
#define SB_SUBS_H
#include <stddef.h>
#include "core/stremio.h"
typedef struct { double s, e; char *t; } Cue;
int  subs_parse(const char *txt, Cue **out);   /* returns the count; free with subs_free */
int  subs_parse_body(const unsigned char *body, size_t len, const char *lang, Cue **out); /* a raw gzip body is unpacked first; UTF-16 / Windows-1252 become UTF-8
     (lang, the subtitle's language when known: a Central European one not in Unicode is read as Windows-1250) */
/* a subtitle file fetched and read on a thread of its own; the answer comes through subs_pump() on the main thread.
   status: HTTP status (0 = network failure); cues are the callback's (free with subs_free), n = 0 when nothing was readable */
void subs_load(const char *url, const char *lang, void (*cb)(int status, Cue *cues, int n, void *ud), void *ud); /* lang: as subs_parse_body's */
void subs_pump(void);                          /* every frame while the player is open */
void subs_free(Cue *c, int n);
/* languages: one canonical code per language whatever the source uses (639-2 B/T, 639-1, BCP-47) */
const char *subs_lang_canon(const char *code, char *out, size_t cap);
int  subs_lang_match(const char *a, const char *b);
const char *subs_lang_name(const char *code);  /* a track's language (eng / en / en-US) in English; NULL when unknown */
const char *subs_sub_name(const char *code);   /* an OpenSubtitles entry's name (its own codes too: spl, pob, ze...); NULL when unknown */
/* the built-in OpenSubtitles list for a video (asked by id alone); cb through subs_pump, subs malloc'd (the callback's
   owner frees them), ok: an answer was read */
void subs_list(const char *type, const char *vid, void (*cb)(SubEntry *subs, int n, int ok, void *ud), void *ud);
/* The subtitles for a video from every installed subtitles addon that serves its type and id (declared at all: movies
   and series always; the id prefixes apply), never the built-in list above; asked with the file's OpenSubtitles hash (hashable: the url can be read
   in ranges; it is worked out first, off the drawing thread), its size and file name when known. own: the subtitles
   the stream carried, listed first. cb (through subs_pump) after each answer with everything so far (malloc'd: the
   callback's owner frees it); final once every source answered or failed; ok: how many answered */
typedef struct { char type[16], vid[80], url[4096], filename[256], hash[24]; double size; int hashable; double fps; } SubQuery; /* fps: the picture's, when known (Auto-sync) */
void subs_find(const SubQuery *q, const SubEntry *own, int nown, void (*cb)(SubEntry *subs, int n, int final, int ok, void *ud), void *ud);
int  subs_addons_for(const char *type, const char *vid);   /* installed subtitles addons serving it */
int  subs_os_hash(const char *url, double *size, char out[17]);  /* worker threads only (it blocks); *size: known size or 0 */
int  subs_hash_of(const char *url, char out[24], double *size);  /* the hash subs_find worked out for this url, if it did (any thread) */
struct cJSON;
void subs_entry_facts(SubEntry *e, const struct cJSON *s);       /* an answer's entry: its file's name (rel) and whether the hash found it (hm) */
int  subs_parse_timedtext(const char *xml, Cue **out);  /* YouTube's caption XML (srv3 or the classic one) */
int  subs_parse_any(const char *txt, Cue **out);        /* timed text when it looks like it, else SRT / WebVTT */
#endif
