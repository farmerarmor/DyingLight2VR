#include "TrackedCamera.h"
#include <cstdio>
#include <limits>
static bool CloseEnough(float a,float b){return std::fabs(a-b)<0.0001f;}
int main(){
 const float base[12]{1,0,0,10,0,1,0,20,0,0,1,30};
 const float projection[16]{1,0,0,0,0,1,0,0,0,0,-1,-.05f,0,0,-1,0};
 TrackingOrigin origin;origin.valid=true;
 TrackedEye eye{{0,0,0,1},{-.032f,0,0},{-.9f,.7f,.8f,-.6f}};
 float left[12],right[12],p[16];
 if(!MakeTrackedCamera(base,projection,eye,origin,left,p))return 1;
 eye.position[0]=.032f;
 if(!MakeTrackedCamera(base,projection,eye,origin,right,p))return 2;
 if(!CloseEnough(right[3]-left[3],.064f)||!CloseEnough(left[7],20)||!CloseEnough(left[11],30))return 3;
 // All four asymmetric FOV edges must map to the corresponding clip edge.
 if(!CloseEnough(p[0]*std::tan(eye.fov[0])-p[2],-1)||!CloseEnough(p[0]*std::tan(eye.fov[1])-p[2],1)||
    !CloseEnough(p[5]*std::tan(eye.fov[2])-p[6],1)||!CloseEnough(p[5]*std::tan(eye.fov[3])-p[6],-1))return 4;
 for(int i=0;i<16;++i)if(i!=0&&i!=2&&i!=5&&i!=6&&p[i]!=projection[i])return 5;
 // Quarter-turn tracking origin: recentered orientation cancels, world -Z maps to local +X.
 const float s=std::sqrt(.5f);origin.q[1]=s;origin.q[3]=s;
 eye.quaternion[1]=s;eye.quaternion[3]=s;eye.position[0]=0;eye.position[2]=-1;
 if(!MakeTrackedCamera(base,projection,eye,origin,right,p))return 6;
 for(int i=0;i<12;++i)if(!CloseEnough(right[i],base[i]+(i==3?1.f:0.f)))return 7;
 // Head yaw relative to neutral rotates camera -Z toward world -X.
 origin=TrackingOrigin{};origin.valid=true;eye.position[2]=0;
 if(!MakeTrackedCamera(base,projection,eye,origin,right,p)||!CloseEnough(right[2],1)||!CloseEnough(right[8],-1))return 8;
 const float turnedBase[12]{0,0,1,10,0,1,0,20,-1,0,0,30};
 eye.quaternion[1]=0;eye.quaternion[3]=1;eye.position[0]=1;
 if(!MakeTrackedCamera(turnedBase,projection,eye,origin,right,p)||!CloseEnough(right[3],10)||!CloseEnough(right[11],29))return 9;
 eye.position[0]=6;if(MakeTrackedCamera(base,projection,eye,origin,right,p))return 10;
 eye.position[0]=0;eye.fov[1]=eye.fov[0];if(MakeTrackedCamera(base,projection,eye,origin,right,p))return 11;
 eye.fov[1]=.7f;origin.q[0]=std::numeric_limits<float>::quiet_NaN();
 if(MakeTrackedCamera(base,projection,eye,origin,right,p))return 12;
 puts("PASS: stereo separation, asymmetric frustum edges, depth preservation, recenter, yaw, rotated base, invalid inputs");
}

