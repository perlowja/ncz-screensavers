# Afterimage — original shader pieces from classical algorithms

**Status:** design, 2026-09-26. Extends
`2026-09-25-catalogue-curation-plan.md` (its Top-12 rewrite shortlist stands);
does not replace it.

An afterimage is what persists on the retina once the source is gone. That is
the whole thesis: the *mathematics* of the classical screensaver canon is
worth keeping, and nothing else is. Not the code, not the names, not the 1997
rendering. Each piece here is an original work that happens to share an
ancestor with something older.

## The decision that triggered this

The rss-glx family is **cut**. All 13.

The engine defect that made four of them render wrongly was real and is fixed
(`cf59926` / `0e450e7`: `glColorMaterial` was a no-op stub, and display-list
replay used recorded colour snapshots instead of live GL1 state). That fix
stays — it affected every GL1-path hack, not just these. But fixing them only
ever bought the right to *judge* them, and on 2026-09-26 the operator judged
five on real hardware:

| target | verdict |
|---|---|
| skyrocket | good — and a fireworks particle system is precisely what a shader does better |
| euphoria | faded |
| flocks | boring (white dot swarm; separate `hsl2rgb(L=1.0,S=1.0)` degeneracy) |
| cyclone | measured 0.15% pixel coverage — near-black even after the fix |
| solarwinds | no objection, no enthusiasm |

One of five is worth anything, and that one is better rebuilt. Keeping the
other four would be grandfathering them for having compiled, which the
curation rubric explicitly forbids.

**Two rss concepts graduate** as original pieces, judged on their own merits:
fireworks (from skyrocket) and a volumetric vortex (from cyclone).

## Non-negotiable constraints

### Clean-room from the mathematics
Every piece is derived from **published mathematics**, never from another
project's source. De Jong and Clifford orbit equations, Draves' flame
variation functions, Markus-Lyapunov exponents, Apollonian curvature
relations, Reynolds' boids rules, DLA — all published, none copyrightable.
Implementations are licensed and attribution-bearing; ours must not be
derived from them.

Per piece, the design note records the **paper or formula** it came from.
If the only available description of an algorithm is somebody's C file, that
piece is deferred, not reimplemented.

### Our own names
No piece carries an upstream hack's name. A piece called `flame` invites the
comparison this whole document exists to avoid. Names follow the catalogue's
existing register — `ephemeris`, `leviathan`, `neonspacewar`.

### Randomisation is the deliverable
Per the family doctrine: these are art pieces with real randomisation, not
loops. The parameter space *is* the work. A piece whose every run looks the
same has failed its acceptance criteria regardless of how it looks once.

## Wave 1

Reconciled against the existing Top-12. Start order is deliberate: the first
piece builds machinery the next three reuse.

| working name | ancestor concept | source mathematics | why a shader wins |
|---|---|---|---|
| **emberflux** | fractal flame / IFS | Draves, "The Fractal Flame Algorithm" (variation functions, log-density display) | built for additive blending and HDR; visibly constrained by 1997 hardware. Density accumulation + tone mapping is reused by every attractor piece after it |
| **saltpetre** | fireworks particles | ballistic motion with quadratic drag; blackbody emission curve | HDR bloom, per-spark chromatic variation, randomised shell types and burst patterns per run |
| **maelstrom** | volumetric vortex | Rankine vortex velocity profile; curl noise advection | raymarched funnel with real density integration, vs a 0.15%-coverage particle wisp |
| **gasket** | Apollonian circle packing | Descartes circle theorem, inversive geometry | infinite depth per-pixel, curvature-driven palettes; unbounded zoom the original could not do |

Deferred to wave 2 pending wave 1 landing: DLA growth (`coral`), n-body
(`galaxy`), Munching Squares, harmonograph trails. Boids and n-body need real
per-agent state (ping-pong FBOs) and are a different effort class — they are
not blocked, just not first.

