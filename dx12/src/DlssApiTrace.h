#pragma once
#include "DlssGpuCapture.h"
namespace DlssApiTrace {
inline std::atomic<unsigned> remaining{};
inline thread_local bool active{},record{};inline thread_local unsigned eye{},mode{},index{};
inline thread_local void* token{};
inline std::atomic<uint64_t> errors{};
struct Scope {
 Scope(unsigned e,unsigned m){active=true;eye=e;mode=m;token=nullptr;index=0;auto n=remaining.load();record=false;while(n&&e){if(remaining.compare_exchange_weak(n,n-1)){record=true;break;}}}
 ~Scope(){active=false;record=false;}
};
inline void Arm(){remaining=32;DlssGpuCapture::Arm();}
using TokenFn=void*(*)(unsigned);inline TokenFn tokenOriginal;
inline void* Token(unsigned i){auto result=tokenOriginal(i);if(active){index=i;token=result;if(record)fprintf(logFile,"DLSS API token eye=%u mode=%u index=%u token=%p\n",eye,mode,i,result);}return result;}
inline unsigned View(const void* p){return p?Read<unsigned>(reinterpret_cast<uintptr_t>(p)+0x20):0;}
inline void Log(const char* what,unsigned result,const void* t,const void* viewport){if(result)++errors;if(record){fprintf(logFile,"DLSS API %s eye=%u mode=%u index=%u token=%p matches=%u viewport=%u result=%u\n",what,eye,mode,index,t,t==token,View(viewport),result);fflush(logFile);}}
using ConstantsFn=unsigned(*)(const void*,const void*,const void*);inline ConstantsFn constantsOriginal;
inline unsigned Constants(const void* c,const void* t,const void* v){auto r=constantsOriginal(c,t,v);if(active)Log("constants",r,t,v);return r;}
using TagsFn=unsigned(*)(const void*,const void*,const void*,unsigned,void*);inline TagsFn tagsOriginal;
inline unsigned Tags(const void* t,const void* v,const void* tags,unsigned count,void* cmd){if(active)DlssGpuCapture::Tags(tags,count,eye);auto r=tagsOriginal(t,v,tags,count,cmd);if(active){Log("tags",r,t,v);if(record)fprintf(logFile,"DLSS API tags count=%u cmd=%p\n",count,cmd);}return r;}
using EvaluateFn=unsigned(*)(unsigned,const void*,const void* const*,unsigned,void*);inline EvaluateFn evaluateOriginal;
inline unsigned Evaluate(unsigned feature,const void* t,const void* const* inputs,unsigned count,void* cmd){if(active&&feature==0)DlssGpuCapture::Before(cmd,eye,mode);auto r=evaluateOriginal(feature,t,inputs,count,cmd);if(active&&feature==0)DlssGpuCapture::After(cmd,eye,mode);if(active){Log("evaluate",r,t,count&&inputs?inputs[0]:nullptr);if(record)fprintf(logFile,"DLSS API evaluate feature=%u inputs=%u cmd=%p\n",feature,count,cmd);}return r;}
}
