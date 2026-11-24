extern "C" void __fastcall SheetAnim_StepWithSound(BYTE*);
extern "C" void* __fastcall Camera_Get(BYTE*, void*, void*);
extern "C" int __fastcall SheetAnim_Step(BYTE*);
struct Ss_V3 { float x, y, z; };
extern "C" void __fastcall Sound_UpdateAt(BYTE*, void*, Ss_V3, double, double);

namespace effect_sound {
static float camera[12];
static bool writeCamera;
static BYTE sound;
struct Result {
    int cameras, sounds, steps;
    Ss_V3 point;
    double range, volume;
};
static Result observed;
static void* __fastcall GetCamera(BYTE*, void*, void* out) {
    ++observed.cameras;
    if (!writeCamera) return camera;
    return memcpy(out, camera, sizeof camera);
}
static void __fastcall OnSound(BYTE* source, void*, Ss_V3 point, double range, double volume) {
    observed.sounds += source == &sound ? 1 : 100;
    observed.point = point;
    observed.range = range;
    observed.volume = volume;
}
static int __fastcall Step(BYTE*) { return ++observed.steps; }
static bool Check() {
    Redirect((void*)Camera_Get, (void*)GetCamera); Redirect((void*)0x5dbcc0, (void*)GetCamera);
    Redirect((void*)Sound_UpdateAt, (void*)OnSound); Redirect((void*)0x5fd870, (void*)OnSound);
    Redirect((void*)SheetAnim_Step, (void*)Step); Redirect((void*)0x5a4ca0, (void*)Step);
    const auto original = reinterpret_cast<decltype(&SheetAnim_StepWithSound)>(0x5a5000);
    const float poses[][12] = {
        {1,0,0, 0,1,0, 0,0,1, 0,0,0},
        {0,1,0, -1,0,0, 0,0,1, -2061.924072f,-1478.427368f,42.041672f}
    };
    BYTE effect[192] = {};
    F(float,effect,88)=-2128.f; F(float,effect,92)=-1463.f; F(float,effect,96)=38.31f;
    F(double,effect,184)=0.875;
    int cases=0;
    // Check the projection first with a detached camera matrix. This rejects
    // shifted coefficients before exercising the caller-owned output buffer.
    for (bool output : {false,true}) for (const auto& pose : poses)
        for (int frame : {0,1}) for (bool audible : {true,false}) {
            writeCamera=output;
            memcpy(camera,pose,sizeof camera);
            F(int,effect,16)=frame;
            F(BYTE*,effect,176)=audible ? &sound : nullptr;
            observed={}; original(effect);
            Result expected=observed;
            observed={}; SheetAnim_StepWithSound(effect);
            int calls=audible && frame==0;
            ++cases;
            if (expected.steps!=1 || expected.sounds!=calls || expected.cameras!=calls ||
                (calls && (expected.range!=50. || expected.volume!=0.875)) ||
                memcmp(&expected,&observed,sizeof expected)) {
                printf("effect sound mismatch case=%d expected=%.3f,%.3f,%.3f actual=%.3f,%.3f,%.3f\n",
                       cases,expected.point.x,expected.point.y,expected.point.z,
                       observed.point.x,observed.point.y,observed.point.z);
                return false;
            }
        }
    printf("effect sound cases=%d mismatches=0\n",cases);
    return true;
}
}
