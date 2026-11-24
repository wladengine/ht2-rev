#include <objbase.h>
#pragma comment(lib, "ole32.lib")

extern "C" void Car_WorldEvent(const int*, BYTE*);
extern "C" void Ai_KillDelayedPlayers();
extern "C" int Ai_CreatePlayers();

namespace driver_return {
// The optional input is a read-only copy of the pre-session campaign profile.
static void check(BYTE* settings) {
    require(SUCCEEDED(CoInitialize(nullptr)), "COM");
    IStorage *root = nullptr, *games = nullptr, *slot = nullptr;
    require(SUCCEEDED(StgOpenStorage(L"respawn.pl1", nullptr, STGM_READ | STGM_SHARE_DENY_WRITE, nullptr, 0, &root)),
            "open copied profile");
    require(
        SUCCEEDED(root->OpenStorage(L"listofgames", nullptr, STGM_READ | STGM_SHARE_EXCLUSIVE, nullptr, 0, &games)),
        "listofgames");
    require(SUCCEEDED(games->OpenStorage(L"\x0411.\x0421\x041e\x0425\x0420", nullptr,
                                         STGM_READ | STGM_SHARE_EXCLUSIVE, nullptr, 0, &slot)),
            "quick slot");
    settings[24] = 1;
    field<double>(settings, 0) = 1.;
    ROUTINE(Ai_StartGame, 0x434780)(reinterpret_cast<const char*>(slot), 0, settings);

    BYTE* head = field<BYTE*>(global(0x68b990), 0);
    auto lookup = [&](const char* id) -> BYTE* {
        for (BYTE* p = field<BYTE*>(head, 0); p != head; p = field<BYTE*>(p, 0))
            if (!strcmp(field<char*>(p, 8), id))
                return p;
        return nullptr;
    };
    auto counters = field<int*>(global(0x68cca8), 0);
    auto model = [](BYTE* hold) { return hold ? field<int>(hold, 4) : -1; };
    auto cargo = [](BYTE* hold) { return hold ? fleet_review::count(hold, 28, 144) : 0; };
    BYTE* human = lookup("$$$_LIVE_0_0");
    BYTE* mark = lookup("$$$_AI_26");
    require(mark && human && field<BYTE*>(mark, 176) == human && counters[2] == 50, "late campaign roster");
    int identity = field<int>(mark, 992);
    auto print = [&](int generation, BYTE* p) {
        const RoadPosition road = field<RoadPosition>(p, 40);
        Point xyz;
        ROUTINE(Road_PointOf, 0x435bf0)(&xyz, road);
        BYTE* rig = field<BYTE*>(p, 516);
        BYTE* trailer = field<BYTE*>(p, 520);
        BYTE* owner = field<BYTE*>(p, 176);
        fprintf(report,
                "generation=%d id=%s name=%s identity=%d owner=%s staff=%d wallet=%.2f licences=%d "
                "streak=%d mafia_debt=%.2f cab=%d trailer=%d orders=%d+%d state=%d lot=%d "
                "ready=%d racer=%d road=%d node=%d xyz=(%.2f,%.2f,%.2f) pickup_town=%d gate=%d\n",
                generation, field<char*>(p, 8), field<char*>(p, 272), field<int>(p, 992),
                owner == head ? "free" : field<char*>(owner, 8), fleet_review::count(p, 160, 16),
                field<double>(p, 136), fleet_review::count(p, 144, 8), field<int>(p, 536), field<double>(p, 224),
                model(rig), model(trailer), cargo(rig), cargo(trailer), field<int>(p, 100), field<int>(p, 120),
                (field<int>(p, 96) & 0x404) == 0x404, p[496], road.road, road.node, xyz.x, xyz.y, xyz.z,
                field<int>(p, 128), field<int>(p, 132));
    };
    print(0, mark);
    for (int generation = 1; generation <= 4; ++generation) {
        // Positive controls for fields that would otherwise already be zero in the old record.
        if (generation == 2) {
            field<double>(mark, 136) = 123456.;
            field<int>(mark, 536) = 7;
            field<double>(mark, 224) = 235.;
            ROUTINE(Player_GrantLicences, 0x4774e0)(mark, 300., 2);
            print(-1, mark);
        }
        char oldId[64];
        strcpy_s(oldId, field<char*>(mark, 8));
        static BYTE cab[22000];
        field<BYTE*>(cab, 20636) = mark;
        // With a loaded scene object, the height event uses its Z in either motion mode.
        float plane = 0.f;
        int removal[4] = {3264};
        if (generation <= 2) {
            unsigned& flags = field<unsigned>(mark, 96);
            unsigned original = flags;
            flags = (flags & ~1u) | (generation - 1);
            removal[0] = 16;
            removal[3] = reinterpret_cast<int>(&plane);
            float& cabZ = field<float>(cab, 16 + 500 + 44);
            cabZ = 3.f;
            ROUTINE(Car_WorldEvent, 0x4ce300)(removal, cab);
            require(!(flags & 0x20000), "above the height plane");
            cabZ = -3.f;
            ROUTINE(Car_WorldEvent, 0x4ce300)(removal, cab);
            require((flags & 0x20000) != 0, "below the height plane");
            fprintf(report, "height_event=16 physical=%d plane=0 z_above=3 removed=0 z_below=-3 marked=1\n",
                    generation - 1);
            flags = original | 0x20000;
        } else
            ROUTINE(Car_WorldEvent, 0x4ce300)(removal, cab);
        ROUTINE(Ai_KillDelayedPlayers, 0x446af0)();
        require(!lookup(oldId) && counters[2] == 49, "trucker removed");
        ROUTINE(Ai_CreatePlayers, 0x423a90)();
        require(counters[2] == 50 && fleet_review::count(human, 160, 16) == 45, "population restored");
        mark = nullptr;
        for (BYTE* p = field<BYTE*>(head, 0); p != head; p = field<BYTE*>(p, 0))
            if (field<int>(p, 92) == 2 && field<int>(p, 992) == identity) {
                require(!mark, "unique trucker identity");
                mark = p;
            }
        require(mark && strcmp(field<char*>(mark, 8), oldId) && field<BYTE*>(mark, 176) == head,
                "fresh independent trucker");
        print(generation, mark);
        BYTE** towns = field<BYTE**>(global(0x68cec0), 0);
        BYTE** end = field<BYTE**>(global(0x68cec4), 0);
        for (; towns != end; ++towns) {
            if (field<int>(*towns, 24) == field<int>(mark, 128)) {
                fprintf(report, "pickup=%s\n", field<char*>(*towns, 12));
                break;
            }
        }
        require(field<double>(mark, 136) == field<double>(global(0x68cfe8), 0) &&
                    !fleet_review::count(mark, 144, 8) && !fleet_review::count(mark, 160, 16) &&
                    !field<int>(mark, 536) && field<double>(mark, 224) == 0. && !cargo(field<BYTE*>(mark, 516)) &&
                    !cargo(field<BYTE*>(mark, 520)),
                "initial finances and orders");
    }
    slot->Release();
    games->Release();
    root->Release();
    CoUninitialize();
}
} // namespace driver_return
