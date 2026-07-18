#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
source_root="$root/src"
files="$source_root/upstream-files.txt"
lock="$source_root/upstream.lock"
default_checkout="$root/refs/melee"

lock_value() {
    awk -F '\t' -v key="$1" '$1 == key { print $2; found = 1 } END { if (!found) exit 1 }' "$lock"
}

local_path() {
    case "$1" in
        src/*) printf '%s/%s\n' "$source_root" "${1#src/}" ;;
        *) printf '%s/%s\n' "$source_root" "$1" ;;
    esac
}

tree_digest() {
    local tree=$1
    (
        cd "$tree"
        LC_ALL=C find . -type f -print0 | sort -z | xargs -0 sha256sum |
            sha256sum | awk '{ print $1 }'
    )
}

archive_snapshot() {
    local checkout=$1
    local destination=$2
    local commit
    local resolved
    local -a paths
    commit=$(lock_value commit)
    resolved=$(git -C "$checkout" rev-parse "$commit^{commit}")
    [[ "$resolved" == "$commit" ]] || {
        echo "resolved source commit $resolved; expected $commit" >&2
        return 1
    }
    mapfile -t paths < "$files"
    mkdir -p "$destination"
    git -C "$checkout" archive "$commit" -- "${paths[@]}" | tar -x -C "$destination"
}

verify_local_inventory() {
    local expected actual path
    expected=$(mktemp)
    actual=$(mktemp)
    while IFS= read -r path; do
        case "$path" in
            src/*) printf '%s\n' "${path#src/}" ;;
            *) printf '%s\n' "$path" ;;
        esac
    done < "$files" | LC_ALL=C sort > "$expected"
    (
        cd "$source_root"
        find MSL MetroTRK Runtime melee sysdolphin extern -type f -printf '%p\n'
        find . -maxdepth 1 -type f ! -name '.clang-format' ! -name 'api.c' ! -name 'api.h' \
            ! -name 'python_api.c' ! -name 'python_api.h' ! -name 'README.md' \
            ! -name 'source_manifest.tsv' ! -name 'upstream-files.txt' \
            ! -name 'upstream.lock' ! -name 'upstream_delta_ledger.tsv' -printf '%P\n'
    ) | LC_ALL=C sort > "$actual"
    if ! diff -u "$expected" "$actual"; then
        echo "canonical source inventory differs from src/upstream-files.txt" >&2
        rm -f "$expected" "$actual"
        return 1
    fi
    rm -f "$expected" "$actual"
}

verify_upstream() {
    local tree=$1
    local count digest
    count=$(find "$tree" -type f | wc -l)
    digest=$(tree_digest "$tree")
    [[ "$count" == "$(lock_value file_count)" ]] || {
        echo "locked source snapshot has $count files; expected $(lock_value file_count)" >&2
        return 1
    }
    [[ "$digest" == "$(lock_value tree_sha256)" ]] || {
        echo "locked source snapshot digest differs from src/upstream.lock" >&2
        return 1
    }
}

overlay_local() {
    local destination=$1
    local upstream local
    while IFS= read -r upstream; do
        local=$(local_path "$upstream")
        mkdir -p "$destination/$(dirname "$upstream")"
        cp "$local" "$destination/$upstream"
    done < "$files"
}

prepare_comparison() {
    local checkout=$1
    local tree=$2
    archive_snapshot "$checkout" "$tree"
    verify_upstream "$tree"
    git -C "$tree" init -q
    git -C "$tree" add -A
    git -C "$tree" -c user.name=source-sync -c user.email=source-sync.invalid \
        commit -q --no-gpg-sign -m upstream
    overlay_local "$tree"
    git -C "$tree" add -A
}

check_all() {
    local checkout=${1:-$default_checkout}
    local scratch changed
    verify_local_inventory
    if ! git -C "$checkout" cat-file -e "$(lock_value commit)^{commit}" 2>/dev/null; then
        echo "canonical source inventory verified; locked checkout unavailable at $checkout"
        return
    fi
    scratch=$(mktemp -d "${TMPDIR:-/tmp}/msl-source-check.XXXXXX")
    prepare_comparison "$checkout" "$scratch"
    changed=$(git -C "$scratch" diff --cached --name-only | wc -l)
    rm -rf "$scratch"
    echo "canonical source and locked upstream verified; $changed files carry local deltas"
}

diff_all() {
    local checkout=${1:-$default_checkout}
    local scratch
    scratch=$(mktemp -d "${TMPDIR:-/tmp}/msl-source-diff.XXXXXX")
    prepare_comparison "$checkout" "$scratch"
    git -C "$scratch" diff --cached --binary --full-index --no-renames
    rm -rf "$scratch"
}

case ${1:-} in
    check) check_all "${2:-}" ;;
    diff) diff_all "${2:-}" ;;
    *)
        echo "usage: $0 {check [CHECKOUT]|diff [CHECKOUT]}" >&2
        exit 2
        ;;
esac
