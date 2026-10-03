#pragma once
namespace NativeTaaTrace {
inline std::mutex mutex;
inline unsigned mask{},copies{},remaining{};inline bool armed{};
inline uint64_t id{};
inline ID3D12Resource* observed[7]{};
inline ULONGLONG deadline{};
inline bool CaptureView(unsigned sub,const D3D12_RESOURCE_DESC& desc){return sub==0||(sub==~0u&&desc.MipLevels>=1&&desc.DepthOrArraySize==1);}
inline void Arm(){std::lock_guard guard(mutex);id=GetTickCount64();deadline=id+15000;mask=0;copies=0;remaining=8;memset(observed,0,sizeof(observed));armed=true;fprintf(logFile,"Native TAA trace armed id=%llu\n",id);}
inline unsigned State(uintptr_t state,uintptr_t allocation,unsigned sub){
 // Read the native transition table used by 7e200; never mutate it.
 if(Read<unsigned>(allocation+0x28)==1)sub=~0u;
 uint64_t key=(Read<uint64_t>(allocation+8)<<32)|sub;
 unsigned hash=unsigned((key>>24)+sub+(key>>16)+(key>>8))&255;
 auto node=Read<uintptr_t>(state+8+0x20+hash*8);
 for(unsigned i=0;node&&i<256;++i){if(Read<unsigned>(node-8)!=hash)break;if(Read<uint64_t>(node-0x30)==key)return Read<unsigned>(node-0x18);node=Read<uintptr_t>(node);}
 return Read<unsigned>(allocation+0x48);
}
inline thread_local bool capturing{};
inline void Before(ID3D12GraphicsCommandList* list,uintptr_t state){
 auto eye=TemporalPort::renderEye.load();if(eye<1||eye>2)return;
 std::lock_guard guard(mutex);if(!armed||!remaining)return;
 --remaining;fprintf(logFile,"Native TAA draw id=%llu eye=%u F12=%u list=%p state=%p\n",id,eye,TemporalPort::latched.load(),list,reinterpret_cast<void*>(state));
 bool capture=(mask&(1u<<eye))==0;
 if(capture){std::lock_guard gpuGuard(DlssGpuCapture::mutex);if(!DlssGpuCapture::pending.empty())capture=false;else DlssGpuCapture::id=id;}
 for(unsigned slot=0;slot<7;++slot){
  auto wrapper=Read<uintptr_t>(state+0xb38+16*slot);if(!wrapper)continue;
  auto alias=Read<uintptr_t>(wrapper+0x60);auto view=alias?alias:wrapper;
  auto backing=(Read<unsigned>(view+0x10)&0x10)?Read<uintptr_t>(view+0x30):view;
  auto allocation=Read<uintptr_t>(backing);auto resource=Read<ID3D12Resource*>(allocation+0x30);auto sub=Read<unsigned>(view+0x28);auto current=State(state,allocation,sub);
  fprintf(logFile,"Native TAA input id=%llu eye=%u slot=%u wrapper=%p resource=%p sub=%u state=%x\n",id,eye,slot,reinterpret_cast<void*>(wrapper),resource,sub,current);
  observed[slot]=resource;
  if(capture&&resource&&(current&D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)){
   Microsoft::WRL::ComPtr<ID3D12Resource> retained;if(SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(&retained)))&&CaptureView(sub,retained->GetDesc())){capturing=true;DlssGpuCapture::Copy(list,{retained,20+slot,current},eye,TemporalPort::latched.load());capturing=false;}
  }
 }
 if(capture)mask|=1u<<eye;
 if((mask&6)==6){std::lock_guard gpuGuard(DlssGpuCapture::mutex);DlssGpuCapture::armed=false;}
 fflush(logFile);
}
using CopyFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12Resource*,ID3D12Resource*);inline CopyFn copyOriginal;
using RegionFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,const D3D12_TEXTURE_COPY_LOCATION*,UINT,UINT,UINT,const D3D12_TEXTURE_COPY_LOCATION*,const D3D12_BOX*);inline RegionFn regionOriginal;
inline void Record(ID3D12GraphicsCommandList* list,ID3D12Resource* dst,ID3D12Resource* src,unsigned kind){
 if(capturing)return;auto eye=TemporalPort::renderEye.load();if(!eye)return;
 std::lock_guard guard(mutex);if(!armed||copies>=512)return;
 if(GetTickCount64()>deadline){armed=false;return;}
 bool relevant=false;for(auto resource:observed)if(resource&&(resource==src||resource==dst)){relevant=true;break;}if(!relevant)return;
 ++copies;fprintf(logFile,"Native TAA copy id=%llu eye=%u list=%p kind=%u source=%p destination=%p\n",id,eye,list,kind,src,dst);
 if(copies==512)armed=false;
}
inline void STDMETHODCALLTYPE Copy(ID3D12GraphicsCommandList* list,ID3D12Resource* dst,ID3D12Resource* src){Record(list,dst,src,0);copyOriginal(list,dst,src);}
inline void STDMETHODCALLTYPE Region(ID3D12GraphicsCommandList* list,const D3D12_TEXTURE_COPY_LOCATION* dst,UINT x,UINT y,UINT z,const D3D12_TEXTURE_COPY_LOCATION* src,const D3D12_BOX* box){Record(list,dst->pResource,src->pResource,1);regionOriginal(list,dst,x,y,z,src,box);}
}
