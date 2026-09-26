# blackhole quality ladder — what each tier ADDS

**Date:** 2026-09-26
**Status:** operator doctrine response, see
`docs/superpowers/specs/2026-09-25-screensaver-family-doctrine.md` § "The
ladder must ADD, not merely subtract".

This document replaces a remove-only ladder for `blackhole` with one
that says, per tier, **what is gained** relative to the tier below —
not just what is dropped. The doctrine is explicit on this point and
the operator has stated the goal directly: *"I want people to be able
to show off their systems with this."* A remove-only ladder defeats
that; the strong machine gets nothing for being strong.

## The tiers

| Tier | Hardware | Frame-rate target | Design intent |
|---|---|---|---|
| **Floor** | Intel UHD CML GT2 and contemporaries | sustained ~22-30 fps | The piece runs, is recognisable, and is not an apology. |
| **Reference** | 2016-era discrete — NVIDIA Pascal (GTX 1060+), AMD Polaris (RX 480+) | sustained 60 fps | The piece as designed. Every effect at full strength. |
| **Ultra** | 2019+ discrete and newer — RTX 2060+, AMD Navi14+ | sustained 60+ fps with **visibly more** present in the frame | Higher march counts, more entities, more lights, extra optical terms. |

Also in scope, not addressed by the per-tier table: **CIX Sky1
Mali-G720** on the NCZ-OS arm64 boards. Per the family doctrine, it is
a primary platform for this distribution and is a different
architecture with different bottlenecks. It must be measured, not
assumed. A separate run on that hardware is owed before this ladder is
considered final.

## What the piece currently does

These are the things that exist in the shader and harness today; the
ladder scales each of them up the tier stack.

- **Disk evaluation** (`disk_color`): the disk is rendered via the
  Schwarzschild-style implicit surface: polar coords `(r, a)` in disk
  space, palette mapping, Doppler asymmetry from `u_rotation`,
  density modulation from `u_density`.
- **Lensing** (in `orbit_color` / `ray_march` loop): the ray is bent
  near the horizon, producing the characteristic gravitational arc and
  the photon ring.
- **Jet** (`if(u_jet>0.)`): an axial twin-jet modulated by
  high-frequency noise on the world-space point.
- **Nebula** (background): a procedural noise field modulated by
  `u_nebula`, `u_nebula_axis`, and `u_nebula_scheme`.
- **Stars** (background): density controlled by `u_star_density`,
  drawn as points.
- **Tonemap**: a Reinhard-style curve with `u_palette_contrast` toe.

## Per-tier table — what each tier ADDS

### Disk evaluation

| Tier | Adds relative to tier below |
|---|---|
| **Floor** | Base disk evaluation. Polar coords + Doppler + density. One disk harmonic per launch (path_o_harm_freq determines the wiggle). |
| **Reference** | **+1 disk harmonic** sampled per fragment, weighted by `path_o_harm_amp`. The harmonic produces visible additional structure in the disk (secondary brightness bands). Cost: ~15% more ALU per fragment. |
| **Ultra** | **+1 more harmonic + a per-fragment sub-disk perturbation** (a small noise modulation on the disk surface brightness with amplitude ~10% of base). This is what makes the disk look "alive" rather than "physically motivated". Cost: ~30% more ALU per fragment over reference. |

### Lensing

| Tier | Adds relative to tier below |
|---|---|
| **Floor** | Base lensing. Fixed-iteration ray-bending loop, ~12 steps. Produces a recognisable photon ring and a single arc over the top. |
| **Reference** | **+50% iteration count** (~18 steps), with adaptive sub-stepping near the horizon. Sharper photon ring, more accurate arc. The `if(r<3*M)` early-out stays; the inner steps add where they matter. |
| **Ultra** | **Full Schwarzschild-style integrator** (~32 steps, RK2 or RK4) instead of the cheap first-order loop, plus **secondary lensing** (one extra ray pass to capture the light that has bent MORE than π around the hole). This is the difference between "lensed" and "properly lensed": the secondary image appears as a faint arc opposite the primary, and is visible at edge-on viewing. Cost: ~3x fragment work; only affordable on real GPUs. |

### Jet

