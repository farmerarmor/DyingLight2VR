#pragma once
#include "FlashlightShadowProjection.h"
namespace ShadowDx12Code {
inline bool Patch(const void* data,unsigned bytes,std::vector<unsigned char>& out,unsigned& materialBytes){
 materialBytes=0;if(!FlashlightShadowCode::Patch(data,bytes,out))return false;
 auto old=out;auto read=[&](unsigned p){uint32_t v;memcpy(&v,old.data()+p,4);return v;};unsigned chunks=read(28);out.assign(old.begin(),old.begin()+32+chunks*4);bool done=false;
 auto append=[&](uint32_t v){auto p=out.size();out.resize(p+4);memcpy(out.data()+p,&v,4);};
 for(unsigned k=0;k<chunks;++k){unsigned off=read(32+k*4),size=read(off+4),newOff=unsigned(out.size());memcpy(out.data()+32+k*4,&newOff,4);
  if(memcmp(old.data()+off,"SHEX",4)&&memcmp(old.data()+off,"SHDR",4)){out.insert(out.end(),old.begin()+off,old.begin()+off+8+size);continue;}
  std::vector<uint32_t> w(size/4),code;memcpy(w.data(),old.data()+off+8,size);code.insert(code.end(),w.begin(),w.begin()+2);unsigned refs[2]{};
  for(unsigned p=2;p<w.size();){unsigned n=(w[p]>>24)&127,op=w[p]&2047;if(!n||p+n>w.size()){out.clear();return false;}
   if(op==89&&n==4){if(w[p+2]==12||w[p+2]==13){p+=n;continue;}if(w[p+2]==0){if(!w[p+3]||w[p+3]>64){out.clear();return false;}materialBytes=w[p+3]*16;w[p+3]=101;}}
   // Exact DXBC CB operands; the extension word, when present, precedes
   // the two immediate indices. Reject unsupported indexing modes.
   for(unsigned i=p+1;i<p+n;++i){auto token=w[i];if((token&0x7ffff000u)!=0x00208000u)continue;unsigned at=i+1;if(token&0x80000000u){if(at>=p+n||(w[at]&0x80000000u)){out.clear();return false;}++at;}if(at+1>=p+n)continue;
    if(w[at]==12||w[at]==13){unsigned which=w[at]-12;if(w[at+1]>=(which?4u:33u)){out.clear();return false;}w[at]=0;w[at+1]+=which?97:64;++refs[which];i=at+1;}
   }
   code.insert(code.end(),w.begin()+p,w.begin()+p+n);p+=n;
  }
  if(!materialBytes||!refs[0]||!refs[1]){out.clear();return false;}code[1]=unsigned(code.size());out.insert(out.end(),old.begin()+off,old.begin()+off+4);append(unsigned(code.size()*4));auto ptr=reinterpret_cast<unsigned char*>(code.data());out.insert(out.end(),ptr,ptr+code.size()*4);done=true;
 }
 uint32_t total=unsigned(out.size());memcpy(out.data()+24,&total,4);return done&&kcdvr::UpdateDxbcChecksum(out.data(),out.size());
}
}
