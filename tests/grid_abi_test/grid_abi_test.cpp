#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <float.h>
#include <initializer_list>
#include <stdlib.h>
extern "C" unsigned char KingBss[];

#define F(T,p,o) (*(T*)((BYTE*)(p)+(o)))
static BYTE* reserved;
extern "C" void __cdecl mainCRTStartup();
extern "C" void __cdecl GridEntry(){reserved=(BYTE*)VirtualAlloc((void*)0x400000,0x3bd000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);mainCRTStartup();}
static BYTE* MapOriginal(const char* path) {
    // Reserve before allocating the raw file: an arbitrary allocation can itself
    // occupy the preferred original image address. Runner pins the input SHA256.
    BYTE* image=reserved;
    if(!image) {
        MEMORY_BASIC_INFORMATION info;
        for(BYTE* p=(BYTE*)0x400000;p<(BYTE*)0x7bd000;) {
            VirtualQuery(p,&info,sizeof info);
            fprintf(stderr,"original address occupied: base=%p allocation=%p size=%x state=%x type=%x\n",info.BaseAddress,info.AllocationBase,(unsigned)info.RegionSize,info.State,info.Type);
            char module[MAX_PATH]={0};
            if(GetModuleFileNameA((HMODULE)info.AllocationBase,module,sizeof module))
                fprintf(stderr,"original address occupied: module=%s\n",module);
            p=(BYTE*)info.BaseAddress+info.RegionSize;
        }
        return 0;
    }
    FILE* f=fopen(path,"rb"); if(!f)return 0;
    fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
    BYTE* raw=(BYTE*)VirtualAlloc(0,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!raw || fread(raw,1,size,f)!=(size_t)size) { fclose(f);return 0; } fclose(f);
    IMAGE_NT_HEADERS32* nt=(IMAGE_NT_HEADERS32*)(raw+((IMAGE_DOS_HEADER*)raw)->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->OptionalHeader.ImageBase!=0x400000)return 0;
    if(nt->OptionalHeader.SizeOfImage!=0x3bd000)return 0;
    memcpy(image,raw,nt->OptionalHeader.SizeOfHeaders);
    IMAGE_SECTION_HEADER* section=IMAGE_FIRST_SECTION(nt);
    for(int i=0;i<nt->FileHeader.NumberOfSections;i++)
        memcpy(image+section[i].VirtualAddress,raw+section[i].PointerToRawData,section[i].SizeOfRawData);
    // Do not run the original's entry point; only the grid removal and branch-joining functions under test.
    FlushInstructionCache(GetCurrentProcess(),image,nt->OptionalHeader.SizeOfImage);
    VirtualFree(raw,0,MEM_RELEASE);
    return image;
}



