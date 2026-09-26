// ridgeline.glsl — procedural fractal mountain landscape in
// continuous flight.
//
// The brief: ridges, valleys, haze receding to the horizon,
// fills the ENTIRE frame, no black sky. Per-launch iSeed
// changes terrain character (billowy vs ridged), time of day,
// and flight path.
//
// Technique: raymarched fBm heightfield with analytic normals
// (central differences), distance-based atmospheric haze, a
// sky/sun gradient that covers the upper portion. Standard
// "inigo quilez heightmap landscape" pattern (iq's 2008
// "elevation" article — published mathematics, not code).
//
// Original work, MIT. No vendor shadertoy reference used;
// iq's writings inform the technique, the implementation
// here is independent.
//
// Per-launch variation (driven by iSeed):
//   .x  terrain character: < 0.5 billowy/soft, >= 0.5 ridged/sharp
//   .y  time of day: 0 dawn, 0.25 midday, 0.5 sunset, 0.75 night
//   .z  flight path: heading +0..2pi, drift rate 0.4..1.6
//   .w  ridge frequency: 0.3..1.2 octaves multiplier

#define PI 3.14159265359
#define TAU (2.0*PI)

// Cheap value noise (no texture dependency). Hash-based gradient
// noise in 2D, classic Perlin-style construction.
float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}
float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y) * 2.0 - 1.0;
}

// Fractal Brownian motion. 5 octaves is plenty for a heightfield;
// budget-conscious on Intel UHD.
float fbm(vec2 p) {
    float a = 0.5;
    float v = 0.0;
    for (int i = 0; i < 5; i++) {
        v += a * vnoise(p);
        p *= 2.03;
        a *= 0.5;
    }
    return v;
}

// Ridged variant: 1 - |fbm| emphasises creases.
float ridge(vec2 p) {
    float a = 0.5;
    float v = 0.0;
    for (int i = 0; i < 5; i++) {
        float n = 1.0 - abs(vnoise(p));
        n = n * n;
        v += a * n;
        p *= 2.03;
        a *= 0.5;
    }
    return (v - 0.6) * 0.9;
}

// Domain warp: nudge the sample point by a small fbm so the
// terrain doesn't look like aligned contour lines.
vec2 warp(vec2 p) {
    float w = 0.35;
    return p + w * vec2(fbm(p + vec2(1.7, 9.2)),
                        fbm(p + vec2(8.3, 2.8)));
}

// Combined terrain — iSeed.x mixes billowy and ridged variants.
// Returns a height in roughly [-0.5, 0.5] before scale.
float terrain(vec2 p, float character) {
    p *= 1.6;
    p = warp(p);
    float billowy = fbm(p);
    float r = ridge(p * 0.85);
    return mix(billowy, r, smoothstep(0.45, 0.55, character));
}

// Raymarch the heightfield from above. Camera altitude 0.6.
// Returns total distance travelled. Caller clamps to maxDist and
// uses the hit position. Up to 96 steps; we exit early when the
// ray goes very high (cleared terrain) or the step overshoots.
float raymarch(vec3 ro, vec3 rd, float maxDist) {
    float t = 0.0;
    for (int i = 0; i < 96; i++) {
        vec3 p = ro + rd * t;
        float h = p.y - terrain(p.xz, 0.5);
        if (h < 0.002) return t;
        if (t > maxDist) break;
        // Step proportional to height error; floor to keep progress.
        float step = max(0.02, h * 0.85);
        t += step;
    }
    return maxDist;
}

// Normal via central differences (3 taps, cheap).
vec3 terrainNormal(vec2 p, float character) {
    float e = 0.02;
    float hL = terrain(p - vec2(e, 0.0), character);
    float hR = terrain(p + vec2(e, 0.0), character);
    float hD = terrain(p - vec2(0.0, e), character);
    float hU = terrain(p + vec2(0.0, e), character);
    return normalize(vec3(hL - hR, 2.0 * e, hD - hU));
}

