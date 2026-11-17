/* The player: plays over the app (the app rests behind the title art), Back fades straight
   back to the exact screen. OSD, seeking, trays (episodes, sources, subtitles, audio), skip
   prompts, the next-up card, subtitles. */
#ifndef SB_PLAYER_H
#define SB_PLAYER_H
#include "core/stremio.h"
#include "core/trailer.h"
void player_init(void);
void player_play(const Stream *s, const char *type, const char *id, const char *vid, int fromStart);
/* the title's art at once, before anything plays (a trailer being found: type "other", trailerName for its tag); then
   player_status / player_fail say how it goes on the art, player_close takes it away, and player_play starts behind it.
   player_waiting: that art is up and still waiting (Back closes it) */
void player_prepare(const char *type, const char *id, const char *vid, const char *trailerName);
void player_status(const char *text);
void player_fail(const char *text);
void player_close(void);
int  player_waiting(void);
/* "Play the best stream automatically": the title art and the wait line at once, Back cancels, the pick plays */
void player_choose_best(const char *type, const char *id, const char *vid);
void player_cast_best(const char *type, const char *id, const char *vid, double pos); /* a phone's playbest: the same choice, then from pos (0: from the start) */
int  player_open(void);
int  player_nerd_lines(char (*out)[120], int max); /* Stats for nerds: what plays and how */
int  player_art_full(void);
int  player_live(void);           /* the picture is up: the window must be transparent */
float player_cover(void);         /* how much of the page the player covers (0..1): it fades over the page and off it */
int  player_page_under(void);     /* the page must be drawn under the player this frame */
void player_key(int k, int down);
void player_tick(float dt);
void player_draw(void);
/* the next play's captions when it is a trailer (type "other"): set before player_play, shown with the CC button; the
   trailer's own name (the tag over the art and in the title block) is the stream's name */
void player_trailer_subs(const TrailerSub *subs, int n);
/* the next play's fallback URL: the same file another way, tried once when the first one fails */
void player_fallback(const char *url);
/* the next play's captions when it is a trailer (type "other"): IMDb's or YouTube's tracks, fetched with
   trailer_sub_load when chosen; n = 0 for none */
#include "core/trailer.h"
void player_trailer_subs(const TrailerSub *subs, int n);
/* a file's chapters, the best source for the skip prompts: a reader that can list them sets the hook; it is called
   when the skips are looked up and answers once with player_chapters() (n = 0: none) */
typedef struct { char name[96]; double s, e; } PlayerChapter;
extern void (*player_chapters_hook)(const char *url, int p2p);
void player_chapters(const char *url, const PlayerChapter *list, int n);
int  player_paused(void);
/* cast: the next play's subtitles as sent (n 0: none), a play at a moment (pos 0: where it was left), the phone's
   commands (1 pause, 2 resume, 3 seek to pos, 4 stop) and the state it is told (0 idle, 1 loading, 2 playing, 3 paused, 4 ended, 5 error) */
void player_next_subs(const char *const *urls, const char *const *langs, int n);
void player_next_art(const char *name, const char *poster, const char *bg, const char *logo); /* the next play's title and pictures while its details load (a phone's) */
void player_play_at(const Stream *s, const char *type, const char *id, const char *vid, double pos);
void player_cast(int cmd, double pos);
int  player_cast_state(double *pos, double *dur, char *title, int cap);
int  player_cast_previews(int *pct, int *target, int *eta); /* waiting for the seek previews: 1 and how far, else 0 */
int player_cast_sync(char *st, int cap);  /* Sync for a phone's remote: 1 running, 0 done, -1 nothing to tell */
const char *player_p2p_line(void);       /* a torrent's peers and speed ("" when none) */
void player_cast_chips(void *arr);         /* the info line's chips into a cJSON array, for a phone's remote */
void player_cast_info(char *out, int cap); /* the stream in a few words, for a phone's remote */
void player_cast_ident(char *type, int tc, char *id, int ic, char *vid, int vc); /* what plays (a phone's remote follows it) */
char *player_cast_tracks(void);                               /* the subtitles and audio as a "tracks" message (malloc'd), NULL when nothing plays */
void player_cast_track(const char *kind, const char *id, int ms); /* "sub" id|"off", "audio" id, "subdelay" ms: as a pick in the trays */   /* a film open, its picture up and paused */
void player_title_hint(const Item *it);
void player_quiet_next(void);
void player_adopt_next(const char *src, float x, float y, float w, float h, float radius); /* the next play is the preview playing in that box: taken as it plays, grown to the screen */
int player_page_morphing(void); /* a preview growing to the screen or back into its box: the page under it is still, drawn once */
float player_page_alpha(void);  /* the page under the player: faded as a preview grows */
void player_zap_begin(void);   /* switching channels full screen: the picture fades to black until the next one runs */
void player_zap_end(void);     /* the switch found nothing: the old picture fades back */   /* the next play is a switch of channel: no art while it loads (the corner pill says it) */ /* a channel's name and pictures from its card, until its details come */
#endif
