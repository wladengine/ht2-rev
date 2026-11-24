// Follow the actual chase waypoint through a three-node road. The final snap
// to a world-space point is observed at the boundary of the driving model.
extern "C" void __cdecl Ai_SetPlayerPosition(BYTE*,const double*);
namespace chase_route {
using namespace gameplay;
static LONG WINAPI Exchange(volatile LONG* target,LONG value) { return InterlockedExchange(target,value); }
static int placements;
static double placed[3];
static void __cdecl Place(BYTE* player,const double* point) {
    placements+=player==racer ? 1 : 100; memcpy(placed,point,sizeof placed);
}
struct Result {
    int placeCount, orders, waypoints;
    double point[3];
    DWORD order[4][3], waypoint[4][9];
};
bool Check() {
    F(void*,(BYTE*)0x64b2b0,0)=(void*)Exchange;  // VC6 STL allocator lock import
    Redirect((void*)0x477b60,(void*)Car); Redirect((void*)Player_FromId,(void*)Car);
    Redirect((void*)0x43dc60,(void*)Place); Redirect((void*)Ai_SetPlayerPosition,(void*)Place);
    Redirect((void*)0x61d857,(void*)malloc); Redirect((void*)0x61d1c8,(void*)free);
    BYTE nodes[3][208]={}, edges[2][128]={}, routes[3][3]={{127,0,0},{128,127,0},{128,128,127}};
    BYTE* nodeList[]={nodes[0],nodes[1],nodes[2]}; BYTE* edgeList[]={edges[0],edges[1]};
    SetWorld(0x68cf00,nodeList+0); SetWorld(0x68cf04,nodeList+3);
    SetWorld(0x68cf38,edgeList+0); SetWorld(0x68cf3c,edgeList+2);
    for (int i=0;i<3;++i) {
        F(int,nodes[i],16)=i; F(double,nodes[i],48)=i*30.0;
        F(BYTE*,nodes[i],196)=F(BYTE*,nodes[i],200)=routes[i];
        for (int dir=0;dir<2;++dir) {
            int k=dir ? i-1 : i;
            if (k<0 || k>=2) continue;
            F(BYTE**,nodes[i],20+12*dir)=edgeList+k;
            F(BYTE**,nodes[i],24+12*dir)=F(BYTE**,nodes[i],28+12*dir)=edgeList+k+1;
        }
    }
    for (int i=0;i<2;++i) {
        F(int,edges[i],12)=i;
        F(BYTE*,edges[i],112)=nodes[i]; F(BYTE*,edges[i],116)=nodes[i+1];
        for (int k=0;k<4;++k) { F(double,edges[i],48+16*k)=2.0+i+k; F(BYTE,edges[i],56+16*k)=1; }
    }
    int cases=0,errors=0;
    BYTE cargoState[160]={}, cargo[256]={}, hops[4][12], stops[4][40];
    F(BYTE*,cargo,16)=cargoState;
    for (int reverse=0;reverse<2;++reverse) for (int variant=0;variant<8;++variant) {
        Result samples[2]={};
        int start=reverse ? 2 : 0, goal=2-start;
        for (int side=0;side<2;++side) {
            memset(racer,0,sizeof racer); memset(human,0,sizeof human);
            memset(hops,0,sizeof hops); memset(stops,0,sizeof stops);
            F(int,racer,40)=-1; F(int,racer,44)=start; F(int,racer,68)=-1;
            F(int,human,40)=-1; F(int,human,44)=goal; F(int,human,68)=-1;
            F(double,racer,504)=100; F(double,human,504)=variant&1 ? 80 : 120;
            F(int,racer,84)=-1; F(BYTE*,racer,516)=cargo;
            F(BYTE,racer,496)=variant!=6; F(BYTE,racer,848)=variant!=2;
            F(int,racer,100)=variant==4 ? 5 : 0;
            F(BYTE*,racer,24)=hops[0]; F(BYTE*,racer,28)=hops[1]; F(BYTE*,racer,32)=hops[0]+sizeof hops;
            F(int,hops[0],0)=variant==0 ? goal : start;
            F(BYTE*,racer,72)=stops[0]; F(BYTE*,racer,76)=stops[1]; F(BYTE*,racer,80)=stops[0]+sizeof stops;
            memcpy(stops[0],racer+40,32); F(int,stops[0],32)=2;
            placements=0; memset(placed,0,sizeof placed);
            auto retrace=side ? Ai_RetraceCompetitorRegular : (decltype(&Ai_RetraceCompetitorRegular))0x409fc0;
            // Variant 7 asks for a new path from the other side of the graph.
            if (variant==7) F(double,human,504)=140;
            retrace((int)racer);
            Result& result=samples[side];
            result.placeCount=placements; memcpy(result.point,placed,sizeof placed);
            result.orders=(F(BYTE*,racer,28)-hops[0])/12;
            result.waypoints=(F(BYTE*,racer,76)-stops[0])/40;
            if (result.orders<0 || result.orders>4 || result.waypoints<0 || result.waypoints>4) return false;
            for (int h=0;h<result.orders;++h) {
                result.order[h][0]=F(DWORD,hops[h],0); result.order[h][1]=F(DWORD,hops[h],4);
                result.order[h][2]=F(WORD,hops[h],8) | (DWORD(F(BYTE,hops[h],10))<<16);
            }
            // Player_GoTo in 8.1 writes the 32-byte PositionId and 4-byte ID.
            // Its final four bytes come from the original stack, not the route.
            for (int w=0;w<result.waypoints;++w) memcpy(result.waypoint[w],stops[w],36);
        }
        if (memcmp(samples,samples+1,sizeof(Result)) ||
            (variant==7 && (samples[0].orders!=3 || samples[0].placeCount!=1 ||
             samples[0].order[0][0]!=(DWORD)start || samples[0].order[1][0]!=1 || samples[0].order[2][0]!=(DWORD)goal))) {
            if (++errors<=4) printf("chase route reverse=%d variant=%d hops=%d/%d stops=%d/%d snap=%d/%d\n",reverse,variant,samples[0].orders,samples[1].orders,samples[0].waypoints,samples[1].waypoints,samples[0].placeCount,samples[1].placeCount);
        }
        ++cases;
    }
    printf("chase waypoint routes cases=%d mismatches=%d\n",cases,errors);
    return !errors;
}
}

