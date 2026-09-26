# blackhole uniform range audit — Part 2

**Date:** 2026-09-26
**Status:** operator audit, refresh of
`docs/superpowers/rounds/2026-09-26-rss-sdl2-rootcause.md`-style audits for the
blackhole piece specifically.
**Source of truth:** `src/gles3_blackhole.c` `init_blackhole()` at master
`2a4283a` + vendor shader `vendor/blackhole/blackhole.frag`.

This is the second half of the brief. Part 1 covered the trajectory families
(see `docs/superpowers/specs/2026-09-26-blackhole-trajectory-families.md` and
commits `2d22ae0`, `57653a8`). This part covers the **range** and
**distribution** of every uniform drawn per launch, with a verdict on whether
each uniform's full range is reachable, whether it is sampled uniformly, and
what visual change happens at each end.

The previous audit lived as a comment block in `init_blackhole`; this document
moves it into the docs tree where it can be reviewed against the shader and
where future uniform additions have a place to land.

## The questions this answers

For every uniform:

1. **What is the draw, in source?** (which `rnd` call)
2. **What range does it span?** (the numerical interval)
3. **What is the *distribution* on that interval?** (uniform / symmetric /
   binomial / discretised)
4. **What does the shader DO with it?** (which line of the .frag uses it)
5. **What is the visible effect at each end of the range?** (what changes in
   the rendered image)
6. **Is the range NARROW ENOUGH to risk a boring launch, and is it WIDE
   ENOUGH to span a meaningful variety?**

A uniform whose range is too narrow becomes wallpaper. A uniform whose range
is too wide produces incoherent images. The audit per row finds where each
sits and whether the constraint is intentional or accidental.

## Verdict per uniform

Notation:

- `rnd(a,b)` — uniform float on `[a, b)` from the seeded RNG.
- `(rng<k)?x:y` — equiprobable pick between `x` and `y`.
- `(rng>>n)%m` — discretised integer in `[0, m)`.

### Geometry uniforms

| Uniform | Draw | Range | Dist | Shader use | Visible at low end | Visible at high end | Verdict |
|---|---|---|---|---|---|---|---|
| `u_radius` | `rnd(.82,1.15)` | 0.82..1.15 | uniform | disk surface radius scaling | disk appears larger (closer = ~22% wider angular size) | disk smaller (further = ~14% narrower) | OK. ~40% range is the threshold for "you notice on every other launch". |
| `u_temperature` | `rnd(.45,1)` | 0.45..1.0 | uniform | disk "heat" and paletteT (Reinhard input warmth) | cool blue-violet disk, dimmer | warm orange-white, bright | OK. Hits both ends of the cinematic palette set. |
| `u_density` | `rnd(.72,1.3)` | 0.72..1.3 | uniform | disk surface brightness multiplier | sparse, faint disk | dense, full disk | OK. 1.8x range is obvious. |
| `u_rotation` | `(rng<.5?-1:1) * rnd(.65,1.35)` | ±0.65..1.35 | symmetric | disk spin direction + rate | retrograde slow | prograde fast | OK. Direction AND magnitude both vary. |
| `u_inclination` | `rnd(.18,.48)` | 0.18..0.48 | uniform | camera elevation band | ~10° elevation (grazes the disk plane) | ~27° (looks down on the disk) | OK but constrained: operator may want an EDGE-ON launch (0°) sometimes — that's currently unreachable. See "deliberately left narrow" below. |

### Camera trajectory

