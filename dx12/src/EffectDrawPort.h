#pragma once
#include <unordered_map>
#include <mutex>
#include "NativeTaaTrace.h"
#include "NativeTaaHistory.h"
#include "WaterHistory.h"
namespace EffectDrawPort {
inline std::atomic<bool> disableTemporalFilter{};
inline std::atomic<uint64_t> skipped{},targetDraws{},bindingMisses{};
inline std::mutex mutex;
inline std::unordered_map<void*,uint64_t> shaders;
struct Binding {uint64_t hash{};uintptr_t state{};};
inline std::unordered_map<ID3D12GraphicsCommandList*,Binding> bindings;
inline wchar_t settingsPath[MAX_PATH]{};
inline std::atomic<bool> installed{};
inline void Settings(){GetModuleFileNameW(self,settingsPath,MAX_PATH);auto slash=wcsrchr(settingsPath,L'\\');if(!slash)return;wcscpy_s(slash+1,MAX_PATH-(slash+1-settingsPath),L"DL2VR.ini");disableTemporalFilter=GetPrivateProfileIntW(L"Rendering",L"DisableTemporalFilter",0,settingsPath)!=0;}
inline void Toggle(){bool off=!disableTemporalFilter.load();disableTemporalFilter=off;bool saved=WritePrivateProfileStringW(L"Rendering",L"DisableTemporalFilter",off?L"1":L"0",settingsPath)!=0;fprintf(logFile,"F11 temporal filter disabled=%u saved=%u\n",off,saved);fflush(logFile);}
inline void Register(void* shader,uint64_t hash,unsigned stage){if(stage||!shader)return;std::lock_guard guard(mutex);shaders[shader]=hash;}
inline ID3D12GraphicsCommandList* CommandList(uintptr_t state){
 // Same native command-list selection as guarded backend 7ac10.
 unsigned count=Read<unsigned char>(state+0x897);count=count?count-1:Read<unsigned>(state+0x898);if(!count||count>4096)return nullptr;
 auto entries=Read<uintptr_t>(state+0x890)&0xffffffffffffull;
 auto wrapper=Read<uintptr_t>(entries+(count-1)*32+0x10);
 return Read<ID3D12GraphicsCommandList*>(wrapper);
}
inline void Bound(void* state,void* descriptor,void* packet){
 constexpr uintptr_t mask=0xffffffffffffull;
 auto material=Read<uintptr_t>(reinterpret_cast<uintptr_t>(packet));auto owner=Read<uintptr_t>(material+0x30);
 auto table=Read<uintptr_t>(owner+0x168)&mask;auto index=Read<unsigned short>(reinterpret_cast<uintptr_t>(descriptor)+2);
 auto shader=Read<void*>(table+index*8);auto list=CommandList(reinterpret_cast<uintptr_t>(state));if(!list){++bindingMisses;return;}
 std::lock_guard guard(mutex);auto it=shaders.find(shader);bindings[list]={it==shaders.end()?0:it->second,reinterpret_cast<uintptr_t>(state)};
}
inline bool Suppress(ID3D12GraphicsCommandList* list){std::lock_guard guard(mutex);auto it=bindings.find(list);if(it==bindings.end()||it->second.hash!=0x7bff3ae2c1f5835aull)return false;++targetDraws;if(!disableTemporalFilter)return false;++skipped;return true;}
using DrawFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,UINT,UINT,UINT);inline DrawFn drawOriginal;
using IndexedFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,UINT,UINT,INT,UINT);inline IndexedFn indexedOriginal;
using ResetFn=HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12CommandAllocator*,ID3D12PipelineState*);inline ResetFn resetOriginal;
using ClearFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12PipelineState*);inline ClearFn clearOriginal;
inline void Forget(ID3D12GraphicsCommandList* list){{std::lock_guard guard(mutex);bindings.erase(list);}WaterHistory::Forget(list);}
inline uintptr_t TaaState(ID3D12GraphicsCommandList* list){std::lock_guard guard(mutex);auto it=bindings.find(list);return it!=bindings.end()&&it->second.hash==0x7bff3ae2c1f5835aull?it->second.state:0;}
inline void BeforeEffects(ID3D12GraphicsCommandList* list){Binding b;{std::lock_guard guard(mutex);auto it=bindings.find(list);if(it==bindings.end())return;b=it->second;}WaterHistory::Before(list,b.state,b.hash);}
inline void STDMETHODCALLTYPE Draw(ID3D12GraphicsCommandList* list,UINT a,UINT b,UINT c,UINT d){BeforeEffects(list);if(!Suppress(list)){auto state=TaaState(list);NativeTaaHistory::Scope history(list,state);if(state)NativeTaaTrace::Before(list,state);drawOriginal(list,a,b,c,d);}}
inline void STDMETHODCALLTYPE Indexed(ID3D12GraphicsCommandList* list,UINT a,UINT b,UINT c,INT d,UINT e){BeforeEffects(list);if(!Suppress(list)){auto state=TaaState(list);NativeTaaHistory::Scope history(list,state);if(state)NativeTaaTrace::Before(list,state);indexedOriginal(list,a,b,c,d,e);}}
inline HRESULT STDMETHODCALLTYPE Reset(ID3D12GraphicsCommandList* list,ID3D12CommandAllocator* allocator,ID3D12PipelineState* pso){auto hr=resetOriginal(list,allocator,pso);if(SUCCEEDED(hr)&&!WaterHistory::injecting)Forget(list);return hr;}
inline void STDMETHODCALLTYPE Clear(ID3D12GraphicsCommandList* list,ID3D12PipelineState* pso){clearOriginal(list,pso);if(!WaterHistory::injecting){std::lock_guard guard(mutex);bindings.erase(list);}}
inline bool Install(ID3D12Device* device,ID3D12CommandQueue* queue){
 if(installed)return true;if(!queue)return false;WaterHistory::queue=queue;
 Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
 if(FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)))||FAILED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))))return false;
 list->Close();auto vt=*reinterpret_cast<void***>(list.Get());
 auto qvt=*reinterpret_cast<void***>(queue);
 struct Hook{void* target;void* replacement;void** original;};
 Hook hooks[]={{vt[17],reinterpret_cast<void*>(&NativeTaaTrace::Copy),reinterpret_cast<void**>(&NativeTaaTrace::copyOriginal)},{vt[16],reinterpret_cast<void*>(&NativeTaaTrace::Region),reinterpret_cast<void**>(&NativeTaaTrace::regionOriginal)},{vt[10],reinterpret_cast<void*>(&Reset),reinterpret_cast<void**>(&resetOriginal)},{vt[11],reinterpret_cast<void*>(&Clear),reinterpret_cast<void**>(&clearOriginal)},{vt[12],reinterpret_cast<void*>(&Draw),reinterpret_cast<void**>(&drawOriginal)},{vt[13],reinterpret_cast<void*>(&Indexed),reinterpret_cast<void**>(&indexedOriginal)},{vt[26],reinterpret_cast<void*>(&WaterHistory::Barriers),reinterpret_cast<void**>(&WaterHistory::barrierOriginal)},{qvt[10],reinterpret_cast<void*>(&WaterHistory::Execute),reinterpret_cast<void**>(&WaterHistory::executeOriginal)}};
 unsigned created=0;for(auto h:hooks){if(MH_CreateHook(h.target,h.replacement,h.original)!=MH_OK)break;++created;}
 if(created!=std::size(hooks)){for(unsigned i=0;i<created;++i)MH_RemoveHook(hooks[i].target);return false;}
 for(auto h:hooks)MH_QueueEnableHook(h.target);if(MH_ApplyQueued()!=MH_OK){for(auto h:hooks)MH_RemoveHook(h.target);return false;}
 installed=true;fprintf(logFile,"DX12 effect draw hooks installed; exact native temporal shader F11\n");return true;
}
inline void Report(){WaterHistory::Report();NativeTaaHistory::Report();fprintf(logFile,"DX12 F11 disabled=%u installed=%u targetDraws=%llu skipped=%llu bindingMisses=%llu\n",disableTemporalFilter.load(),installed.load(),targetDraws.load(),skipped.load(),bindingMisses.load());}
}
