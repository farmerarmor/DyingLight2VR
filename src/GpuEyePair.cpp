#include "GpuEyePair.h"
#include "XrScreen.h"
#include <d3d11.h>
namespace {
template<class T> struct Com {
 T* p{};
 void reset(){if(p){p->Release();p=nullptr;}}
 ~Com(){reset();}
};
struct Pair {
 Com<ID3D11Device> device;
 Com<ID3D11Texture2D> eyes[2];
 D3D11_TEXTURE2D_DESC desc{};
 uintptr_t swap{};
 uint64_t pair{};
 bool first{};
};
thread_local Pair saved;
}
void ReleaseGpuEyes() {
 saved.eyes[0].reset();saved.eyes[1].reset();saved.device.reset();
 saved.first=false;saved.swap=0;saved.pair=0;saved.desc={};
}
HRESULT PreserveGpuEye(void* opaque,unsigned eye,uint64_t pair) {
 if(!opaque || eye<1 || eye>2 || !pair) return E_INVALIDARG;
 auto swap=static_cast<IDXGISwapChain*>(opaque);
 Com<ID3D11Device> device;
 HRESULT hr=swap->GetDevice(__uuidof(ID3D11Device),reinterpret_cast<void**>(&device.p));
 if(FAILED(hr))return hr;
 Com<ID3D11Texture2D> backbuffer;
 hr=swap->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&backbuffer.p));
 if(FAILED(hr))return hr;
 D3D11_TEXTURE2D_DESC d{};backbuffer.p->GetDesc(&d);
 if(!d.Width || !d.Height || d.Width>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || d.Height>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
    d.ArraySize!=1 || d.MipLevels!=1 || d.SampleDesc.Count!=1 ||
    !(d.Format==DXGI_FORMAT_R8G8B8A8_UNORM || d.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
      d.Format==DXGI_FORMAT_B8G8R8A8_UNORM || d.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || d.Format==DXGI_FORMAT_R10G10B10A2_UNORM)) return DXGI_ERROR_UNSUPPORTED;
 bool changed=saved.device.p!=device.p || saved.swap!=reinterpret_cast<uintptr_t>(swap) ||
  saved.desc.Width!=d.Width || saved.desc.Height!=d.Height || saved.desc.Format!=d.Format;
 if(changed || !saved.eyes[0].p || !saved.eyes[1].p) {
  ReleaseGpuEyes();
  if(eye!=1)return E_UNEXPECTED;
  D3D11_TEXTURE2D_DESC copy=d;copy.Usage=D3D11_USAGE_DEFAULT;copy.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  copy.CPUAccessFlags=0;copy.MiscFlags=0;
  for(auto& texture:saved.eyes) {
   hr=device.p->CreateTexture2D(&copy,nullptr,&texture.p);
   if(FAILED(hr)){ReleaseGpuEyes();return hr;}
  }
  saved.device.p=device.p;device.p->AddRef();saved.swap=reinterpret_cast<uintptr_t>(swap);saved.desc=d;
 }
 if(eye==2 && (!saved.first || saved.pair!=pair))return E_UNEXPECTED;
 Com<ID3D11DeviceContext> context;device.p->GetImmediateContext(&context.p);
 if(!context.p)return E_NOINTERFACE;
 context.p->CopyResource(saved.eyes[eye-1].p,backbuffer.p);
 if(eye==1){saved.pair=pair;saved.first=true;}
 else {
  saved.first=false;
  SubmitXrScreen(device.p,saved.eyes[0].p,saved.eyes[1].p,pair);
 }
 return S_OK;
}
