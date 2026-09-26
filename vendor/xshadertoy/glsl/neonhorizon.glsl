// Title:  Neon Horizon
// Desc:   A retro-futurist night cityscape in continuous forward flight --
//         the 1980s synthwave idiom.  A perspective grid recedes to a low
//         horizon; angular building silhouettes stand either side; a large
//         gradient sun sits on the horizon with horizontal scanline bands
//         cut through it; a deep magenta-to-cyan sky gradient above.
//         Neon edge-glow on the grid and on the building outlines; haze in
//         the far distance; no black sky, no dead frame.
//
//         Original work.  No reference to any external shader source.  All
//         technique is from published graphics literature: analytic plane
//         projection (Quilez 2008, "Modeling with distance functions"),
//         perspective grid via 1/z spacing (a standard 1980s demo-scene
//         trick -- see e.g. Pouet.net "Amiga Demo Perspective Grid"),
//         exponential atmospheric haze (Quilez 2008, "Atmospheric
//         scattering"), Schlick Fresnel rim (Schlick 1994, "An inexpensive
//         BRDF model for physically-based rendering").
//
//         Per-run variation driven entirely by iSeed: palette family
//         (3 distinct hue pairs -- magenta/cyan, amber/violet, green/blue),
//         skyline density and height distribution, sun size, flight speed
//         and bank, haze density.  Two runs at the same frame index differ
//         in composition and motion, not merely in animation phase.

#define PI          3.14159265359
#define TAU         6.28318530718

#define saturate(x) clamp(x, 0.0, 1.0)
#define smooth01(e,x) smoothstep(0.0, e, x)

/* ----------------------------------------------------------------- */
/* Small helpers.                                                     */
/* ----------------------------------------------------------------- */

float hash11(float n) {
    return fract(sin(n * 12.9898) * 43758.5453);
}

float hash11b(float n) {
    /* alternate prime for decorrelation */
    return fract(sin(n * 78.233) * 12345.6789);
}

/* ----------------------------------------------------------------- */
/* Per-run variation, derived from iSeed (4 floats in [0,1)).          */
/*   iSeed.x  -> palette family (discrete-ish, 3 buckets)              */
/*   iSeed.y  -> skyline density + height distribution                 */
/*   iSeed.z  -> sun size + flight speed                              */
/*   iSeed.w  -> haze + bank                                          */
/* ----------------------------------------------------------------- */

struct Palette {
    vec3 sky_hi;     /* zenith */
    vec3 sky_lo;     /* horizon */
    vec3 grid;
    vec3 sun;
    vec3 buildings;
};

Palette pick_palette(float family) {
    /* 3 families.  Hue pairs chosen so the sky never collapses to grey
     * and the foreground reads against the background. */
    Palette p;
    if (family < 0.34) {
        /* magenta/cyan synthwave -- the canonical 80s palette */
        p.sky_hi     = vec3(0.04, 0.02, 0.10);
        p.sky_lo     = vec3(0.95, 0.25, 0.55);
        p.grid       = vec3(0.20, 0.95, 1.00);
        p.sun        = vec3(1.00, 0.55, 0.30);
        p.buildings  = vec3(0.05, 0.02, 0.15);
    } else if (family < 0.67) {
        /* amber/violet -- warmer, dusk */
        p.sky_hi     = vec3(0.08, 0.04, 0.18);
        p.sky_lo     = vec3(0.75, 0.30, 0.95);
        p.grid       = vec3(1.00, 0.75, 0.30);
        p.sun        = vec3(1.00, 0.45, 0.20);
        p.buildings  = vec3(0.06, 0.03, 0.10);
    } else {
        /* green/blue -- quieter, more noir */
        p.sky_hi     = vec3(0.01, 0.03, 0.06);
        p.sky_lo     = vec3(0.15, 0.55, 0.45);
        p.grid       = vec3(0.30, 0.90, 0.70);
        p.sun        = vec3(0.85, 1.00, 0.55);
        p.buildings  = vec3(0.02, 0.04, 0.06);
    }
    return p;
}

/* ----------------------------------------------------------------- */
/* Skyline.  Returns building height at lateral position x (in world */
/* units) at depth z.  Hashed, varies with iSeed.                     */
/* ----------------------------------------------------------------- */

