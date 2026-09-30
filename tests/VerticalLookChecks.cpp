#include "VerticalLook.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>
int main(){
    for(unsigned action=0;action<506;++action)for(unsigned flags=0;flags<32;++flags)
    for(float input:{-1.f,-.1f,0.f,.1f,1.f}){
        auto value=VerticalLook::Filter(action,flags,input,true,true);
        if(VerticalLook::IsPitch(action)){
            if(flags&16)value=1-value;if(flags&2)value=-value;if(flags&4)value=fmaxf(value,0);
            if(value!=0)return 1;
        }else if(value!=input)return 2;
        if(VerticalLook::Filter(action,flags,input,false,true)!=input || VerticalLook::Filter(action,flags,input,true,false)!=input)return 3;
    }
    puts("PASS mouse/controller pitch neutralization, inversion/clamping, all unrelated actions and disabled/flat mode preservation");
}
