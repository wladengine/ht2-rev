#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// The stock CRT has initialized its static objects when CompanyRun is called.
using Byte = unsigned char;
template<typename T>
static T& field(void* base, size_t offset) {
    return *reinterpret_cast<T*>(static_cast<Byte*>(base) + offset);
}

static FILE* report;
static double elapsed, peakShare, firstHire = -1, victory = -1;
static unsigned paidDeliveries;
static int maxCompanies, maxEmployees, winnerEmployees;
static Byte windowState[4096], view[18000];
static Byte* human;
static double startPoint[3];

static int entries(Byte* player, size_t offset, int entrySize) {
    return static_cast<int>((field<uintptr_t>(player, offset + 4) -
                             field<uintptr_t>(player, offset)) / entrySize);
}

[[noreturn]] static void fail(const char* message) {
    fprintf(report, "ERROR %s %.9f\n", message, elapsed);
    fclose(report);
    ExitProcess(3);
}

static int onPlayerEvent(Byte* player, int kind, int value) {
    const int role = field<int>(player, 92);
    if (kind == 11 && role == 2 && value &&
        field<double>(reinterpret_cast<void*>(value), 32) > 0) {
        ++paidDeliveries;
    }
    if (kind == 12) {
        if (role != 2 || field<double>(player, 768) < 0.51)
            fail("unexpected-campaign-ending");
        victory = elapsed;
        winnerEmployees = entries(player, 160, 16);
    }
    // In regular play Media_AiEvent returns -1 for arrivals (9 and 10).
    // Suppressing those callbacks would also suppress trips and unloading.
    switch (kind) {
    case 1: case 4: case 5: case 14: case 15: case 17: return 0;
    default: return -1;
    }
}

static void onPresent(int, int) {}

static void observeFleet() {
    Byte* sentinel = *reinterpret_cast<Byte**>(0x68b990);
    int companies = 0, traders = 0;
    bool sawHuman = false;
    for (auto p = field<Byte*>(sentinel, 0); p != sentinel; p = field<Byte*>(p, 0)) {
        const int role = field<int>(p, 92);
        if (role == 1) {
            sawHuman = true;
            if (!human) {
                human = p;
                memcpy(startPoint, p + 624, sizeof startPoint);
                fprintf(report, "player money=%.2f position=%.2f,%.2f,%.2f licences=%d staff=%d\n",
                        field<double>(p, 136), startPoint[0], startPoint[1], startPoint[2],
                        entries(p, 144, 8), entries(p, 160, 16));
            }
            if (p != human || entries(p, 160, 16) || memcmp(startPoint, p + 624, sizeof startPoint))
                fail("player-moved-or-hired");
        }
        if (role != 2)
            continue;
        ++traders;
        int employees = entries(p, 160, 16);
        if (employees)
            ++companies;
        maxEmployees = (std::max)(maxEmployees, employees);
        peakShare = (std::max)(peakShare, field<double>(p, 768));
    }
    if (!sawHuman || !traders)
        fail("missing-fleet");
    if (companies && firstHire < 0)
        firstHire = elapsed;
    maxCompanies = (std::max)(maxCompanies, companies);
}

static std::vector<double> readFrameSamples() {
    std::vector<double> frames;
    char path[MAX_PATH];
    auto length = GetEnvironmentVariableA("HT2_FRAME_FILE", path, sizeof path);
    if (!length)
        return frames;
    if (length >= sizeof path)
        fail("frame-path");
    FILE* input = fopen(path, "rb");
    if (!input)
        fail("frame-file");
    double frame;
    while (fread(&frame, sizeof frame, 1, input) == 1) {
        if (!(frame > 0 && frame <= 0.30))
            fail("frame-duration");
        frames.push_back(frame);
    }
    if (ferror(input) || frames.size() < 256)
        fail("frame-data");
    fclose(input);
    fprintf(report, "frame_samples %u\n", static_cast<unsigned>(frames.size()));
    return frames;
}

static double envNumber(const char* name) {
    char value[64];
    auto length = GetEnvironmentVariableA(name, value, sizeof value);
    if (!length || length >= sizeof value)
        fail(name);
    return atof(value);
}

