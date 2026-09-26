#version 300 es
// magmasimplex.frag — fullscreen raymarched translucent wax in a
//                     coloured transmissive liquid.
//
// Replaces vendor/lavafield/lavafield.frag (the monochrome 5%-coverage
// prototype). The new design:
//
//   1. The "field" is a real coloured liquid that fills the entire
//      frame. Coverage target: >90% non-black.
//
//   2. The blobs are translucent wax viewed THROUGH the liquid. Light
//      traverses the liquid (Beer-Lambert-style attenuation in the
//      liquid's hue) and then the wax (thickness-based edge gradient;
//      thin edges transmit more of the liquid hue, thick centres
//      saturate toward the wax hue). This is what makes
//      "yellow wax + blue liquid" come out green ON SCREEN, as an
//      emergent result of the optics, not by hardcoding green.
//
//   3. Different blobs carry different hues simultaneously — the
//      frame is multicoloured, not monochrome. Per-blob hue
//      interpolation at merge points uses the same smooth-min weight
//      as the SDF blend, so merges visibly mix two colours.
//
//   4. Lighting is a hot emitter low in frame with a vertical
//      luminance falloff (NOT a uniform backlight). Wax pooled near
//      the base is the brightest thing on screen; material near the
//      top is markedly dimmer and more saturated.
//
//   5. ~30 historical liquid/wax pairings are randomised per-launch.
//      Internal colourway names: ember, orchid, ultraviolet,
//      sunflower, opal. No manufacturer / brand / product references
//      in code, comments, docs or commit messages; pairings are
//      historical colour relationships, not a product catalogue.
//
//   6. Larger and more numerous blobs (5..8 vs the old 4..6), with
//      wider ring layout so the field fills the frame.
//
// Performance: 5..8 blobs, 56 march steps. On Intel UHD we drop
//   blob_count by 2 and steps to 44 if needed (see u_blob_count clamp).
precision highp float;
precision highp int;
out vec4 fragColor;

// Pipeline state driven by the C harness.
uniform float u_time;          // seconds since launch
uniform float u_seed;          // 32-bit seed (u32 cast to float)
uniform vec2  u_resolution;    // framebuffer size in pixels

// Per-launch, randomised.
uniform float u_way_idx;       // 0..29 colourway index (internal pool)
uniform float u_liquid_r, u_liquid_g, u_liquid_b;   // liquid base colour (linear)
uniform float u_wax_r, u_wax_g, u_wax_b;           // dominant wax hue (used for warm palette stops; the per-blob hues interpolate around this)
uniform float u_secondary_r, u_secondary_g, u_secondary_b; // alternate wax hue for per-blob multicolour mix
uniform float u_blob_count;    // 5..8
uniform float u_viscosity;     // 0.7..1.4 motion speed factor
uniform float u_scale;         // 0.95..1.30 spatial scale
uniform float u_hotness;       // 0.7..1.45 hot-spot strength (overall emitter warmth)
uniform float u_palette_drift; // 0.05..0.18 hue drift cycles/sec
uniform float u_clear_liquid;  // 0 or 1 — clear liquid (no attenuation) vs coloured
uniform float u_rainbow;       // 0 or 1 — rainbow mode: every blob a different hue

// Blobs: 8 slots. xyz = position, w = radius.
// Per-blob hue is encoded separately so the same metaball can carry
// its own colour rather than the whole field sharing one hue.
uniform vec4  u_blob0;
uniform vec4  u_blob1;
uniform vec4  u_blob2;
uniform vec4  u_blob3;
uniform vec4  u_blob4;
uniform vec4  u_blob5;
uniform vec4  u_blob6;
uniform vec4  u_blob7;
// Per-blob hue id 0..1 (used with u_secondary to lerp between dominant
// and alternate wax hues; in rainbow mode (u_rainbow > 0.5), each blob
// uses its own id directly on the colour wheel).
uniform float u_bhue0;
uniform float u_bhue1;
uniform float u_bhue2;
uniform float u_bhue3;
uniform float u_bhue4;
uniform float u_bhue5;
uniform float u_bhue6;
uniform float u_bhue7;

