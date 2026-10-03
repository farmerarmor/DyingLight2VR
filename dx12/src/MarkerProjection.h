#pragma once
#include "TrackedCamera.h"
#include <cstdint>
#include <initializer_list>

namespace MarkerProjection {
// Return addresses of the HUD world-position consumers, plus its edge-clamp
// wrapper (whose only caller is the same HUD). Excludes aiming/interaction rays.
inline constexpr uintptr_t callers[]={0x165da13,0x1691fb4,0x1692036,0x1693b38,
 0x1697b47,0x169bb5d,0x169dcd6,0x169e3f4,0x16a0421,0x16a0cc4,
 0x16a5b96,0x16b30c3,0x16b33d2,0x16b3462,0x180a05a};
inline bool CameraMatches(uintptr_t native,uintptr_t render,const uintptr_t* views){return native&&(native==render||native==views[0]||native==views[1]);}
inline bool HudCaller(uintptr_t rva){for(auto c:callers)if(c==rva)return true;return false;}
inline bool Center(const TrackedEye* eyes,TrackedEye& center){
 center=eyes[0];float dot=0,norm=0;
 for(int i=0;i<4;++i)dot+=eyes[0].quaternion[i]*eyes[1].quaternion[i];
 for(int i=0;i<4;++i){center.quaternion[i]=eyes[0].quaternion[i]+(dot<0?-1.f:1.f)*eyes[1].quaternion[i];norm+=center.quaternion[i]*center.quaternion[i];}
 if(!std::isfinite(norm)||norm<0.01f)return false;
 for(auto& q:center.quaternion)q/=sqrtf(norm);
 for(int i=0;i<3;++i)center.position[i]=(eyes[0].position[i]+eyes[1].position[i])*0.5f;
 return true;
}
// Re-express a world point for the native projector. Only this stack-local point
// changes: the native front/behind test and edge clamp still run unmodified.
// Match UiProjection's 2.4 x 1.35 m panel at 2 m, including its aspect-fit undo.
inline bool Point(const float* world,const float* head,const float* camera,
                  const float* projection,float aspect,float* result){
 float check[12];
 if(!OffsetCameraRight(head,0,check)||!OffsetCameraRight(camera,0,check))return false;
 if(!std::isfinite(aspect)||aspect<0.1f||aspect>10.f)return false;
 for(int i=0;i<3;++i)if(!std::isfinite(world[i]))return false;
 for(int i=0;i<16;++i)if(!std::isfinite(projection[i]))return false;
 if(projection[0]<0.01f||projection[5]<0.01f||fabsf(projection[14]+1)>0.001f||fabsf(projection[15])>0.001f)return false;
 // Reject skewed/orthographic projections rather than guessing their layout.
 for(int i:{1,3,4,7,12,13})if(fabsf(projection[i])>0.001f)return false;
 float local[3]{};
 for(int a=0;a<3;++a)for(int k=0;k<3;++k)local[a]+=head[k*4+a]*(world[k]-head[k*4+3]);
 float undoX=aspect>16.f/9.f?aspect/(16.f/9.f):1.f;
 float undoY=aspect<16.f/9.f?(16.f/9.f)/aspect:1.f;
 local[0]=(local[0]/(.6f*undoX)-projection[2]*local[2])/projection[0];
 local[1]=(local[1]/(.3375f*undoY)-projection[6]*local[2])/projection[5];
 for(int a=0;a<3;++a){result[a]=camera[a*4+3];for(int k=0;k<3;++k)result[a]+=camera[a*4+k]*local[k];if(!std::isfinite(result[a]))return false;}
 return true;
}
}
