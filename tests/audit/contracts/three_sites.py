"""First unbound entries with three direct sites, in Steam address order."""

import math
import struct

from cases import A, B, C, D, E, dbl
from compare import u32
from four_sites import road_id
from sort_frontier import sorter
from unicorn import x86_const as x


def move_words(p, n, writes):
    src, dst, length = [(A, B, 0), (A, B, 4), (A, B, 15), (A, A, 16),
                        (A, A + 4, 20), (A + 4, A, 20)][n]
    p.put(A, bytes(range(32)))
    p.put(B, bytes([0xAA]) * 24)
    p.args = [src, src + length, dst]
    size = length // 4 * 4
    writes[dst] = bytes(p.uc.mem_read(src, size)) if size else b""
    p.result = "eax"
    return dst + size


def proximity(p, n, writes):
    first, second = [
        ((0, 0, 0), (5, 0, 0)), ((-1, 0, 0), (0, 2, 0)),
        ((0, 0, 10), (3, -4, 0)), ((3, 4, 0), (0, -10, 0)),
        ((400, 0, 0), (401, 0, 0)), ((0, -401, 0), (0, 0, 402)),
        ((0, 0, math.nan), (500, 0, 0)), ((math.inf, 0, 0), (0, 0, 8)),
        ((1e154, 0, 0), (1e154, 0, 0)), ((1e155, 0, 0), (1e155, 0, 0)),
        ((1e200, 0, 0), (1e200, 0, 0)),
    ][n]
    p.put(A, struct.pack("<6d", *first, *second))
    p.args = [A]
    p.result = "fp"
    def score(position):
        # Canonical passes a binary64 sum of squares to x87_sqrt.
        if p.image.symbols is not None and math.isinf(sum(v * v for v in position)):
            return 0.0
        length = math.hypot(*position)
        if not length >= 1:
            return 1.0
        return 1 / (length * 3 if length > 400 else length)
    return max(score(first), score(second))


def vacancy(p, n, writes):
    count, target = [(0, 0), (0, 1), (5, 6), (6, 6), (7, 6), (3, 0x80000000)][n]
    p.g(0x68CF60, B, B + 4 * count)
    p.g(0x66C6D0, target)
    p.result = "al"
    return int(count < target)


def module_id(p, n, writes):
    answer = [0, 9, -1][n]
    p.put(B, b"module_17\0")
    def lookup(args):
        assert args[1] == B
        p.w(args[0], answer)
        p.events.append("lookup")
    def diagnostic(args):
        level, code, filename, revision, line, prefix, key, empty = args[:8]
        assert (level, code, line, key, empty) == (3, 200, 14688, B, p.image.data(0x68D1C0))
        def string_at(address):
            return bytes(p.uc.mem_read(address, 128)).split(b"\0")[0].decode("ascii")
        assert string_at(prefix) == "Module not found: "
        p.events.append([string_at(filename), string_at(revision), string_at(prefix)])
    p.hook(0x44A1C0, "_Modules_FindByName", lookup)
    p.hook(0x401010, "_AiError", diagnostic)
    p.expected_calls = ["lookup"]
    if answer == -1:
        # Validate the strings supplied to the diagnostic in both images.
        label = lambda addr: bytes(p.uc.mem_read(p.image.data(addr), 128)).split(b"\0")[0].decode("ascii")
        p.expected_calls.append([label(0x66C7B0), label(0x66C558), "Module not found: "])
    p.args = [A, B]
    p.result = "eax"
    writes[A] = u32(answer)
    return A


def load_entry(p, n, writes):
    destination = [A, B, 0][n]
    source = bytes(range(32))
    p.put(A, b"\xbd" * 32)
    p.put(B, source)
    p.args = [destination, B if destination else 0]
    if destination:
        writes[destination] = source[:4]
        writes[destination + 8] = source[8:]


