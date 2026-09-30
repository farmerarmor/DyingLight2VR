#pragma once
#include <d3d11.h>
#include <atomic>
#include <mutex>
#include "FlashlightShadowProjection.h"
namespace FlashlightShadow {
inline std::atomic<bool> enabled{true};
inline std::atomic<uint64_t> created{},copies{},applied{},failed{},missing{};
struct Shader {void* original{};ID3D11PixelShader* corrected{};uint64_t hash{};};
inline std::mutex registryLock;
inline Shader shaders[32];
inline std::atomic<void*> candidates[32];
inline thread_local bool boundTarget{};
inline void Bound(void* ps){boundTarget=false;if(ps)for(auto& p:candidates)if(p.load()==ps){boundTarget=true;break;}}
inline unsigned Index(uint64_t hash){
    return (hash==0xb248b0205e047a2bull || hash==0x0678cfb133ec65d1ull)?0:
           (hash==0x4ce5d78b6c06bdfbull || hash==0x0e907ded7c1d61eeull)?1:
           (hash==0xc1344be4b3f10694ull || hash==0x4322e80586869a35ull)?2:3;
}
template<class T> inline void Release(T*& p){if(p)p->Release();p=nullptr;}
inline bool Register(void* original,const void* code,unsigned bytes,unsigned stage){
    std::lock_guard guard(registryLock);
    for(unsigned i=0;i<32;++i)if(shaders[i].original==original){candidates[i]=nullptr;Release(shaders[i].corrected);shaders[i]={};}
    if(!original || stage!=0 || Index(ShaderHash(code,bytes))==3)return false;
    std::vector<unsigned char> patched;if(!FlashlightShadowCode::Patch(code,bytes,patched)){++failed;return false;}
    ID3D11Device* device{};static_cast<ID3D11PixelShader*>(original)->GetDevice(&device);
    ID3D11PixelShader* ps{};auto hr=device->CreatePixelShader(patched.data(),patched.size(),nullptr,&ps);device->Release();
    if(FAILED(hr)){++failed;return false;}
    for(unsigned i=0;i<32;++i)if(!shaders[i].original){shaders[i]={original,ps,ShaderHash(code,bytes)};candidates[i]=original;++created;return true;}
    ps->Release();++failed;return false;
}
struct Snapshot {
    ID3D11Texture2D* depth{};ID3D11ShaderResourceView* view{};
    ID3D11Buffer* camera{};ID3D11Buffer* material{};
    D3D11_TEXTURE2D_DESC desc{};D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
    uint64_t pair{};bool valid{};
    void Clear(){Release(view);Release(depth);Release(camera);Release(material);valid=false;pair=0;}
};
inline Snapshot snapshots[3];
inline ID3D11DeviceContext* context{};
inline thread_local unsigned eye{};
inline thread_local uint64_t pair{};
inline bool pairEnabled{};
inline uint64_t leftPair{};
inline void Begin(ID3D11DeviceContext* c,unsigned e,uint64_t p){
    if(context!=c){for(auto& s:snapshots)s.Clear();context=c;}
    eye=e;pair=p;
    if(e==1){leftPair=p;pairEnabled=enabled.load();for(auto& s:snapshots)s.valid=false;}
    if(e && c){ID3D11PixelShader* ps{};c->PSGetShader(&ps,nullptr,nullptr);Bound(ps);Release(ps);}
}
inline void End(){eye=0;}
inline bool CopyBuffer(ID3D11DeviceContext* c,ID3D11Device* device,ID3D11Buffer* src,ID3D11Buffer*& dst,unsigned minimum){
    if(!src)return false;D3D11_BUFFER_DESC d{};src->GetDesc(&d);if(d.ByteWidth<minimum)return false;
    D3D11_BUFFER_DESC old{};if(dst)dst->GetDesc(&old);if(dst && old.ByteWidth!=d.ByteWidth)Release(dst);
    d.Usage=D3D11_USAGE_DEFAULT;d.CPUAccessFlags=0;d.MiscFlags=0;d.StructureByteStride=0;d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    if(!dst && FAILED(device->CreateBuffer(&d,nullptr,&dst)))return false;c->CopyResource(dst,src);return true;
}
inline bool Capture(ID3D11DeviceContext* c,Snapshot& s){
    ID3D11ShaderResourceView* view{};c->PSGetShaderResources(1,1,&view);if(!view)return false;
    ID3D11Resource* resource{};view->GetResource(&resource);ID3D11Texture2D* texture{};
    auto hr=resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&texture));resource->Release();
    D3D11_SHADER_RESOURCE_VIEW_DESC vd{};view->GetDesc(&vd);view->Release();if(FAILED(hr))return false;
    D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
    if(d.ArraySize!=1 || d.SampleDesc.Count!=1 || d.MipLevels!=1 || vd.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D){texture->Release();return false;}
    ID3D11Device* device{};c->GetDevice(&device);
    if(s.depth && (d.Width!=s.desc.Width || d.Height!=s.desc.Height || d.Format!=s.desc.Format || memcmp(&vd,&s.viewDesc,sizeof(vd))))s.Clear();
    if(!s.depth){
        s.desc=d;s.viewDesc=vd;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;d.CPUAccessFlags=0;d.MiscFlags=0;
        hr=device->CreateTexture2D(&d,nullptr,&s.depth);
        if(SUCCEEDED(hr))hr=device->CreateShaderResourceView(s.depth,&vd,&s.view);
    }
    ID3D11Buffer* camera{},*material{};c->VSGetConstantBuffers(2,1,&camera);c->VSGetConstantBuffers(0,1,&material);
    bool ok=SUCCEEDED(hr) && s.view && CopyBuffer(c,device,camera,s.camera,33*16) && CopyBuffer(c,device,material,s.material,4*16);
    if(ok){c->CopyResource(s.depth,texture);s.pair=pair;s.valid=true;++copies;}else{s.valid=false;++failed;}
    Release(camera);Release(material);device->Release();texture->Release();return ok;
}
// The shader/resource override is confined to one draw. RAII restores even
// empty bindings; the engine's state cache never sees a lasting mutation.
struct DrawScope {
    ID3D11DeviceContext* c{};ID3D11PixelShader* original{};
    ID3D11ShaderResourceView* oldView{};ID3D11Buffer* oldBuffers[2]{};
    DrawScope(ID3D11DeviceContext* ctx){
        if(!eye || !boundTarget || ctx!=context || !pairEnabled || pair!=leftPair)return;
        ID3D11PixelShader* ps{};ctx->PSGetShader(&ps,nullptr,nullptr);if(!ps)return;
        ID3D11PixelShader* replacement{};unsigned index=3;
        {std::lock_guard guard(registryLock);for(auto& s:shaders)if(s.original==ps){replacement=s.corrected;replacement->AddRef();index=Index(s.hash);break;}}
        if(!replacement){ps->Release();return;}
        auto& s=snapshots[index];
        if(eye==1){if(!s.valid)Capture(ctx,s);replacement->Release();ps->Release();return;}
        if(!s.valid || s.pair!=pair){++missing;replacement->Release();ps->Release();return;}
        ctx->PSGetShaderResources(1,1,&oldView);
        ID3D11Resource* current{};ID3D11Texture2D* texture{};D3D11_TEXTURE2D_DESC desc{};
        if(oldView){oldView->GetResource(&current);current->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&texture));Release(current);}
        if(texture){texture->GetDesc(&desc);Release(texture);}
        if(desc.Width!=s.desc.Width || desc.Height!=s.desc.Height || desc.Format!=s.desc.Format || desc.SampleDesc.Count!=1){
            ++missing;Release(oldView);replacement->Release();ps->Release();return;
        }
        c=ctx;original=ps;c->PSGetConstantBuffers(12,2,oldBuffers);
        ID3D11Buffer* buffers[]={s.camera,s.material};
        c->PSSetShaderResources(1,1,&s.view);c->PSSetConstantBuffers(12,2,buffers);c->PSSetShader(replacement,nullptr,0);
        replacement->Release();++applied;
    }
    ~DrawScope(){if(!c)return;c->PSSetShader(original,nullptr,0);c->PSSetShaderResources(1,1,&oldView);c->PSSetConstantBuffers(12,2,oldBuffers);
        Release(original);Release(oldView);Release(oldBuffers[0]);Release(oldBuffers[1]);}
};
}
