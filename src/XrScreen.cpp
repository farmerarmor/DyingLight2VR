#include "XrScreen.h"
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <atomic>
#include <vector>
#include <cstdio>
#include <cstring>
#include <mutex>
namespace {
std::recursive_mutex xrMutex;
std::atomic<bool> enabled{};
std::atomic<bool> trackingReady{};
XrInstance instance{}; XrSession session{}; XrSpace space{};
XrSpace localSpace{};
XrView trackedViews[2]{{XR_TYPE_VIEW},{XR_TYPE_VIEW}};
XrFrameState trackedFrame{XR_TYPE_FRAME_STATE};
bool trackedPending{};
XrSwapchain chains[2]{};
std::vector<XrSwapchainImageD3D11KHR> images[2];
bool running{},failed{};unsigned width{},height{};DXGI_FORMAT format{};
ID3D11Device* boundDevice{};
uint64_t submitted{};
FILE* logFile{};
void Log(const char* message,long long value=0) {
 if(!logFile) {
  wchar_t path[MAX_PATH]{};HMODULE mod{};
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
    reinterpret_cast<LPCWSTR>(&Log),&mod);
  GetModuleFileNameW(mod,path,MAX_PATH);auto name=wcsrchr(path,L'\\');
  if(name) {wcscpy_s(name+1,MAX_PATH-(name+1-path),L"DL2VR-openxr.log");_wfopen_s(&logFile,path,L"w");}
 }
 if(logFile){fprintf(logFile,"%llu %s %lld\n",GetTickCount64(),message,value);fflush(logFile);}
}
void Check(XrResult r){if(XR_FAILED(r))throw r;}
void Close() {
 trackingReady=false;
 if(trackedPending && session) {XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=trackedFrame.predictedDisplayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;xrEndFrame(session,&end);}
 trackedPending=false;
 if(localSpace)xrDestroySpace(localSpace);localSpace=XR_NULL_HANDLE;
 if(space)xrDestroySpace(space);space=XR_NULL_HANDLE;
 for(unsigned i=0;i<2;++i){if(chains[i])xrDestroySwapchain(chains[i]);chains[i]=XR_NULL_HANDLE;images[i].clear();}
 if(session)xrDestroySession(session);session=XR_NULL_HANDLE;
 if(instance)xrDestroyInstance(instance);instance=XR_NULL_HANDLE;
 if(boundDevice)boundDevice->Release();boundDevice=nullptr;running=false;
}
void Initialize(ID3D11Device* device,ID3D11Texture2D* left) {
 D3D11_TEXTURE2D_DESC desc{};left->GetDesc(&desc);width=desc.Width;height=desc.Height;format=desc.Format;
 const char* extension=XR_KHR_D3D11_ENABLE_EXTENSION_NAME;
 XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
 strcpy_s(ci.applicationInfo.applicationName,"DyingLight2VR stereo screen");
 ci.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);ci.enabledExtensionCount=1;ci.enabledExtensionNames=&extension;
 Check(xrCreateInstance(&ci,&instance));
 XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};Check(xrGetInstanceProperties(instance,&props));Log(props.runtimeName);
 XrSystemGetInfo si{XR_TYPE_SYSTEM_GET_INFO};si.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
 XrSystemId system{};Check(xrGetSystem(instance,&si,&system));
 XrViewConfigurationView recommended[2]{{XR_TYPE_VIEW_CONFIGURATION_VIEW},{XR_TYPE_VIEW_CONFIGURATION_VIEW}};uint32_t viewCount{};
 Check(xrEnumerateViewConfigurationViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&viewCount,recommended));
 if(viewCount!=2)throw XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
 for(auto& v:recommended){
  Log("recommended eye width",v.recommendedImageRectWidth);Log("recommended eye height",v.recommendedImageRectHeight);
  if(width>v.maxImageRectWidth || height>v.maxImageRectHeight ||
     width>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || height>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION) {
   Log("source exceeds runtime/D3D11 image limits");throw XR_ERROR_SWAPCHAIN_RECT_INVALID;
  }
 }
 Log("actual source eye width",width);Log("actual source eye height",height);
 if(width!=recommended[0].recommendedImageRectWidth || height!=recommended[0].recommendedImageRectHeight) {
  Log("source resolution differs from recommendation; using source-sized images with headset FOV; run Launch-VR.ps1 to restore recommended dimensions");
 }
 PFN_xrGetD3D11GraphicsRequirementsKHR requirementsFn{};
 Check(xrGetInstanceProcAddr(instance,"xrGetD3D11GraphicsRequirementsKHR",reinterpret_cast<PFN_xrVoidFunction*>(&requirementsFn)));
 XrGraphicsRequirementsD3D11KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};Check(requirementsFn(instance,system,&requirements));
 IDXGIDevice* dxgi{};IDXGIAdapter* adapter{};DXGI_ADAPTER_DESC ad{};
 HRESULT hr=device->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void**>(&dxgi));
 if(SUCCEEDED(hr)){hr=dxgi->GetAdapter(&adapter);dxgi->Release();}
 if(SUCCEEDED(hr)){hr=adapter->GetDesc(&ad);adapter->Release();}
 if(FAILED(hr) || memcmp(&ad.AdapterLuid,&requirements.adapterLuid,sizeof(LUID)) || device->GetFeatureLevel()<requirements.minFeatureLevel)
  throw XR_ERROR_GRAPHICS_DEVICE_INVALID;
 XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};binding.device=device;
 XrSessionCreateInfo sc{XR_TYPE_SESSION_CREATE_INFO};sc.next=&binding;sc.systemId=system;
 Check(xrCreateSession(instance,&sc,&session));boundDevice=device;device->AddRef();
 uint32_t count{};Check(xrEnumerateSwapchainFormats(session,0,&count,nullptr));std::vector<int64_t> formats(count);
 Check(xrEnumerateSwapchainFormats(session,count,&count,formats.data()));
 bool found=false;for(auto f:formats)if(f==format)found=true;
 if(!found)throw XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;
 XrReferenceSpaceCreateInfo rs{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};rs.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;rs.poseInReferenceSpace.orientation.w=1;
 Check(xrCreateReferenceSpace(session,&rs,&space));
 rs.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;Check(xrCreateReferenceSpace(session,&rs,&localSpace));
 for(unsigned eye=0;eye<2;++eye){
  XrSwapchainCreateInfo sw{XR_TYPE_SWAPCHAIN_CREATE_INFO};sw.usageFlags=XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
  sw.format=format;sw.sampleCount=1;sw.width=width;sw.height=height;sw.faceCount=1;sw.arraySize=1;sw.mipCount=1;
  Check(xrCreateSwapchain(session,&sw,&chains[eye]));Check(xrEnumerateSwapchainImages(chains[eye],0,&count,nullptr));
  images[eye].resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
  Check(xrEnumerateSwapchainImages(chains[eye],count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images[eye].data())));
 }
 Log("stereo VIEW-space screen ready; exact source format",format);Log("width",width);Log("height",height);
}
void Events() {
 XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
 XrResult r;
 while((r=xrPollEvent(instance,&event))==XR_SUCCESS){
  if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED){
   auto& e=*reinterpret_cast<XrEventDataSessionStateChanged*>(&event);Log("session state",e.state);
   if(e.state==XR_SESSION_STATE_READY){XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};bi.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;Check(xrBeginSession(session,&bi));running=true;}
   if(e.state==XR_SESSION_STATE_STOPPING){running=false;Check(xrEndSession(session));}
   if(e.state==XR_SESSION_STATE_EXITING || e.state==XR_SESSION_STATE_LOSS_PENDING)throw XR_ERROR_SESSION_LOST;
  } else if(event.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)throw XR_ERROR_INSTANCE_LOST;
  event={XR_TYPE_EVENT_DATA_BUFFER};
 }
 if(r!=XR_EVENT_UNAVAILABLE)Check(r);
}
}
void SetXrScreenEnabled(bool value){enabled.store(value);}
bool XrTrackingReady(){return enabled.load()&&trackingReady.load();}
bool HasTrackedPair(){std::lock_guard lock(xrMutex);return trackedPending;}
void PumpXrScreenIdle(){std::lock_guard lock(xrMutex);if(!enabled.load()){if(instance){Log("transport stopped; submitted pairs",submitted);Close();}failed=false;}}
bool BeginTrackedPair(TrackedEye eyes[2]) {
 std::lock_guard lock(xrMutex);
 if(!enabled.load() || !instance || failed || trackedPending)return false;
 try {
  Events();if(!running)return false;
  XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};Check(xrWaitFrame(session,&wait,&trackedFrame));
  XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};Check(xrBeginFrame(session,&begin));trackedPending=true;
  XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
  locate.displayTime=trackedFrame.predictedDisplayTime;locate.space=localSpace;
  XrViewState state{XR_TYPE_VIEW_STATE};uint32_t count{};
  Check(xrLocateViews(session,&locate,&state,2,&count,trackedViews));
  if(count!=2 || !trackedFrame.shouldRender || (state.viewStateFlags&(XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT))!=(XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT)) {
   XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=trackedFrame.predictedDisplayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
   Check(xrEndFrame(session,&end));trackedPending=false;return false;
  }
  for(int i=0;i<2;++i){
   auto& v=trackedViews[i];auto& o=eyes[i];
   o.quaternion[0]=v.pose.orientation.x;o.quaternion[1]=v.pose.orientation.y;o.quaternion[2]=v.pose.orientation.z;o.quaternion[3]=v.pose.orientation.w;
   o.position[0]=v.pose.position.x;o.position[1]=v.pose.position.y;o.position[2]=v.pose.position.z;
   o.fov[0]=v.fov.angleLeft;o.fov[1]=v.fov.angleRight;o.fov[2]=v.fov.angleUp;o.fov[3]=v.fov.angleDown;
  }
  trackingReady=true;return true;
 } catch(XrResult r){Log("tracked frame failure",r);Close();failed=true;return false;}
}
void CancelTrackedPair() {
 std::lock_guard lock(xrMutex);
 if(trackedPending && session){
  XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=trackedFrame.predictedDisplayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  auto r=xrEndFrame(session,&end);trackedPending=false;
  if(XR_FAILED(r)){Log("cancel tracked frame failed",r);Close();failed=true;}
 }
}
void SubmitXrScreen(ID3D11Device* device,ID3D11Texture2D* left,ID3D11Texture2D* right,uint64_t pair) {
 std::lock_guard lock(xrMutex);
 if(!enabled.load()){PumpXrScreenIdle();return;}if(failed)return;
 bool begun=false;
 XrFrameState fs{XR_TYPE_FRAME_STATE};
 try {
  if(!instance)Initialize(device,left);
  D3D11_TEXTURE2D_DESC d{};left->GetDesc(&d);
  if(device!=boundDevice || d.Width!=width || d.Height!=height || d.Format!=format) {
   // End any frame sampled against the old resources without presenting it.
   // The next pair bootstraps a fresh session from the new source description.
   Log("source changed; dropping current pair and rebuilding transport next pair");
   CancelTrackedPair();Close();failed=false;return;
  }
  const bool immersive=trackedPending;
  if(immersive){fs=trackedFrame;begun=true;trackedPending=false;}
  else {
   Events();if(!running)return;
   XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};Check(xrWaitFrame(session,&wait,&fs));
   XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};Check(xrBeginFrame(session,&begin));begun=true;
  }
  XrCompositionLayerProjectionView pv[2]{{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
  XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};projection.space=localSpace;projection.viewCount=2;projection.views=pv;
  XrCompositionLayerQuad quads[2]{{XR_TYPE_COMPOSITION_LAYER_QUAD},{XR_TYPE_COMPOSITION_LAYER_QUAD}};
  const XrCompositionLayerBaseHeader* layers[2]{};
  if(fs.shouldRender){
   ID3D11DeviceContext* context{};device->GetImmediateContext(&context);
   for(unsigned eye=0;eye<2;++eye){
    uint32_t index{};XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    auto r=xrAcquireSwapchainImage(chains[eye],&acquire,&index);if(XR_FAILED(r)){context->Release();Check(r);}
    XrSwapchainImageWaitInfo imageWait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};imageWait.timeout=XR_INFINITE_DURATION;
    r=xrWaitSwapchainImage(chains[eye],&imageWait);
    if(XR_FAILED(r)){context->Release();Check(r);}
    context->CopyResource(images[eye][index].texture,eye?right:left);
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    r=xrReleaseSwapchainImage(chains[eye],&release);if(XR_FAILED(r)){context->Release();Check(r);}
    auto& q=quads[eye];q.space=space;q.eyeVisibility=eye?XR_EYE_VISIBILITY_RIGHT:XR_EYE_VISIBILITY_LEFT;
    q.subImage.swapchain=chains[eye];q.subImage.imageRect.extent={int32_t(width),int32_t(height)};
    q.pose.orientation.w=1;q.pose.position.z=-2;q.size={3.f,3.f*height/width};
    layers[eye]=reinterpret_cast<XrCompositionLayerBaseHeader*>(&q);
    pv[eye].pose=trackedViews[eye].pose;pv[eye].fov=trackedViews[eye].fov;pv[eye].subImage=q.subImage;
   }
   context->Release();
  }
  XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=fs.predictedDisplayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  if(immersive)layers[0]=reinterpret_cast<XrCompositionLayerBaseHeader*>(&projection);
  end.layerCount=fs.shouldRender?(immersive?1:2):0;end.layers=layers;Check(xrEndFrame(session,&end));begun=false;
  if(fs.shouldRender){++submitted;if(submitted==1 || submitted%120==0){Log(immersive?"submitted tracked projection pair":"submitted bootstrap screen pair",pair);Log("total submissions",submitted);}}
 } catch(XrResult r) {
  Log("OpenXR failure; disabled until next toggle",r);
  if(begun){XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=fs.predictedDisplayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;xrEndFrame(session,&end);}
  Close();failed=true;
 }
}
