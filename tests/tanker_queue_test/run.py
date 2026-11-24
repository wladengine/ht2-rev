"""Run the tanker stress case on a fresh game copy and classify the observed outcome."""

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import struct
import tempfile
import zipfile

from fixture import CONFIGS, FIXTURE, ROOT, verify

ROUTE = ROOT / ".local/fixtures/tanker/route.json"
ORIGINAL = "31fbd1dbbe7706183f09a3688482791255db2bfb557c02675374b96796ce2ebd"
BRIDGE = "83f835f4278ba98a3946c8a058aca28e1a6be69f2e7ba81ce453943eee5d5087"
FATAL = "Not enough blocks in user-defined heap for Task's activation"


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def symbols(exe, sha, map_path):
    if sha == ORIGINAL:
        return 0x68A820, 0x64B000
    if sha == BRIDGE:
        return 0x664020, 0x5FE610
    if not map_path:
        raise ValueError("A matching linker --map is required for a new x86 build")
    with exe.open("rb") as f:
        f.seek(0x3C)
        f.seek(struct.unpack("<I", f.read(4))[0])
        signature, machine, _, timestamp = struct.unpack("<4sHHI", f.read(12))
    mapping = map_path.read_text()
    if (
        signature != b"PE\0\0"
        or machine != 0x14C
        or f"Timestamp is {timestamp:08x}" not in mapping
    ):
        raise ValueError("PE/MAP mismatch or unsupported target")
    names = dict(
        re.findall(
            r"^\s+\w+:\w+\s+(_KingBss|_KingRdata)\s+([\da-fA-F]+)\s", mapping, re.M
        )
    )
    return int(names["_KingBss"], 16), int(names["_KingRdata"], 16)


def copy_game(source, destination):
    destination.mkdir()
    for name in ("ENV", "COMMON", "MENV", "MTR", "MTB", "MUSIC"):
        shutil.copytree(source / name, destination / name)
    # Only run resources, never sessions, saves, crash dumps or old warning logs.
    allowed = {
        ".sft",
        ".dll",
        ".ini",
        ".sch",
        ".fon",
        ".i",
        ".disabled",
        ".avi",
        ".ovl",
        ".res",
        ".scq",
        ".bne",
        ".tech",
    }
    for src in source.iterdir():
        if src.is_file() and (
            src.suffix.lower() in allowed
            or src.name.lower() in {"king.exe", "disk1", "mess.txt"}
        ):
            shutil.copyfile(src, destination / src.name)


