#!/bin/bash
# Deploy _gles3 binaries + a runner script to a remote host.
# Usage: deploy-to-remote.sh <user@host> <password>
set -e
target="$1"
password="$2"
ssh_cmd="sshpass -p \"$password\" ssh -o StrictHostKeyChecking=no -o PubkeyAuthentication=no -o UserKnownHostsFile=/dev/null"
scp_cmd="sshpass -p \"$password\" scp -o StrictHostKeyChecking=no -o PubkeyAuthentication=no -o UserKnownHostsFile=/dev/null"

# Make remote work dir
$ssh_cmd $target 'mkdir -p ~/gles3-validation/bin ~/gles3-validation/screenshots ~/gles3-validation/logs'

# rsync the 90 binaries
echo "Deploying binaries to $target..."
for bin in $(cat validation/o6n/binaries.list); do
    $scp_cmd build/$bin $target:~/gles3-validation/bin/
done

echo "Done."
