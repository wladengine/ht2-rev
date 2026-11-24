from pathlib import Path
import sys
s=Path(sys.argv[1]).read_text(encoding="utf-8")
a="F(BYTE**, F(BYTE*, g_isApp696ca0, 9396), 100)[0]"
b="*F(BYTE**, F(BYTE*, F(BYTE*, g_isApp696ca0, 9396), 100), 0)"
if s.count(a)!=3: raise SystemExit("Expected three corrected menu palette lookups")
Path(sys.argv[2]).write_text(s.replace(a,b),encoding="utf-8")
