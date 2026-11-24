"""AI entries at the head of the remaining four-site group."""

import math
import struct

from cases import A, B, C, D, E, dbl, f32
from compare import u32
from compare import IMPORTS
from unicorn import x86_const as x


def vector(p, n, writes, scale):
    a = [-1.5, 2**35, 0.25]
    b = [3.0, -0.75, 5.5]
    p.put(B, struct.pack("<3d", *a))
    p.put(C, struct.pack("<3d", *b))
    dst = [A, B, C, B + 8][n]
    p.args = [dst, B, *dbl(-0.5)] if scale else [dst, B, C]
    expected = [v * -0.5 for v in a] if scale else [v + w for v, w in zip(a, b)]
    writes[dst] = struct.pack("<3d", *expected)
    p.result = "eax"
    return dst


def pick(p, n, writes):
    index, value = [(0, 0), (1, 2), (1, 3), (1, 0xFFFFFFFF)][n]
    p.g(0x68CBF8, B)
    p.w(B, C, C + 4, C + 4, D, D + 12, D + 12)
    p.w(C, A)
    p.w(D, E, B, C)
    p.args = [index, value]
    p.result = "eax"
    return A if not index else [E, B, C][value % 3]


def vehicle(p, n, writes):
    primary, secondary = [(0, 0), (B, 0), (0, C), (B, C), (B, C)][n]
    p.g(0x68CFD8, D)
    p.w(D, E, E + 16)
    p.w(E + 4, 17)
    p.w(E + 20, 29)
    p.w(B + 4, 1)
    p.w(C + 4, -1 if n == 4 else 0)
    p.w(A + 516, primary, secondary)
    p.args = [A]
    p.result = "eax"
    return [0, 29, 17, 17, 29][n]


def factor(p, n, writes):
    pos = [(-1720, 3950, 3), (-3000, -4715, 20), (0, 0, 0), (10000, 250, 400)][n]
    p.args = [f32(v) for v in pos]
    distance = min(
        math.dist(pos, other) for other in [(-1720, 3950, 3), (-3000, -4715, 20)]
    )
    p.result = "fp"
    return int(20 + distance / 200) * 0.1


def road_id(p, n, writes, advance, vector_address=0x68CF38):
    count, current = [(0, -1), (3, -1), (3, 0), (3, 2), (3, 3), (3, -2)][n]
    p.g(vector_address, B, B + count * 4)
    p.args = [A, current] if advance else [A]
    result = (
        (current + 1 if 0 <= current + 1 < count else -1)
        if advance
        else (0 if count else -1)
    )
    p.result = "eax"
    writes[A] = u32(result)
    return A


def presents(p, n, writes, advance):
    p.g(0x68CF6C, B)
    head, curr, nxt = [(B, B, B), (C, B, B), (C, C, D), (C, D, B)][n]
    p.w(B, head)
    p.w(C, D)
    p.w(D, B)
    p.args = [A, curr] if advance else [A]
    writes[A] = u32(nxt if advance else head)
    p.result = "eax"
    return A


def assign_handles(p, n, writes):
    # Three words per Scheme handle: object, length, ownership/type flags.
    p.w(C, D, 2, 0, 0)
    p.w(D + 12, IMPORTS + 0x6000)
    p.w(E + 4, 2)
    borrowed = 0x10002
    old = [C, 17, 2]
    source = [[0, 0, 0], [E, 17, borrowed], [E, 17, 2], [E, 17, 2], [C, 17, 2]][n]
    p.w(A, *source)
    p.w(B, *old)
    if n >= 3:
        p.w(C + 4, 1)
    count = 0 if n == 0 else 1
    p.args = [A, A + 12 * count, B]

    def destroy(args):
        assert p.uc.reg_read(x.UC_X86_REG_ECX) == C and args[0] == 1
        p.events.append("destroy")

    p.callbacks[IMPORTS + 0x6000] = destroy, 4
    p.expected_calls = ["destroy"] if n == 3 else []
    if count:
        writes[B] = u32(source[0]) + u32(source[1] if n == 1 else 0) + u32(source[2])
        writes[C + 4] = u32([0, 1, 1, 0, 1][n])
        if n in (2, 3):
            writes[E + 4] = u32(3)
    p.result = "eax"
    return B + 12 * count


def block(p, n, writes):
    p.w(A, 11, 22, 33, 44)
    first = [C, D, 0][n]
    p.w(B, first)
    p.args = [B]
    writes[A + 4] = u32(first) + u32(first + 512) + u32(B)
    p.expected_stack = 8


def record_copy(p, n, writes):
    src = bytes(range(32))
    p.put(B, src)
    dst = [A, 0, B][n]
    p.args = [dst, B]
    if dst:
        writes[dst] = src


def long_records(p, n, writes):
    data = bytes(i % 197 for i in range(1592 * n))
    p.put(B, data)
    p.args = [B, B + len(data), C]
    writes[C] = data
    p.result = "eax"
    return C + len(data)


def advance12(p, n, writes):
    delta = [-3, 0, 7][n]
    p.w(A, C)
    p.args = [A, delta]
    writes[A] = u32(C + 12 * delta)


def post_inc(p, n, writes):
    node = B if n != 2 else D
    p.w(A, node)
    p.w(node, C)
    out = A if n == 1 else E
    p.args = [out, 17]
    writes[A] = u32(C)
    writes[out] = u32(node)
    p.result = "eax"
    p.expected_stack = 12
    return out


def field12(p, n, writes):
    value = [0, 0xFFFFFFF2, D][n]
    p.w(A + 8, 33, value, 77)
    p.args = [A]
    p.result = "eax"
    return value


def particle_word(p, n, writes, offset):
    value = [0, 10, 0xC0600000][n]
    p.put(A, bytes(range(60)))
    p.args = [value]
    writes[A + offset] = u32(value)
    p.expected_stack = 8


ENTRIES = [
    (0x411B70, "_DVec3_Add", lambda *a: vector(*a, False), 4),
    (0x415910, "_AI_PickFrom", pick, 4),
    (0x41B9A0, "_Player_VehicleData", vehicle, 5),
    (0x425550, "_DVec3_Times", lambda *a: vector(*a, True), 4),
    (0x4332E0, "_AI_BaseFactor", factor, 4),
    (0x4361E0, "_Road_FirstId", lambda *a: road_id(*a, False), 6),
    (0x436210, "_Road_NextId", lambda *a: road_id(*a, True), 6),
    (0x436410, "_Presents_First", lambda *a: presents(*a, False), 4),
    (0x436440, "_Presents_Next", lambda *a: presents(*a, True), 4),
    (0x45ADF0, "_StrRefs_Copy", assign_handles, 5),
    (0x4B2820, "@SchDequeIt_SetNode@12", block, 3),
    (0x4616A0, "_Construct32", record_copy, 3),
    (0x4699E0, "_UninitCopy1592", long_records, 3),
    (0x46FC50, "_E12_Advance", advance12, 3),
    (0x4AF810, "@SchListIt_PostInc@16", post_inc, 3),
    (0x4410F0, "_Rec_Field12", field12, 3),
    (0x612DA0, "@MarineParticle_SetLifetime@12", lambda *a: particle_word(*a, 44), 3),
    (0x6132C0, "@MarineParticlePool_SetFloor@12", lambda *a: particle_word(*a, 28), 3),
    (
        0x6132D0,
        "@MarineParticlePool_SetLifetime@12",
        lambda *a: particle_word(*a, 32),
        3,
    ),
]
