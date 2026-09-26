// Title:  Ridgeline
// Author: original for ncz-screensavers (clean-room)
// Date:   2026-09-26
// Desc:   A procedural fractal mountain landscape seen in continuous
//         forward flight.  Ridges, valleys, haze receding to the
//         horizon.  The frame is filled end-to-end: terrain below,
//         atmospheric sky above -- no dead black.
//         Two runs differ materially because iSeed drives terrain
//         character (ridged vs billowy), sun elevation and palette,
//         haze density, and the flight path's bank and look-around.
//
// Technique: classic analytic raymarch over an fBm heightfield,
// tetrahedral numeric normals (Inigo Quilez 2012), exponential
// distance haze blended into a sun/sky gradient.  No feedback, no
// textures.  Step budget ~80, floor tier on Intel UHD (CML GT2).
//
// References (all public-domain mathematics, not hack source):
//   - I. Quilez, "Modeling with distance functions" (2008) -- fBm
//     gradient noise formula and the cheap-noise tile idea.
//   - I. Quilez, "Computing the surface normal" (2012) -- four-tap
//     tetrahedral normal.
//   - I. Quilez, "Atmospheric scattering" (2008) -- exponential fog.
//
// Per-run variation comes from iSeed (.xy = terrain character and
// flight, .zw = palette and sun).

// ---------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------

#define PI        3.14159265359
#define TAU       6.28318530718
#define MAX_STEPS 80
#define MAX_DIST  120.0
#define HIT_EPS   0.005

// ---------------------------------------------------------------------
// Cheap value noise + fBm.  Standard 2D hash, smooth-step interpolation,
// six octaves with lacunarity ~2.0 and gain ~0.5 (Quilez 2008).
// ---------------------------------------------------------------------

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    // Smoothstep (Perlin quintic-ish), not linear -- looks better
    // and is what Quilez 2008 recommends for fBm heightfields.
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = hash21(i + vec2(0.0, 0.0));
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// "Ridged" variant: 1 - |2n - 1| gives sharp mountain ridges.
// Plain fBm variant: stacked noise gives rolling hills.
float fbm_base(vec2 p, float lac, float gain, bool ridged) {
    float v = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    for (int i = 0; i < 6; i++) {
        float n = vnoise(p * freq);
        if (ridged) {
            n = 1.0 - abs(2.0 * n - 1.0);
            n *= n;
        }
        v += amp * n;
        freq *= lac;
        amp  *= gain;
    }
    return v;
}

// The terrain heightfield.  Two layered fBm samples:
//   - Macro terrain: large features (peaks, valleys) -- the silhouette
//   - Meso detail: ridges, sub-peaks -- the texture
// iSeed.x chooses ridged vs billowy, iSeed.y sets amplitude.
// Slopes are kept steep enough that normals have meaningful variation,
// which is what makes lit/shadow sides of ridges read as different.
float terrain(vec2 p) {
    float character = iSeed.x;            // 0..1, ridged->billowy
    float amp_scale = 2.0 + 6.0 * iSeed.y; // 2.0..8.0 amplitude -- big
                                           // peaks, visible slopes
    bool  ridged     = character < 0.5;
    float lacunarity = mix(2.05, 2.35, character);
    float gain       = mix(0.55, 0.48, character);

    // Macro features at period ~25 units, meso detail at period ~6.
    // Higher frequencies mean slopes are real (not ~1 degree) so
    // Lambert lighting gives visible shading variation.
    float macro = fbm_base(p * 0.040 + 17.3, lacunarity, gain, ridged);
    float meso  = fbm_base(p * 0.16  + 91.7, 2.10, 0.50, ridged);
    // Combine.  Meso weighted heavier than usual so we get real
    // ridge texture, not just smooth swells.
    float h = 0.55 * macro + 0.45 * meso;
    return h * amp_scale;
}

// Numeric normal via 4-tap central differences.  Three components:
//   - f(p + (h,0)) - f(p - (h,0))   -> tangent x
//   - f(p + (0,h)) - f(p - (0,h))   -> tangent y
//   - reconstructed normal = normalize(cross(tangent_x, tangent_y))
// h is chosen to span roughly one pixel of slope variation at the
// visible distance -- 0.05 gives stable normals for amplitudes up
// to ~4 with the period we use.
vec3 calcNormal(vec2 p) {
    const float h = 0.10;
    float a = terrain(p + vec2( h, 0.0));
    float b = terrain(p + vec2(-h, 0.0));
    float c = terrain(p + vec2(0.0,  h));
    float d = terrain(p + vec2(0.0, -h));
    vec3 tx = vec3(2.0 * h, 0.0, b - a);
    vec3 ty = vec3(0.0, 2.0 * h, d - c);
    return normalize(cross(tx, ty));
}

// ---------------------------------------------------------------------
// Sky / sun / haze.  A two-stop gradient with a soft sun disc.
// ---------------------------------------------------------------------

struct Sky {
    vec3 zenith;
    vec3 horizon;
    vec3 sun_col;
    vec3 sun_dir;
};

