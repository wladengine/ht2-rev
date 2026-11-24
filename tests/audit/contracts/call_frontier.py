"""Typed fixtures for successive direct-call groups in the Steam 8.1 audit."""

import argparse
from collections import Counter
import hashlib
import json
import math
import re
import struct
import sys
from pathlib import Path

import pefile
import capstone

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "call100"))
from cases import A, B, C, D, E, f32
from compare import Image, Probe, sha, u32
from queue_and_five import ENTRIES as NEXT_ENTRIES
from four_sites import ENTRIES as FOUR_ENTRIES
from sort_frontier import ENTRIES as SORT_ENTRIES
from scene_frontier import ENTRIES as SCENE_ENTRIES
from three_sites import ENTRIES as THREE_ENTRIES
from three_sites import expected_difference
from physics_effects import ENTRIES as PHYSICS_EFFECTS
from backdrop_and_water import ENTRIES as BACKDROP_AND_WATER


def town(p, n, writes):
    flags = [0, 1, 2, 3, 0x81234562]
    p.g(0x68CEC0, B)
    for i, value in enumerate(flags):
        rec = C + (len(flags) - i) * 0x100
        p.w(B + 4 * i, rec)
        p.w(rec + 92, 0x44444444, value, 0xFFFFFFFF)
    p.args = [n]
    p.result = "eax"
    return flags[n]


def clear(p, n, writes):
    # Exercise both a shared live key and the destruction of its final reference.
    buckets = [[], [[], [], []], [[0]], [[], [0, 1], [], [2]], [[0, 1]]][n]
    count = sum(map(len, buckets))
    p.w(A, 0x1234, B, B + 4 * len(buckets), B + 32, count or 17)
    p.w(B + 4 * len(buckets), 0xFACE)
    final_reference = n == 4
    p.w(E, 3, count + (not final_reference), 0, 0, D, D)
    p.put(D, b"key\0")
    free = D + 128
    p.g(0x68D1C8, free)
    p.g(0x68D204, 0)

    def exchange(args):
        slot, value = args[:2]
        assert slot == p.image.data(0x68D204)
        before = p.read(slot)
        p.w(slot, value)
        return before

    def decrement(args):
        assert args[0] == E + 4
        v = p.read(E + 4) - 1
        p.w(E + 4, v)
        return v

    p.api("InterlockedExchange", exchange, 8)
    p.api("InterlockedDecrement", decrement, 4)
    for i, bucket in enumerate(buckets):
        nodes = [C + 16 * j for j in bucket]
        p.w(B + 4 * i, nodes[0] if nodes else 0)
        writes[B + 4 * i] = u32(0)
        for j, node in enumerate(nodes):
            p.w(node, nodes[j + 1] if j + 1 < len(nodes) else 0, E, 100 + j)
            writes[node] = u32(free)
            free = node
    writes[A + 16] = u32(0)
    writes[E + 4] = u32(0 if final_reference else 1)
    p.expected_globals = {0x68D1C8: free, 0x68D204: 0}
    if final_reference:
        p.g(0x68D1C4, 0)
        p.g(0x68D1CC, 0)
        writes[D] = writes[E] = u32(0)
        p.expected_globals.update({0x68D1C4: D, 0x68D1CC: E})


def iterator(p, n, writes):
    delta = [0, 1, -1, 7, -11][n]
    p.w(A, C)
    p.args = [A, delta]
    writes[A] = u32(C + 4 * delta)


