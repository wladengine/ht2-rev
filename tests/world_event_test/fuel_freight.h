// Real single-player campaign tables and offers. Position at the gates and
// the physical tractor are outside this test of pickup and settlement.
struct Vec3;
extern "C" int Ai_LoadOrder(BYTE*, int, unsigned, double*, int*);
extern "C" void Ai_PickPresent(int, int);
extern "C" void Task_FormOrders();
extern "C" void Ai_GetPresentData(BYTE*, BYTE*, BYTE*);
extern "C" BYTE* __fastcall Fx_InitState(BYTE*, void*);
extern "C" void __fastcall Vec1_Free(BYTE**, int);
extern "C" void Copter_ApplyCargo();
extern "C" void displayScreenMessage(const char*);
extern "C" void __fastcall Wheels_ForceTorque(BYTE*, void*, Vec3*, Vec3*);
#include "tanker_interactions.h"

namespace fuel_freight {
static void skipCabUpdate() {}
static void skipBanner(const char*) {}

static void checkWheelMoment() {
    alignas(double) BYTE body[10100] = {};
    field<int>(body, 3540) = 1;
    for (int i = 0; i < 3; ++i) field<float>(body, 2304 + 16*i) = 1.f;
    field<float>(body, 6212 + 8) = 1.f;
    field<float>(body, 8436) = 8.f;
    for (int simulator = 0; simulator <= 1; ++simulator) {
        field<int>(body, 10088) = simulator;
        float force[3], torque[3];
        ROUTINE(Wheels_ForceTorque, 0x4f38f0)(body, nullptr, reinterpret_cast<Vec3*>(force), reinterpret_cast<Vec3*>(torque));
        fprintf(report, "simulator=%d lateral=%g moment=%g\n", simulator, force[0], torque[1]);
        require(force[0] == 8.f && torque[1] == (simulator ? 8.f : 2.f), "one supported wheel");
    }
}

struct Gate {
    int town = -1, index = -1;
};

static int transfer(BYTE* human, Gate gate, double quantity) {
    int index = -1;
    return ROUTINE(Ai_LoadOrder, 0x443d50)(human, gate.town, gate.index, &quantity, &index);
}

static int modelFlags(BYTE* manifest) {
    BYTE** models = field<BYTE**>(global(0x68cfd8), 0);
    return field<int>(models[field<int>(manifest, 4)], 4);
}

static void settle(BYTE* human, BYTE* trailer, BYTE** towns, BYTE** end, const char* label) {
    BYTE* order = field<BYTE*>(trailer, 28);
    require(order && field<BYTE*>(trailer, 32) == order + 144 && field<double>(order, 56) > 0., "positive single load");
    require(field<int>(order, 0) == 35, "fuel consignment");
    BYTE* goods = field<BYTE*>(global(0x68ccb8), 0) + 104 * field<int>(order, 0);
    fprintf(report, "cargo=%s unit_mass=%g\n", field<char*>(goods, 88), field<double>(goods, 16));
    int dest = field<int>(order, 12);
    require(dest >= 0 && dest < end - towns, "destination index");
    fprintf(report, "%s trailer=%d goods=%d quantity=%.6f destination=%d (%s)\n",
            label, field<int>(trailer, 4), field<int>(order, 0), field<double>(order, 56), field<int>(towns[dest], 24), field<char*>(towns[dest], 12));
    Gate right, wrong;
    for (BYTE** t = towns; t != end; ++t) {
        BYTE* start = field<BYTE*>(*t, 28);
        for (BYTE* g = start; g != field<BYTE*>(*t, 32); g += 16) {
            if (!(g[4] & 1)) continue;
            // Select another filling station by its localized name prefix.
            const char* name = field<char*>(*t, 12);
            if (t != towns + dest && (!name || strncmp(name, field<char*>(towns[dest], 12), 3))) continue;
            Gate& found = t == towns + dest ? right : wrong;
            if (found.town < 0) found = {field<int>(*t, 24), int(g - start) / 16};
        }
    }
    require(right.town >= 0 && wrong.town >= 0, "delivery gates");
    const double cash = field<double>(human, 136);
    require(transfer(human, wrong, -1.) == 0 && field<BYTE*>(trailer, 32) == order + 144 &&
            field<double>(human, 136) == cash, "wrong destination retains freight and balance");
    require(transfer(human, right, -1.) == 0 && field<BYTE*>(trailer, 32) == order, "destination unloads");
    const double paid = field<double>(human, 136);
    require(paid > cash, "positive receipt");
    require(transfer(human, right, -1.) == 0 && field<double>(human, 136) == paid, "no duplicate payment");
    fprintf(report, "wrong=retained payout=%.6f repeat=0\n", paid - cash);
}

static void check(BYTE* human, bool roadside, bool interactions) {
    approach::frameDouble(ROUTINE(Copter_ApplyCargo, 0x56f6d0), skipCabUpdate);
    approach::frameDouble(ROUTINE(displayScreenMessage, 0x483cb0), skipBanner);
    BYTE** models = field<BYTE**>(global(0x68cfd8), 0);
    BYTE* drivers = field<BYTE*>(global(0x68b990), 0);
    BYTE* tractor = nullptr;
    for (BYTE* p = field<BYTE*>(drivers, 0); p != drivers; p = field<BYTE*>(p, 0)) {
        BYTE* vehicle = field<BYTE*>(p, 516);
        if (vehicle && field<int>(vehicle, 4) >= 0 && field<int>(models[field<int>(vehicle, 4)], 32) == 3 &&
            field<BYTE*>(vehicle, 28) == field<BYTE*>(vehicle, 32)) {
            tractor = vehicle;
            field<BYTE*>(p, 516) = field<BYTE*>(human, 516);
            break;
        }
    }
    require(tractor != nullptr, "campaign tractor");
    field<BYTE*>(human, 516) = tractor;
    require(field<BYTE*>(human, 520) == nullptr, "starts without a trailer");
    BYTE* presents = field<BYTE*>(global(0x68cf6c), 0);
    BYTE* tanker = nullptr;
    BYTE* roadTanker = nullptr;
    for (BYTE* p = field<BYTE*>(presents, 0); p != presents; p = field<BYTE*>(p, 0)) {
        BYTE* item = field<BYTE*>(p, 8);
        BYTE* trailer = field<BYTE*>(item, 20);
        if ((field<int>(item, 8) & 0xf0) == 0x20 && trailer && (modelFlags(trailer) & 1) &&
            field<BYTE*>(trailer, 28) == field<BYTE*>(trailer, 32)) {
            tanker = p;
        }
        if ((field<int>(item, 8) & 0xf0) == 0x60 && trailer && (modelFlags(trailer) & 1) &&
            field<BYTE*>(trailer, 28) != field<BYTE*>(trailer, 32))
            roadTanker = p;
    }
    require(tanker != nullptr, "empty tanker placed by campaign");
    BYTE** towns = field<BYTE**>(global(0x68cec0), 0);
    BYTE** end = field<BYTE**>(global(0x68cec4), 0);
    if (roadside) {
        require(roadTanker != nullptr, "loaded roadside tanker placed by campaign");
        BYTE* roadHold = field<BYTE*>(field<BYTE*>(roadTanker, 8), 20);
        BYTE* order = field<BYTE*>(roadHold, 28);
        const double initial = field<double>(order, 56);
        alignas(double) BYTE preview[280] = {};
        ROUTINE(Fx_InitState, 0x4d06a0)(preview + 80, nullptr);
        ROUTINE(Ai_GetPresentData, 0x41be60)(human, roadTanker, preview);
        const double quoted = field<double>(preview, 88);
        require(quoted > 0 && quoted == field<double>(order, 56), "preview sets actual load");
        fprintf(report, "roadside initial=%g quoted=%.6f\n", initial, quoted);
        for (int offset = 168; offset >= 132; offset -= 12)
            ROUTINE(Vec1_Free, 0x44b090)(reinterpret_cast<BYTE**>(preview + offset), 0);
        ROUTINE(Ai_PickPresent, 0x41f0d0)(int(human), int(roadTanker));
        BYTE* trailer = field<BYTE*>(human, 520);
        require(trailer == roadHold && field<double>(field<BYTE*>(trailer, 28), 56) == quoted, "quoted trailer attached");
        settle(human, trailer, towns, end, "roadside freight");
        return;
    }
    checkWheelMoment();
    BYTE* goods = field<BYTE*>(global(0x68ccb8), 0);
    field<double>(global(0x68a820), 0) += 60.;
    ROUTINE(Task_FormOrders, 0x41b720)();
    Gate oil, ordinary;
    for (BYTE** t = towns; t != end; ++t) {
        BYTE* start = field<BYTE*>(*t, 28);
        for (BYTE* g = start; g != field<BYTE*>(*t, 32); g += 16) {
            BYTE* offer = field<BYTE*>(g, 8);
            if ((g[4] & 1) || !offer) continue;
            Gate& found = goods[field<int>(offer, 0) * 104 + 48] & 4 ? oil : ordinary;
            if (found.town < 0) found = {field<int>(*t, 24), int(g - start) / 16};
        }
    }
    require(oil.town >= 0 && ordinary.town >= 0, "real fuel and dry offers");
    require(transfer(human, oil, 1.) == -10, "fuel refused for bare tractor");
    ROUTINE(Ai_PickPresent, 0x41f0d0)(int(human), int(tanker));
    BYTE* trailer = field<BYTE*>(human, 520);
    require(trailer && (modelFlags(trailer) & 1), "tanker attached");
    if (interactions) tanker_interactions::damage(human, false);
    require(transfer(human, ordinary, 1.) == -11, "dry freight refused for tanker");
    require(transfer(human, oil, 1.) == 0, "depot loads tanker");
    if (interactions) {
        tanker_interactions::container(human, true);
        tanker_interactions::checkReceipt(human);
        tanker_interactions::damage(human, true);
        return;
    }
    fprintf(report, "depot=%d bare=-10 dry=-11\n", oil.town);
    settle(human, trailer, towns, end, "depot freight");
}
}
