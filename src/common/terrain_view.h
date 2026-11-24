#pragma once
#include "king.h"

// Original 0x615230, thiscall. Each input is three contiguous floats.
extern "C" void __fastcall Terrain_PlaceCameraView(BYTE* terrain, void*, const float* position,
    const float* direction, const float* up);
