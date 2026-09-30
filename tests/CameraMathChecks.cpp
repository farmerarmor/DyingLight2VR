#include "CameraMath.h"
#include <limits>
#include <cstdio>
int main() {
    const float source[12]={.214309141f,.135939553f,-.967260122f,194.047684f,
        0,.990268111f,.139173120f,88.8465729f,
        .976765871f,-.0298260748f,.212223515f,1382.97571f};
    float left[12],right[12];
    if(!OffsetCameraRight(source,-.032f,left) || !OffsetCameraRight(source,.032f,right)) return 1;
    for(unsigned axis=0;axis<3;++axis) {
        float projected=0;
        for(unsigned row=0;row<3;++row) projected+=(right[row*4+3]-left[row*4+3])*source[row*4+axis];
        if(std::fabs(projected-(axis==0?.064f:0.f))>.0002f) return 2;
    }
    for(unsigned i=0;i<12;++i) if(i%4!=3 && (left[i]!=source[i] || right[i]!=source[i])) return 3;
    float invalid[12]{};
    if(OffsetCameraRight(invalid,.25f,right)) return 4;
    memcpy(invalid,source,sizeof(invalid)); invalid[0]=std::numeric_limits<float>::quiet_NaN();
    if(OffsetCameraRight(invalid,.25f,right)) return 5;
    if(OffsetCameraRight(source,2.f,right)) return 6;
    if(!OffsetCameraRight(source,0.f,right) || memcmp(source,right,sizeof(right))) return 7;
    puts("PASS: captured rotated camera, right-axis separation, zero-offset preservation, invalid-input rejection");
    return 0;
}
