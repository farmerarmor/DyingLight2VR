#pragma once
#include "MarkerProjection.h"
namespace MarkerPort {
struct Pose {TrackedEye center{};TrackingOrigin origin{};uintptr_t camera{},uiCameras[2]{};ULONGLONG time{};float base[12]{},left[12]{},right[12]{};bool valid{};};
inline SRWLOCK poseLock=SRWLOCK_INIT;
inline Pose pose;
inline uintptr_t module{};
inline std::atomic<uint64_t> calls{},patched{},fallback{},poseRejected{},cameraRejected{},mathRejected{};
using ProjectFn=float*(*)(float*,const float*,void*,bool,bool*,bool);
inline ProjectFn original;
inline void Publish(const SceneState& st,const float* left){
 Pose next;next.camera=st.camera;next.time=GetTickCount64();next.origin=origin;
 next.valid=MarkerProjection::Center(st.eyes,next.center);
 // GetViewCamera chooses level+1390/+13a0 by native thread context. The
 // tracked render camera is embedded at level+13b0, not either view pointer.
 auto level=st.camera-0x13b0;
 next.uiCameras[0]=Read<uintptr_t>(level+0x1390);
 next.uiCameras[1]=Read<uintptr_t>(level+0x13a0);
 memcpy(next.base,st.inverse,sizeof(next.base));memcpy(next.left,left,sizeof(next.left));memcpy(next.right,st.right,sizeof(next.right));
 AcquireSRWLockExclusive(&poseLock);pose=next;ReleaseSRWLockExclusive(&poseLock);
}
inline bool Same(const float* a,const float* b){for(int i=0;i<12;++i)if(fabsf(a[i]-b[i])>0.00001f)return false;return true;}
inline float* Project(float* out,const float* world,void* camera,bool clamp,bool* clipped,bool option){
 auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress())-module;
 if(MapPanel::visible.load()||!camera||!MarkerProjection::HudCaller(caller)||!vrRequested.load()||!stereoEnabled.load()||!UiPort::enabled.load()||recenter.load())return original(out,world,camera,clamp,clipped,option);
 ++calls;Pose st;AcquireSRWLockShared(&poseLock);st=pose;ReleaseSRWLockShared(&poseLock);
 auto native=*reinterpret_cast<uintptr_t*>(static_cast<char*>(camera)+0x38);
 if(!st.valid||GetTickCount64()-st.time>250){++poseRejected;++fallback;return original(out,world,camera,clamp,clipped,option);}
 if(!MarkerProjection::CameraMatches(native,st.camera,st.uiCameras)){++cameraRejected;++fallback;return original(out,world,camera,clamp,clipped,option);}
 alignas(16) float actual[12],projection[16],head[12],unused[16],point[3];
 memcpy(actual,reinterpret_cast<void*>(native+0x40),sizeof(actual));
 memcpy(projection,reinterpret_cast<void*>(native+0x80),sizeof(projection));
 // GUI updates normally precede scene rendering. If called during a tracked
 // render, do not apply the tracked pose a second time to that eye's camera.
 const float* base=(native==st.camera&&(Same(actual,st.left)||Same(actual,st.right)))?st.base:actual;
 if(!MakeTrackedCamera(base,projection,st.center,st.origin,head,unused)||
    !MarkerProjection::Point(world,head,actual,projection,UiPort::aspect.load(),point)){
  ++mathRejected;++fallback;return original(out,world,camera,clamp,clipped,option);
 }
 ++patched;return original(out,point,camera,clamp,clipped,option);
}
inline void Install(){
 module=reinterpret_cast<uintptr_t>(GetModuleHandleW(L"gamedll_ph_x64_rwdi.dll"));
 if(!module){fprintf(logFile,"HUD marker hook: game module absent\n");return;}
 constexpr unsigned char signature[]={0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x81,0xec,0xf0,0,0,0};
 if(memcmp(reinterpret_cast<void*>(module+0x1809c50),signature,sizeof(signature))){fprintf(logFile,"HUD marker signature mismatch; skipped\n");return;}
 for(auto rva:MarkerProjection::callers){auto call=reinterpret_cast<const unsigned char*>(module+rva-5);int d{};memcpy(&d,call+1,4);if(call[0]!=0xe8||rva+d!=0x1809c50){fprintf(logFile,"HUD marker caller mismatch; skipped\n");return;}}
 auto status=MH_CreateHook(reinterpret_cast<void*>(module+0x1809c50),reinterpret_cast<void*>(&Project),reinterpret_cast<void**>(&original));
 fprintf(logFile,"HUD marker head tracking hook=%d; center pose, native edge clamp; follows F1\n",status);
}
inline void Report(){fprintf(logFile,"HUD markers calls=%llu patched=%llu fallback=%llu poseRejected=%llu cameraRejected=%llu mathRejected=%llu\n",calls.load(),patched.load(),fallback.load(),poseRejected.load(),cameraRejected.load(),mathRejected.load());}
}
