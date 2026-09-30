#pragma once
#include <d3d11.h>
#include <atomic>
#include <cstdio>
namespace GpuTrace {
struct Resource {uint64_t id{};unsigned width{},height{},format{},mips{},array{},samples{};};
struct Record {
    uint64_t pair{},hash{},shader{};unsigned eye{},kind{};
    Resource reads[32]{},writes[9]{};
};
struct Header {uint64_t magic{0x3152435455504744ull},request{},pair{};unsigned count{},dropped{},recordBytes{sizeof(Record)},isolation{};};
inline constexpr unsigned capacity=24000;
inline Record* records{};inline Header header{};
inline std::atomic<unsigned> state{},activeEye{};
inline uint64_t currentPair{};inline unsigned count{},dropped{},nextEye{1};inline DWORD captureThread{};
inline uint64_t (*identity)(void*,unsigned){};
inline bool Arm() {
    unsigned idle=0;if(!state.compare_exchange_strong(idle,4))return false;
    if(!records)records=static_cast<Record*>(VirtualAlloc(nullptr,sizeof(Record)*capacity,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!records){state=0;return false;}
    header={};header.request=GetTickCount64();count=dropped=0;nextEye=1;state=1;return true;
}
inline bool Begin(unsigned eye,uint64_t pair,bool isolation) {
    unsigned s=state.load();if((s!=1 && s!=2) || eye<1 || eye>2)return false;
    if(s==1){if(eye!=1)return false;header.pair=pair;header.isolation=isolation;state=2;}
    if(pair>header.pair+1 || pair<header.pair || eye!=nextEye) {
        state=1;count=dropped=0;nextEye=1;return false;
    }
    currentPair=pair;captureThread=GetCurrentThreadId();activeEye=eye;return true;
}
inline void End() {
    unsigned eye=activeEye.exchange(0);if(!eye)return;
    nextEye=eye==1?2:1;
    if(eye==2 && currentPair==header.pair+1){header.count=count;header.dropped=dropped;state=3;}
}
inline Resource Describe(ID3D11Resource* resource) {
    Resource result{};if(!resource)return result;result.id=reinterpret_cast<uint64_t>(resource);
    ID3D11Texture2D* texture{};
    if(SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&texture)))) {
        D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);texture->Release();
        result.width=d.Width;result.height=d.Height;result.format=d.Format;result.mips=d.MipLevels;result.array=d.ArraySize;result.samples=d.SampleDesc.Count;
    }
    return result;
}
inline Resource View(ID3D11View* view) {
    if(!view)return {};ID3D11Resource* resource{};view->GetResource(&resource);
    auto out=Describe(resource);if(resource)resource->Release();return out;
}
inline Record* Next(unsigned kind) {
    unsigned eye=activeEye.load();if(!eye || GetCurrentThreadId()!=captureThread)return nullptr;
    if(count>=capacity){++dropped;return nullptr;}
    auto r=&records[count++];*r={};r->eye=eye;r->pair=currentPair;r->kind=kind;return r;
}
inline void Draw(ID3D11DeviceContext* c,bool compute) {
    auto r=Next(compute?5:0);if(!r)return;
    ID3D11ShaderResourceView* views[32]{};
    if(compute){ID3D11ComputeShader* shader{};c->CSGetShader(&shader,nullptr,nullptr);if(shader){r->shader=reinterpret_cast<uint64_t>(shader);if(identity)r->hash=identity(shader,5);shader->Release();}c->CSGetShaderResources(0,32,views);}
    else {ID3D11PixelShader* shader{};c->PSGetShader(&shader,nullptr,nullptr);if(shader){r->shader=reinterpret_cast<uint64_t>(shader);if(identity)r->hash=identity(shader,0);shader->Release();}c->PSGetShaderResources(0,32,views);}
    // Bind the missing vertex identity to the exact reduced-depth lighting
    // draws. No extra resource reads or work outside an armed F8 capture.
    if(!compute && (r->hash==0xb248b0205e047a2bull || r->hash==0x4ce5d78b6c06bdfbull ||
                   r->hash==0xc1344be4b3f10694ull || r->hash==0x0678cfb133ec65d1ull ||
                   r->hash==0x0e907ded7c1d61eeull || r->hash==0x4322e80586869a35ull || r->hash==0x5d313444810f95c0ull)) {
        if(auto v=Next(1)) {
            ID3D11VertexShader* shader{};c->VSGetShader(&shader,nullptr,nullptr);
            if(shader){v->shader=reinterpret_cast<uint64_t>(shader);if(identity)v->hash=identity(shader,1);shader->Release();}
        }
    }
    for(unsigned i=0;i<32;++i)if(views[i]){r->reads[i]=View(views[i]);views[i]->Release();}
    if(compute){ID3D11UnorderedAccessView* out[8]{};c->CSGetUnorderedAccessViews(0,8,out);for(unsigned i=0;i<8;++i)if(out[i]){r->writes[i]=View(out[i]);out[i]->Release();}}
    else {ID3D11RenderTargetView* out[8]{};ID3D11DepthStencilView* depth{};c->OMGetRenderTargets(8,out,&depth);for(unsigned i=0;i<8;++i)if(out[i]){r->writes[i]=View(out[i]);out[i]->Release();}if(depth){r->writes[8]=View(depth);depth->Release();}}
}
inline void Copy(ID3D11Resource* dst,ID3D11Resource* src,unsigned kind) {
    auto r=Next(kind);if(!r)return;r->reads[0]=Describe(src);r->writes[0]=Describe(dst);
}
inline bool Save(const wchar_t* directory,unsigned& written,unsigned& lost,unsigned& error) {
    unsigned ready=3;if(!state.compare_exchange_strong(ready,4))return false;
    wchar_t path[MAX_PATH];swprintf_s(path,L"%s\\DL2VR-gpu-%llu.bin",directory,header.request);
    FILE* file{};_wfopen_s(&file,path,L"wb");error=0;written=header.count;lost=header.dropped;
    if(!file)error=ERROR_OPEN_FAILED;
    else {if(fwrite(&header,sizeof(header),1,file)!=1 || fwrite(records,sizeof(Record),header.count,file)!=header.count)error=ERROR_WRITE_FAULT;if(fclose(file))error=ERROR_WRITE_FAULT;}
    state=0;return true;
}
}
