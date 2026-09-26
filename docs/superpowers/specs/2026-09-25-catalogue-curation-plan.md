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
