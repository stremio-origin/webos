/* Text in one of the Windows code pages (old subtitle files): which page, from the language when it is known, else from
   the text itself, and each byte's character. */
#ifndef SB_TEXTENC_H
#define SB_TEXTENC_H
#include <stddef.h>
enum { CP_1252 = 0, CP_1250, CP_1251, CP_1253, CP_1254, CP_1255, CP_1256, CP_1257, CP_1258 };
int      textenc_pick(const unsigned char *b, size_t n, const char *lang); /* lang: a three-letter code ("hun"), NULL or "" when unknown */
unsigned textenc_char(int cp, unsigned char c);                          /* the byte's Unicode character in that page (ASCII as it is) */
#endif
