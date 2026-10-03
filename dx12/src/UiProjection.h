#pragma once
#include "TrackedCamera.h"

// Map the native GUI's normalized screen coordinates to a common head-space
// 16:9 panel (2.4 m wide, 2 m away), then through the actual eye projection.
inline bool MakeUiClipTransform(const TrackedEye* eyes,unsigned index,float* out) {
 if(index>1)return false;
 float q[4],norm=0,dot=0;
 for(int i=0;i<4;++i)dot+=eyes[0].quaternion[i]*eyes[1].quaternion[i];
 for(int i=0;i<4;++i){q[i]=eyes[0].quaternion[i]+(dot<0?-1:1)*eyes[1].quaternion[i];norm+=q[i]*q[i];}
 if(!std::isfinite(norm)||norm<0.01f)return false;
 for(auto& v:q)v/=sqrtf(norm);
 float head[9],eye[9],rotation[9]{},offset[3]{};
 QMatrix(q,head);QMatrix(eyes[index].quaternion,eye);
 for(int a=0;a<3;++a)for(int k=0;k<3;++k){
  offset[a]+=eye[k*3+a]*((eyes[0].position[k]+eyes[1].position[k])*0.5f-eyes[index].position[k]);
  for(int b=0;b<3;++b)rotation[a*3+b]+=eye[k*3+a]*head[k*3+b];
 }
 const auto& f=eyes[index].fov;
 float l=tanf(f[0]),r=tanf(f[1]),u=tanf(f[2]),d=tanf(f[3]);
 if(!(r-l>0.01f)||!(u-d>0.01f))return false;
 float panel[3][4]{};
 for(int a=0;a<3;++a){panel[a][0]=1.2f*rotation[a*3];panel[a][1]=0.675f*rotation[a*3+1];panel[a][3]=offset[a]-2*rotation[a*3+2];}
 for(int j=0;j<4;++j){
  out[j]=(2*panel[0][j]+(r+l)*panel[2][j])/(r-l)/2;
  out[4+j]=(2*panel[1][j]+(u+d)*panel[2][j])/(u-d)/2;
  // Keep the transform invertible: GuiCamera also calculates inverse VP.
  // Compress native GUI depth into a small overlay range, retaining ordering.
  out[8+j]=0.49f*(-panel[2][j]/2)+(j==2?0.01f:0.f);
  out[12+j]=-panel[2][j]/2;
 }
 for(int i=0;i<16;++i)if(!std::isfinite(out[i]))return false;
 return true;
}
inline void TransformUiProjection(const float* clip,const float* native,float* out) {
 for(int a=0;a<4;++a)for(int b=0;b<4;++b){out[a*4+b]=0;for(int k=0;k<4;++k)out[a*4+b]+=clip[a*4+k]*native[k*4+b];}
}

// Undo native aspect-fit layout before mapping normalized GUI coordinates onto
// a physical 16:9 panel. This preserves text proportions for tall eye textures.
inline void UndoUiLetterbox(float aspect,float* clip) {
 const float panelAspect=16.f/9.f;
 float x=aspect>panelAspect?aspect/panelAspect:1.f;
 float y=aspect<panelAspect?panelAspect/aspect:1.f;
 for(int row=0;row<4;++row){clip[row*4]*=x;clip[row*4+1]*=y;}
}
