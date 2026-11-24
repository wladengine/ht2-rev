#include <windows.h>
#include <cstdio>
#include <cstring>
#include <float.h>
#include <initializer_list>
#include <utility>

struct Value {
    BYTE* object;
    int aux;
    DWORD tag;
};
static_assert(sizeof(Value) == 12, "Steam 8.1 value ABI");
using Event = int(__cdecl*)(BYTE*, int, int);
using Present = void(__cdecl*)(int, int);
extern "C" {
int VehCache_Load();
void Vehicles_AverageRatio();
int VehTech_Load();
int Ai_Initialize(const char*, Event, Present);
void Ai_StartGame(const char*, DWORD, const BYTE*);
void Ai_Tick();
int Ini_GetDouble(const char*, const char*, double, double*, const char*);
int Ini_GetFloat(const char*, const char*, double, float*, const char*);
BOOL Ini_PutDouble(const char*, const char*, double, const char*);
void Ai_PickPresentLater(int, int);
void Player_Retire(BYTE*);
void Player_MarkListed(BYTE*);
int Ai_FinishPlayer(int);
int Ai_ActOnArrival(BYTE*);
void Ai_MarkAmbushes();
void Save_Ambush(BYTE**, BYTE*);
void Load_Ambush(BYTE**);
Value* Port_MakeB(Value*, int, const char**, const int*);
Value* Port_ReadDatum(Value*, BYTE*, Value);
Value* Sch_EvalTop(Value*, BYTE*, Value);
BYTE* Ai_ReadAmbush(BYTE*, Value);
extern BYTE KingBss[], KingData[];
}

#ifdef STOCK_WORLD
#define ROUTINE(name, addr) reinterpret_cast<decltype(&name)>(addr)
static BYTE* global(DWORD va) { return reinterpret_cast<BYTE*>(va); }
#else
#define ROUTINE(name, addr) name
static BYTE* global(DWORD va) {
    return va >= 0x68a820 ? KingBss + (va - 0x68a820) : KingData + (va - 0x66c000);
}
#endif
template <class T> static T& field(BYTE* base, int off) { return *reinterpret_cast<T*>(base + off); }
static FILE* report;
static void require(bool condition, const char* check) {
    if (!condition) {
        fprintf(report, "FAIL %s\n", check);
        ExitProcess(3);
    }
}
static unsigned eventCounts[256];
static int event(BYTE*, int kind, int) {
    if (static_cast<unsigned>(kind) < 256)
        ++eventCounts[kind];
    switch (kind) {
    case 1:
    case 4:
    case 5:
    case 14:
    case 15:
    case 17:
        return 0;
    default:
        return -1;
    }
}
static void present(int, int) {}
static BYTE windowState[4096], view[18000];

static void checkRingFinish(BYTE* human) {
    // Supply the roster. Grid selection and placement have separate paired tests.
    BYTE* head = field<BYTE*>(global(0x68b990), 0);
    int selected = 0;
    for (BYTE* p = field<BYTE*>(head, 0); p != head && selected < 4; p = field<BYTE*>(p, 0)) {
        if (field<int>(p, 92) == 2) {
            ROUTINE(Player_Retire, 0x417770)(p);
            ROUTINE(Player_MarkListed, 0x4175c0)(p);
            ++selected;
        }
    }
    field<int>(human, 100) = 5;
    field<DWORD>(human, 96) |= 0x4000;
    int& phase = field<int>(global(0x66c6e0), 0);
    phase = 1;
    BYTE** grid = field<BYTE**>(global(0x68cf60), 0);
    const int count = int(field<BYTE**>(global(0x68cf64), 0) - grid);
    require(count == 4, "campaign grid");
    BYTE* racers[4];
    memcpy(racers, grid, count * sizeof(*grid));
    fprintf(report, "prepared four marked opponents\n");
    ROUTINE(Ai_FinishPlayer, 0x426b00)((int)human);
    require(field<int>(human, 100) != 5 && phase == 1, "human finished with opponents remaining");
    for (int i = 0; i < count; ++i) {
        BYTE* racer = racers[i];
        require(field<int>(racer, 100) == 5 && (field<DWORD>(racer, 96) & 0x4000) && !racer[496],
                "still racing after human finish");
    }
    fprintf(report, "human finish leaves %d drivers racing\n", count);
    for (int i = 0; i < count; ++i) {
        BYTE* racer = racers[i];
        // Advance to the last checkpoint. Its normal arrival handler owns cleanup.
        field<int>(racer, 724) = 10;
        ROUTINE(Ai_ActOnArrival, 0x419cb0)(racer);
        const int left = int(field<BYTE**>(global(0x68cf64), 0) - field<BYTE**>(global(0x68cf60), 0));
        require(left == count - i - 1 && field<int>(racer, 100) == 0, "driver leaves own race");
        require(phase == (left ? 1 : 3), "cooldown begins after the last finisher");
        fprintf(report, "AI finish %d remaining=%d job=0 phase=%d\n", i + 1, left, phase);
    }
}

