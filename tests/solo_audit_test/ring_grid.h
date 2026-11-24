extern "C" void __cdecl Ai_FillStartGrid();
extern "C" BYTE** __cdecl Player_RandomTradePartner(BYTE**,BYTE*,BYTE*,BYTE*,DWORD*);
extern "C" BYTE** __cdecl Player_RandomTradePartner3(BYTE**,BYTE*,BYTE*,BYTE*,DWORD*);
extern "C" bool __cdecl Player_CanTrade(BYTE*);

namespace ring_grid {
static int human, head, rng, direction, names, searches, badCalls;
static int* __cdecl StartNode(int* out,const char* name) {
    const char* expected=direction==1 ? "start1_3" : "start2_3";
    if (strcmp(name,expected)) ++badCalls;
    ++names;
    *out=0;
    return out;
}
static BYTE** Select(BYTE** out,BYTE* first,BYTE* last,BYTE* player,DWORD* generator,int pass) {
    ++searches;
    if ((int)first!=human || (int)last!=head || (int)player!=human || (int)generator!=rng || searches!=pass) ++badCalls;
    *out=last;
    return out;
}
static BYTE** __cdecl Primary(BYTE** out,BYTE* first,BYTE* last,BYTE* player,DWORD* rng) {
    return Select(out,first,last,player,rng,1);
}
static BYTE** __cdecl Fallback(BYTE** out,BYTE* first,BYTE* last,BYTE* player,DWORD* rng) {
    return Select(out,first,last,player,rng,2);
}

static bool Entry(bool original) {
    using gameplay::SetWorld;
    auto fill=original ? reinterpret_cast<decltype(&Ai_FillStartGrid)>(0x4279d0) : Ai_FillStartGrid;
    int cases=0, errors=0, attempted=0;
    for (int side : {1,2}) for (int phase : {0,1}) for (int count : {0,1,2,3})
        for (double distance : {0.,699.5,700.,700.5}) for (int axis : {0,1,2}) {
            company_world::World world; world.Init();
            BYTE node[192]={}; BYTE* nodes[]={node};
            BYTE* waiting[]={world.human,world.drivers[0],world.drivers[1]};
            head=(int)world.head; human=(int)world.human;
            rng=original ? 0x68b9c8 : (int)(KingBss+(0x68b9c8-0x68a820));
            direction=side; names=searches=badCalls=0;
            SetWorld(0x68cf00,nodes+0); SetWorld(0x68cf60,waiting+0); SetWorld(0x68cf64,waiting+count);
            SetWorld(0x66c6d0,2); SetWorld(0x66c6e0,phase); SetWorld(0x66c6e4,side);
            for (int k=0; k<3; ++k) {
                F(double,node,48+8*k)=20.+k;
                SetWorld(0x68bab0+8*k,20.+k+(axis==k ? distance : 0.));
            }
            fill();
            const bool look=phase==0 && distance>=700. && count<2;
            attempted+=searches!=0;
            if (badCalls || names!=int(phase==0) || searches!=(look ? 2 : 0)) {
                if (!errors) printf("ring grid mismatch side=%d phase=%d count=%d distance=%g names=%d searches=%d badCalls=%d\n",
                                    side,phase,count,distance,names,searches,badCalls);
                ++errors;
            }
            ++cases;
        }
    printf("ring grid recruitment original=%d cases=%d attempted=%d mismatches=%d\n",original,cases,attempted,errors);
    return !errors;
}
static bool Drivers(bool original) {
    auto primary=original ? reinterpret_cast<decltype(&Player_CanTrade)>(0x425b40) : Player_CanTrade;
    company_world::World w; w.Init();
    BYTE* p=w.drivers[0]+8;
    int cases=0, errors=0;
    for (int model : {0,2,3}) for (int status : {4,5,256,257}) for (int flags : {0,4})
        for (bool hasCar : {false,true}) for (bool busy : {false,true}) for (int kind : {1,2}) {
            F(BYTE*,p,508)=hasCar ? w.holds[1] : nullptr; F(int,w.model,32)=model;
            F(int,p,92)=status; F(DWORD,p,88)=flags; F(BYTE,p,488)=BYTE(busy); F(int,p,84)=kind;
            bool ready=hasCar && !busy && kind==2 && status==4;
            bool a=primary(p);
            if (a!=(ready && model!=2 && !flags)) ++errors;
            ++cases;
        }
    printf("ring driver filters original=%d cases=%d mismatches=%d\n",original,cases,errors);
    return !errors;
}
static bool Selection() {
    company_world::World w; w.Init();
    F(int,w.model,32)=0;
    int cases=0, errors=0;
    for (unsigned available=0; available<4; ++available) for (unsigned employed=0; employed<4; ++employed)
        for (bool fallback : {false,true}) for (unsigned seed : {31u,173u}) {
            for (int i=0; i<2; ++i) {
                BYTE* driver=w.drivers[i];
                F(BYTE*,driver,0)=i==0 ? w.drivers[1] : w.head;
                F(int,driver,100)=(available & (1u<<i)) ? 4 : 5;
                F(BYTE*,driver,176)=(employed & (1u<<i)) ? w.human : w.head;
            }
            DWORD random[2][57]={}; BYTE* picked[2]={};
            for (int side=0; side<2; ++side) {
                for (unsigned i=0; i<55; ++i) random[side][i]=211*i+seed;
                random[side][56]=17;
                auto choose=fallback ? Player_RandomTradePartner3 : Player_RandomTradePartner;
                if (side==0) choose=reinterpret_cast<decltype(choose)>(fallback ? 0x45ed90 : 0x45eba0);
                choose(&picked[side],w.drivers[0],w.head,w.human,random[side]);
            }
            unsigned eligible=fallback ? available : available & ~employed;
            unsigned bit=picked[0]==w.drivers[0] ? 1u : picked[0]==w.drivers[1] ? 2u : 0u;
            bool valid=eligible ? (eligible & bit)!=0 : picked[0]==w.head;
            if (!valid || picked[0]!=picked[1] || memcmp(random[0],random[1],sizeof random[0])) ++errors;
            ++cases;
        }
    printf("ring grid driver selection cases=%d mismatches=%d\n",cases,errors);
    return !errors;
}
static bool Both() {
    const bool selected=Selection();
    Redirect((void*)0x436510,(void*)StartNode); Redirect((void*)Node_ByName,(void*)StartNode);
    Redirect((void*)NameMap68d008_Find,(void*)StartNode);
    Redirect((void*)0x45eba0,(void*)Primary); Redirect((void*)Player_RandomTradePartner,(void*)Primary);
    Redirect((void*)0x45ed90,(void*)Fallback); Redirect((void*)Player_RandomTradePartner3,(void*)Fallback);
    const bool ref=Entry(true), got=Entry(false), refFilters=Drivers(true), gotFilters=Drivers(false);
    return selected && ref && got && refFilters && gotFilters;
}
}
