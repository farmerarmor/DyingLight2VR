#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include "MinHook.h"
static HMODULE self{};static FILE* logFile=stdout;
template<class T>T Read(uintptr_t p){T v{};if(p)memcpy(&v,reinterpret_cast<void*>(p),sizeof(v));return v;}
#include <vector>
namespace TemporalPort {inline std::atomic<unsigned> renderEye{},latched{},epoch{};inline unsigned previousCounter{};}
#include "DlssGpuCapture.h"
#include "EffectDrawPort.h"
static unsigned forwarded{};
static void STDMETHODCALLTYPE NativeDraw(ID3D12GraphicsCommandList*,UINT a,UINT b,UINT c,UINT d){if(a!=3||b!=1||c!=2||d!=4)abort();++forwarded;}
int main(){using namespace EffectDrawPort;auto a=reinterpret_cast<ID3D12GraphicsCommandList*>(1);auto b=reinterpret_cast<ID3D12GraphicsCommandList*>(2);drawOriginal=NativeDraw;bindings[a]={0x7bff3ae2c1f5835aull,0};bindings[b]={42,0};
 disableTemporalFilter=false;Draw(a,3,1,2,4);disableTemporalFilter=true;Draw(a,3,1,2,4);Draw(b,3,1,2,4);Forget(a);Draw(a,3,1,2,4);if(forwarded!=3||skipped!=1)return 1;
 alignas(16) unsigned char state[0x900]{},entries[64]{};uintptr_t wrapper=reinterpret_cast<uintptr_t>(b);uintptr_t entry=reinterpret_cast<uintptr_t>(&wrapper),base=reinterpret_cast<uintptr_t>(entries);memcpy(state+0x890,&base,8);state[0x897]=2;memcpy(entries+0x10,&entry,8);if(CommandList(reinterpret_cast<uintptr_t>(state))!=b)return 2;state[0x897]=0;unsigned count=2;memcpy(state+0x898,&count,4);memcpy(entries+32+0x10,&entry,8);if(CommandList(reinterpret_cast<uintptr_t>(state))!=b)return 3;
 D3D12_RESOURCE_DESC desc{};desc.MipLevels=1;desc.DepthOrArraySize=1;if(!NativeTaaTrace::CaptureView(~0u,desc))return 4;desc.MipLevels=2;if(!NativeTaaTrace::CaptureView(~0u,desc))return 5;if(!NativeTaaTrace::CaptureView(0,desc))return 6;
 puts("PASS native all-subresource capture guard; F11 exact shader isolation, toggle, native forwarding, command-list reset and both native list layouts");}
