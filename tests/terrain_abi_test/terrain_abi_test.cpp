#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <float.h>
#include <initializer_list>
extern "C" unsigned char KingBss[];

#define F(T,p,o) (*(T*)((BYTE*)(p)+(o)))
static BYTE* reserved;
extern "C" void __cdecl mainCRTStartup();
extern "C" void __cdecl TerrainEntry(){reserved=(BYTE*)VirtualAlloc((void*)0x400000,0x3bd000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);mainCRTStartup();}
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
    // Do not run the original's entry point; only the terrain callers and copying wrappers under test.
    FlushInstructionCache(GetCurrentProcess(),image,nt->OptionalHeader.SizeOfImage);
    VirtualFree(raw,0,MEM_RELEASE);
    return image;
}



struct Vec3T { float x,y,z; };
extern "C" void __fastcall Patch_AddPlantFromVectors(BYTE*,void*,const float*,const float*,float,float,int);
struct Vec3;
extern "C" void __fastcall Patch_AddPlant(BYTE*,void*,const Vec3*,const Vec3*,float,float,int);
extern "C" float* __fastcall Terrain_Call614960(BYTE*,void*,const Vec3T*,const Vec3T*,const Vec3T*);
extern "C" void __fastcall Terrain_PlaceCameraView(BYTE*,void*,const float*,const float*,const float*);
extern "C" float* __fastcall TerrainView_Move(BYTE*,void*,const float*,const float*,const float*);
extern "C" void __fastcall TerrainView_Place(BYTE*,void*,const float*,const float*,const float*);
extern "C" int __cdecl Terrain_FollowCamera(int,int);
extern "C" void __fastcall Copter_OnModuleLoad(BYTE*,void*,BYTE*);
extern "C" void* __fastcall Camera_Get(BYTE*,void*,void*);
extern "C" void __fastcall Terrain615670_Update(BYTE**,void*,int);
extern "C" void __fastcall Terrain616770_SetCorners(BYTE**,void*,BYTE**,int);
extern "C" void __fastcall Room_Each(BYTE*,void*,void*,void*);
extern "C" BYTE* __fastcall Room_FindObject(BYTE*,void*,const char*);
extern "C" BYTE* __cdecl Objects_Find(const char*);
extern "C" int __cdecl Ai_OnModuleLoadX(const char*);
extern "C" int __cdecl AnmObj_OnLoad(BYTE*,void*);
static void Redirect(void* address,void* fn) {
 DWORD old; BYTE* p=(BYTE*)address;
 if(!VirtualProtect(p,5,PAGE_EXECUTE_READWRITE,&old))ExitProcess(2);
 p[0]=0xe9; F(int,p,1)=(BYTE*)fn-p-5;
 FlushInstructionCache(GetCurrentProcess(),p,5);VirtualProtect(p,5,old,&old);
}
struct Observation {
 char trace[32]; float vectors[9];
 int selfOK,copiesOK,argsOK,inputsOK,stackOK,returnOK,globalsOK;
 DWORD exception,fault;
};
static Observation* obs;
static bool referenceRun;
static int testCase, traceSize, findCount;
static BYTE terrain[128],game[1400],camera[64],room[128],owner[32],objects[3][16];
static float matrix[12]={1,2,3,4,5,6,7,8,9,125.5f,-42.25f,16};
static Vec3T input[3]={{1.25f,-2.5f,3.75f},{4,5,6},{0,0,1}};
static const char* moduleName;
static BYTE* Global(DWORD address) { return referenceRun?(BYTE*)address:KingBss+address-0x68a820; }
static void Event(char c) { if(traceSize<31)obs->trace[traceSize++]=c;else obs->argsOK=0; }
static void __fastcall Plant(BYTE* patch,void*,float* pos,float* side,float size,float brightness,int type) {
 Event('T');obs->selfOK=patch==terrain;
 obs->copiesOK=pos!=side;
 for(int j=0;j<3;j++)obs->copiesOK&=pos!=(float*)&input[j] && side!=(float*)&input[j];
 memcpy(obs->vectors,pos,12);memcpy(obs->vectors+3,side,12);
 obs->argsOK=size==2.25f && brightness==0.75f && type==7;
 pos[0]=90;side[2]=91;
}
static void Capture(BYTE* self,const float* a,const float* b,const float* c,char kind) {
 Event(kind);obs->selfOK&=self==terrain;
 const float* p[3]={a,b,c};
 obs->copiesOK&=a!=b && b!=c && a!=c;
 for(int i=0;i<3;i++) {
  // The endpoint must receive independent writable locals, including when the
  // wrapper's three source pointers alias. Check both direct and global inputs.
  for(int j=0;j<3;j++)obs->copiesOK&=p[i]!=(float*)&input[j];
  obs->copiesOK&=p[i]!=(float*)Global(0x702630);
  memcpy(obs->vectors+i*3,p[i],12);
 }
 // Copy first, mutate afterwards: original inputs must remain byte-identical.
 for(int i=0;i<3;i++) for(int j=0;j<3;j++)const_cast<float*>(p[i])[j]=float(90+i*3+j);
}
static float* __fastcall Move(BYTE* self,void*,const float* a,const float* b,const float* c) {
 Capture(self,a,b,c,'M'); return (float*)(terrain+16);
}
static void __fastcall Place(BYTE* self,void*,const float* a,const float* b,const float* c) { Capture(self,a,b,c,'P'); }
static void* __fastcall Camera(BYTE* self,void*,void* out) {
 Event('C'); obs->argsOK&=self==camera; memcpy(out,matrix,sizeof matrix);return out;
}
static void __fastcall Update(BYTE** self,void*,int frame) {
 Event('U');obs->argsOK&=(BYTE*)self==terrain && frame==(testCase==4?0:123);
}
static void __fastcall Each(BYTE* self,void*,void* callback,void* context) {
 Event('E');obs->argsOK&=self==room && !context && callback==(referenceRun?(void*)0x56e060:(void*)AnmObj_OnLoad);
}
static BYTE* __fastcall Find(BYTE* self,void*,const char* name) {
 Event('F');obs->argsOK&=self==room && !strcmp(name,"skyMirrorKey");return 0;
}
static BYTE* __cdecl Object(const char* name) {
 Event('O');const char* names[]={"ter000","ter050","ter100"};
 if(findCount>=3){obs->argsOK=0;return 0;}
 obs->argsOK&=!strcmp(name,names[findCount]);return objects[findCount++];
}
static void __fastcall Corners(BYTE** self,void*,BYTE** c,int n) {
 Event('S');obs->argsOK&=(BYTE*)self==terrain && n==3;
 for(int i=0;i<3;i++)obs->argsOK&=c[i]==objects[i];
}
static int __cdecl AI(const char* name) { Event('A');obs->argsOK&=!strcmp(name,moduleName);return 1; }
static DWORD Fault(EXCEPTION_POINTERS* e) {
 obs->exception=e->ExceptionRecord->ExceptionCode;
 obs->fault=(DWORD)e->ExceptionRecord->ExceptionAddress;
 return EXCEPTION_EXECUTE_HANDLER;
}
// Test-only adapter invokes the same x86 ABI for mapped original and linked
// production. EBP is retained (/Oy-); capture and repair ESP after each call so
// a pop-count regression is reported without contaminating subsequent cases.
static void Invoke() {
 void* target;
 DWORD savedSP,afterSP,result;
 if(testCase<4) {
  target=referenceRun?(testCase%2?(void*)0x615230:(void*)0x614f00):
      (testCase%2?(void*)Terrain_PlaceCameraView:(void*)Terrain_Call614960);
  const Vec3T* a=&input[0];const Vec3T* b=testCase>=2?a:&input[1];const Vec3T* c=testCase>=2?a:&input[2];
  __asm {
   mov savedSP,esp
   push c
   push b
   push a
   lea ecx,terrain
   xor edx,edx
   call target
   mov result,eax
   mov afterSP,esp
   mov esp,savedSP
  }
  if(!(testCase%2))obs->returnOK=result==(DWORD)(terrain+16);
 } else if(testCase<6) {
  target=referenceRun?(void*)0x569450:(void*)Terrain_FollowCamera;
  int frame=testCase==4?0:123;
  __asm {
   mov savedSP,esp
   push frame
   push 456
   call target
   add esp,8
   mov result,eax
   mov afterSP,esp
   mov esp,savedSP
  }
  obs->returnOK=result==1;
 } else {
  if(testCase>=9) {
   target=referenceRun?(void*)0x6197e0:(void*)Patch_AddPlantFromVectors;
   const Vec3T* a=&input[0];const Vec3T* b=testCase==9?&input[1]:a;
   __asm {
    mov savedSP,esp
    push 7
    push 03f400000h
    push 040100000h
    push b
    push a
    lea ecx,terrain
    xor edx,edx
    call target
    mov afterSP,esp
    mov esp,savedSP
   }
   obs->stackOK=savedSP==afterSP;
   return;
  }
  target=referenceRun?(void*)0x56dde0:(void*)Copter_OnModuleLoad;
  __asm {
   mov savedSP,esp
   lea eax,room
   push eax
   lea ecx,owner
   xor edx,edx
   call target
   mov afterSP,esp
   mov esp,savedSP
  }
 }
 obs->stackOK=savedSP==afterSP;
}
static void Run(bool original,int t,Observation* out) {
 referenceRun=original;testCase=t;obs=out;traceSize=findCount=0;
 memset(out,0,sizeof *out);
 out->selfOK=out->copiesOK=out->argsOK=out->inputsOK=out->stackOK=out->returnOK=out->globalsOK=1;
 memset(terrain,0x5a,sizeof terrain);memset(game,0,sizeof game);memset(camera,0x2a,sizeof camera);
 memset(room,0,sizeof room);memset(owner,0x3a,sizeof owner);
 moduleName=t==6?"aa":t==7?"tr":"da";
 F(const char*,room,60)=moduleName;F(BYTE*,game,1356)=camera;
 *(BYTE**)Global(0x6cec6c)=game;*(BYTE**)Global(0x6f33b4)=terrain;
 *(int*)Global(0x7208cc)=0;*(int*)Global(0x6f33ac)=0;
 *(BYTE**)Global(0x6f3420)=0;*(const char**)Global(0x6f3424)=0;*(int*)Global(0x7025bc)=17;
 memcpy(Global(0x7025f0),matrix,48);
 float direction[3]={0.25f,-0.5f,1.0f},up[3]={0,0,1};
 memcpy(Global(0x702620),direction,12);memcpy(Global(0x702630),up,12);
 BYTE beforeGame[sizeof game],beforeRoom[sizeof room];Vec3T beforeInput[3];
 memcpy(beforeGame,game,sizeof game);memcpy(beforeRoom,room,sizeof room);memcpy(beforeInput,input,sizeof input);
 __try { Invoke(); } __except(Fault(GetExceptionInformation())) {}
 out->inputsOK=!memcmp(input,beforeInput,sizeof input) && !memcmp(game,beforeGame,sizeof game) && !memcmp(room,beforeRoom,sizeof room)
   && !memcmp(Global(0x7025f0),matrix,48) && !memcmp(Global(0x702620),direction,12) && !memcmp(Global(0x702630),up,12);
 for(int i=0;i<sizeof terrain;i++)out->inputsOK&=terrain[i]==0x5a;
 for(int i=0;i<sizeof camera;i++)out->inputsOK&=camera[i]==0x2a;
 for(int i=0;i<sizeof owner;i++)out->inputsOK&=owner[i]==0x3a;
 if(t>=6 && t<=8)out->globalsOK=*(BYTE**)Global(0x6f3420)==owner && *(const char**)Global(0x6f3424)==moduleName && *(int*)Global(0x7025bc)==0;
 // Restore even on failure: a negative control cannot poison the next case.
 memcpy(input,beforeInput,sizeof input);
}
static bool Valid(const Observation& o) {
 return !o.exception && o.selfOK && o.copiesOK && o.argsOK && o.inputsOK && o.stackOK && o.returnOK && o.globalsOK;
}
// Original patch list layout used by Decals_Add (0x617110).
struct PlantVertex {
    Vec3T point;
    DWORD color;
    float u, v;
};
struct PlantPatch {
    DWORD owner;
    PlantVertex* vertices;
    int count, capacity;
    DWORD tail[4];
};
static_assert(sizeof(PlantVertex) == 24 && sizeof(PlantPatch) == 32, "x86 patch ABI");

