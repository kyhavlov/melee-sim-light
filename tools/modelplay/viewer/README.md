# slippi-viewer

A Web Component for viewing Slippi replays and streams in the browser. Extracted from [Slippi Lab](https://github.com/frankborden/slippilab).

## Features
- [x] View replays
- [x] View streams
- [x] Pause/play/rewind/fast forward/skip
- [ ] Highlights
- [x] Use as custom element
- [ ] Use as SolidJS component

## Usage

```html
<slippi-viewer zips-base-url="/" />
```

The `zips-base-url` attribute refers to where to find the character data for the visualizer. If you have your own site, it is recommended to host the zips yourself. Using `zips-base-url="/"` says "look for a folder called zips at the root of this site, and load each character's data from there". An example of this setup can be found at [examples/replay](examples/replay/index.html).

It is also possible to fetch zips from a remote URL. At the time of writing, this works for both [SpectatorMode](https://github.com/gcpreston/spectator_mode/) (via `zips-base-url="https://spectatormode.tv`), and Slippi Lab.

## Replay mode

```js
const viewer = document.querySelector("slippi-viewer");
const replayFileData; // get from file upload input, for example
viewer.setReplay(replayFileData);
```

## Spectate mode

```js
const viewer = document.querySelector("slippi-viewer");
viewer.spectate("wss://spectatormode.tv/viewer_socket/websocket?stream_id=<stream ID>");
```

The spectate example points to the websocket URL where a stream can be found on SpectatorMode, to give a concrete example.
