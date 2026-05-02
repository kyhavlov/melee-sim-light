#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT/tools/webplay/public"

if ! command -v emcc >/dev/null 2>&1; then
  echo "error: emcc is not on PATH. Install/activate Emscripten before building webplay." >&2
  exit 1
fi

required_data=(
  "$ROOT/data/common/ft_common_data.json"
  "$ROOT/data/characters/fox.json"
  "$ROOT/data/characters/falco.json"
  "$ROOT/data/stages/final_destination.json"
  "$ROOT/data/stages/bin/grnla.bin"
  "$ROOT/data/stages/bin/grnba.bin"
  "$ROOT/data/stages/bin/griz.bin"
  "$ROOT/data/stages/bin/grps.bin"
  "$ROOT/data/stages/bin/grst.bin"
  "$ROOT/data/stages/bin/grop.bin"
  "$ROOT/data/scripts/fox.bin"
  "$ROOT/data/scripts/falco.bin"
  "$ROOT/data/motion_state/owners/fox.bin"
  "$ROOT/data/motion_state/owners/falco.bin"
  "$ROOT/data/items/item_common.json"
  "$ROOT/data/items/lasers.bin"
  "$ROOT/data/items/articles/fox_falco.bin"
  "$ROOT/data/anims/fox.tracks.bin"
  "$ROOT/data/anims/falco.tracks.bin"
  "$ROOT/data/hitboxes/fox.bin"
  "$ROOT/data/hitboxes/falco.bin"
  "$ROOT/data/hurtcaps/fox.bin"
  "$ROOT/data/hurtcaps/falco.bin"
  "$ROOT/data/hurtbox_states/fox.bin"
  "$ROOT/data/hurtbox_states/falco.bin"
  "$ROOT/data/hit_status/fox.bin"
  "$ROOT/data/hit_status/falco.bin"
  "$ROOT/data/ecb/fox_bottom.bin"
  "$ROOT/data/ecb/falco_bottom.bin"
  "$ROOT/data/ecb/fox_extents.bin"
  "$ROOT/data/ecb/falco_extents.bin"
  "$ROOT/data/attack_id/move_id/fox.bin"
  "$ROOT/data/attack_id/move_id/falco.bin"
)

missing=0
for path in "${required_data[@]}"; do
  if [[ ! -f "$path" ]]; then
    echo "missing required data: ${path#$ROOT/}" >&2
    missing=1
  fi
done
if [[ "$missing" != 0 ]]; then
  echo "Generate/extract data first, then rerun this script." >&2
  exit 1
fi

mkdir -p "$OUT_DIR"

mapfile -t SRC_FILES < <(find "$ROOT/src" -maxdepth 1 -type f -name '*.c' | sort)
SRC_FILES+=("$ROOT/src/decomp/lb/lb_00ce.c")

emcc "${SRC_FILES[@]}" \
  -I"$ROOT/src" \
  -O3 \
  -Wall \
  -Wextra \
  -std=c11 \
  -ffp-contract=off \
  -sMODULARIZE=1 \
  -sEXPORT_ES6=1 \
  -sEXPORT_NAME=createMslModule \
  -sENVIRONMENT=web,worker \
  -sALLOW_MEMORY_GROWTH=1 \
  -sEXPORTED_FUNCTIONS='["_malloc","_free","_msl_batch_create","_msl_batch_destroy","_msl_batch_init_match","_msl_batch_step_input","_msl_batch_write_compare","_msl_batch_debug_write_processed_input"]' \
  -sEXPORTED_RUNTIME_METHODS='["HEAPU8"]' \
  --preload-file "$ROOT/data@/data" \
  -o "$OUT_DIR/msl_sim.js"

echo "wrote $OUT_DIR/msl_sim.js"
