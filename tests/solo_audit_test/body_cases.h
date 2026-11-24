// Run the hitch solver's body integrator with finite mass/inertia and empty
// terrain query. The following tick starts with the previous tick's output.
extern "C" void __fastcall Body_FrameIntegrate(BYTE*,void*,float);
namespace body_frames {
bool Check() {
    constexpr size_t bodyBytes=22000;
    static BYTE body[bodyBytes], snapshots[3][bodyBytes];
    const float identity[]={1,0,0,0,1,0,0,0,1,0,0,0};
    for (int i=0;i<12;++i) gameplay::SetWorld(0x7025f0+4*i,identity[i]);
    gameplay::SetWorld(0x702640,1.0f); gameplay::SetWorld(0x702624,1.0f); gameplay::SetWorld(0x702638,1.0f);
    int cases=0,errors=0,zeroSign=0,other=0; bool seen[bodyBytes/4]={};
    using Integrate=void (__thiscall*)(BYTE*,float);
    for (int contacts=0;contacts<3;++contacts) for (int turn : {0,1}) for (float dt : {0.015625f,0.0625f}) {
        for (int side=0;side<2;++side) {
            memset(body,0,sizeof body);
            for (int off : {16,2256,2304,2352,8868,8916,8964,10128}) memcpy(body+off,identity,48);
            F(float,body,9792)=6000; F(int,body,3540)=3;
            F(float,body,2568)=2.5f; F(float,body,2572)=-0.5f;
            F(float,body,3156)=0.125f;
            F(float,body,216)=1; F(float,body,2620)=1;
            for (int i=0;i<3;++i) F(float,body,8008+i*4)=float(1000+100*i);
            F(int,body,10176)=contacts; F(int,body,10180)=turn;
            for (int i=0;i<contacts;++i) {
                F(float,body,10244+12*i)=250.0f;
                F(float,body,10312+12*i)=-100.0f;
                F(float,body,10188+12*i)=turn ? float(i+1)*0.25f : 0;
            }
            F(float,body,10372)=turn ? 1.5f : 0;
            for (int tick=0;tick<3;++tick) {
                if (side) Body_FrameIntegrate(body,nullptr,dt); else ((Integrate)0x4fadb0)(body,dt);
                if (!side) memcpy(snapshots[tick],body,bodyBytes);
                else {
                    bool different=false;
                    for (size_t off=0;off<bodyBytes;off+=4) {
                        if (F(DWORD,body,off)==F(DWORD,snapshots[tick],off)) continue;
                        different=true;
                        DWORD a=F(DWORD,snapshots[tick],off), b=F(DWORD,body,off);
                        bool signOnly=(a|b)==0x80000000u;
                        if (signOnly) ++zeroSign; else ++other;
                        if (!seen[off/4]) {
                            seen[off/4]=true;
                            printf("body offset=%u contacts=%d turn=%d dt=%g tick=%d bits=%08x/%08x%s\n",(unsigned)off,contacts,turn,dt,tick,a,b,signOnly?" zero":"");
                        }
                    }
                    errors+=different;
                    ++cases;
                }
            }
        }
    }
    printf("body word deltas signed zero=%d other=%d\n",zeroSign,other);
    printf("body frame snapshots=%d mismatches=%d\n",cases,errors);
    return !errors;
}
}

