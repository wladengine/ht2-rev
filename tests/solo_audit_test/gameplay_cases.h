// Normal game inputs for two original entry points. The surrounding registry
// and road query are controlled so the same gap and road reach both versions.
extern "C" double __cdecl Ai_TargetSpeed(BYTE*,int,double,double,double,double,int,double,double);
extern "C" int __cdecl Player_Flags(BYTE*);
extern "C" int* __cdecl Player_CarId(int*);
extern "C" DWORD* __cdecl Ai_GetPositionId(DWORD*,BYTE*);
struct LrPos { DWORD d[8]; };
extern "C" int __cdecl Road_TurnAhead(LrPos,int,double*,double*,double*,double,int);
extern "C" double __fastcall Trailer_SaddleLoad(BYTE*,void*,const float*);

namespace gameplay {
static BYTE human[1024], racer[1024];
static int flags, road, queries;
static double lookAhead;
static int __cdecl Flags(BYTE* p) { return p==racer ? flags : 0; }
static int* __cdecl Car(int* out) { *out=(int)human; return out; }
static DWORD* __cdecl Position(DWORD* out,BYTE*) {
    for (int i=0;i<8;++i) out[i]=100+i;
    return out;
}
static int __cdecl Road(LrPos pos,int player,double* turn,double* slope,double* left,double distance,int back) {
    for (int i=0;i<8;++i) if (pos.d[i]!=100u+i) return -1;
    if (player!=(int)racer || back!=(road&1)) return -1;
    ++queries; lookAhead=distance;
    *turn=0.25; *slope=-1; *left=8;
    return road;
}

bool Speed() {
    struct InputProvider { DWORD original; void* candidate; void* fixture; };
    const InputProvider providers[]={
        {0x43f620,(void*)Player_Flags,(void*)Flags},{0x568f60,(void*)Player_CarId,(void*)Car},
        {0x437520,(void*)Ai_GetPositionId,(void*)Position},{0x4cdc90,(void*)Road_TurnAhead,(void*)Road}
    };
    for (const auto& provider : providers) {
        Redirect((void*)provider.original,provider.fixture); Redirect(provider.candidate,provider.fixture);
    }
    using Target=decltype(&Ai_TargetSpeed);
    int cases=0, errors=0;
    const double gaps[]={-900,-300,-150,0,100,300,900};
    unsigned previous=_controlfp(0,0);
    for (unsigned precision : {_PC_53,_PC_64}) {
        _controlfp(precision,_MCW_PC);
        for (double gap : gaps) for (int bits : {0,0x80,0x4080,0x1000080}) for (int mask=0;mask<8;++mask) {
            flags=bits; road=mask; F(double,human,504)=1200; F(double,racer,504)=1200-gap;
            const int constrained=mask&1;
            const double room=(mask&2) ? 0 : (mask&4) ? 4 : 40;
            const double lat=mask==3 ? -12 : 0;
            auto sample=[&](Target f) { return f(racer,constrained,27.5,lat,mask&4 ? 0.25 : 2.0,0,road&1,16,room); };
            queries=0;
            double ref=sample((Target)0x4d4700), refReach=lookAhead;
            double got=sample(Ai_TargetSpeed);
            if (queries!=2 || memcmp(&ref,&got,8) || memcmp(&refReach,&lookAhead,8)) {
                if (++errors<=5) printf("speed gap=%g flags=%x road=%d precision=%x queries=%d result=%.17g/%.17g distance=%.17g/%.17g\n",gap,bits,road,precision,queries,ref,got,refReach,lookAhead);
            }
            ++cases;
        }
    }
    _controlfp(previous,_MCW_PC);
    printf("racer target speed cases=%d mismatches=%d\n",cases,errors);
    return errors==0;
}

bool Saddle() {
    static BYTE rig[10500], initial[sizeof rig];
    using Load=double (__thiscall*)(BYTE*,const float*);
    int cases=0, errors=0;
    const float directions[][3]={{0,0,1},{3,-4,1},{-1,2,0}};
    for (unsigned precision : {_PC_53,_PC_64}) {
        unsigned previous=_controlfp(0,0);
        _controlfp(precision,_MCW_PC);
        for (int wheels : {3,5}) for (int model : {0,2}) for (float offset : {-0.375f,1.25f}) for (const auto& dir : directions) {
            const float hitch[]={0.25f,-0.75f+model*0.25f,0.125f};
            const unsigned spec=0x697f9c+2416*model+168;
            memcpy((void*)spec,hitch,sizeof hitch); memcpy(KingBss+(spec-0x68a820),hitch,sizeof hitch);
            F(int,initial,2252)=model; F(int,initial,3540)=wheels;
            F(float,initial,7360)=-2.75f; F(float,initial,7384)=-1.75f; F(float,initial,7408)=0.375f;
            F(float,initial,10144)=1; F(float,initial,10168)=offset; F(float,initial,9792)=6234.25f;
            memcpy(rig,initial,sizeof rig); double ref=((Load)0x4eec80)(rig,dir);
            bool intact=!memcmp(initial,rig,sizeof rig);
            memcpy(rig,initial,sizeof rig); double got=Trailer_SaddleLoad(rig,nullptr,dir);
            if (!intact || memcmp(initial,rig,sizeof rig) || memcmp(&ref,&got,sizeof ref)) {
                if (++errors<=5) printf("saddle wheels=%d model=%d offset=%g precision=%x result=%.17g/%.17g\n",wheels,model,offset,precision,ref,got);
            }
            ++cases;
        }
        _controlfp(previous,_MCW_PC);
    }
    printf("trailer saddle load cases=%d mismatches=%d\n",cases,errors);
    return errors==0;
}
}
extern "C" double __cdecl Ai_CarTargetSpeed(int);
extern "C" double __cdecl Ai_PrescribeAdvance(int);
extern "C" bool __cdecl Ai_HumanLead(BYTE*,double*);
extern "C" int* __cdecl Player_FromId(int*);
extern "C" void __cdecl Ai_RetraceCompetitorRegular(int);
extern "C" BYTE KingData[];

