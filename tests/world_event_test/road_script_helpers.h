extern "C" DWORD* __fastcall DequeIterator_Copy(DWORD*, void*, const DWORD*);
extern "C" Vec3* Vec3_RotateBy(Vec3*, const Vec3*, const Matrix*);
extern "C" BYTE* ByteRange_Move(const BYTE*, const BYTE*, BYTE*);
extern "C" bool SchValue_IsSymbol(const BYTE*, const char*);

namespace road_script_helpers {
static Point resolve(RoadPosition p) {
    struct { double before; Point point; double after; } out = {-11, {}, -13};
    require(ROUTINE(Road_PointOf, 0x435bf0)(&out.point, p) == &out.point, "road point return");
    require(out.before == -11 && out.after == -13, "road point write bounds");
    unsigned long long bits[3];
    memcpy(bits, &out.point, sizeof(bits));
    fprintf(report, "road=%d node=%d object=%d t=%.2f flags=%d xyz=%016llx %016llx %016llx\n",
            p.road, p.node, p.object, p.t, p.flags, bits[0], bits[1], bits[2]);
    return out.point;
}

static void roadPoints() {
    Point stored = field<Point>(field<BYTE**>(global(0x68cf00), 0)[72], 48);
    for (int road : {-1, 71}) {
        Point atNode = resolve({road, 72, .5, 0, road == -1 ? 0 : 10, 0, -1});
        require(!memcmp(&stored, &atNode, sizeof(Point)), "node position takes precedence");
    }
    for (int object : {1, 10}) {
        Point previous = {};
        for (double t : {0., .5, 1.}) {
            Point forward = resolve(approach::at(71, object, t, 0));
            Point reversed = resolve(approach::at(71, object, t, 1));
            require(!memcmp(&forward, &reversed, sizeof(Point)), "direction flag and world point");
            if (t != 0.) require(approach::distance(previous, forward) > 0., "distinct points along road");
            previous = forward;
        }
    }
}

static void copyIterator() {
    BYTE pages[2][512];
    BYTE* map[] = {pages[0], pages[1]};
    for (int block : {0, 1}) {
        for (int offset : {0, 511, 512}) {
            DWORD it[] = {DWORD(map[block] + offset), DWORD(map[block]), DWORD(map[block] + 512), DWORD(map + block)};
            DWORD saved[4];
            memcpy(saved, it, sizeof(it));
            DWORD dst[] = {11, 0, 0, 0, 0, 13};
            auto copy = ROUTINE(DequeIterator_Copy, 0x4abea0);
            require(copy(dst + 1, nullptr, it) == dst + 1 && !memcmp(dst + 1, saved, sizeof(saved)) &&
                    dst[0] == 11 && dst[5] == 13 && !memcmp(it, saved, sizeof(saved)), "copy iterator fields");
            require(copy(it, nullptr, it) == it && !memcmp(it, saved, sizeof(saved)), "self copy iterator");
            fprintf(report, "iterator block=%d offset=%d copied and self-copied\n", block, offset);
        }
    }
}

static void rotateDirection() {
    struct BasisCase { Pose pose; float expected[3]; };
    const BasisCase cases[] = {
        {{{1,0,0, 0,1,0, 0,0,1}}, {1,2,3}},
        {{{0,1,0, -1,0,0, 0,0,1}}, {-2,1,3}},
        {{{1,2,3, 4,5,6, 7,8,9}}, {30,36,42}},
    };
    for (const auto& c : cases) {
        for (bool translated : {false, true}) {
            Pose pose = c.pose;
            if (translated) { pose.m[9] = -31; pose.m[10] = 17; pose.m[11] = 99; }
            for (bool inPlace : {false, true}) {
                float source[] = {1,2,3}, separate[3] = {};
                float* dst = inPlace ? source : separate;
                auto* out = reinterpret_cast<Vec3*>(dst);
                require(ROUTINE(Vec3_RotateBy, 0x4cb2b0)(out, reinterpret_cast<Vec3*>(source),
                        reinterpret_cast<Matrix*>(&pose)) == out && !memcmp(dst, c.expected, sizeof(source)),
                        "rotation basis, translation and in-place result");
                fprintf(report, "basis=%d translated=%d inplace=%d vector=%.0f %.0f %.0f\n",
                        int(&c - cases), translated, inPlace, dst[0], dst[1], dst[2]);
            }
        }
    }
}

static void moveByteRanges() {
    struct Range { int first, count, dest; };
    for (const Range& r : {Range{3,0,9}, {2,5,20}, {5,38,5}, {5,38,8}, {8,38,5}}) {
        BYTE bytes[64], initial[64];
        for (int i = 0; i < 64; ++i) initial[i] = bytes[i] = BYTE(7*i);
        require(ROUTINE(ByteRange_Move, 0x4072f0)(bytes + r.first, bytes + r.first + r.count, bytes + r.dest)
                == bytes + r.dest + r.count, "byte range return");
        for (int i = 0; i < 64; ++i) {
            bool written = r.dest <= i && i < r.dest + r.count;
            require(bytes[i] == initial[written ? r.first + i - r.dest : i], "byte range contents and bounds");
        }
        fprintf(report, "byte range first=%d count=%d dest=%d\n", r.first, r.count, r.dest);
    }
}

static void matchSymbols() {
    struct Case { DWORD tag; const char* stored; const char* query; bool expected; };
    const Case cases[] = {{9,"WEST","WEST",true}, {0x10009,"WEST","wEsT",true},
                          {9,"WEST","WE",false}, {9,"WEST","WESTERN",false}, {9,"WEST","EAST",false},
                          {9,"","",true}, {9,"WEST","",false}, {2,nullptr,"WEST",false}};
    for (const auto& c : cases) {
        BYTE object[16] = {};
        field<const char*>(object, 12) = c.stored;
        Value v = {c.stored ? object : nullptr, 0, c.tag};
        bool match = ROUTINE(SchValue_IsSymbol, 0x4b65e0)(reinterpret_cast<BYTE*>(&v), c.query);
        require(match == c.expected, "Scheme symbol type and full case-insensitive name");
        fprintf(report, "symbol tag=%lx stored='%s' query='%s' match=%d\n", c.tag, c.stored ? c.stored : "<null>", c.query, match);
    }
}

static void check() {
    roadPoints();
    copyIterator();
    rotateDirection();
    moveByteRanges();
    matchSymbols();
}
}
