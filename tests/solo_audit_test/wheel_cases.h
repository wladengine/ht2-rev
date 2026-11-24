// Record the wheel emission boundary. Road collision queries and the consumer
// are isolated, while both original vehicle branches and transforms run.
extern "C" void __fastcall Car_Effects(BYTE*,void*);
extern "C" void* __cdecl Query_HitObject(void*,const float*,const float*);
extern "C" void __cdecl Fx_WheelDust(const float*,const float*,float);

namespace wheel_effects {
struct Emission { float position[3], direction[3], strength; };
struct Output { int count, queries; Emission wheels[4]; };
static Output current;
static void* __cdecl NoHit(void*,const float*,const float*) { ++current.queries; return nullptr; }
static void __cdecl Record(const float* position,const float* direction,float strength) {
    if (++current.count>4) return;
    Emission& e=current.wheels[current.count-1];
    memcpy(e.position,position,12); memcpy(e.direction,direction,12); e.strength=strength;
}
bool Check() {
    Redirect((void*)0x4e5ba7,(void*)NoHit); Redirect((void*)Query_HitObject,(void*)NoHit);
    Redirect((void*)0x4bb050,(void*)Record); Redirect((void*)Fx_WheelDust,(void*)Record);
    static BYTE car[22000], wheels[12200], settings[1400], effectState[64];
    gameplay::SetWorld(0x6cec6c,settings+0);
    gameplay::SetWorld(0x702640,1.0f);
    F(BYTE*,car,21568+20)=effectState;
    F(int,car,20900)=0x100; F(int,car,10428)=4;
    int errors=0,cases=0;
    for (int external=0;external<=1;++external) for (int heading=0;heading<4;++heading)
        for (int mask : {0,5,10,15}) {
            const float basis[][2]={{1,0},{0,1},{-1,0},{0,-1}};
            for (int off : {516,708}) {
                float* m=(float*)(car+off);
                memset(m,0,48); m[0]=m[4]=m[8]=1;
            }
            float* m=(float*)(car+516);
            m[0]=m[4]=basis[heading][0]; m[1]=basis[heading][1]; m[3]=-m[1];
            m[9]=18.0f; m[10]=-8.0f; m[11]=0.5f;
            F(BYTE,car,10273)=(BYTE)mask;
            F(BYTE*,car,21600)=external ? wheels : nullptr;
            for (int i=0;i<4;++i) {
                int k=3-i;
                F(int,car,20400+4*i)=k;
                gameplay::SetWorld(0x697914-76+328+12*k,k&1 ? -1.25f : 1.25f);
                F(float,car,10224+4*i)=0.5f+i;
                for (int j=0;j<3;++j) {
                    F(float,car,10284+12*i+4*j)=float(3*i-j);
                    F(float,wheels,11172+248*i+4*j)=float(10*i+j);
                }
            }
            using Effects=void (__thiscall*)(BYTE*);
            current={}; ((Effects)0x551970)(car); Output ref=current;
            current={}; Car_Effects(car,0);
            if (memcmp(&current,&ref,sizeof ref) || current.queries!=1 || current.count>4 ||
                (external && !current.count) || (!external && mask==0 && current.count)) {
                if (++errors<=3) printf("wheel source ext=%d heading=%d mask=%d emits=%d/%d queries=%d/%d\n",external,heading,mask,ref.count,current.count,ref.queries,current.queries);
            }
            ++cases;
        }
    printf("wheel emission cases=%d mismatches=%d\n",cases,errors);
    return !errors;
}
}
extern "C" BYTE* __cdecl Pool_Prepare(int,BYTE*);
extern "C" BYTE* __fastcall Spark_Construct2(BYTE*,void*,BYTE*,const float*,const float*,float,float,int,unsigned,int,float);
namespace wheel_effects {
static BYTE particle[200], camera[3000], weather[700];
struct Spawn { Emission emission; float width, height; int allocations, created; };
static Spawn spawn;
static BYTE* __cdecl Allocate(int size,BYTE*) { ++spawn.allocations; return size==sizeof particle ? particle : nullptr; }
static BYTE* __fastcall Construct(BYTE* self,void*,BYTE* view,const float* p,const float* v,float width,float height,int,unsigned,int,float) {
    spawn.created+=(self==particle && view==camera) ? 1 : 100;
    memcpy(spawn.emission.position,p,12); memcpy(spawn.emission.direction,v,12);
    spawn.width=width; spawn.height=height;
    return self;
}
bool CheckVelocities() {
    // The two passes consume one seeded CRT stream each. Keep all production
    // gates, strength scaling, vector arithmetic and count limits intact.
    Redirect((void*)0x61edde,(void*)rand);
    Redirect((void*)0x579380,(void*)Allocate); Redirect((void*)Pool_Prepare,(void*)Allocate);
    Redirect((void*)0x59d790,(void*)Construct); Redirect((void*)Spark_Construct2,(void*)Construct);
    gameplay::SetWorld(0x696224,camera+0); gameplay::SetWorld(0x6f33ac,weather+0);
    F(float,weather,544)=1; F(double,camera,2960)=0.0625;
    gameplay::SetWorld(0x69603c,1.0f); gameplay::SetWorld(0x671700,100u);
    const float at[]={12.5f,-4.125f,2.0f};
    const float directions[][3]={{0,1,0},{0,-1,0},{-0.75f,0.5f,0.25f},{0.75f,-0.5f,0.25f}};
    using Emit=decltype(&Fx_WheelDust);
    int cases=0, errors=0, emitted=0;
    for (const auto& v : directions) for (float strength : {0.125f,1.0f,2.0f})
        for (unsigned seed : {1u,7u,551u,9000u,20000u}) for (unsigned active : {0u,1000u}) {
            gameplay::SetWorld(0x6f6a6c,active);
            srand(seed); spawn={}; ((Emit)0x4bb050)(at,v,strength); Spawn ref=spawn; int refNext=rand();
            srand(seed); spawn={}; Fx_WheelDust(at,v,strength); int next=rand();
            if (memcmp(&ref,&spawn,sizeof ref) || refNext!=next || spawn.created>1) {
                if (++errors<=4) printf("wheel velocity seed=%u limit=%u strength=%g emits=%d/%d next=%d/%d\n",seed,active,strength,ref.created,spawn.created,refNext,next);
            }
            emitted+=spawn.created; ++cases;
        }
    printf("wheel velocities cases=%d emissions=%d mismatches=%d\n",cases,emitted,errors);
    return !errors && emitted>0 && emitted<cases;
}
}
extern "C" BYTE* __fastcall MotionTexture_Construct(BYTE*,void*,int,BYTE*,DWORD,DWORD,DWORD,float,float,DWORD*,int,int,int);
extern "C" void __cdecl Dust_Puffs(const float*,const float*,float,const float*,int);

