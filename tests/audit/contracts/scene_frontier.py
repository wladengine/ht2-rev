"""The remaining four-site entries: arithmetic, contacts, camera, weather and mesh refinement."""

import struct

from cases import A, B, C, D, E, f32
from compare import IMPORTS, u32
from unicorn import x86_const as x
from queue_and_five import insert


def integer(p, n, writes, shift):
    number, argument = [(103, 0), (0x10006, 1), (0x15221, 17), (0x25222, -3)][n]

    def limbs(value):
        value = abs(value)
        return [(value >> k) & 65535 for k in range(0, value.bit_length(), 16)]

    def storage(value):
        digits = limbs(value)
        return struct.pack(
            "<" + "H" * (len(digits) + 3), len(digits), 16, int(value >= 0), *digits
        )

    p.put(A, storage(number))
    p.put(B, storage(0))
    if shift:
        answer = number << argument if argument >= 0 else number >> -argument
    else:
        answer = number * argument
    p.args = [A, argument, B]
    p.result = "eax"
    writes[B] = storage(answer)
    return B


def touch(p, n, writes):
    frame = [0, 37, 0xFFFFFFF0][n]
    p.g(0x696224, B)
    p.w(B + 2992, frame)
    p.w(A + 68, 7)
    p.args = [A]
    writes[A + 68] = u32(frame)


def no_hit(p, n, writes):
    p.args = [[A, B, C], [0, 12, 0], [D, E, 17]][n]
    p.result = "eax"
    return 0


def intersection(p, n, writes):
    # The plane is y=4. A degenerate segment projects its point normally.
    for loc, v in [
        (B, (1, 4, 3)),
        (C, (0, 1, 0)),
        (D, (9, 2, 5)),
        (E, (9, 8, 5) if n else (9, 2, 5)),
    ]:
        p.put(loc, struct.pack("<3f", *v))
    dst = D if n == 2 else A
    writes[dst] = struct.pack("<3f", 9, 4, 5)
    p.args = [dst, B, C, D, E]
    p.result = "eax"
    return dst


def spin(p, n, writes):
    omega = [(0, 0, 0), (1, -2, 3), (-3, 2, 1)][n]
    inertia = (2, 4, 8)
    rotation = (0, 1, 0, 0, 0, 1, 1, 0, 0)
    p.put(A + 8008, struct.pack("<3f", *inertia))
    p.put(A + 8868, struct.pack("<9f", *rotation))
    ang = tuple(v * k for v, k in zip(omega, inertia))
    writes[A + 9048] = struct.pack("<3f", *omega)
    writes[A + 9036] = struct.pack("<3f", *ang)
    writes[A + 9024] = struct.pack("<3f", ang[2], ang[0], ang[1])
    p.args = [f32(v) for v in omega]
    p.expected_stack = 16


def height(p, n, writes):
    p.w(A + 20904, [0, 1, 2][n])
    p.w(A + 20296, f32(-3.25))
    p.result = "fp"
    return -3.25 if n else 0.0


def frame(p, n, writes):
    data = bytes((7 * i + 1) % 256 for i in range(48))
    p.put(A + 548, data)
    dest = [B, B + 4, A + 548][n]
    p.args = [dest]
    writes[dest] = data
    p.result = "eax"
    p.expected_stack = 8
    return dest


def nearby(p, n, writes):
    # The first two nearby objects are sufficient to exercise the early-hit path and skip.
    p.w(A + 3508, B, C, 0)
    addr = IMPORTS + 0x6000
    p.w(B + 256, D)
    p.w(C + 256, D)
    p.w(D + 52, addr)

    def ask(args):
        assert args[:2] == [6, E]
        obj = p.uc.reg_read(x.UC_X86_REG_ECX)
        p.events.append(obj)
        return 9 if n != 2 or obj == C + 256 else 0

    p.callbacks[addr] = ask, 8
    p.args = [B, E, A, B if n == 1 else 0]
    p.expected_calls = [[B + 256], [C + 256], [B + 256, C + 256]][n]
    p.result = "eax"
    return 9


