#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
toolchain_dir="$repo_root/build/melee_core/toolchain"
package_dir="$toolchain_dir/packages"
sysroot="$toolchain_dir/root"
stamp="$toolchain_dir/ready.stamp"

if [[ -x "$sysroot/usr/bin/powerpc-linux-gnu-gcc-13" &&
      -x "$sysroot/usr/bin/qemu-ppc-static" ]]; then
    touch "$stamp"
    exit 0
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
