#include <math.h>

extern "C" void __fastcall Car_TakeContacts(BYTE*, void*, BYTE*, BYTE*);
extern "C" float* __cdecl Vec3_PlaneHit(float*, float*, float*, float*, float*);
extern "C" float* __cdecl Vec3_PlaneHitT(float*, float*, float*, float*, float*, double*);

namespace auxiliary_wheels {
static bool CheckPlanes() {
    auto refHit = reinterpret_cast<decltype(&Vec3_PlaneHit)>(0x4e6490);
    auto refHitT = reinterpret_cast<decltype(&Vec3_PlaneHitT)>(0x4e6502);
    int cases = 0, errors = 0;
    for (float offset : {-1.f, 0.f, 1.f}) for (float tilt : {0.f, .25f})
        for (float step : {-2.f, .5f}) {
            float plane[] = {1, 2, 3}, normal[] = {tilt, 0, 1};
            float origin[] = {3, 1, 3 + offset}, direction[] = {.5f, 0, step};
            float expected[3], actual[3], expectedT[3], actualT[3];
            double t, refT;
            refHit(expected, plane, normal, origin, direction);
            Vec3_PlaneHit(actual, plane, normal, origin, direction);
            refHitT(expectedT, plane, normal, origin, direction, &refT);
            Vec3_PlaneHitT(actualT, plane, normal, origin, direction, &t);
            if (memcmp(expected, actual, sizeof actual) || memcmp(expectedT, actualT, sizeof actualT)
                || t != refT || (tilt == 0 && (expected[2] != 3 || refT != -offset / step))) ++errors;
            ++cases;
        }
    printf("wheel plane intersections cases=%d mismatches=%d\n", cases, errors);
    return !errors;
}

// Four simulated wheels support a body with two additional visual axles.
// Use the entire contact-export routine, including its height clamps.
static void Heights(bool original, unsigned mask, float slope, float hubHeight, float* result) {
    BYTE car[22000] = {}, chassis[16000] = {}, state[5000] = {}, out[300] = {}, model[16] = {};
    F(BYTE*, car, 21600) = chassis;
    F(BYTE*, car, 336) = model;
    F(const char*, model, 12) = "fixture";
    F(int, car, 10424) = 8;
    F(int, car, 10428) = 4;
    for (int axis : {0, 16, 32}) {
        F(float, state, axis) = F(float, state, 48 + axis) = 1;
        F(float, car, 612 + axis) = F(float, car, 756 + axis) = 1;
    }
    F(float, state, 344) = 1;
    F(float, state, 2756) = .5f;
    for (int i=0; i<4; ++i) {
        BYTE* contact = state + 352 + 248*i;
        const float x = i%2 ? 1.f : -1.f, y = i<2 ? 3.f : -3.f;
        F(Wc_V, contact, 0) = Wc_V{x,y,slope*y};
        F(Wc_V, contact, 12) = Wc_V{0,-slope,1};
        F(WORD, contact, 24) = (mask >> i) & 1;
        F(int, car, 20400 + i*4) = i;
        F(Wc_V, car, 10712 + i*12) = Wc_V{x,y/3,hubHeight};
        F(float, car, 10584 + (4+i)*4) = 1;
    }
    const auto take = original ? reinterpret_cast<decltype(&Car_TakeContacts)>(0x548250) : Car_TakeContacts;
    take(car, nullptr, out, state);
    memcpy(result, out+104, 8*sizeof(float));
}

// Unladen StormR / Storm model tables. Move two persistent records over the
// same prescribed road planes and compare each output before the next frame.
static bool CheckStormMotion() {
    struct Rig {
        BYTE car[22000], chassis[16000], state[5000], out[300], model[16];
    };
    Rig rigs[2] = {};
    constexpr float radius = .7112f, limit = .4f;
    const unsigned supports[] = {15, 7, 10, 8, 0, 4, 5, 11, 15};
    for (Rig& rig : rigs) {
        F(BYTE*, rig.car, 21600) = rig.chassis;
        F(BYTE*, rig.car, 336) = rig.model;
        F(const char*, rig.model, 12) = "Storm";
        F(int, rig.car, 10424) = 8;
        F(int, rig.car, 10428) = 4;
        F(float, rig.state, 2756) = radius;
        F(float, rig.state, 344) = 1;
        for (int axis : {0, 16, 32})
            F(float, rig.car, 612 + axis) = F(float, rig.car, 756 + axis) = 1;
        for (int i=0; i<4; ++i) {
            F(int, rig.car, 20400 + i*4) = i;
            F(float, rig.car, 10600 + i*4) = limit;
            F(Wc_V, rig.car, 10712 + i*12) =
                Wc_V{i%2 ? 1.48f : -1.48f, i<2 ? .6465f : -2.6967f, .1f};
        }
    }
    auto original = reinterpret_cast<decltype(&Car_TakeContacts)>(0x548250);
    int frames = 0, differences = 0, geometryErrors = 0, compressed = 0, extended = 0, interior = 0;
    for (int frame=0; frame<144; ++frame) {
        double phase = frame * .055;
        double bank = .16 * sin(phase), grade = .08 * cos(phase);
        double roll = .08 * sin(phase - .35), c = cos(roll), s = sin(roll);
        double x = 11 + .025*frame, y = 20 + .3*frame;
        double roadZ = 4 + .08/.055*.3*sin(phase);
        double z = roadZ + radius + .10 + .16*cos(phase*1.5);
        unsigned mask = supports[frame/16];
        for (Rig& rig : rigs) {
            float* m = reinterpret_cast<float*>(rig.state);
            m[0]=(float)c; m[2]=(float)s; m[4]=1; m[6]=(float)-s; m[8]=(float)c;
            m[9]=(float)x; m[10]=(float)y; m[11]=(float)z;
            m[12]=(float)c; m[14]=(float)-s; m[16]=1; m[18]=(float)s; m[20]=(float)c;
            m[21]=(float)(-x*c-z*s); m[22]=(float)-y; m[23]=(float)(x*s-z*c);
            for (int i=0; i<4; ++i) {
                BYTE* wheel = rig.state + 352 + i*248;
                double dx = (i%2 ? 1.48 : -1.48)*c, dy = i<2 ? 2.6 : -4.6032;
                F(Wc_V,wheel,0) = Wc_V{(float)(x+dx),(float)(y+dy),(float)(roadZ+bank*dx+grade*dy)};
                F(Wc_V,wheel,12) = Wc_V{(float)-bank,(float)-grade,1};
                F(WORD,wheel,24) = (mask >> i) & 1;
            }
        }
        original(rigs[0].car, nullptr, rigs[0].out, rigs[0].state);
        Car_TakeContacts(rigs[1].car, nullptr, rigs[1].out, rigs[1].state);
        if (memcmp(rigs[0].out+120, rigs[1].out+120, 4*sizeof(float))) ++differences;
        for (int i=0; i<4; ++i) {
            Wc_V q = F(Wc_V,rigs[0].car,10712+12*i);
            double distance = (z-roadZ + q.x*(s-bank*c) - grade*q.y)/(c+bank*s) + q.z;
            double travel = mask ? distance-radius : limit;
            if (travel <= 0) { travel = 0; ++compressed; }
            else if (travel >= limit) { travel = limit; ++extended; }
            else ++interior;
            float actual = F(float,rigs[0].out,120+4*i);
            if (!(fabs(actual-travel) < .00003)) ++geometryErrors;
        }
        ++frames;
    }
    printf("Storm slope frames=%d mismatches=%d geometryErrors=%d compressed=%d extended=%d interior=%d\n",
           frames,differences,geometryErrors,compressed,extended,interior);
    return !differences && !geometryErrors && compressed && extended && interior;
}

static bool Check() {
    gameplay::SetWorld(0x702630,Wc_V{0,0,1});
    gameplay::SetWorld(0x702650,Wc_V{0,0,0});
    // Use the host CRT for the original's wheel diagnostic label only.
    Redirect((void*)0x61cca9, (void*)sprintf);
    bool planes = CheckPlanes();
    int cases = 0, errors = 0;
    for (unsigned mask=0; mask<16; ++mask) for (float slope : {0.f,.25f})
        for (float z : {.25f,1.f,2.f}) {
            float ref[8], got[8];
            Heights(true,mask,slope,z,ref);
            Heights(false,mask,slope,z,got);
            if (mask==8 && slope==0 && z==1)
                printf("last supporting wheel: extra travel original=%.3f rebuilt=%.3f\n",ref[4],got[4]);
            if (memcmp(ref,got,sizeof ref) || (mask && slope==0 && z==1 && ref[4]!=.5f)) ++errors;
            ++cases;
        }
    printf("auxiliary wheel travel cases=%d mismatches=%d\n",cases,errors);
    bool motion = CheckStormMotion();
    return planes && !errors && motion;
}
}
