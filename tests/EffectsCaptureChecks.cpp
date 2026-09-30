#include "EffectsCapture.h"
#include <thread>
#include <vector>
#include <filesystem>
#define CHECK(x) do {if(!(x)){printf("FAIL line %d\n",__LINE__);return __LINE__;}}while(0)
int main() {
    using namespace EffectsCapture;
    Init();CHECK(records);CHECK(Arm());CHECK(!Arm());CHECK(!Begin(2,7,1,2,9,0));
    CHECK(Begin(1,7,1,2,9,0));
    uint32_t data[4]{1,2,3,4};
    std::vector<std::thread> threads;
    constexpr unsigned perThread=capacity/4+100;
    for(unsigned t=0;t<4;++t)threads.emplace_back([&] {for(unsigned j=0;j<perThread;++j)Constants(nullptr,2,data,sizeof(data),123,false);});
    for(auto& t:threads)t.join();
    End();CHECK(counts[0]==4*perThread);CHECK(drops[0]==4*perThread-capacity);CHECK(state==3);
    CHECK(!Begin(2,8,1,2,9,0));CHECK(state==1); // Never pair different native iterations.
    CHECK(Begin(1,8,1,2,10,0));
    alignas(16) unsigned char owner[0x180]{},material[0x40]{},desc[26]{},values[80]{};
    unsigned char bytecode[32]{'D','X','B','C'};void* shader=reinterpret_cast<void*>(0x123456);
    Register(shader,bytecode,sizeof(bytecode),0);
    uint32_t entry=64u<<22;uintptr_t list[2]{reinterpret_cast<uintptr_t>(&entry)|(2ull<<56),0};
    *reinterpret_cast<uintptr_t*>(owner+0x168)=reinterpret_cast<uintptr_t>(&shader);
    *reinterpret_cast<uintptr_t*>(owner+0xc0)=reinterpret_cast<uintptr_t>(list);
    *reinterpret_cast<uintptr_t*>(material+0x30)=reinterpret_cast<uintptr_t>(owner);
    uintptr_t packet=reinterpret_cast<uintptr_t>(material);values[20]=77;
    Material(nullptr,0,desc,&packet,values,999,false);
    CHECK(records[0].bytes==80 && records[0].values[20]==77 && records[0].entries==1 && records[0].shaderHash!=0);
    CHECK(values[20]==77 && entry==(64u<<22));
    Register(shader,bytecode,sizeof(bytecode),1);
    Material(nullptr,1,desc,&packet,values,1000,false);
    CHECK(records[1].stage==1 && records[1].shaderHash==records[0].shaderHash && records[1].values[20]==77);
    End();CHECK(Begin(2,8,1,2,10,0));
    Constants(nullptr,2,data,sizeof(data),321,false);
    Constants(nullptr,2,nullptr,9999,321,false);
    Material(nullptr,0,nullptr,nullptr,nullptr,321,false);
    alignas(16) unsigned char world[0x488]{},scene[0x28]{},light[320]{};
    *reinterpret_cast<uintptr_t*>(scene+0x20)=reinterpret_cast<uintptr_t>(world);
    *reinterpret_cast<uintptr_t*>(world+0x480)=0x1000;
    *reinterpret_cast<uintptr_t*>(world+0x2a8)=reinterpret_cast<uintptr_t>(light);
    *reinterpret_cast<unsigned*>(world+0x300)=1;light[64]=45;
    Spotlights(scene,0x23b0,false);Spotlights(scene,0x23b0,true);
    CHECK(records[capacity+4].kind==3 && records[capacity+4].values[64]==45);
    CHECK(records[capacity+6].kind==5 && records[capacity+6].values[64]==45);
    Spotlights(scene,0x33b0,false);CHECK(records[capacity+7].status==3);
    End();CHECK(records[capacity+1].status==1 && records[capacity+2].status==2);
    wchar_t dir[MAX_PATH];GetTempPathW(MAX_PATH,dir);
    auto root=std::filesystem::path(dir)/(L"DL2VR-effects-check-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directory(root);
    unsigned error{},total{},dropped{};uint64_t pair{};
    CHECK(Save((root/L"winmm.dll").c_str(),error,total,dropped,pair));
    CHECK(!error && total==10 && dropped==0 && pair==8 && state==0);
    auto capture=root/(L"DL2VR-effects-"+std::to_wstring(header.request));
    CHECK(std::filesystem::file_size(capture/L"records.bin")==sizeof(Header)+10*sizeof(Record));
    unsigned files=0;for(auto& p:std::filesystem::directory_iterator(capture))++files;CHECK(files==3);
    printf("PASS bounded concurrent capture, pair identity, native mapping, rejected reads, saved records and shader; %ls\n",root.c_str());
}