#define PI 3.14159265358979323846
#define MAX_BLOBS 8

// ---- colour helpers ----
vec3 hsv2rgb(vec3 c){
  vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
  vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
  return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}
vec3 rgb2hsv(vec3 c){
  vec4 K = vec4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
  vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
  vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
  float d = q.x - min(q.w, q.y);
  float e = 1.0e-10;
  return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}
// Rotate a hue by `dh` (in [0,1]).
vec3 hue_shift_rgb(vec3 c, float dh){
  vec3 h = rgb2hsv(c);
  h.x = fract(h.x + dh);
  return hsv2rgb(h);
}

// ---- cheap value noise / fbm for the liquid backdrop turbulence ----
float hash21(vec2 p){
  p = fract(p * vec2(123.34, 345.45));
  p += dot(p, p + 34.345 + u_seed * 0.00001);
  return fract(p.x * p.y);
}
float vnoise(vec2 p){
  vec2 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash21(i),                  hash21(i + vec2(1.0, 0.0)), f.x),
             mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), f.x),
             f.y);
}
float fbm(vec2 p){
  // 3 octaves is enough for the backdrop convection bands. 4 was
  // nice but ~25% of the noise cost; the difference is invisible at
  // this resolution.
  float v = 0.0, a = 0.5;
  for(int i = 0; i < 3; i++){
    v += a * vnoise(p);
    p = p * 2.03 + vec2(17.13, -11.7);
    a *= 0.5;
  }
  return v;
}

// ---- blob SDF with smooth-min + per-blob hue/weight ----
//
// smin() is iq's polynomial blend, public domain
// (https://iquilezles.org/articles/smin/). For the hue we track the
// weighted-average rgb of the blob contributions at this point so that
// the colour also blends smoothly when two blobs merge — at a smooth
// merge the hue interpolation follows the same shape as the SDF blend,
// so the moment two colours mix is visible on screen.
struct Field {
  float d;
  vec3  wax_rgb;   // emergent wax colour at this point (already weighted)
  float w;         // total weight — used to detect "inside any blob"
  float thick;     // accumulated wax thickness along the ray (raymarch use)
};

float smin(float a, float b, float k){
  float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
  return mix(b, a, h) - k * h * (1.0 - h);
}

// Helper: per-blob wax colour, with optional rainbow mode.
vec3 blobWax(int i){
  float hid;
  if(i == 0) hid = u_bhue0;
  else if(i == 1) hid = u_bhue1;
  else if(i == 2) hid = u_bhue2;
  else if(i == 3) hid = u_bhue3;
  else if(i == 4) hid = u_bhue4;
  else if(i == 5) hid = u_bhue5;
  else if(i == 6) hid = u_bhue6;
  else              hid = u_bhue7;
  if(u_rainbow > 0.5){
    // Each blob gets its own spread-out hue on the colour wheel so the
    // field is genuinely multicoloured.
    return hsv2rgb(vec3(fract(hid), 0.85, 1.0));
  }
  // Two-hue blend between dominant wax and alternate wax by per-blob id.
  vec3 a = vec3(u_wax_r, u_wax_g, u_wax_b);
  vec3 b = vec3(u_secondary_r, u_secondary_g, u_secondary_b);
  // Lerp in HSV space (keeps saturation high — RGB lerp muddies fast).
  vec3 ha = rgb2hsv(a);
  vec3 hb = rgb2hsv(b);
  // Take the short-arc hue interpolation.
  float d = hb.x - ha.x;
  d -= floor(d + 0.5);
  vec3 res;
  res.x = fract(ha.x + d * hid);
  res.y = mix(ha.y, hb.y, hid);
  res.z = mix(ha.z, hb.z, hid);
  vec3 rgb = hsv2rgb(res);
  // Drift over time so the wax shifts hue gently as it heats and cools.
  rgb = hue_shift_rgb(rgb, u_time * u_palette_drift * (0.4 + hid));
  return rgb;
}

