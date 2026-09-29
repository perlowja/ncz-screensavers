#!/usr/bin/env python3
"""Generate the checked-in full-matrix ledger from the raw TSV evidence."""

import csv
from collections import Counter, defaultdict
from pathlib import Path

root = Path(__file__).resolve().parents[1]
evidence = root / "validation/full_matrix_2026-09-25"


def load(platform):
    with (evidence / platform / "results.tsv").open() as f:
        return {row["target"]: row for row in csv.DictReader(f, delimiter="\t")}


o6n = load("o6n")
pegasus = load("pegasus")
assert set(o6n) == set(pegasus) and len(o6n) == 140


def detail(row):
    if row["status"] == "PASS":
        return f'PASS (animated; {int(row["changed_pixels"]):,} px changed)'
    reason = row["failure"]
    if reason == "black":
        return "FAIL (black frames; render loop advanced)"
    if reason == "static":
        return f'FAIL (static; {int(row["changed_pixels"]):,} px changed)'
    if reason == "exit_1" and row["target"] == "unicrud_gles3":
        return "FAIL (exit 1: no characters found)"
    if reason == "exit_1" and row["target"] == "unknownpleasures_gles3":
        return "FAIL (exit 1: cannot parse foreground color)"
    if reason == "exit_134":
        return "FAIL (SIGABRT after initial frames)"
    return f"FAIL ({reason})"


def failures(rows):
    grouped = defaultdict(list)
    for name, row in rows.items():
        if row["status"] == "FAIL":
            grouped[row["failure"]].append(name.removesuffix("_gles3"))
    return grouped


lines = [
    "# Full GLES3 hardware matrix — 2026-09-25",
    "",
    "This is the complete current build set: **140/140 targets on O6N arm64** and",
    "**140/140 targets on PEGASUS amd64**. No target is marked N/A. PASS means two",
    "real `grim` captures taken two seconds apart were non-black and differed by",
    "more than 1,000 pixels while the process remained alive. Exit code alone is",
    "not accepted. FAIL targets are set aside below with their observed mode.",
    "",
    "## Hardware and method",
    "",
    "| Host | Architecture | Renderer | Build | Coverage | Result |",
    "|---|---|---|---|---:|---:|",
    f'| O6N | arm64 | Mali-G720-Immortalis | native ULTRA arm64 at `251bf22` + uncommitted runner only | 140/140 | {Counter(r["status"] for r in o6n.values())["PASS"]} PASS / {Counter(r["status"] for r in o6n.values())["FAIL"]} FAIL |',
    f'| PEGASUS | amd64 | Mesa Intel UHD Graphics CML GT2 (the active compositor GPU; not RTX 2060) | fresh native Meson/Ninja build | 140/140 | {Counter(r["status"] for r in pegasus.values())["PASS"]} PASS / {Counter(r["status"] for r in pegasus.values())["FAIL"]} FAIL |',
    "",
    "Each target has stderr, an exit-code file, two 480px evidence thumbnails,",
    "metrics, and SHA-256 hashes of the retained full-resolution captures under",
    "`validation/full_matrix_2026-09-25/{o6n,pegasus}/`. The full-resolution PNGs",
    "remain on each named host at `~/gles3-validation/full-matrix-2026-09-25/shots/`",
    "(176 MiB O6N; 129 MiB PEGASUS); thumbnails are checked in to keep the repo",
    "reviewable. Both runs dynamically discovered `/run/user/1000/wayland-0`.",
    "",
    "The initial O6N attempt inherited an obsolete broad `LD_LIBRARY_PATH` from",
    "the 2026-09-23 sweep and produced allocator corruption. That harness defect",
    "was removed and the O6N matrix was restarted from target 1; only the clean",
    "140-row rerun is reported here.",
    "",
    "## Complete ledger",
    "",
    "| Target | O6N arm64 | PEGASUS amd64 |",
    "|---|---|---|",
]
for target in sorted(o6n):
    lines.append(f"| `{target}` | {detail(o6n[target])} | {detail(pegasus[target])} |")

lines += ["", "## Set aside for focused fixing", ""]
for platform, rows in (("O6N arm64", o6n), ("PEGASUS amd64", pegasus)):
    lines += [f"### {platform}", ""]
    for reason, names in sorted(failures(rows).items()):
        explanation = {
            "black": "process and frame diagnostics continued, but both real captures were black",
            "static": "visible output was captured, but the two frames did not change materially",
            "exit_1": "initialization rejected required content/configuration; see per-target stderr",
            "exit_134": "process aborted after initial frames; see splitflap stderr",
        }.get(reason, reason)
        lines.append(f"- **{reason}** — {explanation}: " + ", ".join(f"`{n}`" for n in names) + ".")
    lines.append("")

lines += [
    "## Interpretation constraints",
    "",
    "- Black/static classification is deliberately fail-closed. Some savers can",
    "  have slow or dark phases, but the directive requires confirmed animation;",
    "  these targets therefore remain FAIL until a focused longer/manual run proves",
    "  otherwise.",
    "- PEGASUS is genuine amd64 hardware, but its active labwc session renders on",
    "  the Intel UHD 630-class iGPU. The installed RTX 2060 was not the compositor",
    "  GPU, so this ledger does not claim NVIDIA coverage.",
    "- MEDUSA was attempted first but was unreachable (`No route to host`); PEGASUS",
    "  supplied the complete amd64 matrix instead.",
]

(root / "docs/FULL-MATRIX-2026-09-25.md").write_text("\n".join(lines) + "\n")
