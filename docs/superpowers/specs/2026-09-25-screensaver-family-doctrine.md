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
