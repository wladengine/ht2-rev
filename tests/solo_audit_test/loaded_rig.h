// Advance both physical bodies through a single suspension substep per road
// sample. Hitch_Solve owns the contact/push slots and the coupling of the two
// bodies. The fixture supplies road contacts and replaces the step scheduler.
extern "C" DWORD __fastcall Hitch_Solve(DWORD, DWORD);
extern "C" int __fastcall Body_StateStep(BYTE*);
extern "C" int __fastcall Body_SubStep(BYTE*, void*, int);

namespace loaded_rig {
constexpr int wheelCount[] = {4, 3};
constexpr int sampleCount = 8;
constexpr float stepSeconds = 1.f / 128.f;
constexpr float roughRoad[] = {0.f, .125f, .25f, .0625f, -.25f, -.125f, 0.f, 0.f};
static BYTE vehicles[2][22400], bodies[2][17000];
static bool original;
static int substeps;

static int __fastcall OneStep(BYTE* body) {
    if (original) reinterpret_cast<int (__thiscall*)(BYTE*, int)>(0x4f8260)(body, 1);
    else Body_SubStep(body, nullptr, 1);
    ++substeps;
    return 0;
}

static void Init(float trailerMass) {
    const float identity[12] = {1,0,0,0,1,0,0,0,1};
    memset(bodies, 0, sizeof bodies);
    memset(vehicles, 0, sizeof vehicles);
    for (int side = 0; side < 2; ++side) {
        BYTE* b = bodies[side];
        BYTE* v = vehicles[side];
        float mass = side ? trailerMass : 6500.f;
        F(float,v,3284) = F(float,b,9792) = mass;
        F(BYTE*,v,21600) = b;
        F(int,v,21892) = 0;
        F(int,v,10420) = F(int,b,2252) = side;
        for (int off : {708,1808,1856}) memcpy(v+off,identity,sizeof identity);
        for (int off : {16,2256,2304,2352,8772,8820,8868,8916,8964,10128}) memcpy(b+off,identity,sizeof identity);
        F(float,b,8908) = side ? -3.f : 0.f;
        F(float,b,9004) = -F(float,b,8908);
        F(int,b,3540) = wheelCount[side];
        F(float,b,9508) = F(float,b,9512) = stepSeconds;
        F(float,b,9016) = 3.f;
        for (int axis = 0; axis < 3; ++axis)
            F(float,v,3304+4*axis) = F(float,b,8008+4*axis) = mass * (axis+1);
        for (int i = 0; i < wheelCount[side]; ++i) {
            Wc_V hub = {i%2 ? 1.f : -1.f,i < 2 ? (side ? -2.f : 2.f) : (side ? 1.f : -2.f),.75f};
            F(Wc_V,b,7356+12*i) = hub;
            F(float,b,7028+4*i) = .5f;
            F(float,b,8060+4*i) = 1.f;
            F(float,b,8092+4*i) = .375f;
        }
    }
    F(BYTE*,vehicles[0],21880) = vehicles[1];
}

struct Sample {
    float load;
    DWORD solverResult;
    struct Body {
        Wc_V position, velocity, spin, hitchForce;
        float travel[4], springForce[4];
    } body[2];
};

static Sample Tick(int tick, bool uneven) {
    const Wc_V up = {0,0,1};
    const float down[] = {0,0,-1};
    Sample sample = {};
    sample.load = static_cast<float>(original
        ? reinterpret_cast<double (__thiscall*)(BYTE*, const float*)>(0x4eec80)(bodies[1],down)
        : Trailer_SaddleLoad(bodies[1],nullptr,down));
    for (int side = 0; side < 2; ++side) {
        BYTE* b = bodies[side];
        // Trailer reaches each road sample one step after the tractor.
        float height = uneven ? roughRoad[max(tick-side,0)] : 0.f;
        Wc_V point = {0,0,height};
        memcpy(b+16,b+8868,48);
        memcpy(b+2304,b+8964,48);
        float load = side ? 0.f : sample.load;
        if (original) {
            reinterpret_cast<void (__thiscall*)(BYTE*,float)>(0x4eeaf0)(b,load);
            reinterpret_cast<void (__thiscall*)(BYTE*,int,const Wc_V*,const Wc_V*)>(0x4f1fa0)(b,tick==0,&point,&up);
        } else {
            Body_SetLoad(b,nullptr,load);
            Wheels_OnPlane(b,nullptr,tick==0,&point,&up);
        }
        for (int i=0; i<wheelCount[side]; ++i) {
            const float y=F(float,b,7360+12*i)+F(float,b,8908);
            F(Wc_V,b,9308+12*i) = {-4,y,height};
            F(Wc_V,b,9404+12*i) = {4,y,height};
            memcpy(b+4120+248*i,b+248+248*i,248);
        }
        F(int,b,9524)=1;
        F(int,b,10176)=F(int,b,10180)=0;
    }
    sample.solverResult = original ? reinterpret_cast<DWORD (__thiscall*)(BYTE*)>(0x558f20)(vehicles[0])
                               : Hitch_Solve(reinterpret_cast<DWORD>(vehicles[0]),0);
    for (int side=0; side<2; ++side) {
        BYTE* b=bodies[side];
        auto& result=sample.body[side];
        result.position=F(Wc_V,b,8904);
        result.velocity=F(Wc_V,b,9012);
        result.spin=F(Wc_V,b,9048);
        result.hitchForce=F(Wc_V,b,10244);
        memcpy(result.travel,b+9244,wheelCount[side]*sizeof(float));
        memcpy(result.springForce,b+8740,wheelCount[side]*sizeof(float));
    }
    return sample;
}

static bool Check() {
    Redirect((void*)0x4fef20,(void*)OneStep);
    Redirect((void*)Body_StateStep,(void*)OneStep);
    const float identity[12]={1,0,0,0,1,0,0,0,1};
    for (int i=0;i<12;++i) gameplay::SetWorld(0x7025f0+4*i,identity[i]);
    gameplay::SetWorld(0x702620,Wc_V{0,1,0});
    gameplay::SetWorld(0x702630,Wc_V{0,0,1});
    gameplay::SetWorld(0x702640,Wc_V{1,0,0});
    gameplay::SetWorld(0x702650,Wc_V{0,0,0});
    gameplay::SetWorld(0x697f9c+204,.125f);
    gameplay::SetWorld(0x697f9c+156,Wc_V{.25f,-1.f,.25f});
    gameplay::SetWorld(0x697f9c+2416+168,Wc_V{.25f,2.f,.25f});
    gameplay::SetWorld(0x67a4a8,0);
    int errors=0, cases=0;
    Sample reference[sampleCount];
    bool exercised=true;
    for (float mass : {3200.f,9200.f}) for (bool uneven : {false,true}) {
        for (bool ref : {true,false}) {
            original=ref;
            Init(mass);
            substeps=0;
            for (int i=0;i<sampleCount;++i) {
                Sample s=Tick(i,uneven);
                if (ref) reference[i]=s;
                else {
                    if (memcmp(&s,&reference[i],sizeof s)) {
                        const DWORD* a=(const DWORD*)&reference[i], *b=(const DWORD*)&s;
                        for (unsigned j=0;j<sizeof s/4;++j) if (a[j]!=b[j]) {
                            printf("rig delta mass=%.0f uneven=%d tick=%d word=%u bits=%08x/%08x\n",mass,uneven,i,j,a[j],b[j]); break;
                        }
                        ++errors;
                    }
                    ++cases;
                }
                if (ref && uneven) printf("rig mass=%.0f tick=%d solverResult=%u load=%.1f z=%.6g/%.6g travel=%.6g/%.6g spring=%.6g/%.6g hitch=%.6g\n",
                    mass,i,s.solverResult,s.load,s.body[0].position.z,s.body[1].position.z,s.body[0].travel[0],s.body[1].travel[0],s.body[0].springForce[0],s.body[1].springForce[0],s.body[0].hitchForce.z);
            }
            exercised &= substeps==2*sampleCount;
            if (ref && uneven) {
                const auto& crest=reference[2];
                const auto& dip=reference[5];
                exercised &= crest.load==(mass==3200.f ? 15680.f : 45080.f) && crest.body[0].springForce[0]>0 &&
                    crest.body[0].travel[0]<crest.body[1].travel[0] &&
                    dip.body[0].springForce[0]==0 && dip.body[1].springForce[0]==0 &&
                    dip.body[0].hitchForce.z!=0 && dip.body[0].hitchForce.z==-dip.body[1].hitchForce.z;
                for (const auto& observed : reference)
                    for (const auto& b : observed.body)
                        exercised &= _finite(b.position.z) && _finite(b.spin.x) && _finite(b.velocity.z);
            }
        }
    }
    printf("loaded rig paired steps=%d mismatches=%d\n",cases,errors);
    return !errors && exercised;
}
}
