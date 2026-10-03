#pragma once
#include "ReflectionProjection.h"
#include "FlashlightProjection.h"
#include <unordered_map>
#include <mutex>
namespace VisualPort {
inline std::atomic<bool> culling{true},auxiliary{false},hideGui{false};
inline thread_local uintptr_t camera{},level{};
inline std::atomic<const void*> inputs[2]{};
inline std::atomic<uint64_t> queries{},edges{},redirects{},hidden{},reflection{},flashlight{};
using MainFn=uintptr_t(*)(void*,unsigned,const void*,const void*);inline MainFn mainOriginal;
using WorldFn=uintptr_t(*)(void*,const void*,const void*);inline WorldFn worldOriginal;
using ProjectFn=int(*)(const void*,const void*,const void*,void*,float);inline ProjectFn projectOriginal;
using CameraFn=void*(*)(void*);inline CameraFn cameraOriginal;
using CopyFn=uintptr_t(*)(void*,const void*);inline CopyFn copyOriginal;
using ViewFn=uintptr_t(*)(void*);inline ViewFn viewOriginal;
using GuiFn=void(*)(void*,void*,bool);inline GuiFn guiOriginal;
inline uintptr_t Main(void* manager,unsigned view,const void* cam,const void* input){if(camera&&view==20&&reinterpret_cast<uintptr_t>(_ReturnAddress())==engine+0xb3a0e7)inputs[0]=input;return mainOriginal(manager,view,cam,input);}
inline uintptr_t World(void* world,const void* cam,const void* input){if(camera==reinterpret_cast<uintptr_t>(cam)&&camera&&reinterpret_cast<uintptr_t>(_ReturnAddress())==engine+0xb39ec5)inputs[1]=input;return worldOriginal(world,cam,input);}
inline int Project(const void* input,const void* bounds,const void* transform,void* rect,float bias){auto r=projectOriginal(input,bounds,transform,rect,bias);if(stereoEnabled&&culling&&input&&(input==inputs[0]||input==inputs[1])&&reinterpret_cast<uintptr_t>(_ReturnAddress())==engine+0xb89315){++queries;if(!r){++edges;return 1;}}return r;}
inline void* Camera(void* wrapper){auto r=cameraOriginal(wrapper);if(auxiliary&&camera&&reinterpret_cast<uintptr_t>(_ReturnAddress())==engine+0xb493e5&&reinterpret_cast<uintptr_t>(wrapper)+0x30==camera){++redirects;return reinterpret_cast<void*>(camera);}return r;}
inline uintptr_t Copy(void* output,const void* source){if(auxiliary&&camera&&level+0x13b0==camera&&reinterpret_cast<uintptr_t>(_ReturnAddress())==engine+0xb39ad8){source=reinterpret_cast<void*>(camera);++redirects;}return copyOriginal(output,source);}
inline uintptr_t View(void* object){auto old=level;level=reinterpret_cast<uintptr_t>(object);auto r=viewOriginal(object);level=old;return r;}
inline void Gui(void* manager,void* data,bool background){if(reinterpret_cast<uintptr_t>(_ReturnAddress())==engine+0x82cc82)UiPort::Aspect(manager);if(hideGui&&reinterpret_cast<uintptr_t>(_ReturnAddress())==engine+0x82cc82){auto f=reinterpret_cast<unsigned char*>(manager)+0x55c;auto enabled=*f&4;*f&=~4;guiOriginal(manager,data,background);*f=(*f&~4)|enabled;++hidden;}else guiOriginal(manager,data,background);}
struct ShaderInfo{uint64_t hash{};unsigned bytes{},stage{},reflectionSites{};bool flashlightFixed{};};
inline std::mutex shadersLock;inline std::unordered_map<uint64_t,ShaderInfo> shaders;
using CreateFn=bool(*)(void*,void**,const void*,unsigned,unsigned,void*);inline CreateFn createOriginal;
inline bool Create(void* backendObject,void** output,const void* data,unsigned bytes,unsigned stage,void* description){
 if(!data||bytes<32||bytes>1024*1024)return createOriginal(backendObject,output,data,bytes,stage,description);
 auto hash=ShaderHash(data,bytes);std::vector<unsigned char> corrected,light;
 unsigned sites=(stage==0||stage==5)?PatchReflectionProjection(data,bytes,corrected):0;bool fixed=stage==1&&PatchFlashlightProjection(data,bytes,light);
 auto r=createOriginal(backendObject,output,fixed?light.data():sites?corrected.data():data,fixed?unsigned(light.size()):bytes,stage,description);
 if(!r&&(sites||fixed)){sites=0;fixed=false;r=createOriginal(backendObject,output,data,bytes,stage,description);}
 if(r && (sites||fixed)){
 // DX12 can return an existing shader object without replacing its bytecode.
 auto object=output?reinterpret_cast<uintptr_t>(*output):0;auto actual=Read<const void*>(object);auto actualBytes=Read<size_t>(object+8);
 const auto& expected=fixed?light:corrected;
 if(!actual||actualBytes!=expected.size()||memcmp(actual,expected.data(),expected.size())){sites=0;fixed=false;}
 }
 if(r){if(output){WaterDrawPort::Register(*output,hash,stage);UiPort::Register(*output,hash,stage);ShadowBindings::Register(*output,hash,stage);}if(sites)++reflection;if(fixed)++flashlight;std::lock_guard guard(shadersLock);shaders[hash]={hash,bytes,stage,sites,fixed};}
 return r;
}
inline void Report(bool inventory){
 UiPort::Report();TemporalPort::Report();WaterDrawPort::Report();
 fprintf(logFile,"Visual ports F2=%u queries=%llu edgeVisible=%llu F5=%u redirects=%llu F10=%u hidden=%llu shaderReflection=%llu flashlightProjection=%llu\n",culling.load(),queries.load(),edges.load(),auxiliary.load(),redirects.load(),hideGui.load(),hidden.load(),reflection.load(),flashlight.load());
 if(inventory){std::lock_guard guard(shadersLock);for(auto& [hash,s]:shaders)fprintf(logFile,"DX12 shader hash=%016llx stage=%u bytes=%u projectionSites=%u flashlight=%u\n",hash,s.stage,s.bytes,s.reflectionSites,s.flashlightFixed);}
 fflush(logFile);
}
}
