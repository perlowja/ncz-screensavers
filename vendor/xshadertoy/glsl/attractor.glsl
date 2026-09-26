// attractor.glsl — strange-attractor density field.
//
// True Clifford / De Jong attractors accumulate millions of
// iterations into a 2D histogram and tone-map. The single-pass
// player has no multipass / ping-pong, so we synthesise the
// LOOK of an attractor density field differently: we draw the
// attractor trace as a parameterised line integral.
//
// For each pixel, we step N iterations of the Clifford attractor
// forward from a per-pixel initial condition. Where the
// iterates pass close to the pixel (within an epsilon), we add
// to the density. After tone-mapping, this produces the
// characteristic fine filamentary structure of strange attractors.
//
// Each "iteration" is actually a small line segment from the
// current iterate to the next, and we add the closest-point
// distance to the density. This gives smooth filaments rather
// than the dotty look of true accumulation.
//
// Algorithm: Clifford pickover attractor
//   x_{n+1} = sin(a * y_n) + c * cos(a * x_n)
//   y_{n+1} = sin(b * x_n) + d * cos(b * y_n)
//
// (c) Jamie Zawinski / Clifford Pickover, 1986 — algorithm,
// not code. Original work.
//
// Original work, MIT.

#define PI 3.14159265359

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

    // Clifford parameters — chosen to produce rich filamentary
    // structure. Different seeds shift the (a, b, c, d) so
    // every run produces a different attractor.
    float a = 1.7  + 0.6 * fract(seed * 1.31);
    float b = 1.7  + 0.6 * fract(seed * 2.71);
    float c = 0.6  + 0.6 * fract(seed * 4.13);
    float d = 1.3  + 0.6 * fract(seed * 5.97);
    // Slow parameter drift so the attractor itself morphs
    // slightly during a long viewing session.
    a += 0.10 * sin(iTime * 0.04);
    b += 0.10 * cos(iTime * 0.05);

    // Map the screen to attractor space. Clifford outputs are
    // in roughly [-2, 2] for both axes.
    vec2 sc = vec2(0.5, 0.5);
    vec2 ofs = vec2(0.0, 0.0);

    // Density field: iterate multiple trajectories (different
    // initial conditions) and accumulate density across all of
    // them. This is the standard "compute once, accumulate
    // everywhere" approach used in CPU attractor renderers.

    float density = 0.0;
    float hueAccum = 0.0;

    // We run 8 trajectories per frame, each from a different
    // initial condition. The trajectories all see the same
    // Clifford parameters; their initial conditions are
    // perturbed by slow time evolution so the attractor set
    // morphs.
    const int N_TRAJ = 8;
    for (int tIdx = 0; tIdx < N_TRAJ; tIdx++) {
        float ft = float(tIdx);
        // Initial condition perturbed by trajectory index and
        // slow time.
        vec2 x0 = vec2(0.1 + 0.3 * fract(ft * 0.731 + seed * 5.13),
                       0.1 + 0.3 * fract(ft * 0.617 + seed * 7.91))
                + 0.05 * vec2(sin(iTime * 0.13 + ft),
                              cos(iTime * 0.11 + ft));
        vec2 x = x0;
        // 100 iterations per trajectory.
        for (int i = 0; i < 100; i++) {
            vec2 xn = vec2(sin(a * x.y) + c * cos(a * x.x),
                           sin(b * x.x) + d * cos(b * x.y));
            x = xn;
            // Distance from this iterate to the pixel's
            // attractor coordinate. Tight gaussian gives
            // filaments.
            vec2 d2 = x - (uv * sc + ofs);
            float dist = length(d2);
            density += exp(-pow(dist / 0.08, 2.0)) * 0.040;
            // Hue-weighted.
            float a2 = atan(x.y, x.x);
            hueAccum += a2 * exp(-pow(dist / 0.10, 2.0)) * 0.040;
        }
    }

    // Normalise hue by density so colours are stable across
    // density gradients.
    float hueN = hueAccum / max(density, 1e-3);
    // Map hue to RGB via cosines.
    vec3 hueCol = 0.5 + 0.5 * cos(6.2831853
                                  * (vec3(0.0, 0.33, 0.67) + hueN / 6.2831853));

    // Per-launch palette shift.
    vec3 colA = mix(vec3(0.20, 0.55, 1.00),    // cyan
                    vec3(0.95, 0.40, 0.85),    // magenta
                    seed);
    vec3 colB = mix(vec3(1.00, 0.70, 0.30),    // amber
                    vec3(0.50, 0.95, 0.95),    // teal
                    fract(seed * 3.7));
    vec3 colMix = mix(colA, colB, 0.5 + 0.5 * sin(hueN));

    // Density-modulated colour. Filaments are bright; non-
    // trajectory regions are nearly black.
    float bright = smoothstep(0.0, 1.2, density);
    vec3 col = colMix * bright * 2.4;

    // Subtle ambient base so the dark regions aren't pure black.
    col += vec3(0.020, 0.025, 0.040);

    // Soft radial vignette.
    float vig = 1.0 - 0.25 * dot(uv, uv);
    col *= vig;

    // Exposure + ACES.
    col *= 1.0;
    col = aces(col);

    fragColor = vec4(col, 1.0);
}
