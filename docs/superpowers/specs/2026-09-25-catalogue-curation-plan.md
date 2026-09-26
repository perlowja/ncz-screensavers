# Screensaver catalogue plan — curation buckets

**Date:** 2026-09-25
**Rule (operator):** visual quality over catalogue completeness. Unless a
hack has nice colours and is visually attractive, it is out of scope.
Interesting algorithms with weak visuals are candidates for rewrite on the
new engine, not for preservation as-is.

Derived from real evidence: two full 141-target cross-vendor sweeps (Intel
UHD + NVIDIA RTX 2060) and visual review of every passing legacy hack's
real screenshot.

## Why curate at all — the measured case

| Rendering path | Targets | PASS | FAIL | Success |
|---|---|---|---|---|
| hyprsaver (modern shaders) | 35 | 35 | 0 | **100%** |
| rss-sdl2 | 13 | 9 | 4 | 69% |
| base (legacy fixed-function) | 92 | 60 | 32 | **65%** |

The modern shader path is flawless; the 1990s fixed-function path tops out
at about two thirds. And on visual review, the *majority* of the 60
passing legacy hacks are a single small 3D object floating centre-frame on
black — competent 1998 screensavers, unremarkable on a 4K display in 2026.
The problem is not that they are broken. They were designed for a
different era.

## Bucket A — SHIP AS-IS (visually attractive, passing)

Real colour, real visual interest, working today. No work needed.

`crackberg` (terrain under a yellow sky), `energystream`, `etruscanvenus`,
`geodesic`, `glforestfire`, `hextrail`, `hypnowheel`, `lockward`, `noof`,
`projectiveplane`, `raverhoop`, `razzledazzle` (high-chroma after the
2026-09-25 fix), `romanboy`, `spheremonics`, `squirtorus` (gold stars +
coloured rings restored 2026-09-25), plus the flagship tier below.

## Bucket B — REIMAGINE on the new GLES3 engine

Strong motion signature AND obvious modern upside (bloom, depth,
volumetrics, soft particles, trails, tone mapping). These justify native
rewrites.

| Hack | The algorithm worth preserving | Modern upside |
|---|---|---|
| `gravitywell` | spacetime grid deformation | pairs thematically with our black hole; real lensing/glow |
| `noof` | spirograph / harmonograph curves | trails, bloom, HDR colour |
| `hypnowheel` | concentric phase banding | crisp banding, motion blur |
| `lockward` | rotating radial segment masks | clean AA, richer palettes |
| `raverhoop` | light-trail persistence | proper trails + bloom is the whole point |
| `projectiveplane`, `romanboy`, `etruscanvenus`, `klein` | non-orientable surface math | banded colour + real lighting transforms these |
| `glforestfire` | particle fire + atmosphere | modern particles, additive blending |
| `hextrail` | procedural hex growth | glow, depth |
| `crackberg` | procedural terrain | sky, fog, lighting |

## Bucket C — ALGORITHM-ONLY (rewrite candidates, do not ship legacy build)

Mathematically interesting, visually weak as they stand (small pale object
on black). Worth the *idea*, not the 1990s rendering. Revisit when the new
engine exists; do not spend shim effort on them.

`menger`, `sierpinski3d` (unported), `hilbert`, `glknots`, `moebiusgears`,
`sphereeversion`, `hypertorus`, `polytopes` (unported), `cubicgrid`,
`cubestorm`, `cubetwist`, `cubestack`, `cubenetic`, `glsnake`, `jigsaw`,
`kaleidocycle`, `rubikblocks`, `papercube`, `geodesicgears` (unported).

## Bucket D — OUT OF SCOPE

Neither visually attractive nor algorithmically compelling. Includes the
32 both-vendor failures and the dull-but-passing remainder. Default action
is **do nothing** — not a backlog.

The running 21-failure investigation may reclaim a few cheaply if they turn
out to be shared shim defects; anything not reclaimed stays here.

