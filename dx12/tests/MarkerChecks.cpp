#include <cstdio>
#include <cstdlib>
#include <limits>
#include "MarkerProjection.h"
static void Check(bool v,const char* why){if(!v){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
static void Near(float a,float b){Check(fabsf(a-b)<0.0001f,"marker position mismatch");}
static void Local(const float* cam,const float* world,float* out){for(int a=0;a<3;++a){out[a]=0;for(int k=0;k<3;++k)out[a]+=cam[k*4+a]*(world[k]-cam[k*4+3]);}}
static void World(const float* cam,const float* local,float* out){for(int a=0;a<3;++a){out[a]=cam[a*4+3];for(int k=0;k<3;++k)out[a]+=cam[a*4+k]*local[k];}}
int main(){
 uintptr_t views[2]{0x19b1900e7b0,0x19b1900e7b0};
 Check(MarkerProjection::CameraMatches(views[0],0x1997eef7f30,views),"distinct live gameplay camera accepted");
 Check(MarkerProjection::CameraMatches(0x1997eef7f30,0x1997eef7f30,views),"render camera accepted");
 Check(!MarkerProjection::CameraMatches(0x1234,0x1997eef7f30,views),"unrelated camera rejected");
 Check(!MarkerProjection::CameraMatches(0,0,views),"null camera rejected");
 float camera[12]{1,0,0,12,0,1,0,4,0,0,1,-7};
 float projection[16]{1.1f,0,.1f,0,0,1.8f,-.08f,0,0,0,-1,-.2f,0,0,-1,0};
 TrackedEye eyes[2]{};for(auto& e:eyes){e.quaternion[3]=1;e.fov[0]=-.7f;e.fov[1]=.8f;e.fov[2]=.75f;e.fov[3]=-.7f;}
 eyes[0].position[0]=-.032f;eyes[1].position[0]=.032f;
 TrackedEye center;Check(MarkerProjection::Center(eyes,center),"center pose");Near(center.position[0],0);
 eyes[1].quaternion[3]=-1;Check(MarkerProjection::Center(eyes,center),"quaternion sign equivalence");Near(center.quaternion[3],1);
 TrackingOrigin origin;origin.valid=true;
 // Independent geometric expectation: marker's direction projected onto the
 // fixed HUD panel must equal its direction in the center tracked camera.
 for(float yaw:{0.f,.45f,1.8f,3.14159265f})for(float pitch:{0.f,.3f})for(float aspect:{.9f,16.f/9.f,2.4f}){
  float qy[4]{0,sinf(yaw/2),0,cosf(yaw/2)},qp[4]{sinf(pitch/2),0,0,cosf(pitch/2)};
  MultiplyQ(qy,qp,center.quaternion);center.position[0]=.1f;center.position[1]=.07f;
  float head[12],unused[16];Check(MakeTrackedCamera(camera,projection,center,origin,head,unused),"tracked center");
  for(auto local: {std::initializer_list<float>{0,0,-10},{3,1,-10},{30,-2,-4},{-5,4,10}}){
   float world[3],point[3],expected[3],actual[3];World(camera,local.begin(),world);Local(head,world,expected);
   Check(MarkerProjection::Point(world,head,camera,projection,aspect,point),"world marker conversion");Local(camera,point,actual);
   float ndcx=(projection[0]*actual[0]+projection[2]*actual[2])/-actual[2];
   float ndcy=(projection[5]*actual[1]+projection[6]*actual[2])/-actual[2];
   float ux=aspect>16.f/9.f?aspect/(16.f/9.f):1,uy=aspect<16.f/9.f?(16.f/9.f)/aspect:1;
   Near(ndcx*ux*.6f,expected[0]/-expected[2]);Near(ndcy*uy*.3375f,expected[1]/-expected[2]);Near(actual[2],expected[2]);
  }
 }
 Check(MarkerProjection::HudCaller(0x16b30c3),"HUD marker caller");
 Check(MarkerProjection::HudCaller(0x180a05a),"HUD edge wrapper");
 Check(!MarkerProjection::HudCaller(0x18587a8),"non-HUD caller excluded");
 float world[3]{0,0,-10},point[3];projection[0]=0;
 Check(!MarkerProjection::Point(world,camera,camera,projection,1,point),"invalid projection rejected");projection[0]=1;
 world[0]=std::numeric_limits<float>::quiet_NaN();
 Check(!MarkerProjection::Point(world,camera,camera,projection,1,point),"invalid world point rejected");
 puts("Marker checks passed: center pose, head yaw/pitch/translation, behind-camera direction, panel/aspect mapping, caller scope and invalid inputs.");
}
