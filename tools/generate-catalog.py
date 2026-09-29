#!/usr/bin/env python3
"""Regenerate assets/screensaver-chooser/hacks.tsv from the ship set.

The single source of truth is the `ncz_ship_bins` list in meson.build (the
same list that decides which executables are installed). Columns:
id, display name, category, group.
"""
import argparse
import pathlib
import re

CLASSICS = {
    "voronoi", "projectiveplane", "klein", "hypertorus", "cubestorm",
    "hexstrut", "crackberg", "cityflow", "geodesic", "gravitywell",
    "noof", "gibson",
}
TITLES = {
    "blackhole": "Black Hole",
}


def ship_list(source):
    m = re.search(r"ncz_ship_bins\s*=\s*\[(.*?)\]", source, re.S)
    if m is None:
        raise SystemExit("missing ncz_ship_bins list in meson.build")
    return re.findall(r"['\"]([A-Za-z0-9_-]+_gles3)['\"]", m.group(1))


def title(stem):
    if stem in TITLES:
        return TITLES[stem]
    # bestill0-0 -> "Bestill 0.0"; neongravity-1 -> "Neongravity 1"
    m = re.fullmatch(r"([a-z]+)(\d)-(\d)", stem)
    if m:
        return f"{m.group(1).capitalize()} {m.group(2)}.{m.group(3)}"
    words = stem.replace("-", " ").replace("_", " ").split()
    return " ".join(w.capitalize() for w in words)


def classify(target):
    stem = target.removesuffix("_gles3")
    if stem == "blackhole":
        return stem, "Black Hole Simulation"
    for prefix in ("hyprsaver", "xshadertoy"):
        if stem.startswith(prefix + "_"):
            return stem[len(prefix) + 1:], prefix
    if stem in CLASSICS:
        return stem, "Classics"
    raise SystemExit(f"unclassified ship entry: {target}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("meson_build", type=pathlib.Path)
    ap.add_argument("output", type=pathlib.Path)
    args = ap.parse_args()
    src = args.meson_build.read_text()
    ships = ship_list(src)
    order = {"Black Hole Simulation": 0, "hyprsaver": 1, "xshadertoy": 2, "Classics": 3}
    rows = []
    for t in ships:
        stem, group = classify(t)
        rows.append((order[group], t, title(stem), group))
    rows.sort(key=lambda r: (r[0], r[1]))
    lines = ["# Generated from meson.build (ncz_ship_bins) by tools/generate-catalog.py"]
    lines += [f"{t}\t{name}\t{g}\t{g}" for _, t, name, g in rows]
    args.output.write_text("\n".join(lines) + "\n")
    counts = {}
    for _, _, _, g in rows:
        counts[g] = counts.get(g, 0) + 1
    print(f"{len(rows)} entries:", " ".join(f"{g}={n}" for g, n in counts.items()))


if __name__ == "__main__":
    main()
