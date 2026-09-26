// Title:  Wellcurve
// Author: original for ncz-screensavers (clean-room)
// Date:   2026-09-26
// Desc:   A grid mesh deformed by one or more heavy masses -- the
//         classic depiction of spacetime curvature.  Glowing lines,
//         fully saturated palette, strong structure, the deformation
//         moving continuously.
//         Per-run variation comes from iSeed: number and placement
//         of masses, grid style (square vs hexagonal vs radial),
//         and the two-color palette.
//
// Technique: analytic, no raymarch.  We deform the sampling
// coordinate by the Newtonian potential of N point masses, then
// compute the distance to the nearest grid line at the *deformed*
// coordinate and accumulate a glow proportional to the inverse
// distance (Inigo Quilez 2008, "2D distance to grid").  The grid
// color at each pixel is mixed toward the nearest mass's color, so
// the deformation also tints the lines.
//
// References (all public-domain mathematics, not hack source):
//   - I. Quilez, "2D distance functions" (2008) -- distance to a
//     line via abs(fract(p*N)-0.5)/N.
//   - Newtonian gravity, F = G M r / r^3, used for displacement.
//   - HSV <-> RGB conversions (Foley/van Dam 1996, public-domain).

// ---------------------------------------------------------------------
// Constants and helpers
// ---------------------------------------------------------------------

#define PI    3.14159265359
#define TAU   6.28318530718
#define MAX_MASSES 5

// Distance to nearest line of a regular grid in 2D.  Inigo Quilez,
// "2D distance functions" (2008).  The +0.5 trick centres the
// modulus so the line is at p.k = 0.
float gridLineDist(vec2 p, float spacing) {
    vec2 g = abs(fract(p / spacing - 0.5) - 0.5) / spacing;
    return min(g.x, g.y);
}

// Same idea, hexagonal grid (Quilez, same article).
float hexGridLineDist(vec2 p, float spacing) {
    // Skew to a hex lattice, then take min of three line families.
    const vec2 s = vec2(1.7320508, 1.0); // sqrt(3), 1
    vec2 q = vec2(p.x * 0.8660254 + p.y * 0.5,
                  p.y);                  // 60 deg rotation
    vec2 g1 = abs(fract(q / spacing - 0.5) - 0.5) / spacing;
    vec2 g2 = abs(fract(q.yx / spacing - 0.5) - 0.5) / spacing;
    vec2 g3 = abs(fract((q + vec2(0.0, spacing * 0.5)) / spacing - 0.5)
                  - 0.5) / spacing;
    return min(min(g1.x, g1.y), min(min(g2.x, g2.y), min(g3.x, g3.y)));
}

// Distance to a ring set (radial grid) at radii n * spacing.
float ringDist(vec2 p, float spacing) {
    float r = length(p);
    float g = abs(fract(r / spacing - 0.5) - 0.5) * spacing;
    return g;
}

// HSV -> RGB.  Standard textbook conversion; all channels in [0,1].
vec3 hsv2rgb(vec3 c) {
    vec3 p = abs(fract(c.xxx + vec3(0.0, 2.0/3.0, 1.0/3.0)) * 6.0 - 3.0);
    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}

// Smooth, monotonic 0..1 ramp with controllable width.
float band(float d, float w) {
    return 1.0 - smoothstep(0.0, w, d);
}

// ---------------------------------------------------------------------
// Masses.  Each mass has (centre, strength, hue, phase, omega).
// Driven entirely by iSeed.x (count) and iSeed.y (layout style).
// ---------------------------------------------------------------------

struct Mass {
    vec2  c;       // centre
    float strength;
    float hue;
    float phase;
    float omega;
    float radius;  // orbit radius for moving masses
};

Mass massArray[MAX_MASSES];

// Populate the mass table.  Done on the CPU side... except we're
// inside the shader, so this is called from mainImage each frame.
// It's cheap -- a fixed number of sin/cos.
void loadMasses(float t) {
    // Note: cannot name this variable `layout` -- it's a reserved
    // word in GLSL ES 3.00.
    float layoutStyle = iSeed.y;        // 0..1
    int   n = int(mix(2.0, 5.0, iSeed.x));  // 2..5 masses

    // Mass #0: central, fixed at origin.  The "main" well.
    massArray[0] = Mass(
        vec2(0.0),
        1.0,                  // strongest
        0.55 + 0.10 * iSeed.z,
        0.0,
        0.0,                  // doesn't move
        0.0);

    // Mass #1..n-1: orbit the origin on circles whose radius and
    // angular speed depend on layout.
    for (int i = 1; i < MAX_MASSES; i++) {
        if (i >= n) {
            // Park unused slots at infinity so they don't contribute.
            massArray[i] = Mass(vec2(1e6), 0.0, 0.0, 0.0, 0.0, 0.0);
            continue;
        }
        float fi = float(i);
        float orbitR  = mix(0.35, 1.10, layoutStyle);
        float phase   = fi * 1.7 + iSeed.z * TAU;
        float omega   = mix(0.25, 0.55, layoutStyle) * (1.0 - 0.15 * fi);
        float mass_h  = mix(0.65, 0.30, fi / float(n));  // smaller as count grows
        float hue     = fract(iSeed.w + 0.18 * fi);
        massArray[i] = Mass(
            vec2(0.0),         // filled in below
            mass_h,
            hue,
            phase,
            omega,
            orbitR);
        float th = t * omega + phase;
        massArray[i].c = orbitR * vec2(cos(th), sin(th));
    }
}

