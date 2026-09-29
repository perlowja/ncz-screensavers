#!/bin/bash
# usage: bhrender.sh <host-with-build-src> ; renders modes on PEGASUS
set -u
export SSHPASS=pegasus
P="sshpass -e ssh -o StrictHostKeyChecking=no pegasus@192.168.207.85"
SRC=192.168.207.68:Projects/ncz-ss-consol
rm -rf bhstage && mkdir -p bhstage/vendor/blackhole
scp -q $SRC/build-amd64/blackhole_gles3 bhstage/
scp -q $SRC/vendor/blackhole/blackhole.frag bhstage/vendor/blackhole/
$P 'rm -rf ~/bhtest && mkdir -p ~/bhtest'
sshpass -e scp -q -r bhstage/* pegasus@192.168.207.85:bhtest/
for mode in ${MODES:-stylized kipthorne faithful}; do
 for seed in ${SEEDS:-7}; do
  $P "cd ~/bhtest && export XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 && rm -rf out_${mode}_$seed && NCZ_BLACKHOLE_COLORS=$mode NCZ_BLACKHOLE_SEED=$seed NCZ_FRAME_DUMP=out_${mode}_$seed timeout --kill-after=2 12 ./blackhole_gles3 > out_${mode}_$seed.log 2>&1; echo rc=\$?; ls out_${mode}_$seed | head -20; grep -ci 'error' out_${mode}_$seed.log"
 done
done
mkdir -p shots
sshpass -e scp -q -r pegasus@192.168.207.85:bhtest/out_* shots/ 
ls shots
