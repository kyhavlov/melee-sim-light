#!/usr/bin/env bash
# timeout(1) replacement for hosts without coreutils timeout (macOS).
set -euo pipefail

if command -v timeout >/dev/null 2>&1; then
    exec timeout "$@"
fi

duration=${1%s}
shift
exec perl -e 'alarm shift @ARGV; exec @ARGV or die "exec failed: $!\n"' \
    "$duration" "$@"
