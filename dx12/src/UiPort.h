#pragma once
#include "UiMaterial.h"
#include "CrosshairProjection.h"
#include "CrosshairUi.h"
#include <unordered_map>
#include <shared_mutex>
namespace UiPort {
inline std::atomic<bool> enabled{true};
inline thread_local bool eyeValid{},drawActive{},aimValid{};
inline thread_local float aimClip[16]{};
inline thread_local float eyeClip[16]{},drawClip[16]{};
inline std::atomic<float> aspect{16.f/9.f};
inline SRWLOCK frameLock=SRWLOCK_INIT;
inline bool frameValid{},frameAimValid{};inline float frameClip[16]{},frameAimClip[16]{};
inline std::unordered_map<void*,uint64_t> shaderHashes;
inline std::unordered_map<uint64_t,unsigned> unknownDraws;
inline std::shared_mutex shaderLock;inline std::unordered_map<void*,unsigned> shaderRows;
inline std::atomic<uint64_t> draws{},materials{},misses{},rejected{};
inline void Register(void* shader,uint64_t hash,unsigned stage){
 if(stage!=1||!shader)return;
 struct Layout{uint64_t hash;unsigned row;};
 constexpr Layout layouts[]={{0x8694772dce92f52bull,5},{0x058e95ade5042d13ull,10},{0x2b32e54f7c62a6acull,9},{0x497da4e04bb3fb7dull,6},{0x527f4d967ec24472ull,12},{0x704417667302d4b1ull,7},{0x9448478f948a9f58ull,8},{0xaf0a0cd538b86a0full,11},{0xb7df7aa8e5342277ull,8},{0xd0309677febd91a9ull,10},{0xe40185374c2fb58aull,5},{0xed329ddaac0bffb7ull,5}};
 unsigned row=0;for(auto l:layouts)if(l.hash==hash)row=l.row;
 std::unique_lock guard(shaderLock);shaderHashes[shader]=hash;if(row)shaderRows[shader]=row;else shaderRows.erase(shader);
}
inline void Eye(const TrackedEye* eyes,unsigned index,bool valid,const TrackingOrigin& aimOrigin){eyeValid=valid&&MakeUiClipTransform(eyes,index,eyeClip);aimValid=eyeValid&&MakeAimUiClip(eyes,index,aimOrigin,eyeClip,aimClip);}
inline void Aspect(void* manager){if(!eyeValid)return;auto camera=Read<uintptr_t>(reinterpret_cast<uintptr_t>(manager)+0x478);if(camera&&Read<uintptr_t>(camera)==engine+0x193c6a0){float value=Read<float>(camera+0x94)/Read<float>(camera+0x80);if(std::isfinite(value)&&value>0.1f&&value<10)aspect=value;}}
inline void Publish(){AcquireSRWLockExclusive(&frameLock);frameValid=eyeValid&&enabled.load();if(frameValid){memcpy(frameClip,eyeClip,sizeof(frameClip));UndoUiLetterbox(aspect,frameClip);}frameAimValid=frameValid&&aimValid;if(frameAimValid){memcpy(frameAimClip,aimClip,sizeof(frameAimClip));UndoUiLetterbox(aspect,frameAimClip);}ReleaseSRWLockExclusive(&frameLock);}
inline void Clear(){AcquireSRWLockExclusive(&frameLock);frameValid=false;ReleaseSRWLockExclusive(&frameLock);}
using DrawFn=int(*)(void*,void*,void*);inline DrawFn imageOriginal,textOriginal,movieOriginal,dynamicOriginal,graphOriginal;
inline int Draw(DrawFn fn,void* object,void* context,void* item){
 bool saved=drawActive;float clip[16];memcpy(clip,drawClip,sizeof(clip));
 bool reticle=CrosshairUi::IsReticle(object);
 AcquireSRWLockShared(&frameLock);drawActive=frameValid;if(drawActive){memcpy(drawClip,reticle&&frameAimValid?frameAimClip:frameClip,sizeof(drawClip));if(reticle&&frameAimValid)++CrosshairUi::draws;}ReleaseSRWLockShared(&frameLock);
 auto dirty=reinterpret_cast<unsigned*>(static_cast<char*>(context)+0x18);
 if(drawActive){++draws;*dirty|=0x406000;}
 auto result=fn(object,context,item);if(drawActive)*dirty|=0x406000;
 drawActive=saved;memcpy(drawClip,clip,sizeof(clip));return result;
}
inline int Image(void* a,void* b,void* c){return Draw(imageOriginal,a,b,c);}
inline int Text(void* a,void* b,void* c){return Draw(textOriginal,a,b,c);}
inline int Dynamic(void* a,void* b,void* c){return Draw(dynamicOriginal,a,b,c);}
inline int Graph(void* a,void* b,void* c){return Draw(graphOriginal,a,b,c);}
inline int Movie(void* a,void* b,void* c){return Draw(movieOriginal,a,b,c);}
using MaterialFn=uintptr_t(*)(void*,unsigned,void*,void*,const void*);inline MaterialFn materialOriginal;
inline uintptr_t Material(void* state,unsigned stage,void* descriptor,void* packet,const void* values){
 if(!drawActive||stage!=1){auto result=materialOriginal(state,stage,descriptor,packet,values);if(stage==0){WaterDrawPort::Bound(state,descriptor,packet);ShadowBindings::Trace(state,descriptor,packet);}return result;}
 constexpr uintptr_t mask=0xffffffffffffull;
 auto material=*static_cast<uintptr_t*>(packet);auto owner=*reinterpret_cast<uintptr_t*>(material+0x30);
 auto table=*reinterpret_cast<uintptr_t*>(owner+0x168)&mask;
 auto index=*reinterpret_cast<unsigned short*>(static_cast<char*>(descriptor)+2);
 auto shader=*reinterpret_cast<void**>(table+index*8);unsigned row=0;
 {std::shared_lock guard(shaderLock);auto it=shaderRows.find(shader);if(it!=shaderRows.end())row=it->second;}
 if(!row){++misses;{std::unique_lock guard(shaderLock);auto it=shaderHashes.find(shader);if(it!=shaderHashes.end()&&unknownDraws.size()<64){auto& n=unknownDraws[it->second];if(n++==0){fprintf(logFile,"DX12 UI unmatched vertex hash=%016llx\n",it->second);fflush(logFile);}}}return materialOriginal(state,stage,descriptor,packet,values);}
 auto mapIndex=*reinterpret_cast<unsigned short*>(static_cast<char*>(descriptor)+0xa);
 auto maps=*reinterpret_cast<uintptr_t*>(owner+0xc0)&mask;auto list=maps+mapIndex*16;
 auto tagged=*reinterpret_cast<uintptr_t*>(list);unsigned inlineCount=unsigned(tagged>>56);
 unsigned count=inlineCount?inlineCount-1:*reinterpret_cast<unsigned*>(list+8);
 // DX12 copies high-byte dword counts (852b0), unlike DX11's bit-22 byte count.
 // Normalize the size while retaining the two verified 11-bit byte offsets.
 uint32_t entries[128]{};auto native=reinterpret_cast<const uint32_t*>(tagged&mask);
 if(count<=128)for(unsigned i=0;i<count;++i)entries[i]=(native[i]&0x3fffff)|((native[i]>>24)*4<<22);
 alignas(16) unsigned char corrected[0x810]{};
 if(count>128||!PatchUiMaterialValues(entries,count,row,drawClip,values,corrected,sizeof(corrected))){++rejected;return materialOriginal(state,stage,descriptor,packet,values);}
 ++materials;return materialOriginal(state,stage,descriptor,packet,corrected);
}
inline void Report(){fprintf(logFile,"DX12 UI F1=%u draws=%llu materials=%llu misses=%llu rejected=%llu\n",enabled.load(),draws.load(),materials.load(),misses.load(),rejected.load());fprintf(logFile,"Crosshair renderersTagged=%llu aimDraws=%llu\n",CrosshairUi::tagged.load(),CrosshairUi::draws.load());}
}
