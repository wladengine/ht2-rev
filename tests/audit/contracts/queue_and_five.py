"""Remaining circular queues and entries with five direct sites. Only dependency calls noted here are replaced."""

import struct

from cases import A, B, C, D, E, f32
from compare import IMPORTS, u32
from unicorn import x86_const as x


def ring(p, items):
    for i, (node, score) in enumerate(items):
        p.w(node + 4, items[(i + 1) % len(items)][0] + 4, items[i - 1][0] + 4)
        p.w(node + 28, f32(score))


def expected_ring(writes, nodes):
    for i, node in enumerate(nodes):
        writes[node + 4] = u32(nodes[(i + 1) % len(nodes)] + 4) + u32(nodes[i - 1] + 4)


def rebuild(p, n, writes):
    # Distinct identities, including ties across the two sorted rings.
    first_scores, second_scores = [
        ([], []),
        ([7], []),
        ([], [9, 4]),
        ([5], [4]),
        ([5], [8]),
        ([8, 4, 1], [9, 6, 2]),
        ([5, 2], [5, 2]),
    ][n]
    first = [(B + 128 * i, v) for i, v in enumerate(first_scores)]
    second = [(C + 128 * i, v) for i, v in enumerate(second_scores)]
    ring(p, first)
    ring(p, second)
    p.w(A, B if first_scores else 0, C if second_scores else 0, 500)
    order = [a for a, v in sorted(second + first, key=lambda t: -t[1])]
    expected_ring(writes, order)
    writes[A] = u32(order[0] if order else 0) + bytes(8)
    # Callers of the old rebuild also receive the final splice destination link.
    p.result = "eax"
    if not first_scores or not second_scores:
        return C if second_scores else 0
    target = next((node for node, score in first if score <= second_scores[-1]), B)
    return target + 4


def empty(p, n, writes):
    p.w(A, B if n & 1 else 0, C if n & 2 else 0, 91)
    p.result = "eax"
    return int(not n)


def town_node(p, n, writes):
    primary, candidates, answer = [
        (0, [9], 0),
        (37, [], 37),
        (-1, [], -1),
        (-1, [-1, -1], -1),
        (-1, [-1, 0, 42], 0),
    ][n]
    p.w(B + 64, primary)
    p.w(B + 28, C, C + len(candidates) * 16)
    for i, v in enumerate(candidates):
        p.w(C + 16 * i, v, 12, 13, 14)
    p.args = [A, B]
    p.result = "eax"
    writes[A] = u32(answer)
    return A


def road_position(p, n, writes):
    point = [
        (0.0, -0.0, 3.0),
        (1.0000000596046448, 2**24 + 1, -12.75),
        (2.0, 3.0, 4.0),
    ][n]
    dst = A if n != 2 else C + 56
    p.g(0x68CF00, B)
    p.w(B, D, C)
    p.put(C + 48, struct.pack("<3d", *point))
    p.args = [dst, 1]
    p.result = "eax"
    writes[dst] = struct.pack("<3f", *point)
    return dst


def road_name(p, n, writes):
    p.g(0x68CF00, B)
    p.w(B, C, D)
    begin, end = [(0, 0), (C, C), (E, E + 4)][n]
    p.w(D, begin, end)
    p.args = [1]
    p.result = "eax"
    return begin if begin != end else 0


def free_string(p, n, writes):
    length, cached, leaf, wanted = [
        (0, B, C, [(B, 1), (C, 8)]),
        (7, C, C, [(C, 8)]),
        (8, 0, C, [(C, 16)]),
        (-1, B, C, []),
    ][n]
    p.w(A, length)
    p.w(A + 16, cached, leaf)
    p.hook(0x44B4A0, "_Ai_PoolFree", lambda a: p.events.append(tuple(a[:2])))
    p.expected_calls = wanted


