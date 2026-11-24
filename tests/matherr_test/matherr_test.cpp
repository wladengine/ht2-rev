// Production CRT startup and original game math-error policy, independently executed.
#include <windows.h>
#include <stdio.h>
#include <math.h>
#include <float.h>
#include <string.h>
#include <stdint.h>
static BYTE* reserved;
extern "C" void __cdecl mainCRTStartup();
extern "C" void __cdecl TrafficEntry(){reserved=(BYTE*)VirtualAlloc((void*)0x400000,0x3bd000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);mainCRTStartup();}
static bool MapOriginal(const char* path){
 if(!reserved){puts("original address occupied: cannot map independent reference");return false;}
 FILE*f=fopen(path,"rb");if(!f)return false;
 fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
 BYTE*raw=(BYTE*)VirtualAlloc(0,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
 if(!raw||fread(raw,1,size,f)!=(size_t)size){fclose(f);return false;}fclose(f);
 IMAGE_NT_HEADERS32*nt=(IMAGE_NT_HEADERS32*)(raw+((IMAGE_DOS_HEADER*)raw)->e_lfanew);
 if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->OptionalHeader.ImageBase!=0x400000||nt->OptionalHeader.SizeOfImage!=0x3bd000)return false;
 memcpy(reserved,raw,nt->OptionalHeader.SizeOfHeaders);
 IMAGE_SECTION_HEADER*s=IMAGE_FIRST_SECTION(nt);
 for(int i=0;i<nt->FileHeader.NumberOfSections;i++)memcpy(reserved+s[i].VirtualAddress,raw+s[i].PointerToRawData,s[i].SizeOfRawData);
 VirtualFree(raw,0,MEM_RELEASE);FlushInstructionCache(GetCurrentProcess(),reserved,0x3bd000);return true;
}
static BYTE threadData[0x74];
static void* __cdecl ReferenceThreadData(){return threadData;}
static void Redirect(unsigned address,void*fn){BYTE*p=(BYTE*)address;p[0]=0xe9;*(int*)(p+1)=(BYTE*)fn-p-5;FlushInstructionCache(GetCurrentProcess(),p,5);}
extern double x87_acos(double);
static double OriginalAcos(double x){
 double y;void*p=(void*)0x61dd70;
 __asm {fld x
 call [p]
 fstp y}
 return y;
}
static uint64_t Bits(double x){uint64_t b;memcpy(&b,&x,8);return b;}
static double Value(uint64_t b){double x;memcpy(&x,&b,8);return x;}
static bool Nan(double x){return (Bits(x)&0x7fffffffffffffffull)>0x7ff0000000000000ull;}
struct Case {const char* name;uint64_t input;int recovery;bool integration;};
static const Case cases[]={
 {"neg-inf",0xfff0000000000000ull,0,true},{"neg-two",0xc000000000000000ull,0,true},
 {"neg-boundary",0xbff028f5c28f5c29ull,0,true},{"neg-inside",0xbff028f5c28f5c28ull,-1,true},
 {"seed383",0xbff0165fe0000000ull,-1,true},{"neg-unit-outside",0xbff0000000000001ull,-1,true},
 {"neg-one",0xbff0000000000000ull,0,true},{"neg-half",0xbfe0000000000000ull,0,false},
 {"neg-zero",0x8000000000000000ull,0,true},{"zero",0,0,true},
 {"half",0x3fe0000000000000ull,0,false},{"one",0x3ff0000000000000ull,0,true},
 {"unit-outside",0x3ff0000000000001ull,1,true},{"positive-sample",0x3ff0165fe0000000ull,1,true},
 {"inside",0x3ff028f5c28f5c28ull,1,true},{"boundary",0x3ff028f5c28f5c29ull,0,true},
 {"two",0x4000000000000000ull,0,true},{"inf",0x7ff0000000000000ull,0,true},
 {"qnan",0x7ff8000000000000ull,0,true}
};
int main(int argc,char**argv){
 if(argc!=2||!MapOriginal(argv[1]))return 2;
 Redirect(0x622ba3,(void*)ReferenceThreadData);
 typedef int(__cdecl* Handler)(_exception*);
 Handler original=(Handler)0x4e4671;
 int failures=0,count=0;const char*names[]={"acos","sqrt","cos"};
 unsigned initial=_controlfp(0,0),cw;
 for(int mode=0;mode<2;mode++){
  _controlfp_s(&cw,mode?_PC_24:_PC_53,_MCW_PC);
  for(unsigned k=0;k<sizeof(cases)/sizeof(cases[0]);k++){
   const Case&c=cases[k];double input=Value(c.input);
   for(int name=0;name<3;name++)for(int type=1;type<=2;type++){
    struct Guard {uint32_t pre[2];_exception error;uint32_t post[2];};
    Guard a={{0x12345678,0x23456789},{type,(char*)names[name],input,123.25,77.75},{0x34567890,0x45678901}},b=a,expected=a;
    bool handled=type==1&&(name==1||(name==0&&c.recovery));
    if(handled)expected.error.retval=Value(name==0&&c.recovery<0?0x400921fb4d12d84aull:0);
    int ar=original(&a.error),br=_matherr(&b.error);
    bool reference=ar==(int)handled&&!memcmp(&a,&expected,sizeof a);
    bool equal=ar==br&&!memcmp(&a,&b,sizeof a);
    bool ok=reference&&equal;count++;failures+=!ok;
    printf("handler %d/%s/%s/%d reference=%d equal=%d %s\n",mode,c.name,names[name],type,reference,equal,ok?"PASS":"FAIL");
   }
   if(c.integration){
    memset(threadData,0,sizeof threadData);_clearfp();double a=OriginalAcos(input);unsigned ac=_controlfp(0,0);
    _clearfp();double b=x87_acos(input);unsigned bc=_controlfp(0,0);
    bool reference=true;
    if(c.recovery)reference=Bits(a)==(c.recovery<0?0x400921fb4d12d84aull:0);
    else if((c.input&0x7fffffffffffffffull)>0x3ff0000000000000ull)reference=Nan(a);
    bool equal=((Nan(a)&&Nan(b))||Bits(a)==Bits(b))&&ac==bc;
    bool ok=reference&&equal;count++;failures+=!ok;
    printf("acos %d/%s reference=%d equal=%d original=%016llx candidate=%016llx %s\n",mode,c.name,reference,equal,Bits(a),Bits(b),ok?"PASS":"FAIL");
   }
  }
 }
 _controlfp_s(&cw,initial,_MCW_PC);_clearfp();
 printf("%d/%d failures\n",failures,count);return failures?1:0;
}
