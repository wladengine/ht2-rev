// x87.cpp - the x87 helpers of x87.h, in C++. This file alone is compiled
// with /fp:fast, under which the compiler turns sqrt, sin, cos, atan2, tan,
// log, log10, exp and fabs into the FPU instructions themselves (fsqrt,
// fsin, fcos, fpatan, fptan, fyl2x, f2xm1/fscale) and keeps a whole
// expression on the FPU stack: what the game's compiler (VC6) generated
// and what the inline __asm of these helpers did. Each function's code was
// compared with that __asm instruction by instruction. The helpers are
// separate calls (x87.h): their unrounded result comes back in ST(0).

#include <windows.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <string.h>
#include "x87.h"

#pragma intrinsic(sqrt, sin, cos, atan2, tan, log, log10, exp, fabs)

double x87_sqrt(double x) { return sqrt(x); }
double x87_sin(double x) { return sin(x); }
double x87_cos(double x) { return cos(x); }

#pragma warning(push)
#pragma warning(disable: 4035) // MSVC x86 floating return register


// A binary32 vector has an extended-precision norm. Both its return value and
// C1 after the final stack cleanup are observed by the volume tests at 0x5f88d0.
double x87_vector_length(const float* components)
{
    __asm {
        mov eax, components
        fld dword ptr [eax]
        fmul st(0), st(0)
        fld dword ptr [eax + 4]
        fmul st(0), st(0)
        faddp st(1), st(0)
        fld dword ptr [eax + 8]
        fmul st(0), st(0)
        faddp st(1), st(0)
        fsqrt
        fld st(0)
        fstp st(0)
    }
}

// This routine returns the unclipped interior value directly in ST(0).
// The weather switch compares that value before any binary64 store.
double x87_weather_wave(float balance, double modulation, double angle, double phase)
{
    static const double modulationScale = 0.3, phaseOrigin = 1.5707963, half = 0.5;
    static const double zero = 0.0, one = 1.0;
    unsigned char bias[10];
    __asm {
        fld1
        fld balance
        fadd st(0), st(0)
        fsubp st(1), st(0)
        fstp tbyte ptr bias

        fld modulation
        fsin
        fmul modulationScale
        fadd one
        fld1
        fld tbyte ptr bias
        fabs
        fsubp st(1), st(0)
        fmulp st(1), st(0)

        fld angle
        fsub phaseOrigin
        fadd phase
        fsin
        fmulp st(1), st(0)
        fld tbyte ptr bias
        faddp st(1), st(0)
        fadd one
        fmul half

        fcom one
        fnstsw ax
        test ah, 41h
        jnz lowerBound
        fstp st(0)
        fld1
        jmp done
    lowerBound:
        fcom zero
        fnstsw ax
        test ah, 1
        jz done
        fstp st(0)
        fldz
    done:
    }
}
#pragma warning(pop)

// 0x439ce0 stores the cosine as binary64. FSIN stays on the x87 stack
// through normalization and the final multiplication of the Y component.
void x87_smoothed_heading(double angle, double rise, double* x, double* y)
{
    volatile double cosine = cos(angle);
    double sine = sin(angle);
    double scale = 1.0 / sqrt((cosine * cosine + sine * sine) + rise * rise);
    *x = scale * cosine;
    *y = sine * scale;
}
double x87_tan(double x) { return tan(x); }
double x87_atan2(double y, double x) { return atan2(y, x); }
double x87_ln(double x) { return log(x); }
double x87_log10(double x) { return log10(x); }
double x87_fabs(double x) { return fabs(x); }

// e^x as VC6 inlines exp()
double x87_exp(double x) { return exp(x); }

// e^-(y*y), one FPU chain
double x87_exp_neg_sq(double y) { return exp(-(y * y)); }

// k^(a*b) as e^(ln k * a * b), one FPU chain
double x87_pow_mul(double k, double a, double b) { return exp(log(k) * a * b); }