**Explicitly not doing:** escape-time fractals with orbit traps. The curation
plan already ruled this duplicated effort given 37 Shadertoy ports and 35
hyprsaver shaders already covering fractals, plasma and tunnels. Recorded here
because it was proposed again on 2026-09-26 and rejected again.

## Acceptance criteria — per piece

A piece ships only when all four hold:

1. **Visually judged by the operator on real hardware.** Not a thumbnail, not
   a pixel count. The rss family passed every automated check and still failed
   here; that is the whole lesson.
2. **Genuine per-run variation.** Two runs captured at the same frame index
   must differ materially — verified by hash, and by eye.
3. **Runs on the floor tier.** Must be watchable on an Intel iGPU, not only on
   discrete. A piece that needs an RTX to be tolerable gets an adaptive tier
   or does not ship.
4. **Provenance recorded.** The mathematics it derives from, cited, in its
   design note.

Plus the standing catalogue constraints: no sustained full-field luminance
flashing in the 3-30 Hz band (validated spatially as well as temporally), and
nothing legible on a lock screen.

## Dropping the legacy targets

"Drop" means removed from the build and the catalogue, not deleted from
history — git retains everything, and a cut is reversible by revert.

- `rss_sdl2_gles2_hacks` (13 targets) removed from `meson.build`.
- The corresponding `src/*_gles3.c` sources removed in the same commit.
- Legacy targets whose concepts are on the rewrite list are removed as their
  replacements land, EXCEPT the rss family, which goes now because it was
  judged and rejected on its own merits rather than superseded.

## Collaboration

Astra co-designs the pieces; this document is the brief. Division: Astra
takes the visual and mathematical design of each piece, Claude takes engine
integration, the randomisation harness, capture/validation and the acceptance
gate. Neither ships a piece the operator has not seen run.

---

# Operator curation calls — live session, 2026-09-26

Recorded as made. Measured columns from this session's own capture analysis
(Hasler-Susstrunk colourfulness C, mean saturation, 24-bin hue entropy,
coverage; box-counting fractal dimension, Sobel edge density, inter-sample
motion). `-` means not in our catalogue and never scored.

## KEEP AS-IS — no reimplementation

| target | measured | why |
|---|---|---|
| all `hyprsaver` (35) | - | whole family kept, operator directive |
| all `xshadertoy` (44) | - | whole family kept; includes `synthwavecity` |
| `cityflow` | C=107.0 sat=0.94 hue=0.83 cov=1.00 | vivid monochrome. Low hue entropy is a narrower hue distribution, NOT a defect -- operator and Astra independently made this correction |
| other edgy high-colour/low-hue | `geodesic` C=72.4 hue=1.01; `atlantis` C=58.0 hue=0.54; `hexstrut` C=64.8 hue=0.22; `gibson` C=67.2 hue=0.00 | same category: graphic, saturated, deliberately narrow palette |

## REIMPLEMENT AS SHADERS

| target | measured | the upgrade |
|---|---|---|
| `cubestorm` | shape=32.3 (highest in catalogue) fracD=1.81 edge=399 C=0.0 | most structurally complex target we have, and literally colourless. Pure form; shading and depth are the whole opportunity |
| `crackberg` | C=78.7 sat=0.42 hue=2.09 cov=1.00 | procedural fractal terrain, already fills the frame. Raymarched fBm with atmospheric scattering |
| `crumbler` | - | Voronoi fracture. GPU cell decomposition with real fragment dynamics |
| `covid19` | C=37.5 hue=1.99 fracD=1.56 motion=18.3 cov=0.13 | spiked virion; only 13% coverage. Raymarched with subsurface scattering. NOTE: must be renamed -- a dated, loaded reference, and our own-names rule applies. Generalise to a radiolarian/spiked-microorganism form |
| `skyrocket` (rss, cut) | recovered then judged good | fireworks: HDR bloom, per-spark chroma, randomised shell types |
| `cyclone` (rss, cut) | cov=0.0015 | volumetric vortex; Rankine profile + curl-noise advection |

