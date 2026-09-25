# unknownpleasures resource-default fix — 2026-09-25

The full matrix run exited during initialization because the standalone
compatibility layer returned an empty string for the standard X resource
classes `Foreground` and `Background`. `unknownpleasures` does not repeat
those application-wide defaults in its own `DEFAULTS` block.

After restoring the XScreenSaver defaults (`white` foreground and `black`
background), the target initialized and rendered animated output on both
matrix hosts. Captures were taken with `grim` two and four seconds after
launch. The PNGs here are 480-pixel evidence thumbnails; the unmodified
full-resolution captures remain in each host's
`~/build-tmp/unknownpleasures-fix/` directory.

| Host | Renderer | 2s mean / distinct colors | 4s mean / distinct colors | Changed pixels |
|---|---|---:|---:|---:|
| O6N arm64 | Mali-G720-Immortalis | 0.325902 / 158 | 0.325610 / 152 | 4,116 |
| PEGASUS amd64 | Mesa Intel UHD Graphics CML GT2 | 0.326018 / 10 | 0.325688 / 10 | 5,488 |

Both processes remained alive through the second capture and were terminated
by the validation command. Per-host stderr is retained alongside the images.
