# Legacy screensaver keep/cut rubric

**Date:** 2026-09-25
**Status:** operator-directed. *"Determine which legacy screensavers we
should keep based on performance, visual impact and uniqueness."*

This exists so the keep/cut decision is **repeatable and evidence-based**
rather than a matter of whoever last looked at a screenshot. Every legacy
target gets scored on all three axes. Score first, decide second.

Scope: the **legacy** families only — `base` (92), `rss-sdl2` (13),
`shadertoy` (38). The native pieces (`magmasimplex`, `genxvectorcade`,
`neonspacewar`) and `hyprsaver` (35/35 passing) are not under review.

---

## Axis 1 — Performance

Measured, not estimated. Per
`2026-09-25-screensaver-family-doctrine.md`: **reference target is
2016-era discrete** (Pascal/Polaris class); **Intel UHD is the floor**.

| Score | Meaning |
|---|---|
| 3 | Comfortable on the Intel floor (< 33 ms) with no hitching |
| 2 | Runs on the floor but reduced or 20-30 ms; smooth on discrete |
| 1 | Discrete only; floor tier stutters or is visibly degraded |
| 0 | Does not run, or hitches on discrete |

Report **mean frame time AND variance / 1% low**, both GPUs. A ragged
30 fps scores worse than a steady 22 — frame pacing is part of this axis,
not a footnote.

## Axis 2 — Visual impact

Three measured proxies plus a judgement, because **no single metric has
survived contact with this catalogue**:

- **Coverage** — non-black pixel %. Catches `lavalite` (4%, dim object on
  black).
- **Saturation** — % of pixels with meaningful chroma. Catches the
  grey/pastel failures that coverage alone passed.
- **Motion** — inter-frame changed-pixel % across a capture spread.
  Catches static and near-static renders.

Then the judgement, which overrides the metrics in both directions:

| Score | Meaning |
|---|---|
| 3 | Genuinely beautiful at 4K. Would ship as a flagship example. |
| 2 | Attractive, holds attention, no embarrassment |
| 1 | Competent but dated — the 1998 small-object-on-black register |
| 0 | Ugly, broken, washed out, or dim |

**Metrics inform, they do not decide.** `magmasimplex` scored 100%
coverage and 99% saturation while looking like matte plastic; the gate
passed `mapscroller` on both vendors while it rendered nothing at all.
Look at the images.

## Axis 3 — Uniqueness

The axis most likely to be skipped, and the one that most improves the
catalogue. **A hack is only as valuable as what it adds that nothing else
provides.**

| Score | Meaning |
|---|---|
| 3 | Nothing else in the catalogue does this |
| 2 | Related to others but a clearly distinct character |
| 1 | One of several similar; keep the best, cut the rest |
| 0 | Redundant — another hack does this and does it better |

Group by algorithm family and compare *within* the group: non-orientable
surface maths, space-filling fractals, cube/polyhedra variations, particle
fire, plasma, tunnels. Where a group has several members, **name the
winner and cut the others** — that judgement is the deliverable, not a
list of scores.

Count `hyprsaver` (35) and the native pieces when assessing redundancy: a
legacy hack that duplicates something the modern shader path already does
better is redundant even though it is not under review itself.

---

## Decision rule

Total 0-9.

| Total | Action |
|---|---|
| 7-9 | **KEEP** — ships |
| 5-6 | **KEEP** if uniqueness >= 2, else cut |
| 3-4 | **CUT** unless uniqueness = 3 and a cheap fix reaches 5 |
| 0-2 | **CUT** |

**Any axis scoring 0 is an automatic CUT**, whatever the total. A
beautiful hack that does not run is not a hack; a unique one that is ugly
is not worth shipping.

Fix-before-cut applies only where the root cause is known and shallow and
the hack would score >= 6 fixed. Do not open-ended-debug a hack to save
it — that is what produced 32 both-vendor failures nobody will ever fix.

## Output

One row per target: `target | family | perf | visual | unique | total |
KEEP/CUT | one-line reason`. Then the group-by-group redundancy calls, and
a final count: how many of each family survive.

State the honest total. If the catalogue drops from 104 working to 60,
that is a better catalogue and should be reported as a win, not softened.

---

## AMENDMENT 2026-09-26 — uniqueness alone may not cut

Operator: *"If the 12 redundancy survive on the first 2 criteria keep
them."*

**A low uniqueness score is no longer sufficient grounds for a CUT.** A
target that scores non-zero on **performance** and non-zero on **visual
impact** is KEEP, regardless of how redundant it is with a sibling.

Revised decision rule:

| Condition | Action |
|---|---|
| perf = 0 **or** visual = 0 | **CUT** (unchanged — any axis at 0 is automatic) |
| perf > 0 **and** visual > 0 | **KEEP** |
| uniqueness | informational only — no longer decides |

Uniqueness stays in the scoring because it is genuinely useful: it names
which member of a family leads, and that ordering feeds the rewrite
shortlist for the new engine. But it **ranks**, it no longer **removes**.

**Rationale.** A hack that runs well and looks good is worth shipping even
if a sibling does something similar. The cost of keeping it is a line in a
chooser; the cost of cutting it is losing work that already passes the two
criteria that actually matter to a viewer. Nobody watching a screensaver
is harmed by there being two good takes on cubes.

Cuts made on grounds **outside** the three axes — subject matter, legal
risk, trademark — are unaffected by this amendment and remain in force.
They are editorial decisions, not rubric outcomes, and must be recorded
with their actual reason rather than dressed as a low score.
