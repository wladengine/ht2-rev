// Calls the real production method and independently mapped original code.
// Does not run the game's entry point or load a world/window.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <float.h>
extern "C" unsigned char KingBss[];
extern "C" float* __fastcall Body_SettleOnPlane(BYTE*, void*, float*, const float*, const float*, const float*, const float*);
typedef float* (__thiscall *Original)(BYTE*, float*, const float*, const float*, const float*, const float*);
static float identity[12]={1,0,0,0,1,0,0,0,1,0,0,0};
static BYTE body[5400], referenceBody[5400];
static void I(int off,int v) { memcpy(body+off,&v,4); }
static void V(int off,float v) { memcpy(body+off,&v,4); }
static BYTE* MapOriginal(const char* path) {
    // Reserve before allocating the raw file: an arbitrary allocation can itself
    // occupy the preferred original image address. Runner pins the input SHA256.
    BYTE* image=(BYTE*)VirtualAlloc((void*)0x400000,0x3bd000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
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
static int Check(Original original,int scenario) {
    memset(body,0,sizeof body);
    float m[12];memcpy(m,identity,48);
    m[9]=-600; m[10]=-2850.5f; m[11]=106.2f;
    float normal[3]={0,0,1}, plane[3]={-600,-2850.5f,106};
    I(3572,4);
    const float points[12]={-1,-1,-1, 1,-1,-1, 1,1,-1, -1,1,-1};
    memcpy(body+3592,points,sizeof points);
    if(scenario>=2) { I(5132,1);V(3988+8,-1);V(3988+12,-1); }
    if(scenario==1) { plane[2]=100; } // no lift
    if(scenario==3) { // tilt branch with penetrating points
        const float c=0.9961947f,s=0.08715574f;
        m[4]=c;m[5]=s;m[7]=-s;m[8]=c;
    }
    if(scenario==4) { plane[2]=100; } // tilted branch exits without iterations
    float inverse[12];memcpy(inverse,identity,48); // controlled independent input
    memcpy(referenceBody,body,sizeof body);
    struct Guarded { DWORD left;float m[12];DWORD right; } a={0x12345678,{},0x87654321},b=a;
    float* expected=original(referenceBody,b.m,m,inverse,normal,plane);
    float* actual=Body_SettleOnPlane(body,0,a.m,m,inverse,normal,plane);
    int bad=expected!=b.m || actual!=a.m || a.left!=b.left || a.right!=b.right
        || b.left!=0x12345678 || b.right!=0x87654321;
    float maxError=0;
    for(int i=0;i<12;i++) {
        float error=(float)fabs(a.m[i]-b.m[i]);if(error>maxError)maxError=error;
        if(!_finite(a.m[i]) || !_finite(b.m[i]) || error>0.0005f)bad=1;
    }
    printf("case %d original_returns_out=%d production_returns_out=%d returned_offset_bytes=%d max_matrix_error=%.9g %s\n",
        scenario,expected==b.m,actual==a.m,(int)((BYTE*)actual-(BYTE*)a.m),maxError,bad?"FAIL":"PASS");
    return bad;
}
int main(int argc,char** argv) {
    if(argc!=2 || !MapOriginal(argv[1])) { fprintf(stderr,"Cannot map original at preferred base: %lu\n",GetLastError());return 2; }
    memcpy(KingBss+0x7025f0-0x68a820,identity,48);
    int bad=0;
    for(int i=0;i<5;i++)bad|=Check((Original)0x58d770,i);
    return bad;
}
