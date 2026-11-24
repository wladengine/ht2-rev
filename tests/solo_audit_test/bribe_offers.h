extern "C" int __cdecl Ai_QueryBribeSum(BYTE*,BYTE*,unsigned,double*);
extern "C" int __cdecl Ai_OfferBribe(BYTE*,BYTE*,unsigned);

namespace bribe_offers {
struct Result {
    double quote, money;
    int answer, payment;
    DWORD flags, draws;
};
static Result Run(bool original,int type,unsigned request,double cargo,double debt,bool accepts,unsigned roll,bool custom,bool loaded) {
    using gameplay::SetWorld;
    company_world::World w; w.Init(); w.Seed(roll);
    BYTE places[56]={};
    F(BYTE,places,16)=BYTE(accepts);
    SetWorld(0x68a958,places+0); SetWorld(0x68a95c,places+56);
    BYTE* giver=w.human; BYTE* taker=w.drivers[0];
    F(int,taker,92)=type; F(int,taker,992)=0;
    F(double,giver,136)=100000.; F(double,giver,960)=cargo; F(double,giver,224)=debt;
    BYTE order[144]={}, goods[104]={};
    BYTE* competitors[]={taker};
    SetWorld(0x68a980,competitors+0); SetWorld(0x68a984,competitors+int(loaded));
    SetWorld(0x68ccb8,goods+0);
    F(double,order,64)=cargo; F(double,order,56)=1.; F(double,order,96)=1.; F(int,order,104)=1;
    for(int i=0; i<2; ++i) {
        F(BYTE*,w.holds[i],28)=order; F(BYTE*,w.holds[i],32)=order+int(loaded)*sizeof order;
    }
    taker[496]=BYTE(loaded);
    F(double,giver,208)=777.;
    SetWorld(0x66c768,custom ? .25 : .5); SetWorld(0x66c770,custom ? 100. : 200.);
    SetWorld(0x66c778,custom ? 300. : 500.); SetWorld(0x66c788,custom ? .5 : .7);
    SetWorld(0x66c790,custom ? .25 : .5);
    const auto query=original ? reinterpret_cast<decltype(&Ai_QueryBribeSum)>(0x4315f0) : Ai_QueryBribeSum;
    const auto offer=original ? reinterpret_cast<decltype(&Ai_OfferBribe)>(0x432560) : Ai_OfferBribe;
    Result r={};
    r.answer=query(giver,taker,request,&r.quote);
    r.payment=type==6 && !(request&4) ? offer(giver,taker,request) : -1;
    r.money=F(double,giver,136); r.flags=F(DWORD,giver,96);
    r.draws=original ? *(DWORD*)0x68baa4 : F(DWORD,KingBss,0x68baa4-0x68a820);
    return r;
}
static bool Check() {
    int cases=0, errors=0, charges=0, refusals=0;
    for(int type : {2,5,6}) for(unsigned request : {0u,1u,2u,4u,8u,12u,16u,32u})
        for(double cargo : {0.,1.,99.,499.,500.,501.,1000.,9999.,14230.88})
        for(double debt : {0.,175.}) for(bool accepts : {false,true}) for(unsigned roll : {249999u,500000u})
        for(bool custom : {false,true}) for(bool loaded : {false,true}) {
            const Result a=Run(true,type,request,cargo,debt,accepts,roll,custom,loaded);
            const Result b=Run(false,type,request,cargo,debt,accepts,roll,custom,loaded);
            charges+=a.money<100000.; refusals+=a.answer==2;
            bool ok=a.quote==b.quote && a.answer==b.answer && a.payment==b.payment &&
                a.flags==b.flags && a.money==b.money && a.draws==b.draws;
            if (type==6 && request==8 && cargo==1000. && !debt && !loaded)
                ok=ok && a.quote==(custom ? 500. : 700.) && a.money==100000.-a.quote &&
                    a.payment==(custom && roll==500000 ? 4 : 0) && a.draws==1;
            if (!ok && !errors) printf("offer mismatch type=%d request=%u cargo=%g debt=%g accepts=%d roll=%u quote=%g/%g answer=%d/%d payment=%d/%d draws=%lu/%lu\n",
                type,request,cargo,debt,accepts,roll,a.quote,b.quote,a.answer,b.answer,a.payment,b.payment,a.draws,b.draws);
            if (!ok) ++errors;
            ++cases;
        }
    printf("police quotes and mafia offers cases=%d charges=%d refusals=%d mismatches=%d\n",cases,charges,refusals,errors);
    return !errors && charges && refusals;
}
}