def merge(p, n, writes, kind):
    keys, step = [
        ([], 1),
        ([2], 4),
        ([1, 3, 1, 4], 2),
        ([-2, 3, 2, 7, 4, 8, 0], 2),
        ([4, 4, 4, 4, 4], 1),
    ][n]
    width = 8 if kind == "double" else 4
    indices = list(range(len(keys)))
    if kind == "near":
        p.g(0x68CF00, E)
        p.g(0x68B990, 0)
        p.w(D, D + 128, D + 164)
        p.w(D + 128, 0, 0, C)
        p.w(E, E + 512)
    values = []
    for i, k in enumerate(keys):
        rec = C + 128 * i
        if kind == "double":
            # Opposite signed zeros distinguish equal-key operands.
            value = -float(k) if n != 4 else (0.0 if i % 2 else -0.0)
            raw = struct.pack("<d", value)
        else:
            raw = u32(rec)
            p.w(rec + 28, k, ~k)
            if kind == "near":
                p.w(rec, i + 1)
                road = E + 1024 + 128 * i
                p.w(E + 4 * (i + 1), road)
                p.put(road + 48, struct.pack("<ddd", 10.0 + k, 0, 0))
        values.append(raw)
    p.put(A, b"".join(values))
    p.put(B - 4, b"\xca" * (8 + width * len(keys)))
    p.args = [A, A + width * len(keys), B, step]
    if kind == "near":
        p.args.append(D)
    order = []
    for i in range(0, len(keys), 2 * step):
        order.extend(sorted(indices[i : i + 2 * step], key=lambda j: keys[j]))
    writes[B] = b"".join(values[j] for j in order)


def reverse(p, n, writes):
    items = [struct.pack("<III", i, i + 55, 3 * i) for i in range(n)]
    p.put(A, b"".join(items))
    p.args = [A, A + 12 * n]
    writes[A] = b"".join(reversed(items))


def grip(p, n, writes):
    count, mask, auxiliary = [
        (2, 0, 0),
        (2, 3, 0),
        (4, 5, 10),
        (4, 10, 0),
        (6, 63, 0),
        (6, 48, 0),
    ][n]
    wheel = [(mask >> i) & 1 for i in range(6)]
    contact = [(auxiliary >> i) & 1 for i in range(4)]
    p.w(A + 3540, count)
    p.put(A + 6596, bytes(3 * v for v in wheel))
    p.put(A + 6564, struct.pack("<4h", *contact))
    old = bytearray([33] * 9)
    p.put(A + 6587, old)
    p.w(A + 6644, 112, 129)
    old[0:4] = bytes(
        [any(wheel[:count]), all(wheel[:count]), all(wheel[:count]), all(wheel[:2])]
    )
    if count >= 4:
        old[4:7] = bytes(
            [all(wheel[2:4]), wheel[0] and wheel[2], wheel[1] and wheel[3]]
        )
        old[8] = all(wheel[i] or contact[i] for i in (2, 3))
    old[7] = all(wheel[i] or contact[i] for i in (0, 1))
    writes[A + 6587] = old
    if old[7]:
        writes[A + 6644] = u32(0)
    if old[8]:
        writes[A + 6648] = u32(0)


def matrix(p, n, writes):
    p.ecx = B + n * 4
    p.put(p.ecx - 8, bytes(range(144)))
    writes[p.ecx] = bytes(128)
    p.result = "eax"
    return p.ecx


def car(p, n, writes):
    p.g(0x6EF1F8, [0, B + 4, B + 4, B + 4, B + 4, 4][n])
    p.w(B + 8, C + 4)
    p.w(C + 8, 4)
    p.args = [[B + 16, 0, B + 16, C + 16, D + 16, B + 16][n]]
    p.result = "eax"
    return [0, 0, B, C, 0, 0][n]


def mode(p, n, writes):
    old, new, inhibit = [(1, 1, 0), (4, 2, 0), (2, 4, 0), (2, 1, 1), (1, 2, 0)][n]
    p.w(A + 10924, old)
    p.w(A + 20908, inhibit)
    p.w(A + 21600, E)
    p.w(E + 9784, 33)
    p.g(0x702650, 0, 0, 0)
    for offset in (20688, 20712):
        p.w(A + offset, 11, 22, 33)
        if new == 2:
            writes[A + offset] = bytes(12)
    if old == 1 and new != 1:
        writes[E + 9784] = u32(0)
    writes[A + 10924] = u32(new)
    p.args = [new]


def plane(p, n, writes):
    normal = [(2, -1, 3), (0, 1, 0), (1, 0, 0), (1, -1, 1)][n]
    point = (4, -2, 8)
    p.put(A, struct.pack("<3f", *normal))
    p.put(B, struct.pack("<3f", *point))
    dst = [C, A, B, C][n]
    writes[dst] = struct.pack(
        "<4f", *normal, -float(sum(x * y for x, y in zip(normal, point)))
    )
    p.args = [A, B, dst]


