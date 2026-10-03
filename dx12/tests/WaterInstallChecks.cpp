#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <atomic>
#include <vector>
#include <mutex>
#include <cstdio>
#include <cstring>
#include <thread>
#include "MinHook.h"
static FILE* logFile=stdout;
template<class T>T Read(uintptr_t p){T v{};SIZE_T n{};ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(p),&v,sizeof(v),&n);return v;}
static uintptr_t backend{};
namespace TemporalPort {inline std::atomic<unsigned> renderEye{},epoch{};inline unsigned previousCounter{};}
#include "WaterDrawPort.h"

static uintptr_t Dummy(void*,const void*,const D3D12_GRAPHICS_PIPELINE_STATE_DESC*,const void*,void*){return 0;}
int main(int argc,char**argv){
 if(argc!=2)return 1;
 HANDLE file=CreateFileA(argv[1],GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);if(file==INVALID_HANDLE_VALUE)return 2;
 HANDLE mapping=CreateFileMappingW(file,nullptr,PAGE_READONLY|SEC_IMAGE_NO_EXECUTE,0,0,nullptr);if(!mapping)return 3;
 auto image=reinterpret_cast<uintptr_t>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,0));if(!image)return 4;
 if(!WaterDrawPort::FactorySignature(image+0x5ef10))return 5;
 auto code=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);memcpy(code,reinterpret_cast<void*>(image+0x5ef10),256);
 if(MH_Initialize()!=MH_OK)return 6;
 void* original{};if(MH_CreateHook(code,reinterpret_cast<void*>(&Dummy),&original)!=MH_OK)return 7;
 if(MH_EnableHook(code)!=MH_OK||MH_DisableHook(code)!=MH_OK||MH_RemoveHook(code)!=MH_OK)return 8;
 puts("PASS actual installed game factory bytes and MinHook trampoline creation/enable/disable");
 Microsoft::WRL::ComPtr<IDXGIFactory4> factory;Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;Microsoft::WRL::ComPtr<ID3D12Device> device;Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
 if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))||FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)))||FAILED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))))return 9;
 D3D12_COMMAND_QUEUE_DESC desc{};if(FAILED(device->CreateCommandQueue(&desc,IID_PPV_ARGS(&queue))))return 10;
 // Deliberately invalid factory address must not block working water hooks.
 backend=0;if(!WaterDrawPort::Install(device.Get(),queue.Get())||!WaterDrawPort::installed||WaterDrawPort::shadowInstalled)return 11;
 puts("PASS water hooks installed with rejected shadow signature");
 MH_Uninitialize();UnmapViewOfFile(reinterpret_cast<void*>(image));CloseHandle(mapping);CloseHandle(file);VirtualFree(code,0,MEM_RELEASE);
}
