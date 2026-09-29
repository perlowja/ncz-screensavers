# Black Hole scenes: design and backlog

One concern: what to add beyond the current single-hole renderer, how it is packaged, what it costs on each GPU, and how accurate each scene is.

## Packaging model

1. **Presets** (done, `BLACKHOLE-PRESETS.md`): named option bundles for the existing renderer. Each is optionally its own catalog row (`presets.tsv`), launching `blackhole_gles3 --preset=<id>`. Cheap to add; no shader work when the look can be reached with existing options plus the small extras (`disk-outer`, `jet-length`, `torus`, `companion`).
2. **Scenes** (backlog): heavier render modes selected by a `scene` option on the same binary, reusing the renderer, palettes, options plumbing and render-scale. Each scene ships a light quality mode for iGPUs. Both mechanisms get an accuracy label shown in the UI: `faithful` (geometry and numbers follow published physics or measurements) or `artistic` (motivated, scaled or stylized).

## Measured baseline (Schwarzschild ray-marcher, default options)

| GPU | Result |
|---|---|
| Radeon Pro 5500M (Navi14, radeonsi) | 60 fps at 1536x960, GPU 40 to 70 percent busy, p99 frame 17 ms |
| Intel UHD 630 (iris) | about 12 to 15 fps at 1080p at the lowest step budget; the adaptive controller holds 60 fps at 1536x960 only at render scale 0.5 with 80 steps |
| Mali-G720 (Sky1, mali_kbase), 1080p output | 59 fps steady, p95 17 ms |
| RTX 2060 (NVIDIA driver) | not measured in this work; expect at least Navi14 class |

Costs below are estimates relative to that baseline (x1.0) and are marked as such until built.

## Scene menu

| Scene | Feasibility | Est. cost | Accuracy | Notes |
|---|---|---|---|---|
| Kerr (spinning), proper | medium | x1.4 to x1.9 | faithful | Replace the Schwarzschild Binet integrator with Kerr geodesics (Carter constant, Boyer-Lindquist or Kerr-Schild). Gives the real frame-dragging shadow offset and asymmetry. Today `spin` only moves the disk edge (ISCO) and speeds the disk. |
| Quasar / AGN, proper | medium | x1.1 to x1.3 | artistic | Presets `quasar`, `quasar-edge-on`, `blazar`, `ton-618` exist. Missing: lensed torus, broad-line clouds, jet precession and knots moving at c. |
| Microquasar (Cygnus X-1), proper | medium | x1.1 | artistic | Preset exists (unlensed star and stream). Proper: lensed Roche-lobe star, stream integrated as geodesic particles. |
| Sagittarius A*, extras | medium | x1.1 | faithful | Preset exists. Missing: flares (hourly brightening hot spot on the ISCO orbit, period about 30 minutes at r = 6 GM/c^2), S-stars on Keplerian ellipses (S2 period 16 yr, pericenter 120 AU), scaled time. |
| M87* | easy | done | faithful | Preset exists; longer jet needs only larger `jet-length`. |
| TON 618 | easy | done | artistic | Preset exists. |
| Binary black holes | hard | x1.5 (two-hole deflection) or x0.4 (baked lensing map) | artistic (superposed) | Superposing two Schwarzschild deflection fields is not GR; a baked lookup of a numerical lensing map per orbital phase is cheap but needs an offline tool. |
| Inspiral, merger, ringdown (GW150914 style) | hard | x1.3 | artistic | Chirp waveform from the post-Newtonian phase plus a damped-sinusoid ringdown; ripples as a lensing perturbation on the sky. |
| 3 or more holes | hard | x2+ | artistic | Chaotic N-body integration on the CPU per frame; deflection from all holes; only for discrete GPUs. |
| Tidal disruption event | medium | x1.2 | artistic | Star shredded into a stream (particle system on the CPU) wrapping into a disk over minutes; fits the microquasar stream code. |
| Wormhole (Ellis, Interstellar style) | easy | x0.8 | faithful (closed form) | The Ellis wormhole has closed-form lensing; no integration needed, cheaper than the black hole. Two sky textures (both mouths). |
| Powers-of-ten zoom into a feeding hole | medium | x1.0 | artistic | Galaxy with dust lanes and gas streams as layered procedural noise, camera dives to the nucleus; implemented as a new flyby type `zoom` plus a galaxy pass at far distance. |
| Galactic center | medium | x1.2 | artistic | Stars on elliptical orbits (Sgr A* S-star extras), gas wind. |
| Neutron star, pulsar, magnetar | easy | x0.3 | artistic | Lighthouse beams and a rotating field-line glow; not a black hole; cheap extra. |
| Naked singularity, stronger white hole | easy | x0.9 | artistic (fantasy) | Change the boundary condition of the integrator or invert the disk emission; label clearly as fantasy. |

## Backlog, priority order, effort

1. Sgr A* flares and S-stars (2 days): particles on analytic Kepler orbits, additive glow, no integration.
2. Wormhole (2 days): closed form, lowest risk, visually strong.
3. Pulsar and magnetar (1 day).
4. Proper Kerr geodesics (5 days plus validation against published shadow shapes): the only scene that changes the core loop; keep the Schwarzschild loop as the fast path for iGPUs.
5. Tidal disruption event (3 days).
6. Powers-of-ten galaxy zoom (4 days).
7. Binary holes with baked lensing maps (6 days including the offline tool); inspiral and merger (3 more days).
8. Three or more holes, naked singularity, white-hole variants (later; low value for the cost).

None of steps 2 and up is started; they are implemented only on request.

## Physical numbers (sources)

Horizon radius r_s = 2GM/c^2 = 2.95 km per solar mass. Sgr A*: 4.3e6 solar masses, 8.28 kpc (GRAVITY 2019; EHT 2022 shadow about 52 microarcseconds). M87*: 6.5e9 solar masses, 16.8 Mpc, shadow about 42 microarcseconds (EHT 2019). TON 618: about 4e10 solar masses (estimates up to 6.6e10). Cygnus X-1: 21.2 solar mass hole, 40.6 solar mass companion, 5.6 day period, inclination 27 degrees, 2.2 kpc (Miller-Jones 2021). ISCO for prograde Kerr orbits: 3 r_s at a = 0 to about 0.6 r_s at a = 0.998 (Bardeen, Press, Teukolsky 1972).
