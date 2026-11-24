"""Verify completed campaign logs and export the results for the no-hire experiment."""

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
from statistics import median


@dataclass(frozen=True)
class Sample:
    elapsed: float
    peak: float
    companies: int
    staff: int
    deliveries: int
    first_hire: float
    victory: float
    winner_staff: int

    @classmethod
    def parse(cls, line):
        _, elapsed, peak, companies, staff, deliveries, hire, victory, winner = line.split()
        return cls(float(elapsed), float(peak), int(companies), int(staff), int(deliveries),
                   float(hire), float(victory), int(winner))

    def cumulative(self):
        return (self.peak, self.companies, self.staff, self.deliveries)


def read_run(root, case):
    path = root / f"run-{case['case']:02}.log"
    raw = path.read_bytes()
    lines = raw.decode("ascii").splitlines()
    if not lines or lines[-1] != "complete" or any(s.startswith("ERROR") for s in lines):
        raise ValueError(f"incomplete run: {path}")
    marker, seed, horizon, step = lines[0].split()
    if (marker, int(seed), float(horizon), float(step)) != (
        "start", case["seed"], case["horizon"], case["step"]
    ):
        raise ValueError(f"unexpected run configuration: {path}")
    samples = [Sample.parse(line) for line in lines if line.startswith("sample ")]
    if not samples:
        raise ValueError(f"missing samples: {path}")
    for earlier, later in zip(samples, samples[1:]):
        if earlier.elapsed >= later.elapsed or any(
            a > b for a, b in zip(earlier.cumulative(), later.cumulative())
        ):
            raise ValueError(f"non-monotonic history: {path}")
    final = samples[-1]
    if min(final.deliveries, final.first_hire, final.companies, final.staff) <= 0:
        raise ValueError(f"missing economic activity: {path}")
    victory = final.victory if final.victory >= 0 else None
    if victory is None:
        if final.elapsed != case["horizon"] or final.peak >= 0.51:
            raise ValueError(f"unexpected cutoff: {path}")
    elif final.elapsed != victory or final.peak < 0.51 or not final.winner_staff:
        raise ValueError(f"unexpected victory: {path}")
    for line in lines:
        if line.startswith(("player ", "player_end ")):
            if "money=40000.00" not in line or "licences=0 staff=0" not in line:
                raise ValueError(f"unexpected human state: {path}")
    row = dict(case=case["case"], timing_seed=case["seed"], elapsed_seconds=final.elapsed,
               first_hire_seconds=final.first_hire, victory_seconds=victory,
               winner_employees=final.winner_staff or None, peak_rival_share=final.peak,
               peak_companies=final.companies, largest_staff=final.staff,
               paid_ai_deliveries=final.deliveries, log_sha256=hashlib.sha256(raw).hexdigest())
    return row, samples


def summarize(cases, runs):
    rows = [runs[c["case"]][0] for c in cases]
    wins = sorted(r["victory_seconds"] for r in rows if r["victory_seconds"] is not None)
    hours = cases[0]["horizon"] / 3600
    cuts = [h for h in (12, 24, 48, 72, 96, 120, 144, 168, 192, 216, 240) if h <= hours]
    peaks_by_hour = {}
    for hour in cuts:
        peaks = []
        for case in cases:
            history = runs[case["case"]][1]
            at_hour = next((s for s in history if s.elapsed >= hour * 3600), history[-1])
            peaks.append(at_hour.peak)
        peaks_by_hour[hour] = dict(min=min(peaks), median=median(peaks), max=max(peaks))
    return dict(
        mean_step_seconds=cases[0]["step"] or None, horizon_hours=hours, runs=len(rows),
        wins=len(wins), peak_share_at_hour=peaks_by_hour,
        cumulative_wins={h: sum(t <= h * 3600 for t in wins) for h in cuts},
        win_time_seconds=dict(first=wins[0], median_of_winners=median(wins), last=wins[-1])
        if wins else None, rows=rows,
    )


def verify_replay(short_runs, long_runs, pairs, cutoff_hours):
    matched = 0
    for short_id, long_id in pairs:
        short_row, short_history = short_runs[short_id]
        long_row, long_history = long_runs[long_id]
        # The shortened horizon can clip the last frame, slightly altering the
        # measurement right at the cutoff. Verify every preceding hourly sample.
        prefix = [s for s in short_history if s.elapsed < cutoff_hours * 3600]
        if short_row["timing_seed"] != long_row["timing_seed"] or prefix != long_history[:len(prefix)]:
            raise ValueError(f"non-reproducible campaign: {short_id}, {long_id}")
        if short_row["victory_seconds"] is not None and short_history != long_history:
            raise ValueError(f"non-reproducible ending: {short_id}, {long_id}")
        matched += len(prefix)
    return matched


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result, histories = {}, {}
    for name in ("series", "profile-series", "long-series", "recorded-series",
                 "recorded-long-series", "twenty-hz-series"):
        root = args.root / name
        design = json.loads((root / "design.json").read_text())
        cases = design["cases"]
        runs = {case["case"]: read_run(root, case) for case in cases}
        histories[name] = runs
        steps = sorted({c["step"] for c in cases}, reverse=True)
        result[name] = dict(sha256=design["sha256"], batches=[
            summarize([case for case in cases if case["step"] == step], runs) for step in steps
        ])
    result["paired_verification"] = dict(
        matching_profile_samples=verify_replay(
            histories["profile-series"], histories["long-series"], [(i, i) for i in range(1, 13)], 120),
        matching_recorded_samples=verify_replay(
            histories["recorded-series"], histories["recorded-long-series"], [(i, i) for i in range(1, 5)], 24),
        matching_twenty_hz_samples=verify_replay(
            histories["profile-series"], histories["twenty-hz-series"], [(i + 12, i) for i in range(1, 5)], 120),
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
