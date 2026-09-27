# jurassicaquarium -- a prehistoric sea, rendered at the waterline

Design doc. Status: approved in conversation 2026-09-27, not yet implemented.
Supersedes nothing. Builds on `2026-09-27-wayshade-fidelity-tiers-design.md`.

## What it is

A wayshade piece: the camera sits half-submerged at the surface of a
prehistoric sea and drifts. The frame is split by the water itself -- sky and
weather above, the water column and seafloor below -- and creatures cross that
boundary.

It cycles through four geological acts. Each act is a different sea: different
cast, different water chemistry, different light.

| Act | Era | Approx | Cast |
|---|---|---|---|
| I | Cambrian | 510 Ma | Anomalocaris, Opabinia, trilobites, algal mats |
| II | Devonian | 380 Ma | Dunkleosteus, Cladoselache, early crinoid beds |
| III | Jurassic | 150 Ma | Ichthyosaurs, plesiosaurs, ammonites, belemnites |
| IV | Cretaceous | 75 Ma | Mosasaur (breaching), Archelon, Xiphactinus, ammonites |

It ends at the Cretaceous. There is deliberately no modern act.

NAMING: the piece spans Cambrian to Cretaceous. "Jurassic" in the name is doing
colloquial duty for "prehistoric" -- the same license Jurassic Park took, given
that T. rex, Velociraptor and Triceratops are all Cretaceous. The scope is four
acts, not one period.

### Why it ends there

Two reasons, both load-bearing.

Every creature in the piece is a RECONSTRUCTION. Nobody has seen a mosasaur, so
a stylized one reads as paleoart. A stylized dolphin reads as cheap, because
the audience knows exactly what a dolphin looks like. Excluding the present
keeps the whole cast on the defensible side of that line.

It also removes the last external-asset dependency. A modern act would have
wanted real animal models; the prehistoric cast is entirely procedural (below),
so `jurassicaquarium` ships with no third-party geometry and no inherited licence.

## Visual reference

Robert Lyn Nelson's "Two Worlds" split-level marine paintings -- the
composition (waterline bisecting the frame, creatures crossing it), the density,
and the saturation. NOT the cast, and not a reproduction of any specific work.
Nelson is a living artist with a distinctive style; this piece borrows the
compositional idea, which is normal practice, and must remain recognizably its
own work.

## Architecture

The camera is at the surface, half-submerged. That is the whole idea, and it is
non-negotiable -- it is what makes the split read as one composition instead of
two stacked pictures. Consequence: the split is NOT a screen-space horizontal
line. It is a real plane seen from a camera that bobs, so it tilts and curves as
waves pass.

Three passes:

| Pass | Contents | Target |
|---|---|---|
| 1 | sky, weather, distant land, above-water creatures, spray | FBO-above |
| 2 | water volume, godrays, seafloor, submerged creatures, particulate | FBO-below |
| 3 | water surface geometry; samples both FBOs, does refraction + Fresnel + foam | screen |

Two FBOs plus a composite. Doing it single-pass with a world-space Y test gives
a hard line and loses the refraction that sells the boundary.

RISK, UNMEASURED: two 1080p FBOs cost real bandwidth on an iGPU. Our geometry is
light so this is expected to hold, but it has not been measured. If it does not
hold, the fallback is the hard-line single-pass split -- which looks worse, and
should be reported as a regression rather than shipped quietly.

## Water and light

Surface: sum of Gerstner waves, 4 at low tier, 8 at high. Normals from the
analytic derivative, not finite differences -- we do not pay for evaluations we
can derive (the ridgeline lesson).

Boundary: Schlick Fresnel, F0 ~= 0.02. Sky reflection above the crossing line,
refracted volume below. Foam band where wave curvature exceeds a threshold.

### The underwater colour model, and the deliberate lie in it

