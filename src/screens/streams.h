/* The streams sheet: slides in from the right over the title page. */
#ifndef SB_STREAMS_H
#define SB_STREAMS_H
void streams_empty_note(const char *text); /* after streams_show: the words instead of "No streams found." */
void streams_show(const char *type, const char *id, const char *vid, const char *title, const char *epLabel);
void streams_close(int instant);
void streams_show_over_player(const char *type, const char *id, const char *title); /* a channel's streams over the playing picture */
int  streams_over_player(void);       /* shown there (it has the keys) */
void streams_over_frame(float dt);    /* the player's side: ticked and drawn over the picture */
int  streams_over_key(int k);
int  streams_open(void);
#endif
