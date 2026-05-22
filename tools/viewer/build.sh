#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT/build/viewer"
CACHE_DIR="${MSL_VIEWER_ASSET_CACHE:-$ROOT/build/cache/viewer-zips}"

"$ROOT/tools/viewer/fetch_assets.sh"
"$ROOT/tools/viewer/live/build_wasm.sh"
npm --prefix "$ROOT/tools/viewer/slippi-viewer" run build

rm -rf "$OUT_DIR"
mkdir -p \
  "$OUT_DIR/tools/viewer" \
  "$OUT_DIR/tools/viewer/live" \
  "$OUT_DIR/tools/viewer/slippi-viewer" \
  "$OUT_DIR/tools/viewer/slippi-viewer/public/zips"

cp "$ROOT/tools/viewer/index.html" "$OUT_DIR/tools/viewer/index.html"
cp "$ROOT/tools/viewer/msltrace1.js" "$OUT_DIR/tools/viewer/msltrace1.js"
cp "$ROOT/tools/viewer/README.md" "$OUT_DIR/tools/viewer/README.md"
cp "$ROOT/tools/viewer/TRACE_FORMAT.md" "$OUT_DIR/tools/viewer/TRACE_FORMAT.md"
cp -R "$ROOT/tools/viewer/examples" "$OUT_DIR/tools/viewer/examples"

cp "$ROOT/tools/viewer/live/index.html" "$OUT_DIR/tools/viewer/live/index.html"
cp "$ROOT/tools/viewer/live/main.js" "$OUT_DIR/tools/viewer/live/main.js"
cp "$ROOT/tools/viewer/live/sim.js" "$OUT_DIR/tools/viewer/live/sim.js"
cp "$ROOT/tools/viewer/live/schema.js" "$OUT_DIR/tools/viewer/live/schema.js"
cp "$ROOT/tools/viewer/live/keyboard.js" "$OUT_DIR/tools/viewer/live/keyboard.js"
cp "$ROOT/tools/viewer/live/gamecube_adapter.js" "$OUT_DIR/tools/viewer/live/gamecube_adapter.js"
cp "$ROOT/tools/viewer/live/trace_export.js" "$OUT_DIR/tools/viewer/live/trace_export.js"
cp "$ROOT/tools/viewer/live/viewer_adapter.js" "$OUT_DIR/tools/viewer/live/viewer_adapter.js"
cp "$ROOT/tools/viewer/live/linux_webhid_setup.sh" "$OUT_DIR/tools/viewer/live/linux_webhid_setup.sh"
cp "$ROOT/tools/viewer/live/debug_gcadapter_hidraw.js" "$OUT_DIR/tools/viewer/live/debug_gcadapter_hidraw.js"
cp "$ROOT/tools/viewer/live/debug_gcadapter_libusb.c" "$OUT_DIR/tools/viewer/live/debug_gcadapter_libusb.c"
cp "$ROOT/tools/viewer/live/debug_gcadapter_libusb.sh" "$OUT_DIR/tools/viewer/live/debug_gcadapter_libusb.sh"
cp -R "$ROOT/tools/viewer/live/public" "$OUT_DIR/tools/viewer/live/public"

cp -R "$ROOT/tools/viewer/slippi-viewer/dist" "$OUT_DIR/tools/viewer/slippi-viewer/dist"
while IFS=$'\t' read -r filename _sha _url; do
  [[ -z "${filename:-}" || "${filename:0:1}" == "#" ]] && continue
  cp "$CACHE_DIR/$filename" "$OUT_DIR/tools/viewer/slippi-viewer/public/zips/$filename"
done <"$ROOT/tools/viewer/assets/character_zips.tsv"

echo "wrote $OUT_DIR"
