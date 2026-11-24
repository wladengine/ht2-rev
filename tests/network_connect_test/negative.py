import sys
from pathlib import Path
text=Path(sys.argv[1]).read_text()
fixed="Gate_BuildAddress(lobby, (const BYTE*)sp, &addr, &size, h);"
assert text.count(fixed)==1
Path(sys.argv[2]).write_text(text.replace(fixed,"Gate_BuildAddress(lobby, (const BYTE*)&sp0, &addr, &size, h);"))
