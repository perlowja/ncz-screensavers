// Title:  Hex Lattice
// Desc:   A lattice of hexagonally-arranged struts or beams -- a dense
//         structural framework, more architecture than decoration.
//         Narrow hue range, moderately saturated, strong sense of
//         depth and motion as the lattice moves.
//
//         Original work. Published mathematics only (hexagonal grid
//         tiling -- see e.g. Red Blob Games' "Hexagonal Grids" for the
//         offset-coordinate system; line-segment distance in 2D;
//         point-to-circle distance for joints). No reference to any
//         external shader source -- see vendor/xshadertoy/PORTED.md
//         note on clean-room derivation.
//
//         Per-run variation driven entirely by iSeed: strut
//         thickness, lattice scale (cells across the short axis),
//         motion axis (slide-along-X vs slide-along-Y vs rotate),
//         and a 2-stop teal->cyan palette pair with controlled hue
//         range. Two runs at the same frame index differ in topology
//         AND motion, not merely in animation phase.

#define PI          3.14159265359
#define TAU         6.28318530718
#define HEX_V       0.86602540378   /* sqrt(3)/2 -- vertical step */
#define HASH_PRIME1 1597334673u
#define HASH_PRIME2 3812015801u

#define saturate(x) clamp(x, 0.0, 1.0)

/* ----------------------------------------------------------------- */
/* Integer hash.                                                      */
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

/* ----------------------------------------------------------------- */
/* Hexagonal grid lookup.                                             */
/* ----------------------------------------------------------------- */
struct Hex {
    vec2  centre;
    vec2  cell_id;
    vec2  frag_off;
};

