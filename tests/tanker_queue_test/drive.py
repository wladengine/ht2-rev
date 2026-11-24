"""Replay the measured approach in a disposable x86 game process on Windows."""

import bisect
import ctypes as c
from ctypes import wintypes as w
import math
import os
import struct
import subprocess
import time


def bind(library, name, result, *arguments):
    function = getattr(library, name)
    function.restype = result
    function.argtypes = arguments
    return function


class Game:
    def __init__(self, proc, bss):
        self.proc = proc
        self.bss = bss - 0x68A820
        kernel = c.WinDLL("kernel32", use_last_error=True)
        self.ui = c.WinDLL("user32", use_last_error=True)
        arguments = (w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.c_void_p)
        self.reader = bind(kernel, "ReadProcessMemory", w.BOOL, *arguments)
        self.writer = bind(kernel, "WriteProcessMemory", w.BOOL, *arguments)
        self.post = bind(
            self.ui, "PostMessageW", w.BOOL, w.HWND, w.UINT, w.WPARAM, w.LPARAM
        )

    def read(self, address, size):
        buffer = c.create_string_buffer(size)
        if not self.reader(self.proc._handle, address, buffer, size, None):
            raise OSError(c.get_last_error(), f"Cannot read {address:#x}")
        return buffer.raw

    def put(self, address, data):
        if not self.writer(self.proc._handle, address, data, len(data), None):
            raise OSError(c.get_last_error(), f"Cannot write {address:#x}")

    def u32(self, address):
        return int.from_bytes(self.read(address, 4), "little")

    def floats(self, address, count):
        return struct.unpack(f"<{count}f", self.read(address, count * 4))

    def vector(self, address, values):
        self.put(address, struct.pack("<3f", *values))

    def matrix(self, car):
        item = car + 16
        return self.floats(item + (1792 if self.u32(item + 1440) else 500), 12)

    def window(self):
        callback_type = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)

        @callback_type
        def visit(hwnd, _):
            pid, rect = w.DWORD(), w.RECT()
            self.ui.GetWindowThreadProcessId(hwnd, c.byref(pid))
            if pid.value == self.proc.pid and self.ui.IsWindowVisible(hwnd):
                self.ui.GetClientRect(hwnd, c.byref(rect))
                if rect.right >= 640 and rect.bottom >= 480:
                    self.hwnd = hwnd
            return True

        self.hwnd = None
        deadline = time.monotonic() + 30
        while self.proc.poll() is None and time.monotonic() < deadline:
            self.ui.EnumWindows(visit, 0)
            if self.hwnd:
                return
            time.sleep(0.1)
        raise RuntimeError("Game window did not open")

    def click(self, x, y):
        rect = w.RECT()
        self.ui.GetClientRect(self.hwnd, c.byref(rect))
        point = round(x * rect.right / 800) | round(y * rect.bottom / 600) << 16
        for message, buttons in ((512, 0), (513, 1), (514, 0)):
            self.post(self.hwnd, message, buttons, point)
            time.sleep(0.16)

    def key(self, key, pressed):
        scan = self.ui.MapVirtualKeyW(key, 0)
        bits = 1 | scan << 16 | int(key in (37, 38, 39, 40)) << 24
        self.post(
            self.hwnd,
            256 if pressed else 257,
            key,
            bits if pressed else bits | 0xC0000000,
        )

    def load(self):
        self.window()
        self.post(self.hwnd, 6, 1, 0)
        time.sleep(4)
        self.click(655, 141)
        time.sleep(1.3)
        self.click(385, 385)
        deadline = time.monotonic() + 48
        while time.monotonic() < deadline and self.proc.poll() is None:
            try:
                car = self.u32(self.u32(0x6D2078 + self.bss) + 616)
                if math.dist(self.matrix(car)[9:], (-1550, -2600, 52)) < 125:
                    break
            except OSError:
                pass
            time.sleep(0.008)
        else:
            raise RuntimeError("Approach save did not load")
        self.view = self.u32(self.u32(0x6CEC6C + self.bss) + 1356)
        self.put(self.view + 3032, struct.pack("<i", 7))
        initial = self.clock()[1]
        deadline = time.monotonic() + 8
        while self.clock()[1] - initial < 0.25:
            if time.monotonic() > deadline:
                raise RuntimeError("Game clock did not start")
            time.sleep(0.002)
        trailer = self.u32(car + 21880)
        head = self.u32(0x68B990 + self.bss)
        node = self.u32(head)
        visited = set()
        while node and node != head and node not in visited:
            if self.u32(node + 92) == 1:
                break
            visited.add(node)
            node = self.u32(node)
        else:
            raise RuntimeError("Player freight owner missing")
        hold = self.u32(node + 520)
        order = self.u32(hold + 28)
        quantity = struct.unpack("<d", self.read(order + 56, 8))[0]
        integrity = struct.unpack("<d", self.read(order + 96, 8))[0]
        if (
            not trailer
            or self.u32(hold + 32) != order + 144
            or self.u32(order) != 35
            or not math.isclose(quantity, 15.3)
            or not math.isclose(integrity, 0.3525689438559959)
        ):
            raise RuntimeError("Expected attached tanker and damaged 15.3 t fuel order")
        return car, trailer

    def clock(self):
        return struct.unpack("<2d", self.read(self.view + 2960, 16))

    def tasks(self, trailer, rdata, known):
        size, capacity, table, flags, _ = struct.unpack(
            "<5I", self.read(0x7200C0 + self.bss, 20)
        )
        if size != 24 or not 1 <= capacity <= 100_000:
            raise RuntimeError("Unsupported event pool layout")
        entries = self.read(table, capacity * size)
        allocated = self.read(flags, capacity)
        owners = {
            struct.unpack_from("<I", entries, i * size + 12)[0]
            for i, used in enumerate(allocated)
            if used
        }
        types = {}
        particles, phases, fire = 0, None, False
        for owner in owners - {0}:
            vt = known[owner] if owner in known else self.u32(owner)
            types[owner] = vt
            if vt == rdata + (0x64FEEC - 0x64B000):
                particles += 1
            if vt == rdata + (0x600AD8 - 0x5FE610) and self.u32(owner + 680) == trailer:
                phases = sum(
                    bool(x) for x in struct.unpack("<14i", self.read(owner + 32, 56))
                )
                fire = self.floats(owner + 660, 1)[0] == 1
        return types, capacity, sum(bool(x) for x in allocated), particles, phases, fire


