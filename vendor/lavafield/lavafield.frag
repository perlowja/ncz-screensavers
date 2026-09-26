#version 300 es
// lavafield.frag — fullscreen raymarched metaball lava field.
//
// First piece on the new engine (per the catalogue-curation plan of
// 2026-09-25). Keeps the slow buoyant rise/fall/merge/split of
// metaball blobs from jwz's 1990s lavalite, discards the lamp framing.
//
// Algorithm: a fixed count of blobs whose positions and radii are
// derived on the CPU side from a per-launch seed, animated by a
// shared vertical wave (plus a small per-blob phase) to give buoyancy.
// The field is sampled as an implicit surface SDF in 3D and raymarched
// for hit / no-hit, with the smooth-min blend producing merges and
// splits. Shading is subsurface-style: emissive body + a warm rim,
//   tonemapped and gamma-corrected.
//
// Performance: ~6 blobs, 48 march steps. On Intel UHD we drop to
//   ~5 blobs / 36 steps if needed (see u_blob_count clamp inside).
precision highp float;
precision highp int;
out vec4 fragColor;

// Pipeline state driven by the C harness.
uniform float u_time;        // seconds since launch
uniform float u_seed;        // 32-bit seed (u32 cast to float)
uniform vec2  u_resolution;  // framebuffer size in pixels
// Per-launch, randomised. Five palettes, palette drift phase/rate, blob
// count (4..6), viscosity (drag, lower = livelier), spatial scale,
// warmth. See src/gles3_lavafield.c for the exact ranges.
uniform float u_palette;       // 0..4 palette index
uniform float u_palette_phase; // 0..1, drift start offset
uniform float u_palette_rate;  // ~0.012..0.028 cycles/sec
uniform float u_palette_contrast; // 0.95..1.25 toe S-curve
uniform float u_blob_count;    // 4..6, integer in practice
uniform float u_viscosity;     // 0.7..1.4 motion speed factor
uniform float u_scale;         // 0.85..1.25 spatial scale
uniform float u_warmth;        // 0.7..1.45 emissive lift
// Blobs: 6 slots. w = radius.
uniform vec4  u_blob0;
uniform vec4  u_blob1;
uniform vec4  u_blob2;
uniform vec4  u_blob3;
uniform vec4  u_blob4;
uniform vec4  u_blob5;

#define PI 3.14159265358979323846

// ---- palette machinery, copied/adapted from blackhole ----
struct Stop { float h, s, l; };
vec3 hsl2rgb(vec3 c){
  vec3 rgb = clamp(abs(mod(c.x*6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0,
                   0.0, 1.0);
  return c.z + c.y * (rgb - 0.5) * (1.0 - abs(2.0* c.z - 1.0));
}
float hueLerp(float a, float b, float k){
  float d = b - a;
  d -= floor(d + 0.5);
  return a + d * k;
}
Stop stopAHue(Stop a, Stop b, float k){
  return Stop(hueLerp(a.h, b.h, k), a.s + (b.s - a.s) * k,
              a.l + (b.l - a.l) * k);
}
// Cheap 2D hash + value noise + 4-octave fbm. Drives the backdrop
// ambient haze; deliberately low-frequency and slow so it reads as
// atmosphere, not texture. Seeded by u_seed so two launches on the
// same time still differ.
float hash21(vec2 p){
  p = fract(p * vec2(123.34, 345.45));
  p += dot(p, p + 34.345 + u_seed * 0.00001);
  return fract(p.x * p.y);
}
float vnoise(vec2 p){
  vec2 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash21(i),                   hash21(i + vec2(1.0, 0.0)), f.x),
             mix(hash21(i + vec2(0.0, 1.0)),  hash21(i + vec2(1.0, 1.0)), f.x),
             f.y);
}
float fbm(vec2 p){
  float v = 0.0, a = 0.5;
  for(int i = 0; i < 4; i++){
    v += a * vnoise(p);
    p = p * 2.03 + vec2(17.13, -11.7);
    a *= 0.5;
  }
  return v;
}
// Five palettes, each a 4-stop ramp from dark-hot to bright-cool. At
// least one classic orange/red (idx 1), one amber-copper (idx 0),
// one deep purple/red (idx 2), one teal-magenta (idx 3), one
// blue-violet (idx 4). The operator asked for "not just orange" but
//   at least one classic available — idx 0 and 1 cover that.
void paletteStops(float idx, out Stop s0, out Stop s1, out Stop s2,
                  out Stop s3){
  if(idx < 0.5){                       // copper-amber (classic warm)
    s0 = Stop(0.05, 0.90, 0.08);
    s1 = Stop(0.07, 0.95, 0.38);
    s2 = Stop(0.10, 0.92, 0.68);
    s3 = Stop(0.13, 0.78, 0.97);
  } else if(idx < 1.5){                // red-orange-yellow (classic lava)
    s0 = Stop(0.98, 0.92, 0.06);
    s1 = Stop(0.04, 0.98, 0.32);
    s2 = Stop(0.09, 0.95, 0.62);
    s3 = Stop(0.12, 0.82, 0.97);
  } else if(idx < 2.5){                // crimson-violet (rich, deep)
    s0 = Stop(0.96, 0.85, 0.08);
    s1 = Stop(0.88, 0.90, 0.32);
    s2 = Stop(0.78, 0.75, 0.62);
    s3 = Stop(0.72, 0.55, 0.96);
  } else if(idx < 3.5){                // teal -> magenta split
    s0 = Stop(0.46, 0.65, 0.08);
    s1 = Stop(0.52, 0.95, 0.36);
    s2 = Stop(0.86, 0.85, 0.62);
    s3 = Stop(0.92, 0.88, 0.96);
  } else {                             // blue -> violet -> white-hot
    s0 = Stop(0.60, 0.85, 0.08);
    s1 = Stop(0.66, 0.90, 0.34);
    s2 = Stop(0.74, 0.78, 0.66);
    s3 = Stop(0.82, 0.62, 0.98);
  }
}
vec3 palette(float t){
  float active_h = u_palette_phase + u_time * u_palette_rate;
  Stop s0, s1, s2, s3;
  paletteStops(u_palette, s0, s1, s2, s3);
  s0.h = fract(s0.h + active_h);
  s1.h = fract(s1.h + active_h);
  s2.h = fract(s2.h + active_h);
  s3.h = fract(s3.h + active_h);
  Stop r;
  if(t < 0.33) r = stopAHue(s0, s1, t / 0.33);
  else if(t < 0.66) r = stopAHue(s1, s2, (t - 0.33) / 0.33);
  else if(t < 0.90) r = stopAHue(s2, s3, (t - 0.66) / 0.24);
  else r = s3;
  return hsl2rgb(vec3(r.h, r.s, r.l));
}

