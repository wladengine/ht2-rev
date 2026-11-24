#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
extern "C" unsigned char KingBss[];
extern "C" unsigned char KingData[];
extern "C" void __fastcall GameWindow_Draw(BYTE*,void*);
extern "C" void __fastcall MovieScreen_Frame(BYTE*,void*);
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





extern "C" BYTE* __fastcall InfoScreen_Construct(BYTE*,void*,int,int);
extern "C" BYTE* __fastcall Shell528c20_Construct(BYTE*,void*,int);
extern "C" BYTE* __fastcall Windows_Add2(BYTE*,void*,BYTE*);
extern "C" BYTE* __fastcall Windows_Add(BYTE*,void*,BYTE*);
extern "C" int __fastcall MainWnd_SetVideoMode(BYTE*,void*,int,int,int,int);
extern "C" int __cdecl Movie_Play(const char*,int);
extern "C" int __cdecl Dd_TestMode(int,int,int,DWORD,int*,DWORD*,DWORD*);
extern "C" void __fastcall Palette_Apply(BYTE*,void*);
extern "C" void __fastcall Window_Refresh(BYTE*,void*);
extern "C" void __fastcall Window_Show(BYTE*,void*,int);
extern "C" void __cdecl Shell_CheckSave();
extern "C" int __fastcall Avi_Start(BYTE*,void*);
extern "C" void __fastcall Media_Close(BYTE*,void*);
static void SetGlobal(unsigned a,int v){*(int*)a=v;*(int*)(a>=0x68a820?KingBss+a-0x68a820:KingData+a-0x66c000)=v;}
static void Redirect(void* address,void* fn){DWORD old;BYTE*p=(BYTE*)address;if(!VirtualProtect(p,5,PAGE_EXECUTE_READWRITE,&old))ExitProcess(2);p[0]=0xe9;F(int,p,1)=(BYTE*)fn-p-5;FlushInstructionCache(GetCurrentProcess(),p,5);VirtualProtect(p,5,old,&old);}
static BYTE* applied;static int applies;
static void __fastcall Apply(BYTE* p,void*){applied=p;applies++;}
static void __fastcall NoThis(BYTE*,void*){}
static void __fastcall Show(BYTE*,void*,int){}
static void __cdecl NoC(){}
static void* vtbl[10];
static BYTE* __fastcall Info(BYTE*p,void*,int,int){F(void*,p,0)=vtbl;return p;}
static BYTE* __fastcall Shell(BYTE*p,void*,int){F(void*,p,0)=vtbl;return p;}
static BYTE* __fastcall Add(BYTE*,void*,BYTE*p){return p;}
static int __fastcall Mode(BYTE*,void*,int,int,int,int){return 1;}
static int __cdecl Movie(const char*,int){return 0;}
static int __cdecl TestMode(int,int,int,DWORD,int*,DWORD*,DWORD*){return 1;}
static void* __cdecl Allocate(unsigned n){return ::operator new(n);}
// Patch only side-effecting collaborators in this disposable fixture process.
// The original and production callers, pointer chain and Window_SetSurface run unchanged.
#define BOTH(addr,name,stub) Redirect((void*)addr,(void*)stub);Redirect((void*)name,(void*)stub)
static BYTE app[9500],cluster[180],palette[816],table[64],parent[64],surface[128],screen[2400];
static BYTE* palettes[1]={palette};
struct Result {BYTE* root;BYTE* applied;int applies;};
static Result Run(bool original,int t){
 memset(surface,0,sizeof surface);memset(screen,0,sizeof screen);F(void*,screen,0)=vtbl;
 F(void*,parent,12)=surface;F(void*,app,9396)=cluster;F(void*,cluster,100)=palettes;F(void*,palette,0)=table;
 SetGlobal(0x696ca0,(int)app);SetGlobal(0x69684c,(int)parent);SetGlobal(0x6cec88,(int)screen);SetGlobal(0x6cec80,(int)screen);
 SetGlobal(0x675d44,1);SetGlobal(0x675d40,t==1?1:0);applied=0;applies=0;
 if(t<2){if(original)((void(__thiscall*)(BYTE*))0x528820)(screen);else GameWindow_Draw(screen,0);}
 else {F(int,screen,2304)=7;F(int,screen,2308)=t-2;if(original)((void(__thiscall*)(BYTE*))0x528ca0)(screen);else MovieScreen_Frame(screen,0);}
 Result r={F(BYTE*,surface,84),applied,applies};return r;
}
int main(int argc,char**argv){
 if(argc!=2||!MapOriginal(argv[1]))return 2;
 for(int i=0;i<10;i++)vtbl[i]=(void*)NoThis;
 Redirect((void*)0x632643,(void*)Allocate);
 BOTH(0x519300,InfoScreen_Construct,Info);BOTH(0x528c20,Shell528c20_Construct,Shell);
 BOTH(0x5e41f0,Windows_Add2,Add);BOTH(0x5e4210,Windows_Add,Add);
 BOTH(0x5dd8c0,MainWnd_SetVideoMode,Mode);BOTH(0x5279e0,Movie_Play,Movie);BOTH(0x5d7c20,Dd_TestMode,TestMode);
 BOTH(0x5ba3d0,Palette_Apply,Apply);BOTH(0x5d88c0,Window_Refresh,NoThis);BOTH(0x5d8630,Window_Show,Show);BOTH(0x527de0,Shell_CheckSave,NoC);
 // Avi_Start/Media_Close addresses supplied below after inspecting source labels.
 BOTH(0x5c2450,Avi_Start,NoThis);BOTH(0x5c12a0,Media_Close,NoThis);
 int failures=0;
 for(int t=0;t<4;t++){
  Result want=Run(true,t),got=Run(false,t);
  bool reference=want.root==(t<2?palette:0)&&want.applied==(t?palette:0)&&want.applies==(t?1:0);
  bool root=want.root==got.root,apply=want.applied==got.applied,state=want.applies==got.applies;
  bool ok=reference&&root&&apply&&state;failures+=!ok;
  printf("case %d reference=%d root=%d palette=%d state=%d %s\n",t,reference,root,apply,state,ok?"PASS":"FAIL");
 }
 printf("%d/4 failures\n",failures);return failures?1:0;
}