## Text-rendering hacks — DROP (legacy), KEEP WITH GUARDRAILS (modern)

GRAEAE consulted 2026-09-25 (5 muses responded, 5 errored with
`CostReservationConflict`; treat the reported 1.0 consensus with care).
Its framing: *"the important distinction is not 'does it render text?' but
whether text is the point, whether it looks beautiful on a modern desktop,
and whether it is technically healthy."*

Measured: 'Text & Data' is our worst legacy theme at 2/7 passing, and text
rendering (fonts, textures, display lists) is exactly where our
compatibility shim is weakest.

**Drop:** `fliptext`, `gltext`, `unicrud`, `splitflap`, `starwars`.
- `gltext` historically renders hostname/date — *"exactly the wrong
  lock-screen instinct."*
- `starwars` is an IP/trademark magnet. If we want the emotional shape,
  build an original crawl: different name, typography, camera, copy.
- Blanking the text (option B) was explicitly rejected: *"A split-flap
  board with no destinations, a terminal with no text, or a crawl with no
  crawl becomes uncanny rather than elegant."*

**Keep, with guardrails:** `hyprsaver_matrix`, `hyprsaver_terminal`
(if tasteful), `hyprsaver_stonks` (only if elegant, not a novelty widget).
These are modern shader hacks and all pass reliably.

Guardrails for anything that ships text — a screensaver is visible while
the machine is **locked**:
- no hostname, no username, no real paths, no real IPs
- no date/time on the lock screen by default (opt-in at most)
- no third-party branding strings
- no fake output implying surveillance, hacking or system failure

## Flagship tier — unaffected

35 hyprsaver + 13 rss + `blackhole` + 38 Shadertoy ports via `xshadertoy`
(`e0c890d`). These already are the visual standard the rest is measured
against.

## Standing consequence

Stop growing the legacy fixed-function path. No jwxyz/jwzgles migration
(see `2026-09-25-jwxyz-adoption-design.md` and the GRAEAE consult that
followed it). New content goes to the new engine or to shader hacks.

## Catalogue metadata schema — era and visual appeal

Operator direction: categorise by **era** and **visual appeal**, surfaced
in both the engine and the chooser UX.

`hacks.tsv` (downstream, NCZ-owned) extends from 4 columns to 7:

```
binary | label | family | theme | era | appeal | verified
```

- **family** (existing): `base` | `hyprsaver` | `rss` | `shadertoy` | `native`
  — the rendering path.
- **theme** (existing): Geometry & Fractals, Machines & Objects, Nature &
  Organic, Space & Sci-Fi, Particles & Physics, Abstract & Psychedelic,
  Text & Data.
- **era** (new): `1990s` | `2000s` | `2010s` | `2020s` | `shader`.
  Sourced from real data — the first copyright year in each hack's
  upstream source header, not guessed. Extracted across all 319 upstream
  hacks.
- **appeal** (new): `showcase` | `standard` | `rewrite` | `archive`,
  mapping to buckets A / (passing but plain) / C / D above.
- **verified** (new, per the gate section): `pass` | `needs-work` |
  `untested`. Only `pass` rows are eligible for the shipped catalogue.

### Measured era distribution

All 319 upstream hacks: 1990s 122, 2000s 121, 2010s 41, 2020s 35.

Our 92 legacy hacks, with pass rate:

| Era | Ours | Pass | Rate |
|---|---|---|---|
| 1990s | 7 | 5 | 71% |
| 2000s | 39 | 26 | 66% |
| 2010s | 26 | 17 | 65% |
| 2020s | 13 | 9 | 69% |
| (no year detected) | 7 | — | — |

**Age does not predict brokenness** — the pass rate is flat across eras.
That is a useful negative result: the failures are not decay or
obsolescence, which supports the hypothesis that they are shim defects or
per-hack porting bugs. Era is therefore a *browsing* dimension, not a
quality signal, and must not be used to rank or filter by quality.

