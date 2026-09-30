#include "MinHook.h"
#include "ShaderHunter.h"
#include <d3dcompiler.h>
#include <filesystem>
#define CHECK(x) do{if(!(x)){printf("FAIL line %d\n",__LINE__);return __LINE__;}}while(0)
int main(){
    using namespace ShaderHunter;
    Entry entry{};unsigned pos{},total{};
    Enable();CHECK(!Observe(10,0));CHECK(!Observe(20,5));CHECK(!Observe(10,0));
    CHECK(Step(1,entry,pos,total) && entry.hash==10 && total==2);
    CHECK(Observe(10,0) && !Observe(20,5));Observe(30,0);CHECK(count==2);
    CHECK(Step(1,entry,pos,total) && entry.hash==20);CHECK(!Observe(10,0) && Observe(20,5));
    CHECK(Step(1,entry,pos,total) && entry.hash==10);CHECK(Step(-1,entry,pos,total) && entry.hash==20);
    Reset();CHECK(!Observe(20,5) && !enabled && !chosen);
    ID3D11Device* device{};ID3D11DeviceContext* ctx{};D3D_FEATURE_LEVEL level;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&ctx)));
    const char* source="float4 vs(uint id:SV_VertexID):SV_Position {return float4(id==2?3:-1,id==1?3:-1,0,1);} float4 ps():SV_Target{return float4(1,0,0,1);} RWTexture2D<float4> output:register(u0); [numthreads(1,1,1)] void cs(uint3 id:SV_DispatchThreadID){output[id.xy]=float4(0,1,0,1);}";
    ID3DBlob *v{},*p{},*c{};
    CHECK(SUCCEEDED(D3DCompile(source,strlen(source),nullptr,nullptr,nullptr,"vs","vs_5_0",0,0,&v,nullptr)));
    CHECK(SUCCEEDED(D3DCompile(source,strlen(source),nullptr,nullptr,nullptr,"ps","ps_5_0",0,0,&p,nullptr)));
    CHECK(SUCCEEDED(D3DCompile(source,strlen(source),nullptr,nullptr,nullptr,"cs","cs_5_0",0,0,&c,nullptr)));
    ID3D11VertexShader* vs{};ID3D11PixelShader* ps{};ID3D11ComputeShader* cs{};
    CHECK(SUCCEEDED(device->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs)));
    CHECK(SUCCEEDED(device->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&ps)));
    CHECK(SUCCEEDED(device->CreateComputeShader(c->GetBufferPointer(),c->GetBufferSize(),nullptr,&cs)));
    EffectsCapture::Init();EffectsCapture::Register(ps,p->GetBufferPointer(),unsigned(p->GetBufferSize()),0,ps);EffectsCapture::Register(cs,c->GetBufferPointer(),unsigned(c->GetBufferSize()),5,cs);
    CHECK(MH_Initialize()==MH_OK);CHECK(Install(ctx));
    D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_UNORDERED_ACCESS;
    ID3D11Texture2D *texture{},*staging{};CHECK(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&texture)));
    td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;CHECK(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&staging)));
    ID3D11RenderTargetView* rtv{};ID3D11UnorderedAccessView* uav{};CHECK(SUCCEEDED(device->CreateRenderTargetView(texture,nullptr,&rtv)));CHECK(SUCCEEDED(device->CreateUnorderedAccessView(texture,nullptr,&uav)));
    ctx->OMSetRenderTargets(1,&rtv,nullptr);D3D11_VIEWPORT viewport{0,0,1,1,0,1};ctx->RSSetViewports(1,&viewport);ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);ctx->VSSetShader(vs,nullptr,0);ctx->PSSetShader(ps,nullptr,0);
    auto pixel=[&](){ctx->CopyResource(staging,texture);D3D11_MAPPED_SUBRESOURCE map{};if(FAILED(ctx->Map(staging,0,D3D11_MAP_READ,0,&map)))return 0xffffffffu;unsigned result=*static_cast<unsigned*>(map.pData);ctx->Unmap(staging,0);return result;};
    float black[4]{};Enable();ctx->ClearRenderTargetView(rtv,black);ctx->Draw(3,0);CHECK(pixel()==0xff0000ffu);CHECK(Step(1,entry,pos,total));
    ctx->ClearRenderTargetView(rtv,black);ctx->Draw(3,0);CHECK(pixel()==0 && skipped==1);
    wchar_t tmp[MAX_PATH];GetTempPathW(MAX_PATH,tmp);auto output=std::filesystem::path(tmp)/(L"DL2VR-hunter-test-"+std::to_wstring(GetCurrentProcessId()));std::filesystem::create_directory(output);wcscpy_s(directory,output.c_str());uint64_t hash{};unsigned stage{};CHECK(Mark(hash,stage));CHECK(std::filesystem::exists(output/L"DL2VR-marked-shaders"/L"marks.txt"));
    Reset();ctx->Draw(3,0);CHECK(pixel()==0xff0000ffu);
    RegisterTemporalShader(ps,0x7bff3ae2c1f5835aull,0);disableTemporalFilter=true;
    ctx->ClearRenderTargetView(rtv,black);ctx->Draw(3,0);CHECK(pixel()==0);
    Reset();ctx->Draw(3,0);CHECK(pixel()==0); // Home affects hunting, not the saved rendering option.
    disableTemporalFilter=false;ctx->Draw(3,0);CHECK(pixel()==0xff0000ffu);
    RegisterTemporalShader(ps,123,0);disableTemporalFilter=true;
    ctx->ClearRenderTargetView(rtv,black);ctx->Draw(3,0);CHECK(pixel()==0xff0000ffu);
    CHECK(!IsTemporalShader(nullptr));disableTemporalFilter=false;
    ctx->OMSetRenderTargets(0,nullptr,nullptr);ctx->CSSetShader(cs,nullptr,0);ctx->CSSetUnorderedAccessViews(0,1,&uav,nullptr);
    Enable();ctx->Dispatch(1,1,1);CHECK(pixel()==0xff00ff00u);CHECK(Step(1,entry,pos,total) && entry.stage==5);
    ctx->ClearUnorderedAccessViewFloat(uav,black);ctx->Dispatch(1,1,1);CHECK(pixel()==0);Reset();ctx->Dispatch(1,1,1);CHECK(pixel()==0xff00ff00u);
    ID3D11UnorderedAccessView* noUav{};ctx->CSSetUnorderedAccessViews(0,1,&noUav,nullptr);ctx->OMSetRenderTargets(1,&rtv,nullptr);
    CHECK(GpuTrace::Arm());CHECK(!GpuTrace::Arm());CHECK(!GpuTrace::Begin(2,99,true));
    for(unsigned pair=100;pair<=101;++pair)for(unsigned eye=1;eye<=2;++eye) {
        CHECK(GpuTrace::Begin(eye,pair,true));ctx->Draw(3,0);ctx->CopyResource(staging,texture);GpuTrace::End();
    }
    CHECK(GpuTrace::state==3 && GpuTrace::header.count==8 && GpuTrace::header.dropped==0);
    CHECK(GpuTrace::records[0].hash==Identity(ps,0));
    CHECK(GpuTrace::records[0].writes[0].id==reinterpret_cast<uint64_t>(texture));
    CHECK(GpuTrace::records[0].writes[0].width==1 && GpuTrace::records[0].writes[0].format==DXGI_FORMAT_R8G8B8A8_UNORM);
    CHECK(GpuTrace::records[1].kind==10 && GpuTrace::records[1].reads[0].id==reinterpret_cast<uint64_t>(texture));
    CHECK(GpuTrace::records[1].writes[0].id==reinterpret_cast<uint64_t>(staging));
    CHECK(GpuTrace::records[6].eye==2 && GpuTrace::records[6].pair==101);
    unsigned written{},lost{},error{};CHECK(GpuTrace::Save(directory,written,lost,error) && written==8 && !lost && !error);
    CHECK(GpuTrace::Arm());CHECK(GpuTrace::Begin(1,200,true));GpuTrace::count=GpuTrace::capacity;ctx->Draw(3,0);CHECK(GpuTrace::dropped==1);GpuTrace::End();
    CHECK(!GpuTrace::Begin(1,203,true) && GpuTrace::state==1);GpuTrace::state=0;
    auto originalIdentity=GpuTrace::identity;
    GpuTrace::identity=[](void*,unsigned stage)->uint64_t{return stage==0?0xb248b0205e047a2bull:0x1234ull;};
    CHECK(GpuTrace::Arm());CHECK(GpuTrace::Begin(1,300,true));ctx->Draw(3,0);GpuTrace::End();
    CHECK(GpuTrace::count==2 && GpuTrace::records[1].kind==1 && GpuTrace::records[1].hash==0x1234);
    CHECK(GpuTrace::records[1].shader==reinterpret_cast<uint64_t>(vs) && GpuTrace::records[1].pair==300);
    GpuTrace::state=0;GpuTrace::identity=originalIdentity;
    RegisterTemporalShader(ps,0xe275ecd626c81c62ull,0);ctx->PSSetShader(ps,nullptr,0);
    CHECK(TextureHistory::boundHash==0xe275ecd626c81c62ull);
    RegisterTemporalShader(ps,123,0);ctx->PSSetShader(ps,nullptr,0);CHECK(!TextureHistory::boundHash);
    CHECK(MH_Uninitialize()==MH_OK);ctx->ClearState();uav->Release();rtv->Release();texture->Release();staging->Release();vs->Release();ps->Release();cs->Release();v->Release();p->Release();c->Release();ctx->Release();device->Release();
    printf("PASS shader cycling, frozen list, Home restore, mark bytecode; actual WARP draw/dispatch suppression and restoration\n");
}
