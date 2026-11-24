"""Check a paired Bukhta reverse experiment from its diagnostic trace and v4 contacts."""
import argparse
import hashlib
import json
import sys
from pathlib import Path




def closest(rows, t):
    return min(rows, key=lambda row: abs(row[0] - t))


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def trial(path):
    trace = path / "safety.tsv"
    capture = path / "session.bin"
    decoded = path / "decoded"
    summary = json.loads((decoded / "summary.json").read_text("utf-8"))
    if not summary["complete"] or int((path / "exit-code.txt").read_text(), 0):
        raise ValueError(f"Incomplete trial {path}")
    front, follower, probes, boxes = [], [], [], []
    ids = {}
    start = end = None
    with trace.open() as f:
        for line in f:
            kind, clock, *data = line.split()
            t = float(clock)
            if kind == "hold":
                start = t
            elif kind == "release":
                end = t
            elif kind == "back":
                car, role, *values = data
                if ids.setdefault(role, car) != car:
                    raise ValueError("Vehicle identity changed")
                (front if role == "1" else follower).append((t, *map(float, values)))
            elif kind == "rearprobe":
                probes.append((t, *map(float, data[1:])))
            elif kind == "rearbox":
                boxes.append((t, data[1], *map(float, data[2:])))
    if start is None or end is None or end-start < 25 or set(ids) != {"0", "1"}:
        raise ValueError("Incomplete Avensis pair or observation interval")
    # back: clock, previous state, final state, x, y, speed, gas, brake,
    #       timer, stuck latch, path flags, forward limit, gear request.
    starts = [r for r in front if r[1] != 2 and r[2] == 2]
    exits = [r for r in front if r[1] == 2 and r[2] != 2]
    until = exits[0][0] if exits else end
    pair = [b for b in boxes if b[1] == ids["0"]]
    begin = starts[0][0] if starts else start
    interval = [r for r in probes if r[1] == 2 and begin-.002 <= r[0] <= until+.002]
    seen = next((r for r in interval if int(r[10]) & 4), None)
    danger = next((r for r in interval if int(r[10]) & 8), None)
    brakes = [r for r in front if r[1] == 2 and r[8] == -1 and r[6] == 0 and r[7] == 1]
    def probe_event(r):
        if r is None:
            return None
        return dict(seconds=round(r[0]-start, 4), speed_mps=r[2], reach_m=r[3],
                    accepted_ray_distance_m=r[8] if r[6] else None, ray_flags=int(r[7]), flags=int(r[10]))

    meta = summary["metadata"]
    hits = []
    with (decoded / "impacts.jsonl").open() as f:
        for line in f:
            event = json.loads(line)
            t = event["qpc"] / meta["qpc_frequency"]
            actors = [event["actor_a"], event["actor_b"]]
            if start <= t < end and {a.get("car_pointer") for a in actors} == {int(i, 16) for i in ids.values()}:
                hits.append(dict(seconds=round(t-start, 4), response=event["response"],
                                 models=[a["model"] for a in actors],
                                 point=[round(v, 3) for v in event["world_point"]]))
    # Both cars are nearly parallel to Y in this section. This is travel from the
    # stopped forward extreme, independent of the game's unsigned speed display.
    ymax = max(r[4] for r in front)
    peak = next(i for i, r in enumerate(front) if r[4] == ymax)
    first_entry = None
    if starts:
        r = starts[0]
        other = closest(follower, r[0])
        if abs(other[0]-r[0]) > .1:
            raise ValueError("No synchronized follower sample")
        first_entry = dict(seconds=round(r[0]-start, 4), y=r[4], follower_y=other[4],
                           follower_speed_mps=other[5], first_exit_seconds=round(until-start, 4))
        if exits:
            first_entry.update(travel_to_exit_m=round(r[4]-exits[0][4], 3),
                               exit_speed_mps=exits[0][5])
        before = [q for q in probes if q[1] != 2 and abs(q[0]-r[0]) < .05]
        if before:
            first_entry["entry_probe"] = probe_event(closest(before, r[0]))
        admission = [b for b in pair if b[2] == 1 and abs(b[0]-r[0]) < .05]
        if admission:
            first_entry["entry_box_gap_m"] = closest(admission, r[0])[3]
    first_brake = None
    if brakes:
        r = brakes[0]
        first_brake = dict(seconds=round(r[0]-start, 4), speed_mps=r[5], y=r[4],
                           follower_y=closest(follower, r[0])[4])
        cause = [b for b in pair if b[2] == 0 and b[5] and abs(b[0]-r[0]) < .05]
        if cause:
            first_brake.update(zip(("gap_m", "required_m", "closing_mps"), closest(cause, r[0])[3:6]))
        stop = next((exit_row for exit_row in exits if exit_row[0] >= r[0]), None)
        if stop:
            commands = [row for row in front if r[0] <= row[0] <= stop[0]]
            first_brake.update(seconds_to_exit=round(stop[0]-r[0], 4), exit_speed_mps=stop[5],
                               continuous_brake=all(row[6] == 0 and row[7] == 1 for row in commands))
    denied = [b[3] for b in pair if b[2] == 1 and b[5]]
    return dict(trial=path.name, trace_sha256=digest(trace), capture_sha256=digest(capture), ids=ids,
                duration_s=round(end-start, 4), n_reverse_entries=len(starts), first_entry=first_entry,
                first_manoeuvre_rear_queries=len(interval), first_ray_in_reach=probe_event(seen),
                first_native_close=probe_event(danger), first_emergency_brake=first_brake,
                blocked_entry_gap_range_m=[min(denied), max(denied)] if denied else None,
                max_backward_travel_m=round(ymax-min(r[4] for r in front[peak:]), 3),
                first_pair_contact=hits[0] if hits else None, pair_responses=len(hits))


def suite(directory):
    rows = {f"{case}-{mode}": trial(directory / f"{case}-{mode}-verified")
            for case in ("queued", "arrival", "clear") for mode in ("control", "fix")}
    queued = rows["queued-control"]
    fixed = rows["queued-fix"]
    arrival = rows["arrival-fix"]
    brake = arrival["first_emergency_brake"]
    clear = rows["clear-fix"]["first_entry"]
    baseline = rows["clear-control"]["first_entry"]
    checks = {
        "baseline_pair_hit_after_native_close": bool(queued["first_pair_contact"] and queued["first_native_close"]
            and queued["first_native_close"]["seconds"] < queued["first_pair_contact"]["seconds"]),
        "occupied_rear_prevents_reverse": fixed["n_reverse_entries"] == 0 and not fixed["pair_responses"]
            and fixed["blocked_entry_gap_range_m"] is not None and fixed["max_backward_travel_m"] < .2,
        "moving_follower_brakes_active_reverse": bool(arrival["first_entry"] and brake
            and brake["seconds"] > arrival["first_entry"]["seconds"] and brake.get("closing_mps", 0) > 1
            and brake["follower_y"] > arrival["first_entry"]["follower_y"] + 1 and brake["continuous_brake"]
            and brake["exit_speed_mps"] < .25 and not arrival["pair_responses"]),
        "free_reverse_preserved": bool(clear and baseline and clear["travel_to_exit_m"] > 5
            and abs(clear["travel_to_exit_m"] - baseline["travel_to_exit_m"]) < 1
            and not rows["clear-fix"]["pair_responses"]),
    }
    if not all(checks.values()):
        raise ValueError(f"Reverse regression failed: {checks}")
    return dict(checks=checks, runs=rows)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--suite", action="store_true", help="verify all six runs beneath the directory")
    args = parser.parse_args()
    print(json.dumps((suite if args.suite else trial)(args.directory), indent=2))