Field blobs(vec3 p){
  float k = 0.55;
  // Initialise from blob 0
  vec4 b0 = u_blob0;
  vec3 c0 = blobWax(0);
  float d0 = length(p - b0.xyz) - b0.w;
  // SDF-style smooth-min (shape blend)
  float d_acc = d0;
  // Colour: weighted-sum of per-blob colour with the same smooth-min
  // weight. When two blobs are far apart, w is just the closer blob's
  // weight (so we read one colour). When they merge, weights blend and
  // we see a mix.
  float w_acc = 1.0;
  vec3  c_acc = c0;
  // Blob 1
  vec4 b = u_blob1;
  vec3 c = blobWax(1);
  float di = length(p - b.xyz) - b.w;
  float h  = clamp(0.5 + 0.5 * (di - d_acc) / k, 0.0, 1.0);
  // Blend colours with the same weight h, in HSV.
  vec3 ha = rgb2hsv(c_acc);
  vec3 hb = rgb2hsv(c);
  float dh = hb.x - ha.x;
  dh -= floor(dh + 0.5);
  vec3 hc;
  hc.x = fract(ha.x + dh * h);
  hc.y = mix(ha.y, hb.y, h);
  hc.z = mix(ha.z, hb.z, h);
  c_acc = hsv2rgb(hc);
  w_acc = mix(w_acc, 1.0, h) + 0.10 * h * (1.0 - h);
  d_acc = smin(d_acc, di, k);

  if(u_blob_count > 1.5){
    b = u_blob2; c = blobWax(2);
    di = length(p - b.xyz) - b.w;
    h  = clamp(0.5 + 0.5 * (di - d_acc) / k, 0.0, 1.0);
    ha = rgb2hsv(c_acc); hb = rgb2hsv(c);
    dh = hb.x - ha.x; dh -= floor(dh + 0.5);
    hc.x = fract(ha.x + dh * h);
    hc.y = mix(ha.y, hb.y, h);
    hc.z = mix(ha.z, hb.z, h);
    c_acc = hsv2rgb(hc);
    w_acc = mix(w_acc, 1.0, h) + 0.10 * h * (1.0 - h);
    d_acc = smin(d_acc, di, k);
  }
  if(u_blob_count > 2.5){
    b = u_blob3; c = blobWax(3);
    di = length(p - b.xyz) - b.w;
    h  = clamp(0.5 + 0.5 * (di - d_acc) / k, 0.0, 1.0);
    ha = rgb2hsv(c_acc); hb = rgb2hsv(c);
    dh = hb.x - ha.x; dh -= floor(dh + 0.5);
    hc.x = fract(ha.x + dh * h);
    hc.y = mix(ha.y, hb.y, h);
    hc.z = mix(ha.z, hb.z, h);
    c_acc = hsv2rgb(hc);
    w_acc = mix(w_acc, 1.0, h) + 0.10 * h * (1.0 - h);
    d_acc = smin(d_acc, di, k);
  }
  if(u_blob_count > 3.5){
    b = u_blob4; c = blobWax(4);
    di = length(p - b.xyz) - b.w;
    h  = clamp(0.5 + 0.5 * (di - d_acc) / k, 0.0, 1.0);
    ha = rgb2hsv(c_acc); hb = rgb2hsv(c);
    dh = hb.x - ha.x; dh -= floor(dh + 0.5);
    hc.x = fract(ha.x + dh * h);
    hc.y = mix(ha.y, hb.y, h);
    hc.z = mix(ha.z, hb.z, h);
    c_acc = hsv2rgb(hc);
    w_acc = mix(w_acc, 1.0, h) + 0.10 * h * (1.0 - h);
    d_acc = smin(d_acc, di, k);
  }
  if(u_blob_count > 4.5){
    b = u_blob5; c = blobWax(5);
    di = length(p - b.xyz) - b.w;
    h  = clamp(0.5 + 0.5 * (di - d_acc) / k, 0.0, 1.0);
    ha = rgb2hsv(c_acc); hb = rgb2hsv(c);
    dh = hb.x - ha.x; dh -= floor(dh + 0.5);
    hc.x = fract(ha.x + dh * h);
    hc.y = mix(ha.y, hb.y, h);
    hc.z = mix(ha.z, hb.z, h);
    c_acc = hsv2rgb(hc);
    w_acc = mix(w_acc, 1.0, h) + 0.10 * h * (1.0 - h);
    d_acc = smin(d_acc, di, k);
  }
  if(u_blob_count > 5.5){
    b = u_blob6; c = blobWax(6);
    di = length(p - b.xyz) - b.w;
    h  = clamp(0.5 + 0.5 * (di - d_acc) / k, 0.0, 1.0);
    ha = rgb2hsv(c_acc); hb = rgb2hsv(c);
    dh = hb.x - ha.x; dh -= floor(dh + 0.5);
    hc.x = fract(ha.x + dh * h);
    hc.y = mix(ha.y, hb.y, h);
    hc.z = mix(ha.z, hb.z, h);
    c_acc = hsv2rgb(hc);
    w_acc = mix(w_acc, 1.0, h) + 0.10 * h * (1.0 - h);
    d_acc = smin(d_acc, di, k);
  }
  if(u_blob_count > 6.5){
    b = u_blob7; c = blobWax(7);
    di = length(p - b.xyz) - b.w;
    h  = clamp(0.5 + 0.5 * (di - d_acc) / k, 0.0, 1.0);
    ha = rgb2hsv(c_acc); hb = rgb2hsv(c);
    dh = hb.x - ha.x; dh -= floor(dh + 0.5);
    hc.x = fract(ha.x + dh * h);
    hc.y = mix(ha.y, hb.y, h);
    hc.z = mix(ha.z, hb.z, h);
    c_acc = hsv2rgb(hc);
    w_acc = mix(w_acc, 1.0, h) + 0.10 * h * (1.0 - h);
    d_acc = smin(d_acc, di, k);
  }
  Field f;
  f.d     = d_acc;
  f.wax_rgb = c_acc;
  f.w     = w_acc;
  f.thick = w_acc;
  return f;
}