def seed(game, rig, velocity, seeds, short):
    for role, obj, yaw in zip(("tractor", "trailer"), rig, (-0.37, -0.45)):
        body = game.u32(obj + 21600)
        v = list(velocity)
        if short:
            for offset, data in seeds[role].items():
                game.put(body + int(offset), bytes.fromhex(data))
            matrix = game.matrix(obj)
            inertia = game.floats(body + 8008, 3)
            spin = [yaw * matrix[j] for j in (2, 5, 8)]
            local = [a * b for a, b in zip(spin, inertia)]
            world = [
                sum(local[j] * matrix[3 * j + i] for j in range(3)) for i in range(3)
            ]
            for address in (body + 76, body + 9024, obj + 456, obj + 3340):
                game.vector(address, world)
            for address in (body + 9048, body + 88, obj + 444):
                game.vector(address, spin)
            game.vector(body + 9036, local)
            if role == "trailer":
                tractor = game.matrix(rig[0])
                v[0] -= yaw * (matrix[10] - tractor[10])
                v[1] += yaw * (matrix[9] - tractor[9])
        for address in (
            obj + 432,
            obj + 3316,
            obj + 20688,
            body + 64,
            body + 2568,
            body + 9012,
            body + 10832,
        ):
            game.vector(address, v)


