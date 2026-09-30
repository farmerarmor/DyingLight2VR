#pragma once
#include <cmath>
#include <cstring>
inline bool OffsetCameraRight(const float* source,float distance,float* output) {
    if(!std::isfinite(distance) || std::fabs(distance)>1.f) return false;
    for(unsigned i=0;i<12;++i) if(!std::isfinite(source[i])) return false;
    // Row-major camera-to-world 3x4. Column zero is camera right in world space.
    for(unsigned a=0;a<3;++a) for(unsigned b=0;b<3;++b) {
        float dot=0; for(unsigned row=0;row<3;++row) dot+=source[row*4+a]*source[row*4+b];
        if(std::fabs(dot-(a==b?1.f:0.f))>0.01f) return false;
    }
    std::memcpy(output,source,12*sizeof(float));
    for(unsigned row=0;row<3;++row) output[row*4+3]+=distance*source[row*4];
    return true;
}
