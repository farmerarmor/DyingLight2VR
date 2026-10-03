#pragma once
namespace NativeTaaHistory {
template<class T> using Com=Microsoft::WRL::ComPtr<T>;
inline std::mutex mutex;
inline std::atomic<uint64_t> applied{},saved{},failed{};
inline std::atomic<bool> enabled{true};
struct Entry {Com<ID3D12Resource> history,scratch;D3D12_RESOURCE_DESC desc{};unsigned epoch{},counter{};bool valid{};};
inline Entry entries[2][3];
struct PreviousDraw {unsigned eye{},epoch{},counter{};ID3D12Resource* colour{};};
inline PreviousDraw previousDraw;
inline std::atomic<uint64_t> colourSaved{},complete{},warmup{};
inline void Invalidate(){for(auto& eye:entries)for(auto& e:eye)e.valid=false;previousDraw={};}
struct Retired {Com<ID3D12Resource> resource;uint64_t fence{};};
inline std::vector<Retired> retired;
inline Com<ID3D12Fence> fence;inline uint64_t tick{};
inline void Retire(Com<ID3D12Resource>& resource){if(resource)retired.push_back({std::move(resource),0});}
inline void Present(ID3D12CommandQueue* queue){
 std::lock_guard guard(mutex);if(retired.empty()||!queue)return;
 if(!fence){Com<ID3D12Device> device;if(FAILED(queue->GetDevice(IID_PPV_ARGS(&device)))||FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))))return;}
 if(FAILED(queue->Signal(fence.Get(),++tick)))return;
 for(auto& r:retired)if(!r.fence)r.fence=tick;
 auto complete=fence->GetCompletedValue();if(complete==~0ull)return;
 retired.erase(std::remove_if(retired.begin(),retired.end(),[&](const Retired& r){return r.fence<=complete;}),retired.end());
}
inline void Barrier(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){if(from==to)return;D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,0,from,to};list->ResourceBarrier(1,&b);}
inline void Copy(ID3D12GraphicsCommandList* list,ID3D12Resource* dst,D3D12_RESOURCE_STATES ds,ID3D12Resource* src,D3D12_RESOURCE_STATES ss){
 Barrier(list,dst,ds,D3D12_RESOURCE_STATE_COPY_DEST);Barrier(list,src,ss,D3D12_RESOURCE_STATE_COPY_SOURCE);
 D3D12_TEXTURE_COPY_LOCATION d{},s{};d.pResource=dst;s.pResource=src;d.Type=s.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
 list->CopyTextureRegion(&d,0,0,0,&s,nullptr);Barrier(list,src,D3D12_RESOURCE_STATE_COPY_SOURCE,ss);Barrier(list,dst,D3D12_RESOURCE_STATE_COPY_DEST,ds);
}
inline bool Match(const D3D12_RESOURCE_DESC& a,const D3D12_RESOURCE_DESC& b){return a.Width==b.Width&&a.Height==b.Height&&a.Format==b.Format&&a.MipLevels==b.MipLevels&&a.DepthOrArraySize==b.DepthOrArraySize&&a.SampleDesc.Count==b.SampleDesc.Count;}
inline bool Prepare(Entry& e,ID3D12Resource* current){auto d=current->GetDesc();
 if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||d.MipLevels<1||d.DepthOrArraySize!=1||d.SampleDesc.Count!=1)return false;
 d.MipLevels=1; // The exact AA shader samples colour history only at LOD zero.
 if(e.history&&Match(d,e.desc))return true;
 Retire(e.history);Retire(e.scratch);e.valid=false;e.desc=d;
 d.Flags=D3D12_RESOURCE_FLAG_NONE;D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;Com<ID3D12Device> device;if(FAILED(current->GetDevice(IID_PPV_ARGS(&device))))return false;
 if(FAILED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&e.history)))||FAILED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&e.scratch)))){Retire(e.history);Retire(e.scratch);return false;}return true;
}
struct Input {Com<ID3D12Resource> resource;D3D12_RESOURCE_STATES state{};};
inline Input Get(uintptr_t state,unsigned slot){Input result;
 auto wrapper=Read<uintptr_t>(state+0xb38+16*slot);auto alias=Read<uintptr_t>(wrapper+0x60);auto view=alias?alias:wrapper;
 auto backing=(Read<unsigned>(view+0x10)&0x10)?Read<uintptr_t>(view+0x30):view;auto allocation=Read<uintptr_t>(backing);auto native=Read<ID3D12Resource*>(allocation+0x30);auto sub=Read<unsigned>(view+0x28);
 auto current=NativeTaaTrace::State(state,allocation,sub);if(!native||(sub!=0&&sub!=~0u)||(current&D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)==0)return result;
 if(FAILED(native->QueryInterface(IID_PPV_ARGS(&result.resource))))return {};
 result.state=static_cast<D3D12_RESOURCE_STATES>(current);return result;
}
// At the next AA draw, native t6 contains the last rendered eye's filtered
// history. Bank that image before temporarily replacing all three histories.
// Never feed same-eye depth/motion together with opposite-eye colour.
struct Scope {
 std::unique_lock<std::mutex> guard;ID3D12GraphicsCommandList* list{};Input previous[3];Entry* banks[3]{};bool replaced{};
 Scope(ID3D12GraphicsCommandList* command,uintptr_t state):guard(mutex,std::defer_lock){
  auto eye=TemporalPort::renderEye.load();if(!enabled||!TemporalPort::latched.load()||eye<1||eye>2||!state)return;
  guard.lock();auto epoch=TemporalPort::epoch.load(),counter=TemporalPort::previousCounter;
  Input current[2]={Get(state,1),Get(state,3)};
  previous[0]=Get(state,2);previous[1]=Get(state,4);previous[2]=Get(state,6);
  bool ready=previous[2].resource!=nullptr;
  for(unsigned k=0;k<2;++k)ready=ready&&current[k].resource&&previous[k].resource&&current[k].resource!=previous[k].resource&&Match(current[k].resource->GetDesc(),previous[k].resource->GetDesc());
  if(!ready){Invalidate();++failed;return;}
  // A colour texture change or discontinuous eye sequence must not relabel an
  // unrelated image as a completed eye history. Require the observed 1,2 order.
  bool consecutive=previousDraw.epoch==epoch&&previousDraw.colour==previous[2].resource.Get()&&
   ((eye==2&&previousDraw.eye==1&&previousDraw.counter==counter)||
    (eye==1&&previousDraw.eye==2&&counter-previousDraw.counter==1));
  if(!consecutive)Invalidate();
  for(unsigned k=0;k<3;++k){banks[k]=&entries[eye-1][k];if(!Prepare(*banks[k],k<2?current[k].resource.Get():previous[2].resource.Get())){Invalidate();++failed;return;}}
  if(consecutive&&!Prepare(entries[previousDraw.eye-1][2],previous[2].resource.Get())){Invalidate();++failed;return;}
  list=command;NativeTaaTrace::capturing=true;
  if(consecutive){auto& colour=entries[previousDraw.eye-1][2];Copy(list,colour.history.Get(),D3D12_RESOURCE_STATE_COMMON,previous[2].resource.Get(),previous[2].state);colour.valid=true;colour.epoch=epoch;colour.counter=previousDraw.counter;++colourSaved;}
  bool valid=true;for(auto bank:banks)valid=valid&&bank->valid&&bank->epoch==epoch&&counter-bank->counter==1;
  if(valid){for(unsigned k=0;k<3;++k){Copy(list,banks[k]->scratch.Get(),D3D12_RESOURCE_STATE_COMMON,previous[k].resource.Get(),previous[k].state);Copy(list,previous[k].resource.Get(),previous[k].state,banks[k]->history.Get(),D3D12_RESOURCE_STATE_COMMON);++applied;}replaced=true;++complete;}else ++warmup;
  for(unsigned k=0;k<2;++k){auto& e=*banks[k];Copy(list,e.history.Get(),D3D12_RESOURCE_STATE_COMMON,current[k].resource.Get(),current[k].state);e.valid=true;e.epoch=epoch;e.counter=counter;++saved;}
  previousDraw={eye,epoch,counter,previous[2].resource.Get()};NativeTaaTrace::capturing=false;
 }
 ~Scope(){if(!list||!replaced)return;NativeTaaTrace::capturing=true;for(unsigned k=0;k<3;++k)Copy(list,previous[k].resource.Get(),previous[k].state,banks[k]->scratch.Get(),D3D12_RESOURCE_STATE_COMMON);NativeTaaTrace::capturing=false;}
};
inline void Report(){fprintf(logFile,"Native AA complete history enabled=%u applied=%llu saved=%llu failed=%llu colourSaved=%llu complete=%llu warmup=%llu\n",enabled.load(),applied.load(),saved.load(),failed.load(),colourSaved.load(),complete.load(),warmup.load());}
}
