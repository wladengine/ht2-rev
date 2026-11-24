extern "C" void __fastcall ServiceScreen_Command(BYTE*,void*,int);
extern "C" BYTE* __fastcall Ctl_FindId(BYTE*,void*,int);
extern "C" int __cdecl Joy_ForceFeedback(int);

namespace service_shop {
static BYTE controls[29][288];
static int refills, messages, closes, refreshes;
static BYTE* __fastcall Control(BYTE*,void*,int id) { return controls[(id-217)/2]; }
static void __fastcall Refill(BYTE*,void*,float* a) { refills+=int(*a); }
static void __fastcall Close(BYTE*,void*) { ++closes; }
static void __fastcall Refresh(BYTE*,void*) { ++refreshes; }
static int __cdecl Feedback(int) { return 1; }
static void __cdecl Message(const char*) { ++messages; }
struct Receipt {
    int total, remaining, refillRemaining, refills, messages, close, refresh, done;
    double cash;
    int settings[19], switches[15];
};
static Receipt Run(bool original,int item,int funds,bool special) {
    using gameplay::SetWorld;
    const auto command=original ? reinterpret_cast<decltype(&ServiceScreen_Command)>(0x5220c0) : ServiceScreen_Command;
    company_world::World w; w.Init();
    BYTE shell[1344]={}, client[640]={}, scene[12]={}, car[22000]={}, cab[16640]={}, screen[520]={};
    BYTE* panel=screen+64;
    void* methods[31]={}; void* window[10]={};
    for (int i=2; i<=8; ++i) methods[i]=(void*)Refill;
    methods[21]=(void*)roadside_service::Damage; methods[29]=(void*)roadside_service::Service; methods[30]=(void*)roadside_service::Repaint;
    window[8]=(void*)Close; window[9]=(void*)Refresh;
    F(void**,screen,0)=F(void**,scene,0)=window; F(void**,car,0)=methods;
    F(BYTE*,client,616)=car; F(BYTE*,car,21600)=cab; F(BYTE*,car,20636)=w.human;
    F(int,shell,1316)=int(special);
    SetWorld(0x6cec6c,shell+0); SetWorld(0x6d2078,client+0); SetWorld(0x696ca0,scene+0);
    const int bodySwitch[]={21204,21152,21256,21516,21412,21464,21360,21308};
    const int cabSwitch[]={16216,16372,16060,16320,16164,16112};
    for (int off : bodySwitch) F(int,car,off+44)=2;
    for (int off : cabSwitch) F(int,cab,off+44)=2;
    memset(controls,0,sizeof controls);
    for (int i=0; i<19; ++i) F(int,panel,316+4*i)=10+i;
    for (int i=0; i<7; ++i) F(int,panel,396+4*i)=30+i;
    F(int,panel,392)=361; F(int,panel,424)=231;
    refills=messages=closes=refreshes=0;
    // A checked control dispatches its odd ID, its de-selection the even ID.
    auto select=[&](int id) { F(int,Control(nullptr,nullptr,id),284)|=2; command(panel,nullptr,id); };
    if (item<19) select(217+item*2);
    else if (item<26) select(257+(item-19)*2);
    else if (item==26) { select(235); select(237); }
    else if (item==27) { select(237); select(235); }
    else if (item==28) { select(217); F(int,controls[0],284)=0; command(panel,nullptr,218); }
    else {
        F(int,controls[3],40)=1; // deliberately unavailable service
        select(255); select(271);
    }
    Receipt r={}; r.total=F(int,panel,312); r.remaining=F(int,panel,392); r.refillRemaining=F(int,panel,424);
    const double wallet=funds==0 ? 0. : r.total+(funds==1 ? 0. : 100.);
    F(double,w.human,136)=wallet;
    command(panel,nullptr,279);
    r.cash=F(double,w.human,136); r.refills=refills; r.messages=messages; r.close=closes; r.refresh=refreshes; r.done=cab[9984];
    for (int i=0; i<19; ++i) r.settings[i]=F(int,w.equipment,16*i);
    int j=0;
    for (int off : bodySwitch) r.switches[j++]=F(int,car,off+40);
    for (int off : cabSwitch) r.switches[j++]=F(int,cab,off+40);
    return r;
}
static bool Check() {
    Redirect((void*)0x5bdb50,(void*)Control); Redirect((void*)Ctl_FindId,(void*)Control);
    Redirect((void*)0x4e12b0,(void*)Feedback); Redirect((void*)Joy_ForceFeedback,(void*)Feedback);
    Redirect((void*)0x56ae60,(void*)Message); Redirect((void*)Messages_Add,(void*)Message);
    const int slots[]={3,4,5,6,7,17,0,1,18,9,9,13,2,15,16,10,12,8,11};
    int errors=0, cases=0, paid=0, refused=0;
    for(int item=0; item<30; ++item) for(int funds=0; funds<3; ++funds) for(bool special : {false,true}) {
        Receipt a=Run(true,item,funds,special), b=Run(false,item,funds,special);
        bool buy=funds!=0 || item==28;
        const int price=item<19 ? 10+item : item<26 ? 30+item-19 : item==26 ? 20 : item==27 ? 19 : item==28 ? 0 : 361-13-20+231;
        bool ok=!memcmp(&a,&b,sizeof a) && a.total==price && a.close==int(buy) && a.refresh==int(buy) &&
            a.done==(buy ? 2 : 0) && a.refills==(buy ? (item==29 ? 7 : int(item>=19 && item<26)) : 0);
        if (item<19 && a.settings[slots[item]]!=(buy ? (item==10 ? 2 : 1) : 0)) ok=false;
        if (a.cash!=(funds==0 ? 0. : (funds==1 ? 0. : 100.)+(special ? price : 0.))) ok=false;
        if (a.cash>=0 && buy && price && !special) ++paid;
        if (!buy) ++refused;
        if (!ok && !errors) printf("station mismatch item=%d funds=%d special=%d price=%d/%d expected=%d close=%d/%d refills=%d/%d cash=%g/%g\n",
            item,funds,special,a.total,b.total,price,a.close,b.close,a.refills,b.refills,a.cash,b.cash);
        if (!ok) ++errors;
        ++cases;
    }
    printf("station purchases cases=%d paid=%d refused=%d mismatches=%d\n",cases,paid,refused,errors);
    return !errors;
}
}
