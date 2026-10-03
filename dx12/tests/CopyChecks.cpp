#include "Dx12Screen.h"
#include <cassert>
#include <cstdio>
static FILE* logFile=stdout;
static HMODULE self=GetModuleHandleW(nullptr);
template<class T> T Read(uintptr_t p){T v{};SIZE_T n{};ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(p),&v,sizeof(v),&n);return v;}
#include "DlssGpuCapture.h"
namespace TemporalPort {inline std::atomic<unsigned> renderEye{},latched{},epoch{};inline unsigned previousCounter{};}
#include "NativeTaaTrace.h"
#include "NativeTaaHistory.h"
struct Dx12ScreenChecks {
 static void Drain(Dx12Screen& s){s.Wait(s.signal,false);}
 static ID3D12Resource* Eye(Dx12Screen& s){return s.eyes[0].Get();}
};
static void Check(HRESULT h){if(FAILED(h)){printf("HRESULT=%08lx\n",h);ExitProcess(1);}}
template<class T> using Com=Microsoft::WRL::ComPtr<T>;
int main(){
 Com<IDXGIFactory4> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));Com<IDXGIAdapter> adapter;Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));Com<ID3D12Device> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
 D3D12_COMMAND_QUEUE_DESC qd{};Com<ID3D12CommandQueue> queue;Check(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
 auto window=CreateWindowExW(0,L"STATIC",L"DL2VR DX12 copy checks",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);if(!window)return 2;
 DXGI_SWAP_CHAIN_DESC1 sd{};sd.Width=64;sd.Height=64;sd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=2;sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
 Com<IDXGISwapChain1> sw1;Check(factory->CreateSwapChainForHwnd(queue.Get(),window,&sd,nullptr,nullptr,&sw1));Com<IDXGISwapChain3> sw;Check(sw1.As(&sw));
 Com<ID3D12Resource> source;Check(sw->GetBuffer(sw->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&source)));Com<ID3D12CommandAllocator> alloc;Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc)));Com<ID3D12GraphicsCommandList> list;Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&list)));
 D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;Com<ID3D12DescriptorHeap> heap;Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));auto handle=heap->GetCPUDescriptorHandleForHeapStart();device->CreateRenderTargetView(source.Get(),nullptr,handle);
 auto barrier=[&](ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;v.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};list->ResourceBarrier(1,&v);};
 barrier(source.Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET);float red[]={1,0,0,1};list->ClearRenderTargetView(handle,red,0,nullptr);barrier(source.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PRESENT);Check(list->Close());ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);
 Dx12Screen screen;
 if(screen.Copy(sw.Get(),queue.Get(),2,1)!=E_UNEXPECTED)return 3;
 Check(screen.Copy(sw.Get(),queue.Get(),1,1));
 if(screen.Copy(sw.Get(),queue.Get(),2,2)!=E_UNEXPECTED)return 4;
 // Exercise more submissions than there are slots; switch modes mid-stream.
 for(unsigned i=3;i<70;++i){screen.SetAsync(i<35||i>=45);Check(screen.Copy(sw.Get(),queue.Get(),1,i));}
 Dx12ScreenChecks::Drain(screen); // Also guarantees the initial clear allocator is idle.
 D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=64*256;bd.Height=1;bd.DepthOrArraySize=bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;Com<ID3D12Resource> readback;Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
 Check(alloc->Reset());Check(list->Reset(alloc.Get(),nullptr));auto saved=Dx12ScreenChecks::Eye(screen);barrier(saved,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint.Footprint={DXGI_FORMAT_R8G8B8A8_UNORM,64,64,1,256};D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=saved;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);barrier(saved,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);Check(list->Close());queue->ExecuteCommandLists(1,lists);Com<ID3D12Fence> fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));Check(queue->Signal(fence.Get(),1));auto event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Check(fence->SetEventOnCompletion(1,event));if(WaitForSingleObject(event,5000)!=WAIT_OBJECT_0)return 5;CloseHandle(event);
 unsigned char* pixels;D3D12_RANGE range{0,64*256};Check(readback->Map(0,&range,reinterpret_cast<void**>(&pixels)));for(unsigned i=0;i<64*64;++i)if(pixels[i*4]!=255||pixels[i*4+1]!=0||pixels[i*4+2]!=0||pixels[i*4+3]!=255)return 6;D3D12_RANGE none{};readback->Unmap(0,&none);
 Check(screen.Copy(sw.Get(),queue.Get(),1,3));screen.Cancel();if(screen.Copy(sw.Get(),queue.Get(),2,3)!=E_UNEXPECTED)return 7;
 // Exercise the exact capture readback and disk path with a known red source.
 Check(alloc->Reset());Check(list->Reset(alloc.Get(),nullptr));
 DlssGpuCapture::Tag captureTag{source,3,D3D12_RESOURCE_STATE_PRESENT};
 DlssGpuCapture::id=424242;DlssGpuCapture::Copy(list.Get(),captureTag,1,0);
 if(DlssGpuCapture::pending.size()!=1)return 8;
 Check(list->Close());queue->ExecuteCommandLists(1,lists);DlssGpuCapture::Flush(queue.Get());
 if(!DlssGpuCapture::pending.empty())return 9;
 wchar_t capturePath[MAX_PATH];GetModuleFileNameW(self,capturePath,MAX_PATH);auto slash=wcsrchr(capturePath,wchar_t(92));wcscpy_s(slash+1,MAX_PATH-(slash+1-capturePath),L"DL2VR-GPU-424242-eye1-mode0-type3.bin");
 FILE* savedFile{};_wfopen_s(&savedFile,capturePath,L"rb");if(!savedFile)return 10;
 unsigned char savedBytes[64*256];auto savedCount=fread(savedBytes,1,sizeof(savedBytes),savedFile);fclose(savedFile);if(savedCount!=sizeof(savedBytes))return 11;
 for(unsigned i=0;i<64*64;++i)if(savedBytes[i*4]!=255||savedBytes[i*4+1]||savedBytes[i*4+2]||savedBytes[i*4+3]!=255)return 12;
 puts("PASS GPU capture copies, fences, writes and preserves exact red pixels");

 // Emulate the native alternating-eye history producer. t6 is a mipchain;
 // only its base level is sampled by AA. No partial correction during warmup.
 alignas(16) unsigned char fakeState[0x3800]{},wrappers[5][0x80]{},allocations[5][0x80]{};
 auto bind=[&](unsigned slot,unsigned n,ID3D12Resource* r){auto wp=reinterpret_cast<uintptr_t>(wrappers[n]);auto ap=reinterpret_cast<uintptr_t>(allocations[n]);memcpy(fakeState+0xb38+16*slot,&wp,8);memcpy(wrappers[n],&ap,8);memcpy(allocations[n]+0x30,&r,8);unsigned one=1,all=~0u,state=0xc0;memcpy(allocations[n]+0x28,&one,4);memcpy(allocations[n]+0x48,&state,4);memcpy(wrappers[n]+0x28,&all,4);};
 Com<ID3D12Resource> previousMotion,colour;auto desc=source->GetDesc();desc.Flags=D3D12_RESOURCE_FLAG_NONE;D3D12_HEAP_PROPERTIES localHeap{};localHeap.Type=D3D12_HEAP_TYPE_DEFAULT;Check(device->CreateCommittedResource(&localHeap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&previousMotion)));
 desc.MipLevels=3;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;Check(device->CreateCommittedResource(&localHeap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&colour)));
 Com<ID3D12DescriptorHeap> colourHeap;Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&colourHeap)));auto colourHandle=colourHeap->GetCPUDescriptorHandleForHeapStart();device->CreateRenderTargetView(colour.Get(),nullptr,colourHandle);
 bind(1,0,source.Get());bind(2,1,saved);bind(3,2,source.Get());bind(4,3,previousMotion.Get());bind(6,4,colour.Get());
 Check(alloc->Reset());Check(list->Reset(alloc.Get(),nullptr));auto readState=static_cast<D3D12_RESOURCE_STATES>(0xc0);
 barrier(source.Get(),D3D12_RESOURCE_STATE_PRESENT,readState);barrier(saved,D3D12_RESOURCE_STATE_COMMON,readState);barrier(previousMotion.Get(),D3D12_RESOURCE_STATE_COMMON,readState);barrier(colour.Get(),D3D12_RESOURCE_STATE_COMMON,readState);
 NativeTaaHistory::Copy(list.Get(),previousMotion.Get(),readState,source.Get(),readState);
 auto clearColour=[&](const float* value){NativeTaaHistory::Barrier(list.Get(),colour.Get(),readState,D3D12_RESOURCE_STATE_RENDER_TARGET);list->ClearRenderTargetView(colourHandle,value,0,nullptr);NativeTaaHistory::Barrier(list.Get(),colour.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,readState);};
 auto capture=[&](ID3D12Resource* r,unsigned type){DlssGpuCapture::Copy(list.Get(),{Com<ID3D12Resource>(r),type,0xc0},1,0);};
 TemporalPort::renderEye=1;TemporalPort::latched=1;TemporalPort::epoch=1;TemporalPort::previousCounter=1;NativeTaaHistory::enabled=true;
 clearColour(red);
 {NativeTaaHistory::Scope scope(list.Get(),reinterpret_cast<uintptr_t>(fakeState));}
 if(NativeTaaHistory::applied!=0)return 13;
 // Left filtered result red; right current depth/motion green.
 barrier(source.Get(),readState,D3D12_RESOURCE_STATE_RENDER_TARGET);float green[]={0,1,0,1},blue[]={0,0,1,1};list->ClearRenderTargetView(handle,green,0,nullptr);barrier(source.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,readState);
 NativeTaaHistory::Copy(list.Get(),saved,readState,source.Get(),readState);NativeTaaHistory::Copy(list.Get(),previousMotion.Get(),readState,source.Get(),readState);
 TemporalPort::renderEye=2;
 {NativeTaaHistory::Scope scope(list.Get(),reinterpret_cast<uintptr_t>(fakeState));}
 if(NativeTaaHistory::applied!=0)return 14;
 clearColour(green); // Right filtered result for the next eye to bank.
 TemporalPort::renderEye=1;TemporalPort::previousCounter=2;
 {NativeTaaHistory::Scope scope(list.Get(),reinterpret_cast<uintptr_t>(fakeState));capture(saved,40);capture(colour.Get(),42);}
 capture(saved,41);capture(colour.Get(),43);
 clearColour(blue); // Left new filtered result. Right still needs old green.
 TemporalPort::renderEye=2;
 {NativeTaaHistory::Scope scope(list.Get(),reinterpret_cast<uintptr_t>(fakeState));capture(colour.Get(),44);}
 capture(colour.Get(),45);
 if(NativeTaaHistory::applied!=6||NativeTaaHistory::failed!=0||NativeTaaHistory::complete!=2)return 15;
 // A discontinuity must invalidate the full set, preserving native blue.
 TemporalPort::renderEye=1;TemporalPort::previousCounter=8;
 {NativeTaaHistory::Scope scope(list.Get(),reinterpret_cast<uintptr_t>(fakeState));capture(colour.Get(),46);}
 if(NativeTaaHistory::applied!=6)return 16;
 barrier(source.Get(),readState,D3D12_RESOURCE_STATE_PRESENT);barrier(saved,readState,D3D12_RESOURCE_STATE_COMMON);
 Check(list->Close());queue->ExecuteCommandLists(1,lists);DlssGpuCapture::Flush(queue.Get());
 for(unsigned t: {40u,41u,42u,43u,44u,45u,46u}){wchar_t name[100];swprintf_s(name,L"DL2VR-GPU-424242-eye1-mode0-type%u.bin",t);wcscpy_s(slash+1,MAX_PATH-(slash+1-capturePath),name);_wfopen_s(&savedFile,capturePath,L"rb");if(!savedFile)return 17;auto n=fread(savedBytes,1,sizeof(savedBytes),savedFile);fclose(savedFile);if(n!=sizeof(savedBytes))return 18;unsigned channel=(t==40||t==42)?0:(t==41||t==43||t==44)?1:2;for(unsigned i=0;i<64*64;++i)for(unsigned c=0;c<3;++c)if(savedBytes[i*4+c]!=(c==channel?255:0)){printf("History pixel failed type=%u pixel=%u channel=%u value=%u\n",t,i,c,savedBytes[i*4+c]);return 19;}}
 puts("PASS complete native AA histories: left red, right green, native colour restored, no partial warmup or stale frame history; multi-mip colour LOD0");

 DestroyWindow(window);screen.Stop();puts("PASS: WARP D3D12 eye-copy pixel verification, allocator reuse, right-without-left rejection, pair mismatch and cancellation.");
}

