#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Read-only, one synchronous Submit per eye. No disk I/O on draw workers.
namespace EffectsCapture {
constexpr unsigned capacity=8192, shaderCapacity=16384;
struct Record {
    uint32_t kind,eye,stage,bytes,entries,status,thread,ui;
    uint64_t object,shaderHash,caller,stack[8];
    uint32_t mapping[128];
    unsigned char values[0x810];
};
struct Header {
    uint64_t magic=0x31465845324c44ull, request{},pair{},engine{},backend{};
    uint32_t recordBytes=sizeof(Record),count[2]{},dropped[2]{},counter[2]{};
    float camera[2][100]{}; // Camera+0x10 through +0x19f, before each Submit.
};
struct Shader {void* object;uint64_t hash;unsigned stage,bytes;void* data;void* native;};
inline Shader shaders[shaderCapacity]{};
inline unsigned shaderCount{},shaderBytes{};
inline SRWLOCK shaderLock=SRWLOCK_INIT;
inline Record* records{};
inline Header header;
inline std::atomic<unsigned> state{},eye{},counts[2]{},drops[2]{};
// state: idle, armed, left, waiting-right, right, ready, writing.
inline void Init() {records=static_cast<Record*>(VirtualAlloc(nullptr,2ull*capacity*sizeof(Record),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));}
inline void Register(void* object,const void* data,unsigned bytes,unsigned stage,void* native=nullptr) {
    if(!records || !object || bytes<32 || bytes>1024*1024 || (stage!=0 && stage!=1 && stage!=5) || memcmp(data,"DXBC",4))return;
    uint64_t hash=14695981039346656037ull;
    for(unsigned i=0;i<bytes;++i){hash^=static_cast<const unsigned char*>(data)[i];hash*=1099511628211ull;}
    AcquireSRWLockExclusive(&shaderLock);
    unsigned i=0;for(;i<shaderCount;++i)if(shaders[i].object==object && shaders[i].stage==stage)break;
    if(i<shaderCapacity) {
        if(i==shaderCount)++shaderCount;
        auto& s=shaders[i];s.native=native;
        if(s.hash!=hash || !s.data) {
            if(s.data){HeapFree(GetProcessHeap(),0,s.data);shaderBytes-=s.bytes;}
            s={object,hash,stage,bytes,nullptr,native};
            if(shaderBytes+bytes<=64*1024*1024) {
                s.data=HeapAlloc(GetProcessHeap(),0,bytes);
                if(s.data){memcpy(s.data,data,bytes);shaderBytes+=bytes;}
            }
        }
    }
    ReleaseSRWLockExclusive(&shaderLock);
}
inline bool Arm() {
    if(!records)return false;
    unsigned expected=0;
    return state.compare_exchange_strong(expected,1);
}
inline bool Begin(unsigned which,uint64_t pair,uint64_t engine,uint64_t backend,unsigned counter,uintptr_t camera) {
    if(which!=1 && which!=2)return false;
    unsigned expected=which==1?1:3;
    if(which==2 && state.load()==3 && pair!=header.pair){state.store(1);return false;}
    if(!state.compare_exchange_strong(expected,which==1?2:4))return false;
    if(which==1) {
        header=Header{};header.request=GetTickCount64();header.pair=pair;header.engine=engine;header.backend=backend;
        for(unsigned i=0;i<2;++i){counts[i]=0;drops[i]=0;}
    }
    header.counter[which-1]=counter;
    __try {if(camera)memcpy(header.camera[which-1],reinterpret_cast<void*>(camera+0x10),400);} __except(EXCEPTION_EXECUTE_HANDLER) {}
    eye.store(which);
    return true;
}
inline void End() {
    unsigned which=eye.exchange(0);
    if(which)state.store(which==1?3:5); // Native Submit's worker barrier already completed.
}
inline Record* Allocate(unsigned kind,unsigned stage,uintptr_t object,uintptr_t caller,bool ui) {
    unsigned which=eye.load();if(!which)return nullptr;
    unsigned i=counts[which-1].fetch_add(1);
    if(i>=capacity){++drops[which-1];return nullptr;}
    auto* r=&records[(which-1)*capacity+i];memset(r,0,sizeof(*r));
    r->kind=kind;r->eye=which;r->stage=stage;r->object=object;r->caller=caller;r->ui=ui;r->thread=GetCurrentThreadId();
    void* stack[8]{};CaptureStackBackTrace(2,8,stack,nullptr);memcpy(r->stack,stack,sizeof(stack));
    return r;
}
inline void Constants(void* context,unsigned slot,const void* data,unsigned bytes,uintptr_t caller,bool ui) {
    if(auto* r=Allocate(2,slot,reinterpret_cast<uintptr_t>(context),caller,ui)) {
        if(bytes>sizeof(r->values)){r->status=1;return;}
        __try {memcpy(r->values,data,bytes);r->bytes=bytes;} __except(EXCEPTION_EXECUTE_HANDLER) {r->status=2;}
    }
}
inline void Spotlights(void* scene,uintptr_t camera,bool after) {
    // Exact native chain: SceneData+20 -> world; 726900/7372d0 write
    // world+2a8, count+300. 1111170 uploads count*320 bytes unchanged.
    auto* info=Allocate(after?6:4,0,0,0,false);if(!info)return;
    __try {
        auto world=*reinterpret_cast<uintptr_t*>(static_cast<unsigned char*>(scene)+0x20);
        info->object=world;
        if(!world || !camera || *reinterpret_cast<uintptr_t*>(world+0x480)!=camera-0x13b0){info->status=3;return;}
        memcpy(info->values,reinterpret_cast<void*>(world),0x220);info->bytes=0x220;
        unsigned count=*reinterpret_cast<unsigned*>(world+0x300);
        info->entries=count;
        if(count>384){info->status=1;return;}
        auto source=*reinterpret_cast<uintptr_t*>(world+0x2a8)&0xffffffffffffull;
        if(count && !source){info->status=2;return;}
        for(unsigned i=0;i<count;++i) {
            auto* r=Allocate(after?5:3,i,source+i*320,0,false);if(!r)break;
            r->bytes=320;
            memcpy(r->values,reinterpret_cast<void*>(source+i*320),320);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {info->status=2;}
}
inline void Material(void* object,unsigned stage,void* descriptor,void* packet,const void* values,uintptr_t caller,bool ui) {
    auto* r=Allocate(1,stage,reinterpret_cast<uintptr_t>(object),caller,ui);if(!r)return;
    void* shader{};
    __try {
        constexpr uintptr_t mask=0xffffffffffffull;
        auto material=*static_cast<uintptr_t*>(packet);
        auto owner=*reinterpret_cast<uintptr_t*>(material+0x30);
        auto table=*reinterpret_cast<uintptr_t*>(owner+0x168)&mask;
        auto d=static_cast<unsigned char*>(descriptor);
        shader=*reinterpret_cast<void**>(table+*reinterpret_cast<unsigned short*>(d+2)*8);
        auto maps=*reinterpret_cast<uintptr_t*>(owner+0xc0)&mask;
        auto list=maps+*reinterpret_cast<unsigned short*>(d+0xa)*16;
        auto tagged=*reinterpret_cast<uintptr_t*>(list);
        unsigned n=static_cast<unsigned>(tagged>>56);
        r->entries=n?n-1:*reinterpret_cast<unsigned*>(list+8);
        if(r->entries>128)r->status=1;
        else {
            memcpy(r->mapping,reinterpret_cast<void*>(tagged&mask),r->entries*4);
            unsigned extent=0;
            for(unsigned i=0;i<r->entries;++i) {
                unsigned entry=r->mapping[i],end=(entry&2047)+16+(entry>>22);
                if(end>extent)extent=end;
            }
            if(extent>sizeof(r->values))r->status=1;
            else {memcpy(r->values,values,extent);r->bytes=extent;}
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {r->status=2;}
    AcquireSRWLockShared(&shaderLock);
    for(unsigned i=0;i<shaderCount;++i)if(shaders[i].object==shader && shaders[i].stage==stage){r->shaderHash=shaders[i].hash;break;}
    ReleaseSRWLockShared(&shaderLock);
}
inline bool Save(const wchar_t* directory,unsigned& error,unsigned& total,unsigned& dropped,uint64_t& pair) {
    unsigned expected=5;if(!state.compare_exchange_strong(expected,6))return false;
    error=0;total=0;dropped=0;pair=header.pair;
    for(unsigned i=0;i<2;++i){header.count[i]=(counts[i].load()<capacity?counts[i].load():capacity);header.dropped[i]=drops[i];total+=header.count[i];dropped+=header.dropped[i];}
    wchar_t folder[MAX_PATH],file[MAX_PATH],base[MAX_PATH];
    wcscpy_s(base,directory);auto slash=wcsrchr(base,L'\\');
    if(!slash){error=ERROR_BAD_PATHNAME;state=0;return true;}*slash=0;
    swprintf_s(folder,L"%s\\DL2VR-effects-%llu",base,header.request);
    if(!CreateDirectoryW(folder,nullptr)){error=GetLastError();state=0;return true;}
    swprintf_s(file,L"%s\\records.bin",folder);
    FILE* f{};_wfopen_s(&f,file,L"wb");
    if(f) {
        if(fwrite(&header,sizeof(header),1,f)!=1)error=ERROR_WRITE_FAULT;
        for(unsigned i=0;i<2;++i)if(fwrite(records+i*capacity,sizeof(Record),header.count[i],f)!=header.count[i])error=ERROR_WRITE_FAULT;
        if(fclose(f))error=ERROR_WRITE_FAULT;
    } else error=ERROR_OPEN_FAILED;
    // Copy one bytecode at a time under the registry lock; perform I/O unlocked.
    for(unsigned i=0;i<shaderCapacity;++i) {
        Shader s{};void* copy{};
        AcquireSRWLockShared(&shaderLock);
        if(i<shaderCount) {
            s=shaders[i];bool used=false;
            for(unsigned e=0;e<2 && !used;++e)for(unsigned j=0;j<header.count[e];++j)if(records[e*capacity+j].shaderHash==s.hash){used=true;break;}
            if(used && s.data){copy=HeapAlloc(GetProcessHeap(),0,s.bytes);if(copy)memcpy(copy,s.data,s.bytes);}
        }
        ReleaseSRWLockShared(&shaderLock);
        if(!s.object)break;
        if(copy) {
            swprintf_s(file,L"%s\\stage%u-%016llx.dxbc",folder,s.stage,s.hash);
            _wfopen_s(&f,file,L"wb");
            if(f){if(fwrite(copy,1,s.bytes,f)!=s.bytes)error=ERROR_WRITE_FAULT;if(fclose(f))error=ERROR_WRITE_FAULT;}else error=ERROR_OPEN_FAILED;
            HeapFree(GetProcessHeap(),0,copy);
        }
    }
    state=0;return true;
}
}
