"""Restore the diagnosed 4-byte output in a temporary production unit only."""
from pathlib import Path
import sys
source = Path(sys.argv[1]).read_text(encoding="utf-8")
start = source.index("    // FUN_437390 writes a complete 32-byte PositionId")
end = source.index("    if (node == -1)", start)
source = source[:start] + "    int node;\n    CallC(FUN_437390, &node, room, *p);\n" + source[end:]
Path(sys.argv[2]).write_text(source, encoding="utf-8")
