#include "core/backdrop_quality.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void check(const char *before, const char *after) {
 char buf[512]; snprintf(buf,sizeof buf,"%s",before); backdrop_tmdb_upgrade(buf,sizeof buf);
 if(strcmp(buf,after)){fprintf(stderr,"URL: %s\nGOT: %s\nWANT: %s\n",before,buf,after);assert(0);}
}
int main(void){
 check("https://image.tmdb.org/t/p/w300/abc.jpg", "https://image.tmdb.org/t/p/w1280/abc.jpg");
 check("https://image.tmdb.org/t/p/w342/abc.jpg", "https://image.tmdb.org/t/p/w1280/abc.jpg");
 check("https://image.tmdb.org/t/p/w400/abc.jpg", "https://image.tmdb.org/t/p/w1280/abc.jpg");
 check("https://image.tmdb.org/t/p/w500/abc.jpg", "https://image.tmdb.org/t/p/w1280/abc.jpg");
 check("https://image.tmdb.org/t/p/w780/abc.jpg", "https://image.tmdb.org/t/p/w1280/abc.jpg");
 check("https://image.tmdb.org/t/p/original/abc.jpg", "https://image.tmdb.org/t/p/w1280/abc.jpg");
 check("https://image.tmdb.org/t/p/w1280/abc.jpg", "https://image.tmdb.org/t/p/w1280/abc.jpg");
 check("https://image.tmdb.org/t/p/w1920/abc.jpg", "https://image.tmdb.org/t/p/w1920/abc.jpg");
 check("https://image.tmdb.org/t/p/h632/abc.jpg", "https://image.tmdb.org/t/p/h632/abc.jpg");
 check("https://image.tmdb.org/t/p/w399/abc.jpg", "https://image.tmdb.org/t/p/w399/abc.jpg");
 check("https://image.tmdb.org/t/p/w300/some.jpg?a=1", "https://image.tmdb.org/t/p/w1280/some.jpg?a=1");
 check("https://other.com/image.tmdb.org/t/p/w300/abc.jpg", "https://other.com/image.tmdb.org/t/p/w300/abc.jpg");
 check("https://image.tmdb.org/t/p/w300wrong/abc.jpg", "https://image.tmdb.org/t/p/w300wrong/abc.jpg");
 char b[80]="https://image.tmdb.org/t/p/w500/example.jpg", orig[80];strcpy(orig,b);
 backdrop_tmdb_upgrade(b,strlen(b));assert(!strcmp(b,orig));
 backdrop_tmdb_upgrade(b,sizeof b);assert(strstr(b,"w1280/"));
 puts("PASS: TMDB backdrop resolution, bounded and non-TMDB URLs");
 return 0;
}
