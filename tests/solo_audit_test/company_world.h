// Fresh process per audit, so human lookup, cargo selection and recruitment run
// without the driving-model substitutes installed by the physics suite.
extern "C" void __cdecl Ai_HireTask();
extern "C" void __cdecl Ai_AccountForGuilt(BYTE*);
extern "C" void __cdecl Ai_MarkAmbushes();
extern "C" void __cdecl Ai_CallMafia(BYTE*);
extern "C" void __cdecl Ai_ClearPlayerFromMafia(int);
extern "C" void __cdecl Ai_AddCompetitor();
extern "C" void __cdecl Player_Place(BYTE*, LrPos);
extern "C" void __cdecl Player_GoTo(BYTE*, LrPos, int);
extern "C" void __cdecl Ai_AdvancePlayer(BYTE*, double*);
extern "C" void __cdecl Ai_UpdateAdvancementRegular(BYTE*, LrPos);
extern "C" void __cdecl Road_PositionCostBest(int, LrPos, LrPos, double*);
extern "C" BYTE* __fastcall Player_Construct(BYTE*, void*, int);
extern "C" int __fastcall AI_FreeCars(void*, void*);

namespace company_world {
using gameplay::SetWorld;
constexpr int driverCount = 4;
constexpr int wallet = 136, licences = 144, staff = 160, boss = 176;
constexpr int orderSize = 144;

struct World {
    BYTE head[8], human[1100], drivers[driverCount][1100], holds[driverCount+1][64];
    BYTE equipment[19*16], model[144], prices[19*24], limits[16], roster[4*16];
    BYTE *models[1];
    DWORD logos[8];
    double accessoryPrices[3];
    double deadlines[driverCount+1][4];