// Sky colour for a given view direction. Time of day mixes four
// palettes: dawn (warm peach), midday (cyan), sunset (magenta/orange),
// night (deep violet with stars).
vec3 skyColor(vec3 rd, vec3 sunDir, float tod) {
    // Horizon gradient (lowest band, hits the haze line).
    vec3 horizonNight   = vec3(0.04, 0.05, 0.10);
    vec3 horizonDawn    = vec3(0.65, 0.45, 0.40);
    vec3 horizonMidday  = vec3(0.55, 0.75, 0.90);
    vec3 horizonSunset  = vec3(0.85, 0.40, 0.30);

    // Zenith (highest band).
    vec3 zenithNight   = vec3(0.01, 0.01, 0.04);
    vec3 zenithDawn    = vec3(0.20, 0.30, 0.55);
    vec3 zenithMidday  = vec3(0.20, 0.50, 0.85);
    vec3 zenithSunset  = vec3(0.30, 0.15, 0.45);

    // Quadratic blend along the four TOD anchors.
    float w0 = max(0.0, 1.0 - 4.0 * abs(tod - 0.00));
    float w1 = max(0.0, 1.0 - 4.0 * abs(tod - 0.25));
    float w2 = max(0.0, 1.0 - 4.0 * abs(tod - 0.50));
    float w3 = max(0.0, 1.0 - 4.0 * abs(tod - 0.75));
    vec3 horiz = (horizonNight * w0 + horizonDawn * w1 +
                  horizonMidday * w2 + horizonSunset * w3);
    vec3 zenith = (zenithNight * w0 + zenithDawn * w1 +
                   zenithMidday * w2 + zenithSunset * w3);
    // Normalise weights so they sum to 1 (clamped above by max).
    float wsum = w0 + w1 + w2 + w3 + 1e-5;
    horiz /= wsum;
    zenith /= wsum;

    // Vertical blend.
    float y = clamp(rd.y * 1.4 + 0.1, 0.0, 1.0);
    vec3 baseSky = mix(horiz, zenith, smoothstep(0.0, 1.0, y));

    // Sun disc + halo.
    float sunDot = max(dot(rd, sunDir), 0.0);
    float disc = smoothstep(0.9985, 0.9998, sunDot);
    float halo = pow(sunDot, 32.0) * 0.4;
    baseSky += vec3(1.0, 0.85, 0.65) * (disc + halo);

    // Stars for night TOD.
    float starSeed = hash21(floor(rd.xz * 200.0));
    float star = step(0.998, starSeed) * smoothstep(0.0, 0.2, w0 + w3);
    baseSky += vec3(0.7, 0.8, 1.0) * star;

    return baseSky;
}

