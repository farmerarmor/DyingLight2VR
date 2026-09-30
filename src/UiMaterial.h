#pragma once
#include "UiProjection.h"
#include <cstdint>
// Native material copy entries encode source offset, destination offset, size.
inline bool PatchUiMaterialValues(const uint32_t* entries,unsigned count,unsigned secondRow,
 const float* clip,const void* source,void* output,unsigned capacity) {
 if(!count || count>128 || secondRow<4 || secondRow>64)return false;
 unsigned matrixSource[2]{},extent=0;
 for(unsigned i=0;i<count;++i){unsigned src=16+(entries[i]&2047),size=entries[i]>>22;if(src+size>capacity)return false;if(src+size>extent)extent=src+size;}
 for(unsigned matrix=0;matrix<2;++matrix){
  unsigned begin=matrix?secondRow*16:0;bool covered[64]{};bool found=false;
  for(unsigned i=0;i<count;++i){unsigned src=16+(entries[i]&2047),dst=(entries[i]>>11)&2047,size=entries[i]>>22;
   for(unsigned k=0;k<size;++k)if(dst+k>=begin && dst+k<begin+64){
    unsigned relative=dst+k-begin;if(src+k<relative)return false;
    unsigned base=src+k-relative;
    if(found && base!=matrixSource[matrix])return false;
    matrixSource[matrix]=base;found=true;covered[relative]=true;
   }
  }
  for(bool b:covered)if(!b)return false;
 }
 if(matrixSource[0]<matrixSource[1]+64 && matrixSource[1]<matrixSource[0]+64)return false;
 // Reject aliases to unrelated material values, such as mask coordinates.
 for(unsigned i=0;i<count;++i){unsigned src=16+(entries[i]&2047),dst=(entries[i]>>11)&2047,size=entries[i]>>22;
  for(unsigned k=0;k<size;++k)for(unsigned m=0;m<2;++m)
   if(src+k>=matrixSource[m] && src+k<matrixSource[m]+64 && dst+k!=(m?secondRow*16:0)+src+k-matrixSource[m])return false;
 }
 memcpy(output,source,extent);
 for(unsigned m=0;m<2;++m){
  alignas(16) float native[16],result[16];memcpy(native,static_cast<const char*>(source)+matrixSource[m],64);
  TransformUiProjection(clip,native,result);
  for(float v:result)if(!std::isfinite(v))return false;
  memcpy(static_cast<char*>(output)+matrixSource[m],result,64);
 }
 return true;
}
