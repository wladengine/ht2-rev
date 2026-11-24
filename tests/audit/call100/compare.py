"""Compare the selected x86 entry points. PE images and executed bytes stay local."""

import argparse
import csv
import hashlib
import json
import re
import struct
from pathlib import Path

import pefile
import unicorn
from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UcError
from unicorn import x86_const as x

from cases import configure, A

BASE = 0x400000
STACK = 0x2100000
STOP = 0x3100000
IMPORTS = STOP + 0x1000
REGS = (x.UC_X86_REG_EBX, x.UC_X86_REG_ESI, x.UC_X86_REG_EDI, x.UC_X86_REG_EBP)


def u32(v):
    return struct.pack("<I", v & 0xFFFFFFFF)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Image:
    def __init__(self, path, symbols=None):
        pe = pefile.PE(str(path))
        assert pe.OPTIONAL_HEADER.ImageBase == BASE and pe.FILE_HEADER.Machine == 0x14C
        self.blob = pe.get_memory_mapped_image()
        self.size = pe.OPTIONAL_HEADER.SizeOfImage
        self.symbols = symbols
        self.imports = {
            i.address: i.name.decode()
            for lib in pe.DIRECTORY_ENTRY_IMPORT
            for i in lib.imports
            if i.name
        }

    def data(self, va):
        if self.symbols is None:
            return va
        for start, name in [
            (0x68A820, "_KingBss"),
            (0x66C000, "_KingData"),
            (0x64B000, "_KingRdata"),
        ]:
            if va >= start:
                return self.symbols[name] + va - start
        raise ValueError(hex(va))


class Probe:
    def __init__(self, img, symbol):
        self.image = img
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.uc.mem_map(BASE, img.size)
        self.uc.mem_write(BASE, img.blob)
        self.uc.mem_map(A, 0x20000)
        self.uc.mem_map(STACK - 0x20000, 0x24000)
        self.uc.mem_map(STOP, 0x10000)
        self.uc.mem_map(0, 0x1000)  # fs:[0] exception-chain head in old MSVC prologues
        self.w(0, -1)
        self.instruction_budget = 50000
        self.args = []
        self.ecx = A
        self.result = "eax"
        self.events = []
        self.observed = {}
        self.fixups = {}
        self.callbacks = {}
        self.ports = {}
        for i, (iat, name) in enumerate(img.imports.items()):
            addr = IMPORTS + 4 * i
            self.w(iat, addr)
            self.ports[name] = addr
        self.symbol = symbol
        self.tsc = None

    def put(self, a, data):
        self.uc.mem_write(a, bytes(data))

    def w(self, a, *words):
        self.put(a, b"".join(u32(v) for v in words))

    def read(self, a):
        return struct.unpack("<I", self.uc.mem_read(a, 4))[0]

    def g(self, va, *words):
        addr = self.image.data(va)
        self.w(addr, *words)
        self.observed[va] = len(words) * 4

    def hook(self, va, symbol, callback, pop=0):
        address = va if self.image.symbols is None else self.image.symbols[symbol]
        self.callbacks[address] = callback, pop

    def api(self, name, callback, pop):
        if name in self.ports:
            self.callbacks[self.ports[name]] = callback, pop

    def run(self, entry):
        u = self.uc
        self.w(STACK, STOP, *self.args)
        u.reg_write(x.UC_X86_REG_ESP, STACK)
        u.reg_write(x.UC_X86_REG_ECX, self.ecx)
        for i, reg in enumerate(REGS):
            u.reg_write(reg, 0x11550000 + i)
        u.reg_write(x.UC_X86_REG_FPCW, 0x027F)
        u.reg_write(x.UC_X86_REG_FPTAG, 0xFFFF)

        def dispatch(uc, address, size, _):
            sp = uc.reg_read(x.UC_X86_REG_ESP)
            callback, pop = self.callbacks[address]
            result = callback([self.read(sp + 4 + i * 4) for i in range(12)])
            if result is not None:
                uc.reg_write(x.UC_X86_REG_EAX, result & 0xFFFFFFFF)
            uc.reg_write(x.UC_X86_REG_ESP, sp + 4 + pop)
            uc.reg_write(x.UC_X86_REG_EIP, self.read(sp))

        for address in self.callbacks:
            u.hook_add(UC_HOOK_CODE, dispatch, begin=address, end=address)

        def guard_low_page(uc, access, address, size, value, data):
            raise ValueError(f"null-relative memory access at {address:#x}")

        u.hook_add(
            UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, guard_low_page, begin=4, end=0xFFF
        )
        if self.tsc is not None:

            def rdtsc(uc, address, size, _):
                if size == 2 and uc.mem_read(address, 2) == b"\x0f\x31":
                    self.events.append(["rdtsc", self.tsc])
                    uc.reg_write(x.UC_X86_REG_EAX, self.tsc & 0xFFFFFFFF)
                    uc.reg_write(x.UC_X86_REG_EDX, self.tsc >> 32)
                    uc.reg_write(x.UC_X86_REG_EIP, address + 2)

            u.hook_add(UC_HOOK_CODE, rdtsc)
        try:
            u.emu_start(entry, STOP, count=self.instruction_budget)
        except UcError as e:
            raise ValueError(f"{e}: EIP={u.reg_read(x.UC_X86_REG_EIP):x}") from e
        if self.tsc is not None:
            assert self.events == [["rdtsc", self.tsc]]
        if u.reg_read(x.UC_X86_REG_EIP) != STOP:
            raise ValueError(f"no return at {u.reg_read(x.UC_X86_REG_EIP):x}")
        for i, reg in enumerate(REGS):
            assert u.reg_read(reg) == 0x11550000 + i
        if self.result == "fp":
            top = (u.reg_read(x.UC_X86_REG_FPSW) >> 11) & 7
            result = u.reg_read(x.UC_X86_REG_FP0 + top)
        elif self.result == "void":
            result = None
        else:
            result = u.reg_read(x.UC_X86_REG_EAX) & (
                255 if self.result == "al" else 0xFFFFFFFF
            )
        for addr, stock in self.fixups.items():
            assert self.read(addr) == self.image.data(stock), (
                hex(addr),
                hex(self.read(addr)),
            )
            self.w(addr, stock)
        return dict(
            result=result,
            stack=u.reg_read(x.UC_X86_REG_ESP) - STACK,
            arena=hashlib.sha256(u.mem_read(A, 0x20000)).hexdigest(),
            calls=self.events,
            globals={
                hex(k): bytes(u.mem_read(self.image.data(k), n)).hex()
                for k, n in self.observed.items()
            },
        )


