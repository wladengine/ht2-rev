// Compare caller-visible distances with the bit patterns measured on Steam 8.1.
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

using Byte = unsigned char;
extern "C" {
double __fastcall Points_Length(Byte*);
double __fastcall Points_Distance(Byte*, void*, double, int, double, int);
double __fastcall Thunk455650(Byte*);
double __fastcall Ai455620_Forward(Byte*, void*, double, int, double, int);
}

static void set_vertices(Byte* object, double* begin, double* end)
{
    // The road object's point vector starts at +56; the three pointers are at +32.
    memcpy(object + 88, &begin, sizeof begin);
    memcpy(object + 92, &end, sizeof end);
    memcpy(object + 96, &end, sizeof end);
}

static int failures;
static void check(const char* name, double value, uint64_t expected)
{
    uint64_t observed;
    memcpy(&observed, &value, sizeof observed);
    printf("%s %016llx %s\n", name, observed, observed == expected ? "PASS" : "FAIL");
    failures += observed != expected;
}

int main()
{
    alignas(8) Byte object[104] = {};
    double vertices[51];
    uint32_t state = 1;
    for (double& coordinate : vertices) {
        state = state * 1664525u + 1013904223u;
        coordinate = (int(state >> 13) - 250000) * .015625;
    }
    set_vertices(object, vertices, vertices + 51);
    _controlfp(_PC_64, _MCW_PC);
    check("Full distance", Points_Length(object + 56), 0x40f292a352a16487ULL);
    check("Partial distance", Points_Distance(object + 56, nullptr, .125, 0, .875, 15), 0x40f247602021793dULL);
    check("Full distance through road method", Thunk455650(object), 0x40f292a352a16487ULL);
    check("Partial distance through road method", Ai455620_Forward(object, nullptr, .125, 0, .875, 15),
          0x40f247602021793dULL);

    _controlfp(_PC_53, _MCW_PC);
    const double scale = 1e-200;
    double tiny[] = {0., 0., 0., scale, 2*scale, -scale, 3*scale, -scale, 4*scale, 4*scale, scale, 5*scale};
    set_vertices(object, tiny, tiny + 12);
    check("Tiny whole road", Thunk455650(object), 0x16a0efdca5528063ULL);
    check("Tiny partial road", Ai455620_Forward(object, nullptr, .125, 0, .875, 2), 0x169fffbbb433b2a6ULL);

    // Scaling the returned ST(0) before a binary64 store keeps this result finite.
    double large[] = {-1.5e308, 0., 0., 1.5e308, 0., 0., -1.5e308, 0., 0., -1.5e308, 0., 0.};
    set_vertices(object, large, large + 12);
    check("Extended return consumed by caller", Thunk455650(object) * .0625, 0x7fcab36d48e1acf0ULL);
    return failures != 0;
}
