from pathlib import Path
import sys
s=Path(sys.argv[1]).read_text(encoding="utf-8")
fixed="const BYTE* entry = rgb + first * 8 + (i - first) * 4;"
if s.count(fixed)!=1: raise SystemExit("Expected one corrected video palette stride")
Path(sys.argv[2]).write_text(s.replace(fixed,"const BYTE* entry = rgb + i * 8;"),encoding="utf-8")
