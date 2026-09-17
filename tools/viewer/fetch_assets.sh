#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MANIFEST="$ROOT/tools/viewer/assets/character_zips.tsv"
CACHE_DIR="${MSL_VIEWER_ASSET_CACHE:-$ROOT/build/cache/viewer-zips}"
LOCAL_ZIPS_DIR="$ROOT/refs/slippilab/public/zips"

if ! command -v curl >/dev/null 2>&1; then
  echo "error: curl is required to fetch viewer assets" >&2
  exit 1
fi
if ! command -v sha256sum >/dev/null 2>&1; then
  echo "error: sha256sum is required to verify viewer assets" >&2
  exit 1
fi

mkdir -p "$CACHE_DIR"

valid_cached_asset() {
  local path="$1"
  local expected_sha="$2"
  [[ -f "$path" ]] && printf '%s  %s\n' "$expected_sha" "$path" | sha256sum -c --status -
}

while IFS=$'\t' read -r filename expected_sha url; do
  [[ -z "${filename:-}" || "${filename:0:1}" == "#" ]] && continue
  if [[ -z "${expected_sha:-}" || -z "${url:-}" ]]; then
    echo "error: malformed viewer asset manifest row: $filename" >&2
    exit 1
  fi

  out="$CACHE_DIR/$filename"
  if valid_cached_asset "$out" "$expected_sha"; then
    echo "viewer asset cached: $filename"
    continue
  fi

  # Repository-baked silhouettes (tools/viewer/bake) live in-tree and are
  # copied straight from the manifest's baked: path.
  if [[ "$url" == baked:* ]]; then
    baked_src="$ROOT/${url#baked:}"
    if ! valid_cached_asset "$baked_src" "$expected_sha"; then
      echo "error: baked viewer asset missing or checksum mismatch: $baked_src" >&2
      exit 1
    fi
    echo "viewer asset from repository bake: $filename"
    cp "$baked_src" "$out.tmp.$$"
    mv "$out.tmp.$$" "$out"
    continue
  fi

  local_src="$LOCAL_ZIPS_DIR/$filename"
  if valid_cached_asset "$local_src" "$expected_sha"; then
    echo "viewer asset from refs/slippilab checkout: $filename"
    cp "$local_src" "$out.tmp.$$"
    mv "$out.tmp.$$" "$out"
    continue
  fi

  tmp="$out.tmp.$$"
  rm -f "$tmp"
  echo "viewer asset download: $filename"
  if ! curl -fsSL --retry 3 --retry-delay 1 --connect-timeout 10 "$url" -o "$tmp"; then
    rm -f "$tmp"
    echo "error: failed to download $url" >&2
    exit 1
  fi
  if ! printf '%s  %s\n' "$expected_sha" "$tmp" | sha256sum -c --status -; then
    rm -f "$tmp"
    echo "error: checksum mismatch for $filename" >&2
    exit 1
  fi
  mv "$tmp" "$out"
done <"$MANIFEST"

echo "viewer assets ready: $CACHE_DIR"
