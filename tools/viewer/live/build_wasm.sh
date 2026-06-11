#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT_DIR="$ROOT/tools/viewer/live/public"

DATA_DIR="${MSL_DATA_DIR:-}"
if [[ -z "$DATA_DIR" ]]; then
  if [[ -d "$ROOT/.msl" ]]; then
    DATA_DIR="$ROOT/.msl"
  else
    DATA_DIR="$ROOT/data"
  fi
fi
if [[ ! -d "$DATA_DIR" ]]; then
  echo "error: extracted simulator data directory not found: $DATA_DIR" >&2
  echo "Set MSL_DATA_DIR, or run python -m melee_sim.extract_data --iso /path/to/SSBM.iso." >&2
  exit 1
fi
DATA_DIR="$(cd "$DATA_DIR" && pwd)"

if ! command -v emcc >/dev/null 2>&1; then
  echo "error: emcc is not on PATH. Install/activate Emscripten before building the live viewer." >&2
  exit 1
fi

required_data=(
  "$DATA_DIR/common/ft_common_data.json"
  "$DATA_DIR/stages/battlefield.json"
  "$DATA_DIR/stages/dream_land_n64.json"
  "$DATA_DIR/stages/final_destination.json"
  "$DATA_DIR/stages/fountain_of_dreams.json"
  "$DATA_DIR/stages/pokemon_stadium.json"
  "$DATA_DIR/stages/yoshis_story.json"
  "$DATA_DIR/stages/bin/grnla.bin"
  "$DATA_DIR/stages/bin/grnba.bin"
  "$DATA_DIR/stages/bin/griz.bin"
  "$DATA_DIR/stages/bin/grps.bin"
  "$DATA_DIR/stages/bin/grst.bin"
  "$DATA_DIR/stages/bin/grop.bin"
  "$DATA_DIR/items/item_common.json"
  "$DATA_DIR/items/lasers.bin"
  "$DATA_DIR/items/articles/fox_falco.bin"
  "$DATA_DIR/stage_items/yoshi_shyguy.bin"
  "$DATA_DIR/stage_items/yoshi_shyguy.json"
  "$DATA_DIR/stage_items/dream_whispy.bin"
  "$DATA_DIR/stage_items/dream_whispy.json"
)

viewer_chars=(fox falco marth)
for char in "${viewer_chars[@]}"; do
  required_data+=(
    "$DATA_DIR/characters/$char.json"
    "$DATA_DIR/scripts/$char.bin"
    "$DATA_DIR/motion_state/owners/$char.bin"
    "$DATA_DIR/anims/$char.tracks.bin"
    "$DATA_DIR/hitboxes/$char.bin"
    "$DATA_DIR/hurtcaps/$char.bin"
    "$DATA_DIR/ecb/${char}_bottom.bin"
    "$DATA_DIR/ecb/${char}_extents.bin"
    "$DATA_DIR/attack_id/move_id/$char.bin"
  )
done

missing=0
for path in "${required_data[@]}"; do
  if [[ ! -f "$path" ]]; then
    echo "missing required data: ${path#$DATA_DIR/}" >&2
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
  -sEXPORTED_FUNCTIONS='["_malloc","_free","_msl_batch_create","_msl_batch_destroy","_msl_batch_init_match","_msl_batch_step_input","_msl_batch_write_compare","_msl_batch_debug_write_processed_input","_msl_batch_debug_write_stage_state","_msl_batch_debug_shield_display_bubbles_world"]' \
  -sEXPORTED_RUNTIME_METHODS='["HEAPU8"]' \
  --preload-file "$DATA_DIR@/data" \
  -o "$OUT_DIR/msl_sim.js"

echo "bundled simulator data from $DATA_DIR"
echo "wrote $OUT_DIR/msl_sim.js"
