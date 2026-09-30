#pragma once
#include <cstdint>
#include <cstring>
#include <cstddef>
namespace TemporalHistory {
inline constexpr uintptr_t writerReturns[]={0x11161b6,0x11161e4,0x1116243,0x11162ad,0x111632d,0x11163a3,0x111640d};
inline unsigned WriterMask(uintptr_t returnRva) {
    for(unsigned i=0;i<7;++i)if(returnRva==writerReturns[i])return 1u<<i;
    return 0;
}
struct ResetTracker {
    unsigned epoch{},viewport{};bool valid{};
    bool NeedsReset(unsigned nextEpoch,unsigned nextViewport) {
        bool reset=!valid || epoch!=nextEpoch || viewport!=nextViewport;
        valid=true;epoch=nextEpoch;viewport=nextViewport;return reset;
    }
};
// Caller serializes lookup and guarantees the selected bank remains alive
// until the native scene worker barrier has completed.
template<size_t Bytes,size_t Capacity> struct Bank {
    struct alignas(16) Entry {
        uintptr_t owner{};unsigned key{},epoch{};bool valid[2]{};
        alignas(16) unsigned char data[2][Bytes]{};
    } entries[Capacity]{};
    bool Store(uintptr_t owner,unsigned key,unsigned epoch,unsigned eye,const void* current) {
        auto dest=Get(owner,key,epoch,eye,current);
        if(!dest)return false;memcpy(dest,current,Bytes);return true;
    }
    void* Get(uintptr_t owner,unsigned key,unsigned epoch,unsigned eye,const void* seed) {
        if(!owner || !seed || eye<1 || eye>2)return nullptr;
        Entry* found=nullptr;Entry* free=nullptr;
        for(auto& e:entries) {
            if(e.owner==owner && e.key==key){found=&e;break;}
            if(!free && (!e.owner || e.epoch!=epoch))free=&e;
        }
        if(!found){if(!free)return nullptr;found=free;*found={};found->owner=owner;found->key=key;}
        if(found->epoch!=epoch){found->epoch=epoch;found->valid[0]=found->valid[1]=false;}
        unsigned i=eye-1;
        if(!found->valid[i]){memcpy(found->data[i],seed,Bytes);found->valid[i]=true;}
        return found->data[i];
    }
};
inline unsigned Viewport(unsigned original,unsigned eye) {
    if(eye<1 || eye>2 || original>=0x40000000u)return original;
    return original | (eye<<30);
}
}
