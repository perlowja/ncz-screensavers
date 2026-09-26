**2. Launch with two families. Make a third earn admission.**

Without the actual 79-piece inventory, exact redundancy judgments would be invented. Based on the coverage you describe, this is my cut:

| Family | Decision | Reason |
|---|---|---|
| Parameterized surfaces and 4D projections | **Launch** | Strong candidate for sculptural form, changing occlusion, and color attached to geometry rather than screen coordinates. |
| Iterated-orbit density images | **Launch conditionally** | Include de Jong/Clifford-style maps and a small published flame variation vocabulary under one rendering family. Earn inclusion through branching, ribbons, voids, and density structure absent from your existing fractals. |
| Descartes circle packings | **Prototype, then judge** | Potentially adds crisp hierarchical composition. Reject if it becomes another decorative zoom. |
| Markus–Lyapunov fields | **Defer** | Another parameter-space fractal image is a weak marginal addition unless its temporal behavior is exceptional. |
| Rankine-vortex imagery | **Exclude initially** | Given existing fluids, plasma, and tunnels, the formula alone does not justify another piece. |
| Boids and n-body | **Defer to a simulation project** | Distinctive behavior is possible, but neighborhood interactions, stability, and choreography deserve their own budget. |
| DLA | **Defer** | Growth, occupancy queries, startup, and eventual visual saturation are substantial problems for this launch. |
| More plasma, Voronoi, tunnels, generic escape-time fractals | **Exclude** | Already represented. |

The surface group should not become four independent implementations merely because four old pieces existed. Also, “non-orientable / 4D” is an aesthetic grouping, not a single mathematical category. Share infrastructure where appropriate without pretending all members have the same topology.