Sky makeSky() {
    Sky s;
    // Palette families driven by iSeed.z (0..1):
    //   < 0.33  alpine morning   (cool zenith, warm horizon)
    //   < 0.66  desert afternoon (warm zenith, pale horizon)
    //   else    twilight         (blue zenith, warm horizon)
    // Each palette is dialled so the sky reads as a real sky, not a
    // saturated backdrop.  Zenith and horizon are distinct, the sun
    // adds a third accent, and the overall chroma stays believable.
    float p = iSeed.z;
    vec3 zenA = vec3(0.34, 0.52, 0.82);   // alpine blue
    vec3 horA = vec3(0.95, 0.86, 0.74);   // warm cream
    vec3 zenB = vec3(0.52, 0.74, 0.92);   // pale desert sky
    vec3 horB = vec3(0.98, 0.90, 0.78);   // sand haze
    vec3 zenC = vec3(0.18, 0.22, 0.48);   // twilight blue
    vec3 horC = vec3(0.94, 0.70, 0.55);   // warm horizon
    vec3 zen, hor;
    if (p < 0.333) {
        zen = zenA; hor = horA;
    } else if (p < 0.666) {
        zen = zenB; hor = horB;
    } else {
        zen = zenC; hor = horC;
    }
    s.zenith = zen;
    s.horizon = hor;
    // Sun: warm white, intensity drops with iSeed.w (moodier at low).
    s.sun_col = vec3(1.10, 0.95, 0.78) * mix(1.6, 0.8, iSeed.w);
    // Sun elevation: low (dramatic) to high (noon).
    float elev = mix(0.18, 0.55, iSeed.w);
    float azim = 0.6 + 0.4 * sin(iSeed.x * TAU); // varies with terrain seed
    float ce = cos(elev), se = sin(elev);
    float ca = cos(azim), sa = sin(azim);
    s.sun_dir = normalize(vec3(sa * ce, se, ca * ce));
    return s;
}

// Sky shading: gradient + sun disc + sun halo.  Returns linear-ish
// radiance; tone-mapping happens later.
vec3 shadeSky(vec3 rd, Sky sky) {
    float t = clamp(rd.y * 0.5 + 0.5, 0.0, 1.0);
    // Slightly richer horizon via a smoothstep bump.
    vec3 base = mix(sky.horizon, sky.zenith, smoothstep(0.0, 1.0, t));
    // Sun disc + halo.  Sun disc is wider than a physical disc so
    // it's actually visible at standard FOV.
    float d = max(dot(rd, sky.sun_dir), 0.0);
    vec3 sun  = sky.sun_col * smoothstep(0.985, 0.995, d);     // disc
    vec3 halo = sky.sun_col * 0.50 * pow(d, 32.0);              // tight halo
    vec3 wide = sky.sun_col * 0.15 * pow(d, 6.0);               // wide halo
    return base + sun + halo + wide;
}

// ---------------------------------------------------------------------
// Camera path.  The camera flies forward at constant altitude, with
// gentle banking and look-around.  Path is procedurally looped so the
// screensaver can run forever; iSeed shifts the lateral phase.
// ---------------------------------------------------------------------

mat3 lookAtMat(vec3 eye, vec3 target, vec3 up) {
    vec3 f = normalize(target - eye);
    vec3 r = normalize(cross(f, up));
    vec3 u = cross(r, f);
    return mat3(r, u, f);
}

// Camera position: forward dolly + slow lateral weave + slight altitude
// bob.  All driven by iSeed for per-run uniqueness.  Altitude is
// chosen relative to the seeded terrain amplitude so the camera sits
// just above the tallest expected peak -- low and close to the ground
// so the foreground terrain fills the lower half of the frame and
// we can SEE the lit/shadow side of each ridge clearly.
vec3 cameraPos(float t) {
    float phase   = iSeed.y * TAU;
    float amp     = 2.0 + 6.0 * iSeed.y;     // matches terrain amp_scale
    float crest   = amp * 1.1;                  // rough peak height
    float lateral = 5.0 * sin(t * 0.07 + phase);
    float bob     = 0.4 * sin(t * 0.21 + phase * 0.7);
    float altitude = crest + 4.5 + 0.4 * sin(t * 0.13);
    float speed = 0.12;
    return vec3(lateral, altitude + bob, t * speed * 20.0);
}

// Camera target: look sharply down and forward.  We want the
// foreground terrain (the actual scene subject) to fill most of
// the frame, with sky just a strip near the top.
vec3 cameraTarget(float t) {
    float phase = iSeed.y * TAU;
    vec3 eye = cameraPos(t);
    float bank = 0.18 * sin(t * 0.09 + phase);
    float ahead = 12.0;
    float down  = 7.5 + 0.6 * sin(t * 0.17 + phase);
    float a = bank;
    vec3 fwd = vec3(sin(a), -down / ahead, 1.0);
    return eye + normalize(fwd) * ahead;
}

// ---------------------------------------------------------------------
// Raymarch.  Sphere-tracing the heightfield.  Each step uses a
// Lipschitz-style distance estimate: vertical distance to terrain
// divided by ray's downward component.  Conservative but works well
// for smooth fBm and stays within the 80-step budget.
// ---------------------------------------------------------------------

