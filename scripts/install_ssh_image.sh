#!/usr/bin/env bash
# Install the SSH worker and operator-provided keys on a separate application disk.
set -euo pipefail
image=$1
hostkeys=$2
authorized=$3
tinyssh_license=$4
mbedtls_license=$5
lwip_license=$6
shift 6

if [[ -z $hostkeys || -z $authorized || ! -f $hostkeys/ed25519.pk ||
      ! -f $hostkeys/.ed25519.sk || ! -f $authorized ]]; then
    echo "Set SSH_HOSTKEY_DIR to a TinySSH key directory and SSH_AUTHORIZED_KEY to an OpenSSH Ed25519 .pub file." >&2
    exit 1
fi

umask 077
stage=$(mktemp -d "$(dirname "$image")/.ssh-image.XXXXXXXX")
trap 'rm -rf -- "$stage"' EXIT
python3 - "$hostkeys" "$authorized" "$stage/login.pub" <<'PY'
import base64
import binascii
import pathlib
import sys

hostkeys, authorized, output = map(pathlib.Path, sys.argv[1:])
public = (hostkeys / 'ed25519.pk').read_bytes()
secret = (hostkeys / '.ed25519.sk').read_bytes()
if len(public) != 32 or len(secret) != 64 or secret[32:] != public:
    sys.exit('Invalid TinySSH Ed25519 host key pair.')

lines = authorized.read_bytes().strip().splitlines()
header = b'\x00\x00\x00\x0bssh-ed25519\x00\x00\x00\x20'
try:
    if len(lines) != 1:
        raise ValueError()
    fields = lines[0].split(None, 2)
    if len(fields) < 2 or fields[0] != b'ssh-ed25519':
        raise ValueError()
    blob = base64.b64decode(fields[1], validate=True)
    if len(blob) != len(header) + 32 or not blob.startswith(header):
        raise ValueError()
except (ValueError, binascii.Error):
    sys.exit('SSH_AUTHORIZED_KEY must be one OpenSSH Ed25519 public key.')
output.write_bytes(base64.b64encode(blob))
PY

bash "$(dirname "$0")/install_userspace_image.sh" apps "$stage/ssh.img" "$@"
mmd -i "$stage/ssh.img" ::/ssh
mcopy -i "$stage/ssh.img" "$hostkeys/ed25519.pk" ::/ssh/HOST.PK
mcopy -i "$stage/ssh.img" "$hostkeys/.ed25519.sk" ::/ssh/HOST.SK
mcopy -i "$stage/ssh.img" "$stage/login.pub" ::/ssh/LOGIN.PUB
mcopy -i "$stage/ssh.img" "$tinyssh_license" ::/TINYSSH.LIC
mcopy -i "$stage/ssh.img" "$mbedtls_license" ::/MBEDTLS.LIC
mcopy -i "$stage/ssh.img" "$lwip_license" ::/LWIP.LIC
mv -f "$stage/ssh.img" "$image"
