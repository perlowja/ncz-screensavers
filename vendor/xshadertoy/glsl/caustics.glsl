// caustics.glsl — water-caustic light on a floor.
//
// Classic shadertoy technique: project a moving 3D fbm / voronoi
// noise through a "water surface" and let the bright pinpoints
// fall on a floor. Reads as sunlight refracted through pool
// water.
//
// The brief: "Light focused through a perturbed surface onto a
// floor. Bright, moving, immediately readable as water without
// simulating water." So we synthesise caustic pinpoints via the
// signed-distance to nearest voronoi cell boundary, modulated by
// a slow perturbation field.
//
// Original work, MIT. No vendor shadertoy reference used.

#define PI 3.14159265359

// Hash + 2D voronoi.
vec2 hash22(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)),
             dot(p, vec2(269.5, 183.3)));
    return -1.0 + 2.0 * fract(sin(p) * 43758.5453);
}

// 2D voronoi returning both the cell distance (F1) and the
// second-closest distance (F2). The standard "caustics" trick
// uses F2 - F1 (distance to the cell boundary) raised to a
// high power — sharp bright ridges along the cell boundaries.
vec2 voronoi(vec2 p) {
    vec2 ip = floor(p);
    vec2 fp = fract(p);
    float F1 = 8.0;
    float F2 = 8.0;
    for (int j = -1; j <= 1; j++) {
        for (int i = -1; i <= 1; i++) {
            vec2 g = vec2(float(i), float(j));
            vec2 o = hash22(ip + g) * 0.5 + 0.5;
            // Animate the cell seeds.
            o = 0.5 + 0.5 * sin(iTime * 0.7 + 6.2831853 * o);
            vec2 r = g + o - fp;
            float d = dot(r, r);
            if (d < F1) { F2 = F1; F1 = d; }
            else if (d < F2) { F2 = d; }
        }
    }
    return vec2(sqrt(F1), sqrt(F2));
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
    // Centre and aspect-correct. Y inverted so the floor reads
    // as a floor (y=+1 is far, y=-1 is near).
    vec2 uv = (fragCoord / iResolution.xy) * 2.0 - 1.0;
    uv.x *= iResolution.x / iResolution.y;

    // Per-launch seed.
    float seed = launchRand(iTime + 0.5);

    // Caustic pattern: voronoi (F2 - F1) inverted and raised
    // to a high power, with a domain perturbation that simulates
    // the water-surface waves.
    //
    // Domain perturbation: a slow low-frequency "wave field"
    // that displaces the sample point.
    vec2 p = uv * 3.0;
    // Wave field — sin combinations driven by time, gives the
    // "water surface" reading.
    vec2 wave = vec2(sin(p.y * 0.5 + iTime * 0.6),
                     cos(p.x * 0.5 + iTime * 0.5));
    p += wave * 0.45;

    // Voronoi at the perturbed point.
    vec2 vor = voronoi(p);
    float caustic = vor.y - vor.x;
    // Raise to a high power — sharp bright ridges along cell
    // boundaries.
    caustic = pow(max(caustic, 0.0), 6.0);

    // A second caustic layer at a different scale, slower time
    // evolution, gives layered detail.
    vec2 vor2 = voronoi(p * 2.3 + vec2(iTime * 0.2));
    float caustic2 = pow(max(vor2.y - vor2.x, 0.0), 7.0) * 0.6;

    // Combined caustics.
    float c = caustic + caustic2;

    // Floor: a deep blue/teal that gets brighter where caustics
    // fall. The floor has a slight gradient with depth.
    vec3 floorBase = mix(vec3(0.02, 0.05, 0.10),    // near (cool)
                         vec3(0.04, 0.08, 0.04),    // far (slightly green)
                         0.5 + 0.5 * uv.y);

    // Caustic colour: warm white that gets a tint from the
    // launch seed. Some runs are more golden, some more icy.
    vec3 causticCol = mix(vec3(1.40, 1.30, 1.10),    // golden
                          vec3(1.20, 1.40, 1.40),    // icy
                          seed);

    // Build the image.
    vec3 col = floorBase;
    col += causticCol * c * 2.0;

    // Subtle horizontal "water surface" highlight — a thin
    // band along the top of the frame suggesting the perturbed
    // surface the light is coming through.
    float surfaceHL = exp(-abs(uv.y - 1.0) * 8.0)
                    * (0.5 + 0.5 * sin(uv.x * 8.0 + iTime * 0.7));
    col += vec3(0.20, 0.30, 0.40) * surfaceHL * 0.4;

    // Soft radial vignette.
    float vig = 1.0 - 0.30 * dot(uv, uv);
    col *= vig;

    // Exposure + ACES.
    col *= 1.2;
    col = aces(col);

    fragColor = vec4(col, 1.0);
}
