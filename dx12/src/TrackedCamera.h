#pragma once
#include "CameraMath.h"
#include "XrScreen.h"
struct TrackingOrigin { float q[4]{0,0,0,1},p[3]{};bool valid{}; };
inline void MultiplyQ(const float* a,const float* b,float* o) {
 o[0]=a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1];
 o[1]=a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0];
 o[2]=a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3];
 o[3]=a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2];
}
inline void QMatrix(const float* q,float* r) {
 float x=q[0],y=q[1],z=q[2],w=q[3];
 r[0]=1-2*(y*y+z*z);r[1]=2*(x*y-z*w);r[2]=2*(x*z+y*w);
 r[3]=2*(x*y+z*w);r[4]=1-2*(x*x+z*z);r[5]=2*(y*z-x*w);
 r[6]=2*(x*z-y*w);r[7]=2*(y*z+x*w);r[8]=1-2*(x*x+y*y);
}
inline bool MakeTrackedCamera(const float* base,const float* projection,const TrackedEye& eye,const TrackingOrigin& origin,float* inverse,float* outProjection) {
 float check[12];if(!origin.valid || !OffsetCameraRight(base,0,check))return false;
 for(auto v:eye.quaternion)if(!std::isfinite(v))return false;
 for(auto v:eye.position)if(!std::isfinite(v))return false;
 for(auto v:origin.q)if(!std::isfinite(v))return false;
 for(auto v:origin.p)if(!std::isfinite(v))return false;
 float originNorm=0;for(auto v:origin.q)originNorm+=v*v;if(std::fabs(originNorm-1)>0.01f)return false;
 float norm=0;for(auto v:eye.quaternion)norm+=v*v;if(std::fabs(norm-1)>0.01f)return false;
 float iq[4]{-origin.q[0],-origin.q[1],-origin.q[2],origin.q[3]},relative[4],r[9],ref[9];
 MultiplyQ(iq,eye.quaternion,relative);QMatrix(relative,r);QMatrix(iq,ref);
 float local[3]{};
 for(int a=0;a<3;++a)for(int b=0;b<3;++b)local[a]+=ref[a*3+b]*(eye.position[b]-origin.p[b]);
 for(auto v:local)if(!std::isfinite(v) || std::fabs(v)>5)return false;
 for(int a=0;a<3;++a){
  for(int b=0;b<3;++b){inverse[a*4+b]=0;for(int c=0;c<3;++c)inverse[a*4+b]+=base[a*4+c]*r[c*3+b];}
  inverse[a*4+3]=base[a*4+3];for(int c=0;c<3;++c)inverse[a*4+3]+=base[a*4+c]*local[c];
 }
 // Preserve the game's RH depth mapping; replace only x/y FOV terms.
 for(int i=0;i<16;++i)if(!std::isfinite(projection[i]))return false;
 if(std::fabs(projection[14]+1)>0.001f || std::fabs(projection[15])>0.001f)return false;
 float t[4];for(int i=0;i<4;++i){if(!std::isfinite(eye.fov[i]) || std::fabs(eye.fov[i])>1.55f)return false;t[i]=std::tan(eye.fov[i]);}
 float w=t[1]-t[0],h=t[2]-t[3];if(w<=0.01f || h<=0.01f)return false;
 memcpy(outProjection,projection,16*sizeof(float));
 outProjection[0]=2/w;outProjection[2]=(t[1]+t[0])/w;
 outProjection[5]=2/h;outProjection[6]=(t[2]+t[3])/h;
 return true;
}
