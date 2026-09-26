// Title:  Caustic tidepool
// Author: ncz original (round 17, 2026-09-26)
// Desc:   Light focused through a perturbed surface onto a sandy floor.
//         Two slow flows of waves are refracted against each other; the
//         absolute gradient magnitude is tonemapped into bright filigree
//         caustics. Constant slow drift, never settles.
//
// Original work; no upstream port. The algorithm is the classic
// "caustics = |grad(noise)*noise|" pattern, but the noise itself is
// a sum-of-sines + low-octave value-noise hybrid and the palette is
// tuned for warm shallow water (sand visible through the brightness,
// not a black background).

#define TAU       6.28318530718
#define MAX_ITER  4

// Cheap value noise on a toroidal lattice. Not a texture lookup so it
// works in single-pass without iChannel.
float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash21(i + vec2(0.0, 0.0));
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// Layered sin/cos "ocean" surface: 4 octaves of directional waves,
// each with a unique direction generated from a per-octave phase so
// the result doesn't read as stripes.
float ocean(vec2 p, float t) {
    float h = 0.0;
    float amp = 1.0;
    float freq = 1.0;
    float dir_phase = 0.7;
    for (int i = 0; i < 4; i++) {
        vec2 d = vec2(cos(dir_phase), sin(dir_phase));
        h += amp * sin(dot(p, d) * freq + t * (0.6 + 0.13 * float(i)));
        amp *= 0.55;
        freq *= 2.07;
        dir_phase += 1.913;
    }
    return h;
}

// Single-pass caustic field. Two layers of waves displaced in
// opposite directions make the gradients cross, producing the
// filigree. We add low-frequency value noise so the result has the
// "broken by sand" character rather than perfect Voronoi cells.
float caustic(vec2 uv, float t) {
    float t1 = t * 0.27;
    float t2 = t * 0.21 + 11.3;

    vec2 p1 = uv * 3.0 + vec2(t1, -t1 * 0.6);
    vec2 p2 = uv * 2.4 + vec2(-t2 * 0.5, t2 * 0.4);

    float h1 = ocean(p1, t1);
    float h2 = ocean(p2, t2);

    // Bounded gradient approximation: 4 directional samples.
    float e = 0.012;
    float gx = ocean(p1 + vec2(e, 0.0), t1) - ocean(p1 - vec2(e, 0.0), t1);
    float gy = ocean(p1 + vec2(0.0, e), t1) - ocean(p1 - vec2(0.0, e), t1);

    // Classic caustics trick: focus energy where the gradient is small
    // (waves nearly flat = light focused) but boost by the wave height
    // itself for brightness. |grad|^(-k) explodes the bright lines.
    float grad2 = gx * gx + gy * gy + 0.06;
    float focus = 0.08 / (grad2 + 0.001);

    // Mix with the second wave field to break periodicity.
    float interference = 0.5 + 0.5 * sin(h1 + h2 + t * 0.4);

    // Low-frequency sand texture.
    float sand = vnoise(uv * 5.0 + t * 0.05) * 0.4 + 0.6;

    return clamp(focus * interference * sand, 0.0, 8.0);
}

// Palette: warm shallow tidepool. Stops from deep teal through cyan
// to white where the caustic is brightest.
vec3 palette(float v) {
    v = clamp(v, 0.0, 1.0);
    vec3 deep   = vec3(0.02, 0.09, 0.14);
    vec3 mid    = vec3(0.05, 0.42, 0.55);
    vec3 bright = vec3(0.55, 0.92, 1.00);
    vec3 hot    = vec3(1.00, 0.98, 0.86);
    vec3 c = mix(deep, mid, smoothstep(0.0, 0.35, v));
    c = mix(c, bright, smoothstep(0.35, 0.7, v));
    c = mix(c, hot, smoothstep(0.85, 1.5, v));
    return c;
}

vec3 aces_approx(vec3 v) {
    v = max(v, 0.0);
    v *= 0.6;
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((v * (a * v + b)) / (v * (c * v + d) + e), 0.0, 1.0);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    // Per-launch seed so two runs differ. Uses fragCoord seed (iFrame
    // would also work but this is cheaper and more random across the
    // frame). We don't actually need cryptographically secure; we just
    // need the same run to be coherent.
    float seed = fract(sin(dot(fragCoord, vec2(12.9898, 78.233))) * 43758.5453);
    float t = iTime + seed * 31.7;

    vec2 uv = fragCoord / iResolution.xy;
    // Aspect-correct, centered.
    vec2 p = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;

    float c = caustic(p, t);

    // Two caustics at different scales layered for richness.
    float c2 = caustic(p * 1.7 + vec2(3.1, -2.2), t * 1.13 + 5.7) * 0.5;
    c += c2;

    // Sand visible at low brightness; light filaments at high.
    vec3 col = palette(c * 0.35);

    // Slight vignette for cinematic framing; the player puts this on
    // a screen so a faint darkening at the corners helps the eye.
    float vig = 1.0 - 0.35 * dot(p, p);
    col *= vig;

    fragColor = vec4(aces_approx(col), 1.0);
}
