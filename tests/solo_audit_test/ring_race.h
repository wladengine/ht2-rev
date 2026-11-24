extern "C" int __cdecl Game_PlayerMessage(int*);
extern "C" void __cdecl Notice_Show(int,const char*,int,int);
extern "C" void __cdecl Messages_Add(const char*);

namespace ring_race {
using company_world::World;
using gameplay::SetWorld;
static BYTE finishRecord[64];
static BYTE* finisher;
static int finishes, lastNotice;
static void __cdecl Notice(int kind,const char*,int,int) { lastNotice=kind; }
static void __cdecl Message(const char*) {}
static int __cdecl Event(BYTE* player,int kind,int data) {
    if (kind==11) { finisher=player; ++finishes; memcpy(finishRecord,(void*)data,sizeof finishRecord); }
    return 0;
}

static bool Check(bool original) {
    const auto send = original ? reinterpret_cast<decltype(&Game_PlayerMessage)>(0x56ffd0) : Game_PlayerMessage;
    BYTE scene[1344]={}, track[400]={}, client[640]={}, car[20700]={};
    SetWorld(0x6cec6c,scene+0); SetWorld(0x6cec98,track+0); SetWorld(0x6d2078,client+0);
    F(BYTE*,client,616)=car;
    SetWorld(0x68d118,(void*)Event);
    SetWorld(0x68d114,BYTE(0)); SetWorld(0x68d115,BYTE(0));
    int cases=0, errors=0;
    for (bool shortRace : {false,true}) for (bool hasOpponent : {false,true})
        for (int alreadyFinished : {0,1}) for (int streak : {0,2,7,15})
        for (int gate : {0,1,2,3}) {
            World w;
            w.Init(); w.Seed(19);
            BYTE* entries[]={w.drivers[0]};
            BYTE* waiting[]={w.human};
            SetWorld(0x68a980,entries+0); SetWorld(0x68a984,entries+int(hasOpponent));
            SetWorld(0x68cf60,waiting+0); SetWorld(0x68cf64,waiting+1);
            SetWorld(0x68d164,alreadyFinished);
            SetWorld(0x6f33b8,-1); SetWorld(0x6f33bc,0);
            SetWorld(0x66c6e0,gate==2 ? 3 : 1);
            F(int,scene,1300)=gate==3 ? 2 : 1;
            F(int,scene,1316)=int(shortRace);
            F(int,track,384)=1;
            F(BYTE*,car,20636)=w.human;
            F(int,w.human,96)=gate==1 ? 0 : 0x4000;
            F(int,w.human,100)=5; F(int,w.human,536)=streak; F(double,w.human,136)=1000.;
            F(double,w.human,792)=60.; w.human[800]=1;
            finishes=lastNotice=0; finisher=nullptr;
            memset(finishRecord,0,sizeof finishRecord);
            bool ok=true;
            const int last=shortRace ? 2 : 4, laps=shortRace ? 1 : 3;
            auto message=[&](int point) {
                int msg[4]={7,point};
                lastNotice=0;
                if (send(msg)!=(gate==3 ? 0 : 1)) ok=false;
            };
            auto readInt=[&](DWORD va) { return original ? *(int*)va : F(int,KingBss,va-0x68a820); };
            for (int lap=0; lap<laps; ++lap) {
                message(0); message(2); // skipped checkpoint must report wrong order
                if (!gate && (lastNotice!=6 || readInt(0x6f33b8)!=0)) ok=false;
                for (int point=1; point<=last; ++point) {
                    message(point);
                    if (!gate && point<last) {
                        if (lastNotice!=7 || readInt(0x6f33b8)!=point) ok=false;
                        message(point); // duplicate checkpoint
                        if (lastNotice || readInt(0x6f33b8)!=point) ok=false;
                    }
                }
                if (!gate && (readInt(0x6f33b8)!=-1 || readInt(0x6f33bc)!=(lap+1)%laps ||
                    (lap+1<laps && lastNotice!=3))) ok=false;
            }
            const bool won=!gate && hasOpponent && !alreadyFinished;
            const int licences=won ? (streak>=7 ? 3 : streak>=2 ? 2 : 1) : 0;
            if (finishes!=int(!gate) || F(double,w.human,136)!=(won ? 6000. : 1000.) ||
                F(int,w.human,536)!=(gate ? streak : won ? streak+1 : 0) ||
                F(double*,w.human,148)!=w.deadlines[0]+licences) ok=false;
            if (!gate && (finisher!=w.human || F(double,finishRecord,24)!=40. ||
                F(double,finishRecord,32)!=(won ? 5000. : 0.) || F(int,finishRecord,8)!=licences ||
                F(int,w.human,100)!=0 || F(int,w.human,532)!=-1 || (F(int,w.human,96)&0x4000))) ok=false;
            for (int i=0; i<licences; ++i) if (w.deadlines[0][i]!=479.) ok=false;
            if (!ok && !errors) printf("ring mismatch original=%d short=%d opponents=%d finished=%d streak=%d gate=%d events=%d notice=%d money=%.0f awards=%d time=%.2f\n",
                original,shortRace,hasOpponent,alreadyFinished,streak,gate,finishes,lastNotice,F(double,w.human,136),F(int,finishRecord,8),F(double,finishRecord,24));
            if (!ok) ++errors;
            ++cases;
        }
    printf("ring checkpoints and payout original=%d cases=%d mismatches=%d\n",original,cases,errors);
    return !errors;
}
static bool Both() {
    Redirect((void*)0x56ae90,(void*)Notice); Redirect((void*)Notice_Show,(void*)Notice);
    Redirect((void*)0x56ae60,(void*)Message); Redirect((void*)Messages_Add,(void*)Message);
    Redirect((void*)0x61cca9,(void*)sprintf);
    const bool ref=Check(true);
    const bool got=Check(false);
    return ref && got;
}
}
