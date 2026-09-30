#pragma once
#include <windows.h>
#include <cstdint>
// Called only on the game's graphics-dispatch thread, before Present.
HRESULT PreserveGpuEye(void* swap,unsigned eye,uint64_t pair);
void ReleaseGpuEyes();
