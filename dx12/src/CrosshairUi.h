#pragma once
#include <unordered_set>
#include <shared_mutex>
namespace CrosshairUi {
inline std::unordered_set<void*> renderers;
inline std::shared_mutex rendererLock;
inline uintptr_t dataVtable{};
inline std::atomic<uint64_t> tagged{},draws{};
using UpdateFn=void(*)(void*);inline UpdateFn realUpdate,realDestroy;
inline bool Belongs(uintptr_t element){
 // GetActualDataContext (a12da0) and CElement::GetWorldMatrix (a1cd50)
 // establish the resolved context and parent layouts. Walk only live owners
 // during renderer update; render-thread draws never dereference GUI objects.
 for(unsigned depth=0;element&&depth<32;++depth){
  auto p=reinterpret_cast<const unsigned char*>(element);
  if(*reinterpret_cast<const uintptr_t*>(p+0x1a0)){
   auto context=*reinterpret_cast<const uintptr_t*>(p+0x1a8);
   if(context&&*reinterpret_cast<const uintptr_t*>(context)==dataVtable)return true;
  }
  auto parent=*reinterpret_cast<const uintptr_t*>(p+0x90);if(parent==element)break;element=parent;
 }
 return false;
}
inline void Update(void* renderer){
 realUpdate(renderer);
 // Match the owner's two native forms in CElementRenderer::Update exactly.
 auto source=*reinterpret_cast<uintptr_t*>(static_cast<char*>(renderer)+0x18);uintptr_t element=0;
 if(source){auto vt=*reinterpret_cast<uintptr_t*>(source);bool component=reinterpret_cast<bool(*)(void*)>(*reinterpret_cast<uintptr_t*>(vt+0x10))(reinterpret_cast<void*>(source));element=component?*reinterpret_cast<uintptr_t*>(source+8):source-0x88;}
 bool isReticle=Belongs(element);
 std::unique_lock guard(rendererLock);
 if(isReticle){if(renderers.insert(renderer).second)++tagged;}else renderers.erase(renderer);
}
inline void Destroy(void* renderer){{std::unique_lock guard(rendererLock);renderers.erase(renderer);}realDestroy(renderer);}
inline bool IsReticle(void* renderer){std::shared_lock guard(rendererLock);return renderers.find(renderer)!=renderers.end();}
inline void Install(){
 auto game=reinterpret_cast<uintptr_t>(GetModuleHandleW(L"gamedll_ph_x64_rwdi.dll"));if(!game)return;
 dataVtable=game+0x28e8150;
 struct Hook{uintptr_t rva;const char* signature;UpdateFn fn;void** original;};
 Hook hooks[]={
  {0xa9eca0,"\x48\x89\x5c\x24\x10\x56\x48\x83\xec\x20",&Update,reinterpret_cast<void**>(&realUpdate)},
  {0x8cdb50,"\x48\x89\x5c\x24\x08\x57\x48\x83\xec\x20",&Destroy,reinterpret_cast<void**>(&realDestroy)}
 };
 if(*reinterpret_cast<uintptr_t*>(dataVtable+0x2f8)!=game+0x169b280||
    memcmp(reinterpret_cast<void*>(engine+0xa12da0),"\x48\x83\xb9\xa0\x01\x00\x00\x00",8)){
  fprintf(logFile,"Crosshair GUI layout mismatch; skipped\n");return;
 }
 for(auto& h:hooks)if(memcmp(reinterpret_cast<void*>(engine+h.rva),h.signature,10)){fprintf(logFile,"Crosshair renderer signature mismatch; skipped\n");return;}
 unsigned made=0;
 for(auto& h:hooks){if(MH_CreateHook(reinterpret_cast<void*>(engine+h.rva),reinterpret_cast<void*>(h.fn),h.original)!=MH_OK){for(unsigned i=0;i<made;++i)MH_RemoveHook(reinterpret_cast<void*>(engine+hooks[i].rva));fprintf(logFile,"Crosshair renderer hook failed\n");return;}++made;}
 fprintf(logFile,"Crosshair native-aim renderer hooks installed; follows F1\n");
}
}
