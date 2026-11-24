#include <cmath>
#include <initializer_list>
struct RoadPosition {
    int road, node;
    double t;
    int segment, object, flags, last;
};
struct Point {
    double x, y, z;
};
struct Pose {
    float m[12];
};
extern "C" Point* Road_PointOf(Point*, RoadPosition);
extern "C" Pose* Fx_VehicleMatrix(Pose*, BYTE*);
extern "C" bool Ambush_AheadOfHuman(BYTE*);
extern "C" void Ai_UpdateAmbushes();
extern "C" int __fastcall Cop_Step(BYTE*);
extern "C" int Ai_CarFrame(BYTE*, int*);
extern "C" void __fastcall CarBody_Step(BYTE*);

namespace approach {
// Headless test hosts exit after this case. Replace only the unrelated physics
// frame before the gameplay decision and the body integration following it.
static int readyFrame(BYTE*, int* f) {
    *f = 0;
    return 1;
}
static void __fastcall skipBody(BYTE*) {}
template <class F> static void frameDouble(F target, F replacement) {
    BYTE* p = reinterpret_cast<BYTE*>(target);
    DWORD prot = 0;
    require(VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &prot) != 0, "frame double");
    p[0] = 0xe9;
    field<int>(p, 1) = int(reinterpret_cast<uintptr_t>(replacement) - reinterpret_cast<uintptr_t>(p) - 5);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    DWORD old;
    require(VirtualProtect(p, 5, prot, &old) != 0, "frame protection");
}

static BYTE* table[500];
static BYTE entries[5][32], states[5][12], objects[5][420];
static unsigned mines() {
    unsigned mask = 0;
    for (int i = 0; i < 5; ++i) {
        int v = field<int>(states[i], 4);
        require(v == field<int>(objects[i], 408) && (v == 0 || v == 1), "mine state");
        if (v)
            mask |= 1u << i;
    }
    return mask;
}
static Point point(RoadPosition p) {
    Point w;
    ROUTINE(Road_PointOf, 0x435bf0)(&w, p);
    return w;
}
static double distance(Point a, Point b) {
    return sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}
static RoadPosition at(int road, int object, double t, int reversed) {
    return {road, -1, t, 0, object, reversed, -1};
}
static void locate(BYTE* human, RoadPosition p) {
    field<RoadPosition>(human, 40) = p;
    field<Point>(human, 624) = point(p);
}
static BYTE game[760], truck[23000], car[23000], tcab[18000], ccab[18000], equipment[256], damage[64];
static BYTE* nearby;
static void startCars(BYTE* human, BYTE* driver) {
    frameDouble(ROUTINE(Ai_CarFrame, 0x4cf620), readyFrame);
    frameDouble(ROUTINE(CarBody_Step, 0x556a70), skipBody);
    field<BYTE*>(global(0x6d2078), 0) = game;
    field<BYTE*>(game, 616) = truck;
    field<BYTE**>(truck, 12) = &nearby;
    field<BYTE*>(truck, 21600) = tcab;
    field<BYTE*>(car, 21600) = ccab;
    field<BYTE*>(car, 21588) = damage;
    field<BYTE*>(car, 9536) = equipment;
    for (int i = 0; i < 9; i++)
        field<int>(equipment, 8 + 20 * i) = 100;
    field<int>(car, 20608) = 1;
    field<BYTE*>(truck, 20636) = human;
    field<BYTE*>(car, 20636) = driver;
    field<int>(car, 21880) = 6;
    field<int>(truck, 20944) = 24;
    field<float>(tcab, 13100) = 12.0f;
    ROUTINE(Fx_VehicleMatrix, 0x4d4520)(&field<Pose>(car, 516), driver);
}
#include "rear_hit.h"

