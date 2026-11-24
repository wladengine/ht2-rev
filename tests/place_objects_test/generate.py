"""Exercise the production save-placement body with deterministic dependencies.

The capacity assertion is instrumentation: Car_GetState's independently checked
contract is 49 floats. Query and matrix stubs check endpoint wiring, not geometry.
"""
from pathlib import Path
import sys
import re

root = Path(__file__).resolve().parents[2]
source = Path(sys.argv[2]) if len(sys.argv) > 2 else root/'src/truck/effects.cpp'
text = source.read_text()
start = text.index('void __cdecl Fx_PlaceObjects()')
brace = text.index('{', start)
depth, end = 1, brace + 1
while depth:
    if text[end] == '{': depth += 1
    elif text[end] == '}': depth -= 1
    end += 1
body = text[start:end]
needle = 'Car_GetState(car, 0, (float*)st);'
assert body.count(needle) == 1
body = body.replace(needle, 'static_assert(sizeof(st) >= 49 * sizeof(float), "Car_GetState writes 49 floats");\n' + needle)
fixture = Path(__file__).with_name('fixture.cpp.in').read_text()
physics = (root/'src/truck/vehicle_physics.cpp').read_text()
physics = physics[physics.index('void __fastcall Body_PhysicsFrame('):]
node = re.search(r'BYTE node\[[^]]+\];', physics)[0]
fixture += '\nstatic void bodyNodeCapacity() { ' + node + ' static_assert(sizeof(node) >= 208, "Node_Init writes 208 bytes"); }\n'

vehicle = Path(sys.argv[3]) if len(sys.argv) > 3 else root/'src/truck/vehicle.cpp'
vtext = vehicle.read_text()
start = vtext.index('    Matrix m288;', vtext.index('int __fastcall Car_AddMass('))
end = vtext.index('    {', vtext.index('    Car_Place(s, 0, q, (const BYTE*)&m288);', start))
placement = vtext[start:end]
ai = Path(sys.argv[4]) if len(sys.argv) > 4 else root/'src/ai/ai_saveload.cpp'
a = ai.read_text(); start=a.index('                node[0] = F(DWORD, *cur, 0);', a.index('void __cdecl Load_Town(')); end=a.index('                if (node[0]',start)
fixture=fixture.replace('// TOWN_FIELDS',a[start:end])
Path(sys.argv[1]).write_text(fixture.replace('// PRODUCTION', body).replace('// ADD_MASS_PLACEMENT', placement))
