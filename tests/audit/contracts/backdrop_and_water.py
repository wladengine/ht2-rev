"""Remaining three-site functions in the shell, route stack and water surface."""

import struct

from unicorn import x86_const as x

from cases import A, B, C, D, E, f32
from compare import u32


def backdrop(p, n, writes):
    cached = B if n == 0 else 0
    p.g(0x6CE4F4, cached)
    p.result = "eax"
    answer = [B, 47, 0][n]
    def grab(args):
        slot = p.uc.reg_read(x.UC_X86_REG_ECX)
        assert slot == p.image.data(0x6CE4F4)
        p.w(slot, C if n == 1 else 0)
        p.events.append("capture")
        return answer
    p.hook(0x50C010, "@Screen_Grab@8", grab)
    p.expected_globals = {0x6CE4F4: [B, C, 0][n]}
    p.expected_calls = [] if cached else ["capture"]
    return answer


def draw(p, n, writes):
    p.g(0x6CE4F4, C if n else 0)
    p.g(0x69684C, A if n else 0)
    p.w(A + 12, B)
    def bitmap(args):
        p.events.append(args[:4])
    p.hook(0x5B86C0, "_Bitmap_Draw", bitmap)
    p.expected_calls = [[B, 0, 0, C]] if n else []


def sound(p, n, writes):
    source, setting = [(0, None), (A, None), (A, 0), (A, 25), (A, -50), (A, 150)][n]
    p.g(0x6CEC78, B if setting is not None else 0)
    p.put(B + 384, struct.pack("<hhh", 11, setting or 0, 99))
    p.args = [source, f32(3.5), f32(0.5), D]
    def play(args):
        p.events.append(args[:4])
    p.hook(0x4E5140, "_Sound_PlayRange3", play)
    volume = 0.5 if setting is None else setting / 200
    p.expected_calls = [[A, f32(3.5), f32(volume), D]] if source else []


def route_stack(p, n, writes):
    top = [0, 3, 998, 999, 1000, 1001][n]
    p.put(A, bytes([0xA5]) * 3012)
    p.w(A + 3000, top)
    p.args = [0x1234ABCD]
    p.result = "eax"
    p.expected_stack = 8
    if top >= 1000:
        return 0
    new = top + 1
    writes[A + 3000] = u32(new)
    writes[A + new * 2] = struct.pack("<H", 0xABCD)
    writes[A + 2000 + new] = b"\0"
    if new == 1000:
        # Entry 1000's byte flag occupies the low byte of the stored count.
        writes[A + 3000] = u32(768)
        return 768
    return new


def activate(p, n, writes):
    count = [-1, 0, 1, 3, 4][n]
    p.w(A + 3572, 9, count, 37, 47)
    p.w(A + 3880, 3, 0, 9, 7, 11)
    for i in range(count):
        writes[A + 3880 + i * 4] = u32(1)


def drain(p, n, writes):
    # The removal callback advances the real queue heads in a known order.
    p.w(A + 28, B)
    p.w(B, C if n else 0, D if n == 2 else 0)
    p.w(C + 72, f32(5))
    p.w(D + 72, f32(2))
    def remove(args):
        edge, mesh = args[:2]
        assert mesh == A and edge in (C, D)
        p.w(B if edge == C else B + 4, 0)
        p.events.append(edge)
    p.hook(0x606760, "_GridMesh_RemoveEdge", remove)
    p.expected_calls = [[], [C], [D, C]][n]
    writes[B] = bytes(8)
    p.result = "eax"
    return 0


def wave_vertex(p, n, writes):
    # A vertical normal and a camera directly above the vertex give UV (.5, .5).
    # Two controlled angle-table values isolate the crest/foam threshold.
    grid_x, grid_y = 2 + n, 4
    p.w(A + 4, f32(grid_x), f32(grid_y), f32(15))
    p.w(A + 56, *(f32(1) for _ in range(4)))
    amplitude = 2 if n == 3 else 0
    p.w(A + 72, f32(amplitude))
    p.w(A + 80, f32(0), f32(1), f32(1))
    p.w(A + 92, f32(2), f32(-3), f32(1), f32(1))
    p.w(A + 112, E)
    p.w(A + 124, C)
    p.w(A + 192, f32(1), f32(1))
    p.w(B, grid_x, grid_y)
    p.g(0x724960, 0)
    p.g(0x7243BC, f32(1))
    p.g(0x724964, f32(1 if n else 0))
    if amplitude:
        p.g(0x724960, f32(1))
        for index, cosine, sine in [(grid_x, 0, 1), (grid_y, 1, 0)]:
            p.g(0x7243BC + 4 * index, f32(cosine))
            p.g(0x724964 + 4 * index, f32(sine))
    p.g(0x725E78, 0, 0, 0)
    p.g(0x6530B8, f32(2), f32(4))
    p.args = [B, 0x12340001 + n, 7 + n]
    p.expected_stack = 16
    def can_add(args):
        assert p.uc.reg_read(x.UC_X86_REG_ECX) == E
        p.events.append("capacity")
        return n == 2
    def add(args):
        assert p.uc.reg_read(x.UC_X86_REG_ECX) == E
        pos, vel = [struct.unpack("<3f", p.uc.mem_read(a, 12)) for a in args[:2]]
        assert vel == (2, -3, 0)
        p.events.append(list(pos))
        return 1
    p.hook(0x613280, "@MarineParticlePool_CanAdd@8", can_add)
    p.hook(0x612EB0, "@MarineParticlePool_Add@16", add, 8)
    p.expected_calls = ["capacity"] if n else []
    if n == 2:
        for offset in (0, -1, 1, 2, -2, 3, -3, 4, -4, -5, 5, 6, -6, 7, -7, 8, -8):
            p.expected_calls.append([grid_x + offset, grid_y, 0])
    minus_zero = f32(-0.0)
    p.expected_globals = {0x725E78: minus_zero}
    normal = [-0.0, -0.0, 1.0]
    record = [grid_x, grid_y, amplitude, *normal, grid_x, grid_y, grid_x / 2, grid_y / 4, 0.5, 0.5]
    writes[C + (1 + n) * 48] = struct.pack("<12f", *record)