static void checkApproach(BYTE* site, BYTE* human, bool offsetPass, RearImpact rearHit) {
    fprintf(report, "approach lateral offset on final road=%d\n", offsetPass ? 5 : 0);
    BYTE* item = site;
    field<BYTE**>(global(0x68a970), 0) = &item;
    field<BYTE**>(global(0x68a974), 0) = &item + 1;
    for (int i = 0; i < 5; ++i) {
        table[325 + i] = entries[i];
        field<BYTE*>(entries[i], 24) = states[i];
        field<BYTE*>(entries[i], 28) = objects[i];
        field<int>(states[i], 4) = field<int>(objects[i], 408) = 1;
    }
    field<BYTE**>(view, 708) = table;
    require(site[240] && !site[28], "site armed by campaign");
    Point center = field<Point>(site, 56);
    for (RoadPosition p : {at(71, 1, .5, 0), at(71, 10, .5, 0)}) {
        for (int reverse = 0; reverse < 2; ++reverse) {
            p.flags = reverse;
            locate(human, p);
            bool ahead = ROUTINE(Ambush_AheadOfHuman, 0x42b550)(site);
            fprintf(report, "control road=%d obj=%d reverse=%d range=%.3f ahead=%d\n", p.road, p.object,
                    reverse, distance(center, point(p)), ahead);
            require(ahead == (p.object == 10), "direction on graph");
        }
    }
    locate(human, at(71, 10, .5, 0));
    for (double radius : {249.99, 250., 600., 600.01}) {
        field<Point>(human, 624) = {center.x + radius, center.y, center.z};
        bool allowed = ROUTINE(Ambush_AheadOfHuman, 0x42b550)(site);
        fprintf(report, "radius control %.2f eligible=%d\n", radius, allowed);
        require(allowed == (radius >= 250 && radius <= 600), "inclusive distance boundaries");
    }
    struct Leg {
        int edge, first, last, step;
    };
    const Leg legs[] = {{837, 1, 1, 1}, {836, 0, 0, 1}, {14, 0, 0, -1}, {70, 1, 0, -1}, {71, 15, 3, -1}};
    BYTE* driver = nullptr;
    bool fired = false;
    double previous = 0;
    double previousForward = 0, previousDistance = 0;
    for (const Leg& leg : legs)
        for (int obj = leg.first;; obj += leg.step) {
            for (int i = 0; i <= 64; ++i) {
                double t = (leg.step == 1 ? i : 64 - i) / 64.;
                RoadPosition pos = at(leg.edge, obj, t, leg.step == -1);
                locate(human, pos);
                Point xyz = point(pos);
                double range = distance(center, xyz);
                ROUTINE(Ai_UpdateAmbushes, 0x42c390)();
                if (site[28] && !driver) {
                    driver = field<BYTE*>(site, 124);
                    require(driver != field<BYTE*>(global(0x68b990), 0) && field<int>(driver, 92) == 6 &&
                                (field<DWORD>(driver, 96) & 0x80000),
                            "mafia assigned");
                    require(mines() == 0, "inactive mines at assignment");
                    require(previous > 600 && range <= 600 && leg.edge == 837, "first eligible sample");
                    fprintf(report,
                            "assigned edge=%d obj=%d t=%.6f xyz=%.3f %.3f %.3f range=%.3f previous=%.3f "
                            "mines=%u\n",
                            leg.edge, obj, t, xyz.x, xyz.y, xyz.z, range, previous, mines());
                    startCars(human, driver);
                    const float* m = field<Pose>(car, 516).m;
                    fprintf(report, "mafia matrix forward=%.6f %.6f %.6f origin=%.3f %.3f %.3f\n", m[3], m[4],
                            m[5], m[9], m[10], m[11]);
                }
                if (driver && !fired) {
                    Pose& pm = field<Pose>(truck, 516);
                    ROUTINE(Fx_VehicleMatrix, 0x4d4520)(&pm, human);
                    if (offsetPass && leg.edge == 71)
                        for (int axis = 0; axis < 3; ++axis)
                            pm.m[9 + axis] += 5 * pm.m[axis];
                    // Sample each car update while close enough for the outer gate.
                    if (range < 110) {
                        const float* m = field<Pose>(car, 516).m;
                        Point delta = {pm.m[9] - m[9], pm.m[10] - m[10], pm.m[11] - m[11]};
                        double forward = delta.x * m[3] + delta.y * m[4] + delta.z * m[5];
                        double separation = distance(delta, {0, 0, 0});
                        if (rearHit != RearImpact::none && forward > -9 && forward < -6 && separation < 10) {
                            rearHitThenPass(pos, human, driver, rearHit);
                            return;
                        }
                        if (previousDistance >= 30 && separation < 30)
                            fprintf(report, "entered 30 separation=%.3f forward=%.3f mask=%u\n", separation,
                                    forward, mines());
                        if (forward > 0 && previousForward <= 0 && separation < 30) {
                            Pose nearPose = pm;
                            for (int axis = 0; axis < 3; ++axis)
                                pm.m[9 + axis] = m[9 + axis] + 31 * m[3 + axis];
                            ROUTINE(Cop_Step, 0x4d1360)(car + 16);
                            require(!mines(), "31 units directly ahead blocks mine switches");
                            pm = nearPose;
                            field<int>(truck, 20944) = 19;
                            ROUTINE(Cop_Step, 0x4d1360)(car + 16);
                            require(!mines() && (field<DWORD>(driver, 96) & 0x80000),
                                    "phase 19 blocks mine switches");
                            field<int>(truck, 20944) = 24;
                            fprintf(report, "phase 19 at crossing mask=%u\n", mines());
                        }
                        ROUTINE(Cop_Step, 0x4d1360)(car + 16);
                        if (mines() != 0) {
                            fired = true;
                            require(mines() == 31 && !(field<DWORD>(driver, 96) & 0x80000) &&
                                        field<BYTE*>(driver, 968) == human,
                                    "all five mine switches and pursuit");
                            require(previousDistance < 30 && previousForward <= 0 && separation < 30 &&
                                        forward > 0,
                                    "crossed front plane within radius");
                            fprintf(report, "relative previous=%.3f forward=%.3f current=%.3f forward=%.3f\n",
                                    previousDistance, previousForward, separation, forward);
                            fprintf(report, "truck matrix origin=%.3f %.3f %.3f\n", pm.m[9], pm.m[10], pm.m[11]);
                            fprintf(report,
                                    "mines edge=%d obj=%d t=%.6f xyz=%.3f %.3f %.3f range=%.3f previous=%.3f "
                                    "mask=%u\n",
                                    leg.edge, obj, t, xyz.x, xyz.y, xyz.z, range, previous, mines());
                        }
                        previousForward = forward;
                        previousDistance = separation;
                    }
                }
                previous = range;
            }
            if (obj == leg.last)
                break;
        }
    require(rearHit == RearImpact::none && driver && fired, "approach reached the requested manoeuvre");
}
} // namespace approach
