// A constructive campaign outcome. Capacities in kg come from stock slot "1":
// human, 30 road drivers in descending capacity, then 20 drivers waiting for hire.
namespace rival_campaign {
static const int kg[] = {
    700, 20000,20000,20000,19000,18000,18000,18000,18000,16500,16000,
    14000,14000,14000,14000,13000,13000,12100,12000,12000,12000,
    12000,12000,10600,8000,8000,800,700,700,700,700,
    20000,19000,18000,18000,16500,16000,14000,14000,12100,12000,
    12000,12000,10600,800,800,800,570,570,550,550
};
static const int count = sizeof(kg)/sizeof(kg[0]);

static bool Run(bool original) {
    using gameplay::SetWorld;
    company_world::World w;
    w.Init();
    const auto hire = original ? reinterpret_cast<decltype(&Ai_HireTask)>(0x429930) : Ai_HireTask;
    const auto grant = original ? reinterpret_cast<decltype(&Ai_MaybeGrantDeliveryLicence)>(0x429850) : Ai_MaybeGrantDeliveryLicence;
    const auto rank = original ? reinterpret_cast<decltype(&Task_Standings)>(0x41fcf0) : Task_Standings;
    BYTE nodes[count][1100], vehicles[count][64], models[count][144], staff[count][18*16] = {};
    double deadlines[count][4] = {};
    BYTE* entries[count];
    SetWorld(0x68cfd8,entries+0);
    SetWorld(0x66c710,.51); SetWorld(0x68d1a0,0);
    SetWorld(0x68d118,(void*)market_share::Award); SetWorld(0x68d114,BYTE(0)); SetWorld(0x68d115,BYTE(0));
    F(BYTE*,w.head,0)=nodes[0]; F(BYTE*,w.head,4)=nodes[count-1];
    int nextType=0, types[count];
    for (int i=0; i<count; ++i) {
        BYTE* p=nodes[i];
        memcpy(p,i ? w.drivers[0] : w.human,sizeof nodes[i]);
        F(BYTE*,p,0)=i+1<count ? nodes[i+1] : w.head;
        F(BYTE*,p,4)=i ? nodes[i-1] : w.head;
        F(BYTE*,p,104)=p;
        F(int,p,100)=i>30 ? 257 : 4;
        F(double*,p,144)=F(double*,p,148)=deadlines[i]; F(double*,p,152)=deadlines[i]+4;
        F(BYTE*,p,160)=F(BYTE*,p,164)=staff[i]; F(BYTE*,p,168)=staff[i]+sizeof staff[i];
        // The last two-driver company has the least capital when the free pool dries up.
        F(double,p,136)=i==29 ? 2000. : 100000.;
        F(BYTE*,p,516)=vehicles[i]; memcpy(vehicles[i],w.holds[0],sizeof vehicles[i]);
        F(int,vehicles[i],4)=i;
        entries[i]=models[i]; memcpy(models[i],w.model,sizeof w.model);
        int j=0;
        while (j<i && kg[j]!=kg[i]) ++j;
        types[i]=j==i ? nextType++ : types[j];
        F(int,models[i],64)=types[i]; SetWorld(0x697914+2416*types[i]+100,float(kg[i]));
        p[800]=1;
    }
    market_share::awards=0; market_share::winner=nullptr;
    rank();
    int hires=0;
    auto recruit = [&](int owner,int target) {
        // Give the current founder a winning ordinary delivery licence roll,
        // then select the target by its rank among eligible independent drivers.
        int choice=0;
        for (int i=1; i<target; ++i)
            if (F(BYTE*,nodes[i],176)==w.head && F(BYTE*,nodes[i],160)==F(BYTE*,nodes[i],164)) ++choice;
        w.Seed(0); grant(nodes[owner]);
        w.Seed(choice);
        for (unsigned i=2; i<10; ++i) SetWorld(0x68b9c8+4*i,i);
        hire(); rank();
        if (F(BYTE*,nodes[target],176)!=nodes[owner] || F(BYTE*,nodes[0],164)!=staff[0]) {
            printf("rival recruitment failed original=%d owner=%d target=%d choice=%d after=%d hires\n",original,owner,target,choice,hires);
            return false;
        }
        ++hires;
        return true;
    };
    if (!recruit(1,2)) return false;
    for (int owner=19; owner<=29; owner+=2)
        if (!recruit(owner,owner+1)) return false;
    for (int target=3; target<=17; ++target)
        if (!recruit(1,target)) return false;
    double before=F(double,nodes[1],768);
    bool dissolved=F(BYTE*,nodes[29],164)==staff[29] && F(BYTE*,nodes[30],176)==w.head;
    if (market_share::awards || !recruit(1,18)) return false;
    rank();
    double after=F(double,nodes[1],768);
    printf("rival campaign original=%d hires=%d dissolved=%d humanStaff=%d before=%.9f after=%.9f events=%d\n",
           original,hires,dissolved,int((F(BYTE*,nodes[0],164)-staff[0])/16),before,after,market_share::awards);
    return dissolved && hires==23 && before==277600./567340. && after==289600./567340. &&
           F(BYTE*,nodes[1],164)==staff[1]+17*16 && market_share::awards==1 && market_share::winner==nodes[1];
}

static bool Both() {
    F(void*,(BYTE*)0x64b2b0,0)=(void*)chase_route::Exchange;
    Redirect((void*)0x61d857,(void*)malloc);
    Redirect((void*)0x61d1c8,(void*)free);
    const bool stock=Run(true), rebuilt=Run(false);
    return stock && rebuilt;
}
}
