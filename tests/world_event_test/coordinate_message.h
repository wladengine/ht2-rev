#include <errno.h>
#ifndef STOCK_WORLD
double x87_pow(double base, double exponent);
#endif

extern "C" int __fastcall Volume_Contains(DWORD*, void*, const float*);
extern "C" double Vehicle_ClampPacketComponent(float, float);
extern "C" int Vec3_OutOfBox(const float*, float);
extern "C" int Sch_Message1(BYTE*, int, int);
extern "C" DWORD Port_WriteString(const char*, DWORD, DWORD, DWORD);
extern "C" void Time_Delta(double*, double*);
extern "C" int __fastcall Position_Valid(const void*, void*);
extern "C" void __fastcall Ramp_SetPosition(BYTE*, void*, float);
extern "C" float Angle_F2Pi(float, const char*);
extern "C" int OldCrt_FormatCoordinates(char*, size_t, const char*, const char*, float, const float*);
extern "C" float* Vec3_DivScalar(float*, const float*, float);
extern "C" int __fastcall Control_Move(BYTE*, void*, int, float*, float);
extern "C" void __fastcall History_Record(BYTE*, void*, DWORD, const char*);
struct GeometryTriple { float x, y, z; };
extern "C" int __stdcall Clip_Inside(const float*, int);
extern "C" float* Matrix_Multiply(float*, const float*, const float*);
extern "C" void Matrix_ToAngles(GeometryTriple, GeometryTriple, GeometryTriple, GeometryTriple,
                                double*, double*, double*, double*, double*, double*);
extern "C" int Weather_IsWaveLevelLow();
extern "C" void Sky_MixColor(float, float, float, float*, float*, float*, BYTE*, int);
extern "C" float Sound_SetVolume(float);
extern "C" int __fastcall BodyLevel_Step(BYTE*, void*, float*, float);
extern "C" double __fastcall Terrain_HeightAt(BYTE**, void*, float, float);
extern "C" void __fastcall SceneTexture_Advance(BYTE*, void*);
extern "C" void Gradient3(const float*, const float*, const float*, float, float, float, float*);
extern "C" void Color_Lerp(const float*, const float*, float, float*);
extern "C" void Color_FromHsl(float, float, float, float*, float*, float*);
extern "C" void __fastcall Ramp_Down(BYTE*, void*, float);
extern "C" int World_ResolveSegmentDestination(int, int, int, char*);
extern "C" int Shadow_FindProjectionObject(int, int, int);
extern "C" BYTE* __fastcall PlayerRec_Copy(BYTE*, void*, const BYTE*);

extern "C" double __fastcall Car_TorqueAt(BYTE*, void*, float);

extern "C" float* Triangle_RandomPoint(float*, const float*, const float*, const float*);
extern "C" int Point_InTriangle(float, float, const float*);

extern "C" void __fastcall Body_KeepFrame(BYTE*, void*, const BYTE*);

extern "C" double __fastcall Filter2_Update(double*, void*, double);
extern "C" int __cdecl World_MathErr(int*);

