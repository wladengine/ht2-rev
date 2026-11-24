extern "C" void __fastcall Body_InWater(BYTE*);

namespace water_contact {
static bool Check() {
    const auto original=reinterpret_cast<decltype(&Body_InWater)>(0x504930);
    gameplay::SetWorld(0x702630,Wc_V{0,0,1});
    int cases=0, errors=0, wet=0, deep=0;
    for (int wheels : {2,4,8}) for (int index : {0,1,2}) for (int kind : {-1,0,4,7})
        for (float level : {.5f,0.f,-.25f,-2.f,-3.f,-3.125f}) for (bool cab : {false,true}) {
            alignas(double) BYTE ref[15600]={}, got[sizeof ref];
            int other[]={1,2}, material[]={1,kind};
            F(int,ref,3540)=wheels; F(WORD,ref,10048)=cab;
            for (int j=0; j<wheels; ++j) F(int*,ref,380+248*j)=other;
            const int slot=index==2 ? wheels-1 : index;
            F(int*,ref,380+248*slot)=kind<0 ? nullptr : material;
            F(float,ref,48)=1.f; F(float,ref,60)=level;
            F(float,ref,3520)=.75f; F(float,ref,3320)=.5f; F(float,ref,3324)=.25f;
            F(Wc_V,ref,64)=Wc_V{1,-2,3}; F(Wc_V,ref,88)=Wc_V{-3,2,1};
            memcpy(got,ref,sizeof ref);
            original(ref); Body_InWater(got);
            const bool submerged=(kind==4 || kind==7) && level<0.f;
            const bool sunk=submerged && cab && level< -3.f;
            wet+=submerged; deep+=sunk;
            if (memcmp(ref,got,sizeof ref) || F(int,ref,15436)!=int(submerged) ||
                F(float,ref,15432)!=(submerged ? -level : 0.f) || F(int,ref,13124)!=int(sunk)) {
                if (!errors) printf("water mismatch wheels=%d contact=%d kind=%d level=%g cab=%d depth=%g/%g deep=%d/%d\n",
                    wheels,slot,kind,level,cab,F(float,ref,15432),F(float,got,15432),F(int,ref,13124),F(int,got,13124));
                ++errors;
            }
            ++cases;
        }
    printf("water contact cases=%d submerged=%d deepCabs=%d mismatches=%d\n",cases,wet,deep,errors);
    return !errors;
}
}