def cursor(p, n, writes):
    handle = 777 if n else 0
    p.g(0x720920, handle)
    p.args = [12, -15]

    def transform(args):
        h, pt = args[:2]
        assert h == handle and [p.read(pt), p.read(pt + 4)] == [12, 0xFFFFFFF1]
        if n == 1:
            p.w(pt, 112, 185)
        p.events.append("convert")
        return int(n == 1)

    def move(args):
        assert args[:2] == ([112, 185] if n == 1 else [12, 0xFFFFFFF1])
        p.events.append("move")
        return 1

    p.api("ClientToScreen", transform, 8)
    p.api("SetCursorPos", move, 8)
    p.expected_calls = ["convert", "move"] if n else []


def angle(p, n, writes):
    value = [0.0, -0.0, 1.0, 2.0, -4.0][n]
    p.w(A + 84, f32(99), f32(value))
    p.result = "fp"
    return 2 * math.atan(value * 0.5)


ENTRIES = [
    (0x440950, "_Town_GetMapFlags", town, 5),
    (0x44FE20, "@AiNameHash_ClearEntries@8", clear, 5),
    (0x469DC0, "_DwordIterator_Advance", iterator, 5),
    (0x46A010, "_Sort_MergeLoop28", lambda *args: merge(*args, "int"), 5),
    (0x46A8C0, "_Near_MergeLoop", lambda *args: merge(*args, "near"), 5),
    (0x470860, "_E12_Reverse", reverse, 6),
    (0x47E810, "_Doubles_MergeLoop", lambda *args: merge(*args, "double"), 5),
    (0x4F2490, "@Body_UpdateGrip@8", grip, 6),
    (0x50B4F0, "@DMat_Zero@8", matrix, 3),
    (0x53F780, "_Car_InListFrom16", car, 6),
    (0x54CA60, "@Car_SetInMode@12", mode, 5),
    (0x59F7C0, "_Plane_ThroughPoint", plane, 4),
    (0x5ACAF0, "_Input_SetCursorPos", cursor, 3),
    (0x5EB490, "@MarineCamera_HorizontalAngle@8", angle, 5),
]