namespace coordinate_message {

static void checkBodyFrameThrottle() {
    alignas(8) BYTE body[16000] = {}, pose[48] = {};
    const int model = 3;
    field<int>(body, 2252) = model;
    DWORD& limit = field<DWORD>(global(0x698078 + model * 2416), 0);
    const DWORD previousLimit = limit;
    limit = 0x3f800000;
    field<DWORD>(body, 7300) = 0xff800071;
    __asm fnclex
    ROUTINE(Body_KeepFrame, 0x4f09f0)(body, nullptr, pose);
    unsigned short status;
    __asm fnstsw status
    require(field<DWORD>(body, 7300) == 0xff800071 && field<DWORD>(body, 15424) == 0xffc00071 &&
            (status & 0x4501) == 0x4501, "frame throttle retains source and quiets stored signaling NaN");
    fprintf(report, "saved throttle=%08x state=%04x\n", field<DWORD>(body, 15424), status);
    field<DWORD>(body, 7300) = 0;
    __asm fnclex
    ROUTINE(Body_KeepFrame, 0x4f09f0)(body, nullptr, pose);
    __asm fnstsw status
    limit = previousLimit;
    require(field<DWORD>(body, 15424) == 0 && (status & 0x4500) == 0x0100,
            "frame throttle compares the stored request against the model limit");
    fprintf(report, "throttle below limit state=%04x\n", status);
}


static void checkTriangleArithmetic() {
    float vertices[3][3] = {{0.03125f, -1503.125f, 27776.0078125f},
                            {-51.75f, 0.0f, 1.0000001f}, {930.125f, 127.375f, -8.625f}};
    const unsigned int seed = 2531011;
#ifdef STOCK_WORLD
    reinterpret_cast<void (__cdecl*)(unsigned int)>(0x61edd1)(seed);
#else
    srand(seed);
#endif
    unsigned short status;
    require(ROUTINE(Triangle_RandomPoint, 0x4b84f0)(vertices[0], vertices[0], vertices[1], vertices[2]) == vertices[0],
            "triangle sample with aliased destination");
    __asm fnstsw status
    require((status & 0x4700) == 0x0100, "reflected triangle sample and final x87 store");
#ifdef STOCK_WORLD
    int next = reinterpret_cast<int (__cdecl*)()>(0x61edde)();
#else
    int next = rand();
#endif
    require(next == 2437, "triangle consumes two random numbers");

    const float triangle[] = {-2, -2, 0, 4, -2, 0, 0, 4, 0};
    const DWORD nanBits = 0x7fc00154;
    float coordinate;
    memcpy(&coordinate, &nanBits, sizeof coordinate);
    __asm fnclex
    int inside = ROUTINE(Point_InTriangle, 0x4be0e0)(coordinate, coordinate, triangle);
    __asm fnstsw status
    require(inside == 1 && (status & 0x4501) == 0x4501, "unordered triangle edges and invalid-operation flag");
    fprintf(report, "triangle rng=%d unordered=%d status=%04x\n", next, inside, status);
}

static unsigned powerExceptionCount;
static LONG resumePower(EXCEPTION_POINTERS* exception) {
    const auto& fault = *exception->ExceptionRecord;
    if (fault.ExceptionCode != EXCEPTION_FLT_INVALID_OPERATION || fault.NumberParameters != 1)
        return EXCEPTION_CONTINUE_SEARCH;
    auto* record = reinterpret_cast<BYTE*>(fault.ExceptionInformation[0]);
    require(record && (field<DWORD>(record, 0) & 0x1ffff) == 0x3a4 &&
            field<DWORD>(record, 4) == 16 && field<DWORD>(record, 8) == 16 &&
            field<DWORD>(record, 12) == 0, "power operation, cause, enables and prior flags");
    require((field<DWORD>(record, 32) & 31) == 3 && (field<DWORD>(record, 56) & 31) == 3 &&
            (field<DWORD>(record, 80) & 31) == 3 &&
            field<unsigned long long>(record, 16) == 0x7ff8000000000043ull &&
            field<unsigned long long>(record, 40) == 0x3ff0000000000000ull &&
            field<unsigned long long>(record, 64) == 0x7ff8000000000043ull,
            "legacy floating-point exception operands");
    ++powerExceptionCount;
    field<unsigned long long>(record, 64) = 0x400d000000000000ull;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static void checkNumericalExceptions() {
    const unsigned short invalidEnabled = 0x027e;
    const unsigned long long base = 0x7ff8000000000043ull, exponent = 0x3ff0000000000000ull;
    unsigned long long value = 0;
    unsigned short savedControl, restored, status;
#ifdef STOCK_WORLD
    auto power = reinterpret_cast<void(__cdecl*)()>(0x61db50);
    int* error = reinterpret_cast<int*(__cdecl*)()>(0x620df3)();
#else
    auto power = x87_pow;
    int* error = _errno();
#endif
    powerExceptionCount = 0;
    *error = 977;
    __asm fnstcw savedControl
    __try {
        __asm fninit
        __asm fldcw invalidEnabled
#ifdef STOCK_WORLD
        __asm fld qword ptr base
        __asm fld qword ptr exponent
        __asm call power
#else
        __asm push dword ptr exponent[4]
        __asm push dword ptr exponent
        __asm push dword ptr base[4]
        __asm push dword ptr base
        __asm call power
        __asm add esp, 16
#endif
        __asm fnstsw status
        __asm fnstcw restored
        __asm fstp qword ptr value
    } __except(resumePower(GetExceptionInformation())) {}
    __asm fninit
    __asm fldcw savedControl
    require(powerExceptionCount == 1 && value == 0x400d000000000000ull && *error == EDOM &&
            restored == invalidEnabled && (status & 0x3f) == 0, "resumed power result, masks and errno");
    fprintf(report, "resumed power=%016llx control=%04x status=%04x errno=%d\n", value, restored, status, *error);

    alignas(8) BYTE mathError[32] = {};
    field<int>(mathError, 0) = 4;
    field<const char*>(mathError, 4) = "pow";
    field<unsigned long long>(mathError, 8) = 1;
    __asm fnclex
    int handled = ROUTINE(World_MathErr, 0x4e4671)(reinterpret_cast<int*>(mathError));
    __asm fnstsw status
    require(!handled && !(status & 0x3f), "unhandled math errors leave their arguments unread");

    unsigned long long history[] = {1, 0x0010000000000000ull, 0, 0x3fefffffffffffffull, 0x7ff8000000000031ull};
    auto update = ROUTINE(Filter2_Update, 0x4881b0);
    const unsigned long long sample = 0xfff0000000000071ull;
    const unsigned short precision53 = 0x027f;
    __asm fninit
    __asm fldcw precision53
    __asm push dword ptr sample[4]
    __asm push dword ptr sample
    __asm lea ecx, history
    __asm xor edx, edx
    __asm call update
    __asm fnstsw status
    __asm fstp qword ptr value
    __asm fninit
    __asm fldcw savedControl
    require((status & 0x3f) == 1 && value == 0xfff8000000000071ull &&
            history[3] == value && history[4] == 0x3fefffffffffffffull,
            "second-order filter history, result and exception flags");
    fprintf(report, "filtered value=%016llx status=%04x\n", value, status);
}

static void checkPowerErrors() {
    struct Case {
        unsigned long long base, exponent, result;
        unsigned exceptions;
        int error;
        unsigned short rounding;
    };
    const Case cases[] = {
        {0x8000000000000000ull, 0xbff0000000000000ull, 0xfff0000000000000ull, 0x24, ERANGE, 0},
        {0x8000000000000001ull, 0x4008000000000000ull, 0x8000000000000000ull, 0x32, 977, 0},
        {0x3ff0000000000000ull, 0x7ff0000000000000ull, 0xfff8000000000000ull, 0x01, EDOM, 0},
        {0x7ff0000000000000ull, 0, 0x3ff0000000000000ull, 0, 977, 0},
        {0x7ff8000000000043ull, 0x3ff0000000000000ull, 0x7ff8000000000043ull, 0x01, EDOM, 0},
        {0, 0x8000000000000001ull, 0, 0x02, 977, 0x0400},
        {0x3d12f739fe173744ull, 0xc0601516dbdcc97dull, 0x7fefffffffffffffull, 0x28, ERANGE, 0x0400},
    };
#ifdef STOCK_WORLD
    auto power = reinterpret_cast<void(__cdecl*)()>(0x61db50);
    int* error = reinterpret_cast<int*(__cdecl*)()>(0x620df3)();
#else
    auto power = x87_pow;
    int* error = _errno();
#endif
    for (const Case& item : cases) {
        const Case* input = &item;
        unsigned short previousControl, actualControl;
        __asm fnstcw previousControl
        const unsigned short requestedControl = 0x027f | item.rounding;
        unsigned short status;
        unsigned long long result;
        *error = 977;
        __asm fninit
        __asm fldcw requestedControl
        __asm mov eax, input
#ifdef STOCK_WORLD
        __asm fld qword ptr [eax]
        __asm fld qword ptr [eax+8]
        __asm call power
#else
        __asm push dword ptr [eax+12]
        __asm push dword ptr [eax+8]
        __asm push dword ptr [eax+4]
        __asm push dword ptr [eax]
        __asm call power
        __asm add esp, 16
#endif
        __asm fnstsw status
        __asm fstp qword ptr result
        __asm fnstcw actualControl
        __asm fldcw previousControl
        require(actualControl == requestedControl && result == item.result && (status & 0x3f) == item.exceptions && *error == item.error,
                "power result, exception flags and thread-local errno");
        fprintf(report, "power base=%016llx result=%016llx flags=%02x errno=%d\n",
                item.base, result, status & 0x3f, *error);
    }
}
static void checkEngineTorque() {
    BYTE car[16400] = {};
    field<float>(car, 16320) = 0.5f;
    field<float>(car, 16332) = 1.5f;
    field<float>(car, 16356) = 0.3125f;
    field<float>(car, 16060) = 65.0f;
    field<float>(car, 16072) = 73.125f;
    field<float>(car, 16096) = 0.75f;
    field<float>(car, 7304) = 875.0f;
    const BYTE expected[][10] = {
        {0x00, 0xc0, 0x2f, 0xb6, 0xb8, 0x14, 0x48, 0xb5, 0x02, 0x40},
        {0x90, 0xc1, 0x2f, 0xb6, 0xb8, 0x14, 0x48, 0xb5, 0x02, 0x40}
    };
    auto torque = ROUTINE(Car_TorqueAt, 0x4ff460);
    unsigned short saved;
    __asm fnstcw saved
    for (int i = 0; i < 2; ++i) {
        unsigned short control = i ? 0x037f : 0x027f;
        unsigned short status;
        BYTE result[10];
        __asm {
            fninit
            fldcw control
            push 0
            lea ecx, car
            xor edx, edx
            call torque
            fnstsw status
            fstp tbyte ptr result
        }
        require(memcmp(result, expected[i], sizeof result) == 0, "engine torque extended result");
        require(status == (i ? 0x3b20 : 0x3920), "engine torque return status");
        fprintf(report, "engine torque control=%04x status=%04x\n", control, status);
    }
    checkPowerErrors();
    __asm fldcw saved
}

static unsigned written;
static int __fastcall put(void*, void*, char c) {
    written = written * 33u + static_cast<unsigned char>(c);
    return 0x12345678;
}
static DWORD __fastcall dispose(void*, void*, int flag) { return 0x52000000u + flag; }

static void checkMessage() {
    void* methods[8] = {};
    methods[3] = reinterpret_cast<void*>(&dispose);
    methods[7] = reinterpret_cast<void*>(&put);
    DWORD port[] = {reinterpret_cast<DWORD>(methods), 0};
    BYTE interpreter[128] = {};
    field<DWORD>(interpreter, 96) = reinterpret_cast<DWORD>(port);
    field<DWORD>(interpreter, 104) = 15;
    for (unsigned count : {2u, 9u, 32u}) {
        port[1] = count;
        written = 0;
        int value = ROUTINE(Sch_Message1, 0x4a5a60)(interpreter, 32, reinterpret_cast<int>("audit"));
        require(value == static_cast<int>(count) && port[1] == count, "message retains port and returns count");
        require(field<int>(interpreter, 120) == 1 && written, "interpreter status and diagnostic");
        fprintf(report, "message refs=%u result=%d output=%08x\n", count, value, written);
    }
    port[1] = 1;
    DWORD result = ROUTINE(Port_WriteString, 0x4b1450)("end", reinterpret_cast<DWORD>(port), 0, 15);
    require(!port[1] && result == 0x52000001, "last port reference propagates disposal result");
}

static void checkCoordinates() {
    const DWORD words[] = {0, 0x80000000, 0x3f800000, 0xbf800000, 0x3f800001,
        0xbf800001, 0x7f800000, 0xff800000, 0x7fc00001, 0x7f800001, 1, 0x80000001};
    unsigned hash = 2166136261u, rejected = 0;
    for (DWORD limit : words) {
        float bound;
        memcpy(&bound, &limit, sizeof bound);
        for (DWORD word : words) for (int axis = 0; axis < 3; ++axis) {
            float point[3] = {};
            memcpy(point + axis, &word, sizeof word);
            __asm fnclex
            int result = ROUTINE(Vec3_OutOfBox, 0x4e28b0)(point, bound);
            unsigned short status;
            __asm fnstsw status
            if (word == 0x7fc00001 || limit == 0x7fc00001)
                require(result == 1, "unordered coordinate or bound must be rejected");
            rejected += result;
            hash = (hash ^ status ^ (static_cast<unsigned>(result) << 16)) * 16777619u;
        }
    }
    fprintf(report, "coordinate cases=432 rejected=%u status-hash=%08x\n", rejected, hash);
}

static void checkTimeDelta() {
    field<int>(global(0x6843e0), 0) = 0;
    field<double>(global(0x6843d8), 0) = 0.0;
    for (int alias = 0; alias <= 1; ++alias) {
        for (double start : {-2.0, 0.0, 2.0}) {
            double last = start, accumulator = 3.0;
            ROUTINE(Time_Delta, 0x5d2fc0)(&last, alias ? &last : &accumulator);
            unsigned short status;
            __asm fnstsw status
            unsigned relation = status & 0x4500;
            require(relation == (start < 0.0 ? 0 : start == 0.0 ? 0x4000 : 0x100),
                    "elapsed time compared with zero in the original order");
            fprintf(report, "timer alias=%d initial=%.0f elapsed=%.0f accumulator=%.0f relation=%04x\n",
                    alias, start, last, accumulator, relation);
        }
    }
}

static void checkScalarComparisons() {
    DWORD nanBits = 0x7fc00031;
    float nan;
    memcpy(&nan, &nanBits, sizeof nan);
    double limited = ROUTINE(Vehicle_ClampPacketComponent, 0x542250)(nan, 1.0f);
    unsigned short clampStatus;
    __asm fnstsw clampStatus
    require(limited == -1.0 && (clampStatus & 0x4500) == 0x4500,
            "unordered vehicle component returns the lower limit");
    DWORD edges[2] = {};
    field<DWORD*>(global(0x68cf38), 0) = edges;
    field<DWORD*>(global(0x68cf3c), 0) = edges + 2;
    BYTE position[32] = {};
    field<double>(position, 8) = -0.0003;
    int valid = ROUTINE(Position_Valid, 0x40a500)(position, nullptr);
    unsigned short status;
    __asm fnstsw status
    require(!valid && (status & 0x4500) == 0x100, "road position below lower bound");
    BYTE ramp[52] = {};
    ROUTINE(Ramp_SetPosition, 0x4eb6a0)(ramp, nullptr, 0.5f);
    __asm fnstsw status
    require(field<float>(ramp, 36) == 0.5f && (status & 0x4500) == 0x100, "ramp below upper bound");
    float angle = ROUTINE(Angle_F2Pi, 0x4e41d9)(0.0f, "audit");
    __asm fnstsw status
    require(angle == 0.0f && (status & 0x4500) == 0x100, "angle below positive wrap boundary");
    fprintf(report, "position=%d ramp=%.1f angle=%.1f\n", valid, field<float>(ramp, 36), angle);
}

static void checkDiagnosticNumbers() {
    const DWORD words[] = {0, 0x80000000, 0x3f800000, 1, 0x80000001, 0xb5000000,
                           0xb5880000, 0x7f800000, 0xff800000, 0x7fc00021, 0xffc00000, 0x7f800071};
    for (DWORD word : words) {
        float value;
        memcpy(&value, &word, sizeof value);
        float position[] = {value, -1.5f, 32.0f};
        char text[192];
        const char* format = "%s limit=%f x=%f y=%f z=%f";
#ifdef STOCK_WORLD
        using OriginalSprintf = int(__cdecl*)(char*, const char*, ...);
        int n = reinterpret_cast<OriginalSprintf>(0x61cca9)(text, format, "point", static_cast<double>(value),
                                                             static_cast<double>(value), -1.5, 32.0);
#else
        int n = OldCrt_FormatCoordinates(text, sizeof text, format, "point", value, position);
#endif
        require(n > 0 && static_cast<size_t>(n) == strlen(text), "diagnostic output length");
        fprintf(report, "%08x %s\n", word, text);
    }
}

static void checkVehicleState() {
    DWORD vector[] = {0x7f800071, 1, 0x3f800001, 0xdeadbeef};
    DWORD divisorBits = 0x7fc00051;
    float divisor;
    memcpy(&divisor, &divisorBits, sizeof divisor);
    auto* input = reinterpret_cast<float*>(vector);
    float* quotient = ROUTINE(Vec3_DivScalar, 0x4e10d0)(input + 1, input, divisor);
    require(quotient == input + 1, "overlapping quotient destination");
    require(vector[0] == 0x7f800071 && vector[1] == 0x7fc00071 && vector[2] == divisorBits &&
            vector[3] == divisorBits, "division preserves the selected NaN payload");
    fprintf(report, "overlap %08x %08x %08x %08x\n", vector[0], vector[1], vector[2], vector[3]);

    BYTE vehicle[16400] = {};
    float amount = -1.0f;
    int moved = ROUTINE(Control_Move, 0x501b90)(vehicle, nullptr, -1, &amount, -2.0f);
    unsigned short status;
    __asm fnstsw status
    require(!moved && amount == -1.0f && (status & 0x4500) == 0x100, "control rejected after amount comparison");

    BYTE world[3000] = {}, history[32] = {};
    field<BYTE*>(global(0x6974c4), 0) = world;
    field<double>(world, 2968) = 4.0;
    DWORD timestamps[] = {0x7fc00005, 0x3f800000};
    DWORD values[] = {13, 15};
    field<float>(history, 0) = 0.5f;
    field<DWORD*>(history, 8) = timestamps;
    field<DWORD*>(history, 12) = values;
    field<int>(history, 28) = 2;
    ROUTINE(History_Record, 0x4eacce)(history, nullptr, 27, "audit");
    __asm fnstsw status
    require(values[0] == 27 && values[1] == 15 && timestamps[0] == 0x7fc00005 &&
            (status & 0x4500) == 0x4500, "unordered history timestamp retains its slot");
    fprintf(report, "control %d history %u %u relation %04x\n", moved, values[0], values[1], status & 0x4500);
}

static void checkVolumeClamp() {
    int& enabled = field<int>(global(0x723b28), 0);
    BYTE*& primary = field<BYTE*>(global(0x724194), 0);
    DWORD& mixer = field<DWORD>(global(0x6843fc), 0);
    const int savedEnabled = enabled;
    BYTE* const savedPrimary = primary;
    const DWORD savedMixer = mixer;
    enabled = 1;
    primary = nullptr;
    mixer = 0;
    unsigned short status;
    ROUTINE(Sound_SetVolume, 0x5fcd40)(0.5f);
    __asm fnstsw status
    require((status & 0x4500) == 0 && mixer == 0x3efd70a4,
            "mixer gain and x87 comparison at half volume");
    mixer = savedMixer;
    primary = savedPrimary;
    enabled = savedEnabled;
}

static void checkSkyMixEndpoints() {
    BYTE saved[32];
    BYTE* mix = global(0x6fb3d0);
    memcpy(saved, mix, sizeof saved);
    for (int i = 0; i < 4; ++i)
        field<int>(mix, 4 * i) = i;
    float table[96], color[3];
    for (int i = 0; i < 96; ++i)
        table[i] = float((i * 31) % 257 - 64) / 128.0f;
    field<unsigned long long>(mix, 16) = 0;
    field<unsigned long long>(mix, 24) = 0x7ff8000000000031ull;
    ROUTINE(Sky_MixColor, 0x5a8e90)(0, 0.25f, 0.375f, color, color + 1, color + 2,
                                     reinterpret_cast<BYTE*>(table), 0);
    const DWORD lowRow[] = {0x3f49c000, 0x3f83e000, 0x3c900000};
    require(!memcmp(color, lowRow, sizeof color), "unordered weather weight selects the low palette row");

    field<unsigned long long>(mix, 16) = 1;
    field<unsigned long long>(mix, 24) = 0;
    unsigned short status;
    __asm fnclex
    ROUTINE(Sky_MixColor, 0x5a8e90)(0, 0.25f, 0.375f, color, color + 1, color + 2,
                                     reinterpret_cast<BYTE*>(table), 0);
    __asm fnstsw status
    require(!memcmp(color, lowRow, sizeof color) && (status & 0x12) == 2,
            "unblended weather weight retains its binary64 precision");
    memcpy(mix, saved, sizeof saved);
}

static void checkWeatherAndTerrain() {
    unsigned previous = _controlfp(0, 0);
    _controlfp(_PC_64, _MCW_PC);
    BYTE weather[552] = {};
    field<DWORD>(weather, 544) = 0x3ec00000;
    field<unsigned long long>(weather, 120) = 0x3fe4000000000000ull;
    field<unsigned long long>(weather, 104) = 0x3fd1331f5ff5acd2ull;
    field<BYTE*>(global(0x6f33ac), 0) = weather;
    int low = ROUTINE(Weather_IsWaveLevelLow, 0x484010)();
    require(low == 0, "wave at the low-level weather threshold");

    _controlfp(_PC_53, _MCW_PC);
    BYTE body[40] = {};
    field<float>(body, 12) = 0.25f;
    DWORD bit = 1;
    float amount, rate;
    memcpy(&amount, &bit, sizeof bit);
    memcpy(&rate, &bit, sizeof bit);
    int moved = ROUTINE(BodyLevel_Step, 0x589490)(body, nullptr, &amount, rate);
    unsigned short status;
    __asm fnstsw status
    DWORD remaining;
    memcpy(&remaining, &amount, sizeof remaining);
    require(!moved && remaining == 1 && !(status & 0x200), "body level rounding flag");

    static BYTE cells[16 * (257 * 4 + 5)] = {};
    BYTE* tiles[] = {cells, cells, cells};
    BYTE grid[40208] = {}, view[8] = {};
    BYTE* handle = view;
    field<BYTE*>(view, 4) = grid;
    field<BYTE**>(grid, 40192) = tiles;
    field<float>(grid, 40140) = field<float>(grid, 40144) = 64.0f;
    field<float>(grid, 40152) = 1.125f;
    field<float>(grid, 40156) = 0.875f;
    __asm fnclex
    double height = ROUTINE(Terrain_HeightAt, 0x616850)(&handle, nullptr, 0.5625f, 1.75f);
    __asm fnstsw status
    require(height == 0.0 && (status & 0x20), "fractional cell rounding sets precision exception");
    fprintf(report, "low=%d body=%d height=%.0f precision=%u\n", low, moved, height, status & 0x20);
    checkSkyMixEndpoints();
    checkVolumeClamp();
    _controlfp(previous, _MCW_PC);
}

static int __fastcall returnPortalName(BYTE*, void*, int, BYTE* query) {
    strcpy(reinterpret_cast<char*>(query + 124), "road.service-yard");
    return 4;
}

static void checkPortalDestination() {
    void* methods[14] = {};
    methods[13] = reinterpret_cast<void*>(&returnPortalName);
    void** object = methods;
    const float start[3] = {}, end[3] = {1.0f, 0.0f, 0.0f};
    const int receiver = reinterpret_cast<int>(&object);
    const int from = reinterpret_cast<int>(start), to = reinterpret_cast<int>(end);
    char name[256] = {};
    int destination = ROUTINE(World_ResolveSegmentDestination, 0x4e5bf5)(receiver, from, to, name);
    require(destination == receiver && !strcmp(name, "road.service-yard"), "named portal destination");
    require(ROUTINE(Shadow_FindProjectionObject, 0x4ca3f0)(receiver, from, to) == receiver,
            "named portal keeps the projection in its source object");
}

static void checkGeometry() {
    checkPortalDestination();
    const unsigned savedPrecision = _controlfp(0, 0);
    float sphere[] = {0, 0, 0, 1};
    DWORD volume[] = {1, 0, reinterpret_cast<DWORD>(sphere)};
    const float edge[] = {1.0f, 1.0f / 1073741824.0f, 0.0f};
    _controlfp(_PC_53, _MCW_PC);
    const int within53 = ROUTINE(Volume_Contains, 0x5f88d0)(volume, nullptr, edge);
    _controlfp(_PC_64, _MCW_PC);
    const int within64 = ROUTINE(Volume_Contains, 0x5f88d0)(volume, nullptr, edge);
    _controlfp(savedPrecision, _MCW_PC);
    require(within53 && !within64, "sphere comparison retains extended vector length");
    unsigned short status;
    DWORD lightBits = 0x3effffff, rgb[3] = {};
    float light, channels[3];
    memcpy(&light, &lightBits, sizeof light);
    ROUTINE(Color_FromHsl, 0x4be7f0)(-360.0f, light, -1.0f, channels, channels + 1, channels + 2);
    __asm fnstsw status
    memcpy(rgb, channels, sizeof rgb);
    require(rgb[0] == 0 && rgb[1] == 0x3f7fffff && rgb[2] == 0x40400000 &&
            (status & 0x4700) == 0x0300, "rounded hue channels and final x87 condition codes");
    BYTE ramp[44] = {};
    field<float>(ramp, 24) = 0.5f;
    field<float>(ramp, 36) = 0.5f;
    ROUTINE(Ramp_Down, 0x4eb6e0)(ramp, nullptr, 0.5f);
    __asm fnstsw status
    require(field<float>(ramp, 36) == 0.25f && !(status & 0x4500), "positive ramp lower-bound comparison");
    const float low[] = {0.25f, 0.5f, 0.75f}, high[] = {1.0f, 0.0f, 0.5f};
    DWORD nanBits = 0x7fc00231;
    float height, color[3];
    memcpy(&height, &nanBits, sizeof height);
    ROUTINE(Gradient3, 0x5a8c20)(low, high, high, 0.0f, height, 1.0f, color);
    require(!memcmp(color, low, sizeof color), "unordered gradient height selects the low endpoint");
    ROUTINE(Color_Lerp, 0x5a8d30)(low, high, 0.5f, color);
    __asm fnstsw status
    require((status & 0x4500) == 0 && color[0] == 0.625f, "interior color weight and condition codes");

    BYTE animation[132] = {}, quad[40] = {}, scene[2976] = {};
    DWORD frames[] = {7, 11};
    field<BYTE*>(animation, 72) = quad;
    field<BYTE*>(animation, 88) = scene;
    field<float>(animation, 96) = 1.0f;
    field<float>(animation, 100) = 2.0f;
    field<float>(animation, 104) = 1.0f;
    field<int>(animation, 112) = 2;
    field<DWORD*>(animation, 120) = frames;
    field<double>(scene, 2960) = 0.125;
    ROUTINE(SceneTexture_Advance, 0x59aa10)(animation, nullptr);
    __asm fnstsw status
    require((status & 0x4500) == 0x100 && field<DWORD>(quad, 36) == 11 &&
            field<float>(animation, 104) == 1.125f, "active texture frame, elapsed time and condition codes");

    const float vertex[] = {0.0f, 0.5f, 0.0f};
    const int inside = ROUTINE(Clip_Inside, 0x5aeb10)(vertex, 1);
    __asm fnstsw status
    require(!inside && (status & 0x4500) == 0x100, "near plane comparison");

    // The last component stored by the point transform rounds upward by one ULP.
    float left[12] = {};
    float right[12] = {};
    left[9] = 3.0f / 33554432.0f;
    right[1] = 1.0f;
    right[10] = 1.0f;
    float* product = ROUTINE(Matrix_Multiply, 0x5aba40)(left, left, right);
    __asm fnstsw status
    DWORD y;
    memcpy(&y, left + 10, sizeof y);
    require(product == left && y == 0x3f800001 && (status & 0x200), "translation rounding and C1");

    double pose[6] = {};
    ROUTINE(Matrix_ToAngles, 0x5de950)({1, 0, 0}, {0, 0, 1}, {-1, 0, 0}, {0, -1500.375f, 1.59375f},
                                      pose, pose + 1, pose + 2, pose + 3, pose + 4, pose + 5);
    __asm fnstsw status
    unsigned long long roll;
    memcpy(&roll, pose + 5, sizeof roll);
    require(roll == 0xbe74442d20000006ull, "roll at the 8.1 rounding boundary");
    fprintf(report, "near %d translation %08x roll %016llx status %04x\n", inside, y, roll, status);
}

static void checkPlayerCopy() {
    BYTE source[992] = {}, destination[992] = {};
    field<unsigned long long>(source, 896) = 1;
    field<unsigned long long>(source, 928) = 0x7ff0000000000013ull;
    __asm fnclex
    BYTE* result = ROUTINE(PlayerRec_Copy, 0x47a230)(destination, nullptr, source);
    unsigned short status;
    __asm fnstsw status
    require(result == destination && field<unsigned long long>(destination, 896) == 1 &&
            field<unsigned long long>(destination, 928) == 0x7ff8000000000013ull &&
            field<unsigned long long>(source, 928) == 0x7ff0000000000013ull &&
            (status & 3) == 3, "player copy retains subnormal, quiets signaling NaN and raises DE and IE");
    fprintf(report, "copy %016llx %016llx exceptions=%u\n", field<unsigned long long>(destination, 896),
            field<unsigned long long>(destination, 928), status & 3);
}
}
