extern "C" int __fastcall Controls_ActionState(BYTE*, void*, int);
struct PtrDequeIt { void **cur, **first, **last, ***node; };
extern "C" PtrDequeIt* __fastcall SchDequeIt_Plus(const PtrDequeIt*, void*, PtrDequeIt*, int);
extern "C" void Joy_Log(const char*);
extern "C" float File_ReadFloat(FILE*);
extern "C" double DVec3_Length(const DVec3*);
extern "C" int Player_NotValid(BYTE*);
extern "C" Vec3* Vec3_ScaleBy(Vec3*, float, const Vec3*);
extern "C" double PlayerNodes_MinDistance(const int* const*, const BYTE**);
extern "C" BYTE* Pool_Prepare(int, BYTE*);
extern "C" void* __fastcall Orientation_CopyRow(const BYTE*, void*, void*, int);

namespace controls_and_ai {
static void actions() {
    BYTE controls[640] = {};
    BYTE values[] = {0, 1, 128, 255};
    for (int index : {0, 26, 46}) {
        for (int a = -1; a < 4; ++a) {
            for (int b = -1; b < 4; ++b) {
                field<BYTE*>(controls, 264 + index * 4) = a < 0 ? nullptr : values + a;
                field<BYTE*>(controls, 452 + index * 4) = b < 0 ? nullptr : values + b;
                int expected = a > 0 ? values[a] : b > 0 ? values[b] : 0;
                require(ROUTINE(Controls_ActionState, 0x530070)(controls, nullptr, index) == expected,
                        "action returns first active unsigned byte");
            }
        }
    }
    fprintf(report, "controls 75 binding cases\n");
}

static void dequeOffsets() {
    void* blocks[5][128];
    void** map[] = {blocks[4], blocks[0], blocks[3], blocks[1], blocks[2]};
    PtrDequeIt initial = {map[2] + 3, map[2], map[2] + 128, map + 2};
    for (int delta : {-259,-132,-131,-4,-3,-2,0,124,125,126,252,253,380}) {
        for (bool alias : {false, true}) {
            int index = 259 + delta, block = index / 128;
            PtrDequeIt source = initial, result = {};
            PtrDequeIt* out = alias ? &source : &result;
            require(ROUTINE(SchDequeIt_Plus, 0x4b2730)(&source, nullptr, out, delta) == out &&
                    out->cur == map[block] + index % 128 && out->first == map[block] &&
                    out->last == map[block] + 128 && out->node == map + block, "iterator signed displacement");
            if (!alias) require(!memcmp(&source, &initial, sizeof(source)), "source iterator unchanged");
        }
    }
    fprintf(report, "deque 26 displacements across five nonconsecutive blocks\n");
}

static const char* lastLog;
static int logCount;
static void captureLog(const char* message) { lastLog = message; ++logCount; }
static void joystickLog() {
    using Log = void(__cdecl*)(const char*);
    Log& sink = field<Log>(global(0x6f4bac), 0);
    Log saved = sink;
    const char msg[] = "joystick test";
    for (Log target : {Log(nullptr), Log(captureLog), Log(nullptr)}) {
        sink = target;
        int before = logCount;
        ROUTINE(Joy_Log, 0x576d50)(msg);
        require(logCount == before + int(target != nullptr) && (!target || lastLog == msg), "joystick log dispatch");
    }
    sink = saved;
    fprintf(report, "joystick callback calls=%d\n", logCount);
}

static void binaryFloats() {
    const DWORD words[] = {0, 0x80000000, 0x3f804001, 0xc0400000, 1, 0x7f7fffff, 0x7f800000, 0x7fc12345};
    const char* path = "read-float-fixture.bin";
    FILE* writer = fopen(path, "wb");
    require(writer && fwrite(words, sizeof(DWORD), 8, writer) == 8 && fclose(writer) == 0, "binary fixture");
    // Open and close inside the implementation's own CRT (VC6 and UCRT have different FILE layouts).
    FILE* in = ROUTINE(fopen, 0x61cc96)(path, "rb");
    require(in != nullptr, "binary stream");
    for (DWORD expected : words) {
        float result = ROUTINE(File_ReadFloat, 0x5ab190)(in);
        DWORD bits;
        memcpy(&bits, &result, sizeof(bits));
        require(bits == expected, "successive binary32 reads");
        fprintf(report, "file float %08lx\n", bits);
    }
    require(ROUTINE(fclose, 0x61cb9a)(in) == 0, "close binary stream");
}

static void lengths() {
    const DVec3 vectors[] = {{0,-0.0,0}, {-3,4,12}, {1,1,1}, {1,0x1p-26,0x1p-27},
                             {1e200,0,0}, {0,1e-200,0}, {1,0x1.6p-27,0x1.6p-27}, {.34, 127.0081, -43.921}};
    unsigned saved = _controlfp(0, 0);
    decltype(&DVec3_Length) lengthFn = ROUTINE(DVec3_Length, 0x4577f0);
    for (unsigned pc : {_PC_53, _PC_64}) {
        for (unsigned rc : {_RC_NEAR, _RC_DOWN, _RC_UP, _RC_CHOP}) {
            _controlfp(pc | rc | _MCW_EM, _MCW_PC | _MCW_RC | _MCW_EM);
            for (const auto& v : vectors) {
                const DVec3* input = &v;
                BYTE extended[10];
                _clearfp();
                __asm {
                    push input
                    call lengthFn
                    add esp, 4
                    fstp tbyte ptr extended
                }
                unsigned flags = _statusfp() & _MCW_EM;
                fprintf(report, "length case=%d pc=%x rc=%x result=", int(&v - vectors), pc, rc);
                for (int i = 9; i >= 0; --i) fprintf(report, "%02x", extended[i]);
                fprintf(report, " flags=%x\n", flags);
            }
        }
    }
    _clearfp();
    _controlfp(saved, _MCW_PC | _MCW_RC | _MCW_EM);
}

static void playerMarker() {
    BYTE record[96] = {};
    for (DWORD marker : {0u, 0x12345678u, 0x12345670u, 0xffffffffu}) {
        field<DWORD>(record, 84) = field<DWORD>(record, 92) = 0x12345678;
        field<DWORD>(record, 88) = marker;
        int invalid = ROUTINE(Player_NotValid, 0x4566c0)(record);
        require(invalid == int(marker != 0x12345678), "player marker at offset 88");
        fprintf(report, "marker %08lx invalid=%d\n", marker, invalid);
    }
}

static void effectScale() {
    const float inputs[][4] = {{1,-2,4,-.5f}, {-0.f,0.f,2.f,-0.f}, {1.00000011920928955f,-3.25f,7,1.5f}};
    for (const auto& row : inputs) {
        for (int offset : {1,2,3,6}) {
            float arena[10], expected[10];
            for (float& x : arena) x = 197.f;
            memcpy(arena + 2, row, 3 * sizeof(float));
            memcpy(expected, arena, sizeof(arena));
            for (int i = 0; i < 3; ++i) expected[offset + i] = row[i] * row[3];
            auto* out = reinterpret_cast<Vec3*>(arena + offset);
            require(ROUTINE(Vec3_ScaleBy, 0x4bff20)(out, row[3], reinterpret_cast<Vec3*>(arena + 2)) == out &&
                    !memcmp(expected, arena, sizeof(arena)), "effect scale and overlapping output");
        }
    }
    fprintf(report, "effect scale 12 cases\n");
}

static void nearestNodes() {
    alignas(8) BYTE storage[5][72] = {};
    BYTE* nodes[] = {storage[0],storage[1],storage[2],storage[3],storage[4]};
    const DVec3 positions[] = {{2,3,4},{5,7,16},{2,3,6},{2000002,3,4},{2,1000003,4}};
    for (int i = 0; i < 5; ++i) field<DVec3>(nodes[i], 48) = positions[i];
    BYTE**& table = field<BYTE**>(global(0x68cf00), 0);
    BYTE** saved = table;
    table = nodes;
    int node = 0;
    const int* key = &node;
    BYTE entries[3][36] = {};
    BYTE* sentinel = field<BYTE*>(global(0x68b990), 0);
    struct Case { int size, first, second, skipMask; double expected; };
    const Case cases[] = {{0,0,0,0,1000000},{2,1,2,3,1000000},{2,1,2,2,13},{2,2,1,0,2},
                          {2,1,2,0,2},{1,0,1,0,0},{2,3,4,0,1000000},{1,3,2,0,1000000}};
    for (const Case& c : cases) {
        field<int>(entries[0], 0) = c.first;
        field<int>(entries[1], 0) = c.second;
        for (int i = 0; i < 2; ++i) field<BYTE*>(entries[i], 8) = (c.skipMask & (1 << i)) ? sentinel : storage[i];
        const BYTE* range[] = {entries[0], entries[c.size]};
        double d = ROUTINE(PlayerNodes_MinDistance, 0x418890)(&key, range);
        require(d == c.expected, "nearest eligible node distance");
        fprintf(report, "nearest case=%d distance=%.0f\n", int(&c - cases), d);
    }
    field<int>(entries[0], 0) = 1;
    field<BYTE*>(entries[0], 8) = storage[0];
    const BYTE* one[] = {entries[0], entries[1]};
    const DVec3 origins[] = {{0,0,0}, {0x1p-55,-0x1p-54,0x1p-52}};
    const DVec3 fractional[] = {{1,1,1}, {0x1p-26, 1, 0x1p-27}, {21.457, -36.987, 15.25}};
    unsigned savedFp = _controlfp(0, 0);
    for (unsigned pc : {_PC_53, _PC_64}) {
        for (unsigned rc : {_RC_NEAR, _RC_DOWN, _RC_UP, _RC_CHOP}) {
            _controlfp(pc | rc, _MCW_PC | _MCW_RC);
            for (const auto& origin : origins) {
                field<DVec3>(nodes[0], 48) = origin;
                for (const auto& point : fractional) {
                    field<DVec3>(nodes[1], 48) = point;
                    double d = ROUTINE(PlayerNodes_MinDistance, 0x418890)(&key, one);
                    unsigned long long bits;
                    memcpy(&bits, &d, sizeof(bits));
                    fprintf(report, "nearest origin=%d fractional=%d pc=%x rc=%x distance=%016llx\n",
                            int(&origin - origins), int(&point - fractional), pc, rc, bits);
                }
            }
        }
    }
    _controlfp(savedFp, _MCW_PC | _MCW_RC);
    table = saved;
}

static void preparePool() {
    BYTE pool[16] = {}, first[24] = {}, second[24] = {};
    field<BYTE*>(pool,4) = first;
    field<BYTE*>(first,8) = second;
    field<BYTE*>(second,12) = first;
    field<DWORD>(first,4) = field<DWORD>(second,4) = 1;
    BYTE* scratch = global(0x6f4bc8);
    DWORD saved[4];
    memcpy(saved, scratch, sizeof(saved));
    auto prepare = ROUTINE(Pool_Prepare, 0x579380);
    require(prepare(24,pool) == first && field<BYTE*>(pool,4) == second && field<BYTE*>(pool,8) == first &&
            !field<BYTE*>(second,12) && !field<BYTE*>(first,8) && field<BYTE*>(first,16) == pool &&
            !field<DWORD>(first,4), "first object moves from free to used list");
    field<BYTE*>(second,16) = pool;
    require(prepare(24,pool) == second && !field<BYTE*>(pool,4) && field<BYTE*>(pool,8) == second &&
            field<BYTE*>(second,8) == first && field<BYTE*>(first,12) == second && !field<DWORD>(second,4),
            "prepend second used object");
    DWORD expected[] = {0, DWORD(first), 0, DWORD(pool)};
    require(!memcmp(expected,scratch,sizeof(expected)) && !prepare(24,pool) && !memcmp(expected,scratch,sizeof(expected)),
            "constructor scratch survives exhausted pool");
    memcpy(scratch, saved, sizeof(saved));
    fprintf(report, "pool first, second, exhausted\n");
}

static void orientationRows() {
    DWORD matrix[4][6];
    for (DWORD i = 0; i < 24; ++i) matrix[i / 6][i % 6] = 0xfff00000u + i;
    for (int i = 0; i < 4; ++i) {
        DWORD guarded[8] = {11,0,0,0,0,0,0,13};
        require(ROUTINE(Orientation_CopyRow, 0x41ce30)(reinterpret_cast<BYTE*>(matrix), nullptr, guarded+1, i) == guarded+1 &&
                !memcmp(matrix[i], guarded+1, 24) && guarded[0] == 11 && guarded[7] == 13, "orientation row copy");
    }
    fprintf(report, "orientation four 24-byte rows\n");
}

static void check() {
    actions();
    dequeOffsets();
    joystickLog();
    binaryFloats();
    lengths();
    playerMarker();
    effectScale();
    nearestNodes();
    preparePool();
    orientationRows();
}
}
