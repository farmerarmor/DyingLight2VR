#pragma once
#include "TemporalHistory.h"
#include "DlssHistory.h"
#include <mutex>
namespace TemporalPort {
inline std::atomic<unsigned> mode{2},latched{2},epoch{1},sceneEye{},renderEye{},key{};
inline std::atomic<bool> reset{true};
inline uintptr_t previousCamera{};inline unsigned previousCounter{};
inline std::atomic<uintptr_t> writeTree{};inline std::atomic<unsigned> writeMask{};
inline TemporalHistory::Bank<0x140,32> cameraBank;
inline TemporalHistory::Bank<0x90,32> externalBank;
inline std::mutex cameraLock,externalLock,dlssLock;
inline DlssHistory::History dlssHistory;
inline TemporalHistory::ResetTracker resetTracker[2];
inline std::atomic<uint64_t> lookups[2]{},commits[2]{},external[2]{},evaluations[3]{},constants[3]{},fixed{},failed{},incomplete{};
using LookupFn=void*(*)(void*,const unsigned*);inline LookupFn lookupOriginal;
using PassFn=uintptr_t(*)(void*,void*);inline PassFn passOriginal,evaluateOriginal,constantsOriginal;
inline void Begin(unsigned eye,unsigned token,uintptr_t camera,unsigned counter){
 if(eye==1){auto next=mode.load();if(latched.exchange(next)!=next)reset=true;
 if(reset.exchange(false)||camera!=previousCamera||counter-previousCounter>1)++epoch;
 previousCamera=camera;previousCounter=counter;}
 if(!eye){reset=true;previousCamera=0;}
 key=token;renderEye=latched.load()?eye:0;sceneEye=latched.load()==2?eye:0;writeTree=0;writeMask=0;
}
inline void* Lookup(void* tree,const unsigned* token){
 auto original=lookupOriginal(tree,token);unsigned eye=sceneEye.load();
 if(!original||!eye||*token!=key.load())return original;
 auto writer=TemporalHistory::WriterMask(reinterpret_cast<uintptr_t>(_ReturnAddress())-engine);
 if(writer){uintptr_t expected=0;auto address=reinterpret_cast<uintptr_t>(tree);if(writeTree.compare_exchange_strong(expected,address)||expected==address)writeMask.fetch_or(writer);return original;}
 std::lock_guard guard(cameraLock);auto result=cameraBank.Get(reinterpret_cast<uintptr_t>(tree),*token,epoch.load(),eye,original);if(result){++lookups[eye-1];return result;}return original;
}
inline void EndScene(){auto eye=sceneEye.exchange(0);if(!eye)return;auto tree=writeTree.load();unsigned token=key.load();
 if(tree&&writeMask.load()==127){auto current=lookupOriginal(reinterpret_cast<void*>(tree),&token);std::lock_guard guard(cameraLock);if(cameraBank.Store(tree,token,epoch.load(),eye,current))++commits[eye-1];}
 else{++incomplete;reset=true;}
}
inline void EndPair(){sceneEye=0;renderEye=0;}
inline uintptr_t External(void* object,void* context){auto eye=sceneEye.load();if(!eye)return passOriginal(object,context);
 std::lock_guard guard(externalLock);auto region=static_cast<unsigned char*>(object)+0x20;
 auto bank=externalBank.Get(reinterpret_cast<uintptr_t>(object),key.load(),epoch.load(),eye,region);alignas(16) unsigned char original[0x90];
 if(bank){memcpy(original,region,sizeof(original));memcpy(region,bank,sizeof(original));}
 auto result=passOriginal(object,context);if(bank){memcpy(bank,region,sizeof(original));memcpy(region,original,sizeof(original));++external[eye-1];}return result;
}
// Both DX12 entry points take (native state, packet), unlike DX11's packet-only ABI.
// 77f20 and 3a560 build the Streamline viewport from (*1648a0)+50, falling
// back to the constants state/global DLSS state's cached viewport at +30.
inline uintptr_t Dlss(void* state,void* packet,PassFn fn,bool evaluate){
 std::lock_guard guard(dlssLock);unsigned eye=renderEye.load();if(eye>2)eye=0;
 if(evaluate)++evaluations[eye];else ++constants[eye];
 auto owner=Read<uintptr_t>(backend+0x1648a0);
 auto cache=evaluate?Read<uintptr_t>(backend+0x164ba0):reinterpret_cast<uintptr_t>(state);
 auto viewport=owner?reinterpret_cast<unsigned*>(owner+0x50):cache?reinterpret_cast<unsigned*>(cache+0x30):nullptr;
 if(!eye||!viewport)return fn(state,packet);
 unsigned original=*viewport,mapped=TemporalHistory::Viewport(original,eye);
 if(original>=0x40000000u){++failed;return fn(state,packet);}
 // Preserve the native cached viewport too; neither temporary ID escapes this command.
 unsigned cached=cache?*reinterpret_cast<unsigned*>(cache+0x30):0;
 *viewport=mapped;
 alignas(16) unsigned char corrected[0x1b0];
 if(!evaluate){memcpy(corrected,packet,sizeof(corrected));bool seed=resetTracker[eye-1].NeedsReset(epoch.load(),mapped)||*reinterpret_cast<unsigned*>(corrected+0x18)!=0;
 if(dlssHistory.Apply(corrected,eye,epoch.load(),mapped,seed))++fixed;else{++failed;*reinterpret_cast<unsigned*>(corrected+0x18)=1;}
 packet=corrected;}
 auto result=fn(state,packet);*viewport=original;if(cache)*reinterpret_cast<unsigned*>(cache+0x30)=cached;return result;
}
inline uintptr_t Evaluate(void* state,void* packet){return Dlss(state,packet,evaluateOriginal,true);}
inline uintptr_t Constants(void* state,void* packet){return Dlss(state,packet,constantsOriginal,false);}
inline void Report(){fprintf(logFile,"DX12 temporal F12=%u latched=%u epoch=%u camera=%llu/%llu commit=%llu/%llu incomplete=%llu external=%llu/%llu DLSS eval=%llu/%llu/%llu constants=%llu/%llu/%llu fixed=%llu failed=%llu (texture banks pending)\n",mode.load(),latched.load(),epoch.load(),lookups[0].load(),lookups[1].load(),commits[0].load(),commits[1].load(),incomplete.load(),external[0].load(),external[1].load(),evaluations[0].load(),evaluations[1].load(),evaluations[2].load(),constants[0].load(),constants[1].load(),constants[2].load(),fixed.load(),failed.load());}
}
