# MSL Viewer

Browser viewer for `MSLTRACE1` files and live WASM play.

All shell commands below are meant to be run from the repository root. Paths in
commands are repository-root relative unless stated otherwise.

## Build

Prerequisites:

- Emscripten (`emcc`) on `PATH` for WASM export of the sim
- extracted simulator data; defaults to repository-root `data/`, or set `MSL_DATA_DIR`
- network access on first build to install Slippi viewer npm dependencies
- network access on first build to fetch character display assets from Slippi Lab

```bash
cd /path/to/melee-sim-light
make viewer-build
```

On the Marth/newchar branch, make sure the local data directory includes Marth
before building:

```bash
uv run python -m tools.extraction.build_data --iso-dir _iso --stages grnla,grnba,griz,grps,grst,grop --chars fox,falco,marth
```

`make viewer-build` writes a self-contained browser asset tree under
repository-root `build/viewer`. The WASM output bundles only the simulator data
files required by the live viewer, staged from `MSL_DATA_DIR` or repository-root
`data/`. Character display zips are downloaded into repository-root
`build/cache/viewer-zips/` and reused on later builds after checksum
verification.

## Run

```bash
make viewer
```

Open:

```text
http://127.0.0.1:8001/tools/viewer/
```

The viewer opens `*.msltrace.json` files. The `Live Play` link opens the same
local server's interactive sim page with keyboard/controller input and trace
export.

Use `OPEN=0` to skip opening the browser:

```bash
make viewer OPEN=0
```

## Trace Format

`MSLTRACE1` is documented in `tools/viewer/TRACE_FORMAT.md`. It is compact JSON
with sparse-delta frame, input, and item streams. Modelplay, live viewer export,
and downstream users should write this format rather than the renderer's
internal replay shape.