static void checkRecord(BYTE* site) {
    require(site && !strcmp(field<char*>(site, 0), "AMBUSH_AD_3"), "ambush name");
    require(field<int>(site, 36) == 1 && field<int>(site, 80) == 4, "mafia / west");
    const double west[] = {-1., 0., 0.};
    require(!memcmp(site + 88, west, sizeof(west)), "AMBUSH_AD_3 west direction vector");
    require(field<int>(site, 52) >= 0, "resolved road node");
    int* ids = field<int*>(site, 112);
    require(field<int*>(site, 116) - ids == 5, "number list length");
    for (int i = 0; i < 5; ++i)
        require(ids[i] == 1325 + i, "number list element");
    BYTE* linked = field<BYTE*>(site, 208);
    require(field<BYTE*>(site, 212) - linked == 12 && !strcmp(field<char*>(linked, 0), "AMBUSH_AD_4"),
            "linked site");
    fprintf(report, "record %s kind=%d node=%d direction=%d numbers=%d..%d linked=%s\n",
            field<char*>(site, 0), field<int>(site, 36), field<int>(site, 52), field<int>(site, 80),
            ids[0], ids[4], field<char*>(linked, 0));
}

static void checkSavedFlags(BYTE* site) {
    BYTE storage[128] = {};
    BYTE* stream[] = {storage, storage, storage + sizeof storage};
    ROUTINE(Save_Ambush, 0x482320)(stream, site);
    const int size = static_cast<int>(stream[1] - storage);
    require(size >= 20 && size <= sizeof storage && !memcmp(storage, "AMBU", 4), "AMBU block");
    const int nameSize = field<int>(storage, 8);
    const int flags = 12 + nameSize;
    require(nameSize == sizeof("AMBUSH_AD_3") && !memcmp(storage + 12, "AMBUSH_AD_3", nameSize),
            "AMBU name");
    require(flags + 6 <= size && storage[flags] == site[240] && storage[flags + 1] == site[28],
            "saved flags");
    site[240] ^= 1;
    site[28] ^= 1;
    BYTE* cursor = storage;
    ROUTINE(Load_Ambush, 0x4824a0)(&cursor);
    require(cursor == stream[1] && site[240] == storage[flags] && site[28] == storage[flags + 1],
            "restored flags");
    fprintf(report, "AMBU ");
    for (int i = 0; i < size; ++i)
        fprintf(report, "%02x", storage[i]);
    fputc('\n', report);
}

static void checkMark(BYTE* site, BYTE* human) {
    // One site per roll. Another world's site must not consume our first draw.
    BYTE** slot = &site;
    auto& begin = field<BYTE**>(global(0x68a970), 0);
    auto& end = field<BYTE**>(global(0x68a974), 0);
    BYTE **oldBegin = begin, **oldEnd = end;
    begin = slot;
    end = slot + 1;
    struct Case {
        double share;
        int noticed;
        double debt;
        unsigned draw;
        int armed, used;
    };
    const Case cases[] = {{.21, 1, 0, 299999, 1, 1},
                          {.21, 1, 0, 300000, 0, 1},
                          {.20, 1, 0, 0, 0, 0},
                          {.21, 0, 0, 0, 0, 0},
                          {.21, 0, 1, 299999, 1, 1}};
    for (const Case& c : cases) {
        field<double>(human, 224) = c.debt;
        field<double>(human, 768) = c.share;
        human[201] = static_cast<BYTE>(c.noticed);
        DWORD* rng = reinterpret_cast<DWORD*>(global(0x68b9c8));
        memset(rng, 0, 57 * sizeof(DWORD));
        rng[1] = c.draw;
        rng[56] = 17;
        site[240] = static_cast<BYTE>(!c.armed);
        BYTE active = site[28];
        ROUTINE(Ai_MarkAmbushes, 0x42c0b0)();
        fprintf(report, "mark share=%.2f noticed=%d debt=%.0f draw=%u armed=%u active=%u consumed=%u\n",
                c.share, c.noticed, c.debt, c.draw, site[240], site[28], rng[55]);
        require(site[240] == c.armed && site[28] == active && rng[55] == c.used &&
                    rng[56] == 17 + c.used,
                "armed state and RNG");
        checkSavedFlags(site);
    }
    begin = oldBegin;
    end = oldEnd;
}

