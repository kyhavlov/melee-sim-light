#!/usr/bin/env bash
# Host-architecture facts shared by the PPC toolchain wrappers and the
# Makefile. The containerized (macOS) paths run a Linux image matching the
# host architecture: qemu-ppc and the cross compiler are host-arch binaries,
# so an amd64 image on Apple Silicon would emulate them under Rosetta on top
# of qemu's own PPC emulation. Only the PPC guest is emulated this way.
#
# Sourced by the wrapper scripts; also invoked as
#   host_arch.sh platform | host_arch.sh multiarch
# so the Makefile can query the same values.

msl_host_platform() {
    case "$(uname -m)" in
        arm64 | aarch64) echo "linux/arm64" ;;
        *) echo "linux/amd64" ;;
    esac
}

# Debian/Ubuntu multiarch directory for the host-arch shared libraries the
# cross compiler itself links against ($TOOLCHAIN_ROOT/usr/lib/<multiarch>).
msl_host_multiarch() {
    case "$(uname -m)" in
        arm64 | aarch64) echo "aarch64-linux-gnu" ;;
        *) echo "x86_64-linux-gnu" ;;
    esac
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    case "${1:-}" in
        platform) msl_host_platform ;;
        multiarch) msl_host_multiarch ;;
        *)
            echo "usage: $0 platform|multiarch" >&2
            exit 2
            ;;
    esac
fi
