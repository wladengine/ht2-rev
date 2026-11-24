#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
extern "C" unsigned char KingBss[];
extern "C" unsigned char KingData[];
extern "C" unsigned char KingRdata[];
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
            char module[MAX_PATH]={0};
            if(info.AllocationBase && GetModuleFileNameA((HMODULE)info.AllocationBase,module,sizeof module))
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
    // Execute only the address builder; no original entry point.
    FlushInstructionCache(GetCurrentProcess(),image,nt->OptionalHeader.SizeOfImage);
    VirtualFree(raw,0,MEM_RELEASE);
    return image;
}

extern "C" HRESULT __cdecl Gate_BuildAddress(BYTE*,const BYTE*,void**,DWORD*,char);
struct Elem {BYTE guid[16];DWORD size;void* data;};
struct Captured {BYTE guid[16];DWORD size;BYTE data[200];};
struct Observation {
    HRESULT result;int enumCalls,sizeCalls,writeCalls,argsOK,callbackCalls;
    int count[2];Captured elements[2][11];char trace[24];
    DWORD outSize;int outSet,bufferOK;
};
static Observation obs;
static int caseId,traceSize,abiErrors;
static void* table[15];static void** lobby=table;
static BYTE service[16]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
static BYTE buffer[96],unknown[16]={99};
static void* const handle=(void*)0x135700;
static unsigned addresses[4]={0x6552d0,0x6552c0,0x6552b0,0x6552a0};
static BYTE* Global(unsigned a){return a>=0x68a820?KingBss+a-0x68a820:a>=0x66c000?KingData+a-0x66c000:KingRdata+a-0x64b000;}
static void String(unsigned a,const char*s){strcpy((char*)a,s);strcpy((char*)Global(a),s);}
static void Event(char c){if(traceSize>=23)ExitProcess(3);obs.trace[traceSize++]=c;}
typedef int(__stdcall* Callback)(const DWORD*,DWORD*,int);
static HRESULT __stdcall EnumImpl(void* self,Callback callback,const BYTE* sp,DWORD* context,DWORD flags){
    Event('E');obs.enumCalls++;obs.argsOK&=self==&lobby && sp==service && flags==0;
    for(int i=0;i<41;i++)obs.argsOK&=context[i]==0;
    if(caseId==0)return E_FAIL;
    int count=caseId==6?0:caseId==13?1:caseId==14?12:4;
    for(int i=0;i<count;i++){
        unsigned a=addresses[caseId==15?3-i:i%4];
        const DWORD* guid=caseId==13?(const DWORD*)unknown:caseId==14?(const DWORD*)addresses[0]:(const DWORD*)a;
        obs.argsOK&=callback(guid,context,0x37)==1;obs.callbackCalls++;
    }
    obs.argsOK&=context[0]==(DWORD)(count>10?10:count);
    return S_OK;
}
static HRESULT __stdcall CreateImpl(void* self,const Elem* e,DWORD count,void* dest,DWORD* size){
    int phase=dest?1:0;Event(dest?'W':'S');if(dest)obs.writeCalls++;else obs.sizeCalls++;
    obs.argsOK&=self==&lobby && count<=11 && size!=0;
    if(count>11)ExitProcess(3);obs.count[phase]=count;
    for(DWORD i=0;i<count;i++){
        Captured& c=obs.elements[phase][i];memcpy(c.guid,e[i].guid,16);c.size=e[i].size;
        if(e[i].size>sizeof c.data || !e[i].data)ExitProcess(3);memcpy(c.data,e[i].data,e[i].size);
    }
    if(!dest){*size=sizeof buffer;return caseId==1?E_INVALIDARG:caseId==2?S_OK:(HRESULT)0x8877001e;}
    obs.argsOK&=dest==buffer&&*size==sizeof buffer;
    memset(dest,0x5a,sizeof buffer);*size=72;
    return caseId==5?E_FAIL:S_FALSE; // any nonnegative result must become S_OK
}
// A wrong old thiscall omits stack self. Record that fact, then adapt only the
// checking endpoint and pop its actual 4 arguments. This lets negative controls
// report deterministically without depending on a crash or corrupting the caller.
__declspec(naked) static HRESULT EnumEntry(){__asm{
    mov eax,offset lobby
    cmp [esp+4],eax
    jne wrong
    jmp EnumImpl
wrong:
    inc abiErrors
    push ebp
    mov ebp,esp
    push [ebp+20]
    push [ebp+16]
    push [ebp+12]
    push [ebp+8]
    push eax
    call EnumImpl
    pop ebp
    ret 16
}}
__declspec(naked) static HRESULT CreateEntry(){__asm{
    mov eax,offset lobby
    cmp [esp+4],eax
    jne wrong
    jmp CreateImpl
wrong:
    inc abiErrors
    push ebp
    mov ebp,esp
    push [ebp+20]
    push [ebp+16]
    push [ebp+12]
    push [ebp+8]
    push eax
    call CreateImpl
    pop ebp
    ret 16
}}
static HGLOBAL WINAPI Alloc(UINT flags,SIZE_T size){Event('A');obs.argsOK&=flags==66&&size==sizeof buffer;return caseId==3?0:(HGLOBAL)handle;}
static void* WINAPI Lock(HGLOBAL h){Event('L');obs.argsOK&=h==(caseId==3?0:handle);return caseId==3||caseId==4?0:buffer;}
static HGLOBAL WINAPI Handle(const void*p){Event('H');obs.argsOK&=p==buffer;return (HGLOBAL)handle;}
static BOOL WINAPI Unlock(HGLOBAL h){Event('U');obs.argsOK&=h==handle;return FALSE;}
static HGLOBAL WINAPI Free(HGLOBAL h){Event('F');obs.argsOK&=h==handle;return 0;}
static void Redirect(void* address,void* fn){DWORD old;BYTE*p=(BYTE*)address;VirtualProtect(p,5,PAGE_EXECUTE_READWRITE,&old);p[0]=0xe9;F(int,p,1)=(BYTE*)fn-p-5;FlushInstructionCache(GetCurrentProcess(),p,5);VirtualProtect(p,5,old,&old);}
static void Import(const char*name,void*fn,unsigned originalSlot){
    *(void**)originalSlot=fn;
    BYTE* base=(BYTE*)GetModuleHandle(0);IMAGE_NT_HEADERS* nt=(IMAGE_NT_HEADERS*)(base+((IMAGE_DOS_HEADER*)base)->e_lfanew);
    IMAGE_IMPORT_DESCRIPTOR* d=(IMAGE_IMPORT_DESCRIPTOR*)(base+nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for(;d->Name;d++)for(IMAGE_THUNK_DATA32 *n=(IMAGE_THUNK_DATA32*)(base+d->OriginalFirstThunk),*v=(IMAGE_THUNK_DATA32*)(base+d->FirstThunk);n->u1.AddressOfData;n++,v++){
        if(IMAGE_SNAP_BY_ORDINAL32(n->u1.Ordinal))continue;
        if(!strcmp((char*)((IMAGE_IMPORT_BY_NAME*)(base+n->u1.AddressOfData))->Name,name)){
            DWORD old;VirtualProtect(&v->u1.Function,4,PAGE_READWRITE,&old);v->u1.Function=(DWORD)fn;VirtualProtect(&v->u1.Function,4,old,&old);return;
        }
    }
    printf("missing import %s\n",name);ExitProcess(2);
}
static Observation Run(bool original,int t){
    memset(&obs,0,sizeof obs);memset(buffer,0xcc,sizeof buffer);obs.argsOK=1;caseId=t;traceSize=abiErrors=0;
    String(0x723df8,t==8||t==9?"":"Driver");String(0x723ec0,"127.0.0.1");String(0x723f88,"second");
    String(0x724050,t==10?"0":t==11?"65536":t==12?"-1":t==16?"65537":"2300");
    void* out=(void*)0x765400;DWORD size=0x12345678;
    obs.result=original?((HRESULT(__cdecl*)(BYTE*,const BYTE*,void**,DWORD*,char))0x601890)((BYTE*)&lobby,service,&out,&size,t!=9):Gate_BuildAddress((BYTE*)&lobby,service,&out,&size,t!=9);
    obs.outSet=out==buffer?1:out==(void*)0x765400?0:-1;obs.outSize=size;
    obs.bufferOK=1;for(unsigned i=0;i<sizeof buffer;i++)obs.bufferOK&=buffer[i]==(t>=5?0x5a:0xcc);
    return obs;
}
static bool Reference(const Observation&o,int t){
    const char* trace=t==0?"E":t<=2?"ES":t<=4?"ESAL":t==5?"ESALWHUHF":"ESALW";
    HRESULT hr=t==0||t==5?E_FAIL:t==1?E_INVALIDARG:t==3||t==4?E_OUTOFMEMORY:S_OK;
    bool ok=o.argsOK&&o.bufferOK&&o.result==hr&&!strcmp(o.trace,trace)&&o.outSet==(t>=6)&&o.outSize==(t>=6?72:0x12345678);
    int num=t==6||t==13?1:t==14?11:t==9||t==10||t==11?4:5;
    if(t==0)return ok&&o.enumCalls==1&&!o.sizeCalls&&!o.writeCalls;
    ok=ok&&o.enumCalls==1&&o.sizeCalls==1&&o.writeCalls==(t>=5)&&o.count[0]==num;
    for(int phase=0;phase<(t>=5?2:1);phase++){
        ok=ok&&o.count[phase]==num;
        for(int i=0;i<num;i++){
            const Captured&c=o.elements[phase][i];
            unsigned type=i==0?0x6552e0:t==14?addresses[0]:t==15?addresses[4-i]:t==9?addresses[i]:addresses[i-1];
            const void* data=service;DWORD size=16;WORD port=t==12?65535:t==16?1:2300;
            if(type==addresses[0]){data=t==8?(void*)0x6842c8:"Driver";size=(DWORD)strlen((const char*)data)+1;}
            else if(type==addresses[1]){data="127.0.0.1";size=10;}
            else if(type==addresses[2]){data="second";size=7;}
            else if(type==addresses[3]){data=&port;size=2;}
            ok=ok&&!memcmp(c.guid,(void*)type,16)&&c.size==size&&!memcmp(c.data,data,size);
        }
    }
    return ok;
}
int main(int argc,char**argv){
    if(argc!=2||!MapOriginal(argv[1]))return 2;
    for(unsigned a=0x6552a0;a<=0x6552e0;a+=16)if(memcmp((void*)a,Global(a),16))return 2;
    *(void**)0x64b1d8=(void*)lstrlenA;Redirect((void*)0x61fc9d,(void*)atoi);
    Import("GlobalAlloc",(void*)Alloc,0x64b200);Import("GlobalLock",(void*)Lock,0x64b1ec);Import("GlobalHandle",(void*)Handle,0x64b1e4);Import("GlobalUnlock",(void*)Unlock,0x64b1fc);Import("GlobalFree",(void*)Free,0x64b1f8);
    table[6]=(void*)EnumEntry;table[14]=(void*)CreateEntry;
    int failures=0;
    for(int t=0;t<17;t++){
        Observation want=Run(true,t);bool reference=Reference(want,t)&&abiErrors==0;
        Observation got=Run(false,t);bool equal=!memcmp(&want,&got,sizeof want),abi=abiErrors==0;
        bool ok=reference&&equal&&abi;failures+=!ok;
        printf("address %d reference=%d equal=%d abi=%d %s\n",t,reference,equal,abi,ok?"PASS":"FAIL");
        if(!reference||!equal)printf("detail trace=%s/%s hr=%08lx/%08lx elements=%d/%d args=%d/%d\n",want.trace,got.trace,want.result,got.result,want.count[0],got.count[0],want.argsOK,got.argsOK);
    }
    return failures?1:0;
}
