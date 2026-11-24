"""Exercise sort and merge specializations on equal keys with distinct record identities."""

import struct

from cases import A, B, C, D, E
from compare import u32


def sorting(p, n, writes, kind, operation):
    width = 8 if kind == "double" else 4
    keys = [[], [3], [2, 0, 2, 0, 1, 4, 2], [(i * 17) % 11 - 5 for i in range(35)]][n]
    size = len(keys)
    if kind == "near" and operation == "sort" and size == 35:
        p.instruction_budget = 100000  # Repeated node lookups during insertion sort.
    middle = (size + 1) // 2
    ids = list(range(size))
    if operation != "sort":
        ids = sorted(ids[:middle], key=keys.__getitem__) + sorted(
            ids[middle:], key=keys.__getitem__
        )
    result = sorted(ids, key=keys.__getitem__)
    raw = []
    if kind == "near":
        p.g(0x68CF00, E)
        p.g(0x68B990, 0)
        p.w(D, D + 128, D + 164)
        p.w(D + 128, 0, 0, C)
        p.w(E, E + 512)
    for i, k in enumerate(keys):
        if kind == "double":
            value = -float(k) if k or i % 2 else 0.0
            raw.append(struct.pack("<d", value))
        else:
            rec = C + i * 64
            raw.append(u32(rec))
            p.w(rec + 28, k if kind == "28" else -k, k if kind == "32" else -k)
            if kind == "near":
                p.w(rec, i + 1)
                node = E + 1024 + i * 128
                p.w(E + 4 * (i + 1), node)
                p.put(node + 48, struct.pack("<3d", 10.0 + k, 0.0, 0.0))
    p.put(A, b"".join(raw[i] for i in ids))
    last = A + size * width
    cut = A + middle * width
    if operation == "sort":
        p.args = [A, last, D]
    elif operation == "merge":
        p.args = [A, cut, last, middle, size - middle, D]
    else:
        p.args = [A, cut, cut, last, B, D]
    dest = B if operation == "out" else A
    writes[dest] = b"".join(raw[i] for i in result)
    if operation == "out":
        p.result = "eax"
        return B + size * width


def fill(p, n, writes):
    count = [0, 1, 8][n]
    p.w(C, 0xABCDEF01)
    p.args = [B, count, C]
    writes[B] = u32(0xABCDEF01) * count
    p.result = "eax"
    return B + count * 4


def backward(p, n, writes, width):
    count = [0, 1, 5, 5][n]
    data = bytes(i % 197 for i in range(count * width))
    p.put(A, data)
    dest = A + width if n == 3 else B
    writes[dest] = data
    p.args = [A, A + len(data), dest + len(data)]
    p.result = "eax"
    return dest


def sorter(kind, op):
    return lambda *args: sorting(*args, kind, op)


ENTRIES = [
    (0x464E20, "_Sort_Stable28", sorter("28", "sort"), 4),
    (0x4657C0, "_Near_StableSort", sorter("near", "sort"), 4),
    (0x467CF0, "_Sort_InsertionBy28", sorter("28", "sort"), 4),
    (0x467D70, "_Merge28", sorter("28", "merge"), 4),
    (0x468210, "_Sort_InsertionBy32", sorter("32", "sort"), 4),
    (0x468710, "_Near_Insertion", sorter("near", "sort"), 4),
    (0x4687F0, "_Near_Merge", sorter("near", "merge"), 4),
    (0x4693E0, "_FillDwords", fill, 3),
    (0x46A1A0, "_Sort_MergeBy28", sorter("28", "out"), 4),
    (0x46A550, "_Sort_MergeBy32", sorter("32", "out"), 4),
    (0x46AA80, "_Sort_MergeByNearest", sorter("near", "out"), 4),
    (0x46FB20, "_E12_CopyBackward", lambda *a: backward(*a, 12), 4),
    (0x47D520, "_Doubles_MergeNoBuf", sorter("double", "merge"), 4),
    (
        0x47E150,
        "?R40_CopyBackward@@YAPAURec40@@PBU1@0PAU1@@Z",
        lambda *a: backward(*a, 40),
        4,
    ),
]
