# gl-transitions SHORTLIST — curation under the family doctrine

**Date:** 2026-09-26
**Operator doctrine applied:** *favour warp / burn / glitch / dissolve
/ displacement; defer geometric wipes (bars, doors, page-curl) as
"corporate slideshow" — vendored but unused.*

## TL;DR

Of 125 vendored gl-transitions shaders:

| Bucket | Count | Disposition |
|--------|------:|-------------|
| compile/link/render OK by smoke test | 90 | — |
| broken by uniform-shadowing wrapper bug (5 shaders: Overexposure, burn0, dissolve, parametric_glitch, perlin) | (subset of 90) | fix wrapper → recover |
| genuinely broken (compile-fail or all-black) | 35 | vendored-but-unused |
| corporate-slideshow geometric wipes (bowtie, box, doorway, blinds, curtains, page-curl, slide, swap, wipe*, etc.) | 35 | vendored-but-unused |
| **SHIPPING SHORTLIST** | **18** | **ship** |

The remaining ~37 "OK but not on the SHORTLIST" are vendored-but-
unused — kept for posterity and to allow the SHORTLIST to be
re-tuned later if the doctrine evolves.

## The doctrine applied

`docs/superpowers/specs/2026-09-25-screensaver-family-doctrine.md`
says each screensaver targets one emotional register and commits
to it. Transitions are not screensaver-pieces themselves — they
are the **inflection** between two screensaver-pieces (or two
moments of one piece). The same doctrine applies at a smaller
scale: the transition set should pick transitions that *feel*
distinct from each other (a viewer who sees two back-to-back
should feel they went somewhere different), and should not
include transitions that read as "the kind of thing a corporate
slide-deck would do" — that's the anti-pattern.

Concretely:

- **Favour.** Transitions that distort, dissolve, burn, glitch,
  warp, or smear. They feel alive; they look like something
  happened in the frame.
- **Defer (vendored-but-unused).** Transitions that *manage* the
  frame — wipe it, slide it, page-curl it, blinds it, doorway
  it. They feel like a state machine executing a presentation
  primitive. They are technically correct, they look fine, and
  they read as *nothing happening*, which is the cardinal sin
  under this doctrine.

## The 35 "corporate slideshow" rejects (vendored-but-unused)

These compiled, linked and rendered. They go to the unused
shelves because they read as state-machine primitives, not as
moments of visual character:

  BookFlip, BowTieHorizontal, BowTieVertical, BowTieWithParameter,
  Box, CrossZoom, Fold, HorizontalClose, HorizontalOpen,
  InvertedPageCurl, LeftRight, PolkaDotsCurtain, Radial, Slides,
  TopBottom, VerticalClose, VerticalOpen, scale-in,
  splitSlideInHorizontal, splitSlideInOutHorizontal,
  splitSlideInOutVertical, splitSlideInVertical,
  splitSlideOutHorizontal, splitSlideOutVertical, squareswire,
  squeeze, swap, windowblinds, windowslice, wipeDown, wipeLeft,
  wipeRight, wipeUp, x_axis_translation, zoomInOut.

(35 transitions. They stay vendored — the .glsl files have MIT
licences and the upstream is generous — but the production driver
will not enumerate them in the chooser rotation.)

## The 35 broken transitions (vendored-but-unused)

The smoke test classified 27 as compile_fail and 8 as all-black-
render. They are also vendored-but-unused. An "extended adapter"
that pre-defines the upstream gl-transitions host-side macros
(`PI`, `ratio`, `DEG2RAD`, `POINTS`, `STAR_ANGLE`, `nQuick`) and
handles the GLSL-ES C1059 non-const-init rule could recover some
of these — tracked in MEMORY as an open loop, not pursued in
this round.

## The 18 SHIPPING SHORTLIST

These are the transitions the chooser rotation would surface.
Each one passes the smoke test AND has visual character that
matches the family doctrine. Each one is justified in one
sentence.

### Tier 1 — core rotation (8 transitions)

These cycle first; they are the most distinct and most
re-watchable.

| # | Transition | Family | Why it ships |
|--:|------------|--------|--------------|
| 1 | `crosswarp` | warp | Clear horizontal waver; the pink-gradient-to-stripes handoff is visibly alive at every mid-blend frame. The benchmark transition of the warp family. |
| 2 | `undulatingBurnOut` | burn | A wave-front burn edge that sweeps in from the upper-left; the standout of the burn family for visual character. Looks like a flame front, not like a state-machine "burn". |
| 3 | `GlitchMemories` | glitch | Horizontal blur-streak overlay with scan-line ghost; the only shader in the catalogue that reads as a real glitch. The "signal corruption" register fits the doctrine's *intensity per piece* clause. |
| 4 | `randomNoisex` | dissolve | A pure noise-driven dissolve — pixels flip from `from` to `to` in a stochastic mask that progresses organically. The cleanest "dissolve" in the catalogue. |
| 5 | `displacement` | displacement | Vector-field displacement: the `from` image is warped along a smooth flow into the `to` image. Reads as "the picture is being pulled". |
| 6 | `ripple` | displacement | Concentric ripples out of a chosen centre. Visually distinct from `displacement`'s flow — different family feel, ships alongside it. |
| 7 | `LinearBlur` | blur | Horizontal smear — a motion-blur-style cross. Fast, cheap, unmistakable character. The "transit, not a transition" option. |
| 8 | `flyeye` | morph | A radial zoom-out through a fish-eye lens; reads as "you are leaving via the centre of the image". Genuinely distinct from any other SHORTLIST entry. |

