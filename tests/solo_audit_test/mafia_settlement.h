extern "C" int __cdecl Ai_MafiaAttack(int);
extern "C" BYTE* __stdcall ListNode16_MakeB(const void*);

namespace mafia_settlement {
static BYTE taskNode[16];
static BYTE* __stdcall Node(const void* value) {
    memset(taskNode,0,sizeof taskNode);
    F(void*,taskNode,8)=*(void* const*)value;
    return taskNode;
}
static bool Check(bool original) {
    using gameplay::SetWorld;
    const auto settle=original ? reinterpret_cast<decltype(&Ai_MafiaAttack)>(0x445740) : Ai_MafiaAttack;
    int cases=0, errors=0, repossessions=0, bankruptcies=0;
    for (int count : {0,1,2}) for (double debt : {0.,99.,100.,101.,200.,201.})
        for (double initialWallet : {0.,100.,300.}) {
            company_world::World w; w.Init();
            BYTE orders[288]={}, goods[104]={}, queue[12]={};
            BYTE* empty=nullptr; SetWorld(0x68a970,&empty); SetWorld(0x68a974,&empty);
            F(BYTE*,queue,0)=F(BYTE*,queue,4)=queue;
            SetWorld(0x68b99c,queue+0); SetWorld(0x68ccb8,goods+0);
            F(double,goods,40)=100.; // order value = sqrt(100 * 1) * 10
            F(double,orders,56)=F(double,orders,144+56)=1.;
            F(BYTE*,w.holds[0],28)=orders; F(BYTE*,w.holds[0],32)=orders+144*count;
            F(BYTE*,w.holds[0],36)=orders+sizeof orders;
            F(double,w.human,224)=debt; F(int,w.human,232)=7; F(double,w.human,136)=initialWallet;
            w.human[201]=1;
            F(int,w.drivers[0],92)=6; F(BYTE*,w.drivers[0],968)=w.human;
            F(int,w.drivers[1],92)=6; F(BYTE*,w.drivers[1],968)=w.drivers[2];
            police_fines::amount=0; police_fines::kind=police_fines::banners=0;
            int result=settle((int)w.human);
            bool lost=100.*count>debt, broke=!lost && initialWallet<debt;
            repossessions+=lost; bankruptcies+=broke;
            bool ok=result==0 && F(double,w.human,224)==0. && F(int,w.human,232)==0 &&
                F(double,w.human,136)==initialWallet-(lost ? 0. : debt) &&
                F(BYTE*,w.holds[0],32)==orders+144*(lost ? 0 : count) && w.human[201]==BYTE(!lost) &&
                F(BYTE*,w.drivers[0],968)==w.head && F(BYTE*,w.drivers[1],968)==w.drivers[2] &&
                police_fines::kind==(lost ? 21 : 20) && police_fines::amount==(lost ? 0. : debt) && police_fines::banners==1;
            if (lost || broke) {
                BYTE* node=F(BYTE*,queue,0);
                BYTE* task=node==queue ? nullptr : F(BYTE*,node,8);
                if (!task || F(BYTE*,task,48)!=w.human || F(double,task,16)!=108. ||
                    F(double,task,24)!=8. || !(F(DWORD,task,8)&1) ||
                    (lost && F(double,task,56)!=100.*count)) ok=false;
                if (task) { if (original) free(task); else ::operator delete(task); }
            } else if (F(BYTE*,queue,0)!=queue) ok=false;
            if (!ok && !errors) printf("settlement mismatch original=%d count=%d debt=%g wallet=%g lost=%d broke=%d amount=%g event=%d\n",
                original,count,debt,initialWallet,lost,broke,police_fines::amount,police_fines::kind);
            if (!ok) ++errors;
            ++cases;
        }
    printf("mafia settlement original=%d cases=%d forfeits=%d bankruptcies=%d mismatches=%d\n",original,cases,repossessions,bankruptcies,errors);
    return !errors;
}
static bool Both() {
    Redirect((void*)0x44b250,(void*)Node); Redirect((void*)ListNode16_MakeB,(void*)Node);
    const bool ref=Check(true), got=Check(false);
    return ref && got;
}
}
