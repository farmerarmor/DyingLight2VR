#include <windows.h>
#include <cstdio>
#include <atomic>
#include <cstdarg>
#include <cstring>
#include <intrin.h>
#include "CameraMath.h"
#include "BackbufferCapture.h"
#include "GpuEyePair.h"
#include "XrScreen.h"
#include "TrackedCamera.h"
#include "VerticalLook.h"
#include "UiProjection.h"
#include "UiMaterial.h"
#include "EffectsCapture.h"
#include "MinHook.h"
#include "ShaderHunter.h"
#include "ReflectionProjection.h"
#include "FlashlightProjection.h"
#include "TemporalHistory.h"
#include "DlssCapture.h"
#include "DlssHistory.h"
#include "exports.h"

extern "C" { FARPROC g_targets[kExportCount]{}; }
static HMODULE selfModule;
static FILE* logFile;
static SRWLOCK logLock = SRWLOCK_INIT;
static std::atomic<unsigned long long> frames{}, sets{}, inverseSets{}, gets{};
static std::atomic<int> budgets[6]{8,8,8,8,8,8};
static std::atomic<ULONGLONG> nextSample[6]{};
static std::atomic<unsigned long long> internalFrames{}, commandBuffers{};
static thread_local unsigned renderDepth;
using RenderFn = void(*)(void*);
using SetFn = void(*)(void*, const void*);
using GetFn = void*(*)(void*);
static RenderFn realRender;
static RenderFn realInternal;
using CommandsFn = uintptr_t(*)(void*);
static CommandsFn realCommands;
static SetFn realSet, realInverseSet;
static GetFn realGet;
using ViewSetupFn=uintptr_t(*)(void*);
static ViewSetupFn realViewSetup;
static std::atomic<unsigned long long> viewSetups{};
static std::atomic<bool> cameraShiftEnabled{};
static std::atomic<unsigned long long> cameraShifts{}, cameraShiftRejected{};
static std::atomic<unsigned long long> primaryCopies{};
using CameraCopyFn=uintptr_t(*)(void*,const void*,bool);
using ComponentSetFn=void(*)(void*,const float*,bool);
static CameraCopyFn realCameraCopy;
static ComponentSetFn componentSet;
using RebuildProjectionFn=void(*)(void*,bool);
static RebuildProjectionFn rebuildProjection;
using SceneFn=uintptr_t(*)(void*,unsigned,void*,void*);
using PrepareFn=uintptr_t(*)(void*,void*);
static SceneFn realScene;
static PrepareFn realPrepare;
static PrepareFn realSubmit;
using EndSceneFn=uintptr_t(*)(void*);
using PresentRequestFn=uintptr_t(*)(void*,void*,void*);
static EndSceneFn realEndScene;
static PresentRequestFn realPresentRequest;
using QueuePhaseFn=uintptr_t(*)(unsigned,void*);
static QueuePhaseFn realQueuePhase;
static std::atomic<bool> drainRequested{}, drainConsumed{};
static std::atomic<bool> vrActive{};
static std::atomic<bool> resumePending{true},vrRequested{true};
static std::atomic<bool> pairSafetyBlocked{};
static std::atomic<unsigned long long> resumeAfterScene{};
static std::atomic<ULONGLONG> resumeAfterTime{};
static std::atomic<unsigned long long> pairCompleted{},pairRejected{},pairInvariantFailures{};
static std::atomic<unsigned> runPairs{};
static std::atomic<unsigned long long> gpuPairs{},gpuFailures{};
static thread_local unsigned dispatchEye{},eyeCopies{};
static thread_local uint64_t dispatchPair{};
static std::atomic<unsigned long long> pairElapsedTotal{},pairElapsedMax{};

static std::atomic<bool> drainSupported{};
static uintptr_t QueuePhase(unsigned phase,void* data);

static std::atomic<uintptr_t> rendererBase{};

static unsigned long long eventSequence{}; // Protected by captureLock.

static std::atomic<unsigned long long> sceneCalls{}, prepareCalls{};
static std::atomic<unsigned long long> cachedSceneSubmits{},sceneSuspensions{};
static thread_local unsigned long long currentScene{};
static thread_local uintptr_t preparedCamera{};
static thread_local bool stereoRepeat{};
static thread_local uintptr_t trackedVisibilityCamera{};
static thread_local uintptr_t visibilitySetupLevel{};
static std::atomic<bool> trackedCullingEnabled{true},skipIntermediatePresents{true};
static std::atomic<bool> normalSceneAsLeft{true};
static std::atomic<bool> uiCorrection{true},hideGui{false};
static std::atomic<int> guiReadBudget{48};
static std::atomic<unsigned long long> guiReads{},guiHidden{};
static GetFn realGuiCombined,realGuiProjection;
static std::atomic<unsigned long long> uiRejected{};
static thread_local bool uiEyeValid{};
static thread_local unsigned effectsEye{};
static thread_local float uiClip[16];
using GuiCollectFn=void(*)(void*,void*,bool);
static GuiCollectFn realGuiCollect;
// Publish once around synchronous scene submission, whose native job barrier
// completes all drawing before return. Workers copy this under the short lock.
static SRWLOCK uiFrameLock=SRWLOCK_INIT;
static bool uiFrameValid{};
static float uiFrameClip[16];
static thread_local float uiOutputAspect{1.f};
static thread_local bool uiDrawActive{};
static thread_local float uiDrawClip[16];
using GuiDrawFn=int(*)(void*,void*,void*);
static GuiDrawFn realGuiImageDraw,realGuiTextDraw,realGuiMovieDraw;
using UploadConstantsFn=uintptr_t(*)(void*,unsigned,const void*,unsigned);
static UploadConstantsFn realUploadConstants;
static std::atomic<unsigned long long> uiImageDraws{},uiTextDraws{},uiConstantUploads{},uiActiveDraws{};
static std::atomic<unsigned long long> normalLeftPairs{},normalLeftRejected{};
static std::atomic<unsigned long long> auxiliaryRedirects{},presentsSkipped{},presentsForwarded{},presentMicroseconds{};
static std::atomic<bool> bypassMainVisibilityInput{true};
static std::atomic<unsigned long long> mainVisibilityCalls{},mainVisibilityInputsBypassed{},worldVisibilityCalls{},worldVisibilityInputsBypassed{};
using MainVisibilityFn=uintptr_t(*)(void*,unsigned,const void*,const void*);
using WorldVisibilityFn=uintptr_t(*)(void*,const void*,const void*);
static MainVisibilityFn realMainVisibility;
static WorldVisibilityFn realWorldVisibility;
using BaseCameraCopyFn=uintptr_t(*)(void*,const void*);
static BaseCameraCopyFn realBaseCameraCopy;
using DxgiPresentFn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);
static DxgiPresentFn realDxgiPresent;
static thread_local IDXGISwapChain* dispatchedSwap{};
static std::atomic<unsigned long long> visibilityRedirects{};
using VisibilityCameraFn=void*(*)(void*);
static VisibilityCameraFn realVisibilityCamera;
struct SceneInput {
    void* game{}; void* a{}; void* b{}; uintptr_t camera{};
    unsigned counter{},token{}; bool valid{},normalLeft{};
    TrackedEye eyes[2]{};
    alignas(16) float inverse[12]{},projection[16]{},frustum[12]{};
    alignas(16) float rightInverse[12]{},rightProjection[16]{};
};
static TrackingOrigin trackingOrigin;
static std::atomic<bool> recenterRequested{true};
static std::atomic<unsigned long long> trackedPairs{};
static SceneInput lastScene; // Protected by captureLock.
using ResetLevelFn=void(*)(void*);
using RendererEnterFn=void(*)(void*,const char*);
using RendererLeaveFn=void(*)(void*);
static ResetLevelFn resetLevel;
static RendererEnterFn rendererEnter;
static RendererLeaveFn rendererLeave;



