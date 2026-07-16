import assert from 'node:assert/strict';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

import { VIEWER_STATE_SIZE } from '../../tools/viewer/live/schema.js';

const modulePath = path.resolve(
  process.argv[2] ?? 'build/melee_core/wasm/melee-core.js',
);
const moduleUrl = pathToFileURL(modulePath);
const { default: createModule } = await import(moduleUrl.href);
const started = performance.now();
const module = await createModule({
  locateFile: (name) => new URL(name, moduleUrl).pathname,
});

const OK = 0;
const INVALID_ARGUMENT = 1;
const INVALID_STATE = 3;
const CONFIG_SIZE = 52;
const INPUT_SIZE = 52;
const STATE_SIZE = 1022;
const VIEWER_SIZE = VIEWER_STATE_SIZE;
const MATCH_COUNT = 2;

const allocations = [];
const alloc = (size) => {
  const pointer = module._malloc(size);
  assert.notEqual(pointer, 0, `malloc(${size})`);
  allocations.push(pointer);
  module.HEAPU8.fill(0, pointer, pointer + size);
  return pointer;
};
const view = () => new DataView(module.HEAPU8.buffer);
const u32 = (pointer) => view().getUint32(pointer, true);
const putU32 = (pointer, value) => view().setUint32(pointer, value, true);
const putI32 = (pointer, value) => view().setInt32(pointer, value, true);
const putF32 = (pointer, value) => view().setFloat32(pointer, value, true);
const callOk = (name, ...args) => {
  const result = module[`_${name}`](...args);
  assert.equal(result, OK, `${name} returned ${result}`);
};
const step = (batch, inputs, mask, label) => {
  try {
    callOk(
      'msl_core_batch_step_matches',
      batch,
      inputs,
      INPUT_SIZE,
      mask,
      mask === 0 ? 0 : 1,
    );
  } catch (error) {
    console.error(`wasm API smoke step failed at ${label}`);
    throw error;
  }
};
const bytes = (pointer, size) =>
  Uint8Array.from(module.HEAPU8.subarray(pointer, pointer + size));
const hash64 = (data) => {
  let hash = 0xcbf29ce484222325n;
  for (const value of data) {
    hash = BigInt.asUintN(64, (hash ^ BigInt(value)) * 0x100000001b3n);
  }
  return hash.toString(16).padStart(16, '0');
};

const dataRootBytes = new TextEncoder().encode('/data/raw\0');
const dataRoot = alloc(dataRootBytes.length);
module.HEAPU8.set(dataRootBytes, dataRoot);
const gameDataOut = alloc(4);
const batchOut = alloc(4);
const snapshotSizeOut = alloc(4);
const snapshotWrittenOut = alloc(4);
const configs = alloc(CONFIG_SIZE * MATCH_COUNT);
const inputs = alloc(INPUT_SIZE * MATCH_COUNT);
const states = alloc(STATE_SIZE * MATCH_COUNT);
const viewers = alloc(VIEWER_SIZE * MATCH_COUNT);
const terminal = alloc(MATCH_COUNT);
const mask = alloc(MATCH_COUNT);
const copyDestination = alloc(4);
const copySource = alloc(4);
putU32(copyDestination, 1);
putU32(copySource, 0);

const writeConfig = (row, stage, character) => {
  const base = configs + row * CONFIG_SIZE;
  putU32(base, stage);
  putI32(base + 4, -123);
  putU32(base + 8, 1);
  putU32(base + 12, 1);
  putF32(base + 16, 1);
  module.HEAPU8[base + 20] = 2;
  module.HEAPU8[base + 23] = 4;
  module.HEAPU8[base + 32] = character;
  module.HEAPU8[base + 37] = character;
};
writeConfig(0, 32, 1);
writeConfig(1, 2, 9);
module.HEAPU8[inputs + 2] = 80;
module.HEAPU8[inputs + INPUT_SIZE + 2] = 80;

