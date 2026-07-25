#!/usr/bin/env bash
# Run qemu-ppc-static from the local toolchain root. Hosts that cannot
# execute Linux binaries (macOS) run it inside a Linux container with the
# repository bind-mounted at the same absolute path, so guest binaries,
# sysroot, and data paths stay host-valid.
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
qemu="$repo_root/build/melee_core/toolchain/root/usr/bin/qemu-ppc-static"

if [[ "$(uname -s)" != "Darwin" ]]; then
    exec "$qemu" "$@"
fi

exec docker run --rm --platform linux/amd64 \
    -v "$repo_root:$repo_root" -w "$PWD" \
    ubuntu:24.04 "$qemu" "$@"