def main():
    ap = argparse.ArgumentParser()
    for k in ("original", "candidate", "map", "out"):
        ap.add_argument("--" + k, required=True, type=Path)
    ap.add_argument("--only", nargs="*")
    ap.add_argument(
        "--negative-control",
        action="store_true",
        help="Compare vector subtraction with multiplication of the same inputs",
    )
    args = ap.parse_args()
    assert (
        sha(args.original)
        == "31fbd1dbbe7706183f09a3688482791255db2bfb557c02675374b96796ce2ebd"
    )
    mapping = args.map.read_text()
    pe = pefile.PE(str(args.candidate), fast_load=True)
    assert f"Timestamp is {pe.FILE_HEADER.TimeDateStamp:08x}" in mapping
    syms = {
        n: int(va, 16)
        for n, va in re.findall(r"^\s+\w+:\w+\s+(\S+)\s+([\da-f]{8})\s+", mapping, re.M)
    }
    images = [Image(args.original), Image(args.candidate, syms)]
    results = []
    targets = Path(__file__).with_name("targets.tsv")
    for r in csv.DictReader(targets.open(), delimiter="\t"):
        va = int(r["stock_va"], 16)
        if args.negative_control and va != 0x4BE230:
            continue
        if args.only and r["stock_va"] not in args.only:
            continue
        row = dict(stock_va=r["stock_va"], symbol=r["symbol"], cases=[])
        for variant in range(
            {0x409F10: 7, 0x471560: 5, 0x473670: 4, 0x4C5770: 3, 0x4C5950: 3}.get(va, 2)
        ):
            obs = []
            for image in images:
                p = Probe(image, r["symbol"])
                try:
                    configure(va, p, variant)
                    entry = (
                        va
                        if image.symbols is None
                        else syms[
                            "_Vec3_MulComponents" if args.negative_control else p.symbol
                        ]
                    )
                    obs.append(p.run(entry))
                except Exception as exc:
                    obs.append({"error": repr(exc)})
            ok = not any("error" in o for o in obs) and obs[0] == obs[1]
            row["cases"].append(dict(variant=variant, match=ok, observations=obs))
        results.append(row)
        print(
            r["stock_va"],
            r["symbol"],
            " ".join("match" if c["match"] else "DIFF" for c in row["cases"]),
        )
    args.out.write_text(
        json.dumps(
            dict(
                original=sha(args.original),
                candidate=sha(args.candidate),
                map=sha(args.map),
                unicorn=unicorn.__version__,
                fixtures={
                    f: sha(targets.with_name(f))
                    for f in ("targets.tsv", "compare.py", "cases.py")
                },
                negative_control=args.negative_control,
                results=results,
            ),
            indent=2,
        )
        + "\n"
    )
    if not results:
        raise ValueError("no targets selected")
    if args.negative_control:
        return int(
            not all(
                not c["match"]
                and all("arena" in o for o in c["observations"])
                and c["observations"][0]["arena"] != c["observations"][1]["arena"]
                for c in results[0]["cases"]
            )
        )
    return int(any(not c["match"] for row in results for c in row["cases"]))


if __name__ == "__main__":
    raise SystemExit(main())
