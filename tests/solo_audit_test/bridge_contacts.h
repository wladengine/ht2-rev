extern "C" int __cdecl Body_PointContact(const float*, BYTE*, BYTE*, float);
extern "C" int __cdecl Body_SegmentContact(const float*, const float*, int, BYTE*, int);

namespace bridge_contacts {
// Apply the same moving-body query to points below, on and above the cab.
// The two world placements include the southbound orientation near room_cb08.
static bool Check(const float planes[][4], int count) {
    BYTE body[5300] = {};
    F(int,body,5132) = count;
    F(float,body,3280) = 6;
    for (int i=0; i<count; ++i) {
        float* r = reinterpret_cast<float*>(body+3988+28*i);
        memcpy(r,planes[i],4*sizeof(float));
        for (int j=0; j<3; ++j) r[4+j] = -r[3]*r[j];
    }
    auto point = reinterpret_cast<decltype(&Body_PointContact)>(0x5917f0);
    auto segment = reinterpret_cast<decltype(&Body_SegmentContact)>(0x591b50);
    int cases=0, mismatches=0, falseHits=0, missed=0, referenceHits=0, referenceErrors=0;
    for (int sign : {1,-1}) {
        const float ox=sign>0 ? 0.f : 1568.f, oy=sign>0 ? 0.f : 2133.f, oz=sign>0 ? 0.f : 5.f;
        const float world[] = {(float)sign,0,0,0,(float)sign,0,0,0,1,ox,oy,oz};
        const float inverse[] = {(float)sign,0,0,0,(float)sign,0,0,0,1,-sign*ox,-sign*oy,-oz};
        memcpy(body+500,world,48); memcpy(body+596,inverse,48);
        for (float x : {0.f, 1.295f, 1.305f, 2.5f})
        for (float y : {3.f, 3.46f, 3.465f, 3.49f})
        for (float z : {0.f, 2.9f, 4.f, 7.f}) {
            float end[] = {ox+sign*x,oy+sign*y,oz+z};
            float start[] = {end[0],end[1]+sign*.25f,end[2]};
            for (bool sweep : {false,true}) {
                BYTE ref[208] = {}, got[208] = {};
                int expect = sweep ? segment(start,end,(int)body,ref,0x3c23d70a) : point(end,body,ref,.01f);
                int actual = sweep ? Body_SegmentContact(start,end,(int)body,got,0x3c23d70a)
                                   : Body_PointContact(end,body,got,.01f);
                if (sign<0 && x==0 && y==3.465f && z==7 && !sweep)
                    printf("overpass probe z=%.1f original=%d rebuilt=%d\n",end[2],expect,actual);
                // These elevated paths are entirely above both the box and the ZIL.
                if (z>=4 && expect) ++referenceErrors;
                if (expect) ++referenceHits;
                if (!expect && actual) ++falseHits;
                if (expect && !actual) ++missed;
                if (actual!=expect || (actual && memcmp(ref+52,got+52,24))) ++mismatches;
                ++cases;
            }
        }
    }
    printf("truck hull planes=%d cases=%d referenceHits=%d referenceErrors=%d mismatches=%d falseHits=%d missed=%d\n",
           count,cases,referenceHits,referenceErrors,mismatches,falseHits,missed);
    return referenceHits && !referenceErrors && !mismatches;
}

static bool Box() {
    const float box[][4] = {{-1,0,0,-1.3f},{1,0,0,-1.3f},{0,-1,0,-8.26f},
                            {0,0,-1,-.923f},{0,0,1,-2.92f},{0,1,0,-3.46f}};
    return Check(box,6);
}

// Optional capture-specific replay from a separately supplied Steam vehicle.tech.
static int Zil(const char* path) {
    FILE* file = fopen(path,"rb");
    if (!file) return 2;
    constexpr int model=50889;
    char label[15]={};
    int count=0;
    float planes[13][4];
    bool read = !fseek(file,model+2056,SEEK_SET) && fread(label,1,sizeof(label),file)==sizeof(label)
             && !memcmp(label,"ZilRBoothSpace",sizeof(label))
             && !fseek(file,model+1672,SEEK_SET) && fread(&count,4,1,file)==1 && count==13
             && !fseek(file,model+1352,SEEK_SET) && fread(planes,sizeof(planes),1,file)==1;
    fclose(file);
    if (!read) return 2;
    return Check(planes,count) ? 0 : 3;
}
}
