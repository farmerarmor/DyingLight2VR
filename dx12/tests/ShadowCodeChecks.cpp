#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <fstream>
#include <iterator>
#include <cstdio>
#include "ShadowDx12Code.h"
int main(int argc,char**argv){if(argc!=7)return 1;ID3D11Device* device{};if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,nullptr)))return 2;for(int i=1;i<argc;++i){std::ifstream f(argv[i],std::ios::binary);std::vector<unsigned char> input((std::istreambuf_iterator<char>(f)),{}),out;unsigned bytes{};if(!ShadowDx12Code::Patch(input.data(),unsigned(input.size()),out,bytes))return 3;ID3DBlob* assembly{};if(FAILED(D3DDisassemble(out.data(),out.size(),0,nullptr,&assembly)))return 4;auto text=static_cast<char*>(assembly->GetBufferPointer());if(!strstr(text,"CB0[101]")||strstr(text,"cb12[")||strstr(text,"cb13[")||!strstr(text,"cb0[94].z")||!strstr(text,"cb0[99].y"))return 5;ID3D11PixelShader* shader{};auto hr=device->CreatePixelShader(out.data(),out.size(),nullptr,&shader);if(FAILED(hr)){printf("Shader validation failed %08lx\n",hr);return 6;}shader->Release();assembly->Release();printf("PASS %s materialBytes=%u right-eye shader CB0 extension validated\n",argv[i],bytes);}device->Release();}
