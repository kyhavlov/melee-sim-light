#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
core="$root/src/melee_core"
canonical="$core/gameplay"
files="$core/upstream-files.txt"
lock="$core/upstream.lock"
default_checkout="$root/refs/melee"

lock_value() {
    awk -F '\t' -v key="$1" \
        '$1 == key { print $2; found = 1 } END { if (!found) exit 1 }' "$lock"
}

tree_digest() {
    local tree=$1
    (
        cd "$tree"
        LC_ALL=C find . -type f -print0 \
            | sort -z \
            | xargs -0 sha256sum \
            | sha256sum \
            | awk '{ print $1 }'
    )
}

copy_tree() {
    local source=$1
    local destination=$2
    mkdir -p "$destination"
    (cd "$source" && tar -cf - .) | (cd "$destination" && tar -xf -)
}

archive_snapshot() {
    local checkout=$1
    local destination=$2
    local commit
    local resolved
    local -a paths

    commit=$(lock_value commit)
    resolved=$(git -C "$checkout" rev-parse "$commit^{commit}")
    if [[ "$resolved" != "$commit" ]]; then
        echo "resolved source commit $resolved; expected $commit" >&2
        return 1
    fi
    mapfile -t paths < "$files"
    mkdir -p "$destination"
    git -C "$checkout" archive "$commit" -- "${paths[@]}" | tar -x -C "$destination"
}

add_paths() {
    local checkout=${1:?usage: source_sync.sh add CHECKOUT PATH...}
    shift
    local commit repository scratch new_files new_lock upstream
    local path
    local -a paths=("$@")
    if (( ${#paths[@]} == 0 )); then
        echo "usage: source_sync.sh add CHECKOUT PATH..." >&2
        return 2
    fi
    commit=$(lock_value commit)
    git -C "$checkout" cat-file -e "$commit^{commit}"
    for path in "${paths[@]}"; do
        if [[ "$path" = /* || "$path" = .. || "$path" = ../* || "$path" = */../* ]]; then
            echo "source path must be repository-relative: $path" >&2
            return 2
        fi
        if [[ -e "$canonical/$path" ]]; then
            echo "canonical source path already exists: $path" >&2
            return 1
        fi
    done

    scratch=$(mktemp -d "${TMPDIR:-/tmp}/msl-core-add.XXXXXX")
    git -C "$checkout" archive "$commit" -- "${paths[@]}" | tar -x -C "$scratch"
    copy_tree "$scratch" "$canonical"

    new_files=$(mktemp "$core/.upstream-files.XXXXXX")
    LC_ALL=C find "$canonical" -type f -printf '%P\n' | sort > "$new_files"
    mv "$new_files" "$files"

    upstream=$(mktemp -d "${TMPDIR:-/tmp}/msl-core-upstream.XXXXXX")
    archive_snapshot "$checkout" "$upstream"
    repository=$(lock_value repository)
    new_lock=$(mktemp "$core/.upstream-lock.XXXXXX")
    printf 'repository\t%s\ncommit\t%s\nfile_count\t%s\ntree_sha256\t%s\n' \
        "$repository" "$commit" "$(find "$upstream" -type f | wc -l)" \
        "$(tree_digest "$upstream")" > "$new_lock"
    mv "$new_lock" "$lock"
    rm -rf "$scratch" "$upstream"
    echo "imported ${#paths[@]} canonical source path(s) from $commit"
}

verify_inventory() {
    local tree=$1
    local label=$2
    local actual
    actual=$(mktemp)
    LC_ALL=C find "$tree" -type f -printf '%P\n' | sort > "$actual"
    if ! diff -u "$files" "$actual"; then
        echo "$label file set differs from upstream-files.txt" >&2
        rm -f "$actual"
        return 1
    fi
    rm -f "$actual"
}

verify_upstream() {
    local tree=$1
    local expected_count expected_digest actual_count actual_digest
    verify_inventory "$tree" "locked upstream snapshot"
    expected_count=$(lock_value file_count)
    actual_count=$(find "$tree" -type f | wc -l)
    if [[ "$actual_count" != "$expected_count" ]]; then
        echo "locked upstream snapshot has $actual_count files; expected $expected_count" >&2
        return 1
    fi
    expected_digest=$(lock_value tree_sha256)
    actual_digest=$(tree_digest "$tree")
    if [[ "$actual_digest" != "$expected_digest" ]]; then
        echo "locked upstream digest $actual_digest; expected $expected_digest" >&2
        return 1
    fi
}

prepare_comparison() {
    local checkout=$1
    local tree=$2
    archive_snapshot "$checkout" "$tree"
    verify_upstream "$tree"
    git -C "$tree" init -q
    git -C "$tree" add -A
    git -C "$tree" \
        -c user.name=melee-core-source-sync \
        -c user.email=source-sync.invalid \
        commit -q --no-gpg-sign -m upstream
    copy_tree "$canonical" "$tree"
    git -C "$tree" add -A
}

check_all() {
    local checkout=${1:-$default_checkout}
    local scratch changed
    verify_inventory "$canonical" "canonical gameplay source"
    if ! git -C "$checkout" cat-file -e "$(lock_value commit)^{commit}" 2>/dev/null; then
        echo "canonical source inventory verified; locked checkout unavailable at $checkout"
        return
    fi
    scratch=$(mktemp -d "${TMPDIR:-/tmp}/msl-core-check.XXXXXX")
    prepare_comparison "$checkout" "$scratch"
    changed=$(git -C "$scratch" diff --cached --name-only | wc -l)
    rm -rf "$scratch"
    echo "canonical source and locked upstream verified; $changed files carry local deltas"
}

diff_all() {
    local checkout=${1:-$default_checkout}
    local scratch
    scratch=$(mktemp -d "${TMPDIR:-/tmp}/msl-core-diff.XXXXXX")
    prepare_comparison "$checkout" "$scratch"
    git -C "$scratch" diff --cached --binary --full-index --no-renames
    rm -rf "$scratch"
}

case ${1:-} in
    check)
        check_all "${2:-}"
        ;;
    diff)
        diff_all "${2:-}"
        ;;
    add)
        shift
        add_paths "$@"
        ;;
    *)
        echo "usage: $0 {check [CHECKOUT]|diff [CHECKOUT]|add CHECKOUT PATH...}" >&2
        exit 2
        ;;
esac
