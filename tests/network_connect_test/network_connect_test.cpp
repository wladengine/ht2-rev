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
    // Execute only the connection path; no original entry point.
    FlushInstructionCache(GetCurrentProcess(),image,nt->OptionalHeader.SizeOfImage);
    VirtualFree(raw,0,MEM_RELEASE);
    return image;
}


extern "C" int __cdecl Dp_Connect(DWORD,DWORD,DWORD,DWORD,int,int,int);
extern "C" int __cdecl Providers_Call601c70(unsigned,int,int,int);
extern "C" void __cdecl Providers_Rebuild();
extern "C" void __cdecl Dp_Create();
extern "C" HRESULT __cdecl Gate_BuildAddress(BYTE*,const BYTE*,void**,DWORD*,char);
extern "C" const char* __cdecl DirectPlay_ErrorText(DWORD);
extern "C" void __cdecl Ai_Report(int,int,int,int,int,int,int,int);
static BYTE* Global(unsigned a){return a>=0x68a820?KingBss+a-0x68a820:a>=0x66c000?KingData+a-0x66c000:KingRdata+a-0x64b000;}
static BYTE provider[64],address[64];
static void* dpTable[40];static void** dp=dpTable;
static void* lobbyTable[3];static void** lobby=lobbyTable;
struct Observation {int result,argsOK,build,init,open,enumerate,player,close,release,free,report;DWORD guid[4],desc[20],playerId,connected;int hosting;};
static Observation obs;static bool original;static int test;
static DWORD guid[4]={0x11223344,0x55667788,0x99aabbcc,0xddeeff00};
static int host,maximum;static char name[]="local test";
static BYTE* G(unsigned a){return original?(BYTE*)a:Global(a);}
static void __cdecl Rebuild(){F(BYTE*,G(0x723d60),0)=provider;F(BYTE*,G(0x723d64),0)=provider+(test==0?0:64);}
static void __cdecl Create(){F(void*,G(0x723ddc),0)=&dp;}
static HRESULT WINAPI CoCreate(REFCLSID cls,LPUNKNOWN outer,DWORD context,REFIID iid,void**p){
    obs.argsOK&=!memcmp(&cls,(void*)0x6552f0,16)&&!memcmp(&iid,(void*)0x655300,16)&&!outer&&context==1;
    *p=&lobby;return S_OK;
}
static HRESULT __cdecl Build(BYTE*l,const BYTE*sp,void**out,DWORD*size,char h){
    obs.build++;obs.argsOK&=l==(BYTE*)&lobby&&h==(host!=0);memcpy(obs.guid,sp,16);*out=address;*size=64;return S_OK;
}
static HRESULT __stdcall ReleaseLobby(void*self){obs.argsOK&=self==&lobby;return 0;}
static HRESULT __stdcall Initialize(void*self,void*addr,DWORD flags){
    obs.init++;obs.argsOK&=self==&dp&&addr==address&&!flags;
    if((test==5||test==6)&&obs.init==1)return (HRESULT)0x88770005;
    return test==4||test==6?E_FAIL:S_OK;
}
static HRESULT __stdcall Open(void*self,const DWORD*d,DWORD flags){
    obs.open++;obs.argsOK&=self==&dp&&flags==130;memcpy(obs.desc,d,80);obs.desc[12]=d[12]==(DWORD)name?1:0;
    return test==7?E_FAIL:test==8&&obs.open==1?(HRESULT)0x8877015e:S_OK;
}
static HRESULT __stdcall Enum(void*self,const DWORD*d,DWORD timeout,void*callback,void*context,DWORD flags){
    obs.enumerate++;obs.argsOK&=self==&dp&&!timeout&&!context&&flags==129&&callback;memcpy(obs.desc,d,80);
    F(int,G(0x723df4),0)=test!=3;
    return test==9?E_FAIL:test==10&&obs.enumerate==1?(HRESULT)0x8877015e:S_OK;
}
static HRESULT __stdcall Player(void*self,DWORD*out,void*n,void*event,void*data,DWORD bytes,DWORD flags){
    obs.player++;obs.argsOK&=self==&dp&&!n&&!event&&!data&&!bytes&&!flags&&out==(DWORD*)G(0x723de0);*out=0x123456;
    return test==11?E_FAIL:S_OK;
}
static HRESULT __stdcall Close(void*self){obs.close++;obs.argsOK&=self==&dp;return S_OK;}
static HRESULT __stdcall ReleaseDP(void*self){obs.release++;obs.argsOK&=self==&dp;return S_OK;}
static HGLOBAL WINAPI Handle(const void*p){obs.argsOK&=p==address;return (HGLOBAL)address;}
static BOOL WINAPI Unlock(HGLOBAL h){obs.argsOK&=h==(HGLOBAL)address;return FALSE;}
static HGLOBAL WINAPI Free(HGLOBAL h){obs.free++;obs.argsOK&=h==(HGLOBAL)address;return 0;}
static const char* __cdecl ErrorText(DWORD hr){obs.argsOK&=hr==static_cast<DWORD>(E_FAIL);return reinterpret_cast<const char*>(0x1234);}
static void __cdecl Report(int a,int level,int file,int id,int line,int message,int error,int tail){obs.report++;obs.argsOK&=a==1&&level==100&&line==1038&&error==0x1234;}
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

