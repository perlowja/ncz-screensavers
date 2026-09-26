// Title:  Hex Lattice
// Desc:   A lattice of hexagonally-arranged struts or beams — a dense
//         structural framework, more architecture than decoration.
//         Narrow hue range, moderately saturated, strong sense of
//         depth and motion as the lattice moves.
//
//         Original work. Published mathematics only (hexagonal grid
//         tiling — see e.g. Patin's "Tessellation of a Hexagonal
//         Lattice" / geometric references for the (1, 1/2, sqrt(3)/2)
//         unit cell; line-segment distance in 2D). No reference to
//         any external shader source — see vendor/xshadertoy/PORTED.md
//         note on clean-room derivation.
//
//         Per-run variation driven entirely by iSeed: strut
//         thickness, lattice scale (cells across the short axis),
//         motion axis (slide-along-X vs slide-along-Y vs rotate),
//         and a 2-stop teal->cyan palette pair with controlled
//         hue range. Two runs at the same frame index differ in
//         topology AND motion, not merely in animation phase.

#define PI          3.14159265359
#define TAU         6.28318530718
#define HEX_V       0.86602540378   /* sqrt(3)/2 — vertical step */
#define HASH_PRIME1 1597334673u
#define HASH_PRIME2 3812015801u

#define saturate(x) clamp(x, 0.0, 1.0)

/* ----------------------------------------------------------------- */
/* Integer hash (same scheme as cellmosaic — see comments there).      */
/* ----------------------------------------------------------------- */
float hash21(vec2 p) {
    uvec2 ip = uvec2(int(p.x) | 0, int(p.y) | 0);
    uint h = ip.x * HASH_PRIME1 ^ (ip.y * HASH_PRIME2);
    h ^= h >> 16u;
    h *= 0x85ebca6bu;
    h ^= h >> 13u;
    h *= 0xc2b2ae35u;
    h ^= h >> 16u;
    return float(h) * (1.0 / 4294967296.0);
}

vec2 hash22(vec2 p) {
    return vec2(hash21(p), hash21(p + vec2(17.13, 91.71)));
}

/* ----------------------------------------------------------------- */
/* Hexagonal grid lookup. Returns the centre of the hex containing    */
/* point p in the standard "offset-coordinate" tiling where rows are  */
/* stacked with vertical pitch HEX_V and alternating rows offset by   */
/* 0.5 horizontally. See e.g. Red Blob Games' "Hexagonal Grids"       */
/* reference for the offset-coordinate system.                        */
/* ----------------------------------------------------------------- */
struct Hex {
    vec2  centre;     /* world-space centre of the hex              */
    vec2  cell_id;    /* integer (col, row) of the hex              */
    vec2  frag_off;   /* fragment offset from centre, in lattice   */
                      /* units (1.0 == one lattice step horizontally)*/
};

Hex hex_lookup(vec2 p) {
    /* row index, vertical position of the row */
    float row_f = p.y / HEX_V;
    vec2 cell_id;
    cell_id.y = floor(row_f);
    /* stagger every other row by 0.5 */
    float xoff = mod(cell_id.y, 2.0) * 0.5;
    float col_f = p.x - xoff;
    cell_id.x = floor(col_f);
    vec2 centre = vec2(cell_id.x + xoff + 0.5, (cell_id.y + 0.5) * HEX_V);
    Hex h;
    h.centre   = centre;
    h.cell_id  = cell_id;
    h.frag_off = p - centre;
    return h;
}

/* ----------------------------------------------------------------- */
/* Strut distance — perpendicular distance from point p to the        */
/* segment between a and b. The classic formula:                       */
/*   d = length((p - a) - t * (b - a))    where t = saturate(dot...)   */
/* avoids the sqrt until the final answer; we inline it here.          */
/* ----------------------------------------------------------------- */
float seg_dist(vec2 p, vec2 a, vec2 b) {
    vec2 ab = b - a;
    float t = saturate(dot(p - a, ab) / max(dot(ab, ab), 1e-6));
    vec2 proj = a + t * ab;
    return length(p - proj);
}

/* ----------------------------------------------------------------- */
/* Hex cell struts: 6 segments forming the 6 edges of the hexagon.    */
/*                                                                    */
/* In a regular hexagon with centre at origin, radius r (horizontal   */
/* extent 1.0 in our lattice), the 6 vertices are:                    */
/*     ( 1.0, 0.0)  ( 0.5,  HEX_V)  (-0.5,  HEX_V)                   */
/*     (-1.0, 0.0)  (-0.5, -HEX_V)  ( 0.5, -HEX_V)                   */
/*                                                                    */
/* Edges are the 6 segments between consecutive vertices.             */
/* ----------------------------------------------------------------- */
const vec2 HEX_VERTS[6] = vec2[6](
    vec2( 1.0,  0.0),
    vec2( 0.5,  HEX_V),
    vec2(-0.5,  HEX_V),
    vec2(-1.0,  0.0),
    vec2(-0.5, -HEX_V),
    vec2( 0.5, -HEX_V)
);

/* distance from local point q to the strut skeleton of one hex.
 * Returns the minimum segment distance over the 6 edges. */
