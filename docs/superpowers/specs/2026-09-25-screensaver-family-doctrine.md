# The screensaver family — each piece is a distinct feeling

**Date:** 2026-09-25
**Status:** operator doctrine. Governs every new piece in this catalogue.

Operator: *"Each screensaver is a distinct feeling."*

The catalogue is not a set of variations on one house style. Each piece
targets **one specific emotional register** and commits to it. A viewer
who sees two of ours back to back should feel that they went somewhere
different, not that a parameter changed.

This is the top-level constraint. When a design decision is ambiguous,
resolve it toward the piece's own feeling — including when that means
diverging from what a sibling piece does.

## The register of each piece

| Piece | Feeling | Form discipline |
|---|---|---|
| `neonspacewar` | **Nostalgia and spectacle.** An arcade hall you remember but never visited; watching a battle you are not playing. Smiling before you know why. | **Strict: vector indulgence.** Gratuitous linework, dense wireframe hulls, phosphor persistence. Raster accents only as arena furniture. No source identifiable. |
| `genxvectorcade` | **Wonder and hallucination.** An artificial world that evolves, never repeats, and is never fully comprehensible. A drug trip with a living ecology underneath. | **None. Anything goes.** Every technique available, mixed freely, pushed to excess. |
| `magmasimplex` | **Warmth and hypnosis.** Slow, organic, molten. Something to fall into rather than watch. | Fullscreen implicit-surface field, no object framing. Psychedelic multicolour. |
| `blackhole` | **Cosmic awe and dread.** Scale that dwarfs you. Silence and gravity. | Physically-motivated lensing and accretion. Restraint is correct here. |

## What this rules out

- **House-style creep.** Do not apply one piece's signature treatment to
  another because it looked good. Shared *infrastructure* (stroke model,
  phosphor decay, quality ladder, platform abstraction) is encouraged;
  shared *identity* is not.
- **Hedging.** A piece that tries to be two feelings at once achieves
  neither. If a design serves the register, keep it even if it is
  extreme; if it dilutes the register, cut it even if it is impressive.
- **Uniform intensity.** Not every piece is maximal. Restraint is the
  right answer for some registers and the failure mode for others — the
  table above says which.

## How to check a piece against this

Before shipping, state in one sentence what the piece makes a viewer
feel, without describing its techniques. If that sentence could equally
describe a sibling piece, the piece has not differentiated itself yet.

---

## Performance doctrine — the discrete GPU is the target; the era's iGPU is the floor

Operator, 2026-09-25: *"These machines have discrete and Intel iGPU of
that time should be viewed as the floor."*

This corrects an inverted assumption that had crept into several briefs,
where "under 33 ms on Intel UHD" was treated as a hard gate that every
design decision had to satisfy. That framing **lets the weakest hardware
dictate the algorithm**, and it was already pushing us toward cheaper,
worse-looking approaches on pieces whose whole purpose is to look
extraordinary.

### The correct framing

| Role | Hardware | Expectation |
|---|---|---|
| **Reference target** | 2019-era discrete mobile — NVIDIA RTX 2060, AMD Navi14 RX 5500M | The piece runs **in full** — every effect, full resolution, smooth. Design here. |
| **Headroom** | Newer discrete (RTX 4500 ADA, RTX 5060 and up) | Room to push further. Never the baseline. |
| **Floor** | Intel UHD CML GT2 and contemporaries | Must run, must look good, may run **reduced**. Not the design constraint. |

These machines have both. The discrete GPU is what a user who cares will
be running the screensaver on. The iGPU is what must not be *excluded*.

### What this changes in practice

- **Choose the algorithm that looks best on the reference target.** Do
  not reject a volumetric simulation, a dense line budget or an expensive
  optical model because an iGPU would struggle. Pick it, then build an
  honest reduced tier for the floor.
- **The floor tier is a real shipping tier, not a stub.** It runs, it is
  stable, it is visually coherent and recognisably the same piece. It is
  allowed to have fewer entities, a coarser grid, lower internal
  resolution, simpler optics or a lower frame rate.
- **A screensaver is ambient.** Sustained 30 fps is comfortable; 20-25 fps
  on the floor tier is acceptable where the motion is slow and the piece
  is soft. What is never acceptable is **stutter, hitching or variable
  frame pacing** — an uneven 30 fps looks worse than a steady 22. Measure
  frame-time variance, not only the mean.
- **Frame-rate targets are per-tier**, and the floor's target is
  "smooth and alive", not a number inherited from the reference target.

### What this does not change

Effort still goes into making the floor tier good. "It only has to run on
the iGPU" is not licence to ship something ugly there — a reduced tier
that looks cheap is a failure, just a different one. The test is whether
a viewer on the floor tier would be happy with what they see, not whether
they would notice it is reduced.

### Reporting

Report per tier, naming the GPU: mean frame time, frame-time variance or
1% low, and the parameters behind each number (grid resolution, segment
count, march steps, entity counts). State plainly which tier you consider
the reference and whether it holds up.

### Correction: the reference target is 2016-era discrete, not 2019

Operator, same day: *"2016 GPUs can handle this."* Correct, and the
reference target moves down accordingly.