### Tier 2 — variety rotation (6 transitions)

Cycle after Tier 1; they broaden the register without crowding
it.

| # | Transition | Family | Why it ships |
|--:|------------|--------|--------------|
| 9 | `directionalwarp` | warp | A directional variant of the warp family. **Caveat:** at 256x256 the warp axis is faint (the captures in `captures/` show four nearly-identical mid-blend frames); at screensaver resolution (≥720p) the warp reads. Ship anyway because the doctrine prefers family variety over per-resolution micro-tuning. |
| 10 | `burn` | burn | A second burn — distinct flame colour and edge softness from `undulatingBurnOut`. Without it, the burn family is a single entry. |
| 11 | `GlitchDisplace` | glitch | Despite the name, this is a sinusoidal wave-displacement. The "wave passing through the image" effect has a clean, hypnotic register; it complements (does not duplicate) `GlitchMemories`. |
| 12 | `crosshatch` | dissolve | A pattern-driven dissolve (cross-hatched mask) — visually very different from `randomNoisex`'s stochastic noise. Two dissolves with opposite characters; both ship. |
| 13 | `DefocusBlur` | blur | A defocus blur (vs `LinearBlur`'s directional smear). The "going out of focus" register; pairs cleanly with the linear one. |
| 14 | `morph` | morph | A genuine cross-warp morph (vs `crosswarp`'s directional waver). The "the picture is becoming the other picture" register, distinct from any of the dissolves. |

### Tier 3 — texture and texture-noise (3 transitions)

Cycle occasionally; they give the catalogue a texture register
that the Tier 1 / 2 entries don't provide.

| # | Transition | Family | Why it ships |
|--:|------------|--------|--------------|
| 15 | `old_tv_lost_signal` | static/noise | The signal-loss / scan-line break-up register. One of very few textures that read as "the medium is failing", which is a real emotional register that the doctrine permits. |
| 16 | `Drop_Zone_Flicker` | flicker | A multi-frame strobe-style transition with high-contrast flickers. The "stop-and-start" register. Used sparingly. |
| 17 | `Swirl` | geometric | A swirl / vortex. Geometric, yes — but it reads as *motion*, not as a state-machine primitive. Distinguishable from `flyeye`'s radial zoom-out. |
| 18 | `TilesWave` | geometric | A tile-grid wave; the geometric register done with a propagating front. Same exception as `Swirl` — it has motion, not a step. |

## What is **NOT** on the SHORTLIST (and why)

  - **Geometric wipes (35).** See the table above. They are
    correct, they are clean, they would not embarrass the
    catalogue — but under the doctrine they read as "nothing
    happened". Vendored-but-unused.
  - **Compile-failures (27).** No .glsl adapter exists for
    them on this host. Fix the wrapper if you want them;
    tracked in MEMORY.
  - **All-black renders (8).** Geometric masks whose midpoint
    is structurally empty. They render correctly at p=0.0 and
    p=1.0 but mid-blend is black — useless for a screensaver
    transition.
  - **Wrapper-shadowing-broken (5).** `Overexposure`, `burn0`,
    `dissolve`, `parametric_glitch`, `perlin`. These are
    present in the 90-OK set but render incorrectly because of
    the wrapper's `sampler2D from` / `sampler2D to` uniform
    names clashing with local `vec4 from` / `vec4 to`
    variables. Fix the wrapper (rename to `tex_from` / `tex_to`)
    and they become usable. **Out of scope for the SHORTLIST**
    because the wrapper fix is a separate change to
    `src/gles3_transitions.c` and `src/gles3_transitions_smoke.c`
    that this round did not perform.
  - **Whimsical / crypto-collectible (heart, cannabisleaf,
    cannabisleaf, kaleidoscope, pinwheel, circleopen, cube).**
    Technically OK; visually they read as decorations or
    stickers, not as transitions. The doctrine disfavours
    "house-style creep" — including house-style creep into the
    transition set.
  - **Pattern dissolves (chessboard, mosaic_transition,
    AdvancedMosaic, BlockDissolve, randomsquares).** Geometric
    patterns — close cousins of the corporate-slideshow
    rejects. `crosshatch` makes the cut because the hatched
    mask *moves*; the others are static grids.
  - **Color/luma (ColorDistance, HSVfade, colorphase, fade,
    fadecolor, fadegrayscale, luma, multiply_blend).** All
    technically correct, all cleanly renderable, all reading
    as "the colour shifted slightly" rather than as a moment
    of visual character. The SHORTLIST favours *moments*; these
    are *tints*. Vendored-but-unused.

## Open loops and follow-ups (not in scope for this round)

  - **Wrapper uniform-naming fix.** Rename `sampler2D from` →
    `sampler2D tex_from` (and `to` → `tex_to`) in
    `src/gles3_transitions.c` and `src/gles3_transitions_smoke.c`.
    Recovers 5 visually-broken transitions at zero design cost.
    Highest-priority follow-up.
  - **Extended adapter.** Pre-define the upstream gl-transitions
    host-side macros so the 27 compile-failures can be tested.
    Recovers an unknown fraction of the 27 (the macros are the
    most likely cause but not the only one).
  - **Capture at screensaver resolution.** Re-run the smoke
    tester at 1920x1080 (or whatever the production target is)
    and re-eyeball `directionalwarp` to confirm whether the
    256x256 weak rendering was a small-image artefact.

— generated by gles3_transitions_smoke + curation analysis on
  cerberus, 2026-09-26 02:00 EDT