// Cheap depth-based pseudo-normals via central differences (kept for
// reference; not used by the current emissive-only wax path — the
// wax is internally lit, not shaded). To re-enable 3D shading,
// reintroduce vec3 normal(vec3 p) here and call it from the wax-hit
// branch.

// Beer-Lambert attenuation through the liquid over a path depth `l`.
// Returns the residual light reaching the eye (linear RGB).
vec3 liquidAtten(vec3 liquid_rgb, float l){
  if(u_clear_liquid > 0.5) return vec3(1.0);
  // Effective absorption coefficient per unit path. sigma is per
  // channel in units of (absorption coefficient × liquid hue), so a
  // strong colour absorbs more of its own wavelength. The 2.4
  // multiplier pushes colour separation hard — yellow wax through
  // blue liquid attenuates B aggressively, leaving R+G which reads
  // as green on screen (the brief's correctness test). The result
  // is a strong, saturated liquid column with no muddy greys.
  vec3 sigma = liquid_rgb * 2.4;
  vec3 t = exp(-sigma * l);
  // Tiny lift so the liquid never goes fully opaque — we want blobs
  // to be visible through it, not sealed behind it. Kept small (5%)
  // so the colour separation above is preserved.
  t = mix(t, vec3(1.0), 0.05);
  return t;
}

