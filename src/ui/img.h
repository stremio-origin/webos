/* Pictures: fetched and decoded on worker threads (disk cache in between), scaled there to the
   size they are shown at, uploaded on the main thread, kept under a memory budget. Screens ask
   for a texture every frame; 0 means "not yet" (and starts the load). */
#ifndef SB_IMG_H
#define SB_IMG_H
#include "ui/gl.h"
#include "ui/palette.h"

int    img_init(const char *cacheDir);
/* w,h = the size the picture is shown at (layout units); the decode scales to cover it. Returns the
   texture or 0. Also reports the texture's own aspect so the caller can cover-crop. */
GLuint img_get(const char *url, int w, int h, float *aspect);
/* An independent 1080p backdrop texture, so an earlier 720p request of the
   same URL cannot silently limit its source resolution. */
GLuint img_get_backdrop(const char *url, int w, int h, float *aspect);
int    img_backdrop_failed(const char *url, int w, int h);
void   img_urgent_backdrop(const char *url, int w, int h);
/* the same picture made to fit the box exactly (fit 0: stretched to it, 1: cover-cropped at the
   centre, 2: cover-cropped from the top) with its corners rounded by `radius` (layout units), so it
   draws with the plain image program. A variant of its own; shares the download with img_get. */
GLuint img_get_shaped(const char *url, int w, int h, float radius, int fit);
int    img_shaped_loaded(const char *url, int w, int h, float radius, int fit);
int    img_shaped_failed(const char *url, int w, int h, float radius, int fit);
int    img_palette(const char *url, int w, int h, int alpha, Palette *out); /* 1 ready, 0 loading, -1 failed */
int    img_failed(const char *url);      /* 1 when the picture could not be loaded */
int    img_loaded(const char *url);      /* 1 when ready */
const char *img_src_of(GLuint tex);     /* the address a plain picture was loaded from, or NULL */
void   img_prefetch(const char *url, int w, int h);
int    img_budget_mb(void);           /* the picture memory allowed (small on most TVs) */
void   img_urgent(const char *url, int w, int h);       /* the next one decoded (the backdrop waited on) */
void   img_prefetch_pri(const char *url, int w, int h); /* ahead of the queue, and never evicted (title art) */
void   img_pump(void);                   /* uploads finished decodes; call per frame (bounded work) */
void   img_frame(void);                  /* marks the frame (for eviction) */
void   img_release_unused(int keepMb);   /* evicts least recently drawn textures over the budget */
void   img_set_budget_mb(int mb);
void   img_set_draw_scale(float s);      /* surface pixels per layout unit when below 1: pictures are decoded at the size really drawn */
int    img_used_mb(void);
/* computes cover-crop uv for drawing a texture of aspect `ta` into a box of aspect `ba`, anchored at the top (posters) or centre */
void   img_cover_uv(float ta, float ba, int anchorTop, float *u0, float *v0, float *u1, float *v1);
#endif
void   img_queue_stats(int *queued, int *waiting); /* development: decodes queued, decoded pictures waiting for upload */
