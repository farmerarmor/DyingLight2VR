#pragma once
#include <d3d11.h>
#include <cstdint>
void SetXrScreenEnabled(bool enabled);
void PumpXrScreenIdle();
void SubmitXrScreen(ID3D11Device* device,ID3D11Texture2D* left,ID3D11Texture2D* right,uint64_t pair);
struct TrackedEye { float quaternion[4],position[3],fov[4]; };
bool BeginTrackedPair(TrackedEye eyes[2]);
void CancelTrackedPair();
bool XrTrackingReady();
bool HasTrackedPair();
