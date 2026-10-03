#pragma once
#include "UiProjection.h"

// The native screen-center reticle represents the untracked camera's aim.
// Project that direction independently into each eye, preserving reticle size.
// It is a direction indicator, not a new collision/ballistics raycast.
inline bool MakeAimUiClip(const TrackedEye* eyes,unsigned index,const TrackingOrigin& origin,const float* ui,float* out){
 if(index>1||!origin.valid)return false;
 float norm=0;for(float v:origin.q){if(!std::isfinite(v))return false;norm+=v*v;}
 if(fabsf(norm-1)>0.01f)return false;
 norm=0;for(float v:eyes[index].quaternion){if(!std::isfinite(v))return false;norm+=v*v;}
 if(fabsf(norm-1)>0.01f)return false;
 float iq[4]{-origin.q[0],-origin.q[1],-origin.q[2],origin.q[3]},relative[4],rotation[9];
 MultiplyQ(iq,eyes[index].quaternion,relative);QMatrix(relative,rotation);
 float x=-rotation[6],y=-rotation[7],z=-rotation[8];
 float t[4];for(int i=0;i<4;++i){float f=eyes[index].fov[i];if(!std::isfinite(f)||fabsf(f)>1.55f)return false;t[i]=tanf(f);}
 if(t[1]-t[0]<0.01f||t[2]-t[3]<0.01f)return false;
 for(int i=0;i<16;++i)if(!std::isfinite(ui[i]))return false;
 if(fabsf(ui[15])<0.01f)return false;
 // Keep a behind-head aim outside the eye instead of mirroring it forward.
 float targetX=4,targetY=4;
 if(z<-.01f){targetX=(2*x+(t[1]+t[0])*z)/((t[1]-t[0])*-z);targetY=(2*y+(t[2]+t[3])*z)/((t[2]-t[3])*-z);}
 float dx=targetX-ui[3]/ui[15],dy=targetY-ui[7]/ui[15];
 memcpy(out,ui,16*sizeof(float));
 for(int i=0;i<4;++i){out[i]+=dx*ui[12+i];out[4+i]+=dy*ui[12+i];}
 return true;
}
