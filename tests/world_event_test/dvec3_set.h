struct DVec3 { double x, y, z; };
struct Poly;
struct VecPoly;
extern "C" DVec3* __fastcall DVec3_SetXYZ(DVec3*, void*, double, double, double);
extern "C" void Poly_DistanceSq(Poly*, VecPoly*, const DVec3*);

namespace dvec3_set {
static void check() {
    // Raw words let the caller pass a signaling NaN without first converting it.
    using Bits = unsigned __int64;
    const Bits cases[][3] = {
        {0x3ff0000000000000ULL, 0xc004000000000000ULL, 0x400921fb54442d18ULL},
        {0x8000000000000000ULL, 0, 1},
        {0x7fefffffffffffffULL, 0x800fffffffffffffULL, 0x0010000000000000ULL},
        {0x7ff0000000000000ULL, 0xfff0000000000000ULL, 0xfff8000012345678ULL},
        {0x7ff0000000000001ULL, 0xfff0000011223344ULL, 0x7ff8abcdef123456ULL},
    };
    static_assert(sizeof(DVec3) == 3 * sizeof(Bits), "three packed binary64 fields");
    auto setXYZ = ROUTINE(DVec3_SetXYZ, 0x405f40);
    for (const auto& bits : cases) {
        struct { Bits before; DVec3 value; Bits after; } output;
        memset(&output, 0xa5, sizeof(output));
        DVec3* dest = &output.value;
        DVec3* returned;
        DWORD stackBefore, stackAfter;
        const Bits* input = bits;
        _clearfp();
        // Stock thiscall passes this in ECX and three binary64 arguments on the stack.
        __asm {
            mov stackBefore, esp
            mov eax, input
            push dword ptr [eax+20]
            push dword ptr [eax+16]
            push dword ptr [eax+12]
            push dword ptr [eax+8]
            push dword ptr [eax+4]
            push dword ptr [eax]
            mov ecx, dest
            call setXYZ
            mov returned, eax
            mov stackAfter, esp
        }
        require(stackAfter == stackBefore && returned == dest, "setter thiscall stack and return");
        require(!memcmp(dest, bits, sizeof(*dest)) && output.before == 0xa5a5a5a5a5a5a5a5ULL &&
                output.after == output.before, "setter preserves all coordinate bits and guards");
        require((_statusfp() & _MCW_EM) == 0, "setter raises no floating point exceptions");
        fprintf(report, "set xyz=%016llx %016llx %016llx flags=0\n", bits[0], bits[1], bits[2]);
    }

    // |(2,-1,3)t + (1,2,3) - (-2,6,1)|^2 = 14t^2 + 32t + 29.
    alignas(8) BYTE curve[584] = {}, result[200] = {};
    const DVec3 coefficients[] = {{2,-1,3}, {1,2,3}};
    const DVec3 point = {-2,6,1};
    memcpy(curve, coefficients, sizeof(coefficients));
    field<unsigned>(curve, 576) = 2;
    ROUTINE(Poly_DistanceSq, 0x48e470)(reinterpret_cast<Poly*>(result), reinterpret_cast<VecPoly*>(curve), &point);
    const double expected[] = {14,32,29};
    require(field<unsigned>(result, 192) == 3 && !memcmp(result, expected, sizeof(expected)), "squared distance polynomial");
    fprintf(report, "distance polynomial=14,32,29\n");
}
}
