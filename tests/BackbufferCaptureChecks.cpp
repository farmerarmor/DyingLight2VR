#include "BackbufferCapture.h"
#include "GpuEyePair.h"
#include <d3d11.h>
#include <cstdio>
#include <vector>
#include <cstring>
template<class T> struct Com { T* p{}; ~Com(){if(p)p->Release();} };
int main() {
    // Separate process, hidden window, software device. Never linked into winmm.
    HWND window=CreateWindowExW(0,L"STATIC",L"DL2VR capture checks",WS_POPUP,0,0,64,32,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window) return 1;
    for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM}) {
        DXGI_SWAP_CHAIN_DESC d{};d.BufferDesc.Width=64;d.BufferDesc.Height=32;d.BufferDesc.Format=format;
        d.SampleDesc.Count=1;d.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;d.BufferCount=1;d.OutputWindow=window;d.Windowed=TRUE;
        Com<IDXGISwapChain> swap;Com<ID3D11Device> device;Com<ID3D11DeviceContext> context;
        auto hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,&swap.p,&device.p,nullptr,&context.p);
        if(FAILED(hr)) { printf("FAIL device %08lx\n",hr); return 2; }
        Com<ID3D11Texture2D> buffer;Com<ID3D11RenderTargetView> view;
        if(FAILED(swap.p->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&buffer.p)))) return 3;
        if(FAILED(device.p->CreateRenderTargetView(buffer.p,nullptr,&view.p))) return 4;
        const float color[]={0.25f,0.5f,0.75f,1};context.p->ClearRenderTargetView(view.p,color);
        if(SUCCEEDED(PreserveGpuEye(swap.p,2,700)))return 16;
        if(FAILED(PreserveGpuEye(swap.p,1,700)))return 17;
        wchar_t path[MAX_PATH]{};GetModuleFileNameW(nullptr,path,MAX_PATH);
        BackbufferReport r{};
        RequestBackbufferCapture(1);
        if(SaveBackbufferCapture(path,r)) return 5;
        TryBackbufferCapture(swap.p,123);
        TryBackbufferCapture(nullptr,999); // A second attempt cannot replace a ready capture.
        const float secondColor[]={0.75f,0.25f,0.5f,1};
        context.p->ClearRenderTargetView(view.p,secondColor);
        if(SUCCEEDED(PreserveGpuEye(swap.p,2,701)))return 18;
        if(FAILED(PreserveGpuEye(swap.p,2,700)))return 19;
        RequestBackbufferCapture(2);
        TryBackbufferCapture(swap.p,124);
        if(!SaveBackbufferCapture(path,r) || FAILED(r.result) || r.dispatchSequence!=123 || r.width!=64 || r.height!=32 || r.format!=format) return 6;
        FILE* f{};_wfopen_s(&f,r.path,L"rb");if(!f)return 7;
        std::vector<unsigned char> bytes(54+64*32*4);auto n=fread(bytes.data(),1,bytes.size(),f);fclose(f);
        if(n!=bytes.size() || bytes[0]!='B' || bytes[1]!='M') return 8;
        int height;memcpy(&height,bytes.data()+22,4);if(height!=-32)return 9;
        for(size_t i=54;i<bytes.size();i+=4)
            if(bytes[i]!=191 || bytes[i+1]!=128 || bytes[i+2]!=64 || bytes[i+3]!=255) return 10;
        if(!SaveBackbufferCapture(path,r) || FAILED(r.result) || r.label!=2 || r.dispatchSequence!=124) return 11;
        _wfopen_s(&f,r.path,L"rb");if(!f)return 12;
        n=fread(bytes.data(),1,bytes.size(),f);fclose(f);
        if(n!=bytes.size())return 13;
        for(size_t i=54;i<bytes.size();i+=4)
            if(bytes[i]!=128 || bytes[i+1]!=64 || bytes[i+2]!=191 || bytes[i+3]!=255) return 14;
        if(SaveBackbufferCapture(path,r)) return 15;
        ReleaseGpuEyes();
        if(SUCCEEDED(PreserveGpuEye(swap.p,2,700)))return 20;
        if(format==DXGI_FORMAT_R8G8B8A8_UNORM) {
            view.p->Release();view.p=nullptr;buffer.p->Release();buffer.p=nullptr;
            context.p->ClearState();context.p->Flush();
            if(FAILED(swap.p->ResizeBuffers(1,4096,4320,format,0)))return 21;
            if(FAILED(PreserveGpuEye(swap.p,1,702)) || FAILED(PreserveGpuEye(swap.p,2,702)))return 22;
            ReleaseGpuEyes();
            Sleep(20); // Distinct capture request timestamp from the small pair.
            RequestBackbufferCapture(1);TryBackbufferCapture(swap.p,125);
            if(!SaveBackbufferCapture(path,r) || FAILED(r.result) || r.width!=4096 || r.height!=4320){printf("Large capture failed hr=%08lx dimensions=%u,%u path=%ls\n",r.result,r.width,r.height,r.path);return 23;}
            WIN32_FILE_ATTRIBUTE_DATA info{};
            if(!GetFileAttributesExW(r.path,GetFileExInfoStandard,&info) || info.nFileSizeHigh || info.nFileSizeLow!=54+4096*4320*4)return 24;
            puts("PASS 4096x4320 GPU pair and screenshot above former allocation caps");
        }
        printf("PASS capture format=%u two independent images, exact colors, top-down BMP, one-shot ownership\n",unsigned(format));
        printf("PASS GPU storage format=%u matched pair accepted, missing/mismatched first eye rejected, desktop pixels unchanged\n",unsigned(format));
        Sleep(20); // GetTickCount64 typically has a 10-16 ms tick resolution.
    }
    DestroyWindow(window);return 0;
}
