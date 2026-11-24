#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
extern "C" unsigned char KingBss[];
extern "C" void __fastcall Obj580ff0_Place(BYTE*,void*);
static float identity[12]={1,0,0,0,1,0,0,0,1,0,0,0};
#define F(T,p,o) (*(T*)((BYTE*)(p)+(o)))
static BYTE* reserved;
extern "C" void __cdecl mainCRTStartup();
extern "C" void __cdecl TrafficEntry(){reserved=(BYTE*)VirtualAlloc((void*)0x400000,0x3bd000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);mainCRTStartup();}
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
    // The game's startup normally initializes these axes/identity. No entry point.
    memcpy((void*)0x7025f0,identity,48);
    FlushInstructionCache(GetCurrentProcess(),image,nt->OptionalHeader.SizeOfImage);
    VirtualFree(raw,0,MEM_RELEASE);
    return image;
}


struct Observation { int queries, applies, resets, flags; BYTE* room; float from[3],to[3],matrix[12]; };
static Observation seen;
static BYTE root[48], nextRoom[48];
static void* rootVtbl[14];
static void* itemVtbl[17];
static int queryResult;
static int __fastcall Query(BYTE*,void*,int flags,BYTE* q) {
 seen.queries++;seen.flags=flags;
 memcpy(seen.from,q+76,12);memcpy(seen.to,q+88,12);
 F(BYTE*,q,104)=nextRoom;
 return queryResult;
}
static void __fastcall Apply(BYTE*,void*,const float* m,BYTE* room) {
 seen.applies++;seen.room=room;memcpy(seen.matrix,m,48);
}
static void __fastcall Reset(BYTE*,void*,int a) {seen.resets+=a;}
typedef void (__thiscall* Original)(BYTE*);
static Observation Run(bool original,int test) {
 BYTE item[1200]={0}; F(void*,item,0)=itemVtbl;
 memcpy(item+500,identity,48);memcpy(item+692,identity,48);
 F(float,item,536)=-2318.5f+test*100;F(float,item,540)=29.25f-test*20;F(float,item,544)=31.125f;
 F(float,item,728)=2.5f;F(float,item,732)=-1.25f;F(float,item,736)=.5f;
 if(test==1) {F(float,item,500)=0;F(float,item,504)=1;F(float,item,512)=-1;F(float,item,516)=0;}
 F(BYTE*,item,800)=test==4?0:root;
 if(test==3) item[1132]='x';
 queryResult=test==2?0:2;
 memset(&seen,0,sizeof seen);
 if(original)((Original)0x580ff0)(item);else Obj580ff0_Place(item,0);
 return seen;
}
int main(int argc,char**argv) {
 if(argc!=2 || !MapOriginal(argv[1]))return 2;
 rootVtbl[13]=(void*)Query;F(void*,root,0)=rootVtbl;
 itemVtbl[7]=(void*)Reset;itemVtbl[16]=(void*)Apply;
 int failures=0;
 for(int t=0;t<5;t++) {
  Observation want=Run(true,t),got=Run(false,t);
  bool queried=t<3;
  bool reference=want.applies==1 && want.queries==(queried?1:0) && want.flags==0 &&
   (!queried || !memcmp(want.to,want.matrix+9,12)) && want.resets==(t==4?1:0) &&
   want.room==((t==0||t==1)?nextRoom:(t==4?0:root));
  bool target=!queried || !memcmp(got.to,want.to,12);
  bool rest=got.queries==want.queries && got.applies==want.applies && got.resets==want.resets && got.flags==want.flags && got.room==want.room && !memcmp(got.matrix,want.matrix,48) && !memcmp(got.from,want.from,12);
  bool ok=reference&&target&&rest;failures+=!ok;
  printf("case %d reference=%d target=%d other_state=%d original=(%.6g,%.6g,%.6g) production=(%.6g,%.6g,%.6g) %s\n",t,reference,target,rest,want.to[0],want.to[1],want.to[2],got.to[0],got.to[1],got.to[2],ok?"PASS":"FAIL");
 }
 printf("%d/5 failures\n",failures);return failures?1:0;
}
