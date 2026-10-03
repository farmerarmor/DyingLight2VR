#pragma once
#include <mutex>
#include <unordered_map>
namespace ShadowBindings {
inline std::mutex mutex;inline std::unordered_map<void*,uint64_t> shaders;
inline std::atomic<bool> armed{false};inline uint64_t seen[12]{};
inline bool Target(uint64_t h){return h==0xb248b0205e047a2bull||h==0x4ce5d78b6c06bdfbull||h==0xc1344be4b3f10694ull||h==0x0678cfb133ec65d1ull||h==0x0e907ded7c1d61eeull||h==0x4322e80586869a35ull;}
inline void Register(void* shader,uint64_t hash,unsigned stage){if(stage!=0)return;std::lock_guard guard(mutex);if(Target(hash))shaders[shader]=hash;else shaders.erase(shader);}
inline void Arm(){std::lock_guard guard(mutex);memset(seen,0,sizeof(seen));armed=true;}
// The native material uploader (83f11..83f41) resolves CB addresses through
// this double-buffered arena. Validate actual resource ranges before any port
// uses a GPU address as a copy source. This trace runs at most once per variant.
inline void TraceConstants(uintptr_t state,unsigned eye,uint64_t hash){
 auto holder=Read<uintptr_t>(backend+0x164a48);auto arena=Read<uintptr_t>(holder+8);
 auto frame=Read<unsigned>(backend+0x1646f0);auto indexed=Read<unsigned char>(arena+0x50);
 fprintf(logFile,"Shadow DX12 constants eye=%u hash=%016llx arena=%p frame=%u indexed=%u\n",eye,hash,reinterpret_cast<void*>(arena),frame,indexed);
 struct Constant {const char* name;unsigned offset,root,minimum;};
 for(auto c:{Constant{"pixelMaterial",0x3508,6,16},Constant{"vertexMaterial",0x3510,10,64},Constant{"camera_b2",0x3538,1,528}}){
  auto gpu=Read<uint64_t>(state+c.offset);auto bound=Read<uint64_t>(state+0x3548+c.root*8);bool matched=false;
  for(unsigned i=0;i<2;++i){auto allocation=Read<uintptr_t>(arena+i*40);auto resource=Read<ID3D12Resource*>(allocation+0x30);auto base=Read<uint64_t>(allocation+0x40);auto cpu=Read<uintptr_t>(arena+i*40+8);
   if(!resource||!gpu||gpu<base||gpu-base>64ull*1024*1024)continue;
   Microsoft::WRL::ComPtr<ID3D12Resource> retained;if(FAILED(resource->QueryInterface(IID_PPV_ARGS(&retained))))continue;auto desc=resource->GetDesc();auto actual=resource->GetGPUVirtualAddress();D3D12_HEAP_PROPERTIES heap{};D3D12_HEAP_FLAGS flags{};auto hr=resource->GetHeapProperties(&heap,&flags);
   bool valid=desc.Dimension==D3D12_RESOURCE_DIMENSION_BUFFER&&actual==base&&gpu-base<=desc.Width&&c.minimum<=desc.Width-(gpu-base);
   fprintf(logFile,"Shadow DX12 CB %s eye=%u gpu=%llx root=%u bound=%llx slot=%u resource=%p base=%llx actual=%llx bytes=%llu offset=%llu state=%x heapHr=%08lx heap=%u cpu=%p valid=%u\n",c.name,eye,gpu,c.root,bound,i,resource,base,actual,desc.Width,gpu-base,Read<unsigned>(allocation+0x48),hr,heap.Type,reinterpret_cast<void*>(cpu),valid);
   matched|=valid;
  }
  if(!matched)fprintf(logFile,"Shadow DX12 CB UNRESOLVED %s eye=%u gpu=%llx root=%u bound=%llx\n",c.name,eye,gpu,c.root,bound);
 }
}
inline void Trace(void* state,void* descriptor,void* packet){
 if(!armed.load())return;auto eye=TemporalPort::renderEye.load();if(!eye)return;
 constexpr uintptr_t mask=0xffffffffffffull;
 auto material=Read<uintptr_t>(reinterpret_cast<uintptr_t>(packet));auto owner=Read<uintptr_t>(material+0x30);
 auto table=Read<uintptr_t>(owner+0x168)&mask;auto index=Read<unsigned short>(reinterpret_cast<uintptr_t>(descriptor)+2);auto shader=Read<void*>(table+index*8);
 std::lock_guard guard(mutex);auto it=shaders.find(shader);if(it==shaders.end())return;auto hash=it->second,marker=hash^eye;
 for(auto v:seen)if(v==marker)return;bool recorded=false;for(auto& v:seen)if(!v){v=marker;recorded=true;break;}if(!recorded){armed=false;return;}
 auto native=reinterpret_cast<uintptr_t>(state);TraceConstants(native,eye,hash);
 // 8a440 stores texture wrapper at b38 + 16*(stage*72+slot).
 auto depth=Read<uintptr_t>(native+0xb48);auto alias=Read<uintptr_t>(depth+0x60);auto resolved=alias?alias:depth;
 auto flags=Read<unsigned>(resolved+0x10);auto backing=(flags&0x10)?Read<uintptr_t>(resolved+0x30):resolved;auto allocation=Read<uintptr_t>(backing);
 fprintf(logFile,"DX12 shadow binding eye=%u shader=%016llx state=%p depth=%p resolved=%p allocation=%p descriptor=%llx materialGpu=%llx cameraGpu=%llx\n",eye,hash,state,reinterpret_cast<void*>(depth),reinterpret_cast<void*>(resolved),reinterpret_cast<void*>(allocation),Read<uint64_t>(native+0x2640),Read<uint64_t>(native+0x3508),Read<uint64_t>(native+0x3538));
 for(unsigned off=0;off<0x50;off+=8)fprintf(logFile,"DX12 shadow allocation offset=%x value=%016llx\n",off,Read<uint64_t>(allocation+off));
 fflush(logFile);
}
}