def rope(p, n, writes):
    # A balanced interior piece must be submitted whole, even with children.
    def node(index, balanced, left=0, right=0):
        address = B + 64 * index
        p.w(address + 12, balanced)
        p.w(address + 20, left, right)
        return address
    a, b, c = [node(i, 1) for i in range(3)]
    fork = node(3, 0, a, b)
    root, order = [(a, [a]), (fork, [a, b]), (node(4, 0, fork, c), [a, b, c]),
                   (node(5, 1, fork, c), [B + 320]), (node(6, 0, c, fork), [c, a, b])][n]
    def add(args):
        assert args[1] == D
        p.events.append(args[0])
    p.hook(0x469520, "_Rope_AddLeafToForest", add)
    p.expected_calls = order
    p.args = [root, D]


def buffered_merge(p, n, writes, near=False):
    left, right, capacity, scratch_indices = [
        ([], [], 1, []), ([3], [1], 1, [0]), ([1, 3], [3, 5, 9], 2, [0, 1]),
        ([1, 3, 3], [0, 3], 2, [3, 4]), ([1, 3, 6, 9], [0, 2, 4, 7], 1, [3]),
    ][n]
    keys = left + right
    ptrs = [C + 64 * i for i in range(len(keys))]
    if near:
        p.g(0x68CF00, E)
        p.g(0x68B990, 0)
        p.w(D, D + 128, D + 164)
        p.w(D + 128, 0, 0, C)
        p.w(E, E + 512)
    for ptr, key in zip(ptrs, keys):
        p.w(ptr + 28, key, -key)
    if near:
        for i, (ptr, key) in enumerate(zip(ptrs, keys)):
            p.w(ptr, i + 1)
            node = E + 1024 + 128 * i
            p.w(E + 4 * (i + 1), node)
            p.put(node + 48, struct.pack("<3d", 10 + key, 0, 0))
    p.w(A, *ptrs)
    p.put(B, b"\xfd" * (capacity * 4))
    p.args = [A, A + 4 * len(left), A + 4 * len(keys), len(left), len(right), B, capacity, D]
    order = sorted(range(len(keys)), key=keys.__getitem__)
    writes[A] = b"".join(u32(ptrs[i]) for i in order)
    writes[B] = b"".join(u32(ptrs[i]) for i in scratch_indices)


def rotate(p, n, writes, adaptive):
    left, right, capacity = [(0, 0, 1), (0, 4, 1), (4, 0, 1), (2, 4, 2),
                             (4, 2, 2), (3, 3, 1), (3, 3, 3)][n]
    ptrs = [C + 16 * i for i in range(left + right)]
    p.w(A, *ptrs)
    p.args = [A, A + left * 4, A + len(ptrs) * 4]
    if adaptive:
        p.args.extend([left, right, B, capacity])
        p.put(B, b"\xfd" * (capacity * 4))
        if right < left and right <= capacity:
            writes[B] = b"".join(map(u32, ptrs[left:]))
        elif left <= capacity:
            writes[B] = b"".join(map(u32, ptrs[:left]))
    writes[A] = b"".join(map(u32, ptrs[left:] + ptrs[:left]))
    p.result = "eax"
    return A + 4 * right


def insert_weight(p, n, writes):
    def record(index, weight):
        return struct.pack("<IId4I", index, index + 100, weight, *range(index, index + 4))
    weights = [99.0, 9.0, 5.0, 5.0, 2.0]
    weight = [1.0, 5.0, 6.0, 98.0, math.nan][n]
    rows = [record(i, v) for i, v in enumerate(weights)]
    value = record(17, weight)
    p.put(A, b"".join(rows))
    p.args = [A + 32 * len(rows), *struct.unpack("<8I", value)]
    position = next((i for i, v in enumerate(weights) if weight > v), len(rows))
    rows.insert(position, value)
    writes[A] = b"".join(rows)


def insert_504(p, n, writes):
    keys = [0, 1, 2, 0, -1]
    value, position = [(-1, 5), (0, 4), (1, 2), (2, 3)][n]
    ptrs = [C + i * 640 for i in range(6)]
    for ptr, key in zip(ptrs, keys + [value]):
        p.put(ptr + 504, struct.pack("<d", key))
    p.w(A, *ptrs[:5])
    p.args = [A + 20, ptrs[-1]]
    writes[A] = b"".join(map(u32, ptrs[:position] + ptrs[-1:] + ptrs[position:5]))


