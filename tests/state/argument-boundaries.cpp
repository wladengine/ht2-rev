// Native PC53 regressions for the ten corrected 8.1 argument-state observations.
// Build with tests/state/native.mk against either candidate's production objects.
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

using Byte = unsigned char;
extern "C" {
void __fastcall SchNumber_Div(Byte*, void*, const Byte*);
int __fastcall Curve_Advance(Byte*, void*, double*, double*, double*);
int __fastcall Curve_NearestTo(Byte*, void*, const double*, double*, double*, double, double);
int __fastcall Road_NearestOn(Byte*, void*, const double*, double*, double*, int*, double, int, double, int);
void* Ai_OrientationFrom(void*, const double*, const double*);
int __fastcall View_SphereClip(Byte*, void*, const float*);
void __fastcall Body_SpinAxis(Byte*);
void __fastcall Body_InWater(Byte*);
void __fastcall App_UpdateCamera(Byte*, void*);
void TerInit_615740();
void TerInit_615770();
int __fastcall Ground_SweepSphere(Byte*, void*, const float*, const float*, float, float*, float*);
}
template<class T>
static void put(Byte* obj, int offset, T value)
{
    memcpy(obj + offset, &value, sizeof value);
}
template<class T>
static T get(const Byte* obj, int offset)
{
    T value;
    memcpy(&value, obj + offset, sizeof value);
    return value;
}
static int errors;
static void check(const char* name, bool equal)
{
    printf("%s %s\n", name, equal ? "PASS" : "FAIL");
    errors += !equal;
}
int main()
{
    _controlfp(_PC_53, _MCW_PC);
    alignas(8) Byte number[32] = {}, divisor[32] = {};
    put(number, 16, 1e200);
    put(divisor, 16, 1e200);
    SchNumber_Div(number, nullptr, divisor);
    check("Scheme division", get<uint64_t>(number, 16) == 0 && get<uint64_t>(number, 24) == 0);

    // A unit-length segment has the cumulative distance polynomial f(t) = t.
    alignas(8) Byte curve[1592] = {};
    put(curve, 1392, 1.0);
    put(curve, 1584, 2);
    double t = .375, distance = .125, step = 0.;
    int result = Curve_Advance(curve, nullptr, &t, &distance, &step);
    check("Zero secant step", result == 0 && t == 0. && distance == 0. && _isnan(step));

    alignas(8) Byte road[48] = {};
    double vertices[] = {0., 0., 0., 1e-160, 1e-160, 1e-160};
    put(road, 24, 2.);
    put(road, 32, vertices + 0);
    put(road, 36, vertices + 6);
    put(road, 40, vertices + 6);
    double point[] = {.5e-160, .5e-160, .5e-160};
    int index = -1;
    double best = 10.;
    t = -1.;
    result = Road_NearestOn(road, nullptr, point, &best, &t, &index, .125, 0, .875, 0);
    uint64_t tBits;
    memcpy(&tBits, &t, sizeof t);
    check("Squared segment lengths", result == 1 && index == 0 && best == 0. && tBits == 0x3fdfff2b49b3bf51ULL);

    // A cubic x(t) = t^3 passes through the query at the lower endpoint.
    put(curve, 0, 1.);
    put(curve, 576, 4);
    put(curve, 608, 2.);
    double query[] = {.001953125, 0., 0.};
    const uint64_t nan64 = 0x7ff8000000000021ULL;
    memcpy(&best, &nan64, sizeof best);
    t = -1.;
    result = Curve_NearestTo(curve, nullptr, query, &best, &t, .125, .875);
    check("Nearest curve sample", result == 1 && best == 0. && t == .125);

    const double normalDirection[] = {1., 1., -1.};
    const double tinyDirection[] = {1e-200, 1e-200, -1e-200};
    double normalBasis[12], tinyBasis[12];
    Ai_OrientationFrom(normalBasis, point, normalDirection);
    Ai_OrientationFrom(tinyBasis, point, tinyDirection);
    check("Orientation basis", !memcmp(normalBasis, tinyBasis, sizeof normalBasis));

    float sphere[] = {3.f, 20.f, 1.f, 0.f};
    const uint32_t unordered = 0x7fc00021;
    memcpy(sphere + 3, &unordered, sizeof unordered);
    alignas(8) Byte view[9000] = {};
    put(view, 8464, 1.);
    put(view, 8472, 230.);
    result = View_SphereClip(view, nullptr, sphere);
    check("Sphere mask", result == 31 && get<int>(view, 8272) == 31);

    alignas(8) Byte camera[3100] = {};
    put(camera, 2984, 1);
    for (int offset = 2904; offset <= 2920; offset += 8)
        put(camera, offset, 1e-200);
    App_UpdateCamera(camera, nullptr);
    check("Orbit radius", get<uint64_t>(camera, 2912) == 0x967536793539fd32ULL);

    alignas(8) Byte body[22016] = {};
    put(body, 64, unordered);
    put(body, 208, 1.f);
    put(body, 2620, 1.f);
    memset(body + 3216, 0x33, 12);
    memset(body + 3288, 0x33, 12);
    Body_SpinAxis(body);
    const Byte zero[12] = {};
    check("Angular damping", !memcmp(body + 3216, zero, 12) && !memcmp(body + 3288, zero, 12));

    memset(body, 0, sizeof body);
    put(body, 3540, 4);
    int water[] = {1, 7};
    for (int i = 0; i < 4; ++i)
        put(body, 380 + i * 248, water + 0);
    put(body, 60, unordered);
    Body_InWater(body);
    check("Water contact", get<int>(body, 15436) == 1 && get<uint32_t>(body, 15432) == unordered);

    // Flat height samples with the original startup probe spacing.
    static uint32_t samples[257 * 257 * 4] = {};
    uint32_t* tiles[] = {samples, samples, samples};
    alignas(8) Byte grid[40208] = {}, handle[8] = {};
    put(grid, 40140, 768.f);
    put(grid, 40144, 768.f);
    put(grid, 40152, 1.f);
    put(grid, 40156, 1.f);
    put(grid, 40192, tiles + 0);
    put(handle, 4, grid + 0);
    Byte* terrain = handle;
    TerInit_615740();
    TerInit_615770();
    float start[] = {24.f, 12.f, 0.f}, end[] = {27.f, 12.5f, -2.f};
    memcpy(start + 2, &unordered, sizeof unordered);
    const float sentinel[] = {-9.f, -9.f, -9.f};
    float contact[3], normal[3];
    memcpy(contact, sentinel, sizeof contact);
    memcpy(normal, sentinel, sizeof normal);
    result = Ground_SweepSphere(reinterpret_cast<Byte*>(&terrain), nullptr, start, end, 1.f, contact, normal);
    check("Terrain contact", !result && !memcmp(contact, sentinel, sizeof contact) && !memcmp(normal, sentinel, sizeof normal));
    return errors != 0;
}
