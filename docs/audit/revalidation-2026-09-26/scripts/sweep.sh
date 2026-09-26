#!/bin/bash
# Sweep all targets on one GPU. Logs results to a TSV.
# Usage: sweep.sh <gpu-tag> <output-tsv> <timeout-sec>
set -u

GPU="${1:-i}"
OUT_TSV="${2:-/tmp/sweep.tsv}"
TIMEOUT_S="${3:-28}"

# 51 targets in scope
SHADERTOY_TARGETS=(
    xshadertoy_alienbeacon_gles3 xshadertoy_amigajuggler_gles3
    xshadertoy_batteredplanet_gles3
    xshadertoy_bestill0-0_gles3 xshadertoy_bestill1-0_gles3 xshadertoy_bestill2-0_gles3
    xshadertoy_bestill3-0_gles3 xshadertoy_bestill4-0_gles3 xshadertoy_bestill5-0_gles3
    xshadertoy_bubblecolors_gles3 xshadertoy_darktransit_gles3 xshadertoy_downfall_gles3
    xshadertoy_driftclouds_gles3 xshadertoy_elementalring_gles3 xshadertoy_fluxcore_gles3
    xshadertoy_gimbalharmonics_gles3 xshadertoy_goldenapollian_gles3 xshadertoy_hexplasma_gles3
    xshadertoy_logarithmiccircles_gles3
    xshadertoy_neongravity-0_gles3 xshadertoy_neongravity-1_gles3 xshadertoy_neontriangulator_gles3
    xshadertoy_noxfire_gles3 xshadertoy_polarnight_gles3 xshadertoy_prococean_gles3
    xshadertoy_protophore_gles3 xshadertoy_rigrekt_gles3
    xshadertoy_selfreflect_gles3 xshadertoy_skyline_gles3 xshadertoy_stardome_gles3
    xshadertoy_starnest_gles3 xshadertoy_stripeytorus_gles3 xshadertoy_synthwavecity_gles3
    xshadertoy_topologica_gles3 xshadertoy_trainmandala_gles3 xshadertoy_trizm_gles3
    xshadertoy_truchetzoom_gles3 xshadertoy_universeball_gles3
)
RSS_TARGETS=(
    cyclone_gles3 euphoria_gles3 fieldlines_gles3 flocks_gles3
    flux_gles3 helios_gles3 hyperspace_gles3 implicitdemo_gles3
    lattice_gles3 microcosm_gles3 plasma_gles3 skyrocket_gles3 solarwinds_gles3
)
ALL_TARGETS=("${SHADERTOY_TARGETS[@]}" "${RSS_TARGETS[@]}")

CAPTURE_SCRIPT="$HOME/build-tmp/audit-2026-09-26/scripts/capture-target.sh"
EVIDENCE_BASE="$HOME/build-tmp/audit-2026-09-26/pegasus-${GPU}"

mkdir -p "$EVIDENCE_BASE"

# TSV header
echo -e "idx\ttarget\tfamily\tgpu\texit\tnframes\trenderer" > "$OUT_TSV"

for i in "${!ALL_TARGETS[@]}"; do
    TGT="${ALL_TARGETS[$i]}"
    FAMILY="rss_sdl2"
    case "$TGT" in
        xshadertoy_*) FAMILY="xshadertoy" ;;
    esac

    TARGET_DIR="$EVIDENCE_BASE/$FAMILY/$TGT"
    mkdir -p "$TARGET_DIR"

    echo "[$GPU] ($((i+1))/${#ALL_TARGETS[@]}) $TGT"
    START=$(date +%s)
    "$CAPTURE_SCRIPT" "$GPU" "$TGT" "$TARGET_DIR" "$TIMEOUT_S" 2>&1 | tail -3
    END=$(date +%s)
    ELAPSED=$((END-START))

    EXIT=$(cat "$TARGET_DIR/exit.txt" 2>/dev/null || echo "?")
    NFRAMES=$(cat "$TARGET_DIR/nframes.txt" 2>/dev/null || echo "0")
    RENDERER=$(cat "$TARGET_DIR/renderer.txt" 2>/dev/null | head -1 | sed 's/RENDERER=//')

    echo -e "$((i+1))\t$TGT\t$FAMILY\t$GPU\t$EXIT\t$NFRAMES\t$RENDERER\t${ELAPSED}s" >> "$OUT_TSV"
done

echo "DONE: $OUT_TSV"
