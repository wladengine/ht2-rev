// Exercise the real town unloading branch, with a player-owned haulier, a
// resident order for another town, and a callback observing the settled books.
extern "C" int __cdecl Ai_LoadOrder(BYTE*,int,unsigned,double*,int*);
extern "C" int __cdecl Ai_ActOnArrival(BYTE*);
extern "C" void __cdecl Ai_SendForSomething(int);
extern "C" void __cdecl Ai_MaybeGrantDeliveryLicence(BYTE*);
extern "C" void __cdecl Ai_AccountForChangeInLoadState(int);
extern "C" const char* __cdecl Message_Text(const char*);
extern "C" void __cdecl displayScreenMessage(const char*);

namespace employee_delivery {
// Offsets in the list node, including its eight-byte list header.
constexpr int occupation = 100;
constexpr int wallet = 136;
constexpr int employer = 176;
constexpr int companyEarnings = 544;
constexpr int freightRevenue = 760;
constexpr int deliveryOccupation = 4;
constexpr int otherTown = 7;
constexpr double otherTownPrice = 825.;
static BYTE owner[1100], driver[1100], sentinel[1100];
static int messages, updates, receipts;
static int dispatches, gateCalls;
static bool permitUnload;
static double eventSum, eventBalance;
static const char* __cdecl Text(const char* id) { return id; }
static void __cdecl Show(const char* id) { if (!strcmp(id,"COMPETITOR_ARRIVED")) ++messages; }
static void __cdecl Updated(int node) { if (node==(int)driver) ++updates; }
static void __cdecl NoNewArrival(BYTE*) {}
static void __cdecl NextJob(int node) {
    if (node==(int)driver && F(int,driver,occupation)==0) ++dispatches;
}
static int __cdecl Event(BYTE* node,int kind,int payload) {
    if (node==driver && kind==10) { ++gateCalls; return permitUnload; }
    if (node!=driver || kind!=11 || !payload) return 0;
    ++receipts;
    eventSum=F(double,(BYTE*)payload,32);
    eventBalance=F(double,owner,wallet);
    return 1;
}

static bool Case(bool ref,bool hired,bool racer,int previous,bool damaged,bool toTrailer) {
    constexpr int stride=144;
    BYTE town[96]={}, entry[16]={}, vehicle[160]={}, cargoType[104]={}, equipment[160]={};
    BYTE container[2][64]={}, orders[3*stride]={}, elsewhere[stride]={};
    BYTE* towns[]={town};
    BYTE* models[]={vehicle};
    memset(driver,0,sizeof driver);
    memset(owner,0,sizeof owner);
    F(int,town,24)=52;
    F(BYTE*,town,28)=entry; F(BYTE*,town,32)=entry+16;
    F(BYTE,entry,4)=1;
    F(int,vehicle,32)=2;
    F(double,cargoType,40)=100;
    gameplay::SetWorld(0x68cec0,towns+0); gameplay::SetWorld(0x68cec4,towns+1);
    gameplay::SetWorld(0x68cfd8,models+0); gameplay::SetWorld(0x68ccb8,cargoType+0);
    gameplay::SetWorld(0x68b990,sentinel+0);
    gameplay::SetWorld(0x68d164,previous);
    gameplay::SetWorld(0x68a980,0); gameplay::SetWorld(0x68a984,0);
    gameplay::SetWorld(0x68d118,&Event);
    gameplay::SetWorld(0x68d188,0);
    F(DWORD,driver,88)=0x12345678;
    F(int,driver,92)=2; F(int,driver,occupation)=deliveryOccupation;
    F(int,owner,92)=1;
    F(BYTE,driver,496)=racer; F(BYTE,driver,800)=1;
    F(BYTE*,driver,employer)=hired ? owner : sentinel;
    F(double,driver,wallet)=750; F(double,owner,wallet)=10000;
    F(double,driver,companyEarnings)=80; F(double,driver,freightRevenue)=30;
    F(BYTE*,driver,516)=container[0];
    if (toTrailer) F(BYTE*,driver,520)=container[1];
    F(BYTE*,container[0],16)=equipment;
    F(BYTE*,container[1],16)=equipment;
    if (toTrailer) {
        F(BYTE*,container[0],28)=elsewhere;
        F(BYTE*,container[0],32)=F(BYTE*,container[0],36)=elsewhere+stride;
    }
    BYTE* active=container[toTrailer];
    F(BYTE*,active,28)=orders;
    F(BYTE*,active,32)=F(BYTE*,active,36)=orders+sizeof orders;
    for (int i=0; i<3; ++i) {
        BYTE* order=orders+i*stride;
        F(int,order,12)=i==1 ? otherTown : 0;
        F(int,order,104)=i==0 ? 1 : 0;
        F(double,order,56)=4;
        constexpr double prices[] = {1000., otherTownPrice, 300.};
        F(double,order,64)=prices[i];
        F(double,order,96)=damaged && i==0 ? .75 : 1.;
    }
    const double earned=1300.+(racer && previous==0 ? 500. : 0.)-(damaged ? 50. : 0.);
    auto unload=ref ? reinterpret_cast<decltype(&Ai_LoadOrder)>(0x443d50) : Ai_LoadOrder;
    double quantity=-1000000.;
    int index=0;
    auto arrive=ref ? reinterpret_cast<decltype(&Ai_ActOnArrival)>(0x419cb0) : Ai_ActOnArrival;
    receipts=updates=messages=dispatches=gateCalls=0;
    eventSum=eventBalance=0.;
    permitUnload=false;
    arrive(driver);
    if (receipts || updates || dispatches || gateCalls!=1 || F(int,driver,occupation)!=deliveryOccupation ||
        F(BYTE*,active,32)!=orders+sizeof orders || F(double,owner,wallet)!=10000. ||
        F(double,driver,wallet)!=750. || F(double,driver,companyEarnings)!=80. ||
        F(double,driver,freightRevenue)!=30.) return false;
    permitUnload=true;
    for (int repeat=0;repeat<2;++repeat) {
        const int status=repeat ? unload(driver,52,0,&quantity,&index) : arrive(driver);
        int arrivals=ref ? *reinterpret_cast<int*>(0x68d164) : F(int,KingBss,0x68d164-0x68a820);
        if (status || (repeat && index!=-1) || dispatches!=1 || gateCalls!=2 || F(int,driver,occupation)!=0 ||
            F(BYTE*,driver,employer)!=(hired ? owner : sentinel) ||
            F(BYTE*,active,32)!=orders+stride || F(int,orders,12)!=otherTown || F(double,orders,64)!=otherTownPrice ||
            (toTrailer && F(BYTE*,container[0],32)!=elsewhere+stride) ||
            F(double,owner,wallet)!=10000.+(hired ? earned : 0.) ||
            F(double,driver,wallet)!=750.+(hired ? 0. : earned) ||
            F(double,driver,companyEarnings)!=80.+(hired ? earned : 0.) ||
            F(double,driver,freightRevenue)!=30.+earned || receipts!=1 || eventSum!=earned ||
            eventBalance!=F(double,owner,wallet) || messages!=int(racer)*(1+repeat) ||
            arrivals!=previous+int(racer)*(1+repeat) || updates!=1+repeat) {
            printf("delivery ref=%d hired=%d racer=%d previous=%d repeat=%d status=%d orders=%d accounts=%g/%g/%g revenue=%g receipt=%d:%g place=%d messages=%d updates=%d\n",
                ref,hired,racer,previous,repeat,status,int((F(BYTE*,active,32)-orders)/stride),F(double,owner,wallet),
                F(double,driver,wallet),F(double,driver,companyEarnings),F(double,driver,freightRevenue),receipts,eventSum,arrivals,messages,updates);
            return false;
        }
    }
    if (hired && racer && toTrailer) {
        printf("employee receipt original=%d previous=%d integrity=%g payout=%g company=%g driver=%g remaining=%d\n",
            ref, previous, damaged ? .75 : 1., eventSum, eventBalance, F(double,driver,wallet),
            int((F(BYTE*,active,32)-orders)/stride));
    }
    return true;
}

static bool Check() {
    Redirect((void*)0x4194e0,(void*)NextJob); Redirect((void*)Ai_SendForSomething,(void*)NextJob);
    Redirect((void*)0x40e270,(void*)Text); Redirect((void*)Message_Text,(void*)Text);
    Redirect((void*)0x483cb0,(void*)Show); Redirect((void*)displayScreenMessage,(void*)Show);
    Redirect((void*)0x442c60,(void*)Updated); Redirect((void*)Ai_AccountForChangeInLoadState,(void*)Updated);
    Redirect((void*)0x429850,(void*)NoNewArrival); Redirect((void*)Ai_MaybeGrantDeliveryLicence,(void*)NoNewArrival);
    int cases=0, errors=0;
    for (bool ref : {true,false}) for (bool hired : {true,false}) for (bool racer : {true,false})
        for (int previous : {0,2}) for (bool damaged : {true,false}) for (bool trailer : {true,false}) {
            ++cases;
            if(!Case(ref,hired,racer,previous,damaged,trailer)) ++errors;
        }
    printf("employee delivery cases=%d mismatches=%d\n",cases,errors);
    return !errors;
}
}
