#include <windows.h>
#include <stdio.h>
#include <string.h>
extern "C" unsigned char KingBss[];

#define F(T,p,o) (*(T*)((BYTE*)(p)+(o)))
static BYTE* reserved;
extern "C" void __cdecl mainCRTStartup();
extern "C" void __cdecl RouteEntry(){reserved=(BYTE*)VirtualAlloc((void*)0x400000,0x3bd000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);mainCRTStartup();}
static BYTE* MapOriginal(const char* path) {
    // Reserve before allocating the raw file: an arbitrary allocation can itself
    // occupy the preferred original image address. Runner pins the input SHA256.
    BYTE* image=reserved;
    if(!image) {
        MEMORY_BASIC_INFORMATION info;
        for(BYTE* p=(BYTE*)0x400000;p<(BYTE*)0x7bd000;) {
            VirtualQuery(p,&info,sizeof info);
            fprintf(stderr,"original address occupied: base=%p allocation=%p size=%x state=%x type=%x\n",info.BaseAddress,info.AllocationBase,(unsigned)info.RegionSize,info.State,info.Type);
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
    // Do not run the original's entry point; only Route_PushRoad and its stack helpers.
    FlushInstructionCache(GetCurrentProcess(),image,nt->OptionalHeader.SizeOfImage);
    VirtualFree(raw,0,MEM_RELEASE);
    return image;
}



extern "C" int __fastcall Route_PushRoad(BYTE*,void*,void*,const float*);
extern "C" void* __cdecl Room_FindIdByName(void*,void*);
extern "C" int* __cdecl Ai_GetPositionIdRV(int*,int,float,float,float);
static void Redirect(void* address,void* fn) {
 DWORD old; BYTE* p=(BYTE*)address;
 if(!VirtualProtect(p,5,PAGE_EXECUTE_READWRITE,&old))ExitProcess(2);
 p[0]=0xe9; F(int,p,1)=(BYTE*)fn-p-5;
 FlushInstructionCache(GetCurrentProcess(),p,5);VirtualProtect(p,5,old,&old);
}
struct Position { int road,node; double t; int segment,object,a,b; };
static_assert(sizeof(Position)==32,"fixture PositionId ABI");
static int testCase, roomCalls, positionCalls, argumentErrors, overwrites;
static DWORD* protectedReturn;
static DWORD overwrittenReturn;
static void* callTarget;
static BYTE edge[128], nodeA[32],nodeB[32];
static BYTE* roads[6]={0,0,0,0,0,edge};
static const float point[3]={1.25f,-3.0f,8.0f};
static void* __cdecl Room(void* out,void* key) {
 ++roomCalls; argumentErrors+=key!=(void*)0x1234;
 *(int*)out=testCase==0?-1:7;return out;
}
static int* __cdecl PositionResult(int* out,int room,float x,float y,float z) {
 ++positionCalls; argumentErrors+=room!=7 || x!=point[0] || y!=point[1] || z!=point[2];
 // Full API output, including node=-1 that overwrote the old return address.
 Position p={testCase==1?-1:5,-1,0.375,0,16,0,-1};
 BYTE before[32];memcpy(before,out,32);
 memcpy(out,&p,sizeof p);
 if((BYTE*)out < (BYTE*)(protectedReturn+3) && (BYTE*)out+32 > (BYTE*)protectedReturn) {
  ++overwrites;overwrittenReturn=*protectedReturn;
  // Observe the actual overwrite, then restore the adjacent stack so the
  // regression can report it instead of corrupting the runner's SEH chain.
  memcpy((BYTE*)out+4,before+4,28);
 }
 return out;
}
struct Observation {
 BYTE route[3104]; int result, rooms, positions, args, guards; DWORD exception, fault; int overwrites; DWORD overwrittenReturn;
};
static DWORD Fault(EXCEPTION_POINTERS* e,Observation* out) {
 out->exception=e->ExceptionRecord->ExceptionCode;
 out->fault=(DWORD)e->ExceptionRecord->ExceptionAddress;
 return EXCEPTION_EXECUTE_HANDLER;
}
// Record the exact caller return slot independently of either compiler frame.
// Test-only ABI adapter: both entry points take ECX=self and pop two arguments.
static int CallRoute(BYTE* route) {
 int result;
 __asm {
  mov ecx,route
  xor edx,edx
  lea eax,point
  push eax
  push 1234h
  lea eax,[esp-4]
  mov protectedReturn,eax
  call dword ptr [callTarget]
  mov result,eax
 }
 return result;
}
static void Invoke(bool original,BYTE* route,Observation* out) {
 callTarget=original?(void*)0x5610f0:(void*)Route_PushRoad;
 __try { out->result=CallRoute(route); }
 __except(Fault(GetExceptionInformation(),out)) {}
}
static void Run(bool original,int t,Observation* out) {
 struct Guarded { DWORD before[4]; BYTE route[3104]; DWORD after[4]; } g;
 memset(&g,0,sizeof g);memset(out,0,sizeof *out);
 for(int i=0;i<4;i++)g.before[i]=g.after[i]=0x76543210;
 testCase=t;roomCalls=positionCalls=argumentErrors=overwrites=0;overwrittenReturn=0;
 int top=1;short a=10,b=20;
 F(short,g.route,0)=10;F(short,g.route,2)=20;g.route[2000]=1;
 if(t==3 || t==7 || t==8){top=2;F(short,g.route,4)=30;}
 if(t==4){a=40;b=50;}
 if(t==5){a=30;b=20;}
 if(t==6){a=20;b=30;}
 if(t==7 || t==8){a=20;b=40; if(t==7)g.route[2001]=1;}
 if(t==9){a=20;b=10;}
 if(t==10){top=999;a=40;b=50;for(int i=2;i<=999;i++)F(short,g.route,i*2)=30;}
 F(int,g.route,3000)=top;F(short,nodeA,16)=a;F(short,nodeB,16)=b;
 Invoke(original,g.route,out);
 memcpy(out->route,g.route,sizeof g.route);
 out->overwrites=overwrites;out->overwrittenReturn=overwrittenReturn;
 out->rooms=roomCalls;out->positions=positionCalls;out->args=argumentErrors;
 out->guards=1;
 for(int i=0;i<4;i++)if(g.before[i]!=0x76543210 || g.after[i]!=0x76543210)out->guards=0;
}
int main(int argc,char**argv) {
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 if(argc!=2 || !MapOriginal(argv[1]))return 2;
 Redirect((void*)0x4364f0,(void*)Room);Redirect((void*)Room_FindIdByName,(void*)Room);
 Redirect((void*)0x437390,(void*)PositionResult);Redirect((void*)Ai_GetPositionIdRV,(void*)PositionResult);
 F(BYTE*,edge,112)=nodeA;F(BYTE*,edge,116)=nodeB;
 *(BYTE***)0x68cf38=roads;*(BYTE***)(KingBss+0x68cf38-0x68a820)=roads;
 const int expected[]={-1,-2,0,1,2,3,4,4,4,0,-3};
 int failures=0;
 for(int t=0;t<11;t++) {
  Observation want,got;Run(true,t,&want);Run(false,t,&got);
  bool reference=!want.overwrites && !want.exception && want.result==expected[t] && want.guards && !want.args && want.rooms==1 && want.positions==(t?1:0);
  bool same=!got.exception && got.result==want.result && got.guards && !got.args && got.rooms==want.rooms && got.positions==want.positions && !memcmp(got.route,want.route,sizeof got.route);
  bool capacity=got.overwrites==0;
  bool ok=reference&&same&&capacity;failures+=!ok;
  printf("case %d reference=%d original=%d production=%d exception=%08lX fault=%08lX state=%d capacity=%d overwritten_return=%08lX %s\n",t,reference,want.result,got.result,got.exception,got.fault,same,capacity,got.overwrittenReturn,ok?"PASS":"FAIL");fflush(stdout);
  // A broken stack must not contaminate further cases in this process.
  if(got.exception)break;
 }
 return failures?1:0;
}