namespace gameplay {
static double vehicleSpeed;
static int replans;
static double __cdecl CarSpeed(int) { return vehicleSpeed; }
static void __cdecl Replan(int player) { replans+=player==(int)racer ? 1 : 100; }
template <class T> void SetWorld(DWORD va,T value) {
    F(T,(BYTE*)va,0)=value;
    BYTE* entry=va>=0x68a820 ? KingBss+(va-0x68a820) : KingData+(va-0x66c000);
    F(T,entry,0)=value;
}

bool Advance() {
    Redirect((void*)0x477b60,(void*)Car); Redirect((void*)Player_FromId,(void*)Car);
    Redirect((void*)0x4d5c50,(void*)CarSpeed); Redirect((void*)Ai_CarTargetSpeed,(void*)CarSpeed);
    Redirect((void*)0x409fc0,(void*)Replan); Redirect((void*)Ai_RetraceCompetitorRegular,(void*)Replan);
    SetWorld(0x68a820,10.25); SetWorld(0x68d0f0,10.0);
    int cases=0, differences=0;
    BYTE cargo[256]={}, initial[sizeof racer];
    const double gaps[]={-900,-300,-150,0,150,300,900};
    for (double gap : gaps) for (int variant=0; variant<32; ++variant) {
        memset(initial,0,sizeof initial);
        F(BYTE*,initial,516)=cargo; F(int,initial,84)=-1;
        F(BYTE,initial,496)=(variant&4)==0; F(BYTE,initial,512)=variant&1;
        F(int,initial,92)=variant&2 ? 5 : 0;
        F(DWORD,initial,96)=variant==3 ? 0x1000000 : 0;
        F(double,human,504)=1000; F(double,initial,504)=1000-gap;
        vehicleSpeed=(variant&16) ? 19.25 : -1;
        SetWorld(0x68d188,variant==1 ? 1 : 0);
        SetWorld(0x66c6a0,variant&8 ? 18.0 : 2.0);
        memcpy(racer,initial,sizeof racer); replans=0;
        double ref=((decltype(&Ai_PrescribeAdvance))0x41a9d0)((int)racer);
        BYTE refRacer[sizeof racer]; memcpy(refRacer,racer,sizeof racer);
        int refPlans=replans;
        memcpy(racer,initial,sizeof racer); replans=0;
        double got=Ai_PrescribeAdvance((int)racer);
        if (memcmp(&ref,&got,8) || memcmp(refRacer,racer,sizeof racer) || refPlans!=replans) {
            if (++differences<=5) printf("advance gap=%g variant=%d distance=%.17g/%.17g replans=%d/%d flags=%x/%x\n",gap,variant,ref,got,refPlans,replans,F(DWORD,refRacer,96),F(DWORD,racer,96));
        }
        ++cases;
    }
    printf("racer advance cases=%d mismatches=%d\n",cases,differences);
    return differences==0;
}
}
extern "C" double __cdecl Vehicle_Value4cd730(int);
extern "C" int __cdecl Vehicle_Exists(int);
namespace gameplay {
static int __cdecl Exists(int id) { return id==17; }
static double __cdecl VehicleFactor(int id) { return id==17 ? 0.25 : 0.0; }

bool FilteredAdvance() {
    Redirect((void*)0x4cd730,(void*)VehicleFactor); Redirect((void*)Vehicle_Value4cd730,(void*)VehicleFactor);
    Redirect((void*)0x4d8a40,(void*)Exists); Redirect((void*)Vehicle_Exists,(void*)Exists);
    struct Tick { double distance; BYTE player[sizeof racer]; int replans; };
    int errors=0, snapshots=0;
    BYTE cargo[256]={};
    for (double dt : {0.0,0.0078125,0.03125,0.125}) for (double curve : {-6.0,0.0,4.0,40.0})
        for (int flag : {0,1}) for (double velocity : {0.0,12.5}) {
            Tick ticks[2][2]={};
            for (int side=0;side<2;++side) {
                memset(racer,0,sizeof racer); F(BYTE*,racer,516)=cargo;
                F(int,racer,84)=17; F(BYTE,racer,496)=1; F(BYTE,racer,976)=(BYTE)flag;
                F(double,racer,984)=curve; F(double,racer,352)=velocity;
                const double filter[]={0.25,0.875,-0.125,0.75,0.5};
                memcpy(racer+456,filter,sizeof filter);
                vehicleSpeed=-1; SetWorld(0x66c6a0,16.0); SetWorld(0x68d188,0);
                for (int tick=0;tick<2;++tick) {
                    replans=0;
                    SetWorld(0x68d0f0,20.0+tick*dt); SetWorld(0x68a820,20.0+(tick+1)*dt);
                    F(double,human,504)=1000; F(double,racer,504)=tick ? 1100.0 : 750.0;
                    Tick& snap=ticks[side][tick];
                    auto advance=side ? Ai_PrescribeAdvance : (decltype(&Ai_PrescribeAdvance))0x41a9d0;
                    snap.distance=advance((int)racer); memcpy(snap.player,racer,sizeof racer); snap.replans=replans;
                }
            }
            for (int tick=0;tick<2;++tick) {
                if (memcmp(&ticks[0][tick],&ticks[1][tick],sizeof(Tick))) {
                    if (++errors<=4) printf("racer filter dt=%g curve=%g flag=%d velocity=%g tick=%d result=%.17g/%.17g\n",dt,curve,flag,velocity,tick,ticks[0][tick].distance,ticks[1][tick].distance);
                }
                ++snapshots;
            }
        }
    printf("racer vehicle filter snapshots=%d mismatches=%d\n",snapshots,errors);
    return !errors;
}
}
