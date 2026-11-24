"""Compare returning PE32 functions on a caller-supplied JSON fixture."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn import x86_const as reg

from binary_state import MemoryObservation, x86_environment


def number(value):
    return int(value, 0) if isinstance(value, str) else int(value)


def run(path, case, side):
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != case["sha256"][side]:
        raise ValueError(f"SHA-256 mismatch for {path}")
    pe = pefile.PE(data=raw)
    if pe.FILE_HEADER.Machine != 0x14C or pe.OPTIONAL_HEADER.Magic != 0x10B:
        raise ValueError("A PE32 x86 image is required")
    cpu = Uc(UC_ARCH_X86, UC_MODE_32)
    image = pe.OPTIONAL_HEADER.ImageBase
    size = pe.OPTIONAL_HEADER.SizeOfImage
    cpu.mem_map(image, (size + 4095) & ~4095)
    cpu.mem_write(image, pe.get_memory_mapped_image())
    for library in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
        for entry in library.imports:
            cpu.mem_write(entry.address, bytes(4))

    arena = 0x1000000
    stack = 0x2100000
    stop = 0x3100000
    cpu.mem_map(arena, 0x20000)
    cpu.mem_map(stack - 0x10000, 0x20000)
    cpu.mem_map(stop, 4096)
    args = [number(n) for n in case["args"]]
    cpu.mem_write(stack, struct.pack(f"<{len(args) + 1}I", stop, *args))
    cpu.reg_write(reg.UC_X86_REG_ESP, stack)
    for name in ("ecx", "edx"):
        cpu.reg_write(getattr(reg, "UC_X86_REG_" + name.upper()), number(case.get(name, 0)))
    saved = [reg.UC_X86_REG_EBX, reg.UC_X86_REG_ESI, reg.UC_X86_REG_EDI, reg.UC_X86_REG_EBP]
    for i, register in enumerate(saved):
        cpu.reg_write(register, 0x11550000 + i)
    cpu.reg_write(reg.UC_X86_REG_FPCW, 0x027F)
    cpu.reg_write(reg.UC_X86_REG_FPTAG, 0xFFFF)

    regions = {}
    for region in case.get("globals", []):
        name = region["name"]
        address = number(region["address"][side])
        data = bytes.fromhex(region["hex"])
        if not data or name == "$arena" or name in regions or not image <= address <= image + size - len(data):
            raise ValueError(f"Invalid global region {name}")
        if any(start < address + len(data) and address < end for start, end, _ in regions.values()):
            raise ValueError(f"Overlapping global region {name}")
        cpu.mem_write(address, data)
        regions[name] = (address, address + len(data), region.get("observe_reads", False))
    for item in case.get("objects", []):
        offset = number(item["offset"])
        data = bytes.fromhex(item["hex"])
        if not 0 <= offset <= 0x20000 - len(data):
            raise ValueError("Object lies outside the fixture arena")
        cpu.mem_write(arena + offset, data)

    watches = []
    observation = {}
    ordered = case.get("observe_store_order", False)
    try:
        for name, (start, end, reads) in regions.items():
            watches.append((name, MemoryObservation(cpu, start, end - start, track_reads=reads,
                                                    track_store_order=ordered)))
        arena_watch = MemoryObservation(cpu, arena, 0x20000, track_store_order=ordered)
        watches.append(("$arena", arena_watch))
        image_watch = MemoryObservation(cpu, image, size)
        try:
            cpu.emu_start(number(case["entry"][side]), stop, count=number(case.get("budget", 50000)))
            if cpu.reg_read(reg.UC_X86_REG_EIP) != stop:
                raise ValueError("Function did not return within the instruction budget")
            image_stores, _ = image_watch.finish()
            for offset in image_stores:
                if not any(start <= image + offset < end for start, end, _ in regions.values()):
                    raise ValueError(f"Undeclared image write at {image + offset:#x} in {path.name}")
        finally:
            image_watch.close()
        for name, watch in watches:
            stores, changes = watch.finish()
            observation[name] = dict(stores=sorted(stores), changes=changes, reads=sorted(watch.read))
            if ordered:
                observation[name]["store_order"] = watch.store_order
    finally:
        for _, watch in watches:
            watch.close()
    if any(cpu.reg_read(r) != 0x11550000 + i for i, r in enumerate(saved)):
        raise ValueError("Callee-saved register changed")
    env = x86_environment(cpu)
    results = {
        "void": None,
        "eax": cpu.reg_read(reg.UC_X86_REG_EAX),
        "al": cpu.reg_read(reg.UC_X86_REG_EAX) & 255,
        "st0": cpu.reg_read(reg.UC_X86_REG_FP0 + env["x87_top"]),
    }
    return dict(result=results[case["result"]], stack_delta=cpu.reg_read(reg.UC_X86_REG_ESP) - stack,
                environment=env, memory=observation)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("reference", "candidate", "map", "fixture"):
        parser.add_argument(key, type=Path)
    args = parser.parse_args()
    case = json.loads(args.fixture.read_text(encoding="utf-8-sig"))
    reference_hash = "31fbd1dbbe7706183f09a3688482791255db2bfb557c02675374b96796ce2ebd"
    candidate = pefile.PE(str(args.candidate), fast_load=True)
    mapping = args.map.read_text()
    stamp = re.search(r"Timestamp is ([0-9a-fA-F]+)", mapping)
    if not stamp or int(stamp[1],16) != candidate.FILE_HEADER.TimeDateStamp:
        parser.error("Map timestamp differs from the candidate PE")
    symbols = {m[1]: int(m[2], 16) for m in re.finditer(
        r"^\s*\w+:\w+\s+(\S+)\s+([0-9a-fA-F]{8})\s", mapping, re.M)}
    case["entry"] = [case["entry"], symbols[case["symbol"]]]
    case["sha256"] = [reference_hash, hashlib.sha256(args.candidate.read_bytes()).hexdigest()]
    for region in case.get("globals", []):
        stock = number(region["address"])
        if not 0x68a820 <= stock < 0x780000:
            parser.error("Expected a game BSS fixture")
        region["address"] = [stock, symbols["_KingBss"] + stock - 0x68a820]
    left = run(args.reference, case, 0)
    right = run(args.candidate, case, 1)
    print(json.dumps(dict(equal=left == right, reference=left, candidate=right), indent=2))
    raise SystemExit(0 if left == right else 1)
