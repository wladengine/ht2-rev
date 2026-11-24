"""Replay typed x86 calls on two PE images. This offline comparison runs no Windows APIs.

The ledger provides the calling convention, result type and ordered parameter types.
Unclassified memory, incomplete calls and ABI violations produce distinct outcomes.
"""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MEM_READ, UcError
from unicorn import x86_const as x

from execute import ARENA, BASE, REGS, SP, STACK, STOP, Session
from pair_input import load_pairs


ARENA_SIZE = 0x48000
GAME_DATA = (
    ("_KingRdata", 0x64B000, 0x66C000),
    ("_KingData", 0x66C000, 0x68A820),
    ("_KingBss", 0x68A820, 0x77FB04),
)
CALLEE_SAVED = (0x11550000, 0x11550001, 0x11550002, 0x11550003)
SCALAR = (0, 1, 2, 0xffffffff, 3, 1, 8, 0x80000000, 0x33333333, 0)
FLOAT = (0., 1.25, -.5, 4097., -1., .125, 3.5, -7., .5, 64.)


def pack(value):
    return struct.pack("<I", value & 0xffffffff)


def f32(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def fixture_arena(case):
    def word(i):
        return (
            0, (0, 1, 2, 3, 7, 0xffffffff, 0, 2)[i % 8], f32((i % 9 - 4) * .25),
            i * 21376471 + 0x51973, 1, ARENA + 0x38000 + (i % 4) * 0x400,
            f32(1.), 0xffffffff, 0x55555555, 0,
        )[case]
    return b"".join(pack(word(i)) for i in range(ARENA_SIZE // 4))


def validate_decorated_abi(row):
    if row.get("entry_abi") != "standalone":
        return
    match = re.fullmatch(r"([@_])[^@]+@(\d+)", row["candidate_symbol"])
    if not match:
        return
    convention = "__fastcall" if match[1] == "@" else "__stdcall"
    total = 0
    for kind in row["parameters"].split():
        if kind.startswith("record:"):
            total += (int(kind.rsplit(":", 1)[1]) + 3) & ~3
        else:
            total += 8 if kind in ("double", "wide") else 4
    if row["calling_convention"] != convention or total != int(match[2]):
        raise ValueError(f"Declared ABI disagrees with {row['candidate_symbol']} at {row['stock_va']}")


def registers_and_stack(uc, types, convention, case, argument_bits=None):
    registers, stack = [], []
    for index, kind in enumerate(types):
        value = FLOAT[case] * (index + 1)
        if kind == "double":
            words = list(struct.unpack("<II", struct.pack("<d", value)))
        elif kind == "wide":
            words = [SCALAR[case], 0]
        elif kind == "ptr":
            words = [ARENA + 0x1000 + 0x3800 * index]
        elif kind == "float":
            words = [f32(value)]
        elif kind.startswith("record:"):
            _, name, size = kind.split(":")
            words = [0 if case == 0 else i + 1 for i in range(int(size) // 4)]
            if name == "PositionId":
                record = struct.pack("<iidiiii", -1, case % 3, (0., 1., .5)[case % 3], 0, 0, 0, -1)
                words = list(struct.unpack("<8I", record))
            elif name in ("DVec3", "Vec3"):
                record = struct.pack("<3" + ("d" if name == "DVec3" else "f"), value, -value, .5 * value)
                words = list(struct.unpack("<" + "I" * (len(record) // 4), record))
            if len(words) * 4 != int(size):
                raise ValueError(f"Record layout disagrees with compiler size for {name}")
        elif kind == "format":
            pointer = ARENA + 0x2e000
            uc.mem_write(pointer, b"state test\0")
            words = [pointer]
        elif kind == "callback":
            # This address is outside executable memory. A case needing it is incomplete.
            words = [STOP + 0x2000]
        elif kind in ("byte", "bool", "word", "int"):
            words = [SCALAR[case]]
        else:
            raise ValueError(f"Unsupported argument type {kind}")
        if argument_bits and index in argument_bits:
            bits = argument_bits[index]
            if bits < 0 or bits.bit_length() > 32 * len(words):
                raise ValueError(f"Argument {index} exceeds its ABI width")
            words = [(bits >> (32 * i)) & 0xffffffff for i in range(len(words))]
        register_arg = convention == "__fastcall" and len(registers) < 2 and len(words) == 1
        if register_arg and not kind.startswith("record:"):
            registers.extend(words)
        else:
            stack.extend(words)
    volatile = [0xb58e33c1 if case % 2 else 0x13579b, ARENA + 0x1000, ARENA + 0x9000]
    volatile[1:1 + len(registers)] = registers
    for register, value in zip(REGS, (*volatile, *CALLEE_SAVED)):
        uc.reg_write(register, value)
    uc.reg_write(x.UC_X86_REG_ESP, SP)
    uc.mem_write(SP, struct.pack("<" + "I" * (len(stack) + 1), STOP, *stack))
    return len(stack) * 4


class StateImage(Session):
    def __init__(self, exe, symbols, inputs):
        super().__init__(exe)
        self.inputs = inputs
        self.segments = [(symbols.get(name, lo), lo, hi - lo) for name, lo, hi in GAME_DATA]
        self.uc.mem_map(ARENA + 0x10000, ARENA_SIZE - 0x10000)
        self.references = set()
        for name, address in symbols.items():
            if not re.match(r"^\?\w+@@3AA", name) or not BASE <= address <= BASE + len(self.img) - 4:
                continue
            target = struct.unpack_from("<I", self.img, address - BASE)[0]
            if self.game_address(address) is None and self.game_address(target) is not None:
                self.references.add(address)
        self.reads, self.writes = set(), set()
        self.uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, self.track,
                         begin=BASE, end=ARENA + ARENA_SIZE - 1)

    def game_address(self, va):
        for actual, original, size in self.segments:
            if actual <= va < actual + size:
                return original + va - actual
        return None

    def track(self, uc, access, address, size, value, data):
        (self.reads if access == UC_MEM_READ else self.writes).update(range(address, address + size))

    def memory(self, addresses, read):
        # Capture exact affected bytes, including stores which restore the old value.
        runs = []
        for address in sorted(addresses):
            if read and address & ~3 in self.references:
                continue
            logical = address if ARENA <= address < ARENA + ARENA_SIZE else self.game_address(address)
            if logical is None:
                return None
            if runs and runs[-1][0] + runs[-1][2] == logical and runs[-1][1] + runs[-1][2] == address:
                runs[-1][2] += 1
            else:
                runs.append([logical, address, 1])
        result = []
        for logical, actual, size in sorted(runs):
            if not read:
                result.append((logical, bytes(self.uc.mem_read(actual, size))))
            elif logical < ARENA:
                initial = bytearray(self.image_initial[actual - BASE:actual - BASE + size])
                for offset in range(size):
                    if logical + offset in self.seeded:
                        initial[offset] = self.seeded[logical + offset]
                result.append((logical, bytes(initial)))
            else:
                result.append((logical, size))
        return result

    def replay(self, entry, row, case, edits, argument_bits=None):
        if row.get("entry_abi") == "parent exception frame":
            return "requires parent exception frame", None
        uc = self.uc
        for page in self.dirty:
            start = page - BASE
            uc.mem_write(page, self.image_initial[start:start + 4096])
        self.dirty.clear()
        uc.context_restore(self.context)
        self.invalid_low = self.clock_used = False
        uc.mem_write(0, b"\xff" * 4)
        uc.mem_write(ARENA, self.inputs[case])
        uc.mem_write(STACK, bytes(65536))
        self.seeded = {}
        for logical, raw in edits:
            if ARENA <= logical and logical + len(raw) <= ARENA + ARENA_SIZE:
                uc.mem_write(logical, raw)
                continue
            for actual, original, size in self.segments:
                if original <= logical and logical + len(raw) <= original + size:
                    where = actual + logical - original
                    uc.mem_write(where, raw)
                    self.seeded.update((logical + i, byte) for i, byte in enumerate(raw))
                    self.dirty.update(range(where & ~4095, where + len(raw), 4096))
                    break
            else:
                raise ValueError(f"Unmapped fixture edit at {logical:#x}")
        convention = row["calling_convention"]
        stack_bytes = registers_and_stack(uc, row["parameters"].split(), convention, case, argument_bits)
        self.reads.clear()
        self.writes.clear()
        try:
            uc.emu_start(entry, STOP, count=2400)
        except UcError as error:
            return str(error).split(" (")[0], None
        if self.clock_used or self.invalid_low or uc.reg_read(x.UC_X86_REG_EIP) != STOP:
            return "incomplete", None
        expect_sp = SP + 4 + (0 if convention == "__cdecl" else stack_bytes)
        if (uc.reg_read(x.UC_X86_REG_ESP) != expect_sp or bytes(uc.mem_read(0, 4)) != b"\xff" * 4
                or tuple(uc.reg_read(r) for r in REGS[3:]) != CALLEE_SAVED):
            return "ABI", None
        reads, writes = self.memory(self.reads, True), self.memory(self.writes, False)
        if reads is None or writes is None:
            return "unmapped state", None
        sw, tag = uc.reg_read(x.UC_X86_REG_FPSW), uc.reg_read(x.UC_X86_REG_FPTAG)
        ret = row["result_type"]
        eax = uc.reg_read(x.UC_X86_REG_EAX)
        if ret == "void":
            value = None
        elif ret in ("float", "double"):
            value = uc.reg_read(x.UC_X86_REG_FP0 + ((sw >> 11) & 7))
        elif ret == "wide":
            value = (eax, uc.reg_read(x.UC_X86_REG_EDX))
        elif ret == "ptr":
            translated = self.game_address(eax)
            value = eax if translated is None else translated
        else:
            value = eax & {"int": 0xffffffff, "bool": 255, "byte": 255, "word": 65535}[ret]
        live_fp = tuple((i, uc.reg_read(x.UC_X86_REG_FP0 + i)) for i in range(8) if tag >> (2 * i) & 3 != 3)
        env = (sw, tag, uc.reg_read(x.UC_X86_REG_FPCW), live_fp, uc.reg_read(x.UC_X86_REG_MXCSR),
               uc.reg_read(x.UC_X86_REG_EFLAGS) & 0x400)
        argument_area = bytes(uc.mem_read(SP + 4, STACK + 65536 - SP - 4))
        original_stack = hashlib.sha256(argument_area).hexdigest()
        # RET n can leave scratch bytes below the caller's restored ESP. Keep
        # them in the historical comparison and identify that case separately.
        self.caller_stack = (original_stack if expect_sp == SP + 4 else
                             hashlib.sha256(argument_area[expect_sp - SP - 4:]).hexdigest())
        return "returned", (value, env, reads, writes, original_stack)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ("original", "candidate", "map", "pairs", "out"):
        parser.add_argument("--" + flag, type=Path, required=True)
    parser.add_argument("--link-columns", choices=("candidate", "production"), default="candidate")
    parser.add_argument("--address", help="One hexadecimal original VA")
    parser.add_argument("--arg-bits", action="append", default=[], metavar="INDEX=HEX",
                        help="Override a zero-based parameter of --address with its exact unsigned bit pattern")
    parser.add_argument("--set", action="append", default=[], metavar="VA=HEX",
                        help="Initialize bytes at a logical game or fixture address before each call")
    args = parser.parse_args()
    try:
        edits = [(int(addr, 16), bytes.fromhex(raw)) for addr, raw in (s.split("=", 1) for s in args.set)]
    except ValueError:
        parser.error("--set expects a hexadecimal VA and hexadecimal bytes separated by '='")
    if any(not raw for _, raw in edits):
        parser.error("--set requires at least one byte")
    if args.arg_bits and not args.address:
        parser.error("--arg-bits requires --address")
    try:
        arguments = {int(index): int(bits, 16) for index, bits in (s.split("=", 1) for s in args.arg_bits)}
    except ValueError:
        parser.error("--arg-bits expects a decimal index and hexadecimal bit pattern separated by '='")
    rows = load_pairs(args)
    syms = {name: int(va, 16) for name, va in re.findall(
        r"^\s+\w+:\w+\s+(\S+)\s+([0-9a-f]{8})\s+", args.map.read_text(), re.M)}
    if args.address:
        rows = [r for r in rows if int(r["stock_va"], 16) == int(args.address, 16)]
        if not rows:
            parser.error("Address is absent from the supplied inventory")
        if any(index < 0 or index >= len(rows[0]["parameters"].split()) for index in arguments):
            parser.error("Argument index is outside the declared parameter list")
    for row in rows:
        validate_decorated_abi(row)
    fixtures = [fixture_arena(n) for n in range(10)]
    images = StateImage(args.original, {}, fixtures), StateImage(args.candidate, syms, fixtures)
    with args.out.open("w", encoding="utf-8") as file:
        for row in rows:
            report = dict(va=row["stock_va"], cases=[])
            if arguments:
                report["argument_bits"] = {index: hex(bits) for index, bits in arguments.items()}
            if not row["candidate_va"]:
                report["status"] = "no separate entry"
            elif row.get("entry_abi") == "parent exception frame":
                report["status"] = "requires parent exception frame"
            elif row.get("entry_abi") == "register span":
                report["status"] = "requires span drawing entry"
            else:
                for n in range(10):
                    obs = [im.replay(int(row[key], 16), row, n, edits, arguments) for im, key in
                           zip(images, ("stock_va", "candidate_va"))]
                    a, b = (v for _, v in obs)
                    case = {
                        "outcome": "incomplete" if a is None or b is None else "match" if a == b else "difference",
                        "status": [s for s, _ in obs],
                        "digest": [None if v is None else hashlib.sha256(repr(v).encode()).hexdigest() for _, v in obs],
                    }
                    if (case["outcome"] == "difference" and a[:-1] == b[:-1]
                            and images[0].caller_stack == images[1].caller_stack):
                        case["difference_scope"] = "discarded callee-pop arguments only"
                    report["cases"].append(case)
            file.write(json.dumps(report) + "\n")


if __name__ == "__main__":
    main()