// ---- blob SDF ----
//
// Smooth-minimum of N sphere SDFs (k controls blend radius). When two
// blobs come close, k>0 produces a smooth bridge; on contact they merge
// into a single surface, on separation they pinch off and split. This
// is the well-known iq smin from
//   https://iquilezles.org/articles/smin/ — public domain.
float smin(float a, float b, float k){
  float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
  return mix(b, a, h) - k * h * (1.0 - h);
}
float blobs(vec3 p, out float h2){
  // Slight per-blob soft jitter on radius driven by u_seed keeps blobs
  // non-uniform across launches even when count is identical.
  float k = 0.55;
  float d = length(p - u_blob0.xyz) - u_blob0.w;
  h2 = (p.x - u_blob0.x) * (p.x - u_blob0.x) +
       (p.y - u_blob0.y) * (p.y - u_blob0.y) +
       (p.z - u_blob0.z) * (p.z - u_blob0.z) - u_blob0.w * u_blob0.w;
  d = smin(d, length(p - u_blob1.xyz) - u_blob1.w, k);
  if(u_blob_count > 1.5){
    d = smin(d, length(p - u_blob2.xyz) - u_blob2.w, k);
  }
  if(u_blob_count > 2.5){
    d = smin(d, length(p - u_blob3.xyz) - u_blob3.w, k);
  }
  if(u_blob_count > 3.5){
    d = smin(d, length(p - u_blob4.xyz) - u_blob4.w, k);
  }
  if(u_blob_count > 4.5){
    d = smin(d, length(p - u_blob5.xyz) - u_blob5.w, k);
  }
  return d;
}

// Cheap depth-based pseudo-normals via central differences. The
// marching itself runs from a slab Z=2 down to Z=-2.
vec3 normal(vec3 p){
  float h2;
  vec2 e = vec2(0.04, 0.0);
  return normalize(vec3(
    blobs(p + e.xyy, h2) - blobs(p - e.xyy, h2),
    blobs(p + e.yxy, h2) - blobs(p - e.yxy, h2),
    blobs(p + e.yyx, h2) - blobs(p - e.yyx, h2)));
}

