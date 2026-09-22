#!/usr/bin/env python3
"""
Regression test for the cross-platform GLES3 validation.

This script is invoked by `validation/validate.sh` (Gate 7, optional).
It re-derives the per-platform PASS/BLACK/CRASH/HANG rollup from the
committed `validation/<plat>/raw/results.csv` + `validation/<plat>/raw/shots/`
artifacts and asserts it matches the canonical numbers in
`docs/CROSS-PLATFORM-GLES3-VALIDATION-2026-09-22.md` §3.

This is the safeguard for the "doc and classifier disagree" failure
mode the previous attempt's reviewer flagged (see MEMORY.md "Lessons
Learned"): if the harness's classifier output is inconsistent with what
the doc claims, this script fails and tells you which platform's
counts drifted.

Exit codes:
  0  — every platform's derived rollup matches the canonical expectation
  1  — a platform's counts do not match (printed as the diagnostic)
"""
from __future__ import annotations

import csv
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DOC = REPO / "docs" / "CROSS-PLATFORM-GLES3-VALIDATION-2026-09-22.md"

# Canonical counts, taken directly from the doc's §3 "Per-platform
# rollup" table. Update these in lockstep with the doc.
EXPECTED = {
    "o6n":     {"PASS": 86, "BLACK": 0, "CRASH": 3, "HANG": 1},
    "medusa":  {"PASS": 86, "BLACK": 0, "CRASH": 3, "HANG": 1},
    "pegasus": {"PASS": 85, "BLACK": 1, "CRASH": 3, "HANG": 1},
}


def derive_rollup(plat: str) -> dict[str, int]:
    """
    Re-derive the rollup for one platform by re-running the harness's
    classify_results.py against the committed raw/ artifacts.

    This must be exactly the same logic the harness ran during the
    validation pass — if the classifier changes, both this test and
    the doc should be re-validated together.
    """
    import tempfile
    csv_in = REPO / "validation" / plat / "raw" / "results.csv"
    shots  = REPO / "validation" / plat / "raw" / "shots"
    logs   = REPO / "validation" / plat / "raw" / "logs"
    # Write intermediate classifier output to a tmpdir rather than into
    # the repo — we only need the CSV's verdict field, and a tmpdir
    # keeps the working tree clean (the previous attempt's commit left
    # classified.{csv,json} artifacts in the repo that no consumer
    # actually reads).
    with tempfile.TemporaryDirectory() as tmp:
        out_json = Path(tmp) / "classified.json"
        out_csv  = Path(tmp) / "classified.csv"
        subprocess.run(
            ["python3",
             str(REPO / "validation" / "classify_results.py"),
             "--results-csv", str(csv_in),
             "--shots-dir",    str(shots),
             "--logs-dir",     str(logs),
             "--out-json",     str(out_json),
             "--out-csv",      str(out_csv)],
            check=True,
            cwd=REPO,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

        counts: Counter[str] = Counter()
        with open(out_csv) as f:
            for row in csv.DictReader(f):
                counts[row["verdict"]] += 1
    return dict(counts)


def assert_doc_matches_rollup() -> list[str]:
    """
    Returns a list of human-readable diagnostics for each platform whose
    rollup disagrees with the canonical expectation. Empty list = OK.
    """
    diagnostics: list[str] = []
    for plat, want in EXPECTED.items():
        got = derive_rollup(plat)
        # Normalize: classifier may produce keys the doc doesn't enumerate.
        normalized = {k: got.get(k, 0) for k in want}
        if normalized != want:
            diff = ", ".join(
                f"{k}: want={want[k]} got={normalized[k]}"
                for k in want
                if normalized[k] != want[k]
            )
            diagnostics.append(
                f"  [{plat}] MISMATCH: {diff} (full got={got})"
            )
        else:
            print(f"  [{plat}] PASS rollup={normalized}")
    return diagnostics


def main() -> int:
    print("=== Re-deriving per-platform rollups from committed raw/ ===")
    fails = assert_doc_matches_rollup()
    if fails:
        print("\nROLLUP REGRESSION FAILURES:")
        for line in fails:
            print(line)
        print("\nDoc section that should match:")
        print(f"  {DOC} §3 'Per-platform rollup' table")
        return 1
    print("\nAll 3 platforms match canonical rollup.")
    return 0


if __name__ == "__main__":
    sys.exit(main())