// Returns (t, hit).  hit=0 means ray escaped to MAX_DIST.
// Sphere-tracing the heightfield with a conservative step factor.
// fBm is not a true distance field, so we also catch the case where
// we step past the surface (h < 0).
vec2 raymarch(vec3 ro, vec3 rd) {
    float t = 0.0;
    float prev_h = ro.y - terrain(ro.xz);
    for (int i = 0; i < MAX_STEPS; i++) {
        vec3 p = ro + rd * t;
        float h = p.y - terrain(p.xz);
        // Hit on the way down.
        if (h < HIT_EPS) return vec2(t, 1.0);
        if (t > MAX_DIST) break;
        // Conservative step: 0.35 is honest for fBm with slopes
        // up to ~50 degrees; higher factors miss the surface.
        float est = max(h * 0.35, 0.02);
        prev_h = h;
        t += est;
    }
    return vec2(MAX_DIST, 0.0);
}

// ---------------------------------------------------------------------
// Shading.  Simple Lambert + rim + exponential haze.  Keeps it well
// inside the floor-tier fragment budget.
// ---------------------------------------------------------------------

vec3 shadeTerrain(vec3 ro, vec3 rd, float t, Sky sky) {
    vec3 p = ro + rd * t;
    vec3 n = calcNormal(p.xz);

    vec3 L = sky.sun_dir;
    float diff = clamp(dot(n, L), 0.0, 1.0);
    // Hemisphere fill -- kept small so the shadow side reads as
    // shadow, not as another version of the lit side.
    float sky_fill = clamp(0.5 + 0.5 * n.y, 0.0, 1.0);
    vec3 fill_col = mix(sky.horizon, sky.zenith, 0.5);

    // Slope-based albedo.  The rock colour is the dominant visible
    // hue for most pixels; snow appears only on the very top of
    // flat areas (we do NOT make snow dominant -- it would wash
    // out billowy runs into a single cream tone).  Albedo values
    // are saturated, not pastel, so the land reads as land.
    float slope = clamp(n.y, 0.0, 1.0);
    vec3 rock = vec3(0.62, 0.46, 0.34);   // warm rocky tan
    vec3 moss = vec3(0.24, 0.36, 0.18);   // dark green moss
    vec3 snow = vec3(0.94, 0.95, 0.99);   // near-white snow
    vec3 albedo = mix(moss, rock, smoothstep(0.05, 0.50, slope));
    // Snow only at very high slope; the mix factor is small so
    // it tints rather than dominates.
    albedo = mix(albedo, snow, smoothstep(0.88, 0.99, slope) * 0.35);

    // Direct sun strong, fill weak -- this is what gives shadow
    // valleys their depth.
    vec3 col = albedo * (diff * sky.sun_col * 1.20 + 0.18 * sky_fill * fill_col);

    // Subtle rim light so ridges read against the haze.
    float rim = pow(1.0 - clamp(dot(n, -rd), 0.0, 1.0), 3.0);
    col += rim * sky.sun_col * 0.18;

    // Exponential distance haze.  Density driven by iSeed.w (moodier
    // runs haze out faster).  Use the sky color as the haze tint so
    // terrain dissolves INTO the sky, never into black.  Tuned so a
    // hit at ~25 units keeps ~75% of its own colour; far peaks
    // fade into the sky within the frame.
    float haze_density = mix(0.006, 0.018, iSeed.w);
    float haze = 1.0 - exp(-t * haze_density);
    vec3 sky_at_p = shadeSky(rd, sky);
    col = mix(col, sky_at_p, haze);

    return col;
}

// ---------------------------------------------------------------------
// Entry point.  Build the camera, march, then composite sky over
// any ray that escapes.
// ---------------------------------------------------------------------

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 q = fragCoord / iResolution.xy;
    vec2 p = q * 2.0 - 1.0;
    p.x *= iResolution.x / iResolution.y;

    Sky sky = makeSky();
    float t = iTime;
    vec3 ro = cameraPos(t);
    vec3 ta = cameraTarget(t);

    mat3 cam = lookAtMat(ro, ta, vec3(0.0, 1.0, 0.0));
    // Slight FOV (~45 deg vertical).  Tighter than 60 so the horizon
    // sits low and we get plenty of terrain in the frame.
    float fov = 2.2;
    vec3 rd = normalize(cam * vec3(p, fov));

    vec2 hit = raymarch(ro, rd);
    vec3 col;
    if (hit.y > 0.5) {
        col = shadeTerrain(ro, rd, hit.x, sky);
    } else {
        col = shadeSky(rd, sky);
    }

    // Filmic-ish tonemap (Reinhard variant) and gamma.  Avoids the
    // "full-field luminance flash" failure mode -- nothing saturates.
    col = col / (1.0 + col);
    col = pow(col, vec3(1.0 / 2.2));

    // Subtle vignette so the corners don't pull attention.
    float vig = smoothstep(1.4, 0.5, length(p));
    col *= mix(0.85, 1.0, vig);

    fragColor = vec4(col, 1.0);
}