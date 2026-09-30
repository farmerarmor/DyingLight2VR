#include <windows.h>
#include <d3d11.h>
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <cstdio>
#include <cstring>
int main() {
 const char* extension=XR_KHR_D3D11_ENABLE_EXTENSION_NAME;
 XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};ci.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);
 strcpy_s(ci.applicationInfo.applicationName,"DL2VR runtime check");ci.enabledExtensionCount=1;ci.enabledExtensionNames=&extension;
 XrInstance instance{};auto r=xrCreateInstance(&ci,&instance);
 if(XR_FAILED(r)){printf("xrCreateInstance=%d\n",r);return 1;}
 XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};r=xrGetInstanceProperties(instance,&props);
 printf("runtime=%s propertiesResult=%d\n",props.runtimeName,r);
 XrSystemGetInfo si{XR_TYPE_SYSTEM_GET_INFO};si.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;XrSystemId system{};
 r=xrGetSystem(instance,&si,&system);printf("xrGetSystem=%d\n",r);
 if(XR_SUCCEEDED(r)){
  XrViewConfigurationView views[2]{{XR_TYPE_VIEW_CONFIGURATION_VIEW},{XR_TYPE_VIEW_CONFIGURATION_VIEW}};uint32_t count{};
  r=xrEnumerateViewConfigurationViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&count,views);
  if(XR_SUCCEEDED(r)&&count==2)for(unsigned i=0;i<2;++i)printf("eye=%u recommended=%ux%u max=%ux%u\n",i,views[i].recommendedImageRectWidth,views[i].recommendedImageRectHeight,views[i].maxImageRectWidth,views[i].maxImageRectHeight);
  PFN_xrGetD3D11GraphicsRequirementsKHR fn{};
  r=xrGetInstanceProcAddr(instance,"xrGetD3D11GraphicsRequirementsKHR",reinterpret_cast<PFN_xrVoidFunction*>(&fn));
  if(XR_SUCCEEDED(r)){XrGraphicsRequirementsD3D11KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};r=fn(instance,system,&req);printf("D3D11 requirements=%d minFeatureLevel=%x adapterLuid=%lx:%lx\n",r,req.minFeatureLevel,req.adapterLuid.HighPart,req.adapterLuid.LowPart);}
 }
 xrDestroyInstance(instance);return XR_FAILED(r)?2:0;
}
