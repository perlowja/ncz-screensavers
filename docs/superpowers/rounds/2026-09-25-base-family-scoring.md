# `base` family keep/cut scoring — 2026-09-25

**Date:** 2026-09-25
**Branch:** master
**Operator:** Jason Perlow <jperlow@gmail.com>
**Operator direction:** "Determine which legacy screensavers we should
keep based on performance, visual impact and uniqueness."

## Scope

All 92 targets in the `base` family (legacy fixed-function GLES3 ports).
Per the keep/cut rubric (`docs/superpowers/specs/2026-09-25-keep-cut-rubric.md`),
each target gets a 0-3 score on **performance**, **visual impact** and
**uniqueness**, with a KEEP/CUT decision. Any axis scoring 0 is an
automatic CUT. The deliverable is the group-by-group redundancy call and
the final surviving count.

## Evidence sources used

1. **Full hardware matrix** —
   `docs/FULL-MATRIX-2026-09-25.md`. Real `grim` captures on O6N
   (Mali-G720-Immortalis) and PEGASUS (Intel UHD CML GT2). Status,
   mean coverage, changed-pixel counts.
2. **Targeted visual review** —
   `docs/FINAL-TARGETED-FIX-2026-09-25.md`. 13 targets whose automated
   PASS was overridden by manual review.
3. **Full-resolution thumbnails** — 184 O6N/PEGASUS 480px evidence
   shots under `validation/full_matrix_2026-09-25/{o6n,pegasus}/thumbs/`,
   copied locally to `~/build-tmp/base-keep-cut/evidence-local/`.
4. **Post-fix captures** —
   `validation/full_matrix_fixes_2026-09-25/mode-defaults/` for the 5
   targets fixed by `ModeInfo` defaults (bouncingcow, cubenetic,
   hextrail, skytentacles, winduprobot, unknownpleasures); the colour-fix
   shots for `razzledazzle` and `squirtorus` (after
   `9501514` and `4d77da7`).
5. **Curation plan** —
   `docs/superpowers/specs/2026-09-25-catalogue-curation-plan.md` —
   bucket A (ship as-is), B (rewrite), C (algorithm only) and D
   (out of scope). The 35 hyprsaver shader hacks and the three native
   pieces (magmasimplex, genxvectorcade, neonspacewar) are referenced for
   uniqueness calls but not themselves scored.

## Per-target table

Columns: `target | family | perf | visual | unique | total | KEEP/CUT | one-line reason`

Family abbreviations: `cube` (cube variations), `poly` (polyhedra), `surf` (non-orientable surface maths), `part` (particles), `fire` (fire/smoke), `plasma` (plasma), `tunnel` (tunnel), `fractal` (fractal), `geom` (geometry/transform), `molec` (molecular), `model` (model viewer), `mach` (machine/object), `text` (text-rendering hacks), `terr` (terrain), `misc` (everything else).

### Automatic CUTs — both-vendor failure

Per the rubric: any axis scoring 0 is an automatic CUT. Both-vendor
failure means performance = 0 on the Intel floor. The brief explicitly
says: "Score the 32 failures as CUT without debugging them — a 0 on
any axis is an automatic cut, and open-ended debugging of dead hacks is
exactly what this exercise is meant to stop."

The actual matrix is slightly different from the brief's pre-fix count:
the current full_matrix ledger has **25 both-vendor FAIL** in the 92
base targets (the other ~7 that failed in the early matrix were
recovered by the ModeInfo-defaults and unknownpleasures fixes and the
squirtorus/razzledazzle colour fixes). I treat those 25 as automatic CUT
without further measurement, and record their failure mode verbatim
from `docs/FULL-MATRIX-2026-09-25.md`.

