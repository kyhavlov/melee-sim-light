#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
source "$repo_root/tools/build/host_arch.sh"
toolchain_dir="$repo_root/build/melee_core/toolchain"
sysroot="$toolchain_dir/root"
stamp="$toolchain_dir/ready.stamp"
manifest="$repo_root/tools/build/ppc32_toolchain_packages.tsv"

# The reference toolchain is pinned as data: the manifest names the exact
# .deb bytes (version + sha256, immutable Launchpad URLs) for every host,
# so populate results never depend on what the local apt archive currently
# carries. The stamp records the signature of the populate that built the
# tree — the manifest digest plus the host deb architecture — and any
# drift (manifest edit, arch switch, older sentinel-era tree) forces a
# wipe and repopulate rather than silently reusing a tree assembled from
# a different package set.
deb_arch=$(msl_host_deb_arch)
signature="$deb_arch $(sha256sum "$manifest" | cut -d' ' -f1)"

if [[ -f "$stamp" && "$(cat "$stamp" 2>/dev/null)" == "$signature" &&
      -x "$sysroot/usr/bin/powerpc-linux-gnu-gcc-13" &&
      -x "$sysroot/usr/bin/qemu-ppc-static" ]]; then
    exit 0
fi

# macOS cannot run the Linux toolchain binaries. Populate the same
# toolchain tree from inside a Linux container; the compiler is then
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
        bash -c "apt-get update -qq >/dev/null && \
            apt-get install -qq -y --no-install-recommends \
                ca-certificates curl >/dev/null && exec \"$0\""
fi

for command in curl dpkg-deb sha256sum; do
    if ! command -v "$command" >/dev/null 2>&1; then
        echo "PPC reference toolchain setup requires $command" >&2
        exit 1
    fi
done

package_dir="$toolchain_dir/packages/$deb_arch"
rm -rf "$sysroot" "$package_dir" "$stamp"
mkdir -p "$package_dir" "$sysroot"

# The debs are downloaded and unpacked locally; nothing is installed on
# the host.
while IFS=$'\t' read -r filename arch sha256 url; do
    [[ -z "$filename" || "$filename" == \#* ]] && continue
    [[ "$arch" == "all" || "$arch" == "$deb_arch" ]] || continue
    package="$package_dir/$filename"
    curl -fsSL --retry 3 -o "$package" "$url" < /dev/null
    echo "$sha256  $package" | sha256sum --check --quiet
    dpkg-deb -x "$package" "$sysroot"
done < "$manifest"

if [[ ! -x "$sysroot/usr/bin/powerpc-linux-gnu-gcc-13" ||
      ! -x "$sysroot/usr/bin/qemu-ppc-static" ]]; then
    echo "PPC reference toolchain extraction was incomplete" >&2
    exit 1
fi

printf '%s\n' "$signature" > "$stamp"
