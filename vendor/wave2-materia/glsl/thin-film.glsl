// thin-film.glsl
// wave2 — quiet materiality: macro thin film.
//
// Algorithm:
//   1. The thin film is a flat sheet filling the frame. We give it
//      a very gentle undulation so the surface normal varies per
//      fragment — this changes the optical path length and gives
//      the angle-dependent interference its variation.
//   2. The film thickness varies SLOWLY across the sheet — most of
//      the frame is one dominant thickness. Only a few narrow bands
//      of slightly different thickness produce coloured fringes.
//   3. Interference colour: 3-component cosine sum (red, green, blue
//      at different phases) for a cheap visual interference.
//
// "Large quiet areas are essential — continuous saturated rainbow
// destroys the material impression. Not another soap bubble."
// Strategy for quiet: the thickness is concentrated around a single
// value with epsilon-amplitude fbm. The colour appears only where the
// phase wraps past a few multiples — a few narrow fringes over a
// wide quiet base.
//
// Tonemap (ACES) + gamma.

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

float fbm2(vec2 p) {
    float s = 0.0;
    float a = 0.5;
    for (int i = 0; i < 2; i++) {
        s += a * vnoise(p);
        p *= 2.07;
        a *= 0.5;
    }
    return s;
}

vec3 interference(float phase) {
    float r = 0.5 + 0.5 * cos(phase * 6.2832 + 0.0);
    float g = 0.5 + 0.5 * cos(phase * 6.2832 + 2.094);
    float b = 0.5 + 0.5 * cos(phase * 6.2832 + 4.189);
    return vec3(r, g, b);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 res = iResolution.xy;
    vec2 uv = (fragCoord - 0.5 * res) / min(res.x, res.y);
    float t = iTime;

    // ---- Surface undulation (very gentle) ----
    vec2 q = uv * 1.0 + vec2(t * 0.025, t * 0.020);
    float h = fbm2(q) * 0.25 + vnoise(uv * 2.0 + t * 0.04) * 0.08;
    float eps = 0.008;
    float h_xp = fbm2(q + vec2(eps, 0.0)) * 0.25 + vnoise((uv + vec2(eps, 0.0)) * 2.0 + t * 0.04) * 0.08;
    float h_xm = fbm2(q - vec2(eps, 0.0)) * 0.25 + vnoise((uv - vec2(eps, 0.0)) * 2.0 + t * 0.04) * 0.08;
    float h_yp = fbm2(q + vec2(0.0, eps)) * 0.25 + vnoise((uv + vec2(0.0, eps)) * 2.0 + t * 0.04) * 0.08;
    float h_ym = fbm2(q - vec2(0.0, eps)) * 0.25 + vnoise((uv - vec2(0.0, eps)) * 2.0 + t * 0.04) * 0.08;
    vec3 N = normalize(vec3(-(h_xp - h_xm) / (2.0 * eps),
                            -(h_yp - h_ym) / (2.0 * eps),
                            1.0));

    // ---- Thickness field ----
    // Low frequency + moderate amplitude. The thickness produces
    // multiple slow bands of different thicknesses across the frame
    // — but each band is a quiet pearl-tinted region, not a
    // saturated rainbow. Colour appears in the GRADIENT zones
    // between bands.
    float thick_raw = fbm2(uv * 0.5 + vec2(t * 0.018, -t * 0.014));
    // Amplitude 0.18 keeps things in a moderate range; we want
    // maybe 2-3 phase cycles across the frame.
    float thickness = 0.50 + 0.18 * thick_raw;

    // ---- Optical path difference ----
    // Phase advances with both thickness AND the normal tilt.
    float cosTheta = max(N.z, 0.1);
    float n = 1.4;
    // The factor 4.0 makes thickness alone produce ~4 * 1.4 * 0.36
    // = 2.0 phase units across the 0.36 thickness range — i.e. one
    // full rainbow cycle. Plus the angle tilt adds a smaller offset
    // within each region.
    float phase = thickness * 4.0 * n * cosTheta + 0.4;

    vec3 ifc = interference(phase);

    // ---- Band-concentration mask ----
    // We want the colour to appear MOSTLY in the gradient zones
    // (where thickness is changing) and be quiet elsewhere. The
    // gradient magnitude is small in flat regions, large at band
    // boundaries. The smoothstep is steep so the colour is
    // localised to narrow bands.
    float t_xp = fbm2((uv + vec2(0.01, 0.0)) * 0.5 + vec2(t * 0.018, -t * 0.014));
    float t_xm = fbm2((uv - vec2(0.01, 0.0)) * 0.5 + vec2(t * 0.018, -t * 0.014));
    float t_yp = fbm2((uv + vec2(0.0, 0.01)) * 0.5 + vec2(t * 0.018, -t * 0.014));
    float t_ym = fbm2((uv - vec2(0.0, 0.01)) * 0.5 + vec2(t * 0.018, -t * 0.014));
    float grad = length(vec2(t_xp - t_xm, t_yp - t_ym)) * 100.0;
    float concentration = smoothstep(0.4, 1.8, grad);

    // ---- Saturation reduction ----
    // The interference colour is full-saturation; desaturate it
    // toward grey so the colours are SUBTLE rather than psychedelic.
    ifc = mix(vec3(0.5), ifc, 0.7);

    // ---- Compose ----
    // Base: pearl-white sheen with a very subtle warm tint.
    vec3 pearl = vec3(0.92, 0.92, 0.94);

    // Add the interference colour, controlled by the concentration
    // mask — strong only in band boundaries.
    vec3 col = pearl + (ifc - vec3(0.5)) * concentration * 0.80;

    // A very faint dark wash where the film is thinnest, so the
    // corners feel a bit darker than the centre.
    float darken = smoothstep(0.48, 0.42, thickness);
    col = mix(col, col * 0.7, darken * 0.3);

    // ---- Vignette ----
    float vig = 1.0 - 0.20 * dot(uv, uv);
    col *= vig;

    // ---- Tonemap (ACES Narkowicz) + gamma ----
    vec3 a = col * (2.51 * col + 0.03);
    vec3 b = col * (2.43 * col + 0.59) + 0.14;
    vec3 mapped = clamp(a / b, 0.0, 1.0);
    fragColor = vec4(pow(mapped, vec3(1.0 / 2.2)), 1.0);
}