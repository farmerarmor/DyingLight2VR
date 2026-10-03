#pragma once
#include "ShadowDx12Code.h"
#include <unordered_map>
namespace ShadowPort {
template<class T>using Com=Microsoft::WRL::ComPtr<T>;
inline std::atomic<bool> enabled{false};inline std::atomic<uint64_t> created{},captured{},applied{},missing{},failed{},waits{};
inline std::mutex mutex;
inline unsigned Index(uint64_t h){return (h==0xb248b0205e047a2bull||h==0x0678cfb133ec65d1ull)?0:(h==0x4ce5d78b6c06bdfbull||h==0x0e907ded7c1d61eeull)?1:(h==0xc1344be4b3f10694ull||h==0x4322e80586869a35ull)?2:3;}
inline bool Target(uint64_t h){return Index(h)<3;}
struct Pipeline {Com<ID3D12PipelineState> original,corrected;unsigned materialBytes{};uint64_t hash{};};
inline std::unordered_map<ID3D12PipelineState*,Pipeline> pipelines;
using CreateFn=HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*,const D3D12_GRAPHICS_PIPELINE_STATE_DESC*,REFIID,void**);inline CreateFn createOriginal;
inline std::atomic<uint64_t> factoryCalls{},factoryTargets{};
inline void RegisterPipeline(ID3D12Device* device,const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc,IUnknown* native){
 if(!native||!desc||!desc->PS.pShaderBytecode||desc->PS.BytecodeLength>1024*1024)return;
 auto hash=ShaderHash(desc->PS.pShaderBytecode,unsigned(desc->PS.BytecodeLength));if(!Target(hash))return;
 Pipeline p;p.hash=hash;if(FAILED(native->QueryInterface(IID_PPV_ARGS(&p.original))))return;
 std::lock_guard guard(mutex);if(pipelines.find(p.original.Get())!=pipelines.end())return;
 std::vector<unsigned char> shader;if(!ShadowDx12Code::Patch(desc->PS.pShaderBytecode,unsigned(desc->PS.BytecodeLength),shader,p.materialBytes)){++failed;return;}
 auto copy=*desc;copy.PS={shader.data(),shader.size()};copy.CachedPSO={};
 auto hr=createOriginal(device,&copy,IID_PPV_ARGS(&p.corrected));if(FAILED(hr)){++failed;fprintf(logFile,"Shadow DX12 replacement pipeline failed hash=%016llx hr=%08lx\n",hash,hr);return;}
 pipelines[p.original.Get()]=std::move(p);++created;
}
inline HRESULT STDMETHODCALLTYPE Create(ID3D12Device* device,const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc,REFIID iid,void** output){auto result=createOriginal(device,desc,iid,output);if(SUCCEEDED(result)&&output&&*output)RegisterPipeline(device,desc,static_cast<IUnknown*>(*output));return result;}
// Exact native factory 5ef10: graphics desc in r8, compute desc in rdx,
// returned 16-byte cache entry starts with ID3D12PipelineState*. This includes
// both native cache hits and PipelineLibrary::LoadGraphicsPipeline successes.
using FactoryFn=uintptr_t(*)(void*,const void*,const D3D12_GRAPHICS_PIPELINE_STATE_DESC*,const void*,void*);inline FactoryFn factoryOriginal;
inline uintptr_t Factory(void* manager,const void* compute,const D3D12_GRAPHICS_PIPELINE_STATE_DESC* graphics,const void* metadata,void* extra){
 auto result=factoryOriginal(manager,compute,graphics,metadata,extra);++factoryCalls;
 if(compute||!graphics||!result||!graphics->PS.pShaderBytecode)return result;
 auto hash=ShaderHash(graphics->PS.pShaderBytecode,unsigned(graphics->PS.BytecodeLength));if(!Target(hash))return result;++factoryTargets;
 auto native=Read<ID3D12PipelineState*>(result);if(!native)return result;Com<ID3D12Device> device;if(SUCCEEDED(native->GetDevice(IID_PPV_ARGS(&device))))RegisterPipeline(device.Get(),graphics,native);return result;
}
struct Snapshot {Com<ID3D12Resource> depth,scratch;D3D12_RESOURCE_DESC desc{};unsigned epoch{},counter{};bool valid{};unsigned char camera[528]{},material[64]{};};inline Snapshot snapshots[3];
struct Slot {Com<ID3D12Resource> upload;unsigned char* cpu{};unsigned used{};uint64_t completion{};std::vector<Com<ID3D12Resource>> keep;};inline Slot slots[16];inline unsigned slotIndex{},epoch{},counter{};inline bool pairActive{};inline std::atomic<bool> pairEnabled{};inline Com<ID3D12Fence> fence;inline uint64_t serial{};inline HANDLE event{};inline ID3D12CommandQueue* queue{};
inline void H(HRESULT hr){if(FAILED(hr))throw hr;}
inline void Begin(ID3D12CommandQueue* q,unsigned e,unsigned c,unsigned ep){if(e!=1){if(!e)pairEnabled=false;return;}std::lock_guard guard(mutex);pairEnabled=false;pairActive=false;for(auto& s:snapshots)s.valid=false;if(!enabled||!q||!created.load())return;queue=q;epoch=ep;counter=c;
 try{Com<ID3D12Device> device;H(q->GetDevice(IID_PPV_ARGS(&device)));if(!fence){H(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw HRESULT(E_FAIL);}
 slotIndex=(slotIndex+1)%16;auto& slot=slots[slotIndex];auto complete=fence->GetCompletedValue();if(complete==~0ull)throw HRESULT(DXGI_ERROR_DEVICE_REMOVED);if(slot.completion>complete){++waits;H(fence->SetEventOnCompletion(slot.completion,event));if(WaitForSingleObject(event,5000)!=WAIT_OBJECT_0)throw HRESULT(DXGI_ERROR_DEVICE_HUNG);}
 slot.keep.clear();slot.used=0;if(!slot.upload){D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=65536;d.Height=1;d.MipLevels=d.DepthOrArraySize=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_UPLOAD;H(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&slot.upload)));D3D12_RANGE noRead{};H(slot.upload->Map(0,&noRead,reinterpret_cast<void**>(&slot.cpu)));}pairActive=true;pairEnabled=true;
 }catch(HRESULT hr){++failed;fprintf(logFile,"Shadow DX12 frame resources failed=%08lx\n",hr);}
}
inline void Present(){std::lock_guard guard(mutex);if(!pairActive||!queue||!fence)return;auto hr=queue->Signal(fence.Get(),++serial);if(SUCCEEDED(hr))slots[slotIndex].completion=serial;else{enabled=false;pairEnabled=false;++failed;} }
// Read only upload-backed native constants, after material/vertex binding has
// finished. Resource size, heap and GPU address are validated for every source.
inline bool Constants(uint64_t gpu,unsigned bytes,void* output){if(!gpu)return false;auto arena=Read<uintptr_t>(Read<uintptr_t>(backend+0x164a48)+8);
 for(unsigned i=0;i<2;++i){auto allocation=Read<uintptr_t>(arena+i*40);auto base=Read<uint64_t>(allocation+0x40);auto cpu=Read<uintptr_t>(arena+i*40+8);if(!base||!cpu||gpu<base||gpu-base>64ull*1024*1024)continue;auto r=Read<ID3D12Resource*>(allocation+0x30);if(!r)continue;Com<ID3D12Resource> resource;if(FAILED(r->QueryInterface(IID_PPV_ARGS(&resource))))continue;auto d=resource->GetDesc();D3D12_HEAP_PROPERTIES hp{};D3D12_HEAP_FLAGS flags{};
 if(d.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER||gpu-base>d.Width||bytes>d.Width-(gpu-base)||r->GetGPUVirtualAddress()!=base||FAILED(r->GetHeapProperties(&hp,&flags))||hp.Type!=D3D12_HEAP_TYPE_UPLOAD)continue;
 SIZE_T got{};if(ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(cpu+(gpu-base)),output,bytes,&got)&&got==bytes)return true;
 }return false;
}
inline void TextureCopy(ID3D12GraphicsCommandList* list,ID3D12Resource* dst,D3D12_RESOURCE_STATES ds,ID3D12Resource* src,D3D12_RESOURCE_STATES ss){WaterHistory::Barrier(list,dst,0,ds,D3D12_RESOURCE_STATE_COPY_DEST);WaterHistory::Barrier(list,src,0,ss,D3D12_RESOURCE_STATE_COPY_SOURCE);D3D12_TEXTURE_COPY_LOCATION d{},s{};d.pResource=dst;s.pResource=src;d.Type=s.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&d,0,0,0,&s,nullptr);WaterHistory::Barrier(list,src,0,D3D12_RESOURCE_STATE_COPY_SOURCE,ss);WaterHistory::Barrier(list,dst,0,D3D12_RESOURCE_STATE_COPY_DEST,ds);}
inline bool Match(const D3D12_RESOURCE_DESC& a,const D3D12_RESOURCE_DESC& b){return a.Width==b.Width&&a.Height==b.Height&&a.Format==b.Format;}
struct Scope {
 std::unique_lock<std::mutex> lock;ID3D12GraphicsCommandList* list{};Snapshot* snapshot{};Com<ID3D12Resource> nativeDepth;D3D12_RESOURCE_STATES depthState{};ID3D12PipelineState* original{};uint64_t originalMaterial{};
 Scope(ID3D12GraphicsCommandList* l,uintptr_t state,uint64_t hash,ID3D12PipelineState* pso):lock(mutex,std::defer_lock){auto index=Index(hash);auto eye=TemporalPort::renderEye.load();if(index==3||eye<1||eye>2||!pairEnabled)return;lock.lock();if(!pairEnabled||!pairActive||TemporalPort::previousCounter!=counter||TemporalPort::epoch.load()!=epoch)return;
  auto it=pipelines.find(pso);if(it==pipelines.end()||it->second.hash!=hash){++missing;return;}auto& pipeline=it->second;
  auto wrapper=Read<uintptr_t>(state+0xb48);auto allocation=WaterHistory::Allocation(wrapper);auto native=Read<ID3D12Resource*>(allocation+0x30);auto alias=Read<uintptr_t>(wrapper+0x60);auto sub=Read<unsigned>((alias?alias:wrapper)+0x28);if(!native||(sub!=0&&sub!=~0u)||FAILED(native->QueryInterface(IID_PPV_ARGS(&nativeDepth)))){++missing;return;}
  auto desc=nativeDepth->GetDesc();if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||desc.MipLevels!=1||desc.DepthOrArraySize!=1||desc.SampleDesc.Count!=1){++missing;return;}
  auto value=Read<unsigned>(allocation+0x48);WaterHistory::CachedState(state,allocation,~0u,value);WaterHistory::CachedState(state,allocation,0,value);if(!(value&D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)){++missing;return;}depthState=static_cast<D3D12_RESOURCE_STATES>(value);
  auto& s=snapshots[index];auto& slot=slots[slotIndex];
  if(eye==1){if(s.valid)return;unsigned char camera[528],material[64];auto cameraGpu=Read<uint64_t>(state+0x3538);if(!cameraGpu||Read<uint64_t>(state+0x3550)!=cameraGpu||!Constants(cameraGpu,sizeof(camera),camera)||!Constants(Read<uint64_t>(state+0x3510),sizeof(material),material)){++missing;return;}
   if(!s.depth||!Match(s.desc,desc)){Com<ID3D12Device> device;if(FAILED(nativeDepth->GetDevice(IID_PPV_ARGS(&device)))){++failed;return;}auto d=desc;d.Flags=D3D12_RESOURCE_FLAG_NONE;D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;Com<ID3D12Resource> depth,scratch;if(FAILED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&depth)))||FAILED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&scratch)))){++failed;return;}s.depth=depth;s.scratch=scratch;s.desc=desc;}
   slot.keep.push_back(s.depth);slot.keep.push_back(s.scratch);slot.keep.push_back(nativeDepth);TextureCopy(l,s.depth.Get(),D3D12_RESOURCE_STATE_COMMON,nativeDepth.Get(),depthState);memcpy(s.camera,camera,sizeof(camera));memcpy(s.material,material,sizeof(material));s.epoch=epoch;s.counter=counter;s.valid=true;++captured;return;
  }
  if(!s.valid||s.counter!=counter||s.epoch!=epoch||!Match(s.desc,desc)||slot.used+2048>65536){++missing;return;}
  auto current=Read<uint64_t>(state+0x3508);if(!current||Read<uint64_t>(state+0x3578)!=current){++missing;return;}unsigned char constants[2048]{};if(!Constants(current,pipeline.materialBytes,constants)){++missing;return;}memcpy(constants+64*16,s.camera,528);memcpy(constants+97*16,s.material,64);auto offset=slot.used;slot.used+=2048;memcpy(slot.cpu+offset,constants,sizeof(constants));slot.keep.push_back(nativeDepth);
  TextureCopy(l,s.scratch.Get(),D3D12_RESOURCE_STATE_COMMON,nativeDepth.Get(),depthState);TextureCopy(l,nativeDepth.Get(),depthState,s.depth.Get(),D3D12_RESOURCE_STATE_COMMON);
  l->SetGraphicsRootConstantBufferView(6,slot.upload->GetGPUVirtualAddress()+offset);l->SetPipelineState(pipeline.corrected.Get());list=l;snapshot=&s;original=pso;originalMaterial=current;++applied;
 }
 ~Scope(){if(!list)return;list->SetPipelineState(original);list->SetGraphicsRootConstantBufferView(6,originalMaterial);TextureCopy(list,nativeDepth.Get(),depthState,snapshot->scratch.Get(),D3D12_RESOURCE_STATE_COMMON);}
};
inline void Toggle(){enabled=!enabled.load();fprintf(logFile,"F9 shared flashlight shadows=%u (next pair)\n",enabled.load());fflush(logFile);}
inline void Report(){fprintf(logFile,"DX12 shared shadows enabled=%u pipelines=%llu captured=%llu applied=%llu missing=%llu failed=%llu reuseWaits=%llu factoryCalls=%llu factoryTargets=%llu\n",enabled.load(),created.load(),captured.load(),applied.load(),missing.load(),failed.load(),waits.load(),factoryCalls.load(),factoryTargets.load());}
}
