// Bowser hit during Whirling Fortress, through the Wasm API. ftKp_SpecialHi
// sets take_dmg_cb and death2_cb, which ftCommon_8007DB58 calls with the
// fighter's GObj when the hit puts Bowser in a damage state. WebAssembly checks
// the type of every indirect call, so a callback defined with another
// signature traps there ("null function or function signature mismatch").
//
// One match on Final Destination, controller inputs only: Bowser (port 1) does
// a grounded Whirling Fortress (up + B) every 90 frames, Falco (port 2) fires
// his blaster every 12. Each frame the viewer state gives Bowser's action; the
// test needs a frame where a laser takes him out of Whirling Fortress.
import assert from 'node:assert/strict';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

import {
  INPUT_PLAYER_SIZE,
  INPUT_SIZE,
  MATCH_CONFIG_PLAYER_SIZE,
  MATCH_CONFIG_SIZE,
  matchConfigOffsets,
  VIEWER_STATE_SIZE,
  viewerOffsets,
  viewerPlayerOffsets,
} from '../../tools/viewer/live/schema.js';

const modulePath = path.resolve(
  process.argv[2] ?? 'build/melee_core/wasm/melee-core.js',
);
const moduleUrl = pathToFileURL(modulePath);
const { default: createModule } = await import(moduleUrl.href);
const module = await createModule({
  locateFile: (name) => new URL(name, moduleUrl).pathname,
});

const OK = 0;
const BOWSER = 5;
const FALCO = 22;
const FINAL_DESTINATION = 32;
const PAD_BUTTON_B = 1 << 9;
// ftKoopa's Whirling Fortress actions start at 341; Damage* are 75..91.
const FIRST_SPECIAL_ACTION = 341;
const FRAMES = 600;

const alloc = (size) => {
  const pointer = module._malloc(size);
  assert.notEqual(pointer, 0, `malloc(${size})`);
  module.HEAPU8.fill(0, pointer, pointer + size);
  return pointer;
};
const view = () => new DataView(module.HEAPU8.buffer);
const callOk = (name, ...args) => {
  const result = module[`_${name}`](...args);
  assert.equal(result, OK, `${name} returned ${result}`);
};

const dataRootBytes = new TextEncoder().encode('/data/raw\0');
const dataRoot = alloc(dataRootBytes.length);
module.HEAPU8.set(dataRootBytes, dataRoot);
const gameDataOut = alloc(4);
const batchOut = alloc(4);
const config = alloc(MATCH_CONFIG_SIZE);
const input = alloc(INPUT_SIZE);
const viewer = alloc(VIEWER_STATE_SIZE);
const mask = alloc(1);
const C = matchConfigOffsets;
view().setUint32(config + C.stageId, FINAL_DESTINATION, true);
view().setInt32(config + C.frameId, -123, true);
view().setUint32(config + C.framePreRandomSeed, 1, true);
view().setUint32(config + C.initialRandomSeed, 1, true);
view().setFloat32(config + C.damageRatio, 1, true);
module.HEAPU8[config + C.numPlayers] = 2;
module.HEAPU8[config + C.stockCount] = 4;
module.HEAPU8[config + C.players] = BOWSER;
module.HEAPU8[config + C.players + MATCH_CONFIG_PLAYER_SIZE] = FALCO;
module.HEAPU8[mask] = 1;

callOk('msl_core_game_data_create', dataRoot, gameDataOut);
callOk('msl_core_batch_create', view().getUint32(gameDataOut, true), 1, batchOut);
const batch = view().getUint32(batchOut, true);
callOk('msl_core_batch_reset_matches', batch, config, MATCH_CONFIG_SIZE, 0, 0);

let previous = -1;
let hit = null;
for (let step = 0; step < FRAMES; step += 1) {
  const upB = step % 90 < 2;
  const laser = step % 12 < 2;
  view().setUint16(input, upB ? PAD_BUTTON_B : 0, true);
  view().setInt8(input + 3, upB ? 80 : 0);
  view().setUint16(input + INPUT_PLAYER_SIZE, laser ? PAD_BUTTON_B : 0, true);
  try {
    callOk('msl_core_batch_step_matches', batch, input, INPUT_SIZE, mask, 1);
  } catch (error) {
    console.error(`step ${step}, Bowser's previous action ${previous}`);
    throw error;
  }
  callOk('msl_core_batch_write_viewer', batch, viewer, VIEWER_STATE_SIZE, 0, 0);
  const action = view().getUint16(
    viewer + viewerOffsets.players + viewerPlayerOffsets.actionId,
    true,
  );
  if (hit === null && previous >= FIRST_SPECIAL_ACTION && action >= 75 && action <= 91) {
    hit = { step, from: previous, to: action };
  }
  previous = action;
}
assert.notEqual(hit, null, 'no laser took Bowser out of Whirling Fortress');
console.log(
  `wasm Bowser callback smoke passed: hit out of action ${hit.from} into ${hit.to} at step ${hit.step}`,
);
