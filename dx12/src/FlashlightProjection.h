#pragma once
#include "ReflectionProjection.h"
// Exact fullscreen lighting VS captured with the flashlight on and off.
// Preserve screen/depth rays, material layout and all unrelated interpolants.
inline bool PatchFlashlightProjection(const void* data,unsigned bytes,std::vector<unsigned char>& output) {
    output.clear();
    if(bytes!=2280 || ShaderHash(data,bytes)!=0xcc49a2529ff637dbull)return false;
    auto b=static_cast<const unsigned char*>(data);
    auto read=[&](unsigned off){uint32_t v;memcpy(&v,b+off,4);return v;};
    unsigned chunks=read(28);if(chunks>(bytes-32)/4)return false;
    output.assign(b,b+32+chunks*4);
    bool patched=false;
    auto append=[&](uint32_t v){auto old=output.size();output.resize(old+4);memcpy(output.data()+old,&v,4);};
    for(unsigned c=0;c<chunks;++c) {
        unsigned off=read(32+c*4);if(off>bytes-8){output.clear();return false;}
        unsigned size=read(off+4);if(size>bytes-off-8){output.clear();return false;}
        uint32_t newOff=static_cast<uint32_t>(output.size());memcpy(output.data()+32+c*4,&newOff,4);
        if(memcmp(b+off,"SHEX",4) && memcmp(b+off,"SHDR",4)) {output.insert(output.end(),b+off,b+off+8+size);continue;}
        std::vector<uint32_t> w(size/4);memcpy(w.data(),b+off+8,size);
        if(size%4 || w.size()<207 || w[1]!=w.size() || w[7]!=0x04000059 || w[9]!=2 || w[10]!=15 ||
           w[183]!=0x0b000032 || w[184]!=0x00102032 || w[185]!=3 ||
           w[194]!=0x0d000032 || w[195]!=0x001020f2 || w[196]!=4) {output.clear();return false;}
        w[10]=33; // Current per-eye projection rows29/30, already in native CB2.
        std::vector<uint32_t> code(w.begin(),w.begin()+183);
        // r2.xy = original shadow near-plane UV (old o3.xy).
        code.insert(code.end(),w.begin()+183,w.begin()+194);code[184]=0x00100032;code[185]=2;
        const uint32_t extra[]={
            // r3.xy = shadow compression scale - screen UV scale.
            0x0b000000,0x00100032,3,0x00208046,0,2,0x00004002,0xbf000000,0x3f000000,0,0,
            0x06000036,0x00100042,3,0x0020802a,2,29,
            0x06000036,0x00100082,3,0x0020802a,2,30,
            // o3.xy += (scale - [0.5,-0.5]) * projection offset.
            0x09000032,0x00102032,3,0x00100ee6,3,0x00100046,3,0x00100046,2,
            // Convert actual view rays to symmetric NDC for the beam cookie.
            0x08000038,0x00100012,2,0x0010000a,1,0x0020800a,2,29,
            0x08000038,0x00100022,2,0x0010001a,1,0x0020801a,2,30
        };
        code.insert(code.end(),std::begin(extra),std::end(extra));
        auto cookie=code.size();code.insert(code.end(),w.begin()+194,w.begin()+207);code[cookie+4]=2;
        code.insert(code.end(),w.begin()+207,w.end());code[1]=static_cast<uint32_t>(code.size());
        output.insert(output.end(),b+off,b+off+4);append(static_cast<uint32_t>(code.size()*4));
        auto p=reinterpret_cast<const unsigned char*>(code.data());output.insert(output.end(),p,p+code.size()*4);patched=true;
    }
    uint32_t total=static_cast<uint32_t>(output.size());memcpy(output.data()+24,&total,4);
    if(!patched || !kcdvr::UpdateDxbcChecksum(output.data(),output.size())){output.clear();return false;}
    return true;
}
