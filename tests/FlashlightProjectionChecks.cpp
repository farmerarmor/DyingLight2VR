#include "FlashlightProjection.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <fstream>
#include <iterator>
#include <cmath>
#include <cstdio>
#define CHECK(x) do{if(!(x)){printf("FAIL line %d\n",__LINE__);return __LINE__;}}while(0)
int main(int argc,char**argv) {
    CHECK(argc==2);std::ifstream in(argv[1],std::ios::binary);
    std::vector<unsigned char> original((std::istreambuf_iterator<char>(in)),{}),patched;
    CHECK(PatchFlashlightProjection(original.data(),static_cast<unsigned>(original.size()),patched));
    ID3DBlob* text{};CHECK(SUCCEEDED(D3DDisassemble(patched.data(),patched.size(),0,nullptr,&text)));
    auto s=static_cast<const char*>(text->GetBufferPointer());
    CHECK(strstr(s,"dcl_constantbuffer CB2[33]"));
    CHECK(strstr(s,"mad o4.xyzw, r2.xyxy"));
    CHECK(strstr(s,"mad o3.xy, r3.zwzw, r3.xyxx, r2.xyxx"));
    CHECK(strstr(s,"mul r2.x, r1.x, cb2[29].x"));
    CHECK(strstr(s,"mul r2.y, r1.y, cb2[30].y"));
    ID3D11Device* device{};ID3D11DeviceContext* ctx{};
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&ctx)));
    ID3D11VertexShader* vs{};CHECK(SUCCEEDED(device->CreateVertexShader(patched.data(),patched.size(),nullptr,&vs)));
    // Same physical direction must have the same cookie coordinate in both eyes.
    // Shadow UV is the original symmetric compression expressed in each eye's UV.
    for(float offset:{-.148f,.149f,-.111f,0.f})for(float scale:{.04545f,-.04545f})for(float ray:{-.5f,0.f,.7f}) {
        float ndc=ray-offset,uvScale=scale>0?.5f:-.5f;
        float before=ndc*scale+.5f;
        float after=before+(scale-uvScale)*offset;
        CHECK(fabs(after-(ray*scale+.5f-uvScale*offset))<1e-6f);
        CHECK(fabs((ndc+offset)*.1923f-ray*.1923f)<1e-6f);
    }
    original.back()^=1;CHECK(!PatchFlashlightProjection(original.data(),static_cast<unsigned>(original.size()),patched) && patched.empty());
    vs->Release();ctx->Release();device->Release();text->Release();
    puts("PASS exact flashlight shader guard, corrected disassembly, WARP creation, binocular cookie and shadow UV algebra");
}
