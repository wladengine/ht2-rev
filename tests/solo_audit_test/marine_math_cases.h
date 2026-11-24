// Angle lookup and transformations used by the marine mesh. Drive both sets of
// tables through their own initializers before comparing the lookups.
extern "C" void __cdecl GspInit_60b9b0();
extern "C" void __cdecl Tables_InitSin();
extern "C" void __cdecl Tables_InitCos();
extern "C" double __cdecl MarineAngle_SignedCosine(float);
extern "C" double __cdecl MarineAngle_SineOfMagnitude(float);
extern "C" DWORD* __cdecl MarineInit_Identity(DWORD*);
extern "C" DWORD* __cdecl MarineMatrix_Zero(DWORD*);
extern "C" DWORD* __cdecl MarineMatrix_RotateZ(DWORD*,float);
extern "C" DWORD* __cdecl MarineMatrix_Translate(DWORD*,DWORD,DWORD,DWORD);
extern "C" DWORD* __cdecl MarineMatrix_Multiply(DWORD*,const float*,const float*);
extern "C" void __fastcall MarineMaterial_UpdateFrame(BYTE*,void*,DWORD);

namespace marine_math {
bool CheckAngles() {
    using Initialize=void (__cdecl*)();
    ((Initialize)0x60b9b0)(); ((Initialize)0x60ba20)(); ((Initialize)0x60bab0)();
    GspInit_60b9b0(); Tables_InitSin(); Tables_InitCos();
    int errors=0, cases=0;
    for (unsigned va : {0x7243bc,0x724964})
        for (int i=0;i<360;++i)
            errors+=F(DWORD,(BYTE*)va,4*i)!=F(DWORD,KingBss+va-0x68a820,4*i);
    using Lookup=double (__cdecl*)(float);
    for (int i=-720;i<=720;++i) {
        float angle=i/45.0f;
        const Lookup original[]={(Lookup)0x60ba60,(Lookup)0x60b9e0};
        const Lookup candidate[]={MarineAngle_SignedCosine,MarineAngle_SineOfMagnitude};
        for (int j=0;j<2;++j) {
            double ref=original[j](angle), got=candidate[j](angle);
            if (memcmp(&ref,&got,8)) ++errors;
            ++cases;
        }
    }
    printf("marine angle samples=720 lookups=%d mismatches=%d\n",cases,errors);
    return !errors;
}

bool CheckTransforms() {
    int errors=0, cases=0;
    for (int variant=0;variant<16;++variant) {
        DWORD reference[16], left[16], right[16];
        for (int side=0;side<2;++side) {
            DWORD out[16], translated[16], rotated[16];
            auto translate=side?MarineMatrix_Translate:(decltype(&MarineMatrix_Translate))0x60a960;
            auto rotate=side?MarineMatrix_RotateZ:(decltype(&MarineMatrix_RotateZ))0x60a900;
            auto multiply=side?MarineMatrix_Multiply:(decltype(&MarineMatrix_Multiply))0x60a9b0;
            float offset[]={variant*1.125f,-variant*0.5f,variant+0.25f};
            if (translate(translated,F(DWORD,offset,0),F(DWORD,offset,4),F(DWORD,offset,8))!=translated ||
                rotate(rotated,(variant-8)*0.25f)!=rotated) return false;
            if (!side) { memcpy(left,translated,64); memcpy(right,rotated,64); }
            else if (memcmp(left,translated,64) || memcmp(right,rotated,64)) ++errors;
            DWORD* dest=variant%3==0 ? translated : variant%3==1 ? rotated : out;
            DWORD* a=variant&1 ? translated : rotated;
            DWORD* b=variant&1 ? rotated : translated;
            if (multiply(dest,(float*)a,(float*)b)!=dest) return false;
            if (!side) memcpy(reference,dest,64);
            else if (memcmp(reference,dest,64)) ++errors;
        }
        ++cases;
    }
    printf("marine transforms cases=%d mismatches=%d\n",cases,errors);
    return !errors;
}

bool CheckFrames() {
    int errors=0, cases=0;
    using Update=void (__thiscall*)(BYTE*,DWORD);
    for (DWORD count : {0u,1u,3u,8u}) for (DWORD ticks : {1u,5u,40u})
        for (DWORD now : {0xfffffff0u,0xfffffff1u,0xffffffffu,0u,0x100u}) {
            DWORD ref[]={count,0xfffffff0u,ticks,99}, got[4];
            memcpy(got,ref,sizeof got);
            ((Update)0x6057b0)((BYTE*)ref,now); MarineMaterial_UpdateFrame((BYTE*)got,0,now);
            errors+=memcmp(ref,got,sizeof ref)!=0; ++cases;
        }
    printf("marine material clock cases=%d mismatches=%d\n",cases,errors);
    return !errors;
}
}
