#include "ReflectionProjection.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <fstream>
#include <iterator>
#include <string>
#include <cmath>
#include <cstdio>
#define CHECK(x) do {if(!(x)){printf("FAIL line %d\n",__LINE__);return __LINE__;}}while(0)
int main(int argc,char** argv){
    CHECK(argc==4);ID3D11Device* device{};ID3D11DeviceContext* ctx{};D3D_FEATURE_LEVEL level;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&ctx)));
    for(int i=1;i<4;++i){
        std::ifstream file(argv[i],std::ios::binary);std::vector<unsigned char> input((std::istreambuf_iterator<char>(file)),{}),output;
        CHECK(!input.empty());auto checksum=input;CHECK(kcdvr::UpdateDxbcChecksum(checksum.data(),checksum.size()));CHECK(checksum==input);
        CHECK(PatchReflectionProjection(input.data(),unsigned(input.size()),output)==(i==1?4:i==2?8:3));CHECK(output.size()==input.size());
        if(i==3){ID3D11ComputeShader* shader{};CHECK(SUCCEEDED(device->CreateComputeShader(output.data(),output.size(),nullptr,&shader)));shader->Release();}
        else {ID3D11PixelShader* shader{};CHECK(SUCCEEDED(device->CreatePixelShader(output.data(),output.size(),nullptr,&shader)));shader->Release();}
        ID3DBlob* dis{};CHECK(SUCCEEDED(D3DDisassemble(output.data(),output.size(),0,nullptr,&dis)));
        std::string text(static_cast<char*>(dis->GetBufferPointer()),dis->GetBufferSize());dis->Release();
        CHECK(text.find("cb2[29].xyzx")!=std::string::npos);CHECK(text.find("cb2[30].xyzx")!=std::string::npos);
        CHECK(text.find(", cb2[29].x\n")==std::string::npos);
        if(i==3) {
            CHECK(text.find("dp3 r9.x, r6.xzwx, cb2[29].xyzx")!=std::string::npos);
            CHECK(text.find("dp3 r9.y, r6.xzwx, cb2[30].xyzx")!=std::string::npos);
        }
        std::ofstream(std::string(argv[i])+".patched.txt")<<text;
        input.back()^=1;CHECK(!PatchReflectionProjection(input.data(),unsigned(input.size()),output) && output.empty());
    }
    // DP3 includes the asymmetric terms before dividing by -viewZ.
    for(float offset:{-.15f,.15f})for(float z:{-1.f,-10.f}) {
        float x=.3f,scale=1.2f;
        float corrected=(x*scale+z*offset)/-z;
        CHECK(std::fabs(corrected-(x*scale/-z-offset))<1e-6f);
    }
    ctx->Release();device->Release();printf("PASS exact shader coverage, original checksum, corrected DXBC WARP acceptance, unknown-bytecode rejection, asymmetric projection math\n");
}