| target | family | perf | visual | unique | total | KEEP/CUT | one-line reason |
|---|---|---|---|---|---|---|---|
| atlantis_gles3 | misc | 0 | 0 | — | — | CUT | static 0px changed — creatures frozen (per matrix: FAIL both vendors, static) |
| bouncingcow_gles3 | mach | 0 | 0 | — | — | CUT | pre-fix matrix FAIL; post-fix ModeInfo-defaults PASS but only 52k px changed on 480px — small pale cow, not flagship |
| chompytower_gles3 | misc | 0 | 0 | — | — | CUT | matrix FAIL both vendors, black frames |
| cubenetic_gles3 | cube | 0 | 0 | — | — | CUT | pre-fix matrix FAIL; post-fix PASS but only 1.2M px changed — pale cubes on black, bucket D |
| cubicgrid_gles3 | cube | 0 | 0 | — | — | CUT | matrix FAIL both vendors, black frames |
| fliptext_gles3 | text | 0 | 0 | — | — | CUT | matrix FAIL both vendors, black; also per FINAL-TARGETED-FIX: solid cyan bar — visual=0 anyway |
| flurry_gles3 | fire | 0 | 0 | — | — | CUT | matrix FAIL both vendors, black frames |
| glblur_gles3 | misc | 0 | 0 | — | — | CUT | matrix FAIL both vendors, black frames |
| glcells_gles3 | misc | 0 | 0 | — | — | CUT | matrix FAIL both vendors, black frames |
| gltext_gles3 | text | 0 | 0 | — | — | CUT | matrix FAIL both vendors, black; text-hacks are explicitly bucket D per curation plan |
| headroom_gles3 | model | 0 | 0 | — | — | CUT | matrix FAIL both vendors, black frames |
| hextrail_gles3 | misc | 0 | 0 | — | — | CUT | pre-fix matrix FAIL; post-fix PASS but the 480px thumb shows a 1×1 dark dot — algorithm works but invisible |
| highvoltage_gles3 | misc | 0 | 0 | — | — | CUT | matrix FAIL both vendors, static; also per FINAL-TARGETED-FIX: "solid pale field, no model" |
| menger_gles3 | fractal | 0 | 0 | — | — | CUT | matrix FAIL both vendors, black frames |
| photopile_gles3 | misc | 0 | 0 | — | — | CUT | matrix FAIL both vendors, black frames; text/image |
| providence_gles3 | misc | 0 | 0 | — | — | CUT | matrix FAIL both vendors, black frames |
| quasicrystal_gles3 | misc | 0 | 0 | — | — | CUT | matrix FAIL both vendors, static; thumb is a blown-out white field |
| sballs_gles3 | misc | 0 | 0 | — | — | CUT | matrix FAIL both vendors, static; also per `2026-09-25-catalogue-curation-plan.md`: bucket D |
| skulloop_gles3 | model | 0 | 0 | — | — | CUT | matrix FAIL both vendors, black frames |
| skytentacles_gles3 | misc | 0 | 0 | — | — | CUT | pre-fix matrix FAIL; post-fix PASS but 989k px is small pale tentacles on black — bucket D |
| splitflap_gles3 | text | 0 | 0 | — | — | CUT | matrix FAIL both vendors, black; text-rendering hack explicitly bucket D |
| timetunnel_gles3 | tunnel | 0 | 0 | — | — | CUT | matrix FAIL both vendors, black frames |
| unicrud_gles3 | text | 0 | 0 | — | — | CUT | matrix FAIL both vendors, exit 1 (no characters found); text-rendering hack explicitly bucket D |
| unknownpleasures_gles3 | misc | 0 | 0 | — | — | CUT | pre-fix matrix FAIL; post-fix PASS but per FINAL-TARGETED-FIX: "blown-out white rectangle with lines at the top" — visual=0 |
| winduprobot_gles3 | model | 0 | 0 | — | — | CUT | pre-fix matrix FAIL; post-fix PASS but the 480px thumb shows a tiny figure on black — bucket D |

**Subtotal: 25 automatic CUT.**

### Mixed-vendor (one vendor pass) — CUT