**The "classics" era is currently near-empty for us.** We ship only 7
hacks originating in the 1990s (`jigsaw`, `juggler3d`, `lament`,
`starwars`, `stonerview` passing). The bulk of the 1990s catalogue — 122
upstream hacks — is the 2D X11 set we never ported and have now decided
not to chase wholesale. If a browsable "Classics" era is wanted as a
product feature, it needs deliberate curation of a handful of 2D hacks
reimplemented on the new engine, not a mass port.

### Chooser UX consequences

This extends the 4-way mode in
`2026-09-25-screensaver-preferences-design.md` (off / random-all /
random-category / specific). With era and appeal available:

- **Showcase mode** — random from `appeal=showcase` only. This should be
  the DEFAULT for a new install: it is the honest answer to "make it look
  good out of the box", and avoids a first-run impression formed by a
  small grey object on black.
- **Browse by era** — a legitimate nostalgia axis ("Classics", "Modern"),
  presented as a filter, never as a quality ranking.
- **Browse by theme** — existing axis, unchanged.
- The picker should show `appeal` as a visual grouping so a user scanning
  140 entries sees the strong ones first rather than alphabetically.
- `archive` entries stay out of the shipped catalogue entirely; they are
  not hidden-but-present, they are absent.

Both new columns are NCZ-owned data. Per the repo boundary recorded in
the jwxyz design doc, the generic chooser widgets go upstream to
Singularity while this metadata stays downstream.

## Rewrite shortlist — what is actually worth building on the new engine

Roughly **12 must-have, ~12 more if the engine makes them cheap** — out of
a 288-hack upstream catalogue. The gap between those numbers is the point
of curating.

### Top tier (12)

| Hack | Algorithm | Why it earns a rewrite |
|---|---|---|
| `flame` | fractal flame / IFS | Scott Draves' algorithm. Best single candidate: built for additive blending and HDR, visibly constrained by 1997 hardware |
| `coral` | diffusion-limited aggregation | organic growth; glow and depth transform it |
| `galaxy` | n-body gravity | particles + bloom; thematic sibling to the black hole |
| `glschool` | boids / flocking | clean classic, large modern upside |
| `munch` | Munching Squares (PDP-1, 1962) | genuinely historic, trivially cheap, razor-sharp at 4K |
| `cloudlife` | Conway's Life variant | cellular automata with modern palettes |
| `thornbird` | "Bird in a Thornbush" attractor | chaos as a glowing point cloud |
| `discrete` | discrete-map fractals | same family, different character |
| `gravitywell` | spacetime deformation | pairs with the black hole |
| `noof` | spirograph / harmonograph | trails and bloom are the whole point |
| `raverhoop` | light-trail persistence | trails are the entire concept |
| `lavalite` -> metaballs | implicit-surface blobs | in progress; first new-engine piece |

### Second tier (~8-12)

`whirlwindwarp`, `moire`, `rorschach`, `kaleidescope`, `hextrail`,
`glforestfire`, `crackberg`, `hypnowheel`, `lockward`, plus **two** of the
surface-math family and **one** space-filling fractal.

### Three reasons the number is smaller than the catalogue

1. **Redundancy.** Whole clusters are one idea each: five cube variations,
   four non-orientable surfaces, three space-filling fractals. Doing the
   best one properly beats reimplementing all of them.
2. **Some are drawings, not algorithms.** `helix`, `spiral`, `squiral`,
   `pedal`, `rotor`, `cynosure` are parametric curve renderings — little
   algorithmic substance to preserve; they would be new shader pieces
   wearing a 1990s name.
3. **We already cover several themes better.** The 38 Shadertoy ports and
   35 hyprsaver hacks already deliver fractals, fluids, tunnels and plasma
   at modern quality. Rebuilding `julia` when shader fractals exist is
   duplicated effort.

Start order: `lavalite`/metaballs (in flight), then `flame` and
`glschool` — both famous, both clean, both dramatically better with
modern rendering than 1997 could show.

