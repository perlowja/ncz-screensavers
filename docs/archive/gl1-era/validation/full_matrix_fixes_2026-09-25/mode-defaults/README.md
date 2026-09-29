# Standard ModeInfo defaults fix — 2026-09-25

The compatibility layer parsed each hack's `DEFAULTS` block for resource
lookups but did not copy xlockmore's standard fields into `ModeInfo`.
Consequently `MI_COUNT(mi)` remained zero even when a hack declared a nonzero
`*count`, causing several draw loops to emit no geometry.

The harness now applies declared `delay`, `count`, `cycles`, `size`,
`wireframe`, and `showFPS` values to their standard `ModeInfo` fields before
initialization. Captures were taken with `grim` at two and four seconds. The
PNGs in the per-target directories are 480-pixel thumbnails; full-resolution
captures remain in `~/build-tmp/mode-defaults-fix/` on each host.

| Target | O6N changed pixels | PEGASUS changed pixels |
|---|---:|---:|
| `bouncingcow_gles3` | 52,546 | 46,783 |
| `cubenetic_gles3` | 1,221,481 | 1,035,024 |
| `hextrail_gles3` | 357,395 | 394,858 |
| `skytentacles_gles3` | 989,230 | 1,027,493 |
| `winduprobot_gles3` | 665,655 | 55,386 |

All ten runs produced non-black changing frames and remained alive through
the second capture. `photopile`, `quasicrystal`, `sballs`, and `timetunnel`
were also retested because they consume these standard fields, but did not
meet the animation gate and remain failures in the ledger.
