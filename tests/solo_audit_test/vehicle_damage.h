extern "C" void __fastcall Car_CheckDamage(BYTE*,void*);

namespace vehicle_damage {
static bool Check() {
    const auto original=reinterpret_cast<decltype(&Car_CheckDamage)>(0x501930);
    // Use distinct, exactly representable entries to expose the selected
    // angle bin independently of CRT sine-table initialization.
    for (int i=0; i<360; ++i) gameplay::SetWorld(0x696ec0+4*i,float(i-180)/256.f);
    int cases=0, errors=0;
    for (int count : {0,2,4,6,8}) for (int mode : {0,1,2}) for (int gears : {2,6,12})
        for (int damage=0; damage<4; ++damage) for (int variant=0; variant<4; ++variant) {
            alignas(double) BYTE ref[17000]={}, got[sizeof ref];
            F(int,ref,3540)=count; F(int,ref,7684)=gears; F(int,ref,15540)=mode;
            for (int j=0; j<3; ++j) F(float,ref,7708+4*j)=(j+1)*.125f;
            F(float,ref,16216)=.25f; F(float,ref,16228)=1.f; F(float,ref,16252)=variant*.25f;
            for (int i=0; i<8; ++i) {
                F(float,ref,3740+4*i)=(i-4)*1.125f+variant*.25f;
                F(float,ref,7520+4*i)=.3125f+i*.125f;
                F(float,ref,7552+4*i)=.75f+i*.0625f;
                F(float,ref,7812+4*i)=.125f*(i+1);
                F(int,ref,15580+20*i)=(damage+i)%4; F(int,ref,15584+20*i)=i%3;
                F(int,ref,15900+20*i)=(i+variant)%2;
            }
            memcpy(got,ref,sizeof ref);
            original(ref,nullptr); Car_CheckDamage(got,nullptr);
            if (memcmp(ref,got,sizeof ref)) {
                int byte=0; while (ref[byte]==got[byte]) ++byte;
                if (!errors) printf("damage mismatch wheels=%d mode=%d gears=%d state=%d variant=%d offset=%d\n",count,mode,gears,damage,variant,byte);
                ++errors;
            }
            ++cases;
        }
    printf("wheel damage and transmission cases=%d mismatches=%d\n",cases,errors);
    return !errors;
}
}