---

# CONSOLIDATED SCOPE (operator direction, 2026-09-25)

**"Sideline the classic 2D ones. Consolidate on what works WELL now and
what new stuff we want to build."**

## SIDELINED — explicitly not doing

- **All 142 upstream 2D X11 hacks.** Including the ~40 "low-hanging" ones
  (1-4 drawing primitives, no text/image deps).
- **The thin GLES3 Xlib primitive shim** that would have run them
  unmodified. Sound idea, no longer in scope.
- **jwxyz / jwzgles adoption.** Already rejected: targets deprecated
  fixed-function GLES 1.1, unverified on Mali, missing a Linux font
  backend.
- **The 46 remaining upstream GL hacks**, except the few named in the
  rewrite shortlist.
- **The 32 both-vendor failures.** Archived, not a backlog. The running
  root-cause investigation may reclaim a couple cheaply; anything it does
  not, stays archived.

No further effort goes into the legacy fixed-function path.

## SHIPS WELL TODAY (~90 targets)

| Set | Count | State |
|---|---|---|
| Shadertoy ports via `xshadertoy` | 38 | landed `e0c890d`, fixes in `123eb3c`; needs visual verification |
| hyprsaver shader hacks | 35 | **35/35 pass**, the healthiest set we own |
| `blackhole` | 1 | flagship; cinematic camera + lensed nebula + palette drift |
| rss-sdl2 family | 9 of 13 | passing |
| Legacy GL, visually strong (bucket A) | ~15 | `crackberg`, `energystream`, `etruscanvenus`, `geodesic`, `glforestfire`, `hextrail`, `hypnowheel`, `lockward`, `noof`, `projectiveplane`, `raverhoop`, `razzledazzle`, `romanboy`, `spheremonics`, `squirtorus` |

## NEEDS ONE MORE PASS (known, specific)

- **`lavafield`** (`977501d`) — metaball algorithm and shading are good;
  palette drift verified (yellow at 4s, teal at 24s); `gl_error=0x0`. But
  measured frame coverage is **5%**, against the old `lavalite`'s 4%. The
  lamp is gone, the composition problem is not. Needs much larger and more
  numerous blobs so the field genuinely fills the frame.
- **`blackhole`** — Intel UHD frame time **50-67 ms** (~15-20 fps),
  regression isolated to the banked-slingshot camera, not the palette
  work. Also still one hue per frame: the time drift works, the in-frame
  banding does not read. Both in flight.
- **`squirtorus` / `razzledazzle`** — fixed and verified today
  (12,092 exact-gold pixels; 482/500 high-chroma colours). Done.

## BUILD NEW

**Algorithm rewrites on the new engine**, in order:
1. `flame` — fractal flame / IFS. Best single candidate: built for
   additive blending and HDR, visibly constrained by 1997 hardware.
2. `glschool` — boids / flocking.
3. `galaxy` — n-body gravity; thematic sibling to the black hole.

**Original work:**
- **Stock visualisations** — volatility field, candlestick terrain, sector
  heat-field, price ribbons. Rendered as *shapes*, no tickers or numbers.
  Architecture: a service writes a small cache; the screensaver does zero
  network I/O and falls back to synthetic data offline. **Market-wide data
  only** — `/opt/marketwatch/portfolios/` holds real holdings and a
  screensaver runs on a locked screen.
- **Black hole viewport/HUD** — the bridge-window framing, still wanted.
- **More Shadertoy-class originals** — where the "wow" demonstrably lives.

## The evidence behind consolidating

| Path | Targets | Pass rate |
|---|---|---|
| hyprsaver (modern shaders) | 35 | **100%** |
| rss-sdl2 | 13 | 69% |
| legacy fixed-function | 92 | **65%** |

And on visual review of all 60 passing legacy hacks, most are a single
small object centred on black — competent in 1998, unremarkable at 4K.
The modern shader path is both more reliable and better looking. That is
where the effort goes.

