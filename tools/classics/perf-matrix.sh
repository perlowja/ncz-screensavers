#!/bin/bash
# perfall.sh LEGACYDIR NEWDIR OUTDIR SECS : legacy vs classic vs enhanced perf matrix (no frame dumps)
LB=$1; NB=$2; OUT=$3; S=$4
PORTED="voronoi gravitywell hexstrut cubestorm crackberg cityflow noof geodesic gibson"
cd "$(dirname "$0")"
# the GL1 builds were retired (docs/CLASSICS-SHADER-PORT.md); the baseline only runs when a pre-0.5 build dir supplies <hack>_legacy_gles3
for h in $PORTED; do [ -x $LB/${h}_legacy_gles3 ] && ./run-hack.sh $LB $OUT $S "" perf-legacy ${h}_legacy 2>&1 | grep -E "^${h}_legacy"; done
for h in $PORTED; do ./run-hack.sh $NB $OUT $S "--style=classic --seed=1" perf-classic $h 2>&1 | grep -E "^${h} "; done
for h in $PORTED; do ./run-hack.sh $NB $OUT $S "--style=enhanced --seed=1" perf-enh $h 2>&1 | grep -E "^${h} "; done
for h in hypertorus klein projectiveplane; do ./run-hack.sh $NB $OUT $S "" perf-upstream $h 2>&1 | grep -E "^${h} "; done
echo DONE
