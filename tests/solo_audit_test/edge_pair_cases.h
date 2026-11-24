extern "C" BYTE* __fastcall GridEdge_Construct(BYTE*,void*);
extern "C" DWORD __fastcall GridEdge_SetPairs(BYTE*,void*,BYTE*,BYTE*);
extern "C" void __fastcall GridEdge_Destruct(BYTE*,void*);
namespace edge_pairs {
struct State { BYTE edge[100], groups[2][16]; DWORD accepted; };

bool Check() {
    BYTE edge[100], groups[2][16], branches[4][20], triangles[4][60];
    int cases=0, errors=0;
    for (int mode=0;mode<5;++mode) for (int flip=0;flip<4;++flip) for (int score=0;score<2;++score) {
        State original[3]={}, comparison[3]={};
        for (int side=0;side<2;++side) {
            memset(edge,0x5a,sizeof edge); memset(groups,0,sizeof groups);
            memset(branches,0,sizeof branches); memset(triangles,0,sizeof triangles);
            for (int i=0;i<4;++i) {
                F(BYTE*,groups[i/2],4*(i%2))=branches[i]; F(BYTE*,groups[i/2],8)=edge;
                F(BYTE*,branches[i],12)=triangles[i];
                F(DWORD,triangles[i],16)=100+i; F(DWORD,triangles[i],24)=300+i;
                if ((flip>>(i/2))&1) F(BYTE*,triangles[i],48)=triangles[i^1];
            }
            BYTE* second=mode==0 ? nullptr : groups[1];
            if (mode==2) F(BYTE*,groups[0],8)=nullptr;
            if (mode==3) F(BYTE*,groups[1],4)=nullptr;
            if (mode==4) F(BYTE*,branches[3],12)=nullptr;
            State* snapshots=side ? comparison : original;
            auto snap=[&](int stage,DWORD accepted) {
                State& s=snapshots[stage];
                memcpy(s.edge,edge,sizeof edge); memcpy(s.groups,groups,sizeof groups);
                s.accepted=accepted;
                // Map only the three known vtable addresses to the same token.
                BYTE* vt=side ? KingRdata+(0x652e48-0x64b000) : (BYTE*)0x652e48;
                for (int offset : {16,44,76}) if (F(BYTE*,s.edge,offset)==vt) F(DWORD,s.edge,offset)=1;
            };
            using Ctor=BYTE* (__thiscall*)(BYTE*);
            using Assign=DWORD (__thiscall*)(BYTE*,BYTE*,BYTE*);
            using Dtor=void (__thiscall*)(BYTE*);
            BYTE* result=side ? GridEdge_Construct(edge,0) : ((Ctor)0x606100)(edge);
            snap(0,result==edge);
            F(float,edge,32)=score ? 5.25f : -1.0f; F(float,edge,60)=2.0f;
            DWORD rc=side ? GridEdge_SetPairs(edge,0,groups[0],second) : ((Assign)0x606170)(edge,groups[0],second);
            snap(1,rc);
            if (side) GridEdge_Destruct(edge,0); else ((Dtor)0x606750)(edge);
            snap(2,0);
        }
        if (memcmp(original,comparison,sizeof original) || !original[0].accepted ||
            original[1].accepted!=(mode<=1 ? 1u : 0u)) {
            if (++errors<=4) printf("edge assignment mode=%d orientation=%d score=%d accepted=%u/%u\n",mode,flip,score,original[1].accepted,comparison[1].accepted);
        }
        ++cases;
    }
    printf("marine edge lifecycle cases=%d mismatches=%d\n",cases,errors);
    return !errors;
}
}
extern "C" int __fastcall MarineTransition_Advance(BYTE*,void*,DWORD);
extern "C" DWORD* __cdecl MarineGeometry_Interpolate(DWORD*,const DWORD*,const DWORD*,float);
extern "C" DWORD __fastcall GridTriangle_SetPoints(BYTE*,void*,DWORD,DWORD,DWORD,DWORD);
namespace transitions {
bool CheckRecords() {
    using Points=DWORD (__thiscall*)(BYTE*,DWORD,DWORD,DWORD,DWORD);
    BYTE a[24],b[24]; memset(a,0x5a,sizeof a); memcpy(b,a,sizeof b);
    DWORD ok=((Points)0x61c1b0)(a,17,29,31,43);
    if (ok!=GridTriangle_SetPoints(b,0,17,29,31,43) || memcmp(a,b,sizeof a)) return false;
    DWORD source[10],target[10],ref[10],got[10];
    for (int j=0;j<10;++j) {
        F(float,source,4*j)=float(j-5)*0.375f;
        F(float,target,4*j)=float(8-j)*1.25f;
    }
    for (float t : {0.0f,0.125f,0.5f,0.875f,1.0f}) {
        auto original=(decltype(&MarineGeometry_Interpolate))0x61c350;
        if (original(ref,source,target,t)!=ref || MarineGeometry_Interpolate(got,source,target,t)!=got ||
            memcmp(ref,got,sizeof ref)) return false;
    }
    return true;
}
bool Check() {
    BYTE transition[104]={}, registration[16]={}, mesh[80]={};
    DWORD attributes[3][10];
    WORD index=1;
    F(WORD*,registration,0)=&index; F(BYTE*,transition,16)=registration;
    F(BYTE*,transition,100)=mesh; F(void*,mesh,16)=attributes;
    int cases=0,errors=0;
    for (DWORD start : {128u,0x7ffffffdu,0xfffffffdu}) for (DWORD duration : {5u,100u,250u})
        for (DWORD step : {0ul,1ul,duration/2,duration,duration+1}) {
            F(DWORD,transition,8)=start; F(DWORD,transition,12)=duration;
            for (int j=0;j<10;++j) {
                F(float,transition,20+4*j)=float(2*j-7)*0.75f;
                F(float,transition,60+4*j)=float(12-j)*1.25f;
            }
            using Advance=int (__thiscall*)(BYTE*,DWORD);
            memset(attributes,0x5a,sizeof attributes);
            int ref=((Advance)0x60f1c0)(transition,start+step);
            DWORD refVertices[3][10]; memcpy(refVertices,attributes,sizeof attributes);
            memset(attributes,0x5a,sizeof attributes);
            int got=MarineTransition_Advance(transition,0,start+step);
            if (got!=ref || memcmp(refVertices,attributes,sizeof attributes)) {
                if (++errors<=4) printf("transition start=%08x duration=%u elapsed=%u done=%d/%d\n",start,duration,step,ref,got);
            }
            ++cases;
        }
    printf("vertex transition cases=%d mismatches=%d\n",cases,errors);
    return !errors;
}
}
