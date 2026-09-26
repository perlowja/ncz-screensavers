# gl-transitions roundup — v2 (cerberus / NVIDIA round)

**Date:** 2026-09-26
**Status:** supersedes `ROUNDUP-2026-09-26.md` (MEDUSA / Navi14 /
Mesa round) on these points:
  - **SHORTLIST recomposition** (removed 3 wrapper-bugged entries,
    added 6 better-character entries — see STEP3-SHORTLIST.md)
  - **Wrapper uniform-name shadowing bug** discovered during
    capture analysis (see STEP2-CAPTURES.md)
  - **Counts re-measured on cerberus / NVIDIA 595.58.03** — see
    STEP1-SMOKE-COUNT.md

The MEDUSA / Navi14 roundup is preserved unmodified for
reproducibility; both hardware runs agree on the family-doctrine
shortlist at the family level (warp / burn / glitch / dissolve /
displacement ships; geometric wipes vendored-but-unused) and
disagree on three transition-specific picks (the wrapper-bug
affected ones) and by ±1 transition on the compile-failure count
(driver-version sensitivity).

## TL;DR

| Bucket | Count | Disposition |
|--------|------:|-------------|
| vendored | 125 | — |
| compile/link/render OK | 90 | — |
| broken by uniform-shadowing wrapper bug (5 shaders) | (subset) | fix wrapper → recover |
| genuinely broken (compile-fail or all-black) | 35 | vendored-but-unused |
| corporate-slideshow geometric wipes | 35 | vendored-but-unused |
| **SHIPPING SHORTLIST (v2)** | **18** | **ship** |

## Hardware run

This round was measured on cerberus (NVIDIA RTX 4500 Ada, NVIDIA
EGL 595.58.03, GLES 3.2, GLSL ES 3.20) using
`src/gles3_transitions_smoke.c` against the 125 vendored shaders.
`EGL_PLATFORM=surfaceless` was used to create the headless
context per the lesson in MEMORY.

  - **Step 1 (compile / link / render count):** see
    `STEP1-SMOKE-COUNT.md` and `smoke-results.csv`.
  - **Step 2 (mid-blend captures, 17 transitions, 6 progress
    values each):** see `STEP2-CAPTURES.md` and the
    `captures/` subdirectory (102 individual PNGs + 17
    montages).
  - **Step 3 (curated SHORTLIST):** see `STEP3-SHORTLIST.md`
    and `SHORTLIST.csv` (machine-readable). The active
    `../shortlist.txt` was updated to match.

## How v2 differs from v1 (the MEDUSA / Navi14 round)

  - **3 entries removed:** `burn0`, `dissolve`, `parametric_glitch`.
    These pass the smoke-tester's compile/link/render check but
    are visually broken because the wrapper declares
    `uniform sampler2D from; uniform sampler2D to;` in its
    preamble, and these three shaders declare local
    `vec4 from = getFromColor(uv); vec4 to = getToColor(uv);`
    that **shadow** the global samplers. The NVIDIA driver
    returns black for the shadowed texture lookup; burn0 dodges
    it at p=0 and p=1 via early returns but is broken mid-blend;
    dissolve is broken at every value below p=0.80. (Two more —
    Overexposure and perlin — are in the same fix bucket and are
    not on any SHORTLIST candidate list.)

  - **6 entries added:** `randomNoisex`, `displacement`,
    `crosshatch`, `DefocusBlur`, `Swirl`, `TilesWave`. These
    are **new SHORTLIST picks** that the v1 round missed:
      - `randomNoisex` — the cleanest pure-noise dissolve in the
        catalogue (v1 listed `dissolve` which is broken; this
        replacement delivers the dissolve family honestly)
      - `displacement` — the vector-field displacement of the
        displacement family (v1 didn't list any displacement-
        specific entry; ripple was the closest)
      - `crosshatch` — a moving cross-hatch dissolve (v1 listed
        the static chessboard-mask ones, which read as corporate
        slides)
      - `DefocusBlur` — defocus blur (v1 had LinearBlur only)
      - `Swirl` — vortex (v1 missed it; reads as motion, not
        as a state-machine primitive, so it earns its slot)
      - `TilesWave` — tile-grid wave with a propagating front
        (same exception as Swirl)

  - **Compile counts:** v1 reported 97 compile / 91 render on
    Navi14/Mesa; v2 reports 98 compile / 90 render on NVIDIA.
    One driver-version difference in each direction; both are
    in the same family and confirm the upstream-portability
    claim. The 5 wrapper-bug-affected shaders (Overexposure,
    burn0, dissolve, parametric_glitch, perlin) are counted as
    render-OK by both runs even though they're visually broken —
    the bug is in the wrapper's preamble, not in the shaders.

## Reproduction

```
EGL_PLATFORM=surfaceless \
gles3_transitions_smoke \
  <list.txt> \
  vendor/gl-transitions/transitions/ \
  <dump-dir> \
  <shortlist.txt>
```

`list.txt` is the 125-line list (one basename per line) derived
from the `gles3_transitions_list` array in `meson.build`.
`shortlist.txt` is `docs/audit/gl-transitions-evidence/shortlist.txt`
(this v2 has 18 names).

Wall time on cerberus: < 2 seconds for 125 compile/link + 102
mid-blend captures + 18 single p=0.5 captures.

## Files in this round's evidence pack

  - `STEP1-SMOKE-COUNT.md`  — concrete count breakdown by failure
    category (this round)
  - `STEP2-CAPTURES.md`     — visual analysis of 17 captured
    transitions + the wrapper-shadowing bug write-up
  - `STEP3-SHORTLIST.md`    — full SHORTLIST rationale (18 entries
    tiered into core / variety / texture)
  - `SHORTLIST.csv`         — machine-readable SHORTLIST with
    rank / tier / family / ship-reason columns
  - `smoke-results.csv`     — full 125-row per-shader table
    (compile / link / render + first line of fail reason)
  - `smoke.log`             — raw stderr from the smoke run
  - `captures/`             — 17 montages + 102 individual
    mid-blend PNGs
  - `rgba2png.py`           — utility that converts the smoke
    tester's `.rgba` dumps into PNGs

## Open loops and follow-ups (not in scope for this round)

  - **Wrapper uniform-naming fix.** Rename `sampler2D from` →
    `sampler2D tex_from` (and `to` → `tex_to`) in
    `src/gles3_transitions.c` and `src/gles3_transitions_smoke.c`.
    Recovers 5 visually-broken transitions at zero design cost.
    Highest-priority follow-up — affects the production driver
    too, not just the smoke tester.
  - **Extended adapter.** Pre-define the upstream gl-transitions
    host-side macros (`PI`, `ratio`, `DEG2RAD`, `POINTS`,
    `STAR_ANGLE`, `nQuick`) and handle the GLSL-ES C1059
    non-const-init rule. Could recover some of the 27 compile-
    failures — tracked in MEMORY.
  - **Capture at screensaver resolution.** Re-run the smoke
    tester at 1920x1080 (or whatever the production target is)
    and re-eyeball `directionalwarp` to confirm whether the
    256x256 weak rendering was a small-image artefact.

— generated by gles3_transitions_smoke + curation analysis on
  cerberus, 2026-09-26 02:00 EDT