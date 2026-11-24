"""Contract checks for audited entries. Each image must also satisfy the expected result."""

import argparse
import math
import json
from pathlib import Path
import re
import struct
import sys
import pefile
from unicorn import x86_const as x

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "call100"))
from compare import Image, Probe, sha  # noqa: E402
from cases import A, B, C, D, E, f32  # noqa: E402

# Original address, present linker name, earlier linker name, variant count.
ENTRIES = [
    (0x4786A0, "_Player_Place", "", 4),
    (0x466780, "_LexCompare", "", 6),
    (0x4EBAE0, "@Engine_AddContact@20", "", 4),
    (0x4EBB70, "@Engine_AddPush@16", "", 4),
    (0x4F7E10, "@Body_SetLinearMomentum@20", "@Body_SetVec9012@20", 3),
    (0x4F7E30, "@Body_SetSpin@20", "", 3),
    (0x4F8030, "@Body_CopyWorldSpin@12", "@Body_GetVec9024@12", 3),
    (0x4F8060, "@Body_Matrix22a4@12", "", 3),
    (0x4F8090, "@Body_Matrix2274@12", "", 3),
    (0x4F80C0, "@Body_CopySavedForce@12", "@Body_GetVec9084@12", 3),
    (0x4F80F0, "@Body_CopyTotalTorque@12", "@Body_GetVec9144@12", 3),
    (0x4F8120, "@Body_GetAngularTimeStep@8", "@Body_Get9508@8", 3),
    (0x4F7FD0, "@Body_CopyLinearMomentum@12", "@Body_GetVec9012@12", 3),
    (0x4F8000, "@Body_CopyLocalAngularVelocity@12", "@Body_GetVec9048@12", 3),
    (0x57C8B0, "_Plane_HeightFromNearestRecord", "_Records_Height", 5),
    (0x4B0ED0, "@SchNumber_IsExact@8", "", 6),
    (0x4B0EE0, "@SchNumber_IsInexact@8", "", 6),
    (0x48AA00, "_BigInt_SetLimbs", "_WStr_Make", 6),
    (0x48AB80, "_BigInt_ResizeLimbs", "_WStr_Resize", 6),
    (0x48AED0, "_Int_SetZero", "", 3),
    (0x4E3170, "_Matrix_ScaleColumns", "", 4),
    (0x58D1F0, "@Body_NextWorldPlane@12", "@Records_Next@12", 4),
    (0x52F710, "_Sound_RestartScaledByOption388", "_Sound_Play388", 5),
    (0x579370, "@Pooled_MarkForRelease@8", "@Pooled_Set4@8", 3),
    (0x47FF20, "_Road_StartOf", "", 3),
    (0x4800A0, "_Road_EndOf", "", 3),
    (0x457A20, "_RoadHop_FindByNode", "_Find12ByField0", 5),
    (0x4573D0, "_RoadHop_FindByEdge", "_Find12ByField4", 5),
    (0x573FE0, "_Surface_ColorFromRGB", "_Obj573fe0", 7),
    (0x4E298B, "_WorldVec3_LengthSquared", "_Vec3_LengthSq4e298b", 8),
    (0x560F50, "@Stack_Peek@12", "", 5),
    (0x414890, "_AI_StoreInt", "", 4),
    (0x423F20, "@AI_CopyRoadPosition@12", "@Rec_Copy32@12", 3),
    (0x4BDEF0, "@Vec3_SetThis@20", "", 3),
    (0x41D300, "@Int_Copy@12", "", 3),
    (0x4A36C0, "@SchHandle_Release@8", "", 4),
    (0x56AFE0, "@Notice_Set@16", "", 6),
]


