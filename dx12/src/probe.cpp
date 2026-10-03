#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <cstdio>
#include <share.h>
#include <atomic>
#include <intrin.h>
#include "MinHook.h"
#include "exports.h"
#include "CameraMath.h"
#include "Dx12Screen.h"
#include "TrackedCamera.h"
extern "C" {FARPROC g_targets[kExportCount]{};}
static HMODULE self;
static FILE* logFile;
static uintptr_t engine,backend;
static std::atomic<unsigned long long> scenes{},presents{},ends{},serial{};
static std::atomic<bool> capture{true};
static SRWLOCK lock=SRWLOCK_INIT;
struct Event{unsigned long long sequence;DWORD thread;unsigned kind,counter;uintptr_t object;};
static Event events[256];static unsigned count;
template<class T> T Read(uintptr_t p){T v{};SIZE_T n{};ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(p),&v,sizeof(v),&n);return v;}
static void Record(unsigned kind,void* object){
 if(!capture.load(std::memory_order_relaxed))return;
 auto game=Read<uintptr_t>(engine+0x272f3f8);auto counter=game?Read<unsigned>(game+0x110):0;
 AcquireSRWLockExclusive(&lock);
 if(count<256)events[count++]={++serial,GetCurrentThreadId(),kind,counter,reinterpret_cast<uintptr_t>(object)};
 if(count==256)capture=false;
 ReleaseSRWLockExclusive(&lock);
}
using SceneFn=uintptr_t(*)(void*,unsigned,void*,void*);
using EndFn=uintptr_t(*)(void*,void*,void*);
using PresentFn=uintptr_t(*)(void*);
static SceneFn realScene;static EndFn realEnd;static PresentFn realPresent;
using SubmitFn=uintptr_t(*)(void*,void*);static SubmitFn realSubmit;
static EndFn realRequest;
using EnterFn=void(*)(void*,void*);using LeaveFn=void(*)(void*);using SetFn=void(*)(void*,const void*,bool);
static EnterFn enterRenderer;static LeaveFn leaveRenderer,resetLevel;static SetFn setCamera;
static std::atomic<bool> stereoEnabled{};static thread_local unsigned eye;static thread_local bool repeating;static thread_local uintptr_t prepared;
static TrackingOrigin origin;static std::atomic<bool> recenter{true};using RebuildFn=void(*)(void*,bool);static RebuildFn rebuild;
static uint64_t pairId;static unsigned copies;static Dx12Screen screen;
struct SceneState{void* game{};unsigned token{},counter{};void* a{};void* b{};uintptr_t camera{};alignas(16) float inverse[12]{};bool valid{},tracked{},mapPanel{};float projection[16]{},frustum[12]{};alignas(16) float right[12]{};float rightProjection[16]{};TrackedEye eyes[2]{};};
static_assert(alignof(SceneState)>=16 && offsetof(SceneState,inverse)%16==0);
static bool Snapshot(SceneState& st){
 SIZE_T n{};if(!st.camera||Read<uintptr_t>(st.camera)!=engine+0x18a2c68)return false;
 if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(st.camera+0x40),st.inverse,sizeof(st.inverse),&n)||n!=sizeof(st.inverse))return false;
 if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(st.camera+0x80),st.projection,sizeof(st.projection),&n)||n!=sizeof(st.projection))return false;
 if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(st.camera+0x1a0),st.frustum,sizeof(st.frustum),&n)||n!=sizeof(st.frustum))return false;
 float check[12];return st.valid=OffsetCameraRight(st.inverse,0,check);
}
static void Frustum(uintptr_t camera,const TrackedEye& eye){auto f=reinterpret_cast<float*>(camera+0x1a0);float nearPlane=f[4];f[0]=nearPlane*tanf(eye.fov[0]);f[1]=nearPlane*tanf(eye.fov[1]);f[2]=nearPlane*tanf(eye.fov[3]);f[3]=nearPlane*tanf(eye.fov[2]);f[9]=f[10]=f[11]=0;rebuild(reinterpret_cast<void*>(camera),false);}
static void Restore(const SceneState& st){memcpy(reinterpret_cast<void*>(st.camera+0x1a0),st.frustum,sizeof(st.frustum));memcpy(reinterpret_cast<void*>(st.camera+0x80),st.projection,sizeof(st.projection));setCamera(reinterpret_cast<void*>(st.camera),st.inverse,false);}
static SceneState last;static SRWLOCK stateLock=SRWLOCK_INIT;
static std::atomic<bool> vrRequested{true},safetyBlocked{false};
static std::atomic<ULONGLONG> resumeAt{};
static std::atomic<uint64_t> suspendScene{},suspensions{};
#include "TemporalPort.h"
static void SuspendVr(const char* reason,bool fatal=false){
 TemporalPort::EndPair();TemporalPort::reset=true;stereoEnabled=false;if(fatal)safetyBlocked=true;
 resumeAt=GetTickCount64()+100;suspendScene=scenes.load();screen.Cancel();
 AcquireSRWLockExclusive(&stateLock);last.tracked=false;ReleaseSRWLockExclusive(&stateLock);
 auto n=++suspensions;if(n<=8||n%100==0){fprintf(logFile,"VR suspended reason=%s fatal=%u requested=%u count=%llu\n",reason,fatal,vrRequested.load(),n);fflush(logFile);}
}
static void UpdateVrRequest(void* game){
 if(!vrRequested.load()){stereoEnabled=false;return;}
 if(stereoEnabled||safetyBlocked||GetTickCount64()<resumeAt||scenes.load()<=suspendScene)return;
 SceneState st;AcquireSRWLockShared(&stateLock);st=last;ReleaseSRWLockShared(&stateLock);
 if(st.valid&&st.game==game&&st.camera&&Read<uintptr_t>(st.camera)==engine+0x18a2c68){stereoEnabled=true;fprintf(logFile,"VR automatically started/resumed; no time limit\n");fflush(logFile);}
}