## The legacy shim is FROZEN

 and  are frozen. No further
changes. They keep running the hacks that already work; they receive no
fixes, no features and no new hacks.

**Why — the shim's own scorecard, all measured 2026-09-25:**

- Pass rate **65%** (60/92), against **100%** (35/35) for the modern
  shader path.
- Every colour defect found today traced to it: squirtorus' ejecta rings
  and ground rendering black (display-list material capture plus lighting
  modulation on unlit geometry), razzledazzle's hull collapsing to the
  20% ambient floor.
- Of 6 both-vendor failures root-caused, **5 are its own admitted stubs**:
  `glTexGeni` inert, `glLogicOp` and `glTexImage1D` stubbed,
  `textclient_getc` returning -1 forever, `texture_string_metrics`
  returning zero widths.
- **Touching it is net-negative.** A careful, well-evidenced 61-line
  `glDrawElements` wrapper (`2629654`) fixed `glcells` and silently broke
  five working hacks -- `romanboy`, `etruscanvenus`, `projectiveplane`,
  `klein`, `sphereeversion` -- three of them in the ship-as-is bucket. It
  was reverted in `27b7eb2` after before/after measurement on real
  hardware. **+1 archived-tier hack, -5 working hacks.**

That last point is the decisive one. The shim is a 2,545-line
fixed-function emulation with enough hidden coupling that a targeted fix,
verified on its target, regressed five unrelated hacks. Further
investment cannot be made safely at a cost proportional to its value.

**The rule:** anything the frozen shim cannot render correctly is either
archived or rewritten on the new engine. It is never patched.

## DECISION 2026-09-25: the upstream port is CLOSED

Operator: *"If nothing newer is left to port from jwz then stop at what we
have."*

Measured against upstream 6.16 (`b99f621`) the same day:

| | count |
|---|---|
| Upstream `.glsl` shaders | 38 |
| **Ours** | **38 — all of them** |
| Upstream GL hacks | 170 |
| Our `_gles3` targets | 172 |
| Upstream GL hacks not in our build | 86 |
| …of which shader-backed (we already have the `.glsl`) | 31 |
| **Genuinely unported GL C hacks** | **55** |
| Upstream 2D X11 hacks | 144 (none ported) |

**The shader seam is fully mined — there is nothing left to take.** That
was the high-value path and it is complete.

**The remaining 55 GL C hacks are OUT OF SCOPE, permanently.** Not a
backlog, not a someday list. Filtered through the curation rule (nice
colours and visually attractive, or out of scope) the bulk of them are
bucket C/D: small pale objects on black (`polyhedra`, `polytopes`,
`sierpinski3d`, `superquadrics`, `morph3d`, `moebius`, `maze3d`,
`stairs`, `rubik`), text/data hacks already ruled out (`gltext`,
`glslideshow`, `dnalogo`, `graphstat`), or board games (`endgame`,
`klondike`, `queens`).

**The 144 2D X11 hacks stay unreachable by design.** They draw with Xlib
primitives and require jwxyz, which was rejected because it targets
deprecated GLES 1.1 (see `2026-09-25-jwxyz-adoption-design.md` and the
GRAEAE consult that followed).

### Why closing is the right call, not a concession

The measured success rates decided it: **100% on the modern shader path,
65% on the legacy fixed-function path.** Every additional C hack carries
shim-compatibility tax on a layer we have already decided not to replace,
and adds 1998-era objects-on-black to a catalogue whose quality bar is
now set by original work.

**Effort goes to original pieces on the new engine** — `neonspacewar`,
`genxvectorcade`, `magmasimplex` — and to making the 38 shaders and the
existing targets genuinely good. A verification pass on those 38 was
already running when this decision was made, precisely because their
quality matters more than their number.

If upstream ships new `.glsl` shaders in a future release, revisit **that
seam only**. Do not reopen the C-hack port.