#include "approach.h"
#include "fuel_freight.h"
#include "fleet_review.h"
#include "driver_return.h"
#include "allocator_lock.h"
#include "vector_copy_failure.h"
#include "vector_scale.h"
#include "vector_binary.h"
#include "vector_sub_callers.h"
#include "dvec3_set.h"
#include "road_script_helpers.h"
#include "token_math_time.h"
#include "controls_and_ai.h"
#include "call100_campaign.h"
#include "campaign_clock.h"

extern "C" DWORD* Ai_GetPositionId(DWORD* out, BYTE* player);
extern "C" BYTE* Ai_GetOrientationSmoothed(BYTE* out, BYTE* player, RoadPosition position);
extern "C" int Mixer_Open(int mode, HWND window);

static unsigned failedDeviceCreations;

static HRESULT WINAPI failDeviceCreation(void*, void** device, void*) {
    ++failedDeviceCreations;
    *device = nullptr;
    return E_FAIL;
}

static void checkFailedMixerOpen() {
    HMODULE library = nullptr;
    require(GetModuleHandleExW(0, L"kernel32.dll", &library) != 0, "fixture module reference");
    field<HMODULE>(global(0x72419c), 0) = library;
    field<decltype(&failDeviceCreation)>(global(0x7241a0), 0) = failDeviceCreation;
    field<void*>(global(0x724190), 0) = nullptr;
    field<void*>(global(0x724194), 0) = nullptr;
    field<int>(global(0x684400), 0) = 0;
    field<int>(global(0x684404), 0) = -1;
    int result = ROUTINE(Mixer_Open, 0x604790)(0, nullptr);
    require(result == 4 && failedDeviceCreations == 1, "device creation error code");
    require(field<void*>(global(0x72419c), 0) == nullptr &&
            field<void*>(global(0x7241a0), 0) == nullptr, "mixer released its module and factory");
    require(field<int>(global(0x684404), 0) == result, "cached mixer error");
    result = ROUTINE(Mixer_Open, 0x604790)(0, nullptr);
    require(result == 4 && failedDeviceCreations == 1, "repeat open returns the cached error");
    fprintf(report, "factory calls=%u returned=%d\n", failedDeviceCreations, result);
}

static void checkSmoothedRoadHeadings() {
    // Sample actual driver routes, including the approach to bends and slopes.
    for (unsigned tick = 0; tick <= 4800; ++tick) {
        field<double>(view, 2968) = tick / 40.0;
        ROUTINE(Ai_Tick, 0x42d6e0)();
        if (tick % 1200) continue;
        BYTE* head = field<BYTE*>(global(0x68b990), 0);
        unsigned index = 0, observed = 0;
        for (BYTE* driver = field<BYTE*>(head, 0); driver != head;
             driver = field<BYTE*>(driver, 0), ++index) {
            RoadPosition position = {};
            ROUTINE(Ai_GetPositionId, 0x437520)((DWORD*)&position, driver);
            if (position.road < 0 && position.node < 0) continue;
            struct {
                unsigned __int64 before;
                double matrix[12];
                unsigned __int64 after;
            } pose = {};
            const unsigned __int64 guard = 0xa50120be77ff39cdull;
            pose.before = pose.after = guard;
            BYTE* output = (BYTE*)pose.matrix;
            require(ROUTINE(Ai_GetOrientationSmoothed, 0x439ce0)(output, driver, position) == output,
                    "smoothed orientation output reference");
            require(pose.before == guard && pose.after == guard, "smoothed orientation bounds");
            fprintf(report, "heading %u driver=%u road=%d node=%d", tick, index, position.road, position.node);
            for (double value : pose.matrix) {
                unsigned __int64 bits;
                memcpy(&bits, &value, sizeof(bits));
                fprintf(report, " %016I64x", bits);
            }
            fputc('\n', report);
            ++observed;
        }
        require(observed > 0, "drivers with road positions");
    }
}

