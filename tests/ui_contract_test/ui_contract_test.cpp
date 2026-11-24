#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
extern "C" unsigned char KingBss[];
extern "C" unsigned char KingData[];
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
    // The game's startup normally initializes these axes/identity. No entry point.
    memcpy((void*)0x7025f0,identity,48);
    FlushInstructionCache(GetCurrentProcess(),image,nt->OptionalHeader.SizeOfImage);
    VirtualFree(raw,0,MEM_RELEASE);
    return image;
}





extern "C" void __fastcall Shell_51d600(BYTE*,void*,int);
extern "C" void __fastcall Shell_518820(BYTE*,void*,int);
extern "C" void __fastcall Map_DrawLine(double*,void*,BYTE*,double,double,double,double,int);
extern "C" int __fastcall List_DrawAt(BYTE*,void*,int);
extern "C" BYTE* __cdecl Storage_ReadImage(const char*,BYTE*);
extern "C" BYTE* __cdecl Storage_ReadSubImage(const char*,BYTE*);
extern "C" void __cdecl Bitmap_Draw(BYTE*,int,int,BYTE*);
extern "C" void __cdecl Text_Draw(int,int,int,BYTE*,const char*,int,int);
extern "C" void __cdecl Window_DrawLine(BYTE*,int,int,int,int,DWORD,int);
extern "C" void __cdecl Carat_SetView(int,int);
extern "C" int __cdecl Mode_TextureLevel();
extern "C" int __fastcall Ctl_PointTo(BYTE*,void*,int,int);
extern "C" BYTE* __fastcall Ctl_Dispatch(BYTE*,void*,int,int);
static void SetGlobal(unsigned a,int v){*(int*)a=v;*(int*)(a>=0x68a820?KingBss+a-0x68a820:KingData+a-0x66c000)=v;}
static void Redirect(void* address,void* fn){DWORD old;BYTE*p=(BYTE*)address;if(!VirtualProtect(p,5,PAGE_EXECUTE_READWRITE,&old))ExitProcess(2);p[0]=0xe9;F(int,p,1)=(BYTE*)fn-p-5;FlushInstructionCache(GetCurrentProcess(),p,5);VirtualProtect(p,5,old,&old);}
#define BOTH(addr,name,stub) Redirect((void*)addr,(void*)stub);Redirect((void*)name,(void*)stub)
struct Observation {int order[8],count;const char* name;char text[64];int tx,ty,bx,by,dst,font,color,zero;int draws;int line[7];};
static Observation obs;
static BYTE app[3000],screen[500],list[400],item[32],picture[32],record[100];
static const char* names="fixture";
static void* pictureVtbl[1];static bool noPicture;
static void Mark(int n){obs.order[obs.count++]=n;}
static int __fastcall List(BYTE*,void*,int){Mark(1);return 0;}
static BYTE* __cdecl Read(const char* name,BYTE* rec){Mark(2);obs.name=name;memcpy(rec,record,sizeof record);return noPicture?0:picture;}
static void __cdecl Bitmap(BYTE* dst,int x,int y,BYTE* pic){Mark(3);obs.bx=x;obs.by=y;if(dst!=(BYTE*)0x123456||pic!=picture)ExitProcess(3);}
static void __fastcall Destroy(BYTE* pic,void*,int release){Mark(5);if(pic!=picture||release!=1)ExitProcess(3);}
static void __cdecl Text(int dst,int x,int y,BYTE* font,const char* text,int color,int zero){Mark(4);obs.tx=x;obs.ty=y;obs.dst=dst;obs.font=(int)font;obs.color=color;obs.zero=zero;if(strlen(text)>=sizeof obs.text)ExitProcess(3);strcpy(obs.text,text);}
static void __cdecl Line(BYTE* d,int x0,int y0,int x1,int y1,DWORD color,int rgb){obs.draws++;int values[]={int(d),x0,y0,x1,y1,int(color),rgb};memcpy(obs.line,values,sizeof values);}
static Observation Dates(bool original,int branch,int t){
 memset(&obs,0,sizeof obs);memset(screen,0xa7,sizeof screen);memset(list,0,sizeof list);memset(item,0,sizeof item);
 BYTE* s=screen+64;F(void*,s,-52)=(void*)0x123456;F(void*,s,332)=list;F(void*,s,316)=list;
 F(void*,list,300)=t==4?0:item;F(void*,item,12)=(void*)names;F(void*,picture,0)=pictureVtbl;
 F(int,app,2864)=0x345678;SetGlobal(0x696ca0,(int)app);
 memset(record,0x91,sizeof record);
 static BYTE fields[][6]={{16,11,26,8,46,34},{1,2,26,0,3,59},{31,12,26,23,59,59},{255,255,255,255,255,255},{9,9,26,9,9,1},{1,1,26,1,1,1}};
 memcpy(record+2,fields[t],6);noPicture=t==5;
 if(original)((void(__thiscall*)(BYTE*,int))(branch?0x518820:0x51d600))(s,19);
 else if(branch)Shell_518820(s,0,19);else Shell_51d600(s,0,19);
 return obs;
}
static Observation Map(bool original,int t){
 memset(&obs,0,sizeof obs);unsigned short size[]={800,600};double line[]={50,180,200,440};double sx=1,sy=1,dx=0,dy=0;int clip=1;
 switch(t){case 1:line[0]=0;break;case 2:line[2]=270;break;case 3:line[1]=20;break;case 4:line[3]=500;break;case 5:line[0]=-60;line[3]=700;clip=0;break;case 6:sx=.5;sy=.5;dx=40;dy=150;break;case 7:sx=-1;dx=260;break;case 8:line[0]=40;line[2]=226;line[1]=150;line[3]=450;break;case 9:line[0]=line[2];line[1]=line[3];break;}
 SetGlobal(0x702e94,t%2?3:2);
 if(original)((void(__thiscall*)(double*,BYTE*,double,double,double,double,int))0x5290d0)(line,(BYTE*)size,sx,sy,dx,dy,clip);
 else Map_DrawLine(line,0,(BYTE*)size,sx,sy,dx,dy,clip);
 // Address belongs to this invocation; independently assert then normalize for comparison.
 if(obs.draws&&obs.line[0]!=(int)size)ExitProcess(3);obs.line[0]=0;
 return obs;
}
struct HintObservation {int hint,mainCursor,childCursor,calls,x,y,result;};
static HintObservation hintObs;
static BYTE hintWindow[480],hintChild[80],hintParent[400],hintButton[80];
static void* hintVtbl[12];static int textureMode;
static int __cdecl Mode(){return textureMode;}
static BYTE* __fastcall Dispatch(BYTE*,void*,int,int){return hintButton;}
static int __fastcall CanHint(BYTE*,void*){return 0;}
static int __fastcall Point(BYTE* p,void*,int x,int y){if(p!=hintButton)ExitProcess(3);hintObs.calls++;hintObs.x=x;hintObs.y=y;return 55;}
static HintObservation Hint(bool original,int t){
 memset(&hintObs,0,sizeof hintObs);memset(hintWindow,0,sizeof hintWindow);memset(hintChild,0,sizeof hintChild);memset(hintParent,0,sizeof hintParent);memset(hintButton,0,sizeof hintButton);
 F(void*,hintButton,0)=hintVtbl;F(int,hintButton,28)=102;F(int,hintParent,28)=-1;F(int,hintParent,44)=10;F(int,hintParent,48)=20;
 F(int,hintWindow,48)=0x111111;F(int,hintWindow,52)=0x222222;F(int,hintChild,48)=0x333333;F(int,hintChild,52)=0x444444;
 bool inactive=t==0||t==7,special=t==3||t==5||t==6,clear=t==4||t==5||t==7;
 textureMode=(t==2||t==6||t==9)?2:1;
 SetGlobal(0x696848,inactive?0:(int)hintWindow);SetGlobal(0x6cec80,special?(int)hintWindow:0);
 F(void*,hintWindow,372)=t==6?0:hintChild;
 SetGlobal(0x6cec60,0x555555);SetGlobal(0x6cec64,0x666666);SetGlobal(0x6f3494,0);
 if(t<8){if(original)((void(__cdecl*)(int,int))0x527ea0)((int)hintParent,clear?0:(int)hintButton);else Carat_SetView((int)hintParent,clear?0:(int)hintButton);}
 else {
  SetGlobal(0x67f3d8,0);SetGlobal(0x702f24,0);SetGlobal(0x702f3c,0);SetGlobal(0x702f4c,original?0x527ea0:(int)Carat_SetView);
  hintObs.result=original?((int(__thiscall*)(BYTE*,int,int))0x5be630)(hintParent,80,90):Ctl_PointTo(hintParent,0,80,90);
 }
 hintObs.hint=*(int*)(original?(BYTE*)0x6f3494:KingBss+0x6f3494-0x68a820);
 hintObs.mainCursor=F(int,hintWindow,52);hintObs.childCursor=F(int,hintChild,52);return hintObs;
}
// Real software rasterizer, including root lookup, palette conversion and line
// clipping. Synthetic palette changes isolate RGB independence from world/time.
enum { PW=320, PH=240, PIXELS=PW*PH };
static WORD raster[PIXELS+32],referenceRaster[PIXELS+32];
static BYTE rasterWindow[128],rasterPalette[820];
static void Raster(bool original,int format,int palette,int line){
 memset(raster,0,sizeof raster);for(int i=0;i<16;i++)raster[i]=raster[PIXELS+16+i]=0xa55a;
 memset(rasterWindow,0,sizeof rasterWindow);memset(rasterPalette,0,sizeof rasterPalette);
 F(WORD,rasterWindow,0)=PW;F(WORD,rasterWindow,2)=PH;F(WORD,rasterWindow,4)=2;F(WORD,rasterWindow,6)=PW*2;
 F(void*,rasterWindow,24)=raster+16;F(void*,rasterWindow,84)=rasterPalette;F(int,rasterPalette,808)=2;
 const BYTE colors[4][3]={{180,30,220},{240,140,20},{20,40,230},{40,40,40}};
 for(int i=0;i<256;i++)memcpy(rasterPalette+24+3*i,colors[palette],3);
 SetGlobal(0x702e94,format);
 double lines[8][4]={{10,20,290,20},{30,10,30,220},{10,10,100,100},{20,200,200,20},{290,200,10,100},{-10,120,340,120},{20,-20,20,280},{150,120,150,120}};
 if(original)((void(__thiscall*)(double*,BYTE*,double,double,double,double,int))0x5290d0)(lines[line],rasterWindow,1.,1.,0.,0.,0);
 else Map_DrawLine(lines[line],0,rasterWindow,1.,1.,0.,0.,0);
}
static int RasterChecks(){
 int failures=0;
 for(int f=2;f<=3;f++)for(int p=0;p<4;p++)for(int l=0;l<8;l++){
  Raster(true,f,p,l);memcpy(referenceRaster,raster,sizeof raster);
  bool reference=true;int count=0;
  for(int i=0;i<PIXELS;i++)if(raster[16+i]){count++;reference&=raster[16+i]==(f==2?0x7ef:0x3ef);}
  for(int i=0;i<16;i++)reference&=raster[i]==0xa55a&&raster[PIXELS+16+i]==0xa55a;
  reference&=count>0;
  Raster(false,f,p,l);bool equal=!memcmp(referenceRaster,raster,sizeof raster);bool ok=reference&&equal;failures+=!ok;
  printf("map pixels-%d/%d/%d reference=%d equal=%d count=%d %s\n",f,p,l,reference,equal,count,ok?"PASS":"FAIL");
 }
 return failures;
}
int main(int argc,char**argv){
 if(argc!=2||!MapOriginal(argv[1]))return 2;
 int failures=RasterChecks();
 BOTH(0x5c0020,List_DrawAt,List);BOTH(0x51c870,Storage_ReadSubImage,Read);BOTH(0x518760,Storage_ReadImage,Read);
 BOTH(0x5b86c0,Bitmap_Draw,Bitmap);BOTH(0x5ea170,Text_Draw,Text);BOTH(0x5b73a0,Window_DrawLine,Line);
 // Use a real CRT formatter for both callers; original argument promotion remains unmodified.
 Redirect((void*)0x61cca9,(void*)sprintf);pictureVtbl[0]=(void*)Destroy;
 BOTH(0x5d3ab0,Mode_TextureLevel,Mode);BOTH(0x5be5a0,Ctl_Dispatch,Dispatch);
 hintVtbl[9]=(void*)Point;hintVtbl[11]=(void*)CanHint;
 const char* expected[]={"16-11 08:46","01-02 00:03","31-12 23:59","255-255 255:255","09-09 09:09",""};
 for(int branch=0;branch<2;branch++)for(int t=0;t<6;t++){
  Observation want=Dates(true,branch,t),got=Dates(false,branch,t);
  int expectedOrder[5]={1,2,3,branch?5:4,branch?4:5};
  bool reference=!strcmp(want.text,expected[t])&&want.count==(t==5?2:5)&&!memcmp(want.order,expectedOrder,want.count*sizeof(int))&&want.name==(t==4?0:names);
  if(t!=5)reference=reference&&want.tx==(branch?483:490)&&want.ty==(branch?276:464)&&want.bx==(branch?478:487)&&want.by==(branch?203:390)&&want.dst==0x123456&&want.font==0x345678&&want.color==5&&want.zero==0;
  bool equal=!memcmp(&want,&got,sizeof want);bool ok=reference&&equal;failures+=!ok;
  printf("dates %d/%d reference=%d equal=%d text=%s %s\n",branch,t,reference,equal,got.text,ok?"PASS":"FAIL");
 }
 for(int t=0;t<10;t++){
  Observation want=Map(true,t),got=Map(false,t);bool visible=t==0||t>=5;
  bool reference=want.draws==(visible?1:0)&&(!visible||(want.line[5]==(t%2?0x3ef:0x7ef)&&want.line[6]==1));
  bool equal=!memcmp(&want,&got,sizeof want);bool ok=reference&&equal;failures+=!ok;
  printf("map %d reference=%d equal=%d draws=%d rgb=%d %s\n",t,reference,equal,got.draws,got.line[6],ok?"PASS":"FAIL");
 }
 for(int t=0;t<10;t++){
  HintObservation want=Hint(true,t),got=Hint(false,t);bool inactive=t==0||t==7,clear=t==4||t==5||t==7;
  int mainCursor=inactive?0x222222:clear?0x111111:(t==2||t==6||t==9)?0x666666:0x555555;
  int childCursor=t==3?0x555555:t==5?0x333333:0x444444;
  bool reference=want.hint==(clear?0:(int)hintButton)&&want.mainCursor==mainCursor&&want.childCursor==childCursor&&want.calls==(t>=8?1:0);
  if(t>=8)reference=reference&&want.x==70&&want.y==70&&want.result==55;
  bool equal=!memcmp(&want,&got,sizeof want);bool ok=reference&&equal;failures+=!ok;
  printf("hint %d reference=%d equal=%d child=%d %s\n",t,reference,equal,got.hint==(int)hintButton,ok?"PASS":"FAIL");
 }
 printf("%d/96 failures\n",failures);return failures?1:0;
}