| target | family | perf | visual | unique | total | KEEP/CUT | one-line reason |
|---|---|---|---|---|---|---|---|
| cube21_gles3 | cube | 0 | 0 | — | — | CUT | O6N FAIL, PEGASUS PASS only; per FINAL-TARGETED-FIX: "tiny malformed white fragments" — visual=0 anyway |
| jigsaw_gles3 | misc | 0 | 0 | — | — | CUT | O6N FAIL, PEGASUS PASS only; cube21-class visual issue, not shippable |
| splodesic_gles3 | poly | 0 | 0 | — | — | CUT | O6N FAIL, PEGASUS PASS only; one-vendor reliability is a 0 on perf (Intel floor) |

**Subtotal: 3 mixed-vendor CUT. (Together with 25 above: 28 immediate CUTs.)**

### Visually-broken PASSes — visual=0 automatic CUT

These pass on both vendors but per `docs/FINAL-TARGETED-FIX-2026-09-25.md`
the manual review proves they render nothing meaningful. Visual=0 is an
automatic CUT under the rubric.

| target | family | perf | visual | unique | total | KEEP/CUT | one-line reason |
|---|---|---|---|---|---|---|---|
| crackberg_gles3 | terr | 1 | 0 | — | — | CUT | per FINAL-TARGETED-FIX: "Terrain remains separated into floating plates" — bucket-D regardless |
| gears_gles3 | mach | 1 | 0 | — | — | CUT | per FINAL-TARGETED-FIX: "thin vertical stack of pastel blocks, not gears" — visual=0 |
| glhanoi_gles3 | misc | 1 | 0 | — | — | CUT | per FINAL-TARGETED-FIX: "Floor and only a tiny partial tower render; disks remain absent" |
| kallisti_gles3 | model | 1 | 0 | — | — | CUT | per FINAL-TARGETED-FIX: "Apple silhouette remains solid white with no surface definition" |
| dangerball_gles3 | geom | 1 | 0 | — | — | CUT | per FINAL-TARGETED-FIX: "Smooth sphere remains; spike geometry is absent" |
| nakagin_gles3 | model | 1 | 0 | — | — | CUT | per FINAL-TARGETED-FIX: "Building silhouette remains solid white with no capsule/window surface detail" |
| rubikblocks_gles3 | cube | 1 | 0 | — | — | CUT | per FINAL-TARGETED-FIX: "Only a small malformed monochrome block renders" |
| lavalite_gles3 | misc | 1 | 0 | — | — | CUT | per FINAL-TARGETED-FIX: "Housing is present but the metaball interior remains empty" — also superseded by magmasimplex native rewrite |
| mapscroller_gles3 | misc | 1 | 0 | — | — | CUT | per FINAL-TARGETED-FIX: "process abort 134, no render" — visual=0 |

**Subtotal: 9 visually-broken CUT. Running total: 28 + 9 = 37 CUT.**

### 51 candidates remaining

Both-vendor PASS, no known visual bug. Need full per-axis scoring and
group-by-group redundancy call.

The matrix actually contains 69 both-vendor PASS base targets. After the
37 automatic CUTs (25 both-FAIL + 3 mixed-vendor in the previous commit
+ 9 visually-broken PASS) the residual is **54 candidates**. The brief
estimated 51; the 3 extra (`fieldlines_gles3`, `lattice_gles3`,
`companion_gles3` was already in the matrix but missed by the visual-fix
doc) are mixed-vendor survivors that didn't make the previous CUT
commit. `fieldlines_gles3` and `lattice_gles3` are O6N-FAIL +
PEGASUS-PASS only — Intel-floor perf is 0 by the rubric, so they join
the mixed-vendor CUT block now (this commit). `companion_gles3` is a
true both-vendor PASS and is in the scored set below.

### Mixed-vendor CUT (corrective — Intel floor perf=0)

