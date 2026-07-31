#!/usr/bin/env bash
# Run qemu-ppc-static from the local toolchain root. Hosts that cannot
# execute Linux binaries (macOS) run it inside a Linux container with the
# repository bind-mounted at the same absolute path, so guest binaries,
# sysroot, and data paths stay host-valid. The image matches the host
# architecture so qemu itself runs natively and only the PPC guest is
# emulated; qemu's PPC FP is softfloat, so results are host-independent.
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
source "$repo_root/tools/build/host_arch.sh"
qemu="$repo_root/build/melee_core/toolchain/root/usr/bin/qemu-ppc-static"

if [[ "$(uname -s)" != "Darwin" ]]; then
    exec "$qemu" "$@"
fi

exec docker run --rm --platform "$(msl_host_platform)" \
    -v "$repo_root:$repo_root" -w "$PWD" \
    ubuntu:24.04 "$qemu" "$@"
