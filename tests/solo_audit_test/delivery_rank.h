extern "C" void __cdecl Ai_RecalculateCompetitionPlace();
extern "C" void __cdecl Ai_CalculateNewPlace(int*,double*);
extern "C" LrPos* __cdecl Ai_GetPositionIdEx(LrPos*,int,const double*);

namespace delivery_rank {
// Control only what the road projection returns. The player list, lead
// calculation, competition count, four offsets and selection run in the game.
static int variant, probes, updates;
static bool validCalls;
static BYTE* player;
static LrPos* __cdecl Project(LrPos* out,int room,const double* xyz) {
    int i=probes++;
    validCalls=validCalls && i<4 && room==7 && xyz[0]==30.+i && xyz[1]==60.-i && xyz[2]==90.+2*i;
    memset(out,0,sizeof *out);
    if (variant==1 || (variant==3 && i==0)) out->d[0]=out->d[1]=DWORD(-1);
    else out->d[0]=1+i;
    return out;
}
static void __cdecl Score(BYTE* data,LrPos pos) {
    ++updates;
    validCalls=validCalls && data==player+8 && pos.d[0]<=4;
    const bool improved=(variant==2 && pos.d[0]==1) || (variant==3 && pos.d[0]==3);
    F(double,player,504)=improved ? 400. : 100.;
}
static bool Check(bool original) {
    using gameplay::SetWorld;
    const auto count=original ? reinterpret_cast<decltype(&Ai_CalculateNewPlace)>(0x427f40) : Ai_CalculateNewPlace;
    const auto rank=original ? reinterpret_cast<decltype(&Ai_RecalculateCompetitionPlace)>(0x428230) : Ai_RecalculateCompetitionPlace;
    int cases=0, errors=0;
    for (int mask=0; mask<16; ++mask) for (double gap : {-13.,-12.,0.,12.,13.})
        for (int finished : {0,2}) for (variant=0; variant<6; ++variant) {
            company_world::World w;
            w.Init();
            BYTE* entrants[4];
            player=w.human;
            F(double,player,504)=100.;
            F(int,player,524)=17; F(int,player,528)=15;
            F(int,player,852)=7; F(int,player,100)=variant==5 ? 5 : 4;
            SetWorld(0x68d188,int(variant==4)); SetWorld(0x68d164,finished);
            for (int j=0; j<3; ++j) {
                F(double,player,552+32*j)=1.;
                F(double,player,624+8*j)=30.*(j+1);
            }
            const double advances[]={100.+gap,160.,65.,90.};
            int active=0, ahead=0;
            double closest=DBL_MAX;
            for (int i=0; i<4; ++i) {
                SetWorld(0x68cc28+24*i,double(i)); SetWorld(0x68cc30+24*i,-double(i)); SetWorld(0x68cc38+24*i,double(2*i));
                if (!(mask&(1<<i))) continue;
                entrants[active++]=w.drivers[i];
                w.drivers[i][496]=1; F(double,w.drivers[i],504)=advances[i];
                ahead+=advances[i]>=100.;
                double distance=advances[i]>100. ? advances[i]-100. : 100.-advances[i];
                if (distance<closest) closest=distance;
            }
            SetWorld(0x68a980,entrants+0); SetWorld(0x68a984,entrants+active);
            int observed=-1;
            double distance=-1.;
            count(&observed,&distance);
            probes=updates=0; validCalls=true;
            rank();
            const bool refresh=closest>12., sample=refresh && variant<4;
            const bool better=sample && ahead && (variant==2 || variant==3);
            const int expectedProbes=sample ? (better ? (variant==2 ? 1 : 3) : 4) : 0;
            const int expectedUpdates=expectedProbes-(variant==1 ? expectedProbes : int(variant==3 && sample))+int(sample && !better);
            if (!validCalls || distance!=closest || observed!=ahead || probes!=expectedProbes || updates!=expectedUpdates ||
                F(int,player,524)!=(refresh ? finished+active : 17) ||
                F(int,player,528)!=(refresh ? finished+(better ? 0 : ahead) : 15) ||
                F(double,player,504)!=(better ? 400. : 100.)) {
                if (!errors) printf("rank mismatch original=%d mask=%d gap=%g variant=%d distance=%g rank=%d probes=%d/%d updates=%d/%d valid=%d\n",
                    original,mask,gap,variant,distance,F(int,player,528),probes,expectedProbes,updates,expectedUpdates,validCalls);
                ++errors;
            }
            ++cases;
        }
    printf("delivery rank and projections original=%d cases=%d mismatches=%d\n",original,cases,errors);
    return !errors;
}
static bool Both() {
    Redirect((void*)0x4365d0,(void*)Project); Redirect((void*)Ai_GetPositionIdEx,(void*)Project);
    Redirect((void*)0x41a7d0,(void*)Score); Redirect((void*)Ai_UpdateAdvancementRegular,(void*)Score);
    const bool ref=Check(true), got=Check(false);
    return ref && got;
}
}
