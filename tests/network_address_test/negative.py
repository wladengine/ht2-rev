"""Restore selected old COM ABI calls in a temporary production unit."""
from pathlib import Path
import sys
mode, source, destination = sys.argv[1:]
text = Path(source).read_text(encoding="utf-8")
calls = {
    "enum": "Com(l, 24, (void*)Gate_AddRecord, sp, types, 0)",
    "size": "Com(l, 56, e, n, (void*)0, &sz)",
    "write": "Com(l, 56, e, n, buf, &sz)",
}
for key, call in calls.items():
    assert text.count(call) == 1, "production anchor changed"
    if mode in (key, "all"):
        text = text.replace(call, call.replace("Com(", "CallVirt<HRESULT>("))
Path(destination).write_text(text, encoding="utf-8")