def run(image, entry, va, n):
    p = Probe(image, "")
    expected_mem = {}
    expected_calls = []
    expected_return = None
    p.result = "void"
    stack = 4
    if va == 0x4786A0:
        p.g(0x68CF00, B, B + 8)
        p.w(B, C, C + 256)
        selected = n % 2
        points = ((3.5, -2, 8), (12, 9, -3))
        for i, xyz in enumerate(points):
            p.put(C + 256 * i + 48, struct.pack("<3d", *xyz))
        p.w(A + 44, -1)
        p.w(A + 88, 0x12345678)
        vehicle = (-1, 9, 12, -1)[n]
        p.w(A + 84, vehicle)
        p.w(A + 24, D, D + 12 * n, D + 48)
        p.w(A + 72, E, E + 40 * n, E + 160)
        pos = struct.pack("<iidiiii", -1, selected, 0.25, 0, 0, 23, 99)
        p.args = [A, *struct.unpack("<8I", pos)]

        def exists(args):
            p.events.append(["exists", args[0]])
            return int(n == 2)

        def release(args):
            p.events.append(["release", args[0]])
            return 0

        p.hook(0x4D8A40, "_Vehicle_Exists", exists)
        p.hook(0x4D8AE0, "_Vehicle_Release", release)
        if vehicle != -1:
            expected_calls = [["exists", vehicle]]
        if n == 2:
            expected_calls.append(["release", vehicle])
        expected_mem[A + 28] = struct.pack("<I", D)
        expected_mem[A + 76] = struct.pack("<I", E)
        expected_mem[A + 872] = b"\x01"
        expected_mem[A + 40] = pos[:28] + b"\xff" * 4
        expected_mem[A + 312] = struct.pack("<3d", *points[selected])
    elif va == 0x466780:
        first, second = (
            ([], []),
            ([], [7]),
            ([7], []),
            ([-1], [1]),
            ([19, -8], [19, -9]),
            ([7], [7]),
        )[n]
        p.put(A, bytes(x % 256 for x in first))
        p.put(B, bytes(x % 256 for x in second))
        p.args = [A, A + len(first), B, B + len(second)]
        p.result = "eax"
        expected_return = ((first > second) - (first < second)) & 0xFFFFFFFF
    elif va in (0x4EBAE0, 0x4EBB70):
        contact = va == 0x4EBAE0
        cursor = 192 if contact else 196
        index = (0, 1, 4, 5)[n]
        record = bytearray(b"\xa5" * 320)
        p.put(A + 188, record)
        p.w(A + cursor, index)
        record[cursor - 188 : cursor - 184] = struct.pack("<I", min(index + 1, 5))
        offsets = (200, 260, 320) if contact else (380, 440)
        p.args = [B, C, D][: len(offsets)]
        for k, offset in enumerate(offsets):
            blob = struct.pack("<III", k + 1, 0x80000000, 0x7FC00010 + n + k)
            p.put(p.args[k], blob)
            if index < 5:
                record[offset + index * 12 - 188 : offset + index * 12 - 176] = blob
        expected_mem[A + 188] = bytes(record)
        p.result = "eax"
        expected_return = index if index < 5 else 0
        stack = 4 + 4 * len(offsets)
    elif va == 0x4F7E10:
        bits = [0x7FC10000 + n, 0x80000000, 27 + n]
        p.args = bits
        stack = 16
        expected_mem[A + 9012] = struct.pack("<3I", *bits)
    elif va == 0x4F7E30:
        v = (3 + n, -4, 8)
        p.args = [f32(k) for k in v]
        p.put(A + 8964, struct.pack("<9f", 0, 1, 0, -1, 0, 0, 0, 0, 1))
        p.put(A + 8008, struct.pack("<3f", 2, 4, 8))
        stack = 16
        expected_mem[A + 9024] = struct.pack(
            "<9f", *v, -v[1], v[0], v[2], -v[1] / 2, v[0] / 4, v[2] / 8
        )
    elif va in (0x4F8060, 0x4F8090):
        off1, off2 = (8868, 8772) if va == 0x4F8060 else (8820, 8964)
        lhs = (2, 0, 0, 0, 3, 0, 0, 0, 4, 2 + n, -3, 5)
        rhs = (0, 1, 0, -1, 0, 0, 0, 0, 1, 5, 2, -1)
        p.put(A + off1, struct.pack("<12f", *lhs))
        p.put(A + off2, struct.pack("<12f", *rhs))
        expected_mem[D] = struct.pack("<12f", 0, 2, 0, -3, 0, 0, 0, 0, 4, 8, 4 + n, 4)
        p.args = [D]
        p.result = "eax"
        expected_return = D
        stack = 8
    elif va == 0x4F8120:
        val = (0.5, 1, 2)[n]
        p.put(A + 9508, struct.pack("<f", val))
        p.result = "fp"
        fraction, exp = math.frexp(val)
        expected_return = (int(fraction * 2**64), exp + 16382)
    elif va in (0x4F8030, 0x4F80C0, 0x4F80F0):
        offset = {0x4F8030: 9024, 0x4F80C0: 9084, 0x4F80F0: 9144}[va]
        blob = struct.pack("<3I", 0x80000000, offset + n, 0x7FC21000 + n)
        p.put(A + offset, blob)
        p.put(D - 8, b"\xa5" * 28)
        p.args = [D]
        expected_mem[D - 8] = b"\xa5" * 8 + blob + b"\xa5" * 8
        p.result = "eax"
        expected_return = D
        stack = 8
    elif va in (0x4F7FD0, 0x4F8000):
        p.w(A + 9012, 0x80000000, n, 0x7FC01000 + n)
        p.w(A + 9048, 0x3F800000, n + 91, 0x7F800000)
        source = A + (9012 if va == 0x4F7FD0 else 9048)
        p.put(D - 4, b"\xa5" * 20)
        expected_mem[D - 4] = (
            b"\xa5" * 4 + bytes(p.uc.mem_read(source, 12)) + b"\xa5" * 4
        )
        p.args = [D]
        p.result = "eax"
        stack = 8
        expected_return = D
    elif va == 0x57C8B0:
        p.put(A + 52, struct.pack("<6f", 0, 0, 5, 0, 0, 1))
        p.put(A + 208 + 52, struct.pack("<6f", 10, 0, 20, 1, 0, 1))
        px = (0, 9, 5, 16, 3)[n]
        count = (2, 2, 2, 2, 0)[n]
        p.args = [f32(px), f32(0), A, count]
        p.result = "fp"
        height = (5, 21, 5, 14, 5)[n]
        frac, exp = math.frexp(height)
        expected_return = (int(frac * 2**64), exp + 16382)
    elif va in (0x4B0ED0, 0x4B0EE0):
        flags = (0, 1, 2, 3, 0xFFFFFFFE, 0xFFFFFFFF)[n]
        p.w(A + 8, flags)
        p.result = "eax"
        expected_return = (flags & 1) if va == 0x4B0EE0 else int(not flags & 1)
    elif va in (0x48AA00, 0x48AB80, 0x48AED0):
        old, cap, previous, length = (
            (0, 0, 0, 0),
            (B, 8, 3, 2),
            (B, 8, 2, 5),
            (B, 2, 2, 5),
            (B, 0, 2, 5),
            (0, 0, 0, 8),
        )[n]
        digits = (19, 77, 8, 11, 23, 29, 31, 37)
        p.put(B, struct.pack("<11H", previous, cap, 0, *digits))
        p.put(C, struct.pack("<8H", *reversed(digits)))
        p.put(D, b"\xa5" * 64)

        def alloc(args):
            p.events.append(["new", args[0]])
            return D

        def free(args):
            p.events.append(["delete", args[0]])
            return 0

        p.hook(0x632643, "??2@YAPAXI@Z", alloc)
        p.hook(0x63266C, "??3@YAXPAX@Z", free)
        p.result = "eax"
        if va == 0x48AED0:
            old = (0, B, B)[n]
            if n == 2:
                p.put(B + 2, b"\0\0")
            p.args = [old]
            expected_return = B if n == 1 else p.image.data(0x6703F0)
            if n == 1:
                expected_mem[B] = struct.pack("<3H", 0, cap, 1)
        else:
            replace = not old or length > cap
            dest = D if replace else B
            capacity = cap
            if replace:
                size = 16
                while size < 12 + length * 2:
                    size *= 2
                capacity = (size - 10) // 2
                expected_calls.append(["new", size - 4])
                if old and cap:
                    expected_calls.append(["delete", old])
            if va == 0x48AA00:
                copied = min(length, (0, 2, 2, 3, 1, 6)[n])
                sign = n % 2
                p.args = [old, C, copied, sign, length]
                values = list(reversed(digits))[:copied] + [0] * (length - copied)
            else:
                sign = 0 if old else 1
                p.args = [old, length]
                used = min(previous, length)
                values = list(digits[:used]) + [0] * (length - used)
            expected_mem[dest] = struct.pack(
                "<" + "H" * (3 + length), length, capacity, sign, *values
            )
            expected_return = dest
    elif va == 0x4E3170:
        vals = (1, 2, -3, 4, 5, 6, 7, -8, 9, 31, 32, 33)
        factors = ((1, 1, 1), (2, -4, 0.5), (0, 1, -1), (3, 2, 1))[n]
        p.put(A, struct.pack("<12f", *vals))
        p.put(B, struct.pack("<3f", *factors))
        dest = A if n in (1, 3) else D
        p.args = [dest, A, B]
        p.result = "eax"
        expected_return = dest
        expected_mem[dest] = struct.pack(
            "<12f", *(vals[i] * factors[i % 3] for i in range(9)), 0, 0, 0
        )
    elif va == 0x58D1F0:
        count, cursor = ((0, 0), (2, 0), (2, 1), (2, 2))[n]
        data = bytes((i * 19 + 3) & 255 for i in range(56))
        p.put(A + 3988, data)
        p.w(A + 5132, count, cursor)
        p.put(D, b"\xa5" * 28)
        p.args = [D]
        expected_return = cursor + 1 if cursor < count else 0
        expected_mem[D] = (
            data[cursor * 28 : cursor * 28 + 28] if expected_return else b"\xa5" * 28
        )
        expected_mem[A + 5136] = struct.pack("<I", expected_return)
        p.result = "eax"
        stack = 8
    elif va == 0x52F710:
        opts = (0, 0, B, B, B)[n]
        level = (0, 100, 0, 50, 125)[n]
        p.g(0x6CEC78, opts)
        p.put(B + 388, struct.pack("<h", level))
        p.args = [A if n else 0, f32(0.75)]

        def restart(args):
            p.events.append(["restart", p.uc.reg_read(x.UC_X86_REG_ECX), *args[:2]])
            return 0

        p.hook(0x5FDAF0, "@Sound_Restart@16", restart, 8)
        val = f32(0.75 * (level / 100 if opts else 1))
        if n:
            expected_calls = [["restart", A, val, val]]
    elif va == 0x579370:
        p.w(A, 0x1234, (0, 1, 0x12345678)[n], 0xA5)
        expected_mem[A] = struct.pack("<3I", 0x1234, 1, 0xA5)
    elif va in (0x47FF20, 0x4800A0):
        nodes = (2, 0, 1)
        p.g(0x68CF38, B, B + 12)
        p.g(0x68CF00, B + 0x100, B + 0x10C)
        for i in range(3):
            edge = A + i * 256
            geom = C + 0x800 + i * 256
            p.w(B + 4 * i, edge)
            p.w(B + 0x100 + 4 * i, C + 256 * i)
            p.w(C + 256 * i + 16, nodes[i])
            p.w(
                edge + 112, C + i * 256, C + (i + 1) % 3 * 256, geom, geom + (i + 1) * 4
            )
            for index in range(i + 1):
                obj = E + i * 256 + index * 16
                p.w(geom + index * 4, obj)
                p.w(obj, E + 0x800)
        p.w(E + 0x800 + 24, 0x310F000)

        def segment_count(_):
            p.events.append(["segments", p.uc.reg_read(x.UC_X86_REG_ECX)])
            return 2 + n

        p.callbacks[0x310F000] = (segment_count, 0)
        end = va == 0x4800A0
        p.args = [D, n]
        p.put(D - 8, b"\xa5" * 48)
        out = struct.pack(
            "<iidiiii",
            n,
            nodes[(n + int(end)) % 3],
            float(end),
            1 + n if end else 0,
            n if end else 0,
            0,
            -1,
        )
        expected_mem[D - 8] = b"\xa5" * 8 + out + b"\xa5" * 8
        if end:
            expected_calls = [["segments", E + n * 256 + n * 16]]
        p.result = "eax"
        expected_return = D
    elif va in (0x457A20, 0x4573D0):
        nodes = (13, 7, 13, 99)
        edges = (E + 32, E + 16, E + 32, E)
        for i in range(4):
            p.w(A + 12 * i, nodes[i], edges[i], 0xA5)
        keys = nodes if va == 0x457A20 else edges
        start, stop, key, index = (
            (0, 0, keys[0], 0),
            (0, 4, keys[0], 0),
            (1, 4, keys[0], 2),
            (0, 4, keys[3], 3),
            (0, 4, 42, 4),
        )[n]
        p.args = [A + start * 12, A + stop * 12, key]
        p.result = "eax"
        expected_return = A + index * 12
    elif va == 0x573FE0:
        mode = (-1, 0, 1, 1, 2, 2, 3)[n]
        rgb = [(37 * n + 81) % 256, n * 31, 255 - n]
        p.w(A + 808, mode)
        p.args = [A, *rgb]
        for addr, sym, pop, label in (
            (0x5B9660, "@Palette_Nearest@20", 12, "palette"),
            (0x5B6210, "_Pixel_Pack", 0, "packed"),
        ):

            def color(args, label=label):
                p.events.append(
                    [
                        label,
                        *(
                            [p.uc.reg_read(x.UC_X86_REG_ECX)]
                            if label == "palette"
                            else []
                        ),
                        *args[:3],
                    ]
                )
                return 0x1234AB + n

            p.hook(addr, sym, color, pop)
        p.result = "eax"
        expected_return = (
            (0x1234AB + n) & 255 if mode == 1 else 0x1234AB + n if mode == 2 else 0
        )
        if mode in (1, 2):
            expected_calls = [["palette", A, *rgb] if mode == 1 else ["packed", *rgb]]
    elif va == 0x4E298B:
        v = (
            (0, 0, 0),
            (3, 4, -12),
            (0.25, -0.5, 2),
            (0.125, 4, 1 / 32),
            (2**25, 0, 0),
            (4096, 1, 0),
            (2**80, 0, 0),
            (2**-90, 0, 0),
        )[n]
        p.put(A, struct.pack("<3f", *v))
        p.args = [A]
        p.result = "fp"
        value = sum(t * t for t in v)
        mantissa, exp = math.frexp(value)
        significand, exponent = (
            (0, 0) if value == 0 else (int(mantissa * 2**64), 16382 + exp)
        )
        expected_return = (significand, exponent)
    elif va == 0x560F50:
        p.put(A, struct.pack("<4h", 10, -7, 116, -20))
        p.w(A + 3000, 3)
        p.args = [n]
        stack = 8
        p.result = "eax"
        expected_return = (-20, 116, -7, 10, 0)[n] & 65535
    elif va in (0x414890, 0x41D300):
        val = (0xFFFFFFFF, 5, 0, 0x80000000)[n]
        p.w(B, val)
        p.w(A, 0x11223344)
        if va == 0x41D300:
            p.args = [A if n == 2 else B]
            stack = 8
            if n == 2:
                val = 0x11223344
        else:
            p.args = [A, val]
        expected_mem[A] = struct.pack("<I", val)
        p.result = "eax"
        expected_return = A
    elif va == 0x423F20:
        blob = bytes((i * 19 + n * 31) & 255 for i in range(64))
        p.put(A, blob)
        p.put(D - 8, b"\xa5" * 48)
        p.args = [D]
        expected_mem[D - 8] = b"\xa5" * 8 + blob[16:48] + b"\xa5" * 8
        p.result = "eax"
        expected_return = D
        stack = 8
    elif va == 0x4BDEF0:
        bits = (
            [f32(-2.5), 0, f32(7)],
            [0x80000000, 0x7FC12345, 1],
            [0xFFFFFFFF, 0x7F800000, 0xFF800000],
        )[n]
        p.put(B - 4, b"\xa5" * 20)
        p.ecx = B
        p.args = bits
        expected_mem[B - 4] = b"\xa5" * 4 + struct.pack("<3I", *bits) + b"\xa5" * 4
        p.result = "eax"
        expected_return = B
        stack = 16
    elif va == 0x4A36C0:
        ptr, flags, count = ((0, 0, 4), (B, 0x10000, 1), (B, 0, 3), (B, 0, 1))[n]
        p.w(A, ptr, 3, flags)
        p.w(B, C, count)
        p.w(C + 12, 0x310F000)

        def release(args):
            p.events.append(["delete", p.uc.reg_read(x.UC_X86_REG_ECX), args[0]])
            return 0

        p.callbacks[0x310F000] = (release, 4)
        expected_mem[B + 4] = struct.pack("<I", count - int(n >= 2))
        if n == 3:
            expected_calls = [["delete", B, 1]]
    elif va == 0x56AFE0:
        cur, level = ((0, 1), (1, 1), (4, 3), (4, 7), (5, 9), (3, 8))[n]
        p.w(A + 60, cur)
        p.g(0x696CA0, B)
        p.put(B + 2968, struct.pack("<d", 1.25))
        p.args = [level, 0]
        p.result = "eax"
        expected_return = int(n in (0, 2, 5))
        stack = 12
        expected_mem[A + 60] = struct.pack("<I", level if expected_return else cur)
        expected_mem[A + 32] = struct.pack("<d", 1250.0 if expected_return else 0.0)
    result = p.run(entry)
    if va == 0x560F50:
        result["result"] &= 65535
    if va == 0x4E298B:
        result["x87_status"] = p.uc.reg_read(x.UC_X86_REG_FPSW)
    expected_ok = (
        result["result"] == expected_return
        and result["stack"] == stack
        and p.events == expected_calls
        and all(
            bytes(p.uc.mem_read(addr, len(blob))) == blob
            for addr, blob in expected_mem.items()
        )
    )
    if va == 0x48AED0 and expected_ok and expected_return != B:
        result["result"] = "shared zero at 0x6703f0"
    return result, expected_ok


