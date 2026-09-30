#pragma once
#include <d3d11.h>
#include <atomic>
#include <vector>
#include <cstdio>
#include <cstring>
namespace DlssCapture {
struct Header {unsigned magic{0x31434c44},width{},height{},format{},pitch{},bytes{};HRESULT result{E_PENDING};unsigned reserved{};uint64_t resource{};};
struct Image {Header header;std::vector<unsigned char> pixels;};
inline Image images[2][4];
inline unsigned char constants[2][0x1b0]{};inline bool haveConstants[2]{};
inline std::atomic<unsigned> state{}; // idle, armed, capturing, ready, saving
inline uint64_t request{},pair{};inline unsigned nextEye{1};
inline bool Arm(){unsigned idle=0;if(!state.compare_exchange_strong(idle,4))return false;request=GetTickCount64();pair=0;nextEye=1;memset(haveConstants,0,sizeof(haveConstants));state=1;return true;}
inline void Constants(unsigned eye,const void* packet){auto s=state.load();if((s==1 || s==2) && eye==nextEye && eye>=1 && eye<=2){memcpy(constants[eye-1],packet,0x1b0);haveConstants[eye-1]=true;}}
inline ID3D11Resource* Resource(void* packet,unsigned offset) {
    // Exact native evaluator: packet[offset] is wrapper; wrapper+10 is resource.
    __try {auto wrapper=*reinterpret_cast<uintptr_t*>(static_cast<unsigned char*>(packet)+offset);return wrapper?*reinterpret_cast<ID3D11Resource**>(wrapper+0x10):nullptr;}__except(EXCEPTION_EXECUTE_HANDLER){return nullptr;}
}
inline HRESULT Read(ID3D11DeviceContext* c,ID3D11Resource* resource,Image& image) {
    image.header={};image.pixels.clear();auto& h=image.header;h.resource=reinterpret_cast<uint64_t>(resource);
    if(!c || !resource)return h.result=E_POINTER;
    ID3D11Texture2D* texture{};auto hr=resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&texture));if(FAILED(hr))return h.result=hr;
    D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);h.width=d.Width;h.height=d.Height;h.format=d.Format;
    unsigned bpp=(d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT || d.Format==DXGI_FORMAT_R32G8X24_TYPELESS)?8:4;
    bool format=d.Format==DXGI_FORMAT_R16G16_TYPELESS || d.Format==DXGI_FORMAT_R32G8X24_TYPELESS || d.Format==DXGI_FORMAT_R11G11B10_FLOAT || d.Format==DXGI_FORMAT_R16G16_FLOAT || d.Format==DXGI_FORMAT_R32_FLOAT || d.Format==DXGI_FORMAT_R32_TYPELESS || d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT;
    if(!format || d.ArraySize!=1 || d.SampleDesc.Count!=1 || !d.Width || !d.Height || uint64_t(d.Width)*d.Height*bpp>192ull*1024*1024){texture->Release();return h.result=DXGI_ERROR_UNSUPPORTED;}
    h.pitch=d.Width*bpp;h.bytes=h.pitch*d.Height;
    try{image.pixels.resize(h.bytes);}catch(...){texture->Release();return h.result=E_OUTOFMEMORY;}
    d.MipLevels=1;d.BindFlags=0;d.MiscFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ID3D11Device* device{};c->GetDevice(&device);ID3D11Texture2D* staging{};hr=device->CreateTexture2D(&d,nullptr,&staging);device->Release();
    if(SUCCEEDED(hr)) {
        c->CopySubresourceRegion(staging,0,0,0,0,texture,0,nullptr);
        D3D11_MAPPED_SUBRESOURCE m{};hr=c->Map(staging,0,D3D11_MAP_READ,0,&m);
        if(SUCCEEDED(hr)){for(unsigned y=0;y<h.height;++y)memcpy(image.pixels.data()+size_t(y)*h.pitch,static_cast<unsigned char*>(m.pData)+size_t(y)*m.RowPitch,h.pitch);c->Unmap(staging,0);}
        staging->Release();
    }
    texture->Release();if(FAILED(hr)){image.pixels.clear();h.bytes=0;}return h.result=hr;
}
inline bool Begin(ID3D11DeviceContext* c,unsigned eye,uint64_t currentPair,void* packet) {
    auto s=state.load();if((s!=1 && s!=2) || eye!=nextEye || eye<1 || eye>2)return false;
    if(s==1){if(eye!=1)return false;pair=currentPair;state=2;}
    if(pair!=currentPair){nextEye=1;state=1;memset(haveConstants,0,sizeof(haveConstants));return false;}
    constexpr unsigned offsets[]={8,0x10,0x38};
    for(unsigned i=0;i<3;++i)Read(c,Resource(packet,offsets[i]),images[eye-1][i]);return true;
}
inline void End(ID3D11DeviceContext* c,unsigned eye,void* packet) {
    Read(c,Resource(packet,0x20),images[eye-1][3]);
    if(eye==1)nextEye=2;else state=3;
}
inline bool Save(const wchar_t* directory,unsigned& failures) {
    unsigned ready=3;if(!state.compare_exchange_strong(ready,4))return false;failures=0;
    wchar_t folder[MAX_PATH],path[MAX_PATH];swprintf_s(folder,L"%s\\DL2VR-dlss-%llu",directory,request);
    if(!CreateDirectoryW(folder,nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS){++failures;state=0;return true;}
    for(unsigned e=0;e<2;++e){
        for(unsigned i=0;i<4;++i){auto& im=images[e][i];if(FAILED(im.header.result))++failures;
            swprintf_s(path,L"%s\\eye%u-buffer%u.bin",folder,e+1,i);FILE* f{};_wfopen_s(&f,path,L"wb");
            if(f){bool ok=fwrite(&im.header,sizeof(im.header),1,f)==1;if(ok && !im.pixels.empty())ok=fwrite(im.pixels.data(),1,im.pixels.size(),f)==im.pixels.size();if(fclose(f))ok=false;if(!ok)++failures;}else ++failures;
            std::vector<unsigned char>().swap(im.pixels);
        }
        if(haveConstants[e]){swprintf_s(path,L"%s\\eye%u-constants.bin",folder,e+1);FILE* f{};_wfopen_s(&f,path,L"wb");if(f){bool ok=fwrite(constants[e],1,0x1b0,f)==0x1b0;if(fclose(f))ok=false;if(!ok)++failures;}else ++failures;}
    }
    swprintf_s(path,L"%s\\pair.txt",folder);FILE* f{};_wfopen_s(&f,path,L"w");if(f){fprintf(f,"pair=%llu buffers: 0=input-color 1=motion 2=depth 3=output-color; constants=%u/%u\n",pair,haveConstants[0],haveConstants[1]);if(fclose(f))++failures;}else ++failures;
    state=0;return true;
}
}