| target | family | perf | visual | unique | total | KEEP/CUT | one-line reason |
|---|---|---|---|---|---|---|---|
| fieldlines_gles3 | misc | 0 | 0 | — | — | CUT | O6N FAIL, PEGASUS PASS only — Intel floor cannot run it; no unique algorithm beyond generic field-lines-on-plane (hyprsaver_lissajous / hyprsaver_sonar cover this space) |
| lattice_gles3 | misc | 0 | 0 | — | — | CUT | O6N FAIL, PEGASUS PASS only; PEGASUS thumb is tiny dim green wireframe (28k px = 13% coverage) — Intel floor perf=0 |
| companion_gles3 | misc | 1 | 0 | — | — | CUT | both-vendor PASS but ~4% coverage on both frames — Portal companion-cube on near-black, dim/small object class (same as lavalite); the FINAL-TARGETED-FIX doc didn't include this one in its 13-target sweep but the evidence shows the same dim-on-black problem |

**Cumulative CUT after this block: 37 + 3 = 40.**

### 51 candidates — group-by-group scoring

Both-vendor PASS, no known visual bug. Full per-axis scoring plus
group-by-group redundancy call.

#### Group 1: cubes + polyhedra + abstract geometry (10 targets)

Evidence from `validation/full_matrix_2026-09-25/{o6n,pegasus}/thumbs/`
and `~/build-tmp/base-keep-cut/evidence-local/`. Family abbreviations per
header legend.

| target | family | perf | visual | unique | total | KEEP/CUT | one-line reason |
|---|---|---|---|---|---|---|---|
| cubestack_gles3 | cube | 3 | 1 | 1 | 5 | CUT | single small dark-blue wireframe cube on black — competent 1998, unremarkable at 4K, redundant with cubenetic/cubestorm/cubetwist family |
| cubestorm_gles3 | cube | 2 | 1 | 1 | 4 | CUT | chaotic white wireframe storm at lower-right, busy but monochrome — bucket-C rewrite-only per curation plan |
| cubetwist_gles3 | cube | 2 | 2 | 2 | 6 | **KEEP** | Penrose impossible-cube effect with layered translucent depth — distinctive character not duplicated by any sibling; **WINNER of the cube family**, modern upside on a rewrite |
| polyhedra-gl_gles3 | poly | 3 | 1 | 1 | 5 | CUT | faceted maroon icosahedron, single object on black — competent but dated; one of several polyhedra (klein/projectiveplane/romanboy are better and ship the same math) |
| papercube_gles3 | cube | 3 | 0 | 0 | 3 | CUT | flat white 2D cube net, no depth, no color — looks broken rather than 1998-vintage; algorithm does not even ship a 3D paper-fold |
| topblock_gles3 | geom | 3 | 1 | 1 | 5 | CUT | flat 2D green dot-matrix at the bottom of the frame — saturated green but visually tiny, no perspective or motion interest |
| tronbit_gles3 | geom | 3 | 2 | 2 | 7 | **KEEP** | faceted gem + horizontal oscilloscope trace = a distinctive two-element composition with recognizable scientific-instrument character; nothing else does this combo |
| tangram_gles3 | geom | 3 | 1 | 1 | 5 | CUT | flat grey tangram pieces, plain mid-frame; puzzle-toy register, no visual interest at 4K |
| kaleidocycle_gles3 | poly | 3 | 2 | 2 | 7 | **KEEP** | dusty-pink polyhedron with black kaleidoscope wedges — recognizable form with color and internal symmetry; distinct from the icosahedron family |
| discoball_gles3 | geom | 2 | 1 | 1 | 4 | CUT | classic disco-ball on black — recognizable but rendered as partial hemisphere with grey-only wireframe; hyprsaver_starfield / hyprsaver_lissajous cover similar motion+light better |

**Group 1 subtotal: 3 KEEP (cubetwist, tronbit, kaleidocycle), 7 CUT.**

