#pragma once
#include <d3d11.h>
#include <atomic>
#include <cstdint>
namespace TextureHistory {
// Exact consumers verified in the GPU trace. No resource-address constants.
inline bool Target(uint64_t h) {return h==0xe275ecd626c81c62ull || h==0x6b258d38d343a365ull || h==0x637fddd208eb09bdull;}
struct Shader {std::atomic<void*> native{};std::atomic<uint64_t> hash{};};
inline Shader shaders[64];
inline void Register(void* native,uint64_t hash,unsigned stage) {
    for(auto& s:shaders)if(s.native==native)s.native=nullptr;
    if(!native || stage || !Target(hash))return;
    for(auto& s:shaders)if(!s.native){s.hash=hash;s.native=native;return;}
}
inline uint64_t Identify(void* native) {if(native)for(auto& s:shaders)if(s.native==native)return s.hash;return 0;}
struct Entry {
    ID3D11Texture2D* source{};ID3D11Texture2D* saved[2]{};bool valid[2]{};bool seen{};
    void Clear(){for(auto& p:saved)if(p){p->Release();p=nullptr;}if(source)source->Release();source=nullptr;valid[0]=valid[1]=seen=false;}
};
// Color mip chain, previous depth, SSR color/confidence, water accumulation.
inline Entry entries[5];
inline ID3D11DeviceContext* context{}; // owned by the game's live swapchain
inline thread_local unsigned eye{},epoch{};
inline thread_local uint64_t boundHash{};
inline unsigned bankEpoch{};
inline std::atomic<uint64_t> discovered{},restored{},saved{},failed{};
inline void Clear(){for(auto& e:entries)e.Clear();}
inline void Begin(unsigned nextEye,unsigned nextEpoch) {
    eye=(nextEye<=2)?nextEye:0;epoch=nextEpoch;
    if(!eye || !context)return;
    if(bankEpoch!=epoch){Clear();bankEpoch=epoch;}
    ID3D11PixelShader* ps{};context->PSGetShader(&ps,nullptr,nullptr);boundHash=Identify(ps);if(ps)ps->Release();
    for(auto& e:entries){e.seen=false;if(e.source && e.valid[eye-1]){context->CopyResource(e.source,e.saved[eye-1]);++restored;}}
}
inline void ObserveView(ID3D11DeviceContext* c,unsigned semantic,ID3D11View* view) {
    if(!view)return;
    ID3D11Resource* resource{};view->GetResource(&resource);view->Release();
    ID3D11Texture2D* texture{};auto hr=resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&texture));resource->Release();if(FAILED(hr))return;
    auto& e=entries[semantic];
    if(e.source!=texture) {
        e.Clear();D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
        // Copies preserve all native mips. Reject arrays/MSAA/unexpected usage.
        if(d.ArraySize!=1 || d.SampleDesc.Count!=1 || d.Usage!=D3D11_USAGE_DEFAULT){texture->Release();++failed;return;}
        d.BindFlags=0;d.CPUAccessFlags=0;d.MiscFlags=0;
        ID3D11Device* device{};c->GetDevice(&device);
        hr=device->CreateTexture2D(&d,nullptr,&e.saved[0]);
        if(SUCCEEDED(hr))hr=device->CreateTexture2D(&d,nullptr,&e.saved[1]);device->Release();
        if(FAILED(hr)){e.Clear();texture->Release();++failed;return;}
        e.source=texture;texture=nullptr;++discovered;
        // First discovery cannot reconstruct last frame. Populate only after
        // this eye finishes; the next pair then has trustworthy eye ownership.
    }
    if(texture)texture->Release();e.seen=true;
}
inline void Observe(ID3D11DeviceContext* c,unsigned semantic,unsigned slot) {
    ID3D11ShaderResourceView* view{};c->PSGetShaderResources(slot,1,&view);ObserveView(c,semantic,view);
}
inline void ObserveWaterOutput(ID3D11DeviceContext* c) {
    // t0 is a scratch copy of the persistent second water output. Bank the
    // original output so next eye's pre-water copy receives the right history.
    ID3D11RenderTargetView* outputs[2]{};c->OMGetRenderTargets(2,outputs,nullptr);
    if(outputs[0])outputs[0]->Release();ObserveView(c,4,outputs[1]);
}
inline void BeforeDraw(ID3D11DeviceContext* c) {
    if(!eye || c!=context || !boundHash)return;
    if(boundHash==0xe275ecd626c81c62ull)Observe(c,0,0);
    else if(boundHash==0x6b258d38d343a365ull){Observe(c,0,1);Observe(c,1,18);ObserveWaterOutput(c);}
    else if(boundHash==0x637fddd208eb09bdull){Observe(c,2,1);Observe(c,3,4);}
}
inline void End() {
    if(eye && context)for(auto& e:entries) {
        if(e.source && e.seen){context->CopyResource(e.saved[eye-1],e.source);e.valid[eye-1]=true;++saved;}
        else e.valid[eye-1]=false;
    }
    eye=0;
}
}
