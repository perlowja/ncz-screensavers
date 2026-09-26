// caustics-on-stone.glsl
// wave2 — quiet materiality: caustics on pale stone.
//
// Algorithm:
//   1. A pale receiving surface (warm cream sandstone, broad slow
//      variation, soft grain). The stone is always visible — the
//      caustics are a modulation layer, not a painted web.
//   2. Caustics — bright concentrated ribbons of light that read as
//      light focused through a perturbed water surface onto the stone.
//      Built from a domain-warped sum of sines evaluated on a slow-
//      moving noise field; the field is cheap (4-octave value-noise +
//      low-frequency warp).
//   3. Three layered scales of caustic intensity — broad slow drift
//      beneath finer local motion — for the "broad illumination
//      beneath finer moving concentration" hierarchy Astra asked for.
//
// CRITICAL: the receiving surface must DOMINATE — pale cream reads as
// stone, not paper. The caustics must brighten but not whitewash.
// "A white web on blue is merely a texture — the receiving surface is
// the point." (Astra brief.)
//
// Approach: we darken the stone base considerably (linear ~0.20) and
// restrict caustic field to a smaller area (ridge power + value
// threshold) so the un-caustic regions are clearly DARKER. The
// ambient water-light is a faint cool wash; the caustic highlights
// punch through it. Bright caustic lines read against the visible
// stone, not against white wash.
//
// Tonemap (ACES Narkowicz) at the end after pre-tonemap exposure
// control; gamma at the very end.

#define PI 3.14159265359

// ---- Hash / value noise (cheap, GPU friendly) ----
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

float fbm4(vec2 p) {
    float s = 0.0;
    float a = 0.5;
    mat2 rot = mat2(0.8, -0.6, 0.6, 0.8);
    for (int i = 0; i < 4; i++) {
        s += a * vnoise(p);
        p = rot * p * 2.07 + vec2(13.1, 7.7);
        a *= 0.5;
    }
    return s;
}

// Caustic intensity at uv. The "look" of underwater caustics
// (sharp moving ribbons of light) comes from taking the absolute
// value of a perturbed cosine grid and raising it to a steep power —
// bright thin lines on a dark broad field.
float caustic(vec2 uv, float t) {
    vec2 drift = vec2(t * 0.06, t * 0.045);
    vec2 q = uv * 3.5 + drift;
    vec2 warp = vec2(fbm4(q + drift), fbm4(q + drift + 5.3));
    q += (warp - 0.5) * 1.6;
    float a = cos(q.x * 1.7 + sin(q.y * 1.3) * 1.8);
    float b = cos((q.x * 0.6 - q.y * 1.4) * 1.2 + 0.7);
    float c = (a + b * 0.7) * 0.45;
    float ridge = pow(1.0 - abs(c), 5.5);
    return ridge;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 res = iResolution.xy;
    vec2 uv = (fragCoord - 0.5 * res) / min(res.x, res.y);

    float t = iTime;

    // ---- Receiving surface (pale sandstone) ----
    // Warm cream stone with broad slow vein variation + soft grain.
    // The base colour is intentionally muted — the caustics will add
    // most of the visible brightness. Higher warmth in some patches
    // (sand veins) and cooler in others keeps the stone legibly a
    // stone and not a uniform slab.
    vec2 stoneUV = uv * 1.6;
    float vein = fbm4(stoneUV * 0.6 + vec2(t * 0.012, -t * 0.008));
    float warmth = mix(0.75, 1.25, vein);

    // Subtle grain — sandstone texture, deterministic per pixel,
    // doesn't animate (so the stone doesn't shimmer).
    float grain = vnoise(uv * 90.0) * 0.05
                + vnoise(uv * 220.0) * 0.025;

    // Base stone: warm cream sandstone under water. Linear ~0.22 —
    // gives room for caustic highlights to push bright lines into the
    // visible range while keeping dark areas clearly stone (not
    // paper).
    vec3 stoneBase = vec3(0.42, 0.34, 0.22);
    vec3 stone = stoneBase * warmth + vec3(grain * 0.6);

    // ---- Caustic field ----
    // Three layered scales. Broad gives continuous underlying
    // illumination; mid + fine add concentrated bright lines.
    float cBroad = caustic(uv * 0.9 + vec2(3.0, 0.0), t * 0.5);
    float cMid   = caustic(uv * 1.6 + vec2(-1.0, 2.0), t * 0.7 + 1.7);
    float cFine  = caustic(uv * 3.1 + vec2(0.5, -2.0), t * 1.1 + 4.2);

    // Smoothstep threshold to keep field concentrated: only the
    // bright ribbons survive, not the broad plateau. Without this
    // threshold the entire frame washes to the same brightness and
    // the receiving surface disappears.
    float field = cBroad * 0.55 + cMid * 0.45 + cFine * 0.35;
    field = smoothstep(0.18, 0.95, field);

    // ---- Ambient water light ----
    // Faint warm-cool wash over the whole surface — gives a
    // "submerged" tint without being a separate lighting effect.
    vec3 water = vec3(0.10, 0.13, 0.16);

    // Caustic highlight colour: warm white (sunlight through water).
    // Strength 1.55 ensures the lines are visibly bright but ACES
    // protects them from pure-clip to white.
    vec3 causticCol = vec3(1.40, 1.25, 0.85);
    vec3 col = stone + water + field * 1.55 * causticCol;

    // ---- Vignette ----
    float vig = 1.0 - 0.28 * dot(uv, uv);
    col *= vig;

    // ---- Tonemap (ACES Narkowicz) + gamma ----
    vec3 a = col * (2.51 * col + 0.03);
    vec3 b = col * (2.43 * col + 0.59) + 0.14;
    vec3 mapped = clamp(a / b, 0.0, 1.0);
    fragColor = vec4(pow(mapped, vec3(1.0 / 2.2)), 1.0);
}