def classify(frames, reason, warning, case, road):
    if not frames:
        return {"outcome": "inconclusive", "reason": "No frames"}
    times = [float(row["elapsed_s"]) for row in frames]
    if any(float(row["step_s"]) != 0.007 for row in frames) or any(
        a >= b for a, b in zip(times, times[1:])
    ):
        return {"outcome": "inconclusive", "reason": "Unexpected timing"}
    active = [i for i, r in enumerate(frames) if r["blast_phases"] != ""]
    phases = max((int(frames[i]["blast_phases"]) for i in active), default=0)
    fire = any(frames[i]["fire"] == "1" for i in active)
    removed = bool(active and active[-1] < len(frames) - 1)
    full_blast = phases == 14 and fire and removed
    impact_position = None
    if active:
        first = frames[active[0]]
        # Trailer centre in the playtest, close to the recorded surface contact.
        impact_position = math.hypot(
            float(first["trailer_x"]) + 1552.4424, float(first["trailer_y"]) + 2564.5581
        )
    ref = 364.035 if case == "long" else 368.52
    check_time = 4.8 if case == "long" else 0.0
    frame = min(frames, key=lambda r: abs(float(r["elapsed_s"]) - check_time))
    sample = min(
        (r for r in road["records"] if r["kind"] == "player"),
        key=lambda r: abs(r["seconds"] - ref - check_time),
    )
    route_error = math.hypot(
        float(frame["tractor_x"]) - sample["matrix"][9],
        float(frame["tractor_y"]) - sample["matrix"][10],
    )
    at_tree = impact_position is not None and impact_position < 4
    queue_fatal = FATAL in warning
    entries = []
    if queue_fatal:
        # The game's own contiguous roster precedes the blank warning before the fatal.
        prefix = warning[: warning.rfind(FATAL)].split("warning : \n")[-2:]
        if len(prefix) == 2:
            for line in reversed(prefix[0].splitlines()):
                match = re.fullmatch(r"warning : (?!Pos=)(.+?) -?\d+", line)
                if not match:
                    break
                entries.append(match[1])
    capacity = int(frames[-1]["capacity"])
    outcome = "inconclusive"
    if route_error < 4 and abs(float(frame["elapsed_s"]) - check_time) < 0.15:
        if (
            case == "long"
            and reason == "exited"
            and queue_fatal
            and len(entries) == capacity
            and entries.count("particlesystemtask") > capacity / 2
        ):
            outcome = "queue_exhausted"
        elif reason == "completed" and times[-1] >= 35 and not queue_fatal:
            if full_blast and at_tree:
                outcome = "blast_completed"
            elif case == "long" and not active:
                outcome = "survived_without_blast"
    return dict(
        outcome=outcome,
        elapsed_s=times[-1],
        peak_slots=max(int(r["used"]) for r in frames),
        peak_particles=max(int(r["particle_tasks"]) for r in frames),
        capacity=capacity,
        fatal_roster=len(entries),
        fatal_particles=entries.count("particlesystemtask"),
        route_error_xy=round(route_error, 3),
        blast_distance_xy=impact_position,
        phases=phases,
        fire=fire,
        removed=removed,
        blast_duration_s=(
            round(times[active[-1] + 1] - times[active[0]], 3) if removed else None
        ),
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--game-dir",
        type=Path,
        required=True,
        help="read-only source of king.exe and full game resources",
    )
    parser.add_argument("--route", type=Path, default=ROUTE)
    parser.add_argument("--fixture", type=Path, default=FIXTURE)
    parser.add_argument("--map", type=Path)
    parser.add_argument("--expect", choices=("baseline", "fixed"), default="baseline")
    parser.add_argument("--profile-name", required=True, help="Profile filename expected by your copied game configuration")
    args = parser.parse_args()
    if Path(args.profile_name).name != args.profile_name or Path(args.profile_name).suffix.lower() != ".pl1":
        parser.error("Supply a profile basename with extension .pl1")
    source = args.game_dir.resolve()
    sha = digest(source / "king.exe")
    with zipfile.ZipFile(args.fixture) as archive:
        verify(archive)
    bss, rdata = symbols(source / "king.exe", sha, args.map)
    runs = ROOT / ".local" / "regressions"
    runs.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="tanker-queue-", dir=runs))
    print(root, flush=True)
    from drive import drive

    with zipfile.ZipFile(args.fixture) as archive:
        road = json.loads(args.route.read_text("utf-8"))
        game = root / "game"
        copy_game(source, game)
        for config in CONFIGS:
            (game / config).write_bytes(archive.read(config))
        report = dict(
            exe_sha256=sha,
            fixture_sha256=digest(args.fixture),
            route_sha256=digest(args.route),
            expected=args.expect,
            map_sha256=digest(args.map) if args.map else None,
            configuration={c: digest(game / c) for c in CONFIGS},
            runtime={
                c: digest(game / c)
                for c in ("ddraw.dll", "ht2ach.dll", "steam_api.dll")
                if (game / c).is_file()
            },
            cases={},
        )
        for case, profile in (("long", "364.035"), ("short", "368.52")):
            out = root / case
            out.mkdir()
            (game / args.profile_name).write_bytes(
                archive.read("approach-" + profile + ".pl1")
            )
            try:
                reason = drive(game, case, road, bss, rdata, out)
            except (OSError, RuntimeError, ValueError) as error:
                reason = f"{type(error).__name__}: {error}"
            logfile = game / "warn.log"
            warning = ""
            if logfile.exists():
                logfile.rename(out / "warn.log")
                warning = (out / "warn.log").read_text("cp1251", errors="replace")
            frames = out / "frames.csv"
            rows = []
            if frames.exists():
                with frames.open() as stream:
                    rows = list(csv.DictReader(stream))
            result = classify(rows, reason, warning, case, road)
            target = (
                "queue_exhausted"
                if case == "long" and args.expect == "baseline"
                else "blast_completed"
            )
            result.update(stop=reason, passed=result["outcome"] == target)
            report["cases"][case] = result
            print(case, result, flush=True)
            (root / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    return int(not all(r["passed"] for r in report["cases"].values()))


if __name__ == "__main__":
    raise SystemExit(main())