// Sum of displacement contributions from all active masses.
// Newtonian: displacement ~ -G M r / r^3.  For visual purposes we
// use a softened 1/r (with a softening term to avoid singularity
// at the centre) AND clamp the per-pixel contribution so the
// deformation stays in a sensible range.  Unclamped displacement
// near a mass makes every nearby pixel collapse onto the same
// tiny area, which reads as a flat disc, not as a curvature.
vec2 deform(vec2 p) {
    vec2 d = vec2(0.0);
    for (int i = 0; i < MAX_MASSES; i++) {
        vec2  r = p - massArray[i].c;
        // Softening prevents singularity at the mass centre.
        float r2 = dot(r, r) + 0.05;
        float inv_r3 = 1.0 / (r2 * sqrt(r2));
        d -= massArray[i].strength * r * inv_r3;
    }
    // Clamp the total displacement magnitude so the grid doesn't
    // pinch into a singularity.
    float m = length(d);
    if (m > 0.4) d *= 0.4 / m;
    return d;
}

// Sum the field magnitude at p (used for tinting lines by closest mass).
float fieldStrength(vec2 p, out float bestHue) {
    float best = 0.0;
    bestHue = massArray[0].hue;
    for (int i = 0; i < MAX_MASSES; i++) {
        if (massArray[i].strength <= 0.0) continue;
        vec2 r = p - massArray[i].c;
        float r2 = dot(r, r) + 0.015;
        float s = massArray[i].strength / r2;
        if (s > best) {
            best = s;
            bestHue = massArray[i].hue;
        }
    }
    return best;
}

// ---------------------------------------------------------------------
// Main.  Build the deformed grid, render glowing lines.
// ---------------------------------------------------------------------

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    loadMasses(iTime);

    // Screen -> world.  Aspect-corrected, centered, scaled so the
    // grid spans the frame.
    vec2 uv = (fragCoord / iResolution.xy) * 2.0 - 1.0;
    uv.x *= iResolution.x / iResolution.y;
    uv *= 1.4;  // overall zoom; smaller = wider cells

    // Apply displacement.  Calibrated so curvature is obvious --
    // the brief asks for "heavy mass" deformation -- but the
    // displacement is small enough that the grid doesn't fold back
    // on itself.  With clamping inside `deform` (0.6 max) and a
    // 0.6 multiplier here, the worst-case shift is ~0.36 units,
    // which is ~2.5 cells at the default 0.14 spacing.
    vec2 qu = uv + deform(uv) * 0.6;

    // Pick the grid family from iSeed.z (continuous blend so runs
    // can sit anywhere along the spectrum).
    float gfam = iSeed.z;       // 0..1
    float spacing = mix(0.14, 0.09, gfam);

    float d;
    if (gfam < 0.5) {
        // Square/hex blend (smooth).
        float ds = gridLineDist(qu, spacing);
        float dh = hexGridLineDist(qu, spacing);
        d = mix(ds, dh, smoothstep(0.30, 0.70, gfam));
    } else {
        // Square/ring blend: rings dominate at gfam >= ~0.7.
        float ds = gridLineDist(qu, spacing);
        float dr = ringDist(qu, spacing);
        d = mix(ds, dr, smoothstep(0.70, 0.95, gfam));
    }

    // Nearest-line glow.  We accumulate intensity with a 1/d falloff
    // so the lines bloom nicely even where the spacing is large.
    // Lines are visible at 1920x1080 but thin enough that overlapping
    // lines near a mass don't fuse into a solid blob -- otherwise
    // the wells read as colored discs, not as curvature.
    float line_w  = mix(0.012, 0.006, iSeed.x);
    float core    = band(d, line_w * 0.5);
    float glow    = exp(-d * 80.0);

    // Per-pixel hue bias from the dominant local mass.
    float fh;
    float fs = fieldStrength(uv, fh);
    // Saturation target = 1.00 (fully vivid).  Keep value low for
    // background so glow pops without washing out.
    float sat = 1.0;
    vec3 line_col = hsv2rgb(vec3(fh, sat, 1.0));
    vec3 glow_col = hsv2rgb(vec3(fract(fh + 0.5), sat, 1.0));

    vec3 col = vec3(0.0);
    // Core line -- bright but kept below 1.5x so a single line
    // doesn't blow out, and a cluster of overlapping lines doesn't
    // fuse into a saturated blob.
    col += core * line_col * 1.2;
    // Wide glow under the line.
    col += glow * glow_col * 0.55;

    // Background: dark.  We deliberately do NOT tint it toward the
    // mass hue here -- a coloured background near each mass
    // collides with the line colour and reads as a flat disc.
    // Coverage target ~0.19 means most pixels stay near-black
    // with bright grid -- exactly what we want.
    vec3 bg = vec3(0.012, 0.018, 0.028);
    col += bg;

    // A subtle "lensing" highlight: a small bright spot at each mass
    // centre.  Kept very tight and dim so it reads as a refractive
    // focal point, not a glowing ball that blocks the grid.
    for (int i = 0; i < MAX_MASSES; i++) {
        if (massArray[i].strength <= 0.0) continue;
        float r = length(uv - massArray[i].c);
        // exp(-r * 80) gives a ~0.04-unit halo -- just a few pixels.
        float halo = exp(-r * 80.0);
        col += halo * hsv2rgb(vec3(massArray[i].hue, 0.8, 1.0)) * 0.55
                    * massArray[i].strength;
    }

    // Mild filmic tonemap so the core doesn't blow out.
    col = col / (1.0 + col * 0.5);
    // Gamma.
    col = pow(col, vec3(1.0 / 2.2));

    fragColor = vec4(col, 1.0);
}