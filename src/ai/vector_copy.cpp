// Uninitialized copies of records with an owned byte vector.
#include "king.h"
#include <string.h>

extern "C" void* __cdecl Stl_Allocate(unsigned size);
extern "C" void __cdecl Ai_PoolFree(void* ptr, unsigned size);

struct ByteVector {
    BYTE* first;
    BYTE* last;
    BYTE* limit;
};
static_assert(sizeof(ByteVector) == 12, "x86 byte vector layout");

static void CopyVectorBytes(ByteVector& dest, const ByteVector& src, unsigned size)
{
    dest.first = dest.last = dest.limit = 0;
    if (!size)
        return;
    BYTE* bytes = static_cast<BYTE*>(Stl_Allocate(size));
    dest.first = dest.last = bytes;
    dest.limit = bytes + size;
    memmove(bytes, src.first, size);
    dest.last = bytes + size;
}

static void FreeCompletedVectors(BYTE* first, BYTE* last, unsigned stride, unsigned offset)
{
    for (; first != last; first += stride) {
        if (!first)
            continue;
        const ByteVector& v = *reinterpret_cast<const ByteVector*>(first + offset);
        if (v.first)
            Ai_PoolFree(v.first, static_cast<unsigned>(v.limit - v.first));
    }
}

template <unsigned Stride, unsigned Offset>
static BYTE* CopyVectorRecords(const BYTE* first, const BYTE* last, BYTE* dest)
{
    BYTE* const start = dest;
    try {
        for (; first != last; first += Stride, dest += Stride) {
            if (!dest)
                continue;
            if (Offset)
                memcpy(dest, first, Offset);
            const ByteVector& src = *reinterpret_cast<const ByteVector*>(first + Offset);
            ByteVector& copy = *reinterpret_cast<ByteVector*>(dest + Offset);
            CopyVectorBytes(copy, src, static_cast<unsigned>(src.last - src.first));
        }
    } catch (...) {
        FreeCompletedVectors(start, dest, Stride, Offset);
        throw;
    }
    return dest;
}

// [FUN_466300] [FUN_47d360] Copy records with one key and an owned byte vector.
extern "C" BYTE* __cdecl Rec16_UninitCopy(BYTE* first, BYTE* last, BYTE* dest, BYTE*)
{
    return CopyVectorRecords<16, 4>(first, last, dest);
}

// [FUN_466200] [FUN_466d50] Copy two keys and a byte vector, preserving trailing padding.
extern "C" BYTE* __cdecl Rec24_UninitCopy(BYTE* first, BYTE* last, BYTE* dest, BYTE*)
{
    return CopyVectorRecords<24, 8>(first, last, dest);
}

// [FUN_467320] Copy an array of byte vectors.
extern "C" BYTE* __cdecl CharVec_UninitCopy(BYTE* first, BYTE* last, BYTE* dest, BYTE*)
{
    return CopyVectorRecords<12, 0>(first, last, dest);
}

// [FUN_466440] Construct count copies of the same vector of 32-bit integers.
extern "C" BYTE* __cdecl IntVec_UninitFillN(BYTE* dest, unsigned count, const BYTE* value, int)
{
    BYTE* const start = dest;
    try {
        for (; count; --count, dest += sizeof(ByteVector)) {
            if (dest) {
                const ByteVector& src = *reinterpret_cast<const ByteVector*>(value);
                unsigned size = static_cast<unsigned>(src.last - src.first) & ~3u;
                CopyVectorBytes(*reinterpret_cast<ByteVector*>(dest), src, size);
            }
        }
    } catch (...) {
        FreeCompletedVectors(start, dest, sizeof(ByteVector), 0);
        throw;
    }
    return dest;
}

extern "C" BYTE* __cdecl UninitFill4s(BYTE* dest, int count, const DWORD* value);
extern "C" void* __cdecl malloc(size_t);
extern "C" void __cdecl free(void*);

// [FUN_462340] Allocate a temporary dword buffer, halving the request after a null return.
extern "C" int* __fastcall TempBuf4_Construct(int* self, void*, DWORD* first, DWORD* last)
{
    self[0] = static_cast<int>(last - first);
    self[1] = self[0] > 536870911 ? 536870911 : self[0];
    self[2] = 0;
    try {
        while (self[1] > 0) {
            self[2] = reinterpret_cast<int>(malloc(self[1] * sizeof(DWORD)));
            if (self[2]) {
                UninitFill4s(reinterpret_cast<BYTE*>(self[2]), self[1], first);
                break;
            }
            self[1] /= 2;
        }
    } catch (...) {
        free(reinterpret_cast<void*>(self[2]));
        self[2] = self[1] = 0;
        throw;
    }
    return self;
}
