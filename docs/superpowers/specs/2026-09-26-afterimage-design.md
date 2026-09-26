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