For packing, the published mathematics supports recursive generation from mutually tangent circles. Generate bounded geometry and rasterize it; avoid testing every circle at every pixel. [Graham et al., *Apollonian Circle Packings: Number Theory*](https://arxiv.org/abs/math/0009113)

My admission test: show anonymous clips alongside the existing catalogue, including grayscale versions. If the candidate contributes only another palette, cut it.

**3. The genome should be a conditional grammar, with randomness concentrated in meaningful decisions.**

Conceptually:

```text
version + root seed
family
    composition archetype
    structural parameters
    evolution rules
    material and palette relationships
    camera constraints
    permitted post-processing
quality budget + acceptance-profile version
```

Separate random streams for structure, color, motion, and sampling. Changing render resolution must not silently generate a different composition. Record the resolved genome as well as the seed; generator changes otherwise destroy reproducibility.

Draw an archetype first, then parameters conditioned on it. An orbit family might have “paired lobes,” “filament fan,” and “nested voids,” each with its own parameter distributions and framing rules. These are proposed compositional categories, not assertions that arbitrary coefficients will produce them.

Do not sample all coefficients uniformly and hope a fitness function supplies taste.

Also, fitness screening is not what defines the flame algorithm. Its foundational ingredients include nonlinear transformations, density rendering, and structural coloring; selection is a separate layer you must design. [Draves and Reckase, *The Fractal Flame Algorithm*](https://flam3.com/flame_draves.pdf)

Use three acceptance stages:

1. **Cheap mathematical rejection.** Invalid values, divergent trajectories, collapsed variance, degenerate geometry, dangerous projection denominators.
2. **Small render probes.** As a starting experiment, try four candidates at roughly 192×108 and several temporal checkpoints. Measure spatial distribution, clipped highlights, concentration of edges, luminance range, and change over time.
3. **Family-specific acceptance.** A surface needs readable folds and depth. An orbit density needs coherent structure with useful voids. A packing needs hierarchy across scales.

Those dimensions should be constraints and acceptance bands, not one weighted score to maximize. Maximizing entropy rewards noise; maximizing coverage rewards filling the screen; maximizing saturation rewards exhausting color.

Low-resolution probes cannot reliably detect fine aliasing or certify temporal safety. Stateful families must actually advance their state to test later behavior.

Bound startup work. Display a previously validated composition while testing fresh candidates, then transition when one passes. Keep a varied fallback bank and record fallback frequency: frequent fallback means the generator is failing, even if viewers never see a blank frame.

**4. Your coverage figures identify a presentation choice, not a demonstrated defect.**

Sixty percent black can give a sculpture scale, contrast, and legibility. Ninety percent black may be timid framing—or deliberate composition. “1997 presentation” is an aesthetic judgment your metrics do not establish.

Likewise, low hue entropy does not prove cityflow is less successful. It establishes a narrower hue distribution. Vivid monochrome can be excellent art.

Measure **where structure is, how it changes, and whether the eye has somewhere to go**. Coverage alone cannot distinguish a beautifully balanced small subject from an accidental speck.

For surfaces, my first fix would be a change in presentation grammar:

- Let some compositions cross the frame boundaries while preserving recognizable folds.
- Sample views for meaningful overlap and depth, rather than defaulting to a centered object rotating in empty space.
- Attach color to intrinsic surface coordinates or geometric properties, so color explains the form.
- Use a restrained background related to the same geometry when the composition needs it.
- Permit a few related instances only when scale and placement create a deliberate hierarchy.

I would **not** begin with volumetric accumulation. It adds cost and can obscure the topology you selected the family to reveal. Temporal trails can similarly turn a legible surface into a luminous smear.

For orbit images, density accumulation is native to the subject. Use robust orbit bounds, outlier handling, multiscale density reconstruction, and restrained tone mapping. Fix inadequate sampling before enlarging every point or adding bloom.

For packings, filled regions and nested material relationships can provide area naturally. Making every circle outline thicker is not the same thing.

Every family needs its own answer to occupied space. That is another reason the genome cannot treat composition as an interchangeable axis.

**5. The most likely failure is that the engine generates novelty but cannot sustain interest.**

A good random still is much easier than a good twenty-minute experience. Parameter drift can cross a bifurcation, collapse a structure, or become restless morphing. A stable structure with a slowly moving camera may be more watchable, despite containing periodic motion.

Give every family an explicit temporal contract: what persists, what changes, at what rate, and how an exhausted or invalid composition is replaced. Keep structural parameters fixed within a passage unless their evolution has been tested. Fresh randomness need not mean constant structural instability.

Then validate **sequences across many seeds**, not selected screenshots. Include startup, long dwell, transitions, frame drops, different aspect ratios, and recovery. Track perceptually similar outputs; a huge seed space can still produce five recognizable pictures.

Two additional requirements deserve tightening:

- **Your flash rule is too narrow if “full-field” and “sustained” are exemptions.** Localized regions and brief sequences can matter. Evaluate opposing luminance transitions and saturated-red transitions across rolling windows, including the final composited output. Whole-frame averages can hide local flashing. Use published spatial and temporal thresholds as a reference, while recognizing their display/viewing assumptions; startup probes are not certification. [W3C flash guidance](https://www.w3.org/WAI/WCAG22/Understanding/three-flashes-or-below-threshold.html)
- **“Intel UHD” is not a performance specification.** Name a floor machine, resolution, frame-rate target, and sustained power budget. Verify framebuffer formats and blending capabilities explicitly; floating-point texture support does not establish every render-target capability you need. [Khronos floating-point color-buffer specification](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_color_buffer_float.txt)

For clean-room work, require a short derivation record per family: publication, equations, numerical choices, and tests. An unfamiliar display name is branding, not provenance. Keep lock-screen rendering free of text, external imagery, and desktop capture; put diagnostic seeds and labels outside that mode.

I would greenlight Afterimage with **two family directors, shared infrastructure, and a seed-replay capture harness**. Its first milestone should be convincing long-running clips from dozens of unselected seeds on the floor machine. Twelve mathematical families would be a much weaker milestone.
tokens used
18,712
I agree with one product and one generative framework. I disagree with **the Cartesian-product genome, coverage as a quality target, and “no loops” as the definition of generative art.** Those premises could produce a very configurable engine whose output all looks like the same glowing noise.

The useful promise is: **each run has a materially different composition and a coherent visual identity that survives prolonged viewing.** Random coefficients, more occupied pixels, and nonrepeating motion do not establish that.

**1. Families should own composition and evolution, not just cameras and post-processing.**

Your mitigation is necessary but insufficient. A camera cannot rescue an attractor collapsed onto a point; post-processing cannot give a static packing meaningful temporal development.

I would separate three things:

- **Shared host:** seed management, capability detection, resource budgets, scheduling, output handling, safety enforcement, reproducibility.
- **Family director:** valid parameter generation, composition, camera, material, temporal evolution, acceptance tests, recovery behavior.
- **Rendering primitives:** fullscreen fields, parametric meshes, point accumulation, optional stateful simulation.

A family supplies a bounded render graph using those primitives. Do not make every family implement the same fullscreen fragment function. Do not build an unrestricted node editor either.

This distinction matters immediately for your strongest candidate: surfaces. A parameterized mesh with vertex transformations and fragment shading is a sensible GLES3 implementation. Forcing it into ray marching would spend the floor-tier budget on an architectural preference.

Likewise, per-agent state does **not inherently require ping-pong FBOs**: GLES3 also has transform feedback. That does not make flocking cheap, but state storage and interaction cost are separate problems. [Khronos GLES3 reference](https://www.khronos.org/files/opengles3-quick-reference-card.pdf)

The genome should describe an artistic decision within a family’s language. Camera and post-chain are conditional consequences of that decision, with bounded variation.

**2. Launch with two families. Make a third earn admission.**

Without the actual 79-piece inventory, exact redundancy judgments would be invented. Based on the coverage you describe, this is my cut:

| Family | Decision | Reason |
|---|---|---|
| Parameterized surfaces and 4D projections | **Launch** | Strong candidate for sculptural form, changing occlusion, and color attached to geometry rather than screen coordinates. |
| Iterated-orbit density images | **Launch conditionally** | Include de Jong/Clifford-style maps and a small published flame variation vocabulary under one rendering family. Earn inclusion through branching, ribbons, voids, and density structure absent from your existing fractals. |
| Descartes circle packings | **Prototype, then judge** | Potentially adds crisp hierarchical composition. Reject if it becomes another decorative zoom. |
| Markus–Lyapunov fields | **Defer** | Another parameter-space fractal image is a weak marginal addition unless its temporal behavior is exceptional. |
| Rankine-vortex imagery | **Exclude initially** | Given existing fluids, plasma, and tunnels, the formula alone does not justify another piece. |
| Boids and n-body | **Defer to a simulation project** | Distinctive behavior is possible, but neighborhood interactions, stability, and choreography deserve their own budget. |
| DLA | **Defer** | Growth, occupancy queries, startup, and eventual visual saturation are substantial problems for this launch. |
| More plasma, Voronoi, tunnels, generic escape-time fractals | **Exclude** | Already represented. |

The surface group should not become four independent implementations merely because four old pieces existed. Also, “non-orientable / 4D” is an aesthetic grouping, not a single mathematical category. Share infrastructure where appropriate without pretending all members have the same topology.

For packing, the published mathematics supports recursive generation from mutually tangent circles. Generate bounded geometry and rasterize it; avoid testing every circle at every pixel. [Graham et al., *Apollonian Circle Packings: Number Theory*](https://arxiv.org/abs/math/0009113)

My admission test: show anonymous clips alongside the existing catalogue, including grayscale versions. If the candidate contributes only another palette, cut it.

**3. The genome should be a conditional grammar, with randomness concentrated in meaningful decisions.**

Conceptually:

```text
version + root seed
family
    composition archetype
    structural parameters
    evolution rules
    material and palette relationships
    camera constraints
    permitted post-processing
quality budget + acceptance-profile version
```

Separate random streams for structure, color, motion, and sampling. Changing render resolution must not silently generate a different composition. Record the resolved genome as well as the seed; generator changes otherwise destroy reproducibility.

Draw an archetype first, then parameters conditioned on it. An orbit family might have “paired lobes,” “filament fan,” and “nested voids,” each with its own parameter distributions and framing rules. These are proposed compositional categories, not assertions that arbitrary coefficients will produce them.

Do not sample all coefficients uniformly and hope a fitness function supplies taste.

Also, fitness screening is not what defines the flame algorithm. Its foundational ingredients include nonlinear transformations, density rendering, and structural coloring; selection is a separate layer you must design. [Draves and Reckase, *The Fractal Flame Algorithm*](https://flam3.com/flame_draves.pdf)

Use three acceptance stages:

1. **Cheap mathematical rejection.** Invalid values, divergent trajectories, collapsed variance, degenerate geometry, dangerous projection denominators.
2. **Small render probes.** As a starting experiment, try four candidates at roughly 192×108 and several temporal checkpoints. Measure spatial distribution, clipped highlights, concentration of edges, luminance range, and change over time.
3. **Family-specific acceptance.** A surface needs readable folds and depth. An orbit density needs coherent structure with useful voids. A packing needs hierarchy across scales.

Those dimensions should be constraints and acceptance bands, not one weighted score to maximize. Maximizing entropy rewards noise; maximizing coverage rewards filling the screen; maximizing saturation rewards exhausting color.

Low-resolution probes cannot reliably detect fine aliasing or certify temporal safety. Stateful families must actually advance their state to test later behavior.

Bound startup work. Display a previously validated composition while testing fresh candidates, then transition when one passes. Keep a varied fallback bank and record fallback frequency: frequent fallback means the generator is failing, even if viewers never see a blank frame.

**4. Your coverage figures identify a presentation choice, not a demonstrated defect.**

Sixty percent black can give a sculpture scale, contrast, and legibility. Ninety percent black may be timid framing—or deliberate composition. “1997 presentation” is an aesthetic judgment your metrics do not establish.

Likewise, low hue entropy does not prove cityflow is less successful. It establishes a narrower hue distribution. Vivid monochrome can be excellent art.

Measure **where structure is, how it changes, and whether the eye has somewhere to go**. Coverage alone cannot distinguish a beautifully balanced small subject from an accidental speck.

For surfaces, my first fix would be a change in presentation grammar:

- Let some compositions cross the frame boundaries while preserving recognizable folds.
- Sample views for meaningful overlap and depth, rather than defaulting to a centered object rotating in empty space.
- Attach color to intrinsic surface coordinates or geometric properties, so color explains the form.
- Use a restrained background related to the same geometry when the composition needs it.
- Permit a few related instances only when scale and placement create a deliberate hierarchy.

I would **not** begin with volumetric accumulation. It adds cost and can obscure the topology you selected the family to reveal. Temporal trails can similarly turn a legible surface into a luminous smear.

For orbit images, density accumulation is native to the subject. Use robust orbit bounds, outlier handling, multiscale density reconstruction, and restrained tone mapping. Fix inadequate sampling before enlarging every point or adding bloom.

For packings, filled regions and nested material relationships can provide area naturally. Making every circle outline thicker is not the same thing.

Every family needs its own answer to occupied space. That is another reason the genome cannot treat composition as an interchangeable axis.

**5. The most likely failure is that the engine generates novelty but cannot sustain interest.**

A good random still is much easier than a good twenty-minute experience. Parameter drift can cross a bifurcation, collapse a structure, or become restless morphing. A stable structure with a slowly moving camera may be more watchable, despite containing periodic motion.

Give every family an explicit temporal contract: what persists, what changes, at what rate, and how an exhausted or invalid composition is replaced. Keep structural parameters fixed within a passage unless their evolution has been tested. Fresh randomness need not mean constant structural instability.

Then validate **sequences across many seeds**, not selected screenshots. Include startup, long dwell, transitions, frame drops, different aspect ratios, and recovery. Track perceptually similar outputs; a huge seed space can still produce five recognizable pictures.

Two additional requirements deserve tightening:

- **Your flash rule is too narrow if “full-field” and “sustained” are exemptions.** Localized regions and brief sequences can matter. Evaluate opposing luminance transitions and saturated-red transitions across rolling windows, including the final composited output. Whole-frame averages can hide local flashing. Use published spatial and temporal thresholds as a reference, while recognizing their display/viewing assumptions; startup probes are not certification. [W3C flash guidance](https://www.w3.org/WAI/WCAG22/Understanding/three-flashes-or-below-threshold.html)
- **“Intel UHD” is not a performance specification.** Name a floor machine, resolution, frame-rate target, and sustained power budget. Verify framebuffer formats and blending capabilities explicitly; floating-point texture support does not establish every render-target capability you need. [Khronos floating-point color-buffer specification](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_color_buffer_float.txt)

For clean-room work, require a short derivation record per family: publication, equations, numerical choices, and tests. An unfamiliar display name is branding, not provenance. Keep lock-screen rendering free of text, external imagery, and desktop capture; put diagnostic seeds and labels outside that mode.

I would greenlight Afterimage with **two family directors, shared infrastructure, and a seed-replay capture harness**. Its first milestone should be convincing long-running clips from dozens of unselected seeds on the floor machine. Twelve mathematical families would be a much weaker milestone.