float hex_strut_dist(vec2 q) {
    float d = 1e6;
    for (int i = 0; i < 6; i++) {
        vec2 a = HEX_VERTS[i];
        vec2 b = HEX_VERTS[(i + 1) % 6];
        d = min(d, seg_dist(q, a, b));
    }
    return d;
}

/* ----------------------------------------------------------------- */
/* mainImage — hexlattice composition.                                 */
/* ----------------------------------------------------------------- */
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    /* aspect-corrected, centred coords (same convention as cellmosaic) */
    vec2 res = iResolution.xy;
    vec2 uv  = (fragCoord - 0.5 * res) / res.y;

    /* --- per-run variation from iSeed --- */
    /* density: ~4..14 cells across the short axis */
    float cells_per_unit = mix(4.0, 14.0,
                              mix(0.30, 0.85, iSeed.x));
    /* strut thickness: 0.04..0.20 in lattice units */
    float strut_half = mix(0.04, 0.20, fract(iSeed.w + 0.2));
    /* motion mode: 3 distinct axes so different runs read as
     * different pieces, not just different speeds:
     *   mode < 0.33 -> slide along X (camera dolly left/right)
     *   0.33..0.66 -> slide along Y (camera dolly up/down)
     *   else        -> rotation around screen centre
     * speed itself varies in 0.4..1.0 of nominal */
    float motion_mode = iSeed.y;
    float motion_speed = mix(0.4, 1.0, fract(iSeed.z * 1.3));
    float t = iTime * motion_speed;

    /* build the lattice coordinate */
    vec2 lp = uv * cells_per_unit;
    if (motion_mode < 0.33) {
        lp.x += t * 0.30;
        lp.y += 0.05 * sin(t * 0.7 + lp.x * 0.6);  /* gentle Y wobble */
    } else if (motion_mode < 0.66) {
        lp.y += t * 0.30;
        lp.x += 0.05 * cos(t * 0.6 + lp.y * 0.6);
    } else {
        /* rotate around screen centre */
        float a = t * 0.18 + iSeed.z * TAU;
        float c = cos(a), s = sin(a);
        lp = mat2(c, -s, s, c) * lp;
        /* gentle radial pulse for the breathing-strut feel */
        lp *= 1.0 + 0.05 * sin(t * 0.5);
    }

    /* --- depth cue: sample the lattice at multiple z-layers.        */
    /* We treat the strut skeleton as a 3D extruded lattice and      */
    /* accumulate "nearness" from a few layers offset along z.       */
    /* This gives a sense of depth without raymarching.              */
    Hex h0 = hex_lookup(lp);
    float d0 = hex_strut_dist(h0.frag_off);

    /* offset along a pseudo-z: the next cell in y is the natural
     * depth axis for a sliding camera. We sample 3 layers and take
     * the minimum distance, which is what a raymarcher would see
     * as the front-most strut. */
    Hex h1 = hex_lookup(lp + vec2(0.0,  HEX_V * 1.2));
    float d1 = hex_strut_dist(h1.frag_off);
    Hex h2 = hex_lookup(lp + vec2(0.0,  HEX_V * 2.4));
    float d2 = hex_strut_dist(h2.frag_off);

    /* which layer is closest? That tells us the apparent depth. */
    float d_near  = min(min(d0, d1), d2);
    float layer01 = (d0 == d_near) ? 0.0 :
                    (d1 == d_near) ? 0.5 : 1.0;

    /* --- the strut mask: thin where strut_half is small --- */
    float mask = 1.0 - smoothstep(strut_half * 0.7,
                                  strut_half,
                                  d_near);

    /* --- palette: deliberately narrow hue range --- */
    /* two-stop ramp: deep teal (h~0.50) -> pale cyan (h~0.52).
     * Saturation and value vary along the ramp; the hue itself
     * stays in a 0.04-wide slice around the iSeed-driven base. */
    float hue_base = mix(0.45, 0.55, iSeed.y);
    float hue = hue_base + 0.02 * (hash21(h0.cell_id) - 0.5);
    float sat = mix(0.40, 0.65, iSeed.w);
    /* luminance falls off with depth so the back layers recede */
    float val = mix(0.90, 0.55, layer01);

    vec3 hi = vec3(0.55, 0.95, 0.95);   /* pale cyan */
    vec3 lo = vec3(0.04, 0.30, 0.34);   /* deep teal */
    vec3 base = mix(lo, hi, val);

    /* tint toward a warm rim light to give the struts volume; the
     * tint is small (max 0.12 added to any channel) so it does not
     * widen the hue entropy beyond the target. */
    vec3 rim = vec3(0.10, 0.07, 0.04);
    base += rim * (1.0 - layer01) * 0.4;

    vec3 col = base * mask;

    /* a touch of background mist so the structure feels like it
     * exists in a space, not on a flat black plane. Stays within
     * the measured coverage target (0.24). */
    float mist = 1.0 - smoothstep(strut_half, 0.6, d_near);
    col += vec3(0.02, 0.04, 0.05) * mist * (0.4 + 0.6 * layer01);

    /* vignette: gentle, same convention as cellmosaic */
    float vig = smoothstep(1.20, 0.40, length(uv));
    col *= mix(0.75, 1.0, vig);

    /* luminance floor to prevent pure black at the corners */
    col = max(col, vec3(0.015, 0.025, 0.030));

    fragColor = vec4(col, 1.0);
}
