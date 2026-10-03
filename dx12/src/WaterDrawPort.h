#pragma once
#include "WaterHistory.h"
#include "ShadowPort.h"
// Only the three effect PS identities are registered. Ordinary draw calls do
// one cached command-list lookup, no mutex, OS memory read or shader-map lookup.
namespace WaterDrawPort {
struct Shader {std::atomic<void*> object{};std::atomic<uint64_t> hash{};};
inline Shader shaders[64];inline std::mutex registration;
inline std::atomic<unsigned> shaderCount{};
inline void Register(void* object,uint64_t hash,unsigned stage){if(stage||!object)return;std::lock_guard guard(registration);auto n=shaderCount.load();for(unsigned i=0;i<n;++i)if(shaders[i].object==object){shaders[i].hash=(WaterHistory::Target(hash)||ShadowPort::Target(hash))?hash:0;return;}if(!(WaterHistory::Target(hash)||ShadowPort::Target(hash))||n==64)return;shaders[n].hash=hash;shaders[n].object=object;shaderCount=n+1;}
struct Binding {std::atomic<ID3D12GraphicsCommandList*> list{};std::atomic<uintptr_t> state{};std::atomic<uint64_t> hash{};std::atomic<ID3D12PipelineState*> pipeline{};};
inline Binding bindings[4096];inline std::atomic<uint64_t> exhausted{},targetDraws{};
inline Binding* Find(ID3D12GraphicsCommandList* list,bool create=false){
 thread_local ID3D12GraphicsCommandList* previous{};thread_local Binding* cached{};
 if(previous==list&&cached)return cached;
 auto start=(reinterpret_cast<uintptr_t>(list)>>4)%std::size(bindings);
 for(unsigned i=0;i<std::size(bindings);++i){auto& b=bindings[(start+i)%std::size(bindings)];auto key=b.list.load(std::memory_order_relaxed);if(key==list){previous=list;return cached=&b;}if(!key){if(!create)return nullptr;ID3D12GraphicsCommandList* empty{};if(b.list.compare_exchange_strong(empty,list)){previous=list;return cached=&b;}if(empty==list){previous=list;return cached=&b;}}}if(create)++exhausted;return nullptr;
}
template<class T>inline T Native(uintptr_t p){T v{};__try{memcpy(&v,reinterpret_cast<void*>(p),sizeof(v));}__except(EXCEPTION_EXECUTE_HANDLER){}return v;}
inline ID3D12GraphicsCommandList* CommandList(uintptr_t state){unsigned count=Native<unsigned char>(state+0x897);count=count?count-1:Native<unsigned>(state+0x898);if(!count||count>4096)return nullptr;auto entries=Native<uintptr_t>(state+0x890)&0xffffffffffffull;return Native<ID3D12GraphicsCommandList*>(Native<uintptr_t>(entries+(count-1)*32+0x10));}
inline void Bound(void* state,void* descriptor,void* packet){
 auto native=reinterpret_cast<uintptr_t>(state);auto list=CommandList(native);if(!list)return;auto b=Find(list,true);if(!b)return;
 auto material=Native<uintptr_t>(reinterpret_cast<uintptr_t>(packet));auto owner=Native<uintptr_t>(material+0x30);auto table=Native<uintptr_t>(owner+0x168)&0xffffffffffffull;auto index=Native<unsigned short>(reinterpret_cast<uintptr_t>(descriptor)+2);auto object=Native<void*>(table+index*8);uint64_t hash=0;
 auto n=shaderCount.load(std::memory_order_acquire);for(unsigned i=0;i<n;++i)if(shaders[i].object.load(std::memory_order_relaxed)==object){hash=shaders[i].hash.load(std::memory_order_relaxed);break;}
 b->state.store(native,std::memory_order_relaxed);b->hash.store(hash,std::memory_order_release);
}
inline void Before(ID3D12GraphicsCommandList* list){auto b=Find(list);if(!b)return;auto hash=b->hash.load(std::memory_order_acquire);if(!hash)return;++targetDraws;WaterHistory::Before(list,b->state.load(std::memory_order_relaxed),hash);}
inline void ClearBinding(ID3D12GraphicsCommandList* list){if(auto b=Find(list))b->hash=0;}
using DrawFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,UINT,UINT,UINT);inline DrawFn drawOriginal;
using IndexedFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,UINT,UINT,INT,UINT);inline IndexedFn indexedOriginal;
using ResetFn=HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12CommandAllocator*,ID3D12PipelineState*);inline ResetFn resetOriginal;
using ClearFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12PipelineState*);inline ClearFn clearOriginal;
inline void STDMETHODCALLTYPE Draw(ID3D12GraphicsCommandList* l,UINT a,UINT b,UINT c,UINT d){auto entry=Find(l);auto h=entry?entry->hash.load():0;if(ShadowPort::Target(h)){ShadowPort::Scope scope(l,entry->state.load(),h,entry->pipeline.load());drawOriginal(l,a,b,c,d);}else{Before(l);drawOriginal(l,a,b,c,d);}}
inline void STDMETHODCALLTYPE Indexed(ID3D12GraphicsCommandList* l,UINT a,UINT b,UINT c,INT d,UINT e){auto entry=Find(l);auto h=entry?entry->hash.load():0;if(ShadowPort::Target(h)){ShadowPort::Scope scope(l,entry->state.load(),h,entry->pipeline.load());indexedOriginal(l,a,b,c,d,e);}else{Before(l);indexedOriginal(l,a,b,c,d,e);}}
inline HRESULT STDMETHODCALLTYPE Reset(ID3D12GraphicsCommandList* l,ID3D12CommandAllocator* a,ID3D12PipelineState* p){auto hr=resetOriginal(l,a,p);if(SUCCEEDED(hr)&&!WaterHistory::injecting){ClearBinding(l);if(auto entry=Find(l,true))entry->pipeline=p;WaterHistory::Forget(l);}return hr;}
inline void STDMETHODCALLTYPE Clear(ID3D12GraphicsCommandList* l,ID3D12PipelineState* p){clearOriginal(l,p);if(!WaterHistory::injecting){ClearBinding(l);if(auto entry=Find(l,true))entry->pipeline=p;}}
using PipelineFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12PipelineState*);inline PipelineFn pipelineOriginal;
inline void STDMETHODCALLTYPE Pipeline(ID3D12GraphicsCommandList* l,ID3D12PipelineState* p){pipelineOriginal(l,p);if(!WaterHistory::injecting)if(auto entry=Find(l,true))entry->pipeline=p;}
inline bool installed{},shadowInstalled{};
inline constexpr unsigned char factorySignature[]={0x40,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x57,0x48,0x8d,0xac,0x24,0x78,0xeb,0xff,0xff};
inline bool FactorySignature(uintptr_t address){for(unsigned i=0;i<sizeof(factorySignature);++i)if(Read<unsigned char>(address+i)!=factorySignature[i])return false;return true;}
struct Hook{void* target;void* replacement;void** original;};
template<size_t N>inline bool HookGroup(const char* label,Hook(&hooks)[N]){
 unsigned created=0;
 for(auto h:hooks){auto status=MH_CreateHook(h.target,h.replacement,h.original);if(status!=MH_OK){fprintf(logFile,"%s create hook %u failed: %s\n",label,created,MH_StatusToString(status));break;}++created;}
 if(created!=N){for(unsigned i=0;i<created;++i)MH_RemoveHook(hooks[i].target);return false;}
 bool ok=true;for(auto h:hooks)if(MH_QueueEnableHook(h.target)!=MH_OK)ok=false;
 if(ok)ok=MH_ApplyQueued()==MH_OK;
 if(!ok){for(auto h:hooks){MH_DisableHook(h.target);MH_RemoveHook(h.target);}fprintf(logFile,"%s enable failed\n",label);return false;}
 return true;
}
inline bool Install(ID3D12Device* device,ID3D12CommandQueue* queue){if(installed)return true;if(!queue)return false;
 WaterHistory::queue=queue;
 Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
 if(FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)))||FAILED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))))return false;
 list->Close();auto vt=*reinterpret_cast<void***>(list.Get()),qvt=*reinterpret_cast<void***>(queue),dvt=*reinterpret_cast<void***>(device);
 Hook water[]={{vt[10],reinterpret_cast<void*>(&Reset),reinterpret_cast<void**>(&resetOriginal)},{vt[11],reinterpret_cast<void*>(&Clear),reinterpret_cast<void**>(&clearOriginal)},{vt[12],reinterpret_cast<void*>(&Draw),reinterpret_cast<void**>(&drawOriginal)},{vt[13],reinterpret_cast<void*>(&Indexed),reinterpret_cast<void**>(&indexedOriginal)},{vt[26],reinterpret_cast<void*>(&WaterHistory::Barriers),reinterpret_cast<void**>(&WaterHistory::barrierOriginal)},{qvt[10],reinterpret_cast<void*>(&WaterHistory::Execute),reinterpret_cast<void**>(&WaterHistory::executeOriginal)}};
 if(!HookGroup("Water",water))return false;
 installed=true;fprintf(logFile,"Water/reflection hooks installed independently\n");
 if(!FactorySignature(backend+0x5ef10)){fprintf(logFile,"Shadow factory signature mismatch; water remains active\n");return true;}
 Hook shadows[]={{vt[25],reinterpret_cast<void*>(&Pipeline),reinterpret_cast<void**>(&pipelineOriginal)},{dvt[10],reinterpret_cast<void*>(&ShadowPort::Create),reinterpret_cast<void**>(&ShadowPort::createOriginal)},{reinterpret_cast<void*>(backend+0x5ef10),reinterpret_cast<void*>(&ShadowPort::Factory),reinterpret_cast<void**>(&ShadowPort::factoryOriginal)}};
 shadowInstalled=HookGroup("Shadow",shadows);fprintf(logFile,"Shadow hooks installed=%u; factory signature verified\n",shadowInstalled);return true;
}
inline void Report(){ShadowPort::Report();WaterHistory::Report();fprintf(logFile,"Water draw targets=%llu bindingCapacityMisses=%llu shaders=%u installed=%u shadowInstalled=%u\n",targetDraws.load(),exhausted.load(),shaderCount.load(),installed,shadowInstalled);}
}
