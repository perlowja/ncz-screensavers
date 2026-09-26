# magmasimplex round-20 — capture evidence report (MUST READ BEFORE COMMITTING)

**Date:** 2026-09-26 04:15 -0400
**Host:** MEDUSA (192.168.207.84), user `medusa`/`medusa`
**Binary:** `/home/medusa/ncz-screensavers/build/magmasimplex_gles3`,
built 2026-09-26 03:05 from the round-20 fluid-sim sources
(`gles3_magmasimplex.c` and `vendor/magmasimplex/sim3d_*.frag`).
MD5 of `src/gles3_magmasimplex.c` on MEDUSA matches master HEAD byte-for-byte:
`16113199c67aa33ca58b9a17f7aeaa54`. Round-20 is committed and built cleanly.
**Capture mechanism:** `NCZ_FRAME_DUMP=/tmp/m260-...` via the harness
`report_framebuffer()` path. Captures every 60 frames = 1 fps at the 60 fps
vsync the harness reports on MEDUSA's AMD Navi14.

**Tree state:** local repo clean. MEDUSA's working tree has uncommitted
scratch from earlier round-19 → round-20 work; simulation source files
match master byte-for-byte.

---

## TL;DR

**The captured frames do not show fluid phenomena. They show the
matte-plastic failure mode.** The simulation IS running (per-frame
hashes differ; dt_ms=16.06 stable 60 fps vsync), but the rendered
output is a smooth radial gradient with zero sharp features, on every
seed, every quality tier, every duration tested. The phase-field
topology that the round-20 brief was for (necking, coalescence,
pinch-off) is not visible on MEDUSA's 3D-voxel tier.

This matches the operator's standing warning exactly:
*"this piece passed a metrics gate once while looking like matte
plastic."* Coverage is 100%, chroma is 78–80%, frame rate is 60 fps
vsync — the metrics gate would say the build is healthy. The visual
check says it isn't.

The brief asked for:
1. a merge showing visible NECKING between two masses;
2. a separation where the neck PINCHES OFF.

Neither is present in any captured frame. Per the operator's standing
rule ("Look at the images before claiming anything"), these captures
are committed as **honest evidence of the failure mode**, not as
celebration of success. The user must decide whether to allow a
simulation-code change to fix this, or to accept that MEDUSA's 3D
tier cannot produce the brief's deliverables.

---

## What was captured

Five runs total. Each committed as a subdirectory. PNG previews only;
raw `.rgba` files were used to generate the PNGs via
`/home/jasonperlow/build-tmp/rgba2png.py` (PIL Image.frombytes
RGBA→PNG). The harness's `report_framebuffer()` writes the raw bytes
with a `.png` suffix but they are NOT real PNGs (confirmed with
`file(1)`), so the captures had to be regenerated.

| Run | Seed | Quality | Duration | Surface | Notes |
|---|---|---|---|---|---|
| `neck-25-probe/` | 25 | default (256² grid, 512² atlas, 64 slices) | 12 s | 1280×720 | ultraviolet colourway; the brief's reference seed |
| `neck-25-probe-low/` | 25 | `NCZ_MAGMASIMPLEX_QUALITY=low` (192² grid, 336² atlas) | 12 s | 1280×720 | same seed, lower-res voxel atlas to test if that helps |
| `neck-25-long/` | 25 | default | 60 s | 1280×720 | long run to see if phase field eventually separates |
| `seed-sweep/seed-60/` | 60 | default | 12 s | 1280×720 | neon colourway with rainbow=1 |
| (deleted, evidence only) | 7, 99, 12345 | default | 12 s each | 1280×720 | seed sweep; varied `surface_tension` from 0.00124 to 0.00351. All identical matte plastic. Not committed. |

All runs reached vsync 60 fps with `dt_ms=16.056–16.060` throughout.
All runs initialised the round-20 simulation cleanly (6 blobs seeded
with phase>0 in `init_seed`), all runs logged `[diag] magmasimplex
tier=3d-voxel` (MEDUSA's navi14 GPU is non-Intel so the binary
selects the 3D tier, not the 2D tier; `NCZ_MAGMASIMPLEX_2D=1` is
ignored on non-Intel hardware per `src/gles3_magmasimplex.c:724-731`).

