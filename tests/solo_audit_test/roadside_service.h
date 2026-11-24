extern "C" BYTE* __cdecl Objects_Find(const char*);
extern "C" int __cdecl Table71e858_Get(int);
extern "C" void __fastcall Messages_AddText(BYTE*,void*,const char*);
extern "C" void __fastcall Sound_Restart(BYTE*,void*,float,float);
extern "C" int __fastcall King_53b3c0(BYTE*,void*,int);
extern "C" void __fastcall Shell_Refuel(BYTE*,void*,int);

namespace roadside_service {
struct Receipt {
    double money, ordered;
    float tank, damageCommand;
    int sound, charge, services, repaint, damageCalls, status;
    short slider;
};
static Receipt obs;
static double initialDamage;
static int __cdecl Format(int) { return int("%d"); }
static void __fastcall Invoice(BYTE*,void*,const char* s) { obs.charge=atoi(s); }
static BYTE* __cdecl Sample(const char*) { static BYTE sound; return &sound; }
static void __fastcall Play(BYTE*,void*,float,float) { ++obs.sound; }
static double __fastcall Damage(BYTE*,void*) { return initialDamage; }
static void __fastcall Service(BYTE*,void*) { ++obs.services; }
static void __fastcall Repaint(BYTE*,void*) { ++obs.repaint; }
static int __fastcall Apply(BYTE*,void*,int bits) { memcpy(&obs.damageCommand,&bits,4); ++obs.damageCalls; return 0; }

static Receipt Run(bool original,int id,double money,float level,int mode) {
    company_world::World w; w.Init();
    alignas(double) BYTE scene[1344]={},client[640]={},car[22000]={},cab[16200]={},panel[350]={};
    void* methods[31]={};
    methods[84/4]=(void*)Damage; methods[116/4]=(void*)Service; methods[120/4]=(void*)Repaint;
    F(void**,car,0)=methods; F(BYTE*,client,616)=car; F(BYTE*,car,20636)=w.human;
    F(BYTE*,car,21600)=cab;
    gameplay::SetWorld(0x6cec6c,scene+0); gameplay::SetWorld(0x6d2078,client+0);
    gameplay::SetWorld(0x6f33f4,0); gameplay::SetWorld(0x6f3430,(BYTE*)nullptr);
    F(int,scene,1300)=mode==2 ? 2 : 0; F(int,scene,1304)=mode!=1;
    F(int,scene,1316)=mode==3;
    F(double,w.human,136)=money;
    F(float,cab,16112+12)=100.f; F(float,cab,16112+36)=level;
    F(double,panel,312)=level*100.; F(double,panel,320)=100.;
    F(short,panel,336)=25;
    initialDamage=level;
    obs={}; obs.charge=-1;
    if (id==275 || id==276) {
        const auto command=original ? reinterpret_cast<decltype(&Shell_Refuel)>(0x520ce0) : Shell_Refuel;
        command(panel,nullptr,id);
        obs.ordered=F(double,panel,304); obs.slider=F(short,panel,336);
    } else {
        int msg[4]={id};
        const auto send=original ? reinterpret_cast<decltype(&Game_PlayerMessage)>(0x56ffd0) : Game_PlayerMessage;
        obs.status=send(msg);
    }
    obs.tank=F(float,cab,16112+36); obs.money=F(double,w.human,136);
    return obs;
}

static bool Check() {
    Redirect((void*)0x5d2e50,(void*)Format); Redirect((void*)Table71e858_Get,(void*)Format);
    Redirect((void*)0x5699b0,(void*)Invoice); Redirect((void*)Messages_AddText,(void*)Invoice);
    Redirect((void*)0x5ef3d0,(void*)Sample); Redirect((void*)Objects_Find,(void*)Sample);
    Redirect((void*)0x5fdaf0,(void*)Play); Redirect((void*)Sound_Restart,(void*)Play);
    Redirect((void*)0x53b3c0,(void*)Apply); Redirect((void*)King_53b3c0,(void*)Apply);
    int cases=0, errors=0, fills=0, repairs=0;
    for(int id : {14,21,15,275,276}) for(double money : {0.,1.,1.75,50.,100.,10000.})
        for(float level : {0.f,.01f,.125f,.5f,.95f,1.f}) for(int mode=0; mode<4; ++mode) {
            const Receipt ref=Run(true,id,money,level,mode), got=Run(false,id,money,level,mode);
            if(ref.tank!=level) ++fills;
            repairs+=ref.damageCalls;
            if (ref.money!=got.money || ref.ordered!=got.ordered || ref.tank!=got.tank ||
                ref.damageCommand!=got.damageCommand || ref.sound!=got.sound || ref.charge!=got.charge ||
                ref.services!=got.services || ref.repaint!=got.repaint || ref.damageCalls!=got.damageCalls ||
                ref.status!=got.status || ref.slider!=got.slider) {
                if (!errors) printf("service mismatch id=%d cash=%g level=%g mode=%d money=%g/%g tank=%g/%g charge=%d/%d damage=%g/%g\n",
                    id,money,level,mode,ref.money,got.money,ref.tank,got.tank,ref.charge,got.charge,ref.damageCommand,got.damageCommand);
                ++errors;
            }
            ++cases;
        }
    printf("fuel and roadside service cases=%d fills=%d repairs=%d mismatches=%d\n",cases,fills,repairs,errors);
    return !errors && fills && repairs;
}
}