let gameData = 0;
let batch = 0;
let restoredBatch = 0;
let snapshot = 0;
try {
  callOk('msl_core_game_data_create', dataRoot, gameDataOut);
  gameData = u32(gameDataOut);
  assert.notEqual(gameData, 0);
  callOk('msl_core_batch_create', gameData, MATCH_COUNT, batchOut);
  batch = u32(batchOut);
  assert.equal(module._msl_core_batch_match_count(batch), MATCH_COUNT);
  assert.equal(
    module._msl_core_batch_step_matches(batch, inputs, INPUT_SIZE, 0, 0),
    INVALID_STATE,
  );
  assert.equal(
    module._msl_core_batch_reset_matches(
      batch,
      configs,
      CONFIG_SIZE - 1,
      0,
      0,
    ),
    INVALID_ARGUMENT,
  );
  callOk(
    'msl_core_batch_reset_matches',
    batch,
    configs,
    CONFIG_SIZE,
    0,
    0,
  );
  module.HEAPU8[mask] = 1;
  module.HEAPU8[mask + 1] = 1;
  for (let frame = 0; frame < 60; frame += 1) {
    step(batch, inputs, mask, `prefix ${frame}`);
  }
  callOk('msl_core_batch_match_save_size', batch, 0, snapshotSizeOut);
  const snapshotSize = u32(snapshotSizeOut);
  assert(snapshotSize > 0);
  snapshot = alloc(snapshotSize);
  const heapAtRuntimeBoundary = module.HEAPU8.buffer.byteLength;
  callOk(
    'msl_core_batch_save_match',
    batch,
    0,
    snapshot,
    snapshotSize,
    snapshotWrittenOut,
  );
  assert.equal(u32(snapshotWrittenOut), snapshotSize);
  module.HEAPU8[mask + 1] = 0;
  for (let frame = 0; frame < 30; frame += 1) {
    step(batch, inputs, mask, `suffix ${frame}`);
  }
  callOk('msl_core_batch_write_state', batch, states, STATE_SIZE, 0, 0);
  callOk('msl_core_batch_write_viewer', batch, viewers, VIEWER_SIZE, 0, 0);
  const expected = bytes(states, STATE_SIZE);
  const expectedViewer = bytes(viewers, VIEWER_SIZE);
  assert.equal(view().getUint32(viewers + 8, true), 32);
  assert.equal(module.HEAPU8[viewers + 16], 2);
  callOk('msl_core_batch_write_terminal', batch, terminal, 1, 0, 0);

  // The artifact has no source-index affinity: restore match 0 into index 1,
  // advance with the same input suffix, and require byte-identical output.
  callOk(
    'msl_core_batch_restore_match',
    batch,
    1,
    snapshot,
    snapshotSize,
  );
  module.HEAPU8[mask] = 0;
  module.HEAPU8[mask + 1] = 1;
  for (let frame = 0; frame < 30; frame += 1) {
    step(batch, inputs, mask, `restored suffix ${frame}`);
  }
  callOk('msl_core_batch_write_state', batch, states, STATE_SIZE, 0, 0);
  callOk('msl_core_batch_write_viewer', batch, viewers, VIEWER_SIZE, 0, 0);
  assert.deepEqual(bytes(states + STATE_SIZE, STATE_SIZE), expected);
  assert.deepEqual(bytes(viewers + VIEWER_SIZE, VIEWER_SIZE), expectedViewer);
  callOk(
    'msl_core_batch_copy_matches',
    batch,
    batch,
    copyDestination,
    copySource,
    1,
  );
  module.HEAPU8[mask] = 1;
  module.HEAPU8[mask + 1] = 1;
  step(batch, inputs, mask, 'copied continuation');
  callOk('msl_core_batch_write_state', batch, states, STATE_SIZE, 0, 0);
  callOk('msl_core_batch_write_viewer', batch, viewers, VIEWER_SIZE, 0, 0);
  assert.deepEqual(
    bytes(states, STATE_SIZE),
    bytes(states + STATE_SIZE, STATE_SIZE),
  );
  assert.deepEqual(
    bytes(viewers, VIEWER_SIZE),
    bytes(viewers + VIEWER_SIZE, VIEWER_SIZE),
  );
  callOk(
    'msl_core_batch_reset_matches',
    batch,
    configs,
    CONFIG_SIZE,
    0,
    0,
  );
  step(batch, inputs, mask, 'post-reset');
  callOk('msl_core_batch_write_viewer', batch, viewers, VIEWER_SIZE, 0, 0);
  assert.equal(module.HEAPU8.buffer.byteLength, heapAtRuntimeBoundary);

  // The same artifact remains location-independent after destroying its
  // source Batch and GameData and recreating equivalent immutable data.
  module._msl_core_batch_destroy(batch);
  batch = 0;
  module._msl_core_game_data_destroy(gameData);
  gameData = 0;
  putU32(gameDataOut, 0);
  callOk('msl_core_game_data_create', dataRoot, gameDataOut);
  gameData = u32(gameDataOut);
  callOk('msl_core_batch_create', gameData, 1, batchOut);
  restoredBatch = u32(batchOut);
  callOk(
    'msl_core_batch_restore_match',
    restoredBatch,
    0,
    snapshot,
    snapshotSize,
  );
  for (let frame = 0; frame < 30; frame += 1) {
    step(restoredBatch, inputs, 0, `recreated suffix ${frame}`);
  }
  callOk(
    'msl_core_batch_write_state',
    restoredBatch,
    states,
    STATE_SIZE,
    0,
    0,
  );
  assert.deepEqual(bytes(states, STATE_SIZE), expected);
  callOk(
    'msl_core_batch_write_viewer',
    restoredBatch,
    viewers,
    VIEWER_SIZE,
    0,
    0,
  );
  assert.deepEqual(bytes(viewers, VIEWER_SIZE), expectedViewer);
  const stateDigest = hash64(expected);
  const viewerDigest = hash64(expectedViewer);
  if (process.env.MSL_CORE_NATIVE_DIGEST !== undefined) {
    assert.equal(stateDigest, process.env.MSL_CORE_NATIVE_DIGEST);
  }
  console.log(
    JSON.stringify({
      status: 'PASS',
      state_digest: stateDigest,
      viewer_digest: viewerDigest,
      snapshot_bytes: snapshotSize,
      module_init_ms: Math.round(performance.now() - started),
    }),
  );
} finally {
  if (restoredBatch !== 0) module._msl_core_batch_destroy(restoredBatch);
  if (batch !== 0) module._msl_core_batch_destroy(batch);
  if (gameData !== 0) module._msl_core_game_data_destroy(gameData);
  for (const pointer of allocations.reverse()) module._free(pointer);
}