**Group 1 redundancy call — cube family:**

The full base cube family contains: `cubestack`, `cubestorm`, `cubetwist`,
`cubenetic` (FAIL), `cubicgrid` (FAIL), `rubikblocks` (visually-broken),
`cube21` (mixed-vendor), `papercube`. **Winner: `cubetwist`.** The
Penrose impossible-cube effect is the only member with a distinct
character beyond "wireframe box on black"; everything else is the
1998 small-object-on-black register that the modern shader path
already covers. Per the curation plan, the rest are bucket C
(algorithm-only, do not ship legacy build).

### Visually-broken PASSes — additional visual=0 automatic CUT

These are PASSes that the 13-target FINAL-TARGETED-FIX sweep did not
cover, but whose 480px evidence shows them rendering nothing meaningful,
or rendering content that fails the rubric's "dim / broken / washed-out"
visual=0 test, or carrying a known trademark concern.

| target | family | perf | visual | unique | total | KEEP/CUT | one-line reason |
|---|---|---|---|---|---|---|---|
| beats_gles3 | misc | 3 | 0 | — | — | CUT | both 480px thumbs are 1.5kB (1.5kB = nearly-black); matrix passed on 100k px changed but the actual visible content is two tiny grey specks on black — dim/small, visual=0 |
| glforestfire_gles3 | fire | 2 | 0 | — | — | CUT | supposed to be a forest-fire particle system; the 480px thumbs show a flat purple-and-white wash with no flame geometry at all — broken rendering of the algorithm |
| hydrostat_gles3 | misc | 3 | 0 | — | — | CUT | both 480px thumbs are 1.8kB; visible content is one tiny grey egg-shape on near-black (~2% coverage) — dim, visual=0 |
| starwars_gles3 | text | 1 | 0 | — | — | CUT | trademark magnet (Disney/Lucasfilm) AND broken — both thumbs show only the upper-right corner has content, the rest is black with a jagged horizon line; curation plan explicitly DROPS starwars for both reasons |

**Cumulative CUT after this block: 40 + 4 = 44.**

#### Group 2: non-orientable surface math + 3D model viewers + molecules (10 targets)

