/* blackhole_opts.h - the option table of blackhole_gles3.
 *
 * Single source of truth: the parser, --help/--list-options, the schema dump
 * (--dump-schema, compared with assets/screensaver-chooser/options/blackhole.tsv
 * by a meson test) and the unit test all read this table.
 */
#ifndef BLACKHOLE_OPTS_H
#define BLACKHOLE_OPTS_H
#include "ncz_options.h"

#define BH_PALETTES "stylized,kipthorne,faithful,singularity,slingshot,whitehole"
#define BH_FLYBYS   "auto,orbit,slow-orbit,equatorial,polar,plunge,slingshot,drift,random"

static const ncz_opt_def BH_OPTS[] = {
 /* name, type, default, min, max, choices, aliases, env_alias, label, description, group */
 {"palette", NCZ_OPT_ENUM, "stylized", 0, 0, BH_PALETTES, "stylised=stylized", "NCZ_BLACKHOLE_COLORS",
  "Palette", "Color model. stylized: original art-directed palette. kipthorne: symmetric amber blackbody (Interstellar). faithful: Doppler and redshift shifted blackbody. singularity: strict monochrome white on black (Singularity desktop). slingshot: rust disk with a gold-white beamed arc. whitehole: icy teal and steel blue.", "Look"},
 {"palette-transition", NCZ_OPT_FLOAT, "8", 0, 120, "", "", NULL,
  "Palette cross-fade (s)", "Seconds to cross-fade when the palette changes (palette cycling). 0 switches instantly.", "Look"},
 {"cycle-palettes", NCZ_OPT_LIST, "", 0, 0, BH_PALETTES, "stylised=stylized", NULL,
  "Palette cycle list", "Comma separated palettes to cycle through after the first one. Empty disables cycling.", "Look"},
 {"cycle-interval", NCZ_OPT_FLOAT, "90", 5, 3600, "", "", NULL,
  "Cycle interval (s)", "Seconds each palette stays before cross-fading to the next one in the cycle list.", "Look"},
 {"exposure", NCZ_OPT_FLOAT, "1", 0.2, 4, "", "", NULL,
  "Exposure", "Overall brightness multiplier applied before tone mapping.", "Look"},
 {"disk-brightness", NCZ_OPT_FLOAT, "1", 0.2, 3, "", "", NULL,
  "Disk brightness", "Surface brightness of the accretion disk relative to the per-launch value.", "Look"},
 {"bloom", NCZ_OPT_FLOAT, "1", 0, 3, "", "", NULL,
  "Bloom", "Strength of the halo around the shadow, as a multiple of the palette's own halo (stylized, kipthorne and faithful have none).", "Look"},
 {"fringe", NCZ_OPT_FLOAT, "0.15", 0, 1, "", "", NULL,
  "Chromatic fringe", "Cyan and magenta color fringing at the bright disk edges (whitehole palette).", "Look"},
 {"beaming", NCZ_OPT_FLOAT, "1", 0, 2, "", "", NULL,
  "Beaming", "Strength of Doppler shift and relativistic beaming: 0 removes the bright/dim asymmetry, 2 doubles it.", "Look"},
 {"star-density", NCZ_OPT_FLOAT, "1", 0, 3, "", "", NULL,
  "Star density", "Multiplier on the number of background stars.", "Look"},
 {"nebula", NCZ_OPT_FLOAT, "1", 0, 2, "", "", NULL,
  "Nebula", "Multiplier on the background nebula clouds.", "Look"},
 {"jet", NCZ_OPT_FLOAT, "1", 0, 2, "", "", NULL,
  "Jets", "Multiplier on the polar jets. 0 turns them off.", "Look"},
 {"hot-sector", NCZ_OPT_FLOAT, "0.22", 0, 1, "", "", NULL,
  "Hot sector", "Amplitude of the rotating bright sector in the stylized palette.", "Look"},
 {"flyby", NCZ_OPT_ENUM, "auto", 0, 0, BH_FLYBYS, "", NULL,
  "Camera path", "auto: per-launch random path (previous behavior). orbit and slow-orbit: circular. equatorial: fly past along the disk plane. polar: over the poles. plunge: dive toward the horizon and out. slingshot: swoop in close on a hyperbolic arc and leave. drift: very slow wandering. random: pick one per cycle.", "Camera"},
 {"speed", NCZ_OPT_FLOAT, "1", 0.1, 4, "", "", NULL,
  "Animation speed", "Time scale of the whole animation (camera, disk rotation, palette drift).", "Camera"},
 {"distance-min", NCZ_OPT_FLOAT, "0", 0, 60, "", "", NULL,
  "Closest distance", "Closest camera distance in units of the Schwarzschild radius; 0 = per-path default. Values below 3.2 are raised to 3.2 so the camera never crosses the horizon.", "Camera"},
 {"distance-max", NCZ_OPT_FLOAT, "0", 0, 120, "", "", NULL,
  "Farthest distance", "Farthest camera distance; 0 = per-path default.", "Camera"},
 {"inclination", NCZ_OPT_FLOAT, "-1", -1, 90, "", "", NULL,
  "Inclination (deg)", "Elevation of the view above the disk plane in degrees; -1 = automatic.", "Camera"},
 {"duration", NCZ_OPT_FLOAT, "0", 0, 3600, "", "", NULL,
  "Cycle length (s)", "Seconds per flyby before it restarts with a new variation (with a short fade); 0 = the path's natural length.", "Camera"},
 {"spin", NCZ_OPT_FLOAT, "0", 0, 0.998, "", "", NULL,
  "Spin", "Dimensionless spin a/M. Approximation: moves the inner disk edge inward and speeds up the disk (not full Kerr geodesics).", "Physics"},
 {"rotation-speed", NCZ_OPT_FLOAT, "1", 0, 3, "", "", NULL,
  "Disk rotation speed", "Multiplier on the disk rotation rate.", "Physics"},
 {"precession", NCZ_OPT_FLOAT, "1", 0, 4, "", "", NULL,
  "Disk precession", "Multiplier on the disk's slow precession (auto camera path only).", "Physics"},
 {"temperature", NCZ_OPT_FLOAT, "1", 0.3, 1.6, "", "", NULL,
  "Disk temperature", "Multiplier on the disk heat, which shifts colors toward the hot end.", "Physics"},
 {"lensing", NCZ_OPT_BOOL, "true", 0, 0, "", "", NULL,
  "Gravitational lensing", "Turn off to draw straight rays (debugging).", "Physics"},
 {"adaptive", NCZ_OPT_BOOL, "true", 0, 0, "", "", NULL,
  "Adaptive quality", "Measure frame times and lower the ray-step budget when frames miss the display refresh, with hysteresis (only when quality is auto). Resolution is controlled by the shared render-scale options.", "System"},
 {"quality", NCZ_OPT_ENUM, "auto", 0, 0, "auto,low,medium,high,ultra", "", NULL,
  "Quality", "Ray-march step budget tier. auto picks by GPU and adapts to frame time.", "System"},
 {"seed", NCZ_OPT_INT, "0", 0, 4294967295.0, "", "", "NCZ_BLACKHOLE_FIXED_SEED",
  "Random seed", "Fixes the per-launch randomness so a run can be reproduced; 0 = random.", "System"},
};
#define BH_NOPTS (sizeof BH_OPTS / sizeof BH_OPTS[0])
#endif
