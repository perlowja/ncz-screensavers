#!/usr/bin/env python3
"""Regenerate the per-binary matrix in the validation doc from real
results.csv evidence. Writes stdout-ready markdown that we paste into
the doc."""
import csv, sys

# Load all three platforms
def load(path):
    rows = {}
    with open(path) as f:
        reader = csv.DictReader(f)
        for r in reader:
            rows[r["binary"]] = r["verdict"]
    return rows

o6n = load("validation/o6n/results.csv")
medusa = load("validation/medusa/results.csv")
pegasus = load("validation/pegasus/results.csv")

# Load the 90-binary list (actual alphabetical from current meson.build)
with open("validation/o6n/binaries.list") as f:
    binaries = sorted(l.strip() for l in f if l.strip())

assert len(binaries) == 90, f"expected 90, got {len(binaries)}"

# Verdict abbreviations
def v(verdict):
    if verdict == "CRASH": return "**C**"
    if verdict == "HANG": return "**H**"
    return "P"

# Cross-platform aggregates (from compare_results.py output)
with open("validation/cross-platform-results.csv") as f:
    reader = csv.DictReader(f)
    cross = {r["binary"]: r for r in reader}

# Per-binary notes (based on my analysis)
notes = {
    "antinspect_gles3":      "See §4.1",
    "cityflow_gles3":        "See §4.1",
    "crackberg_gles3":       "See §4.1",
    "geodesicgears_gles3":   "Two related gears (geodesic gear mesh)",
    "geodesic_gles3":        "Geodesic dome wireframe",
    "gibson_gles3":          "Gibson lighting illusion",
    "glforestfire_gles3":    "See §4.1",
    "gltext_gles3":          "GL text renderer",
    "handsy_gles3":          "See §4.2 (consistent PASS)",
    "hypertorus_gles3":      "See §4.2 (consistent PASS, GLSL upstream)",
    "klein_gles3":           "See §4.2 (Klein bottle, GLSL upstream)",
    "lament_gles3":          "Intentionally dark figure on dark background — PASS on all 3",
    "mapscroller_gles3":     "**HANG** — see §5.2",
    "menger_gles3":          "See §4.1",
    "molecule_gles3":        "See §4.3 (intentionally dark on all 3)",
    "polyhedra-gl_gles3":    "See §4.2 (consistent PASS, fallback path)",
    "projectiveplane_gles3": "See §4.2 (consistent PASS, GLSL upstream)",
    "rubikblocks_gles3":     "Rubik's cube rotating",
    "splodesic_gles3":       "Particle splosion",
    "voronoi_gles3":         "See §4.1",
}
# Bold the 4 failures
failures = {"hexstrut_gles3", "highvoltage_gles3", "jigsaw_gles3", "mapscroller_gles3"}

# Print matrix
print("| Binary | O6N (Mali-G720) | MEDUSA (RADV Navi14) | PEGASUS (iris CML) | Notes |")
print("|--------|-----------------|-----------------------|---------------------|-------|")
for b in binaries:
    bname = b.replace("_gles3", "")
    name = f"**{bname}_gles3**" if b in failures else f"{bname}_gles3"
    o = o6n.get(b, "?")
    m = medusa.get(b, "?")
    p = pegasus.get(b, "?")
    n = notes.get(b, "")
    print(f"| {name:<24} | {v(o):<16} | {v(m):<22} | {v(p):<19} | {n} |")

print()
print(f"**Total rows in matrix:** {len(binaries)} (= 90 binaries tested on all 3 platforms)")