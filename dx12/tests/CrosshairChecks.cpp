#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <mutex>
#include "MinHook.h"
#include "CrosshairProjection.h"
static uintptr_t engine{};static FILE* logFile{};
#include "CrosshairUi.h"
static void Check(bool ok,const char* why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
static void Near(float a,float b){Check(fabsf(a-b)<.0001f,"aim projection mismatch");}
static void Noop(void*){}
static bool Component(void*){return true;}
static void Ptr(unsigned char* p,unsigned offset,uintptr_t value){memcpy(p+offset,&value,sizeof(value));}
int main(){
 TrackingOrigin origin;origin.valid=true;origin.q[1]=sinf(.3f);origin.q[3]=cosf(.3f);
 TrackedEye eyes[2]{};
 for(auto& eye:eyes){eye.fov[0]=-.7f;eye.fov[1]=.85f;eye.fov[2]=.8f;eye.fov[3]=-.65f;}
 eyes[0].position[0]=-.034f;eyes[1].position[0]=.034f;
 float base[12]{0,0,1,12,0,1,0,3,-1,0,0,4};
 float projection[16]{1,0,0,0,0,1,0,0,0,0,-1,-.2f,0,0,-1,0};
 for(float yaw:{-.6f,0.f,.6f})for(float pitch:{-.3f,0.f,.3f}){
  float yq[4]{0,sinf(yaw/2),0,cosf(yaw/2)},pq[4]{sinf(pitch/2),0,0,cosf(pitch/2)},relative[4];MultiplyQ(yq,pq,relative);
  for(auto& eye:eyes)MultiplyQ(origin.q,relative,eye.quaternion);
  for(unsigned i=0;i<2;++i){
   float ui[16],aim[16],head[12],proj[16];
   Check(MakeUiClipTransform(eyes,i,ui)&&MakeAimUiClip(eyes,i,origin,ui,aim),"valid eye");
   Check(MakeTrackedCamera(base,projection,eyes[i],origin,head,proj),"reference camera");
   float dir[3]{};for(int a=0;a<3;++a)for(int k=0;k<3;++k)dir[a]-=head[k*4+a]*base[k*4+2];
   Near(aim[3]/aim[15],(proj[0]*dir[0]+proj[2]*dir[2])/-dir[2]);
   Near(aim[7]/aim[15],(proj[5]*dir[1]+proj[6]*dir[2])/-dir[2]);
   for(int j=8;j<16;++j)Near(aim[j],ui[j]);
   // Aspect undo preserves the center and applies the same reticle sizing.
   for(float aspect:{.9f,16.f/9.f,2.4f}){float fit[16];memcpy(fit,aim,sizeof fit);UndoUiLetterbox(aspect,fit);Near(fit[3],aim[3]);Near(fit[7],aim[7]);}
  }
 }
 for(auto& eye:eyes){float q[4]{0,1,0,0};MultiplyQ(origin.q,q,eye.quaternion);}
 float ui[16],aim[16];Check(MakeUiClipTransform(eyes,0,ui)&&MakeAimUiClip(eyes,0,origin,ui,aim),"behind aim handled");Check(aim[3]/aim[15]>2,"behind reticle outside view");
 origin.valid=false;Check(!MakeAimUiClip(eyes,0,origin,ui,aim),"uninitialized origin rejected");
 alignas(16) unsigned char root[0x1b0]{},child[0x1b0]{},component[16]{},renderer[0x30]{};
 uintptr_t context=0x1234;CrosshairUi::dataVtable=context;
 Ptr(root,0x1a0,1);Ptr(root,0x1a8,reinterpret_cast<uintptr_t>(&context));Ptr(child,0x90,reinterpret_cast<uintptr_t>(root));
 uintptr_t vtable[3]{0,0,reinterpret_cast<uintptr_t>(&Component)};
 Ptr(component,0,reinterpret_cast<uintptr_t>(vtable));Ptr(component,8,reinterpret_cast<uintptr_t>(child));Ptr(renderer,0x18,reinterpret_cast<uintptr_t>(component));
 CrosshairUi::realUpdate=&Noop;CrosshairUi::realDestroy=&Noop;
 CrosshairUi::Update(renderer);Check(CrosshairUi::IsReticle(renderer),"inherited crosshair context tagged");
 context=0x4321;CrosshairUi::Update(renderer);Check(!CrosshairUi::IsReticle(renderer),"rebound renderer untagged");
 context=0x1234;CrosshairUi::Update(renderer);CrosshairUi::Destroy(renderer);Check(!CrosshairUi::IsReticle(renderer),"destroyed renderer removed");
 puts("Crosshair projection and renderer lifecycle checks passed");
}
