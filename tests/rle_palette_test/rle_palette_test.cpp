#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
extern "C" unsigned char KingBss[];
extern "C" void __fastcall Image_RleTo16(BYTE*,void*,const BYTE*);
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



// Only allocation is redirected into this process CRT. Conversion and pixel packing
// execute from the independently supplied PE; no game startup or imports run.
static void* __cdecl Allocate(unsigned n) {return ::operator new(n);}
static void __cdecl Release(void* p) {::operator delete(p);}
static void Redirect(unsigned address,void* fn) {
 BYTE* p=(BYTE*)address;p[0]=0xe9;F(int,p,1)=(BYTE*)fn-p-5;
 FlushInstructionCache(GetCurrentProcess(),p,5);
}
static void SetGlobal(unsigned addr,int value) {
 *(int*)addr=value;*(int*)(KingBss+addr-0x68a820)=value;
}
struct Result {BYTE bytes[32];WORD depth;DWORD flags;bool pointerKept;};
static Result Run(bool original,int fmt,int mode) {
 // Poison the image object's unrelated trailing memory: the former implementation
 // used it as a palette. Actual palette is a distinct allocation, all 256 entries.
 BYTE object[1024];memset(object,0xa5,sizeof object);
 BYTE palette[816]={0};
 for(int i=0;i<256;i++){palette[24+i*3]=(BYTE)(i*37);palette[25+i*3]=(BYTE)(i*13);palette[26+i*3]=(BYTE)(255-i);}
 BYTE encoded[32]={3,0,1,255,0x82,2,17,128,0x81,4,63,64,127,254,0x82}; // two rows of 7 pixels
 BYTE* input=(BYTE*)::operator new(32);memcpy(input,encoded,32);
 F(WORD,object,4)=7;F(WORD,object,6)=2;F(WORD,object,8)=mode==4?2:1;
 F(int,object,12)=32;F(BYTE*,object,16)=mode==5?0:input;
 F(DWORD,object,28)=mode==1?4:mode==2?32:mode==3?128:0;
 SetGlobal(0x702e94,fmt);SetGlobal(0x720894,mode==6?1:2);
 if(original)((void(__thiscall*)(BYTE*,const BYTE*))0x5bbf00)(object,palette);
 else Image_RleTo16(object,0,palette);
 Result r={};BYTE* output=F(BYTE*,object,16);
 r.pointerKept=output==(mode==5?0:input);r.depth=F(WORD,object,8);r.flags=F(DWORD,object,28);
 // Converted meaningful prefix only; allocation tail is intentionally unspecified.
 if(output)memcpy(r.bytes,output,mode<2?24:32);
 if(output)::operator delete(output);if(mode==5)::operator delete(input);
 return r;
}
int main(int argc,char** argv) {
 if(argc!=2||!MapOriginal(argv[1]))return 2;
 Redirect(0x632643,(void*)Allocate);Redirect(0x63266c,(void*)Release);
 int failures=0;
 for(int fmt=2;fmt<=3;fmt++)for(int mode=0;mode<7;mode++) {
  Result want=Run(true,fmt,mode),got=Run(false,fmt,mode);
  bool converted=mode<2;
  bool ref=want.depth==(converted||mode==4?2:1)&&want.pointerKept==(mode!=0);
  // Header + first pixel index 0 = RGB(0,0,255); RGB565 and RGB555 both 0x001f.
  if(converted)ref=ref&&want.bytes[0]==3&&want.bytes[1]==31&&want.bytes[2]==0&&(want.flags&32);
  bool colors=!memcmp(want.bytes,got.bytes,converted?24:32);
  bool state=want.depth==got.depth&&want.flags==got.flags&&want.pointerKept==got.pointerKept;
  bool ok=ref&&colors&&state;failures+=!ok;
  printf("format=%d mode=%d reference=%d pixels=%d state=%d %s\n",fmt,mode,ref,colors,state,ok?"PASS":"FAIL");
 }
 printf("%d/14 failures\n",failures);return failures?1:0;
}
