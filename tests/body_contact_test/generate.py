"""Extract the production handler; instrument its frame, not its arithmetic.

Dependencies are test doubles with checked write ranges. The 48-byte matrix
contract and frame offsets were independently checked in the original EXE.
This is a memory/wiring regression, not an oracle for collision physics.
"""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[2]
source = Path(sys.argv[2]) if len(sys.argv) > 2 else root/'src/truck/moving_items.cpp'
text = source.read_text()
start = text.index('void __cdecl MovingItem_BodyContact(')
brace = text.index('{', start)
depth, end = 1, brace + 1
while depth:
    if text[end] == '{': depth += 1
    elif text[end] == '}': depth -= 1
    end += 1
body = text[start:end]
pattern = r'BYTE fr\[(\d+)\];'
assert len(re.findall(pattern, body)) == 1, 'Frame declaration changed: review instrumentation'
body = re.sub(pattern, lambda m: (
    f'GuardedFrame<{m[1]}> guarded; BYTE (&fr)[{m[1]}] = guarded.bytes; '
    'frameBegin = fr; frameSize = sizeof(fr);'), body)
template = Path(__file__).with_name('fixture.cpp.in').read_text()
assert template.count('// PRODUCTION') == 1
Path(sys.argv[1]).write_text(template.replace('// PRODUCTION', body))
