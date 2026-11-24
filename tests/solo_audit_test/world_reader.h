extern "C" BYTE* __cdecl Reader_Read(BYTE*,int,BYTE*);

namespace world_reader {
struct Literal { const char* text; DWORD bits, tag; };
static bool Check(bool original) {
    const auto read=original ? reinterpret_cast<decltype(&Reader_Read)>(0x4b3e30) : Reader_Read;
    const Literal literals[]={{"#t",DWORD(-1),0x10007},{"#f",0,0x10007},{"#\\a",'a',0x10008},
        {"#\\newline",10,0x10008},{"#\\space",32,0x10008},{"()",0,0}};
    const char* gaps[]={"","   ","\t\r\n","; delivery rules\n"," \n; warehouse\n; freight\n\t"};
    int cases=0, errors=0;
    for (const char* gap : gaps) for (const Literal& first : literals) for (const Literal& second : literals) {
        char line[110];
        const int length=sprintf(line,"%s%s %s",gap,first.text,second.text);
        int page[128]; int* pages[]={page};
        for (int i=0; i<128; ++i) page[i]=i<length ? int((unsigned char)line[i]) : -1;
        BYTE r[80]={};
        F(int*,r,8)=F(int*,r,12)=F(int*,r,28)=page;
        F(int*,r,16)=F(int*,r,32)=page+128;
        F(int**,r,20)=F(int**,r,36)=pages;
        F(int*,r,24)=page+127;
        bool ok=true;
        int consumed=0;
        for (const Literal& expected : {first,second,Literal{"",0,0}}) {
            DWORD out[4]={0xcdcdcdcd,0xcdcdcdcd,0xcdcdcdcd,0x12345678};
            BYTE* v=read((BYTE*)out,0,r);
            const int next=int(F(int*,r,8)-page);
            if (v!=(BYTE*)out || out[0]!=expected.bits || out[1]!=0 || out[2]!=expected.tag ||
                out[3]!=0x12345678 || next<=consumed) ok=false;
            consumed=next;
        }
        if (!ok || consumed!=length+1) {
            if (!errors) printf("world reader mismatch original=%d first=%s second=%s consumed=%d/%d\n",original,first.text,second.text,consumed,length+1);
            ++errors;
        }
        ++cases;
    }
    printf("world script literals original=%d cases=%d mismatches=%d\n",original,cases,errors);
    return !errors;
}
}
