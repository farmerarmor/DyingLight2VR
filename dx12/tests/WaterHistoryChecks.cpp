#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <atomic>
#include <vector>
#include <mutex>
#include <cstdio>
#include <cstring>
static FILE* logFile=stdout;
template<class T>T Read(uintptr_t p){T v{};SIZE_T n{};ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(p),&v,sizeof(v),&n);return v;}
#include "WaterHistory.h"
using namespace WaterHistory;
static void Check(HRESULT h){if(FAILED(h)){printf("HRESULT %08lx\n",h);ExitProcess(1);}}
int main(){
 Com<IDXGIFactory4> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));Com<IDXGIAdapter> adapter;Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));Com<ID3D12Device> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
 D3D12_COMMAND_QUEUE_DESC qd{};Com<ID3D12CommandQueue> q;Check(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));queue=q.Get();executeOriginal=[](ID3D12CommandQueue* q,UINT n,ID3D12CommandList*const* c){q->ExecuteCommandLists(n,c);};barrierOriginal=[](ID3D12GraphicsCommandList* l,UINT n,const D3D12_RESOURCE_BARRIER* b){l->ResourceBarrier(n,b);};
 Com<ID3D12CommandAllocator> allocator;Com<ID3D12GraphicsCommandList> list;Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));Check(list->Close());
 Com<ID3D12Fence> done;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&done)));auto event=CreateEventW(nullptr,FALSE,FALSE,nullptr);uint64_t tick=0;
 auto wait=[&](){Check(q->Signal(done.Get(),++tick));Check(done->SetEventOnCompletion(tick,event));if(WaitForSingleObject(event,5000)!=WAIT_OBJECT_0)ExitProcess(2);};
 auto open=[&](){wait();Check(allocator->Reset());Forget(list.Get());Check(list->Reset(allocator.Get(),nullptr));};
 auto submit=[&](){Check(list->Close());ID3D12CommandList* batch[]={list.Get()};Execute(q.Get(),1,batch);};
 D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=d.Height=16;d.DepthOrArraySize=1;d.MipLevels=3;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
 D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;Com<ID3D12Resource> sources[5];
 D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=15;Com<ID3D12DescriptorHeap> heap;Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));auto base=heap->GetCPUDescriptorHandleForHeapStart();auto stride=device->GetDescriptorHandleIncrementSize(hd.Type);
 alignas(16) unsigned char state[0x3800]{},allocs[5][0x80]{};
 for(unsigned k=0;k<5;++k){Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&sources[k])));auto r=sources[k].Get();memcpy(allocs[k]+0x30,&r,8);unsigned st=4,id=k+10;memcpy(allocs[k]+0x48,&st,4);memcpy(allocs[k]+8,&id,4);for(unsigned m=0;m<3;++m){D3D12_RENDER_TARGET_VIEW_DESC v{};v.Format=d.Format;v.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;v.Texture2D.MipSlice=m;device->CreateRenderTargetView(r,&v,{base.ptr+(k*3+m)*stride});}}
 auto fill=[&](unsigned channel){open();for(unsigned k=0;k<5;++k)for(unsigned m=0;m<3;++m){float c[4]={0,0,0,1};c[(channel+m)%3]=1;list->ClearRenderTargetView({base.ptr+(k*3+m)*stride},c,0,nullptr);}for(unsigned k=0;k<5;++k)Observe(list.Get(),reinterpret_cast<uintptr_t>(state),k,reinterpret_cast<uintptr_t>(allocs[k]));submit();};
 Begin(q.Get(),1,1,1,true);fill(0);End();Begin(q.Get(),2,1,1,true);fill(1);End();
 auto verify=[&](unsigned channel){open();std::vector<Com<ID3D12Resource>> results;for(unsigned k=0;k<5;++k)for(unsigned m=0;m<3;++m){unsigned w=16>>m;D3D12_RESOURCE_DESC b{};b.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;b.Width=256*w;b.Height=1;b.DepthOrArraySize=b.MipLevels=1;b.SampleDesc.Count=1;b.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;D3D12_HEAP_PROPERTIES read{};read.Type=D3D12_HEAP_TYPE_READBACK;Com<ID3D12Resource> output;Check(device->CreateCommittedResource(&read,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&output)));Barrier(list.Get(),sources[k].Get(),m,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=output.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint.Footprint={d.Format,w,w,1,256};src.pResource=sources[k].Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;src.SubresourceIndex=m;list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);Barrier(list.Get(),sources[k].Get(),m,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);results.push_back(output);}submit();wait();for(unsigned i=0;i<results.size();++i){unsigned m=i%3,w=16>>m;unsigned char* pixels{};Check(results[i]->Map(0,nullptr,reinterpret_cast<void**>(&pixels)));for(unsigned y=0;y<w;++y)for(unsigned x=0;x<w;++x)for(unsigned c=0;c<3;++c)if(pixels[y*256+x*4+c]!=(c==(channel+m)%3?255:0)){printf("Mismatch bank=%u mip=%u\n",i/3,m);ExitProcess(3);}results[i]->Unmap(0,nullptr);}};
 Begin(q.Get(),1,1,2,true);verify(0);for(auto& b:banks)b->seen=true;End();Begin(q.Get(),2,1,2,true);verify(1);for(auto& b:banks)b->seen=true;End();
 // Force ring reuse and validate no unconditional CPU waits or stale banks.
 for(unsigned frame=3;frame<22;++frame)for(unsigned eye=1;eye<=2;++eye){Begin(q.Get(),eye,1,frame,true);for(auto& b:banks)b->seen=true;End();}
 auto before=restored.load();Begin(q.Get(),1,2,22,true);if(restored!=before)return 4;End();
 Begin(q.Get(),1,2,23,true);for(auto& b:banks){b->seen=true;b->states[1]=-1;}End();if(unknown!=5)return 5;
 // Track per-mip transitions at recording, commit only in submission order.
 open();D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;v.Transition={sources[0].Get(),1,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};Barriers(list.Get(),1,&v);if(banks[0]->states[1]!=-1)return 6;submit();if(banks[0]->states[1]!=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)return 7;wait();
 if(discovered!=5||failed!=0)return 8;
 Report();puts("PASS five water histories, all mip pixels, both eyes, ring reuse, epoch invalidation, unknown-state rejection and submitted per-mip state tracking");CloseHandle(event);
}