| Tier | Adds relative to tier below |
|---|---|
| **Floor** | Base jet. `u_jet * jet_color(axis) * (0.45+0.55*noise(p*45+...))`. One noise octave at frequency 45. |
| **Reference** | **+1 noise octave** at frequency 90 (the higher octave adds fine detail on the jet column). Also widens the jet's angular spread slightly so it reads at 4K. |
| **Ultra** | **+1 more noise octave at frequency 180 + temporal coherence** (the jet noise now varies slowly along the jet's length rather than per-fragment, giving a coherent "flowing" appearance). Cost: ~2x ALU on the jet term. |

### Nebula

| Tier | Adds relative to tier below |
|---|---|
| **Floor** | Base nebula. 2 noise octaves. Coverage from `u_nebula.z`. |
| **Reference** | **+2 octaves** (4 total). The nebula gains mid-frequency structure that was missing at floor; clouds have visible internal variation. |
| **Ultra** | **+2 more octaves + a slow drift** (the nebula noise input itself advances with time, so the clouds evolve during the launch rather than being a static field). Cost: ~2x ALU on the nebula term; only sensible on real GPUs. |

### Stars

| Tier | Adds relative to tier below |
|---|---|
| **Floor** | Base stars. `u_star_density` controls count. No twinkling. |
| **Reference** | **+twinkle modulation** at 2-4 Hz with small per-star phase offsets (so stars don't all blink in sync). |
| **Ultra** | **+brighter stars with a soft bloom halo** (each star's PSF is wider, with a gentle additive halo). At 4K this is what makes the starfield feel "deep" rather than "scattered". |

### Tonemap

| Tier | Adds relative to tier below |
|---|---|
| **Floor** | Reinhard-style curve. Single knee, controlled by `u_palette_contrast` (clamped 0.85..1.35). |
| **Reference** | **+a filmic shoulder** (a soft toe-to-knee-to-shoulder curve replacing the Reinhard, modelled on a Hejl-Burgess-Dawson fit). This is what gives the disk its "filmic" feel without blowing out the highlights. |
| **Ultra** | **+exposure adaptation** (the tonemap key value adapts slowly to the average scene luminance, so a launch with a bright disk and a launch with a dim disk both look correctly exposed). |

### Resolution

| Tier | Adds relative to tier below |
|---|---|
| **Floor** | Native panel resolution, no super-sampling. Anti-aliasing comes from shader-internal detail and the noise terms. |
| **Reference** | **+1x SSAA** (renders at 2x panel, downsamples). The photon ring and disk edges are visibly crisper at 4K. |
| **Ultra** | **+2x SSAA** (renders at 4x panel, downsamples). The piece is "good in the first fifteen seconds" partly because the edges don't crawl. |

### Internal precision

| Tier | Adds relative to tier below |
|---|---|
| **Floor** | Half-float (`GL_RGBA16F`) framebuffer; `mediump` in the shader. Adequate at 1080p; precision artefacts visible at 4K. |
| **Reference** | **Full float** (`GL_RGBA32F`) framebuffer; `highp` in the shader. No precision artefacts at 4K. |
| **Ultra** | Full float + **explicit dither** at the tonemap output (to break up banding from the float→8-bit conversion when downsampling). |

### Bloom / glow

| Tier | Adds relative to tier below |
|---|---|
| **Floor** | No separate bloom pass. The tonemap shoulder is the only thing keeping highlights under control. |
| **Reference** | **+single-pass separable Gaussian bloom** at small kernel (radius ~8px). Subtle halo around bright disk pixels and the photon ring. |
| **Ultra** | **+multi-pass progressive bloom** (radius 8, 16, 32 stacked) with a brightness threshold so only the brightest pixels contribute. This is what makes the disk look like it is actually emitting light into the surrounding space, not just being a bright surface. |

## What the table rules out

A few things are deliberately NOT in the ladder:

- **Volume / 3D fog.** Not in the piece at any tier; would change the
  register from "cosmic awe and dread" to "atmospheric". The family
  doctrine rules it out for this piece.
- **Reflection / refraction on the disk.** Same reason — the disk is a
  surface, not a fluid. Reflection would make it look like a mirror,
  which is wrong for the register.
- **Per-pixel motion blur on the disk.** Adds GPU cost for a "filming
  a real camera" effect that is the wrong register for this piece.

## How to verify a tier

The doctrine requires frame-time variance, not just the mean. For each
tier:

- **Measure mean frame time** at the target's preferred resolution.
- **Measure 1% low frame time** (the slowest 1% of frames; this is the
  stutter indicator).
- **State the parameters explicitly** (resolution, march steps, octaves,
  SSAA, tonemap mode).
- **Confirm the visible GAINS are visible.** A viewer with no metrics
  should look at the floor and reference side-by-side and see the
  difference in under five seconds. If they have to count pixels to
  notice, the gain is too small and the ladder entry needs to be
  revised.

A reference run against a Pascal or Polaris part is owed before this
ladder is shipped. Until that measurement exists, the per-tier entries
above are *targets* and the floor measurement (this evidence set, on
Intel UHD) is the only one we have.

## Why this is a per-piece document

The doctrine says "State per tier what was added, not only what was
removed." That phrasing assumes the ladder is a per-piece property,
not a global one — a `neonspacewar` ladder would add things like
"more vector sprites" or "longer phosphor decay trails", not more disk
harmonics. This document is the blackhole-specific version. Sibling
pieces will need their own.

## Provenance

- The "what the piece currently does" section reflects
  `vendor/blackhole/blackhole.frag` and `src/gles3_blackhole.c` at
  HEAD `341af1e` (post-tune `52b2a2f`).
- The family doctrine this responds to is
  `docs/superpowers/specs/2026-09-25-screensaver-family-doctrine.md`
  § "The catalogue's purpose" § "People should be able to SHOW OFF
  their systems with this" (operator, 2026-09-26).
- The reference-tier Intel UHD measurements are in
  `docs/superpowers/rounds/2026-09-26-blackhole-per-launch-captures.md`
  (this commit series).
- The CIX Sky1 Mali-G720 row in the tier table is a placeholder until
  that hardware is measured. The doctrine is explicit that this must
  not be assumed.
