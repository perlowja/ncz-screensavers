#!/bin/bash
# build_patched_launcher_fixture.sh — rebuild tests/fixtures/patched_launcher.py
# from the upstream /usr/bin/ncz-screensaver + our patch.
#
# This is run by CI / the operator on demand; the resulting fixture is
# committed to the repo (see tests/fixtures/patched_launcher.py).
#
# Steps:
#   1. Copy /usr/bin/ncz-screensaver to tests/fixtures/patched_launcher.py
#   2. Apply tools/gpu-classifier/patches/launcher-gpu-class.patch
#   3. Verify the result parses as valid Python
#
# The launcher source is taken from the live host 192.168.207.66 over
# ssh (the "mini" login). Run from tools/gpu-classifier/:

set -euo pipefail

HERE="$(cd "$(dirname "$0")/../.." && pwd)"
FIXTURE_DIR="$HERE/tests/fixtures"
FIXTURE="$FIXTURE_DIR/patched_launcher.py"
PATCH="$HERE/patches/launcher-gpu-class.patch"
ORIG="/tmp/ncz-screensaver-orig.py"

mkdir -p "$FIXTURE_DIR"

# Pull the current upstream launcher over ssh. The patch is generated
# against /usr/bin/ncz-screensaver from ncz-screensavers 0.7.1; if the
# installed version drifts the patch may not apply — fix the patch in
# that case.
if ! command -v sshpass >/dev/null 2>&1; then
    echo "ERROR: sshpass not installed" >&2
    exit 1
fi

if [ -n "${V66_HOST:-}" ]; then
    HOST="$V66_HOST"
else
    HOST="192.168.207.66"
fi
if [ -n "${V66_PASS:-}" ]; then
    PASSWORD="$V66_PASS"
else
    PASSWORD="mini"
fi

echo "==> fetching /usr/bin/ncz-screensaver from $HOST"
SSHPASS="$PASSWORD" sshpass -e scp -o StrictHostKeyChecking=no \
    "mini@$HOST:/usr/bin/ncz-screensaver" "$ORIG"
md5sum "$ORIG"

# Apply the patch
echo "==> applying $PATCH"
cp "$ORIG" "$FIXTURE"
# The patch header references /tmp/ paths because we generated it
# from /tmp; rewrite to /usr/bin/ncz-screensaver for portability.
sed -e "s|/tmp/ncz-screensaver-orig.py|a/usr/bin/ncz-screensaver|" \
    -e "s|/tmp/fixed-ncz-screensaver.py|b/usr/bin/ncz-screensaver|" \
    "$PATCH" > "$FIXTURE.patch.tmp"
( cd "$FIXTURE_DIR" && patch -p1 -i "$FIXTURE.patch.tmp" )
rm -f "$FIXTURE.patch.tmp"

# Verify the result parses
echo "==> verifying the patched launcher parses"
python3 -c "import ast; ast.parse(open('$FIXTURE').read()); print('OK')"

# Verify the new symbols are present
echo "==> verifying the patch added the expected symbols"
for sym in classify_from_calibrator_result augment_calibrator_env discover_gpus _all_known_gpus; do
    if grep -q "$sym" "$FIXTURE"; then
        echo "  $sym: present"
    else
        echo "  $sym: MISSING (patch may not have applied)"
        exit 1
    fi
done

echo "==> done. Fixture at $FIXTURE ($(wc -l < "$FIXTURE") lines)"