def delete_handle(p, n, writes):
    p.args = [n]
    p.ecx = A
    p.expected_stack = 8
    def release(args):
        assert p.uc.reg_read(x.UC_X86_REG_ECX) == A
        p.events.append("release")
    def delete(args):
        assert args[0] == A
        p.events.append("delete")
    p.hook(0x4A36C0, "@SchHandle_Release@8", release)
    p.hook(0x63266C, "??3@YAXPAX@Z", delete)
    p.expected_calls = ["release", "delete"] if n & 1 else ["release"]
    p.result = "eax"
    return A


def swap_record(p, n, writes):
    other = [A, A + 12, B][n]
    p.put(A, bytes(range(24)))
    p.put(B, bytes(range(100, 112)))
    writes[A] = bytes(p.uc.mem_read(other, 12))
    writes[other] = bytes(p.uc.mem_read(A, 12))
    p.args = [A, other]


def order(p, n, writes, margin=False):
    index, coefficient, quantity, fraction = [
        (0, 4, 0, 0), (1, 9, 16, 0), (0, 4, 9, 0.25),
        (1, 25, 4, 1), (0, 9, 16, 0.5), (1, 9, 16, 0), (0, 9, 16, 0),
    ][n]
    p.g(0x68CCB8, B)
    p.put(B + 104 * index + 40, struct.pack("<d", coefficient))
    p.w(A, index)
    p.put(A + 56, struct.pack("<d", quantity))
    p.put(A + 96, struct.pack("<d", fraction))
    p.args = [A]
    p.result = "fp"
    value = math.sqrt(coefficient * quantity) * 10 * (1 - fraction)
    if not margin:
        return value
    price, done, bonus, mode = [(10, 0, 1, 1), (150, 1, 1, 1), (50, 0, 0, 1),
                                (10, 0, 1, 2), (0, 0, 1, 1), (160, 0, 1, 1),
                                (100, 0, 0, 2)][n]
    p.put(A + 64, struct.pack("<d", price))
    p.w(A + 104, mode)
    price_out = C if n % 3 != 1 else 0
    value_out = D if n % 3 != 2 else 0
    p.args.extend([*dbl(done), bonus, price_out, value_out])
    amount = price * 1.5 if bonus and done == 0 and price > 0 and mode == 1 else price
    if price_out:
        writes[C] = struct.pack("<d", amount)
    if value_out:
        writes[D] = struct.pack("<d", value)
    def diagnostic(args):
        assert args[:2] == [0, 200]
        p.events.append(args[4])
    p.hook(0x401010, "_AiError", diagnostic)
    p.expected_calls = [201] if amount - value < -1e-8 else []
    return amount - value


def doubles_merge(p, n, writes):
    left, right, cap, scratch = [
        ([], [], 1, []), ([2, 0.0], [3, -0.0, -1], 2, [2, 0.0]),
        ([4, 3, 0.0], [3, -0.0], 2, [3, -0.0]),
        ([9, 6, 3, 1], [7, 4, 2, 0], 1, [1]),
    ][n]
    def pack(values):
        return b"".join(struct.pack("<d", v) for v in values)
    p.put(A, pack(left + right))
    p.put(B, b"\xfd" * (8 * cap))
    p.args = [A, A + len(left) * 8, A + (len(left) + len(right)) * 8, len(left), len(right), B, cap, 0]
    writes[A] = pack(sorted(left + right, reverse=True))
    writes[B] = pack(scratch)


