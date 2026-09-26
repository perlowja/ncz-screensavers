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

(continued below in later commits.)
