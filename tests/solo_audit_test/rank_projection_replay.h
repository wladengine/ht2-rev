// Standalone, optional replay of numeric road geometry and player samples.
// No pointers or code from the recorded process are used in the fixture.
#include <vector>
extern "C" int __fastcall Ai455560_Forward(BYTE*,void*,DWORD,DWORD,DWORD,DWORD);
extern "C" int __fastcall Ai454e80_Find(BYTE*,void*,int,int,int,int*);
namespace rank_replay {
struct Object {
    BYTE record[128];
    void* methods[4];
    std::vector<BYTE> geometry;
    int kind;
};
struct Edge {
    BYTE record[128];
    std::vector<Object> objects;
    std::vector<BYTE*> pointers;
};
static int Number(FILE* f,int maxValue) {
    int value;
    if (fread(&value,sizeof value,1,f)!=1 || value<0 || value>maxValue) {
        fprintf(stderr,"Invalid replay count or index\n"); exit(2);
    }
    return value;
}
static void Read(FILE* f,void* out,size_t bytes) {
    if (fread(out,1,bytes,f)!=bytes) { fprintf(stderr,"Truncated replay geometry\n"); exit(2); }
}
static void Load(FILE* f,Object& o) {
    o.kind=Number(f,1);
    int count=Number(f,8192);
    if (count<(o.kind ? 1 : 2)) exit(2);
    o.geometry.resize(count*(o.kind ? 1592 : 24));
    if (o.kind) {
        for (int i=0; i<count; ++i) {
            BYTE* curve=o.geometry.data()+1592*i;
            int n=Number(f,12);
            if (n<1) exit(2);
            F(int,curve,576)=n;
            Read(f,curve,24*n); Read(f,curve+584,32);
        }
    } else {
        Read(f,o.record+56,32); Read(f,o.geometry.data(),o.geometry.size());
    }
    const int start=o.kind ? 56 : 88;
    F(BYTE*,o.record,start)=o.geometry.data();
    F(BYTE*,o.record,start+4)=o.geometry.data()+o.geometry.size();
    F(void**,o.record,0)=o.methods;
}
static int Run(const char* path) {
    FILE* f=fopen(path,"rb");
    if (!f) return 2;
    char magic[4]; Read(f,magic,4);
    if (memcmp(magic,"RPR1",4)) return 2;
    int rooms=Number(f,1024), cases=0, errors=0;
    for (int room=0; room<rooms; ++room) {
        int roomId=Number(f,4095), edgeCount=Number(f,4096);
        if (!edgeCount) return 2;
        std::vector<Edge> edges(edgeCount);
        std::vector<BYTE*> list(edgeCount), byId(8192), registry(roomId+1);
        for (int i=0; i<edgeCount; ++i) {
            Edge& e=edges[i];
            int id=Number(f,8191), objects=Number(f,4096);
            byId[id]=list[i]=e.record; F(int,e.record,12)=id;
            e.objects.resize(objects); e.pointers.resize(objects);
            for (int j=0; j<objects; ++j) { Load(f,e.objects[j]); e.pointers[j]=e.objects[j].record; }
            F(BYTE**,e.record,120)=e.pointers.data(); F(BYTE**,e.record,124)=e.pointers.data()+objects;
        }
        BYTE record[52]={};
        F(BYTE**,record,40)=list.data(); F(BYTE**,record,44)=list.data()+edgeCount;
        registry[roomId]=record;
        gameplay::SetWorld(0x68d0d8,registry.data());
        gameplay::SetWorld(0x68cf38,byId.data()); gameplay::SetWorld(0x68cf3c,byId.data()+byId.size());
        int samples=Number(f,100000);
        for (int i=0; i<samples; ++i) {
            int frame=Number(f,0x7fffffff),probe=Number(f,4); double point[3]; Read(f,point,sizeof point);
            LrPos p[2]={};
            for (int side=0; side<2; ++side) {
                for (auto& e : edges) for (auto& o : e.objects)
                    o.methods[3]=o.kind ? (side ? (void*)Ai454e80_Find : (void*)0x454e80)
                                        : (side ? (void*)Ai455560_Forward : (void*)0x455560);
                auto project=side ? Ai_GetPositionIdEx : reinterpret_cast<decltype(&Ai_GetPositionIdEx)>(0x4365d0);
                project(&p[side],roomId,point);
            }
            const double delta=F(double,p,8)-F(double,p+1,8);
            bool same=p[0].d[0]==p[1].d[0] && p[0].d[4]==p[1].d[4] && p[0].d[5]==p[1].d[5] &&
                      delta>=-1e-7 && delta<=1e-7;
            if (!same) ++errors;
            printf("projection %d %d %d %d %d %d %.12g %d %d %d %.12g\n",frame,probe,roomId,
                   p[0].d[0],p[0].d[5],p[0].d[4],F(double,p,8),p[1].d[0],p[1].d[5],p[1].d[4],F(double,p+1,8));
            ++cases;
        }
    }
    if (fgetc(f)!=EOF) return 2;
    fclose(f);
    printf("rank projection replay cases=%d mismatches=%d\n",cases,errors);
    return errors ? 3 : 0;
}
}
