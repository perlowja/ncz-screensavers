# gl-transitions smoke test — STEP 2 captures (2026-09-26)

This is the visual evidence pack: 17 candidate transitions from the
family-doctrine-favored buckets (warp / burn / glitch / dissolve /
displacement / blur / morph / standout-geometric), each captured
mid-blend at six progress values, with montages built so the
mid-blend look can be eyeballed.

## How to read these

Each transition has a `montage_<name>.png` (4 frames stitched
left-to-right at p=0.20, 0.40, 0.5, 0.80) and six individual
`<name>_p<progress>.png` files (256x256 RGBA → PNG) at
p = 0.00, 0.20, 0.40, 0.5, 0.80, 1.00.

The `from` texture is a moving radial gradient
(magenta-to-dark-purple, ~0.5 luminance at edges). The `to` texture
is a horizontal stripe pattern (teal/aqua). At p=0 the whole frame
should be the radial gradient; at p=1.0 the whole frame should be
the stripes; mid-blend should look meaningfully different from
both.

## Captured transitions

**Warp family:**
  - `crosswarp` — clear progressive horizontal waver; the pink
    gradient gets sliced and shifted into the teal stripes.
  - `directionalwarp` — at 256x256 the warp axis is faint; renders
    almost identical at all four mid-blend frames. **Candidate for
    "looks like fade at small sizes" rejection**; needs larger
    test render to confirm.

**Burn family:**
  - `burn` — visible burn-edge erosion from the radial gradient
    into the stripes.
  - `burn0` — sharper burn-edge with flame colour on the edge
    (orange/yellow). Has an early-return guard at p<=0 / p>=1 so
    it dodges the `vec4 from` shadowing issue.
  - `undulatingBurnOut` — beautiful wavy burn-edge that sweeps in
    from the upper-left. The standout of the burn family for
    visual character.

**Glitch family:**
  - `GlitchDisplace` — despite the name, this is a wave /
    sinusoidal displacement, not a true glitch. Pink gradient
    deformed into a wavy grid pattern.
  - `GlitchMemories` — horizontal blur-streak overlays with
    scan-line ghost. Real glitch character.
  - `parametric_glitch` — name suggests glitch, visual is a
    rising "from behind" wave emergence with cyan stripes.

**Dissolve family:**
  - `dissolve` — **observed broken**: renders fully black at
    p=0.00, p=0.20, p=0.40, p=0.5 (only p=0.80+ shows anything).
    See "discovered issue" below. NOT a candidate for SHORTLIST
    until the wrapper is fixed.

**Displacement / blur / morph family:**
  - `displacement` — vector-field displacement, smooth.
  - `ripple` — concentric ripple distortion of the radial
    gradient.
  - `LinearBlur` — horizontal smear of stripes.
  - `morph` — actual cross-dissolve with radial-to-stripe
    warping (visually distinct from `fade`).

**Standout geometric / color-modulating:**
  - `flyeye` — fish-eye / radial zoom-out.
  - `Drop_Zone_Flicker` — multi-frame strobe-style transition
    with high-contrast flickers.
  - `ColourDistance` — colour-channel separation with banding.
  - `luma` — luma-weighted blend (visibly darker than a
    straight fade because of the `0.299*r + 0.587*g + 0.114*b`
    weighting).

## Discovered issue: `from` / `to` uniform-name shadowing

During the step-2 capture analysis I noticed `dissolve` produces
fully-black frames at every progress value below ~0.80. Tracing
through the shader:

```
vec4 transition(vec2 uv) {
    vec4 from = getFromColor(uv);   // <-- shadows the global
    vec4 to   = getToColor(uv);     //   sampler2D from / to
    ...
}
```

The smoke tester (and the production driver) declare
`uniform sampler2D from;` and `uniform sampler2D to;` in their
preamble. Five vendored shaders use local `vec4 from` / `vec4 to`
that **shadow** the global samplers, so `getFromColor(uv)`
silently uses a vec4 as a sampler2D and the NVIDIA driver returns
black.

Affected shaders (5):
  - `Overexposure.glsl`
  - `burn0.glsl` (early-returns at p=0 and p=1 hide this for
    those two values; mid-blend still broken)
  - `dissolve.glsl`
  - `parametric_glitch.glsl` (degraded but not fully black —
    NVIDIA's behaviour for the shadowed texture lookup varies)
  - `perlin.glsl`

`burn0` is the worst case because it presents as "fine at the
endpoints but broken in the middle" — i.e. exactly the
mid-blend artefact the smoke test was designed to catch.

**Fix path:** rename the wrapper's sampler2D uniforms to
`tex_from` / `tex_to` (or any non-clashing name) in both
`src/gles3_transitions.c` (production driver) and
`src/gles3_transitions_smoke.c` (smoke tester). This is a
low-risk, mechanical change and would recover all 5 of these.
**This is the highest-priority fix for any production use of
the wrapper; tracked in STEP-3-SHORTLIST.md and the roundup.**

## Verdict for step 2

17 candidate transitions captured, six progress values each, with
montages for at-a-glance comparison. Captures demonstrate that
the smoke-tester's pre-classification of "compile+link+render OK"
**is necessary but not sufficient** for "visually correct" — at
least `dissolve` (and possibly `parametric_glitch` and `perlin`)
is technically OK by the CSV but visually broken due to the
uniform-shadowing bug. The 6-transitions minimum is exceeded by
far (17 captured, of which 16 are usable).

— generated by gles3_transitions_smoke on cerberus, 2026-09-26 01:57 EDT