    void Init() {
        memset(this, 0, sizeof *this);
        for (int i=0; i<19; ++i) F(double*,prices,i*24+12)=accessoryPrices;
        models[0]=model;
        F(double,model,48)=1024.;
        F(int,model,32)=2;
        F(int,limits,8)=50;
        SetWorld(0x68cfd8,models+0); SetWorld(0x68cc08,prices+0);
        SetWorld(0x68b990,head+0); SetWorld(0x68cc88,logos+0);
        SetWorld(0x68cf28,limits+0); SetWorld(0x68d150,20);
        SetWorld(0x68d190,42000.); SetWorld(0x68d18c,0); SetWorld(0x66c700,7);
        SetWorld(0x68a820,100.); SetWorld(0x68d188,0);
        F(BYTE*,head,0)=human; F(BYTE*,head,4)=drivers[driverCount-1];
        for (int i=0; i<=driverCount; ++i) {
            BYTE* node=i ? drivers[i-1] : human;
            F(BYTE*,node,0)=i==driverCount ? head : drivers[i];
            F(BYTE*,node,4)=i>1 ? drivers[i-2] : i ? human : head;
            F(int,node,92)=i ? 2 : 1;
            F(int,node,84)=-1; F(int,node,88)=0x12345678;
            F(int,node,100)=4;
            F(BYTE*,node,104)=node;
            F(BYTE*,node,boss)=head;
            F(int,node,156)=-1;
            F(double*,node,licences)=F(double*,node,licences+4)=deadlines[i];
            F(double*,node,licences+8)=deadlines[i]+4;
            F(BYTE*,node,516)=holds[i]; F(BYTE*,holds[i],16)=equipment;
        }
        F(BYTE*,drivers[0],staff)=F(BYTE*,drivers[0],staff+4)=roster;
        F(BYTE*,drivers[0],staff+8)=roster+sizeof roster;
    }
    void Seed(unsigned next) {
        for (int i=0; i<57; ++i) SetWorld(0x68b9c8+4*i,0u);
        SetWorld(0x68baa8,17u);
        SetWorld(0x68b9cc,next);
    }
};

// One legal order followed by a mixed list of ambushes. Drive the real AI
// generator through exact boundaries and check where each routine leaves it.
static bool CheckMafiaAttention(bool original) {
    const auto account = original ? reinterpret_cast<decltype(&Ai_AccountForGuilt)>(0x442850) : Ai_AccountForGuilt;
    const auto mark = original ? reinterpret_cast<decltype(&Ai_MarkAmbushes)>(0x42c0b0) : Ai_MarkAmbushes;
    const DWORD* rng = original ? (DWORD*)0x68b9c8 : (DWORD*)(KingBss + (0x68b9c8 - 0x68a820));
    struct Cargo { int id; BYTE flags; double price; };
    const Cargo loads[] = {{-1,0,0.}, {0,0x10,14230.88}, {1,2,9999.99},
                           {1,2,10000.}, {29,0x12,14230.88}};
    int cases = 0, errors = 0;
    for (const Cargo& cargo : loads) for (double share : {.15,.15001,.16619,.20,.20001})
        for (double debt : {0.,1.}) for (unsigned first : {199999u,200000u,299999u,300000u,499999u,500000u}) {
            World w;
            w.Init();
            w.Seed(first);
            const unsigned draws[] = {first,299999,300000,1000000};
            for (int i=1; i<4; ++i) SetWorld(0x68b9cc + 4*i,draws[i]);
            BYTE order[orderSize] = {}, goods[30*104] = {};
            SetWorld(0x68ccb8,goods+0);
            if (cargo.id >= 0) goods[104*cargo.id+48] = cargo.flags;
            F(int,order,0) = cargo.id;
            F(int,order,4) = -1;
            F(double,order,64) = cargo.price;
            F(BYTE*,w.holds[0],28) = order;
            F(BYTE*,w.holds[0],32) = F(BYTE*,w.holds[0],36) = order + (cargo.id >= 0 ? orderSize : 0);
            F(double,w.human,768) = share;
            F(double,w.human,224) = debt;
            w.human[200] = w.human[201] = w.human[264] = 1;
            account(w.human);
            const bool rollsForCargo = (cargo.flags & 2) && share > .15;
            unsigned used = rollsForCargo ? 1u : 0u;
            const bool noticed = rollsForCargo && first >= (cargo.price < 10000. ? 500000u : 200000u);
            bool ok = !w.human[200] && w.human[201] == BYTE(noticed) && !w.human[264] && rng[55] == used;

            BYTE sites[4][244] = {};
            BYTE* list[] = {sites[0],sites[1],sites[2],sites[3]};
            const int kinds[] = {1,0,2,1};
            for (int i=0; i<4; ++i) {
                F(int,sites[i],36) = kinds[i];
                sites[i][240] = 1;
            }
            SetWorld(0x68a970,list+0);
            SetWorld(0x68a974,list+4);
            mark();
            for (int i=0; i<4; ++i) {
                bool eligible = kinds[i] == 2 || (kinds[i] == 1 && share > .2 && (noticed || debt > 0.));
                BYTE expected = 0;
                if (eligible) {
                    expected = draws[used] % 1000000 < 300000;
                    ++used;
                }
                if (sites[i][240] != expected) ok = false;
            }
            if (!ok || rng[55] != used || rng[56] != 17+used) {
                if (!errors) printf("first mafia mismatch original=%d cargo=%d share=%.5f debt=%.0f draw=%u noticed=%d/%d indices=%u,%u expected=%u,%u marks=%u%u%u%u\n",
                    original,cargo.id,share,debt,first,w.human[201],noticed,rng[55],rng[56],used,17+used,
                    sites[0][240],sites[1][240],sites[2][240],sites[3][240]);
                ++errors;
            }
            ++cases;
        }
    printf("mafia cargo and ambushes original=%d cases=%d mismatches=%d\n",original,cases,errors);
    return errors == 0;
}

#include "company_endings.h"

static bool CheckAwards(bool original) {
    using Award=void (__cdecl*)(BYTE*);
    const auto award=original ? reinterpret_cast<Award>(0x429850) : Ai_MaybeGrantDeliveryLicence;
    int errors=0, cases=0;
    for (double share : {0.,.1,.10001,.2,.20001}) {
        for (unsigned draw=0; draw<24; ++draw) {
            World w;
            w.Init(); w.Seed(draw);
            F(double,w.human,768)=share;
            award(w.drivers[0]);
            int divisor=share>.2 ? 6 : share>.1 ? 12 : 24;
            bool issued=draw%divisor==0;
            if (F(double*,w.human,148)!=w.deadlines[0] ||
                F(double*,w.drivers[0],148)!=w.deadlines[1]+int(issued) ||
                (issued && w.deadlines[1][0]!=400.)) ++errors;
            ++cases;
        }
    }
    printf("ordinary delivery licences original=%d cases=%d mismatches=%d\n",original,cases,errors);
    return !errors;
}

enum class HireCase { eligible, noLicence, earlyClock, exactFunds, atHireMarket, atCompanyLimit };
constexpr unsigned hiringSeeds=8;
static int staffAfterTick[6][hiringSeeds];

static bool CheckHireLimit(bool original) {
    World w; w.Init();
    F(int,w.limits,8)=50; // standard ENV/GAME.CNF: 50 traders, 20 places for hire
    const auto limit=original ? reinterpret_cast<decltype(&AI_FreeCars)>(0x429340) : AI_FreeCars;
    struct Point { double elapsed; int limit; };
    const Point points[]={{0.,0},{1399.,0},{1400.,1},{1401.,1},{21000.,15},{42000.,30},{84000.,30}};
    int errors=0;
    for(const auto& point : points) {
        SetWorld(0x68d190,point.elapsed);
        if (limit(nullptr,nullptr)!=point.limit) ++errors;
    }
    printf("company growth limit original=%d cases=7 mismatches=%d\n",original,errors);
    return !errors;
}

static bool CheckHiring(bool original) {
    const auto hireTask=original ? reinterpret_cast<decltype(&Ai_HireTask)>(0x429930) : Ai_HireTask;
    const auto award=original ? reinterpret_cast<decltype(&Ai_MaybeGrantDeliveryLicence)>(0x429850) : Ai_MaybeGrantDeliveryLicence;
    int errors=0, hires=0, attempts=0;
    for (HireCase variant : {HireCase::eligible, HireCase::noLicence, HireCase::earlyClock,
                             HireCase::exactFunds, HireCase::atHireMarket, HireCase::atCompanyLimit}) {
        for (unsigned seed=0; seed<hiringSeeds; ++seed) {
            World w;
            w.Init();
            BYTE* founder=w.drivers[0];
            const double cost=1034.; // vehicle value 1024 + 10 times the driver advance of 1
            F(double,founder,wallet)=variant==HireCase::exactFunds ? cost : cost+100.;
            w.Seed(0);
            if (variant!=HireCase::noLicence) award(founder);
            if (variant==HireCase::earlyClock) SetWorld(0x68d190,0.);
            if (variant==HireCase::atHireMarket) F(int,founder,100)=257;
            if (variant==HireCase::atCompanyLimit) SetWorld(0x68d18c,7);
            w.Seed(seed);
            hireTask();
            const bool hired=F(BYTE*,founder,staff+4)!=w.roster;
            if (hired) {
                ++hires;
                BYTE* employee=F(BYTE*,w.roster,0);
                if (variant!=HireCase::eligible || F(BYTE*,employee,boss)!=founder || F(double,employee,wallet)!=cost ||
                    F(double,founder,wallet)!=100. || F(double*,founder,148)!=w.deadlines[1] ||
                    F(int,founder,156)<0 || F(int,founder,156)>7) ++errors;
            }
            int selected=-1;
            for (int i=1; i<driverCount; ++i)
                if (F(BYTE*,w.drivers[i],boss)==founder) selected=i;
            if (original) staffAfterTick[static_cast<unsigned>(variant)][seed]=selected;
            else if (selected!=staffAfterTick[static_cast<unsigned>(variant)][seed]) ++errors;
            if (!hired && (selected!=-1 || F(double*,founder,148)!=w.deadlines[1]+int(variant!=HireCase::noLicence) ||
                F(double,founder,wallet)!=(variant==HireCase::exactFunds ? cost : cost+100.))) ++errors;
            ++attempts;
        }
    }
    printf("AI first hire original=%d cases=%d schedulerHires=%d mismatches=%d\n",original,attempts,hires,errors);
    return hires==6 && errors==0;
}

// Route geometry is outside this contract. Capture the dispatches and retain
// the actual dispatcher, both selectors, order handling and racer registration.
static double routeCost;
static BYTE* placed;
static int target, placementNode, advances, snaps;
static void __cdecl Cost(int, LrPos, LrPos, double* value) { *value=routeCost; }
static void __cdecl Place(BYTE* p,LrPos pos) {
    placed=p;
    placementNode=pos.d[1];
    memcpy(p+40,&pos,sizeof pos);
}
static void __cdecl Go(BYTE*, LrPos pos, int) { target=pos.d[1]; }
static void __cdecl Snap(BYTE*, const double*) { ++snaps; }
static void __cdecl Advance(BYTE*, double*) { ++advances; }
static void __cdecl Recalculate(BYTE*, LrPos) {}
static void __cdecl UpdateLoad(int) {}

struct Dispatch {
    BYTE* drivers[2];
    int count;
    bool wrongDestination;
};
static Dispatch dispatch;
static void __cdecl RecordChase(BYTE* driver, LrPos pos, int id) {
    if (dispatch.count < 2) dispatch.drivers[dispatch.count] = driver;
    ++dispatch.count;
    if (id != 2 || pos.d[0] != DWORD(-1) || pos.d[1] != 0) dispatch.wrongDestination = true;
}

static bool CheckDispatch(bool original) {
    const auto call = original ? reinterpret_cast<decltype(&Ai_CallMafia)>(0x40aa00) : Ai_CallMafia;
    const auto clear = original ? reinterpret_cast<decltype(&Ai_ClearPlayerFromMafia)>(0x427090) : Ai_ClearPlayerFromMafia;
    int cases = 0, errors = 0;
    for (int mask=0; mask<16; ++mask) for (bool tied : {false,true}) for (bool skip : {false,true}) {
        World w;
        w.Init();
        BYTE nodes[5][208] = {};
        BYTE* list[] = {nodes[0],nodes[1],nodes[2],nodes[3],nodes[4]};
        SetWorld(0x68cf00,list+0); SetWorld(0x68cf04,list+5);
        F(int,w.human,40) = -1;
        F(int,w.human,100) = skip ? 5 : 4;
        F(BYTE*,w.human,968) = w.human;
        const int distances[] = {30,10,tied ? 10 : 20,40};
        const int nearest[] = {1,2,0,3};
        for (int i=0; i<4; ++i) {
            BYTE* d = w.drivers[i];
            F(int,d,92) = 6;
            F(int,d,96) = (mask & (1<<i)) ? 0 : (i%2 ? 4 : 0x80000);
            F(int,d,40) = -1; F(int,d,44) = i+1;
            F(double,nodes[i+1],48) = distances[i];
            F(BYTE*,d,968) = w.drivers[0]; // An unrelated target must survive cleanup.
        }
        bool ok = true;
        bool assigned[4] = {};
        for (int pass=0; pass<3; ++pass) {
            dispatch = {};
            call(w.human);
            int count = 0;
            for (int i : nearest) {
                BYTE* d = w.drivers[i];
                if (skip || F(int,d,96) || count==2) continue;
                if (dispatch.drivers[count]!=d || F(BYTE*,d,968)!=w.human) ok=false;
                assigned[i]=true;
                ++count;
                // The first repeat keeps the candidates unchanged. Then model
                // the two dispatched drivers becoming unavailable on the road.
                if (pass==1) F(int,d,96) |= 4;
            }
            if (dispatch.count!=count || dispatch.wrongDestination) ok=false;
            for (int i=0; i<4; ++i)
                if (F(BYTE*,w.drivers[i],968)!=(assigned[i] ? w.human : w.drivers[0])) ok=false;
        }
        clear((int)w.human);
        for (int i=0; i<4; ++i)
            if (F(BYTE*,w.drivers[i],968)!=(assigned[i] ? w.head : w.drivers[0])) ok=false;
        if (F(BYTE*,w.human,968)!=w.human) ok=false;
        if (!ok) {
            if (!errors) printf("dispatch mismatch original=%d mask=%d tied=%d skip=%d\n",original,mask,tied,skip);
            ++errors;
        }
        ++cases;
    }
    printf("mafia dispatch and release original=%d cases=%d mismatches=%d\n",original,cases,errors);
    return !errors;
}
static void Hooks() {
    struct Hook { DWORD original; void* rebuilt; void* observer; };
    const Hook hooks[]={
        {0x487500,(void*)Road_PositionCostBest,(void*)Cost},
        {0x4786a0,(void*)Player_Place,(void*)Place},
        {0x478260,(void*)Player_GoTo,(void*)Go},
        {0x43dc60,(void*)Ai_SetPlayerPosition,(void*)Snap},
        {0x435a90,(void*)Ai_AdvancePlayer,(void*)Advance},
        {0x41a7d0,(void*)Ai_UpdateAdvancementRegular,(void*)Recalculate},
        {0x442c60,(void*)Ai_AccountForChangeInLoadState,(void*)UpdateLoad}
    };
    for(const auto& h : hooks) {
        Redirect((void*)h.original,h.observer);
        Redirect(h.rebuilt,h.observer);
    }
}

static bool CheckStart(bool original) {
    const auto start=original ? reinterpret_cast<decltype(&Ai_AddCompetitor)>(0x4262a0) : Ai_AddCompetitor;
    int errors=0, cases=0;
    for (bool special : {false,true}) for (bool independent : {false,true}) for (bool trailer : {false,true}) for (bool largeBody : {false,true}) {
        World w; w.Init(); w.Seed(2);
        BYTE manifest[orderSize]={}, oldOrders[2][2*orderSize]={}, spareHold[64]={};
        BYTE cargo[2*104]={}, town[96]={}, roadNodes[2][208]={};
        BYTE *towns[]={town,town}, *nodes[]={roadNodes[0],roadNodes[1]}, *racers[4]={};
        BYTE* employee=w.drivers[0];
        BYTE* recruit=independent ? w.drivers[1] : employee;
        F(BYTE*,employee,boss)=w.human;
        F(BYTE*,w.human,staff)=w.roster; F(BYTE*,w.human,staff+4)=w.roster+16;
        F(BYTE*,w.roster,0)=employee;
        F(double,w.human,wallet)=7000.;
        F(double,employee,544)=30.;
        F(double,employee,760)=40.;
        for (int i=1; i<driverCount; ++i) F(int,w.drivers[i],100)=(independent && i==1) ? 4 : 257;
        F(int,manifest,0)=1; F(int,manifest,8)=87; F(int,manifest,12)=1;
        F(int,manifest,24)=-1; F(int,manifest,28)=0; F(int,manifest,52)=-1;
        F(double,manifest,56)=8.; F(double,manifest,64)=800.;
        F(double,manifest,72)=155.; F(double,manifest,96)=.75; F(int,manifest,104)=1;
        F(BYTE*,w.holds[0],28)=manifest;
        F(BYTE*,w.holds[0],32)=F(BYTE*,w.holds[0],36)=manifest+orderSize;
        BYTE* primary=F(BYTE*,recruit,516);
        for(int i=0;i<2;++i) {
            BYTE* hold=i ? spareHold : primary;
            F(BYTE*,hold,28)=oldOrders[i]; F(BYTE*,hold,32)=oldOrders[i]+orderSize;
            F(BYTE*,hold,36)=oldOrders[i]+sizeof oldOrders[i];
            F(int,oldOrders[i],8)=900+i;
        }
        if (trailer) { F(BYTE*,recruit,520)=spareHold; F(BYTE*,spareHold,16)=w.equipment; }
        F(int,w.human,40)=-1; F(int,w.human,44)=1; F(int,town,64)=1;
        F(double,cargo,104+16)=2.; F(double,cargo,104+24)=4.;
        SetWorld(0x68ccb8,cargo+0); SetWorld(0x68cec0,towns+0);
        SetWorld(0x68cf00,nodes+0); SetWorld(0x68cf04,nodes+2);
        SetWorld(0x68a980,racers+0); SetWorld(0x68a984,racers+0); SetWorld(0x68a988,racers+4);
        SetWorld(0x68d164,0); SetWorld(0x68d15c,1u); SetWorld(0x68d160,1u);
        SetWorld(0x697914+100,40.f); SetWorld(0x697914+108,largeBody ? 32.f : 16.f); SetWorld(0x697914+112,1.f);
        SetWorld(0x68bab0,50000.); // ordinary start already outside camera range
        routeCost=special ? 1001. : 1000.;
        placed=nullptr; placementNode=target=-1; advances=snaps=0;
        start();
        BYTE* active=trailer ? spareHold : primary;
        BYTE* order=F(BYTE*,active,28);
        double quantity=(trailer ? 8. : 4.)*(largeBody ? 2. : 1.);
        if (placed!=recruit || placementNode!=0 || target!=1 || racers[0]!=recruit ||
            snaps!=int(special) || advances!=int(special) || F(int,recruit,100)!=4 ||
            F(BYTE,recruit,496)!=1 || F(BYTE*,employee,boss)!=w.human || F(BYTE*,w.roster,0)!=employee ||
            F(BYTE*,active,32)!=order+orderSize || (trailer && F(BYTE*,primary,32)!=oldOrders[0]) ||
            F(int,order,0)!=1 || F(int,order,8)!=87 || F(int,order,12)!=1 || F(int,order,104)!=1 ||
            F(double,order,72)!=155. || F(double,order,96)!=.75 ||
            F(double,order,56)!=quantity || F(double,order,64)!=quantity*100. || F(double,manifest,56)!=8. ||
            F(double,manifest,64)!=800. || F(double,recruit,wallet)!=0. || F(double,w.human,wallet)!=7000. ||
            F(double,employee,544)!=30. || F(double,employee,760)!=40.) {
            ++errors;
            printf("start original=%d special=%d independent=%d trailer=%d selected=%d placed=%d target=%d listed=%d snaps=%d advances=%d units=%g price=%g\n",
                original,special,independent,trailer,placed==recruit,placementNode,target,racers[0]==recruit,snaps,advances,F(double,order,56),F(double,order,64));
        }
        ++cases;
    }
    printf("campaign competitor starts original=%d cases=%d mismatches=%d\n",original,cases,errors);
    return !errors;
}

static bool CheckNewDriver(bool original) {
    World w; w.Init();
    BYTE storage[1100]; memset(storage,0xcd,sizeof storage);
    const char name[]="AI_01";
    BYTE* p=storage+8;
    if(original) reinterpret_cast<BYTE* (__thiscall*)(BYTE*,int)>(0x4729e0)(p,(int)name);
    else Player_Construct(p,nullptr,(int)name);
    bool ok=F(BYTE*,storage,boss)==w.head && F(BYTE*,storage,staff)==F(BYTE*,storage,staff+4) &&
        F(BYTE*,storage,licences)==F(BYTE*,storage,licences+4) && F(int,storage,156)==-1;
    printf("new driver original=%d noEmployerStaffLicences=%d\n",original,ok);
    return ok;
}

// Observe an actual goods delivery with both the award rule and its RNG live.
// Leave HUD and unrelated post-load scheduling outside this fixture.
static bool CheckUnload(bool original) {
    const auto unload=original ? reinterpret_cast<decltype(&Ai_LoadOrder)>(0x443d50) : Ai_LoadOrder;
    int errors=0, cases=0;
    for(bool racer : {false,true}) for(bool alreadyFinished : {false,true})
        for(bool listed : {false,true}) for(unsigned roll : {0u,1u}) {
            World w; w.Init(); w.Seed(19); // first draw sets the race licence's lifetime
            SetWorld(0x68b9c8+2*4,roll);  // second draw is the ordinary delivery lottery
            BYTE town[96]={}, entry[16]={}, record[104]={}, order[orderSize]={};
            BYTE* towns[]={town}; BYTE* racers[]={w.drivers[0]};
            SetWorld(0x68cec0,towns+0); SetWorld(0x68cec4,towns+1);
            SetWorld(0x68ccb8,record+0);
            SetWorld(0x68a980,racers+0); SetWorld(0x68a984,racers+int(listed));
            SetWorld(0x68d164,int(alreadyFinished)); SetWorld(0x68d115,BYTE(1));
            F(int,town,24)=52; F(BYTE*,town,28)=entry; F(BYTE*,town,32)=entry+16;
            F(BYTE,entry,4)=1; F(double,order,56)=1.; F(double,order,64)=25.; F(double,order,96)=1.;
            BYTE* driver=w.drivers[0];
            F(BYTE,driver,496)=racer;
            F(BYTE*,w.holds[1],28)=order;
            F(BYTE*,w.holds[1],32)=F(BYTE*,w.holds[1],36)=order+sizeof order;
            double quantity=-100.; int index=-1;
            const int result=unload(driver,52,0,&quantity,&index);
            bool awarded=racer ? !alreadyFinished && listed : roll==0;
            double expires=racer ? 479. : 400.;
            if (result || F(double,driver,wallet)!=25. || F(BYTE*,w.holds[1],32)!=order ||
                F(double*,w.human,148)!=w.deadlines[0] || F(double*,driver,148)!=w.deadlines[1]+int(awarded) ||
                (awarded && w.deadlines[1][0]!=expires)) ++errors;
            ++cases;
        }
    printf("delivery to licence original=%d cases=%d mismatches=%d\n",original,cases,errors);
    return !errors;
}

static bool Check() {
    F(void*,(BYTE*)0x64b2b0,0)=(void*)chase_route::Exchange;
    Redirect((void*)0x61d857,(void*)malloc);
    Redirect((void*)0x61d1c8,(void*)free);
    const bool mafiaRef=CheckMafiaAttention(true);
    const bool mafiaGot=CheckMafiaAttention(false);
    const bool dissolutionRef=CheckDissolution(true);
    const bool dissolutionGot=CheckDissolution(false);
    const bool ref=CheckAwards(true);
    const bool got=CheckAwards(false);
    const bool limitRef=CheckHireLimit(true);
    const bool limitGot=CheckHireLimit(false);
    const bool hireRef=CheckHiring(true);
    const bool hireGot=CheckHiring(false);
    const bool initRef=CheckNewDriver(true);
    const bool initGot=CheckNewDriver(false);
    Redirect((void*)0x478260,(void*)RecordChase);
    Redirect((void*)Player_GoTo,(void*)RecordChase);
    const bool pursuitRef=CheckDispatch(true);
    const bool pursuitGot=CheckDispatch(false);
    Hooks();
    Redirect((void*)0x40e270,(void*)employee_delivery::Text);
    Redirect((void*)Message_Text,(void*)employee_delivery::Text);
    Redirect((void*)0x483cb0,(void*)employee_delivery::Show);
    Redirect((void*)displayScreenMessage,(void*)employee_delivery::Show);
    const bool unloadRef=CheckUnload(true);
    const bool unloadGot=CheckUnload(false);
    const bool startRef=CheckStart(true);
    const bool startGot=CheckStart(false);
    return dissolutionRef && dissolutionGot && pursuitRef && pursuitGot && mafiaRef && mafiaGot && ref && got && limitRef && limitGot && hireRef && hireGot && initRef && initGot && unloadRef && unloadGot && startRef && startGot;
}
}
