#!/usr/bin/env bash
# Install the network service and programs on an application disk; init starts netd at boot.
set -euo pipefail
image=$1
lwip_license=$2
shift 2

bash "$(dirname "$0")/install_userspace_image.sh" apps "$image" "$@"
stage=$(mktemp -d "$(dirname "$image")/.network-image.XXXXXXXX")
trap 'rm -rf -- "$stage"' EXIT
printf '%s\n' '# Started by init at boot. netd takes its address from DHCP; give' \
    '# "-a ADDRESS/PREFIX -g GATEWAY -d DNS" for a fixed one.' \
    '/opt/bin/netd' > "$stage/services"
if ! mdir -i "$image" ::/etc >/dev/null 2>&1; then
    mmd -i "$image" ::/etc
fi
mcopy -o -i "$image" "$stage/services" ::/etc/services
mcopy -o -i "$image" "$lwip_license" ::/LWIP.LIC
