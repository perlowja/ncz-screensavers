# gl-transitions SHORTLIST — capture-completion addendum

**Date:** 2026-09-26
**Supersedes:** (additive — does not replace the v2 SHORTLIST)

The v2 SHORTLIST in `STEP3-SHORTLIST.md` ships 18 transitions
across three tiers. The earlier capture round (`STEP2-CAPTURES.md`)
produced six-progress captures + montages for 11 of those 18
(crosswarp, undulatingBurnOut, GlitchMemories, displacement,
ripple, LinearBlur, flyeye, directionalwarp, burn, GlitchDisplace,
morph, Drop_Zone_Flicker — i.e. the obvious "look alive"
candidates). The other six — **randomNoisex, crosshatch,
DefocusBlur, old_tv_lost_signal, Swirl, TilesWave** — were
shortlisted from smoke-test output (compile=98 / link=98 / render=90)
and per-shader .glsl inspection, but lacked actual captured
evidence.

This addendum fills that gap: each of the six now has the
standard six-progress capture set (p = 0.00, 0.20, 0.40, 0.50,
0.80, 1.00) plus a 4-panel mid-blend montage, all generated on the
same cerberus / NVIDIA 595.58.03 / GLES 3.20 surface the rest of
the round ran on.

## Files added (42 total: 36 PNGs + 6 montages)

| Transition | Family | Captures | Montage |
|---|---|---|---|
| randomNoisex | dissolve | 6 | montage_randomNoisex.png |
| crosshatch | dissolve | 6 | montage_crosshatch.png |
| DefocusBlur | blur | 6 | montage_DefocusBlur.png |
| old_tv_lost_signal | static/noise | 6 | montage_old_tv_lost_signal.png |
| Swirl | geometric (motion) | 6 | montage_Swirl.png |
| TilesWave | geometric (motion) | 6 | montage_TilesWave.png |

Each individual capture is `captures/<name>_p<progress>.png`;
each montage is `captures/montage_<name>.png` (4-panel
left-to-right at p = 0.20, 0.40, 0.5, 0.80).

## Post-capture re-confirmation of the v2 SHORTLIST rationale

After seeing the actual mid-blend frames for the six previously
unvisualized entries, the v2 SHORTLIST rationale holds and *no
entry is re-ranked*:

- **randomNoisex** — a pure noise-driven dissolve; pixels flip
  stochastically. Confirms the v2 description ("cleanest dissolve
  in the catalogue"). Ships as Tier 1.
- **crosshatch** — a hatched-mask dissolve, visually distinct from
  randomNoisex (pattern vs noise). Confirms v2 description. Ships
  as Tier 2.
- **DefocusBlur** — defocus-blur cross. Reads as "going out of
  focus". Confirms v2 description. Ships as Tier 2.
- **old_tv_lost_signal** — signal-loss / scan-line break-up.
  Reads as "the medium is failing". Confirms v2 description.
  Ships as Tier 3.
- **Swirl** — vortex / swirl. Reads as motion, not as a state-
  machine primitive. Confirms v2 description. Ships as Tier 3.
- **TilesWave** — tile-grid wave. Reads as a propagating front
  (motion), not as a slide. Confirms v2 description. Ships as
  Tier 3.

The 18-entry SHORTLIST is now backed by evidence for every entry.

— generated 2026-09-26 by gles3_transitions_smoke + rgba2png.py + montage.py