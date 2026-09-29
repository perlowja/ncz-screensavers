#!/usr/bin/env python3
"""Build assets/screensaver-chooser/tiers.tsv from host-test perf runs.

usage: measure-tiers.py OUT.tsv --mali DIR --uhd630 DIR [--navi14 DIR] [--rtx2060 DIR] --commit SHA

Each DIR is a host-test.sh result directory (or its host subdirectory) from a run
with the perf phase. Rule: a hack is tier "igpu" when it holds at least 30 fps
with a p95 frame time of at most 40 ms on BOTH the Mali-G720 and the Intel UHD 630
runs; everything else is "discrete". Columns: id, tier, then fps/p95_ms for
mali_g720, intel_uhd630, amd_navi14, nvidia_rtx2060 ("-" when not measured),
then "date commit".
"""

import argparse
import datetime
import json
import pathlib
import sys

MIN_FPS = 30.0
MAX_P95_MS = 40.0
COLUMNS = ("mali", "uhd630", "navi14", "rtx2060")


def load(path):
    p = pathlib.Path(path)
    cands = [p / "results.json", *sorted(p.glob("*/results.json"))]
    for c in cands:
        if c.is_file():
            return json.loads(c.read_text())["perf"]["hacks"]
    sys.exit(f"no results.json with perf data under {path}")


def cell(entry):
    if not entry or "fps" not in entry:
        return "-"
    return f"{entry['fps']:.1f}/{entry['p95_ms']:.1f}"


def ok(entry):
    return (
        bool(entry)
        and entry.get("fps", 0) >= MIN_FPS
        and entry.get("p95_ms", 1e9) <= MAX_P95_MS
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out", type=pathlib.Path)
    ap.add_argument("--mali", required=True)
    ap.add_argument("--uhd630", required=True)
    ap.add_argument("--navi14")
    ap.add_argument("--rtx2060")
    ap.add_argument("--commit", required=True)
    args = ap.parse_args()
    data = {
        "mali": load(args.mali),
        "uhd630": load(args.uhd630),
        "navi14": load(args.navi14) if args.navi14 else {},
        "rtx2060": load(args.rtx2060) if args.rtx2060 else {},
    }
    ids = sorted(set(data["mali"]) | set(data["uhd630"]))
    stamp = f"{datetime.datetime.now(tz=datetime.UTC).date().isoformat()} {args.commit}"
    lines = [
        "# GPU tier per screensaver: igpu = at least 30 fps with p95 frame time up to 40 ms on BOTH the Mali-G720 and the",
        "# Intel UHD 630 at native resolution; discrete = everything else. Columns (tab separated): id, tier, then fps/p95_ms",
        "# for mali_g720, intel_uhd630, amd_navi14, nvidia_rtx2060 ('-' = not measured), then measured date and commit.",
        "# Produced by tools/measure-tiers.py from host-test.sh perf runs. Consumed by the launcher and the settings UIs.",
    ]
    for hid in ids:
        good = ok(data["mali"].get(hid)) and ok(data["uhd630"].get(hid))
        cells = [cell(data[c].get(hid)) for c in COLUMNS]
        lines.append("\t".join([hid, "igpu" if good else "discrete", *cells, stamp]))
    args.out.write_text("\n".join(lines) + "\n")
    igpu = sum(1 for ln in lines if "\tigpu\t" in ln)
    print(f"{len(ids)} hacks: {igpu} igpu, {len(ids) - igpu} discrete -> {args.out}")


if __name__ == "__main__":
    main()
