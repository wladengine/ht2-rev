from pathlib import Path
import sys
kind,src,dst=sys.argv[1:]
s=Path(src).read_text()
def replace(old,new):
    global s
    assert s.count(old)==1, (old,s.count(old))
    s=s.replace(old,new)
if kind=='caller':
    replace('CallThis(FUN_614f00, g_q214_6f33b4, m + 9, v, (void*)g_q214_702630);','CallStd(FUN_614f00, m + 9, v, (void*)g_q214_702630);')
    replace('#include "terrain_view.h"', '#include "terrain_view.h"\nextern "C" void FUN_615230(); KING_ALIAS("_FUN_615230", "@Terrain_PlaceCameraView@20")')
    replace('Terrain_PlaceCameraView(g_terrain6f33b4, 0, cameraPosition, v, g_terrainViewUp);','CallStd(FUN_615230, m + 9, v, (void*)g_q214_702630);')
    # Keep aliases pointing to corrected wrappers: isolate caller boundary.
elif kind=='wrapper':
    replace('float* __fastcall Terrain_Call614960(BYTE* self,void*,const Vec3T* a,const Vec3T* b,const Vec3T* c)', 'int __stdcall Terrain_Call614960(const Vec3T* a,const Vec3T* b,const Vec3T* c)')
    start=s.index('extern "C" void __fastcall Terrain_PlaceCameraView(')
    end=s.index('\n}',start)+2
    s=s[:start]+'''extern "C" void FUN_614f70(); KING_ALIAS("_FUN_614f70", "@TerrainView_Place@20")
extern "C" int __stdcall Terrain_Call614f70(const Vec3T* a,const Vec3T* b,const Vec3T* c)
{
    Vec3T ca=*a,cb=*b,cc=*c;
    return CallStd<int>(FUN_614f70,&ca,&cb,&cc);
}'''+s[end:]
    replace('return CallThis<float*>(FUN_614960,self,&ca,&cb,&cc);','return CallStd<int>(FUN_614960,&ca,&cb,&cc);')
    # Resolve corrected callers to the exact old signatures/bodies. Both ABIs
    # pop 12 stack bytes, but the old wrapper does not preserve incoming ECX.
    s+='\nKING_ALIAS("@Terrain_Call614960@20", "_Terrain_Call614960@12")\nKING_ALIAS("@Terrain_PlaceCameraView@20", "_Terrain_Call614f70@12")\n'
elif kind=='plant':
    start=s.index('extern "C" void __fastcall Patch_AddPlantFromVectors(')
    body=s.index('\n{',start)
    end=s.index('\n}',body)+2
    s=s[:body]+''';
extern "C" void FUN_617180(); KING_ALIAS("_FUN_617180", "@Patch_AddPlant@28")
extern "C" int __stdcall Terrain_Call617180(const Vec3T* a,const Vec3T* b,int c,int d,int e)
{
    Vec3T cb=*b,ca=*a;
    return CallStd<int>(FUN_617180,&ca,&cb,c,d,e);
}
KING_ALIAS("@Patch_AddPlantFromVectors@28", "_Terrain_Call617180@20")
'''+s[end:]
elif kind=='height':
    replace('            c.up.z = 2.0f;', '            c.up.z = 1.0f;')
elif kind=='arch':
    start=s.index('void __cdecl Strip_DrawArch(')
    end=s.index('            q[0] = V[0];',start)
    vertices=s[start:end]
    assert vertices.count('+ p[')==9
    s=s[:start]+vertices.replace('+ p[','+ q[')+s[end:]
else: raise ValueError(kind)
Path(dst).write_text(s)
