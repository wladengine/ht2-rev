// Local independent PE reference for the existing deterministic test fixtures.
// Included after their KingRdata/KingData/KingBss arrays have been defined.
static BYTE* referenceImage;
extern "C" void __cdecl mainCRTStartup();
extern "C" void __cdecl PeEntry() {
    referenceImage=(BYTE*)VirtualAlloc((void*)0x400000,0x3bd000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
    mainCRTStartup();
}
static bool MapOriginal(const char* path) {
    if(!referenceImage) {
        MEMORY_BASIC_INFORMATION info;
        for(BYTE*p=(BYTE*)0x400000;p<(BYTE*)0x7bd000;) {
            if(!VirtualQuery(p,&info,sizeof info))break;
            printf("original address occupied: base=%p allocation=%p size=%x state=%x type=%x\n",info.BaseAddress,info.AllocationBase,(unsigned)info.RegionSize,info.State,info.Type);
            p=(BYTE*)info.BaseAddress+info.RegionSize;
        }
        return false;
    }
    FILE*f=fopen(path,"rb");if(!f)return false;
    fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
    BYTE*raw=(BYTE*)VirtualAlloc(0,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!raw||fread(raw,1,size,f)!=(size_t)size){fclose(f);return false;}fclose(f);
    IMAGE_NT_HEADERS32*nt=(IMAGE_NT_HEADERS32*)(raw+((IMAGE_DOS_HEADER*)raw)->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->OptionalHeader.ImageBase!=0x400000||nt->OptionalHeader.SizeOfImage!=0x3bd000)return false;
    memcpy(referenceImage,raw,nt->OptionalHeader.SizeOfHeaders);
    IMAGE_SECTION_HEADER*sec=IMAGE_FIRST_SECTION(nt);
    for(int i=0;i<nt->FileHeader.NumberOfSections;i++)memcpy(referenceImage+sec[i].VirtualAddress,raw+sec[i].PointerToRawData,sec[i].SizeOfRawData);
    VirtualFree(raw,0,MEM_RELEASE);
    FlushInstructionCache(GetCurrentProcess(),referenceImage,0x3bd000);
    return true;
}
static void Redirect(unsigned address,void*fn) {
    BYTE*p=(BYTE*)address;p[0]=0xe9;*(int*)(p+1)=(BYTE*)fn-p-5;
    FlushInstructionCache(GetCurrentProcess(),p,5);
}
// The old fixture arrays have padding beyond the real section boundaries.
// Copy only genuine data ranges so adjacent BSS and fixture padding stay separate.
static void ToOriginal() {
    memcpy((void*)0x64b000,KingRdata,0x21000);
    memcpy((void*)0x66c000,KingData,0x1e820);
    memcpy((void*)0x68a820,KingBss,sizeof KingBss);
}
static void FromOriginal() {
    memcpy(KingData,(void*)0x66c000,0x1e820);
    memcpy(KingBss,(void*)0x68a820,sizeof KingBss);
}