## NEW — not in our catalogue, never ported

| concept | status | note |
|---|---|---|
| Celtic knotwork | strongest third-family candidate | plane-filling by construction, so it answers the coverage problem natively; passes a grayscale test by definition since structure IS the content; published construction (Mercat) so clean-room is straightforward. Risk: inert line art -- needs a real temporal contract |
| molecules + periodic table | operator request | see below; splits into two pieces |
| `cwaves` | probe, must earn admission | strict sinusoid superposition -- hard banding and beat nodes. Distinct in MECHANISM from our noise-based cohort (aurora, caustics, clouds, gridwave, marble, plasma, driftclouds, hexplasma) though adjacent in palette. Cut it if grayscale reveals only another soft gradient |

### Molecules and the periodic table — two pieces, not one

- **Molecules.** Ball-and-stick / space-filling from published coordinates.
  CPK colours and covalent radii are factual data, not copyrightable.
- **Elements.** A lone atom is a sphere and visually inert. The beautiful
  object is the ORBITAL -- hydrogenic wavefunctions are spherical harmonics
  times a radial term, giving the s/p/d/f lobe geometry. Real physics,
  genuinely mathematical, and directly adjacent to the surface-math family
  that measured strongest. This is the better periodic table piece.

Open question for the lock-screen rule: element symbols and atomic numbers are
text. The standing rule bars hostname/username/IP/numbers -- aimed at PRIVATE
information. Element labels disclose nothing, but the rule needs an explicit
carve-out rather than a silent exception.

## The geometric cluster folds into the meta-saver as ONE family

Operator direction, 2026-09-26: `cubetwist` "and basically all of the
geometrical ones" become a family inside the universal engine rather than
individual pieces.

The measurement supports this directly. The geometric cluster OWNS the
shape-complexity axis — it is the top of the ranking almost to the exclusion of
everything else:

| target | shape | fractal dim | edge density | colour |
|---|---|---|---|---|
| `cubestorm` | 32.30 | 1.812 | 399.1 | 0.00 |
| `topblock` | 25.99 | 1.843 | 284.3 | 2.92 |
| `lament` | 25.29 | 1.769 | 268.5 | 1.36 |
| `glknots` | 24.44 | 1.650 | 305.0 | 0.00 |
| `hexstrut` | 21.98 | 1.447 | 269.3 | 3.01 |
| `moebiusgears` | 20.29 | 1.599 | 207.6 | 2.30 |

These are not six ideas. They are one idea — an articulated polyhedral lattice
— under six different joint, subdivision and motion rules. They were separate
programs because 1997 had no way to express "vary the rule" other than writing
another program.

As a family director this becomes: a lattice/polyhedron generator, a joint rule
(hinge, twist, shear, nest, unfold), a subdivision depth, a motion contract,
and a material. `cubestorm`, `cubetwist`, `cubicgrid`, `cubestack`, `topblock`,
`hexstrut`, `geodesic`, `lament`, `glknots` and `moebiusgears` all fall out as
points in that space, and so do configurations none of them reached.

This also resolves the existing curation plan's own objection — "five cube
variations ... doing the best one properly beats reimplementing all of them."
A parametric family does the best one properly AND keeps the variation.

Note the colour column: this family is where the catalogue's structural
sophistication lives, and it is almost entirely uncoloured. Material and
lighting are the whole opportunity, not palette cycling.

### Revised launch set — three families

1. **Parameterized surfaces and 4D projections** (Astra: launch).
2. **Articulated polyhedral lattices** (operator: fold the geometric cluster
   in). Empirically our strongest cluster on shape complexity.
3. **Iterated-orbit density** (Astra: launch conditionally — must show
   branching, ribbons and voids absent from the existing 79 shader pieces).

Earning admission behind those: Celtic knotwork, Apollonian packings, `cwaves`.
Deferred: Lyapunov, Rankine vortex, DLA, boids, n-body.

## Era filter — 2000-2006 is the reimplementation pool

