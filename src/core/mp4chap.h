#ifndef SB_MP4CHAP_H
#define SB_MP4CHAP_H
/* An MP4 / M4V / MOV file's own chapters, read over the network on a thread of their own: the Nero list (moov/udta/chpl)
   or the QuickTime chapter track (a text track the video's tref "chap" points to). A few small ranged reads: the
   file's boxes are walked by their headers and only the chapter parts are read whole. */
typedef struct { double s; char name[96]; } Mp4Chapter;
/* cb on the drawing thread: n chapters (sorted by start), 0 when the file has none, -1 when it is no MP4 or could not
   be read. The list is only valid during the call */
void mp4_chapters(const char *url, void (*cb)(const char *url, const Mp4Chapter *list, int n, void *ud), void *ud);
#endif
