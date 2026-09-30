#include "BackbufferCapture.h"
#include <d3d11.h>
#include <atomic>
#include <vector>
#include <cstring>
#include <new>

namespace {
// One request at a time. Only the dispatcher owns GPU work; the worker writes files.
enum { Idle,Requested,Busy,Ready,Saving };
struct Slot {
    std::atomic<int> state{Idle};
    std::atomic<ULONGLONG> deadline{};
    BackbufferReport result;
    std::vector<unsigned char> pixels;
};
Slot slots[2];
template<class T> struct Com {
    T* p{};
    ~Com() { if(p) p->Release(); }
};
bool Supported(DXGI_FORMAT f) {
    return f==DXGI_FORMAT_R8G8B8A8_UNORM || f==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
           f==DXGI_FORMAT_B8G8R8A8_UNORM || f==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
           f==DXGI_FORMAT_R10G10B10A2_UNORM;
}
HRESULT ReadPixels(IDXGISwapChain* swap,Slot& slot) {
    auto& result=slot.result; auto& pixels=slot.pixels;
    Com<ID3D11Device> device;
    HRESULT hr=swap->GetDevice(__uuidof(ID3D11Device),reinterpret_cast<void**>(&device.p));
    if(FAILED(hr)) return hr;
    Com<ID3D11Texture2D> buffer;
    hr=swap->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&buffer.p));
    if(FAILED(hr)) return hr;
    D3D11_TEXTURE2D_DESC desc{}; buffer.p->GetDesc(&desc);
    result.width=desc.Width; result.height=desc.Height; result.format=desc.Format; result.samples=desc.SampleDesc.Count;
    if(!desc.Width || !desc.Height || desc.Width>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || desc.Height>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
       uint64_t(desc.Width)*desc.Height*4>256ull*1024*1024 || desc.ArraySize!=1 ||
       desc.MipLevels!=1 || desc.SampleDesc.Count!=1 || !Supported(desc.Format)) return DXGI_ERROR_UNSUPPORTED;
    // Allocate before mapping so exceptions cannot leave a resource mapped.
    try { pixels.resize(size_t(desc.Width)*desc.Height*4); }
    catch(const std::bad_alloc&) { return E_OUTOFMEMORY; }
    D3D11_TEXTURE2D_DESC stagingDesc=desc;
    stagingDesc.Usage=D3D11_USAGE_STAGING; stagingDesc.BindFlags=0;
    stagingDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ; stagingDesc.MiscFlags=0;
    Com<ID3D11Texture2D> staging;
    hr=device.p->CreateTexture2D(&stagingDesc,nullptr,&staging.p);
    if(FAILED(hr)) return hr;
    Com<ID3D11DeviceContext> context; device.p->GetImmediateContext(&context.p);
    if(!context.p) return E_NOINTERFACE;
    context.p->CopyResource(staging.p,buffer.p);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr=context.p->Map(staging.p,0,D3D11_MAP_READ,0,&mapped);
    if(FAILED(hr)) return hr;
    if(!mapped.pData || mapped.RowPitch<desc.Width*4) { context.p->Unmap(staging.p,0); return E_FAIL; }
    for(unsigned y=0;y<desc.Height;++y) {
        auto src=static_cast<const unsigned char*>(mapped.pData)+size_t(y)*mapped.RowPitch;
        auto dst=pixels.data()+size_t(y)*desc.Width*4;
        for(unsigned x=0;x<desc.Width;++x,src+=4,dst+=4) {
            if(desc.Format==DXGI_FORMAT_R10G10B10A2_UNORM) {
                unsigned packed; memcpy(&packed,src,4);
                dst[2]=((packed&1023)*255+511)/1023;
                dst[1]=(((packed>>10)&1023)*255+511)/1023;
                dst[0]=(((packed>>20)&1023)*255+511)/1023;
            } else if(desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM || desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
                dst[0]=src[2]; dst[1]=src[1]; dst[2]=src[0];
            } else memcpy(dst,src,3);
            dst[3]=255;
        }
    }
    context.p->Unmap(staging.p,0);
    return S_OK;
}
bool WriteAll(HANDLE f,const void* data,DWORD size) {
    DWORD written{};
    return WriteFile(f,data,size,&written,nullptr) && written==size;
}
}
void RequestBackbufferCapture(unsigned label) {
    auto& slot=slots[label==2 ? 1 : 0];
    auto& state=slot.state; auto& result=slot.result; auto& pixels=slot.pixels;
    int expected=Idle;
    if(!state.compare_exchange_strong(expected,Busy)) return;
    result={}; result.request=GetTickCount64(); result.label=label; slot.deadline.store(result.request+5000); pixels.clear();
    state.store(Requested);
}
static bool TrySlot(void* swap,uint64_t dispatchSequence,Slot& slot) {
    auto& state=slot.state; auto& result=slot.result;
    int expected=Requested;
    if(!state.compare_exchange_strong(expected,Busy)) return false;
    result.swap=reinterpret_cast<uintptr_t>(swap); result.dispatchSequence=dispatchSequence;
    result.result=GetTickCount64()-result.request>5000 ? HRESULT_FROM_WIN32(ERROR_TIMEOUT) :
        (swap ? ReadPixels(static_cast<IDXGISwapChain*>(swap),slot) : E_POINTER);
    state.store(Ready); return true;
}
void TryBackbufferCapture(void* swap,uint64_t dispatchSequence) {
    for(auto& slot:slots) if(TrySlot(swap,dispatchSequence,slot)) return;
}
static bool SaveSlot(const wchar_t* modulePath,BackbufferReport& report,Slot& slot) {
    auto& state=slot.state; auto& result=slot.result; auto& pixels=slot.pixels;
    if(state.load()==Requested && GetTickCount64()>slot.deadline.load()) {
        int expected=Requested;
        if(state.compare_exchange_strong(expected,Busy)) {
            result.result=HRESULT_FROM_WIN32(ERROR_TIMEOUT); state.store(Ready);
        }
    }
    int expected=Ready;
    if(!state.compare_exchange_strong(expected,Saving)) return false;
    if(SUCCEEDED(result.result)) {
        wcscpy_s(result.path,modulePath);
        auto name=wcsrchr(result.path,L'\\');
        if(!name) result.result=E_INVALIDARG;
        else {
            ++name;
            swprintf_s(name,MAX_PATH-(name-result.path),L"DL2VR-backbuffer-%llu-view%u.bmp",result.request,result.label);
            HANDLE file=CreateFileW(result.path,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(file==INVALID_HANDLE_VALUE) result.result=HRESULT_FROM_WIN32(GetLastError());
            else {
                BITMAPFILEHEADER fh{}; BITMAPINFOHEADER ih{};
                fh.bfType=0x4d42; fh.bfOffBits=sizeof(fh)+sizeof(ih); fh.bfSize=fh.bfOffBits+DWORD(pixels.size());
                ih.biSize=sizeof(ih); ih.biWidth=LONG(result.width); ih.biHeight=-LONG(result.height);
                ih.biPlanes=1; ih.biBitCount=32; ih.biCompression=BI_RGB; ih.biSizeImage=DWORD(pixels.size());
                if(!WriteAll(file,&fh,sizeof(fh)) || !WriteAll(file,&ih,sizeof(ih)) ||
                   !WriteAll(file,pixels.data(),DWORD(pixels.size()))) result.result=E_FAIL;
                CloseHandle(file);
            }
        }
    }
    report=result; pixels.clear(); state.store(Idle); return true;
}

bool SaveBackbufferCapture(const wchar_t* modulePath,BackbufferReport& report) {
    for(auto& slot:slots) if(SaveSlot(modulePath,report,slot)) return true;
    return false;
}
