#pragma once
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <vector>
#include <atomic>
#include <array>
#include "XrScreen.h"
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D12
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
inline DXGI_FORMAT HeadsetColorFormat(DXGI_FORMAT source){
 switch(source){case DXGI_FORMAT_R8G8B8A8_UNORM:return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
 case DXGI_FORMAT_B8G8R8A8_UNORM:return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
 case DXGI_FORMAT_B8G8R8X8_UNORM:return DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
 default:return source;}
}
inline XrPosef MapPanelPose(const XrView* views){
 XrPosef panelPose{};
   auto h=views[0].pose;float yaw=atan2f(2*(h.orientation.x*h.orientation.z+h.orientation.w*h.orientation.y),1-2*(h.orientation.x*h.orientation.x+h.orientation.y*h.orientation.y));
   panelPose.orientation={0,sinf(yaw*.5f),0,cosf(yaw*.5f)};
   panelPose.position={(views[0].pose.position.x+views[1].pose.position.x)*.5f-2*sinf(yaw),(views[0].pose.position.y+views[1].pose.position.y)*.5f,(views[0].pose.position.z+views[1].pose.position.z)*.5f-2*cosf(yaw)};
 return panelPose;
}
// All copies remain on the native direct queue. Wait only before reusing a busy slot.
class Dx12Screen {
 friend struct Dx12ScreenChecks;
 template<class T> using Com=Microsoft::WRL::ComPtr<T>;
 Com<ID3D12Device> device;Com<ID3D12CommandQueue> queue;
 struct Slot{Com<ID3D12CommandAllocator> allocator;Com<ID3D12GraphicsCommandList> list;UINT64 completion{};std::vector<Com<ID3D12Resource>> retained;};
 std::array<Slot,12> slots;unsigned nextSlot{},activeSlot{};bool pairAsync{true};
 std::atomic<bool> requestedAsync{true};std::atomic<uint64_t> submissions{},forcedWaits{},reuseWaits{},waitTicks{};
 static uint64_t Now(){LARGE_INTEGER t;QueryPerformanceCounter(&t);return t.QuadPart;}
 Com<ID3D12Resource> eyes[2];Com<ID3D12CommandAllocator> allocator;
 Com<ID3D12GraphicsCommandList> list;Com<ID3D12Fence> fence;UINT64 signal{};
 HANDLE event{};UINT width{},height{};DXGI_FORMAT format{};uint64_t firstPair{};bool first{};
 bool mapPanel{},panelAnchored{};XrPosef panelPose{};
 XrSpace local{};bool pending{};XrFrameState tracked{XR_TYPE_FRAME_STATE};XrView views[2]{{XR_TYPE_VIEW},{XR_TYPE_VIEW}};
 XrInstance instance{};XrSession session{};XrSpace space{};XrSwapchain chains[2]{};
 std::vector<XrSwapchainImageD3D12KHR> images[2];bool running{},poisoned{};
 static void H(HRESULT h){if(FAILED(h))throw h;}
 static void X(XrResult r){if(XR_FAILED(r))throw HRESULT(0xA0010000|(-r&0xffff));}
 void Barrier(ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;v.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};list->ResourceBarrier(1,&v);}
 void Wait(UINT64 value,bool reuse){
  if(!value)return;auto done=fence->GetCompletedValue();if(done==UINT64_MAX)throw HRESULT(DXGI_ERROR_DEVICE_REMOVED);if(done>=value)return;
  auto start=Now();if(reuse)++reuseWaits;else ++forcedWaits;
  H(fence->SetEventOnCompletion(value,event));if(WaitForSingleObject(event,5000)!=WAIT_OBJECT_0)throw HRESULT(DXGI_ERROR_DEVICE_HUNG);
  if(fence->GetCompletedValue()==UINT64_MAX)throw HRESULT(DXGI_ERROR_DEVICE_REMOVED);waitTicks.fetch_add(Now()-start);
 }
 void Begin(){
  activeSlot=nextSlot;nextSlot=(nextSlot+1)%slots.size();auto& slot=slots[activeSlot];Wait(slot.completion,true);slot.retained.clear();
  allocator=slot.allocator;list=slot.list;H(allocator->Reset());H(list->Reset(allocator.Get(),nullptr));
 }
 void Retain(ID3D12Resource* resource){slots[activeSlot].retained.emplace_back(resource);}
 void Execute(){H(list->Close());ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);H(queue->Signal(fence.Get(),++signal));slots[activeSlot].completion=signal;++submissions;if(!pairAsync)Wait(signal,false);}
 void InitXr(){
  const char* ext=XR_KHR_D3D12_ENABLE_EXTENSION_NAME;XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};strcpy_s(ci.applicationInfo.applicationName,"DL2VR DX12 stereo prototype");ci.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);ci.enabledExtensionCount=1;ci.enabledExtensionNames=&ext;X(xrCreateInstance(&ci,&instance));
  XrSystemGetInfo si{XR_TYPE_SYSTEM_GET_INFO};si.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;XrSystemId system;X(xrGetSystem(instance,&si,&system));
  PFN_xrGetD3D12GraphicsRequirementsKHR requirements{};X(xrGetInstanceProcAddr(instance,"xrGetD3D12GraphicsRequirementsKHR",reinterpret_cast<PFN_xrVoidFunction*>(&requirements)));XrGraphicsRequirementsD3D12KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR};X(requirements(instance,system,&req));auto luid=device->GetAdapterLuid();if(memcmp(&luid,&req.adapterLuid,sizeof(luid)))throw HRESULT(E_FAIL);
  D3D12_FEATURE_DATA_FEATURE_LEVELS levels{};levels.NumFeatureLevels=1;levels.pFeatureLevelsRequested=&req.minFeatureLevel;H(device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS,&levels,sizeof(levels)));
  XrGraphicsBindingD3D12KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D12_KHR};binding.device=device.Get();binding.queue=queue.Get();XrSessionCreateInfo sc{XR_TYPE_SESSION_CREATE_INFO};sc.next=&binding;sc.systemId=system;X(xrCreateSession(instance,&sc,&session));
  uint32_t n{};X(xrEnumerateSwapchainFormats(session,0,&n,nullptr));std::vector<int64_t> formats(n);X(xrEnumerateSwapchainFormats(session,n,&n,formats.data()));auto xrFormat=HeadsetColorFormat(format);bool match=false;for(auto f:formats)match|=f==xrFormat;if(!match)throw HRESULT(DXGI_ERROR_UNSUPPORTED);
  XrReferenceSpaceCreateInfo rs{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};rs.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;rs.poseInReferenceSpace.orientation.w=1;X(xrCreateReferenceSpace(session,&rs,&space));
  rs.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;X(xrCreateReferenceSpace(session,&rs,&local));
  for(int eye=0;eye<2;++eye){XrSwapchainCreateInfo sw{XR_TYPE_SWAPCHAIN_CREATE_INFO};sw.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT;sw.format=xrFormat;sw.sampleCount=1;sw.width=width;sw.height=height;sw.faceCount=sw.arraySize=sw.mipCount=1;X(xrCreateSwapchain(session,&sw,&chains[eye]));X(xrEnumerateSwapchainImages(chains[eye],0,&n,nullptr));images[eye].resize(n,{XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});X(xrEnumerateSwapchainImages(chains[eye],n,&n,reinterpret_cast<XrSwapchainImageBaseHeader*>(images[eye].data())));}
 }
 void Events(){
  XrEventDataBuffer e{XR_TYPE_EVENT_DATA_BUFFER};while(xrPollEvent(instance,&e)==XR_SUCCESS){if(e.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED){auto state=reinterpret_cast<XrEventDataSessionStateChanged*>(&e)->state;if(state==XR_SESSION_STATE_READY){XrSessionBeginInfo b{XR_TYPE_SESSION_BEGIN_INFO};b.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;X(xrBeginSession(session,&b));running=true;}if(state==XR_SESSION_STATE_STOPPING){Cancel();running=false;X(xrEndSession(session));}if(state==XR_SESSION_STATE_EXITING||state==XR_SESSION_STATE_LOSS_PENDING)throw HRESULT(E_ABORT);}e={XR_TYPE_EVENT_DATA_BUFFER};}
 }
 void Submit(){
  if(!instance)InitXr();
  bool hasFrame=pending;bool immersive=hasFrame&&!mapPanel;XrFrameState frame{XR_TYPE_FRAME_STATE};
  if(hasFrame){frame=tracked;pending=false;}
  else{Events();if(!running)return;XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};X(xrWaitFrame(session,&wait,&frame));XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};X(xrBeginFrame(session,&begin));}
  XrCompositionLayerProjectionView pv[2]{{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
  XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};projection.space=local;projection.viewCount=2;projection.views=pv;
  XrCompositionLayerQuad quads[2]{{XR_TYPE_COMPOSITION_LAYER_QUAD},{XR_TYPE_COMPOSITION_LAYER_QUAD}};const XrCompositionLayerBaseHeader* layers[2]{};
  try{if(frame.shouldRender)for(int eye=0;eye<2;++eye){uint32_t index;XrSwapchainImageAcquireInfo ac{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};X(xrAcquireSwapchainImage(chains[eye],&ac,&index));XrSwapchainImageWaitInfo iw{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};iw.timeout=XR_INFINITE_DURATION;X(xrWaitSwapchainImage(chains[eye],&iw));auto dest=images[eye][index].texture;Begin();Retain(dest);Retain(eyes[eye].Get());Barrier(eyes[eye].Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);Barrier(dest,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_DEST);list->CopyResource(dest,eyes[eye].Get());Barrier(dest,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_RENDER_TARGET);Barrier(eyes[eye].Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);Execute();XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};X(xrReleaseSwapchainImage(chains[eye],&release));auto& q=quads[eye];q.space=space;q.eyeVisibility=eye?XR_EYE_VISIBILITY_RIGHT:XR_EYE_VISIBILITY_LEFT;q.subImage.swapchain=chains[eye];q.subImage.imageRect.extent={int32_t(width),int32_t(height)};q.pose.orientation.w=1;q.pose.position.z=-2;q.size={3.f,3.f*height/width};
 if(mapPanel){
  if(!panelAnchored&&hasFrame){
   panelPose=MapPanelPose(views);panelAnchored=true;
  }
  if(panelAnchored){q.space=local;q.pose=panelPose;}
 }
 layers[eye]=reinterpret_cast<XrCompositionLayerBaseHeader*>(&q);pv[eye].pose=views[eye].pose;pv[eye].fov=views[eye].fov;pv[eye].subImage=q.subImage;}}
  catch(...){XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=frame.predictedDisplayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;xrEndFrame(session,&end);throw;}
  XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=frame.predictedDisplayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;if(immersive)layers[0]=reinterpret_cast<XrCompositionLayerBaseHeader*>(&projection);end.layerCount=frame.shouldRender?(immersive?1:2):0;end.layers=layers;X(xrEndFrame(session,&end));
 }