def distance(p, n, writes):
    start, end, prior = [(B, B, 8), (B, B + 12, 7), (B + 20, B, -2)][n]
    p.w(A, prior)
    p.args = [start, end, A]
    writes[A] = u32(prior + (end - start) // 4)


def query(p, n, writes):
    # World lookup and the parts' virtual query return controlled answers.
    p.args = [A, 6, C]
    p.w(C, f32(1.0))
    p.w(A + 12, E + 512)
    p.w(B + 12, E + 512)
    p.g(0x6974F0, A, D if n in (1, 2) else 0, 0)
    p.w(D, E)
    p.w(D + 64, E)
    port = IMPORTS + 0x6000
    p.w(E + 52, port)

    def part(args):
        assert args[0] == D
        return D + 64

    def test(args):
        assert args[:2] == [6, C]
        sub = p.uc.reg_read(x.UC_X86_REG_ECX) == D + 64
        p.events.append("sub" if sub else "part")
        if sub:
            p.w(C + 140, 87)
            return int(n == 1)
        assert p.read(C + 140) == 0
        return 1

    def world(args):
        assert args[:4] == [A, f32(1.0), C + 76, C + 88]
        p.events.append("world")
        if n == 3:
            p.g(0x6974F0, B, D, 0)
        return B if n == 3 else 0

    p.hook(0x4E5D60, "_Model_FindDynamicCollisionChild", part)
    world_symbol = "_World_FindHitObjectAtRadius"
    if p.image.symbols is not None and world_symbol not in p.image.symbols:
        world_symbol = "_World_ObjectAt"
    p.hook(0x4E5F87, world_symbol, world)

    def transform(args):
        assert args[0] == C
        p.events.append("transform")

    p.hook(0x4E5698, "_Obj_TransformVecs", transform)
    p.callbacks[port] = test, 8
    p.expected_calls = [
        ["world"],
        ["sub", "transform"],
        ["sub", "part", "transform"],
        ["world", "sub", "part", "transform"],
    ][n]
    if n == 1:
        writes[C + 140] = u32(87)
    if n == 3:
        writes[C + 104] = u32(B)
    p.result = "eax"
    return [0, 1, 1, 3][n]


def ini(p, n, writes):
    count = [0, 1, 5, 1000][n]
    p.args = [A, B, f32(-3.5), C, D, count]

    def reader(args):
        assert args[:2] == [A, B] and args[3:6] == [C, D, count]
        assert bytes(p.uc.mem_read(args[2], 4 * count)) == u32(f32(-3.5)) * count
        p.events.append(count)
        return 123

    p.hook(0x507AD0, "_Ini_ReadFloats", reader)
    p.expected_calls = [count]
    p.result = "eax"
    return 123


def car_type(p, n, writes):
    starts = [1, 31, 41, 51, 61, 81]
    vals = [-1, 0, 1, 30, 31, 40, 41, 50, 51, 60, 61, 80, 81, 90, 91]
    value = vals[n]
    bases = [170, 210, 350, 420, 510, 680]
    p.g(0x6CE350, *bases)
    p.args = [value]
    p.result = "eax"
    if 1 <= value <= 90:
        group = max(i for i, s in enumerate(starts) if value >= s)
        return bases[group] + value - starts[group]
    return 0


def sound(p, n, writes):
    obj = A if n else 0
    p.args = [obj, 2, f32(0.5), f32(1.25), 7]
    p.g(0x6CEC78, B if n == 2 else 0)
    p.w(B + 388, 50)
    p.hook(0x4E5140, "_Sound_PlayRange3", lambda a: p.events.append(tuple(a[:4])))

    def pitch(args):
        p.events.append((p.uc.reg_read(x.UC_X86_REG_ECX), args[0]))

    p.hook(0x5FD5C0, "@Sound_SetPitch@12", pitch, 4)
    p.expected_calls = (
        [(A, 2, f32(0.25 if n == 2 else 0.5), 7), (A, f32(1.25))] if obj else []
    )


def status(p, n, writes):
    p.args = [C]
    p.w(A + 4, B if n else 0)
    p.w(B, D)
    addr = IMPORTS + 0x6000
    p.w(D + 36, addr)
    p.w(C, 123)

    def get(args):
        assert args[0] == B
        p.w(args[1], 9)
        p.events.append("status")
        return 0 if n == 1 else -1

    p.callbacks[addr] = get, 8
    p.expected_calls = ["status"] if n else []
    p.expected_stack = 8
    p.result = "eax"
    writes[C] = u32(9 if n == 1 else 0)
    return [0x80070015, 0, 0xFFFFFFFF][n]


def joy(p, n, writes):
    p.args = [8]
    p.g(0x6F4B88, 0 if n == 0 else B)
    p.g(0x6F4B80, 0 if n == 1 else 1)
    p.w(B, D)
    addr = IMPORTS + 0x6000
    p.w(D + 88, addr)

    def command(args):
        assert args[:2] == [B, 8]
        p.events.append("send")
        return -7

    p.callbacks[addr] = command, 8

    # Isolate formatting and disk logging. The command still reaches the vtable.
    def fmt(args):
        assert args[1:3] == [p.image.data(0x67CBAC), 8]
        p.put(args[0], b"command\0")
        p.events.append("format")
        return 7

    def log(args):
        assert bytes(p.uc.mem_read(args[0], 8)) == b"command\0"
        p.events.append("log")

    p.hook(0x61CCA9, "_sprintf", fmt)
    p.g(0x6F4BAC, addr + 4)
    p.callbacks[addr + 4] = log, 0
    p.expected_calls = ["format", "log", "send"] if n == 2 else []
    p.result = "eax"
    return 0xFFFFFFF9 if n == 2 else 0x80070015


def insert(p, n, writes, deferred=False):
    scores = [[], [7], [7], [7, 2], [7, 2], [7, 2]][n]
    value = [8, 0, 8, 1, 5, 5][n]
    nodes = [(B + 128 * i, k) for i, k in enumerate(scores)]
    ring(p, nodes)
    ring(p, [(C, value)])
    p.w(A, 0, B if scores else 0, 499 if n == 5 else 3)
    p.args = [C]
    expected = [a for a, k in sorted(nodes + [(C, value)], key=lambda pair: -pair[1])]
    expected_ring(writes, expected)
    writes[A + 4] = u32(0 if n == 5 else expected[0])
    skips_counter = value == 0 and not deferred
    writes[A + 8] = u32(0 if n == 5 else 3 if skips_counter else 4)
    if n == 5:
        writes[A] = u32(expected[0])
    p.expected_stack = 8
    p.result = "eax"
    return expected[0] if n == 5 else C + 4 if skips_counter else 4


def top(p, n, writes, edge):
    first, last = [(0, 0), (B, 0), (0, C), (B, C), (B, C), (B, C), (B, C)][n]
    a, b = [(0, 0), (0, 0), (0, 0), (4, -1), (4, 8), (4, 4), (4, float("nan"))][n]
    p.w(A, first, last)
    # Deliberately opposite ordering at the other record offset.
    for addr, v, other in [(B, a, -12), (C, b, 20)]:
        p.w(addr + 28, f32(other if edge else v))
        p.w(addr + 72, f32(v if edge else other))
    p.result = "eax"
    if not first or not last:
        return first or last
    if edge:
        return first if b > a else last
    return last if b > a else first


def map_y(p, n, writes):
    y = [0, 12, 0x80000000, 0xFFFFFFFF][n]
    p.w(A + 188, f32(-5), f32(100), f32(0.5))
    p.w(B, 0xDEADBEEF, y)
    p.args = [B]
    p.expected_stack = 8
    p.result = "fp"
    return y * 0.5 - 5


def active(p, n, writes):
    value = [0, 1, 0xF0051234][n]
    p.w(A, 12, 23, 34)
    p.args = [value]
    p.expected_stack = 8
    writes[A + 4] = u32(value)
    p.result = "eax"
    return value


ENTRIES = [
    (0x60F390, "@MarineQueue_RebuildNodes@8", rebuild, 7),
    (0x6137D0, "@MarineQueue_Empty@8", empty, 4),
    (0x404E90, "_Town_ResolveRoadNodeId", town_node, 5),
    (0x436460, "_Node_Position", road_position, 3),
    (0x436490, "_Node_Name", road_name, 3),
    (0x459C10, "@Str_FreeBoth@8", free_string, 4),
    (0x467FC0, "_DwordIterator_AddDistance", distance, 3),
    (0x4E6059, "_World_QueryRoadCollision", query, 4),
    (0x507D30, "_Ini_ReadFloatsWithDefault", ini, 4),
    (0x52EB40, "_CarType_FromNumber", car_type, 15),
    (0x52F830, "_Sound_StartScaledWithPitch", sound, 3),
    (0x577380, "@JoyEffect_GetStatus@12", status, 3),
    (0x578230, "_Joy_SendCommand", joy, 3),
    (0x60F520, "@MarineQueue_InsertNode@12", insert, 6),
    (0x60F9D0, "@MarineQueue_TopEdge@8", lambda *args: top(*args, True), 7),
    (0x612040, "@MarineGrid_MapY@12", map_y, 4),
    (0x6132E0, "@MarineTransition_SetActive@12", active, 3),
    (0x6134C0, "@MarineQueue_TopDeferredNode@8", lambda *args: top(*args, False), 7),
]