class Approach:
    def __init__(self, samples):
        self.times, self.poses = [], []
        for sample in samples:
            matrix = sample["matrix"]
            yaw = math.atan2(matrix[4], matrix[3])
            fraction = min(1, max(0, (106.8 - math.degrees(yaw)) / 160.2))
            trailer_yaw = yaw + fraction * math.radians(34)
            x, y = matrix[9:11]
            self.times.append(sample["seconds"])
            self.poses.append(
                (
                    (x, y, yaw),
                    (
                        x - 2.1 * math.cos(yaw) - 5.25 * math.cos(trailer_yaw),
                        y - 2.1 * math.sin(yaw) - 5.25 * math.sin(trailer_yaw),
                        trailer_yaw,
                    ),
                )
            )

    def guide(self, game, rig, when):
        i = max(0, min(len(self.times) - 2, bisect.bisect_right(self.times, when) - 1))
        span = self.times[i + 1] - self.times[i]
        fraction = min(1, max(0, (when - self.times[i]) / span))
        for obj, a, b in zip(rig, self.poses[i], self.poses[i + 1]):
            matrix = game.matrix(obj)
            body = game.u32(obj + 21600)
            target = [x + (y - x) * fraction for x, y in zip(a, b)]
            v = [
                (b[j] - a[j]) / span + max(-4, min(4, 3 * (target[j] - matrix[9 + j])))
                for j in (0, 1)
            ]
            v.append(game.floats(body + 64, 3)[2])
            for offset in (64, 2568, 9012):
                game.vector(body + offset, v)
            yaw = math.atan2(matrix[4], matrix[3])
            rate = max(
                -2.2, min(2.2, (b[2] - a[2]) / span + 6 * math.sin(target[2] - yaw))
            )
            spin = [*game.floats(body + 9048, 2), rate]
            local = [a * b for a, b in zip(spin, game.floats(body + 8008, 3))]
            game.vector(
                body + 76,
                [sum(local[k] * matrix[3 * k + j] for k in range(3)) for j in range(3)],
            )


def drive(folder, match, data, bss, rdata, output):
    short = match == "short"
    reference = 368.52 if short else 364.035
    records = data["records"]
    samples = [
        r
        for r in records
        if r["kind"] == "player" and reference - 0.15 < r["seconds"] <= 369.16
    ]
    path = Approach(samples)
    initial = min(samples, key=lambda r: abs(r["seconds"] - reference))
    held = set(
        next(
            r["controls_on"]
            for r in reversed(records)
            if r["kind"] == "controls" and r["seconds"] <= reference
        )
    )
    inputs = iter(
        r
        for r in records
        if r["kind"] == "input"
        and reference < r["seconds"] < 369.17
        and r.get("message") in (256, 257)
        and r["key"] != 27
    )
    event = next(inputs, None)
    env = os.environ.copy()
    proc = subprocess.Popen([str(folder / "king.exe")], cwd=folder, env=env)
    game = Game(proc, bss)
    try:
        rig = game.load()
        seed(game, rig, initial["velocity"], data["wheel_state"], short)
        for key in held:
            game.key(key, True)
        start = game.clock()[1]
        previous = None
        known = {}
        deadline = time.monotonic() + 75
        last_update = time.monotonic()
        with (output / "frames.csv").open(
            "w", encoding="ascii", buffering=65536
        ) as trace:
            trace.write(
                "step_s,elapsed_s,wall_s,tractor_x,tractor_y,trailer_x,trailer_y,capacity,used,particle_tasks,blast_phases,fire\n"
            )
            wall_start = time.monotonic()
            while proc.poll() is None:
                try:
                    step, clock = game.clock()
                    t = clock - start
                    if clock == previous:
                        if time.monotonic() - last_update > 5:
                            raise TimeoutError("Game clock stopped")
                        time.sleep(0.0008)
                        continue
                    if time.monotonic() >= deadline:
                        raise TimeoutError("Run exceeded time limit")
                    last_update = time.monotonic()
                    previous = clock
                    while event and event["seconds"] <= reference + t:
                        key = event["key"]
                        pressed = event["message"] == 256
                        game.key(key, pressed)
                        if pressed:
                            held.add(key)
                        else:
                            held.discard(key)
                        event = next(inputs, None)
                    if not short and reference + t < 369.08:
                        path.guide(game, rig, reference + t)
                    if held and reference + t >= 369.7:
                        for key in held:
                            game.key(key, False)
                        held.clear()
                    known, capacity, used, particles, phases, fire = game.tasks(
                        rig[1], rdata, known
                    )
                    x, y = game.matrix(rig[0])[9:11]
                    tx, ty = game.matrix(rig[1])[9:11]
                    trace.write(
                        f"{step:.6f},{t:.6f},{last_update - wall_start:.6f},{x},{y},{tx},{ty},{capacity},{used},{particles},{phases if phases is not None else ''},{int(fire)}\n"
                    )
                    if t >= 35:
                        return "completed"
                except OSError as read_error:
                    # A process exiting through Sys_Fatal may disappear between two reads.
                    try:
                        proc.wait(0.3)
                    except subprocess.TimeoutExpired:
                        raise read_error
        return "exited"
    finally:
        if proc.poll() is None:
            if getattr(game, "hwnd", None):
                game.post(game.hwnd, 16, 0, 0)
            try:
                proc.wait(6)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
