#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
core="$root/src/melee_core"
vendor="$core/vendor"
files="$core/upstream-files.txt"
lock="$core/upstream.lock"
patches="$core/patches"

lock_value() {
    awk -F '\t' -v key="$1" '$1 == key { print $2; found = 1 } END { if (!found) exit 1 }' "$lock"
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

verify_snapshot() {
    local tree=$1
    local expected_count expected_digest actual_count actual_digest
    local actual_files
    actual_files=$(mktemp)

    LC_ALL=C find "$tree" -type f -printf '%P\n' | sort > "$actual_files"
    if ! diff -u "$files" "$actual_files"; then
        echo "upstream snapshot file set differs from upstream-files.txt" >&2
        rm -f "$actual_files"
        return 1
    fi

    expected_count=$(lock_value file_count)
    actual_count=$(wc -l < "$actual_files")
    if [[ "$actual_count" != "$expected_count" ]]; then
        echo "upstream snapshot has $actual_count files; expected $expected_count" >&2
        rm -f "$actual_files"
        return 1
    fi

    expected_digest=$(lock_value tree_sha256)
    actual_digest=$(tree_digest "$tree")
    if [[ "$actual_digest" != "$expected_digest" ]]; then
        echo "upstream snapshot digest $actual_digest; expected $expected_digest" >&2
        rm -f "$actual_files"
        return 1
    fi
    rm -f "$actual_files"
}

apply_series() {
    local tree=$1
    local name
    while IFS= read -r name; do
        [[ -n "$name" ]] || continue
        if ! patch --batch --forward --silent -d "$tree" -p1 \
            < "$patches/$name"; then
            echo "upstream patch conflict: $name" >&2
            return 1
        fi
    done < "$patches/series"
}

copy_tree() {
    local source=$1
    local destination=$2
    mkdir -p "$destination"
    (cd "$source" && tar -cf - .) | (cd "$destination" && tar -xf -)
}

archive_snapshot() {
    local checkout=$1
    local commit=$2
    local destination=$3
    local resolved
    local -a paths

    resolved=$(git -C "$checkout" rev-parse "$commit^{commit}")
    mapfile -t paths < "$files"
    mkdir -p "$destination"
    if ! git -C "$checkout" archive "$resolved" -- "${paths[@]}" \
        | tar -x -C "$destination"; then
        echo "could not import the locked source set from $resolved" >&2
        return 1
    fi
    printf '%s\n' "$resolved"
}

replace_vendor() {
    local replacement=$1
    local backup="$core/.vendor.backup.$$"
    mv "$vendor" "$backup"
    if ! mv "$replacement" "$vendor"; then
        mv "$backup" "$vendor"
        return 1
    fi
    rm -rf "$backup"
}

check_all() {
    local scratch
    verify_snapshot "$vendor"
    scratch=$(mktemp -d "${TMPDIR:-/tmp}/msl-core-source.XXXXXX")
    copy_tree "$vendor" "$scratch"
    if ! apply_series "$scratch"; then
        rm -rf "$scratch"
        return 1
    fi
    rm -rf "$scratch"
    echo "source snapshot and patch series verified"
}

materialize() {
    local output=${1:-"$root/build/melee_core/source"}
    local parent scratch
    verify_snapshot "$vendor"
    parent=$(dirname "$output")
    mkdir -p "$parent"
    scratch=$(mktemp -d "$parent/.source.XXXXXX")
    copy_tree "$vendor" "$scratch"
    if ! apply_series "$scratch"; then
        rm -rf "$scratch"
        return 1
    fi
    touch "$scratch/.materialized"
    rm -rf "$output"
    mv "$scratch" "$output"
}

import_locked() {
    local checkout=${1:?usage: source_sync.sh import CHECKOUT}
    local expected actual scratch
    expected=$(lock_value commit)
    scratch=$(mktemp -d "${TMPDIR:-/tmp}/msl-core-import.XXXXXX")
    actual=$(archive_snapshot "$checkout" "$expected" "$scratch")
    if [[ "$actual" != "$expected" ]]; then
        echo "resolved import commit $actual; expected $expected" >&2
        rm -rf "$scratch"
        return 1
    fi
    if ! verify_snapshot "$scratch"; then
        rm -rf "$scratch"
        return 1
    fi
    replace_vendor "$scratch"
    echo "imported pristine upstream snapshot at $expected"
}

update_pin() {
    local checkout=${1:?usage: source_sync.sh update CHECKOUT COMMIT}
    local requested=${2:?usage: source_sync.sh update CHECKOUT COMMIT}
    local pristine patched resolved digest count repository new_lock
    pristine=$(mktemp -d "${TMPDIR:-/tmp}/msl-core-update.XXXXXX")
    patched=$(mktemp -d "${TMPDIR:-/tmp}/msl-core-patched.XXXXXX")
    new_lock=$(mktemp)

    resolved=$(archive_snapshot "$checkout" "$requested" "$pristine")
    copy_tree "$pristine" "$patched"
    if ! apply_series "$patched"; then
        rm -rf "$pristine" "$patched"
        rm -f "$new_lock"
        return 1
    fi

    digest=$(tree_digest "$pristine")
    count=$(find "$pristine" -type f | wc -l)
    repository=$(git -C "$checkout" remote get-url origin 2>/dev/null \
        || lock_value repository)
    printf 'repository\t%s\ncommit\t%s\nfile_count\t%s\ntree_sha256\t%s\n' \
        "$repository" "$resolved" "$count" "$digest" > "$new_lock"

    replace_vendor "$pristine"
    mv "$new_lock" "$lock"
    rm -rf "$patched"
    echo "updated pristine upstream snapshot to $resolved"
}

case ${1:-} in
    check)
        check_all
        ;;
    materialize)
        materialize "${2:-}"
        ;;
    import)
        import_locked "${2:-}"
        ;;
    update)
        update_pin "${2:-}" "${3:-}"
        ;;
    *)
        echo "usage: $0 {check|materialize [OUTPUT]|import CHECKOUT|update CHECKOUT COMMIT}" >&2
        exit 2
        ;;
esac
