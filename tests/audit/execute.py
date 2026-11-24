"""Offline comparison on fixed x86 machine states; no Windows APIs are executed."""

import argparse
from pair_input import load_pairs
import hashlib
import json
import re
import struct
from pathlib import Path
import pefile
from unicorn import (
    Uc,
    UcError,
    UC_ARCH_X86,
    UC_MODE_32,
    UC_HOOK_CODE,
    UC_HOOK_MEM_WRITE,
    UC_HOOK_MEM_READ,
    UC_PROT_WRITE,
    UC_PROT_READ,
    UC_PROT_EXEC,
)
from unicorn import x86_const as x

BASE = 0x400000
ARENA = 0x1000000
STACK = 0x2000000
SP = STACK + 0x8000
STOP = 0x3100000
WORDS = struct.Struct("<I")
REGS = (
    x.UC_X86_REG_EAX,
    x.UC_X86_REG_ECX,
    x.UC_X86_REG_EDX,
    x.UC_X86_REG_EBX,
    x.UC_X86_REG_ESI,
    x.UC_X86_REG_EDI,
    x.UC_X86_REG_EBP,
)


class Session:
    def __init__(self, exe):
        pe = pefile.PE(str(exe))
        self.img = pe.get_memory_mapped_image()
        if pe.OPTIONAL_HEADER.ImageBase != BASE or pe.FILE_HEADER.Machine != 0x14C:
            raise ValueError("expected i386 game at preferred base")
        self.uc = uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.dirty = set()
        uc.mem_map(BASE, (len(self.img) + 4095) & ~4095, UC_PROT_READ | UC_PROT_WRITE)
        uc.mem_write(BASE, self.img)
        # Treat every IAT target as unavailable. Do not execute unresolved thunk data.
        for lib in pe.DIRECTORY_ENTRY_IMPORT:
            for imp in lib.imports:
                uc.mem_write(imp.address, struct.pack("<I", 0x3200000))
        self.image_initial = bytes(uc.mem_read(BASE, len(self.img)))
        for section in pe.sections:
            access = UC_PROT_READ
            if section.Characteristics & 0x20000000:
                access |= UC_PROT_EXEC
            elif section.Characteristics & 0x80000000:
                access |= UC_PROT_WRITE
            uc.mem_protect(
                BASE + section.VirtualAddress,
                (section.Misc_VirtualSize + 4095) & ~4095,
                access,
            )

        # An unseeded hardware clock makes a direct comparison meaningless.
        def clock_read(uc, address, size, _):
            self.clock_used = True
            uc.emu_stop()

        for match in re.finditer(b"\x0f\x31|\x0f\x01\xf9", self.img):
            address = BASE + match.start()
            uc.hook_add(UC_HOOK_CODE, clock_read, begin=address, end=address)
        uc.mem_map(ARENA, 0x10000, UC_PROT_READ | UC_PROT_WRITE)
        uc.mem_map(STACK, 0x10000, UC_PROT_READ | UC_PROT_WRITE)
        uc.mem_map(STOP, 4096, UC_PROT_READ | UC_PROT_EXEC)
        uc.reg_write(x.UC_X86_REG_FPCW, 0x027F)
        uc.reg_write(x.UC_X86_REG_FPTAG, 0xFFFF)
        uc.mem_map(0, 4096, UC_PROT_READ | UC_PROT_WRITE)
        self.invalid_low = False

        def check_seh(uc, access, addr, size, value, _):
            pc = uc.reg_read(x.UC_X86_REG_EIP)
            if (
                addr != 0
                or size != 4
                or not BASE <= pc < BASE + len(self.img)
                or self.img[pc - BASE] != 0x64
            ):
                self.invalid_low = True
                uc.emu_stop()

        uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, check_seh, begin=0, end=4095)
        self.context = uc.context_save()

        def written(uc, access, adr, size, value, _):
            if BASE <= adr < BASE + len(self.img):
                self.dirty.update(range(adr & ~4095, adr + size, 4096))

        uc.hook_add(UC_HOOK_MEM_WRITE, written)
        self.arenas = []
        for n in range(6):
            buf = bytearray(0x10000)
            for off in range(0, len(buf), 4):
                slot = off // 4
                val = (
                    0,
                    ((slot * 3 + 1) % 9),
                    (ARENA + 0x4000 if slot % 4 == 0 else (slot % 3)),
                    (slot % 2 and 0x3FC00000 or 0x3F000000),
                    ARENA + 0x4000 + (slot % 3) * 0x1000,
                    0,
                )[n]
                WORDS.pack_into(buf, off, val)
            self.arenas.append(bytes(buf))

    def run(self, a, n):
        uc = self.uc
        for page in sorted(self.dirty):
            start = page - BASE
            uc.mem_write(page, self.image_initial[start : start + 4096])
        self.dirty.clear()
        uc.context_restore(self.context)
        self.clock_used = False
        self.invalid_low = False
        uc.mem_write(0, b"\xff" * 4)
        uc.mem_write(ARENA, self.arenas[n])
        uc.mem_write(STACK, b"\0" * 0x10000)
        args = (
            [0] * 12
            if n == 0
            else [ARENA + 0x6000, ARENA + 0x8000, 0, 1] * 3
            if n in (1, 3)
            else [1, 2, 0, 1] * 3
        )
        if n == 4:
            args = [ARENA + 0x2000 + (i % 8) * 0x1000 for i in range(12)]
        elif n == 5:
            args = [ARENA + 0x6000, 1, 0, 0] * 3
        uc.mem_write(SP, struct.pack("<13I", STOP, *args))
        for reg, value in zip(
            REGS,
            (
                0x13579B,
                ARENA + 0x1000,
                ARENA + 0x3000,
                0x11550000,
                0x11550001,
                0x11550002,
                0x11550003,
            ),
        ):
            uc.reg_write(reg, value)
        uc.reg_write(x.UC_X86_REG_ESP, SP)
        try:
            uc.emu_start(a, STOP, count=1500)
        except UcError as e:
            return str(e).split(" (")[0], None
        if self.clock_used:
            return "hardware clock", None
        if self.invalid_low:
            return "missing object", None
        if uc.reg_read(x.UC_X86_REG_EIP) != STOP:
            return "instruction budget", None
        esp = uc.reg_read(x.UC_X86_REG_ESP)
        if not SP + 4 <= esp <= SP + 260:
            return "nonstandard return", None
        g = {}
        for page in sorted(self.dirty):
            i = page - BASE
            before = self.image_initial[i : i + 4096]
            after = uc.mem_read(page, len(before))
            g.update({page + j: v for j, v in enumerate(after) if v != before[j]})
        tag = uc.reg_read(x.UC_X86_REG_FPTAG)
        fp = tuple(
            (i, uc.reg_read(x.UC_X86_REG_FP0 + i))
            for i in range(8)
            if (tag >> (i * 2)) & 3 != 3
        )
        arena = bytes(uc.mem_read(ARENA, 65536))
        obs = (
            tuple(uc.reg_read(reg) for reg in (REGS[0], *REGS[2:])),
            esp - SP,
            fp,
            uc.reg_read(x.UC_X86_REG_FPSW),
            uc.reg_read(x.UC_X86_REG_FPCW),
            tag,
            bytes(uc.mem_read(0, 4)).hex(),
            bytes(uc.mem_read(SP + 4, 48)).hex(),
            hashlib.sha256(arena).hexdigest(),
            g,
        )
        return "returned", obs