// sqrt(x*x + y*y) computed on the FPU in one helper
double x87_hypot(double x, double y) { return sqrt(x * x + y * y); }

// Keep the sum of squares in ST(0) until FSQRT, including its extended exponent range.
double x87_length3(double x, double y, double z) { return sqrt((x * x + y * y) + z * z); }

// 0x48d670 keeps the running length and the X/Y differences in x87 registers.
double x87_polyline_length(const double* points, unsigned segments)
{
    double length = 0.;
    for (unsigned i = 0; i < segments; ++i, points += 3) {
        double dx = points[3] - points[0], dy = points[4] - points[1];
        volatile double dz = points[5] - points[2];
        length += sqrt((dy * dy + dz * dz) + dx * dx);
    }
    return length;
}

// 0x48d710 stores Y before squaring in the single, interior and last segments.
static __forceinline double span_segment_length(const double* p)
{
    double dx = p[3] - p[0], dz = p[5] - p[2];
    volatile double dy = p[4] - p[1];
    return sqrt((dy * dy + dz * dz) + dx * dx);
}

double x87_polyline_span(const double* points, double t1, int i1, double t2, int i2)
{
    const double* first = points + 3 * i1;
    if (i1 == i2)
        return span_segment_length(first) * (t2 - t1);

    // The first partial segment goes through the original three-double vector constructor.
    volatile double dx = first[3] - first[0], dy = first[4] - first[1], dz = first[5] - first[2];
    double length = sqrt((dy * dy + dz * dz) + dx * dx) * (1. - t1);
    for (int j = i1 + 1; j < i2; ++j)
        length += span_segment_length(points + 3 * j);
    return length + span_segment_length(points + 3 * i2) * t2;
}

double x87_player_node_distance(double ax, double ay, double az, double bx, double by, double bz)
{
    // 0x418890 spills the Y difference to binary64 before squaring, then sums Y, Z, X.
    double dx = ax - bx;
    volatile double dy = ay - by;
    double dz = az - bz;
    return sqrt((dy * dy + dz * dz) + dx * dx);
}

// Left child in the distance heap. Only the Y difference is stored before squaring.
double x87_xyz_node_distance(double ax, double ay, double az, double bx, double by, double bz)
{
    double dx = ax - bx;
    volatile double dy = ay - by;
    double dz = az - bz;
    return sqrt((dx * dx + dy * dy) + dz * dz);
}

// The two sift-up instances compare the square roots in x87 registers.
// The parent's three differences and the candidate's Y and Z have been stored as doubles.
bool x87_heap_value_farther(double parentX, double parentY, double parentZ, double originX,
                           double valueX, double valueDY, double valueDZ, bool valueXYZ)
{
    double dx = originX - valueX;
    double parent = sqrt((parentY * parentY + parentZ * parentZ) + parentX * parentX);
    double candidate = valueXYZ ? sqrt((dx * dx + valueDY * valueDY) + valueDZ * valueDZ)
                                : sqrt((valueDY * valueDY + valueDZ * valueDZ) + dx * dx);
    return candidate > parent;
}

// unit / (sqrt(x*x + y*y) + unit) from floats, one FPU chain
double x87_inv_dist(float x, float y, double unit) { return unit / (sqrt(x * x + y * y) + unit); }

// v / |v| per component, the length kept unrounded on the FPU stack
void x87_normalize(const float* v, float* out)
{
    double len = sqrt((v[0] * v[0] + v[1] * v[1]) + v[2] * v[2]);
    out[0] = (float)(v[0] / len);
    out[1] = (float)(v[1] / len);
    out[2] = (float)(v[2] / len);
}