// Atmospheric haze — Beer's law-ish exponential fade to sky as
// the ray travels through air. Distance squared in the exponent
// keeps it cheap.
vec3 applyHaze(vec3 col, float dist, vec3 skyCol) {
    float fog = 1.0 - exp(-dist * 0.06);
    return mix(col, skyCol, fog);
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

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord / iResolution.xy) * 2.0 - 1.0;
    uv.x *= iResolution.x / iResolution.y;

    // Per-launch variation decoded once.
    float character  = iSeed.x;
    float timeOfDay  = fract(iSeed.y);
    float heading0   = iSeed.z * TAU;
    float driftRate  = mix(0.4, 1.6, fract(iSeed.w * 7.31));
    float ridgeFreq  = mix(0.3, 1.2, fract(iSeed.w * 13.97));

    // Camera flight: an advancing curve. Heading wanders slowly
    // over time via a low-frequency cosine, so the path is never
    // a straight line.
    float heading = heading0 + 0.4 * sin(iTime * 0.07);
    float advance = iTime * driftRate * 0.7;
    vec2 pathCentre = vec2(cos(heading), sin(heading)) * advance;
    // Add a slow lateral weave for visual interest.
    pathCentre += vec2(-sin(heading), cos(heading)) *
                  0.6 * sin(iTime * 0.13 + iSeed.x * 6.28);

    vec3 ro = vec3(pathCentre.x, 0.6, pathCentre.y);

    // Build view direction. Camera looks slightly down at the
    // terrain ahead; pitch is constant so the horizon stays
    // stable. Roll is gentle.
    float pitch = -0.15;
    float roll  = 0.06 * sin(iTime * 0.05);
    vec3 forward = vec3(cos(heading), sin(pitch), sin(heading));
    vec3 worldUp = vec3(sin(roll), cos(roll), 0.0);
    vec3 right = normalize(cross(forward, worldUp));
    vec3 up = normalize(cross(right, forward));
    vec3 rd = normalize(forward + uv.x * right + uv.y * up);

    // Sun direction: fixed elevation, azimuth tied to TOD.
    // Dawn: sun rises (east). Midday: overhead-ish. Sunset: west.
    // Night: below horizon, so disc is hidden.
    float sunAz = mix(-0.5, 0.5, fract(timeOfDay + 0.25));
    float sunEl = sin(timeOfDay * TAU + 0.4);
    vec3 sunDir = normalize(vec3(cos(sunAz) * cos(sunEl),
                                 sin(sunEl),
                                 sin(sunAz) * cos(sunEl)));

    // Sky colour (sampled even when we hit terrain, for haze blend).
    vec3 skyCol = skyColor(rd, sunDir, timeOfDay);

    // Raymarch. If the ray points away from the terrain (rd.y < 0
    // case is impossible since we set pitch < 0; but the upper sky
    // is reached when the ray exits the heightfield via distance).
    float maxDist = 16.0;
    float dist = raymarch(ro, rd, maxDist);

    vec3 col;
    if (dist < maxDist) {
        // Hit. Compute lighting from the normal and sun.
        vec3 hitPos = ro + rd * dist;
        vec3 N = terrainNormal(hitPos.xz * ridgeFreq,
                                character) / ridgeFreq;
        // Diffuse.
        float diff = max(dot(N, sunDir), 0.0);
        // Specular only when sun is reasonably above horizon.
        float spec = pow(max(dot(reflect(-sunDir, N), rd), 0.0), 16.0)
                     * smoothstep(0.0, 0.3, sunDir.y);
        // Terrain tint: cool in shadow, warm in sun. Mix by TOD
        // palette anchors — same scheme as the sky.
        vec3 grass  = vec3(0.22, 0.35, 0.20);
        vec3 rock   = vec3(0.45, 0.40, 0.35);
        vec3 snow   = vec3(0.85, 0.85, 0.90);
        float slope = clamp(N.y, 0.0, 1.0);
        // Alt = hitPos.y is in roughly [-0.5, 0.5]. Scale so the
        // snow line is well above most terrain (only highest
        // peaks get snow).
        float alt = clamp((hitPos.y - 0.40) * 8.0, 0.0, 1.0);
        vec3 base = mix(grass, rock, 1.0 - slope);
        base = mix(base, snow, alt * alt);

        // Sun colour depends on TOD: warm at low elevation.
        vec3 sunCol = mix(vec3(1.0, 0.45, 0.30),
                          vec3(1.0, 0.95, 0.85),
                          smoothstep(-0.05, 0.4, sunDir.y));
        // Ambient from the sky.
        vec3 ambient = skyCol * 0.35;

        col = base * (diff * sunCol + ambient) + spec * sunCol;
        col = applyHaze(col, dist, skyCol);
    } else {
        // No hit — sky.
        col = skyCol;
    }

    // Sun-side rim light: a warm glow near the horizon in the
    // sun's direction. Cheap and ties sky to terrain.
    float horizonGlow = pow(max(1.0 - abs(rd.y), 0.0), 6.0) *
                        max(dot(rd, sunDir), 0.0) * 0.4;
    col += vec3(1.0, 0.6, 0.3) * horizonGlow *
           smoothstep(-0.05, 0.5, sunDir.y);

    // Vignette.
    float vig = 1.0 - 0.20 * dot(uv, uv);
    col *= vig;

    col *= 1.15;
    col = aces(col);

    fragColor = vec4(col, 1.0);
}