---

## Per-frame metrics (measured against the committed PNG previews)

| Frame | coverage | chroma | edge10 | edge30 | edge50 |
|---|---|---|---|---|---|
| `neck-25-probe/t03s.png` | 100.0% | 77.9% | 0.00% | 0.00% | 0.00% |
| `neck-25-probe/t12s.png` | 100.0% | 80.1% | 0.00% | 0.00% | 0.00% |
| `neck-25-probe-low/t03s.png` | 100.0% | 79.3% | 0.00% | 0.00% | 0.00% |
| `neck-25-probe-low/t12s.png` | 100.0% | 77.9% | 0.00% | 0.00% | 0.00% |
| `neck-25-long/t00s.png` | 100.0% | 80.2% | 0.00% | 0.00% | 0.00% |
| `neck-25-long/t58s.png` | 100.0% | 77.9% | 0.00% | 0.00% | 0.00% |
| `neck-25-long/t62s.png` | 100.0% | 80.2% | 0.00% | 0.00% | 0.00% |
| `seed-sweep/seed-60/t12s.png` | 100.0% | 63.5% | 0.00% | 0.00% | 0.00% |

Plus, from the seed-sweep raw `.rgba` files (deleted from the
committed tree but still on MEDUSA's `/tmp/m260-seed-{7,99,12345}/`):

| Seed | surface_tension | edge10 |
|---|---|---|
| 7 | 0.00132 | 0.00% |
| 99 | 0.00296 | 0.00% |
| 12345 | 0.00351 | 0.00% |

**Edge definitions:** `edgeN_pct = % of adjacent pixel pairs (x or y)
with max channel difference > N`. **0.00% at N=10 means NO pixel pair
differs by more than 10** — the image is a perfectly smooth gradient.
A real lava-lamp capture with visible blobs would be >50% at N=10
and >5% at N=50.

`chroma_pct` = pixels where `max(rgb) - min(rgb) > 60`. 77–80% is
"many colours" but on a smooth radial gradient that's just the natural
red→yellow→green sweep from the liquid to the hot emitter at the
bottom-centre, not distinct hue regions. The 63.5% chroma on the
seed=60 capture is the lowest of the bunch, consistent with the neon
colourway's deeper magenta backdrop having less spread to the yellow
emitter.

The frame hash DOES change frame-to-frame (different hashes in the
run.log), so the simulation is computing — but the rendered output
is a featureless radial gradient in every frame.

---

## What was NOT done (per the operator's brief)

- Did not modify `src/gles3_magmasimplex.c`.
- Did not modify any `vendor/magmasimplex/*.frag`.
- Did not modify `meson.build`.
- Captures are honest evidence of the failure mode; they are NOT
  presented as a celebration of success.

---

## Suspected cause (read-only diagnosis, no code changes)

The previous session's `REPORT-matte-plastic.md` (in
`~/build-tmp/captures-r20-fm/`) raised three hypotheses. The
captures here confirm two of them and rule out a third:

1. **The 3D voxel tier is producing a featureless render at MEDUSA's
   resolution.** Confirmed across 5 different seed/quality/duration
   combinations. The 3D phase field is not developing visible
   structure (masses, necks, pinches) within the time horizons
   tested (≤ 60 s). The 64³ voxel atlas at default tier, or 48³ at
   low tier, is producing a smooth render.

2. **The 2D tier is unreachable on MEDUSA.** Confirmed by reading
   `src/gles3_magmasimplex.c:724-731`. The 2D path requires
   `detect_intel_uhd() == true`, which checks for "Intel" AND "UHD"
   in `glGetString(GL_RENDERER)`. MEDUSA's navi14 returns
   `RENDERER=AMD Radeon Graphics (radeonsi, navi14, ACO, DRM 3.64,
   7.2.7-2-t2-trixie)` so the 2D tier is unreachable here. Even
   setting `NCZ_MAGMASIMPLEX_2D=1` does not switch tiers on non-Intel
   hardware — the logic falls through to the else branch which picks
   `TIER_HIGH_3D` for non-Intel.

