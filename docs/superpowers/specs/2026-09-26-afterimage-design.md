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
