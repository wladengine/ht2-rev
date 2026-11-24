#include "../../src/engine/outline_sphere.h"
#include <type_traits>

extern "C" void __fastcall Chain_MoveTo(BYTE*, void*);

namespace outline_chain {
using BoundsFunction = void (__fastcall*)(BYTE*, void*, float (&)[4]);
static_assert(std::is_same<decltype(&Outline_BoundingSphere), BoundsFunction>::value,
              "The output must require storage for XYZ and radius");

static bool Check() {
    // Each record has its own points and centre, so reversing the merge order
    // or reusing the preceding sphere's centre changes the final bounds.
    float x[][3] = {{-2, 2, 0}, {3, 5, 7}, {-7, -4, -1}, {5, 6, 10}};
    float z[][3] = {{0, 0, 0}, {2, 2, 2}, {1, -2, 1}, {4, 1, 4}};
    alignas(4) BYTE outlines[4][40] = {};
    for (int i = 0; i < 4; ++i) {
        F(WORD, outlines[i], 4) = i == 0 ? 2 : 3;
        F(float*, outlines[i], 16) = x[i];
        F(float*, outlines[i], 20) = z[i];
    }

    const auto original = reinterpret_cast<decltype(&Chain_MoveTo)>(0x5fa5f0);
    int cases = 0, mismatches = 0;
    for (int count : {-1, 0, 1, 2, 3, 4}) {
        for (float offset : {0.0f, 13.25f}) {
            alignas(4) BYTE initial[72], expected[72], actual[72];
            memset(initial, 0xa5, sizeof initial);
            F(int, initial, 48) = count;
            F(BYTE*, initial, 52) = count > 0 ? outlines[0] : nullptr;
            F(float, initial, 56) = offset;
            F(float, initial, 60) = -2.5f;
            F(float, initial, 64) = -offset;
            memcpy(expected, initial, sizeof expected);
            memcpy(actual, initial, sizeof actual);
            original(expected, nullptr);
            Chain_MoveTo(actual, nullptr);

            bool ok = memcmp(expected, actual, sizeof actual) == 0;
            if (count <= 0) {
                ok = ok && memcmp(initial, actual, sizeof actual) == 0;
            } else {
                ok = ok && memcmp(initial, actual, 20) == 0 &&
                     memcmp(initial + 36, actual + 36, sizeof actual - 36) == 0;
                if (count == 1) {
                    const float bounds[] = {offset, -2.5f, -offset, 2.0f};
                    for (int i = 0; i < 4; ++i)
                        ok = ok && F(float, actual, 20 + i * sizeof(float)) == bounds[i];
                }
            }
            if (!ok) {
                ++mismatches;
                printf("outline chain mismatch: outlines=%d offset=%g\n", count, offset);
            }
            ++cases;
        }
    }
    printf("outline chains cases=%d mismatches=%d\n", cases, mismatches);
    return mismatches == 0;
}
}