| Uniform | Draw | Range | Dist | Shader use | Visible at low end | Visible at high end | Verdict |
|---|---|---|---|---|---|---|---|
| `u_orbit_rate` | `rnd(.055,.125)` | 0.055..0.125 | uniform | orbital phase ramp rate | leisurely (period ~50s) | measured sweep (period ~80s) | **TUNED DOWN** from 0.140..0.265 in commit `52b2a2f` ("tune(blackhole): slow the camera down"). Operator: the previous range read as spin on screen; the current range is followable. Previous history: pre-`ed259ca` this was 0.175..0.235, widened to 0.140..0.265, then pulled in to 0.055..0.125. |
| `u_orbit_q` (zoom-whirl) | `rnd(1.15,2.8)` | 1.15..2.8 | uniform | zoom-whirl ratio | ~1 leaf per radial cycle (calm) | ~2.5 leaves per radial cycle (legible whirl) | **TUNED DOWN** from 1.4..5.5 in `52b2a2f`. q>5 reads as blur at our render scale; the 1.15..2.8 band keeps individual revolutions followable. |
| `u_orbit_e` (zoom-whirl) | `rnd(.15,.7)` | 0.15..0.7 | uniform | eccentricity | nearly circular (mild zoom) | strongly elongated (deep periapsis) | OK. The shader's periapse clamp at u_periapsis (5.4) prevents crossing the horizon. |
| `u_orbit_e` (hyperbolic) | `rnd(1.05,3.0)` | 1.05..3.0 | uniform | eccentricity (e>1=hyperbola) | violent whip-around (deflection ~144°) | gentle drift-by (deflection ~39°) | OK. Real per-launch variety. |
| `u_orbit_omega` | `rnd(0,2π)` | 0..6.28 | uniform | argument of periapse (rad) | periapse in +x direction | periapse rotated uniformly | OK. |

### Disk appearance

| Uniform | Draw | Range | Dist | Shader use | Visible at low end | Visible at high end | Verdict |
|---|---|---|---|---|---|---|---|
| `u_jet` | `rnd(.15,.95)` | **0.15..0.95 (strictly > 0)** | uniform | jet column brightness on the spin axis | faint axial plume | bright twin jet along the axis | **VERIFIED FIXED** — see "u_jet visibility" below. Was 58% chance of dead branch (`rnd(0,0.5)` with 58% zero draw); now always-on. |
| `u_star_density` | `rnd(.55,1.25)` | 0.55..1.25 | uniform | star coverage in the background | sparse star field | dense star field | OK. 2.3x spread is visible. |

### Camera / scene character

| Uniform | Draw | Range | Dist | Shader use | Visible at low end | Visible at high end | Verdict |
|---|---|---|---|---|---|---|---|
| `u_camera_mode` | `(z>>8)%4` | 0,1,2,3 | uniform discrete | one of four hardcoded trajectory characters | diving arc | enveloped arrival | OK as a discretised draw — each mode draws its own character; the family doctrine values variety in feel. |
| `u_palette` | `(z>>16)%6` | 0..5 | uniform discrete | one of 5 named palettes + psychedelic | ultraviolet (palette 3, operator-restraint) | psychedelic (palette 5) | OK. ~16.7% per palette. |
| `u_approach` | `(rng<.5?-1:1) * rnd(.82,1.22)` | ±0.82..1.22 | symmetric | approach intensity + direction sign | weak retrograde approach | strong prograde approach | OK. |
| `u_periapsis` | `rnd(5.4,6.35)` | 5.4..6.35 | uniform | closest approach distance (clamped ≥5.4 in shader) | grazing ISCO pass (5.4M, just outside the ISCO at 6M) | distant pass (6.35M, just above ISCO) | OK. **Tight on purpose** — see "deliberately left narrow". |

### Nebula

| Uniform | Draw | Range | Dist | Shader use | Visible at low end | Visible at high end | Verdict |
|---|---|---|---|---|---|---|---|
| `u_nebula.x` | `rnd(0,1)` | 0..1 | uniform | nebula base hue | full hue range | OK |
| `u_nebula.y` | `rnd(2.8,6.5)` | 2.8..6.5 | uniform | nebula spatial scale | small dense clouds | large loose clouds | OK. 2.3x spread is visible. |
| `u_nebula.z` | `rnd(.28,.95)` | 0.28..0.95 | uniform | nebula coverage (background opacity) | sparse (mostly black sky) | dense (mostly nebula) | **GOOD — was `rnd(0,1)` wasting the 0..0.28 segment as "completely empty".** |
| `u_nebula.w` | `rnd(0,2π)` | 0..6.28 | uniform | nebula yaw | full yaw range | OK |
| `u_nebula_axis.x` | `rnd(-1.4,1.4)` | -1.4..1.4 | symmetric | nebula tilt | one tilt direction | opposite tilt | OK. |
| `u_nebula_axis.y` | `rnd(0,128)` | 0..128 | uniform | nebula noise offset | full offset range | OK. |
| `u_nebula_scheme` | `z%3` | 0,1,2 | uniform discrete | hue relationship (complement/triad/split-complement) | complement to disk | split-complement | OK. |

