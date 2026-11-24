"""Extract the resources (.rsrc) of the game's original king.exe into a .res
file so the reassembled king.exe can be linked with the same icons, cursor,
bitmaps and strings:  python extractResources.py <game's king.exe> king.res
then add king.res to the link command."""
import struct
import sys
import hashlib
import json
from pathlib import Path
import pefile


def res_id(entry):
    """name or ordinal of a resource directory entry, encoded for a .res header"""
    if entry.name is not None:
        return str(entry.name).encode("utf-16-le") + b"\0\0"
    return struct.pack("<HH", 0xFFFF, entry.id)


def pad4(data):
    return data + b"\0" * (-len(data) % 4)


def res_entry(type_id, name_id, lang, data, flags=0x1030):
    header = type_id + name_id
    header = pad4(header)
    # DataVersion, MemoryFlags (default MOVEABLE|PURE|DISCARDABLE), LanguageId, Version, Characteristics
    header += struct.pack("<IHHII", 0, flags, lang, 0, 0)
    header = struct.pack("<II", len(data), 8 + len(header)) + header
    return header + pad4(data)


def main():
    if len(sys.argv) != 3 or not sys.argv[1]:
        sys.exit("usage: python extractResources.py <game's king.exe> <output .res>")
    input_file, output_file = sys.argv[1:]
    source = Path(input_file).resolve()
    output = Path(output_file)
    provenance_file = output.with_suffix(output.suffix + ".json")
    provenance = {
        "source": str(source),
        "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        "extractor_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "pefile_version": pefile.__version__,
    }
    if output.exists() and provenance_file.exists():
        try:
            previous = json.loads(provenance_file.read_text(encoding="utf-8"))
        except (ValueError, OSError):
            previous = {}
        if (all(previous.get(k) == v for k, v in provenance.items())
                and previous.get("resource_sha256") == hashlib.sha256(output.read_bytes()).hexdigest()):
            print("resources up to date:", output_file)
            return
    pe = pefile.PE(input_file)
    if not hasattr(pe, "DIRECTORY_ENTRY_RESOURCE"):
        sys.exit("input PE has no resource directory: " + input_file)
    out = [res_entry(struct.pack("<HH", 0xFFFF, 0), struct.pack("<HH", 0xFFFF, 0), 0, b"", flags=0)]
    count = 0
    for type_entry in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        for name_entry in type_entry.directory.entries:
            for lang_entry in name_entry.directory.entries:
                d = lang_entry.data.struct
                data = pe.get_data(d.OffsetToData, d.Size)
                out.append(res_entry(res_id(type_entry), res_id(name_entry), lang_entry.id, data))
                count += 1
    if not count:
        sys.exit("input PE contains no resources: " + input_file)
    data = b"".join(out)
    # Replace only after extraction succeeds. Never trust an untracked king.res.
    temp = output.with_suffix(output.suffix + ".tmp")
    temp.write_bytes(data)
    temp.replace(output)
    provenance.update(resource_sha256=hashlib.sha256(data).hexdigest(), count=count)
    provenance_file.write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    print("wrote", output_file, "with", count, "resources")


if __name__ == "__main__":
    main()
