extern "C" void __cdecl Task_Standings();

namespace market_share {
static BYTE* winner;
static int awards;
static int __cdecl Award(BYTE* driver,int kind,int arg) {
    if (kind==12 && arg==0) { winner=driver; ++awards; }
    return 0;
}
static bool Check(bool original) {
    using gameplay::SetWorld;
    const auto tick=original ? reinterpret_cast<decltype(&Task_Standings)>(0x41fcf0) : Task_Standings;
    int cases=0, errors=0;
    for (int owner : {0,1}) for (bool staffed : {false,true}) for (int value : {0,49,50,51,52,100})
        for (bool triggered : {false,true}) {
            company_world::World w; w.Init();
            BYTE* people[]={w.human,w.drivers[0],w.drivers[1],w.drivers[2],w.drivers[3]};
            BYTE models[5][144]={}, roster[16]={};
            BYTE* entries[5];
            // At 0 every truck has zero value. Otherwise the market sums to
            // 100 and competitors split the remaining share evenly.
            const double prices[]={0.,0.,staffed ? 10. : 0.,value ? (100.-value)/2. : 0.,value ? (100.-value)/2. : 0.};
            SetWorld(0x68cfd8,entries+0);
            SetWorld(0x66c710,triggered ? 1.05 : .51);
            SetWorld(0x68d1a0,int(triggered));
            SetWorld(0x68d118,(void*)Award); SetWorld(0x68d114,BYTE(0)); SetWorld(0x68d115,BYTE(0));
            if (staffed) {
                F(BYTE*,people[owner],160)=roster; F(BYTE*,people[owner],164)=roster+16;
                F(BYTE*,roster,0)=people[2]; F(BYTE*,people[2],176)=people[owner];
            }
            for (int i=0; i<5; ++i) {
                entries[i]=models[i]; F(int,models[i],64)=i; F(int,w.holds[i],4)=i;
                double price=!value ? 0. : i==owner ? value-prices[2] : prices[i];
                SetWorld(0x697914+2416*i+100,float(price));
                F(double,people[i],736)=.5; F(double,people[i],744)=.5;
                F(double,people[i],752)=8.; F(double,people[i],760)=4.*i;
                F(double,people[i],768)=-1.; F(double,people[i],776)=-2.;
                people[i][784]=triggered && i==owner;
                people[i][800]=1;
            }
            awards=0; winner=nullptr;
            const bool crosses=!triggered && value>=51;
            bool ok=true;
            for (int pass=0; pass<2; ++pass) {
                tick();
                for (int i=0; i<5; ++i) {
                    const double cost=i==owner ? double(value) : i>=3 ? prices[i] : 0.;
                    const double filtered=(4.+2.*i)/(pass+1);
                    if (F(double,people[i],760)!=0. || F(double,people[i],752)!=filtered ||
                        F(double,people[i],776)!=(value ? cost : -2.) ||
                        F(double,people[i],768)!=(value ? cost/100. : -1.) ||
                        people[i][784]!=BYTE(i==owner && (triggered || crosses))) ok=false;
                }
            }
            double threshold=original ? *(double*)0x66c710 : F(double,KingData,0x66c710-0x66c000);
            if (!ok || awards!=int(crosses) || winner!=(crosses ? people[owner] : nullptr) ||
                threshold!=((triggered || crosses) ? 1.05 : .51)) {
                if (!errors) printf("market mismatch original=%d owner=%d staff=%d value=%d triggered=%d awards=%d valid=%d share=%.17g threshold=%.17g\n",
                    original,owner,staffed,value,triggered,awards,ok,F(double,people[owner],768),threshold);
                ++errors;
            }
            ++cases;
        }
    printf("campaign market share original=%d cases=%d mismatches=%d\n",original,cases,errors);
    return !errors;
}
static bool Both() {
    const bool ref=Check(true), got=Check(false);
    return ref && got;
}
}
