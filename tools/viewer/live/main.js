import { GameCubeAdapterInput } from "./gamecube_adapter.js";
import { installKeyboard, readKeyboardController } from "./keyboard.js";
import { STAGE_FINAL_DESTINATION, SUPPORTED_STAGES } from "./schema.js";
import { MslWasmSim } from "./sim.js";
import { saveLiveTrace, traceInputFromControllers } from "./trace_export.js";
import { viewerFrameFromCompare, viewerSettingsFromCompare } from "./viewer_adapter.js";

const viewer = document.querySelector("slippi-viewer");
const statusEl = document.querySelector("#status");
const inputStatusEl = document.querySelector("#input-status");
const resetButton = document.querySelector("#reset");
const saveTraceButton = document.querySelector("#save-trace");
const connectAdapterButton = document.querySelector("#connect-adapter");
const adapterPortSelect = document.querySelector("#adapter-port");
const controlP1Button = document.querySelector("#control-p1");
const controlP2Button = document.querySelector("#control-p2");
const stageSelectorEl = document.querySelector("#stage-selector");
const MAX_RENDER_FRAMES = 60 * 60 * 8 + 123;
const STEP_MS = 1000 / 60;
const MAX_STEPS_PER_PAINT = 5;
const MAX_TICK_WORK_MS = STEP_MS;
const SLOW_STEP_MS = 8;
const SLOW_TICK_MS = 50;
const SLOW_PHASE_MS = 8;
const SLOW_STEP_LOG_CAP = 128;

let sim = null;
let replayData = null;
let running = false;
let lastTickMs = 0;
let accumulatorMs = 0;
let frameCount = 0;
let seed = 1;
let inputTrace = [];
let controlledPlayer = 0;
let selectedStageId = STAGE_FINAL_DESTINATION;
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

function neutralControllers() {
  return [neutralController(), neutralController()];
}

function readHumanController() {
  if (adapterInput.active) {
    return adapterInput.readController();
  }
  return readKeyboardController();
}

function controllersForHuman(controller) {
  const controllers = neutralControllers();
  controllers[controlledPlayer] = controller;
  return controllers;
}

function controlledPlayerLabel() {
  return `P${controlledPlayer + 1}`;
}

function inputSourceLabel() {
  if (adapterInput.active) {
    return `${controlledPlayerLabel()} ${adapterInput.transportLabel()} port ${adapterInput.port + 1}`;
  }
  return `${controlledPlayerLabel()} keyboard`;
}

function playerStatusSuffix() {
  const stage = selectedStage();
  const stagePrefix = stage ? `${stage.name}. ` : "";
  if (controlledPlayer === 0) {
    return `${stagePrefix}${inputSourceLabel()}, P2 neutral.`;
  }
  return `${stagePrefix}P1 neutral, ${inputSourceLabel()}.`;
}

function selectedStage() {
  return SUPPORTED_STAGES.find((stage) => stage.id === selectedStageId) || SUPPORTED_STAGES[0];
}

function setControlledPlayer(playerIndex) {
  controlledPlayer = playerIndex;
  controlP1Button.setAttribute("aria-pressed", controlledPlayer === 0 ? "true" : "false");
  controlP2Button.setAttribute("aria-pressed", controlledPlayer === 1 ? "true" : "false");
  if (sim) {
    setStatus(`Running. Frame ${frameCount}. ${playerStatusSuffix()}`);
  }
}

function updateStageButtons() {
  for (const button of stageSelectorEl.querySelectorAll("button[data-stage-id]")) {
    const isSelected = Number(button.dataset.stageId) === selectedStageId;
    button.setAttribute("aria-pressed", isSelected ? "true" : "false");
  }
}

function setSelectedStage(stageId) {
  selectedStageId = stageId >>> 0;
  updateStageButtons();
  reset();
}

function installStageSelector() {
  stageSelectorEl.textContent = "";
  for (const stage of SUPPORTED_STAGES) {
    const button = document.createElement("button");
    button.type = "button";
    button.dataset.stageId = String(stage.id);
    button.textContent = stage.label;
    button.title = stage.name;
    button.setAttribute("aria-label", stage.name);
    button.setAttribute("aria-pressed", stage.id === selectedStageId ? "true" : "false");
    button.addEventListener("click", () => setSelectedStage(stage.id));
    stageSelectorEl.appendChild(button);
  }
}

