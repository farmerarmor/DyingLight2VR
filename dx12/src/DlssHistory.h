#pragma once
#include <cmath>
#include <cstring>
#include <algorithm>
namespace DlssHistory {
struct Matrix {double a[4][4]{};};
inline Matrix Mul(const Matrix& a,const Matrix& b){Matrix c;for(int i=0;i<4;++i)for(int j=0;j<4;++j)for(int k=0;k<4;++k)c.a[i][j]+=a.a[i][k]*b.a[k][j];return c;}
inline bool Inverse(const Matrix& m,Matrix& out){double a[4][8]{};for(int i=0;i<4;++i){for(int j=0;j<4;++j)a[i][j]=m.a[i][j];a[i][i+4]=1;}
    for(int k=0;k<4;++k){int p=k;for(int i=k+1;i<4;++i)if(std::abs(a[i][k])>std::abs(a[p][k]))p=i;if(!std::isfinite(a[p][k]) || std::abs(a[p][k])<1e-12)return false;for(int j=0;j<8;++j)std::swap(a[k][j],a[p][j]);double s=a[k][k];for(auto& v:a[k])v/=s;for(int i=0;i<4;++i)if(i!=k){s=a[i][k];for(int j=0;j<8;++j)a[i][j]-=s*a[k][j];}}
    for(int i=0;i<4;++i)for(int j=0;j<4;++j)out.a[i][j]=a[i][j+4];return true;
}
inline float Float(const unsigned char* p,unsigned off){float f;memcpy(&f,p+off,4);return f;}
inline bool Camera(const unsigned char* packet,Matrix& clip) {
    Matrix p,v;v.a[3][3]=1;
    for(int i=0;i<4;++i)for(int j=0;j<4;++j){p.a[i][j]=Float(packet,0x30+4*(j*4+i));if(!std::isfinite(p.a[i][j]))return false;}
    constexpr unsigned basis[]={0x188,0x17c,0x194}; // native right/up/view-Z rows
    for(int i=0;i<3;++i)for(int j=0;j<3;++j){double x=Float(packet,basis[i]+4*j),pos=Float(packet,0x170+4*j);if(!std::isfinite(x)||!std::isfinite(pos))return false;v.a[i][j]=x;v.a[i][3]-=x*pos;}
    clip=Mul(p,v);return true;
}
inline void Write(unsigned char* packet,unsigned offset,const Matrix& m){for(int i=0;i<4;++i)for(int j=0;j<4;++j){float x=static_cast<float>(m.a[i][j]);memcpy(packet+offset+4*(j*4+i),&x,4);}}
struct History {
    struct Entry {Matrix clip;unsigned epoch{},viewport{},width{},height{};bool valid{};} entries[2];
    bool Apply(unsigned char* packet,unsigned eye,unsigned epoch,unsigned viewport,bool reset) {
        if(eye<1 || eye>2)return false;
        Matrix current,inverse;if(!Camera(packet,current) || !Inverse(current,inverse))return false;
        auto& e=entries[eye-1];unsigned width,height;memcpy(&width,packet+0x24,4);memcpy(&height,packet+0x28,4);
        bool seed=reset || !e.valid || e.epoch!=epoch || e.viewport!=viewport || e.width!=width || e.height!=height;
        Matrix toPrevious=Mul(seed?current:e.clip,inverse),toCurrent;
        if(!Inverse(toPrevious,toCurrent))return false;
        Write(packet,0xf0,toPrevious);Write(packet,0x130,toCurrent);
        if(seed){unsigned yes=1;memcpy(packet+0x18,&yes,4);}
        e={current,epoch,viewport,width,height,true};return true;
    }
};
}
