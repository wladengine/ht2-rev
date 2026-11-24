"""Extract the recorded game frame duration from decoded player-state telemetry."""

import argparse
import json
import math
from pathlib import Path
import struct


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("events", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    with args.events.open(encoding="utf-8") as source, args.output.open("xb") as output:
        for line in source:
            frame = json.loads(line).get("dt_raw")
            if frame is None:
                continue
            if not math.isfinite(frame) or not 0 < frame <= 0.30:
                raise ValueError(f"unexpected active frame duration {frame}")
            output.write(struct.pack("<d", frame))


if __name__ == "__main__":
    main()