static bool ComparePlantGeometry() {
    using EmitPlant = void (__fastcall*)(BYTE*, void*, const Vec3T*, const Vec3T*, float, float, int);
    const EmitPlant emit[] = {reinterpret_cast<EmitPlant>(0x617180),
                              reinterpret_cast<EmitPlant>(Patch_AddPlant)};
    BYTE lighting[552] = {};
    F(float, lighting, 548) = 0.625f;
    const Vec3T sides[] = {{0.6f, -0.8f, 0}, {-0.375f, 0.625f, 0.125f}};
    int cases = 0, failures = 0;
    for (const Vec3T& side : sides) {
        for (int type = 0; type < 9; ++type) {
            for (int parity = 0; parity < 2; ++parity) {
                const Vec3T point = {268.125f + 8.0f * parity, -234.75f, 41.25f};
                for (const int capacity : {0, 2, 8}) {
                    PlantVertex vertices[2][9];
                    memset(vertices, 0x5a, sizeof(vertices));
                    PlantPatch patches[2] = {};
                    for (int which = 0; which < 2; ++which) {
                        referenceRun = which == 0;
                        BYTE*& world = *reinterpret_cast<BYTE**>(Global(0x7025b8));
                        BYTE* previousWorld = world;
                        world = lighting;
                        patches[which].vertices = vertices[which];
                        patches[which].capacity = capacity;
                        emit[which](reinterpret_cast<BYTE*>(&patches[which]), nullptr,
                                    &point, &side, 5.125f, 0.875f, type);
                        world = previousWorld;
                    }
                    const int expected = type < 2 ? 3 : 6;
                    const int written = capacity < expected ? capacity : expected;
                    bool equal = patches[0].count == written && patches[1].count == written &&
                                 !memcmp(vertices[0], vertices[1], sizeof(vertices[0]));
                    if (!equal) {
                        printf("plant mismatch type=%d parity=%d capacity=%d counts=%d,%d\n",
                               type, parity, capacity, patches[0].count, patches[1].count);
                        for (int v = 0; v < written; ++v) {
                            for (int w = 0; w < 6; ++w) {
                                DWORD a, b;
                                memcpy(&a, (const BYTE*)&vertices[0][v] + 4 * w, 4);
                                memcpy(&b, (const BYTE*)&vertices[1][v] + 4 * w, 4);
                                if (a != b) printf(" vertex=%d component=%d original=%08lx actual=%08lx\n", v, w, a, b);
                            }
                        }
                        ++failures;
                    }
                    ++cases;
                }
            }
        }
    }
    printf("plant geometry: cases=%d failures=%d\n", cases, failures);
    return failures == 0;
}
extern "C" void __cdecl Strip_DrawArch(float*,int,float,float,float,float,float);
namespace arch {
struct Vertex { float position[3]; DWORD colour; float uv[2]; };
struct Batch {
    Vertex vertices[9];
    WORD indices[36];
    float points[12];
    int vertexCount, indexCount;
};
static Batch captured;
static long __stdcall Capture(void*, int primitive, int format, Vertex* vertices, int count,
                              WORD* indices, int indexCount, int flags) {
    if (primitive != 4 || format != 322 || count < 0 || count > 9 ||
        indexCount < 0 || indexCount > 36 || flags) ExitProcess(4);
    captured.vertexCount = count;
    captured.indexCount = indexCount;
    memcpy(captured.vertices,vertices,count*sizeof(Vertex));
    memcpy(captured.indices,indices,indexCount*sizeof(WORD));
    return 0;
}
static Batch Draw(bool original,int sections,float sway) {
    referenceRun = original;
    auto& device = *reinterpret_cast<void***>(Global(0x720908));
    auto& initialized = *Global(0x707f90);
    const BYTE oldFlag = initialized;
    auto oldDevice = device;
    void* methods[27] = {};
    methods[26] = reinterpret_cast<void*>(Capture);
    void** table = methods;
    device = reinterpret_cast<void**>(&table);
    initialized |= 1;
    captured = {};
    constexpr float spacing = 11.5f/3;
    for (int i=0; i<4; ++i) {
        captured.points[3*i] = -2616.4987793f + i*sway;
        captured.points[3*i+1] = 4267.3652344f - i*sway;
        captured.points[3*i+2] = 19.532629f + i*spacing;
    }
    const auto emit = original ? reinterpret_cast<decltype(&Strip_DrawArch)>(0x5c6830) : Strip_DrawArch;
    emit(captured.points,sections,spacing*.1f,spacing,sway,-sway,.5f);
    device = oldDevice;
    initialized = oldFlag;
    return captured;
}
static bool Check() {
    int failures = 0;
    for (int sections : {0,1,2}) for (float sway : {0.0f,.125f}) {
        Batch expected = Draw(true,sections,sway);
        Batch got = Draw(false,sections,sway);
        bool match = expected.vertexCount == 3*(sections+1) && expected.indexCount == 18*sections &&
                     memcmp(&expected,&got,sizeof got) == 0;
        failures += !match;
    }
    printf("strip geometry: cases=6 failures=%d\n",failures);
    return failures == 0;
}
}
int main(int argc,char**argv) {
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 if(argc!=2 || !MapOriginal(argv[1]))return 2;
 _controlfp(_PC_53,_MCW_PC);
 const bool geometryOK = ComparePlantGeometry();
 const bool stripOK = arch::Check();
 struct Hook { DWORD original;void* production;void* stub; } hooks[]={
  {0x617180,(void*)Patch_AddPlant,(void*)Plant},
  {0x614960,(void*)TerrainView_Move,(void*)Move},{0x614f70,(void*)TerrainView_Place,(void*)Place},
  {0x5dbcc0,(void*)Camera_Get,(void*)Camera},{0x615670,(void*)Terrain615670_Update,(void*)Update},
  {0x5ead00,(void*)Room_Each,(void*)Each},{0x5ef390,(void*)Room_FindObject,(void*)Find},
  {0x5ef3d0,(void*)Objects_Find,(void*)Object},{0x616770,(void*)Terrain616770_SetCorners,(void*)Corners},
  {0x42aba0,(void*)Ai_OnModuleLoadX,(void*)AI}
 };
 for(int i=0;i<sizeof hooks/sizeof *hooks;i++){Redirect((void*)hooks[i].original,hooks[i].stub);Redirect(hooks[i].production,hooks[i].stub);}
 const char* names[]={"Move","Place","Move-alias","Place-alias","Follow-0","Follow-123","module-aa","module-tr","module-da","plant","plant-alias"};
 const char* traces[]={"M","P","M","P","CMU","CMU","EFOOOSCPA","EFOOOSCPA","EFA","T","T"};
 const float cameraVectors[]={125.5f,-42.25f,16,5.25f,6,6.75f,0,0,1};
 int failures=(geometryOK && stripOK)?0:1;
 for(int t=0;t<11;t++) {
  Observation want,got;Run(true,t,&want);Run(false,t,&got);
  float expected[9]={0};
  if(t<4)for(int i=0;i<3;i++)memcpy(expected+3*i,&input[t>=2?0:i],12);
  else if(t<8)memcpy(expected,cameraVectors,sizeof expected);
  else if(t>=9){memcpy(expected,&input[0],12);memcpy(expected+3,&input[t==9?1:0],12);}
  bool reference=Valid(want) && !strcmp(want.trace,traces[t]) && !memcmp(want.vectors,expected,sizeof expected);
  bool same=!strcmp(got.trace,want.trace) && !memcmp(got.vectors,want.vectors,sizeof want.vectors);
  bool ok=reference && Valid(got) && same;failures+=!ok;
  printf("case %d %s reference=%d this=%d copies=%d stack=%d inputs=%d args=%d return=%d globals=%d same=%d exception=%08lX fault=%08lX trace=%s %s\n",
    t,names[t],reference,got.selfOK,got.copiesOK,got.stackOK,got.inputsOK,got.argsOK,got.returnOK,got.globalsOK,same,got.exception,got.fault,got.trace,ok?"PASS":"FAIL");fflush(stdout);
  if(want.exception || got.exception)break;
 }
 return failures?1:0;
}
