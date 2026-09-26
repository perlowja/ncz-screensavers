// aurora.glsl — vertical aurora curtains.
//
// Aurora "curtains" — vertical sheets of emission with height-
// dependent intensity, modulated by a slowly drifting
// horizontal flow field. Strong colour (greens, pinks, blues),
// calm motion, very cold-looking.
//
// The brief: "Vertical curtains with height-dependent emission
// and slow lateral drift. Strong colour, very calm."
//
// Technique: a 2D fbm flow field advected over time drives the
// curtain positions. The emission at each (x, y) is the
// accumulation of two sine-curtains with different frequencies
// and phases, modulated by the flow field. Height-dependent
// falloff (exponential) gives the "curtain hanging from the
// sky" reading.
//
// Original work, MIT. No vendor shadertoy reference used.

#define PI 3.14159265359

vec3 hash3(vec3 p) {
    p = vec3(dot(p, vec3(127.1, 311.7, 74.7)),
             dot(p, vec3(269.5, 183.3, 246.1)),
             dot(p, vec3(113.5, 271.9, 124.6)));
    return -1.0 + 2.0 * fract(sin(p) * 43758.5453123);
}
float vnoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(dot(hash3(i + vec3(0,0,0)), f - vec3(0,0,0)),
                       dot(hash3(i + vec3(1,0,0)), f - vec3(1,0,0)), u.x),
                   mix(dot(hash3(i + vec3(0,1,0)), f - vec3(0,1,0)),
                       dot(hash3(i + vec3(1,1,0)), f - vec3(1,1,0)), u.x), u.y),
               mix(mix(dot(hash3(i + vec3(0,0,1)), f - vec3(0,0,1)),
                       dot(hash3(i + vec3(1,0,1)), f - vec3(1,0,1)), u.x),
                   mix(dot(hash3(i + vec3(0,1,1)), f - vec3(0,1,1)),
                       dot(hash3(i + vec3(1,1,1)), f - vec3(1,1,1)), u.x), u.y), u.z);
}
float fbm(vec3 p) {
    float a = 0.5, v = 0.0;
    for (int i = 0; i < 4; i++) {
        v += a * vnoise(p);
        p *= 2.03;
        a *= 0.5;
    }
    return v;
}

// Cheap ACES tonemap.
vec3 aces(vec3 c) {
    mat3 m1 = mat3(0.59719, 0.07600, 0.02840,
                   0.35458, 0.90834, 0.13383,
                   0.04823, 0.01566, 0.83777);
    mat3 m2 = mat3( 1.60475, -0.10208, -0.00327,
                   -0.53108,  1.10813, -0.07276,
                   -0.07367, -0.00605,  1.07602);
    vec3 v = m1 * c;
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return pow(clamp(m2 * (a / b), 0.0, 1.0), vec3(1.0 / 2.2));
}

float launchRand(float t) {
    return fract(sin(t * 0.137 + 17.31) * 43758.5453);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    // Centre and aspect-correct. Y inverted so up is up.
    vec2 uv = (fragCoord / iResolution.xy) * 2.0 - 1.0;
    uv.x *= iResolution.x / iResolution.y;

    // Per-launch seed.
    float seed = launchRand(iTime + 0.5);

    // Background: a deep night sky with a subtle star field
    // (low-amplitude high-frequency noise).
    vec3 sky = vec3(0.005, 0.008, 0.020);
    float stars = smoothstep(0.7, 0.95, vnoise(vec3(uv * 80.0, 0.0)));
    sky += vec3(0.6, 0.7, 0.8) * stars * 0.5;

    // Aurora curtain: a horizontal position function of x,
    // evaluated at multiple frequencies and modulated by a
    // slow flow field. Each curtain has a vertical emission
    // profile (gaussian in y) so it reads as a sheet of light
    // hanging from the sky.
    //
    // Curtains at 3 different frequencies layered for richness.
    float flow = 0.0;
    flow += 0.4 * fbm(vec3(uv.x * 0.7 + iTime * 0.04,
                            uv.y * 0.5,
                            seed * 7.0));
    flow += 0.2 * fbm(vec3(uv.x * 1.5 - iTime * 0.03,
                            uv.y * 0.3,
                            seed * 11.0 + 4.0));

    // Curtain "centre" position — a horizontal position that
    // varies with x via the flow field. The curtain has a
    // finite vertical extent.
    float curtainCentre = uv.x + flow * 0.4;

    // Three overlapping curtain layers at different frequencies.
    float e1 = exp(-pow((uv.x + flow * 0.4) * 3.0, 2.0));
    float e2 = 0.6 * exp(-pow((uv.x + flow * 0.5 + 0.5) * 4.5, 2.0));
    float e3 = 0.5 * exp(-pow((uv.x + flow * 0.3 - 0.4) * 5.5, 2.0));
    float horiz = e1 + e2 + e3;

    // Vertical falloff: aurora hangs from the sky and fades
    // toward the horizon. Exponential from the top.
    float vertical = exp(-pow(uv.y + 0.4, 2.0) * 1.5);
    // Brightest near the top.
    vertical *= smoothstep(-0.8, -0.2, uv.y);

    // Vertical filaments — thin bright lines running down the
    // curtain. fbm at a small scale gives the "rays" reading.
    float filaments = 0.6 + 0.4 * fbm(vec3(uv.x * 8.0
                                          + iTime * 0.5
                                          + flow * 4.0,
                                          uv.y * 2.0,
                                          3.0));

    // Combined emission.
    float emit = horiz * vertical * filaments;

    // Colour: a multi-stop palette. Per-launch shift picks a
    // different colour regime (green-dominant vs pink-dominant
    // vs blue-dominant — real aurora can be any of these).
    vec3 colGreen = vec3(0.30, 1.10, 0.45);
    vec3 colPink  = vec3(1.20, 0.35, 0.75);
    vec3 colBlue  = vec3(0.30, 0.45, 1.20);
    vec3 colViolet= vec3(0.85, 0.30, 1.10);
    // Mix colours based on height — green at the bottom (most
    // common oxygen emission), pink/violet at the top.
    vec3 colLow  = mix(colGreen, colBlue,  fract(seed * 1.31));
    vec3 colHigh = mix(colPink,  colViolet, fract(seed * 2.71));
    vec3 auroraCol = mix(colLow, colHigh,
                         smoothstep(-0.5, 0.5, uv.y + 0.3));

    // Build the image.
    vec3 col = sky;
    col += auroraCol * emit * 1.6;

    // Subtle "horizon glow" — a low band of warmth along the
    // bottom suggesting distant settlements or twilight.
    col += vec3(0.20, 0.10, 0.05) * exp(-pow(uv.y + 0.95, 2.0) * 200.0) * 0.6;

    // Vignette.
    float vig = 1.0 - 0.30 * dot(uv, uv);
    col *= vig;

    // Exposure + ACES.
    col *= 1.2;
    col = aces(col);

    fragColor = vec4(col, 1.0);
}