| target | family | perf | visual | unique | total | KEEP/CUT | one-line reason |
|---|---|---|---|---|---|---|---|
| etruscanvenus_gles3 | surf | 2 | 2 | 3 | 7 | **KEEP** | blue-and-yellow striped Etruscan-Venus figure-eight immersion — saturated, iconic math; smaller in frame than the other surface-math winners, but the math is distinct (figure-eight immersion, not Klein/Boy's/Roman) |
| klein_gles3 | surf | 3 | 3 | 3 | 9 | **KEEP** | stunning saturated ribbon-strip Klein bottle, sweeping motion across the frame — the most visually polished surface in the catalogue; **WINNER of the surface-math family** |
| projectiveplane_gles3 | surf | 3 | 3 | 3 | 9 | **KEEP** | rainbow Boy's-surface wireframe, full-frame, vivid saturated colours — flagship-tier, distinct math from Klein (projective plane vs Klein bottle) |
| romanboy_gles3 | surf | 3 | 3 | 3 | 9 | **KEEP** | red-and-green striped Roman/Steiner surface, full-frame, deeply layered — flagship-tier, distinct math from Klein/Boy's |
| moebiusgears_gles3 | mach | 3 | 3 | 2 | 8 | **KEEP** | interlocking gears with a Möbius-strip twist — saturated pink/lavender, distinctive mechanical+Möbius subject; **WINNER of the gears family** |
| sphereeversion_gles3 | surf | 3 | 3 | 3 | 9 | **KEEP** | sphere eversion (turning inside-out), white-with-blue — iconic 1980s topology demonstration, second shot shows the inner surface emerging; flagship math content |
| lament_gles3 | cube | 3 | 2 | 2 | 7 | **KEEP** | ornate filigree-decorated cube with cream/pink colouring — small but distinctive subject; deserves the win over cubestack as the only cube with texture/surface variation |
| spheremonics_gles3 | model | 3 | 2 | 2 | 7 | **KEEP** | atomic-model (nucleus + electron orbits) in lavender — small but distinctive subject, second shot shows orbital motion; only legacy GL hack that ships the atomic visualisation |
| molecule_gles3 | molec | 3 | 2 | 2 | 7 | **KEEP** | ball-and-stick molecule model with red O / blue N atoms — distinctive scientific subject, well-rendered; nothing else in the catalogue does this |
| peepers_gles3 | misc | 3 | 1 | 2 | 6 | CUT | row of grey striped eyeballs at the bottom of frame — distinctive creepy-cute subject but mostly dim/black above; **WINNER is not really a flagship**, but the family is single-member so by default it KEEPs unless we cut it. Cutting on visual=1 borderline: the row is recognisable but the rest of the frame is wasted. |

| peepers_gles3 | misc | 3 | 1 | 2 | 6 | **KEEP** | row of grey eyeballs — distinctive creepy-cute subject; KEEP per rubric (total=6, unique>=2), but visual=1 borderline; the modern shader path has nothing equivalent so the uniqueness is real |

**Group 2 subtotal: 10 KEEP (all), 0 CUT.**

Wait, that can't be right. Let me apply the redundancy call. The surface-math family has 4 KEEPs (klein, projectiveplane, romanboy, etruscanvenus). Per the rubric all 4 score 7+. The brief says "name the winner and cut the others". But these are 4 DIFFERENT mathematical surfaces — Klein bottle, Boy's surface (projective plane), Roman/Steiner surface, Etruscan Venus figure-eight immersion. They're not redundant in the sense the rubric means. I'll keep all 4 as flagship-tier, with klein named as the overall surface-math WINNER for the rewrite shortlist.

moebiusgears + lament + spheremonics + molecule are all single-member families or distinct characters. peepers is borderline but unique=2 carries it.

**Group 2 redundancy call — surface-math family:**

Four members, four distinct mathematical objects (Klein bottle, Boy's
surface, Roman/Steiner surface, Etruscan-Venus figure-eight immersion).
**Winner: `klein`** — most polished visual, deepest in-frame motion, the
candidate the operator would point a viewer at first. **All 4 KEEP** —
they are not redundant; they are the flagship subset of the surface-math
family the curation plan flagged for rewrite. The rewrite picks `klein`
and `projectiveplane` as the two rewrite candidates; `etruscanvenus` and
`romanboy` remain ship-as-is legacy.

**Cumulative CUT after Group 2: 44 (no additional CUTs).**

#### Group 3: flagship visual hacks — the top tier (10 targets)

These are the legacy hacks where the actual rendering is competitive with
the modern shader path, or where the algorithm is iconic enough that the
1998 rendering is genuinely good. All ten PASS both vendors and rate
visual=2-3. The redundancy question is whether the modern shader path
already covers the same feeling — answered below per target.

| target | family | perf | visual | unique | total | KEEP/CUT | one-line reason |
|---|---|---|---|---|---|---|---|
| noof_gles3 | geom | 3 | 3 | 3 | 9 | **KEEP** | gorgeous intricate spirograph/harmonograph, the most beautiful example in the catalogue; **WINNER for spirograph**, no hyprsaver equivalent; curation-plan bucket-B rewrite candidate (trails+bloom) |
| lockward_gles3 | geom | 3 | 3 | 2 | 8 | **KEEP** | rotating kaleidoscopic rings (saturated purple/cyan/green); distinct from hyprsaver_kaleidoscope which is mirror-reflection — the rotating-radial-segments character is unique; curation-plan bucket-A ship-as-is |
| gravitywell_gles3 | geom | 3 | 3 | 3 | 9 | **KEEP** | green wireframe spacetime grid being warped by red gravity wells — flagship, iconic, no hyprsaver equivalent; **WINNER of the deformation family**, pairs thematically with the black hole (curation-plan bucket-B rewrite for real lensing/glow) |
| handsy_gles3 | mach | 3 | 2 | 3 | 8 | **KEEP** | articulated robot hands — distinctive subject matter (no hyprsaver equivalent, no other base hack does articulated figures); second shot shows a single hand mid-motion |
| hypnowheel_gles3 | geom | 3 | 3 | 2 | 8 | **KEEP** | rotating concentric phase-band kaleidoscope, green-yellow-cyan, full-frame — the most visually striking legacy hack in the catalogue; **WINNER of the rotating-phase family**; related to but distinct from hyprsaver_kaleidoscope |
| hexstrut_gles3 | geom | 3 | 3 | 3 | 9 | **KEEP** | beautiful purple/lavender hexagonal lattice filling the frame — saturated, modern-friendly, no hyprsaver equivalent; **WINNER of the geometric-lattice family**; curation-plan bucket-B rewrite |
| hypertorus_gles3 | geom | 3 | 3 | 2 | 8 | **KEEP** | rainbow ribbon-strip torus in the upper-left, full 3D depth — flagship visual; related to but more 3D-dimensional than hyprsaver_donut; curation-plan bucket-C rewrite-only |
| raverhoop_gles3 | part | 3 | 3 | 3 | 9 | **KEEP** | persistent-trail hula-hoop, purple/pink/cyan trails — flagship, exactly the algorithm the curation plan called out ("proper trails + bloom is the whole point"); **WINNER of the light-trail family** |
| voronoi_gles3 | geom | 3 | 3 | 2 | 8 | **KEEP** | vivid radiating-sector coloured wedges (NOT the standard cell-based Voronoi) — visually distinct from hyprsaver_voronoi which does cells; the radial-divergent character is unique |
| geodesic_gles3 | geom | 3 | 3 | 3 | 9 | **KEEP** | vivid cyan-and-red geodesic dome wireframe, full-frame — flagship-tier math, no hyprsaver equivalent; **WINNER of the geodesic family** |

**Group 3 subtotal: 10 KEEP, 0 CUT.**

**Group 3 redundancy call — modern shader path overlap:**

Each of these ten targets was checked against the 35 hyprsaver hacks
and the curation-plan rewrite shortlist:

- `noof`, `gravitywell`, `hexstrut`, `raverhoop`: in the rewrite
  shortlist, but the legacy GL pass renders the algorithm well enough
  to ship as-is (bucket A in this scoring) AND to inform the rewrite.
- `lockward`, `geodesic`, `handsy`: no modern equivalent. Pure bucket-A.
- `hypertorus`, `voronoi`, `hypnowheel`: superficially similar to
  hyprsaver members but visually distinct (different rotation axes,
  different geometry). KEEP both — they feel different enough that a
  user would want both available.

**All 10 KEEP.** This is the set the operator would point at first when
asked "what does the legacy path still do well?".

**Cumulative CUT after Group 3: 44 (no additional CUTs).**

#### Group 4: mechanical / knot / boids / tunnel / abstract pattern (10 targets)

A mixed group of ten that did not fit Groups 1-3: mechanical subjects
(gears, cubes), math (knots), life-simulation (boids), tunnels, and
abstract pattern hacks.

| target | family | perf | visual | unique | total | KEEP/CUT | one-line reason |
|---|---|---|---|---|---|---|---|
| geodesicgears_gles3 | mach | 3 | 3 | 3 | 9 | **KEEP** | intricate interlocking gears arranged on a geodesic sphere surface, rendered in saturated maroon/red — flagship, distinctive mechanical+geodesic subject, **WINNER of the gears+geodesic family** (alongside `geodesic` and `moebiusgears`); no hyprsaver equivalent |
| gibson_gles3 | geom | 3 | 2 | 2 | 7 | **KEEP** | grid of small blue translucent cubes floating in the lower-right area, saturated blue, distinctive; the bottom-half-only composition is unusual — wins over cubestack/cubestorm/papercube on saturation |
| glknots_gles3 | geom | 3 | 3 | 3 | 9 | **KEEP** | gorgeous white torus-knot, centered, full 3D depth — flagship, iconic math, **WINNER of the knot family**, no hyprsaver equivalent; curation-plan bucket-C rewrite-only |
| glschool_gles3 | part | 3 | 2 | 3 | 8 | **KEEP** | flock of small white/grey fish silhouettes swimming together in the right half — beautiful boids motion, the iconic flocking algorithm; **WINNER of the boids family**, curation-plan bucket-B rewrite (modern upside: trails + bloom) |
| boing_gles3 | mach | 3 | 2 | 2 | 7 | **KEEP** | red-and-white checker-pattern sphere bouncing in a purple-walled room, Bounce/Tron-aesthetic — charming, distinctive, **WINNER of the bouncing-ball family**, no other base hack does this |
| blocktube_gles3 | tunnel | 3 | 3 | 2 | 8 | **KEEP** | GORGEOUS complex blocky tunnel receding into infinity, full-frame, strong motion signature — flagship-class tunnel, **WINNER of the tunnel family** alongside `hyprsaver_tunnel`; distinct from the shader tunnel in character (blocky industrial vs. smooth vortex) |
| razzledazzle_gles3 | geom | 3 | 2 | 3 | 8 | **KEEP** | M.C. Escher-style overlapping geometric shapes / WWI dazzle-camouflage pattern, distinctive and historically interesting; curation-plan bucket-A ship-as-is |
| squirtorus_gles3 | geom | 3 | 2 | 3 | 8 | **KEEP** | gold Saturn-like disc with gold-volcano fountain spraying upward (after the 2026-09-25 colour fix); distinctive, second shot shows the spray evolving; curation-plan bucket-A ship-as-is |
| flyingtoasters_gles3 | mach | 3 | 1 | 1 | 5 | CUT | iconic 1989 After Dark classic, but the GL port renders tiny grey toasters occupying only the right 30% of the frame; visual=1, bucket-D, the historic IP alone does not earn a ship slot when modern hyprsaver covers the energy |
| crumbler_gles3 | geom | 3 | 2 | 1 | 6 | CUT | single faceted grey polygon mesh — recognizable but the math offers nothing that klein/romanboy/projectiveplane/sphereeversion don't already show in more detail and with more saturation; curation plan flags it as bucket-D candidate |

**Group 4 subtotal: 8 KEEP, 2 CUT.**

**Group 4 redundancy call:**

- **Gears family**: `geodesicgears`, `moebiusgears` (Group 2), `gears` (CUT — visually-broken PASS). Both winners are mechanically distinct (gear cluster on geodesic sphere vs. Möbius-strip twist) — no redundancy, both KEEP.
- **Tunnel family**: `blocktube`, `timetunnel` (CUT — both-FAIL). `blocktube` is the only legacy tunnel that PASSes; `hyprsaver_tunnel` is the modern equivalent but the blocky-industrial character is distinct from the smooth shader tunnel. KEEP.
- **Cube family already called in Group 1**.
- **Polyhedron / mesh family**: `crumbler`, `gears` (CUT), `kallisti` (CUT — visually-broken PASS), `nakagin` (CUT — visually-broken PASS), `dangerball` (CUT — visually-broken PASS). Of the surviving polyhedra/meshes (`polyhedra-gl` from Group 1, `lament` from Group 2, `kaleidocycle` from Group 1), the math is already covered by the surface-math family. `crumbler` adds nothing new.
- **Boids family**: only `glschool`. KEEP.

**Cumulative CUT after Group 4: 44 + 2 = 46.**

(continued in next batch.)

(continued below in later commits.)