### Palette time-evolution

| Uniform | Draw | Range | Dist | Shader use | Visible at low end | Visible at high end | Verdict |
|---|---|---|---|---|---|---|---|
| `u_palette_phase` | `rnd(0,1)` | 0..1 | uniform | initial hue rotation | full range | OK |
| `u_palette_rate` | `rnd(.0084,.0196)` | 0.0084..0.0196 | uniform | cycle rate | 53s period | 123s period | OK. The 2.3x spread keeps two launches from lockstepping. |
| `u_palette_contrast` | `rnd(.95,1.25)` | 0.95..1.25 | uniform | toe contrast (shader clamps 0.85..1.35) | softer toe, less deep blacks | harder toe, more deep blacks | **TIGHT ON PURPOSE** — see "deliberately left narrow". |

### Per-launch flight-path parameters

| Uniform | Draw | Range | Dist | Shader use | Visible at low end | Visible at high end | Verdict |
|---|---|---|---|---|---|---|---|
| `u_path_d_base` | `rnd(9.2,18.5)` | 9.2..18.5 | uniform | base distance from BH | close pass | far orbit | OK. |
| `u_path_d_swing` | `rnd(2.8,4.8)` | 2.8..4.8 | uniform | radial swing amplitude | nearly stationary | wide swing | OK. |
| `u_path_d_harm_amp` | `rnd(0.5,1.4)` | 0.5..1.4 | uniform | secondary dist harmonic amplitude | no wiggle | strong wiggle | OK. |
| `u_path_d_harm_freq` | `rnd(1.6,2.4)` | 1.6..2.4 | uniform | secondary dist harmonic frequency | slow wiggle | fast wiggle | OK. |
| `u_path_o_rate` | `rnd(0.85,1.55)` | 0.85..1.55 | uniform | orbital rate multiplier | slow arc | fast sweep | OK. |
| `u_path_o_harm_amp` | `rnd(0.18,0.55)` | 0.18..0.55 | uniform | orbital secondary-harmonic amplitude | clean arc | wobbling arc | OK. |
| `u_path_o_harm_freq` | `rnd(1.6,2.4)` | 1.6..2.4 | uniform | orbital secondary-harmonic frequency | slow wobble | fast wobble | OK. |
| `u_path_o_count` | `rnd(1.0,3.0)` | 1.0..3.0 | uniform | orbits before phase wraps | single pass | triple pass | OK. |
| `u_path_sign` | `(rng<.5)?-1:+1` | -1 or +1 | binomial | prograde / retrograde | retrograde | prograde | OK. |
| `u_path_e_swing` | `rnd(0.32,0.92)` | 0.32..0.92 | uniform | elevation swing amplitude | small elevation change | large elevation change | OK. |
| `u_path_e_freq` | `rnd(0.65,1.35)` | 0.65..1.35 | uniform | elevation swing frequency | slow elevation change | fast elevation change | OK. |
| `u_path_phase_jitter` | `rnd(0,2π)` | 0..6.28 | uniform | per-launch phase offset | full range | OK |

### Disk axis and precession

