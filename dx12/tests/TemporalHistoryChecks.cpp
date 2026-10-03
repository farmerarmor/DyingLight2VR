#include "TemporalHistory.h"
#include "DlssHistory.h"
#include <cstdio>
#include <initializer_list>
#define CHECK(x) do{if(!(x)){printf("FAIL line %d\n",__LINE__);return __LINE__;}}while(0)
int main(){
    TemporalHistory::ResetTracker reset[2];
    CHECK(reset[0].NeedsReset(1,100));CHECK(reset[1].NeedsReset(1,200));
    CHECK(!reset[0].NeedsReset(1,100));CHECK(!reset[1].NeedsReset(1,200));
    CHECK(reset[0].NeedsReset(2,100));CHECK(reset[1].NeedsReset(2,200));
    CHECK(!reset[0].NeedsReset(2,100));CHECK(reset[0].NeedsReset(2,101));
    TemporalHistory::Bank<16,2> bank;
    unsigned seed[4]={100,200,300,400};
    auto left=static_cast<unsigned*>(bank.Get(1000,7,1,1,seed));
    auto right=static_cast<unsigned*>(bank.Get(1000,7,1,2,seed));
    CHECK(left && right && left!=right);left[0]=11;right[0]=22;
    seed[0]=999;
    CHECK(static_cast<unsigned*>(bank.Get(1000,7,1,1,seed))[0]==11);
    CHECK(static_cast<unsigned*>(bank.Get(1000,7,1,2,seed))[0]==22);
    CHECK(static_cast<unsigned*>(bank.Get(1000,7,2,1,seed))[0]==999);
    CHECK(static_cast<unsigned*>(bank.Get(1000,7,2,2,seed))[0]==999);
    CHECK(bank.Get(2000,7,2,1,seed));CHECK(!bank.Get(3000,7,2,1,seed));
    CHECK(bank.Get(3000,8,3,1,seed));CHECK(!bank.Get(3000,8,3,3,seed));
    CHECK(!bank.Get(0,8,3,1,seed));CHECK(!bank.Get(3000,8,3,1,nullptr));
    // Native cache continues to change while the prior-eye snapshot stays fixed
    // for all workers until the scene completes. Other eyes remain independent.
    TemporalHistory::Bank<16,2> writes;
    unsigned native[4]={1,2,3,4};
    auto previous=static_cast<unsigned*>(writes.Get(99,0,1,1,native));
    native[0]=10;CHECK(previous[0]==1 && native[0]==10);
    CHECK(writes.Store(99,0,1,1,native));CHECK(previous[0]==10);
    auto second=static_cast<unsigned*>(writes.Get(99,0,1,2,native));
    native[0]=20;CHECK(second[0]==10 && previous[0]==10);
    CHECK(writes.Store(99,0,1,2,native));CHECK(previous[0]==10 && second[0]==20 && native[0]==20);
    CHECK(!writes.Store(99,0,1,0,native));CHECK(!writes.Store(99,0,1,1,nullptr));
    CHECK(!TemporalHistory::WriterMask(0x1148184));
    unsigned mask=0;for(auto address:{0x11161b6u,0x11161e4u,0x1116243u,0x11162adu,0x111632du,0x11163a3u,0x111640du})mask|=TemporalHistory::WriterMask(address);
    CHECK(mask==127);
    for(unsigned id:{0u,1u,777u}) {
        auto l=TemporalHistory::Viewport(id,1),r=TemporalHistory::Viewport(id,2);
        CHECK(l!=r && l!=id && r!=id);
        CHECK(TemporalHistory::Viewport(id,0)==id);
    }
    CHECK(TemporalHistory::Viewport(0x80000000u,1)==0x80000000u);
    DlssHistory::History matrices;
    alignas(16) unsigned char packet[0x1b0]{};
    auto put=[&](unsigned off,float f){memcpy(packet+off,&f,4);};
    put(0x30,1);put(0x44,1);put(0x50,-.15f);put(0x5c,-1);put(0x68,.05f);
    put(0x188,1);put(0x180,1);put(0x19c,1);
    CHECK(matrices.Apply(packet,1,1,100,false));
    auto identity=[&](){for(int i=0;i<4;++i)for(int j=0;j<4;++j)if(std::abs(DlssHistory::Float(packet,0xf0+4*(j*4+i))-(i==j?1.f:0.f))>1e-5)return false;return true;};
    CHECK(identity());put(0x18,0);put(0x50,.15f);put(0x170,.064f);
    CHECK(matrices.Apply(packet,2,1,200,false) && identity());
    // Stationary left must remain identity despite intervening right eye.
    put(0x18,0);put(0x50,-.15f);put(0x170,0);
    CHECK(matrices.Apply(packet,1,1,100,false) && identity());
    // Same-eye motion .1 with near=.05 yields +2 in clip row0,col2.
    put(0x170,.1f);CHECK(matrices.Apply(packet,1,1,100,false));
    CHECK(std::abs(DlssHistory::Float(packet,0xf0+4*8)-2.f)<1e-4);
    CHECK(std::abs(DlssHistory::Float(packet,0x130+4*8)+2.f)<1e-4);
    CHECK(matrices.Apply(packet,1,2,100,false) && identity());
    CHECK(!matrices.Apply(packet,0,2,100,false));put(0x30,0);CHECK(!matrices.Apply(packet,1,2,100,false));
    puts("PASS eye histories, native writes, DLSS same-eye reprojection, translation, inverse and resets");
}
