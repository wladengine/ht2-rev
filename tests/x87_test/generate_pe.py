"""Adapt existing deterministic fixtures to independently loaded PE procedures.

No reference body is generated from C++. Production hitch/copter OBJ files are
linked unchanged; the small fixture keeps its existing exact-source extraction.
"""
from pathlib import Path
import sys

root = Path(__file__).resolve().parent
out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)

# Numeric fixtures link only the tested objects, so include the game's actual
# CRT bridge/policy too. The reference executes its own original 0x4e4671 body.
runtime = ['#include <math.h>', '#include <stddef.h>', '#include <string.h>', '#include "x87.h"',
           'typedef unsigned char BYTE;', '#define F(T,p,o) (*(T*)((BYTE*)(p)+(o)))']
for path, name in [('src/truck/world.cpp', 'World_MathErr'), ('src/engine/crt.cpp', '_matherr')]:
    source = (root.parent.parent / path).read_text(encoding='utf-8')
    start = source.index(f'extern "C" int __cdecl {name}(')
    end = source.index('\n}', start) + 2
    runtime.append(f'#line {source[:start].count(chr(10))+1} "{path}"\n' + source[start:end])
(out / 'matherr-support.cpp').write_text('\n'.join(runtime)+'\n', encoding='utf-8')

HOOKS = {
    "hitch": {
        0x4BD4D0: "Vec3_ZeroThis", 0x4BDD60: "Vec3_Scaled", 0x4BDDB0: "Vec3_Add",
        0x4BDE50: "Vec3_Cross", 0x4BDEF0: "Vec3_SetThis", 0x4CB270: "Vec3_Negated",
        0x4CB2B0: "Vec3_RotateBy", 0x4E10D0: "Vec3_DivScalar", 0x4E3132: "Vec3_DivComponents",
        0x4EBAE0: "Engine_AddContact", 0x4EBB70: "Engine_AddPush", 0x4EEC80: "Trailer_SaddleLoad",
        0x4F7E10: "Body_SetVec9012", 0x4F7E30: "Body_SetSpin", 0x4F7FD0: "Body_GetVec9012",
        0x4F8000: "Body_GetVec9048", 0x4F8030: "Body_GetVec9024", 0x4F8060: "Body_Matrix22a4",
        0x4F8090: "Body_Matrix2274", 0x4F80C0: "Body_GetVec9084", 0x4F80F0: "Body_GetVec9144",
        0x4F8120: "Body_Get9508", 0x4FAB20: "Body_Integrate", 0x4FADB0: "Body_FrameIntegrate",
        0x4FEF20: "Body_StateStep", 0x55B590: "Car_PlaceWithTrailer", 0x55B6F0: "Hitch_Align",
        0x5AB400: "Vec3_Normalized", 0x5AB4D0: "Vec3_Length", 0x5AB620: "Vec3_MulAssign",
        0x5AB710: "Vec3_AddAssign",
    },
    "copter": {
        0x4E260C: "World_Fatal", 0x4E2749: "Game_FrameTime", 0x4E41D9: "Angle_F2Pi",
        0x4E7261: "Query_4e7261", 0x4E7566: "Query_4e7566", 0x562C20: "Copter_SideDrift",
        0x563490: "Copter_PlaneCircle", 0x5AB400: "Vec3_Normalized", 0x5AB4D0: "Vec3_Length",
        0x5AB710: "Vec3_AddAssign", 0x5AC0C0: "Matrix_RotationZ", 0x61EDDE: "rand",
        0x622BA3: "ReferenceThreadData",
    },
    "small": {
        0x4AFC20: "SchList_End", 0x4AFBF0: "SchList_Begin", 0x47A010: "Range_CallE",
        0x426280: "Int_Set", 0x466780: "LexCompare", 0x461B20: "Rec60_Copy",
        0x463260: "Rec60_Call4667d0", 0x4632D0: "Rec60_Call4668d0", 0x463310: "Rec60_Call466a00",
    },
}

def replace(text, old, new, count=1):
    assert text.count(old) == count, f"fixture anchor changed: {old[:80]}"
    return text.replace(old, new)