#include "WaterDrawPort.h"
#include "ShadowBindings.h"
#include "UiPort.h"
#include "MapPanel.h"
#include "MarkerPort.h"
#include "VisualPort.h"
static uintptr_t Submit(void* renderer,void* data){prepared=Read<uintptr_t>(reinterpret_cast<uintptr_t>(data)+0xd8);UiPort::Publish();auto result=realSubmit(renderer,data);UiPort::Clear();return result;}
static uintptr_t Request(void* renderer,void* a,void* b){
 if(!stereoEnabled.load()){
  SceneState st;AcquireSRWLockExclusive(&stateLock);st=last;last.tracked=false;ReleaseSRWLockExclusive(&stateLock);
  if(st.tracked)Restore(st);if(!vrRequested||safetyBlocked)screen.Stop();else screen.Cancel();
 }
 if(repeating || !stereoEnabled.load() || reinterpret_cast<uintptr_t>(_ReturnAddress())!=engine+0x82e200)return realRequest(renderer,a,b);
 SceneState base;AcquireSRWLockShared(&stateLock);base=last;ReleaseSRWLockShared(&stateLock);
 auto game=Read<uintptr_t>(engine+0x272f3f8);
 if(!base.valid||reinterpret_cast<uintptr_t>(base.game)!=game||base.counter!=Read<unsigned>(game+0x110)||a!=base.a||b!=base.b){if(base.tracked)Restore(base);SuspendVr("scene unavailable or changed");return realRequest(renderer,a,b);}
 auto contextGlobal=Read<uintptr_t>(engine+0x180c6f0);auto manager=Read<uintptr_t>(contextGlobal);
 if(!manager||reinterpret_cast<uintptr_t>(TlsGetValue(Read<DWORD>(manager+8)))!=2){if(base.tracked)Restore(base);SuspendVr("native thread context mismatch",true);fprintf(logFile,"Stereo rejected: native thread context mismatch\n");fflush(logFile);return realRequest(renderer,a,b);}
 alignas(16) float moved[12];if(!OffsetCameraRight(base.inverse,base.mapPanel?0.f:0.064f,moved)){if(base.tracked)Restore(base);SuspendVr("scene unavailable or changed");return realRequest(renderer,a,b);}
 ++pairId;copies=0;eye=1;auto result=realRequest(renderer,a,b);eye=0;
 if(copies!=1){if(base.tracked)Restore(base);SuspendVr("first eye unavailable");fprintf(logFile,"Stereo stopped: first eye copies=%u pair=%llu\n",copies,pairId);fflush(logFile);return result;}
 enterRenderer(renderer,nullptr);repeating=true;resetLevel(reinterpret_cast<void*>(base.camera-0x13b0));if(base.tracked){memcpy(moved,base.right,sizeof(moved));Frustum(base.camera,base.eyes[1]);}setCamera(reinterpret_cast<void*>(base.camera),moved,false);
 TemporalPort::Begin(base.tracked?2:0,base.token,base.camera,base.counter);WaterHistory::Begin(Read<ID3D12CommandQueue*>(Read<uintptr_t>(backend+0x164b50)),TemporalPort::renderEye.load(),TemporalPort::epoch.load(),base.counter,TemporalPort::latched.load()==2);UiPort::Eye(base.eyes,1,base.tracked,origin);prepared=0;VisualPort::camera=base.tracked?base.camera:0;realScene(base.game,base.token,base.a,base.b);TemporalPort::EndScene();VisualPort::camera=0;UiPort::eyeValid=false;
 bool valid=prepared==base.camera && Read<unsigned>(game+0x110)==base.counter;
 float actual[12]{};SIZE_T got{};valid=valid&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(base.camera+0x40),actual,sizeof(actual),&got)&&got==sizeof(actual);
 for(unsigned i=0;i<12;++i)valid=valid&&std::isfinite(actual[i])&&fabsf(actual[i]-moved[i])<0.0001f;
 if(base.tracked)for(unsigned i:{0u,2u,5u,6u}){auto actual=Read<float>(base.camera+0x80+i*4);valid=valid&&std::isfinite(actual)&&fabsf(actual-base.rightProjection[i])<0.001f;}
 if(!valid)screen.Cancel();eye=valid?2:0;realRequest(renderer,a,b);eye=0;
 Restore(base);AcquireSRWLockExclusive(&stateLock);last.tracked=false;ReleaseSRWLockExclusive(&stateLock);resetLevel(reinterpret_cast<void*>(base.camera-0x13b0));leaveRenderer(renderer);repeating=false;TemporalPort::EndPair();
 float restored[12]{};SIZE_T restoredBytes{};bool restoredValid=ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(base.camera+0x40),restored,sizeof(restored),&restoredBytes)&&restoredBytes==sizeof(restored)&&memcmp(restored,base.inverse,sizeof(restored))==0;
 if(!valid||!restoredValid||copies!=2||Read<unsigned>(game+0x110)!=base.counter){SuspendVr(!restoredValid?"camera restoration failed":!valid?"effective right camera changed":"GPU pair unavailable",!restoredValid||Read<unsigned>(game+0x110)!=base.counter);fprintf(logFile,"Stereo stopped: pair=%llu camera=%u copies=%u counter=%u expected=%u\n",pairId,valid,copies,Read<unsigned>(game+0x110),base.counter);fflush(logFile);}
 else if(pairId<=3||pairId%120==0){fprintf(logFile,"Same-frame DX12 stereo pair=%llu simulation=%u copies=%u tracked=%u\n",pairId,base.counter,copies,base.tracked);fflush(logFile);}
 return result;
}
static uintptr_t Scene(void* g,unsigned token,void* a,void* b){
 ++scenes;if(!repeating)UpdateVrRequest(g);SceneState base;bool tracked=false;alignas(16) float left[12];float leftProjection[16];
 if(!repeating&&stereoEnabled.load()){
  AcquireSRWLockShared(&stateLock);base=last;ReleaseSRWLockShared(&stateLock);
  if(base.game==g&&base.camera){base.tracked=false;base.counter=Read<unsigned>(reinterpret_cast<uintptr_t>(g)+0x110);base.token=token;base.a=a;base.b=b;
   base.mapPanel=MapPanel::visible.load()!=0;screen.SetMapPanel(base.mapPanel);
   if(Snapshot(base)&&std::isfinite(base.frustum[4])&&base.frustum[4]>0&&screen.BeginTracking(base.eyes)){
    if(recenter.exchange(false)||!origin.valid){memcpy(origin.q,base.eyes[0].quaternion,sizeof(origin.q));for(int i=0;i<3;++i)origin.p[i]=(base.eyes[0].position[i]+base.eyes[1].position[i])*0.5f;origin.valid=true;}
    tracked=!base.mapPanel&&MakeTrackedCamera(base.inverse,base.projection,base.eyes[0],origin,left,leftProjection)&&MakeTrackedCamera(base.inverse,base.projection,base.eyes[1],origin,base.right,base.rightProjection);
    if(tracked){MarkerPort::Publish(base,left);Frustum(base.camera,base.eyes[0]);setCamera(reinterpret_cast<void*>(base.camera),left,false);}else if(!base.mapPanel)screen.Cancel();
   }
  }
 }
 TemporalPort::Begin(tracked?1:0,token,base.camera,base.counter);WaterHistory::Begin(Read<ID3D12CommandQueue*>(Read<uintptr_t>(backend+0x164b50)),TemporalPort::renderEye.load(),TemporalPort::epoch.load(),base.counter,TemporalPort::latched.load()==2);ShadowPort::Begin(Read<ID3D12CommandQueue*>(Read<uintptr_t>(backend+0x164b50)),TemporalPort::renderEye.load(),base.counter,TemporalPort::epoch.load());UiPort::Eye(base.eyes,0,tracked,origin);prepared=0;VisualPort::camera=tracked?base.camera:0;Record(1,g);auto r=realScene(g,token,a,b);Record(2,g);TemporalPort::EndScene();VisualPort::camera=0;UiPort::eyeValid=false;
 if(!repeating){SceneState st;st.game=g;st.token=token;st.a=a;st.b=b;st.counter=Read<unsigned>(reinterpret_cast<uintptr_t>(g)+0x110);st.camera=prepared;st.mapPanel=base.mapPanel;Snapshot(st);
  if(tracked){bool valid=st.valid&&st.camera==base.camera&&st.counter==base.counter;
   for(unsigned i=0;i<12;++i)valid=valid&&std::isfinite(st.inverse[i])&&fabsf(st.inverse[i]-left[i])<0.0001f;
   for(unsigned i:{0u,2u,5u,6u})valid=valid&&std::isfinite(st.projection[i])&&fabsf(st.projection[i]-leftProjection[i])<0.001f;
   if(valid){base.tracked=true;st=base;}else{Restore(base);screen.Cancel();stereoEnabled=false;Snapshot(st);fprintf(logFile,"Tracked left validation failed; disabled\n");fflush(logFile);}
  }
  AcquireSRWLockExclusive(&stateLock);last=st;ReleaseSRWLockExclusive(&stateLock);
 }
 return r;
}
static uintptr_t End(void* d,void* a,void* b){++ends;Record(3,a);auto r=realEnd(d,a,b);Record(4,a);return r;}
static std::atomic<bool> inspected{};
static uintptr_t Present(void* object){
 ++presents;Record(5,object);
 auto swap=Read<IDXGISwapChain3*>(reinterpret_cast<uintptr_t>(object)+0x60);
 if(Read<uintptr_t>(reinterpret_cast<uintptr_t>(object)+0x50) && swap && !inspected.exchange(true)){
  ID3D12Device* device{};auto hr=swap->GetDevice(IID_PPV_ARGS(&device));
  DXGI_SWAP_CHAIN_DESC desc{};auto dh=swap->GetDesc(&desc);
  auto qw=Read<uintptr_t>(backend+0x164b50);auto queue=Read<ID3D12CommandQueue*>(qw);
  ID3D12CommandQueue* verified{};HRESULT qhr=queue?queue->QueryInterface(IID_PPV_ARGS(&verified)):E_POINTER;
  fprintf(logFile,"DX12 swap=%p deviceHr=%08lx descHr=%08lx width=%u height=%u buffers=%u format=%u index=%u queue=%p queueQI=%08lx\n",swap,hr,dh,desc.BufferDesc.Width,desc.BufferDesc.Height,desc.BufferCount,desc.BufferDesc.Format,swap->GetCurrentBackBufferIndex(),queue,qhr);
  if(verified){auto d=verified->GetDesc();fprintf(logFile,"queue type=%u\n",d.Type);verified->Release();}
  if(device){WaterDrawPort::Install(device,queue);device->Release();}fflush(logFile);
 }
 if(swap&&Read<uintptr_t>(reinterpret_cast<uintptr_t>(object)+0x50)){WaterHistory::End();ShadowPort::Present();}
 if(eye && swap && Read<uintptr_t>(reinterpret_cast<uintptr_t>(object)+0x50)){
 auto q=Read<ID3D12CommandQueue*>(Read<uintptr_t>(backend+0x164b50));auto hr=screen.Copy(swap,q,eye,pairId);
 if(SUCCEEDED(hr))++copies;else{fprintf(logFile,"DX12 eye copy/XR failure=%08lx eye=%u pair=%llu\n",hr,eye,pairId);fflush(logFile);SuspendVr("DX12 eye copy/XR failure",true);}
 }
 auto r=realPresent(object);Record(6,object);return r;
}
static void Dump(){
 Event copy[256];unsigned n;AcquireSRWLockExclusive(&lock);n=count;memcpy(copy,events,n*sizeof(Event));count=0;capture=false;ReleaseSRWLockExclusive(&lock);
 for(unsigned i=0;i<n;++i){auto& e=copy[i];fprintf(logFile,"event seq=%llu tid=%lu kind=%u simulation=%u object=%p\n",e.sequence,e.thread,e.kind,e.counter,reinterpret_cast<void*>(e.object));}
 fflush(logFile);
}
static DWORD WINAPI Worker(void*){
 HMODULE pin{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Worker),&pin);
 wchar_t path[MAX_PATH];GetModuleFileNameW(self,path,MAX_PATH);auto slash=wcsrchr(path,L'\\');if(!slash)return 0;wcscpy_s(slash+1,MAX_PATH-(slash+1-path),L"DL2VR-DX12.log");logFile=_wfsopen(path,L"w",_SH_DENYNO);if(!logFile)return 0;
 fprintf(logFile,"DX12 stage42 native-aim crosshair; stage41 fixed panel for shared player submenus and map; stage38 gameplay marker tracking; stage8 baseline plus water/reflection and shared flashlight shadow candidate; F9 shadows OFF: F7 tracked VR; F6 recenter; Insert toggles water/reflection histories default ON; asynchronous copies remain ON. F2 culling/F5 auxiliary/F10 hideGUI; F8 shader inventory. F1 UI correction ON; shared-shadow/history GPU bindings pending. Exact reflection/AO/flashlight-projection shaders patched when matched.\n");fflush(logFile);
 for(unsigned i=0;i<1200;++i){engine=reinterpret_cast<uintptr_t>(GetModuleHandleW(L"engine_x64_rwdi.dll"));backend=reinterpret_cast<uintptr_t>(GetModuleHandleW(L"rd3d12_x64_rwdi.dll"));if(engine&&backend)break;Sleep(100);}
 if(!engine||!backend){fprintf(logFile,"DX12 modules absent; no hooks.\n");fflush(logFile);return 0;}
 const unsigned char sceneSig[]={0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57,0x48,0x81,0xec,0x40,0x03,0x00};
 const unsigned char endSig[]={0x48,0x89,0x5c,0x24,0x08,0x55,0x56,0x57,0x41,0x54,0x41,0x55};
 const unsigned char presentSig[]={0x48,0x8b,0xc4,0x55,0x53,0x48,0x8d,0xa8,0x28,0xf8,0xff,0xff};
 if(memcmp(reinterpret_cast<void*>(engine+0x835710),sceneSig,sizeof(sceneSig))||memcmp(reinterpret_cast<void*>(backend+0x71e20),endSig,sizeof(endSig))||memcmp(reinterpret_cast<void*>(backend+0x6ab40),presentSig,sizeof(presentSig))||Read<uintptr_t>(backend+0x122148+0x330)!=backend+0x71e20){fprintf(logFile,"Signature mismatch; no hooks.\n");fflush(logFile);return 0;}
 // Exact signatures from the installed engine image.
 struct Guard{uintptr_t rva;const char* bytes;unsigned n;};
 Guard guards[]={{0x111ebf0,"\x4c\x8b\xdc\x49\x89\x4b\x08\x53\x56\x57\x48\x81\xec\xc0\x00\x00",16},{0x1115ef0,"\x40\x53\x48\x83\xec\x20\x48\x8b\xd9\x48\x89\x91\xe0\x18\x00\x00",16},{0x11157d0,"\x48\x89\x5c\x24\x08\x48\x89\x6c\x24\x10\x48\x89\x74\x24\x18\x57",16},{0x1110200,"\x40\x53\x48\x83\xec\x20\x48\x8b\xd9\xf0\xff\x41\x78\x80\xb9\xb8",16},{0x1115b20,"\x40\x53\x48\x83\xec\x20\x80\xb9\xb8\x00\x00\x00\x00\x48\x8b\xd9",16},{0xb43350,"\x48\x89\x5c\x24\x08\x57\x48\x83\xec\x20\x48\x8b\xf9\x48\x8b\x89",16},{0x111fd20,"\x40\x53\x48\x83\xec\x20\x48\x8b\xc2\x48\x8b\xd9\x48\x8d\x51\x10",16}};
 for(auto& g:guards)if(memcmp(reinterpret_cast<void*>(engine+g.rva),g.bytes,g.n)){fprintf(logFile,"Stereo engine signature mismatch RVA=%llx\n",g.rva);fflush(logFile);return 0;}
 rebuild=reinterpret_cast<RebuildFn>(engine+0x111ebf0);
 enterRenderer=reinterpret_cast<EnterFn>(engine+0x1110200);leaveRenderer=reinterpret_cast<LeaveFn>(engine+0x1115b20);resetLevel=reinterpret_cast<LeaveFn>(engine+0xb43350);setCamera=reinterpret_cast<SetFn>(engine+0x111fd20);
 if(MH_Initialize()!=MH_OK)return 0;
 if(MH_CreateHook(reinterpret_cast<void*>(engine+0x835710),reinterpret_cast<void*>(&Scene),reinterpret_cast<void**>(&realScene))!=MH_OK || MH_CreateHook(reinterpret_cast<void*>(backend+0x71e20),reinterpret_cast<void*>(&End),reinterpret_cast<void**>(&realEnd))!=MH_OK || MH_CreateHook(reinterpret_cast<void*>(backend+0x6ab40),reinterpret_cast<void*>(&Present),reinterpret_cast<void**>(&realPresent))!=MH_OK){MH_Uninitialize();return 0;}
 if(MH_CreateHook(reinterpret_cast<void*>(engine+0x1115ef0),reinterpret_cast<void*>(&Submit),reinterpret_cast<void**>(&realSubmit))!=MH_OK||MH_CreateHook(reinterpret_cast<void*>(engine+0x11157d0),reinterpret_cast<void*>(&Request),reinterpret_cast<void**>(&realRequest))!=MH_OK){MH_Uninitialize();return 0;}
 if(memcmp(reinterpret_cast<void*>(engine+0x6bc2e0),"\x48\x89\x5c\x24\x08\x48\x89\x6c\x24\x10\x48\x89\x74\x24\x18\x57",16)||MH_CreateHook(reinterpret_cast<void*>(engine+0x6bc2e0),reinterpret_cast<void*>(&VisualPort::Main),reinterpret_cast<void**>(&VisualPort::mainOriginal))!=MH_OK){fprintf(logFile,"Visual port hook failed: Main\n");fflush(logFile);MH_Uninitialize();return 0;}
 if(memcmp(reinterpret_cast<void*>(engine+0x70d780),"\x48\x89\x5c\x24\x10\x48\x89\x6c\x24\x18\x56\x57\x41\x56\x48\x83",16)||MH_CreateHook(reinterpret_cast<void*>(engine+0x70d780),reinterpret_cast<void*>(&VisualPort::World),reinterpret_cast<void**>(&VisualPort::worldOriginal))!=MH_OK){fprintf(logFile,"Visual port hook failed: World\n");fflush(logFile);MH_Uninitialize();return 0;}
 if(memcmp(reinterpret_cast<void*>(engine+0xb78430),"\x48\x8b\xc4\x48\x89\x58\x08\x48\x89\x70\x10\x48\x89\x78\x18\x55",16)||MH_CreateHook(reinterpret_cast<void*>(engine+0xb78430),reinterpret_cast<void*>(&VisualPort::Project),reinterpret_cast<void**>(&VisualPort::projectOriginal))!=MH_OK){fprintf(logFile,"Visual port hook failed: Project\n");fflush(logFile);MH_Uninitialize();return 0;}
 if(memcmp(reinterpret_cast<void*>(engine+0x61e7c0),"\x40\x53\x48\x83\xec\x20\x48\x8b\x05\x23\xdf\x1e\x01\x48\x8b\xd9",16)||MH_CreateHook(reinterpret_cast<void*>(engine+0x61e7c0),reinterpret_cast<void*>(&VisualPort::Camera),reinterpret_cast<void**>(&VisualPort::cameraOriginal))!=MH_OK){fprintf(logFile,"Visual port hook failed: Camera\n");fflush(logFile);MH_Uninitialize();return 0;}
 if(memcmp(reinterpret_cast<void*>(engine+0x403d40),"\x0f\x10\x42\x10\x0f\x11\x41\x10\x0f\x10\x4a\x20\x0f\x11\x49\x20",16)||MH_CreateHook(reinterpret_cast<void*>(engine+0x403d40),reinterpret_cast<void*>(&VisualPort::Copy),reinterpret_cast<void**>(&VisualPort::copyOriginal))!=MH_OK){fprintf(logFile,"Visual port hook failed: Copy\n");fflush(logFile);MH_Uninitialize();return 0;}
 if(memcmp(reinterpret_cast<void*>(engine+0xb39850),"\x40\x55\x53\x41\x56\x48\x8d\xac\x24\xb0\xfd\xff\xff\x48\x81\xec",16)||MH_CreateHook(reinterpret_cast<void*>(engine+0xb39850),reinterpret_cast<void*>(&VisualPort::View),reinterpret_cast<void**>(&VisualPort::viewOriginal))!=MH_OK){fprintf(logFile,"Visual port hook failed: View\n");fflush(logFile);MH_Uninitialize();return 0;}
 if(memcmp(reinterpret_cast<void*>(engine+0x904180),"\x48\x89\x54\x24\x10\x55\x56\x41\x55\x41\x56\x48\x8b\xec\x48\x83",16)||MH_CreateHook(reinterpret_cast<void*>(engine+0x904180),reinterpret_cast<void*>(&VisualPort::Gui),reinterpret_cast<void**>(&VisualPort::guiOriginal))!=MH_OK){fprintf(logFile,"Visual port hook failed: Gui\n");fflush(logFile);MH_Uninitialize();return 0;}
 if(memcmp(reinterpret_cast<void*>(backend+0x6eeb0),"\x48\x89\x5c\x24\x08\x48\x89\x6c\x24\x10\x48\x89\x74\x24\x18\x57",16)||MH_CreateHook(reinterpret_cast<void*>(backend+0x6eeb0),reinterpret_cast<void*>(&VisualPort::Create),reinterpret_cast<void**>(&VisualPort::createOriginal))!=MH_OK){fprintf(logFile,"Visual port hook failed: Create\n");fflush(logFile);MH_Uninitialize();return 0;}
 if(memcmp(reinterpret_cast<void*>(engine+0xab3ff0),"\x40\x55\x53\x41\x55\x41\x56\x48\x8d\xac\x24\x08\xf9\xff\xff\x48",16)||MH_CreateHook(reinterpret_cast<void*>(engine+0xab3ff0),reinterpret_cast<void*>(&UiPort::Image),reinterpret_cast<void**>(&UiPort::imageOriginal))!=MH_OK){fprintf(logFile,"UI hook failed: Image\n");fflush(logFile);MH_Uninitialize();return 0;}
 if(memcmp(reinterpret_cast<void*>(engine+0xab7850),"\x48\x89\x5c\x24\x20\x4c\x89\x44\x24\x18\x48\x89\x54\x24\x10\x48",16)||MH_CreateHook(reinterpret_cast<void*>(engine+0xab7850),reinterpret_cast<void*>(&UiPort::Text),reinterpret_cast<void**>(&UiPort::textOriginal))!=MH_OK){fprintf(logFile,"UI hook failed: Text\n");fflush(logFile);MH_Uninitialize();return 0;}
 if(memcmp(reinterpret_cast<void*>(engine+0xab71c0),"\x40\x55\x56\x57\x41\x55\x41\x56\x48\x8d\xac\x24\x90\xf9\xff\xff",16)||MH_CreateHook(reinterpret_cast<void*>(engine+0xab71c0),reinterpret_cast<void*>(&UiPort::Movie),reinterpret_cast<void**>(&UiPort::movieOriginal))!=MH_OK){fprintf(logFile,"UI hook failed: Movie\n");fflush(logFile);MH_Uninitialize();return 0;}
 if(memcmp(reinterpret_cast<void*>(backend+0x838d0),"\x4c\x89\x44\x24\x18\x53\x55\x56\x57\x41\x54\x41\x55\x41\x56\x41",16)||MH_CreateHook(reinterpret_cast<void*>(backend+0x838d0),reinterpret_cast<void*>(&UiPort::Material),reinterpret_cast<void**>(&UiPort::materialOriginal))!=MH_OK){fprintf(logFile,"UI hook failed: Material\n");fflush(logFile);MH_Uninitialize();return 0;}
 if(memcmp(reinterpret_cast<void*>(engine+0xab3480),"\x48\x89\x5c\x24\x18\x55\x56\x57\x41\x56\x41\x57\x48\x8d\xac\x24",16)||MH_CreateHook(reinterpret_cast<void*>(engine+0xab3480),reinterpret_cast<void*>(&UiPort::Dynamic),reinterpret_cast<void**>(&UiPort::dynamicOriginal))!=MH_OK){fprintf(logFile,"UI hook failed: Dynamic\n");fflush(logFile);MH_Uninitialize();return 0;}
 if(memcmp(reinterpret_cast<void*>(engine+0xab3800),"\x48\x8b\xc4\x48\x89\x58\x20\x4c\x89\x40\x18\x55\x56\x57\x41\x54",16)||MH_CreateHook(reinterpret_cast<void*>(engine+0xab3800),reinterpret_cast<void*>(&UiPort::Graph),reinterpret_cast<void**>(&UiPort::graphOriginal))!=MH_OK){fprintf(logFile,"UI hook failed: Graph\n");fflush(logFile);MH_Uninitialize();return 0;}
 for(auto rva:TemporalHistory::writerReturns){auto call=reinterpret_cast<const unsigned char*>(engine+rva-5);int displacement{};memcpy(&displacement,call+1,4);if(call[0]!=0xe8||engine+rva+displacement!=engine+0x110fa90){fprintf(logFile,"Temporal writer signature failed\n");fflush(logFile);MH_Uninitialize();return 0;}}
 if(memcmp(reinterpret_cast<void*>(engine+0x110fa90),"\x48\x89\x5c\x24\x10\x48\x89\x6c\x24\x18\x56\x41\x56\x41\x57\x48",16)||MH_CreateHook(reinterpret_cast<void*>(engine+0x110fa90),reinterpret_cast<void*>(&TemporalPort::Lookup),reinterpret_cast<void**>(&TemporalPort::lookupOriginal))!=MH_OK){fprintf(logFile,"Temporal hook failed: Lookup\n");fflush(logFile);MH_Uninitialize();return 0;}
 if(memcmp(reinterpret_cast<void*>(engine+0x116e310),"\x48\x8b\xc4\x55\x56\x48\x8d\xa8\xc8\xf9\xff\xff\x48\x81\xec\x28",16)||MH_CreateHook(reinterpret_cast<void*>(engine+0x116e310),reinterpret_cast<void*>(&TemporalPort::External),reinterpret_cast<void**>(&TemporalPort::passOriginal))!=MH_OK){fprintf(logFile,"Temporal hook failed: External\n");fflush(logFile);MH_Uninitialize();return 0;}
 if(memcmp(reinterpret_cast<void*>(backend+0x3a560),"\x48\x8b\xc4\x48\x89\x58\x08\x48\x89\x70\x18\x55\x57\x41\x54\x41",16)||MH_CreateHook(reinterpret_cast<void*>(backend+0x3a560),reinterpret_cast<void*>(&TemporalPort::Evaluate),reinterpret_cast<void**>(&TemporalPort::evaluateOriginal))!=MH_OK){fprintf(logFile,"Temporal hook failed: Evaluate\n");fflush(logFile);MH_Uninitialize();return 0;}
 if(memcmp(reinterpret_cast<void*>(backend+0x77f20),"\x48\x8b\xc4\x48\x89\x58\x08\x48\x89\x70\x10\x48\x89\x78\x18\x55",16)||MH_CreateHook(reinterpret_cast<void*>(backend+0x77f20),reinterpret_cast<void*>(&TemporalPort::Constants),reinterpret_cast<void**>(&TemporalPort::constantsOriginal))!=MH_OK){fprintf(logFile,"Temporal hook failed: Constants\n");fflush(logFile);MH_Uninitialize();return 0;}
 MarkerPort::Install();MapPanel::Install();CrosshairUi::Install();
 auto status=MH_EnableHook(MH_ALL_HOOKS);fprintf(logFile,"hooks=%d\n",status);fflush(logFile);if(status!=MH_OK)return 0;
 bool wasF9=false;bool visualKeys[6]{};bool held=false,wasF7=false,wasF6=false,asyncCopies=true;ULONGLONG releasedAt=0;unsigned tick=0;
 for(;;){Sleep(20);
 bool f9=(GetAsyncKeyState(VK_F9)&0x8000)!=0;if(f9&&!wasF9)ShadowPort::Toggle();wasF9=f9;
 const int visualCodes[]={VK_F2,VK_F5,VK_F10,VK_F8,VK_F1,VK_F12};
 for(unsigned k=0;k<6;++k){bool down=(GetAsyncKeyState(visualCodes[k])&0x8000)!=0;if(down&&!visualKeys[k]){if(k==0)VisualPort::culling=!VisualPort::culling.load();if(k==1)VisualPort::auxiliary=!VisualPort::auxiliary.load();if(k==2)VisualPort::hideGui=!VisualPort::hideGui.load();if(k==4)UiPort::enabled=!UiPort::enabled.load();if(k==5)TemporalPort::mode=(TemporalPort::mode.load()+1)%3;VisualPort::Report(k==3);if(k==3){ShadowBindings::Arm();Dump();capture=true;}}visualKeys[k]=down;}
bool f7=(GetAsyncKeyState(VK_F7)&0x8000)!=0;if(f7&&!wasF7){bool on=!vrRequested.load();vrRequested=on;if(on)recenter=true;fprintf(logFile,"F7 requested=%u safetyBlocked=%u; effective at next scene boundary\n",on,safetyBlocked.load());fflush(logFile);}wasF7=f7;bool f6=(GetAsyncKeyState(VK_F6)&0x8000)!=0;if(f6&&!wasF6){recenter=true;TemporalPort::reset=true;}wasF6=f6;bool key=(GetAsyncKeyState(VK_INSERT)&0x8000)!=0;if(key){releasedAt=0;if(!held){held=true;WaterHistory::Toggle();}}
 else if(held){if(!releasedAt)releasedAt=GetTickCount64();else if(GetTickCount64()-releasedAt>=100)held=false;}
 if(++tick%250==0){MarkerPort::Report();VisualPort::Report(false);screen.Report([](const char* fmt,auto... values){fprintf(logFile,fmt,values...);fputc('\n',logFile);});if(!capture.load())Dump();fprintf(logFile,"counts scenes=%llu ends=%llu presents=%llu\n",scenes.load(),ends.load(),presents.load());fflush(logFile);}}
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID){if(reason!=DLL_PROCESS_ATTACH)return TRUE;self=module;wchar_t path[MAX_PATH]{};if(!GetSystemDirectoryW(path,MAX_PATH))return FALSE;wcscat_s(path,L"\\winmm.dll");auto original=LoadLibraryW(path);if(!original)return FALSE;for(unsigned i=0;i<kExportCount;++i){g_targets[i]=GetProcAddress(original,MAKEINTRESOURCEA(kOrdinals[i]));if(!g_targets[i])return FALSE;}auto t=CreateThread(nullptr,0,Worker,nullptr,0,nullptr);if(t)CloseHandle(t);return TRUE;}




