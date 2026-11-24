// The integer and byte deques use the same 512-byte allocation and rollback.
extern "C" void* __cdecl Stl_OomMalloc(unsigned bytes);
extern "C" void __cdecl Ai_PoolFree(void* address, unsigned bytes);

template <class T>
static void AllocateDequePages(T** first, T** last)
{
    const unsigned pageSize = 512;
    T** next = first;
    try {
        for (; next < last; ++next)
            *next = static_cast<T*>(Stl_OomMalloc(pageSize));
    } catch (...) {
        for (T** slot = first; slot < next; ++slot)
            Ai_PoolFree(*slot, pageSize);
        throw;
    }
}

// [FUN_4a29e0] Allocate the pages for a range of slots in the integer deque map.
extern "C" void __stdcall IntDeque_AllocatePages(int** first, int** last)
{
    AllocateDequePages(first, last);
}

// [FUN_4ac330] Fill a range of byte deque map slots with pages.
extern "C" void __fastcall Deque_AllocPages512(unsigned char*, void*, char** first, char** last)
{
    AllocateDequePages(first, last);
}