extern "C" void __cdecl Task_TrafficJam();
extern "C" int __cdecl Vehicle_Exists(int);
namespace traffic_jam {
static int __cdecl Exists(int id) { return id>=0 && id<4; }
bool Check() {
    // Only the vehicle registry is controlled. The game builds the list of
    // slow vehicles, compares their world positions, and clears temporary marks.
    Redirect((void*)0x4d8a40,(void*)Exists);
    Redirect((void*)Vehicle_Exists,(void*)Exists);
    BYTE drivers[4][768]={},head[16]={};
    gameplay::SetWorld(0x68b990,head+0);
    int cases=0,errors=0;
    for (int count : {2,3,4}) for (double separation : {5.,20.,32.})
        for (bool fastLeader : {false,true}) {
            for (int impl=0;impl<2;++impl) {
                F(BYTE*,head,0)=drivers[0];
                for (int i=0;i<count;++i) {
                    F(BYTE*,drivers[i],0)=i+1<count ? drivers[i+1] : head;
                    F(int,drivers[i],84)=i;
                    F(DWORD,drivers[i],96)=0;
                    F(double,drivers[i],344)=fastLeader && !i ? 5. : 4.;
                    F(double,drivers[i],312)=separation*i;
                }
                gameplay::SetWorld(0x68d138,BYTE(1));
                const auto detect=impl ? Task_TrafficJam : reinterpret_cast<decltype(&Task_TrafficJam)>(0x420040);
                detect();
                const bool expected=separation<20. && count-int(fastLeader)>2;
                BYTE* flag=impl ? KingBss+(0x68d138-0x68a820) : (BYTE*)0x68d138;
                if (*flag!=static_cast<BYTE>(expected)) ++errors;
                for (int i=0;i<count;++i) if (F(DWORD,drivers[i],96)!=0) ++errors;
            }
            ++cases;
        }
    printf("traffic jam groups=%d mismatches=%d\n",cases,errors);
    return errors==0;
}
}
// Follow the uncached search, then ask the same question using the tables it
// wrote. Four connected nodes form a diamond; the fifth has no connections.
struct AuditHopVector { BYTE *first, *last, *limit; };
extern "C" AuditHopVector* __cdecl Road_FindRoute(AuditHopVector*, int, int, int, double*);
extern "C" void __cdecl Ai_PoolFree(void*,unsigned);
namespace route_search {
using gameplay::SetWorld;
constexpr int nodeCount=5, edgeCount=4;
struct CostObject {
    void** methods;
    DWORD reserved, flags;
    BYTE unused[16];
    int roadClass;
    double baseCost;
};
static double __fastcall SegmentCost(CostObject* object, void*) { return object->baseCost; }
struct Sample {
    double cost;
    int count;
    BYTE hops[nodeCount][11];  // the last byte of each 12-byte hop is padding
    BYTE cache[nodeCount][2][nodeCount];
};
bool Check() {
    BYTE nodes[nodeCount][208]={}, edges[edgeCount][128]={};
    BYTE* nodeList[nodeCount], *edgeList[edgeCount];
    BYTE* adjacent[nodeCount][2][2]={};
    int degrees[nodeCount][2]={};
    BYTE cache[nodeCount][2][nodeCount];
    const int endpoints[][2]={{0,1},{1,3},{0,2},{2,3}};
    for (int i=0;i<edgeCount;++i) {
        edgeList[i]=edges[i];
        for (int dir=0;dir<2;++dir) {
            const int n=endpoints[i][dir];
            adjacent[n][dir][degrees[n][dir]++]=edges[i];
            F(BYTE*,edges[i],112+4*dir)=nodes[n];
        }
        F(int,edges[i],12)=i;
    }
    for (int n=0;n<nodeCount;++n) {
        nodeList[n]=nodes[n]; F(int,nodes[n],16)=n;
        for (int dir=0;dir<2;++dir) {
            F(BYTE**,nodes[n],20+12*dir)=adjacent[n][dir];
            F(BYTE**,nodes[n],24+12*dir)=F(BYTE**,nodes[n],28+12*dir)=adjacent[n][dir]+degrees[n][dir];
            F(BYTE*,nodes[n],196+4*dir)=cache[n][dir];
        }
    }
    SetWorld(0x68cf00,nodeList+0); SetWorld(0x68cf04,nodeList+nodeCount);
    SetWorld(0x68cf38,edgeList+0); SetWorld(0x68cf3c,edgeList+edgeCount);
    void* methods[10]={};
    methods[9]=reinterpret_cast<void*>(SegmentCost);
    CostObject objects[edgeCount]={};
    CostObject* objectPointers[edgeCount];
    for (int i=0;i<edgeCount;++i) {
        objects[i].methods=methods;
        objects[i].baseCost=i<2 ? 1.0 : 1.75;
        objectPointers[i]=objects+i;
        F(CostObject**,edges[i],120)=objectPointers+i;
        F(CostObject**,edges[i],124)=objectPointers+i+1;
    }
    int cases=0,errors=0;
    for (int side=0;side<2;++side) for (int reverse=0;reverse<2;++reverse) {
        for (int variant=0;variant<12;++variant) {
            const int from=reverse ? 3 : 0;
            const int to=variant==4 ? from : variant==3 ? 4 : 3-from;
            for (int i=0;i<edgeCount;++i) for (int k=0;k<4;++k) {
                const bool upperCheap=((variant==0) ^ (k>>1) ^ (k&1))!=0;
                F(double,edges[i],48+16*k)=variant==2 ? 2.0 : ((i<2)==upperCheap ? 1.25 : 4.5);
                F(BYTE,edges[i],56+16*k)=1;
            }
            Sample samples[2][2]={};
            for (int impl=0;impl<2;++impl) {
                if (variant>=5) {
                    // Cold object costs, then a second query against the populated tables.
                    const DWORD flags[]={0,0,0,4,8,64,0};
                    gameplay::SetWorld(0x68d138,BYTE(variant==11));
                    for (int i=0;i<edgeCount;++i) {
                        memset(edges[i]+48,0,64);
                        objects[i].flags=i<2 ? flags[variant-5] : 0;
                        objects[i].roadClass=i<2 && variant==6 ? 2 : i<2 && variant==7 ? 3 : 0;
                    }
                }
                memset(cache,255,sizeof cache);
                auto find=impl ? Road_FindRoute : (decltype(&Road_FindRoute))0x484c80;
                auto release=impl ? Ai_PoolFree : (decltype(&Ai_PoolFree))0x44b4a0;
                for (int query=0;query<2;++query) {
                    Sample& s=samples[impl][query];
                    AuditHopVector hops={};
                    if (find(&hops,side,from,to,&s.cost)!=&hops) return false;
                    s.count=hops.first ? (hops.last-hops.first)/12 : 0;
                    if (s.count<0 || s.count>nodeCount) return false;
                    for (int h=0;h<s.count;++h) memcpy(s.hops[h],hops.first+12*h,11);
                    memcpy(s.cache,cache,sizeof cache);
                    if (hops.first) release(hops.first,hops.limit-hops.first);
                }
            }
            const int expected=to==from ? 1 : to==4 ? 0 : 3;
            const auto& cold=samples[0][0];
            const bool restrictUpper=side==0 && (variant==10 || (reverse ? variant==8 : variant==9));
            const bool lowerChosen=variant==7 || restrictUpper;
            if (cold.count!=expected || memcmp(samples[0],samples[1],sizeof samples[0]) ||
                memcmp(&cold,&samples[0][1],sizeof cold) ||
                (variant>=5 && F(int,cold.hops[1],0)!=(lowerChosen ? 2 : 1)) ||
                (expected==3 && variant<2 && F(int,cold.hops[1],0)!=
                   (((variant==0)^side^reverse) ? 1 : 2))) {
                if (++errors<=5) printf("route search side=%d reverse=%d variant=%d hops=%d/%d cost=%.17g/%.17g\n",
                    side,reverse,variant,cold.count,samples[1][0].count,cold.cost,samples[1][0].cost);
            }
            cases+=2;
        }
    }
    printf("road search cold and cached queries=%d mismatches=%d\n",cases,errors);
    return !errors;
}
}

