// satin-wind.glsl
// wave2 — quiet materiality: wind across satin / brushed silk.
//
// Algorithm:
//   1. A broad anisotropic height field along one axis (x-axis folds,
//      so the silk is "drawn" horizontally). The folds have weight —
//      neighbouring regions respond with related but DELAYED motion
//      via a phase-offset secondary wave.
//   2. The receiving colour (silk) is stable: warm cream satin with
//      a slow gradient that does not pulse. Only the highlight moves.
//   3. Analytic surface normal from the height field — dh/dx for the
//      dominant fold direction, dh/dy for a tiny perturbation.
//   4. Anisotropic specular highlight that bends and splits as the
//      surface moves. Phong-like, with an elongated specular lobe
//      along the fold direction so the highlight reads as a satin
//      sheen, not a chrome blob.
//
// Tonemap (ACES) + gamma. No microscopic sparkle, no fibres.

#define PI 3.14159265359

// ---- Hash / value noise ----
float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm3(vec2 p) {
    float s = 0.0;
    float a = 0.5;
    for (int i = 0; i < 3; i++) {
        s += a * vnoise(p);
        p *= 2.07;
        a *= 0.5;
    }
    return s;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 res = iResolution.xy;
    vec2 uv = (fragCoord - 0.5 * res) / min(res.x, res.y);
    float t = iTime;

    // ---- Height field: broad anisotropic folds along x-axis ----
    // The silk is "drawn" horizontally; folds form along the y
    // direction (each horizontal slice has its own fold position).
    // We accumulate several overlapping sin waves at different
    // wavelengths + a phase offset that varies along y (so the
    // folds bend subtly) — this is the source of "broad overlapping
    // folds filling the frame".
    //
    // The "delayed motion" comes from a phase that propagates
    // along y: regions at higher y respond to a wave crest that has
    // already passed through regions at lower y. This makes the
    // folds feel heavy — they drift, but adjacent regions don't
    // all snap to the same state at once.
    //
    // Higher base frequency (8x not 4x) so the frame has ~5-7
    // visible fold crests at any time — fills the frame.
    float h = 0.0;
    h += sin(uv.x * 8.0 + t * 0.55 + uv.y * 1.2) * 0.50;
    h += sin(uv.x * 14.0 + t * 0.40 + uv.y * 1.8 + 1.7) * 0.28;
    h += sin(uv.x * 22.0 + t * 0.30 + uv.y * 0.8 - 2.4) * 0.16;

    // Slow vertical warp — the folds themselves bend slightly as if
    // the silk is draped, not perfectly horizontal. A low-frequency
    // noise modulates where the fold crests sit in y.
    float ywarp = fbm3(vec2(uv.y * 1.5, t * 0.07)) * 0.5;
    h += sin(uv.x * 8.0 + t * 0.55 + uv.y * 1.2 + ywarp) * 0.20;

    // ---- Analytic surface normal ----
    // Numerical derivative: dh/dx (dominant) and dh/dy (small).
    // dh/dx is the source of the directional highlight along the
    // fold direction.
    float eps = 0.0035;
    float h_xp = sin((uv.x + eps) * 8.0 + t * 0.55 + uv.y * 1.2 + ywarp) * 0.50
               + sin((uv.x + eps) * 14.0 + t * 0.40 + uv.y * 1.8 + 1.7) * 0.28
               + sin((uv.x + eps) * 22.0 + t * 0.30 + uv.y * 0.8 - 2.4) * 0.16;
    float h_xm = sin((uv.x - eps) * 8.0 + t * 0.55 + uv.y * 1.2 + ywarp) * 0.50
               + sin((uv.x - eps) * 14.0 + t * 0.40 + uv.y * 1.8 + 1.7) * 0.28
               + sin((uv.x - eps) * 22.0 + t * 0.30 + uv.y * 0.8 - 2.4) * 0.16;
    float dhdx = (h_xp - h_xm) / (2.0 * eps);

    // dh/dy: small, used for surface modulation not highlight.
    float h_yp = sin(uv.x * 8.0 + t * 0.55 + (uv.y + eps) * 1.2) * 0.50
               + sin(uv.x * 14.0 + t * 0.40 + (uv.y + eps) * 1.8 + 1.7) * 0.28;
    float h_ym = sin(uv.x * 8.0 + t * 0.55 + (uv.y - eps) * 1.2) * 0.50
               + sin(uv.x * 14.0 + t * 0.40 + (uv.y - eps) * 1.8 + 1.7) * 0.28;
    float dhdy = (h_yp - h_ym) / (2.0 * eps);

    // Build surface normal. The normal has a strong x-tilt (along
    // the fold direction) and a small y-tilt. We don't bother with
    // the y-tilt in the highlight math — it's just for surface
    // shading variation.
    vec3 N = normalize(vec3(-dhdx * 0.10, -dhdy * 0.04, 1.0));

    // ---- Lighting ----
    // A directional light comes from above-left. Stable direction;
    // only the surface normal changes per frame.
    vec3 L = normalize(vec3(-0.55, 0.45, 0.85));

    // ---- Anisotropic highlight ----
    // The satin sheen is along the fold direction. We compute the
    // reflected vector R = reflect(-L, N), then take dot(R, view)
    // where view is roughly +z (we're looking at the silk straight
    // on, the highlight is a horizontal sheen).
    vec3 V = vec3(0.0, 0.0, 1.0);
    vec3 R = reflect(-L, N);

    // Anisotropy: stretch the highlight along the fold direction
    // (x-axis). We compute the half-vector style spec but
    // separately scale the x and y components — making the lobe
    // elongated along the folds.
    vec3 Hr = normalize(L + V);
    float iso = pow(max(dot(N, Hr), 0.0), 32.0);

    // Anisotropic version: elongate along x. Use the x-component of
    // R projected to the camera plane and amplify.
    float aniso_x = R.x;
    float aniso_y = R.y;
    float aniso = pow(max(0.0, sqrt(aniso_x * aniso_x * 4.0 + aniso_y * aniso_y)), 64.0);

    // Combine: the anisotropic term gives the satin "ribbon"
    // highlight; the isotropic term gives a softer broad glow on
    // fold crests.
    float spec = aniso * 1.2 + iso * 0.4;

    // ---- Base silk colour ----
    // Warm cream satin — stable, slow gradient. The colour does not
    // pulse with the highlight. Linear values around 0.20-0.40 so
    // there is real range between the dark fold valleys and the
    // bright fold crests in the ACES curve.
    vec3 baseCol = mix(vec3(0.22, 0.15, 0.10), vec3(0.40, 0.30, 0.20),
                       0.5 + 0.30 * fbm3(uv * 1.8));
    // A subtle warm-to-cool vertical drift so the silk has a soft
    // tonal band but doesn't read as a stripe.
    baseCol *= mix(0.92, 1.05, 0.5 + 0.5 * sin(uv.y * 0.4 + 0.5));

    // ---- Diffuse shading ----
    // Folds facing the light get brighter; folds facing away dim.
    // Ambient + diffuse keep the silk visible without crushing
    // either end of the ACES curve.
    float diff = max(dot(N, L), 0.0);
    vec3 ambient = baseCol * 0.70;
    vec3 lit = ambient + baseCol * diff * 0.40;

    // ---- Highlight ----
    // Warm white sheen; sheen's colour is slightly cooler than the
    // base silk so the highlight pops. ACES protects the brightest
    // peaks from full clipping.
    vec3 sheenCol = vec3(1.0, 0.95, 0.88);
    vec3 col = lit + spec * 0.45 * sheenCol;

    // ---- Vignette ----
    float vig = 1.0 - 0.18 * dot(uv, uv);
    col *= vig;

    // ---- Tonemap (ACES Narkowicz) + gamma ----
    vec3 a = col * (2.51 * col + 0.03);
    vec3 b = col * (2.43 * col + 0.59) + 0.14;
    vec3 mapped = clamp(a / b, 0.0, 1.0);
    fragColor = vec4(pow(mapped, vec3(1.0 / 2.2)), 1.0);
}