def pixel(p, n, writes):
    p.put(A, struct.pack("<4H", 6, 8, 2, 16))
    p.w(A + 16, 1)
    p.w(A + 24, D, B if n != 4 else 0)
    p.w(C + 28, int(n != 1))
    x, y = [(3, 4), (3, 4), (6, 3), (-1, 7), (3, 4), (1, 1)][n]
    p.args = [A, x, y, C if n != 5 else 0]
    if n == 0:
        writes[B + 70] = bytes((0x88, 0x88))


def color(p, n, writes):
    weight = [0, 1, 0.25, -2, 2, float("nan")][n]
    a = (8, -4, 1)
    b = (0, 4, 9)
    p.put(A, struct.pack("<3f", *a))
    p.put(B, struct.pack("<3f", *b))
    dest = A if n == 2 else C
    p.args = [A, B, f32(weight), dest]
    t = 0 if not weight >= 0 else min(1, weight)
    writes[dest] = struct.pack("<3f", *(t * v + (1 - t) * w for v, w in zip(a, b)))


def material(p, n, writes):
    v = [0, 0xFFFFFFFF, C][n]
    p.w(A + 116, v)
    p.result = "eax"
    return v


def node_top(p, n, writes):
    a, b = [(0, 0), (B, C), (B, C), (B, C), (B, C)][n]
    p.w(A, a, b)
    val = [0, 3, 1, 2, float("nan")][n]
    p.w(B + 28, f32(2))
    p.w(C + 28, f32(val))
    p.result = "eax"
    return (C if val >= 2 else B) if a else 0


def vertex(p, n, writes):
    p.put(B, bytes(range(48)))
    p.put(C, bytes(range(12, 24)))
    dest = [A, B, B + 4][n]
    normal = [C, B + 12, C][n]
    p.ecx = dest
    p.args = [B, normal, 11, 22, 33, 44, 55, 66]
    # Sequential reads also define the result when position overlaps destination.
    mem = bytearray(p.uc.mem_read(A, 0x20000))
    for i, src in enumerate([B, B + 4, B + 8, normal, normal + 4, normal + 8]):
        offset = dest - A + 4 * i
        mem[offset : offset + 4] = mem[src - A : src - A + 4]
    writes[dest] = mem[dest - A : dest - A + 24] + struct.pack("<6I", *p.args[2:])
    p.expected_stack = 36
    p.result = "eax"
    return dest


def trim(p, n, writes):
    p.w(B, 0, 0, 0, C if n else 0)
    if n:
        classifier = "@MarineGrid_ClassifyTriangleAgainstLimits@12"
        if p.image.symbols is not None and classifier not in p.image.symbols:
            classifier = "@MarineGrid_ClassifyTriangle@12"
        p.hook(0x6127E0, classifier, lambda a: 4, 4)
    p.args = [B]
    p.expected_stack = 8
    p.result = "eax"
    return int(bool(n))


ENTRIES = [
    (0x48B6B0, "_Int_MulInt", lambda *a: integer(*a, False), 4),
    (0x48C040, "_Int_Shift", lambda *a: integer(*a, True), 4),
    (0x4CCBA0, "_Fade_Touch", touch, 3),
    (0x4E634F, "_World_ReturnNoHit", no_hit, 3),
    (0x4E63BE, "_Line_Project", intersection, 3),
    (0x4F7F00, "@Body_SetLocalAngularVelocity@20", spin, 3),
    (0x556650, "@Car_GetActiveCabHeightOffset@8", height, 3),
    (0x57DF70, "@SceneObj_Matrix548@12", frame, 3),
    (0x599120, "_Car_AskNear", nearby, 3),
    (0x5A5730, "_Surface_Mark", pixel, 6),
    (0x5A8D30, "_Color_Lerp", color, 6),
    (0x60E0E0, "@MarineTask_GetMaterial@8", material, 3),
    (0x60F7B0, "@MarineQueue_TopNode@8", node_top, 5),
    (0x6114D0, "@MarineGrid_WriteVertex@40", vertex, 3),
    (0x612880, "@MarineGrid_TrimBranch@12", trim, 2),
    (
        0x613340,
        "@MarineQueue_InsertDeferredNode@12",
        lambda *args: insert(*args, deferred=True),
        6,
    ),
]