public:
 void SetMapPanel(bool enabled){if(!enabled)panelAnchored=false;mapPanel=enabled;}
 void SetAsync(bool enabled){requestedAsync=enabled;}
 template<class Log> void Report(Log log){LARGE_INTEGER f;QueryPerformanceFrequency(&f);log("DX12 copies requestedAsync=%u submits=%llu forcedWaits=%llu reuseWaits=%llu waitMs=%.3f",requestedAsync.load(),submissions.load(),forcedWaits.load(),reuseWaits.load(),waitTicks.load()*1000./f.QuadPart);}
 bool BeginTracking(TrackedEye* eyesOut){
  if(!instance||poisoned||pending)return false;
  try{Events();if(!running)return false;XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};X(xrWaitFrame(session,&wait,&tracked));XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};X(xrBeginFrame(session,&begin));pending=true;
  XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;locate.displayTime=tracked.predictedDisplayTime;locate.space=local;XrViewState state{XR_TYPE_VIEW_STATE};uint32_t n{};X(xrLocateViews(session,&locate,&state,2,&n,views));
  auto flags=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;if(!tracked.shouldRender||n!=2||(state.viewStateFlags&flags)!=flags){Cancel();return false;}
  for(unsigned i=0;i<2;++i){auto& v=views[i];auto& o=eyesOut[i];o.quaternion[0]=v.pose.orientation.x;o.quaternion[1]=v.pose.orientation.y;o.quaternion[2]=v.pose.orientation.z;o.quaternion[3]=v.pose.orientation.w;o.position[0]=v.pose.position.x;o.position[1]=v.pose.position.y;o.position[2]=v.pose.position.z;o.fov[0]=v.fov.angleLeft;o.fov[1]=v.fov.angleRight;o.fov[2]=v.fov.angleUp;o.fov[3]=v.fov.angleDown;}return true;
  }catch(HRESULT){Cancel();poisoned=true;return false;}
 }
 void Cancel(){first=false;if(pending&&session){XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=tracked.predictedDisplayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;xrEndFrame(session,&end);}pending=false;}
 void Stop(){
 Cancel();
 // Never release resources which may still be used by a timed-out queue.
 if(fence){try{Wait(signal,false);}catch(HRESULT){poisoned=true;return;}}
 if(local){xrDestroySpace(local);local=XR_NULL_HANDLE;}
 if(space){xrDestroySpace(space);space=XR_NULL_HANDLE;}
 for(unsigned i=0;i<2;++i){if(chains[i])xrDestroySwapchain(chains[i]);chains[i]=XR_NULL_HANDLE;images[i].clear();}
 if(session){xrDestroySession(session);session=XR_NULL_HANDLE;}
 if(instance){xrDestroyInstance(instance);instance=XR_NULL_HANDLE;}
 running=false;panelAnchored=false;
 }

 HRESULT Copy(IDXGISwapChain3* swap,ID3D12CommandQueue* q,unsigned eye,uint64_t pair){try{
  if(poisoned)return E_FAIL;
  if(!swap||!q||eye<1||eye>2||!pair)return E_INVALIDARG;
  Com<ID3D12Device> d;H(swap->GetDevice(IID_PPV_ARGS(&d)));Com<ID3D12Resource> source;H(swap->GetBuffer(swap->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&source)));auto desc=source->GetDesc();if(desc.SampleDesc.Count!=1||desc.DepthOrArraySize!=1||desc.MipLevels!=1)return DXGI_ERROR_UNSUPPORTED;
  if(device && (device.Get()!=d.Get()||queue.Get()!=q||width!=desc.Width||height!=desc.Height||format!=desc.Format))return DXGI_ERROR_UNSUPPORTED;
  if(!device){if(eye!=1)return E_UNEXPECTED;device=d;queue=q;width=UINT(desc.Width);height=desc.Height;format=desc.Format;for(auto& slot:slots){H(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&slot.allocator)));H(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,slot.allocator.Get(),nullptr,IID_PPV_ARGS(&slot.list)));H(slot.list->Close());}H(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw HRESULT(E_FAIL);D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;desc.Flags=D3D12_RESOURCE_FLAG_NONE;for(auto& t:eyes)H(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&t)));}
  if(eye==2&&(!first||firstPair!=pair))return E_UNEXPECTED;
  if(eye==1)pairAsync=requestedAsync.load();
  Begin();Retain(source.Get());Retain(eyes[eye-1].Get());Barrier(source.Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_SOURCE);Barrier(eyes[eye-1].Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);list->CopyResource(eyes[eye-1].Get(),source.Get());Barrier(source.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PRESENT);Barrier(eyes[eye-1].Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);Execute();
  if(eye==1){first=true;firstPair=pair;}else{first=false;Submit();}return S_OK;
 }catch(HRESULT h){first=false;poisoned=true;return h;}}
};