Physically correct is Beer-Lambert extinction with per-channel coefficients: red
dies within metres, then green, leaving blue. Do only that and the result is a
realistic ocean, which is muddy and desaturated, and looks nothing like the
reference.

The model is therefore:

    extinction         Beer-Lambert, per channel    (structure, depth cue)
  + ambient scatter    lifts the floor              (kills the mud)
  + saturation bias    INCREASES with depth         (deliberately anti-physical)

The third term is what makes it glow. It is recorded here explicitly so that a
later reader does not "fix" it as a bug. It is the point.

HOW FAR to push that bias is a judgement call that needs eyes on real output.
Ship it tunable with a conservative default; do not pick a number blind.

Per-act water chemistry is grounded, not arbitrary: Cretaceous seas were warmer
and coccolithophore-rich, so chalkier and milkier; Cambrian shallower and
greener. Acts differ in extinction coefficients, scatter, and sun angle.

Caustics: animated pattern projected down, applied to seafloor AND creature
bodies, attenuated by depth. Not raytraced.

Particulate: instanced points, parallax by depth, tier-gated count.

## Creature generation

No third-party models. Four generators and one animator cover the whole cast.

### G1 -- swept generalized cylinder

Spine: Catmull-Rom through control points. At each station `t`, a superellipse
cross-section with `width(t)`, `height(t)`, exponent `n(t)`. Swept with a
PARALLEL-TRANSPORT frame, not Frenet -- Frenet flips on straight sections.
Capped at both ends.

This produces every vertebrate body and every fin. Lateral compression toward
the tail is `n(t)` and the w/h ratio varying along `t` -- a real anatomical
control.

### G2 -- Raup coil

Ammonites, nautiloids. Raup's 1966 morphospace parameters:

    W   whorl expansion rate
    D   distance from coiling axis
    T   translation along axis
    S   cross-section shape

Four numbers generate essentially every real coiled shell. Ribs are periodic
displacement along theta. This is the parameterization palaeontologists actually
use, which is why it is preferred over an ad-hoc spiral.

### G3 -- segmented repeater

Trilobites, anomalocarids. N repeated units, each a swept arc, with taper and
per-segment scale. Axial lobe plus pleural lobes.

### G4 -- branching L-system

Crinoids, corals, algal mats. Depth 2-3. Animated by gentle current sway.

### The animator -- one travelling wave

    lateral(t, time) = A(t) * sin(2*pi*(t/lambda - f*time))

The amplitude envelope `A(t)` alone distinguishes the real named locomotion
modes from fish biomechanics:

| Mode | A(t) | Cast |
|---|---|---|
| Anguilliform | large across most of the body | eels, elongate forms |
| Carangiform | rises in the rear third | most fish, Xiphactinus |
| Thunniform | concentrated at peduncle and fluke | ichthyosaur, mosasaur |

Swimming style is therefore a PARAMETER, not a separate animation. A second flag
sets the axis: lateral for fish and mosasaurs, vertical for any secondarily
aquatic tetrapod that swims that way. Plesiosaurs are the exception -- they are
underwater FLIERS, four-flipper oscillation, not axial undulation; they get
appendage-phase animation instead of spine displacement.

Anomalocarids use the same travelling wave applied to appendage phase rather
than the spine, producing the metachronal wave down the swim flaps.

### Cost

Meshes are generated CPU-side once at load into VBOs. The wave deforms the spine
in the VERTEX SHADER, so animation is effectively free and LOD is just station
and ring counts -- which wires directly into the fidelity tiers.

### Where procedural genuinely fails

Heads. A mosasaur skull with teeth, Dunkleosteus's bony plates, and
Anomalocaris's grasping appendages are detail-dense and are not swept forms.

Honest answer: procedural bodies plus a SMALL set of hand-authored head meshes,
a few hundred polys each, authored by us. This is a hybrid, not pure procedural,
and should not be described as pure procedural.

### ArtistPack

