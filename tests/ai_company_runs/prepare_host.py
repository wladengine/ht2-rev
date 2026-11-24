"""Prepare an isolated Steam 8.1 benchmark host without changing its campaign code."""

import argparse
import hashlib
from pathlib import Path
import struct

import pefile


STOCK_SHA256 = "31fbd1dbbe7706183f09a3688482791255db2bfb557c02675374b96796ce2ebd"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game_exe", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    data = bytearray(args.game_exe.read_bytes())
    if hashlib.sha256(data).hexdigest() != STOCK_SHA256:
        parser.error("this host requires the verified Russian Steam 8.1 executable")

    image = pefile.PE(data=bytes(data))
    dependency = next(d for d in image.DIRECTORY_ENTRY_IMPORT if d.dll.lower() == b"ht2ach.dll")
    if [symbol.name for symbol in dependency.imports] != [b"Ht2Ach_Attach"]:
        parser.error("unexpected Steam integration import")
    # At the usual handoff from CRT initialization to the Windows UI, enter the
    # worker linked under the same import name. Keep all AI routines in situ.
    name = image.get_offset_from_rva(dependency.struct.Name)
    data[name : name + 10] = b"aiqa81.dll"
    handoff = image.get_offset_from_rva(0x636B51 - image.OPTIONAL_HEADER.ImageBase)
    data[handoff : handoff + 6] = b"\xff\x25" + struct.pack("<I", dependency.imports[0].address)
    with args.output.open("xb") as output:
        output.write(data)


if __name__ == "__main__":
    main()