def run(img, entry, sym, setup, n):
    p = Probe(img, sym)
    p.result = "void"
    writes = {}
    result = setup(p, n, writes)
    expected = bytearray(p.uc.mem_read(A, 0x20000))
    for loc, data in writes.items():
        expected[loc - A : loc - A + len(data)] = data
    observed = p.run(entry)
    actual = observed["result"]
    if p.result == "fp":
        mantissa, exponent = actual
        actual = math.ldexp(float(mantissa), (exponent & 0x7FFF) - 16383 - 63)
        if exponent & 0x8000:
            actual = -actual
        assert math.isclose(actual, result, rel_tol=1e-15), (sym, n, actual, result)
    else:
        assert actual == result, (sym, n, actual, result)
    assert observed["stack"] == getattr(p, "expected_stack", 8 if setup is mode else 4)
    assert observed["arena"] == hashlib.sha256(expected).hexdigest(), (sym, n, "memory")
    for va, value in getattr(p, "expected_globals", {}).items():
        assert p.read(img.data(va)) == value
    assert p.events == getattr(p, "expected_calls", [])
    return observed


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for k in ("original", "candidate", "map", "out"):
        ap.add_argument("--" + k, type=Path, required=True)
    group = ap.add_mutually_exclusive_group()
    group.add_argument("--three-sites", action="store_true", help="Run the first three-site group separately")
    group.add_argument("--physics-effects", action="store_true", help="Run the next three-site group separately")
    args = ap.parse_args()
    assert (
        sha(args.original)
        == "31fbd1dbbe7706183f09a3688482791255db2bfb557c02675374b96796ce2ebd"
    )
    mapping = args.map.read_text()
    assert (
        f"Timestamp is {pefile.PE(str(args.candidate), fast_load=True).FILE_HEADER.TimeDateStamp:08x}"
        in mapping
    )
    syms = {
        k: int(v, 16)
        for k, v in re.findall(r"^\s+\w+:\w+\s+(\S+)\s+([\da-f]{8})\s+", mapping, re.M)
    }
    left, right = Image(args.original), Image(args.candidate, syms)
    rows = []
    differences = []
    pe = pefile.PE(str(args.original), fast_load=True)
    section = next(sec for sec in pe.sections if sec.Name.rstrip(b"\0") == b".text")
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.skipdata = True
    sites = Counter(
        int(dest, 16)
        for _, _, op, dest in md.disasm_lite(
            section.get_data()[: section.Misc_VirtualSize],
            pe.OPTIONAL_HEADER.ImageBase + section.VirtualAddress,
        )
        if op == "call" and dest.startswith("0x")
    )
    targets = ENTRIES + NEXT_ENTRIES + FOUR_ENTRIES + SORT_ENTRIES + SCENE_ENTRIES
    if args.three_sites:
        targets = THREE_ENTRIES
    if args.physics_effects:
        targets = PHYSICS_EFFECTS + BACKDROP_AND_WATER
    assert len(targets) == len({t[0] for t in targets})
    for va, sym, setup, count in sorted(targets, key=lambda t: (-sites[t[0]], t[0])):
        cases = []
        for n in range(count):
            a = run(left, va, sym, setup, n)
            b = run(right, syms[sym], sym, setup, n)
            field = expected_difference(va, n) if args.three_sites else None
            different = {k for k in a.keys() | b.keys() if a.get(k) != b.get(k)}
            assert different == ({field} if field else set()), (hex(va), n, a, b)
            if field:
                differences.append(dict(stock_va=f"{va:06x}", case=n, field=field,
                                        original=a[field], canonical=b[field]))
            cases.append(
                hashlib.sha256(json.dumps(a, sort_keys=True).encode()).hexdigest()
            )
        rows.append(
            dict(stock_va=f"{va:06x}", sites=sites[va], symbol=sym, cases=cases)
        )
        print(f"{va:06x} {sym}: {count} PASS", flush=True)
    # A real, same-ABI getter at +12 must fail the data oracle for the flags at +96.
    try:
        run(right, syms["_Town_MapLabel"], "_Town_MapLabel", town, 4)
    except AssertionError as error:
        assert error.args == (("_Town_MapLabel", 4, 0, 0x81234562),)
    else:
        raise AssertionError("negative control accepted the wrong town field")
    # The old candidate for the edge selector compares the node score at +28.
    setup = next(t[2] for t in NEXT_ENTRIES if t[0] == 0x60F9D0)
    wrong = "@MarineQueue_TopDeferredNode@8"
    try:
        run(right, syms[wrong], wrong, setup, 4)
    except AssertionError as error:
        assert error.args == ((wrong, 4, C, B),)
    else:
        raise AssertionError("negative control accepted the wrong priority selector")
    controls = ["wrong town field rejected", "wrong priority selector rejected"]
    if args.physics_effects:
        setup = next(t[2] for t in BACKDROP_AND_WATER if t[0] == 0x612010)
        wrong = "@MarineGrid_MapY@12"
        try:
            run(right, syms[wrong], wrong, setup, 0)
        except AssertionError as error:
            assert error.args == ((wrong, 0, 0, 4),)
        else:
            raise AssertionError("negative control accepted Y for X")
        controls.append("wrong grid coordinate rejected")
    if args.three_sites:
        setup = next(t[2] for t in THREE_ENTRIES if t[0] == 0x465300)
        wrong = "_Sort_Stable28"
        try:
            run(right, syms[wrong], wrong, setup, 2)
        except AssertionError as error:
            assert error.args == ((wrong, 2, "memory"),)
        else:
            raise AssertionError("negative control accepted the wrong image sort key")
        controls.append("wrong image sort key rejected")
    args.out.write_text(
        json.dumps(
            dict(
                original=sha(args.original),
                candidate=sha(args.candidate),
                map=sha(args.map),
                fixtures={
                    f: sha(Path(__file__).with_name(f))
                    for f in (
                        "call_frontier.py",
                        "queue_and_five.py",
                        "four_sites.py",
                        "sort_frontier.py",
                        "scene_frontier.py",
                        "three_sites.py",
                        "physics_effects.py",
                        "backdrop_and_water.py",
                    )
                },
                probe=sha(Path(__file__).resolve().parents[1] / "call100/compare.py"),
                negative_controls=controls,
                rows=rows,
                differences=differences,
            ),
            indent=2,
        )
        + "\n"
    )


if __name__ == "__main__":
    main()
