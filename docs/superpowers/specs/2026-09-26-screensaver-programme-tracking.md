# NCZ screensaver programme — tracking

Status as of 2026-09-26. Supersedes ad-hoc lists.

## A. Ship with the next NCZ build

Operator direction: package the engine plus only what is known to work.

| component | state | blocker |
|---|---|---|
| screensaver / lock engine | in tree | — |
| **settings UX** | **never tested** | needs a real test pass before it ships |
| `blackhole` | works, operator-reviewed | — |
| `hyprsaver` cohort (35) | keep-as-is, whole family | — |
| Shadertoy cohort (44) | keep-as-is, whole family; includes `synthwavecity` | Shadertoy pieces were NOT in the capture sweep, so they are unmeasured — kept on operator direction, not on evidence |
| `magmasimplex` | in build | prococean-ending port (ACES first, then Fresnel-as-weight, then depth scattering) not started |
| `neonspacewar` | in build | — |
| `genxvectorcade` | in build | — |

**Not shipping:** every classic xscreensaver port, the whole rss-sdl2 family
(cut 2026-09-26), and all seven 1990s targets.

### Known gaps before release
- Settings UX untested — the one real unknown in the shipping set.
- `ephemeris` and `leviathan` are **not wired into `meson.build`** and are not
  built at all. Two of the five named originals.
- Harness in `builddir` is stale: it writes raw RGBA under a `.png` name. The
  source already fixes this; the binaries predate it. Rebuild before any
  capture work is trusted.

## B. Originals in flight

| piece | state |
|---|---|
| `blackhole` | shipping |
| `magmasimplex` | shipping; fluid/optics improvements outstanding |
| `neonspacewar` | shipping |
| `genxvectorcade` | shipping |
| **`ephemeris`** | designed, chronicled, **not in the build** |
| **`leviathan`** | designed (3 movements), **not in the build** |
| **`apothecary`** | NEW — Afterimage proof of concept. Data layer DONE: 3,094 named drug CIDs identified, 415MB PubChem 3D chunks downloaded, converter written, 98-molecule demo header committed. Renderer not started |
| **`chaperone`** | NEW — protein ribbons. Feasibility confirmed (RCSB is public domain and ships HELIX/SHEET records, so no DSSP dependency). Not started |

## C. Afterimage — port candidates and level of effort

Effort bands: **S** = 1-3 days (pure fragment shader, published formula, no
state). **M** = 4-7 days (geometry generation or multi-pass). **L** = 1-3 weeks
(family engine, or per-agent state via FBO ping-pong / transform feedback).

| unit | effort | absorbs | why it earns the work |
|---|---|---|---|
| **Polyhedral lattice family** | **L** (2-3 wk) | **~14 targets** | Best ROI in the programme. `cubestorm` `cubetwist` `cubicgrid` `cubestack` `topblock` `hexstrut` `geodesic` `glknots` `moebiusgears` `menger` `glsnake` `polyhedra-gl` `lament` `cube21` are one idea under different joint rules. Owns the shape-complexity axis (cubestorm edge 399, fracD 1.81) at **colour 0.00** — material and texture are the entire opportunity |
| **Surfaces / 4D family** | **L** (1-2 wk) | **~7 targets** | `klein` `hypertorus` `projectiveplane` `romanboy` `spheremonics` `sphereeversion` `etruscanvenus`. Highest measured hue entropy (projectiveplane 4.08) but 60-90% black — needs presentation grammar, not more zoom |
| **`apothecary`** (molecules) | **M** (4-6 d) | 1 + new territory | PoC, data already built. Instanced spheres/cylinders, PBR, AO, name text |
| **Iterated-orbit density** | **M** (~1 wk) | 3-4 | `noof` `thornbird` `discrete` + flame variations. Needs an accumulation buffer, additive blend, tone mapping — machinery every later attractor reuses |
| **Terrain** (from `crackberg`) | **S-M** (2-3 d) | 1 | Pure raymarched fBm + atmospheric scattering. Already full-frame (cov 1.00). High payoff per day |
| **Apollonian gasket** | **S** (2 d) | 0 (new) | Descartes circle theorem, per-pixel, unbounded zoom |
| **`cwaves`** | **S** (1 d) | 0 (new) | Sinusoid superposition. Distinct in mechanism from our noise-based cohort. **Must pass the grayscale test** or it is only a palette |
| **Celtic knotwork** | **M** (4-6 d) | 0 (new) | Plane-filling by construction — answers the coverage problem natively. Published (Mercat). Risk: inert line art without a temporal contract |
| **`chaperone`** (proteins) | **L** (1-2 wk) | 0 (new) | Ribbon extrusion along splines, secondary structure from the PDB file itself |
| **Voronoi fracture** (from `crumbler`) | **M** (5-7 d) | 1 | Cell decomposition plus fragment dynamics |

**Deferred:** boids/`glschool`, n-body/`galaxy`, DLA/`coral` — all need real
per-agent state and belong to a separate simulation effort. Lyapunov and
Rankine-vortex excluded as marginal against the 79 shader pieces already
shipping.

### Recommended order

1. **`apothecary`** — PoC, data done, proves the pipeline end to end (M)
2. **Terrain** — cheapest real visual win (S-M)
3. **Polyhedral lattice family** — 14 targets retired in one unit (L)
4. **Surfaces / 4D family** — 7 more (L)
5. Everything else, judged on what the first four teach us

Two families plus a PoC retires ~21 legacy targets and adds one genuinely new
piece. That matches Astra's independent advice: launch two families, make a
third earn admission, and measure the milestone in convincing long-running
clips rather than family count.