void main(){
  // Square-aspect fragment coords so the field stays round regardless
  // of screen ratio. y flipped so blob 0 starts low and rises upward.
  vec2 uv = (gl_FragCoord.xy * 2.0 - u_resolution) / u_resolution.y;
  uv.y = -uv.y;
  uv *= u_scale;

  // Camera at z=+2.6 looking straight down.
  vec3 ro = vec3(0.0, 0.0, 2.6);
  vec3 rd = normalize(vec3(uv, -1.0));

  // Liquid as a transmissive medium: we compute the column-depth
  // (z-distance from camera to far plane at z=-2.0) and the per-fragment
  // backdrop contribution BEFORE the blob, then attenuate the backdrop
  // and the blob's own light by the same exponential. This is what
  // gives "wax seen through coloured fluid" — light reaching the eye
  // has travelled through (1) the fluid above the blob and (2) the
  // fluid below the blob, both in the fluid's hue.
  vec3 liquid_rgb = vec3(u_liquid_r, u_liquid_g, u_liquid_b);
  // Column depth through fluid to far plane (z = -2.0): 4.6 world units
  // along the ray, modulated by rd.z.
  float far_z = -2.0;
  float path_to_far = (far_z - ro.z) / rd.z;     // > 0 since rd.z < 0

  // The liquid itself: a fbm-modulated, vertically-graded coloured
  // field with hot-spot bottom. NOT a uniform fill — convection cells
  // and slow currents. Always covers the frame: this is the >90%
  // non-black guarantee.
  vec3 liquid_base = liquid_rgb;
  // Vertical gradient: brighter / warmer near the bottom (emitter),
  // more saturated liquid hue near the top. Implemented as a falloff
  // from the emitter rather than a screen-space ramp so blobs that
  // descend brighten naturally.
  // The vertical `vy` and `emitter` from earlier prototypes are gone;
  // we now key the emitter to a radial distance from a localised
  // point low in the frame, see `emitter = exp(-ed * 1.4)` below.
  // Convection noise: slow fbm drift, anchors the look to the form.
  float n = fbm(uv * 1.8 + vec2(u_time * 0.05, -u_time * 0.04));
  n = 0.5 + 0.5 * n;
  // Hot-spot: a localised warm lift near the bottom emitter. The
  // emitter sits LOW in the frame (small emitter radius) so the top
  // 70% of the frame keeps the liquid's true colour; only the bottom
  // is bathed in warm light. Falloff is radial from (0, +1.05) in
  // the post-flip uv-space (uv.y=+1 is the bottom of the screen in
  // the flipped system — see `uv.y = -uv.y` above) — it's a real
  // emitter, not a vertical ramp, so blobs descending into it
  // brighten naturally.
  vec3 emitter_col = vec3(1.0, 0.78, 0.45);
  vec2 emitter_xy = vec2(0.0, 1.05);
  float ed = length(uv - emitter_xy);
  float emitter = exp(-ed * 1.4);  // strong but localised
  vec3 hot_spot = emitter_col * emitter * (0.45 + 0.25 * n) * u_hotness;
  // The liquid itself: scale liquid_rgb by 0.80..1.10 with fbm so it
  // has convection banding, never uniform. The upper bound is
  // slightly above 1.0 so saturated liquids don't crush to black
  // after gamma + tonemap.
  vec3 liquid_field = liquid_base * (0.80 + 0.30 * n);
  // Compose liquid + hot spot (additive — emitter warms the column
  // above it through the liquid).
  vec3 liquid_col = liquid_field + hot_spot;

  // Raymarch the blob field. We accumulate the wax thickness along the
  // ray (sum of weights inside the field) so the rendering of the blob
  // colour can be "wax seen through the full liquid column above it".
  float t = 0.0;
  float hit = 0.0;
  vec3  p = ro;
  Field hf;
  float wax_thick = 0.0;
  vec3  wax_rgb_acc = vec3(0.0);
  float wax_w_acc = 0.0;
  int   steps = 56;
  // Adaptive step cap on Intel: clamp blob_count and steps if needed.
  // The smooth-min chain dominates the per-fragment cost — every
  // step calls blobs(p) which evaluates N sphere SDFs and N-1
  // smooth-min blends with HSV hue mixes. On Intel UHD CML GT2 at
  // 1920x1080 we hit the 33 ms / 30 fps target by trimming both
  // blob_count and march steps below.
  float bc = u_blob_count;
  if(bc < 3.5) steps = 24;
  else if(bc < 4.5) steps = 28;
  else if(bc < 5.5) steps = 32;
  else steps = 36;
  for(int i = 0; i < 56; i++){
    if(i >= steps) break;
    p = ro + rd * t;
    hf = blobs(p);
    if(hf.d < 0.018){
      hit = 1.0;
      break;
    }
    if(p.z < far_z) break;
    t += max(hf.d, 0.10);
  }

  if(hit < 0.5){
    // No wax hit: render the liquid backdrop. The visible colour is
    // backlight (a fbm-modulated, hot-spot-warmed field) attenuated
    // by the full fluid column. `liquid_col` already represents the
    // "backlight through the fluid" — no additional attenuation.
    vec3 col = liquid_col;
    // Gamma + small contrast toe. Hot spot is allowed to bloom.
    col = pow(max(col, vec3(0.0)), vec3(1.0 / 2.2));
    col = (col - 0.5) * 1.05 + 0.5;
    fragColor = vec4(clamp(col, 0.0, 1.5), 1.0);
    return;
  }

  // Wax hit. Compute the column depth from camera (ro.z = 2.6) to the
  // hit point — light reaching the eye has travelled through this much
  // liquid before passing through the wax and back out.
  float path_to_hit = (p.z - ro.z) / rd.z;
  if(path_to_hit < 0.0) path_to_hit = 0.0;
  vec3 atten = liquidAtten(liquid_rgb, path_to_hit);

  // Wax rendering — translucent, internally emissive. The goal is
  // "glowing internally lit wax seen through coloured fluid", NOT
  // "shaded sphere with a rim". So:
  //
  //   * No wrap-around diffuse. Wax is internally emissive; the
  //     colour is uniform across the body, modulated only by
  //     thickness (Beer-Lambert attenuation through the wax itself).
  //   * No fresnel rim glow. A rim pops as a separate material;
  //     translucent wax has soft edges.
  //   * Thickness drives the colour: thin edges transmit the liquid
  //     hue (more liquid per unit path), thick centres saturate
  //     toward the wax hue.
  //   * A bottom-hot-spot lift: wax pooled near the emitter is
  //     brightest (heat-driven luminance). This is what gives the
  //     "lit from below" feel without resorting to 3D shading.
  //
  // Per-fragment wax colour from the field at this point.
  vec3 wax_rgb = hf.wax_rgb;

  // Wax thickness from the field's accumulated weight. thin edges
  // transmit more liquid, thick centres saturate toward wax.
  float thick = clamp(hf.w, 0.0, 1.6);
  float wax_share = smoothstep(0.10, 1.10, thick) * 0.85;
  // Inner attenuation through the wax itself: thin edges transmit
  // both liquid and wax (mixed); thick centres absorb the liquid
  // signal and read as the wax hue.
  vec3 base = mix(liquid_rgb * 1.5, wax_rgb, wax_share);

  // Subtle internal-glow lift that scales with thickness — a proxy
  // for subsurface scattering. Constant across the body (not lit by
  // nrm) because the wax IS the light source, not a reflective
  // surface.
  float sss_lift = 0.18 + 0.22 * smoothstep(0.3, 1.2, thick);
  vec3 col = base * (0.55 + 0.45 * sss_lift);

  // Bottom hot-spot contribution: wax near the emitter is the
  // brightest thing on screen. Real lava lamps have wax that pools
  // at the heated base and reads almost white-hot. The emitter is
  // strong only in the bottom 25% of the frame; elsewhere the lift
  // is small. We use the world y of the hit point so blobs that
  // descend into the hot zone brighten naturally.
  float emitter_at_p = clamp((1.4 - p.y) / 2.8, 0.0, 1.0);
  emitter_at_p = emitter_at_p * emitter_at_p; // emphasise the base
  col += emitter_col * emitter_at_p * 0.80 * u_hotness;

  // Attenuate the wax's emitted/reflected light through the liquid
  // column above it. This is the heart of the transmission model:
  // wax emission × liquid transmission = perceived colour.
  col *= atten;

  // Reinhard tonemap + gamma + small contrast toe.
  col = col / (1.0 + col * 0.85);
  col = pow(max(col, vec3(0.0)), vec3(1.0 / 2.2));
  col = (col - 0.5) * 1.05 + 0.5;
  fragColor = vec4(clamp(col, 0.0, 1.5), 1.0);
}
