#pragma once
#include <d3d11.h>
#include <algorithm>
#include "EffectsCapture.h"
#include "GpuTrace.h"
#include "TextureHistory.h"
#include "FlashlightShadow.h"
namespace ShaderHunter {
struct Entry {uint64_t hash;unsigned stage;};
inline SRWLOCK lock=SRWLOCK_INIT;
inline Entry candidates[8192]{};
inline unsigned count{},index{};
inline bool frozen{},selected{};
inline std::atomic<bool> enabled{};
inline std::atomic<uint64_t> chosen{},skipped{};
inline std::atomic<unsigned> chosenStage{};
inline bool installed{};
inline wchar_t directory[MAX_PATH]{};
inline std::atomic<bool> disableTemporalFilter{};
inline std::atomic<void*> temporalShaders[64]{};
inline void RegisterTemporalShader(void* native,uint64_t hash,unsigned stage) {
    TextureHistory::Register(native,hash,stage);
    // Clear reused native addresses even when the new shader is unrelated.
    for(auto& slot:temporalShaders){void* old=native;slot.compare_exchange_strong(old,nullptr);}
    if(!native || stage!=0 || hash!=0x7bff3ae2c1f5835aull)return;
    for(auto& slot:temporalShaders){void* empty=nullptr;if(slot.compare_exchange_strong(empty,native))return;}
}
inline bool IsTemporalShader(void* native) {
    if(!native)return false;
    for(auto& slot:temporalShaders)if(slot.load()==native)return true;
    return false;
}
inline void Reset() {
    enabled=false;chosen=0;
    AcquireSRWLockExclusive(&lock);count=0;index=0;selected=false;frozen=false;ReleaseSRWLockExclusive(&lock);
}
inline void Enable(){Reset();enabled=true;}
inline bool Observe(uint64_t hash,unsigned stage) {
    if(!enabled || !hash)return false;
    AcquireSRWLockExclusive(&lock);
    if(!frozen) {
        unsigned i=0;for(;i<count;++i)if(candidates[i].hash==hash && candidates[i].stage==stage)break;
        if(i==count && count<8192)candidates[count++]={hash,stage};
    }
    ReleaseSRWLockExclusive(&lock);
    return enabled && chosen==hash && chosenStage==stage;
}
inline bool Step(int direction,Entry& out,unsigned& position,unsigned& total) {
    if(!enabled)return false;
    AcquireSRWLockExclusive(&lock);
    if(!count){ReleaseSRWLockExclusive(&lock);return false;}
    if(!frozen){std::sort(candidates,candidates+count,[](Entry a,Entry b){return a.stage==b.stage?a.hash<b.hash:a.stage<b.stage;});frozen=true;}
    if(!selected){index=direction>0?0:count-1;selected=true;}
    else index=direction>0?(index+1)%count:(index+count-1)%count;
    out=candidates[index];position=index+1;total=count;
    chosen=0;chosenStage=out.stage;chosen=out.hash;skipped=0;
    ReleaseSRWLockExclusive(&lock);return true;
}
inline uint64_t Identity(void* native,unsigned stage) {
    uint64_t hash=0;
    AcquireSRWLockShared(&EffectsCapture::shaderLock);
    for(unsigned i=0;i<EffectsCapture::shaderCount;++i) {
        const auto& s=EffectsCapture::shaders[i];
        if(s.native==native && s.stage==stage){hash=s.hash;break;}
    }
    ReleaseSRWLockShared(&EffectsCapture::shaderLock);return hash;
}
inline bool Suppress(ID3D11DeviceContext* context,bool compute) {
    if(!compute)TextureHistory::BeforeDraw(context);
    if(GpuTrace::activeEye.load())GpuTrace::Draw(context,compute);
    if(!enabled && (compute || !disableTemporalFilter))return false;
    uint64_t hash{};
    if(compute){ID3D11ComputeShader* shader{};context->CSGetShader(&shader,nullptr,nullptr);if(shader){hash=Identity(shader,5);shader->Release();}}
    else {ID3D11PixelShader* shader{};context->PSGetShader(&shader,nullptr,nullptr);if(shader){
        bool suppress=disableTemporalFilter && IsTemporalShader(shader);
        if(enabled)hash=Identity(shader,0);shader->Release();
        if(suppress)return true;
    }}
    if(Observe(hash,compute?5:0)){++skipped;return true;}return false;
}
using IndexedFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,INT);
using DrawFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT);
using IndexedInstFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT,INT,UINT);
using InstFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT,UINT);
using AutoFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*);
using IndirectFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Buffer*,UINT);
using DispatchFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT);
inline IndexedFn indexed;inline DrawFn draw;inline IndexedInstFn indexedInst;inline InstFn inst;inline AutoFn automatic;
inline IndirectFn indexedIndirect,indirect,dispatchIndirect;inline DispatchFn dispatch;
inline void STDMETHODCALLTYPE Indexed(ID3D11DeviceContext* c,UINT a,UINT b,INT d){if(!Suppress(c,false)){FlashlightShadow::DrawScope shadow(c);indexed(c,a,b,d);}}
inline void STDMETHODCALLTYPE Draw(ID3D11DeviceContext* c,UINT a,UINT b){if(!Suppress(c,false)){FlashlightShadow::DrawScope shadow(c);draw(c,a,b);}}
inline void STDMETHODCALLTYPE IndexedInst(ID3D11DeviceContext* c,UINT a,UINT b,UINT d,INT e,UINT f){if(!Suppress(c,false)){FlashlightShadow::DrawScope shadow(c);indexedInst(c,a,b,d,e,f);}}
inline void STDMETHODCALLTYPE Inst(ID3D11DeviceContext* c,UINT a,UINT b,UINT d,UINT e){if(!Suppress(c,false)){FlashlightShadow::DrawScope shadow(c);inst(c,a,b,d,e);}}
inline void STDMETHODCALLTYPE Auto(ID3D11DeviceContext* c){if(!Suppress(c,false)){FlashlightShadow::DrawScope shadow(c);automatic(c);}}
inline void STDMETHODCALLTYPE IndexedIndirect(ID3D11DeviceContext* c,ID3D11Buffer* b,UINT o){if(!Suppress(c,false)){FlashlightShadow::DrawScope shadow(c);indexedIndirect(c,b,o);}}
inline void STDMETHODCALLTYPE Indirect(ID3D11DeviceContext* c,ID3D11Buffer* b,UINT o){if(!Suppress(c,false)){FlashlightShadow::DrawScope shadow(c);indirect(c,b,o);}}
inline void STDMETHODCALLTYPE Dispatch(ID3D11DeviceContext* c,UINT a,UINT b,UINT d){if(!Suppress(c,true))dispatch(c,a,b,d);}
inline void STDMETHODCALLTYPE DispatchIndirect(ID3D11DeviceContext* c,ID3D11Buffer* b,UINT o){if(!Suppress(c,true))dispatchIndirect(c,b,o);}

using CopyFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,ID3D11Resource*);
using CopyRegionFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT,UINT,UINT,UINT,ID3D11Resource*,UINT,const D3D11_BOX*);
inline CopyFn copy;inline CopyRegionFn copyRegion;
using SetPSFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11PixelShader*,ID3D11ClassInstance* const*,UINT);
inline SetPSFn setPS;
inline void STDMETHODCALLTYPE SetPS(ID3D11DeviceContext* c,ID3D11PixelShader* ps,ID3D11ClassInstance* const* instances,UINT count) {
    if(c==TextureHistory::context)TextureHistory::boundHash=TextureHistory::Identify(ps);
    if(c==FlashlightShadow::context)FlashlightShadow::Bound(ps);
    setPS(c,ps,instances,count);
}
inline void STDMETHODCALLTYPE Copy(ID3D11DeviceContext* c,ID3D11Resource* dst,ID3D11Resource* src){if(GpuTrace::activeEye.load())GpuTrace::Copy(dst,src,10);copy(c,dst,src);}
inline void STDMETHODCALLTYPE CopyRegion(ID3D11DeviceContext* c,ID3D11Resource* dst,UINT ds,UINT x,UINT y,UINT z,ID3D11Resource* src,UINT ss,const D3D11_BOX* box){if(GpuTrace::activeEye.load())GpuTrace::Copy(dst,src,11);copyRegion(c,dst,ds,x,y,z,src,ss,box);}
inline bool Install(ID3D11DeviceContext* context) {
    if(installed)return true;
    GpuTrace::identity=Identity;
    auto vt=*reinterpret_cast<void***>(context);
    struct Hook {unsigned slot;void* detour;void** original;};
    Hook hooks[]={{9,(void*)SetPS,(void**)&setPS},{12,(void*)Indexed,(void**)&indexed},{13,(void*)Draw,(void**)&draw},{20,(void*)IndexedInst,(void**)&indexedInst},{21,(void*)Inst,(void**)&inst},{38,(void*)Auto,(void**)&automatic},{39,(void*)IndexedIndirect,(void**)&indexedIndirect},{40,(void*)Indirect,(void**)&indirect},{41,(void*)Dispatch,(void**)&dispatch},{42,(void*)DispatchIndirect,(void**)&dispatchIndirect},{46,(void*)CopyRegion,(void**)&copyRegion},{47,(void*)Copy,(void**)&copy}};
    unsigned created=0;
    for(auto h:hooks){if(MH_CreateHook(vt[h.slot],h.detour,h.original)!=MH_OK)break;++created;}
    bool ok=created==std::size(hooks);
    if(ok)for(auto h:hooks)if(MH_EnableHook(vt[h.slot])!=MH_OK){ok=false;break;}
    if(!ok){for(unsigned i=0;i<created;++i){MH_DisableHook(vt[hooks[i].slot]);MH_RemoveHook(vt[hooks[i].slot]);}return false;}
    TextureHistory::context=context;installed=true;return true;
}
inline bool Mark(uint64_t& hash,unsigned& stage) {
    hash=chosen.load();stage=chosenStage.load();if(!enabled || !hash)return false;
    void* data{};unsigned bytes{};
    AcquireSRWLockShared(&EffectsCapture::shaderLock);
    for(unsigned i=0;i<EffectsCapture::shaderCount;++i){auto& s=EffectsCapture::shaders[i];if(s.hash==hash && s.stage==stage && s.data){bytes=s.bytes;data=HeapAlloc(GetProcessHeap(),0,bytes);if(data)memcpy(data,s.data,bytes);break;}}
    ReleaseSRWLockShared(&EffectsCapture::shaderLock);
    if(!data)return false;
    wchar_t folder[MAX_PATH],path[MAX_PATH];swprintf_s(folder,L"%s\\DL2VR-marked-shaders",directory);CreateDirectoryW(folder,nullptr);
    swprintf_s(path,L"%s\\stage%u-%016llx.dxbc",folder,stage,hash);
    FILE* f{};_wfopen_s(&f,path,L"wb");bool ok=false;
    if(f){ok=fwrite(data,1,bytes,f)==bytes;if(fclose(f))ok=false;}HeapFree(GetProcessHeap(),0,data);
    if(ok){swprintf_s(path,L"%s\\marks.txt",folder);_wfopen_s(&f,path,L"a");if(f){fprintf(f,"tick=%llu stage=%u hash=%016llx suppressed=%llu\n",GetTickCount64(),stage,hash,skipped.load());if(fclose(f))ok=false;}else ok=false;}
    return ok;
}
}
