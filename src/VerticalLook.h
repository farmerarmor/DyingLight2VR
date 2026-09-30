#pragma once
#include <cstdint>
namespace VerticalLook {
// Exact EGameActions values from this build's enum registration. Mouse and
// gamepad use separate actions; neither raw device axes nor menus are blocked.
inline bool IsPitch(uint32_t action){return action==0x5e || action==0x5f || action==0x89 || action==0x8a;}
inline float Filter(uint32_t action,uint8_t flags,float value,bool enabled,bool vr){
    if(!enabled || !vr || !IsPitch(action))return value;
    // The native action converter applies (1-value) when flag 0x10 is set,
    // then sign inversion/clamping. Supply a neutral value before conversion.
    return (flags&0x10)?1.f:0.f;
}
}
