// physarum.glsl — single-pass approximation of physarum
// (slime-mould) transport networks.
//
// True physarum is multipass: hundreds of agents deposit trails
// into a field, sense the field, and steer toward higher
// concentrations. The trail field then guides future agents,
// producing organic branching networks that constantly
// reorganise.
//
// The single-pass player here has no multipass / ping-pong, so
// we synthesise the LOOK of physarum trails without simulating
// the agents. The key tricks:
//
//   1. Domain-warped 3D fbm where the warp vector is itself a
//      fbm-of-position gives the "thin veins, fat blobs, hair-
//      like filaments" reading of physarum. Threshold the warped
//      fbm and you get filament networks.
//   2. A 2D flow field (curl noise) advected over time moves the
//      filament network so it appears to "grow" and "retract".
//      The advection is what gives the constant-organic-motion
//      reading.
//   3. Per-launch randomisation of scale, warp, and threshold
//      means every run looks different.
//
// This isn't real physarum — it's a procedurally synthesised
// pattern that occupies the same visual register: organic
// branching networks with constant slow restructuring.
//
// Original work, MIT.

#define PI 3.14159265359

// Hash + value noise (3D) — shared with the rest of the catalogue.
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

// 2D curl of a 2D noise — produces a divergence-free flow field,
// exactly the right kind for advecting patterns without piling
// them up at singularities.
vec2 curl(vec2 p) {
    float h = 0.1;
    float n1 = vnoise(vec3(p.x, p.y + h, 0.0));
    float n2 = vnoise(vec3(p.x, p.y - h, 0.0));
    float n3 = vnoise(vec3(p.x + h, p.y, 1.0));
    float n4 = vnoise(vec3(p.x - h, p.y, 1.0));
    return vec2((n1 - n2), -(n3 - n4)) / (2.0 * h);
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
    // Scale + warp + threshold randomised per launch.
    float scale = 1.5 + 1.5 * seed;
    float warpAmp = 0.5 + 0.4 * fract(seed * 7.31);
    float threshold = 0.05 + 0.08 * fract(seed * 13.7);

    // Domain warp: fbm(uv + fbm(uv + time)). Two nested fbm
    // calls is what gives the "organic" reading — straight fbm
    // looks mechanical.
    //
    // We sample at z = iTime * 0.04 so the pattern slowly
    // evolves. The time evolution is the "constant restructuring"
    // of physarum.
    vec2 p = uv * scale;
    // Curl-noise advection: drift the sample point through a
    // divergence-free flow field. This is the "growing" of the
    // network.
    vec2 advect = curl(p * 0.7 + vec2(iTime * 0.06, 0.0)) * 0.8;
    p += advect;

    // First-level warp.
    vec2 warp1 = vec2(fbm(vec3(p + vec2(0.0, iTime * 0.04), 0.0)),
                      fbm(vec3(p + vec2(7.3, iTime * 0.04), 1.0)));
    // Second-level warp on the first warp.
    vec2 warp2 = vec2(fbm(vec3(warp1 * 2.0 + p, 2.0)),
                      fbm(vec3(warp1 * 2.0 + p + vec2(11.7, 0.0), 3.0)));

    // The actual sample, doubly warped.
    float v = fbm(vec3(p + warp2 * warpAmp, 4.0 + iTime * 0.03));

    // Filament network: the THIN BRIGHT regions of v, near the
    // threshold, are the "trails". Two-sided threshold gives
    // both ridges and valleys (physarum has both).
    float trail = smoothstep(threshold, threshold + 0.10, v)
                - smoothstep(threshold + 0.10, threshold + 0.25, v);
    // A second, finer scale — hair-like filaments.
    float fine = smoothstep(threshold * 0.6, threshold * 0.6 + 0.05, v)
               - smoothstep(threshold * 0.6 + 0.05, threshold * 0.6 + 0.12, v);

    // Bulb regions — the bright "blobs" of the network. These
    // read as the nodes where trails meet.
    float bulb = smoothstep(0.45, 0.7, v);

    // Colour: a multi-stop palette. Per-launch shift gives
    // variety — some runs warm (amber/teal), some cool
    // (cyan/violet).
    vec3 colTrail = mix(vec3(0.95, 0.55, 0.30),    // warm amber
                        vec3(0.30, 0.85, 0.95),    // cyan
                        seed);
    vec3 colFine  = mix(vec3(1.10, 0.85, 0.55),    // bright amber
                        vec3(0.55, 1.10, 1.20),    // bright cyan
                        seed);
    vec3 colBulb  = mix(vec3(1.20, 0.40, 0.55),    // hot pink
                        vec3(0.85, 0.40, 1.10),    // violet
                        seed);
    vec3 colBg    = mix(vec3(0.04, 0.05, 0.10),    // deep blue-grey
                        vec3(0.06, 0.08, 0.04),    // deep olive
                        seed);

    // Build the image: deep background + trails + fine filaments
    // + bulbs.
    vec3 col = colBg;
    col += colTrail * trail * 1.3;
    col += colFine  * fine  * 1.5;
    col += colBulb  * bulb  * 0.7;

    // Soft radial vignette so the corners feel slightly recessed.
    float vig = 1.0 - 0.30 * dot(uv, uv);
    col *= vig;

    // Exposure: hand-tuned so trails + filaments read bright but
    // don't blow out.
    col *= 1.4;
    col = aces(col);

    fragColor = vec4(col, 1.0);
}
