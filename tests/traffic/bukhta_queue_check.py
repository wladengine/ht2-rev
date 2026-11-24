"""Summarize one controlled Bukhta run with a stationary exit or facing queue."""
import argparse
import collections
import hashlib
import json
import math
import sys
from pathlib import Path




def digest(path):
    with path.open("rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


def summarize(folder, facing):
    capture = folder / "session.bin"
    trace = folder / "safety.tsv"
    decoded = folder / "decoded"
    summary = json.loads((decoded / "summary.json").read_text("utf-8"))
    if not summary["complete"]:
        raise ValueError(summary["issues"])
    meta = summary["metadata"]
    origin = meta["qpc_start"] / meta["qpc_frequency"]
    anchors, releases = {}, {}
    poses = collections.defaultdict(list)
    traffic = collections.defaultdict(list)
    forced = set()
    with trace.open() as f:
        for line in f:
            kind, clock, car, *values = line.split()
            t = float(clock) - origin
            if kind == "pinned":
                anchors[car] = (t, *map(float, values))
            elif kind == "release":
                releases[car] = t
            elif kind == "fixture":
                poses[car].append((t, *map(float, values)))
            elif kind == "force":
                forced.add(car)
            elif kind == "state":
                model, x, y, dx, dy, speed, target = map(float, values[:7])
                traffic[car].append((t, int(model), x, y, dx, dy, speed, target))
    if len(anchors) != (2 if facing else 1) or anchors.keys() != releases.keys():
        raise ValueError("Incomplete obstacle setup or release")
    # The first tick of placement may still publish the prior AI pose.
    begin = max(anchor[0] for anchor in anchors.values()) + 0.35
    end = min(releases.values())
    if end - begin < 10:
        raise ValueError("The complete queue existed for less than ten seconds")

    fixture = {}
    for car, (_, x, y, dx, dy) in anchors.items():
        phase = [p for p in poses[car] if begin <= p[0] < end]
        if len(phase) < 2:
            raise ValueError(f"Missing body and AI pose samples for {car}")
        sign = -1 if facing else 1
        # Check the collision body's world pose AND the pose used for AI selection.
        max_drift = max(math.hypot(p[i]-x, p[i+1]-y) for p in phase for i in (1, 5))
        min_forward = min(sign*p[i] for p in phase for i in (4, 8))
        sample_times = [begin, *(p[0] for p in phase), end]
        max_interval = max(b-a for a, b in zip(sample_times, sample_times[1:]))
        if max_drift > 0.50 or min_forward < 0.98 or max_interval > 1:
            raise ValueError(f"Obstacle {car} left the specified pose or observation window")
        next_five = next((v[0]-end for v in traffic[car]
                          if v[0] >= end and math.hypot(v[2]-x, v[3]-y) >= 5), None)
        fixture[car] = dict(x=x, y=y, heading_y=dy, samples=len(phase),
                            max_drift_m=round(max_drift, 3), min_signed_heading_y=min_forward,
                            max_sample_interval_s=round(max_interval, 3),
                            seconds_to_move_5m_after_release=round(next_five, 3) if next_five is not None else None)

    front = min(anchors, key=lambda car: anchors[car][2])
    _, x, y, _, _ = anchors[front]
    gate = max(a[2] for a in anchors.values()) + 5
    approaches = {}
    for car, states in traffic.items():
        if car in fixture:
            continue
        # A northbound arrival must first be observed upstream on this approach.
        upstream = next((v for v in states if begin <= v[0] < end and x-10 < v[2] < x+12
                         and y-35 < v[3] < y-2 and v[5] > .65), None)
        if upstream is None:
            continue
        rows = [v for v in states if upstream[0] <= v[0] < end and x-10 < v[2] < x+12
                and y-35 < v[3] < gate+15 and v[5] > .65]
        # Require a measured crossing, so a respawn at a downstream point cannot count.
        crossings = [b[0] for a, b in zip(states, states[1:])
                     if a[0] >= upstream[0] and a[3] <= gate < b[3] and b[0]-a[0] < .5
                     and b[3]-a[3] < 4 and x-10 < b[2] < x+12 and b[5] > .65]
        post = next((round(t-end, 3) for t in crossings if t >= end), None)
        entry_y = y - 20
        entered = any(a[3] <= entry_y < b[3] and b[0]-a[0] < .5 and b[3]-a[3] < 4
                      for a, b in zip(rows, rows[1:]))
        approaches[car] = dict(model=upstream[1], min_origin_distance_m=round(
            min(math.hypot(v[2]-x, v[3]-y) for v in rows), 3),
            minimum_speed_mps=min(v[6] for v in rows), zero_target_samples=sum(v[7] == 0 for v in rows),
            entered_last_20m=entered, crossed_downstream_gate=any(t < end for t in crossings),
            passed_gate_seconds_after_release=post)

    responses, pairs, nearby = collections.Counter(), collections.defaultdict(list), {}
    with (decoded / "impacts.jsonl").open() as f:
        for line in f:
            hit = json.loads(line)
            if not begin <= hit["seconds"] < end:
                continue
            actors = [hit["actor_a"], hit["actor_b"]]
            if not all(a["kind"] == "vehicle" for a in actors):
                continue
            ids = tuple(sorted(f'{a["car_pointer"]:08X}' for a in actors))
            responses[hit["response"]] += 1
            px, py, _ = hit["world_point"]
            if x-10 < px < x+12 and y-35 < py < gate+15:
                nearby.setdefault(ids, dict(models=[a["model"] for a in actors],
                                            point=[round(v, 3) for v in hit["world_point"]],
                                            seconds=round(hit["seconds"]-begin, 3)))
            if any(i in fixture for i in ids):
                pairs[ids].append((hit["response"], round(hit["seconds"]-begin, 3),
                                   [round(v, 3) for v in hit["world_point"]]))
    return dict(trial=folder.name, profile="facing" if facing else "fixed_exit",
                capture_sha256=digest(capture), trace_sha256=digest(trace),
                seconds_from_capture=[round(begin, 3), round(end, 3)], fixtures=fixture,
                forced_throttle_vehicles=sorted(forced),
                downstream_gate_y=round(gate, 3), approaches=approaches,
                all_vehicle_responses_in_window=dict(responses),
                nearby_contact_pairs=[dict(pair=ids, **hit) for ids, hit in nearby.items()],
                fixture_contacts=[dict(pair=ids, first=hits[0], count=len(hits)) for ids, hits in pairs.items()])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    parser.add_argument("--facing", action="store_true")
    args = parser.parse_args()
    print(json.dumps(summarize(args.folder, args.facing), indent=2))