A creature is a parameter file. That makes creatures authorable content without
anyone modelling geometry, which is a natural fit for ArtistPack as a content
type. Not a Phase 1 dependency.

## Fidelity tiers

Wires into `2026-09-27-wayshade-fidelity-tiers-design.md`.

| Element | low -> extra-high |
|---|---|
| Render scale | 0.375 -> 1.0 |
| Gerstner waves | 4 -> 8 |
| Godray steps | 8 -> 32 |
| Caustics | off -> projected -> projected + chromatic |
| Particulate | sparse -> dense |
| Creature stations/rings | coarse -> fine |
| Spray particles | low -> high |

## Phasing

PHASE 1 -- one act (Cretaceous), engine complete.
  Waterline surface, refraction, Fresnel, foam. Sky and weather above.
  Volume extinction + scatter, godrays, caustics, particulate.
  G1 and G2 generators. Mosasaur, ammonites, seafloor silhouette.
  The breach: surface-crossing event, spray burst, wet-specular decay.
  All tier knobs.

PHASE 2 -- remaining three acts, G3 and G4, act transitions, seafloor density.

Cretaceous first because it carries the breach, which is the piece's hardest
technical requirement and its best shot. If the breach does not work, that is
worth finding out in Phase 1, not Phase 2.

## Verification

Reuse the existing matrix harness (`validation/full_matrix_*`): per-host logs
and thumbs across o6n / cerberus / pegasus. Frame counts on at least two GPU
classes.

Appearance sign-off requires a real test host. As of 2026-09-27 PEGASUS and
MEDUSA are both being reflashed, and CERBERUS's RTX 4500 proves nothing about
the iGPU target -- ridgeline measured 0.4 fps on Intel UHD versus 30 fps on an
RTX 2060 in the same harness. Do not accept a discrete-GPU frame count as
evidence the piece is shippable.

## Open items

1. PALEO ACCURACY -- how hard to chase current scientific consensus.
   Reconstructions move; mosasaur tail flukes were contested until recently.
   Aiming at consensus is a real differentiator for a piece called `jurassicaquarium`,
   but it is a commitment. OPERATOR DECISION, not made.
2. Two-FBO bandwidth on an iGPU -- unmeasured, see Architecture.
3. Saturation bias magnitude -- needs eyes on real output, blocked on a test
   host.
4. Repo licence conflict: root says Apache-2.0, `meson.build:27` says
   GPL-2.0-or-later. `jurassicaquarium` no longer depends on the outcome (it is
   clean-room -- no SGI/xlockmore derivation, no third-party geometry), but the
   conflict is real and still wants resolving for the repo generally. Relevant
   to wayshade's stated Windows/Mac portability goal.

## Rejected alternatives, and why

PORT ATLANTIS'S CREATURES. `shark.c`, `whale.c`, `dolphin.c` are 3,018 lines of
immediate-mode vertex literals, animated as rotating segment chains, and they
already swim. Rejected because they are SGI/xlockmore-derived: porting them, or
`swim.c`, makes wayshade a derivative work and inherits terms we would rather
not carry into a cross-platform binary. `jurassicaquarium` is clean-room instead.

CC0 ASSET PACKS. Quaternius's Animated Fish Bundle (CC0, rigged, GLB, with swim
animations) is a genuinely clean source for MODERN marine animals and remains a
good option for any future piece that needs them. It does not cover the
Mesozoic. Surveyed 2026-09-27: CC0 rigged prehistoric marine reptiles are
essentially unavailable -- the marketplaces carrying mosasaurs and plesiosaurs
are paid or CC-BY-NC, and the one CC0 prehistoric source found (Meshy) is
AI-generated, which sits badly against ArtistPack's C2PA provenance discipline.

MUSEUM SCANS. Smithsonian Open Access has CC0 3D scans, including a fossil whale
(MPC 677). These are SKELETONS -- static, unrigged. Turning bones into a fleshed
swimming animal is paleoart, not asset reuse.
