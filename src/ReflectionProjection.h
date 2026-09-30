#pragma once
#include <vector>
#include <cstring>
#include <cstdint>
#include "DxbcChecksum.h"
inline uint64_t ShaderHash(const void* data,unsigned bytes) {
    uint64_t h=14695981039346656037ull;for(unsigned i=0;i<bytes;++i){h^=static_cast<const unsigned char*>(data)[i];h*=1099511628211ull;}return h;
}
inline unsigned PatchReflectionProjection(const void* data,unsigned bytes,std::vector<unsigned char>& output) {
    output.clear();auto hash=ShaderHash(data,bytes);
    unsigned expected=hash==0xe275ecd626c81c62ull?4:hash==0x6b258d38d343a365ull?8:hash==0xd8177bb354ee5ed0ull?3:0;
    if(!expected || bytes<36 || memcmp(data,"DXBC",4))return 0;
    output.assign(static_cast<const unsigned char*>(data),static_cast<const unsigned char*>(data)+bytes);
    auto read=[&](unsigned off){uint32_t v;memcpy(&v,output.data()+off,4);return v;};
    unsigned chunks=read(28),fixed=0;
    if(chunks>(bytes-32)/4){output.clear();return 0;}
    for(unsigned ch=0;ch<chunks;++ch){
        unsigned off=read(32+ch*4);if(off>bytes-8){output.clear();return 0;}
        unsigned size=read(off+4);if(size>bytes-off-8){output.clear();return 0;}
        if(memcmp(output.data()+off,"SHEX",4) && memcmp(output.data()+off,"SHDR",4))continue;
        if(size<8 || size%4){output.clear();return 0;}
        auto w=reinterpret_cast<uint32_t*>(output.data()+off+8);unsigned n=size/4;
        if(w[1]!=n){output.clear();return 0;}
        for(unsigned i=2;i<n;){
            unsigned op=w[i]&2047,len=(w[i]>>24)&127;
            if(op==53 && i+1<n)len=w[i+1]; // CUSTOMDATA contains its own length.
            if(!len || len>n-i){output.clear();return 0;}
            if(len==8 && i+16<=n && w[i]==0x08000038 && w[i+8]==0x08000038 &&
               w[i+1]==0x00100012 && w[i+9]==0x00100022 && w[i+2]==w[i+10] &&
               w[i+5]==0x0020800a && w[i+6]==2 && w[i+7]==29 &&
               w[i+13]==0x0020801a && w[i+14]==2 && w[i+15]==30 && w[i+4]==w[i+12]) {
                auto a=w[i+3],b=w[i+11];unsigned base=(a>>4)&3;
                // Only direct temp/input scalar operands, consecutive x/y or y/z.
                if((a&~0x30u)==0x0010000a || (a&~0x30u)==0x0010100a) {
                    bool packedXzw=hash==0xd8177bb354ee5ed0ull && base==0 && b==a+0x20;
                    if((base<=1 && b==a+0x10) || packedXzw) {
                        unsigned swizzle=packedXzw?0x38u:base|((base+1)<<2)|((base+2)<<4)|(base<<6);
                        unsigned vector=(a&~0xfffu)|6|(swizzle<<4);
                        w[i]=w[i+8]=0x08000010; // DP3; preserve instruction length and all other code.
                        w[i+3]=w[i+11]=vector;w[i+5]=w[i+13]=0x00208246;
                        ++fixed;i+=16;continue;
                    }
                }
            }
            i+=len;
        }
    }
    if(fixed!=expected || !kcdvr::UpdateDxbcChecksum(output.data(),output.size())){output.clear();return 0;}
    return fixed;
}