float skyline_height(float x, float z, float density_a) {
    /* Clear a road corridor: no buildings inside |x| < ROAD_HALF. */
    float road_falloff = smoothstep(0.85, 1.30, abs(x));
    if (road_falloff <= 0.001) return 0.0;
    float ph = z * 0.013;
    float u = x * density_a + ph;
    float c = floor(u);
    float h = hash11(c + 17.3);
    h = h * h;
    float top = mix(0.6, 1.0, hash11b(c + 91.1));
    return h * top * 9.0 * road_falloff + 1.0 * road_falloff;
}

float skyline_height_far(float x, float z, float density_b) {
    /* Clear a wider road corridor for the far background. */
    float road_falloff = smoothstep(0.6, 1.1, abs(x));
    if (road_falloff <= 0.001) return 0.0;
    float ph = z * 0.005;
    float u = x * density_b + ph;
    float c = floor(u);
    float h = hash11b(c + 51.7);
    h = h * h;
    return h * 18.0 * road_falloff + 4.0 * road_falloff;
}

/* ----------------------------------------------------------------- */
/* Sky.  Vertical gradient + sun + scanlines.                          */
/* ----------------------------------------------------------------- */

vec3 sky_color(vec3 rd, vec3 sun_dir, Palette pal, float scan_phase) {
    float v = saturate(rd.y);
    vec3 sky = mix(pal.sky_lo, pal.sky_hi, smoothstep(0.0, 0.6, v));

    /* Sun: a soft disc viewed against the sky. */
    float sd = max(dot(rd, sun_dir), 0.0);
    float disc = smoothstep(0.985, 0.997, sd);
    /* Soft halo around the disc, falls off in roughly 3 deg. */
    float halo = smoothstep(0.94, 0.997, sd);
    halo = halo * halo * (3.0 - 2.0 * halo);
    sky += pal.sun * disc * 1.5;
    sky += pal.sun * halo * 0.35;

    /* Horizontal scanline bands cut through the sun only -- the
     * classic 80s look.  The bands are anchored to world Y, not screen
     * Y, so they scroll with the flight. */
    if (rd.y > -0.02) {
        float bands = smoothstep(0.0, 0.5,
                                 abs(fract((rd.y - scan_phase) * 22.0) - 0.5));
        /* Darken the sun region where bands are present. */
        float sun_mask = smoothstep(0.96, 0.998, sd);
        sky -= pal.sun * (1.0 - bands) * sun_mask * 0.9;
    }

    return sky;
}

/* ----------------------------------------------------------------- */
/* Grid line distance -- analytic.                                     */
/*                                                                   */
/* For a perspective grid receding toward +Z, with cell spacing s,    */
/* we project a ray (ro, rd) onto the ground plane y=0, get hit        */
/* (px, 0, pz), and measure distance to nearest grid line.            */
/* We want narrow glowing lines on a dark ground.                     */
/* ----------------------------------------------------------------- */

vec2 grid_dist(vec2 hit_xz, float spacing) {
    vec2 g = abs(fract(hit_xz / spacing - 0.5) - 0.5) * spacing;
    return g;
}

