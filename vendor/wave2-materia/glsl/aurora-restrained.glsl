// aurora-restrained.glsl
// wave2 — quiet materiality: restrained aurora.
//
// Algorithm:
//   1. Two broad curtains. Each curtain is a soft horizontal band
//      in y, with intensity peaking along a noise-driven curve.
//      The curtains leave dark gaps between them — the dark sky is
//      visible across ~40% of the frame.
//   2. Fine vertical structure within each curtain: a per-x
//      intensity modulation so the curtain has visible striations,
//      like the real aurora's "curtain folds".
//   3. Slow lateral folds: the curtain curves drift in y on a
//      slow noise trajectory. The motion is minutes-scale, not
//      seconds.
//   4. Restrained palette: cool green dominant with a hint of
//      magenta at the bottom edge of each curtain. NO spectral
//      complexity — no rainbow gradients.
//
// "Restraint matters more than spectral complexity. Full-frame
// green noise is just another plasma." (brief)
// Strategy for restraint:
//   - dark sky base (linear ~0.02) so the curtains are highlights
//     on dark, not the whole frame being colour
//   - two curtains only, leaving dark gaps
//   - green dominant with one secondary hue, not full spectrum
//   - tonemap crushes any remaining brightness to the top of the
//     green band, not into pure white

#define PI 3.14159265359

// ---- Hash / value noise ----
float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm3(vec2 p) {
    float s = 0.0;
    float a = 0.5;
    mat2 rot = mat2(0.8, -0.6, 0.6, 0.8);
    for (int i = 0; i < 3; i++) {
        s += a * vnoise(p);
        p = rot * p * 2.07;
        a *= 0.5;
    }
    return s;
}

// Returns the intensity of curtain `i` at world position uv.
// A curtain is a soft horizontal band whose centre line is a
// noise-modulated curve. The intensity at uv depends on:
//   - vertical distance from the curtain's centre curve
//   - per-x fine striations
float curtain(vec2 uv, float t, float seed) {
    // Curtain centre curve: a slow noise that drifts the curtain
    // centre in y as a function of x. Drifts in time as well.
    vec2 cn = vec2(seed * 5.3, seed * 3.1);
    float drift1 = fbm3(uv * 0.6 + cn + vec2(t * 0.05, 0.0)) - 0.5;
    float drift2 = fbm3(uv * 1.4 + cn + vec2(0.0, t * 0.07)) - 0.5;
    float centre = 0.20 * drift1 + 0.10 * drift2; // gentle

    // Vertical distance from the curtain centre, normalised to the
    // curtain's half-thickness.
    float dy = (uv.y - centre) / 0.18; // curtain half-thickness = 0.18
    // The curtain intensity: full at the centre, fading out at the
    // edges. Use a Gaussian-like falloff (1 - smoothstep).
    float band = 1.0 - smoothstep(0.0, 1.0, abs(dy));

    // Fine vertical striations: per-x intensity modulation so the
    // curtain has visible folds. The striations vary along the
    // curtain (so it doesn't look like a uniform bar).
    float stripes = 0.7 + 0.30 * vnoise(vec2(uv.x * 18.0 + seed, t * 0.3));

    // The intensity is band * stripes. The stripes modulate
    // brightness, not position, so the curtain stays at its centre
    // but brightens and dims along x.
    return band * stripes;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 res = iResolution.xy;
    vec2 uv = (fragCoord - 0.5 * res) / min(res.x, res.y);
    float t = iTime;

    // ---- Dark sky base ----
    // A very faint deep-night gradient — slightly lighter at the
    // horizon (bottom of frame) than at the zenith.
    vec3 sky = mix(vec3(0.012, 0.015, 0.025),
                   vec3(0.005, 0.008, 0.015),
                   smoothstep(-0.5, 0.5, uv.y));

    // ---- Curtains ----
    // Two curtains at different y positions, both green-dominant
    // with hints of secondary colours. Each curtain has its own
    // drift trajectory.
    //
    // We add a per-anchor y offset to push the second curtain up.
    // The curtain() function centres around uv.y=0; we sample at
    // uv.y shifted by the offset, so the second curtain sits in
    // the upper third of the frame.
    float c1 = curtain(uv, t, 0.0);
    float c2 = curtain(vec2(uv.x, uv.y - 0.45), t, 1.3) * 0.85;

    // ---- Per-curtain colour ----
    // Curtain 1: cool green dominant with a hint of pink at the
    // bottom edge.
    vec3 greenCore = vec3(0.20, 0.85, 0.45);
    vec3 greenEdge = vec3(0.45, 0.30, 0.60);
    // Bottom-edge weight: stronger at the bottom of the curtain.
    vec3 col1 = mix(greenCore, greenEdge, smoothstep(0.0, -0.15, uv.y - 0.0) * 0.6);

    // Curtain 2: warmer green with a slight blue shift at top.
    vec3 col2 = vec3(0.18, 0.70, 0.55);

    vec3 col = sky + col1 * c1 * 1.30 + col2 * c2 * 1.20;

    // ---- Subtle vertical ray structure ----
    // Very fine vertical rays, mostly in the curtain regions. This
    // gives the "fine vertical structure" the brief asks for — the
    // rays appear as bright vertical streaks within each curtain.
    float rays = 0.0;
    {
        // A high-frequency vertical pattern that only contributes
        // where curtains are present.
        float r = 0.5 + 0.5 * sin(uv.x * 60.0 + t * 0.6);
        r *= 0.5 + 0.5 * sin(uv.x * 23.0 + t * 0.4 + 2.3);
        // Gate by curtain presence.
        float gate = smoothstep(0.05, 0.4, c1 + c2);
        rays = r * gate * 0.20;
    }
    col += rays * vec3(0.30, 0.85, 0.55);

    // ---- A few distant stars ----
    // Tiny static pinpricks of light — the brief asks for the dark
    // sky to be visible, and a few stars read as "real night sky".
    float stars = step(0.998, hash21(floor(uv * 300.0))) * 0.5;
    col += stars * vec3(0.8, 0.85, 0.95);

    // ---- Vignette ----
    // Slightly darker corners (where the curtains rarely reach).
    float vig = 1.0 - 0.18 * dot(uv, uv);
    col *= vig;

    // ---- Tonemap (ACES Narkowicz) + gamma ----
    // Pre-tonemap exposure boost so the curtains read as visible
    // green light. ACES protects the brightest rays from clipping
    // to white — they stay green.
    col *= 1.20;
    vec3 a = col * (2.51 * col + 0.03);
    vec3 b = col * (2.43 * col + 0.59) + 0.14;
    vec3 mapped = clamp(a / b, 0.0, 1.0);
    fragColor = vec4(pow(mapped, vec3(1.0 / 2.2)), 1.0);
}