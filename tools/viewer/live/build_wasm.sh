#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT_DIR="$ROOT/tools/viewer/live/public"
DATA_ROOT="${MSL_DATA_DIR:-$ROOT/data}"
RAW_DIR="$DATA_ROOT/raw"

if [[ ! -d "$RAW_DIR" ]]; then
  echo "error: extracted raw game data not found: $RAW_DIR" >&2
  echo "Set MSL_DATA_DIR, or run python -m tools.data.extract --iso /path/to/SSBM.iso." >&2
  exit 1
fi
DATA_ROOT="$(cd "$DATA_ROOT" && pwd)"
RAW_DIR="$DATA_ROOT/raw"

if ! command -v emcc >/dev/null 2>&1; then
  echo "error: emcc is not on PATH. Install/activate Emscripten before building the live viewer." >&2
  exit 1
fi

jobs="${MSL_BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
if (( jobs > 16 )); then
  jobs=16
fi
make_jobs=()
if [[ "${MAKEFLAGS:-}" != *jobserver* ]]; then
  make_jobs=(-j"$jobs")
fi

# The canonical melee_core target owns compilation, exports, and data
# packaging. The ignored raw profile contains only the supported fighter,
# stage, effect, and common archives produced by the normal extraction
# contract; no game asset is copied into the repository source tree.
make --no-print-directory -f "$ROOT/Makefile" \
  viewer-schema wasm "${make_jobs[@]}" MSL_DATA_DIR="$DATA_ROOT" DATA="$RAW_DIR"

mkdir -p "$OUT_DIR"
for artifact in melee-core.js melee-core.wasm melee-core.data; do
  source="$ROOT/build/melee_core/wasm/$artifact"
  if [[ ! -f "$source" ]]; then
    echo "error: expected Wasm artifact missing: $source" >&2
    exit 1
  fi
  cp "$source" "$OUT_DIR/$artifact"
done

echo "bundled supported raw profile from $RAW_DIR ($(du -sh "$RAW_DIR" | awk '{print $1}'))"
echo "wrote $OUT_DIR/melee-core.{js,wasm,data}"
