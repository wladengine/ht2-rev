extern "C" BYTE* __fastcall Lights_Construct(BYTE*, void*, unsigned, void*);
extern "C" void __fastcall Lights_Destruct(BYTE*, void*);
extern "C" int __fastcall Lights_Add(unsigned*, void*, const void*, int, int);
extern "C" int __fastcall Lights_Choose(DWORD*, void*, BYTE*);
extern "C" int __fastcall Lights_Release(BYTE*, void*, int);
extern "C" void __fastcall Effect4d9e70_Destroy(BYTE*, void*);

namespace light_lifetime {
struct Lamp {
    alignas(DWORD) BYTE node[144] = {};
    BYTE* source() { return node + 56; }
};

static int __stdcall SetLight(void*, int, void*) { return 0; }
static int __stdcall EnableLight(void*, int, int) { return 0; }

struct Fixture {
    DWORD manager[4] = {};
    alignas(double) BYTE view[8480] = {};
    alignas(DWORD) BYTE camera[84] = {}, cameraNode[112] = {};
    void* methods[45] = {};
    void** device = methods;
    BYTE*& activeManager = F(BYTE*, KingBss, 0x6ce520 - 0x68a820);
    BYTE*& activeView = F(BYTE*, KingBss, 0x696224 - 0x68a820);
    void*& activeDevice = F(void*, KingBss, 0x696260 - 0x68a820);
    int& lightsOn = F(int, KingBss, 0x69625c - 0x68a820);
    BYTE* oldManager = activeManager;
    BYTE* oldView = activeView;
    void* oldDevice = activeDevice;
    int oldLightsOn = lightsOn;

