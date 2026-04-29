import { GameCubeAdapterInput } from "./gamecube_adapter.js";
import { installKeyboard, readKeyboardController } from "./keyboard.js";
import { MslWasmSim } from "./sim.js";
import { saveWebplayTrace, traceInputFromController } from "./trace_export.js";
import { viewerFrameFromCompare, viewerSettingsFromCompare } from "./viewer_adapter.js";

const viewer = document.querySelector("slippi-viewer");
const statusEl = document.querySelector("#status");
const inputStatusEl = document.querySelector("#input-status");
const resetButton = document.querySelector("#reset");
const saveTraceButton = document.querySelector("#save-trace");
const connectAdapterButton = document.querySelector("#connect-adapter");
const adapterPortSelect = document.querySelector("#adapter-port");
const MAX_RENDER_FRAMES = 60 * 60 * 8 + 123;
const STEP_MS = 1000 / 60;
const MAX_STEPS_PER_PAINT = 5;

let sim = null;
let replayData = null;
let running = false;
let lastTickMs = 0;
let accumulatorMs = 0;
let frameCount = 0;
let seed = 1;
let inputTrace = [];
const adapterInput = new GameCubeAdapterInput({
  onStatus(message) {
    inputStatusEl.textContent = message;
  },
});

function setStatus(message, isError = false) {
  statusEl.textContent = message;
  statusEl.className = isError ? "status error" : "status";
}

function neutralController() {
  return { buttons: 0, mainX: 0, mainY: 0, cX: 0, cY: 0, l: 0, r: 0 };
}

function readP1Controller() {
  if (adapterInput.active) {
    return adapterInput.readController();
  }
  return readKeyboardController();
}

function inputSourceLabel() {
  if (adapterInput.active) {
    return `P1 ${adapterInput.transportLabel()} port ${adapterInput.port + 1}`;
  }
  return "P1 keyboard";
}

function currentViewerFrame(frameNumber, controller) {
  const compare = sim.compareView();
  return viewerFrameFromCompare(compare, frameNumber, [controller, neutralController()]);
}

function appendCurrentFrame(controller, { render = true } = {}) {
  const frameNumber = frameCount + 1;
  if (frameNumber >= MAX_RENDER_FRAMES) {
    reset();
    return;
  }
  const frame = currentViewerFrame(frameNumber, controller);
  replayData.frames[frameNumber] = frame;
  inputTrace[frameNumber] = traceInputFromController(frameNumber, controller);
  frameCount = frameNumber;
  window.__webplayFrameCount = frameCount;
  window.__webplayReplayData = replayData;
  if (render && typeof viewer.setFrameData === "function") {
    viewer.setFrameData(frameNumber, frame);
    return;
  }
  if (render && typeof viewer.setFrame === "function") {
    viewer.setFrame(frameNumber);
  }
}

function reset() {
  if (!sim) return;
  seed = (seed + 1) >>> 0;
  const compare = sim.reset({ seed });
  const firstFrame = currentViewerFrame(0, neutralController());
  inputTrace = [traceInputFromController(0, neutralController())];
  const frames = new Array(MAX_RENDER_FRAMES);
  frames[0] = firstFrame;
  replayData = {
    settings: viewerSettingsFromCompare(compare),
    frames,
    ending: {
      gameEndMethod: "GAME!",
      quitInitiator: -1,
    },
  };
  viewer.setLiveReplayData(replayData);
  if (typeof viewer.pausePlayback === "function") {
    viewer.pausePlayback();
  }
  frameCount = 0;
  window.__webplayFrameCount = frameCount;
  window.__webplayReplayData = replayData;
  if (typeof viewer.setFrame === "function") {
    viewer.setFrame(0);
  }
  setStatus(`Running. ${inputSourceLabel()}, P2 neutral.`);
}

function tick(nowMs) {
  if (!running) {
    return;
  }
  accumulatorMs += nowMs - lastTickMs;
  lastTickMs = nowMs;

  let steps = 0;
  while (accumulatorMs >= STEP_MS && steps < MAX_STEPS_PER_PAINT) {
    const controller = readP1Controller();
    sim.step(controller);
    appendCurrentFrame(controller, { render: false });
    accumulatorMs -= STEP_MS;
    steps += 1;
  }
  if (steps === MAX_STEPS_PER_PAINT && accumulatorMs >= STEP_MS) {
    accumulatorMs = 0;
  }

  if (steps > 0) {
    const frame = replayData.frames[frameCount];
    if (typeof viewer.setFrameData === "function") {
      viewer.setFrameData(frameCount, frame);
    } else if (typeof viewer.setFrame === "function") {
      viewer.setFrame(frameCount);
    }
    if ((frameCount % 30) === 0) {
      setStatus(`Running. Frame ${frameCount}. ${inputSourceLabel()}, P2 neutral.`);
    }
  }
  requestAnimationFrame(tick);
}

async function main() {
  if (typeof viewer?.setLiveReplayData !== "function") {
    throw new Error("slippi-viewer bundle is not loaded or lacks setLiveReplayData()");
  }
  sim = await MslWasmSim.create();
  installKeyboard(reset);
  resetButton.addEventListener("click", reset);
  saveTraceButton.addEventListener("click", () => {
    try {
      const filename = saveWebplayTrace({
        replayData,
        frameCount,
        seed,
        inputTrace,
        sourceLabel: inputSourceLabel(),
      });
      setStatus(`Saved ${filename}. Frame ${frameCount}. ${inputSourceLabel()}, P2 neutral.`);
    } catch (error) {
      setStatus(error.message, true);
    }
  });
  adapterPortSelect.addEventListener("change", () => {
    adapterInput.setPort(adapterPortSelect.value);
  });
  connectAdapterButton.addEventListener("click", async () => {
    connectAdapterButton.disabled = true;
    inputStatusEl.textContent = "GameCube adapter: checking browser gamepads / local bridge...";
    try {
      adapterInput.setPort(adapterPortSelect.value);
      await adapterInput.connect();
    } catch (error) {
      inputStatusEl.textContent = `GameCube adapter: ${error.message}`;
    } finally {
      connectAdapterButton.disabled = false;
    }
  });
  inputStatusEl.textContent = adapterInput.supported
    ? "GameCube adapter: checking browser gamepads..."
    : "GameCube adapter: Gamepad/WebHID unavailable.";
  adapterInput.startGamepadPolling();
  reset();
  running = true;
  lastTickMs = performance.now();
  accumulatorMs = 0;
  requestAnimationFrame(tick);
}

main().catch((error) => {
  console.error(error);
  setStatus(error.message, true);
});