extern "C" unsigned char KingRdata[];
extern "C" void __fastcall Grd_JoinBranches(BYTE*,void*,BYTE*);
extern "C" void __cdecl GridTree_RemoveEdge(BYTE*,BYTE*);
extern "C" void __fastcall Grid_DrainQueue(BYTE*,void*);
extern "C" BYTE* __fastcall Grid_NewSplit(BYTE*,void*,BYTE*,BYTE*);
extern "C" void __fastcall Grid_MergeCells(BYTE*,void*,BYTE*,BYTE*,BYTE**);
extern "C" void __fastcall Grid_MergeFourCells(BYTE*,void*,BYTE*,BYTE*,BYTE*,BYTE*,BYTE**,BYTE**);
extern "C" BYTE* __fastcall MarineQueue_RemoveLink(BYTE*,void*,BYTE*);
extern "C" BYTE* __fastcall MarineQueue_TopWorkItem(BYTE*,void*);
extern "C" void DeleteEntry();
#pragma comment(linker,"/alternatename:_DeleteEntry=??3@YAXPAX@Z")
static void Redirect(void* address,void* fn) {
 DWORD old;BYTE* p=(BYTE*)address;
 if(!VirtualProtect(p,5,PAGE_EXECUTE_READWRITE,&old))ExitProcess(2);
 p[0]=0xe9;F(int,p,1)=(BYTE*)fn-p-5;
 FlushInstructionCache(GetCurrentProcess(),p,5);VirtualProtect(p,5,old,&old);
}
struct Branch {
 BYTE cell[80],nearCell[80],farCell[80],foreign[80];
 BYTE pair[16],nodes[2][16],holders[3][16],otherPair[16],unequalPair[16];
};
struct Arena {
 DWORD guard1[4]; BYTE grid[96],queue[16],edge[160];
 BYTE oldPairs[2][16],oldBranches[4][16],oldCells[4][80];
 Branch cfg[2]; DWORD guard2[4];
};
static Arena arena;
struct Observation {
 char trace[64]; int badThis,argsOK,stackOK,stateOK,splits;
 DWORD exception,fault; Arena state;
};
static Observation* obs;
static bool original,inner;
static int kind,variant,expectedCount,traceSize,deleteCount,topCount;
static BYTE* expectedA[2];static BYTE* expectedB[2];
static void Event(char c){if(traceSize<63)obs->trace[traceSize++]=c;else obs->argsOK=0;}
static void BranchSetup(Branch& b,int v) {
 F(BYTE*,b.cell,40)=b.holders[0];F(BYTE*,b.holders[0],8)=b.pair;
 F(BYTE*,b.pair,0)=b.nodes[0];F(BYTE*,b.pair,4)=b.nodes[1];
 bool second=v==2 || v==3 || v==10 || v==11;
 F(BYTE*,b.nodes[0],12)=second?b.foreign:b.cell;
 F(BYTE*,b.nodes[1],12)=second?b.cell:b.foreign;
 F(BYTE*,b.nearCell,40)=b.holders[1];F(BYTE*,b.farCell,40)=b.holders[2];
 F(BYTE*,b.holders[1],8)=b.otherPair;F(BYTE*,b.holders[2],8)=b.otherPair;
 if(v==0)F(BYTE*,b.cell,64)=b.nearCell;
 if(v==1 || v==8 || v==9) {
  F(BYTE*,b.cell,68)=b.nearCell;
  if(v!=8)F(BYTE*,b.nearCell,68)=b.farCell;
 }
 if(v==2)F(BYTE*,b.cell,68)=b.nearCell;
 if(v==3 || v==10 || v==11) {
  F(BYTE*,b.cell,64)=b.nearCell;
  if(v!=10)F(BYTE*,b.nearCell,64)=b.farCell;
 }
 if(v==4)F(int,b.cell,72)=1;
 if(v==6 || v==7){F(BYTE*,b.cell,64)=b.nearCell;F(BYTE*,b.pair,v==6?0:4)=0;}
 if(v==9 || v==11)F(BYTE*,b.holders[2],8)=b.unequalPair;
 if(v<4) {
  expectedA[expectedCount]=b.pair;
  expectedB[expectedCount]=(v==1 || v==3)?b.otherPair:0;++expectedCount;
 }
}
static BYTE* __fastcall Split(BYTE* self,void*,BYTE* a,BYTE* b) {
 Event('S');obs->badThis+=self!=arena.grid;
 int i=obs->splits++;
 obs->argsOK&=i<expectedCount;
 if(i<expectedCount)obs->argsOK&=a==expectedA[i] && b==expectedB[i];
 return arena.edge;
}
static void MarkCheck(int n) {
 obs->argsOK&=F(WORD,arena.grid,64)==6;
 for(int i=0;i<n;i++)obs->argsOK&=F(int,arena.oldCells[i],72)==0;
}
static BYTE* __fastcall Remove(BYTE* self,void*,BYTE* edge) {
 Event('R');obs->argsOK&=self==arena.queue && edge==arena.edge;MarkCheck(inner?4:2);return edge;
}
static void __fastcall Merge(BYTE* self,void*,BYTE* a,BYTE* b,BYTE** out) {
 Event('M');obs->argsOK&=self==arena.grid && a==arena.oldCells[0] && b==arena.oldCells[1];
 MarkCheck(2);*out=arena.cfg[0].cell;
}
static void __fastcall MergeFour(BYTE* self,void*,BYTE* a,BYTE* b,BYTE* c,BYTE* d,BYTE** out1,BYTE** out2) {
 Event('F');obs->argsOK&=self==arena.grid && a==arena.oldCells[0] && b==arena.oldCells[1] && c==arena.oldCells[2] && d==arena.oldCells[3];
 MarkCheck(4);*out1=arena.cfg[0].cell;*out2=arena.cfg[1].cell;
}
static void __cdecl Delete(void* p) {
 // Both production CRT and mapped original use this observer. The harness
 // itself owns static storage; suppress deallocation only for this process.
 if(!obs || (BYTE*)p<(BYTE*)&arena || (BYTE*)p>=(BYTE*)(&arena+1))return;
 Event('D');int n=inner?4:2;
 void* expected=deleteCount<n?(void*)arena.oldBranches[deleteCount]:(void*)arena.edge;
 obs->argsOK&=p==expected && deleteCount<=n;++deleteCount;
}
static BYTE* __fastcall Top(BYTE* self,void*) {
 Event('T');obs->argsOK&=self==arena.queue && topCount<2;
 return topCount++==0?arena.edge:0;
}
static DWORD Fault(EXCEPTION_POINTERS* e) {
 obs->exception=e->ExceptionRecord->ExceptionCode;obs->fault=(DWORD)e->ExceptionRecord->ExceptionAddress;
 return EXCEPTION_EXECUTE_HANDLER;
}
static void Invoke() {
 void* target;DWORD before,after;
 BYTE* g=arena.grid;BYTE* e=arena.edge;BYTE* cell=arena.cfg[0].cell;
 if(kind==0) {
  target=original?(void*)0x60a0b0:(void*)Grd_JoinBranches;
  __asm {
   mov before,esp
   push cell
   mov ecx,g
   xor edx,edx
   call target
   mov after,esp
   mov esp,before
  }
 } else if(kind==1) {
  target=original?(void*)0x609de0:(void*)GridTree_RemoveEdge;
  __asm {
   mov before,esp
   push g
   push e
   call target
   add esp,8
   mov after,esp
   mov esp,before
  }
 } else {
  target=original?(void*)0x60cae0:(void*)Grid_DrainQueue;
  __asm {
   mov before,esp
   mov ecx,g
   xor edx,edx
   call target
   mov after,esp
   mov esp,before
  }
 }
 obs->stackOK=before==after;
}
static void Run(bool isOriginal,int t,Observation* out) {
 original=isOriginal;obs=out;memset(out,0,sizeof *out);obs->argsOK=obs->stackOK=obs->stateOK=1;
 memset(&arena,0,sizeof arena);for(int i=0;i<4;i++)arena.guard1[i]=arena.guard2[i]=0x76543210;
 expectedCount=traceSize=deleteCount=topCount=0;inner=false;
 kind=t<12?0:t<32?1:2;variant=t<24?t%12:t%4;
 if(t>=24 && t<32)inner=true;
 BranchSetup(arena.cfg[0],t>=24 && t<28?4:variant);
 if(inner)BranchSetup(arena.cfg[1],t<28?variant:3-variant);
 F(BYTE*,arena.grid,32)=arena.queue;F(WORD,arena.grid,64)=7;
 // Exercise the real Pair_Next cursor update for all removal cases. Its
 // single-node ring returns null; no queue or list arithmetic is substituted.
 F(BYTE*,arena.grid,56)=arena.edge;F(BYTE*,arena.queue,0)=arena.edge;
 F(BYTE*,arena.edge,4)=arena.edge;
 F(BYTE*,arena.edge,8)=arena.oldPairs[0];F(BYTE*,arena.edge,12)=inner?arena.oldPairs[1]:0;
 for(int i=0;i<(inner?4:2);i++) {
  BYTE* pair=arena.oldPairs[i/2];F(BYTE*,pair,(i%2)*4)=arena.oldBranches[i];
  F(BYTE*,arena.oldBranches[i],8)=pair;F(BYTE*,arena.oldBranches[i],12)=arena.oldCells[i];
  F(int,arena.oldCells[i],72)=123;
 }
 for(int i=0;i<2;i++)F(BYTE*,arena.oldPairs[i],8)=arena.cfg[i].pair;
 Arena before=arena;
 __try { Invoke(); } __except(Fault(GetExceptionInformation())) {}
 char trace[64]="";
 if(kind) {
  if(kind==2)strcat(trace,"T");strcat(trace,inner?"RFDDDDD":"RMDDD");
  obs->stateOK&=F(WORD,arena.grid,64)==6 && F(BYTE*,arena.grid,56)==0;
  for(int i=0;i<(inner?2:1);i++) {
   obs->stateOK&=F(BYTE*,arena.oldPairs[i],0)==0 && F(BYTE*,arena.oldPairs[i],4)==0;
   obs->stateOK&=F(BYTE*,arena.oldPairs[i],12)==arena.cfg[i].cell && F(BYTE*,arena.cfg[i].cell,40)==arena.oldPairs[i];
  }
  DWORD vt=original?0x652ef4:(DWORD)(KingRdata+0x652ef4-0x64b000);
  int offsets[]={16,60,108};for(int i=0;i<3;i++){obs->stateOK&=F(DWORD,arena.edge,offsets[i])==vt;F(DWORD,arena.edge,offsets[i])=0;}
 } else obs->stateOK&=!memcmp(&arena,&before,sizeof arena);
 for(int i=0;i<expectedCount;i++)strcat(trace,"S");
 if(kind==2)strcat(trace,"T");
 obs->argsOK&=!strcmp(obs->trace,trace) && obs->splits==expectedCount;
 for(int i=0;i<4;i++)obs->stateOK&=arena.guard1[i]==0x76543210 && arena.guard2[i]==0x76543210;
 out->state=arena;obs=0;
}
static bool Valid(const Observation& o){return !o.exception && !o.badThis && o.argsOK && o.stackOK && o.stateOK;}
// Exercise the actual split implementation before installing the caller-test hooks.
// Only the original CRT allocator/deallocator are replaced; the queue starts empty.
static BYTE originalSplitNode[156];
static int allocs,frees;
static void* __cdecl SplitAlloc(size_t size){
 if(size!=sizeof originalSplitNode || ++allocs!=1)ExitProcess(3);
 memset(originalSplitNode,0xcd,sizeof originalSplitNode);return originalSplitNode;
}
static void __cdecl SplitFree(void* p){if(p!=originalSplitNode)ExitProcess(3);frees++;}
struct QueueInput {
 float priority[4];
 bool hasFirst, rebuild;
};
struct SplitResult {
 DWORD circle[2][3],priority;
 int ok, head[2], next[5], prev[5], count, top;
};
static int WorkId(BYTE* p,BYTE old[][156],BYTE* added) {
 if(!p)return 0;
 if(p==added)return 5;
 for(int i=0;i<4;i++)if(p==old[i])return i+1;
 return -1;
}
static SplitResult RealSplit(bool original,int mode,int distance,bool reverse,const QueueInput* seed=0) {
 BYTE grid[80]={},queue[16]={},verts[4*16]={},pairs[2][24]={},links[4][16]={},cells[4][80]={};
 static BYTE cam[40200];memset(cam,0,sizeof cam);
 WORD ids[]={0,1,2,3};
 F(BYTE*,grid,4)=cam;F(BYTE*,grid,12)=verts;F(BYTE*,grid,32)=queue;
 BYTE old[4][156]={};
 if(seed) {
  for(int i=seed->hasFirst?0:2;i<4;i++){
   F(BYTE*,old[i],0)=F(BYTE*,old[i],4)=old[i^1];
   F(float,old[i],104)=seed->priority[i];
  }
  if(seed->hasFirst)F(BYTE*,queue,0)=old[0];
  F(BYTE*,queue,4)=old[2];
  F(DWORD,queue,8)=seed->rebuild?199:8;
 }
 F(float,cam,4)=(float)distance;F(float,cam,40152)=1.5f;F(float,cam,40156)=0.25f;
 for(int i=0;i<4;i++){
  F(DWORD,verts,i*16+8)=2+i*i;F(DWORD,verts,i*16+12)=16-3*i;
  F(BYTE*,pairs[i/2],(i%2)*4)=links[i];F(BYTE*,links[i],12)=cells[i];
  F(WORD*,cells[i],16)=ids+i;F(WORD*,cells[i],24)=ids+(i+2)%4;
  F(BYTE*,cells[i],64)=cells[reverse?i:i^1];
 }
 for(int i=0;i<2;i++){F(int,pairs[i],8)=1;F(int,pairs[i],16)=7+i;}
 if(mode==0)F(int,pairs[0],8)=0;
 if(mode==3)F(int,pairs[1],8)=0;
 BYTE* second=mode>=2?pairs[1]:0;
 typedef BYTE* (__thiscall *SplitFn)(BYTE*,BYTE*,BYTE*);
 allocs=frees=0;
 BYTE* n=original?((SplitFn)0x608fa0)(grid,pairs[0],second):Grid_NewSplit(grid,0,pairs[0],second);
 bool valid=mode==1||mode==2;
 SplitResult r={};r.ok=(!!n==valid && F(WORD,grid,64)==(int)valid &&
    (!original || (allocs==1 && frees==!valid)));
 if(!seed)r.ok &= F(DWORD,queue,8)==(int)valid && F(BYTE*,queue,4)==n;
 if(n){
  if(!seed)r.ok &= F(BYTE*,n,0)==n && F(BYTE*,n,4)==n;
  r.ok &= F(BYTE*,n,8)==pairs[0] && F(BYTE*,n,12)==second;
  for(int i=0;i<(second?4:2);i++)r.ok &= F(BYTE*,cells[i],72)==n;
  memcpy(r.circle[0],n+48,12);if(second)memcpy(r.circle[1],n+92,12);
  r.priority=F(DWORD,n,104);
  if(seed){
   r.count=F(DWORD,queue,8);
   r.ok &= r.count==(seed->rebuild?0:9);
   typedef BYTE* (__thiscall *TopFn)(BYTE*);
   BYTE* top=original?((TopFn)0x613790)(queue):MarineQueue_TopWorkItem(queue,0);
   r.top=WorkId(top,old,n);
   unsigned seen=0;
   for(int row=0;row<2;row++){
    BYTE* head=F(BYTE*,queue,row*4),*p=head;
    r.head[row]=WorkId(head,old,n);
    float last=-FLT_MAX;
    while(p){
     int id=WorkId(p,old,n);
     if(id<1 || (seen & (1u<<id))) {r.ok=0;break;}
     seen |= 1u<<id;
     BYTE* next=F(BYTE*,p,0),*prev=F(BYTE*,p,4);
     r.next[id-1]=WorkId(next,old,n);r.prev[id-1]=WorkId(prev,old,n);
     if(r.next[id-1]<1 || r.prev[id-1]<1) {r.ok=0;break;}
     r.ok &= F(BYTE*,next,4)==p && F(BYTE*,prev,0)==p && F(float,p,104)>=last;
     last=F(float,p,104); p=next;
     if(p==head)break;
    }
   }
   r.ok &= seen==(seed->hasFirst?62u:56u);
  }
  if(!original)::operator delete(n);
 }
 return r;
}
static bool CheckNonemptySplit(){
 int errors=0,cases=0;
 for(int mode=1;mode<=2;mode++)for(int pattern=0;pattern<5;pattern++)
 for(int first=0;first<=1;first++)for(int rebuild=0;rebuild<=1;rebuild++){
  const int distance=250;
  DWORD bits=RealSplit(true,mode,distance,false).priority;
  float p;memcpy(&p,&bits,sizeof p);
  const float offsets[][4]={{1,3,2,4},{-4,-2,-3,-1},{-2,2,-1,1},{0,0,0,0},{-1,0,0,1}};
  QueueInput seed={};
  for(int i=0;i<4;i++)seed.priority[i]=p+offsets[pattern][i];
  seed.hasFirst=first!=0;seed.rebuild=rebuild!=0;
  SplitResult ref=RealSplit(true,mode,distance,false,&seed);
  SplitResult got=RealSplit(false,mode,distance,false,&seed);
  if(!ref.ok || !got.ok || memcmp(&ref,&got,sizeof ref)){
   printf("queue mismatch mode=%d pattern=%d first=%d rebuild=%d valid=%d/%d top=%d/%d\n",mode,pattern,first,rebuild,ref.ok,got.ok,ref.top,got.top);
   errors++;
  }
  cases++;
 }
 printf("nonempty split cases=%d mismatches=%d\n",cases,errors);return !errors;
}
static bool CheckRealSplit(){
 Redirect((void*)0x632643,(void*)SplitAlloc);Redirect((void*)0x63266c,(void*)SplitFree);
 int errors=0;
 for(int m=0;m<4;m++)for(int rev=0;rev<2;rev++)for(int j=0;j<3;j++){
  int d=j==0?0:j==1?250:1800;
  SplitResult ref=RealSplit(true,m,d,rev!=0),got=RealSplit(false,m,d,rev!=0);
  if(!ref.ok||!got.ok||memcmp(&ref,&got,sizeof ref)){
   if(!errors)printf("split mismatch mode=%d reverse=%d d=%d oracle=%d production=%d score=%08x/%08x\n",m,rev,d,ref.ok,got.ok,ref.priority,got.priority);
   errors++;
  }
 }
 printf("real split cases=24 mismatches=%d\n",errors);return errors==0;
}
extern "C" double __fastcall GridCamera_NodePriority(BYTE*,void*,BYTE*);
extern "C" int __fastcall Point_InWedge(const float*,void*,float,float,float,float,float,float);
static bool CheckPriority() {
 // No queue, renderer or geometry hooks: both PE routines read the same immutable inputs.
 _controlfp(_PC_53,_MCW_PC);
 typedef double (__thiscall *Priority)(BYTE*,BYTE*);
 typedef int (__thiscall *Wedge)(const float*,float,float,float,float,float,float);
 float distances[]={-1,0,1,49.5f,50,50.5f,499.5f,500,500.5f,1500,1500.5f};
 int count=0,errors=0;
 for(int i=0;i<11;i++)for(int level=7;level<=8;level++)for(int mask=0;mask<4;mask++){
  BYTE cam[96]={},node[48]={};
  F(float,cam,4)=0.25f;F(float,cam,8)=-0.5f;
  F(float,node,32)=distances[i]+1.25f;F(float,node,36)=-0.5f;F(float,node,40)=1;
  F(float,node,20)=17;F(int,node,24)=level;
  F(float,cam,64)=mask&1?1.f:-1.f;F(float,cam,88)=mask&2?1.f:-1.f;
  double ref=((Priority)0x6097d0)(cam,node),got=GridCamera_NodePriority(cam,0,node);
  float* circle=(float*)(node+32);
  int refW=((Wedge)0x608460)(circle,0.25f,-0.5f,0,F(float,cam,64),0,F(float,cam,88));
  int gotW=Point_InWedge(circle,0,0.25f,-0.5f,0,F(float,cam,64),0,F(float,cam,88));
  if(memcmp(&ref,&got,sizeof ref) || refW!=gotW) {
   if(!errors)printf("priority mismatch d=%g level=%d mask=%d ref=%.17g got=%.17g wedge=%d/%d\n",distances[i],level,mask,ref,got,refW,gotW);
   errors++;
  }
  count++;
 }
 printf("priority/wedge pairs=%d mismatches=%d\n",count,errors);return !errors;
}
#include "merge_cases.h"
#include "sequence_cases.h"
#include "../solo_audit_test/cases.h"
#include "../solo_audit_test/company_world.h"
#include "../solo_audit_test/ring_race.h"
#include "../solo_audit_test/roadside_service.h"
#include "../solo_audit_test/police_fines.h"
#include "../solo_audit_test/delivery_rank.h"
#include "../solo_audit_test/market_share.h"
#include "../solo_audit_test/rival_campaign.h"
#include "../solo_audit_test/bribe_offers.h"
#include "../solo_audit_test/mafia_settlement.h"
#include "../solo_audit_test/ring_start.h"
#include "../solo_audit_test/ring_grid.h"
#include "../solo_audit_test/ring_countdown.h"
#include "../solo_audit_test/service_shop.h"
#include "../solo_audit_test/vehicle_dealers.h"
#include "../solo_audit_test/first_assignment.h"
#include "../solo_audit_test/rank_projection_replay.h"
#include "mesh_join_cases.h"
static LONG WINAPI ComparisonException(EXCEPTION_POINTERS* x) {
 fprintf(stderr,"comparison exception %08lx at %p, harness RVA %08lx\n",x->ExceptionRecord->ExceptionCode,x->ExceptionRecord->ExceptionAddress,(DWORD)x->ExceptionRecord->ExceptionAddress-(DWORD)GetModuleHandle(nullptr)); return EXCEPTION_EXECUTE_HANDLER;
}
int main(int argc,char**argv) {
 setvbuf(stdout,nullptr,_IONBF,0);
 SetUnhandledExceptionFilter(ComparisonException);
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 if((argc!=2 && argc!=3 && !(argc==4 && (!strcmp(argv[2],"rank-replay") || !strcmp(argv[2],"bridge-hull")))) || !MapOriginal(argv[1]))return 2;
 if(argc==4 && !strcmp(argv[2],"bridge-hull")) return bridge_contacts::Zil(argv[3]);
 if(argc==4) return rank_replay::Run(argv[3]);
 if(argc==3 && !strcmp(argv[2],"rival-campaign")) return rival_campaign::Both()?0:3;
 if(argc==3 && !strcmp(argv[2],"company")) {
     bool companies=company_world::Check();
     bool ring=ring_race::Both();
     bool services=roadside_service::Check();
     bool police=police_fines::Both();
     bool rank=delivery_rank::Both();
     bool standings=market_share::Both();
     bool offers=bribe_offers::Check();
     bool settlement=mafia_settlement::Both();
     bool start=ring_start::Both();
     bool shop=service_shop::Check();
     bool dealers=vehicle_dealers::Check();
     bool assignment=first_assignment::Both();
     bool recruitment=ring_grid::Both();
     bool countdown=ring_countdown::Both();
     return countdown && recruitment && companies && ring && services && police && rank && standings && offers && settlement && start && shop && dealers && assignment ? 0 : 3;
 }
 if(argc==3 && !strcmp(argv[2],"gameplay")) return RunSoloAudit()?0:3;
 if(!CheckPriority())return 3;
 if(!CheckMerges())return 3;
 if(!sequence::Check())return 3;
 if(!sequence::CheckDiamonds())return 3;
 if(!CheckRealSplit())return 3;
 if(!CheckNonemptySplit())return 3;
 if(!CheckMarineJoin(argc==3 && !strcmp(argv[2],"old-marine")))return 3;
 struct Hook {DWORD original;void* production;void* stub;} hooks[]={
  {0x608fa0,(void*)Grid_NewSplit,(void*)Split},
  {0x60a1c0,(void*)Grid_MergeCells,(void*)Merge},
  {0x60a460,(void*)Grid_MergeFourCells,(void*)MergeFour},
  {0x6136f0,(void*)MarineQueue_RemoveLink,(void*)Remove},
  {0x613790,(void*)MarineQueue_TopWorkItem,(void*)Top},
  {0x63266c,(void*)DeleteEntry,(void*)Delete}
 };
 for(int i=0;i<sizeof hooks/sizeof *hooks;i++){Redirect((void*)hooks[i].original,hooks[i].stub);Redirect(hooks[i].production,hooks[i].stub);}
 int failures=0;
 for(int t=0;t<36;t++) {
  Observation want,got;Run(true,t,&want);Run(false,t,&got);
  bool reference=Valid(want);
  bool same=!memcmp(&want.state,&got.state,sizeof want.state) && !strcmp(want.trace,got.trace) && want.splits==got.splits;
  bool ok=reference && Valid(got) && same;failures+=!ok;
  printf("case %d reference=%d badThis=%d args=%d stack=%d state=%d same=%d splits=%d exception=%08lX fault=%08lX trace=%s %s\n",t,reference,got.badThis,got.argsOK,got.stackOK,got.stateOK,same,got.splits,got.exception,got.fault,got.trace[0]?got.trace:"empty",ok?"PASS":"FAIL");fflush(stdout);
  if(want.exception || got.exception)break;
 }
 return failures?1:0;
}