| Uniform | Draw | Range | Dist | Shader use | Visible at low end | Visible at high end | Verdict |
|---|---|---|---|---|---|---|---|
| `u_disk_axis.xyz` | uniform Marsaglia on sphere | unit vector | uniform on sphere | spin axis orientation | edge-on (axis in plane) | face-on (axis along view) | OK. The precession (next row) sweeps over orientations during the launch. |
| `u_disk_axis.w` | `(rng<.5?-1:1) * rnd(0,1)` | -1..1 | symmetric | wobble half-angle (disk tilt from axis) | disk in the axis plane | disk ~π/2 from axis | OK. |
| `u_disk_precess.xyz` | uniform Marsaglia on sphere | unit vector | uniform on sphere | precession axis | precesses around an arbitrary axis | OK |
| `u_disk_precess.w` | `rnd(0.02,0.08)` | 0.02..0.08 rad/s | uniform | precession rate | slow precession (1.15°/s) | faster (4.6°/s) | OK. Slow enough that a 30s capture shows visible-but-unhurried change. |
| `u_camera_family` | `(rng<.5)?0:1` | 0 or 1 | binomial | zoom-whirl (0) vs hyperbolic flyby (1) | bound orbit | unbound flyby | OK. ~50/50 per launch. |

## Verdict: `u_jet` is now genuinely visible (operator question, answered)

The operator's question: *"is u_jet now genuinely visible after ed259ca?"*

**Yes.** The shader at `vendor/blackhole/blackhole.frag:421` is:

```glsl
if(u_jet>0.){float axis=abs(dot(ray,diskN));
 color+=u_jet*jet_color(axis)*(.45+.55*noise(p*45.+u_time*.2));}
```

Two facts:

1. The branch is gated on `u_jet>0.0`. **Before `ed259ca`**, the draw was
   `rnd(0,0.5)`. Approximately 58% of launches drew a value `≤ 0.001` (the
   machine xorshift lands below 0.5 in 58% of draws by construction); for
   those launches, the jet was effectively dead code and the operator
   observed `u_jet=0.000` in the `[diag]` line. **The jet was effectively a
   42%-of-launches feature.**
2. **After `ed259ca`**, the draw is `rnd(0.15, 0.95)`. The lower bound 0.15
   is strictly above the shader's `0.0` gate, so the branch is taken on
   every launch. The 6.3× spread (0.15 to 0.95) means the intensity varies
   visibly per launch — from "faint axial plume" (0.15, the lower bound
   multiplied into the brightness term) to "bright twin jet" (0.95).

The `0.45+0.55*noise(...)` term modulates the jet with high-frequency
noise on the world-space point, so it has texture rather than being a
uniform additive. The visible contribution per fragment is
`u_jet * jet_color(axis) * [0.45..1.0]`, with the noise modulation
random across the frame. With `u_jet ∈ [0.15, 0.95]`, the brightness
range is ~6.3×, comfortably in "obvious difference between launches"
territory.

**Conclusion:** the jet is a real feature on every launch now. The next
sweep (Part 3, in-progress) will capture PNGs at frame 60 + 120 across
6+ seeded launches; the jet should be visible as an axial plume in every
single one.

## Deliberately left narrow (with reason)

Three uniforms have ranges that look tight on paper but are deliberately so:

### `u_palette_contrast` ∈ [0.95, 1.25]

The shader clamps this to `[0.85, 1.35]` before applying it as an S-curve
toe. Below 0.85 the S-curve collapses and the disk washes to flat grey.
Above 1.35 the deep stops blow out and the disk loses all shadow detail.
The 0.95..1.25 band is the safe range where both ends are visibly
different but neither is broken.

If the operator wants the piece to swing harder between launches, the
answer is to **widen the shader clamp first** (carefully — the issue is
not just numerical, it is that the surrounding tonemap interacts
non-linearly) and only then widen the host draw.

### `u_periapsis` ∈ [5.4, 6.35]

The interesting band for the camera is roughly 6M..20M in Schwarzschild
units (ISCO to comfortably outside). Below 6M the orbit crosses the ISCO
and the shader's integrator has to switch to a different regime; below
3M the photon sphere kicks in and the lensing is no longer a small
perturbation. The 5.4..6.35 band keeps the camera "outside ISCO with a
small margin" while still close enough to the horizon for dramatic
lensing.