3. **Seeds don't help.** The seed sweep at default tier (seeds 7, 25,
   60, 99, 12345) all produced `edge10=0.00%` matte plastic. Surface
   tension was varied from 0.00124 (seed=60) to 0.00351 (seed=12345)
   across the sweep — high surface tension did not produce visible
   structure either. This is consistent with the 3D-tier hypothesis:
   no matter how the phase field evolves, the render does not show
   it.

The user's prior standing warning applies: the matte-plastic failure
mode passes the metrics gate (100% coverage, ~78% chroma, 60 fps
vsync) but is not visually fluid. Per the user's standing rule
("Look at the images before claiming anything"), these captures are
committed honestly with this report.

---

## What the operator should consider next

1. **Allow a simulation-code change to fix the 3D tier render.**
   The 32-step raymarch through the 64³ atlas may be averaging out
   the phase-field structure — either the step count is too low to
   catch fine surface detail, the atlas layout has a problem, or the
   Beer-Lambert accumulation is over-smoothing. Without touching the
   code, this cannot be diagnosed further.

2. **Try the 2D tier on a host that exposes Intel UHD.** PEGASUS is
   "in use" per the brief; the previous report noted that the 2D
   path can be exercised on PEGASUS. The 2D tier is a 256² 2D field,
   not the 64³ voxel atlas; it is much cheaper and the phase field
   should be more directly readable.

3. **Acknowledge the matte-plastic mode is the entire visible output
   for this build.** The brief was for necking + pinch-off; the
   current build on MEDUSA cannot deliver those frames. Either the
   simulation needs more work, or the round-20 brief is incompatible
   with the 3D-voxel-only tier on AMD.

---

## Artifacts on disk (these are committed)

```
docs/superpowers/rounds/2026-09-26-magmasimplex-fluid-evidence/
├── neck-25-probe/
│   ├── t03s.png   (frame 180 = t≈3s, seed=25, default tier, 12s run)
│   ├── t12s.png   (frame 720 = t≈12s, end of run)
│   └── run.log    (full stderr from binary)
├── neck-25-probe-low/
│   ├── t03s.png   (seed=25, NCZ_MAGMASIMPLEX_QUALITY=low)
│   ├── t12s.png
│   └── run.log
├── neck-25-long/
│   ├── t00s.png   (frame 4 = t≈67ms)
│   ├── t58s.png   (frame 3600 = t≈58s)
│   ├── t62s.png   (frame 3720 = t≈62s, end of 60s run)
│   └── run.log
└── seed-sweep/
    └── seed-60/
        ├── t12s.png  (seed=60, neon colourway, rainbow=1)
        └── run.log
```

The host's `/tmp/m260-*` capture dirs are kept on MEDUSA until the
next capture run overwrites them.

---

## Co-author / provenance

Performed by the current agent session. Co-author trailer and Claude
session URL are appended to commits per the operator's standing rule.
The captures and this report are committed as evidence of the current
failure mode, NOT as celebration of success.
---

# The remaining problem is OPTICS, not simulation (2026-09-26)

Three rounds have now attacked magmasimplex's "looks like matte plastic" verdict from the
motion side, and the verdict has survived all three:

1. Kinematic metaballs -> flagged as plastic.
2. Real 2D+3D fluid simulation with buoyancy, surface tension and a phase field
   (`20478f9`) -> still flagged as plastic.
3. Volumetric voxel tier -> the round's own evidence is titled *"honest evidence that
   round-20 3D-voxel tier renders matte plastic on MEDUSA"*
   (`docs/superpowers/rounds/2026-09-26-magmasimplex-fluid-evidence/REPORT.md`).

**That is three different motion models producing the same material failure. The motion
was never the problem.**

## External input, filtered

The operator relayed general voxel-fluid guidance. Most of it targets a problem this piece
does not have, and is recorded here as explicitly NOT applicable so nobody re-raises it:

