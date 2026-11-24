"""Compare two build outputs, permitting only PE timestamps and CheckSum to vary."""
import hashlib
import json
from pathlib import Path
import sys
import pefile


def inspect(path):
    raw = Path(path).read_bytes()
    pe = pefile.PE(data=raw)
    normalized = bytearray(raw)
    ignored = [("COFF.TimeDateStamp", pe.FILE_HEADER.get_field_absolute_offset("TimeDateStamp")),
               ("OptionalHeader.CheckSum", pe.OPTIONAL_HEADER.get_field_absolute_offset("CheckSum"))]
    for i, entry in enumerate(getattr(pe, "DIRECTORY_ENTRY_DEBUG", [])):
        ignored.append((f"Debug[{i}].TimeDateStamp", entry.struct.get_field_absolute_offset("TimeDateStamp")))
    for _, offset in ignored:
        normalized[offset:offset + 4] = bytes(4)
    return normalized, {
        "file": str(Path(path).resolve()), "sha256": hashlib.sha256(raw).hexdigest(),
        "normalized_sha256": hashlib.sha256(normalized).hexdigest(),
        "size": len(raw), "machine": hex(pe.FILE_HEADER.Machine),
        "ignored_fields": [{"field": name, "offset": hex(offset)} for name, offset in ignored],
        "text_sha256": next(hashlib.sha256(s.get_data()).hexdigest() for s in pe.sections if s.Name.rstrip(b"\0") == b".text"),
    }


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: python tools/compare-builds.py first.exe second.exe")
    a, ai = inspect(sys.argv[1])
    b, bi = inspect(sys.argv[2])
    equal = a == b
    print(json.dumps({"builds": [ai, bi], "equal_except_PE_timestamps_and_checksum": equal}, indent=2))
    sys.exit(0 if equal else 1)
