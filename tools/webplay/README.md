# Webplay

Flat webplay harness for `melee-sim-light`, plus a small local adapter bridge for raw
GameCube adapter input.

The first stage is intentionally narrow:

- compile the existing C sim API to WASM
- run Fox vs Falco on Final Destination from C-owned match init
- control P1 with keyboard mappings or a GameCube adapter
- keep P2 neutral
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

Start webplay, then click `Connect GC Adapter` and select the adapter port to use for P1:

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

Click `Save Trace` to download a compact `webplay_input_trace_...json` from match start through the
current frame. It stores the match settings, seed, and compact P1 inputs by frame for reproducing
the same interactive prefix; P2 is neutral. Full modelplay viewer frames are intentionally omitted
from this artifact to keep it small enough to send around.

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
