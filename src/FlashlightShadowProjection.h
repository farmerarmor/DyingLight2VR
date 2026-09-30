#pragma once
#include "ReflectionProjection.h"
#include <initializer_list>
// A right-eye variant only. The draw wrapper supplies this pair's left-eye
// depth and camera, so visibility is a function of the receiver's world point.
// Native PBR, cookie, attenuation, and all other lighting remain per-eye.
namespace FlashlightShadowCode {
using Words=std::vector<uint32_t>;
inline Words D(unsigned r,unsigned mask){return {0x00100002u|(mask<<4),r};}
inline Words R(unsigned r,unsigned sw=0xe4){return {0x00100006u|(sw<<4),r};}
inline Words S(unsigned r,unsigned lane){return {0x0010000au|(lane<<4),r};}
inline Words C(unsigned cb,unsigned row,unsigned sw=0xe4){return {0x00208006u|(sw<<4),cb,row};}
inline Words CS(unsigned cb,unsigned row,unsigned lane){return {0x0020800au|(lane<<4),cb,row};}
inline Words V(unsigned r,unsigned sw=0xe4){return {0x00101006u|(sw<<4),r};}
inline Words Neg(Words o){o[0]|=0x80000000u;o.insert(o.begin()+1,0x41);return o;}
inline Words F(float f){uint32_t u;memcpy(&u,&f,4);return {0x00004001,u};}
inline void Op(Words& w,unsigned op,std::initializer_list<Words> args){
    auto start=w.size();w.push_back(op);for(auto& a:args)w.insert(w.end(),a.begin(),a.end());w[start]|=uint32_t(w.size()-start)<<24;
}
inline Words Receiver(unsigned n){
    Words w;unsigned a=n,b=n+1,c=n+2,d=n+3;
    Op(w,0,{D(a,7),C(2,13),Neg(C(12,13))});
    Op(w,50,{D(a,7),V(6),S(1,0),R(a)});
    for(unsigned k=0;k<3;++k)Op(w,16,{D(b,1<<k),R(a),C(12,4+k)});
    Op(w,52,{D(b,8),Neg(S(b,2)),F(.0001f)});
    Op(w,14,{D(c,3),R(b,0x04),S(b,3)});
    Op(w,56,{D(c,1),S(c,0),CS(12,29,0)});
    Op(w,56,{D(c,2),S(c,1),CS(12,30,1)});
    // The same off-centre correction as 047, evaluated in the reference view.
    Op(w,50,{D(c,4),S(c,0),CS(13,2,0),CS(13,1,1)});
    Op(w,50,{D(c,8),S(c,1),CS(13,2,1),CS(13,1,2)});
    Op(w,50,{D(c,4),CS(12,29,2),F(-.5f),S(c,2)});
    Op(w,50,{D(c,8),CS(12,30,2),F(.5f),S(c,3)});
    Op(w,0,{D(c,1),S(c,0),Neg(CS(12,29,2))});
    Op(w,0,{D(c,2),S(c,1),Neg(CS(12,30,2))});
    Op(w,50,{D(c,1),S(c,0),F(.5f),F(.5f)});
    Op(w,50,{D(c,2),S(c,1),F(-.5f),F(.5f)});
    Op(w,29,{D(d,3),R(c,0x04),F(0)});
    Op(w,29,{D(d,12),F(1),R(c,0x40)});
    Op(w,1,{D(d,1),S(d,0),S(d,1)});
    Op(w,1,{D(d,1),S(d,0),S(d,2)});
    Op(w,1,{D(d,1),S(d,0),S(d,3)});
    Op(w,49,{D(a,1),S(b,2),F(0)});
    Op(w,1,{D(d,1),S(d,0),S(a,0)});
    return w;
}
inline bool Patch(const void* data,unsigned bytes,std::vector<unsigned char>& output){
    output.clear();if(!data || bytes<32)return false;
    auto hash=ShaderHash(data,bytes);unsigned tempAt,start,end,n,fr,fc;
    if(hash==0xb248b0205e047a2bull && bytes==10856){tempAt=96;start=301;end=659;n=12;fr=3;fc=3;}
    else if(hash==0x4ce5d78b6c06bdfbull && bytes==7788){tempAt=85;start=274;end=633;n=11;fr=1;fc=2;}
    else if(hash==0xc1344be4b3f10694ull && bytes==11792){tempAt=100;start=314;end=672;n=15;fr=5;fc=3;}
    else if(hash==0x0678cfb133ec65d1ull && bytes==7956){tempAt=88;start=293;end=651;n=12;fr=2;fc=3;}
    else if(hash==0x0e907ded7c1d61eeull && bytes==4976){tempAt=77;start=266;end=625;n=10;fr=1;fc=2;}
    else if(hash==0x4322e80586869a35ull && bytes==8892){tempAt=92;start=306;end=664;n=14;fr=4;fc=3;}
    else return false;
    auto b=static_cast<const unsigned char*>(data);auto read=[&](unsigned off){uint32_t v;memcpy(&v,b+off,4);return v;};
    unsigned chunks=read(28);if(chunks>(bytes-32)/4)return false;
    output.assign(b,b+32+chunks*4);bool done=false;
    auto append=[&](uint32_t v){auto at=output.size();output.resize(at+4);memcpy(output.data()+at,&v,4);};
    for(unsigned k=0;k<chunks;++k){
        unsigned off=read(32+4*k);if(off>bytes-8){output.clear();return false;}
        unsigned size=read(off+4);if(size>bytes-off-8){output.clear();return false;}
        uint32_t newOff=unsigned(output.size());memcpy(output.data()+32+4*k,&newOff,4);
        if(memcmp(b+off,"SHEX",4) && memcmp(b+off,"SHDR",4)){output.insert(output.end(),b+off,b+off+8+size);continue;}
        Words w(size/4);memcpy(w.data(),b+off+8,size);
        if(size%4 || w.size()<=end+7 || w[1]!=w.size() || w[tempAt]!=0x02000068 || w[tempAt+1]!=n ||
           w[start]!=0x08000038 || w[start+3]!=0x0010000a || w[start+4]!=1 ||
           w[start+8]!=0x08000000 || w[start+16]!=0x09000032 ||
           w[end]!=0x0700200e || w[end+5]!=0x0010000a || w[end+6]!=1){output.clear();return false;}
        w[tempAt+1]=n+4;
        w[start+3]=0x0010003a;w[start+4]=n+1; // positive reference-view depth
        w[start+11]=0x00100ee6;w[start+12]=n+2; // near UV .zwzw
        w[start+13]=0x80100446;w[start+15]=n+2; // minus current UV .xyxy
        w[start+23]=0x00100446;w[start+24]=n+2;
        w[end+5]=0x0010003a;w[end+6]=n+1;
        Words code(w.begin(),w.begin()+tempAt);
        // Spare pixel CB slots, explicitly restored by the draw wrapper.
        code.insert(code.end(),{0x04000059,0x00208000,12,33});
        code.insert(code.end(),{0x04000059,0x00208000,13,4});
        code.insert(code.end(),w.begin()+tempAt,w.begin()+start);
        auto receiver=Receiver(n);code.insert(code.end(),receiver.begin(),receiver.end());
        code.insert(code.end(),w.begin()+start,w.begin()+end+7);
        Op(code,55,{D(fr,1<<fc),S(n+3,0),S(fr,fc),F(1)}); // no clamped border shadows
        code.insert(code.end(),w.begin()+end+7,w.end());code[1]=unsigned(code.size());
        output.insert(output.end(),b+off,b+off+4);append(unsigned(code.size()*4));
        auto p=reinterpret_cast<const unsigned char*>(code.data());output.insert(output.end(),p,p+code.size()*4);done=true;
    }
    uint32_t total=unsigned(output.size());memcpy(output.data()+24,&total,4);
    if(!done || !kcdvr::UpdateDxbcChecksum(output.data(),output.size())){output.clear();return false;}return true;
}
}
