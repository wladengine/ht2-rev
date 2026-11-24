"""Restore the previously missing CRT bridge in a disposable source only."""
from pathlib import Path
import sys

source=Path(sys.argv[1]).read_text(encoding='utf-8')
start=source.index('extern "C" int __cdecl _matherr(struct _exception* e) {')
end=source.index('\n}',start)+2
# No definition means the linker uses libcmt's original default handler.
source=source[:start]+source[end:]
Path(sys.argv[2]).write_text(source,encoding='utf-8')