def map_x(p, n, writes):
    index = [0, 7, 0x80000000, 0xFFFFFFFF][n]
    p.w(B, index, 17)
    p.w(A + 176, f32(4))
    p.w(A + 192, f32(-0.5))
    p.args = [B]
    p.result = "fp"
    p.expected_stack = 8
    return 4 - index / 2


def grid_uv(p, n, writes):
    ix, iy = [(0, 0), (12, 39), (0xFFFFFFFF, 0x80000000), (18, 33)][n]
    p.w(B, ix, iy)
    p.w(A + 100, f32(4), f32(-2))
    p.w(A + 152, f32(2), f32(1))
    y_out = C if n == 3 else D
    p.args = [B, C, y_out]
    p.expected_stack = 16
    writes[C] = u32(f32((ix - 2) / 4))
    writes[y_out] = u32(f32((iy - 1) / -2))


def reflection(p, n, writes):
    camera = [(0, 0, 2), (3, 0, 4), (0, 3, -4), (2, 0, 0)][n]
    p.w(A + 4, *(f32(v + delta) for v, delta in zip(camera, (2, 4, 6))))
    p.w(B, 0, 0, f32(1))
    p.args = [f32(2), f32(4), f32(6), B, C, D]
    p.expected_stack = 28
    # Use exactly representable coefficients; the real vector code stays in the path.
    p.g(0x65311C, f32(0.25))
    p.g(0x6503E0, f32(0.125))
    u, v = [(0.5, 0.5), (0.3125, 0.5), (0.5, 0.3125), (-1.5, 0.5)][n]
    writes[C] = u32(f32(u))
    writes[D] = u32(f32(v))


def limits(p, n, writes):
    ix, iy = [(0, 0), (1, 1), (2, 0), (0, 2), (12, 15), (0xFFFFFFFF, 0)][n]
    p.w(A + 4, C)
    p.w(B, ix, iy)
    p.w(C + 176, f32(-1701))
    p.w(C + 188, f32(3749), f32(1), f32(1))
    p.args = [B]
    p.expected_stack = 8
    p.result = "eax"
    return int(ix > 1 or iy > 1)


def work_item(p, n, writes):
    scores, incoming, count = [([], 1, 0), ([3], 3, 1), ([2, 4, 8], 1, 3),
                               ([2, 4, 8], 6, 3), ([2, 4, 8], 10, 198),
                               ([2, 4], 5, 199)][n]
    nodes = [B + i * 128 for i in range(len(scores))]
    p.w(A, 0, nodes[0] if nodes else 0, count)
    for i, node in enumerate(nodes):
        p.w(node, nodes[(i + 1) % len(nodes)], nodes[i - 1])
        p.w(node + 104, f32(scores[i]))
    p.w(C, C, C)
    p.w(C + 104, f32(incoming))
    order = [addr for _, addr in sorted([(incoming, C)] + list(zip(scores, nodes)), key=lambda pair: pair[0])]
    for i, node in enumerate(order):
        writes[node] = u32(order[(i + 1) % len(order)]) + u32(order[i - 1])
    rebuild = count == 199
    writes[A] = u32(order[0] if rebuild else 0) + u32(0 if rebuild else order[0]) + u32(0 if rebuild else count + 1)
    p.args = [C]
    p.expected_stack = 8
    p.result = "eax"
    return count + 1


ENTRIES = [
    (0x50BFF0, "_Screen_EnsureBackdrop", backdrop, 3),
    (0x50C130, "_Screen_DrawBackdrop", draw, 2),
    (0x52F980, "_Sound_StartRangeScaledByOption386", sound, 6),
    (0x561030, "@RouteStack_PushNode@12", route_stack, 6),
    (0x58CF30, "@Points_ActivateAll@8", activate, 5),
    (0x60D750, "@MarineMesh_DrainEdges@8", drain, 3),
    (0x611530, "@MarineGrid_UpdateWaveVertex@20", wave_vertex, 4),
    (0x612010, "@MarineGrid_MapX@12", map_x, 4),
    (0x612070, "@MarineGrid_ComputeGridUV@20", grid_uv, 4),
    (0x6120C0, "@MarineGrid_ReflectionUV@32", reflection, 4),
    (0x612760, "@MarineGrid_ExceedsWorldLimits@12", limits, 6),
    (0x613620, "@MarineQueue_InsertWorkItem@12", work_item, 6),
]
