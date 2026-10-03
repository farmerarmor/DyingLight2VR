#pragma once
namespace MapPanel {
inline std::atomic<unsigned> visible{};
using Fn=uintptr_t(*)(void*,void*);
inline Fn realShow3d,realHide3d,realShow2d,realHide2d,realShowTabs,realHideTabs;
inline uintptr_t Show3d(void* a,void* b){visible.fetch_or(1);return realShow3d(a,b);}
inline uintptr_t Hide3d(void* a,void* b){auto r=realHide3d(a,b);visible.fetch_and(~1u);return r;}
inline uintptr_t Show2d(void* a,void* b){visible.fetch_or(2);return realShow2d(a,b);}
inline uintptr_t Hide2d(void* a,void* b){auto r=realHide2d(a,b);visible.fetch_and(~2u);return r;}
// Parent tab container stays open while its individual pages are replaced.
inline uintptr_t ShowTabs(void* a,void* b){visible.fetch_or(4);return realShowTabs(a,b);}
inline uintptr_t HideTabs(void* a,void* b){auto r=realHideTabs(a,b);visible.fetch_and(~4u);return r;}
inline void Install(){
 auto m=reinterpret_cast<uintptr_t>(GetModuleHandleW(L"gamedll_ph_x64_rwdi.dll"));if(!m)return;
 struct Hook{uintptr_t rva;const char* bytes;Fn fn;void** original;};
 Hook hooks[]={
 {0x17f5b50,"\x48\x89\x54\x24\x10\x48\x89\x4c\x24\x08\x55\x53\x56\x57\x41\x54",&ShowTabs,reinterpret_cast<void**>(&realShowTabs)},
 {0x17d5990,"\x40\x53\x48\x83\xec\x20\x48\x8b\xd9\xe8\xa2\x6c\x70\x00\x80\x3d",&HideTabs,reinterpret_cast<void**>(&realHideTabs)},
 {0x17ed180,"\x48\x8b\xc4\x48\x89\x58\x10\x55\x56\x57\x41\x54\x41\x55\x41\x56",&Show3d,reinterpret_cast<void**>(&realShow3d)},
 {0x17d23e0,"\x48\x89\x5c\x24\x10\x57\x48\x83\xec\x20\x48\x8b\xf9\xe8\x2e\x85",&Hide3d,reinterpret_cast<void**>(&realHide3d)},
 {0x17f2780,"\x48\x8b\xc4\x48\x89\x58\x18\x48\x89\x70\x20\x55\x57\x41\x56\x48",&Show2d,reinterpret_cast<void**>(&realShow2d)},
 {0x17d3c00,"\x48\x89\x5c\x24\x10\x48\x89\x74\x24\x18\x57\x48\x83\xec\x20\x48",&Hide2d,reinterpret_cast<void**>(&realHide2d)},
 };
 for(auto& h:hooks)if(memcmp(reinterpret_cast<void*>(m+h.rva),h.bytes,16)){fprintf(logFile,"Menu/map panel lifecycle signature mismatch\n");return;}
 unsigned made=0;
 for(auto& h:hooks){if(MH_CreateHook(reinterpret_cast<void*>(m+h.rva),reinterpret_cast<void*>(h.fn),h.original)!=MH_OK){for(unsigned i=0;i<made;++i)MH_RemoveHook(reinterpret_cast<void*>(m+hooks[i].rva));fprintf(logFile,"Menu/map panel lifecycle hook failed\n");return;}++made;}
 fprintf(logFile,"Menu/map panel lifecycle hooks installed; fixed local-space panel\n");
}
}
