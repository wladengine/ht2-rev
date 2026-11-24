#include <psapi.h>

extern "C" BYTE* Rec16_UninitCopy(BYTE*, BYTE*, BYTE*, BYTE*);
extern "C" BYTE* Rec24_UninitCopy(BYTE*, BYTE*, BYTE*, BYTE*);
extern "C" BYTE* CharVec_UninitCopy(BYTE*, BYTE*, BYTE*, BYTE*);
using AllocationHandler = int (__cdecl*)(size_t);
extern "C" AllocationHandler _set_new_handler(AllocationHandler);
extern "C" int _set_new_mode(int);

extern "C" BYTE* IntVec_UninitFillN(BYTE*, unsigned, const BYTE*, int);
extern "C" BYTE* DpRecords_UninitCopy(BYTE*, BYTE*, BYTE*, BYTE*);
extern "C" int* __fastcall TempBuf4_Construct(int*, void*, DWORD*, DWORD*);

namespace vector_copy_failure {
// Limit only this disposable worker. The source allocation is valid before the
// limit is applied, so failure occurs inside the actual game allocator.
static SIZE_T privateBytes() {
    PROCESS_MEMORY_COUNTERS_EX counters = {};
    require(GetProcessMemoryInfo(GetCurrentProcess(),
        reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof counters) != 0,
        "read worker commit charge");
    return counters.PrivateUsage;
}

static HANDLE limitWorkerMemory(SIZE_T extra = 4 * 1024 * 1024) {
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    require(job != nullptr, "create worker job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit = {};
    limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    limit.ProcessMemoryLimit = privateBytes() + extra;
    require(SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limit, sizeof limit)
        && AssignProcessToJobObject(job, GetCurrentProcess()), "limit worker memory");
    return job;
}

static void check() {
    const unsigned size = 16 * 1024 * 1024;
    BYTE* large = static_cast<BYTE*>(VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    require(large != nullptr, "large source vector");
    memset(large, 0x57, size);
    BYTE* blocks[6];
    for (auto& block : blocks)
        block = static_cast<BYTE*>(ROUTINE(Stl_Allocate, 0x44b740)(8));
    for (int i = 5; i >= 0; --i)
        ROUTINE(Ai_PoolFree, 0x44b4a0)(blocks[i], 8);
    ROUTINE(_set_new_handler, 0x61fd1c)(nullptr);
    ROUTINE(_set_new_mode, 0x61fcff)(0);
    HANDLE job = limitWorkerMemory();
    using Copy = decltype(&Rec16_UninitCopy);
    struct Layout { Copy copy; unsigned stride, prefix; };
    const Layout layouts[] = {
        {ROUTINE(Rec16_UninitCopy, 0x466300), 16, 4}, {ROUTINE(Rec16_UninitCopy, 0x47d360), 16, 4},
        {ROUTINE(Rec24_UninitCopy, 0x466200), 24, 8}, {ROUTINE(Rec24_UninitCopy, 0x466d50), 24, 8},
        {ROUTINE(CharVec_UninitCopy, 0x467320), 12, 0}
    };
    for (const Layout& layout : layouts) {
        for (unsigned failure = 0; failure < 4; ++failure) {
            DWORD input[24] = {}, output[26];
            memset(output, 0xa5, sizeof output);
            BYTE* source = reinterpret_cast<BYTE*>(input);
            BYTE* dest = reinterpret_cast<BYTE*>(output + 1);
            BYTE* chain[6];
            BYTE* node = field<BYTE*>(global(0x68d1c4), 0);
            for (auto& entry : chain) {
                require(node != nullptr, "available small block");
                entry = node;
                node = field<BYTE*>(node, 0);
            }
            for (unsigned i = 0; i < 4; ++i) {
                BYTE* record = source + i * layout.stride;
                memset(record, 0x21 + i, layout.prefix);
                field<BYTE*>(record, layout.prefix) = large;
                field<BYTE*>(record, layout.prefix + 4) = large + (i == failure ? size : 8);
                field<BYTE*>(record, layout.prefix + 8) = field<BYTE*>(record, layout.prefix + 4);
            }
            bool caught = false;
            try { layout.copy(source, source + 4 * layout.stride, dest, nullptr); }
            catch (...) { caught = true; }
            require(caught, "allocation failure propagated");
            node = field<BYTE*>(global(0x68d1c4), 0);
            for (unsigned i = 0; i < 6; ++i) {
                require(node == chain[i < failure ? failure - i - 1 : i], "partial copy cleanup order");
                node = field<BYTE*>(node, 0);
            }
            BYTE* failed = dest + layout.stride * failure;
            require(memcmp(failed, source + layout.stride * failure, layout.prefix) == 0, "failed record keys");
            for (unsigned j = 0; j < 12; j += 4)
                require(field<DWORD>(failed, layout.prefix + j) == 0, "failed vector is empty");
            for (BYTE* tail = failed + layout.prefix + 12; tail < reinterpret_cast<BYTE*>(output + 26); ++tail)
                require(*tail == 0xa5, "untouched destination tail");
            require(output[0] == 0xa5a5a5a5, "destination lower bound");
            fprintf(report, "stride=%u prefix=%u completed=%u\n", layout.stride, layout.prefix, failure);
        }
    }
    CloseHandle(job);
}

static int __cdecl throwFromHandler(size_t) { throw 314; }

static void checkLargeCopies(bool providers) {
    const unsigned size = 8 * 1024 * 1024;
    BYTE* source = static_cast<BYTE*>(VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    require(source != nullptr, "source buffer for four copies");
    memset(source, 0x36, size);
    BYTE* value[] = {source, source + size, source + size};
    struct Provider { DWORD guid[4]; BYTE* data; unsigned size; const char* name; int flags; };
    Provider records[4] = {};
    for (int i = 0; i < 4; ++i) {
        records[i].guid[0] = 11 + i;
        records[i].data = source;
        records[i].size = size;
        records[i].name = "audit";
        records[i].flags = 14 + i;
    }
    DWORD storage[34];
    memset(storage, 0xa5, sizeof storage);
    BYTE* dest = reinterpret_cast<BYTE*>(storage + 1);
    ROUTINE(_set_new_handler, 0x61fd1c)(nullptr);
    ROUTINE(_set_new_mode, 0x61fcff)(0);
    field<AllocationHandler>(global(0x685e30), 0) = throwFromHandler;
    HANDLE job = limitWorkerMemory(2 * size + 2 * 1024 * 1024);
    const SIZE_T before = privateBytes();
    int caught = 0;
    try {
        if (providers) {
            ROUTINE(DpRecords_UninitCopy, 0x602ad0)(reinterpret_cast<BYTE*>(records),
                reinterpret_cast<BYTE*>(records + 4), dest, nullptr);
        } else {
            ROUTINE(IntVec_UninitFillN, 0x466440)(dest, 4, reinterpret_cast<BYTE*>(value), 0);
        }
    } catch (int code) { caught = code; }
      catch (...) { caught = -1; }
    require(caught == (providers ? 314 : -1), "expected allocation exception propagated");
    require(privateBytes() < before + size / 2, "completed large copies released");
    if (providers) {
        const Provider* out = reinterpret_cast<const Provider*>(dest);
        require(out[0].size == size && out[1].size == size && out[2].guid[0] == 13 &&
            out[2].size == 0xa5a5a5a5 && out[3].guid[0] == 0xa5a5a5a5, "provider copy stopped on third record");
    } else {
        require(storage[1] != 0xa5a5a5a5 && storage[4] != 0xa5a5a5a5 &&
            storage[7] == 0 && storage[8] == 0 && storage[9] == 0 &&
            storage[10] == 0xa5a5a5a5, "third vector header cleared");
    }
    require(storage[0] == 0xa5a5a5a5 && storage[33] == storage[0], "large copy bounds");
    fprintf(report, "completed=2 propagated=%d released=2\n", caught);
    CloseHandle(job);
}

static void checkTemporaryBuffer() {
    const unsigned count = 2 * 1024 * 1024;
    DWORD* values = static_cast<DWORD*>(VirtualAlloc(nullptr, count * sizeof(DWORD),
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    require(values != nullptr, "temporary buffer source");
    memset(values, 0x36, count * sizeof(DWORD));
    HANDLE job = limitWorkerMemory(2 * 1024 * 1024 + 256 * 1024);
    ROUTINE(_set_new_handler, 0x61fd1c)(throwFromHandler);
    ROUTINE(_set_new_mode, 0x61fcff)(1);
    int buf[3] = {}, caught = 0;
    try { ROUTINE(TempBuf4_Construct, 0x462340)(buf, nullptr, values, values + count); }
    catch (int code) { caught = code; }
    require(caught == 314 && buf[0] == count && !buf[1] && !buf[2], "temporary buffer reset on exception");
    ROUTINE(_set_new_handler, 0x61fd1c)(nullptr);
    ROUTINE(_set_new_mode, 0x61fcff)(0);
    require(ROUTINE(TempBuf4_Construct, 0x462340)(buf, nullptr, values, values + count) == buf
        && buf[0] == count && buf[1] == count / 4 && buf[2], "temporary buffer halves the requested capacity");
    DWORD* stored = reinterpret_cast<DWORD*>(buf[2]);
    require(stored[0] == values[0] && stored[buf[1] - 1] == values[0], "temporary buffer filled");
    fprintf(report, "count=%d capacity=%d propagated=%d\n", buf[0], buf[1], caught);
    CloseHandle(job);
}
} // namespace vector_copy_failure