static void checkIniNumbers() {
    char file[MAX_PATH];
    const DWORD length = GetFullPathNameA("audit-numbers.ini", sizeof(file), file, nullptr);
    require(length > 0 && length < sizeof(file), "fixture INI path");
    for (auto sample : { std::pair<const char*, double>("1D3", 1000.0),
                         {"-1d-2", -0.01}, {"0x20", 0.0}, {"inf", 0.0}, {"N", 17.0} }) {
        require(WritePrivateProfileStringA("audit", "value", sample.first, file) != 0, "fixture INI value");
        double d = -1;
        float f = -1;
        require(ROUTINE(Ini_GetDouble, 0x603cb0)("audit", "value", 17.0, &d, file) == 0 &&
                d == sample.second, "read legacy INI double");
        require(ROUTINE(Ini_GetFloat, 0x603d30)("audit", "value", 17.0, &f, file) == 0 &&
                f == static_cast<float>(sample.second), "read legacy INI float");
        fprintf(report, "read %s %.17g %.9g\n", sample.first, d, f);
    }
    for (auto sample : { std::pair<double, const char*>(-0.0, "0"),
                         {99999999.0, "1e+008"}, {1.25e-7, "1.25e-007"}, {1.234575, "1.23458"} }) {
        require(ROUTINE(Ini_PutDouble, 0x603db0)("audit", "value", sample.first, file) != 0,
                "write legacy INI double");
        char text[96] = {};
        GetPrivateProfileStringA("audit", "value", "", text, sizeof(text), file);
        require(strcmp(text, sample.second) == 0, "serialized INI value");
        fprintf(report, "written %s\n", text);
    }
}

static void checkCampaignTaskPeriods() {
    const struct { const char* name; double seconds; } expected[] = {
        {"updateDealers", 27.5}, {"provideRacesRegular", 30.0},
        {"provideCompetitionRegular", 30.0}, {"updateCompanies", 30.0},
        {"recoverServices", 27.1}
    };
    BYTE* head = field<BYTE*>(global(0x68b99c), 0);
    for (const auto& period : expected) {
        int found = 0;
        for (BYTE* node = field<BYTE*>(head, 0); node != head; node = field<BYTE*>(node, 0)) {
            BYTE* task = field<BYTE*>(node, 8);
            if (!strcmp(field<const char*>(task, 32), period.name)) {
                double seconds = field<double>(task, 24);
                fprintf(report, "campaign task %s period=%.17g\n", period.name, seconds);
                require(seconds == period.seconds, period.name);
                ++found;
            }
        }
        require(found == 1, "unique scheduled task");
    }
}

#include "coordinate_message.h"

