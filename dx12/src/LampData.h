#pragma once
// Opt-in, bounded GPU input capture. Never changes lighting inputs.
namespace LampData {
template<class T>using Com=Microsoft::WRL::ComPtr<T>;
inline std::atomic<bool> armed{};
inline std::mutex mutex;
inline ULONGLONG deadline{};
inline bool seen[2]{};
inline wchar_t folder[MAX_PATH]{};
inline Com<ID3D12Fence> fence;
inline uint64_t serial{};inline std::atomic<unsigned> bindings{},directCalls{},indirectCalls{},matched{};
struct Record {Com<ID3D12Resource> source,readback;Com<ID3D12Fence> completion;ID3D12CommandList* command{};UINT64 bytes{},ticket{};unsigned eye{},slot{};wchar_t path[MAX_PATH]{};};
inline std::vector<Record> pending;inline std::atomic<unsigned> pendingCount{};
inline bool Target(uint64_t h){return h==0x057c34d856e62863ull||h==0x80a8050763998abcull||h==0xf1997c8be6d320b0ull;}
inline void Arm(const wchar_t* path){std::lock_guard guard(mutex);if(!pending.empty()){fprintf(logFile,"Lamp GPU capture still pending; rearm skipped\n");return;}wcscpy_s(folder,path);seen[0]=seen[1]=false;bindings=directCalls=indirectCalls=matched=0;deadline=GetTickCount64()+10000;armed=true;fprintf(logFile,"Lamp GPU armed 10 seconds\n");}
inline void Capture(ID3D12GraphicsCommandList* list,uintptr_t state,uint64_t hash){
 if(!armed||!Target(hash))return;if(list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT&&list->GetType()!=D3D12_COMMAND_LIST_TYPE_COMPUTE){armed=false;fprintf(logFile,"Lamp GPU capture rejected unsupported queue list\n");return;}
 ++matched;std::lock_guard guard(mutex);auto eye=TemporalPort::renderEye.load();
 if(GetTickCount64()>deadline){armed=false;return;}if(eye<1||eye>2||seen[eye-1])return;seen[eye-1]=true;
 auto camera=Read<uint64_t>(state+0x3538);unsigned char constants[896]{};
 if(camera&&Read<uint64_t>(state+0x3618)==camera&&ShadowPort::Constants(camera,sizeof(constants),constants)){
  wchar_t path[MAX_PATH]{};swprintf_s(path,L"%s/eye%u-camera.bin",folder,eye);FILE* f{};if(!_wfopen_s(&f,path,L"wb")){fwrite(constants,1,sizeof(constants),f);fclose(f);}
 }else fprintf(logFile,"Lamp GPU camera unavailable eye=%u\n",eye);
 fprintf(logFile,"Lamp GPU capture eye=%u shader=%016llx counter=%u epoch=%u\n",eye,hash,TemporalPort::previousCounter,TemporalPort::epoch.load());
 for(unsigned slot:{11u,12u,13u}){
  // Structured-buffer views differ from texture wrappers: first qword is
  // the allocation, with no texture alias/flag indirection.
  auto view=Read<uintptr_t>(state+0xb38+16*(5*72+slot));auto allocation=Read<uintptr_t>(view);auto source=Read<ID3D12Resource*>(allocation+0x30);
  if(!source){fprintf(logFile,"Lamp GPU missing buffer eye=%u slot=%u\n",eye,slot);continue;}
  auto desc=source->GetDesc();if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER||!desc.Width||desc.Width>1024*1024){fprintf(logFile,"Lamp GPU rejected buffer eye=%u slot=%u\n",eye,slot);continue;}
  unsigned nativeState=Read<unsigned>(allocation+0x48);WaterHistory::CachedState(state,allocation,~0u,nativeState);
  if(!(nativeState&D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)){fprintf(logFile,"Lamp GPU unknown state eye=%u slot=%u state=%x\n",eye,slot,nativeState);continue;}
  Com<ID3D12Device> device;if(FAILED(source->GetDevice(IID_PPV_ARGS(&device))))continue;
  if(!fence&&FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))))continue;
  Record r;r.command=list;auto fenceHr=device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&r.completion));if(FAILED(fenceHr))continue;r.source=source;r.bytes=desc.Width;r.eye=eye;r.slot=slot;swprintf_s(r.path,L"%s/eye%u-light%u.bin",folder,eye,slot);
  D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;desc.Flags=D3D12_RESOURCE_FLAG_NONE;
  if(FAILED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&r.readback))))continue;
  auto original=static_cast<D3D12_RESOURCE_STATES>(nativeState);WaterHistory::Barrier(list,source,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,original,D3D12_RESOURCE_STATE_COPY_SOURCE);
  list->CopyBufferRegion(r.readback.Get(),0,source,0,r.bytes);
  WaterHistory::Barrier(list,source,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_SOURCE,original);
  fprintf(logFile,"Lamp GPU queued eye=%u slot=%u bytes=%llu source=%p state=%x\n",eye,slot,r.bytes,source,nativeState);pending.push_back(std::move(r));pendingCount=unsigned(pending.size());
 }
 if(seen[0]&&seen[1])armed=false;
}
inline void Submitted(ID3D12CommandQueue* q,UINT n,ID3D12CommandList*const* commands){if(!pendingCount.load(std::memory_order_relaxed))return;std::lock_guard guard(mutex);
 for(auto& r:pending)if(!r.ticket)for(UINT i=0;i<n;++i)if(r.command==commands[i]){if(SUCCEEDED(q->Signal(r.completion.Get(),1))){r.ticket=1;fprintf(logFile,"Lamp GPU submitted eye=%u slot=%u queueType=%u\n",r.eye,r.slot,q->GetDesc().Type);}break;}
}
inline void Present(){if(!armed.load(std::memory_order_relaxed)&&!pendingCount.load(std::memory_order_relaxed))return;std::lock_guard guard(mutex);if(armed&&GetTickCount64()>deadline){armed=false;fprintf(logFile,"Lamp GPU timeout bindings=%u direct=%u indirect=%u matched=%u seen=%u/%u\n",bindings.load(),directCalls.load(),indirectCalls.load(),matched.load(),seen[0],seen[1]);fflush(logFile);}
 for(auto it=pending.begin();it!=pending.end();){auto complete=it->completion->GetCompletedValue();if(!it->ticket||complete==~0ull||it->ticket>complete){++it;continue;}void* data{};D3D12_RANGE range{0,SIZE_T(it->bytes)};
  if(SUCCEEDED(it->readback->Map(0,&range,&data))){FILE* f{};if(!_wfopen_s(&f,it->path,L"wb")){auto written=fwrite(data,1,size_t(it->bytes),f);fclose(f);fprintf(logFile,"Lamp GPU saved eye=%u slot=%u bytes=%zu\n",it->eye,it->slot,written);}D3D12_RANGE none{};it->readback->Unmap(0,&none);}it=pending.erase(it);pendingCount=unsigned(pending.size());
 }
}
}


