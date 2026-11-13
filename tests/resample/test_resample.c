#include "ui/resample.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static void near(int got,int want,const char *where) {
  if(abs(got-want)>1){fprintf(stderr,"%s: got %d want %d\n",where,got,want);abort();}
}
static void gradient(void) {
  unsigned char src[]={0,0,0,255, 255,255,255,255};
  unsigned char *dst=sb_resample_rgba(src,2,1,0,0,2,1,4,1);assert(dst);
  const int exp[]={0,64,191,255};
  for(int x=0;x<4;x++){for(int c=0;c<3;c++)near(dst[x*4+c],exp[x],"gradient");assert(dst[x*4+3]==255);}free(dst);
}
static void centre_2d(void) {
  unsigned char src[]={0,0,0,255, 100,100,100,255, 150,150,150,255, 250,250,250,255};
  unsigned char *dst=sb_resample_rgba(src,2,2,0,0,2,2,3,3);assert(dst);
  near(dst[(1*3+1)*4],125,"2D interpolation");assert(dst[0]==0);assert(dst[(2*3+2)*4]==250);free(dst);
}
static void transparency(void) {
  unsigned char src[]={255,0,0,255, 0,0,0,0};
  unsigned char *dst=sb_resample_rgba(src,2,1,0,0,2,1,4,1);assert(dst);
  near(dst[4+3],191,"alpha 25pct");near(dst[8+3],64,"alpha 75pct");
  assert(dst[4]==255 && dst[8]==255); /* no black halo */
  assert(dst[12+3]==0 && dst[12]==0);free(dst);
}
static void reduction(void) {
  unsigned char src[]={0,0,0,255, 40,40,40,255, 80,80,80,255, 120,120,120,255};
  unsigned char *dst=sb_resample_rgba(src,2,2,0,0,2,2,1,1);assert(dst);
  assert(dst[0]==60&&dst[1]==60&&dst[2]==60&&dst[3]==255);free(dst);
}
static void mixed(void) {
  /* Stretch horizontally while shrinking vertically: retain area average vertically. */
  unsigned char src[2*4*4];
  for(int y=0;y<4;y++)for(int x=0;x<2;x++){
    unsigned char *p=src+(y*2+x)*4;p[0]=x?200:0;p[1]=y*40;p[2]=0;p[3]=255;
  }
  unsigned char *dst=sb_resample_rgba(src,2,4,0,0,2,4,4,1);assert(dst);
  const int expected[]={0,50,150,200};
  for(int x=0;x<4;x++){near(dst[x*4],expected[x],"mixed horizontal");near(dst[x*4+1],60,"mixed vertical");}
  free(dst);
}
static void crop_bounds(void) {
  unsigned char src[]={10,10,10,255, 30,30,30,255, 50,50,50,255};
  unsigned char *dst=sb_resample_rgba(src,3,1,1,0,2,1,4,1);assert(dst);
  near(dst[0],30,"crop left");near(dst[12],50,"crop right");free(dst);
  assert(!sb_resample_rgba(src,3,1,-1,0,2,1,4,1));
  assert(!sb_resample_rgba(src,3,1,2,0,2,1,4,1));
  assert(!sb_resample_rgba(src,3,1,0,0,2,1,0,1));
  assert(!sb_resample_rgba(src,3,1,0,0,NAN,1,4,1));
  /* A single source pixel must replicate without reading a neighbour. */
  unsigned char solid[]={21,65,137,255};
  dst=sb_resample_rgba(solid,1,1,0,0,1,1,7,5);assert(dst);
  for(int i=0;i<7*5;i++)for(int c=0;c<4;c++)assert(dst[4*i+c]==solid[c]);
  free(dst);
  /* Alpha fully absent: deterministic black transparent output. */
  unsigned char clear[]={255,100,20,0};
  dst=sb_resample_rgba(clear,1,1,0,0,1,1,3,4);assert(dst);
  for(int i=0;i<12*4;i++)assert(dst[i]==0);
  free(dst);
  /* Vertical interpolation and mixed vertical-upscale / horizontal-reduction. */
  unsigned char vertical[]={10,0,0,255, 200,0,0,255};
  dst=sb_resample_rgba(vertical,1,2,0,0,1,2,1,4);assert(dst);
  near(dst[4],58,"vertical interpolation");near(dst[8],153,"vertical interpolation 2");free(dst);
}
static void reference_area(const unsigned char *src,int w,int h,float cx,float cy,float cw,float ch,int nw,int nh,unsigned char *out){
  for(int y=0;y<nh;y++){
    int y0=(int)(cy+y*ch/nh),y1=(int)(cy+(y+1)*ch/nh);
    if(y1<=y0)y1=y0+1;
    if(y0>=h)y0=h-1;
    if(y1>h)y1=h;
    for(int x=0;x<nw;x++){
      int x0=(int)(cx+x*cw/nw),x1=(int)(cx+(x+1)*cw/nw);
      if(x1<=x0)x1=x0+1;
      if(x0>=w)x0=w-1;
      if(x1>w)x1=w;
      unsigned sum[4]={0}, n=0;
      for(int yy=y0;yy<y1;yy++)for(int xx=x0;xx<x1;xx++){
        const unsigned char *p=src+((size_t)yy*w+xx)*4;
        for(int c=0;c<4;c++)sum[c]+=p[c];
        n++;
      }
      for(int c=0;c<4;c++)out[((size_t)y*nw+x)*4+c]=(unsigned char)(sum[c]/n);
    }
  }
}
static void shrink_regression(void){
  unsigned seed=0xC0FFEEu;
  for(int trial=0;trial<200;trial++){
    seed=seed*1664525u+1013904223u;int w=2+(seed%29);
    seed=seed*1664525u+1013904223u;int h=2+(seed%29);
    size_t bytes=(size_t)w*h*4;unsigned char *src=malloc(bytes);assert(src);
    for(size_t k=0;k<bytes;k++){seed=seed*1664525u+1013904223u;src[k]=(unsigned char)(seed>>24);}
    float cx=(float)(w>6?2:0),cy=(float)(h>6?2:0),cw=(float)w-cx,ch=(float)h-cy;
    int nw=1+(w/3),nh=1+(h/3);
    unsigned char *out=sb_resample_rgba(src,w,h,cx,cy,cw,ch,nw,nh);assert(out);
    unsigned char *ref=malloc((size_t)nw*nh*4);assert(ref);
    reference_area(src,w,h,cx,cy,cw,ch,nw,nh,ref);
    for(size_t k=0;k<(size_t)nw*nh*4;k++)if(out[k]!=ref[k]){
      fprintf(stderr,"downscaling changed at trial %d byte %zu\n",trial,k);abort();
    }
    free(src);free(out);free(ref);
  }
}
int main(void) {
  gradient();centre_2d();transparency();reduction();mixed();crop_bounds();shrink_regression();
  puts("PASS bilinear, premultiplied-alpha, 200 byte-for-byte shrink regressions, mixed-scale, crop and bounds");return 0;
}