static void Log(const char* fmt, ...) {
    AcquireSRWLockExclusive(&logLock);
    if (logFile) {
        fprintf(logFile, "%llu tid=%lu ", GetTickCount64(), GetCurrentThreadId());
        va_list args; va_start(args, fmt); vfprintf(logFile, fmt, args); va_end(args);
        fputc('\n', logFile); fflush(logFile);
    }
    ReleaseSRWLockExclusive(&logLock);
}
static std::atomic<bool> lockVerticalLook{true};
static std::atomic<unsigned long long> verticalInputsBlocked{};
using InputActionFn=void(*)(void*,void*,float,bool,bool);
static InputActionFn realInputAction;
static void InputAction(void* binding,void* receivers,float value,bool source,bool repeat) {
    auto action=*static_cast<const uint32_t*>(binding);
    auto flags=*(static_cast<const uint8_t*>(binding)+0x10);
    float filtered=VerticalLook::Filter(action,flags,value,lockVerticalLook.load(),vrActive.load());
    if(filtered!=value)++verticalInputsBlocked;
    realInputAction(binding,receivers,filtered,source,repeat);
}
static bool InstallVerticalLookHook() {
    auto game=reinterpret_cast<uintptr_t>(GetModuleHandleW(L"gamedll_ph_x64_rwdi.dll"));
    auto engine=reinterpret_cast<uintptr_t>(GetModuleHandleW(L"engine_x64_rwdi.dll"));
    if(!game || !engine)return false;
    const unsigned char expected[]={0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x30};
    if(memcmp(reinterpret_cast<void*>(game+0x1ec1700),expected,sizeof(expected))) {Log("vertical look hook signature mismatch");return true;}
    struct Action {uintptr_t instruction,name;unsigned id;const char* label;};
    const Action actions[]={
        {0xfd3f85,0x1a06a58,0x5e,"_ACTION_LOOK_UP"},
        {0xfd3fcc,0x1a06a68,0x5f,"_ACTION_LOOK_DOWN"},
        {0xfd4b9c,0x1a06e48,0x89,"_ACTION_ROTATE_UP"},
        {0xfd4be5,0x1a06e60,0x8a,"_ACTION_ROTATE_DOWN"}};
    for(auto a:actions){auto code=reinterpret_cast<const unsigned char*>(engine+a.instruction);
        if(code[0]!=0x48 || code[1]!=0xc7 || code[2]!=5 || *reinterpret_cast<const unsigned*>(code+7)!=a.id ||
           strcmp(reinterpret_cast<const char*>(engine+a.name),a.label)){Log("vertical look action ID mismatch");return true;}}
    auto target=reinterpret_cast<void*>(game+0x1ec1700);
    auto result=MH_CreateHook(target,reinterpret_cast<void*>(&InputAction),reinterpret_cast<void**>(&realInputAction));
    if(result==MH_OK){result=MH_EnableHook(target);if(result!=MH_OK)MH_RemoveHook(target);}
    Log("vertical look hook status=%d LockVerticalLook=%u",result,lockVerticalLook.load());return true;
}
static bool Trace(const char* kind, void* object, unsigned category=0) {
    auto now=GetTickCount64(); auto next=nextSample[category].load();
    if (now<next || !nextSample[category].compare_exchange_strong(next,now+200)) return false;
    auto& budget=budgets[category];
    int remaining = budget.load();
    while (remaining > 0 && !budget.compare_exchange_weak(remaining, remaining - 1)) {}
    if (remaining <= 0) return false;
    void* stack[12]{};
    const USHORT count = CaptureStackBackTrace(1, 12, stack, nullptr);
    char line[2600]{}; size_t used = 0;
    for (USHORT i = 0; i < count && used < sizeof(line)-300; ++i) {
        HMODULE module{}; char path[MAX_PATH]{};
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCSTR>(stack[i]), &module);
        if (module) GetModuleFileNameA(module, path, MAX_PATH);
        const char* base = strrchr(path, '\\'); base = base ? base+1 : path;
        used += _snprintf_s(line+used, sizeof(line)-used, _TRUNCATE, "%s+0x%llx;", base,
                  reinterpret_cast<uintptr_t>(stack[i])-reinterpret_cast<uintptr_t>(module));
    }
    Log("event=%s object=%p renderDepth=%u renderCalls=%llu internal=%llu commandBuffers=%llu stack=%s", kind, object,
        renderDepth, frames.load(), internalFrames.load(),commandBuffers.load(),line);
    return true;
}
static void Render(void* object) {
    ++frames; ++renderDepth;
    Trace("render-enter", object);
    realRender(object);
    --renderDepth;
}
static void CaptureBoundary(unsigned long long frame);
static void Internal(void* object) { auto frame=++internalFrames; CaptureBoundary(frame); ++renderDepth; Trace("internal-render",object,4); realInternal(object); --renderDepth; }
static void Set(void* object, const void* matrix) { ++sets; Trace("camera-set", object,1); realSet(object, matrix); }
static void InverseSet(void* object, const void* matrix) { ++inverseSets; Trace("camera-inverse-set", object,2); realInverseSet(object, matrix); }
static void* Get(void* object) { ++gets; void* camera = realGet(object); Trace("view-camera", camera,3); return camera; }
// This observes the engine-owned command buffer. It neither calls graphics APIs
// nor modifies the buffer. All uncertain memory reads are bounded and guarded.
struct CommandSample { unsigned count{}, present{}, invalid{}, types[66]{}; uintptr_t owner{}, swap{}; };
static CommandSample SampleCommands(void* object) {
    CommandSample result{};
    __try {
        auto cursor=*reinterpret_cast<const unsigned char**>(object);
        size_t bytes=0;
        while(result.count<65536 && bytes<32*1024*1024) {
            auto size=*reinterpret_cast<const unsigned*>(cursor);
            auto type=*reinterpret_cast<const unsigned*>(cursor+4);
            if (!type) return result;
            if(size<8 || size>1024*1024 || type>65) { result.invalid=1; return result; }
            ++result.count;
            ++result.types[type];
            if(type==51 && size>=16) {
                ++result.present;
                result.owner=*reinterpret_cast<const uintptr_t*>(cursor+8);
                if(result.owner) result.swap=*reinterpret_cast<const uintptr_t*>(result.owner+0x120);
            }
            cursor+=size; bytes+=size;
        }
        result.invalid=2; // Scan limit, not necessarily invalid game data.
    } __except(EXCEPTION_EXECUTE_HANDLER) { result.invalid=3; }
    return result;
}
struct BufferRecord { unsigned long long frame{}; DWORD thread{}; unsigned depth{}; CommandSample sample{}; unsigned long long enter{}, leave{}; };
static SRWLOCK captureLock=SRWLOCK_INIT;
static BufferRecord records[768]{};
static unsigned recordCount{}, dropped{}, intervals{};
static unsigned long long captureFrame{};
static ULONGLONG captureDeadline{};
static bool armed{}, active{}, ready{}, timedOut{};
struct CameraSnapshot {
    unsigned long long renderId{};
    DWORD thread{};
    uintptr_t level{}, source[2]{};
    unsigned status{}, guard[2]{}, gameCounter{}, viewCount{};
    float cached[100]{}, camera[2][100]{};
};
static CameraSnapshot cameraRecords[48]{};
static unsigned cameraCount{}, cameraDropped{};
static uintptr_t engineBase{};
struct StageRecord {
    unsigned stage{}, counter{}, token{}, status{}, queueBank{~0u};
    DWORD thread{};
    unsigned long long frame{}, scene{}, copies{}, buffers{}, sequence{};
    uintptr_t renderer{}, table{}, targets[7]{}, camera[2]{}, prepared{}, graph{}, graphTable{}, graphTargets[4]{};
};
static StageRecord stageRecords[256]{};
static unsigned stageCount{}, stageDropped{};
static constexpr unsigned rendererSlots[]={0x80,0x88,0x90,0x1a0,0x280,0x288,0x3d8};
static StageRecord ReadStage(unsigned stage,void* game,unsigned token,void* prepared) {
    StageRecord r{}; r.stage=stage; r.token=token; r.thread=GetCurrentThreadId();
    r.sequence=++eventSequence; r.frame=captureFrame; r.scene=currentScene; r.copies=primaryCopies.load(); r.buffers=commandBuffers.load();
    __try {
        if(auto backend=rendererBase.load()) r.queueBank=*reinterpret_cast<unsigned*>(backend+0xe9e080);
        if(!game) game=*reinterpret_cast<void**>(engineBase+0x272f3f8);
        if(game) r.counter=*reinterpret_cast<unsigned*>(static_cast<unsigned char*>(game)+0x110);
        r.renderer=*reinterpret_cast<uintptr_t*>(engineBase+0x28b2b28);
        if(r.renderer) {
            r.table=*reinterpret_cast<uintptr_t*>(r.renderer);
            for(unsigned i=0;i<7;++i) r.targets[i]=*reinterpret_cast<uintptr_t*>(r.table+rendererSlots[i]);
        }
        r.prepared=reinterpret_cast<uintptr_t>(prepared);
        if(prepared) {
            r.graph=*reinterpret_cast<uintptr_t*>(static_cast<unsigned char*>(prepared)+0x18);
            if(r.graph) {
                r.graphTable=*reinterpret_cast<uintptr_t*>(r.graph);
                constexpr unsigned slots[]={8,0x10,0x48,0x50};
                for(unsigned i=0;i<4;++i) r.graphTargets[i]=*reinterpret_cast<uintptr_t*>(r.graphTable+slots[i]);
            }
            r.camera[0]=*reinterpret_cast<uintptr_t*>(static_cast<unsigned char*>(prepared)+0xd8);
            r.camera[1]=*reinterpret_cast<uintptr_t*>(static_cast<unsigned char*>(prepared)+0xf0);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { r.status=1; }
    return r;
}
static void RecordStage(unsigned stage,void* game,unsigned token=0,void* prepared=nullptr) {
    AcquireSRWLockExclusive(&captureLock);
    if(active) {
        if(stageCount<256) stageRecords[stageCount++]=ReadStage(stage,game,token,prepared);
        else ++stageDropped;
    }
    ReleaseSRWLockExclusive(&captureLock);
}
static SceneInput SnapshotScene(void* game,unsigned token,void* a,void* b) {
    SceneInput r{};
    __try {
        r.game=game; r.token=token; r.a=a; r.b=b; r.camera=preparedCamera;
        r.counter=*reinterpret_cast<unsigned*>(static_cast<unsigned char*>(game)+0x110);
        if(r.camera && *reinterpret_cast<uintptr_t*>(r.camera)==engineBase+0x18a2c68) {
            memcpy(r.inverse,reinterpret_cast<void*>(r.camera+0x40),sizeof(r.inverse));
            memcpy(r.projection,reinterpret_cast<void*>(r.camera+0x80),sizeof(r.projection));
            memcpy(r.frustum,reinterpret_cast<void*>(r.camera+0x1a0),sizeof(r.frustum));
            alignas(16) float check[12];
            r.valid=OffsetCameraRight(r.inverse,0.064f,check);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { r.valid=false; }
    return r;
}
static void RememberPreparedCamera(void* data) {
    __try { preparedCamera=*reinterpret_cast<uintptr_t*>(static_cast<unsigned char*>(data)+0xd8); }
    __except(EXCEPTION_EXECUTE_HANDLER) { preparedCamera=0; }
}
static void ApplyEyeFrustum(uintptr_t camera,const TrackedEye& eye);
static void RestoreCamera(const SceneInput& input) {
    memcpy(reinterpret_cast<void*>(input.camera+0x1a0),input.frustum,sizeof(input.frustum));
    memcpy(reinterpret_cast<void*>(input.camera+0x80),input.projection,sizeof(input.projection));
    componentSet(reinterpret_cast<void*>(input.camera),input.inverse,false);
}
static bool BuildTrackedCameras(const SceneInput& input,const TrackedEye* eyes,float* left,float* leftProjection,float* right,float* rightProjection) {
    if(recenterRequested.exchange(false) || !trackingOrigin.valid) {
        memcpy(trackingOrigin.q,eyes[0].quaternion,sizeof(trackingOrigin.q));
        for(int axis=0;axis<3;++axis)trackingOrigin.p[axis]=(eyes[0].position[axis]+eyes[1].position[axis])*0.5f;
        trackingOrigin.valid=true;Log("tracking recentered counter=%u",input.counter);
    }
    return std::isfinite(input.frustum[4]) && input.frustum[4]>0 &&
        MakeTrackedCamera(input.inverse,input.projection,eyes[0],trackingOrigin,left,leftProjection) &&
        MakeTrackedCamera(input.inverse,input.projection,eyes[1],trackingOrigin,right,rightProjection);
}
static void GuiCollect(void* manager,void* data,bool background) {
    auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    if(caller==engineBase+0x82cc82 && hideGui.load()) {
        // Exercise the engine's existing rendering-disabled branch. Preserve
        // unrelated flag bits and restore immediately after native collection.
        auto flags=static_cast<unsigned char*>(manager)+0x55c;
        auto enabled=*flags&4;*flags&=~4;
        realGuiCollect(manager,data,background);
        *flags=(*flags&~4)|enabled;
        ++guiHidden;return;
    }
    // GUI layout has already letterboxed a 16:9 canvas into the eye-sized
    // output. Read its aspect only; the ineffective camera mutation is removed.
    if(caller==engineBase+0x82cc82 && uiEyeValid) {
        auto camera=*reinterpret_cast<uintptr_t*>(static_cast<unsigned char*>(manager)+0x478);
        if(camera && *reinterpret_cast<uintptr_t*>(camera)==engineBase+0x193c6a0) {
            auto p=reinterpret_cast<const float*>(camera+0x80);
            float aspect=p[5]/p[0];
            if(std::isfinite(aspect) && aspect>0.1f && aspect<10.f)uiOutputAspect=aspect;
        }
    }
    realGuiCollect(manager,data,background);
}
static int DrawGui(GuiDrawFn original,void* object,void* context,void* item,bool text) {
    bool previous=uiDrawActive;
    float saved[16];memcpy(saved,uiDrawClip,sizeof(saved));
    AcquireSRWLockShared(&uiFrameLock);
    uiDrawActive=uiFrameValid;
    if(uiDrawActive)memcpy(uiDrawClip,uiFrameClip,sizeof(uiDrawClip));
    ReleaseSRWLockShared(&uiFrameLock);
    if(text)++uiTextDraws;else ++uiImageDraws;
    if(uiDrawActive) {
        auto n=++uiActiveDraws;
        if(n<=6)Log("GUI draw active text=%u viewport=%u,%u camera=%p",text,
            *reinterpret_cast<unsigned*>(static_cast<unsigned char*>(context)+0x2050),
            *reinterpret_cast<unsigned*>(static_cast<unsigned char*>(context)+0x2054),
            *reinterpret_cast<void**>(static_cast<unsigned char*>(context)+0x2030));
    }
    // Force fresh constants at both boundaries so a cached UI matrix cannot
    // leak to the next non-UI draw, or miss the first corrected UI draw.
    auto dirty=reinterpret_cast<unsigned*>(static_cast<unsigned char*>(context)+0x18);
    if(uiDrawActive)*dirty|=0x406000;
    int result=original(object,context,item);
    if(uiDrawActive)*dirty|=0x406000;
    uiDrawActive=previous;memcpy(uiDrawClip,saved,sizeof(saved));
    return result;
}
static int GuiImageDraw(void* object,void* context,void* item) {return DrawGui(realGuiImageDraw,object,context,item,false);}
static int GuiTextDraw(void* object,void* context,void* item) {return DrawGui(realGuiTextDraw,object,context,item,true);}
static std::atomic<unsigned long long> uiMovieDraws{};
static int GuiMovieDraw(void* object,void* context,void* item) {++uiMovieDraws;return DrawGui(realGuiMovieDraw,object,context,item,false);}
static uintptr_t UploadConstants(void* context,unsigned slot,const void* data,unsigned bytes) {
    if(slot==2 && EffectsCapture::eye.load())EffectsCapture::Constants(context,slot,data,bytes,reinterpret_cast<uintptr_t>(_ReturnAddress()),uiDrawActive);
    return realUploadConstants(context,slot,data,bytes);
}
static void TraceGuiRead(void* camera,uintptr_t caller,unsigned offset) {
    if(*static_cast<uintptr_t*>(camera)!=engineBase+0x193c6a0)return;
    ++guiReads;
    int budget=guiReadBudget.load();
    while(budget>0)if(guiReadBudget.compare_exchange_weak(budget,budget-1)) {
        auto m=reinterpret_cast<const float*>(static_cast<unsigned char*>(camera)+offset);
        Log("GUI matrix read callerRva=%llx camera=%p offset=%x scene=%llu eyeValid=%u correction=%u m=%g,%g,%g,%g/%g,%g,%g,%g",
            caller-engineBase,camera,offset,currentScene,uiEyeValid,uiCorrection.load(),m[0],m[1],m[2],m[3],m[4],m[5],m[6],m[7]);
        break;
    }
}
static void* GuiCombined(void* camera) {
    TraceGuiRead(camera,reinterpret_cast<uintptr_t>(_ReturnAddress()),0xc0);
    return realGuiCombined(camera);
}
static void* GuiProjection(void* camera) {
    TraceGuiRead(camera,reinterpret_cast<uintptr_t>(_ReturnAddress()),0x80);
    return realGuiProjection(camera);
}

static std::atomic<unsigned> temporalSceneEye{},temporalSceneKey{},temporalEpoch{1};
// 0 native histories, 1 DLSS viewport isolation only, 2 full camera + texture isolation.
static std::atomic<unsigned> temporalMode{2},temporalLatchedMode{2};
static std::atomic<bool> temporalHooksReady{},temporalEffective{},dlssEffective{},temporalResetRequested{};
static std::atomic<unsigned long long> temporalLookups[2]{},temporalPasses[2]{},dlssCalls[3]{},dlssConstants[3]{};
static std::atomic<unsigned long long> dlssResets[2]{},dlssMatrixFixed{},dlssMatrixFailed{};
static DlssHistory::History dlssHistory;
static std::atomic<uintptr_t> cameraWriteTree{};
static std::atomic<unsigned> cameraWriteMask{};
static std::atomic<unsigned long long> cameraCommits[2]{},cameraIncomplete{};
static TemporalHistory::Bank<0x140,32> cameraHistory;
static TemporalHistory::Bank<0x90,32> externalHistory;
static SRWLOCK historyLock=SRWLOCK_INIT,externalLock=SRWLOCK_INIT;
using HistoryLookupFn=void*(*)(void*,const unsigned*);
static HistoryLookupFn realHistoryLookup;
using ExternalPassFn=uintptr_t(*)(void*,void*);
static ExternalPassFn realExternalPass;
static CommandsFn realDlssEvaluate,realDlssConstants;
static void* HistoryLookup(void* tree,const unsigned* key) {
    void* original=realHistoryLookup(tree,key);
    unsigned eye=temporalSceneEye.load();
    if(!original || eye<1 || eye>2 || *key!=temporalSceneKey.load())return original;
    unsigned writer=TemporalHistory::WriterMask(reinterpret_cast<uintptr_t>(_ReturnAddress())-engineBase);
    if(writer) {
        uintptr_t expected=0,address=reinterpret_cast<uintptr_t>(tree);
        if(cameraWriteTree.compare_exchange_strong(expected,address) || expected==address)cameraWriteMask.fetch_or(writer);
        // Native writes must remain visible to all consumers, including those
        // outside this Scene. Snapshot the completed camera after its barrier.
        return original;
    }
    AcquireSRWLockExclusive(&historyLock);
    auto out=cameraHistory.Get(reinterpret_cast<uintptr_t>(tree),*key,temporalEpoch.load(),eye,original);
    ReleaseSRWLockExclusive(&historyLock);
    if(out){auto n=++temporalLookups[eye-1];if(n<=2)Log("camera history eye=%u key=%u tree=%p original=%p isolated=%p",eye,*key,tree,original,out);return out;}
    return original;
}
static uintptr_t ExternalPass(void* object,void* context) {
    unsigned eye=temporalSceneEye.load();
    if(eye<1 || eye>2)return realExternalPass(object,context);
    // This exact command reads/writes only its previous-camera block +20..af.
    // The original object identity and resource fields remain intact.
    AcquireSRWLockExclusive(&externalLock);
    auto region=static_cast<unsigned char*>(object)+0x20;
    auto saved=externalHistory.Get(reinterpret_cast<uintptr_t>(object),temporalSceneKey.load(),temporalEpoch.load(),eye,region);
    alignas(16) unsigned char original[0x90];
    if(saved){memcpy(original,region,sizeof(original));memcpy(region,saved,sizeof(original));}
    auto result=realExternalPass(object,context);
    if(saved){memcpy(saved,region,sizeof(original));memcpy(region,original,sizeof(original));++temporalPasses[eye-1];}
    ReleaseSRWLockExclusive(&externalLock);return result;
}
static uintptr_t ScopedDlss(void* packet,CommandsFn fn,bool evaluate) {
    unsigned eye=dlssEffective?dispatchEye:0;
    if(eye>2)eye=0;
    if(evaluate)++dlssCalls[eye];else ++dlssConstants[eye];
    auto viewport=reinterpret_cast<unsigned*>(rendererBase.load()+0xea5218);
    unsigned original=*viewport;
    // The native packet functions use this viewport for constants, tags,
    // DLSS options and evaluation. Restore before any unrelated packet.
    unsigned mapped=eye?TemporalHistory::Viewport(original,eye):original;
    if(eye)*viewport=mapped;
    static TemporalHistory::ResetTracker resets[2];
    alignas(16) unsigned char resetPacket[0x1b0];
    bool reset=false;
    if(eye && !evaluate) {
        memcpy(resetPacket,packet,sizeof(resetPacket));
        reset=resets[eye-1].NeedsReset(temporalEpoch.load(),mapped) || *reinterpret_cast<unsigned*>(resetPacket+0x18)!=0;
        if(dlssHistory.Apply(resetPacket,eye,temporalEpoch.load(),mapped,reset))++dlssMatrixFixed;
        else {++dlssMatrixFailed;reset=true;}
        if(reset){++dlssResets[eye-1];*reinterpret_cast<unsigned*>(resetPacket+0x18)=1;}
        packet=resetPacket;
    }
    auto count=evaluate?dlssCalls[eye].load():dlssConstants[eye].load();
    if(count<=3)Log("DLSS %s eye=%u viewport=%u mapped=%u reset=%u",evaluate?"evaluate":"constants",eye,original,mapped,reset);
    if(!evaluate)DlssCapture::Constants(dispatchEye,packet);
    bool capture=evaluate && DlssCapture::Begin(TextureHistory::context,dispatchEye,dispatchPair,packet);
    auto result=fn(packet);
    if(capture)DlssCapture::End(TextureHistory::context,dispatchEye,packet);
    *viewport=original;
    return result;
}
static uintptr_t DlssEvaluate(void* packet){return ScopedDlss(packet,realDlssEvaluate,true);}
static uintptr_t DlssConstants(void* packet){return ScopedDlss(packet,realDlssConstants,false);}
static bool InstallTemporalHooks(uintptr_t backend) {
    for(auto rva:TemporalHistory::writerReturns) {
        auto call=reinterpret_cast<const unsigned char*>(engineBase+rva-5);
        int displacement{};memcpy(&displacement,call+1,4);
        if(call[0]!=0xe8 || engineBase+rva+displacement!=engineBase+0x110fa90) {
            Log("camera writer signature mismatch at %p",call);return false;
        }
    }
    const unsigned char historyLookupExpected[]={0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x41,0x56,0x41,0x57,0x48};
    const unsigned char externalPassExpected[]={0x48,0x8b,0xc4,0x55,0x56,0x48,0x8d,0xa8,0xc8,0xf9,0xff,0xff,0x48,0x81,0xec,0x28};
    const unsigned char dlssEvaluateExpected[]={0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
    const unsigned char dlssConstantsExpected[]={0x48,0x8b,0xc4,0x55,0x53,0x57,0x48,0x8d,0xa8,0xe8,0xfd,0xff,0xff,0x48,0x81,0xec};
    struct Hook {uintptr_t address;const unsigned char* signature;unsigned size;void* hook;void** original;};
    Hook hooks[]={
        {engineBase+0x110fa90,historyLookupExpected,sizeof(historyLookupExpected),(void*)HistoryLookup,(void**)&realHistoryLookup},
        {engineBase+0x116e310,externalPassExpected,sizeof(externalPassExpected),(void*)ExternalPass,(void**)&realExternalPass},
        {backend+0x37300,dlssEvaluateExpected,sizeof(dlssEvaluateExpected),(void*)DlssEvaluate,(void**)&realDlssEvaluate},
        {backend+0x57b20,dlssConstantsExpected,sizeof(dlssConstantsExpected),(void*)DlssConstants,(void**)&realDlssConstants}};
    for(auto h:hooks)if(memcmp((void*)h.address,h.signature,h.size)){Log("temporal hook signature mismatch at %p",(void*)h.address);return false;}
    unsigned created=0;
    for(auto h:hooks){if(MH_CreateHook((void*)h.address,h.hook,h.original)!=MH_OK)break;++created;}
    bool ok=created==std::size(hooks);
    if(ok)for(auto h:hooks)if(MH_EnableHook((void*)h.address)!=MH_OK){ok=false;break;}
    if(!ok)for(unsigned i=0;i<created;++i){MH_DisableHook((void*)hooks[i].address);MH_RemoveHook((void*)hooks[i].address);}
    temporalHooksReady=ok;Log("temporal isolation hooks installed=%u",ok);return ok;
}
static void SuspendSceneVr(const char* reason) {
    // A cached/menu/loading scene is not a permanent renderer failure. Keep
    // the XR session and tracking origin; retry only after a fresh native scene.
    vrActive=false;temporalResetRequested=true;
    resumeAfterScene=sceneCalls.load();resumeAfterTime=GetTickCount64()+100;
    resumePending=vrRequested.load() && !pairSafetyBlocked.load();
    auto n=++sceneSuspensions;
    if(n<=8 || n%100==0)Log("VR scene suspended: %s; autoResume=%u count=%llu",reason,resumePending.load(),n);
}
static uintptr_t Scene(void* game,unsigned token,void* a,void* b) {
    auto previous=currentScene; currentScene=++sceneCalls; preparedCamera=0;
    SceneInput base;
    bool early=false;
    alignas(16) float left[12],leftProjection[16],right[12],rightProjection[16];
    if(!stereoRepeat && normalSceneAsLeft.load() && XrTrackingReady() &&
       vrActive.load()) {
        SceneInput candidate;
        AcquireSRWLockShared(&captureLock);candidate=lastScene;ReleaseSRWLockShared(&captureLock);
        if(candidate.game==game && candidate.camera) {
            preparedCamera=candidate.camera;base=SnapshotScene(game,token,a,b);preparedCamera=0;
            if(base.valid && BeginTrackedPair(base.eyes)) {
                if(BuildTrackedCameras(base,base.eyes,left,leftProjection,right,rightProjection)) {
                    memcpy(base.rightInverse,right,sizeof(right));memcpy(base.rightProjection,rightProjection,sizeof(rightProjection));
                    ApplyEyeFrustum(base.camera,base.eyes[0]);
                    componentSet(reinterpret_cast<void*>(base.camera),left,false);
                    trackedVisibilityCamera=base.camera;stereoRepeat=true;early=true;
                    uiEyeValid=MakeUiClipTransform(base.eyes,0,uiClip);
                    effectsEye=1;
                } else {CancelTrackedPair();++normalLeftRejected;}
            }
        }
    }
    RecordStage(1,game,token);
    static uintptr_t temporalCamera{};
    static unsigned temporalCounter{};
    if(effectsEye==1) {
        unsigned next=temporalHooksReady?temporalMode.load():0;
        bool changed=temporalLatchedMode.exchange(next)!=next;
        temporalEffective=next==2;dlssEffective=next!=0;
        if(temporalResetRequested.exchange(false) || changed)++temporalEpoch;
    } else if(!effectsEye){temporalEffective=false;dlssEffective=false;}
    // Continuity belongs to the tracked pair, including DLSS-only mode.
    // Using camera override coverage here would reset DLSS every eye.
    unsigned eye=dlssEffective?effectsEye:0;
    if(eye && trackedVisibilityCamera) {
        unsigned counter=*reinterpret_cast<unsigned*>(static_cast<unsigned char*>(game)+0x110);
        if(temporalCamera!=trackedVisibilityCamera || counter-temporalCounter>1)++temporalEpoch;
        temporalCamera=trackedVisibilityCamera;temporalCounter=counter;
    } else {temporalCamera=0;++temporalEpoch;}
    cameraWriteTree=0;cameraWriteMask=0;
    temporalSceneKey=token;temporalSceneEye=temporalEffective?eye:0;
    auto result=realScene(game,token,a,b);
    temporalSceneEye=0;
    if(temporalEffective && eye) {
        auto tree=cameraWriteTree.load();
        if(tree && cameraWriteMask.load()==127) {
            auto original=realHistoryLookup(reinterpret_cast<void*>(tree),&token);
            AcquireSRWLockExclusive(&historyLock);
            bool stored=cameraHistory.Store(tree,token,temporalEpoch.load(),eye,original);
            ReleaseSRWLockExclusive(&historyLock);
            if(stored)++cameraCommits[eye-1];
        } else {++cameraIncomplete;temporalResetRequested=true;}
    }
    uiEyeValid=false;
    effectsEye=0;
    RecordStage(4,game,token);
    auto input=SnapshotScene(game,token,a,b);
    if(early) {
        bool valid=input.valid && input.camera==base.camera && input.counter==base.counter;
        for(unsigned i=0;i<12;++i)if(!std::isfinite(input.inverse[i]) || fabsf(input.inverse[i]-left[i])>0.0001f)valid=false;
        for(unsigned i: {0u,2u,5u,6u})if(!std::isfinite(input.projection[i]) || fabsf(input.projection[i]-leftProjection[i])>0.001f)valid=false;
        stereoRepeat=false;trackedVisibilityCamera=0;
        if(valid) {
            base.normalLeft=true;input=base;
        } else {
            RestoreCamera(base);CancelTrackedPair();++normalLeftRejected;
            // Publish the restored native camera, not the rejected tracked one.
            input=SnapshotScene(game,token,a,b);
            SuspendSceneVr("normal-left camera changed");
        }
    }
    AcquireSRWLockExclusive(&captureLock); lastScene=input; ReleaseSRWLockExclusive(&captureLock);
    currentScene=previous;
    return result;
}
static uintptr_t Prepare(void* game,void* data) {
    ++prepareCalls;
    RecordStage(2,game);
    auto result=realPrepare(game,data);
    RememberPreparedCamera(data);
    RecordStage(3,game,0,data);
    return result;
}
static uintptr_t Submit(void* renderer,void* data) {
    // Both native Scene branches converge here: fresh Prepare and the cached
    // menu graph (82edf0) which never calls Prepare. d8 is the original level
    // camera in both; the cache deliberately does not clone this slot.
    if(currentScene && reinterpret_cast<uintptr_t>(_ReturnAddress())==engineBase+0x8359c7) {
        if(!preparedCamera) {
            auto n=++cachedSceneSubmits;
            if(n<=4)Log("cached scene camera recovered at Submit; eye=%u",effectsEye);
        }
        RememberPreparedCamera(data);
    }
    RecordStage(5,nullptr,0,data);
    unsigned effectsCounter=0;
    if(EffectsCapture::state.load()) {
        auto game=*reinterpret_cast<uintptr_t*>(engineBase+0x272f3f8);
        if(game)effectsCounter=*reinterpret_cast<unsigned*>(game+0x110);
    }
    const bool effectsRecording=EffectsCapture::Begin(effectsEye,pairCompleted.load()+1,engineBase,rendererBase.load(),effectsCounter,trackedVisibilityCamera);
    if(effectsRecording)EffectsCapture::Spotlights(data,trackedVisibilityCamera,false);
    AcquireSRWLockExclusive(&uiFrameLock);
    uiFrameValid=uiEyeValid && uiCorrection.load();
    if(uiFrameValid) {
        memcpy(uiFrameClip,uiClip,sizeof(uiFrameClip));
        UndoUiLetterbox(uiOutputAspect,uiFrameClip);
    }
    ReleaseSRWLockExclusive(&uiFrameLock);
    auto result=realSubmit(renderer,data);
    if(effectsRecording){EffectsCapture::Spotlights(data,trackedVisibilityCamera,true);EffectsCapture::End();}
    AcquireSRWLockExclusive(&uiFrameLock);uiFrameValid=false;ReleaseSRWLockExclusive(&uiFrameLock);
    RecordStage(6,nullptr,0,data);
    return result;
}
static uintptr_t EndScene(void* renderer) {
    RecordStage(7,nullptr);
    auto result=realEndScene(renderer);
    RecordStage(8,nullptr);
    return result;
}
static void ApplyEyeFrustum(uintptr_t camera,const TrackedEye& eye) {
    auto f=reinterpret_cast<float*>(camera+0x1a0);
    const float nearPlane=f[4];
    f[0]=nearPlane*tanf(eye.fov[0]);f[1]=nearPlane*tanf(eye.fov[1]);
    f[2]=nearPlane*tanf(eye.fov[3]);f[3]=nearPlane*tanf(eye.fov[2]);
    // Native projection offsets are applied after the frustum matrix is built.
    f[9]=f[10]=f[11]=0;
    rebuildProjection(reinterpret_cast<void*>(camera),false);
}
static void AbortPendingPair() {
    SceneInput input;
    AcquireSRWLockExclusive(&captureLock);input=lastScene;lastScene.normalLeft=false;ReleaseSRWLockExclusive(&captureLock);
    if(input.normalLeft)RestoreCamera(input);
    CancelTrackedPair();
}
static uintptr_t PresentRequest(void* renderer,void* a,void* b) {
    auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    RecordStage(9,nullptr);
    auto result=realPresentRequest(renderer,a,b);
    RecordStage(10,nullptr);
    bool run=false, capturePair=false;
    const bool continuous=vrActive.load();
    AcquireSRWLockExclusive(&captureLock);
    if(drainSupported && !pairSafetyBlocked.load() && caller==engineBase+0x82e200) {
        capturePair=active && !timedOut && GetTickCount64()<captureDeadline && drainRequested.exchange(false) &&
            (continuous || !drainConsumed.exchange(true));
        run=continuous || capturePair;
    }
    ReleaseSRWLockExclusive(&captureLock);
    if(run) {
        drainConsumed.store(true);
        const auto pairStart=GetTickCount64();
        auto reject=[](bool fatal=true) {
            AbortPendingPair();++pairRejected;
            if(fatal) {pairSafetyBlocked=true;resumePending=false;vrActive=false;SetXrScreenEnabled(false);}
            else SuspendSceneVr("scene unavailable or changed");
        };
        auto backend=rendererBase.load();
        auto driver=*reinterpret_cast<uintptr_t*>(engineBase+0x28c3360);
        if(!driver || *reinterpret_cast<uintptr_t*>(driver)!=backend+0x8ea88) {
            reject(); Log("pair-test stopped: live backend identity mismatch"); return result;
        }
        auto game=*reinterpret_cast<uintptr_t*>(engineBase+0x272f3f8);
        const unsigned bankBefore=*reinterpret_cast<unsigned*>(backend+0xe9e080);
        if(!game || bankBefore>1) { reject(); Log("pair-test stopped: invalid game/bank"); return result; }
        const unsigned counterBefore=*reinterpret_cast<unsigned*>(game+0x110);
        SceneInput input;
        AcquireSRWLockExclusive(&captureLock); input=lastScene; ReleaseSRWLockExclusive(&captureLock);
        if(!input.valid || input.game!=reinterpret_cast<void*>(game) || input.counter!=counterBefore || input.a!=a || input.b!=b) {
            reject(false);return result;
        }
        // The native direct-render path runs in engine thread context 2.
        auto contextGlobal=*reinterpret_cast<uintptr_t*>(engineBase+0x180c6f0);
        auto contextManager=contextGlobal?*reinterpret_cast<uintptr_t*>(contextGlobal):0;
        if(!contextManager || reinterpret_cast<uintptr_t>(TlsGetValue(*reinterpret_cast<DWORD*>(contextManager+8)))!=2) {
            reject(); Log("pair-test stopped: unexpected engine thread context"); return result;
        }
        auto level=reinterpret_cast<void*>(input.camera-0x13b0);
        alignas(16) float moved[12];
        if(!OffsetCameraRight(input.inverse,0.064f,moved)) { reject(); return result; }
        const auto buffersBefore=commandBuffers.load();
        if(capturePair) Log("pair-test begin bank=%u counter=%u baseline=0 right=0.064 game-units; continuous=%u",bankBefore,counterBefore,continuous);
        dispatchPair=pairCompleted.load()+1;dispatchEye=1;eyeCopies=0;
        if(input.normalLeft && capturePair)RequestBackbufferCapture(1);
        // Initial drain bootstraps the GPU pair; tracked captures replace it below.
        QueuePhase(2,nullptr);
        dispatchEye=0;
        QueuePhase(0,nullptr);
        QueuePhase(0,nullptr);
        TrackedEye eyes[2];
        bool immersive=false;
        if(input.normalLeft) {
            if(!HasTrackedPair()){reject(false);return result;}
            memcpy(eyes,input.eyes,sizeof(eyes));immersive=true;
        } else immersive=BeginTrackedPair(eyes);
        alignas(16) float leftInverse[12],leftProjection[16],rightProjection[16];
        if(immersive) {
            if(input.normalLeft) {
                memcpy(moved,input.rightInverse,sizeof(moved));memcpy(rightProjection,input.rightProjection,sizeof(rightProjection));
            } else if(!BuildTrackedCameras(input,eyes,leftInverse,leftProjection,moved,rightProjection)) {
                CancelTrackedPair();reject(false);return result;
            }
        }
        rendererEnter(renderer,nullptr);
        stereoRepeat=true;
        trackedVisibilityCamera=immersive?input.camera:0;
        if(immersive && !input.normalLeft) {
            eyeCopies=0;
            resetLevel(level);
            ApplyEyeFrustum(input.camera,eyes[0]);
            componentSet(reinterpret_cast<void*>(input.camera),leftInverse,false);
            uiEyeValid=MakeUiClipTransform(eyes,0,uiClip);
            effectsEye=1;
            Scene(input.game,input.token,input.a,input.b);
            realPresentRequest(renderer,a,b);
            dispatchEye=1;
            if(capturePair)RequestBackbufferCapture(1);
            QueuePhase(2,nullptr);dispatchEye=0;
            QueuePhase(0,nullptr);QueuePhase(0,nullptr);
        }
        if(immersive)ApplyEyeFrustum(input.camera,eyes[1]);
        if(capturePair) Log("rendering second view tracked=%u",immersive);
        resetLevel(level);
        componentSet(reinterpret_cast<void*>(input.camera),moved,false);
        uiEyeValid=immersive && MakeUiClipTransform(eyes,1,uiClip);
        effectsEye=immersive?2:0;
        Scene(input.game,input.token,input.a,input.b);
        auto second=SnapshotScene(input.game,input.token,input.a,input.b);
        bool cameraValid=second.valid && second.camera==input.camera && second.counter==counterBefore;
        for(unsigned i=0;i<12;++i)if(!std::isfinite(second.inverse[i]) || fabsf(second.inverse[i]-moved[i])>0.0001f)cameraValid=false;
        if(immersive)for(unsigned i: {0u,2u,5u,6u})
            if(!std::isfinite(second.projection[i]) || fabsf(second.projection[i]-rightProjection[i])>0.001f)cameraValid=false;
        if(immersive && capturePair) {
            Log("XR eye delta=%.6f,%.6f,%.6f near=%.6f source=%g,%g,%g,%g expected=%g,%g,%g,%g actual=%g,%g,%g,%g",
                eyes[1].position[0]-eyes[0].position[0],eyes[1].position[1]-eyes[0].position[1],eyes[1].position[2]-eyes[0].position[2],input.frustum[4],
                input.projection[0],input.projection[2],input.projection[5],input.projection[6],
                rightProjection[0],rightProjection[2],rightProjection[5],rightProjection[6],
                second.projection[0],second.projection[2],second.projection[5],second.projection[6]);
        }
        if(capturePair) Log("pair-test second scene counter=%u camera=%p valid=%u positions first=%.9g,%.9g,%.9g second=%.9g,%.9g,%.9g",
            second.counter,reinterpret_cast<void*>(second.camera),second.valid,input.inverse[3],input.inverse[7],input.inverse[11],
            second.inverse[3],second.inverse[7],second.inverse[11]);
        // Preserve the engine's end/presentation bookkeeping for both eyes.
        realPresentRequest(renderer,a,b);
        // Drain native bookkeeping even on a transition, but never submit a
        // rejected eye pair to XR. A later valid scene may recover normally.
        if(!cameraValid)CancelTrackedPair();
        dispatchEye=cameraValid?2:0;
        if(capturePair) RequestBackbufferCapture(2);
        QueuePhase(2,nullptr);
        dispatchEye=0;
        QueuePhase(0,nullptr);
        QueuePhase(0,nullptr);
        RestoreCamera(input);
        resetLevel(level);
        rendererLeave(renderer);
        stereoRepeat=false;
        trackedVisibilityCamera=0;
        if(capturePair) Log("pair-test end bankBefore=%u bankAfter=%u counterBefore=%u counterAfter=%u dispatchedBuffers=%llu",
            bankBefore,*reinterpret_cast<unsigned*>(backend+0xe9e080),counterBefore,
            *reinterpret_cast<unsigned*>(game+0x110),commandBuffers.load()-buffersBefore);
        // Verify the camera we actually changed/restored. A newly selected menu
        // camera is a recoverable transition, not failed restoration of this one.
        preparedCamera=input.camera;
        auto restored=SnapshotScene(input.game,input.token,input.a,input.b);
        if(cameraValid && eyeCopies==2) { ++gpuPairs;if(immersive)++trackedPairs; }
        else if(cameraValid) { ++gpuFailures;CancelTrackedPair();SuspendSceneVr("GPU pair unavailable"); }
        bool restoredValid=restored.valid && restored.camera==input.camera && restored.counter==counterBefore &&
            !memcmp(restored.inverse,input.inverse,sizeof(input.inverse)) &&
            !memcmp(restored.projection,input.projection,sizeof(input.projection)) &&
            *reinterpret_cast<unsigned*>(backend+0xe9e080)==bankBefore;
        if(!restoredValid) {
            ++pairInvariantFailures;pairSafetyBlocked=true;resumePending=false;
            vrActive=false;SetXrScreenEnabled(false);Log("pair safety stop: native camera/counter/queue restoration failed");
        } else if(!cameraValid) {++pairInvariantFailures;SuspendSceneVr("effective right camera changed");}
        else if(eyeCopies==2) {++pairCompleted;if(input.normalLeft)++normalLeftPairs;}
        ++runPairs;
        const auto elapsed=GetTickCount64()-pairStart;
        pairElapsedTotal.fetch_add(elapsed);
        auto maximum=pairElapsedMax.load();
        while(elapsed>maximum && !pairElapsedMax.compare_exchange_weak(maximum,elapsed)) {}
    } else if(caller==engineBase+0x82e200) AbortPendingPair();
    return result;
}
static uintptr_t QueuePhase(unsigned phase,void* data) {
    // Only the queue execution/switch phases are relevant; annotation phases
    // 3/4 can occur many times per scene and remain untouched.
    if(phase<=2) RecordStage(11,nullptr,phase);
    bool gpuRecording=phase==2 && GpuTrace::Begin(dispatchEye,dispatchPair,temporalEffective.load());
    if(phase==2)TextureHistory::Begin(temporalEffective?dispatchEye:0,temporalEpoch.load());
    if(phase==2)FlashlightShadow::Begin(TextureHistory::context,vrActive?dispatchEye:0,dispatchPair);
    auto result=realQueuePhase(phase,data);
    if(phase==2)FlashlightShadow::End();
    if(phase==2)TextureHistory::End();
    if(gpuRecording)GpuTrace::End();
    if(phase<=2) RecordStage(12,nullptr,phase);
    return result;
}
static void LogTarget(unsigned index,unsigned slot,uintptr_t address) {
    HMODULE module{}; char path[MAX_PATH]{};
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(address),&module);
    if(module) GetModuleFileNameA(module,path,MAX_PATH);
    const char* name=strrchr(path,'\\'); name=name?name+1:path;
    Log("stage-target index=%u slot=%x address=%p module=%s rva=%llx",index,slot,reinterpret_cast<void*>(address),name,address-reinterpret_cast<uintptr_t>(module));
}
static uintptr_t CameraCopy(void* output,const void* source,bool copyBase) {
    auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    auto result=realCameraCopy(output,source,copyBase);
    if(caller!=engineBase+0xb4805a) return result;
    ++primaryCopies;
    if(copyBase && cameraShiftEnabled.load()) {
        alignas(16) float moved[12];
        // Exact primary-cache call site and CLevel +0x13b0 constructor vtable.
        if(*reinterpret_cast<uintptr_t*>(output)==engineBase+0x18a2c68 && output!=source &&
           OffsetCameraRight(reinterpret_cast<const float*>(static_cast<unsigned char*>(output)+0x40),0.25f,moved)) {
            // Engine updates view/inverse, derived matrices, and frustum together.
            componentSet(output,moved,false);
            ++cameraShifts;
        } else ++cameraShiftRejected;
    }
    return result;
}
static CameraSnapshot ReadCamera(void* object) {
    CameraSnapshot r{};
    r.thread=GetCurrentThreadId(); r.level=reinterpret_cast<uintptr_t>(object);
    __try {
        auto level=static_cast<const unsigned char*>(object);
        r.guard[0]=*reinterpret_cast<const unsigned*>(level+0xac8);
        r.guard[1]=*reinterpret_cast<const unsigned*>(level+0xad0);
        r.viewCount=*reinterpret_cast<const unsigned*>(level+0x1650+0x280);
        auto game=*reinterpret_cast<const uintptr_t*>(engineBase+0x272f3f8);
        if(game) r.gameCounter=*reinterpret_cast<const unsigned*>(game+0x110);
        memcpy(r.cached,level+0x13b0+0x10,sizeof(r.cached));
        for(unsigned i=0;i<2;++i) {
            r.source[i]=*reinterpret_cast<const uintptr_t*>(level+0x1390+i*0x10);
            if(r.source[i]) memcpy(r.camera[i],reinterpret_cast<const void*>(r.source[i]+0x10),sizeof(r.camera[i]));
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { r.status=1; }
    return r;
}
static uintptr_t MainVisibility(void* manager,unsigned view,const void* camera,const void* input) {
    auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    if(caller==engineBase+0xb3a0e7 && view==20 && stereoRepeat && trackedVisibilityCamera) {
        ++mainVisibilityCalls;
        if(input && bypassMainVisibilityInput.load()) {
            auto n=++mainVisibilityInputsBypassed;
            if(n<=4)Log("main visibility supplied input bypassed=%p camera=%p pair=%llu",input,camera,dispatchPair);
            input=nullptr;
        }
    }
    return realMainVisibility(manager,view,camera,input);
}
static uintptr_t WorldVisibility(void* world,const void* camera,const void* input) {
    auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    if(caller==engineBase+0xb39ec5 && stereoRepeat && reinterpret_cast<uintptr_t>(camera)==trackedVisibilityCamera) {
        ++worldVisibilityCalls;
        if(input && bypassMainVisibilityInput.load()) {
            auto n=++worldVisibilityInputsBypassed;
            if(n<=4)Log("world visibility supplied input bypassed=%p camera=%p pair=%llu",input,camera,dispatchPair);
            input=nullptr;
        }
    }
    return realWorldVisibility(world,camera,input);
}
static uintptr_t BaseCameraCopy(void* output,const void* source) {
    auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    if(caller==engineBase+0xb39ad8 && trackedCullingEnabled.load() && stereoRepeat && trackedVisibilityCamera &&
       visibilitySetupLevel+0x13b0==trackedVisibilityCamera) {
        source=reinterpret_cast<const void*>(trackedVisibilityCamera);
        auto n=++auxiliaryRedirects;
        if(n<=4)Log("auxiliary main view redirected to tracked eye camera=%p pair=%llu",source,dispatchPair);
    }
    return realBaseCameraCopy(output,source);
}
static void* VisibilityCamera(void* wrapper) {
    auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    auto original=realVisibilityCamera(wrapper);
    // b49340 deliberately switches engine context to 1 before this lookup,
    // selecting level+1390 (mouse camera) instead of the tracked cache.
    // Redirect only this main visibility setup for the active stereo level.
    if(caller==engineBase+0xb493e5 && trackedCullingEnabled.load() && stereoRepeat &&
       trackedVisibilityCamera && reinterpret_cast<uintptr_t>(wrapper)+0x30==trackedVisibilityCamera) {
        auto n=++visibilityRedirects;
        if(n<=4)Log("visibility camera redirected source=%p tracked=%p pair=%llu",original,reinterpret_cast<void*>(trackedVisibilityCamera),dispatchPair);
        return reinterpret_cast<void*>(trackedVisibilityCamera);
    }
    return original;
}
static uintptr_t ViewSetup(void* object) {
    ++viewSetups;
    auto previousLevel=visibilitySetupLevel;visibilitySetupLevel=reinterpret_cast<uintptr_t>(object);
    auto result=realViewSetup(object);
    visibilitySetupLevel=previousLevel;
    AcquireSRWLockExclusive(&captureLock);
    if(active) {
        if(cameraCount<48) {
            auto r=ReadCamera(object); r.renderId=captureFrame;
            cameraRecords[cameraCount++]=r;
        } else ++cameraDropped;
    }
    ReleaseSRWLockExclusive(&captureLock);
    return result;
}
static void ArmCapture() {
    AcquireSRWLockExclusive(&captureLock);
    if(!armed && !active && !ready) {
        recordCount=dropped=intervals=cameraCount=cameraDropped=stageCount=stageDropped=0; timedOut=false;
        armed=true; captureDeadline=GetTickCount64()+5000;
        if(vrActive.load() || !drainConsumed.load()) drainRequested.store(true);
    }
    ReleaseSRWLockExclusive(&captureLock);
}
static void CaptureBoundary(unsigned long long frame) {
    AcquireSRWLockExclusive(&captureLock);
    if(armed) { armed=false; active=true; intervals=1; captureFrame=frame; }
    else if(active) {
        if(intervals==3) { active=false; ready=true; }
        else { ++intervals; captureFrame=frame; }
    }
    ReleaseSRWLockExclusive(&captureLock);
}
static void SaveCapture() {
    AcquireSRWLockExclusive(&captureLock);
    if((armed || active) && GetTickCount64()>=captureDeadline) { armed=active=false; ready=true; timedOut=true; }
    if(ready) {
        Log("interval-capture intervals=%u buffers=%u dropped=%u timedOut=%u",intervals,recordCount,dropped,timedOut);
        for(unsigned i=0;i<recordCount;++i) {
            auto& r=records[i]; char histogram[1024]{}; size_t n=0;
            for(unsigned type=1;type<66;++type) if(r.sample.types[type])
                n+=_snprintf_s(histogram+n,sizeof(histogram)-n,_TRUNCATE,"%u:%u,",type,r.sample.types[type]);
            Log("interval-buffer index=%u frame=%llu thread=%lu depth=%u commands=%u presentCommands=%u scanStatus=%u owner=%p swap=%p types=%s enterSeq=%llu leaveSeq=%llu",
                i,r.frame,r.thread,r.depth,r.sample.count,r.sample.present,r.sample.invalid,
                reinterpret_cast<void*>(r.sample.owner),reinterpret_cast<void*>(r.sample.swap),histogram,r.enter,r.leave);
        }
        Log("interval-capture end; frame labels are render-invocation IDs, not proof of simulation identity or stereo");
        Log("camera-capture count=%u dropped=%u",cameraCount,cameraDropped);
        for(unsigned i=0;i<cameraCount;++i) {
            auto& r=cameraRecords[i];
            Log("camera-state index=%u renderId=%llu thread=%lu level=%p status=%u guard0=%u guard1=%u gameRenderCounter=%u unverified1650_280=%u source0=%p source1=%p",
                i,r.renderId,r.thread,reinterpret_cast<void*>(r.level),r.status,r.guard[0],r.guard[1],r.gameCounter,r.viewCount,
                reinterpret_cast<void*>(r.source[0]),reinterpret_cast<void*>(r.source[1]));
            for(unsigned block=0;block<3;++block) {
                auto values=block==0?r.cached:r.camera[block-1];
                for(unsigned row=0;row<25;++row)
                    Log("camera-data index=%u block=%u offset=%x values=%.9g,%.9g,%.9g,%.9g",i,block,0x10+row*16,values[row*4],values[row*4+1],values[row*4+2],values[row*4+3]);
            }
        }
        Log("stage-capture count=%u dropped=%u sceneCalls=%llu prepareCalls=%llu",stageCount,stageDropped,sceneCalls.load(),prepareCalls.load());
        for(unsigned i=0;i<stageCount;++i) {
            auto& r=stageRecords[i];
            Log("stage index=%u kind=%u frame=%llu scene=%llu thread=%lu counter=%u token=%u status=%u copies=%llu buffers=%llu renderer=%p table=%p cameraD8=%p cameraF0=%p sequence=%llu prepared=%p graph=%p graphTable=%p queueBank=%u",
                i,r.stage,r.frame,r.scene,r.thread,r.counter,r.token,r.status,r.copies,r.buffers,
                reinterpret_cast<void*>(r.renderer),reinterpret_cast<void*>(r.table),reinterpret_cast<void*>(r.camera[0]),reinterpret_cast<void*>(r.camera[1]),r.sequence,reinterpret_cast<void*>(r.prepared),reinterpret_cast<void*>(r.graph),reinterpret_cast<void*>(r.graphTable),r.queueBank);
            if(r.graph && (i==0 || r.graphTable!=stageRecords[i-1].graphTable)) {
                constexpr unsigned slots[]={8,0x10,0x48,0x50};
                for(unsigned j=0;j<4;++j) LogTarget(i,0x10000+slots[j],r.graphTargets[j]);
            }
            if(i==0 || r.table!=stageRecords[i-1].table)
                for(unsigned j=0;j<7;++j) LogTarget(i,rendererSlots[j],r.targets[j]);
        }
        ready=false;
    }
    ReleaseSRWLockExclusive(&captureLock);
}
static uintptr_t PurePresentSwap(void* object) {
    __try {
        auto cmd=*reinterpret_cast<const unsigned char**>(object);
        if(*reinterpret_cast<const unsigned*>(cmd)!=16 || *reinterpret_cast<const unsigned*>(cmd+4)!=51 ||
           *reinterpret_cast<const unsigned*>(cmd+20)!=0) return 0;
        auto owner=*reinterpret_cast<const uintptr_t*>(cmd+8);
        return owner?*reinterpret_cast<const uintptr_t*>(owner+0x120):0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static HRESULT STDMETHODCALLTYPE DxgiPresent(IDXGISwapChain* swap,UINT sync,UINT flags) {
    const bool owned=swap==dispatchedSwap && dispatchEye!=0;
    if(owned && dispatchEye==1 && XrTrackingReady() && skipIntermediatePresents.load() && !(flags&DXGI_PRESENT_TEST)) {
        ++presentsSkipped;return S_OK;
    }
    LARGE_INTEGER before{},after{},frequency{};
    if(owned)QueryPerformanceCounter(&before);
    auto result=realDxgiPresent(swap,sync,flags);
    if(owned){
        QueryPerformanceCounter(&after);QueryPerformanceFrequency(&frequency);
        presentMicroseconds.fetch_add((after.QuadPart-before.QuadPart)*1000000/frequency.QuadPart);
        auto n=++presentsForwarded;
        if(n<=4)Log("DXGI Present forwarded eye=%u sync=%u flags=%u hr=%08lx",dispatchEye,sync,flags,result);
    }
    return result;
}
static void EnsurePresentHook(IDXGISwapChain* swap) {
    static bool attempted=false;if(attempted)return;attempted=true;
    ID3D11Device* device{};
    if(SUCCEEDED(swap->GetDevice(__uuidof(ID3D11Device),reinterpret_cast<void**>(&device)))) {
        ID3D11DeviceContext* context{};device->GetImmediateContext(&context);
        if(context){Log("shader hunter draw/dispatch hooks installed=%u",ShaderHunter::Install(context));context->Release();}
        device->Release();
    }
    auto target=(*reinterpret_cast<void***>(swap))[8];
    auto status=MH_CreateHook(target,reinterpret_cast<void*>(&DxgiPresent),reinterpret_cast<void**>(&realDxgiPresent));
    if(status==MH_OK)status=MH_EnableHook(target);
    Log("live swapchain Present hook status=%d target=%p",status,target);
}
static uintptr_t Commands(void* object) {
    ++commandBuffers;
    unsigned index=768; unsigned long long sequence=0;
    uintptr_t captureSwap{};
    AcquireSRWLockExclusive(&captureLock);
    if(active) {
        if(recordCount<768) {
            index=recordCount++; sequence=++eventSequence;
            records[index]={captureFrame,GetCurrentThreadId(),renderDepth,SampleCommands(object),sequence,0};
            const auto& sample=records[index].sample;
            if(!sample.invalid && sample.count==1 && sample.present==1) captureSwap=sample.swap;
        }
        else ++dropped;
    }
    ReleaseSRWLockExclusive(&captureLock);
    // Sample at most eight buffers per capture; never scan every draw/command.
    auto now=GetTickCount64(); auto next=nextSample[5].load();
    if(budgets[5].load()>0 && now>=next && Trace("command-buffer",object,5)) {
        auto sample=SampleCommands(object);
        Log("buffer-sample commands=%u presentCommands=%u scanStatus=%u owner=%p swap=%p",
            sample.count,sample.present,sample.invalid,reinterpret_cast<void*>(sample.owner),reinterpret_cast<void*>(sample.swap));
    }
    // Only a pure presentation buffer qualifies: all earlier queued graphics
    // commands have been dispatched. Never create an extra D3D device.
    if(captureSwap) TryBackbufferCapture(reinterpret_cast<void*>(captureSwap),sequence);
    if(dispatchEye) {
        if(auto swap=PurePresentSwap(object)) {
            dispatchedSwap=reinterpret_cast<IDXGISwapChain*>(swap);
            EnsurePresentHook(dispatchedSwap);
            auto hr=PreserveGpuEye(reinterpret_cast<void*>(swap),dispatchEye,dispatchPair);
            if(SUCCEEDED(hr)) ++eyeCopies;
            else { ++gpuFailures; Log("GPU eye copy failed eye=%u hr=%08lx",dispatchEye,static_cast<unsigned long>(hr)); }
        }
    } else if(!vrActive.load()) PumpXrScreenIdle();
    auto result=realCommands(object);
    dispatchedSwap=nullptr;
    if(index<768) {
        AcquireSRWLockExclusive(&captureLock);
        if(index<recordCount && records[index].enter==sequence) records[index].leave=++eventSequence;
        ReleaseSRWLockExclusive(&captureLock);
    }
    return result;
}
// Exact captured UI shaders: both CB0 position matrices, never generic CB2.
struct UiShaderLayout {unsigned long long hash;unsigned secondRow;};
static constexpr UiShaderLayout uiLayouts[]={
    {0x8694772dce92f52bull,5}, // Captured position/UV-only UI variant.
    {0x058e95ade5042d13ull,10},
    {0x2b32e54f7c62a6acull,9},
    {0x497da4e04bb3fb7dull,6},
    {0x527f4d967ec24472ull,12},
    {0x704417667302d4b1ull,7},
    {0x9448478f948a9f58ull,8},
    {0xaf0a0cd538b86a0full,11},
    {0xb7df7aa8e5342277ull,8},
    {0xd0309677febd91a9ull,10},
    {0xe40185374c2fb58aull,5},
    {0xed329ddaac0bffb7ull,5},
};
struct UiShaderIdentity {void* shader;unsigned secondRow;};
static UiShaderIdentity uiShaders[256]{};
static unsigned uiShaderCount{};
static SRWLOCK uiShaderLock=SRWLOCK_INIT;
using CreateNativeShaderFn=bool(*)(void*,void**,const void*,unsigned,unsigned,void*);
static CreateNativeShaderFn realCreateNativeShader;
static std::atomic<unsigned long long> uiMaterials{},uiMaterialMisses{},uiMaterialRejected{};
static bool CreateNativeShader(void* backend,void** shader,const void* bytecode,unsigned bytes,unsigned stage,void* description) {
    unsigned row=0;
    if(stage==1 && bytes>=32 && bytes<=1024*1024 && !memcmp(bytecode,"DXBC",4)) {
        unsigned long long hash=14695981039346656037ull;
        for(unsigned i=0;i<bytes;++i){hash^=static_cast<const unsigned char*>(bytecode)[i];hash*=1099511628211ull;}
        for(auto layout:uiLayouts)if(layout.hash==hash){row=layout.secondRow;break;}
    }
    std::vector<unsigned char> correctedReflection;
    unsigned reflectionSites=(stage==0 || stage==5)?PatchReflectionProjection(bytecode,bytes,correctedReflection):0;
    std::vector<unsigned char> flashlight;
    bool flashlightFixed=stage==1 && PatchFlashlightProjection(bytecode,bytes,flashlight);
    bool result=realCreateNativeShader(backend,shader,flashlightFixed?flashlight.data():reflectionSites?correctedReflection.data():bytecode,
        flashlightFixed?static_cast<unsigned>(flashlight.size()):bytes,stage,description);
    if(flashlightFixed) {
        Log("flashlight off-center projection correction created=%u",result);
        if(!result)result=realCreateNativeShader(backend,shader,bytecode,bytes,stage,description);
    }
    if(reflectionSites) {
        Log("reflection projection shader hash=%016llx sites=%u created=%u",ShaderHash(bytecode,bytes),reflectionSites,result);
        if(!result)result=realCreateNativeShader(backend,shader,bytecode,bytes,stage,description);
    }
    if(result && shader && *shader) {
        auto native=*static_cast<void**>(*shader);
        bool sharedShadow=FlashlightShadow::Register(native,bytecode,bytes,stage);
        if(sharedShadow)Log("flashlight shared receiver variant created hash=%016llx",ShaderHash(bytecode,bytes));
        EffectsCapture::Register(*shader,bytecode,bytes,stage,native);
        ShaderHunter::RegisterTemporalShader(native,ShaderHash(bytecode,bytes),stage);
    }
    if(result && shader && *shader && stage==1) {
        AcquireSRWLockExclusive(&uiShaderLock);
        unsigned i=0;for(;i<uiShaderCount;++i)if(uiShaders[i].shader==*shader)break;
        if(i<uiShaderCount)uiShaders[i].secondRow=row;
        else if(row && uiShaderCount<256)uiShaders[uiShaderCount++]={*shader,row};
        ReleaseSRWLockExclusive(&uiShaderLock);
        if(row)Log("UI shader registered object=%p secondMatrixRow=%u",*shader,row);
    }
    return result;
}
using MaterialBindFn=uintptr_t(*)(void*,unsigned,void*,void*,const void*);
static MaterialBindFn realMaterialBind;
static uintptr_t MaterialBind(void* state,unsigned stage,void* descriptor,void* packet,const void* values) {
    if((stage==0 || stage==1 || stage==5) && !uiDrawActive && EffectsCapture::eye.load())EffectsCapture::Material(state,stage,descriptor,packet,values,reinterpret_cast<uintptr_t>(_ReturnAddress()),false);
    if(!uiDrawActive || stage!=1)return realMaterialBind(state,stage,descriptor,packet,values);
    constexpr uintptr_t mask=0xffffffffffffull;
    auto material=*static_cast<uintptr_t*>(packet);
    auto owner=*reinterpret_cast<uintptr_t*>(material+0x30);
    auto table=*reinterpret_cast<uintptr_t*>(owner+0x168)&mask;
    unsigned index=*reinterpret_cast<unsigned short*>(static_cast<char*>(descriptor)+2);
    auto shader=*reinterpret_cast<void**>(table+index*8);
    unsigned row=0;
    AcquireSRWLockShared(&uiShaderLock);
    for(unsigned i=0;i<uiShaderCount;++i)if(uiShaders[i].shader==shader){row=uiShaders[i].secondRow;break;}
    ReleaseSRWLockShared(&uiShaderLock);
    if(!row){++uiMaterialMisses;return realMaterialBind(state,stage,descriptor,packet,values);}
    unsigned mapIndex=*reinterpret_cast<unsigned short*>(static_cast<char*>(descriptor)+0xa);
    auto maps=*reinterpret_cast<uintptr_t*>(owner+0xc0)&mask;
    auto list=maps+mapIndex*16;
    auto tagged=*reinterpret_cast<uintptr_t*>(list);
    unsigned inlineCount=static_cast<unsigned>(tagged>>56);
    unsigned count=inlineCount?inlineCount-1:*reinterpret_cast<unsigned*>(list+8);
    alignas(16) unsigned char corrected[0x810]{};
    if(!PatchUiMaterialValues(reinterpret_cast<const uint32_t*>(tagged&mask),count,row,uiDrawClip,values,corrected,sizeof(corrected))) {
        auto n=++uiMaterialRejected;if(n<=8)Log("UI material mapping rejected shader=%p row=%u entries=%u",shader,row,count);
        return realMaterialBind(state,stage,descriptor,packet,values);
    }
    auto n=++uiMaterials;
    if(n<=12)Log("UI material corrected shader=%p row=%u entries=%u",shader,row,count);
    return realMaterialBind(state,stage,descriptor,packet,corrected);
}
static bool InstallCommands() {
    auto module=GetModuleHandleW(L"rd3d11_x64_rwdi.dll");
    if(!module) return false;
    auto backend=module;
    const unsigned char drainExpected[]={0x40,0x53,0x57,0x48,0x83,0xec,0x38,0x32,0xdb,0x85,0xd2,0xf,0x84,0xe8,0x2,0x0};
    if(backend) {
        rendererBase.store(reinterpret_cast<uintptr_t>(backend));
        auto backendBase=reinterpret_cast<uintptr_t>(backend);
        drainSupported=!memcmp(reinterpret_cast<void*>(backendBase+0x552e0),drainExpected,sizeof(drainExpected)) &&
            *reinterpret_cast<uintptr_t*>(backendBase+0x8ea88+0x5b0)==backendBase+0x552e0;
    }
    Log("native drain availability=%u; no re-entry if backend identity is unverified",drainSupported.load());
    rendererBase.store(reinterpret_cast<uintptr_t>(module));
    auto createShader=reinterpret_cast<unsigned char*>(module)+0x4b560;
    const unsigned char createShaderExpected[]={0x4c,0x8b,0xdc,0x45,0x89,0x4b,0x20,0x4d,0x89,0x43,0x18,0x49,0x89,0x53,0x10,0x55};
    if(!memcmp(createShader,createShaderExpected,sizeof(createShaderExpected))) {
        auto shaderStatus=MH_CreateHook(createShader,reinterpret_cast<void*>(&CreateNativeShader),reinterpret_cast<void**>(&realCreateNativeShader));
        if(shaderStatus==MH_OK)shaderStatus=MH_EnableHook(createShader);
        Log("UI shader identification hook status=%d",shaderStatus);
    } else Log("UI shader identification signature mismatch; skipped");
    auto materialBind=reinterpret_cast<unsigned char*>(module)+0x5bf90;
    const unsigned char materialBindExpected[]={0x4c,0x89,0x4c,0x24,0x20,0x4c,0x89,0x44,0x24,0x18,0x53,0x55,0x56,0x57,0x41,0x54};
    if(!memcmp(materialBind,materialBindExpected,sizeof(materialBindExpected))) {
        auto materialStatus=MH_CreateHook(materialBind,reinterpret_cast<void*>(&MaterialBind),reinterpret_cast<void**>(&realMaterialBind));
        if(materialStatus==MH_OK)materialStatus=MH_EnableHook(materialBind);
        Log("UI material bind hook status=%d",materialStatus);
    } else Log("UI material bind signature mismatch; skipped");
    InstallTemporalHooks(reinterpret_cast<uintptr_t>(module));
    auto target=reinterpret_cast<unsigned char*>(module)+0x428d0;
    const unsigned char expected[]={0x4c,0x8b,0xdc,0x49,0x89,0x4b,0x08,0x55,0x56,0x41,0x55,0x41,0x56};
    if(memcmp(target,expected,sizeof(expected))) { Log("command dispatcher signature mismatch; skipped"); return true; }
    auto status=MH_CreateHook(target,reinterpret_cast<void*>(&Commands),reinterpret_cast<void**>(&realCommands));
    if(status==MH_OK) { status=MH_EnableHook(target); if(status!=MH_OK) MH_RemoveHook(target); }
    Log("command dispatcher hook status=%d",status);
    return true; // No repeated mutation if hooking failed.
}

static DWORD WINAPI Worker(void*) {
    HMODULE pinned{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                      reinterpret_cast<LPCWSTR>(&Worker), &pinned);
    wchar_t path[MAX_PATH]{}; GetModuleFileNameW(selfModule, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\'); if (!slash) return 0;
    wcscpy_s(ShaderHunter::directory,path);ShaderHunter::directory[slash-path]=0;
    wcscpy_s(slash+1, MAX_PATH-(slash+1-path), L"DL2VR-probe.log");
    _wfopen_s(&logFile, path, L"w");
    EffectsCapture::Init();
    Log("DL2VR 0.1.0 same-frame stereo VR; vertical look setting");
    wchar_t settingsPath[MAX_PATH]{};
    swprintf_s(settingsPath,L"%s\\DL2VR.ini",ShaderHunter::directory);
    lockVerticalLook=GetPrivateProfileIntW(L"Controls",L"LockVerticalLook",1,settingsPath)!=0;
    wchar_t configured[32]{};
    GetPrivateProfileStringW(L"Controls",L"LockVerticalLook",L"",configured,32,settingsPath);
    if(!configured[0])WritePrivateProfileStringW(L"Controls",L"LockVerticalLook",L"1",settingsPath);
    Log("LockVerticalLook=%u (mouse/controller pitch only while VR active)",lockVerticalLook.load());
    ShaderHunter::disableTemporalFilter=GetPrivateProfileIntW(L"Rendering",L"DisableTemporalFilter",0,settingsPath)!=0;
    Log("DisableTemporalFilter=%u; F11 toggles and saves",ShaderHunter::disableTemporalFilter.load());
    HMODULE engine{};
    for (unsigned i=0; i<1200 && !engine; ++i) { engine = GetModuleHandleW(L"engine_x64_rwdi.dll"); if (!engine) Sleep(100); }
    if (!engine) { Log("engine wait timed out"); return 0; }
    engineBase=reinterpret_cast<uintptr_t>(engine);
    auto guiCombined=reinterpret_cast<unsigned char*>(engine)+0x151350;
    auto guiProjection=reinterpret_cast<unsigned char*>(engine)+0x14d990;
    const unsigned char guiCombinedExpected[]={0x48,0x8d,0x81,0xc0,0,0,0,0xc3};
    const unsigned char guiProjectionExpected[]={0x48,0x8d,0x81,0x80,0,0,0,0xc3};
    if(memcmp(guiCombined,guiCombinedExpected,sizeof(guiCombinedExpected)) || memcmp(guiProjection,guiProjectionExpected,sizeof(guiProjectionExpected))) {
        Log("GUI matrix accessor signature mismatch");return 0;
    }
    auto guiCollect=reinterpret_cast<unsigned char*>(engine)+0x904180;
    const unsigned char guiCollectExpected[]={0x48,0x89,0x54,0x24,0x10,0x55,0x56,0x41,0x55,0x41,0x56,0x48,0x8b,0xec,0x48,0x83};
    const unsigned char guiCallerExpected[]={0x48,0x8b,0x0d,0x49,0x71,0xf0,0x01,0x45,0x33,0xc0,0x48,0x8b,0xd7,0xe8,0xfe,0x74,0x0d,0x00};
    if(memcmp(guiCollect,guiCollectExpected,sizeof(guiCollectExpected)) ||
       memcmp(reinterpret_cast<void*>(engineBase+0x82cc70),guiCallerExpected,sizeof(guiCallerExpected))) {
        Log("GUI collection signature mismatch; no hooks installed");return 0;
    }
    struct Hook { const char* name; uintptr_t rva; void* detour; void** original; };
    Hook hooks[] = {
        {"?RenderGame@IGame@@QEAAXXZ",0x835570,reinterpret_cast<void*>(&Render),reinterpret_cast<void**>(&realRender)},
        {"?SetCameraMatrix@IBaseCamera@@QEAAXAEBVmtx34@@@Z",0x443740,reinterpret_cast<void*>(&Set),reinterpret_cast<void**>(&realSet)},
        {"?SetInvCameraMatrix@IBaseCamera@@QEAAXAEBVmtx34@@@Z",0x445150,reinterpret_cast<void*>(&InverseSet),reinterpret_cast<void**>(&realInverseSet)},
        {"?GetViewCamera@ILevel@@QEBAPEAVIBaseCamera@@XZ",0xb29c20,reinterpret_cast<void*>(&Get),reinterpret_cast<void**>(&realGet)}
    };
    for (auto& h : hooks) {
        auto address = GetProcAddress(engine,h.name);
        if (reinterpret_cast<uintptr_t>(address)-reinterpret_cast<uintptr_t>(engine) != h.rva) {
            Log("unsupported engine export/RVA: %s; no hooks installed", h.name); return 0;
        }
    }
    auto internal=reinterpret_cast<unsigned char*>(engine)+0x834f20;
    const unsigned char expected[]={0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57,0x41,0x54};
    if (memcmp(internal,expected,sizeof(expected))) { Log("internal render signature mismatch; no hooks installed"); return 0; }
    auto view=reinterpret_cast<unsigned char*>(engine)+0xb39850;
    const unsigned char viewExpected[]={0x40,0x55,0x53,0x41,0x56,0x48,0x8d,0xac,0x24,0xb0,0xfd,0xff,0xff};
    if(memcmp(view,viewExpected,sizeof(viewExpected))) { Log("view setup signature mismatch; no hooks installed"); return 0; }
    auto copy=reinterpret_cast<unsigned char*>(engine)+0x413000;
    const unsigned char copyExpected[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x60,0x48,0x8b,0xfa};
    auto setter=reinterpret_cast<unsigned char*>(engine)+0x111fd20;
    const unsigned char setterExpected[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xc2,0x48,0x8b,0xd9};
    if(memcmp(copy,copyExpected,sizeof(copyExpected)) || memcmp(setter,setterExpected,sizeof(setterExpected))) {
        Log("camera copy/setter signature mismatch; no hooks installed"); return 0;
    }
    auto rebuild=reinterpret_cast<unsigned char*>(engine)+0x111ebf0;
    const unsigned char rebuildExpected[]={0x4c,0x8b,0xdc,0x49,0x89,0x4b,0x08,0x53,0x56,0x57,0x48,0x81,0xec,0xc0,0,0,0};
    if(memcmp(rebuild,rebuildExpected,sizeof(rebuildExpected))) { Log("projection rebuild signature mismatch");return 0; }
    rebuildProjection=reinterpret_cast<RebuildProjectionFn>(rebuild);
    auto scene=reinterpret_cast<unsigned char*>(engine)+0x835710;
    const unsigned char sceneExpected[]={0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57,0x48,0x81,0xec,0x40,0x3,0x0};
    if(memcmp(scene,sceneExpected,sizeof(sceneExpected))) { Log("scene signature mismatch; no hooks installed"); return 0; }
    auto prepare=reinterpret_cast<unsigned char*>(engine)+0x82c960;
    const unsigned char prepareExpected[]={0x40,0x53,0x55,0x56,0x57,0x41,0x54,0x41,0x56,0x48,0x83,0xec,0x68,0x4c};
    if(memcmp(prepare,prepareExpected,sizeof(prepareExpected))) { Log("prepare signature mismatch; no hooks installed"); return 0; }
    auto submit=reinterpret_cast<unsigned char*>(engine)+0x1115ef0;
    const unsigned char submitExpected[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x48,0x89,0x91,0xe0,0x18};
    if(memcmp(submit,submitExpected,sizeof(submitExpected))) { Log("submit signature mismatch; no hooks installed"); return 0; }
    const unsigned char commonSubmitExpected[]={0x48,0x8d,0x54,0x24,0x20,0x48,0x8b,0x01,0xff,0x90,0xa0,0x01,0x00,0x00};
    if(memcmp(reinterpret_cast<void*>(engineBase+0x8359b9),commonSubmitExpected,sizeof(commonSubmitExpected))) {
        Log("shared fresh/cached scene submission signature mismatch");return 0;
    }
    auto endScene=reinterpret_cast<unsigned char*>(engine)+0x11133b0;
    const unsigned char endSceneExpected[]={0x48,0x83,0xec,0x28,0x48,0x89,0x74,0x24,0x38,0x48,0x89,0x7c,0x24,0x20};
    if(memcmp(endScene,endSceneExpected,sizeof(endSceneExpected))) { Log("endScene signature mismatch; no hooks installed"); return 0; }
    auto presentRequest=reinterpret_cast<unsigned char*>(engine)+0x11157d0;
    const unsigned char presentRequestExpected[]={0x48,0x89,0x5c,0x24,0x8,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24};
    if(memcmp(presentRequest,presentRequestExpected,sizeof(presentRequestExpected))) { Log("presentRequest signature mismatch; no hooks installed"); return 0; }
    auto queuePhase=reinterpret_cast<unsigned char*>(engine)+0x118f910;
    const unsigned char queuePhaseExpected[]={0x44,0x8b,0xc9,0x4c,0x8b,0xc2,0x48,0x8b,0xd,0x43,0x3a,0x73,0x1,0x41};
    if(memcmp(queuePhase,queuePhaseExpected,sizeof(queuePhaseExpected))) { Log("queue phase signature mismatch; no hooks installed"); return 0; }
    const unsigned char resetLevelExpected[]={0x48,0x89,0x5c,0x24,0x8,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xf9,0x48};
    resetLevel=reinterpret_cast<ResetLevelFn>(engineBase+0xb43350);
    if(memcmp(reinterpret_cast<void*>(resetLevel),resetLevelExpected,sizeof(resetLevelExpected))) { Log("resetLevel signature mismatch; no hooks installed"); return 0; }
    const unsigned char rendererEnterExpected[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xf0,0xff,0x41,0x78,0x80};
    rendererEnter=reinterpret_cast<RendererEnterFn>(engineBase+0x1110200);
    if(memcmp(reinterpret_cast<void*>(rendererEnter),rendererEnterExpected,sizeof(rendererEnterExpected))) { Log("rendererEnter signature mismatch; no hooks installed"); return 0; }
    const unsigned char rendererLeaveExpected[]={0x40,0x53,0x48,0x83,0xec,0x20,0x80,0xb9,0xb8,0x0,0x0,0x0,0x0,0x48};
    rendererLeave=reinterpret_cast<RendererLeaveFn>(engineBase+0x1115b20);
    if(memcmp(reinterpret_cast<void*>(rendererLeave),rendererLeaveExpected,sizeof(rendererLeaveExpected))) { Log("rendererLeave signature mismatch; no hooks installed"); return 0; }
    auto visibilityCamera=reinterpret_cast<unsigned char*>(engine)+0x61e7c0;
    const unsigned char visibilityExpected[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x05,0x23,0xdf,0x1e,0x01};
    if(memcmp(visibilityCamera,visibilityExpected,sizeof(visibilityExpected))) { Log("visibility selector signature mismatch");return 0; }
    auto baseCameraCopy=reinterpret_cast<unsigned char*>(engine)+0x403d40;
    const unsigned char baseCopyExpected[]={0x0f,0x10,0x42,0x10,0x0f,0x11,0x41,0x10,0x0f,0x10,0x4a,0x20,0x0f,0x11,0x49,0x20};
    if(memcmp(baseCameraCopy,baseCopyExpected,sizeof(baseCopyExpected))) { Log("base camera copy signature mismatch");return 0; }
    auto mainVisibility=reinterpret_cast<unsigned char*>(engine)+0x6bc2e0;
    const unsigned char mainVisibilityExpected[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18};
    auto worldVisibility=reinterpret_cast<unsigned char*>(engine)+0x70d780;
    const unsigned char worldVisibilityExpected[]={0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x57,0x41,0x56};
    if(memcmp(mainVisibility,mainVisibilityExpected,sizeof(mainVisibilityExpected)) || memcmp(worldVisibility,worldVisibilityExpected,sizeof(worldVisibilityExpected))) { Log("main/world visibility signatures mismatch");return 0; }
    auto guiImageDraw=reinterpret_cast<unsigned char*>(engine)+0xab3ff0;
    const unsigned char guiImageDrawExpected[]={0x40,0x55,0x53,0x41,0x55,0x41,0x56,0x48,0x8d,0xac,0x24,0x08,0xf9,0xff,0xff,0x48};
    auto guiMovieDraw=reinterpret_cast<unsigned char*>(engine)+0xab71c0;
    const unsigned char guiMovieDrawExpected[]={0x40,0x55,0x56,0x57,0x41,0x55,0x41,0x56,0x48,0x8d,0xac,0x24,0x90,0xf9,0xff,0xff};
    if(memcmp(guiMovieDraw,guiMovieDrawExpected,sizeof(guiMovieDrawExpected))){Log("GUI movie draw signature mismatch");return 0;}
    auto guiTextDraw=reinterpret_cast<unsigned char*>(engine)+0xab7850;
    const unsigned char guiTextDrawExpected[]={0x48,0x89,0x5c,0x24,0x20,0x4c,0x89,0x44,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x48};
    auto uploadConstants=reinterpret_cast<unsigned char*>(engine)+0x118f5b0;
    const unsigned char uploadConstantsExpected[]={0x48,0x83,0xec,0x38,0x4c,0x8b,0xd1,0x44,0x89,0x4c,0x24,0x20,0x48,0x8b,0x0d,0x9d};
    if(memcmp(guiImageDraw,guiImageDrawExpected,sizeof(guiImageDrawExpected)) ||
       memcmp(guiTextDraw,guiTextDrawExpected,sizeof(guiTextDrawExpected)) ||
       memcmp(uploadConstants,uploadConstantsExpected,sizeof(uploadConstantsExpected))) {Log("GUI draw signatures mismatch");return 0;}
    componentSet=reinterpret_cast<ComponentSetFn>(setter);
    if (MH_Initialize()!=MH_OK) { Log("MinHook initialize failed"); return 0; }
    for (auto& h : hooks) {
        auto result=MH_CreateHook(reinterpret_cast<void*>(GetProcAddress(engine,h.name)),h.detour,h.original);
        if (result!=MH_OK) { Log("hook create failed %s %d",h.name,result); MH_Uninitialize(); return 0; }
    }
    auto internalStatus=MH_CreateHook(internal,reinterpret_cast<void*>(&Internal),reinterpret_cast<void**>(&realInternal));
    if (internalStatus!=MH_OK) { Log("internal hook failed %d",internalStatus); MH_Uninitialize(); return 0; }
    auto viewStatus=MH_CreateHook(view,reinterpret_cast<void*>(&ViewSetup),reinterpret_cast<void**>(&realViewSetup));
    if(viewStatus!=MH_OK) { Log("view setup hook failed %d",viewStatus); MH_Uninitialize(); return 0; }
    auto copyStatus=MH_CreateHook(copy,reinterpret_cast<void*>(&CameraCopy),reinterpret_cast<void**>(&realCameraCopy));
    if(copyStatus!=MH_OK) { Log("camera copy hook failed %d",copyStatus); MH_Uninitialize(); return 0; }
    auto sceneStatus=MH_CreateHook(scene,reinterpret_cast<void*>(&Scene),reinterpret_cast<void**>(&realScene));
    auto prepareStatus=MH_CreateHook(prepare,reinterpret_cast<void*>(&Prepare),reinterpret_cast<void**>(&realPrepare));
    if(sceneStatus!=MH_OK || prepareStatus!=MH_OK) { Log("scene/prepare hooks failed %d/%d",sceneStatus,prepareStatus); MH_Uninitialize(); return 0; }
    auto submitStatus=MH_CreateHook(submit,reinterpret_cast<void*>(&Submit),reinterpret_cast<void**>(&realSubmit));
    if(submitStatus!=MH_OK) { Log("submit hook failed %d",submitStatus); MH_Uninitialize(); return 0; }
    auto endSceneStatus=MH_CreateHook(endScene,reinterpret_cast<void*>(&EndScene),reinterpret_cast<void**>(&realEndScene));
    if(endSceneStatus!=MH_OK) { Log("endScene hook failed %d",endSceneStatus); MH_Uninitialize(); return 0; }
    auto presentRequestStatus=MH_CreateHook(presentRequest,reinterpret_cast<void*>(&PresentRequest),reinterpret_cast<void**>(&realPresentRequest));
    if(presentRequestStatus!=MH_OK) { Log("presentRequest hook failed %d",presentRequestStatus); MH_Uninitialize(); return 0; }
    auto queuePhaseStatus=MH_CreateHook(queuePhase,reinterpret_cast<void*>(&QueuePhase),reinterpret_cast<void**>(&realQueuePhase));
    if(queuePhaseStatus!=MH_OK) { Log("queue phase hook failed %d",queuePhaseStatus); MH_Uninitialize(); return 0; }
    auto visibilityStatus=MH_CreateHook(visibilityCamera,reinterpret_cast<void*>(&VisibilityCamera),reinterpret_cast<void**>(&realVisibilityCamera));
    if(visibilityStatus!=MH_OK) { Log("visibility selector hook failed %d",visibilityStatus);MH_Uninitialize();return 0; }
    auto baseCopyStatus=MH_CreateHook(baseCameraCopy,reinterpret_cast<void*>(&BaseCameraCopy),reinterpret_cast<void**>(&realBaseCameraCopy));
    if(baseCopyStatus!=MH_OK){Log("base camera copy hook failed %d",baseCopyStatus);MH_Uninitialize();return 0;}
    auto mainVisibilityStatus=MH_CreateHook(mainVisibility,reinterpret_cast<void*>(&MainVisibility),reinterpret_cast<void**>(&realMainVisibility));
    auto worldVisibilityStatus=MH_CreateHook(worldVisibility,reinterpret_cast<void*>(&WorldVisibility),reinterpret_cast<void**>(&realWorldVisibility));
    if(mainVisibilityStatus!=MH_OK || worldVisibilityStatus!=MH_OK){Log("main/world visibility hook failed %d/%d",mainVisibilityStatus,worldVisibilityStatus);MH_Uninitialize();return 0;}
    auto guiStatus=MH_CreateHook(guiCollect,reinterpret_cast<void*>(&GuiCollect),reinterpret_cast<void**>(&realGuiCollect));
    if(guiStatus!=MH_OK){Log("GUI hook failed %d",guiStatus);MH_Uninitialize();return 0;}
    auto gcStatus=MH_CreateHook(guiCombined,reinterpret_cast<void*>(&GuiCombined),reinterpret_cast<void**>(&realGuiCombined));
    auto gpStatus=MH_CreateHook(guiProjection,reinterpret_cast<void*>(&GuiProjection),reinterpret_cast<void**>(&realGuiProjection));
    if(gcStatus!=MH_OK || gpStatus!=MH_OK){Log("GUI accessor hooks failed %d/%d",gcStatus,gpStatus);MH_Uninitialize();return 0;}
    auto giStatus=MH_CreateHook(guiImageDraw,reinterpret_cast<void*>(&GuiImageDraw),reinterpret_cast<void**>(&realGuiImageDraw));
    auto gmStatus=MH_CreateHook(guiMovieDraw,reinterpret_cast<void*>(&GuiMovieDraw),reinterpret_cast<void**>(&realGuiMovieDraw));
    if(gmStatus!=MH_OK){Log("GUI movie hook failed %d",gmStatus);MH_Uninitialize();return 0;}
    auto gtStatus=MH_CreateHook(guiTextDraw,reinterpret_cast<void*>(&GuiTextDraw),reinterpret_cast<void**>(&realGuiTextDraw));
    auto ucStatus=MH_CreateHook(uploadConstants,reinterpret_cast<void*>(&UploadConstants),reinterpret_cast<void**>(&realUploadConstants));
    if(giStatus!=MH_OK || gtStatus!=MH_OK || ucStatus!=MH_OK){Log("GUI draw hooks failed %d/%d/%d",giStatus,gtStatus,ucStatus);MH_Uninitialize();return 0;}
    auto enabled = MH_EnableHook(MH_ALL_HOOKS);
    Log("hooks enable status=%d",enabled);
    if (enabled!=MH_OK) { MH_Uninitialize(); return 0; }
    bool wasDown=false, wasF7=false, wasF6=false, wasF5=false, wasF4=false, wasF3=false, wasF2=false, wasF1=false, wasF9=false;
    bool wasF10=false,wasF11=false,wasF12=false;
    bool commandsAttempted=InstallCommands();
    bool inputAttempted=InstallVerticalLookHook();
    for (unsigned long long seconds=0; ; ++seconds) {
        for (int n=0; n<50; ++n) {
            if(!inputAttempted)inputAttempted=InstallVerticalLookHook();
            bool f12=(GetAsyncKeyState(VK_F12)&0x8000)!=0;
            if(f12 && !wasF12){unsigned mode=(temporalMode.load()+1)%3;temporalMode=mode;Log("F12 history mode=%u (0=native, 1=DLSS-only, 2=full camera/textures)",mode);}
            wasF12=f12;
            SHORT f11key=GetAsyncKeyState(VK_F11);
            bool f11=(f11key&0x8000)!=0;
            if((f11 && !wasF11) || (f11key&1)) {
                bool off=!ShaderHunter::disableTemporalFilter.load();ShaderHunter::disableTemporalFilter=off;
                bool saved=WritePrivateProfileStringW(L"Rendering",L"DisableTemporalFilter",off?L"1":L"0",settingsPath)!=0;
                Log("F11 temporal filter disabled=%u saved=%u",off,saved);
            }
            wasF11=f11;
            bool f10=(GetAsyncKeyState(VK_F10)&0x8000)!=0;
            if(f10 && !wasF10){hideGui.store(!hideGui.load());guiReadBudget.store(96);Log("F10 native GUI hidden=%u",hideGui.load());}
            wasF10=f10;
            bool f1=(GetAsyncKeyState(VK_F1)&0x8000)!=0;
            if(f1 && !wasF1){uiCorrection.store(!uiCorrection.load());Log("F1 UI material correction=%u",uiCorrection.load());}
            wasF1=f1;
            bool f2=(GetAsyncKeyState(VK_F2)&0x8000)!=0;
            if(f2 && !wasF2){bypassMainVisibilityInput.store(!bypassMainVisibilityInput.load());Log("F2 bypass supplied main visibility input=%u",bypassMainVisibilityInput.load());}
            wasF2=f2;
            bool f3=(GetAsyncKeyState(VK_F3)&0x8000)!=0;
            if(f3 && !wasF3){skipIntermediatePresents.store(!skipIntermediatePresents.load());Log("F3 skip intermediate desktop Presents=%u",skipIntermediatePresents.load());}
            wasF3=f3;
            bool f9=(GetAsyncKeyState(VK_F9)&0x8000)!=0;
            if(f9 && !wasF9){FlashlightShadow::enabled=!FlashlightShadow::enabled.load();Log("F9 shared flashlight shadows=%u",FlashlightShadow::enabled.load());}
            wasF9=f9;
            bool f5=(GetAsyncKeyState(VK_F5)&0x8000)!=0;
            if(f5 && !wasF5) {trackedCullingEnabled.store(!trackedCullingEnabled.load());Log("F5 tracked visibility camera=%u",trackedCullingEnabled.load());}
            wasF5=f5;
            bool f4=(GetAsyncKeyState(VK_F4)&0x8000)!=0;
            if(f4 && !wasF4) {normalSceneAsLeft.store(!normalSceneAsLeft.load());Log("F4 normal scene as left eye=%u",normalSceneAsLeft.load());}
            wasF4=f4;
            bool f6=(GetAsyncKeyState(VK_F6)&0x8000)!=0;
            if(f6 && !wasF6){recenterRequested.store(true);temporalResetRequested=true;}
            wasF6=f6;
            bool f7=(GetAsyncKeyState(VK_F7)&0x8000)!=0;
            bool start=false,automaticStart=false;
            if(f7 && !wasF7) {
                bool off=vrActive.load() || (resumePending.load() && vrRequested.load());
                vrRequested.store(!off);resumePending.store(false);
                if(off) {vrActive.store(false); drainRequested.store(false); SetXrScreenEnabled(false); Log("VR stopped by F7; automatic resume cancelled"); }
                else start=true;
            }
            wasF7=f7;
            if(resumePending.load() && vrRequested.load() && drainSupported.load() && !pairSafetyBlocked.load() &&
               sceneCalls.load()>resumeAfterScene.load() && GetTickCount64()>=resumeAfterTime.load()) {
                AcquireSRWLockShared(&captureLock);
                bool ready=lastScene.valid;
                ReleaseSRWLockShared(&captureLock);
                if(ready) {start=true;automaticStart=true;resumePending.store(false);}
            }
            if(start && drainSupported.load() && !pairSafetyBlocked.load()) {
                if(!automaticStart)recenterRequested.store(true);
                temporalResetRequested=true;runPairs.store(0);pairElapsedTotal.store(0);pairElapsedMax.store(0);
                SetXrScreenEnabled(true);vrActive.store(true);
                Log("VR started without time/frame limit; automatic=%u cachedSubmits=%llu; F7 toggles; F6 recenters",automaticStart,cachedSceneSubmits.load());
            }
            bool shift=!vrActive.load() && (GetAsyncKeyState(VK_F9)&0x8000)!=0;
            if(cameraShiftEnabled.exchange(shift)!=shift) Log("F9 renderer-camera offset=%s distance=0.25 game-units",shift?"ON":"OFF");
            static bool huntKeys[5]{};
            const int huntCodes[5]={VK_END,VK_PRIOR,VK_NEXT,VK_DELETE,VK_HOME};
            for(unsigned k=0;k<5;++k) {
                SHORT key=GetAsyncKeyState(huntCodes[k]);bool held=(key&0x8000)!=0;
                bool pressed=(held && !huntKeys[k]) || (!held && (key&1));huntKeys[k]=held;
                if(!pressed)continue;
                if(k==4){ShaderHunter::Reset();Log("shader hunter OFF/reset; all shaders restored; saved marks retained");}
                else if(k==0){if(ShaderHunter::installed){ShaderHunter::Enable();Log("shader hunter ON; gathering visible pixel/compute shaders; PageUp/Down selects and disables one");}else Log("shader hunter unavailable: draw hooks not installed");}
                else if(k==1 || k==2){ShaderHunter::Entry e{};unsigned pos{},total{};if(ShaderHunter::Step(k==1?1:-1,e,pos,total))Log("shader hunter disabled %u/%u stage=%u hash=%016llx",pos,total,e.stage,e.hash);else Log("shader hunter no selection; enable with End and allow scene to render");}
                else {uint64_t hash{};unsigned stage{};bool ok=ShaderHunter::Mark(hash,stage);Log("shader hunter mark saved=%u stage=%u hash=%016llx suppressed=%llu",ok,stage,hash,ShaderHunter::skipped.load());}
            }
            SHORT captureKey=GetAsyncKeyState(VK_F8);
            if(captureKey&1)Log("temporal stats mode=%u epoch=%u camera=%llu/%llu external=%llu/%llu DLSS eval flat/L/R=%llu/%llu/%llu constants=%llu/%llu/%llu",
                temporalMode.load(),temporalEpoch.load(),temporalLookups[0].load(),temporalLookups[1].load(),temporalPasses[0].load(),temporalPasses[1].load(),
                dlssCalls[0].load(),dlssCalls[1].load(),dlssCalls[2].load(),dlssConstants[0].load(),dlssConstants[1].load(),dlssConstants[2].load());
            bool down=(captureKey&0x8000)!=0;
            if ((down && !wasDown) || (!down && (captureKey&1))) { Log("DLSS matrices fixed=%llu failed=%llu",dlssMatrixFixed.load(),dlssMatrixFailed.load()); Log("camera commits=%llu/%llu incomplete=%llu",cameraCommits[0].load(),cameraCommits[1].load(),cameraIncomplete.load()); Log("history mode requested=%u latched=%u camera=%u DLSS=%u resets=%llu/%llu",temporalMode.load(),temporalLatchedMode.load(),temporalEffective.load(),dlssEffective.load(),dlssResets[0].load(),dlssResets[1].load()); Log("texture history discovered=%llu restored=%llu saved=%llu failed=%llu",TextureHistory::discovered.load(),TextureHistory::restored.load(),TextureHistory::saved.load(),TextureHistory::failed.load()); Log("F8 capture marker effectsArmed=%u gpuArmed=%u dlssArmed=%u",EffectsCapture::Arm(),GpuTrace::Arm(),DlssCapture::Arm()); guiReadBudget.store(96);for(auto& b:budgets) b.store(8); ArmCapture(); }
            unsigned effectsError{},effectsCount{},effectsDropped{};uint64_t effectsPair{};
            if(EffectsCapture::Save(path,effectsError,effectsCount,effectsDropped,effectsPair))
                Log("effects-capture pair=%llu records=%u dropped=%u error=%u",effectsPair,effectsCount,effectsDropped,effectsError);
            unsigned gpuWritten{},gpuLost{},gpuError{};
            if(GpuTrace::Save(ShaderHunter::directory,gpuWritten,gpuLost,gpuError))Log("GPU resource trace records=%u dropped=%u error=%u",gpuWritten,gpuLost,gpuError);
            unsigned dlssCaptureFailures{};if(DlssCapture::Save(ShaderHunter::directory,dlssCaptureFailures))Log("DLSS capture saved failures=%u",dlssCaptureFailures);
            SaveCapture();
            BackbufferReport backbuffer;
            if(SaveBackbufferCapture(path,backbuffer))
                Log("backbuffer-capture hr=%08lx request=%llu dispatchSeq=%llu swap=%p width=%u height=%u format=%u samples=%u view=%u path=%ls",
                    static_cast<unsigned long>(backbuffer.result),backbuffer.request,backbuffer.dispatchSequence,
                    reinterpret_cast<void*>(backbuffer.swap),backbuffer.width,backbuffer.height,backbuffer.format,backbuffer.samples,backbuffer.label,backbuffer.path);
            wasDown=down; Sleep(20);
        }
        if(!commandsAttempted) commandsAttempted=InstallCommands();
        if(seconds>=60 && seconds%30)continue;
        Log("UI draw image=%llu text=%llu active=%llu correctedUploads=%llu rejected=%llu enabled=%u",uiImageDraws.load(),uiTextDraws.load(),uiActiveDraws.load(),uiConstantUploads.load(),uiRejected.load(),uiCorrection.load());
        Log("UI materials corrected=%llu unmatched=%llu rejected=%llu movieDraws=%llu",uiMaterials.load(),uiMaterialMisses.load(),uiMaterialRejected.load(),uiMovieDraws.load());
        Log("GUI matrix reads=%llu hiddenCollections=%llu hidden=%u",guiReads.load(),guiHidden.load(),hideGui.load());
        Log("vertical look locked=%u blockedInputs=%llu",lockVerticalLook.load(),verticalInputsBlocked.load());
        Log("flashlight shared enabled=%u created=%llu copies=%llu applied=%llu failed=%llu missing=%llu",FlashlightShadow::enabled.load(),FlashlightShadow::created.load(),FlashlightShadow::copies.load(),FlashlightShadow::applied.load(),FlashlightShadow::failed.load(),FlashlightShadow::missing.load());
        Log("tracked projection pairs=%llu visibilityRedirects=%llu trackedCulling=%u",trackedPairs.load(),visibilityRedirects.load(),trackedCullingEnabled.load());
        Log("auxiliaryRedirects=%llu presentsSkipped=%llu presentsForwarded=%llu presentMicroseconds=%llu skipIntermediate=%u",auxiliaryRedirects.load(),presentsSkipped.load(),presentsForwarded.load(),presentMicroseconds.load(),skipIntermediatePresents.load());
        Log("mainVisibilityCalls=%llu inputBypasses=%llu worldVisibilityCalls=%llu inputBypasses=%llu bypassEnabled=%u",mainVisibilityCalls.load(),mainVisibilityInputsBypassed.load(),worldVisibilityCalls.load(),worldVisibilityInputsBypassed.load(),bypassMainVisibilityInput.load());
        Log("normalLeftPairs=%llu normalLeftRejected=%llu twoRenderMode=%u",normalLeftPairs.load(),normalLeftRejected.load(),normalSceneAsLeft.load());
        Log("GPU transport pairs=%llu failures=%llu",gpuPairs.load(),gpuFailures.load());
        Log("pair totals completed=%llu rejected=%llu invariantFailures=%llu active=%u",pairCompleted.load(),pairRejected.load(),pairInvariantFailures.load(),vrActive.load()!=0);
        Log("totals render=%llu internal=%llu commandBuffers=%llu set=%llu inverseSet=%llu getView=%llu viewSetups=%llu primaryCopies=%llu shiftedCopies=%llu shiftRejected=%llu",frames.load(),internalFrames.load(),commandBuffers.load(),sets.load(),inverseSets.load(),gets.load(),viewSetups.load(),primaryCopies.load(),cameraShifts.load(),cameraShiftRejected.load());
    }
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason!=DLL_PROCESS_ATTACH) return TRUE;
    selfModule=module;
    wchar_t system[MAX_PATH]{};
    if (!GetSystemDirectoryW(system,MAX_PATH)) return FALSE;
    wcscat_s(system,L"\\winmm.dll");
    HMODULE original=LoadLibraryW(system);
    if (!original) return FALSE;
    for (unsigned i=0;i<kExportCount;++i) {
        g_targets[i]=GetProcAddress(original,MAKEINTRESOURCEA(kOrdinals[i]));
        if (!g_targets[i]) return FALSE;
    }
    HANDLE thread=CreateThread(nullptr,0,Worker,nullptr,0,nullptr);
    if (thread) CloseHandle(thread);
    return TRUE;
}

