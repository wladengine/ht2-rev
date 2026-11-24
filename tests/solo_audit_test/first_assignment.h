extern "C" int* __cdecl Town_NearestFree(int*,const double*);
extern "C" void __cdecl Ai_TruckerNextOrder(BYTE*);
extern "C" int __cdecl Ai_GetOrderInfo(int,int,int,BYTE*);
extern "C" void __cdecl Ai_ArriveAt(BYTE*,LrPos);
extern "C" void __cdecl Ai_GoToOrder(BYTE*,int,int);
extern "C" void __cdecl Ai_GoToGoodsTown(BYTE*);

namespace first_assignment {
static int calls, accept, townId, assigned, arrived, nextOrder, wrong;
static int __cdecl Offer(int,int town,int index,BYTE* out) {
    if (index<0 || index>8 || town!=townId) ++wrong;
    out[184]=BYTE(++calls==accept); if(out[184]) nextOrder=index;
    return 0;
}
static void __cdecl Arrive(BYTE*,LrPos) { ++arrived; }
static void __cdecl Route(BYTE*,int town,int index) { ++assigned; if (town!=townId || index!=nextOrder) ++wrong; }
static bool Check(bool original) {
    using gameplay::SetWorld;
    const auto select=original ? reinterpret_cast<decltype(&Town_NearestFree)>(0x4050f0) : Town_NearestFree;
    const auto next=original ? reinterpret_cast<decltype(&Ai_TruckerNextOrder)>(0x4185d0) : Ai_TruckerNextOrder;
    int cases=0, errors=0, dispatches=0;
    for (int unavailable=0; unavailable<8; ++unavailable) for (int pos : {0,6,20})
        for (int cutoff : {1,3,100,101}) for (bool alternate : {false,true}) {
            company_world::World w; w.Init();
            BYTE towns[3][112]={}, nodes[3][192]={};
            BYTE* locations[]={towns[0],towns[1],towns[2]};
            BYTE* roads[]={nodes[0],nodes[1],nodes[2]};
            SetWorld(0x68cec0,locations+0); SetWorld(0x68cec4,locations+3);
            SetWorld(0x68cf00,roads+0); SetWorld(0x68cf04,roads+3);
            const int x[]={-6,6,20};
            int best=-1, distance=100;
            for (int i=0; i<3; ++i) {
                F(int,towns[i],24)=40+i; F(int,towns[i],64)=i; F(double,nodes[i],48)=x[i];
                towns[i][96]=(unavailable&(1<<i)) ? (alternate ? 2 : 1) : 4;
                if (!(towns[i][96]&3) && abs(pos-x[i])<distance) { distance=abs(pos-x[i]); best=i; }
            }
            // The search can return -1. The caller assumes a populated campaign.
            double here[]={double(pos),0.,0.}; int observed=-2;
            select(&observed,here);
            bool ok=observed==best;
            if (best!=-1) {
                BYTE* p=w.drivers[0]; F(int,p,96)=alternate ? 1 : 0;
                F(double,p,alternate ? 624 : 312)=pos;
                for (int i=0; i<55; ++i) SetWorld(0x68a870+4*i,12345u*i);
                SetWorld(0x68a94c,0u); SetWorld(0x68a950,17u);
                calls=arrived=assigned=wrong=0; accept=cutoff; nextOrder=-1; townId=40+best;
                next(p);
                const bool taken=cutoff<=100;
                if (wrong || calls!=(taken ? cutoff : 100) || arrived!=int(!taken) || assigned!=int(taken) ||
                    F(int,p,128)!=townId || F(int,p,132)!=nextOrder || (taken && F(int,p,100)!=3)) ok=false;
                dispatches+=assigned;
            }
            if (!ok && !errors) printf("assignment mismatch original=%d mask=%d pos=%d cutoff=%d observed=%d expected=%d calls=%d route=%d\n",
                original,unavailable,pos,cutoff,observed,best,calls,assigned);
            if (!ok) ++errors;
            ++cases;
        }
    printf("first warehouse assignment original=%d cases=%d dispatches=%d mismatches=%d\n",original,cases,dispatches,errors);
    return !errors;
}
static bool Both() {
    Redirect((void*)0x441950,(void*)Offer); Redirect((void*)Ai_GetOrderInfo,(void*)Offer);
    Redirect((void*)0x417820,(void*)Arrive); Redirect((void*)Ai_ArriveAt,(void*)Arrive);
    Redirect((void*)0x4180f0,(void*)Route); Redirect((void*)Ai_GoToOrder,(void*)Route);
    const bool ref=Check(true), got=Check(false);
    return ref && got;
}
}
