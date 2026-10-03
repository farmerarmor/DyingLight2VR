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
static unsigned forwarded{};
static void STDMETHODCALLTYPE Original(ID3D12GraphicsCommandList*,UINT a,UINT b,UINT c,UINT d){if(a!=3||b!=1||c!=2||d!=4)abort();++forwarded;}
int main(){using namespace WaterDrawPort;auto a=reinterpret_cast<ID3D12GraphicsCommandList*>(0x100);auto b=reinterpret_cast<ID3D12GraphicsCommandList*>(0x200);drawOriginal=Original;
 alignas(16) unsigned char state[0x900]{},entries[64]{},material[0x40]{},owner[0x180]{},descriptor[16]{};
 uintptr_t wrapper=reinterpret_cast<uintptr_t>(a),wp=reinterpret_cast<uintptr_t>(&wrapper),ep=reinterpret_cast<uintptr_t>(entries),mp=reinterpret_cast<uintptr_t>(material),op=reinterpret_cast<uintptr_t>(owner);void* table[1]={reinterpret_cast<void*>(0x300)};auto tp=reinterpret_cast<uintptr_t>(table);
 memcpy(state+0x890,&ep,8);state[0x897]=2;memcpy(entries+0x10,&wp,8);memcpy(material+0x30,&op,8);memcpy(owner+0x168,&tp,8);
 Register(table[0],0xe275ecd626c81c62ull,0);Bound(state,descriptor,&mp);Draw(a,3,1,2,4);if(targetDraws!=1)return 1;
 // Clear stale identity when a shader object is reused for an ordinary shader.
 Register(table[0],42,0);Bound(state,descriptor,&mp);Draw(a,3,1,2,4);if(targetDraws!=1)return 2;
 auto other=Find(b,true);other->hash=0x637fddd208eb09bdull;other->state=0;Draw(b,3,1,2,4);Draw(a,3,1,2,4);if(targetDraws!=2)return 3;
 // Native command-list recording can transfer to another worker after bind.
 std::thread worker([&](){Draw(b,3,1,2,4);ClearBinding(b);});worker.join();Draw(b,3,1,2,4);if(targetDraws!=3||forwarded!=6)return 4;
 // Slots with colliding hashes must retain independent identities.
 auto collision=reinterpret_cast<ID3D12GraphicsCommandList*>(0x10100);auto cb=Find(collision,true);if(cb==Find(a)||cb->list!=collision)return 5;
 LARGE_INTEGER begin,end,freq;QueryPerformanceFrequency(&freq);QueryPerformanceCounter(&begin);for(unsigned i=0;i<2000000;++i)Draw(a,3,1,2,4);QueryPerformanceCounter(&end);printf("Ordinary draw wrapper microbenchmark %.3f ms / 2 million (not in-game performance)\n",1000.*(end.QuadPart-begin.QuadPart)/freq.QuadPart);
 if(exhausted)return 6;puts("PASS target-only dispatch, shader identity reuse, interleaved lists, cross-thread handoff, binding reset, collisions and native draw arguments");
}
