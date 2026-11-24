extern "C" void __fastcall Fx_FireStep(BYTE*,void*);
extern "C" BYTE* __fastcall Spark59ad70_Construct(BYTE*,void*,int,const float*,const float*,int,int,float);
extern "C" BYTE* __fastcall Spark59bed0_Construct(BYTE*,void*,int,const float*,const float*,int,int,float);
extern "C" BYTE* __fastcall Flash_Construct(BYTE*,void*,int,const float*,const float*,int,int,float,float);
extern "C" int __fastcall Queue_PostEvent(BYTE*,void*,int);

namespace fire_frames {
struct Frame {
    BYTE fire[72];
    float rgb[3], velocity[3], strength;
    int emitted, ended, queued, nextRandom;
};
static Frame frame;
static BYTE* __cdecl Reserve(int,BYTE*) { static BYTE effect[212]; return effect; }
static BYTE* __fastcall Puff(BYTE* p,void*,int,const float*,const float* velocity,int,int,float strength) {
    frame.emitted=1; frame.strength=strength;
    memcpy(frame.velocity,velocity,sizeof frame.velocity);
    return p;
}
static BYTE* __fastcall Vapour(BYTE* p,void* edx,int view,const float* at,const float* velocity,int texture,int count,float strength) {
    Puff(p,edx,view,at,velocity,texture,count,strength); frame.emitted=2; return p;
}
static BYTE* __fastcall Flash(BYTE* p,void* edx,int view,const float* at,const float* velocity,int texture,int count,float strength,float) {
    Puff(p,edx,view,at,velocity,texture,count,strength); frame.emitted=3; return p;
}
static void __fastcall End(BYTE*,void*,int arg) { frame.ended+=arg; }
static int __fastcall Queue(BYTE*,void*,int arg) { frame.queued+=arg; return 0; }
static Frame Run(bool original,double dt,float distance,float age,bool sparks,unsigned seed) {
    const auto step=original ? reinterpret_cast<decltype(&Fx_FireStep)>(0x4d9f40) : Fx_FireStep;
    BYTE scene[3000]={}, fire[108]={}, lamp[144]={};
    void* vtable[8]={}; vtable[7]=(void*)End;
    F(void**,fire,0)=vtable; F(BYTE*,fire,32)=scene;
    F(double,scene,2960)=dt; F(double,scene,2904)=distance;
    F(float,fire,48)=.75f; F(float,fire,52)=-.5f; F(float,fire,56)=.25f;
    F(float,fire,60)=1.; F(float,fire,64)=age; F(float,fire,68)=.25f; F(float,fire,72)=.125f;
    F(float,fire,76)=.25f; F(float,fire,80)=.5f; F(float,fire,84)=.75f;
    F(float,fire,88)=-.25f; F(float,fire,92)=.25f; F(float,fire,96)=1.25f;
    F(int,fire,100)=int(sparks); F(float,fire,104)=.5f;
    gameplay::SetWorld(0x696734,lamp+0);
    frame={}; srand(seed); step(fire,nullptr);
    frame.nextRandom=rand();
    memcpy(frame.fire,fire+36,sizeof fire-36);
    memcpy(frame.rgb,lamp+108,sizeof frame.rgb);
    return frame;
}
static bool Check() {
    Redirect((void*)0x579380,(void*)Reserve); Redirect((void*)Pool_Prepare,(void*)Reserve);
    Redirect((void*)0x59ad70,(void*)Puff); Redirect((void*)Spark59ad70_Construct,(void*)Puff);
    Redirect((void*)0x59bed0,(void*)Vapour); Redirect((void*)Spark59bed0_Construct,(void*)Vapour);
    Redirect((void*)0x59b3f0,(void*)Flash); Redirect((void*)Flash_Construct,(void*)Flash);
    Redirect((void*)0x5d32f0,(void*)Queue); Redirect((void*)Queue_PostEvent,(void*)Queue);
    Redirect((void*)0x61edde,(void*)rand);
    gameplay::SetWorld(0x696078,1.f);
    gameplay::SetWorld(0x702640,Wc_V{1,0,0}); gameplay::SetWorld(0x702620,Wc_V{0,1,0});
    for(int i=0; i<31; ++i) {
        gameplay::SetWorld(0x695fb0+4*i,(i+1)/32.f);
        gameplay::SetWorld(0x695328+4*i,(31-i)/32.f);
        gameplay::SetWorld(0x695e30+4*i,(i+5)/64.f);
    }
    int cases=0, errors=0, culled=0, expired=0;
    for (double dt : {.03125,.125,.25}) for (float distance : {0.f,1.99f,2.f,10.f})
        for (float age : {0.f,.25f,.875f,1.f}) for (bool sparks : {false,true}) for (unsigned seed : {2u,19u}) {
            const Frame ref=Run(true,dt,distance,age,sparks,seed);
            const Frame got=Run(false,dt,distance,age,sparks,seed);
            const bool skip=1./dt<7. && distance<2.f, end=skip || dt+age>1.;
            culled+=skip; expired+=end && !skip;
            if (memcmp(&ref,&got,sizeof ref) || ref.ended!=int(end) || ref.queued!=int(!end) ||
                bool(ref.emitted)==skip) {
                if (!errors) {
                    unsigned byte=0; while (byte<sizeof ref && ((BYTE*)&ref)[byte]==((BYTE*)&got)[byte]) ++byte;
                    printf("fire mismatch dt=%g distance=%g age=%g sparks=%d seed=%u byte=%u events=%d/%d\n",
                        dt,distance,age,sparks,seed,byte,ref.ended,got.ended);
                    printf("emitted=%d/%d velocity=(%g,%g,%g)/(%g,%g,%g) colour=%g/%g strengths=%g/%g rng=%d/%d\n",
                        ref.emitted,got.emitted,ref.velocity[0],ref.velocity[1],ref.velocity[2],got.velocity[0],got.velocity[1],got.velocity[2],
                        ref.rgb[0],got.rgb[0],ref.strength,got.strength,ref.nextRandom,got.nextRandom);
                }
                ++errors;
            }
            ++cases;
        }
    printf("fire frames cases=%d nearCulled=%d expired=%d mismatches=%d\n",cases,culled,expired,errors);
    return !errors;
}
}