- **Marching cubes, micro-voxels, layered depth slabs, particle hybrids over block
  placements** — all fix VISIBLE BLOCKINESS. This piece raymarches a continuous phase
  field; there are no cubes on screen. Not our failure.

What does apply, and matches the standing hypothesis:

- **Refraction / index of refraction (~1.1-1.33) — the biggest identified gap.** The
  implementation has Beer-Lambert ABSORPTION (light dimming with path length) and no
  REFRACTION. Real translucent material BENDS what is behind it. The eye reads bent
  background as "transparent substance" and unbent background as "coloured solid". This is
  very likely the strongest single reason the wax reads as plastic, and it has never been
  implemented.
- **Thickness-driven emission plus internal scattering.** Thin parts should be hot and
  bright, thick cores deep and saturated. If that relationship is absent or inverted, no
  amount of simulation fidelity rescues it. Verify which way round it currently is before
  changing anything else.
- **Bright rims at edges and contact points** ("foam at the crests", generalised): a
  specular/edge highlight where the surface turns away from the viewer is much of what
  makes a surface read as wet rather than matte.

## Direction for the next round

**Do not add another motion model.** Do not increase grid resolution. Do not replace the
simulation. The fluid simulation landed and works; leave it alone.

Change only how light enters, travels through and leaves the material:

1. Add refraction — offset the background sample by the surface normal scaled by an IOR
   term. Cheap, and probably the highest-value single change available.
2. Audit the thickness -> emission and thickness -> absorption relationships and confirm
   thin edges are hotter and thick cores deeper. Fix if inverted.
3. Add an edge/rim term driven by the angle between the view ray and the surface normal.
4. Only then revisit palette.

Measure by comparison, not in isolation: same seed, same frame, before and after each
change, so it is visible which optical term did the work.

This entry exists because the piece has now absorbed three rounds of effort aimed at the
wrong layer. A fourth motion round would be the fourth wrong answer.

## Refinement + a correction to the IOR figure above (2026-09-26)

### CORRECTION: IOR 1.45-1.60, not 1.1-1.33

The range recorded above is **water** (1.33) and is wrong for this material. Magma, wax
and obsidian sit at roughly **1.45-1.60**. A too-low IOR under-bends the background and
produces a weaker version of the same plastic reading, so implementing against the earlier
number would have partially wasted the round.

### The constraint the external advice does not know about

Standard screen-space refraction assumes a **background colour/depth buffer** of a scene
behind the transparent object. **We do not have one.** `magmasimplex` is a fullscreen
procedural effect: the liquid medium is generated in the same shader, and there is no
separate scene behind the wax.

So do NOT implement a screen-space buffer lookup. **Re-evaluate the liquid/medium function
along the refracted ray** inside the existing raymarch. This is cheaper than screen-space
and strictly better here: no silhouette edge artifacts and no missing-information problem
where the refracted ray leaves the screen, because the field is defined everywhere.

### The three optical terms, concretely

**1. Refraction.** At the phase-field boundary, refract the view ray through the surface
normal using Snell's law at IOR ~1.5, then continue sampling the medium along that bent
ray rather than the straight one. Background distortion is the single strongest cue that
separates "transparent substance" from "coloured solid".

**2. Thickness-driven scattering and emission.** Absorption alone only dims. Add:
   - a cheap subsurface approximation — measure distance to the exit boundary along the
     ray; where that distance is SHORT, let light bleed through aggressively and shift
     toward a brighter, more saturated tone. Thin crust edges glow hot orange-red; thick
     cores absorb down to deep crimson.
   - emission tied **inversely to optical depth**, or directly to the local phase-field
     density **gradient**. This is sharper than "thin edges are hot": it makes fast
     shearing edges intensely hot while massive stagnant cores stay cooler and crust-like,
     which is what real magma does.

**3. Fresnel rim.** The "bright edges" term is the Fresnel effect. Use Schlick:

    R(theta) = R0 + (1 - R0) * pow(1 - dot(N, V), 5.0)

A matte surface reflects uniformly; a wet or fluid one becomes near-mirror at grazing
angles. This puts crisp bright rims on the fluid contours and separates the material from
its surroundings even where the internal absorption colours are identical. This term alone
does a lot of the "wet, not matte" work.

