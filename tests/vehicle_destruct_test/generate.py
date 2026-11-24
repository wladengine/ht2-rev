"""Compile current production function bodies against a small headless fixture.

Only dependencies outside Vehicle_Destruct/Resources_SetData are stubbed.
The forwarding wrapper rejects the wrong ID before indexing fixture memory.
This checks the call contract, not complete world teardown or original equivalence.
"""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
def function(path, signature):
    text = (root / path).read_text()
    start = text.index(signature)
    brace = text.index('{', start)
    while ';' in text[start:brace]:  # skip forward declarations
        start = text.index(signature, start + len(signature))
        brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        if text[end] == '{': depth += 1
        elif text[end] == '}': depth -= 1
        end += 1
    return text[start:end]

vehicle = function(sys.argv[2] if len(sys.argv)>2 else 'src/truck/vehicle.cpp',
                   'void __fastcall Vehicle_Destruct(BYTE* s)')
free = function('src/truck/vehicle.cpp', 'static void Vd_FreeRec(BYTE* r)')
resources = function('src/engine/framework.cpp', 'extern "C" int __fastcall Resources_SetData(')
resources = resources.replace('Resources_SetData(', 'Actual_Resources_SetData(', 1)
fixture = Path(__file__).with_name('fixture.cpp.in').read_text()
Path(sys.argv[1]).write_text(fixture.replace('// PRODUCTION_RESOURCES', resources)
    .replace('// PRODUCTION_VEHICLE', free + '\n' + vehicle))
