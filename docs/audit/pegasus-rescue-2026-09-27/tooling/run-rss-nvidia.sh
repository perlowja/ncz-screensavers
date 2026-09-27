#!/bin/bash
cd ~/ncz-screensavers/builddir
for t in euphoria skyrocket solarwinds cyclone flocks; do
  echo "=== $t $(date +%H:%M:%S) ==="
  ncz-display-run -t 40 $(command -v ncz-nv) ./${t}_gles3 2>&1 | tail -4
done
echo RSS_NVIDIA_SEQUENCE_DONE
