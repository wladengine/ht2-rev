// Copy the DirectPlay provider records and release fully constructed elements on unwind.
#include "king.h"

extern "C" BYTE* __fastcall DpRecord_Copy(BYTE* dest, void*, const BYTE* src);
extern "C" void __fastcall Gate_FreeBuffers(BYTE* record, int);

// [FUN_602ad0]
extern "C" BYTE* __cdecl DpRecords_UninitCopy(BYTE* first, BYTE* last, BYTE* dest, BYTE*)
{
    BYTE* const start = dest;
    try {
        for (; first != last; first += 32, dest += 32)
            if (dest)
                DpRecord_Copy(dest, nullptr, first);
    } catch (...) {
        for (BYTE* record = start; record != dest; record += 32)
            Gate_FreeBuffers(record, 0);
        throw;
    }
    return dest;
}
