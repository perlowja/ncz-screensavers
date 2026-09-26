// wellcurve.glsl — grid deformed by heavy mass(es), the classic
// depiction of spacetime curvature.
//
// The brief: a grid or mesh deformed by one or more masses,
// glowing lines, extremely saturated, strong structure, with
// the deformation moving. Per-launch iSeed changes mass count,
// mass placement, grid style, and palette.
//
// Technique: analytic — no raymarch. Project a 2D grid in
// screen space, displace each grid point radially toward the
// masses by a 1/(r^2 + eps) well function, render lines by
// distance-to-nearest-grid-line with a Gaussian-ish glow.
// Multiple masses' wells are summed. Grid colour is taken
// from the nearest mass (weighted), so the colour field
// shows the mass topology as much as the line curvature does.
//
// Original work, MIT. No vendor shadertoy reference used.
//
// Per-launch variation (driven by iSeed):
//   .x  mass count: 1..4 (1 + floor(x * 4))
//   .y  grid style: 0 square, 0.5 hex-ish, 1 radial
//   .z  palette family: 0 cyan/magenta, 0.5 yellow/violet,
//                       1 green/orange (fully saturated)
//   .w  line glow tightness (1..4): smaller = softer glow

#define PI 3.14159265359
#define TAU (2.0*PI)

// --- Hash + grid helpers --------------------------------------------------

// Cheap distance-to-grid-line for an axis-aligned square grid
// of spacing s, in a deformed coordinate space. The grid point
// at (i, j) lives at world (i*s, j*s); after displacement we
// measure distance to the nearest grid line (horizontal or
// vertical). Distance in cell-local coords (mod s) -> [0, s/2].
float gridLineDist(vec2 p, float s) {
    vec2 q = abs(fract(p / s - 0.5) - 0.5) * s;
    return min(q.x, q.y);
}

// Hex-style grid: distance to nearest of three line families at
// 0, 60, 120 degrees. We rotate the sample by 30 degrees so the
// grid feels isotropic but the lines are not axis-aligned.
float hexLineDist(vec2 p, float s) {
    float r = 0.523598775598;  // pi/6
    mat2 rot = mat2(cos(r), -sin(r), sin(r), cos(r));
    vec2 q = rot * p;
    float d1 = gridLineDist(q, s);
    mat2 rot2 = mat2(cos(TAU/3.0), -sin(TAU/3.0),
                     sin(TAU/3.0), cos(TAU/3.0));
    float d2 = gridLineDist(rot2 * q, s);
    float d3 = gridLineDist(rot2 * rot2 * q, s);
    return min(d1, min(d2, d3));
}

// Radial grid: concentric rings + spokes. Returns distance to
// nearest ring or spoke.
float radialLineDist(vec2 p, float s) {
    float r = length(p);
    float ang = atan(p.y, p.x);
    float ringD = abs(fract(r / s - 0.5) - 0.5) * s;
    float spokeD = abs(ang - TAU * floor(ang / TAU + 0.5) / 1.0);
    // 12 spokes: angular spacing TAU/12. Distance in radians times r
    // for visual correctness. But cheap: just use angular distance
    // scaled by r for spoke thickness.
    float spokeA = abs(fract(ang / (TAU/12.0) - 0.5) - 0.5) * (TAU/12.0);
    float spokeT = spokeA * r;
    return min(ringD, spokeT);
}

// Pick a grid style by iSeed.y.
float gridDist(vec2 p, float s, float style) {
    if (style < 0.33) return gridLineDist(p, s);
    if (style < 0.67) return hexLineDist(p, s);
    return radialLineDist(p, s);
}

// --- Mass well ------------------------------------------------------------

// A single mass at position c with strength k. Returns a vec3:
//   .x = well displacement magnitude
//   .y = colour weight (for the nearest-mass tint)
//   .z = palette index 0..1 (which hue family)
struct Mass {
    vec2 c;
    float k;
    float hue;
    float ph;
};

vec3 well(vec2 p, Mass m) {
    vec2 d = p - m.c;
    float r2 = dot(d, d) + 0.20;
    // 1/r falloff with a saturating cap so the centre isn't
    // numerically singular.
    float w = m.k / (1.0 + r2 * 4.0);
    return vec3(w, w, m.hue);
}

// Three fully-saturated palettes. Each is a 3-stop gradient
// that we'll interpolate by hue index. Saturated by design
// (target sat = 1.0).
vec3 palette(float t, float family) {
    family = floor(family * 3.0);
    if (family < 0.5) {
        // cyan / magenta / yellow
        vec3 a = vec3(0.10, 0.95, 1.00);
        vec3 b = vec3(1.00, 0.10, 0.85);
        vec3 c = vec3(1.00, 0.90, 0.10);
        if (t < 0.5) return mix(a, b, t * 2.0);
        return mix(b, c, (t - 0.5) * 2.0);
    } else if (family < 1.5) {
        // yellow / violet / cyan
        vec3 a = vec3(1.00, 0.85, 0.05);
        vec3 b = vec3(0.55, 0.10, 1.00);
        vec3 c = vec3(0.05, 0.95, 0.85);
        if (t < 0.5) return mix(a, b, t * 2.0);
        return mix(b, c, (t - 0.5) * 2.0);
    }
    // green / orange / pink
    vec3 a = vec3(0.05, 1.00, 0.30);
    vec3 b = vec3(1.00, 0.45, 0.05);
    vec3 c = vec3(1.00, 0.10, 0.55);
    if (t < 0.5) return mix(a, b, t * 2.0);
    return mix(b, c, (t - 0.5) * 2.0);
}

