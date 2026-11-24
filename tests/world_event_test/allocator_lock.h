extern "C" void __fastcall StlAllocator_AcquireLock(volatile LONG*, void*);
extern "C" void* Stl_Allocate(unsigned);
extern "C" void Ai_PoolFree(void*, unsigned);

namespace allocator_lock {
using SleepFn = void (WINAPI*)(DWORD);
#ifndef STOCK_WORLD
extern "C" SleepFn allocatorSleepImport;
#pragma comment(linker, "/alternatename:_allocatorSleepImport=__imp__Sleep@4")
#endif

static volatile LONG* held;
static DWORD waits[32];
static unsigned waitCount, releaseAt;

static void WINAPI releaseOnWait(DWORD ms) {
    require(waitCount < releaseAt && *held == 1, "wait while owned");
    waits[waitCount++] = ms;
    if (waitCount == releaseAt)
        InterlockedExchange(held, 0);
}

// Only the private worker's Sleep import is replaced, for the duration of
// the scripted waits. Atomic operations in both lock implementations run natively.
static SleepFn replaceSleep(SleepFn replacement) {
#ifdef STOCK_WORLD
    auto slot = reinterpret_cast<SleepFn*>(0x64b2b4);
#else
    auto slot = &allocatorSleepImport;
#endif
    DWORD oldProtection, ignored;
    require(VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtection) != 0, "access Sleep import");
    SleepFn previous = *slot;
    *slot = replacement;
    require(VirtualProtect(slot, sizeof(*slot), oldProtection, &ignored) != 0, "restore import protection");
    return previous;
}

static DWORD WINAPI releaseOnExchange(void* ready) {
    SetEvent(static_cast<HANDLE>(ready));
    while (*held == 2)
        YieldProcessor();
    InterlockedExchange(held, 0);
    return 0;
}

static void checkSpin(unsigned& limit, unsigned& last) {
    // Allow the other thread time to run, but permit exactly one eligible
    // spin iteration. A late release can only acquire on the fallback path.
    constexpr unsigned eligible = 4 * 1024 * 1024;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        limit = eligible + 1;
        last = 2 * eligible + 1;
        *held = 2;
        HANDLE ready = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        require(ready != nullptr, "spin worker event");
        HANDLE thread = CreateThread(nullptr, 0, releaseOnExchange, ready, 0, nullptr);
        require(thread && WaitForSingleObject(ready, 5000) == WAIT_OBJECT_0, "spin worker ready");
        ROUTINE(StlAllocator_AcquireLock, 0x401aa0)(held, nullptr);
        require(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0 && *held == 1, "spin worker handoff");
        CloseHandle(thread);
        CloseHandle(ready);
        if (limit == 1000 && last == eligible) {
            fprintf(report, "spin acquired at %u limit=%u\n", last, limit);
            return;
        }
        require(limit == 30 && last == 2 * eligible + 1, "late release reached fallback");
    }
    require(false, "worker missed the eligible spin or spin state differed");
}

static void check() {
    auto& limit = field<unsigned>(global(0x66c83c), 0);
    auto& last = field<unsigned>(global(0x68d208), 0);
    const unsigned savedLimit = limit, savedLast = last;
    struct { DWORD left; LONG word; DWORD right; } lock = {0xfeed1234, 0, 0xfade5678};
    held = &lock.word;
    limit = 37;
    last = 11;
    ROUTINE(StlAllocator_AcquireLock, 0x401aa0)(held, nullptr);
    require(*held == 1 && limit == 37 && last == 11, "uncontended acquire preserves tuning");
    fprintf(report, "free acquired limit=%u last=%u\n", limit, last);
    checkSpin(limit, last);

    SleepFn systemSleep = replaceSleep(releaseOnWait);
    const DWORD expected[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,4,8,16,32,64,128,128,128};
    for (unsigned budget : {0u, 1u, 30u, 1000u}) {
        limit = budget;
        last = 9;
        *held = 1;
        waitCount = 0;
        releaseAt = _countof(expected);
        ROUTINE(StlAllocator_AcquireLock, 0x401aa0)(held, nullptr);
        require(waitCount == releaseAt && !memcmp(waits, expected, sizeof(expected)), "sleep sequence");
        require(*held == 1 && limit == 30 && last == 9, "fallback acquire state");
        fprintf(report, "backoff initial=%u waits=%u last_delay=%lu limit=%u last=%u\n",
                budget, waitCount, waits[waitCount - 1], limit, last);
    }
    require(lock.left == 0xfeed1234 && lock.right == 0xfade5678, "lock word boundaries");

    held = reinterpret_cast<volatile LONG*>(global(0x68d204));
    require(*held == 0, "allocator initially unlocked");
    for (unsigned bytes : {8u, 128u}) {
        auto& head = field<void*>(global(0x68d1c0), int((bytes + 7) / 8 * sizeof(void*)));
        void* savedHead = head;
        alignas(8) BYTE block[128];
        head = block;
        field<void*>(block, 0) = savedHead;
        *held = 1;
        waitCount = 0;
        releaseAt = 1;
        require(ROUTINE(Stl_Allocate, 0x44b740)(bytes) == block && head == savedHead &&
                waitCount == 1 && *held == 0, "allocate waits, pops head and unlocks");
        *held = 1;
        waitCount = 0;
        ROUTINE(Ai_PoolFree, 0x44b4a0)(block, bytes);
        require(head == block && field<void*>(block, 0) == savedHead &&
                waitCount == 1 && *held == 0, "free waits, pushes head and unlocks");
        head = savedHead;
        fprintf(report, "pool bytes=%u allocate+free unlocked\n", bytes);
    }
    replaceSleep(systemSleep);
    limit = savedLimit;
    last = savedLast;
}
}
