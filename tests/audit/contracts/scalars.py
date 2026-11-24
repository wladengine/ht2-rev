"""Run the per-entry scalar contracts, including exact width, guard bytes and x86 return ABI."""

import argparse
import csv
import hashlib
import math
import re
import struct
import sys
from pathlib import Path

import pefile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "call100"))
from compare import Image, Probe, sha  # noqa: E402
from cases import A  # noqa: E402

TYPES = {
    "u32": "I",
    "ptr": "I",
    "i32": "i",
    "i16": "h",
    "u16": "H",
    "u8": "B",
    "i8": "b",
    "bool": "?",
    "f32": "f",
    "f64": "d",
}


def fixture(image, entry, row, i):
    p = Probe(image, row["symbol"])
    types = row["arguments"].split(",") if row["arguments"] else []
    cc = row["cc"]
    op = row["operation"]
    ty = row["field"]
    bases = [A + 0x6000, A + 0x2000, A + 0x10000, A + 0x14000]
    # Encode each actual object separately; unused fastcall padding is immaterial.
    values = [
        (bases[k] + i * 0x80 if t == "ptr" else (13, 0xA4, 0x11223344)[i])
        for k, t in enumerate(types)
    ]
    if cc == "fastcall":
        p.ecx = values[0]
    p.args = values[2:] if cc == "fastcall" else values
    fmt = "<" + TYPES[ty]
    if ty.startswith("f"):
        value = (-0.25, 3.125, 0.0)[i]
    elif ty == "ptr":
        value = (0, A + 0x1200, A + 0x4500)[i]
    elif ty == "bool":
        value = (False, True, False)[i]
    else:
        value = int.from_bytes(
            bytes([0xD7, 0x36, 0x80][i : i + 1]) * struct.calcsize(fmt),
            "little",
            signed=ty.startswith("i"),
        )
    offset = int(row["offset_or_va"])
    where = (
        image.data(offset)
        if op == "read-global"
        else values[int(row["object_argument"])] + offset
    )
    original = struct.pack(fmt, value)
    p.put(where - 8, b"\xa5" * 8 + original + b"\x5a" * 8)
    expected_arena = bytearray(p.uc.mem_read(A, 0x20000))
    expected = value
    if op.startswith("store-"):
        new_value = (
            int(row["constant"])
            if op == "store-constant"
            else values[int(row["value_argument"])]
        )
        encoded = (new_value % (1 << (8 * len(original)))).to_bytes(
            len(original), "little"
        )
        expected_arena[where - A : where - A + len(original)] = encoded
        expected = (
            values[int(row["object_argument"])] if row["return"] == "ptr" else None
        )
    elif ty.startswith("f"):
        mantissa, exp = math.frexp(abs(value))
        expected = (
            int(mantissa * 2**64),
            (16382 + exp if value else 0) | (0x8000 if value < 0 else 0),
        )
    else:
        expected = int(expected) % (1 << (8 * len(original)))
    p.result = (
        "void"
        if row["return"] == "void"
        else "fp"
        if row["return"].startswith("f")
        else "eax"
    )
    result = p.run(entry)
    observed = result["result"]
    if p.result == "eax" and row["return"] != "ptr":
        observed %= 1 << (8 * struct.calcsize("<" + TYPES[row["return"]]))
    stack = 4 if cc == "cdecl" else 4 + len(p.args) * 4
    ok = (
        observed == expected
        and result["stack"] == stack
        and result["arena"] == hashlib.sha256(expected_arena).hexdigest()
    )
    if op == "read-global":
        ok &= (
            bytes(p.uc.mem_read(where - 8, len(original) + 16))
            == b"\xa5" * 8 + original + b"\x5a" * 8
        )
    return ok


def main():
    parser = argparse.ArgumentParser()
    for key in ("original", "candidate", "map", "targets", "out"):
        parser.add_argument("--" + key, type=Path, required=True)
    args = parser.parse_args()
    if (
        sha(args.original)
        != "31fbd1dbbe7706183f09a3688482791255db2bfb557c02675374b96796ce2ebd"
    ):
        raise ValueError("Wrong original 8.1")
    mapping = args.map.read_text()
    pe = pefile.PE(str(args.candidate), fast_load=True)
    if f"Timestamp is {pe.FILE_HEADER.TimeDateStamp:08x}" not in mapping:
        raise ValueError("Wrong MAP")
    symbols = {
        s: int(a, 16)
        for s, a in re.findall(r"^\s+\w+:\w+\s+(\S+)\s+([\da-f]{8})\s+", mapping, re.M)
    }
    stock = Image(args.original)
    canonical = Image(args.candidate, symbols)
    with args.targets.open() as inp, args.out.open("w", newline="") as out:
        writer = csv.writer(out, delimiter="\t", lineterminator="\n")
        writer.writerow(("stock_va", "symbol", "stock_expected", "canonical_expected"))
        for row in csv.DictReader(inp, delimiter="\t"):
            outcomes = []
            for img, entry in (
                (stock, int(row["stock_va"], 16)),
                (canonical, symbols[row["symbol"]]),
            ):
                states = ""
                for i in range(3):
                    try:
                        states += "+" if fixture(img, entry, row, i) else "-"
                    except ValueError:
                        states += "?"
                outcomes.append(states)
            writer.writerow((row["stock_va"], row["symbol"], *outcomes))


if __name__ == "__main__":
    main()