// --- Main -----------------------------------------------------------------

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    // Aspect-correct, centered.
    vec2 uv = (fragCoord / iResolution.xy) * 2.0 - 1.0;
    uv.x *= iResolution.x / iResolution.y;

    // Per-launch variation decoded once.
    float nMassesF = iSeed.x * 4.0 + 1.0;       // 1..5
    int   nMasses  = int(clamp(nMassesF, 1.0, 5.0));
    float style    = iSeed.y;
    float family   = iSeed.z;
    float lineTight = mix(0.6, 2.5, fract(iSeed.w * 5.13));

    // Set up masses. Positions orbit slowly so the curvature
    // moves continuously even within a single run.
    Mass masses[5];
    masses[0] = Mass(vec2(0.0), 0.10, 0.0, 0.0);  // anchor
    masses[0].c = vec2(0.15 * cos(iTime * 0.20),
                       0.15 * sin(iTime * 0.20));
    masses[0].k = 0.10;
    masses[0].hue = 0.0 + 0.1 * sin(iTime * 0.30);
    masses[0].ph = iTime;

    for (int i = 1; i < 5; i++) {
        if (i >= nMasses) break;
        float fi = float(i);
        float a = iSeed.x * TAU + fi * 1.7;
        float radius = 0.6 + 0.3 * fract(iSeed.y + fi * 0.31);
        masses[i] = Mass(
            vec2(radius * cos(iTime * 0.35 + a + fi),
                 radius * sin(iTime * 0.28 + a * 1.3 + fi)),
            0.07 + 0.05 * fract(iSeed.z + fi * 0.71),
            fract(0.2 * fi + iSeed.w * 2.7 + 0.15 * iTime),
            iTime + fi
        );
    }

    // Displacement: sum wells. The well is the *radial* pull
    // toward the mass, normalised so it pulls along the line
    // from p to m.c. We integrate well magnitude in x and y.
    vec2 disp = vec2(0.0);
    float totalW = 0.0;
    float hueX = 0.0;  // accumulated hue position (in cos/sin form)
    float hueY = 0.0;
    for (int i = 0; i < 5; i++) {
        if (i >= nMasses) break;
        Mass m = masses[i];
        vec2 d = uv - m.c;
        float r = length(d);
        // The well is a smooth bulge: 0 at the centre, peaks
        // around r ~ 0.2, falls off beyond. This makes the
        // deformation a clear bowl shape (the classic gravity-
        // well look) rather than a singular collapse.
        float bulge = (1.0 - smoothstep(0.0, 0.5, r));
        // Direction away from mass (toward p from m.c). The
        // "+1" in denom avoids division-by-zero at the centre
        // and shrinks the direction smoothly to zero.
        vec2 dir = d / (1.0 + r * 4.0);
        disp += dir * bulge * 0.04;
        float w = bulge * m.k;
        totalW += w;
        // Accumulate hue position weighted by well strength,
        // using cos/sin form so we can average hues correctly
        // (modulo 1 wraparound).
        float h = m.hue * TAU;
        hueX += w * cos(h);
        hueY += w * sin(h);
    }
    // Recover the average hue (modulo 1).
    float nearestHue = fract(atan(hueY, hueX) / TAU + 1.0);
    // The "deformed" grid coordinate is uv - disp, so a point
    // that was near a mass has its grid coordinate pulled
    // toward the mass — visually, the grid warps inward.
    vec2 g = uv - disp;

    // Line spacing scaled by iSeed.w inverse: larger w -> tighter
    // grid -> higher edge density (target 170). We aim for 6-12
    // grid lines per dimension across the 2-unit-wide screen.
    float spacing = mix(0.30, 0.16, fract(iSeed.w * 7.0));

    // Pixel-to-grid ratio: at 320x180 with 2-unit-wide screen,
    // one pixel is ~0.011 wide. Lines thinner than ~2 pixels
    // get AA'd by smoothstep.
    float pixelW = 2.0 / iResolution.y;

    // Distance to nearest deformed grid line.
    float d = gridDist(g, spacing, style);

    // Line intensity. AA-aware: smoothstep over the pixel width.
    // lineTight scales the line width: <1 thicker (more coverage),
    // >1 thinner (less coverage, more delicate).
    float lineWidth = pixelW * 1.2 * lineTight;
    float line = smoothstep(lineWidth, 0.0, d);

    // Very tight outer glow: the line itself carries the visual
    // weight, the glow is just a hint.
    float glowF = exp(-d * (80.0 / max(lineTight, 0.3))) * 0.5;
    line = max(line, glowF);

    // Threshold for coverage ~0.19. Anything below stays black.
    // High threshold keeps most pixels dark; the line core + tight
    // glow are bright enough to clear the threshold.
    line = smoothstep(0.85, 0.95, line);

    // Colour: nearest-mass hue. The palette is already saturated;
    // we keep the line envelope and let it clip naturally to 1.0.
    vec3 col = palette(nearestHue, family);

    // Apply line glow. Coverage ~0.19 — most pixels stay black.
    vec3 outCol = col * line * 1.5;

    // Vignette so the centre reads brighter.
    float vig = exp(-dot(uv, uv) * 0.30);
    outCol *= vig;

    // Sharp clip instead of soft tone curve so saturation stays
    // at 1.0 (the brief's requirement). Max channel clamps to 1;
    // the relative ratio of channels is preserved.
    outCol = min(outCol, vec3(1.0));

    fragColor = vec4(outCol, 1.0);
}