Operator direction, 2026-09-26: everything from the early-to-mid 2000s in the
upstream collection is game to reimplement.

Built from the first copyright year in each target's own source (an era proxy,
not an exact date — several carry ranges such as 2003-2019 and the minimum was
taken). Excludes hyprsaver, xshadertoy and the transition targets.

| era | count | notable |
|---|---|---|
| 1985-1999 | 7 | atlantis, lament, stonerview, juggler3d, jigsaw, cube21, starwars |
| **2000-2006** | **31** | klein, hypertorus, spheremonics, cubestorm, cubenetic, glknots, crackberg, molecule, menger, noof, glschool, polyhedra-gl, dangerball, flurry, blocktube, glblur, glsnake |
| 2007-2014 | 23 | cityflow, voronoi, projectiveplane, romanboy, quasicrystal, hypnowheel, lockward, geodesic, hilbert, moebiusgears |
| 2015+ | 31 | covid19, crumbler, cubetwist, cubestack, hexstrut, gravitywell, raverhoop, sphereeversion, etruscanvenus, hextrail |

The 2000-2006 bucket independently contains most of what the colour and shape
measurements already flagged as strongest. That is a useful convergence: the
era filter and the visual-quality metrics agree, which means neither is doing
the work alone.

## What "improved" has to mean

Reimplementing is only worth it if the result is categorically better, not
merely current. The trap is that the cheapest-looking upgrades are the ones
that destroy what made these worth keeping.

**The improvements that matter for THIS catalogue**, because our strongest
cluster is structural and almost entirely uncoloured:

- **Linear-space lighting and ACES tone mapping.** The single largest
  "generational" lever. Most of the 1997-2006 pieces composite in gamma space,
  which is why their colour reads as flat and their highlights clip.
- **Ambient occlusion.** This is what makes a complex lattice legible —
  contact darkening in the interior is the difference between a readable
  structure and a wireframe mess. Highest value per cost for the polyhedral
  family.
- **Real self-shadowing.** An articulated lattice that cannot shadow itself is
  not showing its articulation.
- **PBR materials** — metallic/roughness, anisotropic highlights on machined
  forms, clearcoat. The geometric cluster measured at colour 0.00; material,
  not palette, is its opportunity.
- **Depth of field.** Directs the eye. Solves the "everything equally sharp,
  therefore flat" problem that afflicts the surface family.
- **Image-based / procedural environment lighting.** Coherent reflections turn
  a flat-shaded solid into an object with a place.
- **Anti-aliasing.** The 1997 pieces alias badly at 4K; MSAA or temporal AA
  alone is a visible jump.
- **Subsurface scattering** for organic forms (molecules, virions,
  radiolaria) — the difference between plastic and living.

**The improvements to distrust**, per Astra's review: bloom and motion trails.
They read as "modern" instantly and cost almost nothing, which is exactly why
they get over-applied. On a legible structure they smear it. Astra's warning
about volumetric accumulation was specifically that it "can obscure the
topology you selected the family to reveal" — the same applies to trails.

The rule: **effects that clarify form are the upgrade; effects that add glow
are decoration.** A reimplementation that is only brighter has failed.

## The 1990s bucket — cut all seven, overhaul none

Operator direction, 2026-09-26: nothing from the 1990s is worth keeping unless
it would get a major shader overhaul. Measured, none of it earns one.

| target | C | sat | hueE | cov | edge | fracD | motion |
|---|---|---|---|---|---|---|---|
| `atlantis` | 58.0 | 1.00 | 0.54 | 0.89 | **2.3** | 1.63 | 0.07 |
| `lament` | 8.9 | 0.25 | 0.53 | 0.09 | 268.5 | 1.77 | 13.16 |
| `stonerview` | 48.3 | 0.89 | 2.24 | 0.08 | 81.0 | 1.41 | 7.63 |
| `juggler3d` | 8.2 | 0.49 | 0.26 | 0.02 | 225.8 | 1.62 | 0.93 |
| `cube21` | 0.9 | 0.26 | 1.88 | 0.01 | 67.6 | 1.28 | 0.34 |
| `jigsaw` | 3.5 | 0.14 | 0.09 | 0.18 | 90.2 | 1.47 | 10.33 |
| `starwars` | 8.1 | 0.09 | 3.24 | 0.15 | 41.0 | 1.36 | 7.46 |

