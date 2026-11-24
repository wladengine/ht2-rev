// Marine mesh triangles have a different layout from the terrain cells.
extern "C" void __cdecl GridMesh_RemoveEdge(BYTE*,BYTE*);
extern "C" BYTE* __fastcall GridMesh_CreateEdge(BYTE*,void*,BYTE*,BYTE*);
extern "C" void __fastcall Grid_MergeFour(BYTE*,void*,BYTE*,BYTE*,BYTE*,BYTE*,BYTE**,BYTE**);
struct MeshJoinFixture {
    BYTE mesh[64],queue[16],pair[2][24],group[24],other[24],parent[24],sibling[24];
    BYTE old[4][60],out[2][60],nearTri[60],farTri[60],owners[2][24];
    int mode,calls,badThis,badArgs,merges;
};
static MeshJoinFixture marine;
static void __fastcall MergeMarineFixture(BYTE* mesh,void*,BYTE* a,BYTE* b,BYTE* c,BYTE* d,BYTE** x,BYTE** y) {
    marine.merges++;
    marine.badArgs += mesh!=marine.mesh || a!=marine.old[0] || b!=marine.old[1] ||
                      c!=marine.old[2] || d!=marine.old[3];
    *x=marine.out[0]; *y=marine.out[1];
}
static BYTE* __fastcall ObserveMarineEdge(BYTE* mesh,void*,BYTE* left,BYTE* right) {
    marine.calls++;
    marine.badThis += mesh!=marine.mesh;
    marine.badArgs += left!=marine.group || right!=(marine.mode%2?marine.other:0);
    return 0;
}
static void PrepareMarineJoin(int mode,BYTE* edge) {
    memset(&marine,0,sizeof marine);
    marine.mode=mode;
    F(BYTE*,marine.mesh,28)=marine.queue;
    F(WORD,marine.mesh,52)=7;
    F(BYTE*,marine.mesh,44)=edge;
    F(BYTE*,marine.queue,0)=F(BYTE*,edge,0)=F(BYTE*,edge,4)=edge;
    F(BYTE*,edge,8)=marine.pair[0]; F(BYTE*,edge,12)=marine.pair[1];
    for(int i=0;i<4;i++) {
        BYTE* child=(BYTE*)::operator new(24);
        memset(child,0,24);
        BYTE* p=marine.pair[i/2];
        F(BYTE*,p,4*(i%2))=child;
        F(BYTE*,child,8)=p; F(BYTE*,child,12)=marine.old[i];
        F(BYTE*,marine.old[i],56)=edge;
    }
    F(int,marine.out[0],56)=1; // only the second merged triangle needs joining
    bool second=mode==2 || mode==3;
    F(BYTE*,marine.pair[1],8)=marine.group;
    F(BYTE*,marine.group,second?4:0)=marine.pair[1];
    F(BYTE*,marine.group,second?0:4)=marine.sibling;
    F(BYTE*,marine.group,8)=marine.parent;
    if(mode==4)F(int,marine.out[1],56)=1;
    int side=second?48:52;
    if(mode==0 || mode==2)F(BYTE*,marine.out[1],second?52:48)=marine.nearTri;
    else {
        F(BYTE*,marine.out[1],side)=marine.nearTri;
        F(BYTE*,marine.nearTri,side)=marine.farTri;
        F(BYTE*,marine.nearTri,32)=marine.owners[0]; F(BYTE*,marine.farTri,32)=marine.owners[1];
        F(BYTE*,marine.owners[0],8)=marine.other;
        F(BYTE*,marine.owners[1],8)=mode==5?marine.parent:marine.other;
    }
}
static bool CheckMarineJoin(bool expectOldFailure) {
    Redirect((void*)0x6064c0,(void*)ObserveMarineEdge);
    Redirect((void*)GridMesh_CreateEdge,(void*)ObserveMarineEdge);
    Redirect((void*)0x606d50,(void*)MergeMarineFixture);
    Redirect((void*)Grid_MergeFour,(void*)MergeMarineFixture);
    Redirect((void*)0x63266c,(void*)ReleasePoint);
    int badThis=0;
    for(int mode=0;mode<6;mode++) for(int target=0;target<2;target++) {
        BYTE* e=(BYTE*)::operator new(108);
        memset(e,0,108);
        PrepareMarineJoin(mode,e);
        typedef void (__cdecl *RemoveFn)(BYTE*,BYTE*);
        RemoveFn remove=target?GridMesh_RemoveEdge:(RemoveFn)0x606760;
        remove(e,marine.mesh);
        if(marine.merges!=1 || marine.calls!=(mode<4) || marine.badArgs ||
           F(WORD,marine.mesh,52)!=6 || F(BYTE*,marine.mesh,44) ||
           F(BYTE*,marine.out[1],32)!=marine.pair[1] ||
           F(BYTE*,marine.pair[1],12)!=marine.out[1] || (!target && marine.badThis)) {
            printf("marine unexpected mode=%d target=%d merges=%d calls=%d args=%d this=%d\n",
                   mode,target,marine.merges,marine.calls,marine.badArgs,marine.badThis);
            return false;
        }
        if(target) badThis+=marine.badThis;
    }
    printf("marine inner edge cases=6 wrong mesh=%d\n",badThis);
    return expectOldFailure ? badThis>0 : badThis==0;
}
