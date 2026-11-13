#ifndef SB_IMGQUALITY_H
#define SB_IMGQUALITY_H
/* Plain (unshaped) images can be reused only if their decoded resolution is
   sufficient for a new layout. Allow a small (<=8%) stretch to avoid creating
   a variant for every incidental width change during layout animation. */
int img_quality_covers(int haveW, int haveH, int wantW, int wantH);
/* Bound the extra GPU memory while a sharper variant loads; existing art is
   always retained when the memory budget is tight. */
int img_quality_room(long usedBytes, long budgetBytes, int w, int h, int variants);
#endif
