// Called before any grid entry points are substituted. Old cells are fixture
// storage with a recording destructor. Both versions allocate real new cells.
struct MergeResult {
    int points, cells, cursor, queueHead[2], ring[2], prev[2], removed[4], removedCount;
    int corners[2][3], neighbours[2][3], outer[4][3], movedId, ok;
    DWORD geometry[2][6];
    BYTE lastVertex[8], lastAttributes[40];
};
static BYTE* retiring[4];
static MergeResult* mergeResult;
static void __fastcall RecordRetirement(BYTE* self, void*, int flags) {
    int i=0;
    while(i<4 && retiring[i]!=self) ++i;
    if(i==4 || flags!=1 || mergeResult->removedCount>=4) { mergeResult->ok=0; return; }
    mergeResult->removed[mergeResult->removedCount++]=i;
}
static void* __cdecl AllocateCell(size_t bytes) { return ::operator new(bytes); }
static void __cdecl ReleasePoint(void* point) { ::operator delete(point); }
static int CellId(BYTE* p, BYTE* const* created, BYTE outside[][76]) {
    if(!p) return 0;
    for(int i=0;i<2;i++) if(p==created[i]) return 1+i;
    for(int i=0;i<4;i++) if(p==outside[i]) return 3+i;
    return -1;
}
static MergeResult ExerciseMerge(bool reference, int count, int variant) {
    MergeResult result={};
    result.ok=1;
    mergeResult=&result;
    BYTE grid[80]={}, queue[16]={}, camera[40200]={};
    BYTE vertices[6][16]={}, attributes[6][40]={}, cells[4][76]={}, outside[4][76]={};
    static BYTE heights[5*257*16];
    BYTE* tiles[]={heights,heights,heights};
    WORD ids[]={0,1,2,3,4,5};
    WORD* midpoint=(WORD*)::operator new(sizeof(WORD));
    *midpoint=4;
    const int coords[][2]={{0,0},{0,4},{4,4},{4,0},{2,2},{3,3}};
    F(BYTE*,grid,4)=camera; F(BYTE*,grid,12)=vertices[0]; F(BYTE*,grid,16)=attributes[0];
    F(BYTE*,grid,36)=queue; F(WORD,grid,40)=variant&1?6:5; F(WORD,grid,42)=(WORD)count;
    F(BYTE**,camera,40192)=tiles; F(float,camera,40152)=1.25f; F(float,camera,40156)=0.75f;
    for(int i=0;i<6;i++) {
        int x=coords[i][0],y=coords[i][1];
        F(WORD*,vertices[i],0)=i==4?midpoint:&ids[i];
        F(int,vertices[i],8)=x; F(int,vertices[i],12)=y;
        F(float,heights,(x*257+y)*16)=(float)(x*x+y);
        memset(attributes[i],10+i,40);
    }
    void* vt[]={(void*)RecordRetirement};
    const int tip[]={1,2,3,0}, end[]={0,1,2,3};
    for(int i=0;i<count;i++) {
        BYTE* c=cells[i]; retiring[i]=c;
        F(void**,c,0)=vt;
        F(BYTE*,c,4)=cells[(i+1)%count]+4; F(BYTE*,c,8)=cells[(i+count-1)%count]+4;
        F(WORD*,c,16)=ids+tip[i]; F(WORD*,c,20)=midpoint; F(WORD*,c,24)=ids+end[i];
        F(int,c,36)=3;
        if(variant) {
            F(BYTE*,c,60)=outside[i];
            F(BYTE*,outside[i],60+4*((i+variant)%3))=c;
        }
    }
    for(int i=0;i<count;i+=2) {
        F(BYTE*,cells[i],64)=cells[i+1]; F(BYTE*,cells[i+1],68)=cells[i];
    }
    if(count==4) {
        F(BYTE*,cells[0],68)=cells[3]; F(BYTE*,cells[3],64)=cells[0];
        F(BYTE*,cells[1],64)=cells[2]; F(BYTE*,cells[2],68)=cells[1];
    }
    F(BYTE*,queue,variant&1?0:4)=cells[0];
    F(BYTE*,grid,52)=cells[variant%count];
    BYTE* created[2]={};
    typedef void (__thiscall *Pair)(BYTE*,BYTE*,BYTE*,BYTE**);
    typedef void (__thiscall *Four)(BYTE*,BYTE*,BYTE*,BYTE*,BYTE*,BYTE**,BYTE**);
    if(count==2) {
        if(reference) ((Pair)0x60a1c0)(grid,cells[0],cells[1],&created[0]);
        else Grid_MergeCells(grid,0,cells[0],cells[1],&created[0]);
    } else {
        if(reference) ((Four)0x60a460)(grid,cells[0],cells[1],cells[2],cells[3],&created[0],&created[1]);
        else Grid_MergeFourCells(grid,0,cells[0],cells[1],cells[2],cells[3],&created[0],&created[1]);
    }
    result.points=F(WORD,grid,40); result.cells=F(WORD,grid,42);
    result.cursor=CellId(F(BYTE*,grid,52),created,outside);
    result.movedId=ids[5];
    // The unused tail slot may contain the released midpoint pointer. Only
    // compare its numeric coordinates and the compacted attributes.
    memcpy(result.lastVertex,vertices[4]+8,8);
    memcpy(result.lastAttributes,attributes[4],40);
    const int fields[]={28,32,36,44,48,52};
    for(int i=0;i<count/2;i++) {
        BYTE* c=created[i];
        if(!c) {result.ok=0;continue;}
        for(int j=0;j<6;j++) result.geometry[i][j]=F(DWORD,c,fields[j]);
        for(int j=0;j<3;j++) {
            result.corners[i][j]=*F(WORD*,c,16+4*j);
            result.neighbours[i][j]=CellId(F(BYTE*,c,60+4*j),created,outside);
        }
        result.ring[i]=CellId(F(BYTE*,c,4)-4,created,outside);
        result.prev[i]=CellId(F(BYTE*,c,8)-4,created,outside);
    }
    for(int i=0;i<2;i++) result.queueHead[i]=CellId(F(BYTE*,queue,4*i),created,outside);
    for(int i=0;i<count;i++) for(int j=0;j<3;j++)
        result.outer[i][j]=CellId(F(BYTE*,outside[i],60+4*j),created,outside);
    result.ok &= result.cells==count/2 && result.points==(variant&1?5:4) &&
                 result.removedCount==count && result.cursor==0 && result.queueHead[1]>0;
    for(int i=0;i<count/2;i++) ::operator delete(created[i]);
    mergeResult=0;
    return result;
}
static bool CheckMerges() {
    Redirect((void*)0x632643,(void*)AllocateCell); Redirect((void*)0x63266c,(void*)ReleasePoint);
    for(int count=2;count<=4;count+=2) {
        int mismatches=0;
        for(int variant=0;variant<4;variant++) {
            MergeResult ref=ExerciseMerge(true,count,variant);
            MergeResult got=ExerciseMerge(false,count,variant);
            if(!ref.ok || !got.ok || memcmp(&ref,&got,sizeof ref)) {
                printf("merge mismatch cells=%d variant=%d valid=%d/%d\n",count,variant,ref.ok,got.ok);
                const int* a=(const int*)&ref,*b=(const int*)&got;
                for(unsigned i=0;i<sizeof ref/4;i++) if(a[i]!=b[i]) printf("  word %u %08x/%08x\n",i,a[i],b[i]);
                mismatches++;
            }
        }
        printf("merge %d cells cases=4 mismatches=%d\n",count,mismatches);
        if(mismatches) return false;
    }
    return true;
}
