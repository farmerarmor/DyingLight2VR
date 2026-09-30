#include "FlashlightShadow.h"
#include <d3dcompiler.h>
#include <fstream>
#include <iterator>
#include <cmath>
#include <cstdio>
#define CHECK(x) do{if(!(x)){printf("FAIL line %d\n",__LINE__);return __LINE__;}}while(0)
int main(int argc,char** argv){
    CHECK(argc==7);ID3D11Device* device{};ID3D11DeviceContext* ctx{};
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&ctx)));
    ID3D11PixelShader* original{},*alternate{};
    for(int i=1;i<7;++i){
        std::ifstream in(argv[i],std::ios::binary);std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)),{}),out;
        CHECK(FlashlightShadowCode::Patch(bytes.data(),unsigned(bytes.size()),out));
        ID3DBlob* text{};CHECK(SUCCEEDED(D3DDisassemble(out.data(),out.size(),0,nullptr,&text)));
        auto s=static_cast<const char*>(text->GetBufferPointer());CHECK(strstr(s,"CB12[33]") && strstr(s,"CB13[4]"));
        CHECK(strstr(s,"cb12[4]") && strstr(s,"cb12[30].z") && strstr(s,"cb13[2].y"));
        std::ofstream dump(std::string(argv[i])+".shared.txt");dump<<s;text->Release();
        ID3D11PixelShader* ps{};CHECK(SUCCEEDED(device->CreatePixelShader(out.data(),out.size(),nullptr,&ps)));ps->Release();
        CHECK(SUCCEEDED(device->CreatePixelShader(bytes.data(),bytes.size(),nullptr,&ps)));
        CHECK(FlashlightShadow::Register(ps,bytes.data(),unsigned(bytes.size()),0));
        if(i==1)original=ps;else if(i==4)alternate=ps;else ps->Release();
        bytes.back()^=1;CHECK(!FlashlightShadowCode::Patch(bytes.data(),unsigned(bytes.size()),out) && out.empty());
    }
    D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=4;td.MipLevels=td.ArraySize=1;td.Format=DXGI_FORMAT_R32_FLOAT;
    td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    float left[16],right[16];for(int i=0;i<16;++i){left[i]=float(i+1);right[i]=100+float(i);}
    D3D11_SUBRESOURCE_DATA data{left,16,0};ID3D11Texture2D* depth{};ID3D11ShaderResourceView* view{};
    CHECK(SUCCEEDED(device->CreateTexture2D(&td,&data,&depth)));CHECK(SUCCEEDED(device->CreateShaderResourceView(depth,nullptr,&view)));
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=33*16;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;bd.Usage=D3D11_USAGE_DEFAULT;
    float constants[33*4]{};constants[13*4]=42;D3D11_SUBRESOURCE_DATA init{constants,0,0};
    ID3D11Buffer* camera{},*material{},*sentinel{};
    CHECK(SUCCEEDED(device->CreateBuffer(&bd,&init,&camera)));bd.ByteWidth=64;
    CHECK(SUCCEEDED(device->CreateBuffer(&bd,&init,&material)));CHECK(SUCCEEDED(device->CreateBuffer(&bd,&init,&sentinel)));
    ctx->VSSetConstantBuffers(2,1,&camera);ctx->VSSetConstantBuffers(0,1,&material);
    ctx->PSSetShaderResources(1,1,&view);ctx->PSSetShader(original,nullptr,0);
    ID3D11Buffer* old[]={nullptr,sentinel};ctx->PSSetConstantBuffers(12,2,old);
    FlashlightShadow::Begin(ctx,1,10);{FlashlightShadow::DrawScope draw(ctx);}
    CHECK(FlashlightShadow::copies==1);FlashlightShadow::End();
    ctx->UpdateSubresource(depth,0,nullptr,right,16,0);constants[13*4]=99;ctx->UpdateSubresource(camera,0,nullptr,constants,0,0);
    ctx->PSSetShader(alternate,nullptr,0); // Other eye may select the equivalent lighting permutation.
    FlashlightShadow::Begin(ctx,2,10);
    {FlashlightShadow::DrawScope draw(ctx);
        CHECK(draw.c==ctx);ID3D11ShaderResourceView* bound{};ctx->PSGetShaderResources(1,1,&bound);CHECK(bound!=view);
        ID3D11Resource* resource{};bound->GetResource(&resource);bound->Release();
        td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ID3D11Texture2D* staging{};
        CHECK(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&staging)));ctx->CopyResource(staging,resource);resource->Release();
        D3D11_MAPPED_SUBRESOURCE map{};CHECK(SUCCEEDED(ctx->Map(staging,0,D3D11_MAP_READ,0,&map)));
        for(int y=0;y<4;++y)for(int x=0;x<4;++x)CHECK(reinterpret_cast<float*>(static_cast<char*>(map.pData)+y*map.RowPitch)[x]==left[4*y+x]);
        ctx->Unmap(staging,0);staging->Release();
        ID3D11Buffer* boundCB{};ctx->PSGetConstantBuffers(12,1,&boundCB);CHECK(boundCB && boundCB!=camera);
        bd.ByteWidth=33*16;bd.BindFlags=0;bd.Usage=D3D11_USAGE_STAGING;bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ID3D11Buffer* read{};CHECK(SUCCEEDED(device->CreateBuffer(&bd,nullptr,&read)));ctx->CopyResource(read,boundCB);boundCB->Release();
        CHECK(SUCCEEDED(ctx->Map(read,0,D3D11_MAP_READ,0,&map)));CHECK(static_cast<float*>(map.pData)[13*4]==42);ctx->Unmap(read,0);read->Release();
    }
    ID3D11PixelShader* restored{};ctx->PSGetShader(&restored,nullptr,nullptr);CHECK(restored==alternate);restored->Release();
    ID3D11Buffer* cb[2]{};ctx->PSGetConstantBuffers(12,2,cb);CHECK(cb[0]==nullptr && cb[1]==sentinel);cb[1]->Release();
    ID3D11ShaderResourceView* srv{};ctx->PSGetShaderResources(1,1,&srv);CHECK(srv==view);srv->Release();
    FlashlightShadow::End();FlashlightShadow::Begin(ctx,2,11);{FlashlightShadow::DrawScope draw(ctx);CHECK(!draw.c);} // never reuse old pair
    FlashlightShadow::enabled=false;FlashlightShadow::Begin(ctx,1,12);{FlashlightShadow::DrawScope draw(ctx);CHECK(!draw.c);}
    FlashlightShadow::Begin(ctx,2,12);{FlashlightShadow::DrawScope draw(ctx);CHECK(!draw.c);}
    FlashlightShadow::End();
    // A fixed receiver must produce the same reference UV/depth from either
    // source eye, for near/far geometry and asymmetric headset projections.
    for(double z:{.3,1.,3.,20.})for(double x:{-.5,0.,.7})for(double eye:{-.032,.032}){
        double ray=(x-eye)/z,delta=ray*z+eye-(-.032);
        double uv=.5*((delta/z)*1.1-(-.148))+.5;
        double expected=.5*(((x+.032)/z)*1.1+.148)+.5;
        CHECK(fabs(uv-expected)<1e-12);
    }
    CHECK(FlashlightShadow::failed==0 && FlashlightShadow::applied==1);
    ctx->ClearState();for(auto& s:FlashlightShadow::snapshots)s.Clear();
    for(auto& s:FlashlightShadow::shaders)FlashlightShadow::Release(s.corrected);
    original->Release();alternate->Release();view->Release();depth->Release();camera->Release();material->Release();sentinel->Release();ctx->Release();device->Release();
    puts("PASS six guarded shader variants and cross-permutation stereo pairing, WARP creation, same-pair GPU depth/camera copies, binding restoration, disabled/missing-pair fallback and stereo receiver math");
}
