// Sequential boundary splits on two triangles sharing one corner. Each step uses
// the live output of the previous step, including registrations and queue links.
extern "C" BYTE* __fastcall Grid_NewCell(BYTE*, void*, WORD*, WORD*, WORD*, unsigned);
extern "C" int __fastcall Grid_SplitEdge(BYTE*, void*, BYTE*, BYTE**, BYTE**);

namespace sequence {
constexpr int basePoints = 5, maxPoints = 8, slots = 8;
struct Snapshot {
    int pointCount, cellCount, head[2], cursor, transitionHead;
    int ring[maxPoints][3], xy[maxPoints][2], corners[slots][3], links[slots][5];
    DWORD attributes[maxPoints][10], transition[maxPoints][22], geometry[slots][6];
};

int NodeId(BYTE* node, BYTE* const* live) {
    if (!node) return 0;
    for (int i=0; i<slots; ++i) if (node==live[i]) return i+1;
    return -1;
}

int VertexOf(BYTE* transition, BYTE vertices[][16], int count) {
    if (!transition) return 0;
    for (int i=0; i<count; ++i)
        if (F(BYTE*,vertices[i],4)==transition && F(BYTE*,transition,16)==vertices[i]) return i+1;
    return -1;
}

bool Capture(Snapshot& out, BYTE* grid, BYTE vertices[][16], BYTE* const* live) {
    out.pointCount=F(WORD,grid,40);
    out.cellCount=F(WORD,grid,42);
    if (out.pointCount<basePoints || out.pointCount>maxPoints) return false;
    out.transitionHead=VertexOf(F(BYTE*,grid,44),vertices,out.pointCount);
    if (out.transitionHead<0) return false;
    for (int i=0; i<out.pointCount; ++i) {
        if (*F(WORD*,vertices[i],0)!=i) return false;
        memcpy(out.xy[i],vertices[i]+8,8);
        memcpy(out.attributes[i],F(BYTE*,grid,16)+40*i,40);
        BYTE* tr=F(BYTE*,vertices[i],4);
        if (!tr) continue;
        out.ring[i][0]=VertexOf(tr,vertices,out.pointCount);
        out.ring[i][1]=VertexOf(F(BYTE*,tr,0),vertices,out.pointCount);
        out.ring[i][2]=VertexOf(F(BYTE*,tr,4),vertices,out.pointCount);
        if (out.ring[i][0]!=i+1 || out.ring[i][1]<=0 || out.ring[i][2]<=0 || F(BYTE*,tr,100)!=grid) return false;
        memcpy(out.transition[i],tr+8,8);
        memcpy(out.transition[i]+2,tr+20,80);
    }
    for (int i=0; i<2; ++i) out.head[i]=NodeId(F(BYTE*,F(BYTE*,grid,36),4*i),live);
    out.cursor=NodeId(F(BYTE*,grid,52),live);
    int count=0;
    for (int i=0; i<slots; ++i) {
        BYTE* cell=live[i];
        if (!cell) continue;
        ++count;
        const int offsets[]={28,32,36,44,48,52};
        for (int j=0; j<6; ++j) out.geometry[i][j]=F(DWORD,cell,offsets[j]);
        for (int j=0; j<3; ++j) {
            out.corners[i][j]=*F(WORD*,cell,16+4*j);
            out.links[i][j]=NodeId(F(BYTE*,cell,60+4*j),live);
        }
        for (int j=0; j<2; ++j) out.links[i][3+j]=NodeId(F(BYTE*,cell,4+4*j)-4,live);
        for (int id : out.links[i]) if (id<0) return false;
    }
    return out.cellCount==count && out.head[0]>=0 && out.head[1]>=0 && out.cursor>=0;
}

bool Exercise(bool original, int variant, Snapshot (&steps)[7]) {
    BYTE grid[80]={}, queue[16]={}, camera[40200]={};
    BYTE vertices[maxPoints][16]={}, attributes[maxPoints][40]={};
    static float heights[9][257][4]={};
    float* tiles[]={&heights[0][0][0],&heights[0][0][0],&heights[0][0][0]};
    F(void*,camera,40192)=tiles;
    F(float,camera,40152)=1; F(float,camera,40156)=1;
    F(BYTE*,grid,4)=camera; F(BYTE*,grid,12)=vertices[0]; F(BYTE*,grid,16)=attributes[0];
    F(BYTE*,grid,36)=queue; F(WORD,grid,40)=basePoints; F(int,grid,48)=350;
    WORD ids[basePoints]={0,1,2,3,4};
    const int xy[][2]={{0,0},{0,4},{4,4},{8,4},{8,8}};
    for (int x=0; x<9; ++x) for (int y=0; y<9; ++y) {
        heights[x][y][0]=float(x*x+y); heights[x][y][3]=1;
    }
    for (int i=0; i<basePoints; ++i) {
        F(WORD*,vertices[i],0)=ids+i;
        memcpy(vertices[i]+8,xy[i],8);
        for (int j=0; j<10; ++j) F(float,attributes[i],4*j)=float(10*i+j);
    }
    BYTE* live[slots]={};
    using NewCell=BYTE* (__thiscall*)(BYTE*,WORD*,WORD*,WORD*,unsigned);
    using Split=int (__thiscall*)(BYTE*,BYTE*,BYTE**,BYTE**);
    using Merge=void (__thiscall*)(BYTE*,BYTE*,BYTE*,BYTE**);
    for (int i=0; i<2; ++i) {
        WORD* p=ids+i*2;
        unsigned level=variant & 1 ? 8 : 2;
        live[i]=original ? ((NewCell)0x608530)(grid,p,p+1,p+2,level)
                         : Grid_NewCell(grid,0,p,p+1,p+2,level);
    }
    F(BYTE*,grid,52)=live[variant%2];
    bool ok=Capture(steps[0],grid,vertices,live);
    int step=0;
    auto split=[&](int parent, int firstChild) {
        BYTE** a=live+firstChild;
        int rc=original ? ((Split)0x609990)(grid,live[parent],a,a+1)
                        : Grid_SplitEdge(grid,0,live[parent],a,a+1);
        ::operator delete(live[parent]); live[parent]=nullptr;
        ok=Capture(steps[++step],grid,vertices,live) && rc==1 && ok;
    };
    auto merge=[&](int parent, int firstChild) {
        BYTE** a=live+firstChild;
        if (original) ((Merge)0x60a1c0)(grid,a[0],a[1],live+parent);
        else Grid_MergeCells(grid,0,a[0],a[1],live+parent);
        a[0]=a[1]=nullptr;
        ok=Capture(steps[++step],grid,vertices,live) && ok;
    };
    split(0,2); split(1,4);
    int nested=variant&4 ? 1 : 0;
    int child=nested ? 5 : 2;
    split(child,6);
    if (variant&2) merge(1-nested,2+2*(1-nested));
    merge(child,6);
    merge(nested,2+2*nested);
    if (!(variant&2)) merge(1-nested,2+2*(1-nested));
    ::operator delete(live[0]); ::operator delete(live[1]);
    return ok && steps[3].pointCount==8 && steps[3].cellCount==5 && steps[5].transitionHead>0 && steps[6].transitionHead==0;
}

bool Check() {
    int failures=0;
    for (int variant=0; variant<8; ++variant) {
        Snapshot reference[7]={}, candidate[7]={};
        bool refOk=Exercise(true,variant,reference);
        bool gotOk=Exercise(false,variant,candidate);
        for (int i=0; i<7; ++i) {
            if (memcmp(reference+i,candidate+i,sizeof(Snapshot)) || !refOk || !gotOk) {
                printf("grid sequence variant=%d step=%d valid=%d/%d\n",variant,i,refOk,gotOk); ++failures;
            }
        }
    }
    printf("grid sequence snapshots=56 mismatches=%d\n",failures);
    return !failures;
}

extern "C" void __cdecl Grid_ForceSplit(BYTE*,BYTE*);
struct DiamondStep {
    Snapshot geometry;
    DWORD circles[2][3], priority;
    int pending, first, next, previous, pairOwner[2], cellOwner[4];
};
bool Diamond(bool original,int variant,DiamondStep (&steps)[4]) {
    BYTE grid[80]={}, nodes[16]={}, work[16]={}, camera[40200]={};
    BYTE vertices[maxPoints][16]={}, attributes[maxPoints][40]={};
    BYTE parents[2][20]={}, root[20]={}, waiting[156]={};
    static float heights[5][257][4]={};
    float* tiles[]={heights[0][0],heights[0][0],heights[0][0]};
    F(void*,camera,40192)=tiles;
    F(float,camera,40152)=1; F(float,camera,40156)=1;
    F(BYTE*,grid,4)=camera; F(BYTE*,grid,12)=vertices[0]; F(BYTE*,grid,16)=attributes[0];
    F(BYTE*,grid,32)=work; F(BYTE*,grid,36)=nodes; F(WORD,grid,40)=basePoints; F(int,grid,48)=250;
    // A separate waiting edge makes the generated pair compete for queue order.
    F(BYTE*,work,4)=waiting; F(BYTE*,waiting,0)=F(BYTE*,waiting,4)=waiting; F(float,waiting,104)=100;
    WORD ids[basePoints]={0,1,2,3,4};
    const int xy[][2]={{0,0},{0,4},{4,4},{4,0},{2,0}};
    for (int i=0;i<basePoints;++i) {
        F(WORD*,vertices[i],0)=ids+i; memcpy(vertices[i]+8,xy[i],8);
        for(int j=0;j<10;++j) F(float,attributes[i],4*j)=float(i*2+j);
    }
    BYTE* live[slots]={};
    using NewCell=BYTE* (__thiscall*)(BYTE*,WORD*,WORD*,WORD*,unsigned);
    for(int i=0;i<2;++i) {
        WORD* a=ids+(i ? 2 : 0); WORD* b=ids+(i ? 3 : 1); WORD* c=ids+(i ? 0 : 2);
        live[i]=original ? ((NewCell)0x608530)(grid,a,b,c,2) : Grid_NewCell(grid,0,a,b,c,2);
        F(BYTE*,live[i],40)=parents[i]; F(BYTE*,parents[i],8)=root;
        F(BYTE*,parents[i],12)=live[i]; F(BYTE*,root,4*i)=parents[i];
    }
    F(BYTE*,live[0],60)=live[1]; F(BYTE*,live[1],60)=live[0];
    auto split=original ? (decltype(&Grid_ForceSplit))0x607410 : Grid_ForceSplit;
    auto merge=original ? (decltype(&GridTree_RemoveEdge))0x609de0 : GridTree_RemoveEdge;
    for(int cycle=0;cycle<2;++cycle) {
        F(float,camera,4)=((cycle^(variant>>1))&1) ? 4000.0f : 1.0f;
        F(BYTE*,grid,52)=live[variant&1];
        split(parents[(cycle^variant)&1],grid);
        live[0]=live[1]=nullptr;
        for(int i=0;i<4;++i) live[i+2]=F(BYTE*,F(BYTE*,parents[i/2],(i%2)*4),12);
        DiamondStep& cut=steps[cycle*2];
        if (!Capture(cut.geometry,grid,vertices,live)) return false;
        BYTE* pair=F(BYTE*,live[2],72);
        if (!pair || F(WORD,grid,64)!=1 || cut.geometry.cellCount!=4 || cut.geometry.pointCount!=6) return false;
        cut.priority=F(DWORD,pair,104);
        cut.pending=F(DWORD,work,8);
        cut.first=F(BYTE*,work,4)==pair;
        cut.next=F(BYTE*,pair,0)==waiting; cut.previous=F(BYTE*,pair,4)==waiting;
        for (int i=0;i<2;++i) {
            memcpy(cut.circles[i],pair+48+44*i,12);
            BYTE* p=F(BYTE*,pair,8+4*i);
            cut.pairOwner[i]=p==parents[0] ? 1 : p==parents[1] ? 2 : 0;
        }
        for (int i=0;i<4;++i) cut.cellOwner[i]=F(BYTE*,live[i+2],72)==pair;
        if (!cut.next || !cut.previous || !cut.pairOwner[0] || !cut.pairOwner[1]) return false;
        F(BYTE*,grid,56)=pair;
        merge(pair,grid);
        memset(live,0,sizeof live);
        for(int i=0;i<2;++i) live[i]=F(BYTE*,parents[i],12);
        DiamondStep& joined=steps[cycle*2+1];
        if (!Capture(joined.geometry,grid,vertices,live)) return false;
        joined.pending=F(DWORD,work,8);
        if (F(WORD,grid,64)!=0 || joined.geometry.pointCount!=5 || joined.geometry.cellCount!=2 ||
            F(BYTE*,work,4)!=waiting || F(BYTE*,waiting,0)!=waiting ||
            F(BYTE*,live[0],60)!=live[1] || F(BYTE*,live[1],60)!=live[0]) return false;
    }
    ::operator delete(live[0]); ::operator delete(live[1]);
    return true;
}
bool CheckDiamonds() {
    int errors=0;
    for(int variant=0;variant<4;++variant) {
        DiamondStep ref[4]={},got[4]={};
        bool refOK=Diamond(true,variant,ref), gotOK=Diamond(false,variant,got);
        if(!refOK || !gotOK || memcmp(ref,got,sizeof ref) || ref[0].first==ref[2].first) {
            ++errors;
            printf("shared edge variant=%d valid=%d/%d first=%d,%d priority=%.8g,%.8g\n",
                variant,refOK,gotOK,ref[0].first,ref[2].first,F(float,&ref[0].priority,0),F(float,&ref[2].priority,0));
        }
    }
    printf("shared edge camera snapshots=16 mismatches=%d\n",errors);
    return !errors;
}
}