| Role | Hardware | Expectation |
|---|---|---|
| **Reference target** | **2016-era discrete — NVIDIA Pascal (GTX 1060 and up), AMD Polaris (RX 480 and up)** | The piece runs in full. Design here. |
| Also in scope | CIX Sky1 Mali-G720 (the NCZ-OS arm64 boards) | Must run well; verify rather than assume, it is a different architecture with different bottlenecks |
| Headroom | 2019+ discrete and newer | Room to push. Never the baseline. |
| **Floor** | Intel UHD CML GT2 and contemporaries | Must run, reduced tier permitted |

A GTX 1060 is roughly 4.4 TFLOPS FP32 with full GL 4.5 and Vulkan. A 64³
fluid simulation with a volumetric raymarch is not close to its limit.
**Do not design defensively against hardware of this class** — it is not
the constraint, and treating it as one produces timid work.

The practical consequence: choose the ambitious technique. A volumetric
voxel simulation, a dense line budget, expensive optics — a Pascal or
Polaris part handles all of it. Reserve tier reductions for the Intel
floor, and stop trimming the design to protect hardware that does not
need protecting.

**Note on the arm64 boards.** NCZ-OS ships on CIX Sky1 with Mali-G720,
which is the primary platform for this distribution rather than an
afterthought. It is a capable part but a different architecture —
tile-based deferred rendering, different bandwidth and different
behaviour under heavy fragment work. Do not extrapolate its performance
from the x86 numbers in either direction. Measure it when the hardware is
available, and say so explicitly if it was not.

---

# The catalogue's purpose — art and entertainment, never repeating

Operator, 2026-09-26:

> *"I happen to like visualizations that have an aspect of randomization in them, these
> should be art creations that are entertainment. We want NCZ to be the linux distribution
> that even takes its screensavers seriously."*

This is the north star for the whole catalogue and it settles a real tension in the
design advice we have been given.

## Randomisation is a REQUIREMENT, not a feature

**Every piece must be different every time it runs.** A screensaver the viewer has
already seen is a screensaver they stop looking at. This is not decoration on top of a
fixed design — it is part of what the piece IS.

What that means concretely, and what it rules out:

- Per-launch randomisation of the things that determine CHARACTER, not just surface: the
  palette, the structure, the composition, the behaviour. Randomising only a hue and
  calling it varied is the failure mode.
- Where the piece has a generator — a trajectory, a genome, a roster, a weather system —
  **draw its parameters, do not script it.** Four hardcoded choreographies with three
  scalars varying is not randomisation; it is four shots at different scales. This exact
  defect was found and fixed in `blackhole`.
- Prefer parameterisations where one drawn number produces genuinely different results
  over ones where it produces the same result louder. The zoom-whirl `q` is the model:
  rational gives a closed rosette, irrational gives a path that never repeats.
- Log the draw. Every piece prints a `[diag]` line with its parameters so a striking run
  can be reproduced and a dull one diagnosed.

## Entertainment, not wallpaper

A consultation (Astra, 2026-09-26) argued the catalogue's missing category is **quiet
materiality** — things that receive light rather than emit it — and recommended judging
each piece by whether it "remains pleasant beside ordinary desk work". Its craft advice is
excellent and stands. **Its register does not.**

Optimising for *pleasant beside desk work* produces tasteful wallpaper. The stated goal is
**art creations that are entertainment** — pieces worth actually watching, which is a
higher and riskier bar than not being annoying.

So the catalogue needs BOTH, and should not pretend one is the other:

| register | purpose | examples |
|---|---|---|
| **showpiece** | rewards attention; makes someone stop and watch | `leviathan`, `genxvectorcade`, `blackhole`, `neonspacewar` |
| **quiet material** | rewards peripheral vision; ambient | caustics, satin, foliage shadows, frosted glass, thin film |

Astra's refuse-list includes "endless tunnels, hyperspace travel" and "prefer a stable
camera" — which would rule out `leviathan` entirely. That advice is correct FOR THE QUIET
REGISTER and wrong for the showpieces. A catalogue that is all showpieces is exhausting; a
catalogue with none is forgettable. **Judge each piece against its own register**, exactly
as the family doctrine already requires.

## What survives from the craft advice regardless of register

These are quality, not style, and apply to everything:

- **Tonemap the output.** Untonemapped linear colour is the commonest reason a
  technically-correct shader looks amateur. But tonemapping is the END of a discipline,
  not a rescue: decide where highlights fall and what stays subdued first.
- **Separate motion scales** — small movements over seconds, regional change over tens of
  seconds, broad change over minutes, asynchronously. One global sine controlling
  everything reads as a metronome.
- **Keep quiet regions.** Filling the frame does not mean filling every pixel with detail.
- **Filter for motion.** Detail that looks excellent in a still can crawl and sparkle.
  Tonemapping does not fix aliasing.
- **Refuse rainbow-as-default-mapping** of every scalar, kaleidoscopic symmetry used to
  manufacture complexity, noise added to hide weak large-scale composition, and arbitrary
  chromatic aberration / scanlines / film grain.
- **Cost is real:** 4K is about 8.3 million fragments per frame. "Only another thirty
  iterations" is consequential.

## The standard

The distribution's claim is that it takes screensavers seriously. That is measured by
whether a stranger who knows nothing about any of this keeps one switched on — and, for
the showpieces, whether they watch it twice and notice it was different the second time.
