// frosted-glass.glsl
// wave2 — quiet materiality: frosted / fluted glass with light behind.
//
// Algorithm:
//   1. A few broad soft-coloured light sources (4 blobs in distinct
//      hues) sit behind the glass. They drift slowly on noise-driven
//      trajectories, so the light pattern is always shifting but
//      never "moving".
//   2. The glass itself is fluted (vertical ridges that refract
//      light horizontally) + frosted (roughness that scatters
//      light). The combination stretches and compresses the sources
//      without showing sharp contours.
//   3. Sampling: the renderer walks back through the glass surface
//      by perturbing its UV with the glass normal + jitter, then
//      samples the background (sources). Multiple jittered samples
//      give the frosted look; the normal's x-component drives the
//      fluted stretching.
//
// Tonemap (ACES) + gamma. Luminous without looking emissive or neon.

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
    for (int i = 0; i < 3; i++) {
        s += a * vnoise(p);
        p *= 2.07;
        a *= 0.5;
    }
    return s;
}

// Background colour: the light sources behind the glass. Returns
// the cumulative radiance at uv, summed across the 4 sources. Each
// source is a soft coloured radial blob.
vec3 background(vec2 uv, float t) {
    vec3 col = vec3(0.0);

    // Source 1: warm peach, top-left area.
    {
        vec2 c = vec2(-0.5, 0.35) + vec2(fbm3(vec2(t * 0.05, 0.0)),
                                         fbm3(vec2(t * 0.06, 1.7))) * 0.4 - 0.2;
        float d = length((uv - c) * vec2(1.0, 1.3));
        col += vec3(0.85, 0.55, 0.35) * (1.0 - smoothstep(0.0, 0.7, d)) * 0.95;
    }
    // Source 2: cool cyan, bottom-right.
    {
        vec2 c = vec2(0.45, -0.30) + vec2(fbm3(vec2(t * 0.07, 2.0)),
                                          fbm3(vec2(t * 0.05, 3.7))) * 0.4 - 0.2;
        float d = length((uv - c) * vec2(1.0, 1.3));
        col += vec3(0.30, 0.55, 0.75) * (1.0 - smoothstep(0.0, 0.8, d)) * 0.85;
    }
    // Source 3: pale yellow, centre, slightly above.
    {
        vec2 c = vec2(0.10, 0.05) + vec2(fbm3(vec2(t * 0.04, 4.0)),
                                         fbm3(vec2(t * 0.05, 5.7))) * 0.5 - 0.25;
        float d = length((uv - c) * vec2(1.1, 1.0));
        col += vec3(0.95, 0.85, 0.50) * (1.0 - smoothstep(0.0, 0.65, d)) * 0.80;
    }
    // Source 4: violet, top-right.
    {
        vec2 c = vec2(0.55, 0.40) + vec2(fbm3(vec2(t * 0.06, 6.0)),
                                          fbm3(vec2(t * 0.04, 7.7))) * 0.4 - 0.2;
        float d = length((uv - c) * vec2(1.0, 1.2));
        col += vec3(0.50, 0.40, 0.65) * (1.0 - smoothstep(0.0, 0.55, d)) * 0.70;
    }

    // A faint warm haze base — so unilluminated regions aren't pure
    // black. This is what gives the glass its luminous-but-not-neon
    // character: a soft warm wash underneath everything.
    col += vec3(0.18, 0.14, 0.12);

    return col;
}

// Glass surface height: fluted vertical ridges + frosted roughness.
// Returns (height, derivative_x) so we can compute the refraction
// shift directly.
void glass(vec2 uv, out float h, out float dhdx) {
    // Fluting: vertical ridges. Period ~0.06 in uv-x (so the frame
    // shows ~30 ridges across [-1.78, 1.78] = ~60 cycles). Tiny
    // temporal drift — the ridges are essentially static.
    float ridges = sin(uv.x * 90.0) * 0.5 + 0.5;
    // Smooth the ridges a bit so they're not razor-sharp.
    ridges = pow(ridges, 1.3);

    // Frosted: a low-frequency noise modulating the glass thickness
    // and a higher-frequency fine roughness. Both very subtle —
    // contributes to the "scattering" feel without being visible.
    float frost_lo = fbm3(uv * 2.5) * 0.3;
    float frost_hi = vnoise(uv * 35.0) * 0.05;
    h = ridges * 0.7 + frost_lo + frost_hi;

    // Derivative wrt x: ridges dominate.
    dhdx = cos(uv.x * 90.0) * 90.0 * 0.7 * 0.5
         + (fbm3(vec2(uv.x + 0.01, uv.y) * 2.5) - fbm3(vec2(uv.x - 0.01, uv.y) * 2.5)) * 50.0 * 0.3;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 res = iResolution.xy;
    vec2 uv = (fragCoord - 0.5 * res) / min(res.x, res.y);
    float t = iTime;

    // ---- Glass surface ----
    float h, dhdx;
    glass(uv, h, dhdx);

    // Refraction shift: the surface normal tilts in the +x
    // direction in proportion to dhdx. Light from behind is
    // refracted horizontally — exactly what fluted glass does.
    vec2 refr = vec2(-dhdx * 0.015, 0.0);

    // ---- Multi-tap frosted sample ----
    // We sample the background at the refracted UV plus a jittered
    // ring around it. 8 samples; each is shifted by a small random
    // offset. This gives the frosted-scattering look without going
    // to full resolution loss.
    const int NSAMP = 8;
    vec3 col = vec3(0.0);
    for (int i = 0; i < NSAMP; i++) {
        float fi = float(i);
        float ang = fi * 0.785398; // 2*pi / 8
        float rad = 0.012 + 0.008 * hash21(vec2(fi, 9.0));
        vec2 jitt = vec2(cos(ang), sin(ang)) * rad;
        // Per-sample refraction: combine the deterministic refracted
        // UV with the jittered frosted offset.
        vec2 sample_uv = uv + refr + jitt;
        col += background(sample_uv, t);
    }
    col /= float(NSAMP);

    // ---- Glass surface tint ----
    // A faint cool cast from the glass material itself (real glass
    // absorbs a tiny bit of red). And a subtle vertical "fluted"
    // gradient — the ridges slightly darken the channels between
    // them.
    col *= vec3(0.96, 0.99, 1.04);
    col *= 0.92 + 0.08 * h;

    // ---- Vignette ----
    float vig = 1.0 - 0.18 * dot(uv, uv);
    col *= vig;

    // ---- Tonemap (ACES Narkowicz) + gamma ----
    vec3 a = col * (2.51 * col + 0.03);
    vec3 b = col * (2.43 * col + 0.59) + 0.14;
    vec3 mapped = clamp(a / b, 0.0, 1.0);
    fragColor = vec4(pow(mapped, vec3(1.0 / 2.2)), 1.0);
}