`atlantis` is the instructive case and the reason the shape axis was worth
adding. It scores C=58.0 at saturation 1.00 with 89% coverage — respectable by
colour alone — but its edge density is **2.3**. It is a flat, single-hue
expanse with effectively no structure anywhere in frame. Colour metrics liked
it; the eye does not. Measuring only colour would have kept it.

Four of the seven occupy 1-18% of the frame. `cube21` occupies one percent.

### Nothing is lost by cutting them

The only two with real structure are already absorbed by decisions made
earlier today:

- `lament` (edge 268.5, fracD 1.77) and `cube21` are both articulated
  polyhedra. That is exactly the **polyhedral lattice family** the geometric
  cluster was folded into — "unfold" and "twist" are joint rules in that
  parameter space, not separate programs. Their ideas survive as points in a
  family; their implementations do not.
- `stonerview` is the only one with hue variety (2.24) but sits at 8% coverage
  — a small bright object in a large black frame, and the rotating-coloured-bar
  idea is well covered by the existing hyprsaver cohort.

`starwars` is excluded on three independent grounds regardless of era: it is a
text-rendering hack, text hacks were already dropped by the curation plan, and
the name is not ours to ship.

`atlantis` and `juggler3d` are figurative/character pieces rather than
algorithmic ones. Reimplementing them would mean authoring new art assets, not
deriving mathematics — which is outside what Afterimage is for.

**Decision: all seven cut. No overhauls. No concepts carried forward beyond the
two already covered by the polyhedral lattice family.**

# The blackhole bar — a measurable acceptance standard

Operator direction, 2026-09-26: *"Black hole should be the bar for what looks
good."* Measured on PEGASUS hardware so it is a number, not an opinion.

## The bar

| metric | blackhole |
|---|---|
| Hasler-Susstrunk colourfulness | 48.0 |
| mean saturation | 0.35 |
| **hue entropy (24-bin)** | **3.56** |
| coverage | 0.87 |
| edge density | 4.9 |
| inter-frame motion | 1.83 |

**What actually makes it good is not maximum anything.** It is mid-range on
colourfulness and low on edge density. Its signature is **wide hue variety at
moderate saturation, a mostly-filled frame, and smooth continuous motion**.
Rich but restrained. A piece that maximises saturation or detail does not
thereby match it.

## First four Afterimage shaders, measured against it

| shader | colour | hue | edge | motion | verdict |
|---|---|---|---|---|---|
| `cellmosaic` | 3.85x | 1.14x | 2.3x | **0.11x** | beats the bar on colour; **nearly static** |
| `hexlattice` | 0.76x | **0.00** | 11x | 10-20x | single-hue, frantic, busy |
| `ridgeline` | 0.28x | 0.38x | 2.1x | **0.00** | dim and frozen; also 0.4fps on the floor tier |
| `wellcurve` | 0.27x | 0.08x | 1.9x | 0.30x | dim, near-monochrome; brief asked saturation 1.00, got 0.32 |

**None of the four clears the bar.** Each passes some axis and fails others -
which is precisely how a catalogue of individually-defensible, collectively
mediocre pieces gets shipped.

Caveat: `ridgeline`'s motion of 0.00 is partly an artifact of sampling frames
at 0.4fps, where consecutive dumps are near-identical. Its real defect is
throughput.

## Measured performance — frames rendered in 10 s

