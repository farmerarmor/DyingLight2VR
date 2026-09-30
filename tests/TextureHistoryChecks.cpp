#include <windows.h>
#include <d3d11.h>
#include <cstdio>
#include <vector>
#include "TextureHistory.h"
#define CHECK(x) do{if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(){
 ID3D11Device* d{};ID3D11DeviceContext* c{};
 CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&c)));
 TextureHistory::context=c;
 D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=8;desc.MipLevels=2;desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
 ID3D11Texture2D *t{},*read{};ID3D11ShaderResourceView* v{};
 CHECK(SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&t)));CHECK(SUCCEEDED(d->CreateShaderResourceView(t,nullptr,&v)));
 desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.BindFlags=0;CHECK(SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&read)));
 auto fill=[&](unsigned a,unsigned b){unsigned data[64];for(unsigned mip=0;mip<2;++mip){for(auto& x:data)x=mip?b:a;c->UpdateSubresource(t,mip,nullptr,data,(8>>mip)*4,0);}};
 auto pixel=[&](unsigned mip){c->CopyResource(read,t);D3D11_MAPPED_SUBRESOURCE map{};if(FAILED(c->Map(read,mip,D3D11_MAP_READ,0,&map)))return 0u;unsigned out=*static_cast<unsigned*>(map.pData);c->Unmap(read,mip);return out;};
 c->PSSetShaderResources(0,1,&v);
 auto observe=[&](){TextureHistory::boundHash=0xe275ecd626c81c62ull;TextureHistory::BeforeDraw(c);};
 TextureHistory::Begin(1,1);observe();fill(11,12);TextureHistory::End();
 TextureHistory::Begin(2,1);observe();fill(21,22);TextureHistory::End();
 TextureHistory::Begin(1,1);CHECK(pixel(0)==11 && pixel(1)==12);observe();fill(31,32);TextureHistory::End();
 TextureHistory::Begin(2,1);CHECK(pixel(0)==21 && pixel(1)==22);observe();TextureHistory::End();
 TextureHistory::Begin(1,1);CHECK(pixel(0)==31 && pixel(1)==32);observe();TextureHistory::End();
 // Disabled scope leaves native image alone; epoch change discards old banks.
 fill(41,42);TextureHistory::Begin(0,2);TextureHistory::End();CHECK(pixel(0)==41);
 TextureHistory::Begin(1,2);CHECK(!TextureHistory::entries[0].source);observe();CHECK(pixel(0)==41);TextureHistory::End();
 // A replacement texture gets new banks, never stale data from old resolution.
 ID3D11Texture2D* replacement{};ID3D11ShaderResourceView* rv{};desc.Usage=D3D11_USAGE_DEFAULT;desc.CPUAccessFlags=0;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.Width=desc.Height=16;
 CHECK(SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&replacement)));CHECK(SUCCEEDED(d->CreateShaderResourceView(replacement,nullptr,&rv)));
 c->PSSetShaderResources(0,1,&rv);TextureHistory::Begin(2,2);observe();CHECK(TextureHistory::entries[0].source==replacement && !TextureHistory::entries[0].valid[0]);TextureHistory::End();
 CHECK(TextureHistory::discovered==3 && !TextureHistory::failed);
 // All four exact semantic slots are selected, unrelated shaders do nothing.
 TextureHistory::Begin(1,2);c->PSSetShaderResources(1,1,&rv);c->PSSetShaderResources(4,1,&rv);c->PSSetShaderResources(18,1,&rv);
 TextureHistory::boundHash=0x6b258d38d343a365ull;TextureHistory::BeforeDraw(c);CHECK(TextureHistory::entries[1].source==replacement);
 TextureHistory::boundHash=0x637fddd208eb09bdull;TextureHistory::BeforeDraw(c);CHECK(TextureHistory::entries[2].source==replacement && TextureHistory::entries[3].source==replacement);TextureHistory::End();
 // Reproduce native water: output #1 -> scratch copy -> sampled next frame.
 ID3D11Texture2D *water{},*scratch{};ID3D11RenderTargetView* waterRT{};
 t->GetDesc(&desc);desc.BindFlags=D3D11_BIND_RENDER_TARGET;
 CHECK(SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&water)));CHECK(SUCCEEDED(d->CreateRenderTargetView(water,nullptr,&waterRT)));
 desc.BindFlags=0;CHECK(SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&scratch)));
 ID3D11RenderTargetView* outputs[2]={nullptr,waterRT};c->OMSetRenderTargets(2,outputs,nullptr);
 auto waterPixel=[&](){c->CopyResource(read,scratch);D3D11_MAPPED_SUBRESOURCE m{};if(FAILED(c->Map(read,0,D3D11_MAP_READ,0,&m)))return 0u;auto x=*static_cast<unsigned*>(m.pData);c->Unmap(read,0);return x;};
 auto waterDraw=[&](unsigned value){TextureHistory::boundHash=0x6b258d38d343a365ull;TextureHistory::BeforeDraw(c);unsigned data[64];for(auto& x:data)x=value;c->UpdateSubresource(water,0,nullptr,data,32,0);};
 TextureHistory::Begin(1,2);waterDraw(101);TextureHistory::End();
 TextureHistory::Begin(2,2);waterDraw(202);TextureHistory::End();
 TextureHistory::Begin(1,2);c->CopyResource(scratch,water);CHECK(waterPixel()==101);waterDraw(303);TextureHistory::End();
 TextureHistory::Begin(2,2);c->CopyResource(scratch,water);CHECK(waterPixel()==202);waterDraw(404);TextureHistory::End();
 CHECK(TextureHistory::entries[4].source==water);
 c->OMSetRenderTargets(0,nullptr,nullptr);waterRT->Release();water->Release();scratch->Release();
 TextureHistory::Register(t,0xe275ecd626c81c62ull,0);CHECK(TextureHistory::Identify(t)==0xe275ecd626c81c62ull);TextureHistory::Register(t,123,0);CHECK(!TextureHistory::Identify(t));
 TextureHistory::Clear();c->ClearState();rv->Release();replacement->Release();v->Release();t->Release();read->Release();c->Release();d->Release();
 puts("PASS actual GPU eye history values, all mips, reset, disabled scope, resource replacement, exact consumer slots");
}