function currentViewerFrame(frameNumber, controllers) {
  const compare = sim.compareView();
  return viewerFrameFromCompare(
    compare,
    frameNumber,
    controllers,
    sim.stageStateView(),
    sim.shieldBubblesView()
  );
}

function frameHasDeadPlayer(frame) {
  return frame.players.some((player) => player.state?.isDead);
}

function recordSlowTick(entry) {
  const log = window.__mslViewerSlowSteps || [];
  log.push(entry);
  if (log.length > SLOW_STEP_LOG_CAP) {
    log.splice(0, log.length - SLOW_STEP_LOG_CAP);
  }
  window.__mslViewerSlowSteps = log;
  console.warn("live viewer slow sim tick", entry);
}

function snapshotWasmTimings() {
  const timings = sim?.lastTimings;
  if (!timings) {
    return null;
  }
  return {
    inputMs: timings.inputMs,
    stepInputMs: timings.stepInputMs,
    writeCompareMs: timings.writeCompareMs,
    totalMs: timings.totalMs,
  };
}

function renderViewerFrame(frameNumber, frame) {
  const renderStartMs = performance.now();
  // `replayData.frames[frameNumber]` has already been mutated by live play.
  // Advancing the viewer is enough, and avoids a second Solid-store write into
  // the large live frames array every RAF.
  if (typeof viewer.setFrame === "function") {
    viewer.setFrame(frameNumber);
  } else if (typeof viewer.setFrameData === "function") {
    viewer.setFrameData(frameNumber, frame);
  }
  return performance.now() - renderStartMs;
}

function appendCurrentFrame(controllers, { render = true } = {}) {
  const frameNumber = frameCount + 1;
  if (frameNumber >= MAX_RENDER_FRAMES) {
    reset();
    return null;
  }
  const frame = currentViewerFrame(frameNumber, controllers);
  replayData.frames[frameNumber] = frame;
  inputTrace[frameNumber] = traceInputFromControllers(frameNumber, controllers);
  frameCount = frameNumber;
  window.__mslViewerFrameCount = frameCount;
  window.__mslViewerReplayData = replayData;
  if (render && (typeof viewer.setFrame === "function" || typeof viewer.setFrameData === "function")) {
    renderViewerFrame(frameNumber, frame);
    return;
  }
  if (render && typeof viewer.setFrame === "function") {
    viewer.setFrame(frameNumber);
  }
  return frame;
}

function reset() {
  if (!sim) return;
  seed = (seed + 1) >>> 0;
  const compare = sim.reset({ seed, stageId: selectedStageId });
  const firstControllers = neutralControllers();
  const firstFrame = currentViewerFrame(0, firstControllers);
  inputTrace = [traceInputFromControllers(0, firstControllers)];
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
  window.__mslViewerFrameCount = frameCount;
  window.__mslViewerReplayData = replayData;
  if (typeof viewer.setFrame === "function") {
    viewer.setFrame(0);
  }
  setStatus(`Running. ${playerStatusSuffix()}`);
}

