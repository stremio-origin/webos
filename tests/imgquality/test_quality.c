#include "ui/imgquality.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
 assert(img_quality_covers(300,120,300,120));
 assert(img_quality_covers(500,175,520,175)); /* minor resize jitter tolerated */
 assert(img_quality_covers(1280,720,500,280));
 assert(!img_quality_covers(300,120,520,175));
 assert(!img_quality_covers(400,225,589,331));
 assert(!img_quality_covers(520,175,500,220)); /* height matters as well */
 assert(!img_quality_covers(0,175,500,175));
 assert(!img_quality_covers(400,225,0,175));
 const long mb=1024L*1024;
 assert(img_quality_room(5*mb,40*mb,1280,720,1));
 assert(!img_quality_room(38*mb,40*mb,1280,720,1));
 assert(!img_quality_room(4*mb,40*mb,2560,1440,1)); /* bounded per-image allocation */
 assert(!img_quality_room(4*mb,40*mb,1280,720,3)); /* up to three variants */
 puts("PASS: resolution coverage, memory limits and bounded variant count");
 return 0;
}
