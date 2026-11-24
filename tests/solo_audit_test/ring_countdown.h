// Exercise the scheduler from a selected driver to the end of the wait.
// World node names, travel cost, movement commands and the HUD are observed at their boundaries.
namespace ring_countdown {
using gameplay::SetWorld;
static int side,limit,available,next,places,routes,quotes,notices,minutes;
static bool valid;
static double cost;
static BYTE** drivers;

static BYTE** __cdecl Pick(BYTE** out,BYTE*,BYTE* end,BYTE*,DWORD*) {
    *out=next<available ? drivers[next++] : end;
    return out;
}
static BYTE** __cdecl Empty(BYTE** out,BYTE*,BYTE* end,BYTE*,DWORD*) { *out=end; return out; }
static int* __cdecl Name(int* out,const char* name) {
    *out=0;
    if (!strcmp(name,"check_point1")) { *out=10; return out; }
    int key=0, slot=0;
    if (sscanf(name,"start%d_%d",&key,&slot)!=2 || key!=side || slot<1 || slot>4) valid=false;
    else *out=side*10+slot;
    return out;
}
static void __cdecl Place(BYTE* who,LrPos pos) {
    valid=valid && places<4 && who==drivers[places] && int(pos.d[0])==-1 &&
          int(pos.d[1])==side*10+places+1 && int(pos.d[7])==-1;
    ++places;
}
static void __cdecl Route(BYTE* who,LrPos pos,int mode) {
    valid=valid && routes<4 && who==drivers[routes] && pos.d[1]==10 && mode==1;
    ++routes;
}
static void __cdecl Travel(int mode,LrPos from,LrPos dest,double* result) {
    valid=valid && mode==0 && from.d[1]==37 && int(dest.d[1])==side*10+limit;
    ++quotes; *result=cost;
}
static const char* __cdecl Format(const char* key) {
    if (strcmp(key,"RACES_TO_TAKE_PLACE_IN")) valid=false;
    return "minutes %.0f";
}
static void __cdecl Notice(const char* text) {
    char expected[32]; sprintf(expected,"minutes %d",minutes);
    if (strcmp(expected,text)) valid=false;
    ++notices;
}
template<class T> static T Read(bool original,DWORD address) {
    if (original) return *(T*)address;
    return address>=0x68a820 ? F(T,KingBss,address-0x68a820) : F(T,KingData,address-0x66c000);
}
struct CostCase { double value; int minutes; };
static bool Check(bool original) {
    auto fill=original ? reinterpret_cast<decltype(&Ai_FillStartGrid)>(0x4279d0) : Ai_FillStartGrid;
    int cases=0,errors=0,full=0;
    const CostCase prices[]={{0.,0},{599.5,0},{600.,1},{600.5,1},{1499.,2},{36599.5,60},{36600.,61},{36600.5,61},{70000.,61}};
    for (int direction : {1,2}) for (int capacity : {1,3,4}) for (int start=0; start<capacity; ++start)
        for (int supply : {0,1,4}) for (auto price : prices) {
            company_world::World w; w.Init();
            BYTE* roster[]={w.drivers[0],w.drivers[1],w.drivers[2],w.drivers[3]};
            BYTE* waiting[4]={}; BYTE node[192]={}; BYTE* nodes[25];
            for (auto& n : nodes) n=node;
            for (int i=0; i<start; ++i) waiting[i]=roster[i];
            drivers=roster; limit=capacity; side=direction; cost=price.value; minutes=price.minutes;
            valid=true; next=places=routes=start; available=start+supply; notices=quotes=0;
            SetWorld(0x68cf00,nodes+0); SetWorld(0x68bab0,710.); SetWorld(0x68bab8,0.); SetWorld(0x68bac0,0.);
            SetWorld(0x68cf60,waiting+0); SetWorld(0x68cf64,waiting+start); SetWorld(0x68cf68,waiting+4);
            SetWorld(0x66c6e0,0); SetWorld(0x66c6e4,side); SetWorld(0x66c6d0,limit); SetWorld(0x68d168,0);
            SetWorld(0x66c6d8,-1.); SetWorld(0x66c6f8,-1.); F(int,w.human,44)=37;
            int end=available<limit ? available : limit;
            bool ready=end==limit;
            fill();
            bool ok=valid && next==end && places==end && routes==end && quotes==int(ready) && notices==int(ready) &&
                Read<BYTE**>(original,0x68cf64)==waiting+end && Read<int>(original,0x66c6e0)==(ready ? 2 : 0) &&
                Read<int>(original,0x68d168)==int(ready) && Read<double>(original,0x66c6d8)==(ready ? 100. : -1.) &&
                Read<double>(original,0x66c6f8)==(ready ? minutes*60. : -1.);
            for (int i=start; i<end; ++i)
                if (waiting[i]!=drivers[i] || F(int,drivers[i],728)!=i || F(int,drivers[i],100)!=5 ||
                    !(F(DWORD,drivers[i],96)&0x4000)) ok=false;
            if (ready) {
                ++full;
                double deadline=100.+minutes*60.;
                SetWorld(0x68a820,deadline); fill();
                if (Read<int>(original,0x66c6e0)!=2 || !Read<int>(original,0x68d168)) ok=false;
                for (int i=0; i<end; ++i) if (F(double,drivers[i],792)!=0.) ok=false;
                SetWorld(0x68a820,deadline+.25); fill();
                SetWorld(0x68a820,deadline+1.); fill();
                if (Read<int>(original,0x66c6e0)!=1 || Read<int>(original,0x68d168) || notices!=1) ok=false;
                for (int i=0; i<end; ++i) if (F(double,drivers[i],792)!=deadline+.25) ok=false;
            }
            if (!ok && !errors) printf("grid placement mismatch original=%d capacity=%d initial=%d supply=%d cost=%g places=%d routes=%d notices=%d duration=%g valid=%d\n",
                original,capacity,start,supply,cost,places,routes,notices,Read<double>(original,0x66c6f8),valid);
            if (!ok) ++errors;
            ++cases;
        }
    printf("grid placement and countdown original=%d cases=%d full=%d mismatches=%d\n",original,cases,full,errors);
    return !errors;
}
static bool Both() {
    Redirect((void*)0x45eba0,(void*)Pick); Redirect((void*)Player_RandomTradePartner,(void*)Pick);
    Redirect((void*)0x45ed90,(void*)Empty); Redirect((void*)Player_RandomTradePartner3,(void*)Empty);
    Redirect((void*)0x436510,(void*)Name); Redirect((void*)Node_ByName,(void*)Name); Redirect((void*)NameMap68d008_Find,(void*)Name);
    Redirect((void*)0x4786a0,(void*)Place); Redirect((void*)Player_Place,(void*)Place);
    Redirect((void*)0x478260,(void*)Route); Redirect((void*)Player_GoTo,(void*)Route);
    Redirect((void*)0x487500,(void*)Travel); Redirect((void*)Road_PositionCostBest,(void*)Travel);
    Redirect((void*)0x40e270,(void*)Format); Redirect((void*)Message_Text,(void*)Format);
    Redirect((void*)0x56ae60,(void*)Notice); Redirect((void*)Messages_Add,(void*)Notice);
    const bool ref=Check(true), got=Check(false);
    return ref && got;
}
}
