import createMslModule from "./public/msl_sim.js";
import {
  CHAR_FALCO,
  CHAR_FOX,
  COMPARE_SIZE,
  INPUT_SIZE,
  MATCH_CONFIG_SIZE,
  STAGE_FINAL_DESTINATION,
  inputPlayerOffsets,
  matchConfigOffsets,
} from "./schema.js";

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
    const module = await createMslModule({
      locateFile(path) {
        return new URL(`./public/${path}`, import.meta.url).href;
      },
    });
    return new MslWasmSim(module);
  }

  constructor(module) {
    this.module = module;
    this.handle = module._msl_batch_create(1, 2);
    if (!this.handle) {
      throw new Error("msl_batch_create failed");
    }

    this.matchPtr = module._malloc(MATCH_CONFIG_SIZE);
    this.prevInputPtr = module._malloc(INPUT_SIZE);
    this.inputPtr = module._malloc(INPUT_SIZE);
    this.comparePtr = module._malloc(COMPARE_SIZE);
    if (!this.matchPtr || !this.prevInputPtr || !this.inputPtr || !this.comparePtr) {
      throw new Error("failed to allocate WASM IO buffers");
    }
    this.displayFrame = 0;
    this.lastTimings = {
      inputMs: 0,
      stepInputMs: 0,
      writeCompareMs: 0,
      totalMs: 0,
    };
  }

  destroy() {
    if (this.handle) {
      this.module._msl_batch_destroy(this.handle);
      this.handle = 0;
    }
    for (const ptr of [this.matchPtr, this.prevInputPtr, this.inputPtr, this.comparePtr]) {
      if (ptr) this.module._free(ptr);
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
    this.module.HEAPU8.fill(0, this.matchPtr, this.matchPtr + MATCH_CONFIG_SIZE);
    this.module.HEAPU8.fill(0, this.prevInputPtr, this.prevInputPtr + INPUT_SIZE);
    this.module.HEAPU8.fill(0, this.inputPtr, this.inputPtr + INPUT_SIZE);

    const view = new DataView(this.module.HEAPU8.buffer, this.matchPtr, MATCH_CONFIG_SIZE);
    view.setUint32(matchConfigOffsets.stageId, stageId >>> 0, true);
    view.setInt32(matchConfigOffsets.frameId, 0, true);
    view.setUint32(matchConfigOffsets.randomSeed, seed >>> 0, true);
    view.setFloat32(matchConfigOffsets.damageRatio, 1.0, true);
    view.setUint8(matchConfigOffsets.numPlayers, 2);
    view.setUint8(matchConfigOffsets.isTeams, 0);
    view.setUint8(matchConfigOffsets.stockCount, stocks);
    this.#writePlayerConfig(view, 0, p1Char, 0, 1);
    this.#writePlayerConfig(view, 1, p2Char, 1, 0);

    const err = this.module._msl_batch_init_match(this.handle, this.matchPtr, MATCH_CONFIG_SIZE);
    if (err !== 0) {
      throw new Error(`msl_batch_init_match failed: ${err}`);
    }
    this.#writeCompare();
    return this.compareView();
  }

  step(controllers) {
    const totalStartMs = performance.now();
    const inputStartMs = totalStartMs;
    this.module.HEAPU8.copyWithin(
      this.prevInputPtr,
      this.inputPtr,
      this.inputPtr + INPUT_SIZE
    );
    this.module.HEAPU8.fill(0, this.inputPtr, this.inputPtr + INPUT_SIZE);
    const playerControllers = Array.isArray(controllers) ? controllers : [controllers];
    this.#writeController(0, playerControllers[0] || {});
    this.#writeController(1, playerControllers[1] || {});
    const inputMs = performance.now() - inputStartMs;

    const stepInputStartMs = performance.now();
    const err = this.module._msl_batch_step_input(
      this.handle,
      this.prevInputPtr,
      INPUT_SIZE,
      this.inputPtr,
      INPUT_SIZE
    );
    const stepInputMs = performance.now() - stepInputStartMs;
    if (err !== 0) {
      throw new Error(`msl_batch_step_input failed: ${err}`);
    }
    const writeCompareStartMs = performance.now();
    this.#writeCompare();
    const writeCompareMs = performance.now() - writeCompareStartMs;
    this.displayFrame += 1;
    this.lastTimings.inputMs = inputMs;
    this.lastTimings.stepInputMs = stepInputMs;
    this.lastTimings.writeCompareMs = writeCompareMs;
    this.lastTimings.totalMs = performance.now() - totalStartMs;
    return this.compareView();
  }

  compareView() {
    return new DataView(this.module.HEAPU8.buffer, this.comparePtr, COMPARE_SIZE);
  }

  #writeCompare() {
    const err = this.module._msl_batch_write_compare(this.handle, this.comparePtr, COMPARE_SIZE);
    if (err !== 0) {
      throw new Error(`msl_batch_write_compare failed: ${err}`);
    }
  }

  #writePlayerConfig(view, playerIndex, charId, teamId, facing) {
    const off = matchConfigOffsets.players + playerIndex * matchConfigOffsets.playerSize;
    view.setUint8(off, charId);
    view.setUint8(off + 1, teamId);
    view.setUint8(off + 2, facing);
  }

  #writeController(playerIndex, controller) {
    const off = this.inputPtr + playerIndex * inputPlayerOffsets.size;
    const view = new DataView(this.module.HEAPU8.buffer, off, inputPlayerOffsets.size);
    view.setUint16(inputPlayerOffsets.buttons, controller.buttons & 0xffff, true);
    view.setInt8(inputPlayerOffsets.mainX, stickToRaw(controller.mainX));
    view.setInt8(inputPlayerOffsets.mainY, stickToRaw(controller.mainY));
    view.setInt8(inputPlayerOffsets.cX, stickToRaw(controller.cX));
    view.setInt8(inputPlayerOffsets.cY, stickToRaw(controller.cY));
    view.setUint8(inputPlayerOffsets.l, triggerToRaw(controller.l));
    view.setUint8(inputPlayerOffsets.r, triggerToRaw(controller.r));
  }
}