function tick(nowMs) {
  if (!running) {
    return;
  }
  accumulatorMs += nowMs - lastTickMs;
  lastTickMs = nowMs;

  let steps = 0;
  const tickStartMs = performance.now();
  let slowStep = null;
  let slowPhase = null;
  while (accumulatorMs >= STEP_MS && steps < MAX_STEPS_PER_PAINT) {
    const controllers = controllersForHuman(readHumanController());
    const stepStartMs = performance.now();
    sim.step(controllers);
    const stepMs = performance.now() - stepStartMs;
    const appendStartMs = performance.now();
    const frame = appendCurrentFrame(controllers, { render: false });
    const appendMs = performance.now() - appendStartMs;
    accumulatorMs -= STEP_MS;
    steps += 1;
    if (stepMs >= SLOW_STEP_MS && frame) {
      slowStep = {
        frame: frameCount,
        stepMs,
        wasmTimings: snapshotWasmTimings(),
        controlledPlayer,
        players: frame.players.map((player) => ({
          actionStateId: player.state?.actionStateId,
          actionStateFrameCounter: player.state?.actionStateFrameCounter,
          animationIndex: player.state?.animationIndex,
          stocksRemaining: player.state?.stocksRemaining,
          isDead: player.state?.isDead,
          xPosition: player.state?.xPosition,
          yPosition: player.state?.yPosition,
        })),
      };
    }
    if (appendMs >= SLOW_PHASE_MS && frame) {
      slowPhase = {
        frame: frameCount,
        phase: "appendFrame",
        phaseMs: appendMs,
        wasmTimings: snapshotWasmTimings(),
        controlledPlayer,
        players: frame.players.map((player) => ({
          actionStateId: player.state?.actionStateId,
          actionStateFrameCounter: player.state?.actionStateFrameCounter,
          animationIndex: player.state?.animationIndex,
          stocksRemaining: player.state?.stocksRemaining,
          isDead: player.state?.isDead,
          xPosition: player.state?.xPosition,
          yPosition: player.state?.yPosition,
        })),
      };
    }
    if (frame && frameHasDeadPlayer(frame)) {
      const resetStartMs = performance.now();
      reset();
      const resetMs = performance.now() - resetStartMs;
      if (resetMs >= SLOW_PHASE_MS) {
        slowPhase = {
          frame: frameCount,
          phase: "reset",
          phaseMs: resetMs,
          controlledPlayer,
        };
      }
      accumulatorMs = 0;
      steps = 0;
      break;
    }
    if (performance.now() - tickStartMs >= MAX_TICK_WORK_MS) {
      accumulatorMs = 0;
      break;
    }
  }
  if (steps === MAX_STEPS_PER_PAINT && accumulatorMs >= STEP_MS) {
    accumulatorMs = 0;
  }
  const tickWorkMs = performance.now() - tickStartMs;
  if (slowStep || slowPhase || tickWorkMs >= SLOW_TICK_MS) {
    recordSlowTick({
      ...(slowStep || slowPhase || { frame: frameCount, controlledPlayer }),
      wasmTimings: snapshotWasmTimings(),
      tickWorkMs,
      steps,
    });
  }

  if (steps > 0) {
    const frame = replayData.frames[frameCount];
    const renderMs = renderViewerFrame(frameCount, frame);
    if (renderMs >= SLOW_PHASE_MS) {
      recordSlowTick({
        frame: frameCount,
        phase: "renderViewerFrame",
        phaseMs: renderMs,
        controlledPlayer,
        tickWorkMs,
        steps,
        players: frame?.players?.map((player) => ({
          actionStateId: player.state?.actionStateId,
          actionStateFrameCounter: player.state?.actionStateFrameCounter,
          animationIndex: player.state?.animationIndex,
          stocksRemaining: player.state?.stocksRemaining,
          isDead: player.state?.isDead,
          xPosition: player.state?.xPosition,
          yPosition: player.state?.yPosition,
        })),
      });
    }
    if ((frameCount % 30) === 0) {
      setStatus(`Running. Frame ${frameCount}. ${playerStatusSuffix()}`);
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
  installStageSelector();
  resetButton.addEventListener("click", reset);
  saveTraceButton.addEventListener("click", () => {
    try {
      const traceName = window.prompt("Trace name", `live_f${frameCount}`);
      if (traceName === null) {
        setStatus(`Trace save canceled. Frame ${frameCount}. ${playerStatusSuffix()}`);
        return;
      }
      const filename = saveLiveTrace({
        replayData,
        frameCount,
        seed,
        inputTrace,
        sourceLabel: inputSourceLabel(),
        matchStart: {
          mode: "live-sim-init",
          traceFrame: 0,
          simFrameId: 0,
          randomSeed: seed,
          stageId: selectedStageId,
          controlledPlayer: controlledPlayer + 1,
          inputSource: inputSourceLabel(),
        },
        traceName,
      });
      setStatus(`Saved ${filename}. Frame ${frameCount}. ${playerStatusSuffix()}`);
    } catch (error) {
      setStatus(error.message, true);
    }
  });
  adapterPortSelect.addEventListener("change", () => {
    adapterInput.setPort(adapterPortSelect.value);
  });
  controlP1Button.addEventListener("click", () => setControlledPlayer(0));
  controlP2Button.addEventListener("click", () => setControlledPlayer(1));
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
