# Black Hole presets

A preset is a named bundle of Black Hole options. Run one with `blackhole_gles3 --preset=ID`; list them with `blackhole_gles3 --list-presets` (id, name, accuracy, description); see what a run will use with `--print-config`.

Precedence, lowest to highest: built-in defaults, `blackhole.conf`, **the preset**, `NCZ_BLACKHOLE_<OPTION>` environment, command line. An explicit preset beats the saved defaults, and anything you set on the command line or in the environment still wins (`--preset=kerr --spin=0.7` is Kerr with spin 0.7). An unknown preset id is reported and ignored.

## Files

* Sources of truth: `assets/screensaver-chooser/options/presets/blackhole/<id>.conf`, installed to `/usr/share/ncz-screensavers/presets/blackhole/`. Also searched: `$NCZ_PRESET_DIR`, `~/.local/share/ncz-screensavers/presets/blackhole/`, so users can add their own.
* Format (key file): a `[preset]` section with `name`, `description`, `accuracy` (`faithful` or `artistic`) and `sources`; then a `[blackhole]` section of option values. The generator validates every value against `options/blackhole.tsv` (type, range, choices).
* Catalog rows: `tools/generate-catalog.py` writes `assets/screensaver-chooser/presets.tsv` (installed to `/usr/share/ncz-screensavers/` and `/usr/share/ncz-screensaver-chooser/`), 9 tab-separated columns: `id` (row id, `blackhole_gles3--<preset>`), `title` ("Black Hole: M87*"), `group`, `hack` (binary id), `args` (`--preset=<id>`), `accuracy`, `description`, `min_gpu_class` (`weak`, `mid` or `strong`), `measured` (fps on the reference GPUs). `hacks.tsv` stays exactly four columns; a launcher or chooser lists these rows next to the hack rows and starts `hack args`, so each preset can be picked individually, put in the random pool or a playlist. A build test regenerates the file and fails on drift.

## Accuracy labels

`faithful`: geometry, viewing angle and colors follow published measurements (numbers below). `artistic`: physically motivated but scaled or stylized for viewing; the description says how. In every case the ray tracing is Schwarzschild; the `spin` option changes the disk edge and speed only (an approximation, see the design doc).

| Preset | Accuracy | What it shows | Numbers used |
|---|---|---|---|
| sagittarius-a-star | faithful | small hot flow, orange crescent, thin photon ring, seen 30 degrees from the pole | 4.3e6 solar masses, 8.28 kpc, shadow about 52 microarcseconds (EHT 2022, GRAVITY 2019); r_s = 2.95 km per solar mass = 1.27e7 km |
| m87-star | faithful | bright ring, brighter on one side, long jet, 17 degrees from the pole | 6.5e9 solar masses, 16.8 Mpc, shadow about 42 microarcseconds (EHT 2019); r_s about 130 AU |
| ton-618 | artistic | hyperluminous quasar: enormous disk, twin jets, dusty torus | about 4e10 solar masses (estimates 4 to 6.6e10), r_s about 790 AU, luminosity about 4e40 W; scene scaled |
| quasar | artistic | bright disk, dusty torus, twin jets, viewed 25 degrees off the pole | unified model (Antonucci 1993, Urry and Padovani 1995) |
| quasar-edge-on | artistic | the same nucleus seen through the torus plane: disk hidden, jets to the sides | same |
| blazar | artistic | jet pointing at us, strong beaming, disk face-on | Urry and Padovani 1995 |
| microquasar | artistic | stellar-mass hole, blue supergiant companion, gas stream into the disk | Cygnus X-1: 21.2 and 40.6 solar masses, inclination 27 degrees, 2.2 kpc, period 5.6 d (Miller-Jones 2021); the real system is wind fed, and the star and stream are drawn unlensed |
| kerr | artistic | near-maximal spin: disk edge close to the horizon, strongly beamed | Bardeen, Press and Teukolsky 1972 (ISCO 3 r_s at spin 0 to about 0.6 r_s at 0.998, prograde) |

`inclination` in the files is the elevation above the disk plane, so "30 degrees from the pole" is 60. New extras used by presets: `disk-outer` (disk size in r_s), `jet-length`, `torus` (opacity of the dusty ring, a straight-ray approximation) and `companion` (star and stream, artistic). The `eht` palette (Event Horizon Telescope false color) is the seventh palette.

## Screenshots

`docs/blackhole/presets-*.jpg`: all eight presets at four times each.

## GPU classes and measured performance

Every preset declares `min-gpu-class` (the weakest GPU class that still runs it at the bar of 30 fps with a 95th percentile frame time under 40 ms) and `measured`. Classes: `weak` (Intel UHD 630 reference), `mid` (Mali-G720 reference), `strong` (Radeon Pro 5500M, RTX 2060 class). The class comes from `NCZ_GPU_CLASS` when the launcher sets it (from its calibration), otherwise from GL_RENDERER (Intel and CPU renderers weak, Mali mid, everything else strong).

Weak-class defaults (used unless the user sets the options): render scale 0.5 (960x540 upscaled to 1080p by the compositor), the ray-step budget at its lowest, no bloom, half the nebula and star density, torus opacity x0.6. Measured on PEGASUS (Intel UHD 630, Mesa iris, 1920x1080, clean timing, 26 s per run, no readbacks):

| Preset | weak (UHD 630) fps, p95 | mid (Mali-G720, 1080p) | strong (Navi14) | min-gpu-class |
|---|---|---|---|---|
| default (no preset) | 49, 21.5 ms | 59 | 60 | weak |
| sagittarius-a-star | 54, 18.6 ms | 60 | 60 | weak |
| m87-star | 54, 18.7 ms | 60 | 60 | weak |
| ton-618 | 49, 20.6 ms | 60 | 60 | weak |
| quasar | 48, 20.9 ms | 60 | 60 | weak |
| quasar-edge-on | 50, 20.1 ms | 60 | 60 | weak |
| blazar | 49, 20.7 ms | 60 | 60 | weak |
| microquasar | 52, 19.6 ms | 60 | 60 | weak |
| kerr | 53, 19.2 ms | 60 | 60 | weak |

All existing presets and Black Hole itself clear the bar on the weakest reference GPU at the weak-class defaults, so all are `weak`. The RTX 2060 was not measured here. Scenes from `BLACKHOLE-SCENES-DESIGN.md` that are not built yet: binary and merger, galaxy zoom, tidal disruption and three-hole scenes are expected to be `strong` only, proper Kerr geodesics `mid` or better (design-doc estimates, to be measured when built).
