#version 300 es
/* lines.frag — optional override for the line-shader fragment.
 *
 * The default (inline in gles3_neonasteroids.c) outputs solid colour
 * into the additive-blended trail FBO:
 *
 *   o_col = vec4(v_color * v_alpha, v_alpha);
 *
 * This file exists for cases where you want line shading to vary
 * along the segment length (e.g. a per-end gradient or noise
 * dithering). For now it matches the default.
 *
 * Vertex inputs:
 *   in vec3 v_color;
 *   in float v_alpha;
 * Output (RGBA8 trail FBO, additive):
 *   o_col.rgb = contribution to add
 *   o_col.a   = alpha contribution (kept around in case the
 *               compositing pass wants to read coverage)
 */
precision mediump float;

in vec3 v_color;
in float v_alpha;

out vec4 o_col;

void main() {
    /* Centre-weighted: lines look slightly brighter at the middle
     * of the segment than at the ends when the segment is long.
     * We approximate that by a slight luminance boost based on
     * alpha (already saturated in practice). */
    vec3 c = v_color * v_alpha;
    o_col = vec4(c, v_alpha);
}