static Observation Run(bool orig,int t){
    original=orig;test=t;memset(&obs,0,sizeof obs);obs.argsOK=1;
    memset(provider,0,sizeof provider);memcpy(provider,guid,16);memcpy(provider+32,guid,16);
    if(t>=13)F(DWORD,provider,0)^=0xabcdef01; // wrapper must select the second provider
    host=t==2||t==3||t==9||t==10||t==14?0:t==12?-7:1;maximum=t==12?3:8;
    F(void*,G(0x723ddc),0)=0;F(DWORD,G(0x723de0),0)=0;F(DWORD,G(0x723d88),0)=0;
    Rebuild();
    typedef int(__cdecl*Fn)(DWORD,DWORD,DWORD,DWORD,int,int,int);
    typedef int(__cdecl*Wrapper)(unsigned,int,int,int);
    if(t>=13)obs.result=orig?((Wrapper)0x601c10)(t==15?2:1,host,maximum,(int)name):Providers_Call601c70(t==15?2:1,host,maximum,(int)name);
    else obs.result=orig?((Fn)0x601c70)(guid[0],guid[1],guid[2],guid[3],host,maximum,(int)name):Dp_Connect(guid[0],guid[1],guid[2],guid[3],host,maximum,(int)name);
    obs.playerId=F(DWORD,G(0x723de0),0);obs.connected=F(DWORD,G(0x723d88),0);obs.hosting=F(BYTE,G(0x723dd8),0);
    return obs;
}
static bool Reference(const Observation&o,int t){
    int result=t==0||t==4||t==6||t==7||t==9?3:t==3?1:t==11||t==15?-1:0;
    if(!o.argsOK||o.result!=result)return false;
    if(t==0||t==15)return o.build==0&&o.init==0;
    if(o.build!=1||memcmp(o.guid,guid,16)||o.free!=1||o.hosting!=(host!=0))return false;
    if(o.init!=((t==5||t==6)?2:1))return false;
    int dropped=result!=0;int retry=t==5||t==6;
    if(o.close!=dropped+retry||o.release!=dropped+retry)return false;
    if(t==4||t==6)return !o.open&&!o.enumerate&&!o.player;
    if(o.desc[0]!=80||memcmp(o.desc+2,(void*)0x683ca0,16)||memcmp(o.desc+6,(void*)0x683ca0,16))return false;
    if(host){if(o.open!=(t==8?2:1)||o.enumerate||o.desc[1]!=8260||o.desc[10]!=(DWORD)maximum||o.desc[12]!=1)return false;}
    else if(o.enumerate!=(t==10?2:1)||o.open||o.desc[1]||o.desc[10]||o.desc[12])return false;
    bool reachesPlayer=t!=3&&t!=7&&t!=9;
    return o.player==int(reachesPlayer)&&o.report==int(t==11)&&o.connected==(result==0?0x123456u:0u);
}
extern "C" int __fastcall Net_SendJoinReply(BYTE*, void*, const char*, const char*);
extern "C" BYTE* __fastcall DpMessage_Construct(BYTE*, void*);
extern "C" void __fastcall MsgBuf_Destroy(BYTE*, int);
extern "C" void __fastcall Msg_Write120(BYTE*, int, int, const BYTE*, int);
extern "C" int __cdecl Dp_SendTo(int, int, int);