### Order of implementation, and how to measure

Refraction first (biggest single cue), then thickness-driven scattering/emission, then
Fresnel. **Same seed, same frame, capture before and after EACH term** so it is visible
which one did the work. Do not land all three and declare victory — this piece has already
absorbed three rounds whose contribution could not be separated afterwards.

### Environment, for anyone writing against this

Pure **GLSL ES 3.00** plus C, on a custom Wayland/EGL harness. No engine, no Unity, no
Unreal, no post-processing stack, and **no background scene buffer** (see above).

---

## A working reference implementation is already in this repo: `prococean`

Operator, 2026-09-26, watching the shadertoy set on MEDUSA: *"several are highly fluidic...
might want to look at those algorithms for inspiration for the magmasimplex."*

This is the strongest evidence yet for the optics-not-simulation conclusion, and it is
empirical rather than argued:

**`vendor/xshadertoy/glsl/prococean.glsl` is 224 lines with NO fluid simulation — no
Navier-Stokes, no phase field, no buoyancy, no surface tension — and it reads as more
convincingly fluid than magmasimplex does WITH all of that.** Several others in the set
(`driftclouds`, `fluxcore`, `noxfire`, `hexplasma`) land the same way.

Whatever makes something look like a liquid, it is not the simulation.

### What prococean actually does — copy this structure

Its entire material model is four lines at the end of `mainImage`:

```glsl
// Schlick Fresnel, R0 = 0.04
float fresnel = (0.04 + (1.0-0.04)*(pow(1.0 - max(0.0, dot(-N, ray)), 5.0)));

vec3 R = normalize(reflect(ray, N));
vec3 reflection = getAtmosphere(R) + getSun(R);

// thickness/depth-driven scattering
vec3 scattering = vec3(0.0293, 0.0698, 0.1717) * 0.1
                * (0.2 + (waterHitPos.y + WATER_DEPTH) / WATER_DEPTH);

vec3 C = fresnel * reflection + scattering;
fragColor = vec4(aces_tonemap(C * 2.0), 1.0);
```

Five techniques, in order of how much they matter here:

1. **ACES tonemapping on the way out.** `aces_tonemap()` — a filmic curve applied to the
   final colour. **This was missed in the earlier optics analysis and may be a large part
   of the problem.** Writing linear colour straight to the framebuffer looks flat and
   waxy regardless of how good the shading underneath is. Check whether magmasimplex
   tonemaps at all; if it does not, add this first, because it is the cheapest change on
   the list and affects every pixel.
2. **Schlick Fresnel with R0=0.04**, exactly the formula specified earlier. Note it is
   used as a WEIGHT between reflection and scattering, not added on top.
3. **`fresnel * reflection + scattering`** — the whole material in one expression.
   Grazing angles go reflective, face-on goes to the scattering colour. That single
   relationship is most of what the eye reads as "wet".
4. **Scattering scaled by depth**: `(y + WATER_DEPTH)/WATER_DEPTH`. Thickness-driven,
   exactly as specified, and trivially cheap.
5. **Normals from finite differences of the height field**, then **smoothed with
   distance** (`mix(N, up, 0.8*min(1.0, sqrt(dist*0.01)*1.1))`) to keep high-frequency
   noise from reading as sparkle. magmasimplex has a phase field to take normals from and
   should do the same smoothing.

### The instruction this produces

Do not port prococean. **Port its ENDING.** Keep the existing fluid simulation — it works
and it produces good motion — and replace the final shading with this structure:
normals from the phase field, Schlick Fresnel as a weight, depth-driven scattering,
refraction along the bent ray (per the earlier note), and ACES tonemapping last.

Measure it the same way: same seed, same frame, before and after each term, so it is
visible which one did the work. If ACES alone moves the piece substantially, that is worth
knowing before any other change is made.

`prococean` is in-tree, MIT-compatible with the rest of the vendored Shadertoy set, and
already known to compile and run on our harness — so it can be read, diffed against and
tested directly rather than treated as an external reference.
