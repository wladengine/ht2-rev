"""Restore only the old room-query endpoint in a temporary production unit."""
from pathlib import Path
import sys
source = Path(sys.argv[1]).read_text(encoding="utf-8")
old = "(int)(s + 536), (int)(m + 9), (char*)(s + 1132)"
if source.count(old) != 1:
    raise SystemExit("Expected exactly one corrected render-room endpoint")
Path(sys.argv[2]).write_text(source.replace(old, old.replace("m + 9", "m + 8")), encoding="utf-8")
