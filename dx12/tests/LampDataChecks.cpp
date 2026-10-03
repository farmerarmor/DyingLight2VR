#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <atomic>
#include <mutex>
#include <vector>
#include <cstdio>
#include <fstream>
#include <iterator>
static FILE* logFile=stdout;static uintptr_t backend{};
template<class T>T Read(uintptr_t p){T v{};SIZE_T n{};ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(p),&v,sizeof(v),&n);return v;}
namespace TemporalPort {inline std::atomic<unsigned> renderEye{},epoch{};inline unsigned previousCounter{};}
#include "WaterHistory.h"
#include "ShadowPort.h"
template<class T>using Com=Microsoft::WRL::ComPtr<T>;
static void Check(HRESULT hr,unsigned line){if(FAILED(hr)){printf("HRESULT=%08lx line=%u\n",hr,line);ExitProcess(1);}}
#define H(hr) Check(hr,__LINE__)
#include "LampData.h"

int main(){
 Com<IDXGIFactory4> factory;H(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));Com<IDXGIAdapter> adapter;H(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));Com<ID3D12Device> device;H(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
 Com<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;H(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));WaterHistory::queue=queue.Get();
 Com<ID3D12CommandAllocator> allocator;H(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE,IID_PPV_ARGS(&allocator)));Com<ID3D12GraphicsCommandList> list;H(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_COMPUTE,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
 D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=4096;d.Height=d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
 D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_UPLOAD;Com<ID3D12Resource> upload,source;H(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)));void* bytes{};H(upload->Map(0,nullptr,&bytes));memset(bytes,0x5a,4096);upload->Unmap(0,nullptr);
 hp.Type=D3D12_HEAP_TYPE_DEFAULT;H(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&source)));list->CopyBufferRegion(source.Get(),0,upload.Get(),0,4096);WaterHistory::Barrier(list.Get(),source.Get(),~0u,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
 unsigned char state[0x3800]{},view[128]{},allocation[128]{};auto put=[](void*p,uintptr_t v){memcpy(p,&v,8);};put(view,reinterpret_cast<uintptr_t>(allocation));put(allocation+0x30,reinterpret_cast<uintptr_t>(source.Get()));unsigned ss=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;memcpy(allocation+0x48,&ss,4);for(unsigned slot:{11u,12u,13u})put(state+0xb38+16*(5*72+slot),reinterpret_cast<uintptr_t>(view));
 wchar_t temp[MAX_PATH]{},path[MAX_PATH]{};GetTempPathW(MAX_PATH,temp);swprintf_s(path,L"%sDL2VR-LampCheck-%llu",temp,GetTickCount64());if(!CreateDirectoryW(path,nullptr))return 1;LampData::Arm(path);
 for(unsigned eye:{1u,2u}){TemporalPort::renderEye=eye;LampData::Capture(list.Get(),reinterpret_cast<uintptr_t>(state),0x057c34d856e62863ull);LampData::Capture(list.Get(),reinterpret_cast<uintptr_t>(state),0x057c34d856e62863ull);}
 if(LampData::armed||LampData::pending.size()!=6)return 2;
 H(list->Close());ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);LampData::Present();if(LampData::pending.size()!=6)return 8;LampData::Submitted(queue.Get(),1,lists);HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);H(LampData::pending.back().completion->SetEventOnCompletion(1,event));if(WaitForSingleObject(event,5000)!=WAIT_OBJECT_0)return 3;LampData::Present();if(!LampData::pending.empty())return 4;
 for(unsigned eye:{1u,2u})for(unsigned slot:{11u,12u,13u}){wchar_t file[MAX_PATH]{};swprintf_s(file,L"%s/eye%u-light%u.bin",path,eye,slot);FILE*f{};if(_wfopen_s(&f,file,L"rb"))return 5;unsigned char data[4096]{};auto n=fread(data,1,4096,f);fclose(f);if(n!=4096)return 6;for(auto b:data)if(b!=0x5a)return 7;}
 puts("PASS six GPU light-buffer captures, eye deduplication, queue fence retention and saved file contents");
}
