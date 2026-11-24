#pragma once

#include <windows.h>

// XYZ initially holds the outline origin. The result is centre XYZ and radius.
using OutlineSphere = float[4];
extern "C" void __fastcall Outline_BoundingSphere(BYTE* outline, void*, OutlineSphere& sphere);
