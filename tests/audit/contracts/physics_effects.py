"""Three-call-site functions from the hit record through the physics snapshot."""

import struct

from unicorn import x86_const as x

from cases import A, B, C, D, E, f32
from compare import u32


def hit_record(p, n, writes):
    kind, owner = [(None, 0), (None, D), (4, 0), (5, D), (6, 0), (8, D), (9, D)][n]
    p.args = [B if n else 0]
    p.result = "eax"
    p.expected_stack = 8
    p.put(A, bytes(range(92)))
    basis = bytes(range(48, 96))
    p.g(0x7025F0, *struct.unpack("<12I", basis))
    writes[A] = bytes(24)
    writes[A + 36] = basis
    if n:
        query_matrix = bytes(range(105, 153))
        contact = bytes(range(31, 55))
        p.put(B + 4, query_matrix + contact)
        p.w(B + 104, owner, C if kind is not None else 0, 123)
        p.w(C, kind or 0, 55, 456)
        p.w(D + 36, 789)
        writes[A] = contact
        writes[A + 24] = u32(0)
        if kind in (5, 6, 8):
            writes[A + 24] = struct.pack("<III", kind, 123, 456)
            writes[A + 36] = query_matrix
        writes[A + 84] = u32(owner)
        if owner:
            writes[A + 88] = u32(789)
    return A


def hue_channel(p, n, writes):
    hue = [-750, -360, -30, 0, 30, 59, 60, 179, 180, 210, 239, 240, 360, 390, 750][n]
    low, high = (-0.25, 0.75) if n % 2 else (0.75, -0.25)
    angle = hue - 360 if hue > 360 else hue + 360 if hue < 0 else hue
    reciprocal = struct.unpack("<f", struct.pack("<f", 1 / 60))[0]
    p.args = [f32(v) for v in (low, high, hue)]
    p.result = "fp"
    if angle < 60:
        return low + (high - low) * angle * reciprocal
    if angle < 180:
        return high
    if angle < 240:
        return low + (high - low) * (240 - angle) * reciprocal
    return low


def fade_material(p, n, writes):
    now, last, state = [(7, 7, 3), (7, 6, 4), (7, 5, 3), (0, -1, 4),
                        (8, 6, 0), (8, 6, 1), (8, 6, 2), (8, 6, 5)][n]
    p.g(0x696224, C)
    p.w(C + 2992, now)
    p.w(A + 64, f32(0.25), last, 0xCAFE, state)
    colors = [float(i - 7) for i in range(16)]
    p.put(B, struct.pack("<16f", *colors))
    p.args = [A, B]
    p.result = "eax"
    if last == now:
        return 0
    writes[A + 68] = u32(now)
    if state not in (3, 4):
        return 0
    for i in (1, 2, 3, 5, 6, 7, 9, 10, 11):
        writes[B + i * 4] = struct.pack("<f", colors[i] / 4)
    return 1


def effect_level(p, n, writes):
    step, level = [(-2**31, 0), (-3, -1), (-2, 0), (-1, -1), (0, 0), (1, -1),
                   (2, 0), (3, -1), (4, -1), (5, 0), (6, -1), (2**31 - 1, 0)][n]
    p.args = [step]
    p.result = "eax"
    return level & 0xFFFFFFFF


def vehicle_resource(p, n, writes):
    vehicle, table_state, present = [(-1, 1, False), (1001, 1, False), (1001, 0, True),
                                     (1001, 1, True), (1001, 2, True), (1001, 0x83, True),
                                     (1500, 1, False)][n]
    p.g(0x6CEC6C, A)
    p.w(A + 1356, B)
    p.w(B + 704 + 4, C)
    p.w(C + 4, D if present else 0)
    p.w(D + 12, E)
    p.w(E + 4012, table_state)
    p.args = [vehicle]
    p.result = "eax"
    return int(present and table_state & 1)


def unpack_pose(p, n, writes):
    # Exercise the caller's ABI and narrowing to six consecutive floats.
    matrix = [f32(i + n / 2) for i in range(12)]
    values = [-11.75, 2.125 + 2**-35, 99.5, -0.0, 0.03125, 4.5]
    out = [B, B + 4, A][n]
    p.w(A, *matrix)
    p.put(B, bytes(range(48)))
    p.args = [A, out]

    def decompose(args):
        assert args == matrix
        sp = p.uc.reg_read(x.UC_X86_REG_ESP)
        for i, value in enumerate(values):
            p.put(p.read(sp + 52 + 4 * i), struct.pack("<d", value))
        p.events.append("pose")

    p.hook(0x5DE950, "_Matrix_ToAngles", decompose)
    p.expected_calls = ["pose"]
    writes[out] = struct.pack("<6f", *values)


def first_part_ray(p, n, writes):
    skip, response = [(0, 0), (D, 1), (D, 2), (D, 3), (0, 0x101)][n]
    start, direction = struct.pack("<3f", 1, 3, -5), struct.pack("<3f", 0.5, -1, 0)
    p.put(A, bytes(range(208)))
    p.put(B, start + direction)
    basis = bytes(range(48))
    p.g(0x7025F0, *struct.unpack("<12I", basis))
    p.args = [skip, C, B, B + 12, A]
    p.result = "eax"
    writes[A] = u32(0) + basis
    writes[A + 76] = start + direction
    writes[A + 120] = u32(skip)

    def query(args):
        assert args[:3] == [C, 4, A]
        for addr, data in writes.items():
            if addr != A + 52:
                assert bytes(p.uc.mem_read(addr, len(data))) == data
        p.w(A + 52, 1122)
        p.events.append("query")
        return response

    writes[A + 52] = u32(1122)
    p.hook(0x4E623E, "_World_QueryObjectCollision", query)
    p.expected_calls = ["query"]
    return response if response & 1 else 0


def body_snapshot(p, n, writes):
    wheels = [-1, 0, 1, 2, 4, 6][n]
    data = bytes(((i * 67 + 23 * (i // 251)) % 256) for i in range(6752))
    p.put(A, data)
    p.w(A + 3540, wheels)
    fields = [(16, 2256, 48), (2304, 2352, 48), (244, 6672, 4), (64, 2568, 12), (76, 3132, 12)]
    for i in range(wheels):
        fields.extend([(248 + i * 248, 4120 + i * 248, 248),
                       (100 + i * 12, 6116 + i * 12, 12),
                       (3544 + i * 4, 3576 + i * 4, 4)])
    for src, dst, size in fields:
        writes[A + dst] = data[src:src + size]


ENTRIES = [
    (0x4BD620, "@HitRecord_InitFromQuery@12", hit_record, 7),
    (0x4BE750, "_Color_ChannelFromHue", hue_channel, 15),
    (0x4CCDA0, "_Fade_ApplyToMaterialOncePerFrame", fade_material, 8),
    (0x4D43A0, "_Fx_LevelFromVehicleStep", effect_level, 12),
    (0x4D8A60, "_Vehicle_HasUnloadedResourceTableFlag", vehicle_resource, 7),
    (0x4E2F7C, "_Matrix_Decompose6FArray", unpack_pose, 3),
    (0x4E6B4D, "_World_RaycastFirstPartSkippingObject", first_part_ray, 5),
    (0x4F0450, "@Body_SnapshotBodyAndWheels@8", body_snapshot, 6),
]