for part in HOOKS:
    text = (root / f"{part}_test.cpp").read_text(encoding="utf-8")
    setup = "    if(argc<3||!MapOriginal(argv[2]))return 2;\n" + "".join(
        f"    Redirect(0x{address:x},(void*){name});\n" for address, name in HOOKS[part].items()
    )
    if part != "small":
        entry, address, candidate, loader = (
            ("Hitch_Solve558f20", 0x558F20, "Hitch_Solve", "LoadRdata") if part == "hitch" else
            ("Copter_Step563600", 0x563600, "Copter_Step", "LoadImage")
        )
        text = text.replace(candidate + "C", candidate)
        text = replace(text, f'extern "C" void {entry}();', f'static void* {entry}=(void*)0x{address:x};')
        text = replace(text, 'extern "C" unsigned g_cov[];\nextern "C" unsigned g_ncov;', '#include "pe_reference.h"')
        text = replace(text, f"        call {entry}", f"        call [{entry}]")
        text = replace(text, '    DWORD r;\n    BYTE* self', '    ToOriginal();\n    DWORD r;\n    BYTE* self')
        text = replace(text, '            Snapshot(a);', '            FromOriginal();\n            Snapshot(a);')
        text = replace(text, f'    {loader}(argc > 2 ? argv[2] : "king_original.exe");', setup + f'    {loader}(argv[2]);')
        start = text.index('    unsigned hit = 0;')
        end = text.index('    return fails != 0;', start)
        text = text[:start] + f'    printf("{part} PE: requested=%u completed=%u precision=%s failures=%d\\n",cases,executed,precision,fails);\n' + text[end:]
        text = replace(text, '    int fails = 0;', '    int fails = 0;unsigned executed=0;\n    const char* precision=argc>4?argv[4]:"both";')
        if part == "hitch":
            text = replace(text, '    for (unsigned seed = 0; seed < cases && fails < 10; seed++)', '    unsigned first=argc>3?atoi(argv[3]):0;\n    for (unsigned seed = first; seed < first+cases && fails < 10; seed++)')
        text = replace(text, '            unsigned cw;', '            if(strcmp(precision,"both")&&strcmp(precision,names[m]))continue;\n            executed++;\n            unsigned cw;')
        text = replace(text, '    Result a, b;', '    Result a, b, initialA, initialB;unsigned initialControl;')
        anchor = '            g_log.clear();'
        assert text.count(anchor) == 2
        text = text.replace(anchor, anchor+'\n            Snapshot(initialA);initialControl=_controlfp(0,0);', 1)
        marker = text.index('            Snapshot(a);')
        text = text[:marker] + replace(text[marker:], anchor, anchor+'\n            Snapshot(initialB);\n            if(initialA.mem!=initialB.mem||initialControl!=_controlfp(0,0)){printf("fixture inputs/FPU differ seed=%u %s\\n",seed,names[m]);return 3;}')
        if part == "copter":
            # Original string addresses differ from relocated candidate arrays;
            # normalize pointer identity only, never numerical inputs or outputs.
            canon = 'static const char* Canon(const char*p){unsigned a=(unsigned)p;return a>=0x66c000&&a<0x68a820?(char*)KingData+a-0x66c000:a>=0x64b000&&a<0x66c000?(char*)KingRdata+a-0x64b000:p;}\n'
            text = replace(text, 'static void Lf(float f)', canon+'static void Lf(float f)')
            text = replace(text, 'Lf(x); L(&name, 4);', 'Lf(x); name=Canon(name); L(&name, 4);')
            text = replace(text, 'Lid(9); L(&msg, 4);', 'Lid(9); msg=Canon(msg); L(&msg, 4);')
            # VC6 _getptd needs CRT startup which the fixture intentionally avoids.
            # Supply initialized per-thread storage; keep original CIpow/CIacos/ftol.
            text = replace(text, 'static DWORD RunAsm()', 'static BYTE referenceThreadData[0x74];\nstatic void* __cdecl ReferenceThreadData(){return referenceThreadData;}\nstatic DWORD RunAsm()')
            text = replace(text, '    ToOriginal();', '    memset(referenceThreadData,0,sizeof referenceThreadData);\n    ToOriginal();')
    else:
        text = replace(text, 'unsigned char KingBss[0xA0000];\n\n#define', 'unsigned char KingBss[0xA0000];\n#include "pe_reference.h"\n\n#define')
        for entry, address in [('Player_FromId477b60', 0x477B60), ('Rope_Compare45a7d0', 0x45A7D0)]:
            text = replace(text, f'extern "C" void {entry}();', f'static void* {entry}=(void*)0x{address:x};')
            text = replace(text, f'        call {entry}', f'        call [{entry}]')
        text = replace(text, '    __asm {', '    ToOriginal();\n    __asm {', 2)
        text = replace(text, '    std::vector<BYTE> log1 = g_log;', '    FromOriginal();\n    std::vector<BYTE> log1 = g_log;', 2)
        text = replace(text, '    int fails = 0;', setup+'    int fails = 0;')
    text = '// Generated PE adapter. See tests/x87_test/README.md for scope.\n' + text
    (out / f'{part}-pe.cpp').write_text(text, encoding='utf-8')
