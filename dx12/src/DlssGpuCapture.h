#pragma once
#include <mutex>
namespace DlssGpuCapture {
template<class T> using Com=Microsoft::WRL::ComPtr<T>;
struct Tag {Com<ID3D12Resource> resource;unsigned type{},state{};};
struct Pending {Com<ID3D12Resource> source,readback;D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes{};unsigned type{},eye{},mode{};uint64_t id{};};
inline std::mutex mutex;inline std::vector<Pending> pending;
inline bool armed{};inline unsigned captured{};inline uint64_t id{};
inline thread_local Tag tags[4];inline thread_local bool selected{};
inline void Arm(){std::lock_guard guard(mutex);if(!pending.empty()||armed)return;id=GetTickCount64();captured=0;armed=true;fprintf(logFile,"GPU capture armed id=%llu\n",id);}
inline void Tags(const void* data,unsigned count,unsigned eye){
 selected=false;for(auto& t:tags)t={};if(!eye||count!=5)return;
 {std::lock_guard guard(mutex);if(!armed||(captured&(1u<<eye))||(eye==2&&!(captured&2)))return;selected=true;}
 // Exact native 3a560 stack structs: ResourceTag stride40, resource+20,
 // semantic+28; Resource native+28 and D3D12 state+40 (hex offsets).
 for(unsigned i=0;i<count;++i){auto tag=reinterpret_cast<uintptr_t>(data)+i*0x40;auto type=Read<unsigned>(tag+0x28);unsigned slot=type==0?0:type==1?1:type==3?2:type==4?3:4;if(slot==4)continue;
 auto r=Read<uintptr_t>(tag+0x20);auto native=Read<ID3D12Resource*>(r+0x28);if(!native)continue;
 Com<ID3D12Resource> resource;if(FAILED(native->QueryInterface(IID_PPV_ARGS(&resource))))continue;
 tags[slot]={resource,type,Read<unsigned>(r+0x40)};
 }
}
inline void Copy(ID3D12GraphicsCommandList* list,const Tag& tag,unsigned eye,unsigned mode){
 if(!tag.resource)return;auto desc=tag.resource->GetDesc();if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||desc.SampleDesc.Count!=1||desc.DepthOrArraySize!=1)return;
 Pending item;item.source=tag.resource;item.eye=eye;item.mode=mode;item.type=tag.type;item.id=id;
 Com<ID3D12Device> device;if(FAILED(tag.resource->GetDevice(IID_PPV_ARGS(&device))))return;
 device->GetCopyableFootprints(&desc,0,1,0,&item.footprint,nullptr,nullptr,&item.bytes);if(!item.bytes||item.bytes>512ull*1024*1024)return;
 D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=item.bytes;buffer.Height=1;buffer.DepthOrArraySize=buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
 auto hr=device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&item.readback));if(FAILED(hr)){fprintf(logFile,"GPU capture allocation failed=%08lx\n",hr);return;}
 D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={tag.resource.Get(),0,static_cast<D3D12_RESOURCE_STATES>(tag.state),D3D12_RESOURCE_STATE_COPY_SOURCE};
 if(tag.state!=D3D12_RESOURCE_STATE_COPY_SOURCE)list->ResourceBarrier(1,&barrier);
 D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=tag.resource.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;src.SubresourceIndex=0;dst.pResource=item.readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=item.footprint;list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
 if(tag.state!=D3D12_RESOURCE_STATE_COPY_SOURCE){std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);list->ResourceBarrier(1,&barrier);}
 std::lock_guard guard(mutex);pending.push_back(std::move(item));
}
inline void Before(void* command,unsigned eye,unsigned mode){if(!selected||!command)return;auto list=static_cast<ID3D12GraphicsCommandList*>(command);for(unsigned i=0;i<3;++i)Copy(list,tags[i],eye,mode);}
inline void After(void* command,unsigned eye,unsigned mode){if(!selected||!command)return;Copy(static_cast<ID3D12GraphicsCommandList*>(command),tags[3],eye,mode);selected=false;for(auto& t:tags)t={};std::lock_guard guard(mutex);captured|=1u<<eye;if((captured&6)==6)armed=false;}
inline void Flush(ID3D12CommandQueue* queue){
 std::lock_guard guard(mutex);if(pending.empty()||!queue)return;
 // Called at native Present, after native command-list submission, before
 // eye transport. Capture only: wait for the submitted readbacks, not per frame.
 Com<ID3D12Device> device;Com<ID3D12Fence> fence;if(FAILED(queue->GetDevice(IID_PPV_ARGS(&device)))||FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))))return;
 if(FAILED(queue->Signal(fence.Get(),1)))return;auto event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)return;
 auto hr=fence->SetEventOnCompletion(1,event);bool ready=SUCCEEDED(hr)&&WaitForSingleObject(event,10000)==WAIT_OBJECT_0;CloseHandle(event);if(!ready)return;
 wchar_t folder[MAX_PATH];GetModuleFileNameW(self,folder,MAX_PATH);auto slash=wcsrchr(folder,L'\\');if(!slash)return;slash[1]=0;
 for(auto& item:pending){wchar_t path[MAX_PATH];swprintf_s(path,L"%sDL2VR-GPU-%llu-eye%u-mode%u-type%u.bin",folder,item.id,item.eye,item.mode,item.type);
 void* data{};D3D12_RANGE range{0,static_cast<SIZE_T>(item.bytes)};if(FAILED(item.readback->Map(0,&range,&data)))continue;
 FILE* file{};_wfopen_s(&file,path,L"wb");size_t written=0;if(file){written=fwrite(data,1,static_cast<size_t>(item.bytes),file);fclose(file);}D3D12_RANGE none{};item.readback->Unmap(0,&none);
 auto& fp=item.footprint;fprintf(logFile,"GPU capture saved id=%llu eye=%u mode=%u type=%u format=%u width=%u height=%u rowPitch=%u offset=%llu bytes=%llu written=%zu\n",item.id,item.eye,item.mode,item.type,fp.Footprint.Format,fp.Footprint.Width,fp.Footprint.Height,fp.Footprint.RowPitch,fp.Offset,item.bytes,written);
 }pending.clear();fflush(logFile);
}
}
