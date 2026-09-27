#!/bin/bash
# Build ncz-screensavers natively on PEGASUS (x86_64, hybrid Intel+NVIDIA)
# for appearance validation and the per-shader cost sweep.
set -uo pipefail
cd "$HOME" || exit 1
if [ -d ncz-screensavers/.git ]; then
  cd ncz-screensavers && git fetch --quiet origin master 2>/dev/null && git reset --hard origin/master --quiet 2>/dev/null
else
  git clone --quiet https://gitlab.com/ncz-os/ncz-screensavers.git ncz-screensavers || exit 1
  cd ncz-screensavers
fi
echo "head: $(git log -1 --oneline | cut -c1-60)"
meson setup build -Dgl4es=disabled -Dxscreensaver-shim=disabled >/tmp/pegasus-ms.log 2>&1 || {
  echo "SETUP_FAIL"; grep -iE "error|not found|missing" /tmp/pegasus-ms.log | tail -15; exit 2; }
echo "configure OK"
ninja -C build 2>&1 | tail -4
echo "NINJA_EXIT=${PIPESTATUS[0]}"
echo "built binaries: $(ls build/*_gles3 2>/dev/null | wc -l)"
echo "PEGASUS_BUILD_DONE"
