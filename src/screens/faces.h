/* Faces for the title page's people strip: each name's IMDb id and photo from IMDb's name suggestions (the entry
   with exactly that name, accents aside, else the most popular one when it is plainly the same person); anyone
   IMDb has no photo for gets a free one, Wikidata's (Wikimedia Commons) by IMDb id, then the photo of their
   Wikipedia page when that page says they work in film or television. Lookups that fail are asked again up to
   three rounds; answers are kept in memory. */
#ifndef SB_FACES_H
#define SB_FACES_H
/* called once per name when it is settled: id "" = unknown; img "" = no photo anywhere */
typedef void (*FaceCb)(int i, const char *id, const char *img, void *ud);
/* a new lookup for up to 16 names, for the title named (it tells apart people of the same name); any earlier one stops answering */
void faces_find(const char *const *names, int n, const char *title, FaceCb cb, void *ud);
void faces_cancel(void);
void faces_tick(void);   /* the retries' clock: once a frame from the screen that asked */
#endif
