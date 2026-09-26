// marble.glsl — domain-warped fbm landscape.
//
// Three layers of fbm, each warping the next, produces flowing
// marbled fields that read as alien terrain or astronomical
// nebulae. Cheap (no raymarching, no iteration) and very rich.
//
// The brief: "fbm warped by fbm warped by fbm — flowing marbled
// fields. Cheap and very rich."
//
// Original work, MIT.

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
    for (int i = 0; i < 5; i++) {
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
    // Centre and aspect-correct.
    vec2 uv = (fragCoord / iResolution.xy) * 2.0 - 1.0;
    uv.x *= iResolution.x / iResolution.y;

    // Per-launch seed.
    float seed = launchRand(iTime + 0.5);
    float scale = 1.5 + 1.5 * seed;

    vec2 p = uv * scale;

    // Three nested warps. Each fbm call uses a different
    // coordinate slice so the warps don't collapse.
    // p -> fbm1(p, t1) -> fbm2(p + fbm1, t2) -> fbm3(p + fbm2, t3)
    vec2 q = vec2(fbm(vec3(p + vec2(0.0, iTime * 0.05), 1.0)),
                  fbm(vec3(p + vec2(5.2, iTime * 0.04), 1.0)));

    vec2 r = vec2(fbm(vec3(p + 4.0 * q + vec2(1.7, 9.2), 2.0)),
                  fbm(vec3(p + 4.0 * q + vec2(8.3, 2.8), 2.0)));

    // Final fbm at the doubly-warped point.
    float v = fbm(vec3(p + 4.0 * r, 3.0 + iTime * 0.03));

    // Map the value (-ish 0..1) to colour via a multi-stop
    // palette. Per-launch shift picks different colour regimes.
    vec3 colA = mix(vec3(0.95, 0.40, 0.30),    // warm red
                    vec3(0.10, 0.45, 0.95),    // cool blue
                    seed);
    vec3 colB = mix(vec3(1.10, 0.85, 0.30),    // gold
                    vec3(0.50, 0.95, 0.80),    // teal
                    fract(seed * 3.71));
    vec3 colC = mix(vec3(0.85, 0.30, 1.00),    // violet
                    vec3(1.00, 0.85, 0.40),    // amber
                    fract(seed * 5.13));

    // Smooth palette: three colours interpolated by v.
    vec3 col = mix(colA, colB, smoothstep(0.0, 0.5, v));
    col = mix(col, colC, smoothstep(0.5, 1.0, v));

    // Add the warp field as a luminance modulation so the
    // marbled reading is visible.
    float warpMag = length(r);
    col *= 0.7 + 0.6 * warpMag;

    // Soft radial vignette.
    float vig = 1.0 - 0.25 * dot(uv, uv);
    col *= vig;

    // Exposure + ACES.
    col *= 1.4;
    col = aces(col);

    fragColor = vec4(col, 1.0);
}
