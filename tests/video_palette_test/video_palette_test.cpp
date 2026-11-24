#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
extern "C" unsigned char KingBss[];
extern "C" void __cdecl Video_SetPalette(const BYTE*,DWORD,DWORD);
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




static void SetGlobal(unsigned a,int v){*(int*)a=v;*(int*)(KingBss+a-0x68a820)=v;}
struct Result {int calls,unlocks;DWORD flags,first,count;BYTE entries[1024];};
static Result seen;
static long __stdcall Entries(void*,DWORD flags,DWORD first,DWORD count,const BYTE* p){
 seen.calls++;seen.flags=flags;seen.first=first;seen.count=count;
 // Original only initializes [first,count). Do not compare unspecified prefix.
 if(count>first)memcpy(seen.entries+first*4,p+first*4,(count-first)*4);return 0;
}
static long __stdcall Unlock(void*,void*){seen.unlocks++;return 0;}
static void* palVtbl[7];static void* ddVtbl[33];
static void** pal=palVtbl;static void** dd=ddVtbl;
static Result Run(bool original,int t){
 BYTE input[2048];for(int i=0;i<2048;i++)input[i]=(BYTE)(i*37+i/256);
 SetGlobal(0x7208ec,t==4?1:2);SetGlobal(0x7208a8,t==2?1:0);
 SetGlobal(0x7208f4,t==3?0:(int)&pal);SetGlobal(0x7208fc,(int)&dd);
 memset(&seen,0,sizeof seen);
 DWORD first=t==1?3:0,count=t==1?17:256;
 if(original)((void(__cdecl*)(const BYTE*,DWORD,DWORD))0x5d3ec0)(input,first,count);
 else Video_SetPalette(input,first,count);
 return seen;
}
int main(int argc,char**argv){
 if(argc!=2||!MapOriginal(argv[1]))return 2;
 palVtbl[6]=(void*)Entries;ddVtbl[32]=(void*)Unlock;int failures=0;
 for(int t=0;t<5;t++){
  Result want=Run(true,t),got=Run(false,t);bool ref=want.calls==(t<3?1:0)&&want.unlocks==(t==2?1:0);
  if(t<3)for(DWORD i=want.first;i<want.count;i++){
   DWORD pos=want.first*8+(i-want.first)*4;
   for(int c=0;c<3;c++){DWORD k=pos+2-c;ref=ref&&want.entries[i*4+c]==(BYTE)(k*37+k/256);}
   ref=ref&&want.entries[i*4+3]==4;
  }
  bool pixels=!memcmp(want.entries,got.entries,1024);
  bool state=want.calls==got.calls&&want.unlocks==got.unlocks&&want.flags==got.flags&&want.first==got.first&&want.count==got.count;
  bool ok=ref&&pixels&&state;failures+=!ok;
  printf("case %d reference=%d pixels=%d state=%d %s\n",t,ref,pixels,state,ok?"PASS":"FAIL");
 }
 printf("%d/5 failures\n",failures);return failures?1:0;
}
