import createMslCoreModule from "./public/melee-core.js";
import {
  CHAR_FALCO,
  CHAR_FOX,
  INPUT_PLAYER_SIZE,
  INPUT_SIZE,
  MATCH_CONFIG_PLAYER_SIZE,
  MATCH_CONFIG_SIZE,
  STAGE_FINAL_DESTINATION,
  VIEWER_STATE_SIZE,
  inputPlayerOffsets,
  matchConfigOffsets,
  matchConfigPlayerOffsets,
} from "./schema.js";

const OK = 0;

function clampInt(value, lo, hi) {
  return Math.max(lo, Math.min(hi, Math.round(value)));
}

function stickToRaw(value) {
  return clampInt(value * 80, -80, 80);
}

function triggerToRaw(value) {
  return clampInt(value * 255, 0, 255);
}

export class MslWasmSim {
  static async create() {
    const module = await createMslCoreModule({
      locateFile(path) {
        const url = new URL(`./public/${path}`, import.meta.url);
        return url.protocol === "file:" ? url.pathname : url.href;
      },
    });
    return new MslWasmSim(module);
  }

  constructor(module) {
    this.module = module;
    this.gameData = 0;
    this.handle = 0;
    this.configPtr = 0;
    this.inputPtr = 0;
    this.viewerPtr = 0;
    this.displayFrame = 0;
    this.lastTimings = {
      inputMs: 0,
      stepInputMs: 0,
      writeViewerMs: 0,
      totalMs: 0,
    };

    const gameDataOut = module._malloc(4);
    const batchOut = module._malloc(4);
    const dataRootBytes = new TextEncoder().encode("/data/raw\0");
    const dataRoot = module._malloc(dataRootBytes.length);
    if (!gameDataOut || !batchOut || !dataRoot) {
      throw new Error("failed to allocate Wasm initialization arguments");
    }
    try {
      module.HEAPU8.set(dataRootBytes, dataRoot);
      module.HEAPU8.fill(0, gameDataOut, gameDataOut + 4);
      module.HEAPU8.fill(0, batchOut, batchOut + 4);
      let err = module._msl_core_game_data_create(dataRoot, gameDataOut);
      if (err !== OK) {
        throw new Error(`msl_core_game_data_create failed: ${err}`);
      }
      this.gameData = new DataView(module.HEAPU8.buffer).getUint32(gameDataOut, true);
      err = module._msl_core_batch_create(this.gameData, 1, batchOut);
      if (err !== OK) {
        throw new Error(`msl_core_batch_create failed: ${err}`);
      }
      this.handle = new DataView(module.HEAPU8.buffer).getUint32(batchOut, true);
    } catch (error) {
      if (this.handle) module._msl_core_batch_destroy(this.handle);
      if (this.gameData) module._msl_core_game_data_destroy(this.gameData);
      this.handle = 0;
      this.gameData = 0;
      throw error;
    } finally {
      module._free(dataRoot);
      module._free(batchOut);
      module._free(gameDataOut);
    }

    this.configPtr = module._malloc(MATCH_CONFIG_SIZE);
    this.inputPtr = module._malloc(INPUT_SIZE);
    this.viewerPtr = module._malloc(VIEWER_STATE_SIZE);
    if (!this.configPtr || !this.inputPtr || !this.viewerPtr) {
      this.destroy();
      throw new Error("failed to allocate Wasm simulator I/O buffers");
    }
    // GameData preload is complete and the core's runtime paths are sealed.
    // The module is configured with enough initial memory that this view stays
    // valid across reset/step/projection without allocator calls or growth.
    this.io = new DataView(module.HEAPU8.buffer);
    this.viewer = new DataView(module.HEAPU8.buffer, this.viewerPtr, VIEWER_STATE_SIZE);
  }

  destroy() {
    if (this.handle) {
      this.module._msl_core_batch_destroy(this.handle);
      this.handle = 0;
    }
    if (this.gameData) {
      this.module._msl_core_game_data_destroy(this.gameData);
      this.gameData = 0;
    }
    for (const field of ["viewerPtr", "inputPtr", "configPtr"]) {
      if (this[field]) {
        this.module._free(this[field]);
        this[field] = 0;
      }
    }
  }

