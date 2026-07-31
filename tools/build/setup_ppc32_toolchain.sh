#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
source "$repo_root/tools/build/host_arch.sh"
toolchain_dir="$repo_root/build/melee_core/toolchain"
sysroot="$toolchain_dir/root"
stamp="$toolchain_dir/ready.stamp"

# The compiler and qemu binaries are host-arch; the multiarch library
# directory is the tell for which host a cached tree was populated for. A
# tree built for the other architecture must be repopulated, not reused.
# The package cache is per-arch because extraction unpacks every .deb it
# finds: a shared directory would let stale foreign-arch packages overwrite
# the binaries just downloaded.
multiarch=$(msl_host_multiarch)
package_dir="$toolchain_dir/packages/$multiarch"

if [[ -x "$sysroot/usr/bin/powerpc-linux-gnu-gcc-13" &&
      -x "$sysroot/usr/bin/qemu-ppc-static" &&
      -d "$sysroot/usr/lib/$multiarch" ]]; then
    touch "$stamp"
    exit 0
fi

# macOS cannot run apt-get or the Linux toolchain binaries. Populate the
# same toolchain tree from inside a Linux container; the compiler is then
# invoked through tools/build/ppc32_cc.sh, which containerizes each call.
if [[ "$(uname -s)" == "Darwin" && -z "${MSL_TOOLCHAIN_IN_CONTAINER:-}" ]]; then
    if ! command -v docker >/dev/null 2>&1; then
        echo "PPC reference toolchain setup on macOS requires docker" >&2
        exit 1
    fi
    exec docker run --rm --platform "$(msl_host_platform)" \
        -v "$repo_root:$repo_root" -w "$repo_root" \
        -e MSL_TOOLCHAIN_IN_CONTAINER=1 \
        ubuntu:24.04 \
        bash -c "apt-get update -qq >/dev/null && exec \"$0\""
fi

for command in apt-get dpkg-deb; do
    if ! command -v "$command" >/dev/null 2>&1; then
        echo "PPC reference toolchain setup requires $command" >&2
        exit 1
    fi
done

# These are downloaded and unpacked locally; nothing is installed on the host.
packages=(
    binutils-powerpc-linux-gnu
    cpp-13-powerpc-linux-gnu
    gcc-13-cross-base
    gcc-13-powerpc-linux-gnu
    gcc-13-powerpc-linux-gnu-base
    libasan8-powerpc-cross
    libatomic1-powerpc-cross
    libc6-dev-powerpc-cross
    libc6-powerpc-cross
    libgcc-13-dev-powerpc-cross
    libgcc-s1-powerpc-cross
    libgomp1-powerpc-cross
    libubsan1-powerpc-cross
    linux-libc-dev-powerpc-cross
    qemu-user-static
    # Host-side shared libraries the cross compiler itself links against.
    # Hosts with a native gcc already have them; the containerized macOS
    # path runs the toolchain in a bare image and needs them in-tree.
    binutils-common
    libbinutils
    libgmp10
    libisl23
    libmpc3
    libmpfr6
    libsframe1
    libzstd1
    zlib1g
)

mkdir -p "$package_dir" "$sysroot"
(
    cd "$package_dir"
    apt-get download "${packages[@]}"
)

while IFS= read -r -d '' package; do
    dpkg-deb -x "$package" "$sysroot"
done < <(find "$package_dir" -maxdepth 1 -type f -name '*.deb' -print0)

if [[ ! -x "$sysroot/usr/bin/powerpc-linux-gnu-gcc-13" ||
      ! -x "$sysroot/usr/bin/qemu-ppc-static" ]]; then
    echo "PPC reference toolchain extraction was incomplete" >&2
    exit 1
fi

touch "$stamp"