extern "C" void __cdecl WorldEvent() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    report = fopen("event-result.txt", "w");
    if (!report)
        ExitProcess(2);
    setvbuf(report, nullptr, _IONBF, 0);
    const char* command = GetCommandLineA();
    if (strstr(command, " --coordinate-bounds-pass") || strstr(command, " --message-return-pass") ||
        strstr(command, " --elapsed-time-pass") || strstr(command, " --scalar-comparisons-pass") ||
        strstr(command, " --diagnostic-numbers-pass") || strstr(command, " --vehicle-state-pass") ||
        strstr(command, " --geometry-pass") || strstr(command, " --weather-terrain-pass") ||
        strstr(command, " --player-copy-pass") || strstr(command, " --engine-torque-pass") ||
        strstr(command, " --triangle-arithmetic-pass") || strstr(command, " --body-frame-pass") ||
        strstr(command, " --numeric-exceptions-pass")) {
        if (strstr(command, " --numeric-exceptions-pass"))
            coordinate_message::checkNumericalExceptions();
        else if (strstr(command, " --body-frame-pass"))
            coordinate_message::checkBodyFrameThrottle();
        else if (strstr(command, " --triangle-arithmetic-pass"))
            coordinate_message::checkTriangleArithmetic();
        else if (strstr(command, " --engine-torque-pass"))
            coordinate_message::checkEngineTorque();
        else if (strstr(command, " --player-copy-pass"))
            coordinate_message::checkPlayerCopy();
        else if (strstr(command, " --coordinate-bounds-pass"))
            coordinate_message::checkCoordinates();
        else if (strstr(command, " --elapsed-time-pass"))
            coordinate_message::checkTimeDelta();
        else if (strstr(command, " --scalar-comparisons-pass"))
            coordinate_message::checkScalarComparisons();
        else if (strstr(command, " --diagnostic-numbers-pass"))
            coordinate_message::checkDiagnosticNumbers();
        else if (strstr(command, " --geometry-pass"))
            coordinate_message::checkGeometry();
        else if (strstr(command, " --weather-terrain-pass"))
            coordinate_message::checkWeatherAndTerrain();
        else if (strstr(command, " --vehicle-state-pass"))
            coordinate_message::checkVehicleState();
        else
            coordinate_message::checkMessage();
        fprintf(report, "PASS\n");
        fclose(report);
        ExitProcess(0);
    }
    if (strstr(command, " --vector-copy-failure-pass") || strstr(command, " --fill-failure-pass") ||
        strstr(command, " --provider-copy-failure-pass") || strstr(command, " --temp-buffer-failure-pass")) {
        if (strstr(command, " --temp-buffer-failure-pass"))
            vector_copy_failure::checkTemporaryBuffer();
        else if (strstr(command, " --vector-copy-failure-pass"))
            vector_copy_failure::check();
        else
            vector_copy_failure::checkLargeCopies(strstr(command, " --provider-copy-failure-pass") != nullptr);
        fprintf(report, "PASS\n");
        fclose(report);
        ExitProcess(0);
    }
    bool vectorPass = strstr(GetCommandLineA(), " --vector-pass") != nullptr;
    if (vectorPass || strstr(GetCommandLineA(), " --allocator-pass")) {
        if (vectorPass) {
            vector_scale::check();
            vector_binary::check();
            vector_sub_callers::check();
            dvec3_set::check();
        } else allocator_lock::check();
        fprintf(report, "PASS\n");
        fclose(report);
        ExitProcess(0);
    }
    require(ROUTINE(VehCache_Load, 0x508000)() && ROUTINE(VehTech_Load, 0x50a5a0)(), "vehicle inputs");
    ROUTINE(Ai_Initialize, 0x435730)("ENV", event, present);
    field<BYTE*>(global(0x6cec6c), 0) = windowState;
    field<BYTE*>(windowState, 1356) = view;
    field<BYTE*>(global(0x696ca0), 0) = view;
    BYTE settings[32] = {};
    field<double>(settings, 0) = .5;
    field<const char*>(settings, 12) = "Audit";
    field<int>(settings, 16) = 1;
    if (strstr(GetCommandLineA(), " --respawn-pass")) {
        driver_return::check(settings);
        fprintf(report, "PASS\n");
        fclose(report);
        ExitProcess(0);
    }
    ROUTINE(Ai_StartGame, 0x434780)(nullptr, 0, settings);
    if (strstr(GetCommandLineA(), " --campaign-state-pass")) {
        unsigned short startup, average;
        __asm fnstsw startup
        ROUTINE(Vehicles_AverageRatio, 0x430480)();
        __asm fnstsw average
        const unsigned long long ratio = field<unsigned long long>(global(0x68d180), 0);
        require((startup & 0x4500) == 0x0100, "new campaign leaves the player's threshold relation");
        require((average & 0x4500) == 0x0100 && ratio == 0x406ed7cbaf88985aull,
                "vehicle definition ratio and return condition codes");
        fprintf(report, "campaign relation=%04x average=%016llx relation=%04x\nPASS\n",
                startup & 0x4500, ratio, average & 0x4500);
        fclose(report);
        ExitProcess(0);
    }
    checkCampaignTaskPeriods();
    require(field<double>(global(0x68d100), 0) == -2000.0, "initial last human load time");
    if (strstr(GetCommandLineA(), " --road-heading-pass")) {
        checkSmoothedRoadHeadings();
        campaign_clock::checkParkingDistance();
        fprintf(report, "PASS\n");
        fclose(report);
        ExitProcess(0);
    }
    if (strstr(GetCommandLineA(), " --mixer-failure-pass")) {
        checkFailedMixerOpen();
        fprintf(report, "PASS\n");
        fclose(report);
        ExitProcess(0);
    }
    if (strstr(GetCommandLineA(), " --ini-numbers-pass")) {
        checkIniNumbers();
        fprintf(report, "PASS\n");
        fclose(report);
        ExitProcess(0);
    }
    if (strstr(GetCommandLineA(), " --print-list-pass")) {
        BYTE* interpreter = global(0x68bb58);
        const char* input = "print-list.sch";
        int mode = 1;
        Value port = {}, expression = {}, result = {};
        ROUTINE(Port_MakeB, 0x455f00)(&port, 0, &input, &mode);
        ROUTINE(Port_ReadDatum, 0x4b4c20)(&expression, interpreter, port);
        require(expression.tag == 2, "list expression parsed");
        ROUTINE(Sch_EvalTop, 0x4a6af0)(&result, interpreter, expression);
        require(field<int>(interpreter, 120) == 0, "list expression evaluated");
        FILE* printed = fopen("printed-list.txt", "rb");
        require(printed != nullptr, "list output file");
        const char expected[] = "\r\n( 1  2  3 )";
        char bytes[sizeof(expected)];
        size_t size = fread(bytes, 1, sizeof(bytes), printed);
        fclose(printed);
        require(size == sizeof(expected) - 1 && !memcmp(bytes, expected, size), "list output bytes");
        fprintf(report, "printed list bytes=%u\nPASS\n", unsigned(size));
        fclose(report);
        ExitProcess(0);
    }
    if (strstr(GetCommandLineA(), " --clock-")) {
        campaign_clock::run(strstr(GetCommandLineA(), " --clock-b-pass") ? 9187u : 517u);
        fprintf(report, "PASS\n");
        fclose(report);
        ExitProcess(0);
    }
    if (strstr(GetCommandLineA(), " --call100-pass")) {
        call100_campaign::check();
        fprintf(report, "PASS\n");
        fclose(report);
        ExitProcess(0);
    }
    if (strstr(GetCommandLineA(), " --controls-ai-pass")) {
        controls_and_ai::check();
        fprintf(report, "PASS\n");
        fclose(report);
        ExitProcess(0);
    }
    if (strstr(GetCommandLineA(), " --token-math-pass")) {
        token_math_time::check();
        fprintf(report, "PASS\n");
        fclose(report);
        ExitProcess(0);
    }
    if (strstr(GetCommandLineA(), " --road-script-pass")) {
        road_script_helpers::check();
        fprintf(report, "PASS\n");
        fclose(report);
        ExitProcess(0);
    }
    BYTE* human = nullptr;
    BYTE* head = field<BYTE*>(global(0x68b990), 0);
    for (BYTE* p = field<BYTE*>(head, 0); p != head; p = field<BYTE*>(p, 0))
        if (field<int>(p, 92) == 1)
            human = p;
    require(human != nullptr, "human player");
    const char* fleetPass = strstr(GetCommandLineA(), " --fleet-");
    if (fleetPass) {
        if (strstr(fleetPass, "-hire-pass"))
            fleet_review::hiring(human);
        else if (strstr(fleetPass, "-deadline-pass"))
            fleet_review::checkOrderDeadline(human);
        else if (strstr(fleetPass, "-vehicle-settings-pass"))
            fleet_review::checkVehicleSettingsRounding();
        else if (strstr(fleetPass, "-salary-pass"))
            fleet_review::salary(human);
        else
            fleet_review::cargo(human, strstr(fleetPass, "-main") != nullptr, strstr(fleetPass, "-merge") != nullptr,
                                strstr(fleetPass, "-mixed") != nullptr, strstr(fleetPass, "-depots") != nullptr);
        fprintf(report, "PASS\n");
        fclose(report);
        ExitProcess(0);
    }
    const bool roadsideFuel = strstr(GetCommandLineA(), " --roadside-pass") != nullptr;
    const bool tankerInteractions = strstr(GetCommandLineA(), " --supercargo-pass") != nullptr;
    const bool boxControl = strstr(GetCommandLineA(), " --box-pass") != nullptr;
    if (roadsideFuel || tankerInteractions || boxControl || strstr(GetCommandLineA(), " --fuel-pass")) {
        if (boxControl) tanker_interactions::container(human, false);
        else fuel_freight::check(human, roadsideFuel, tankerInteractions);
        fprintf(report, "PASS\n");
        fclose(report);
        ExitProcess(0);
    }
    if (strstr(GetCommandLineA(), " --finish-pass")) {
        checkRingFinish(human);
        fprintf(report, "PASS\n");
        fclose(report);
        ExitProcess(0);
    }
    if (strstr(GetCommandLineA(), " --pickup-pass")) {
        // Queue the same kind of pickup as the tractor interaction. The task
        // owns neither the player nor the present. End this host before dispatch.
        BYTE node[12] = {}, item[112] = {};
        field<BYTE*>(node, 8) = item;
        field<DWORD>(item, 4) = 0x12345678;
        require(field<int>(human, 100) != 5, "outside a Ring race");
        for (int flags = 0x10; flags <= 0x20; flags += 0x10) {
            field<int>(item, 8) = flags;
            field<int>(human, 536) = 36;
            ROUTINE(Ai_PickPresentLater, 0x41f5e0)((int)human, (int)node);
            int wins = field<int>(human, 536);
            fprintf(report, "pickup flags=%x order=null streak=36->%d\n", flags, wins);
            require(wins == (flags == 0x20 ? 0 : 36), "pickup streak");
        }
        fprintf(report, "PASS\n");
        fclose(report);
        ExitProcess(0);
    }
    BYTE* loaded = nullptr;
    for (BYTE** it = field<BYTE**>(global(0x68a970), 0); it != field<BYTE**>(global(0x68a974), 0); ++it)
        if (!strcmp(field<char*>(*it, 0), "AMBUSH_AD_3"))
            loaded = *it;
    fprintf(report, "world loader\n");
    checkRecord(loaded);
    BYTE** linkedSites = field<BYTE**>(loaded, 220);
    require(field<BYTE**>(loaded, 224) == linkedSites + 1 &&
                !strcmp(field<char*>(*linkedSites, 0), "AMBUSH_AD_4"),
            "resolved ambush link");
    checkMark(loaded, human);

    // An explicit evaluator control using the same stock record. Transfer the
    // owning 12-byte handles into the by-value arguments, as the game does.
    BYTE* interp = global(0x68bb58);
    const char* path = "event-expression.sch";
    int readMode = 1;
    Value port = {}, expression = {}, result = {};
    ROUTINE(Port_MakeB, 0x455f00)(&port, 0, &path, &readMode);
    ROUTINE(Port_ReadDatum, 0x4b4c20)(&expression, interp, port);
    require(expression.tag == 2, "read expression");
    ROUTINE(Sch_EvalTop, 0x4a6af0)(&result, interp, expression);
    require(result.tag == 2 && field<int>(interp, 120) == 0, "evaluated record");
    require(field<BYTE*>(interp, 108) == field<BYTE*>(interp, 112), "evaluator stack unwound");
    BYTE* fromClosure = ROUTINE(Ai_ReadAmbush, 0x412320)(interp, result);
    fprintf(report, "evaluator control\n");
    checkRecord(fromClosure);
    require(field<int>(loaded, 52) == field<int>(fromClosure, 52), "same road node");
    checkMark(fromClosure, human);
    approach::RearImpact impact = approach::RearImpact::none;
    if (strstr(GetCommandLineA(), " --rear-pass"))
        impact = approach::RearImpact::mild;
    if (strstr(GetCommandLineA(), " --strong-pass"))
        impact = approach::RearImpact::strong;
    approach::checkApproach(loaded, human, strstr(GetCommandLineA(), " --offset-pass") != nullptr, impact);
    fprintf(report, "PASS\n");
    fclose(report);
    ExitProcess(0);
}
#ifndef STOCK_WORLD
int main() {
    _controlfp(_PC_53, _MCW_PC);
    using Init = void(__cdecl*)();
    for (Init* f = (Init*)KingData; f < (Init*)(KingData + 0x488); ++f)
        if (*f)
            (*f)();
    WorldEvent();
    return 0;
}
#endif