extern "C" void __cdecl CompanyRun() {
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
    char path[MAX_PATH];
    auto length = GetEnvironmentVariableA("HT2_RUN_LOG", path, sizeof path);
    if (!length || length >= sizeof path)
        ExitProcess(2);
    report = fopen(path, "wt");
    if (!report)
        ExitProcess(2);
    setvbuf(report, nullptr, _IONBF, 0);

    auto jitter = static_cast<uint32_t>(envNumber("HT2_TIMING_SEED"));
    const double horizon = envNumber("HT2_HORIZON");
    const double step = envNumber("HT2_MEAN_STEP");
    fprintf(report, "start %u %.0f %.4f\n", jitter, horizon, step);
    const auto frames = readFrameSamples();
    if (!(horizon > 0) || (frames.empty() && !(step > 0)))
        fail("clock-configuration");

    auto loadVehicleCache = reinterpret_cast<int(__cdecl*)()>(0x508000);
    auto loadVehicleTech = reinterpret_cast<int(__cdecl*)()>(0x50a5a0);
    if (!loadVehicleCache() || !loadVehicleTech())
        fail("vehicle-cache");
    using Initialize = int(__cdecl*)(const char*, decltype(&onPlayerEvent), decltype(&onPresent));
    reinterpret_cast<Initialize>(0x435730)("ENV", onPlayerEvent, onPresent);

    // AI_Time reads the clock on the main window's game view. Empty scene and
    // resource lists keep vehicles on the game's abstract road simulation.
    *reinterpret_cast<void**>(0x6cec6c) = windowState;
    field<void*>(windowState, 1356) = view;
    *reinterpret_cast<void**>(0x696ca0) = view;
    const int configuredCar = GetPrivateProfileIntA("EFFECTS", "cartruck", 0, ".\\TRUCK.INI");
    const unsigned traffic = GetPrivateProfileIntA("EFFECTS", "trafs", 50, ".\\TRUCK.INI");
    Byte settings[32] = {};
    field<double>(settings, 0) = traffic * 0.01;
    field<const char*>(settings, 12) = "Test";
    field<int>(settings, 16) = 1;
    settings[24] = envNumber("HT2_START_FROM_INI") != 0 && configuredCar != 0;
    fprintf(report, "configuration jeep=%d traffic=%u\n", int(settings[24]), traffic);
    using StartGame = void(__cdecl*)(const char*, unsigned, const Byte*);
    reinterpret_cast<StartGame>(0x434780)(nullptr, 0, settings);

    auto tick = reinterpret_cast<void(__cdecl*)()>(0x42d6e0);
    double nextObservation = 0, nextHour = 3600;
    size_t frameIndex = 0;
    unsigned blockLeft = 0;
    for (;;) {
        field<double>(view, 2968) = elapsed;
        tick();
        const bool finished = victory >= 0 || elapsed >= horizon;
        if (elapsed >= nextObservation || finished) {
            observeFleet();
            nextObservation = elapsed + 1;
        }
        if (elapsed >= nextHour || finished) {
            fprintf(report, "sample %.6f %.10f %d %d %u %.6f %.6f %d\n",
                    elapsed, peakShare, maxCompanies, maxEmployees, paidDeliveries,
                    firstHire, victory, winnerEmployees);
            nextHour += 3600;
        }
        if (finished)
            break;
        // Only frame timing uses this generator. The game owns both campaign RNGs.
        if (frames.empty()) {
            jitter = 1664525u * jitter + 1013904223u;
            elapsed += step * (0.8 + 0.4 * (jitter / 4294967296.0));
        } else {
            if (!blockLeft) {
                jitter = 1664525u * jitter + 1013904223u;
                frameIndex = jitter % (frames.size() - 255);
                blockLeft = 256;
            }
            elapsed += frames[frameIndex++];
            --blockLeft;
        }
        elapsed = (std::min)(elapsed, horizon);
    }
    fprintf(report, "player_end money=%.2f licences=%d staff=%d\n", field<double>(human, 136),
            entries(human, 144, 8), entries(human, 160, 16));
    fputs("complete\n", report);
    fclose(report);
    ExitProcess(0);
}