namespace {
struct JoinTrace {
    int registrations, writes, sends;
    bool valid;
    const char* registeredName;
    BYTE assigned;
};
JoinTrace joinTrace;
BYTE fakeMessage[16];
constexpr char kHost[] = "host";

BYTE __cdecl RegisterDriver(const char* driver, int id) {
    ++joinTrace.registrations;
    joinTrace.registeredName = driver;
    joinTrace.valid &= id == 7;
    return strcmp(driver, kHost) == 0 ? 255 : 1;
}
BYTE* __fastcall InitJoinMessage(BYTE* msg, void*) { return msg; }
void __fastcall EndJoinMessage(BYTE*, int) {}
void __fastcall RecordReply(BYTE* msg, int, int id, const BYTE* data, int length) {
    ++joinTrace.writes;
    joinTrace.valid &= id == 901 && length == sizeof(kHost) + 2 && data[0] == 7 &&
                       memcmp(data + 2, kHost, sizeof(kHost)) == 0;
    joinTrace.assigned = data[1];
    F(BYTE*, msg, 4) = fakeMessage;
    F(BYTE*, msg, 8) = fakeMessage + sizeof(fakeMessage);
}
int __cdecl RecordSend(int recipient, int buffer, int bytes) {
    ++joinTrace.sends;
    joinTrace.valid &= recipient == 23 && buffer == (int)fakeMessage && bytes == sizeof(fakeMessage);
    return 0;
}
bool CheckJoin(int mode, bool callback, const char* driver, bool reference) {
    BYTE session[448] = {};
    original = reference;
    F(BYTE*, G(0x707f6c), 0) = session;
    F(int, session, 420) = mode;
    F(int, session, 440) = 7;
    F(int, session, 408) = 23;
    F(void*, session, 444) = callback ? (void*)RegisterDriver : nullptr;
    joinTrace = {};
    joinTrace.valid = true;
    auto reply = reference ? reinterpret_cast<decltype(&Net_SendJoinReply)>(0x5c5450) : Net_SendJoinReply;
    const int result = reply(session, nullptr, driver, kHost);
    const bool hosting = mode == 2;
    const bool registered = hosting && callback;
    const BYTE expected = callback && strcmp(driver, kHost) ? 1 : 255;
    return result == 1 && joinTrace.valid && joinTrace.registrations == int(registered) &&
           (!registered || !strcmp(joinTrace.registeredName, driver)) &&
           joinTrace.writes == int(hosting) && joinTrace.sends == int(hosting) &&
           (!hosting || joinTrace.assigned == expected) && F(int, session, 440) == 7 + int(hosting);
}
int CompareJoinReplies() {
    struct Hook { DWORD address; void* production; void* observer; };
    const Hook hooks[] = {
        {0x602be0, (void*)DpMessage_Construct, (void*)InitJoinMessage},
        {0x602e80, (void*)MsgBuf_Destroy, (void*)EndJoinMessage},
        {0x603110, (void*)Msg_Write120, (void*)RecordReply},
        {0x6021d0, (void*)Dp_SendTo, (void*)RecordSend},
    };
    for (const Hook& hook : hooks) {
        Redirect((void*)hook.address, hook.observer);
        Redirect(hook.production, hook.observer);
    }
    int failures = 0;
    const char* drivers[] = {"guest", kHost};
    for (const char* driver : drivers) {
        for (int mode = 1; mode <= 2; ++mode) {
            for (int callback = 0; callback <= 1; ++callback) {
                const bool ref = CheckJoin(mode, callback != 0, driver, true);
                const bool prod = CheckJoin(mode, callback != 0, driver, false);
                printf("join driver=%s mode=%d callback=%d reference=%d production=%d %s\n",
                       driver, mode, callback, ref, prod, ref && prod ? "PASS" : "FAIL");
                failures += !(ref && prod);
            }
        }
    }
    return failures;
}
}

int main(int argc,char**argv){
    if(argc!=2||!MapOriginal(argv[1]))return 2;
    if(memcmp((void*)0x683ca0,Global(0x683ca0),16))return 2;
    Redirect((void*)0x6016f0,(void*)Rebuild);Redirect((void*)Providers_Rebuild,(void*)Rebuild);
    Redirect((void*)0x6015d0,(void*)Create);Redirect((void*)Dp_Create,(void*)Create);
    Redirect((void*)0x601890,(void*)Build);Redirect((void*)Gate_BuildAddress,(void*)Build);
    Redirect((void*)0x600b50,(void*)ErrorText);Redirect((void*)DirectPlay_ErrorText,(void*)ErrorText);
    Redirect((void*)0x401010,(void*)Report);Redirect((void*)Ai_Report,(void*)Report);
    Import("CoCreateInstance",(void*)CoCreate,0x64b518);
    Import("GlobalHandle",(void*)Handle,0x64b1e4);Import("GlobalUnlock",(void*)Unlock,0x64b1fc);Import("GlobalFree",(void*)Free,0x64b1f8);
    lobbyTable[2]=(void*)ReleaseLobby;dpTable[2]=(void*)ReleaseDP;dpTable[4]=(void*)Close;
    dpTable[6]=(void*)Player;dpTable[13]=(void*)Enum;dpTable[24]=(void*)Open;dpTable[38]=(void*)Initialize;
    int failures=0;
    for(int t=0;t<16;t++){
        Observation want=Run(true,t);bool reference=Reference(want,t);
        Observation got=Run(false,t);bool equal=!memcmp(&want,&got,sizeof want),guidOK=!memcmp(want.guid,got.guid,16);
        bool ok=reference&&equal;failures+=!ok;
        printf("connect %d reference=%d equal=%d guid=%d %s\n",t,reference,equal,guidOK,ok?"PASS":"FAIL");
        if(!ok)printf("detail result=%d/%d args=%d/%d init=%d/%d open=%d/%d enum=%d/%d player=%d/%d GUID=%08lx,%08lx,%08lx,%08lx\n",want.result,got.result,want.argsOK,got.argsOK,want.init,got.init,want.open,got.open,want.enumerate,got.enumerate,want.player,got.player,got.guid[0],got.guid[1],got.guid[2],got.guid[3]);
    }
    failures += CompareJoinReplies();
    return failures?1:0;
}
