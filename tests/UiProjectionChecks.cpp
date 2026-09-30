#include "UiProjection.h"
#include "UiMaterial.h"
#include <cstdio>
int main() {
 for(float yaw: {0.f,0.8f}) {
  TrackedEye eyes[2]{};
  float q[]{0,sinf(yaw/2),0,cosf(yaw/2)},r[9];QMatrix(q,r);
  for(unsigned i=0;i<2;++i){
   memcpy(eyes[i].quaternion,q,sizeof(q));
   for(int a=0;a<3;++a)eyes[i].position[a]=r[a*3]*(i?0.032f:-0.032f);
   eyes[i].fov[0]=i?-0.65f:-0.95f;eyes[i].fov[1]=i?0.95f:0.65f;
   eyes[i].fov[2]=0.85f;eyes[i].fov[3]=-0.75f;
  }
  for(unsigned i=0;i<2;++i) {
   float warp[16];if(!MakeUiClipTransform(eyes,i,warp))return 1;
   for(float x:{-1.f,0.f,1.f})for(float y:{-1.f,0.f,1.f}) {
    float point[]{x,y,0.1f,1},clip[4]{};
    for(int a=0;a<4;++a)for(int b=0;b<4;++b)clip[a]+=warp[a*4+b]*point[b];
    float l=tanf(eyes[i].fov[0]),rr=tanf(eyes[i].fov[1]),u=tanf(eyes[i].fov[2]),d=tanf(eyes[i].fov[3]);
    float recoveredX=2*((clip[0]/clip[3])*(rr-l)/2+(rr+l)/2)+(i?0.032f:-0.032f);
    float recoveredY=2*((clip[1]/clip[3])*(u-d)/2+(u+d)/2);
    if(fabsf(recoveredX-x*1.2f)>1e-5f || fabsf(recoveredY-y*0.675f)>1e-5f)return 2;
    if(clip[2]<=0 || clip[2]>=clip[3])return 3;
   }
   // Native aspect-fit points must land on the same physical panel at any
   // eye resolution, without stretching the 16:9 canvas.
   for(float aspect:{0.84f,1.f,16.f/9.f,2.4f}) {
    float adjusted[16];memcpy(adjusted,warp,sizeof(adjusted));UndoUiLetterbox(aspect,adjusted);
    float sx=aspect>16.f/9.f?(16.f/9.f)/aspect:1.f;
    float sy=aspect<16.f/9.f?aspect/(16.f/9.f):1.f;
    for(float x:{-1.f,0.6f,1.f})for(float y:{-1.f,0.4f,1.f}) {
     float full[]{x,y,0.2f,1},fit[]{x*sx,y*sy,0.2f,1};
     for(int row=0;row<4;++row){float a=0,b=0;for(int k=0;k<4;++k){a+=warp[row*4+k]*full[k];b+=adjusted[row*4+k]*fit[k];}if(fabsf(a-b)>1e-5f)return 5;}
    }
   }
   float native[]{2,0,0.1f,0,0,3,-0.2f,0,0,0,-1,-0.1f,0,0,-1,0},combined[16];
   TransformUiProjection(warp,native,combined);
   float p[]{0.1f,0.2f,-2,1},a[4]{},b[4]{},c[4]{};
   for(int row=0;row<4;++row)for(int col=0;col<4;++col){a[row]+=native[row*4+col]*p[col];c[row]+=combined[row*4+col]*p[col];}
   for(int row=0;row<4;++row)for(int col=0;col<4;++col)b[row]+=warp[row*4+col]*a[col];
   for(int j=0;j<4;++j)if(fabsf(b[j]-c[j])>1e-5f)return 4;
  }
 }
 {
  alignas(16) unsigned char source[0x810]{},output[0x810]{};
  float identity[16]{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
  memcpy(source+16,identity,64);memcpy(source+96,identity,64);
  float clip[16];memcpy(clip,identity,64);clip[0]=0.7f;clip[3]=0.2f;
  auto entry=[](unsigned src,unsigned dst,unsigned size){return src|(dst<<11)|(size<<22);};
  uint32_t map[]{entry(0,0,32),entry(32,32,32),entry(64,64,16),entry(80,80,64)};
  if(!PatchUiMaterialValues(map,4,5,clip,source,output,sizeof(output)))return 6;
  if(memcmp(output+16,clip,64)||memcmp(output+96,clip,64)||memcmp(source+16,identity,64))return 7;
  map[1]=entry(33,32,32);if(PatchUiMaterialValues(map,4,5,clip,source,output,sizeof(output)))return 8;
  map[1]=entry(32,32,32);map[2]=entry(0,64,16);if(PatchUiMaterialValues(map,4,5,clip,source,output,sizeof(output)))return 9;
  map[2]=entry(64,64,16);if(PatchUiMaterialValues(map,4,5,clip,source,output,100))return 10;
 }
 puts("PASS asymmetric FOV binocular UI convergence, common 16:9 panel, rotated head, depth, matrix composition and eye-resolution aspect fit");
}