namespace dust_puffs {
struct Pair { BYTE puffs[2][188]; DWORD base[2][11]; int allocations, creations; };
static Pair observed;
static BYTE* __cdecl Reserve(int bytes,BYTE*) {
    if (bytes!=188 || observed.allocations>=2) return nullptr;
    return observed.puffs[observed.allocations++];
}
static BYTE* __fastcall Base(BYTE* self,void*,int scene,BYTE* list,DWORD x,DWORD y,DWORD z,float w,float h,DWORD* owner,int t,int c,int d) {
    int index=observed.creations++;
    if (index>=2) return self;
    DWORD material=(DWORD)owner;
    DWORD originalMaterial=(DWORD)(KingBss+(0x695cc0-0x68a820));
    if (material>=originalMaterial && material<=originalMaterial+8) material=0x695cc0+material-originalMaterial;
    DWORD args[]={(DWORD)scene,(DWORD)list,x,y,z,F(DWORD,&w,0),F(DWORD,&h,0),material,(DWORD)t,(DWORD)c,(DWORD)d};
    memcpy(observed.base[index],args,sizeof args);
    return self;
}
bool Check() {
    Redirect((void*)0x579380,(void*)Reserve); Redirect((void*)Pool_Prepare,(void*)Reserve);
    Redirect((void*)0x59a780,(void*)Base); Redirect((void*)MotionTexture_Construct,(void*)Base);
    BYTE camera[3000]={};
    gameplay::SetWorld(0x696224,camera+0);
    const float point[]={32.5f,12.0f,4.125f};
    using Emit=decltype(&Dust_Puffs);
    int cases=0, created=0, errors=0;
    for (int kind=0;kind<3;++kind) for (int sign : {-1,1}) for (int angle : {-1,0,1})
        for (double distance : {1.5,45.0,225.0}) for (bool surface : {false,true}) {
            float dir[]={float(angle*sign),0.0f,0.125f};
            float plane[16]={};
            if (surface) {plane[13]=1;plane[15]=3.75f;}
            F(double,camera,2904)=point[0]+sign*distance; F(double,camera,2912)=point[1]+0.25;
            F(double,camera,2920)=point[2]+0.125;
            Pair ref={};
            for (int side=0;side<2;++side) {
                observed={};
                auto emit=side ? Dust_Puffs : (Emit)0x4cb560;
                emit(point,dir,1.75f,plane,kind);
                for (int i=0;i<observed.allocations;++i) {
                    BYTE* base=side ? KingRdata : (BYTE*)0x64b000;
                    for (int offset : {0,20}) F(DWORD,observed.puffs[i],offset)-=(DWORD)base;
                }
                if (!side) ref=observed;
            }
            if (memcmp(&ref,&observed,sizeof ref) || ref.allocations!=ref.creations) {
                if (!errors) for (unsigned i=0;i<sizeof ref/4;++i) if (((DWORD*)&ref)[i]!=((DWORD*)&observed)[i]) printf("puff word %d %08x/%08x\n",i,((DWORD*)&ref)[i],((DWORD*)&observed)[i]);
                if (++errors<=4) printf("puff kind=%d angle=%d distance=%g emitted=%d/%d\n",kind,angle,distance,ref.creations,observed.creations);
            }
            created+=ref.creations; ++cases;
        }
    printf("dust pairs cases=%d puffs=%d mismatches=%d\n",cases,created,errors);
    return !errors && created>0 && created<cases*2;
}
}