    Fixture() {
        methods[72 / 4] = (void*)SetLight;
        methods[176 / 4] = (void*)EnableLight;
        activeView = view;
        F(BYTE*, view, 488) = camera;
        F(BYTE*, camera, 80) = cameraNode;
        F(double, view, 8472) = 100.;
        F(int, view, 2992) = 100;
        F(double, view, 2960) = 0.01;
        activeManager = Lights_Construct((BYTE*)manager, nullptr, 8, &device);
    }
    ~Fixture() {
        Lights_Destruct((BYTE*)manager, nullptr);
        activeManager = oldManager;
        activeView = oldView;
        activeDevice = oldDevice;
        lightsOn = oldLightsOn;
    }
    bool add(Lamp& lamp, int id) {
        DWORD record[26] = {};
        record[0] = 1;
        record[25] = id; // opaque to selection, travels with the sorted record
        return Lights_Add((unsigned*)manager, nullptr, record, (int)view, (int)lamp.source()) == 0;
    }
    BYTE* sourceFor(int id) {
        for (DWORD i = 0; i < manager[1]; ++i) {
            BYTE* record = (BYTE*)manager[3] + i * 120;
            if (F(int, record, 100) == id)
                return F(BYTE*, record, 104);
        }
        return nullptr;
    }
    bool drain() {
        while (manager[1])
            if (Lights_Release((BYTE*)manager, nullptr, (int)view) != 0)
                return false;
        return manager[2] == 0;
    }
};

static bool SharedSource() {
    Fixture f;
    Lamp fire, other, live;
    if (!f.add(fire, 1) || !f.add(other, 2) || !f.add(fire, 3) || !f.add(live, 4))
        return false;
    Effect4d9e70_Destroy(fire.node, nullptr);
    BYTE* retired = f.sourceFor(1);
    if (!retired || retired == fire.source() || retired != f.sourceFor(3) ||
        memcmp(retired, fire.source(), 80) || f.sourceFor(4) != live.source())
        return false;

    // Reuse the caller-owned storage as a new node. The old submission must
    // keep its own state through sorting and retirement of the second node.
    F(int, fire.source(), 72) = 110;
    if (!f.add(fire, 5) || f.sourceFor(5) != fire.source())
        return false;
    Effect4d9e70_Destroy(other.node, nullptr);
    BYTE* second = f.sourceFor(2);
    if (!second || second == other.source() || second == retired)
        return false;
    F(int, f.view, 2992) = 101;
    if (Lights_Choose(f.manager, nullptr, f.view) != 0 ||
        F(int, retired, 68) != 101 || F(int, second, 68) != 101 ||
        F(int, other.source(), 68) != 100 || F(int, retired, 72) != 0 ||
        F(int, fire.source(), 72) != 110 || F(int, live.source(), 68) != 101)
        return false;
    return f.drain();
}

static bool FullTable() {
    Fixture f;
    Lamp lamps[255];
    for (int i = 0; i < 255; ++i) {
        F(int, lamps[i].source(), 72) = 255 - i;
        if (!f.add(lamps[i], i))
            return false;
    }
    for (int i = 0; i < 255; ++i)
        Effect4d9e70_Destroy(lamps[i].node, nullptr);
    for (int i = 0; i < 255; ++i) {
        BYTE* snapshot = f.sourceFor(i);
        if (!snapshot || snapshot == lamps[i].source() || F(int, snapshot, 72) != 255 - i)
            return false;
    }
    if (!f.drain() || !f.add(lamps[0], 255))
        return false;
    Effect4d9e70_Destroy(lamps[0].node, nullptr);
    return f.sourceFor(255) != lamps[0].source() && f.drain();
}

// Both versions receive the same live light and mock device. Compare the
// whole submitted record and the fade state written back to the scene node.
static bool LiveLightParity() {
    Fixture f;
    F(double, f.view, 2960) = 1.0 / 64.0;
    BYTE*& originalView = *(BYTE**)0x696224;
    void*& originalDevice = *(void**)0x696260;
    int& originalOn = *(int*)0x69625c;
    BYTE* previousView = originalView;
    void* previousDevice = originalDevice;
    int previousOn = originalOn;
    originalView = f.view;
    originalDevice = &f.device;
    const auto originalAdd = reinterpret_cast<decltype(&Lights_Add)>(0x4cd470);
    int mismatches = 0;
    for (int state = 0; state <= 5; ++state) {
        for (int frame : {98, 99, 100}) {
            alignas(DWORD) BYTE source[80] = {};
            F(float, source, 64) = 0.5f;
            F(int, source, 68) = frame;
            F(int, source, 72) = 70;
            F(int, source, 76) = state;
            BYTE initial[80], expected[80];
            memcpy(initial, source, sizeof source);
            float submitted[26] = {};
            F(int, submitted, 0) = 1;
            submitted[1] = 0.75f;
            alignas(DWORD) BYTE refRecord[120] = {}, actualRecord[120] = {};
            unsigned ref[] = {8, 0, 0, (unsigned)refRecord};
            unsigned actual[] = {8, 0, 0, (unsigned)actualRecord};
            originalOn = f.lightsOn = 0;
            int refResult = originalAdd(ref, nullptr, submitted, (int)f.view, (int)source);
            memcpy(expected, source, sizeof source);
            memcpy(source, initial, sizeof source);
            int result = Lights_Add(actual, nullptr, submitted, (int)f.view, (int)source);
            if (result != refResult || actual[1] != ref[1] || actual[2] != ref[2] ||
                f.lightsOn != originalOn || memcmp(source, expected, sizeof source) ||
                memcmp(actualRecord, refRecord, sizeof refRecord))
                ++mismatches;
        }
    }
    originalOn = previousOn;
    originalDevice = previousDevice;
    originalView = previousView;
    printf("live lights original comparisons=18 mismatches=%d\n", mismatches);
    return mismatches == 0;
}

static bool Check() {
    const bool parity = LiveLightParity();
    const bool shared = SharedSource();
    const bool full = FullTable();
    printf("light retirement shared=%d full=%d\n", shared, full);
    return parity && shared && full;
}
}