extern "C" BYTE** __cdecl Player_RandomTradePartner2(BYTE**, BYTE*, BYTE*, BYTE*, DWORD*);
extern "C" BYTE** __cdecl Player_RandomTradePartner4(BYTE**, BYTE*, BYTE*, BYTE*, DWORD*);

namespace company_racers {
extern "C" void __cdecl Player_ClearOrderLists(BYTE*);

bool CheckOrderLists() {
    constexpr int orderBytes = 144;
    constexpr int capacity = 3;
    int cases = 0;
    int errors = 0;
    for (auto clear : {reinterpret_cast<decltype(&Player_ClearOrderLists)>(0x4778a0), &Player_ClearOrderLists}) {
        for (int first : {-1, 0, 1, capacity}) for (int second : {-1, 0, 1, capacity}) {
            BYTE player[528] = {};
            BYTE sets[2][40] = {};
            BYTE orders[2][capacity * orderBytes] = {};
            const int counts[] = {first, second};
            for (int i = 0; i < 2; ++i) {
                if (counts[i] >= 0) F(BYTE*, player, 508 + 4*i) = sets[i];
                F(BYTE*, sets[i], 28) = orders[i];
                F(BYTE*, sets[i], 32) = orders[i] + orderBytes * max(0, counts[i]);
                F(BYTE*, sets[i], 36) = orders[i] + sizeof orders[i];
            }
            BYTE before[sizeof player];
            memcpy(before, player, sizeof player);
            clear(player);
            if (memcmp(before, player, sizeof player)) ++errors;
            for (int i = 0; i < 2; ++i) {
                if (F(BYTE*, sets[i], 28) != orders[i] || F(BYTE*, sets[i], 32) != orders[i] ||
                    F(BYTE*, sets[i], 36) != orders[i] + sizeof orders[i]) ++errors;
            }
            ++cases;
        }
    }
    printf("competitor order list clear cases=%d mismatches=%d\n", cases, errors);
    return !errors;
}
bool Check() {
    BYTE human[1024] = {}, drivers[2][1024] = {}, end[8] = {};
    BYTE vehicle[40] = {}, cargo[16] = {};
    BYTE* models[] = {vehicle};
    F(int, vehicle, 32) = 2;
    gameplay::SetWorld(0x68cfd8, models);
    F(BYTE*, human + 8, 508) = cargo;
    int cases = 0, errors = 0;
    for (unsigned hired = 0; hired < 4; ++hired) {
        for (unsigned available = 0; available < 4; ++available) {
            for (unsigned i = 0; i < 2; ++i) {
                BYTE* node = drivers[i];
                F(BYTE*, node, 0) = i == 0 ? drivers[1] : end;
                BYTE* player = node + 8;
                F(BYTE*, player, 96) = node;
                F(int, player, 76) = -1;
                F(int, player, 84) = 2;
                F(int, player, 92) = available & (1u << i) ? 4 : 257;
                F(BYTE*, player, 508) = cargo;
                F(BYTE*, player, 168) = hired & (1u << i) ? human : end;
            }
            for (unsigned fallback = 0; fallback < 2; ++fallback) {
                unsigned eligible = fallback ? available : available & ~hired;
                DWORD rng[2][57] = {};
                BYTE* picked[2] = {};
                for (unsigned impl = 0; impl < 2; ++impl) {
                    for (unsigned i = 0; i < 55; ++i) rng[impl][i] = 211*i + 31;
                    rng[impl][56] = 17;
                    auto choose = fallback ? Player_RandomTradePartner4 : Player_RandomTradePartner2;
                    if (impl == 0) choose = reinterpret_cast<decltype(choose)>(fallback ? 0x45e710 : 0x45e520);
                    choose(&picked[impl], drivers[0], end, human, rng[impl]);
                }
                unsigned bit = picked[0] == drivers[0] ? 1u : picked[0] == drivers[1] ? 2u : 0u;
                bool valid = eligible ? (bit & eligible) != 0 : picked[0] == end;
                if (!valid || picked[0] != picked[1] || memcmp(rng[0], rng[1], sizeof rng[0])) {
                    ++errors;
                    printf("company selection hired=%u available=%u fallback=%u selected=%u\n", hired, available, fallback, bit);
                }
                ++cases;
            }
        }
    }
    printf("company competitor selection cases=%d mismatches=%d\n", cases, errors);
    return errors == 0;
}
}
