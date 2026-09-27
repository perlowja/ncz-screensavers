# PEGASUS rescue, 2026-09-27

PEGASUS is being reflashed. These are the artifacts that existed only on that
machine. Everything else on it was verified to be either already in git, a
stale snapshot, a third-party clone, or regenerable raw frame capture.

## What was checked before rescuing

* `~/ncz-screensavers` - clean, 0 unpushed. Its single dirty entry was an
  untracked `ridgeline.glsl` already present in master.
* `~/Projects/ncz-screensavers` - 3606 staged adds on a branch with NO
  commits: an init'd copy, never committed. Content is OLDER than master
  (197 vs 199 src files, missing gles3_transitions.c) and still carries
  `amigajuggler.glsl`, which was deliberately cut from the catalogue in
  ba81c7c. Nothing unique.
* `~/Projects/cix-chooser` - branch `feature/ncz-screensaver-chooser` at
  85e8934, confirmed present on its remote.
* `~/ncz-screensavers-jp`, `~/ncz-xshadertoy-verify` - clean, 0 unpushed.
* `~/priorart/{glbg,glshell,hyprsaver,shaderbg}` - third-party upstream
  clones, re-clonable.
* `docs/audit/per-target-evidence` - 44 files, identical count in master,
  already tracked.
* `~/rss-visual-evidence`, `~/remeasure`, `~/verify4`, `~/gles3-validation`
  (7.6G combined) - raw `.rgba` frame captures and a repo copy. Regenerable
  measurement output, deliberately NOT rescued.

## What is here

`scan/results.tsv` - a per-shader scan of 38 xshadertoy targets on
**Mesa Intel(R) UHD Graphics (CML GT2)**, recording PASS/fail plus two pixel
samples per target. The two sample sets are what demonstrate per-run
variation, so this is the measured evidence behind the iSeed work. Not
present anywhere in the repo.

`tooling/` - the measurement and capture harness scripts that produced the
evidence directories above: xshadertoy batch/scan runners, rss visual and
NVIDIA validation runners, the magma capture script, fps and verify
harnesses, and the apothecary dataset fetchers.

`nvidia-poc/` - logs from the NVIDIA linear-buffer proof of concept
(`nvidia-linear-buffer-poc-run.log`, `nvidia-linear-buffer-test.log`,
`nvidia-fourcc-probe.log`). Related to the branch
`argonas/nvidia-linear-buffer-poc-2026-09-22`.

Captured on the hybrid reference machine: Intel UHD CML GT2 iGPU + NVIDIA
RTX 2060 Mobile. That pairing is what makes the numbers meaningful.