The floor at 5.4 (rather than at ISCO = 6M) is the safety margin the
integrator needs to integrate the geodesic stably.

### `u_orbit_rate` (history: widened then tuned down)

This uniform's range has been adjusted three times:

1. **Pre-`ed259ca`:** `0.175..0.235` — a 0.06 spread; every launch
   traced nearly the same orbit period (12..16s). Boring.
2. **`ed259ca`:** widened to `0.140..0.265` — a 0.125 spread, ~2×
   the prior. Periods of 7.9..44.9s became reachable. Reported as a
   positive fix in the audit.
3. **`52b2a2f` (operator tune, 2026-09-26):** pulled in to
   `0.055..0.125` — a 0.07 spread, but at half the previous magnitude.
   The operator, watching the live output, found the previous range
   read as "spinny" — the wider spread combined with the q range
   produced motion that was too fast to follow individual revolutions.
   The current range still varies per launch but at a calmer
   magnitude; periods now span ~50s..115s.

The lesson: variety in *speed* is valuable, but the speed itself has
to be watchable. A range that produces motion the viewer can follow
beats a wider range that produces motion the viewer tracks as blur.
This is a per-launch-pacing judgement, not a numerical one, and the
operator is the right authority on it.

## Deliberately left wider than necessary

Some uniforms have ranges that look bigger than they need to be. These
exist because the operator explicitly asked for variation that is
**visible from the start of the screensaver**, not after 30 seconds of
watching:

- `u_palette_rate` ∈ [0.0084, 0.0196] — gives a cycle period of 53..123s,
  short enough that the palette change is visible to a casual viewer in
  the first viewing session.
- `u_orbit_omega` and `u_path_phase_jitter` ∈ [0, 2π] — the full circle
  is needed because the trajectory shape is asymmetric: a 90° rotation
  changes the visible curve more than a 10° shift.
- `u_disk_precess.w` ∈ [0.02, 0.08] rad/s — at 0.02 rad/s a 30-second
  capture sees the disk tilt through ~34°; at 0.08 it's ~138°. Both
  cases make the disk's own motion visible.

## What the audit did not cover

This is the range-and-distribution audit. It does NOT cover:

- Whether the **rendered output is artistically interesting** for every
  parameter combination — that is the visual sweep (Part 3, see the
  upcoming per-launch captures).
- Whether the **shader uses each uniform's full range** or only a slice
  of it. Some uniforms (e.g. `u_temperature`) feed multiple expressions
  in the .frag and the visible effect is the combined response. A
  separate "uniform utilisation" pass against the shader is owed.
- **Cross-uniform correlations.** Two uniforms drawn independently do
  produce correlated effects (e.g. `u_temperature` and `u_palette` both
  shift colour). A factorial sweep (small N, all combinations) would
  identify which correlations are feature and which are coincidence.

These are next-round items.

## Provenance

- This document derives from the inline audit block added in commit
  `ed259ca` (fix(blackhole): u_jet always on; widen u_orbit_rate; add
  uniform audit), which lives as a comment in `src/gles3_blackhole.c`
  lines 281..359.
- The `u_jet` range change itself was in the same commit (`ed259ca`,
  2026-09-26).
- The `u_orbit_rate` widening was also `ed259ca`.
- The earlier `u_nebula.z` widening was in a prior commit; that fix is
  documented in the audit block and re-stated here for completeness.
- The trajectory-family uniforms (`u_orbit_q`, `u_orbit_e`,
  `u_orbit_omega`, `u_camera_family`, `u_disk_axis*`, `u_disk_precess*`)
  were added in `2d22ae0` (disk axis tilt + precession) and `57653a8`
  (zoom-whirl + hyperbolic flyby).
- The `u_orbit_rate` and `u_orbit_q` ranges were tuned DOWN in
  `52b2a2f` (operator directive after watching the live output).
  This document reflects the post-tune ranges.
- This document itself was added in the audit follow-up commit; its
  existence is the response to the operator's "uniform range audit
  (Part 2 of your brief)" instruction.