def main():
    ap = argparse.ArgumentParser()
    for key in ("original", "candidate", "map", "out"):
        ap.add_argument("--" + key, required=True, type=Path)
    ap.add_argument("--control", action="store_true")
    args = ap.parse_args()
    if (
        sha(args.original)
        != "31fbd1dbbe7706183f09a3688482791255db2bfb557c02675374b96796ce2ebd"
    ):
        raise ValueError("Wrong original")
    mapping = args.map.read_text()
    if (
        f"Timestamp is {pefile.PE(str(args.candidate), fast_load=True).FILE_HEADER.TimeDateStamp:08x}"
        not in mapping
    ):
        raise ValueError("Wrong MAP")
    syms = {
        n: int(address, 16)
        for n, address in re.findall(
            r"^\s+\w+:\w+\s+(\S+)\s+([\da-f]{8})\s+", mapping, re.M
        )
    }
    left = Image(args.original)
    right = Image(args.candidate, syms)
    rows = []
    passed = True
    for va, sym, old, count in ENTRIES:
        if args.control and va != 0x457A20:
            continue
        key = sym if sym in syms else old
        entry = syms[key]
        if args.control:
            entry = syms.get("_RoadHop_FindByEdge") or syms["_Find12ByField4"]
        cases = []
        for n in range(count):
            a, ok = run(left, va, va, n)
            b, ok2 = run(right, entry, va, n)
            match = a == b and ok and ok2
            cases.append(
                dict(
                    input=n,
                    match=match,
                    original_expected=ok,
                    candidate_expected=ok2,
                    original=a,
                    candidate=b,
                )
            )
        passed &= (
            (
                all(c["original_expected"] for c in cases)
                and any(not c["candidate_expected"] for c in cases)
            )
            if args.control
            else all(c["match"] for c in cases)
        )
        print(f"{va:06x}", key, "".join("+" if c["match"] else "-" for c in cases))
        rows.append(dict(stock_va=f"{va:06x}", symbol=key, cases=cases))
    args.out.write_text(
        json.dumps(
            dict(
                original=sha(args.original),
                candidate=sha(args.candidate),
                map=sha(args.map),
                fixture=sha(Path(__file__)),
                probe=sha(Path(__file__).resolve().parents[1] / "call100/compare.py"),
                control=args.control,
                rows=rows,
            ),
            indent=2,
        )
        + "\n"
    )
    return int(not passed)


if __name__ == "__main__":
    sys.exit(main())