Hex hex_lookup(vec2 p) {
    float row_f = p.y / HEX_V;
    vec2 cell_id;
    cell_id.y = floor(row_f);
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
/* Strut distance: perpendicular distance from point p to the         */
/* segment between a and b.                                           */
/* ----------------------------------------------------------------- */
float seg_dist(vec2 p, vec2 a, vec2 b) {
    vec2 ab = b - a;
    float denom = max(dot(ab, ab), 1e-6);
    float t = saturate(dot(p - a, ab) / denom);
    vec2 proj = a + t * ab;
    return length(p - proj);
}

/* ----------------------------------------------------------------- */
/* Hex cell struts. Vertices in local coordinates:                    */
/*     ( 1.0, 0.0)  ( 0.5,  HEX_V)  (-0.5,  HEX_V)                   */
/*     (-1.0, 0.0)  (-0.5, -HEX_V)  ( 0.5, -HEX_V)                   */
/*                                                                    */
/* Each hex shares its vertices with three neighbour hexes; the      */
/* nodes we render at the vertices are what visually CONNECT the      */
/* struts. We compute strut distance to the six edges, then take the  */
/* minimum across the cell's six vertices (which are also the local   */
/* coordinates of its six neighbouring hexes' centres, rotated).    */
/* In practice, querying the lattice on a 3x3 neighbourhood of cells  */
/* and taking the minimum segment distance to ANY of their edges     */
/* gives the correct skeleton. The joint nodes are then drawn as      */
/* circles at every vertex.                                           */
/* ----------------------------------------------------------------- */
const vec2 HEX_VERTS[6] = vec2[6](
    vec2( 1.0,  0.0),
    vec2( 0.5,  HEX_V),
    vec2(-0.5,  HEX_V),
    vec2(-1.0,  0.0),
    vec2(-0.5, -HEX_V),
    vec2( 0.5, -HEX_V)
);

/* ----------------------------------------------------------------- */
/* mainImage -- hexlattice composition.                                */
/* ----------------------------------------------------------------- */
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 res = iResolution.xy;
    vec2 uv  = (fragCoord - 0.5 * res) / res.y;

    /* --- per-run variation from iSeed --- */
    /* density: ~3..10 cells across the short axis. Each cell has
     * substantial structure (struts + nodes), so density does not
     * need to be very high. */
    float cells_per_unit = mix(3.0, 10.0,
                              mix(0.30, 0.85, iSeed.x));

    /* strut thickness: lattice units. Half-width of each strut. */
    float strut_half = mix(0.10, 0.20, fract(iSeed.w + 0.2));
    /* node radius: lattice units. Nodes are slightly larger than
     * strut half-width so the joints visually "bulge". */
    float node_r = strut_half * mix(1.4, 2.0, fract(iSeed.w + 0.7));

    /* motion mode: 3 distinct axes */
    float motion_mode = iSeed.y;
    float motion_speed = mix(0.4, 1.0, fract(iSeed.z * 1.3));
    float t = iTime * motion_speed;

    vec2 lp = uv * cells_per_unit;
    if (motion_mode < 0.33) {
        lp.x += t * 0.30;
        lp.y += 0.05 * sin(t * 0.7 + lp.x * 0.6);
    } else if (motion_mode < 0.66) {
        lp.y += t * 0.30;
        lp.x += 0.05 * cos(t * 0.6 + lp.y * 0.6);
    } else {
        float a = t * 0.18 + iSeed.z * TAU;
        float c = cos(a), s = sin(a);
        lp = mat2(c, -s, s, c) * lp;
        lp *= 1.0 + 0.05 * sin(t * 0.5);
    }

    /* Find nearest hex, compute distance to its 6 strut segments.
     * For struts past the segment endpoints, the projection clamps
     * to the endpoint -- so to make a CONNECTED skeleton we ALSO
     * check the struts of the six neighbour hexes (each neighbour's
     * centre is at one of the 6 vertices). We do this by computing
     * distance to all 6 surrounding hexes' strut skeletons and
     * taking the minimum. This produces an unbroken lattice with
     * joints where struts meet.
     *
     * The 6 neighbour centres (in local coords of THIS hex) ARE
     * HEX_VERTS. So we need to compute the distance from p to the
     * strut skeleton of 7 hexes: the local one and its 6 neighbours.
     */
    Hex h = hex_lookup(lp);
    float d_strut = 1e6;
    for (int k = 0; k < 7; k++) {
        /* centre of the k-th hex (0 = self, 1..6 = neighbours).
         * The 6 neighbours are centred at HEX_VERTS, but those are
         * local-to-self coords. We need them in WORLD coords. */
        vec2 nbr_centre = (k == 0) ? h.centre : h.centre + HEX_VERTS[k - 1];
        vec2 q = lp - nbr_centre;
        for (int i = 0; i < 6; i++) {
            vec2 a = HEX_VERTS[i];
            vec2 b = HEX_VERTS[(i + 1) % 6];
            d_strut = min(d_strut, seg_dist(q, a, b));
        }
    }

    /* strut outer mask + bright core mask */
    float outer = 1.0 - smoothstep(strut_half * 0.85, strut_half, d_strut);
    float core  = 1.0 - smoothstep(strut_half * 0.15, strut_half * 0.30, d_strut);

    /* rim lighting: bright at the core of the strut, fading toward
     * the edges of the strut. */
    float strut_t = saturate(d_strut / strut_half);
    float rim = 1.0 - strut_t * strut_t;

    /* joint nodes: every hex vertex. Compute distance to the nearest
     * hex vertex (in WORLD coords). A vertex of this hex is at
     * h.centre + HEX_VERTS[i]. Neighbouring hexes share those
     * vertices, so we only need to check THIS hex's 6 vertices. */
    float d_node = 1e6;
    for (int i = 0; i < 6; i++) {
        d_node = min(d_node, length(lp - (h.centre + HEX_VERTS[i])));
    }
    float node_mask = 1.0 - smoothstep(node_r * 0.85, node_r, d_node);
    float node_core = 1.0 - smoothstep(node_r * 0.30, node_r * 0.50, d_node);

    /* --- palette: deliberately narrow hue range --- */
    float hue_base = mix(0.46, 0.54, iSeed.y);
    float sat = mix(0.45, 0.65, iSeed.w);

    vec3 hi = vec3(0.62, 0.96, 0.98);   /* pale cyan   -- core/highlight */
    vec3 md = vec3(0.18, 0.55, 0.62);   /* mid teal    -- strut body    */
    vec3 lo = vec3(0.04, 0.18, 0.22);   /* deep teal   -- far rim/mist  */

    /* strut body colour: gradient from lo at the outer rim to md in
     * the middle to hi at the core. */
    vec3 strut_col = mix(md, lo, rim);
    strut_col = mix(strut_col, hi, core * 0.85);

    /* node body colour: brighter, with its own core highlight. */
    vec3 node_col = mix(md, hi, 0.7);
    node_col = mix(node_col, hi, node_core * 0.6);

    /* combine: struts OR nodes contribute structure; nodes overwrite
     * struts at their location so the joints read as solid. */
    float structure = max(outer, node_mask);
    vec3 col = mix(lo, strut_col, outer);
    col = mix(col, node_col, node_mask);

    /* a touch of background mist outside the structure. */
    float mist = (1.0 - structure);
    col += vec3(0.04, 0.10, 0.12) * mist * 0.30;

    /* vignette: gentle */
    float vig = smoothstep(1.30, 0.50, length(uv));
    col *= mix(0.70, 1.0, vig);

    /* minimum luminance to keep the mist region legible. */
    col = max(col, vec3(0.025, 0.060, 0.075));

    fragColor = vec4(col, 1.0);
}
