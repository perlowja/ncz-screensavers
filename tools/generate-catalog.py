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
    "voronoi",
    "projectiveplane",
    "klein",
    "hypertorus",
    "cubestorm",
    "hexstrut",
    "crackberg",
    "cityflow",
    "geodesic",
    "gravitywell",
    "noof",
    "gibson",
}
TITLES = {
    "blackhole": "Black Hole",
}


def ship_list(source):
    m = re.search(r"ncz_ship_bins\s*=\s*\[(.*?)\]", source, re.DOTALL)
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
            return stem[len(prefix) + 1 :], prefix
    if stem in CLASSICS:
        return stem, "Classics"
    raise SystemExit(f"unclassified ship entry: {target}")



OPT_TYPES = {"bool", "int", "float", "enum", "string"}


def validate_options(directory, ships):
    """Validate options/<hack>.tsv schemas: 10 tab separated columns
    (name, type, default, min, max, choices, label, description, group, env)."""
    if not directory.is_dir():
        return
    problems = []
    for path in sorted(directory.glob("*.tsv")):
        hack = path.stem
        if not hack.startswith("_") and not any(t == hack or t == hack + "_gles3" for t in ships):
            problems.append(f"{path.name}: no ship-set hack named {hack}")
        seen = set()
        for n, line in enumerate(path.read_text().splitlines(), 1):
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            c = line.split("\t")
            where = f"{path.name}:{n}"
            if len(c) != 10:
                problems.append(f"{where}: expected 10 columns, got {len(c)}")
                continue
            name, typ, default, lo, hi, choices, label, desc, group, env = c
            if not re.fullmatch(r"[a-z][a-z0-9-]*", name):
                problems.append(f"{where}: bad option name {name!r}")
            if name in seen:
                problems.append(f"{where}: duplicate option {name}")
            seen.add(name)
            if typ not in OPT_TYPES:
                problems.append(f"{where}: unknown type {typ!r}")
                continue
            if not (label.strip() and desc.strip() and group.strip()):
                problems.append(f"{where}: label, description and group are required")
            if not re.fullmatch(r"[A-Z][A-Z0-9_]*", env):
                problems.append(f"{where}: bad env name {env!r}")
            try:
                if typ in ("int", "float"):
                    fl, fh, fd = float(lo), float(hi), float(default)
                    if not fl <= fd <= fh:
                        problems.append(f"{where}: default {default} outside {lo}..{hi}")
                    if typ == "int" and fd != int(fd):
                        problems.append(f"{where}: int default {default} is not an integer")
                elif typ == "bool":
                    if default not in ("true", "false"):
                        problems.append(f"{where}: bool default must be true or false")
                elif typ == "enum":
                    if default not in choices.split(","):
                        problems.append(f"{where}: enum default {default!r} not in choices")
            except ValueError:
                problems.append(f"{where}: numeric column is not a number")
    if problems:
        raise SystemExit("options schema: " + "; ".join(problems))


def load_schema(path):
    rows = {}
    for line in path.read_text().splitlines():
        if line.strip() and not line.startswith("#"):
            c = line.split("\t")
            if len(c) == 10:
                rows[c[0]] = c
    return rows


def check_value(row, value):
    """Return an error string or None for `value` against one schema row."""
    name, typ, default, lo, hi, choices = row[:6]
    if typ in ("int", "float"):
        try:
            v = float(value)
        except ValueError:
            return "not a number"
        if not float(lo) <= v <= float(hi):
            return f"outside {lo}..{hi}"
        if typ == "int" and v != int(v):
            return "not an integer"
    elif typ == "bool":
        if value not in ("true", "false", "1", "0"):
            return "not a bool"
    elif typ == "enum":
        if value not in choices.split(","):
            return f"not one of {choices}"
    return None


