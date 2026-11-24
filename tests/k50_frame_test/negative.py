"""Restore only the old return contract; leave the production arithmetic intact."""
from pathlib import Path
import sys
s=Path(sys.argv[1]).read_text()
a=s.index('float* __fastcall Body_SettleOnPlane(')
b=s.index('} // extern "C"',a)
part=s[a:b]
assert part.count('return out;')==2
part=part.replace('float* __fastcall Body_SettleOnPlane','void __fastcall Body_SettleOnPlane')
part=part.replace('return out;','return;')
Path(sys.argv[2]).write_text(s[:a]+part+s[b:])