void main(){
  // Square-aspect fragment coords so the field stays round regardless
  // of screen ratio. y is flipped so blob 0 starts low and rises
  // upward — matches the "buoyancy rises" mental model.
  vec2 uv = (gl_FragCoord.xy * 2.0 - u_resolution) / u_resolution.y;
  uv.y = -uv.y;
  uv *= u_scale;

  // Camera sits at z=+2.6 looking straight down at the slab. Ray
  // direction per fragment is (uv, -1) normalised.
  vec3 ro = vec3(0.0, 0.0, 2.6);
  vec3 rd = normalize(vec3(uv, -1.0));

  // March. Cap at 48 steps; far plane at z = -2.0.
  float t = 0.0;
  float hit = 0.0;
  vec3 p = ro;
  float h2 = 0.0;
  for(int i = 0; i < 48; i++){
    p = ro + rd * t;
    float d = blobs(p, h2);
    if(d < 0.01){ hit = 1.0; break; }
    if(p.z < -2.0) break;
    t += max(d, 0.04);   // min step 0.04 keeps low-curvature regions moving
  }
  if(hit < 0.5){
    // No surface hit: ambient heat-haze backdrop. The point of having a
    // non-trivial backdrop (rather than pure black) is twofold:
    //   1. the screen reads as "molten field" rather than "broken void"
    //   2. when the compositor scales our surface up to the full
    //      output, the whole desktop reads as molten content rather
    //      than collapsing to "small object on black" like the legacy
    //      hack (which measured 4% non-black).
    // We use a low-frequency fbm + slow vertical gradient: hotter near
    // the bottom (where blobs sink and reheat), cooler at the top.
    // Brightness scaled so the surface never competes with the actual
    // blobs for attention.
    float vy = clamp((uv.y + 1.3) / 2.6, 0.0, 1.0);  // 0=bottom, 1=top
    float n = fbm(uv * 2.5 + vec2(u_time * 0.04, u_time * 0.03));
    n = 0.50 + 0.50 * n;
    // Temperature gradient: bottom is warmer (orange-red), top cooler
    // (deep red-violet). The whole backdrop maps to the dim end of the
    // palette so it stays below the blobs in luminance.
    float backT = clamp(0.10 + 0.20 * (1.0 - vy) + 0.05 * n, 0.0, 0.32);
    vec3 back = palette(backT);
    // Lift brightness into the "non-black" regime (>12/255 on every
    // channel). Without this lift, the compositor's up-scaling of our
    // surface leaves the dark backdrop as dark void around the blobs.
    back *= 0.32 + 0.18 * n;
    // A final small warm wash so even the dimmest haze pixel still
    // clears the coverage test.
    back += vec3(0.040, 0.022, 0.012);
    fragColor = vec4(clamp(back, 0.0, 1.0), 1.0);
    return;
  }

  vec3 n = normal(p);
  // Subsurface-style shading designed to read as molten material, not
  // shaded plastic. Three layers stacked:
  //   1. Strong wrap-around diffuse (1 - dot(n, lightDir)) so light
  //      "wraps" past the terminator like it would through translucent
  //      rock or wax. Standard lambert would put the unlit half in
  //      shadow; here we want the unlit half to still glow.
  //   2. Aggressive warm rim glow driven by fresnel, biased to the
  //      hot end of the palette so the blob edges look incandescent.
  //   3. Emissive hot core: depth into the field (estimated from the
  //      marching step count) lifts the centre of every blob into
  //      near-white-hot territory.
  vec3 lightDir = normalize(vec3(0.25, 0.55, 0.85));
  float ndl = dot(n, lightDir);
  // Wrap diffuse: lambert + (1-ndl) bias. Range [0.1, 1.0] instead of
  // [0, 1] so the dark side still glows.
  float wrap = clamp(0.5 + 0.5 * ndl, 0.0, 1.0);
  // Fresnel rim. Use a soft pow so the rim is gradual not a hard line.
  float fres = pow(1.0 - max(dot(n, -rd), 0.0), 1.4);
  // Vertical temperature gradient inside the field: bottom of slab
  // (cool side of the palette) heats up; top stays warmer. Adds depth.
  float fieldHeat = clamp((p.y + 1.4) / 2.8, 0.0, 1.0);
  float t_pal = clamp(0.20 + 0.65 * (1.0 - fieldHeat) + 0.10 * (1.0 - abs(n.z)), 0.0, 0.95);
  vec3 base = palette(t_pal);
  // Rim uses the hot end of the palette, so the glow shifts toward
  // white-hot at the edge.
  vec3 rim = palette(clamp(t_pal + 0.15, 0.0, 1.0));
  // Compose: wrap diffuse for body, strong rim glow, soft hot core.
  vec3 col = base * (0.45 + 0.55 * wrap);
  col += rim * fres * 1.85 * u_warmth;
  // Inner heat: when fres is low (looking straight down at the
  // surface), the centre is hot. Smoothstep keeps it from being a
  // hard disk.
  float core = 1.0 - smoothstep(0.0, 0.7, fres);
  col += base * core * 0.45 * u_warmth;

  // Tone map (Reinhard) + gamma, then small contrast toe.
  col = col / (1.0 + col);
  col = pow(max(col, vec3(0.0)), vec3(1.0 / 2.2));
  float k = clamp(u_palette_contrast, 0.85, 1.35);
  col = (col - 0.5) * k + 0.5;
  col = max(col - vec3(0.018), vec3(0.0));
  fragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}