// The C runtime's register-based intrinsics (__CIpow, __CIacos, ...)
using PowerWord = unsigned long long;
static constexpr PowerWord powerSign = 0x8000000000000000ull;
static constexpr PowerWord powerInfinity = 0x7ff0000000000000ull;
static constexpr PowerWord powerFraction = 0x000fffffffffffffull;
static constexpr PowerWord powerHiddenBit = 0x0010000000000000ull;
static constexpr PowerWord powerOne = 0x3ff0000000000000ull;
static constexpr PowerWord powerIndefinite = 0xfff8000000000000ull;

static PowerWord PowerBits(double value)
{
    PowerWord bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static double PowerNumber(PowerWord bits)
{
    double value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

// Set sticky status bits without disturbing the caller's stack, condition codes or masks.
static __declspec(noinline) void RaisePowerFlags(unsigned short flags)
{
    unsigned short environment[14];
    __asm {
        fnstenv environment
        mov ax, flags
        or word ptr environment[4], ax
        fldenv environment
    }
}

enum class ExponentParity { Fractional, Odd, Even };

// 0x61dd45 rounds twice. The second comparison, including C0 and C3, is observable.
static ExponentParity PowerExponentParity(double exponent)
{
    const double half = 0.5;
    unsigned short status;
    __asm {
        fld exponent
        fld st(0)
        frndint
        fcomp st(1)
        fnstsw ax
        test ah, 40h
        jz fractional
        fmul half
        fld st(0)
        frndint
        fcompp
        fnstsw status
        jmp finished
    fractional:
        fstp st(0)
        mov status, 0ffffh
    finished:
    }
    if (status == 0xffff)
        return ExponentParity::Fractional;
    return status & 0x4000 ? ExponentParity::Even : ExponentParity::Odd;
}

// The infinity case classifies the exponent with the incoming x87 environment saved.
static bool IsOddBinaryInteger(PowerWord magnitude)
{
    const int fractionalBits = 1075 - int(magnitude >> 52);
    if (fractionalBits < 0 || fractionalBits > 52)
        return false;
    const PowerWord units = 1ull << fractionalBits;
    const PowerWord significand = (magnitude & powerFraction) | powerHiddenBit;
    return (significand & (2 * units - 1)) == units;
}

// The x86 VC6 floating-point exception record uses three 24-byte operands.
struct PowerExceptionOperand {
    PowerWord bits;
    PowerWord padding;
    unsigned format;
    unsigned reserved;
};
struct PowerExceptionRecord {
    unsigned operation, cause, enable, status;
    PowerExceptionOperand base, exponent, result;
};
static_assert(sizeof(PowerExceptionRecord) == 88, "8.1 floating-point exception ABI");

static unsigned PowerRecordFlags(unsigned x87Flags)
{
    return ((x87Flags & 1) << 4) | ((x87Flags & 4) << 1) | ((x87Flags & 8) >> 1)
        | ((x87Flags & 16) >> 3) | ((x87Flags & 32) >> 5);
}

static __declspec(noinline) unsigned short DeliverPowerException(struct _exception& error, unsigned flags, unsigned short control)
{
    unsigned short status;
    __asm fnstsw status
    const unsigned precision = (control & 0x0300) == 0x0300 ? 0 : (control & 0x0300) == 0x0200 ? 1 : 2;
    PowerExceptionRecord record = {};
    record.operation = (29u << 5) | (precision << 2) | ((control >> 10) & 3);
    record.cause = PowerRecordFlags(flags);
    record.enable = PowerRecordFlags(~control);
    record.status = PowerRecordFlags(status);
    memcpy(&record.base.bits, &error.arg1, sizeof(double));
    record.base.format = 3;
    memcpy(&record.exponent.bits, &error.arg2, sizeof(double));
    record.exponent.format = 3;
    memcpy(&record.result.bits, &error.retval, sizeof(double));
    record.result.format = 3;
    const DWORD code = flags & 1 ? EXCEPTION_FLT_INVALID_OPERATION :
        flags & 4 ? EXCEPTION_FLT_DIVIDE_BY_ZERO : flags & 8 ? EXCEPTION_FLT_OVERFLOW :
        flags & 16 ? EXCEPTION_FLT_UNDERFLOW : EXCEPTION_FLT_INEXACT_RESULT;
    ULONG_PTR argument = reinterpret_cast<ULONG_PTR>(&record);
    __asm fnclex
    RaiseException(code, 0, 1, &argument);
    // 0x625ac6 applies the handler's enable bits before reloading the replacement.
    // A recognized precision field clears RC, preserving the precision bits already set.
    const unsigned enables = record.enable;
    const unsigned unmask = ((enables & 16) >> 4) | ((enables & 8) >> 1) | ((enables & 4) << 1)
                    | ((enables & 2) << 3) | ((enables & 1) << 5);
    unsigned short resumed = (control & ~(unmask | 0x0c00)) | ((record.operation & 3) << 10);
    const unsigned requestedPrecision = (record.operation >> 2) & 7;
    if (requestedPrecision < 3)
        resumed = (resumed & ~0x0c00) | (requestedPrecision == 0 ? 0x0300 : requestedPrecision == 1 ? 0x0200 : 0);
    double* destination = &error.retval;
    PowerWord replacement = record.result.bits;
    __asm {
        mov eax, destination
        fld qword ptr replacement
        fstp qword ptr [eax]
    }
    return resumed;
}

static __declspec(noinline) double __cdecl ReportPowerError(int type, double base, double exponent, double result, unsigned callerControl)
{
    struct _exception error;
    error.type = type;
    error.name = const_cast<char*>("pow");
    memcpy(&error.arg1, &base, sizeof base);
    memcpy(&error.arg2, &exponent, sizeof exponent);
    memcpy(&error.retval, &result, sizeof result);
    const unsigned flags = type == _DOMAIN ? 1 : type == _SING ? 4 : type == _OVERFLOW ? 0x28 : type == _UNDERFLOW ? 0x30 : 0x20;
    unsigned short resumed = static_cast<unsigned short>(callerControl);
    if (flags & ~callerControl) {
        resumed = DeliverPowerException(error, flags, resumed);
        __asm fldcw resumed
    }
    if (type == 8)
        return error.retval;
    if (!_matherr(&error) && type != _UNDERFLOW)
        errno = type == _DOMAIN ? EDOM : ERANGE;
    return error.retval;
}

// The VC6 underflow handler receives a normal significand scaled up by 2^1536.
// It shifts off the extra bits of the binary64 significand to form a subnormal.
static double UnscalePowerResult(double scaled)
{
    PowerWord bits = PowerBits(scaled);
    const PowerWord sign = bits & powerSign;
    const int shift = 1537 - int((bits >> 52) & 2047);
    if (shift >= 53)
        bits = (bits & ~powerSign) == 0 ? sign : 0;
    else if (shift >= 0)
        bits = sign | (((bits & powerFraction) | powerHiddenBit) >> shift);
    return PowerNumber(bits);
}

static __declspec(noinline) double __cdecl HandlePowerRange(double base, double exponent, double scaled, int overflow, unsigned callerControl)
{
    const double zero = 0.0;
    double result = scaled;
    if (!overflow && (callerControl & 0x10))
        result = UnscalePowerResult(scaled);
    if (overflow && (callerControl & 8)) {
        const PowerWord sign = PowerBits(scaled) & powerSign;
        const unsigned rounding = callerControl & 0x0c00;
        if (rounding == 0x0c00 || rounding == (sign ? 0x0800u : 0x0400u))
            result = PowerNumber(sign | (powerInfinity - 1));
    }
    if (callerControl & (overflow ? 8u : 16u)) {
        __asm {
            fld scaled
            fcomp zero
        }
    }
    RaisePowerFlags((overflow ? 0x28 : 0x30) & callerControl);
    return ReportPowerError(overflow ? _OVERFLOW : _UNDERFLOW, base, exponent, result, callerControl);
}

// 0x61dbbf and 0x624770 retain the extended result. The binary64 store at
// 0x62485c classifies its range and leaves the store's rounding indication in C1.
#pragma warning(push)
#pragma warning(disable: 4035)
static __declspec(noinline) double FinitePower(double base, double exponent, int negative, unsigned callerControl)
{
    const double boost = 1536.0;
    const double minNormal = DBL_MIN;
    const double reduce = -1536.0;
    const double maxNormal = DBL_MAX;
    const PowerWord infinity = powerInfinity;
    double rounded;
    __asm {
        fld exponent
        fld base
        fabs
        fyl2x
        fld st(0)
        frndint
        fxch st(1)
        fsub st(0), st(1)
        f2xm1
        fld1
        faddp st(1), st(0)
        fscale
        fstp st(1)
        cmp negative, 0
        je classify
        fchs
    classify:
        fst rounded
        mov eax, dword ptr rounded[4]
        and eax, 7ff00000h
        jz tiny
        cmp eax, 7ff00000h
        jne ordinary
        test callerControl, 8
        jnz overflowMasked
        fld reduce
        fxch st(1)
        fscale
        fstp st(1)
        fld st(0)
        fabs
        fcomp maxNormal
        fnstsw ax
        test ah, 41h
        jnz overflowScale
        fmul qword ptr infinity
    overflowScale:
        fstp rounded
        jmp overflowArgs
    overflowMasked:
        fstp st(0)
    overflowArgs:
        push callerControl
        push 1
        jmp range
    tiny:
        fld boost
        fxch st(1)
        fscale
        fstp st(1)
        fld st(0)
        fabs
        fcomp minNormal
        fnstsw ax
        test ah, 1
        jz storeScale
        fldz
        fmulp st(1), st(0)
    storeScale:
        fstp rounded
        push callerControl
        push 0
    range:
        push dword ptr rounded[4]
        push dword ptr rounded
        push dword ptr exponent[4]
        push dword ptr exponent
        push dword ptr base[4]
        push dword ptr base
        call HandlePowerRange
        add esp, 32
        jmp done
    ordinary:
        test callerControl, 20h
        jnz done
        fnstsw ax
        test ax, 20h
        jz done
        fstp rounded
        push callerControl
        push dword ptr rounded[4]
        push dword ptr rounded
        push dword ptr exponent[4]
        push dword ptr exponent
        push dword ptr base[4]
        push dword ptr base
        push 8
        call ReportPowerError
        add esp, 32
    done:
    }
}
#pragma warning(pop)

static __declspec(noinline) double __cdecl PowerNearest(double base, double exponent, unsigned callerControl)
{
    const PowerWord baseBits = PowerBits(base);
    const PowerWord exponentBits = PowerBits(exponent);
    const PowerWord magnitude = baseBits & ~powerSign;
    const PowerWord power = exponentBits & ~powerSign;
    const bool negativeExponent = (exponentBits & powerSign) != 0;

    // The original entry receives two x87 registers, including any operand exceptions raised on load.
    __asm {
        fld base
        fstp st(0)
        fld exponent
        fstp st(0)
    }
    if (magnitude > powerInfinity || power > powerInfinity) {
        volatile double payload = base + exponent;
        RaisePowerFlags(1 & callerControl);
        return ReportPowerError(_DOMAIN, base, exponent, payload, callerControl);
    }
    if (power == powerInfinity) {
        if (magnitude == powerOne) {
            RaisePowerFlags(1 & callerControl);
            return ReportPowerError(_DOMAIN, base, exponent, PowerNumber(powerIndefinite), callerControl);
        }
        return PowerNumber((magnitude > powerOne) != negativeExponent ? powerInfinity : 0);
    }
    if (magnitude == powerInfinity) {
        if (!power)
            return 1.0;
        const PowerWord sign = IsOddBinaryInteger(power) ? baseBits & powerSign : 0;
        return PowerNumber(sign | (negativeExponent ? 0 : powerInfinity));
    }
    if (!magnitude) {
        if (power < powerHiddenBit)
            return power ? 0.0 : 1.0;
        const PowerWord sign = PowerExponentParity(exponent) == ExponentParity::Odd ? baseBits & powerSign : 0;
        if (negativeExponent) {
            RaisePowerFlags(4 & callerControl);
            return ReportPowerError(_SING, base, exponent, PowerNumber(sign | powerInfinity), callerControl);
        }
        return PowerNumber(sign);
    }
    ExponentParity parity = ExponentParity::Even;
    if (baseBits & powerSign) {
        parity = PowerExponentParity(exponent);
        if (parity == ExponentParity::Fractional) {
            RaisePowerFlags(1 & callerControl);
            return ReportPowerError(_DOMAIN, base, exponent, PowerNumber(powerIndefinite), callerControl);
        }
    }
    return FinitePower(base, exponent, parity == ExponentParity::Odd, callerControl);
}

// The intrinsic evaluates with exceptions masked and round-to-nearest, retaining
// the incoming precision. Restore the caller's rounding before it stores ST(0).
#pragma warning(push)
#pragma warning(disable:4035)
static __declspec(noinline) double PowerWithCallerEnvironment(double base, double exponent, unsigned short control)
{
    const unsigned short internalControl = (control & 0x0300) | 0x007f;
    __asm {
        // The original register entry spills the operands before masking the exceptions.
        fld base
        fld exponent
        fxch st(1)
        fstp base
        fstp exponent
        fwait
        fldcw internalControl
        movzx eax, control
        push eax
        push dword ptr exponent[4]
        push dword ptr exponent
        push dword ptr base[4]
        push dword ptr base
        call PowerNearest
        add esp, 20
        fldcw control
        fwait
    }
}
#pragma warning(pop)

double x87_pow(double base, double exponent)
{
    unsigned short control;
    __asm fnstcw control
    if ((control & 0x0300) == 0x0100)
        return pow(base, exponent);
    if ((control & 0x0c3f) != 0x003f)
        return PowerWithCallerEnvironment(base, exponent, control);
    return PowerNearest(base, exponent, control);
}

double x87_acos(double x) { return acos(x); }
double x87_asin(double x) { return asin(x); }
// Keep the complementary component on the x87 stack through FPATAN.
// The release CRT uses this product for inputs strictly inside the unit interval.
static __declspec(noinline) double __cdecl PoseRollAngle(double component, int useCosine)
{
    if (!(fabs(component) < 1.0))
        return useCosine ? acos(component) : asin(component);
    double complement = sqrt((1.0 + component) * (1.0 - component));
    return useCosine ? atan2(complement, component) : atan2(component, complement);
}

void x87_store_pose_roll(double* output, double component, float signComponent, bool useCosine, const double& pi)
{
    unsigned short controlWord;
    // Store the first angle without reducing the precision of the copy used
    // for a quadrant correction. FSTP sets C1 when that final store rounds up.
    __asm {
        movzx eax, useCosine
        push eax
        push dword ptr [component+4]
        push dword ptr [component]
        call PoseRollAngle
        add esp, 12
        mov ecx, output
        fst qword ptr [ecx]
        fnstcw controlWord
        mov ax, controlWord
        and ax, 0300h
        cmp ax, 0300h
        jne testSign
        // Under extended precision the CRT returns the binary64 result.
        fstp st(0)
        fld qword ptr [ecx]
    testSign:
        fld signComponent
        ftst
        fnstsw ax
        fstp st(0)
        test ah, 1
        jz discard
        cmp useCosine, 0
        je adjustSine
        fchs
        fstp qword ptr [ecx]
        jmp finished
    adjustSine:
        mov eax, pi
        fld qword ptr [eax]
        fsub st(0), st(1)
        fstp qword ptr [ecx]
    discard:
        fstp st(0)
    finished:
    }
}
double x87_fmod(double x, double y) { return fmod(x, y); }