def scene_switch(p, n, writes):
    module, name, found_module, found_switch, value = [
        (0, B, False, False, 0), (A, 0, False, False, 0),
        (A, B, False, False, 1), (A, B, True, False, 1),
        (A, B, True, True, 0), (A, B, True, True, 7), (A, B, False, False, 1),
    ][n]
    p.put(A, b"dr\0")
    p.put(B, b"StartKey\0" if n != 6 else b"\0")
    p.g(0x696CA0, E)
    p.g(0x71E810, 256, D, 0)
    signature = b"updateSwitch(char const * moduleName, char const * switchName, int v)"
    trace = b"\r\nSTART:" + signature + b"\r\nCOMPL:" + signature
    writes[D] = trace
    p.expected_globals = {0x71E818: len(trace)}
    p.args = [module, name, value]
    def find_module(args):
        assert args[0] == A and p.uc.reg_read(x.UC_X86_REG_ECX) == E
        p.events.append("module")
        return C if found_module else 0
    def find_switch(args):
        assert args[0] == B and p.uc.reg_read(x.UC_X86_REG_ECX) == C
        p.events.append("switch")
        return C + 128 if found_switch else 0
    def select(args):
        assert args[:2] == [C + 128, value]
        p.events.append("select")
    p.hook(0x5D9290, "@Scenes_Find@12", find_module, 4)
    p.hook(0x5EF390, "@Room_FindObject@12", find_switch, 4)
    p.hook(0x5E1A10, "_Switch_Select", select)
    p.expected_calls = (["module"] if module and name and n != 6 else [])
    if found_module:
        p.expected_calls.append("switch")
    if found_switch:
        p.expected_calls.append("select")


def divide_limbs(p, n, writes):
    count, divisor, output = [(0, 7, B), (3, 1, B), (3, 1, A), (3, 17, 0),
                              (3, 17, B), (3, 65535, A), (2, 0x10003, B)][n]
    limbs = [0x12EF, 0xDCBA, 0xFEDC][:count]
    digits = struct.pack(f"<{count}H", *limbs)
    p.put(A, digits)
    p.put(B, b"\xa3" * 8)
    p.args = [A, count, divisor, output]
    quotient, remainder = divmod(int.from_bytes(digits, "little"), divisor & 0xffff)
    if count and divisor & 0xffff != 1 and output:
        writes[output] = quotient.to_bytes(2 * count, "little")
    p.result = "eax"
    return remainder


# Each differing field still has a separate expected result in the fixture.
def expected_difference(address, case):
    if address == 0x4244C0 and case in (9, 10):
        return "result"
    return None


ENTRIES = [
    (0x404260, "_MoveDwordRange", move_words, 6),
    (0x4244C0, "_Ai_EntityLoadProximityWeight", proximity, 11),
    (0x425B20, "_Ring_GridHasVacancy", vacancy, 6),
    (0x4362A0, "_Town_FirstId", lambda *a: road_id(*a, False, 0x68CEC0), 6),
    (0x4362D0, "_Town_NextId", lambda *a: road_id(*a, True, 0x68CEC0), 6),
    (0x436570, "_Module_ByName", module_id, 3),
    (0x461640, "_LoadEntry_CopyConstruct", load_entry, 3),
    (0x465300, "_AiImage_StableSortByKey32", sorter("32", "sort"), 4),
    (0x466740, "_Rope_AddSubtreeToForest", rope, 5),
    (0x467FE0, "_Merge28_Adaptive", buffered_merge, 5),
    (0x468290, "_Merge32", sorter("32", "merge"), 4),
    (0x468AB0, "_Near_MergeAdaptive", lambda *a: buffered_merge(*a, near=True), 5),
    (0x469DE0, "_Ptrs_Rotate", lambda *a: rotate(*a, False), 7),
    (0x46A220, "_Ptrs_RotateAdaptive", lambda *a: rotate(*a, True), 7),
    (0x46AB20, "_Sort_UnguardedInsertByKey", insert_weight, 5),
    (0x46AD90, "_Sort_UnguardedInsertBy504", insert_504, 4),
    (0x46C940, "@SchHandle_DeletingDestructor@12", delete_handle, 4),
    (0x4708C0, "_E12_Swap", swap_record, 3),
    (0x4713F0, "_Order_Value", order, 5),
    (0x471450, "_Order_Margin", lambda *a: order(*a, margin=True), 7),
    (0x47D7C0, "_Doubles_MergeAdaptive", doubles_merge, 4),
    (0x483A70, "_Ai_SetModuleSwitchByName", scene_switch, 7),
    (0x48BCC0, "_Int_DivSmall", divide_limbs, 7),
]
