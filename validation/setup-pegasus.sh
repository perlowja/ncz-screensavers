#!/bin/bash
# setup-pegasus.sh — push source + scripts to PEGASUS, build 90 _gles3
# binaries natively.
set -e
TARGET="pegasus@192.168.207.85"
PASSWORD="pegasus"
SRC=/home/jasonperlow/Projects/ncz-screensavers

export SSHPASS="$PASSWORD"
SSH_RSH="sshpass -e ssh -o StrictHostKeyChecking=no -o PubkeyAuthentication=no -o UserKnownHostsFile=/dev/null"
SSH="sshpass -e ssh -o StrictHostKeyChecking=no -o PubkeyAuthentication=no -o UserKnownHostsFile=/dev/null"
SCP="sshpass -e scp -o StrictHostKeyChecking=no -o PubkeyAuthentication=no -o UserKnownHostsFile=/dev/null"

echo "Setting up remote dir on $TARGET..."
$SSH $TARGET 'mkdir -p ~/gles3-validation/bin ~/gles3-validation/shots ~/gles3-validation/logs ~/gles3-validation/src'

echo "Installing missing build deps on PEGASUS..."
$SSH $TARGET 'echo "pegasus" | sudo -S apt-get install -y --no-install-recommends pkg-config libpng-dev libglu1-mesa-dev 2>&1 | tail -3'

echo "Pushing source tree to PEGASUS..."
rsync -avz --delete -e "$SSH_RSH" \
    --exclude='.git' --exclude='build' \
    --exclude='validation/o6n/raw' \
    --exclude='validation/medusa/raw' \
    --exclude='validation/pegasus/raw' \
    $SRC/ $TARGET:~/gles3-validation/src/ 2>&1 | tail -3

echo "Deploying run-all-gles3.sh + classify_results.py..."
$SCP validation/run-all-gles3.sh validation/classify_results.py $TARGET:~/gles3-validation/

echo "Done."
