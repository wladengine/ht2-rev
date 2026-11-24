extern "C" int __cdecl Ai_PrepareVehicleOrder(int,int,const char*,BYTE*);

namespace vehicle_dealers {
struct Quote { int status,market,slot,node,variant,kind; double model,credit,base,factor; };
static Quote Run(bool original,int dealerKind,int state,int buyerState,int position,int item,int availability) {
    using gameplay::SetWorld;
    company_world::World w; w.Init();
    BYTE models[2][144]={}, symbols[2][24]={}, markets[2][96]={}, slots[2][72]={}, nodes[4][192]={}, order[80]={};
    BYTE* fleet[]={models[0],models[1]}; BYTE* sales[]={markets[0],markets[1]}; BYTE* road[]={nodes[0],nodes[1],nodes[2],nodes[3]};
    int stock[]={0,1}; const char* keys[]={"fixture_a","fixture_b"};
    SetWorld(0x68cfd8,fleet+0); SetWorld(0x68cfdc,fleet+2);
    SetWorld(0x68cc98,sales+0); SetWorld(0x68cc9c,sales+2);
    SetWorld(0x68cf00,road+0); SetWorld(0x68cf04,road+4);
    for (int i=0; i<2; ++i) {
        F(BYTE*,models[i],8)=symbols[i]; models[i][16]=9; F(const char*,symbols[i],12)=keys[i];
        F(double,models[i],48)=1000.+100*i; F(double,models[i],56)=250.+10*i;
        F(int,models[i],64)=i; F(int,models[i],72)=7+i;
        SetWorld(0x697914+2416*i+100,1600.f+i*100.f);
        F(BYTE*,markets[i],28)=slots[i]; F(BYTE*,markets[i],32)=slots[i]+72;
        F(double,markets[i],48)=.5+i; F(double,markets[i],56)=700.+1000*i;
        for (int k=0; k<2; ++k) {
            BYTE* entry=slots[i]+36*k;
            int n=2*i+k; F(int,entry,0)=n;
            F(int,entry,4)=k==availability ? 4+32*i : 0;
            F(int,entry,12)=200+n; F(int*,entry,24)=stock+i; F(int*,entry,28)=stock+i+1;
            F(double,nodes[n],48)=700.+1000*i+k*5.; F(int,nodes[n],172)=i;
        }
    }
    BYTE* d=w.drivers[0]; F(int,d,92)=dealerKind; F(int,d,44)=position<2 ? position*2 : -1;
    F(double,d,312)=position<2 ? 0. : 2400.;
    F(double,d,224)=state==1 ? 99. : 0.; d[201]=state==2;
    F(double,w.human,224)=buyerState==1 ? 99. : 0.; w.human[201]=buyerState==2;
    const auto prepare=original ? reinterpret_cast<decltype(&Ai_PrepareVehicleOrder)>(0x433660) : Ai_PrepareVehicleOrder;
    Quote q={};
    q.status=prepare((int)w.human,(int)d,item<0 ? nullptr : keys[item],order);
    q.market=F(int,order,56); q.slot=F(int,order,64); q.node=F(int,order,52); q.variant=F(int,order,48);
    q.kind=F(int,order,12); q.model=F(double,order,24); q.credit=F(double,order,32);
    q.base=F(double,order,16); q.factor=F(double,order,40);
    return q;
}
static bool Check() {
    int cases=0, errors=0, quoted=0;
    for (int kind : {2,6,7,9}) for (int state : {0,1,2}) for (int buyer : {0,1,2}) for (int at : {0,1,2})
        for (int model : {-1,0,1}) for (int slot : {0,1}) {
            const Quote a=Run(true,kind,state,buyer,at,model,slot), b=Run(false,kind,state,buyer,at,model,slot);
            const bool blocked=kind==2 || (kind==6 && buyer);
            const bool filled=!blocked && model>=0;
            const int expected=blocked ? 3 : filled && kind!=6 && model!=int(at!=0) ? 1 : 0;
            bool ok=!memcmp(&a,&b,sizeof a) && a.status==expected;
            if (filled && (a.market!=int(at==2) || a.slot!=slot || a.node!=2*int(at==2)+slot || a.variant!=200+a.node ||
                a.base!=1600.+model*100. || a.model!=1000.+100*model || a.credit!=250.+10*model || a.kind!=7+model)) ok=false;
            quoted+=filled && !expected;
            if (!ok && !errors) printf("dealer mismatch kind=%d state=%d at=%d model=%d saleSlot=%d answer=%d/%d expected=%d market=%d slot=%d cost=%g factor=%g/%g\n",
                kind,state,at,model,slot,a.status,b.status,expected,a.market,a.slot,a.base,a.factor,b.factor);
            if (!ok) ++errors;
            ++cases;
        }
    printf("vehicle dealer quotes cases=%d offers=%d mismatches=%d\n",cases,quoted,errors);
    return !errors;
}
}