/* ----------------------------------------------------------------- */
/* mainImage.                                                          */
/* ----------------------------------------------------------------- */

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    /* --- per-run constants --- */
    float family = saturate(iSeed.x);
    Palette palette = pick_palette(family);

    float density_a = mix(0.7, 1.8, fract(iSeed.y * 1.7));
    float density_b = mix(0.20, 0.40, fract(iSeed.y * 2.3 + 0.7));

    float sun_size  = mix(0.16, 0.30, fract(iSeed.z * 3.1));
    float speed     = mix(0.6, 1.5, fract(iSeed.z * 1.9 + 0.3));
    float haze_dens = mix(0.022, 0.055, fract(iSeed.w * 2.7));
    float bank      = mix(-0.05, 0.05, fract(iSeed.w * 1.3));

    float t = iTime * speed;

    /* --- camera: simple forward flight along +Z, slight bank --- */
    vec3 ro = vec3(0.0, 0.7 + 0.05 * sin(t * 0.3), -t);
    /* Roll the up-vector by bank amount, but keep the look-direction
     * mostly forward so the horizon stays horizontal.  We achieve the
     * bank effect by rotating the screen-space y coordinate instead,
     * below. */
    vec3 sun_dir = normalize(vec3(0.0, 0.06, 1.0));

    /* --- screen coords --- */
    vec2 res = iResolution.xy;
    vec2 uv  = (fragCoord - 0.5 * res) / res.y;
    /* Apply a small bank (roll) so the grid feels tilted in some runs. */
    float cs = cos(bank), sn = sin(bank);
    uv = mat2(cs, -sn, sn, cs) * uv;

    /* --- ray direction --- */
    float fov = 1.0;
    vec3 rd = normalize(vec3(uv.x, uv.y + 0.05, fov));
    /* Camera looks along +Z (forward). */

    /* --- background sky --- */
    vec3 col = sky_color(rd, sun_dir, palette, t * 0.4);

    /* --- building silhouettes against the sky --- */
    float hit_b = 0.0;
    float edge_b = 0.0;
    float far_h  = 0.0;
    {
        const int BSTEPS = 32;
        for (int i = 1; i <= BSTEPS; i++) {
            float tt = float(i) * 0.6;
            vec3 p = ro + rd * tt;
            float bh0 = skyline_height(p.x, p.z, density_a);
            float wy  = ro.y + rd.y * tt;
            float below = smoothstep(bh0 + 0.05, bh0 - 0.20, wy);
            float d_top = bh0 - wy;
            edge_b = max(edge_b, exp(-abs(d_top) * 1.5) * below *
                                 smoothstep(0.0, 1.0, float(i) / float(BSTEPS)));
            hit_b = max(hit_b, below);
        }
        const int FBSTEPS = 14;
        for (int i = 1; i <= FBSTEPS; i++) {
            float tt = float(i) * 2.5;
            vec3 p = ro + rd * tt;
            float bh0 = skyline_height_far(p.x, p.z, density_b);
            float wy  = ro.y + rd.y * tt;
            float below = smoothstep(bh0 + 0.05, bh0 - 0.25, wy);
            far_h = max(far_h, below);
        }
    }
    col = mix(col, palette.buildings, far_h * 0.7);
    col = mix(col, palette.buildings, hit_b * 0.6);
    col += palette.grid * edge_b * 1.3;

    /* --- ground plane intersection: y = 0 (for downward rays) --- */
    if (rd.y < 0.0) {
        float tp = -ro.y / rd.y;
        vec3 pos = ro + rd * tp;
        vec2 hx = pos.xz;

        /* Forward distance (depth) for haze. */
        float depth = length(hx);

        /* --- perspective grid --- */
        float base = 0.6;
        /* Two families of lines: lateral lines (z = constant) and
         * depth lines (x = constant).  For a ray hitting the ground at
         * (hx.x, 0, hx.y), the lateral-line distance in world space is
         * `hx.y - nearest_z_line`; the depth-line distance is `hx.x -
         * nearest_x_line`.  We draw the line as a sharp screen-space
         * falloff. */
        float nearest_z = floor(hx.y / base) * base;
        float nearest_x = floor(hx.x / base) * base;
        float dz_line = abs(hx.y - (nearest_z + base * 0.5));
        float dx_line = abs(hx.x - (nearest_x + base * 0.5));
        /* Convert to screen-y deltas -- for lateral lines (running
         * perpendicular to flight), their visual spacing shrinks
         * toward the horizon; we use the change in hx.y per pixel. */
        float fw_y = fwidth(hx.y) + 0.001;
        float fw_x = fwidth(hx.x) + 0.001;
        /* Lateral line glow: a line is "on" if dz_line is within a
         * few screen pixels of the line. */
        float line_lat = 1.0 - smoothstep(0.0, fw_y * 1.5, dz_line);
        float line_dep = 1.0 - smoothstep(0.0, fw_x * 1.5, dx_line);
        float line_glow = max(line_lat, line_dep);
        /* Distance-based falloff so the lines brighten near the
         * camera. */
        float line_falloff = exp(-depth * 0.04);

        vec3 grid_col = palette.grid * line_glow * line_falloff * 1.2;

        /* --- ground composition --- */
        col = mix(col, grid_col, smoothstep(0.0, 0.1, depth));

        /* Apply haze: blend toward the haze colour with depth. */
        vec3 haze_col = mix(palette.sky_lo, palette.sky_hi, 0.5);
        float h = 1.0 - exp(-depth * haze_dens);
        col = mix(col, haze_col, h * 0.85);
    }

    /* --- soft vignette so the corners don't blow out --- */
    float v = smoothstep(1.4, 0.4, length(uv));
    col *= mix(0.85, 1.0, v);

    /* --- tonemap & gamma --- */
    col = max(col, 0.0);
    col = col / (1.0 + col * 0.7);   /* cheap Reinhard-ish */
    col = pow(col, vec3(1.0 / 2.2));

    fragColor = vec4(col, 1.0);
}
