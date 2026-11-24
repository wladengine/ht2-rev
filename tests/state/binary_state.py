"""Observe a region of emulated memory and the x86 floating-point environment.

Take a snapshot after arranging the inputs and compare it immediately after
execution. Offsets in the result are relative to the region's own base.
"""

from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MEM_READ, x86_const as x86


class MemoryObservation:
    def __init__(self, emulator, start, size, *, track_reads=False, track_store_order=False):
        if size <= 0:
            raise ValueError("Memory observation needs a non-empty mapped range")
        self.emulator = emulator
        self.start = start
        self.size = size
        self.before = bytes(emulator.mem_read(start, size))
        self.stored = set()
        self.read = set()
        self.store_order = [] if track_store_order else None
        events = UC_HOOK_MEM_WRITE | (UC_HOOK_MEM_READ if track_reads else 0)
        self.hook = emulator.hook_add(events, self._access)

    def _access(self, emulator, access, address, size, value, user_data):
        if address >= self.start + self.size or address + size <= self.start:
            return
        first = max(0, address - self.start)
        end = min(self.size, address - self.start + size)
        offsets = self.read if access == UC_MEM_READ else self.stored
        offsets.update(range(first, end))
        if access != UC_MEM_READ and self.store_order is not None:
            if size > 8:
                raise ValueError("Ordered tracing requires guest stores of at most eight bytes")
            shift = 8 * (self.start + first - address)
            width = end - first
            written = (value >> shift) & ((1 << (8 * width)) - 1)
            self.store_order.append((first, width, written))

    def close(self):
        if self.hook is not None:
            self.emulator.hook_del(self.hook)
            self.hook = None

    def finish(self):
        """Return guest store offsets and persistent byte changes (including host writes)."""
        if self.hook is None:
            raise RuntimeError("Observation has already ended")
        self.close()
        after = bytes(self.emulator.mem_read(self.start, self.size))
        changes = {}
        if self.before != after:
            for start in range(0, self.size, 4096):
                old = self.before[start:start + 4096]
                new = after[start:start + 4096]
                if old != new:
                    for i, (a, b) in enumerate(zip(old, new)):
                        if a != b:
                            changes[start + i] = (a, b)
        return self.stored, changes


def x86_environment(emulator):
    status = emulator.reg_read(x86.UC_X86_REG_FPSW)
    flags = emulator.reg_read(x86.UC_X86_REG_EFLAGS)
    return {
        "x87_control": emulator.reg_read(x86.UC_X86_REG_FPCW),
        "x87_exceptions": status & 0x3f,
        "x87_top": (status >> 11) & 7,
        "x87_tags": emulator.reg_read(x86.UC_X86_REG_FPTAG),
        "direction_flag": (flags >> 10) & 1,
        "mxcsr": emulator.reg_read(x86.UC_X86_REG_MXCSR),
    }