def parse_conf(path):
    sections, cur = {}, None
    for n, line in enumerate(path.read_text().splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("["):
            cur = line.strip("[]")
            sections.setdefault(cur, {})
        elif "=" in line and cur:
            k, v = line.split("=", 1)
            sections[cur][k.strip()] = v.strip()
        else:
            raise SystemExit(f"{path}: line {n}: cannot parse")
    return sections


PRESET_TITLES = {"blackhole": ("Black Hole", "Black Hole Simulation")}


def build_presets(root, ships):
    """Validate assets/screensaver-chooser/options/presets/<hack>/*.conf against the
    option schema and return presets.tsv rows: id, title, group, hack, args,
    accuracy, description."""
    base = root / "assets" / "screensaver-chooser" / "options"
    rows, problems = [], []
    pdir = base / "presets"
    if not pdir.is_dir():
        return rows
    for hdir in sorted(d for d in pdir.iterdir() if d.is_dir()):
        hack = hdir.name
        binary = hack + "_gles3"
        if binary not in ships:
            problems.append(f"{hdir}: no ship-set hack {binary}")
            continue
        schema_path = base / (hack + ".tsv")
        schema = load_schema(schema_path) if schema_path.exists() else {}
        title, group = PRESET_TITLES.get(hack, (hack, hack))
        for conf in sorted(hdir.glob("*.conf")):
            pid = conf.stem
            if not re.fullmatch(r"[a-z0-9][a-z0-9-]*", pid):
                problems.append(f"{conf.name}: bad preset id")
                continue
            sec = parse_conf(conf)
            meta = sec.get("preset", {})
            for k in ("name", "description", "accuracy", "sources"):
                if not meta.get(k):
                    problems.append(f"{conf.name}: [preset] {k} is required")
            if meta.get("accuracy") not in ("faithful", "artistic"):
                problems.append(f"{conf.name}: accuracy must be faithful or artistic")
            opts = sec.get(hack, {})
            if not opts:
                problems.append(f"{conf.name}: missing [{hack}] section")
            for k, v in opts.items():
                if k == "preset" or k not in schema:
                    problems.append(f"{conf.name}: option {k!r} is not in the {hack} schema")
                    continue
                err = check_value(schema[k], v)
                if err:
                    problems.append(f"{conf.name}: {k}={v}: {err}")
            if problems and any(conf.name in p for p in problems):
                continue
            rows.append("\t".join([
                f"{binary}--{pid}", f"{title}: {meta['name']}", group, binary,
                f"--preset={pid}", meta["accuracy"], meta["description"]]))
    if problems:
        raise SystemExit("presets: " + "; ".join(problems))
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("meson_build", type=pathlib.Path)
    ap.add_argument("output", type=pathlib.Path)
    args = ap.parse_args()
    src = args.meson_build.read_text()
    ships = ship_list(src)
    sparse = args.output.with_name("sparse.tsv")
    if not sparse.exists():
        sparse = (
            args.meson_build.parent / "assets" / "screensaver-chooser" / "sparse.tsv"
        )
    if not sparse.exists():
        raise SystemExit(
            f"sparse.tsv not found (looked next to the output and at {sparse})"
        )
    bad = []
    seen = set()
    for n, line in enumerate(sparse.read_text().splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        cols = line.split("\t")
        dup = cols[0] in seen
        seen.add(cols[0])
        problem = None
        if len(cols) != 3:
            problem = "needs exactly 3 tab-separated columns"
        elif cols[0] not in ships:
            problem = "id is not in the ship set"
        elif dup:
            problem = "duplicate id"
        elif not cols[2].strip():
            problem = "empty reason"
        else:
            try:
                floor = float(cols[1])
            except ValueError:
                floor = -1.0
            if not 0.0 < floor <= 1.0:
                problem = "floor must be a number in (0, 1]"
        if problem:
            bad.append(f"line {n}: {problem}: {line.strip()[:70]}")
    if bad:
        raise SystemExit("sparse.tsv: " + "; ".join(bad))
    validate_options(args.meson_build.parent / "assets" / "screensaver-chooser" / "options", ships)
    prows = build_presets(args.meson_build.parent, ships)
    if prows:
        args.output.with_name("presets.tsv").write_text(
            "# Generated by tools/generate-catalog.py from options/presets/<hack>/*.conf\n"
            "# id\ttitle\tgroup\thack\targs\taccuracy\tdescription\n" .replace("\\t","\t") + "\n".join(prows) + "\n")
    tiers = args.output.with_name("tiers.tsv")
    if not tiers.exists():
        tiers = args.meson_build.parent / "assets" / "screensaver-chooser" / "tiers.tsv"
    if tiers.exists():
        bad = []
        seen = set()
        for n, line in enumerate(tiers.read_text().splitlines(), 1):
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            cols = line.split("\t")
            problem = None
            if len(cols) != 7:
                problem = "needs exactly 7 tab-separated columns"
            elif cols[0] not in ships:
                problem = "id is not in the ship set"
            elif cols[0] in seen:
                problem = "duplicate id"
            elif cols[1] not in ("igpu", "discrete"):
                problem = "tier must be igpu or discrete"
            elif not all(
                c == "-" or re.fullmatch(r"\d+(\.\d+)?/\d+(\.\d+)?", c)
                for c in cols[2:6]
            ):
                problem = "GPU columns must be fps/p95_ms or -"
            elif not cols[6].strip():
                problem = "empty measured column"
            seen.add(cols[0])
            if problem:
                bad.append(f"line {n}: {problem}: {line.strip()[:70]}")
        missing = sorted(set(ships) - seen)
        if missing:
            bad.append(
                "missing ids: "
                + ", ".join(missing[:5])
                + (" ..." if len(missing) > 5 else "")
            )
        if bad:
            raise SystemExit("tiers.tsv: " + "; ".join(bad))
    order = {"Black Hole Simulation": 0, "hyprsaver": 1, "xshadertoy": 2, "Classics": 3}
    rows = []
    for t in ships:
        stem, group = classify(t)
        rows.append((order[group], t, title(stem), group))
    rows.sort(key=lambda r: (r[0], r[1]))
    lines = [
        "# Generated from meson.build (ncz_ship_bins) by tools/generate-catalog.py"
    ]
    lines += [f"{t}\t{name}\t{g}\t{g}" for _, t, name, g in rows]
    args.output.write_text("\n".join(lines) + "\n")
    counts = {}
    for _, _, _, g in rows:
        counts[g] = counts.get(g, 0) + 1
    print(f"{len(rows)} entries:", " ".join(f"{g}={n}" for g, n in counts.items()))


if __name__ == "__main__":
    main()