// Controlled road cross-section, four tractor hubs, and the real load on
// the tractor's rear axle from a three- or five-wheel-set trailer.
struct Wc_V { float x,y,z; };
extern "C" int __fastcall Wheel_SegmentContact(BYTE*,void*,Wc_V*,Wc_V*,const float*,const Wc_V*,const Wc_V*,int);
extern "C" void __fastcall Body_SetLoad(BYTE*,void*,float);
extern "C" void __fastcall Tyre_Forces(BYTE*,void*,const float*);
extern "C" void __fastcall Wheels_OnPlane(BYTE*,void*,int,const Wc_V*,const Wc_V*);
namespace loaded_wheels {
struct Contact { Wc_V point,normal; int hit; };
struct State { BYTE tractor[11000]; Contact ground[4]; };
static bool Sample(bool original,int sets,float mass,float slope,int airborne,State& state) {
    BYTE trailer[11000]={};
    BYTE* truck=state.tractor;
    F(int,trailer,3540)=sets;
    F(float,trailer,7360)=-2.75f; F(float,trailer,7384)=-1.75f;
    F(float,trailer,7408)=0.375f; F(float,trailer,10144)=1;
    F(float,trailer,9792)=mass;
    const float identity[12]={1,0,0,0,1,0,0,0,1};
    memcpy(truck+2304,identity,sizeof identity);
    F(int,truck,3540)=4; F(float,truck,9792)=6000;
    F(float,truck,8032)=2; F(float,truck,8056)=2;
    F(float,truck,6740)=1; F(float,truck,6744)=-1;
    F(float,truck,3532)=0.5f; F(float,truck,3536)=0.25f;
    for (int i=0;i<4;++i) {
        const Wc_V hub={i%2 ? 1.f : -1.f,i<2 ? 2.f : -2.f,0.75f};
        const Wc_V a={-3,hub.y,-3*slope}, b={3,hub.y,3*slope};
        const float facing=i==airborne ? -1.f : 1.f;
        const Wc_V normal={-facing*slope,0,facing};
        F(Wc_V,truck,7356+12*i)=hub; F(Wc_V,truck,6212+12*i)=hub;
        F(Wc_V,truck,4132+248*i)=F(Wc_V,truck,260+248*i)=normal;
        Contact& c=state.ground[i];
        using ContactFn=int (__thiscall*)(BYTE*,Wc_V*,Wc_V*,const float*,const Wc_V*,const Wc_V*,int);
        c.hit=original ? reinterpret_cast<ContactFn>(0x4fbdd0)(truck,&c.point,&c.normal,identity,&a,&b,i)
                       : Wheel_SegmentContact(truck,nullptr,&c.point,&c.normal,identity,&a,&b,i);
        F(WORD,truck,6564+2*i)=c.hit==0;
        F(float,truck,8060+4*i)=1; F(float,truck,8092+4*i)=0.25f;
        F(float,truck,9840+4*i)=0.7f;
        F(float,truck,6500+4*i)=1; F(float,truck,6704+4*i)=0.125f;
        F(float,truck,2820+12*i)=1; F(float,truck,2916+12*i+4)=1;
        F(int,truck,7124+4*i)=i>=2;
        if (c.hit!=int(i!=airborne)) return false;
    }
    const float down[3]={0,0,-1};
    using Saddle=double (__thiscall*)(BYTE*,const float*);
    const float load=static_cast<float>(original ? reinterpret_cast<Saddle>(0x4eec80)(trailer,down)
                                                 : Trailer_SaddleLoad(trailer,nullptr,down));
    using SetLoad=void (__thiscall*)(BYTE*,float);
    if (original) reinterpret_cast<SetLoad>(0x4eeaf0)(truck,load);
    else Body_SetLoad(truck,nullptr,load);
    // Use static wheel loads on a supported wheel; the suspension's transient
    // response is deliberately outside this fixture.
    float supportedLoad[4];
    for (int i=0;i<4;++i) supportedLoad[i]=state.ground[i].hit ? F(float,truck,7484+4*i) : 0;
    using Forces=void (__thiscall*)(BYTE*,const float*);
    if (original) reinterpret_cast<Forces>(0x4f4f60)(truck,supportedLoad);
    else Tyre_Forces(truck,nullptr,supportedLoad);
    return load>0 && F(float,truck,7484+8)>F(float,truck,7484) &&
           F(float,truck,8436)>0 && F(float,truck,3328+8)>0;
}
static bool Check() {
    gameplay::SetWorld(0x702630,0.f); gameplay::SetWorld(0x702634,0.f);
    gameplay::SetWorld(0x702638,1.f); gameplay::SetWorld(0x697f9c+172,1.25f);
    int cases=0,errors=0;
    for (int sets : {3,5}) for (float mass : {3200.f,9600.f}) for (float slope : {0.f,.25f})
        for (int airborne : {-1,3}) {
            State expected={},got={};
            const bool ref=Sample(true,sets,mass,slope,airborne,expected);
            const bool candidate=Sample(false,sets,mass,slope,airborne,got);
            if (!ref || !candidate || memcmp(&expected,&got,sizeof got)) {
                if (++errors<=4) {
                    printf("contacts=%d,%d,%d,%d normal=%g load=%g front=%g rear=%g side=%g grip=%g\n",expected.ground[0].hit,expected.ground[1].hit,expected.ground[2].hit,expected.ground[3].hit,expected.ground[0].normal.z,F(float,expected.tractor,10120),F(float,expected.tractor,7484),F(float,expected.tractor,7492),F(float,expected.tractor,8436),F(float,expected.tractor,3336));
                    size_t k=0; while(k<sizeof got && reinterpret_cast<BYTE*>(&expected)[k]==reinterpret_cast<BYTE*>(&got)[k]) ++k;
                    printf("loaded wheels sets=%d mass=%g slope=%g lifted=%d valid=%d/%d offset=%u\n",sets,mass,slope,airborne,ref,candidate,unsigned(k));
                }
            }
            ++cases;
        }
    printf("loaded wheel contacts cases=%d mismatches=%d\n",cases,errors);
    return errors==0;
}
}
namespace suspension {
bool Check() {
    const float identity[12]={1,0,0,0,1,0,0,0,1};
    gameplay::SetWorld(0x702630,Wc_V{0,0,1});
    gameplay::SetWorld(0x702650,Wc_V{0,0,0});
    const float heights[]={0.f,0.125f,0.3125f,0.0625f,-0.25f,0.f};
    const Wc_V up{0,0,1};
    using Ground=void (__thiscall*)(BYTE*,int,const Wc_V*,const Wc_V*);
    int cases=0,errors=0,compressed=0,unsupported=0;
    for(int wheels : {3,4,5}) {
        static BYTE bodies[2][11000];
        memset(bodies,0,sizeof bodies);
        for(auto& b : bodies) {
            memcpy(b+16,identity,sizeof identity);
            memcpy(b+2304,identity,sizeof identity);
            F(int,b,3540)=wheels;
            for(int i=0;i<wheels;++i) {
                F(Wc_V,b,7356+12*i)={i%2 ? 1.f : -1.f,2.f-i,0.75f};
                F(float,b,7028+4*i)=0.5f;
                F(float,b,8092+4*i)=0.375f;
            }
        }
        for(unsigned tick=0;tick<sizeof heights/sizeof *heights;++tick) {
            Wc_V p{0,0,heights[tick]};
            for(int impl=0;impl<2;++impl) {
                if(impl) Wheels_OnPlane(bodies[1],nullptr,tick==0,&p,&up);
                else reinterpret_cast<Ground>(0x4f1fa0)(bodies[0],tick==0,&p,&up);
            }
            bool same=memcmp(bodies[0],bodies[1],sizeof bodies[0])==0;
            for(int i=0;i<wheels;++i) {
                float travel=0.25f-p.z;
                float observed=F(float,bodies[0],3544+4*i);
                bool airborne=F(WORD,bodies[0],6564+2*i)!=0;
                same &= airborne==(travel>0.375f) && observed==min(max(travel,0.f),0.375f);
                compressed+=observed==0.f;
                unsupported+=airborne;
            }
            if(!same) { ++errors; printf("suspension wheels=%d tick=%u z=%g travel=%g\n",wheels,tick,p.z,F(float,bodies[0],3544)); }
            ++cases;
        }
    }
    printf("suspension road steps=%d bottomed=%d unsupported=%d mismatches=%d\n",cases,compressed,unsupported,errors);
    return !errors && compressed && unsupported;
}
}
extern "C" void __fastcall Car_HitchImpulse(BYTE*,void*);
namespace hitch_events {
static BYTE truck[22400], trailer[22400];
struct Response { int count; DWORD values[4][4]; int flags[2]; };
static Response current;
static void Record(BYTE* b,const float* v,int extra,int kind) {
    if (current.count>=4 || extra!=0 || (b!=truck+16 && b!=trailer+16)) { current.count=99; return; }
    auto& slot=current.values[current.count++];
    slot[0]=kind+(b==trailer+16 ? 2 : 0); memcpy(slot+1,v,12);
}
static void __fastcall Linear(BYTE* b,void*,const float* v,int extra) { Record(b,v,extra,0); }
static void __fastcall Angular(BYTE* b,void*,const float* v,int extra) { Record(b,v,extra,1); }
bool Check() {
    void* methods[39]={}; methods[36]=(void*)Linear; methods[38]=(void*)Angular;
    const float identity[12]={1,0,0,0,1,0,0,0,1};
    const float rotation[12]={0,1,0,-1,0,0,0,0,1};
    const float arms[2][3]={{0.375f,-1.5f,0.25f},{-0.5f,1.25f,0.75f}};
    for(int i=0;i<3;++i) {
        gameplay::SetWorld(0x697f9c+156+4*i,arms[0][i]);
        gameplay::SetWorld(0x697f9c+2416+168+4*i,arms[1][i]);
    }
    gameplay::SetWorld(0x697f9c+204,0.125f);
    using Impulse=void (__thiscall*)(BYTE*);
    int cases=0,errors=0,active=0;
    for(unsigned precision : {_PC_53,_PC_64}) {
        unsigned previous=_controlfp(0,0);
        _controlfp(precision,_MCW_PC);
        for (int mode=0;mode<3;++mode) for (int orientation=0;orientation<2;++orientation) {
            gameplay::SetWorld(0x67a4a8,mode);
            Response samples[2][3]={};
            for(int impl=0;impl<2;++impl) {
                memset(truck,0,sizeof truck); memset(trailer,0,sizeof trailer);
                F(BYTE*,truck,21880)=trailer; F(int,trailer,10420)=1;
                for (BYTE* obj : {truck,trailer}) {
                    F(void**,obj,16)=methods;
                    F(float,obj,3284)=obj==truck ? 6500.f : 3200.f;
                    for(int off : {708,1808,1856})
                        memcpy(obj+off,(orientation && obj==trailer && off!=1856) ? rotation : identity,48);
                    for(int k=0;k<3;++k) {
                        F(float,obj,3304+4*k)=(obj==truck ? 1.3f : 2.1f)*(k+1);
                        F(float,obj,(obj==truck ? 21896 : 21888)+4*k)=(obj==truck ? 2.5f : -1.f)*(k+1);
                        F(float,obj,(obj==truck ? 21908 : 21900)+4*k)=0.125f*(k+1);
                    }
                }
                for(int tick=0;tick<3;++tick) {
                    const bool pending=tick!=1;
                    F(int,truck,21888)=pending; F(int,trailer,21884)=pending;
                    current={};
                    if(impl) Car_HitchImpulse(truck,nullptr); else ((Impulse)0x55cc30)(truck);
                    current.flags[0]=F(int,truck,21888); current.flags[1]=F(int,trailer,21884);
                    if(current.count!=(pending ? 4 : 0)) return false;
                    if(current.flags[0] || current.flags[1]) return false;
                    samples[impl][tick]=current;
                    if (pending) {
                        if(!impl && memcmp(current.values[0]+1,truck+21896,12)) ++active;
                        // Give the next event the actual contact response from this one.
                        for (int i=0;i<4;++i) {
                            auto& v=current.values[i];
                            if(v[0]!=static_cast<DWORD>(i)) return false;
                            BYTE* obj=v[0]<2 ? truck : trailer;
                            int off=obj==truck ? 21896 : 21888;
                            if(v[0]&1) off+=12;
                            memcpy(obj+off,v+1,12);
                        }
                    }
                }
            }
            for(int tick=0;tick<3;++tick) {
                if(memcmp(&samples[0][tick],&samples[1][tick],sizeof(Response))) {
                    if(++errors<=4) printf("hitch event precision=%x mode=%d orientation=%d tick=%d\n",precision,mode,orientation,tick);
                }
                ++cases;
            }
        }
        _controlfp(previous,_MCW_PC);
    }
    printf("hitch event snapshots=%d changed responses=%d mismatches=%d\n",cases,active,errors);
    return active>0 && !errors;
}
}