  reset({
    p1Char = CHAR_FOX,
    p2Char = CHAR_FALCO,
    seed = 1,
    stocks = 4,
    stageId = STAGE_FINAL_DESTINATION,
  } = {}) {
    this.displayFrame = 0;
    this.module.HEAPU8.fill(0, this.configPtr, this.configPtr + MATCH_CONFIG_SIZE);
    this.module.HEAPU8.fill(0, this.inputPtr, this.inputPtr + INPUT_SIZE);

    const base = this.configPtr;
    this.io.setUint32(base + matchConfigOffsets.stageId, stageId >>> 0, true);
    this.io.setInt32(base + matchConfigOffsets.frameId, -123, true);
    this.io.setUint32(base + matchConfigOffsets.framePreRandomSeed, seed >>> 0, true);
    this.io.setUint32(base + matchConfigOffsets.initialRandomSeed, seed >>> 0, true);
    this.io.setFloat32(base + matchConfigOffsets.damageRatio, 1.0, true);
    this.io.setUint8(base + matchConfigOffsets.numPlayers, 2);
    this.io.setUint8(base + matchConfigOffsets.stockCount, stocks);
    this.io.setUint8(base + matchConfigOffsets.onlineFnmsubsZero, 1);
    this.io.setUint8(base + matchConfigOffsets.brawlOffscreenDamage, 1);
    this.io.setUint8(base + matchConfigOffsets.freezeDeadUpFallPhysics, 1);
    this.io.setUint8(base + matchConfigOffsets.whispyDeadFighterFix, 1);
    this.io.setUint8(base + matchConfigOffsets.ucfCardinals10Enabled, 1);
    this.io.setUint8(base + matchConfigOffsets.ucfShieldSdiEnabled, 1);
    this.io.setUint8(base + matchConfigOffsets.ucfSdiEnabled, 1);
    this.io.setUint8(base + matchConfigOffsets.ucfShieldDropExtendedEnabled, 1);
    this.io.setUint8(base + matchConfigOffsets.ucfShieldDrop084Enabled, 1);
    this.#writePlayerConfig(0, p1Char, 0, true);
    this.#writePlayerConfig(1, p2Char, 1, false);

    const err = this.module._msl_core_batch_reset_matches(
      this.handle,
      this.configPtr,
      MATCH_CONFIG_SIZE,
      0,
      0
    );
    if (err !== OK) {
      throw new Error(`msl_core_batch_reset_matches failed: ${err}`);
    }
    // Source versus bootstrap owns Entry/Appeal state and the -40 match-start
    // transition. Advance that real scheduler with neutral input so the live
    // page presents frame 0 as immediately playable without adding a second
    // initialization path inside the core.
    // refs/melee/src/melee/gm/gm_16AE.c::{fn_8016B7F8,fn_8016E730}
    for (let frame = -123; frame < 0; frame += 1) {
      let stepError;
      try {
        stepError = this.module._msl_core_batch_step_matches(
          this.handle,
          this.inputPtr,
          INPUT_SIZE,
          0,
          0
        );
      } catch (error) {
        throw new Error(`match bootstrap trapped at frame ${frame}`, { cause: error });
      }
      if (stepError !== OK) {
        throw new Error(`match bootstrap step ${frame} failed: ${stepError}`);
      }
    }
    this.#writeViewer();
    return this.viewer;
  }

  step(controllers) {
    const totalStartMs = performance.now();
    const inputStartMs = totalStartMs;
    this.module.HEAPU8.fill(0, this.inputPtr, this.inputPtr + INPUT_SIZE);
    const playerControllers = Array.isArray(controllers) ? controllers : [controllers];
    this.#writeController(0, playerControllers[0] || {});
    this.#writeController(1, playerControllers[1] || {});
    const inputMs = performance.now() - inputStartMs;

    const stepStartMs = performance.now();
    const err = this.module._msl_core_batch_step_matches(
      this.handle,
      this.inputPtr,
      INPUT_SIZE,
      0,
      0
    );
    const stepInputMs = performance.now() - stepStartMs;
    if (err !== OK) {
      throw new Error(`msl_core_batch_step_matches failed: ${err}`);
    }
    const writeStartMs = performance.now();
    this.#writeViewer();
    const writeViewerMs = performance.now() - writeStartMs;
    this.displayFrame += 1;
    this.lastTimings.inputMs = inputMs;
    this.lastTimings.stepInputMs = stepInputMs;
    this.lastTimings.writeViewerMs = writeViewerMs;
    this.lastTimings.totalMs = performance.now() - totalStartMs;
    return this.viewer;
  }

  viewerView() {
    return this.viewer;
  }

  #writeViewer() {
    const err = this.module._msl_core_batch_write_viewer(
      this.handle,
      this.viewerPtr,
      VIEWER_STATE_SIZE,
      0,
      0
    );
    if (err !== OK) {
      throw new Error(`msl_core_batch_write_viewer failed: ${err}`);
    }
  }

  #writePlayerConfig(playerIndex, charId, teamId, facing) {
    const off =
      this.configPtr +
      matchConfigOffsets.players +
      playerIndex * MATCH_CONFIG_PLAYER_SIZE;
    this.io.setUint8(off + matchConfigPlayerOffsets.charId, charId);
    this.io.setUint8(off + matchConfigPlayerOffsets.teamId, teamId);
    const physicalPort = playerIndex + 1;
    this.io.setUint8(
      off + matchConfigPlayerOffsets.facingAndPort,
      (facing ? 1 : 0) | (physicalPort << 1)
    );
    this.io.setUint8(off + matchConfigPlayerOffsets.handicap, 9);
  }

  #writeController(playerIndex, controller) {
    const off = this.inputPtr + playerIndex * INPUT_PLAYER_SIZE;
    this.io.setUint16(off + inputPlayerOffsets.buttons, controller.buttons & 0xffff, true);
    this.io.setInt8(off + inputPlayerOffsets.mainX, stickToRaw(controller.mainX ?? 0));
    this.io.setInt8(off + inputPlayerOffsets.mainY, stickToRaw(controller.mainY ?? 0));
    this.io.setInt8(off + inputPlayerOffsets.cX, stickToRaw(controller.cX ?? 0));
    this.io.setInt8(off + inputPlayerOffsets.cY, stickToRaw(controller.cY ?? 0));
    this.io.setUint8(off + inputPlayerOffsets.l, triggerToRaw(controller.l ?? 0));
    this.io.setUint8(off + inputPlayerOffsets.r, triggerToRaw(controller.r ?? 0));
  }
}
