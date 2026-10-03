#pragma once
#include <memory>
#include <array>
#include <unordered_map>
// Five DX11 TextureHistory semantics, restored before native eye work and saved
// after its queue submissions. Track actual submitted per-mip D3D12 states.
namespace WaterHistory {
template<class T> using Com=Microsoft::WRL::ComPtr<T>;
inline std::mutex mutex;
inline std::atomic<bool> enabled{true};
inline std::atomic<uint64_t> discovered{},restored{},saved{},failed{},unknown{},waits{};
inline thread_local bool injecting{};
inline ID3D12CommandQueue* queue{};
struct Bank {Com<ID3D12Resource> source,history[2];D3D12_RESOURCE_DESC desc{};std::vector<int64_t> states;bool valid[2]{},seen{};unsigned counter[2]{};};
inline std::shared_ptr<Bank> banks[5];
inline std::atomic<ID3D12Resource*> watched[5]{};
struct Tracked {std::shared_ptr<Bank> bank;std::vector<int64_t> states;};
inline std::unordered_map<ID3D12CommandList*,std::unordered_map<ID3D12Resource*,Tracked>> lists;
inline unsigned activeEye{},activeCounter{},epoch{};inline bool active{};
inline bool Target(uint64_t h){return h==0xe275ecd626c81c62ull||h==0x6b258d38d343a365ull||h==0x637fddd208eb09bdull;}
inline void Invalidate(){for(auto& b:banks)if(b){b->valid[0]=b->valid[1]=b->seen=false;}}
inline void Forget(ID3D12GraphicsCommandList* list){if(injecting)return;std::lock_guard guard(mutex);lists.erase(list);}
inline Tracked& Track(ID3D12CommandList* list,const std::shared_ptr<Bank>& bank){auto& t=lists[list][bank->source.Get()];if(t.bank!=bank){t.bank=bank;t.states.assign(bank->desc.MipLevels,-2);}return t;}
using BarrierFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,const D3D12_RESOURCE_BARRIER*);inline BarrierFn barrierOriginal;
inline void STDMETHODCALLTYPE Barriers(ID3D12GraphicsCommandList* list,UINT n,const D3D12_RESOURCE_BARRIER* values){
 bool relevant=false;if(!injecting)for(UINT i=0;i<n&&!relevant;++i)if(values[i].Type==D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)for(auto& r:watched)if(r.load(std::memory_order_relaxed)==values[i].Transition.pResource){relevant=true;break;}
 if(relevant){std::lock_guard guard(mutex);for(UINT i=0;i<n;++i){auto& v=values[i];if(v.Type!=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)continue;
  for(auto& b:banks)if(b&&b->source.Get()==v.Transition.pResource){auto& t=Track(list,b);auto set=[&](unsigned sub){if(sub<t.states.size())t.states[sub]=v.Flags==D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY?-1:int64_t(v.Transition.StateAfter);};
   if(v.Transition.Subresource==D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)for(unsigned m=0;m<t.states.size();++m)set(m);else set(v.Transition.Subresource);}
 }}barrierOriginal(list,n,values);
}
using ExecuteFn=void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*,UINT,ID3D12CommandList*const*);inline ExecuteFn executeOriginal;
inline void STDMETHODCALLTYPE Execute(ID3D12CommandQueue* q,UINT n,ID3D12CommandList*const* commands){
 if(injecting||q!=queue){executeOriginal(q,n,commands);return;}
 std::lock_guard guard(mutex);executeOriginal(q,n,commands);
 for(UINT i=0;i<n;++i){auto it=lists.find(commands[i]);if(it==lists.end())continue;for(auto& [r,t]:it->second)for(unsigned m=0;m<t.states.size();++m)if(t.states[m]>=-1)t.bank->states[m]=t.states[m];}
}
inline uintptr_t Allocation(uintptr_t wrapper){auto alias=Read<uintptr_t>(wrapper+0x60);auto view=alias?alias:wrapper;auto backing=(Read<unsigned>(view+0x10)&0x10)?Read<uintptr_t>(view+0x30):view;return Read<uintptr_t>(backing);}
inline bool CachedState(uintptr_t state,uintptr_t allocation,unsigned sub,unsigned& value){
 if(Read<unsigned>(allocation+0x28)==1)sub=~0u;
 uint64_t key=(Read<uint64_t>(allocation+8)<<32)|sub;unsigned hash=unsigned((key>>24)+sub+(key>>16)+(key>>8))&255;
 auto node=Read<uintptr_t>(state+8+0x20+hash*8);
 for(unsigned i=0;node&&i<256;++i){if(Read<unsigned>(node-8)!=hash)break;if(Read<uint64_t>(node-0x30)==key){value=Read<unsigned>(node-0x18);return true;}node=Read<uintptr_t>(node);}return false;
}
inline void Observe(ID3D12GraphicsCommandList* list,uintptr_t state,unsigned semantic,uintptr_t allocation){
 auto resource=Read<ID3D12Resource*>(allocation+0x30);if(!resource)return;
 auto& b=banks[semantic];if(!b||b->source.Get()!=resource){
  auto fresh=std::make_shared<Bank>();if(FAILED(resource->QueryInterface(IID_PPV_ARGS(&fresh->source)))){++failed;return;}auto d=resource->GetDesc();
  // These five histories are single-plane 2D colour/depth-as-colour textures.
  if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||d.DepthOrArraySize!=1||d.SampleDesc.Count!=1||!d.MipLevels||d.MipLevels>16){++failed;return;}
  Com<ID3D12Device> device;if(FAILED(resource->GetDevice(IID_PPV_ARGS(&device)))){++failed;return;}
  D3D12_FEATURE_DATA_FORMAT_INFO format{d.Format};if(FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO,&format,sizeof(format)))||format.PlaneCount!=1){++failed;return;}
  fresh->desc=d;fresh->states.assign(d.MipLevels,-1);d.Flags=D3D12_RESOURCE_FLAG_NONE;D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
  for(auto& h:fresh->history)if(FAILED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&h)))){++failed;return;}
  b=std::move(fresh);watched[semantic]=resource;++discovered;
  fprintf(logFile,"DX12 water history discovered semantic=%u resource=%p size=%llux%u format=%u mips=%u\n",semantic,resource,b->desc.Width,b->desc.Height,b->desc.Format,b->desc.MipLevels);
 }
 b->seen=true;auto& tracked=Track(list,b);
 // Native state cache describes this point in this command list, including
 // explicit mip transitions; later barriers overwrite it before submission.
 auto all=Read<unsigned>(allocation+0x48);CachedState(state,allocation,~0u,all);
 for(unsigned m=0;m<b->desc.MipLevels;++m){auto value=all;CachedState(state,allocation,m,value);tracked.states[m]=value;}
}
inline void Before(ID3D12GraphicsCommandList* list,uintptr_t state,uint64_t hash){
 if(!Target(hash))return;std::lock_guard guard(mutex);if(!active)return;
 auto srv=[&](unsigned semantic,unsigned slot){auto wrapper=Read<uintptr_t>(state+0xb38+16*slot);if(wrapper)Observe(list,state,semantic,Allocation(wrapper));};
 if(hash==0xe275ecd626c81c62ull)srv(0,0);
 else if(hash==0x637fddd208eb09bdull){srv(2,1);srv(3,4);}
 else {srv(0,1);srv(1,18);auto group=Read<uintptr_t>(state+0x920);
  // Exact 719e6/7ab40: group+98 RT count, +50 allocation array,
  // +188 subresource array. Persistent second output, never scratch t0.
  if(group&&Read<unsigned>(group+0x98)>=2&&Read<unsigned>(group+0x98)<=8){auto sub=Read<unsigned>(group+0x18c);if(sub==0||sub==~0u)Observe(list,state,4,Read<uintptr_t>(group+0x58));else ++failed;}
 }
}
struct Slot {Com<ID3D12CommandAllocator> allocator;Com<ID3D12GraphicsCommandList> list;uint64_t completion{};std::vector<std::shared_ptr<Bank>> keep;};
inline std::array<Slot,16> slots;inline unsigned nextSlot{};inline Com<ID3D12Fence> fence;inline uint64_t serial{};inline HANDLE event{};inline bool poisoned{};
inline void H(HRESULT hr){if(FAILED(hr))throw hr;}
inline void Barrier(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,unsigned mip,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){if(from==to)return;D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,mip,from,to};list->ResourceBarrier(1,&b);}
inline void CopyBank(ID3D12GraphicsCommandList* list,Bank& b,unsigned eye,bool restore){
 auto history=b.history[eye].Get();for(unsigned mip=0;mip<b.desc.MipLevels;++mip){auto state=static_cast<D3D12_RESOURCE_STATES>(b.states[mip]);auto src=restore?history:b.source.Get(),dst=restore?b.source.Get():history;
  auto ss=restore?D3D12_RESOURCE_STATE_COMMON:state,ds=restore?state:D3D12_RESOURCE_STATE_COMMON;
  Barrier(list,src,mip,ss,D3D12_RESOURCE_STATE_COPY_SOURCE);Barrier(list,dst,mip,ds,D3D12_RESOURCE_STATE_COPY_DEST);
  D3D12_TEXTURE_COPY_LOCATION s{},d{};s.pResource=src;d.pResource=dst;s.Type=d.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;s.SubresourceIndex=d.SubresourceIndex=mip;list->CopyTextureRegion(&d,0,0,0,&s,nullptr);
  Barrier(list,src,mip,D3D12_RESOURCE_STATE_COPY_SOURCE,ss);Barrier(list,dst,mip,D3D12_RESOURCE_STATE_COPY_DEST,ds);
 }
}
inline void Transfer(bool restore){
 std::vector<std::shared_ptr<Bank>> selected;auto index=activeEye-1;
 for(auto& b:banks)if(b){bool use=restore?(b->valid[index]&&activeCounter-b->counter[index]==1):b->seen;
  if(!use){if(!restore)b->valid[index]=false;continue;}bool known=true;for(auto s:b->states)known=known&&s>=0;
  if(!known){b->valid[index]=false;++unknown;continue;}selected.push_back(b);}
 if(selected.empty()||!queue||poisoned)return;
 struct Injection {Injection(){injecting=true;}~Injection(){injecting=false;}} injection;
 try {
  Com<ID3D12Device> device;H(queue->GetDevice(IID_PPV_ARGS(&device)));
  if(!fence){H(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw HRESULT(E_FAIL);}
  auto& slot=slots[nextSlot];nextSlot=(nextSlot+1)%slots.size();auto done=fence->GetCompletedValue();if(done==~0ull)throw HRESULT(DXGI_ERROR_DEVICE_REMOVED);
  if(slot.completion>done){++waits;H(fence->SetEventOnCompletion(slot.completion,event));if(WaitForSingleObject(event,5000)!=WAIT_OBJECT_0)throw HRESULT(DXGI_ERROR_DEVICE_HUNG);}
  slot.keep.clear();
  if(!slot.allocator){H(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&slot.allocator)));H(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,slot.allocator.Get(),nullptr,IID_PPV_ARGS(&slot.list)));}
  else{H(slot.allocator->Reset());H(slot.list->Reset(slot.allocator.Get(),nullptr));}
  slot.keep=selected;for(auto& b:selected)CopyBank(slot.list.Get(),*b,index,restore);
  H(slot.list->Close());ID3D12CommandList* batch[]={slot.list.Get()};queue->ExecuteCommandLists(1,batch);H(queue->Signal(fence.Get(),++serial));slot.completion=serial;
  for(auto& b:selected)if(restore)++restored;else{b->valid[index]=true;b->counter[index]=activeCounter;++saved;}
 }catch(HRESULT hr){poisoned=true;Invalidate();++failed;fprintf(logFile,"DX12 water history disabled after GPU failure=%08lx\n",hr);}
}
inline void Begin(ID3D12CommandQueue* q,unsigned eye,unsigned nextEpoch,unsigned counter,bool temporal){
 std::lock_guard guard(mutex);if(active)Invalidate();active=false;
 if(!enabled||!temporal||eye<1||eye>2||!q||poisoned){Invalidate();return;}
 queue=q;if(epoch!=nextEpoch){Invalidate();epoch=nextEpoch;}
 active=true;activeEye=eye;activeCounter=counter;for(auto& b:banks)if(b)b->seen=false;Transfer(true);
}
inline void End(){std::lock_guard guard(mutex);if(!active)return;Transfer(false);active=false;}
inline void Toggle(){std::lock_guard guard(mutex);enabled=!enabled.load();Invalidate();fprintf(logFile,"Insert water/reflection history=%u; effective next eye\n",enabled.load());fflush(logFile);}
inline void Report(){fprintf(logFile,"DX12 water history enabled=%u discovered=%llu restored=%llu saved=%llu failed=%llu unknownStates=%llu reuseWaits=%llu\n",enabled.load(),discovered.load(),restored.load(),saved.load(),failed.load(),unknown.load(),waits.load());}
}
