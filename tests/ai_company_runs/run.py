"""Measure rival-company growth in fresh, stationary single-player campaigns."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import random
import subprocess


def design(profile, recorded_frames, primary_only, hours):
    rng = random.Random(20261121)
    seeds = [rng.getrandbits(32) for _ in range(16)]
    if recorded_frames:
        return [
            dict(case=i, seed=seed, step=0.0, horizon=(hours or 24) * 3600)
            for i, seed in enumerate(seeds[:4], 1)
        ]
    count, interval = (12, 0.25) if profile == "ini" else (16, 1.0)
    horizon = (hours or 120) * 3600
    primary = [
        dict(case=i, seed=seed, step=interval, horizon=horizon)
        for i, seed in enumerate(seeds[:count], 1)
    ]
    if primary_only:
        return primary
    controls = [
        {**case, "case": count + case["case"], "step": 0.05 if profile == "ini" else 0.25}
        for case in primary[:4]
    ]
    # Start the longer, fine-step controls early. Keep their identities distinct.
    order = []
    stride = count // len(controls)
    for i, control in enumerate(controls):
        order.extend([control, *primary[i * stride : (i + 1) * stride]])
    return order


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host", type=Path)
    parser.add_argument("game_copy", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--profile", choices=["truck", "ini"], default="truck")
    parser.add_argument("--frame-file", type=Path)
    parser.add_argument("--primary-only", action="store_true")
    parser.add_argument("--hours", type=int)
    parser.add_argument("--twenty-hz", action="store_true",
                        help="run sixteen paired ini-profile starts at a mean of 20 AI updates per second")
    args = parser.parse_args()
    if args.workers < 1 or (args.hours is not None and args.hours < 1):
        parser.error("hours and workers must be positive")
    if args.frame_file and args.profile != "ini":
        parser.error("recorded timing requires the ini profile")

    host = args.host.resolve(strict=True)
    game = args.game_copy.resolve(strict=True)
    cases = design(args.profile, args.frame_file, args.primary_only, args.hours)
    if args.twenty_hz:
        if args.profile != "ini" or args.frame_file:
            parser.error("--twenty-hz uses the ini profile and a generated frame clock")
        cases = design("truck", False, True, args.hours)
        for case in cases:
            case["step"] = 0.05
    inputs = [host, host.with_name("aiqa81.dll"), game / "king.exe", game / "vehicle.tech",
              game / "ENV" / "GAME.CNF", game / "TRUCK.INI"]
    if args.frame_file:
        inputs.append(args.frame_file)
    hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    if hashes["king.exe"] != "31fbd1dbbe7706183f09a3688482791255db2bfb557c02675374b96796ce2ebd":
        parser.error("the worker requires Steam 8.1")
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=False)
    manifest = dict(profile=args.profile, cases=sorted(cases, key=lambda c: c["case"]), sha256=hashes)
    (root / "design.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")

    def run(case):
        log = root / f"run-{case['case']:02}.log"
        env = dict(os.environ, HT2_RUN_LOG=str(log), HT2_HORIZON=str(case["horizon"]),
                   HT2_MEAN_STEP=str(case["step"]), HT2_TIMING_SEED=str(case["seed"]),
                   HT2_START_FROM_INI=str(int(args.profile == "ini")))
        if args.frame_file:
            env["HT2_FRAME_FILE"] = str(args.frame_file.resolve())
        else:
            env.pop("HT2_FRAME_FILE", None)
        subprocess.run([host], cwd=game, env=env, check=True, creationflags=subprocess.CREATE_NO_WINDOW)
        if not log.read_text().endswith("complete\n"):
            raise RuntimeError(f"incomplete run {case['case']}")
        print(f"completed {case['case']}", flush=True)

    with ThreadPoolExecutor(args.workers) as pool:
        list(pool.map(run, cases))


if __name__ == "__main__":
    main()
