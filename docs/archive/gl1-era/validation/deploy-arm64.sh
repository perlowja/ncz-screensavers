#!/bin/bash
# Deploy arm64 _gles3 binaries + scripts to O6N.
set -e
target="mini@192.168.207.3"
password="mini"

export SSHPASS="$password"
SSH="sshpass -e ssh -o StrictHostKeyChecking=no -o PubkeyAuthentication=no -o UserKnownHostsFile=/dev/null"
SCP="sshpass -e scp -o StrictHostKeyChecking=no -o PubkeyAuthentication=no -o UserKnownHostsFile=/dev/null"

echo "Setting up remote dir on $target..."
$SSH $target 'mkdir -p ~/gles3-validation/bin ~/gles3-validation/shots ~/gles3-validation/logs && rm -f ~/gles3-validation/bin/* ~/gles3-validation/shots/* ~/gles3-validation/logs/*'

echo "Deploying 90 _gles3 binaries..."
for bin in $(ls build/ | grep -v '\.p$' | grep _gles3$); do
    $SCP build/$bin $target:~/gles3-validation/bin/
done

echo "Deploying run-all-gles3.sh..."
$SCP validation/run-all-gles3.sh $target:~/gles3-validation/run-all-gles3.sh

echo "Done."