| shader | Intel UHD (floor) | RTX 2060 |
|---|---|---|
| `cellmosaic` | 600 (60fps) | 540 (54fps) |
| `hexlattice` | 600 (60fps) | 540 (54fps) |
| `wellcurve` | 540 (54fps) | 540 (54fps) |
| `ridgeline` | **4 (0.4fps)** | 300 (30fps) |
| `blackhole` | **120 (12fps)** | 540 (54fps) |

Two findings worth keeping:

1. **`ridgeline` is 75x slower on the iGPU than on the discrete GPU.** The
   brief said "raymarched fBm heightfield" without a step budget - a spec
   defect, not an implementation one. A heightfield with an analytic horizon
   gets most of the look for a fraction of the cost.
2. **PRIME offload has a real cost.** `cellmosaic` and `hexlattice` are FASTER
   on the iGPU (600) than offloaded to the RTX (540) because they already hit
   vsync and offload adds a copy back across the bus. **The adaptive tier must
   not blindly offload**; doing so would make cheap shaders worse.

Note the tension this exposes: blackhole is the visual bar AND runs at 12fps on
the floor tier. The quality standard and the performance floor are in direct
conflict, which is exactly why adaptive quality tiering is required rather than
optional.

## Proposed acceptance gate

All four together, not any one:

- hue entropy >= 3.0
- motion within a BAND (roughly 0.5 - 8), not a maximum - static fails, frantic
  fails
- coverage >= 0.8
- >= 30fps at 1920x1080 on Intel UHD, or an adaptive tier that reaches it
- per-run variation: two runs differ in hash at the same frame index

`cellmosaic` already satisfies colour, hue, coverage and performance. It fails
only on motion. That is a tractable fix, not a rewrite.

# POLICY CORRECTION — clean-room ONLY where the licence requires it

Operator directive, 2026-09-26: *"If we do not NEED to clean room we should
not."*

This **supersedes** the blanket "CLEAN ROOM FROM THE MATHEMATICS" rule written
into `2026-09-26-afterimage-design.md` earlier the same day. That rule was
applied everywhere. It should have been applied only where a licence compels
it.

## Where reuse is permitted — read the original, port the real algorithm

| source | licence | reuse |
|---|---|---|
| **xscreensaver (jwz)** | *"Permission to use, copy, modify, distribute, and sell this software..."* | **Yes.** Port the actual algorithm. Retain his copyright notice |
| **hyprsaver** | MIT (Copyright (c) 2026 Mara Vexa) | Yes, retain notice |
| **Shadertoy MIT** (17) | MIT | Yes, retain notice |
| **Shadertoy CC0** (17) | CC0 | Yes |
| **Shadertoy CC BY** (1, `synthwavecity`) | CC BY 3.0 | Yes, with attribution |

## Where clean-room IS required

Only shaders under **CC BY-NC-SA** — non-commercial and share-alike. Reuse
would produce a derivative bound by both terms, and no amount of modification
escapes that. As of this writing that was three shaders (`driftclouds`,
`bubblecolors`, `synthwavecity` pending its CC BY confirmation), now replaced
by `stratus`, `iridescence` and `neonhorizon`.

## Why this matters — the blanket rule cost real quality

The first four Afterimage shaders were written clean-room from a prose
description I wrote, rather than from the actual algorithm. Measured against
the blackhole bar, none cleared it:

- `ridgeline` - dim, effectively frozen, and **0.4fps on the floor tier**
- `wellcurve` - brief specified saturation 1.00, delivered 0.32
- `hexlattice` - hue entropy 0.00, single-hue
- `cellmosaic` - good colour, but motion 0.11x the bar

They were guessing at an effect instead of implementing a known one. For
xscreensaver-derived work there was never a legal reason to make them guess.

**The correct division of effort:** take the real algorithm, then spend the
budget on the rendering upgrade that 2003 could not do - PBR materials,
ambient occlusion, HDR accumulation, ACES tonemapping, depth of field,
anti-aliasing. That is where the value is, not in re-deriving a heightfield
from prose.

## Standing rule

Before imposing clean-room on any piece, state which licence compels it. If
none does, read the original.
