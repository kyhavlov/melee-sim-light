#!/usr/bin/env bash
# Run the pinned powerpc-linux-gnu cross compiler from the local toolchain
# root. Hosts that cannot execute the Linux toolchain binaries (macOS) run
# the identical compiler inside a Linux container with the repository
# bind-mounted at the same absolute path, so dependency files and DWARF
# paths stay host-valid.
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
source "$repo_root/tools/build/host_arch.sh"
sysroot="$repo_root/build/melee_core/toolchain/root"
cross_cc="$sysroot/usr/bin/powerpc-linux-gnu-gcc-13"
libdir="$sysroot/usr/lib/$(msl_host_multiarch)"

if [[ "$(uname -s)" != "Darwin" ]]; then
    exec env PATH="$sysroot/usr/bin:$PATH" \
        LD_LIBRARY_PATH="$libdir:${LD_LIBRARY_PATH:-}" \
        "$cross_cc" "$@"
fi

exec docker run --rm --platform "$(msl_host_platform)" \
    -v "$repo_root:$repo_root" -w "$PWD" \
    -e PATH="$sysroot/usr/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin" \
    -e LD_LIBRARY_PATH="$libdir" \
    ubuntu:24.04 "$cross_cc" "$@"