def main():
    ap = argparse.ArgumentParser()
    for n in ("original", "candidate", "map", "pairs", "out"):
        ap.add_argument("--" + n, type=Path, required=True)
    args = ap.parse_args()
    rows = load_pairs(args)
    left, right = Session(args.original), Session(args.candidate)
    with args.out.open("w") as f:
        for r in rows:
            a = int(r["stock_va"], 16)
            b = int(r["candidate_va"], 16) if r["candidate_va"] else None
            results = []
            summaries = []
            for n in range(6):
                if r.get("entry_abi") == "parent exception frame":
                    results.append(["requires parent exception frame", "requires parent exception frame", "incomplete"])
                    continue
                stock_status, stock = left.run(int(a), n)
                candidate_status, rebuilt = (
                    right.run(b, n) if b else ("no candidate", None)
                )
                if stock is not None and rebuilt is not None:
                    # Raw global addresses are version-specific. Require a targeted case for any global write.
                    if stock[-1] or rebuilt[-1]:
                        outcome = "global writes need interpretation"
                    else:
                        outcome = (
                            "equal" if stock == rebuilt else "observable difference"
                        )
                    summaries.append(
                        [
                            hashlib.sha256(repr(x).encode()).hexdigest()
                            for x in (stock, rebuilt)
                        ]
                    )
                else:
                    outcome = "incomplete"
                results.append([stock_status, candidate_status, outcome])
            f.write(
                json.dumps(dict(va=r["stock_va"], results=results, digests=summaries))
                + "\n"
            )


if __name__ == "__main__":
    main()
