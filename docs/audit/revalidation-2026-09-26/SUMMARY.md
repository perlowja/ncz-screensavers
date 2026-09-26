# revalidation-2026-09-26 — summary

Captured on PEGASUS (192.168.207.85), Intel UHD Graphics (CML GT2), Mesa 26.1.6.
Harness includes fixes **9902704** (frame counter off-by-one) and **142ad27**
(sample back buffer AFTER draw_cb, BEFORE eglSwapBuffers). PEGASUS is the only
host used; MEDUSA belongs to another agent and was not touched. O6N was not
booted for this sweep.

## Per-family results

### xshadertoy (38 targets) — 37 GOOD, 1 BLACK

- **GOOD (37):** alienbeacon, amigajuggler (nframes=1 → STATIC label),
  batteredplanet, bestill0-0, bestill1-0, bestill2-0, bestill3-0,
  bestill4-0, bestill5-0, bubblecolors, darktransit, downfall (mono),
  driftclouds, elementalring, fluxcore, gimbalharmonics (mono with
  pink core), goldenapollian, hexplasma, logarithmiccircles (white
  paper-like spiral), neongravity-1, neontriangulator, noxfire,
  polarnight, prococean, protophore, rigrekt (mono), selfreflect,
  skyline, stardome, starnest, stripeytorus (mono), synthwavecity,
  topologica, trainmandala, trizm, truchetzoom (mono), universeball.
- **BLACK (1):** `xshadertoy_neongravity-0_gles3` — 19 captured frames all
  with `nonblack=0` in `framebuffer.log`, no GL errors, harness ran the full
  timeout. Shader compiles and the loop iterates but produces no visible
  output. Its sibling `xshadertoy_neongravity-1_gles3` (same source family)
  renders correctly, so this is a per-variant defect.

The monochrome-low-saturation cases (downfall, gimbalharmonics,
logarithmiccircles, protophore, rigrekt, stripeytorus, truchetzoom) are
classified GOOD, not BROKEN, because visual inspection of the saved
`small_*.png` files confirms the grayscale palette is the intentional
artistic choice — not a rendering defect.

### rss-sdl2 (13 targets) — 1 GOOD, 12 BROKEN (with varying severity)

- **GOOD (1):** `microcosm_gles3` — gorgeous soft bokeh-blob render with
  real motion and color. The only fully-correct rss-sdl2 target in the set.
- **BROKEN (12):** cyclone, euphoria, fieldlines, flocks, flux, helios,
  hyperspace, implicitdemo (all cov<3%, only the gles3_compat cursor
  widget draws); lattice, plasma, skyrocket, solarwinds (partial renders —
  some content visible but most of the frame is empty or the colour
  mapping is wrong).

This 12/13 BROKEN result on rss-sdl2 is consistent with the family defect
already documented in `MEMORY.md` and the prior rebase commits: the
gles3_compat layer only handles the cursor widget, not the renderer-specific
particle/vector/grid geometry for this family.

## Pipeline notes

- `/tmp` on PEGASUS was **100% full** from the prior batch's `/tmp/cap-*`
  directories. Symptom: harness `fwrite` reported success but the files
  appeared as 0-byte on disk. Fix applied: `rm -rf /tmp/cap-*` on the
  remote before re-capture. Both `skyrocket_gles3` and `solarwinds_gles3`
  were re-captured after this cleanup; their `notes.md` files document
  the recovery.
- An `analyze2.py` was added alongside `analyze.py` to handle targets with
  mixed/empty/corrupted frame sizes (the original `analyze.py` choked on
  non-canonical W×H ratios).
- `classify.py` provides a simple metrics-based classifier that produces
  the GOOD/BLACK/WASHED/STATIC/BROKEN/HUNG labels used in this summary.

## Files

- `scripts/` — capture, analyze, classify, sweep helpers (committed in 83de0fe)
- `pegasus-i/xshadertoy/<target>/` — 38 per-target evidence dirs
- `pegasus-i/rss_sdl2/<target>/` — 13 per-target evidence dirs

Each target dir contains `metrics.json`, `notes.md`, `log.txt`, `renderer.txt`,
`exit.txt`, `hung.txt`, `framebuffer.log`, `nframes.txt`, and `small_*.png`
sample frames downscaled to 960px wide.
