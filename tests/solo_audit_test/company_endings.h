// Included inside company_world after the shared world fixture.
static bool CheckDissolution(bool original) {
    const auto tick = original ? reinterpret_cast<decltype(&Ai_HireTask)>(0x429930) : Ai_HireTask;
    int cases=0, errors=0;
    for (int staffCount : {1,2}) for (int available : {0,1,2})
        for (int companyLimit : {1,7}) for (double driverCash : {20.,2048.}) {
            if (staffCount+available > driverCount-1) continue;
            World w;
            w.Init();
            SetWorld(0x68d190,0.); // suppress new recruitment in this tick
            SetWorld(0x66c700,companyLimit);
            SetWorld(0x68d18c,1);
            BYTE* owner=w.drivers[0];
            F(int,owner,156)=2;
            F(double,owner,wallet)=3000.;
            w.logos[2]=1;
            F(BYTE*,owner,164)=w.roster+16*staffCount;
            for (int i=1; i<driverCount; ++i) {
                BYTE* d=w.drivers[i];
                F(double,d,wallet)=driverCash;
                if (i<=staffCount) {
                    F(BYTE*,w.roster,16*(i-1))=d;
                    F(BYTE*,d,boss)=owner;
                }
                else if (i>staffCount+available) F(int,d,100)=257;
            }
            tick();
            bool dissolve=available<=1;
            bool ok=F(double,owner,wallet)==3000. &&
                    F(BYTE*,owner,164)==w.roster+(dissolve ? 0 : 16*staffCount) &&
                    F(int,owner,156)==(dissolve ? -1 : 2) && w.logos[2]==(dissolve ? 0u : 1u);
            for (int i=1; i<=staffCount; ++i) {
                BYTE* d=w.drivers[i];
                double remaining=driverCash>1024. ? driverCash-1024. : 0.;
                ok=ok && F(BYTE*,d,boss)==(dissolve ? w.head : owner) &&
                       F(double,d,wallet)==(dissolve ? remaining : driverCash);
            }
            int limit=original ? *(int*)0x66c700 : F(int,KingData,0x66c700-0x66c000);
            if (!ok || limit!=companyLimit-int(dissolve && companyLimit>1)) {
                if (!errors) printf("dissolution original=%d staff=%d available=%d limit=%d cash=%g retained=%d driver=%g\n",
                    original,staffCount,available,limit,driverCash,int((F(BYTE*,owner,164)-w.roster)/16),F(double,w.drivers[1],wallet));
                ++errors;
            }
            ++cases;
        }
    printf("company dissolution original=%d cases=%d mismatches=%d\n",original,cases,errors);
    return !errors;
}
