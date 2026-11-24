from pathlib import Path
import sys
s=Path(sys.argv[1]).read_text(encoding="utf-8")
fixed="const BYTE* pe = palette + 24 + *src++ * 3;"
if s.count(fixed)!=1: raise SystemExit("Expected one corrected palette lookup")
Path(sys.argv[2]).write_text(s.replace(fixed,"const BYTE* pe = self + 24 + *src++ * 3;"),encoding="utf-8")
