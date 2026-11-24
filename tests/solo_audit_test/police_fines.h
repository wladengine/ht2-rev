extern "C" int __cdecl Ai_CleanBeforeLaw(BYTE*);
extern "C" int __cdecl Ai_FineForSpeeding(int,double);
extern "C" void __cdecl AI_AddToBanner(double);
extern "C" void __cdecl eventBanner(int,char*);
extern "C" void __cdecl Copter_ApplyCargo();

namespace police_fines {
static double amount;
static int kind, banners;
static void __cdecl Amount(double v) { amount+=v; }
static void __cdecl Banner(int id,char*) { kind=id; ++banners; }
static void __cdecl RefreshVehicle() {}
static bool Check(bool original) {
    using company_world::World;
    using gameplay::SetWorld;
    const auto clean=original ? reinterpret_cast<decltype(&Ai_CleanBeforeLaw)>(0x430a80) : Ai_CleanBeforeLaw;
    const auto camera=original ? reinterpret_cast<decltype(&Ai_FineForSpeeding)>(0x431170) : Ai_FineForSpeeding;
    int cases=0, errors=0;
    for(unsigned mask=0; mask<32; ++mask) for(double direct : {0.,250.,500.,750.}) {
        World w; w.Init(); w.Seed(7);
        BYTE* empty=nullptr;
        SetWorld(0x68a970,&empty); SetWorld(0x68a974,&empty);
        SetWorld(0x68a980,&empty); SetWorld(0x68a984,&empty);
        BYTE* p=w.human;
        F(double,p,136)=10000.;
        F(int,w.drivers[0],92)=5;
        F(BYTE*,w.drivers[0],968)=p;
        F(int,w.drivers[1],92)=5;
        F(BYTE*,w.drivers[1],968)=w.drivers[2];
        const int offsets[]={208,240,248,256};
        const int events[]={14,10,12,11,13};
        const double individual[]={125.5,250.,75.,60.25,1024.*.8};
        double expected=0;
        int count=0, last=0;
        for(int i=0; i<5; ++i) if (mask&(1u<<i)) {
            if(i<4) F(double,p,offsets[i])=individual[i];
            else { p[264]=1; F(DWORD,w.holds[0],40)=2; }
            expected+=individual[i]; ++count; last=events[i];
        }
        banners=kind=0; amount=0;
        bool ok=camera((int)p,direct)==0 && F(double,p,136)==10000.-direct &&
                banners==int(direct!=0.) && amount==direct && kind==(direct ? 16 : 0);
        banners=kind=0; amount=0;
        if (clean(p)!=0 || F(double,p,136)!=10000.-direct-expected || banners!=int(count!=0) ||
            kind!=(count>1 ? 15 : last) || amount!=expected || p[264] || (F(DWORD,w.holds[0],40)&2) ||
            F(BYTE*,w.drivers[0],968)!=(count ? w.head : p) ||
            F(BYTE*,w.drivers[1],968)!=w.drivers[2]) ok=false;
        for (int off : offsets) if(F(double,p,off)!=0.) ok=false;
        if(!ok && !errors) printf("police mismatch original=%d mask=%u direct=%g cash=%g banner=%d amount=%g expected=%g\n",
            original,mask,direct,F(double,p,136),kind,amount,expected);
        if(!ok) ++errors;
        ++cases;
    }
    printf("traffic fines original=%d cases=%d mismatches=%d\n",original,cases,errors);
    return !errors;
}
static bool Both() {
    Redirect((void*)0x483e60,(void*)Amount); Redirect((void*)AI_AddToBanner,(void*)Amount);
    Redirect((void*)0x483e80,(void*)Banner); Redirect((void*)eventBanner,(void*)Banner);
    Redirect((void*)0x56f6d0,(void*)RefreshVehicle); Redirect((void*)Copter_ApplyCargo,(void*)RefreshVehicle);
    const bool ref=Check(true), got=Check(false);
    return ref && got;
}
}
