extern "C" int __cdecl Ai_StartRaces();
extern "C" int* __cdecl Node_ByName(int*,const char*);
extern "C" int* __cdecl NameMap68d008_Find(int*,const char*);
extern "C" void __cdecl Road_NodeCost(int,int,int,double*);
extern "C" void __cdecl Road_PositionCost(int,LrPos,LrPos,double*);

namespace ring_start {
static int costs, routes, signs;
static int* __cdecl Name(int* out,const char* key) {
    *out=!strncmp(key,"check_point",11) ? key[11]-'0' : -1;
    return out;
}
static void __cdecl Leg(int side,int from,int to,double* out) {
    *out=10.*from+to;
    if (side==0 && from>=1 && from<=3 && to>=1 && to<=3) ++costs;
}
static void __cdecl First(int side,LrPos from,LrPos to,double* out) {
    if (!side && from.d[1]==4 && to.d[1]==1) ++routes;
    *out=13.;
}
static void __cdecl Go(BYTE*,LrPos to,int flag) { if (to.d[1]==1 && flag==1) ++signs; }
static bool Check(bool original) {
    using gameplay::SetWorld;
    const auto start=original ? reinterpret_cast<decltype(&Ai_StartRaces)>(0x427520) : Ai_StartRaces;
    int cases=0, errors=0;
    for (int direction : {1,2}) for (int opponents : {0,1,3}) for (bool requested : {false,true})
        for (int load : {0,1,2}) {
            company_world::World w; w.Init();
            BYTE* waiting[]={w.human,w.drivers[0],w.drivers[1],w.drivers[2]};
            BYTE* active[4]={}; BYTE* empty=nullptr;
            SetWorld(0x68cf60,waiting+0); SetWorld(0x68cf64,waiting+1+opponents);
            SetWorld(0x68a980,active+0); SetWorld(0x68a984,active+0); SetWorld(0x68a988,active+4);
            SetWorld(0x68a970,&empty); SetWorld(0x68a974,&empty);
            SetWorld(0x68d168,int(requested)); SetWorld(0x66c6e0,2); SetWorld(0x66c6e4,direction);
            SetWorld(0x68d164,2); SetWorld(0x68d188,0);
            BYTE orders[288]={}, good[104]={};
            SetWorld(0x68ccb8,good+0); F(double,good,40)=100.;
            F(double,orders,56)=F(double,orders,144+56)=1.;
            F(BYTE*,w.holds[0],28)=orders; F(BYTE*,w.holds[0],32)=orders+144*load;
            F(BYTE*,w.holds[0],36)=orders+sizeof orders;
            F(int,w.human,100)=5; F(int,w.human,44)=4; F(double,w.human,136)=1500.;
            for (int i=0; i<12; ++i) SetWorld(0x68cf70+8*i,0.);
            costs=routes=signs=0;
            int result=start();
            bool ok=result==(requested ? 0 : -1) && start()==-1 && costs==9*int(requested) &&
                routes==int(requested) && signs==int(requested) &&
                F(double,w.human,136)==1500.-(requested ? 100.*load : 0.) &&
                F(BYTE*,w.holds[0],32)==orders+(requested ? 0 : 144*load) &&
                F(double,w.human,792)==(requested ? 100. : 0.) && bool(F(DWORD,w.human,96)&0x4000)==requested;
            for (int i=0; i<4; ++i)
                if (bool(w.drivers[i][496])!=(requested && i<opponents) ||
                    F(double,w.drivers[i],792)!=(requested && i<opponents ? 100. : 0.)) ok=false;
            double sum=0.;
            for (int i=8; i>=0; --i) {
                int from=direction==1 ? i%3+1 : (3-i%3)%3+1;
                int to=direction==1 ? (i+1)%3+1 : (5-i%3)%3+1;
                if (requested) sum+=10.*from+to;
                double observed=original ? *(double*)(0x68cf70+8*i) : F(double,KingBss,0x68cf70-0x68a820+8*i);
                if (observed!=sum) ok=false;
            }
            if (F(double,w.human,504)!=(requested ? -13.-sum : 0.)) ok=false;
            if (!ok && !errors) printf("race start mismatch original=%d direction=%d opponents=%d requested=%d load=%d result=%d costs=%d routes=%d signs=%d advancement=%g cash=%g\n",
                original,direction,opponents,requested,load,result,costs,routes,signs,F(double,w.human,504),F(double,w.human,136));
            if (!ok) ++errors;
            ++cases;
        }
    printf("campaign ring start original=%d cases=%d mismatches=%d\n",original,cases,errors);
    return !errors;
}
static bool Both() {
    Redirect((void*)0x436510,(void*)Name); Redirect((void*)Node_ByName,(void*)Name);
    Redirect((void*)NameMap68d008_Find,(void*)Name);
    Redirect((void*)0x485680,(void*)Leg); Redirect((void*)Road_NodeCost,(void*)Leg);
    Redirect((void*)0x486800,(void*)First); Redirect((void*)Road_PositionCost,(void*)First);
    Redirect((void*)0x478260,(void*)Go); Redirect((void*)Player_GoTo,(void*)Go);
    const bool ref=Check(true), got=Check(false);
    return ref && got;
}
}
