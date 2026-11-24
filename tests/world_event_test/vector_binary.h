extern "C" Vec3* Vec3_Add(Vec3*, const Vec3*, const Vec3*);
extern "C" Vec3* Vec3_Sub(Vec3*, const Vec3*, const Vec3*);
extern "C" float* Vec3_PlaneHit(float*, float*, float*, float*, float*);

namespace vector_binary {
static void checkOperation(const char* name, decltype(&Vec3_Add) operation) {
    struct Pair { DWORD a[3], b[3]; };
    const Pair pairs[] = {
        {{0x3f800000, 0x40000000, 0x40800000}, {0x41000000, 0xc0000000, 0x3f000000}},
        {{0x00000000, 0x80000000, 0x80000000}, {0x80000000, 0x80000000, 0x00000000}},
        {{0x3f800000, 0x3f800001, 0xbf800000}, {0x33800000, 0x33800000, 0xb3800000}},
        {{0x00800000, 0x007fffff, 0x00000001}, {0x807fffff, 0x00000001, 0x80000001}},
        {{0x7f7fffff, 0xff7fffff, 0xff7fffff}, {0x7f7fffff, 0xff7fffff, 0x7f7fffff}},
        {{0x7f800000, 0xff800000, 0x00000000}, {0xff800000, 0xbf800000, 0x7f800000}},
        {{0x7fc00111, 0x7f800123, 0xffc00222}, {0xffc00333, 0x3f800000, 0x7f800321}},
    };
    // Offsets in one guarded arena include an output straddling the two inputs.
    struct Layout { int left, right, out; };
    const Layout layouts[] = {{2,5,9}, {2,5,2}, {2,5,5}, {2,5,1}, {2,5,6},
                              {2,5,4}, {2,5,3}, {2,2,9}, {2,2,2}};
    const unsigned saved = _controlfp(0, 0);
    for (unsigned rounding : {_RC_NEAR, _RC_UP, _RC_DOWN, _RC_CHOP}) {
        _controlfp(_PC_53 | rounding | _MCW_EM, _MCW_PC | _MCW_RC | _MCW_EM);
        for (const Pair& pair : pairs) {
            for (const Layout& layout : layouts) {
                DWORD arena[13], before[13];
                for (auto& word : arena) word = 0xdeadbeef;
                memcpy(arena + layout.left, pair.a, sizeof(pair.a));
                if (layout.left != layout.right) memcpy(arena + layout.right, pair.b, sizeof(pair.b));
                memcpy(before, arena, sizeof(before));
                auto* out = reinterpret_cast<Vec3*>(arena + layout.out);
                _clearfp();
                auto* result = operation(out, reinterpret_cast<const Vec3*>(arena + layout.left),
                                         reinterpret_cast<const Vec3*>(arena + layout.right));
                const unsigned flags = _statusfp() & _MCW_EM;
                require(result == out, "vector operation returns its destination");
                for (int i = 0; i < 13; ++i)
                    if (i < layout.out || i >= layout.out + 3) require(arena[i] == before[i], "vector write extent");
                const DWORD* words = arena + layout.out;
                fprintf(report, "%s case=%d rc=%x a=%d b=%d out=%d result=%08lx %08lx %08lx flags=%x\n",
                        name, int(&pair - pairs), rounding, layout.left, layout.right, layout.out, words[0], words[1], words[2], flags);
            }
        }
    }
    _clearfp();
    _controlfp(saved, _MCW_RC | _MCW_PC | _MCW_EM);
}

static void check() {
    checkOperation("sum", ROUTINE(Vec3_Add, 0x4bddb0));
    checkOperation("difference", ROUTINE(Vec3_Sub, 0x4bde00));
    // The intersection is origin + 2 * direction on the plane z=3.
    float plane[] = {0,0,3}, normal[] = {0,0,1}, origin[] = {4,8,7}, direction[] = {1,-3,-2};
    float point[3], expected[] = {6,2,3};
    require(ROUTINE(Vec3_PlaneHit, 0x4e6490)(point, plane, normal, origin, direction) == point &&
            !memcmp(point, expected, sizeof(point)), "plane intersection uses vector sum");
    fprintf(report, "sum plane hit=(6,2,3)\n");
}
}
