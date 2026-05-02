# Webplay

Flat webplay harness for `melee-sim-light`, plus a small local adapter bridge for raw
GameCube adapter input.

The first stage is intentionally narrow:

- compile the existing C sim API to WASM
- run Fox vs Falco on the supported runtime stages from C-owned match init
- switch stages from the browser controls, restarting the match on selection
- control P1 or P2 with keyboard mappings or a GameCube adapter
- keep the non-controlled player neutral
- display live sim frames through the existing modelplay viewer bundle

The sim and viewer run in the page. `make webplay` starts the one local process needed for static
file serving and raw adapter I/O.

## Build

Prerequisites:

- Emscripten (`emcc`) on `PATH`
- generated sim data present under repo-root `data/`
- modelplay viewer bundle built at `tools/modelplay/viewer/dist/index.js`

```bash
cd /path/to/melee-sim-light
tools/webplay/build_wasm.sh
cd tools/modelplay/viewer
npm run build
cd /path/to/melee-sim-light
make webplay
```

Open:

`http://127.0.0.1:8001/tools/webplay/`

## Keyboard Controls

- `WASD`: main stick
- `T/G/F/H`: C-stick up/down/left/right
- `X`: A
- `C`: B
- `V`: X
- `B`: Y
- `N`: Z
- `Q`: L digital + analog
- `E`: R digital + analog
- `R`: reset match

## GameCube Adapter

Start webplay, then click `Connect GC Adapter` and select the adapter port to use for the
currently controlled player:

```bash
make webplay
```

Webplay uses the browser Gamepad API by default, so the Lossless adapter's `PC - XInput` mode can
work without the local adapter bridge once the browser exposes the gamepad. Browsers often hide
gamepads until a controller button is pressed after page load.

`make webplay` starts one local process that serves the static browser files and also bridges the
GameCube adapter over a localhost WebSocket for Switch/Dolphin mode. It prints the URL and attempts
to open it in your browser. Keyboard remains the fallback when no controller is present. Use `OPEN=0`
to skip opening the browser:

```bash
make webplay OPEN=0
```

Use `make webplay-build` to rebuild both browser artifacts:

```bash
make webplay-build
```

Click `Save Trace`, enter a trace name, and it downloads a compact `<name>.json` from match start
through the current frame. Spaces and unsafe filename characters are converted to underscores. It
stores the match settings, seed, compact P1/P2 inputs by frame, and array-encoded debug rows for
player/item state. Full modelplay viewer frame objects are intentionally omitted to avoid repeated
per-frame JSON keys.

The raw Wii U adapter protocol works through libusb/Dolphin-style access, but it is not available
to a flat browser page: Chromium blocks WebUSB `claimInterface()` for HID-class interfaces, and
WebHID does not expose the raw interrupt endpoint path needed by this adapter. For browser-only use,
launch the adapter in `PC - XInput` mode and use the Gamepad API path. The local bridge keeps only
adapter I/O outside the browser for Switch/Dolphin mode; sim and viewer still run in the page.

The local debug tools can still diagnose the raw adapter path:

```bash
tools/webplay/linux_webhid_setup.sh
tools/webplay/debug_gcadapter_libusb.sh
```
