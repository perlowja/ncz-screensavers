# Black Hole options

`blackhole_gles3` reads the same options from three places. Precedence, highest first:

1. command line `--option=value` (booleans also `--lensing` and `--no-lensing`)
2. environment `NCZ_BLACKHOLE_<OPTION>` (upper case, `-` written as `_`)
3. key file `$XDG_CONFIG_HOME/ncz-screensavers/blackhole.conf` (`~/.config/...` when unset), group `[blackhole]`, one `option=value` per line; `NCZ_BLACKHOLE_CONFIG` overrides the path
4. built-in defaults

An unknown option or an invalid value (wrong type, outside its range, not one of the choices) is reported once on stderr and ignored: the next lower source applies. `--help` and `--list-options` print everything with type, range and default. The declarative schema for settings UIs is `assets/screensaver-chooser/options/blackhole.tsv` (installed to `/usr/share/ncz-screensavers/options/`); a build test keeps it identical to what `blackhole_gles3 --dump-schema` prints.

Legacy names still work: `NCZ_BLACKHOLE_COLORS` sets `palette` (`stylised` is accepted), `NCZ_BLACKHOLE_FIXED_SEED` sets `seed`.

Examples:

```
blackhole_gles3 --palette=slingshot --flyby=slingshot --spin=0.95
blackhole_gles3 --palette=whitehole --cycle-palettes=slingshot,singularity --cycle-interval=60 --palette-transition=10
NCZ_BLACKHOLE_FLYBY=random NCZ_BLACKHOLE_DURATION=45 blackhole_gles3
printf '[blackhole]\npalette=singularity\nbloom=0.5\n' > ~/.config/ncz-screensavers/blackhole.conf
```

## Look

| Option | Type | Default | Range / choices | Description |
|---|---|---|---|---|
| `--palette` | enum | `stylized` | stylized, kipthorne, faithful, singularity, slingshot, whitehole | Color model. stylized: original art-directed palette. kipthorne: symmetric amber blackbody (Interstellar). faithful: Doppler and redshift shifted blackbody. singularity: strict monochrome white on black (Singularity desktop). slingshot: rust disk with a gold-white beamed arc. whitehole: icy teal and steel blue. |
| `--palette-transition` | float | `8` | 0 to 120 | Seconds to cross-fade when the palette changes (palette cycling). 0 switches instantly. |
| `--cycle-palettes` | string | `(empty)` | stylized, kipthorne, faithful, singularity, slingshot, whitehole | Comma separated palettes to cycle through after the first one. Empty disables cycling. |
| `--cycle-interval` | float | `90` | 5 to 3600 | Seconds each palette stays before cross-fading to the next one in the cycle list. |
| `--exposure` | float | `1` | 0.2 to 4 | Overall brightness multiplier applied before tone mapping. |
| `--disk-brightness` | float | `1` | 0.2 to 3 | Surface brightness of the accretion disk relative to the per-launch value. |
| `--bloom` | float | `1` | 0 to 3 | Strength of the halo around the shadow, as a multiple of the palette's own halo (stylized, kipthorne and faithful have none). |
| `--fringe` | float | `0.15` | 0 to 1 | Cyan and magenta color fringing at the bright disk edges (whitehole palette). |
| `--beaming` | float | `1` | 0 to 2 | Strength of Doppler shift and relativistic beaming: 0 removes the bright/dim asymmetry, 2 doubles it. |
| `--star-density` | float | `1` | 0 to 3 | Multiplier on the number of background stars. |
| `--nebula` | float | `1` | 0 to 2 | Multiplier on the background nebula clouds. |
| `--jet` | float | `1` | 0 to 2 | Multiplier on the polar jets. 0 turns them off. |
| `--hot-sector` | float | `0.22` | 0 to 1 | Amplitude of the rotating bright sector in the stylized palette. |

## Camera

| Option | Type | Default | Range / choices | Description |
|---|---|---|---|---|
| `--flyby` | enum | `auto` | auto, orbit, slow-orbit, equatorial, polar, plunge, slingshot, drift, random | auto: per-launch random path (previous behavior). orbit and slow-orbit: circular. equatorial: fly past along the disk plane. polar: over the poles. plunge: dive toward the horizon and out. slingshot: swoop in close on a hyperbolic arc and leave. drift: very slow wandering. random: pick one per cycle. |
| `--speed` | float | `1` | 0.1 to 4 | Time scale of the whole animation (camera, disk rotation, palette drift). |
| `--distance-min` | float | `0` | 0 to 60 | Closest camera distance in units of the Schwarzschild radius; 0 = per-path default. Values below 3.2 are raised to 3.2 so the camera never crosses the horizon. |
| `--distance-max` | float | `0` | 0 to 120 | Farthest camera distance; 0 = per-path default. |
| `--inclination` | float | `-1` | -1 to 90 | Elevation of the view above the disk plane in degrees; -1 = automatic. |
| `--duration` | float | `0` | 0 to 3600 | Seconds per flyby before it restarts with a new variation (with a short fade); 0 = the path's natural length. |

## Physics

| Option | Type | Default | Range / choices | Description |
|---|---|---|---|---|
| `--spin` | float | `0` | 0 to 0.998 | Dimensionless spin a/M. Approximation: moves the inner disk edge inward and speeds up the disk (not full Kerr geodesics). |
| `--rotation-speed` | float | `1` | 0 to 3 | Multiplier on the disk rotation rate. |
| `--precession` | float | `1` | 0 to 4 | Multiplier on the disk's slow precession (auto camera path only). |
| `--temperature` | float | `1` | 0.3 to 1.6 | Multiplier on the disk heat, which shifts colors toward the hot end. |
| `--lensing` | bool | `true` |  | Turn off to draw straight rays (debugging). |

## System

| Option | Type | Default | Range / choices | Description |
|---|---|---|---|---|
| `--render-scale` | float | `1` | 0.4 to 1 | Fraction of the output resolution the ray-marcher renders at (linear upscale). Lower it on heavy panels; combined with automatic scaling. |
| `--adaptive` | bool | `true` |  | Measure frame times and lower ray steps and resolution when frames miss the display refresh, with hysteresis (only when quality is auto). |
| `--quality` | enum | `auto` | auto, low, medium, high, ultra | Ray-march step budget tier. auto picks by GPU and adapts to frame time. |
| `--seed` | int | `0` | 0 to 4.29497e+09 | Fixes the per-launch randomness so a run can be reproduced; 0 = random. |


## Performance notes

`quality=auto` (default) with `adaptive=true` adapts ray steps and render scale to the display refresh; see `BLACKHOLE-STUTTER.md`. `NCZ_BLACKHOLE_PERF_LOG=2` prints frame-time statistics. Palette and flyby settings never recompile the shader: they are uniforms, and palette changes cross-fade over `palette-transition` seconds.

## Flyby camera paths

`auto` keeps the previous per-launch random path. The other types are deterministic functions of time and the seed: `orbit`/`slow-orbit` (circular, one revolution per cycle of 60/180 s), `equatorial` (hyperbolic pass in the disk plane), `polar` (pass over the poles), `plunge` (dive to `distance-min` and out), `slingshot` (eccentricity 1.18, swings through about 285 degrees), `drift` (slow closed wandering), `random` (a type per cycle). Types that do not close on themselves fade through black for 1.2 s at each cycle boundary. The camera never goes closer than 3.2 Schwarzschild radii, and each path has its own default distance range (orbit 11 to 16, equatorial 6.5 to 30, polar 7.5 to 30, plunge 3.8 to 26, slingshot 4.6 to 34, drift 9 to 20).
