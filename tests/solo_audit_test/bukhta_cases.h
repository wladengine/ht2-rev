extern "C" int __cdecl Traffic_ShouldEnterPhysics(BYTE*);
extern "C" int __cdecl Traffic_ShouldLeavePhysics(BYTE*);
extern "C" void __fastcall Renderer_PushLight(BYTE*, void*, const BYTE*);

namespace bukhta {
static bool SceneLightCapacity() {
    void*& stockDevice = *reinterpret_cast<void**>(0x720908);
    void*& device = F(void*, KingBss, 0x720908 - 0x68a820);
    void* savedStock = stockDevice;
    void* savedDevice = device;
    stockDevice = device = nullptr;
    auto push81 = reinterpret_cast<decltype(&Renderer_PushLight)>(0x5ad180);
    int mismatches = 0;
    for (int count : {0, 18, 19, 20}) {
        alignas(DWORD) BYTE reference[8704] = {}, actual[8704] = {}, source[80] = {};
        F(int, reference, 8240) = count;
        F(float, reference, 5216) = F(float, reference, 5232) = F(float, reference, 5248) = 1;
        F(float, reference, 5252) = 75;
        F(int, source, 0) = 3;
        F(float, source, 4) = 25;
        memcpy(actual, reference, sizeof actual);
        push81(reference, nullptr, source);
        Renderer_PushLight(actual, nullptr, source);
        int expected = count < 20 ? count + 1 : count;
        if (F(int, actual, 8240) != expected || memcmp(reference, actual, sizeof actual) ||
            F(float, actual, 7828) != (count < 20 ? 100.f : 0.f))
            ++mismatches;
    }
    stockDevice = savedStock;
    device = savedDevice;
    printf("scene light admission boundaries=4 mismatches=%d\n", mismatches);
    return !mismatches;
}

static bool TrafficDistances() {
    BYTE game[620] = {}, human[2000] = {}, car[21608] = {}, driver[16576] = {};
    BYTE*& stock = *reinterpret_cast<BYTE**>(0x6d2078);
    BYTE*& canonical = F(BYTE*, KingBss, 0x6d2078 - 0x68a820);
    BYTE* previousStock = stock;
    BYTE* previousCanonical = canonical;
    stock = canonical = game;
    F(BYTE*, game, 616) = human;
    F(BYTE*, car, 21600) = driver;
    auto near81 = reinterpret_cast<decltype(&Traffic_ShouldEnterPhysics)>(0x4cedd0);
    auto far81 = reinterpret_cast<decltype(&Traffic_ShouldLeavePhysics)>(0x4cee70);
    int cases = 0, mismatches = 0;
    for (float distance : {79.99f, 80.f, 99.99f, 100.f, 300.f, 300.01f})
        for (double lateral : {-0.51, -0.5, 0.0, 0.5, 0.51})
            for (int flag : {0, 2}) {
                F(float, car, 560) = distance;  // separate bodies vertically
                F(double, driver, 16568) = lateral;
                F(BYTE, driver, 16496) = (BYTE)flag;
                int nearResult = near81(car), farResult = far81(car);
                ++cases;
                if (nearResult != Traffic_ShouldEnterPhysics(car) || farResult != Traffic_ShouldLeavePhysics(car) ||
                    nearResult != (distance < 80) ||
                    farResult != (distance > 300 || (distance >= 100 && fabs(lateral) <= .5 && !flag)))
                    ++mismatches;
            }
    stock = previousStock;
    canonical = previousCanonical;
    printf("traffic distance boundaries=%d mismatches=%d\n", cases, mismatches);
    return !mismatches;
}

// A device ID is assigned from the submission count. Record the IDs and the
// owners after sorting, releasing and inserting into the vacated array entry.
struct LightTrace {
    int ids[3][3], owners[3][3], counts[3], active[3];
};
static LightTrace Trace(bool stock, BYTE* view) {
    LightTrace trace = {};
    alignas(DWORD) BYTE records[4][120] = {}, sources[4][80] = {};
    unsigned manager[] = {2, 0, 0, (unsigned)records};
    DWORD point[26] = {1};
    auto add = stock ? reinterpret_cast<decltype(&Lights_Add)>(0x4cd470) : Lights_Add;
    auto pop = stock ? reinterpret_cast<decltype(&Lights_Release)>(0x4cd3c0) : Lights_Release;
    for (int i = 0; i < 4; ++i) F(int, sources[i], 72) = 141 + i;
    for (int i = 0; i < 3; ++i) add(manager, nullptr, point, (int)view, (int)sources[i]);
    for (int step = 0; step < 3; ++step) {
        if (step == 1) pop((BYTE*)manager, nullptr, (int)view);
        if (step == 2) add(manager, nullptr, point, (int)view, (int)sources[3]);
        trace.counts[step] = manager[1];
        trace.active[step] = manager[2];
        for (unsigned i = 0; i < manager[1]; ++i) {
            trace.ids[step][i] = F(int, records[i], 116);
            trace.owners[step][i] = F(int, F(BYTE*, records[i], 104), 72);
        }
    }
    return trace;
}
static bool LightOrder() {
    light_lifetime::Fixture f;
    BYTE*& view = *reinterpret_cast<BYTE**>(0x696224);
    void*& device = *reinterpret_cast<void**>(0x696260);
    BYTE* oldView = view;
    void* oldDevice = device;
    view = f.view;
    device = &f.device;
    LightTrace reference = Trace(true, f.view);
    LightTrace actual = Trace(false, f.view);
    int priorityMismatches = 0;
    auto choose81 = reinterpret_cast<decltype(&Lights_Choose)>(0x4ccf30);
    for (int kind : {1, 2, 3}) for (float offset : {0.f, 50.f, 125.f}) {
        alignas(DWORD) BYTE source[80] = {}, record[120] = {};
        F(int, source, 72) = 140;
        F(int, record, 0) = kind;
        F(float, record, 52) = offset;
        F(BYTE*, record, 104) = source;
        DWORD manager[] = {0, 1, 0, (DWORD)record};
        choose81(manager, nullptr, f.view);
        DWORD referenceScore = F(DWORD, record, 108);
        F(DWORD, record, 108) = 0;
        Lights_Choose(manager, nullptr, f.view);
        if (F(DWORD, record, 108) != referenceScore) ++priorityMismatches;
    }
    view = oldView;
    device = oldDevice;
    const LightTrace expected = {{{2,1,0},{2,1,0},{2,2,1}},
                                 {{143,142,141},{143,142,0},{144,143,142}},
                                 {3,2,3},{2,2,2}};
    bool match = !memcmp(&actual,&reference,sizeof actual) && !memcmp(&expected,&actual,sizeof actual);
    printf("light priority order, release and slot reuse match=%d\n", match);
    printf("light distance priorities=9 mismatches=%d\n", priorityMismatches);
    return match && !priorityMismatches;
}
}
