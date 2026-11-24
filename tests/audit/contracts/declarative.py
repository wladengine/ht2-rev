"""Compare callable x86 PE procedures using declarative memory and register fixtures.

Arena offsets are relative to the existing offline probe. Global addresses refer
to Steam 8.1 and are translated through the three canonical game data sections.
"""
import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

import pefile
from unicorn import UC_HOOK_MEM_WRITE
from unicorn import x86_const as x

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "call100"))
from compare import A, BASE, STACK, STOP, Image, Probe, sha


def locate(image, address):
    if isinstance(address, int):
        if not 0 <= address < 0x20000:
            raise ValueError(f"Arena offset out of range: {address}")
        return A + address
    return image.data(int(address["global"], 16))


def encode(field):
    fmt, values = field
    return struct.pack("<" + fmt, *values)


def word(image, value):
    return locate(image, value["at"]) if isinstance(value, dict) else value


def observe_calls(probe, image, services, expected, failures):
    seen = []

    def observe(name, args):
        index = len(seen)
        seen.append(name)
        if index >= len(expected) or expected[index]["service"] != name:
            raise ValueError(f"Unexpected service {name} at call {index}")
        call = expected[index]
        for i, value in enumerate(call.get("args", [])):
            if value is not None and args[i] != word(image, value):
                failures.append({"service": name, "argument": i, "expected": word(image, value), "actual": args[i]})
        for index, offset, encoded in call.get("expect_at_arg", []):
            expected_bytes = bytes.fromhex(encoded)
            actual = bytes(probe.uc.mem_read(args[index] + offset, len(expected_bytes)))
            if actual != expected_bytes:
                failures.append({"service": name, "argument": index, "offset": offset,
                                 "expected": encoded, "actual": actual.hex()})
        for index, data in call.get("write_to_arg", []):
            if data:
                probe.put(args[index], bytes.fromhex(data))
        return word(image, call["return"]) if "return" in call else None

    for name, service in services.items():
        def call(args, name=name):
            return observe(name, args)

        pop = service.get("pop", 0)
        if "callback_offset" in service:
            probe.callbacks[STOP + service["callback_offset"]] = (call, pop)
        else:
            probe.hook(int(service["original_va"], 16), service["symbol"], call, pop)
    return seen


def index_map(path, exe):
    text = path.read_text()
    pe = pefile.PE(str(exe), fast_load=True)
    if f"Timestamp is {pe.FILE_HEADER.TimeDateStamp:08x}" not in text:
        raise ValueError("Candidate and MAP timestamps differ")
    result = {}
    for name, addr in re.findall(r"^\s+\w+:\w+\s+(\S+)\s+([\da-f]{8})\s+", text, re.M):
        va = int(addr, 16)
        if name in result and result[name] != va:
            result[name] = None
        else:
            result[name] = va
    return result


def observe(image, entry, spec, case):
    probe = Probe(image, spec["symbol"])
    probe.result = spec["result"]
    probe.ecx = locate(image, spec.get("this_offset", 0))
    probe.args = [word(image, value) for value in case.get("args", [])]
    for addr, *field in spec.get("initial", []) + case.get("initial", []):
        probe.put(locate(image, addr), encode(field))
    # Only normalize pointers whose targets the fixture independently specifies.
    for addr, target_va in case.get("expect_image_pointers", []):
        probe.fixups[locate(image, addr)] = int(target_va, 16)

    failures = []
    calls = case.get("calls", [])
    seen = observe_calls(probe, image, spec.get("services", {}), calls, failures)
    written = set()

    def track_write(uc, kind, va, size, value, data):
        # A compiler-generated SEH frame temporarily changes fs:[0]. The
        # caller's chain head is compared explicitly after the return.
        if va == 0 and size == 4 and uc.mem_read(uc.reg_read(x.UC_X86_REG_EIP), 1) == b"\x64":
            return
        if not STACK - 0x20000 <= va < STACK + 0x24000:
            written.update(range(va, va + size))

    probe.uc.hook_add(UC_HOOK_MEM_WRITE, track_write)
    result = probe.run(entry)
    if result["stack"] != spec["stack"] or probe.read(0) != 0xffffffff:
        raise ValueError("Stack or SEH chain changed")
    regions = [(A, A + 0x20000, A)]
    for lo, hi in [(0x64b000, 0x66c000), (0x66c000, 0x68a820), (0x68a820, 0x77fb04)]:
        start = image.data(lo)
        regions.append((start, start + hi - lo, lo))
    footprint = []
    for address in sorted(written):
        for start, end, original in regions:
            if start <= address < end:
                footprint.append((original + address - start, probe.uc.mem_read(address, 1)[0]))
                break
        else:
            raise ValueError(f"Write outside declared state at {address:#x}")
    fp = probe.uc.reg_read(x.UC_X86_REG_FPTAG)
    registers = [probe.uc.reg_read(x.UC_X86_REG_FP0 + i) for i in range(8) if ((fp >> (2 * i)) & 3) != 3]
    env = [fp, probe.uc.reg_read(x.UC_X86_REG_FPCW), probe.uc.reg_read(x.UC_X86_REG_FPSW),
           probe.uc.reg_read(x.UC_X86_REG_MXCSR), probe.uc.reg_read(x.UC_X86_REG_EFLAGS) & 0x400, registers]
    if len(seen) != len(calls):
        failures.append({"expected_services": [call["service"] for call in calls], "actual": seen})
    if "expect_result" in case and result["result"] != word(image, case["expect_result"]):
        failures.append({"expected_result": word(image, case["expect_result"]), "actual": result["result"]})
    for slot, value in enumerate(case.get("expect_stack_args", [])):
        actual = probe.read(STACK + 4 * (slot + 1))
        if value is not None and actual != word(image, value):
            failures.append({"stack_arg": slot, "expected": word(image, value), "actual": actual})
    for addr, *field in case.get("expect", []):
        expected = encode(field)
        actual = bytes(probe.uc.mem_read(locate(image, addr), len(expected)))
        if expected != actual:
            failures.append({"at": addr, "expected": expected.hex(), "actual": actual.hex()})
    return dict(arena=result["arena"], result=result["result"], env=env, checks=failures,
                writes=hashlib.sha256(repr(sorted(footprint)).encode()).hexdigest())


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ("original", "candidate", "map", "fixture", "out"):
        ap.add_argument("--" + name, type=Path, required=True)
    args = ap.parse_args()
    spec = json.loads(args.fixture.read_text())
    if not spec["cases"]:
        raise ValueError("A fixture must contain at least one case")
    if sha(args.original) != spec["original_sha256"]:
        raise ValueError("Unexpected original EXE")
    symbols = index_map(args.map, args.candidate)
    entry = symbols[spec["symbol"]]
    if entry is None:
        raise ValueError("Ambiguous MAP symbol")
    sides = [(Image(args.original), int(spec["original_va"], 16)), (Image(args.candidate, symbols), entry)]
    results = []
    for case in spec["cases"]:
        observations = [observe(im, va, spec, case) for im, va in sides]
        if observations[0]["checks"]:
            raise ValueError(f"Original disagrees with stated contract: {case['name']} {observations[0]['checks']}")
        results.append({"case": case["name"], "match": observations[0] == observations[1], "observations": observations})
    report = dict(original=sha(args.original), candidate=sha(args.candidate), map=sha(args.map),
                  fixture=sha(args.fixture), results=results)
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    print(f"{sum(row['match'] for row in results)} / {len(results)} matched")
    return int(any(not row["match"] for row in results))


if __name__ == "__main__":
    sys.exit(main())
