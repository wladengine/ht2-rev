// One input captured immediately before the second orbit zoom update in 8.1.
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern "C" unsigned char KingData[], KingBss[];
extern "C" void __fastcall App_UpdateCamera(unsigned char*, void*);

int main()
{
    _controlfp(_PC_53, _MCW_PC);
    alignas(8) unsigned char camera[3000] = {};
    unsigned char window[264] = {};
    unsigned char* windowPointer = window;
    int height = 768;
    memcpy(window + 260, &height, sizeof height);
    memcpy(KingBss + (0x721750 - 0x68a820), &windowPointer, sizeof windowPointer);

    // Two mouse coordinates, distance, lateral motion, three offsets, two angles,
    // two unused fields, frame delta, two unused fields, then mode and button flags.
    const uint64_t state[] = {
        0x3fe8000000000000ULL, 0xbfe0000000000000ULL, 0x4018000000000000ULL, 0,
        0x3ff40d4137e3b778ULL, 0xc00e6198661a3ca0ULL, 0xbfb5c22412f1b455ULL,
        0x3fd4666658666666ULL, 0xbf95c28f655c28f8ULL, 0, 0, 0x3fa0000000000000ULL,
        0, 0, 0x0000000200000001ULL
    };
    memcpy(camera + 2872, state, sizeof state);
    const uint32_t previousButtons = 2, zoomScale = 0x40ed3857;
    const uint32_t horizontalMotion = 0x3e999999, verticalMotion = 0xbca3d70b;
    memcpy(KingBss + (0x721780 - 0x68a820), &previousButtons, 4);
    memcpy(KingData + (0x682a28 - 0x66c000), &zoomScale, 4);
    memcpy(KingBss + (0x721214 - 0x68a820), &horizontalMotion, 4);
    memcpy(KingBss + (0x7212b8 - 0x68a820), &verticalMotion, 4);

    App_UpdateCamera(camera, 0);
    const uint64_t expected[] = {
        0x3ff4854f1c7f7b89ULL, 0xc00e23ab3ae2b07fULL, 0xbfb64a44f542bc25ULL
    };
    uint64_t result[3];
    memcpy(result, camera + 2904, sizeof result);
    printf("Orbit offsets %016llx %016llx %016llx\n", result[0], result[1], result[2]);
    return memcmp(result, expected, sizeof result) != 0;
}
