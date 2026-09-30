#include <windows.h>
#include <d3d11.h>
#include <filesystem>
#include <cstdio>
#include "DlssCapture.h"
#define CHECK(x) do{if(!(x)){printf("FAIL %d %s\n",__LINE__,#x);return 1;}}while(0)
int main(){
 ID3D11Device* d{};ID3D11DeviceContext* c{};CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&c)));
 D3D11_TEXTURE2D_DESC desc{};desc.Width=3;desc.Height=2;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R32_FLOAT;
 ID3D11Texture2D* t{};float data[]={1,2,3,4,5,6};D3D11_SUBRESOURCE_DATA init{data,12,0};CHECK(SUCCEEDED(d->CreateTexture2D(&desc,&init,&t)));
 DlssCapture::Image im;CHECK(SUCCEEDED(DlssCapture::Read(c,t,im)));CHECK(im.header.bytes==24 && im.header.pitch==12 && memcmp(im.pixels.data(),data,24)==0);
 CHECK(FAILED(DlssCapture::Read(c,nullptr,im)));
 for(auto fmt:{DXGI_FORMAT_R16G16_TYPELESS,DXGI_FORMAT_R32G8X24_TYPELESS}) {
     auto td=desc;td.Format=fmt;ID3D11Texture2D* typeless{};
     CHECK(SUCCEEDED(d->CreateTexture2D(&td,nullptr,&typeless)));
     unsigned bytes=fmt==DXGI_FORMAT_R32G8X24_TYPELESS?8:4;unsigned char raw[48];for(unsigned i=0;i<48;++i)raw[i]=static_cast<unsigned char>(i);
     c->UpdateSubresource(typeless,0,nullptr,raw,3*bytes,0);
     CHECK(SUCCEEDED(DlssCapture::Read(c,typeless,im)) && im.header.bytes==6*bytes && memcmp(im.pixels.data(),raw,6*bytes)==0);typeless->Release();
 }
 uintptr_t wrapper[3]={0,0,reinterpret_cast<uintptr_t>(t)};uintptr_t packet[10]{};for(unsigned off:{8u,0x10u,0x20u,0x38u})packet[off/8]=reinterpret_cast<uintptr_t>(wrapper);
 CHECK(DlssCapture::Resource(packet,8)==t);CHECK(!DlssCapture::Resource(packet,0));
 unsigned char constants[0x1b0]{};constants[0]=123;
 CHECK(DlssCapture::Arm());CHECK(!DlssCapture::Arm());CHECK(!DlssCapture::Begin(c,2,7,packet));
 for(unsigned eye=1;eye<=2;++eye){DlssCapture::Constants(eye,constants);CHECK(DlssCapture::Begin(c,eye,7,packet));DlssCapture::End(c,eye,packet);}
 CHECK(DlssCapture::state==3 && DlssCapture::haveConstants[0] && DlssCapture::haveConstants[1]);
 CHECK(DlssCapture::images[1][3].header.bytes==24 && DlssCapture::constants[1][0]==123);
 wchar_t tmp[MAX_PATH];GetTempPathW(MAX_PATH,tmp);auto dir=std::filesystem::path(tmp)/(L"DL2VR-dlss-test-"+std::to_wstring(GetCurrentProcessId()));std::filesystem::create_directory(dir);
 unsigned failures{};CHECK(DlssCapture::Save(dir.c_str(),failures) && !failures);auto folder=dir/(L"DL2VR-dlss-"+std::to_wstring(DlssCapture::request));CHECK(std::filesystem::file_size(folder/L"eye2-buffer3.bin")==sizeof(DlssCapture::Header)+24);CHECK(std::filesystem::file_size(folder/L"eye1-constants.bin")==0x1b0);
 CHECK(DlssCapture::Arm());CHECK(DlssCapture::Begin(c,1,8,packet));DlssCapture::End(c,1,packet);CHECK(!DlssCapture::Begin(c,2,9,packet) && DlssCapture::state==1);DlssCapture::state=0;
 t->Release();c->Release();d->Release();puts("PASS DLSS snapshot row packing, native packet resources, per-eye pair matching, constants and saved files");
}
