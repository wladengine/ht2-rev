"""Restore one diagnosed ABI error in a disposable production unit."""
from pathlib import Path
import sys
mode, source, destination = sys.argv[1:]
text = Path(source).read_text(encoding="utf-8")
replacements = {
    "hints": (
        'extern "C" void __cdecl Carat_SetView(int /*parent*/, int a)',
        'extern "C" void __cdecl Carat_SetView(int a)',
    ),
    "dates": (
        "    sprintf(buf, (const char*)s_gamshe6772b0, rec[2], rec[3], rec[5], rec[6]);",
        "    CallC(Gs_sprintf, buf, (const char*)s_gamshe6772b0, rec[2], rec[3], rec[5], rec[6]);",
    ),
    "map": (
        "    Window_DrawLine(d, x0, y0, x1, y1, Pixel_Pack(0, 255, 120), 1);",
        "    CallC(FUN_5b73a0, d, x0, y0, x1, y1, CallC<int>(FUN_5b6210, 0, 255, 120, 1));",
    ),
}
old, new = replacements[mode]
assert text.count(old) == 1, "production anchor changed"
Path(destination).write_text(text.replace(old, new), encoding="utf-8")
