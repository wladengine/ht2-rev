"""Isolated inputs for direct-call audit. Addresses A..E belong to the test, never to a PE."""

import struct

A, B, C, D, E = (0x1000000 + i * 0x4000 for i in range(5))


def f32(v):
    return struct.unpack("<I", struct.pack("<f", v))[0]


def dbl(v):
    return struct.unpack("<2I", struct.pack("<d", v))


def configure(va, p, n):
    w, g = p.w, p.g

    def floats(addr, *v):
        w(addr, *(f32(x) for x in v))

    def call(stock, name, label, count, answer=0, pop=0):
        def observe(args):
            p.events.append([label, *args[:count]])
            return answer

        p.hook(stock, name, observe, pop)

    def write_id(args):
        w(args[0], 9 + n)
        p.events.append(["lookup", args[1]])
        return args[0]

    # Different signs and nonzero destination bytes distinguish actual writes from early returns.
    p.put(D, bytes([0x5A]) * 128)
    k = va
    if k in (0x436510, 0x4364F0):
        p.args = [D, B]
        p.put(B, b"NODE\0")
        node = k == 0x436510
        p.hook(
            0x449F00 if node else 0x449C40,
            "_NameMap68d008_Find" if node else "_NameMap68cff0_Find",
            write_id,
        )
    elif k in (0x4BE230, 0x4E30F4, 0x4C03A0, 0x4BFF50, 0x4CB270, 0x4BFF80, 0x5AB750):
        floats(A, 1.25, -8, 0.5)
        floats(B, -3, 4, 2)
        out = B if n else D
        if k in (0x4BE230, 0x4E30F4):
            p.args = [out, A, B]
        elif k in (0x4C03A0, 0x4BFF50):
            p.args = [A if n else D, A, f32(-2)]
        elif k == 0x4BFF80:
            p.args = [B if n else A]
            p.result = "fp"
        elif k == 0x5AB750:
            p.args = [A if n else D, B]
        else:
            p.args = [A if n else D, A]
    elif k in (0x45A990, 0x45A970, 0x415790, 0x4B2840, 0x4B26E0):
        call(0x44B4A0, "_Ai_PoolFree", "free", 2)
        if k == 0x415790:
            # The original inlines the small-block release on this path.
            p.callbacks.pop(
                0x44B4A0 if p.image.symbols is None else p.image.symbols["_Ai_PoolFree"]
            )
            g(0x68D204, 0)
            g(0x68D1C4, 0)

            def exchange(v):
                old = p.read(v[0])
                w(v[0], v[1])
                return old

            p.api("InterlockedExchange", exchange, 8)
        if k == 0x45A990:
            w(A, 12 if n else -1)
            w(A + 16, B)
            p.result = "void"
        elif k == 0x45A970:
            p.args = [B, 12 if n else -8]
            p.result = "void"
        elif k == 0x415790:
            w(B, 11, 22, 33)
            p.args = [B, B + 12, B + 16, 22 if n else 44]
            p.result = "al"
        else:
            w(A + 8, B + 508 if n else B + 16, B, B + 512, C)
            w(C, B, E)
            p.result = "void"
    elif k in (0x507D00, 0x507D90):
        w(B, 10, 20, 30)
        p.args = [1, 2, B, 4, 5] if k == 0x507D00 else [1, 2, B, 4, 5, 1 + n]

        def ini(v):
            p.events.append(
                [
                    "read",
                    v[0],
                    v[1],
                    v[3],
                    v[4],
                    v[5],
                    [p.read(v[2] + 4 * i) for i in range(v[5])],
                ]
            )
            return 13 + n

        p.hook(0x507AD0, "_Ini_ReadFloats", ini)
    elif k == 0x5ABC90:
        ident = [1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0]
        g(0x7025F0, *(f32(x) for x in ident))
        p.args = [D, *map(f32, (4, -2, 3, 0, 0, n, 0))]
    elif k == 0x4EB460:
        p.ecx = D + n * 4
    elif k == 0x4EB580:
        p.args = [n, f32(-3), f32(8), f32(0 if n else 4)]
        w(A + 44, n)
        p.result = "void"
    elif k == 0x402AF0:
        p.args = [A, 11 + n]
        w(A, 72 - n)

        def exchange(v):
            old = p.read(v[0])
            w(v[0], v[1])
            return old

        p.api("InterlockedExchange", exchange, 8)
    elif k == 0x4777B0:
        w(A + 508, B)
        w(A + 512, C if n else 0)
        w(B + 28, D, D + 144)
        w(C + 28, E, E + 144)
        p.args = [A]
    elif k == 0x50EF60:
        p.args = [3, 6, 20 + n, -12]
        call(0x50E200, "_Carat_Window", "lookup_window", 2, B)
        w(B + 44, 5, 7, 30, 40)
    elif k == 0x402B10:
        p.args = [20 + 2 * n]
        p.result = "void"
        p.api("Sleep", lambda a: p.events.append(["sleep", a[0]]), 4)
    elif k == 0x433F50:
        p.ecx = D + n * 4
        p.result = "void"
    elif k == 0x4608F0:
        w(B, 7, -4)
        p.args = [0 if n else D, B]
        p.result = "void"
    elif k == 0x549BF0:
        w(A + 20, B)
        w(B + 12, 31 + n)
    elif k == 0x4EB5E0:
        w(A + 40, n)
        floats(A + n * 4, -2)
        floats(A + 12 + n * 4, 8)
        floats(A + 36, 0.25 + n * 0.5)
        p.result = "fp"
    elif k in (0x58D0F0, 0x58D120):
        w(A + 3572, 1)
        w(A + 3580, n)
        w(A + 3592, 17, 18, 19)
        p.args = [D]
    elif k == 0x485680:
        p.args = [n, 3 + n, 3 + n, D]
        p.result = "void"
    elif k == 0x4172E0:
        p.args = [A]
        w(A + 496, 4 + n)
        p.result = "al"
        call(0x477210, "_Player_UpdateFlags", "update", 1)
    elif k == 0x40B0D0:
        w(A + 96, n)
        w(A + 312, *range(11, 17))
        w(A + 624, *range(31, 37))
        p.args = [D, A]
    elif k == 0x48ACD0:
        p.args = [A, B if n else 0]
        p.put(A, struct.pack("<7H", 2, 4, 1, 5, 6, 0, 0))
        p.put(B, struct.pack("<6H", 3, 3, 2, 12, 13, 14))
    elif k in (0x4E9CE4, 0x4E9D90):
        g(0x6974D0, n)
        g(0x6975A8, B if n else 0)
        p.result = "void"

        def open_log(a):
            # Compare both referenced C strings rather than PE-specific pointers.
            p.events.append(
                [
                    "open",
                    *[
                        bytes(p.uc.mem_read(addr, 16)).split(b"\0")[0].hex()
                        for addr in a[:2]
                    ],
                ]
            )
            return C

        p.hook(0x61CC96, "_fopen", open_log)
        call(0x61CB9A, "_fclose", "close", 1)
    elif k == 0x44CB40:
        w(A, B, 5, 2)
        w(B + 4, 3 + n)
        w(C, 0, 0, 0)
        p.args = [C]
        p.result = "void"
    elif k == 0x491AA0:
        w(A, B, B + 36)
        w(B + 12, 7 + n, 0, 0x10000)
        w(B + 24, 15, 0, 0x10000)
        p.args = [E, C, D, A, 1]
        w(D, 0, 0, 0)
    elif k in (0x4B27B0, 0x4AC2C0):
        block = 512
        w(C, B, E)
        w(A, B + 4, B, B + block, C)
        p.args = [(-2 if n else 2)]
        if n:
            w(C - 4, D)
    elif k in (0x4C5770, 0x4C5950):
        g(0x696248, B)
        slot = (0, 100, 99)[n]
        g(0x69624C, slot)
        w(B + slot * 116 + 52, C)
        w(C + 36, 33)
        w(A, 11, 22, 33)
        p.args = [3, A, f32(2), f32(3), f32(4), 0x55, 0x22, 7]
        if k == 0x4C5950:
            p.args += [9, 10]
        p.args += [f32(0.5)]
    elif k == 0x480650:
        g(0x68D6C8, *dbl(4))
        w(A, *(dbl(12) if n else (0xFFFFFFFF, 0x7FEFFFFF)))
        p.args = [A]
        p.result = "void"
    elif k == 0x579400:
        p.ecx = D
        p.args = [200 + n]
        p.fixups[D] = 0x65033C
    elif k == 0x579600:
        w(A + 432, 1 + 5 * n)
        p.args = [99 + n]
        p.result = "void"
    elif k == 0x612EB0:
        w(A + 4, 1)
        w(A + 12, E, 4, 5 if n else 4)
        w(A + 32, 42)
        g(0x725E30, 1)
        p.args = [B, C]
        floats(B, -2, 5, 11)
        floats(C, 6, 2, -9)
        p.api("timeGetTime", lambda args: 123, 0)
        draws = iter((1871, 2342))

        def random_draw(args):
            value = next(draws)
            p.events.append(["rand", value])
            return value

        p.hook(0x61EDDE, "_rand", random_draw)
    elif k == 0x4AA180:
        w(A + 4, B + 44 * (3 + n))
    elif k == 0x5DD8C0:
        w(A + 264, 16, 0)
        p.args = [640, 480, -1 if n else 32, 0]
        call(0x5D7C20, "_Dd_TestMode", "supported_mode", 7, 0)
    elif k == 0x46CF20:
        w(B, 1, 2, 3, 4, 5)
        p.args = [B, B + 4 * (4 + n)]
        p.result = "void"
    elif k == 0x574100:
        p.args = [A, 20, 30, 7, B, 17 + n]
        p.result = "void"
        call(0x5D2E50, "_Table71e858_Get", "label", 3, C)
        call(0x5EA170, "_Text_Draw", "text", 7)
    elif k == 0x591660:
        w(A + 9520, B)
        w(A + 9516, 1)
        w(B + 4, 0, 4, f32(0.5), f32(0.75))
        p.args = [0 if n else -1, f32(1), C, D]
    elif k == 0x5A91E0:
        floats(A, 1, 6, 2, -9)
        p.args = [f32(0.25 + 0.5 * n), f32(0.5), A]
        p.result = "fp"
    elif k == 0x40A630:
        w(A + 44, 31)
        w(A + 24, B, B + 12 * n)
        w(B, 17)
        p.args = [D, A]
    elif k == 0x461B20:
        w(B, *range(1, 16))
        w(B + 16, n)
        p.ecx = D
        p.args = [B]
    elif k == 0x465D20:
        w(A, *range(1, 8))
        p.args = [A, A + 4 * (4 + n), A + 24]
    elif k == 0x471560:
        w(A + 64, *dbl(-100.5 if n == 4 else 100.5))
        w(A + 104, 0 if n == 3 else 1)
        p.args = [A, *dbl(1 if n == 1 else 0), int(n != 2)]
        p.result = "fp"
    elif k == 0x483E60:
        g(0x68D7D0, *dbl(3))
        p.args = [*dbl(-2 + n)]
        p.result = "void"
    elif k == 0x483E80:
        g(0x68D7D0, *dbl(7 + n))
        p.args = [31 + n, B]
        p.result = "void"
        banner = "_Notice_ShowEventBanner"
        if p.image.symbols is not None and banner not in p.image.symbols:
            banner = "_Notice_Banner"
        call(0x56AED0, banner, "banner", 4)
    elif k == 0x404E80:
        w(A + 64, 44 + n)
        p.args = [D, A]
    elif k == 0x4652E0:
        w(A, 8)
        w(B, 8 - n)
        p.args = [A, B]
    elif k == 0x484660:
        p.args = [A, B]
        p.put(B, b"route.txt\0")
        p.result = "void"

        def open_route(v):
            p.events.append(["open", v[0], bytes(p.uc.mem_read(v[1], 3)).hex()])
            return C if n else 0

        p.hook(0x61CC96, "_fopen", open_route)
        call(0x61CB9A, "_fclose", "close", 1)
    elif k == 0x511130:
        p.args = [D, B]
        w(B + 12, C)
        for i in range(4):
            w(C + 44 + 4 * i, 2 + n + i)
    elif k == 0x4165E0:
        w(A + 96, 0x40 + 4 * n)
        p.args = [A]
    elif k == 0x490C00:
        p.args = [B, 100, 5 + n * 2]
        call(0x61E45F, "_ftell", "tell", 1, 100 if n else 108)
        call(0x61E6A3, "_fseek", "seek", 3)
    elif k == 0x4EAC13:
        w(A + 8, B, C)
        w(A + 28, 1 + 2 * n)
        p.args = [4, 9]
        p.result = "void"
    elif k == 0x4EB150:
        w(A + 16, 17 + n)
    elif k == 0x5279E0:
        g(0x6CEC88, n)
        p.args = [B, 1 + 5 * n]
        p.put(B, b"/intro.avi\0")

        def profile(args):
            p.put(args[3], b"movies\0")
            return 6

        def open_movie(args):
            path = bytes(p.uc.mem_read(args[0], 32)).split(b"\0")[0]
            p.events.append(["open", path.decode("ascii")])
            return 0

        p.api("GetPrivateProfileStringA", profile, 24)
        p.hook(0x61CC96, "_fopen", open_movie)
    elif k == 0x529050:
        w(A, 600 + (400 << 16))
        p.args = [A, 30 - n, 100]
    elif k == 0x577330:
        w(A, 1)
        w(A + 4, B if n else 0)
        w(B, C)
        w(C + 32, 0x310F000)
        p.callbacks[0x310F000] = (lambda v: p.events.append(["stop", v[0]]), 4)
        p.result = "void"
    elif k == 0x594E00:
        w(A + 10420, 0)
        g(0x697F9C, 1, 2, 3)
        g(0x697F5C, 4, 5, 6)
        p.args = [D, A, -1 if n else 0]
    elif k == 0x4012C0:
        call(0x510690, "_Loading_Step", "step", 0, n)
        p.result = "void"
    elif k == 0x447960:
        w(A, B)
        p.args = [3 * n]
    elif k == 0x448060:
        w(A, B, B + 12 * (3 + n), B + 120)
        p.result = "void"
    elif k == 0x45A9B0:
        w(A, *range(1, 10))
        p.args = [A, A + 12 * (1 + n), D]
    elif k in (0x47FCF0, 0x47FE20):
        g(0x68CF00, B, B + 12)
        g(0x68CF38, B, B + 12)
        w(A + 12, 1)
        w(A + 112, C, E, B, B + 4)
        w(B, C)
        w(C + 16, n)
        w(E + 16, 2 - n)
        # One road geometry object with a real (fake-device) SegmentCount slot.
        w(C, B + 0x100)
        w(B + 0x100 + 24, 0x310F000)
        p.callbacks[0x310F000] = (lambda _: 3, 0)
        p.args = [D, A]
    elif k == 0x483CB0:
        p.put(B, b"cargo\0" if n else b"route!\0")
        g(0x68D7C0, C, C + 1, C + 64)
        p.args = [B]
        p.result = "void"
    elif k == 0x490BA0:
        p.args = [B, C, D, E]

        def read(v):
            index = len(p.events)
            p.events.append(["read", v[1], v[2], v[3]])
            if index == 0 and not n:
                return 0
            w(v[0], (0x41424344, 27)[index])
            return 1

        p.hook(0x61E20F, "_fread", read)
        call(0x61E45F, "_ftell", "tell", 1, 42)
    elif k == 0x4CCA70:
        g(0x7025B8, A)
        floats(A + 548, 4 + 4 * n)
        p.result = "fp"
    elif k == 0x4DD900:
        p.ecx = D
        p.args = [7 + n]
        call(0x5EF3D0, "_Objects_Find", "find", 1, B)
    elif k == 0x4E9DE7:
        g(0x6974CC, 1)
        g(0x697580, B)
        p.args = [-1 if n else 0]
    elif k == 0x418880:
        w(A, B + 8 * n)
    elif k == 0x422390:
        g(0x68CCA8, B)
        g(0x68CFD8, C)
        w(C, E)
        w(A + 84, 2)
        w(A + 96, 17 + n)
        w(A + 508, D if n else 0)
        w(D + 4, 0)
        p.args = [D + 64, A]
    elif k == 0x44B730:
        p.args = [8 + n]
    elif k == 0x44B970:
        g(0x68D204, 0)
        p.ecx = A + 4 * n

        def exchange(v):
            old = p.read(v[0])
            w(v[0], v[1])
            return old

        p.api("InterlockedExchange", exchange, 8)
    elif k in (0x455940, 0x455950):
        w(A, 10 + n, 20 - n)
    elif k == 0x477D60:
        w(A + 96, 0x8000004)
        w(A + 100, 257 if n else 0)
        p.args = [A]
        p.result = "void"
    elif k == 0x4A7530:
        p.ecx = D + n * 16
        p.fixups[p.ecx] = 0x64BF70
    elif k == 0x4E2FB4:
        floats(A, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1 + n, 2, 3)
        p.args = [A, *[D + i * 4 for i in range(6)]]
        p.result = "void"
    elif k == 0x4EB1B0:
        w(A + 60, 13, 15, 17)
        p.args = [D, -1 if n else 0]
    elif k == 0x52F860:
        p.args = [B if n else 0, 2, f32(3), 4]
        p.result = "void"
        call(0x4E51B1, "_Sound_PlayRange", "play", 4)
    elif k == 0x56AE90:
        g(0x6F3430, A)
        p.args = [2, B, 5, 8]
        p.result = "void"
        call(0x56AFE0, "@Notice_Set@16", "set", 2, n, 8)
    elif k == 0x5AB110:
        p.args = [0xA0B0 + n, B]
        p.result = "void"
        p.hook(
            0x61ED24, "_fputc", lambda v: p.events.append(["byte", v[0] & 255, v[1]])
        )
    elif k == 0x5ACB60:
        g(0x702984, 1 if n else 5)
        g(0x702988, 0)
        g(0x702660, 0, 0)
        w(A, 0xFF1234, 287)
        p.args = [A]
    elif k == 0x600B60:
        g(0x6843E0, 0)
        g(0x6843D8, *dbl(0.25 + n))
        p.result = "fp"
        p.tsc = 0x100000080 + n
    elif k == 0x409F10:
        kind, flags, mode, side = (
            (2, 0, 0, 0),
            (0, 0, 0, 0),
            (3, 64, 0, 0),
            (3, 64, 1, 0),
            (2, 4, 0, 1),
            (2, 8, 0, 0),
            (2, 4, 0, 0),
        )[n]
        w(B, flags)
        w(B + 20, kind)
        w(A, mode, side)
        p.args = [B]
        p.result = "fp"
    elif k == 0x44AF80:
        w(A, B, B + 20, B + 24)
        w(B, 3, 4, 6, 8, 2)
        p.args = [B + 4, B + 4 + 4 * n]
    elif k == 0x456CB0:
        w(A + 8, B)
        w(B + 4, 0x12345678 - n)
        p.args = [A]
    elif k == 0x460910:
        w(B, C, 5, 0x10000 if n else 4)
        w(C + 4, 3)
        p.args = [D, B]
        p.result = "void"
    elif k == 0x473670:
        p.args = [A]
        w(A + 516, B if n else 0)
        w(B + 4, -1 if n < 2 else 0)
        if n >= 2:
            g(0x68CFD8, C)
            w(C, C + 64)
            w(C + 64 + 48, *dbl(200))
            g(0x68CC08, E)
            w(B + 16, B + 256)
            w(B + 40, 3 if n == 3 else 0)
            for index in range(19):
                w(E + 24 * index + 12, C + 256)
                w(B + 256 + 16 * index + 8, *dbl(index + 1))
            w(C + 256, *dbl(0.5))
        p.result = "fp"
    else:
        raise ValueError(f"missing case {va:x}")
