// The Wasm module's sound core (make wasm MSL_SOUND=1; src/sound/README.md).
//
//   node tests/melee_core/sound_wasm_smoke.mjs [build/melee_core/wasm-sound/melee-core.js]
//
// Without the disc's sound files it checks what needs none: the sound's start
// names the files it reads (the sound macros, a bank, the stage's music) and
// stays off, and the match plays on as before. With MSL_SOUND_FILES set to a
// folder holding the disc's audio/ folder (audio/us/smash2.sem, audio/*.ssm,
// audio/*.hps; read here, written nowhere) it also checks, on Final
// Destination with two fighters for 400 frames:
// - every frame's PCM: 32 kHz at the console's 59.94 frames a second, in
//   whole 5 ms AX frames (480 or 640 stereo samples a frame);
// - the same match gives the same sound and the same requests, twice, and a
//   start after one that named missing files sounds as a new module's first
//   start does;
// - the requests are the ones the decomp has the game make: each fighter's
//   entry (ft_0C31.c: lbAudioAx_80024304(0x8B)) at volume 0x7F and pan 0x40,
//   both doubled by fn_80023750 (254, 128), on track 0x16 and channel 7, and
//   the stage's music before the first frame;
// - the game's state is the same frame for frame with the sound on and off;
// - restoring the sound's match turns the sound off.
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

import {
  MATCH_CONFIG_SIZE,
  MATCH_CONFIG_PLAYER_SIZE,
  matchConfigOffsets,
} from '../../tools/viewer/live/schema.js';

const modulePath = path.resolve(
  process.argv[2] ?? 'build/melee_core/wasm-sound/melee-core.js',
);
const soundFiles = process.env.MSL_SOUND_FILES || '';
const moduleUrl = pathToFileURL(modulePath);
const { default: createModule } = await import(moduleUrl.href);
const newModule = () =>
  createModule({ locateFile: (name) => fileURLToPath(new URL(name, moduleUrl)) });
let module = await newModule();
assert.equal(
  typeof module._msl_core_batch_sound_start,
  'function',
  'a module built with MSL_SOUND=1',
);

const OK = 0;
const INPUT_SIZE = 52;
const STATE_SIZE = 1302;
const FRAMES = 400;
const SOUND_ROOT = '/sound';
const ENTRY_SOUND = 0x8b;

const view = () => new DataView(module.HEAPU8.buffer);
const u32 = (pointer) => view().getUint32(pointer, true);
const i32 = (pointer) => view().getInt32(pointer, true);
const alloc = (size) => {
  const pointer = module._malloc(size);
  assert.notEqual(pointer, 0);
  module.HEAPU8.fill(0, pointer, pointer + size);
  return pointer;
};
const callOk = (name, ...args) => {
  const result = module[`_${name}`](...args);
  assert.equal(result, OK, `${name} returned ${result}`);
};
const cString = (pointer) => {
  let end = pointer;
  while (module.HEAPU8[end] !== 0) end += 1;
  return new TextDecoder().decode(module.HEAPU8.subarray(pointer, end));
};
const putString = (text) => {
  const bytes = new TextEncoder().encode(`${text}\0`);
  const pointer = alloc(bytes.length);
  module.HEAPU8.set(bytes, pointer);
  return pointer;
};

let dataRoot, soundRoot, out, config, inputs, state, gameData, batch;
// One match (Final Destination, two fighters) in a batch of one.
const setup = () => {
  dataRoot = putString('/data/raw');
  soundRoot = putString(SOUND_ROOT);
  out = alloc(4);
  config = alloc(MATCH_CONFIG_SIZE);
  inputs = alloc(INPUT_SIZE);
  state = alloc(STATE_SIZE);
  const v = view();
  v.setUint32(config, 32, true);
  v.setInt32(config + 4, -123, true);
  v.setUint32(config + 8, 1, true);
  v.setUint32(config + 12, 1, true);
  v.setFloat32(config + 16, 1, true);
  module.HEAPU8[config + 20] = 2;
  module.HEAPU8[config + 23] = 4;
  module.HEAPU8[config + matchConfigOffsets.players] = 2;
  module.HEAPU8[config + matchConfigOffsets.players + MATCH_CONFIG_PLAYER_SIZE] = 9;
  callOk('msl_core_game_data_create', dataRoot, out);
  gameData = u32(out);
  callOk('msl_core_batch_create', gameData, 1, out);
  batch = u32(out);
};
setup();

const reset = () => callOk('msl_core_batch_reset_matches', batch, config, MATCH_CONFIG_SIZE, 0, 0);
const needed = () =>
  Array.from({ length: module._msl_sound_need_count() }, (_, i) => cString(module._msl_sound_need(i)));
const start = () => {
  callOk('msl_core_batch_sound_start', batch, 0, soundRoot, out);
  return i32(out);
};
const sha = (chunks) => {
  const hash = crypto.createHash('sha256');
  for (const chunk of chunks) hash.update(chunk);
  return hash.digest('hex');
};
// A few seconds of play: both fighters walk, jump and land.
const setInputs = (frame) => {
  module.HEAPU8.fill(0, inputs, inputs + INPUT_SIZE);
  if (frame < 0) return;
  const v = view();
  v.setInt8(inputs + 2, frame % 120 < 60 ? 80 : -80);
  v.setInt8(inputs + 13 + 2, frame % 90 < 45 ? -80 : 80);
  if (frame % 50 === 10) v.setUint16(inputs, 0x0800, true); // Y: jump
  if (frame % 70 === 20) v.setUint16(inputs + 13, 0x0400, true); // X: jump
};
const play = (withSound) => {
  const frames = [];
  for (let n = 0; n < FRAMES; n += 1) {
    setInputs(n - 123);
    callOk('msl_core_batch_step_matches', batch, inputs, INPUT_SIZE, 0, 0);
    callOk('msl_core_batch_write_state', batch, state, STATE_SIZE, 0, 0);
    const pcmFrames = module._msl_sound_pcm_frames();
    const pcmPointer = module._msl_sound_pcm();
    frames.push({
      state: Buffer.from(module.HEAPU8.slice(state, state + STATE_SIZE)),
      pcm: Buffer.from(module.HEAPU8.slice(pcmPointer, pcmPointer + pcmFrames * 4)),
    });
    if (!withSound) assert.equal(pcmFrames, 0, 'no sound while it is off');
  }
  return frames;
};
const log = () => {
  const count = module._msl_sound_log_count();
  const base = module._msl_sound_log();
  return Array.from({ length: count }, (_, i) =>
    Array.from({ length: 8 }, (_, k) => i32(base + (i * 8 + k) * 4)),
  );
};

// Without the files: the start names them and the sound stays off.
reset();
const missing = start();
assert.ok(missing > 0, 'files needed');
const names = needed();
assert.equal(names.length, missing);
assert.ok(names.includes('audio/us/smash2.sem'), names.join(' '));
assert.ok(names.some((n) => /^audio\/(us\/)?[a-z0-9_]+\.ssm$/.test(n)), names.join(' '));
assert.ok(names.some((n) => /^audio\/[a-z0-9_]+\.hps$/.test(n)), names.join(' '));
assert.equal(module._msl_sound_info(1), 0);
reset();
let started = performance.now();
const silent = play(false);
const silentElapsed = performance.now() - started;
console.log(`without sound files: ${missing} files named (${names.join(', ')}), the match plays on silent`);

if (!soundFiles) {
  console.log('MSL_SOUND_FILES is not set: the sound run is skipped');
  process.exit(0);
}

const namesWritten = [];
const writeFiles = (list) => {
  for (const name of list) {
    namesWritten.push(name);
    const target = `${SOUND_ROOT}/${name}`;
    module.FS.mkdirTree(path.posix.dirname(target));
    module.FS.writeFile(target, new Uint8Array(fs.readFileSync(path.join(soundFiles, name))));
  }
};
const startWithFiles = () => {
  for (let attempt = 0; attempt < 4; attempt += 1) {
    reset();
    const left = start();
    if (left === 0) return;
    writeFiles(needed());
  }
  assert.fail('the sound did not start with the files written');
};
assert.ok(module.FS, 'the module exposes its file system (FS)');

const runs = [];
for (let k = 0; k < 2; k += 1) {
  startWithFiles();
  started = performance.now();
  const frames = play(true);
  const elapsed = performance.now() - started;
  runs.push({ frames, log: log(), elapsed });
}
const [first, second] = runs;

// Every frame's PCM: whole AX frames of 160 stereo samples, 8008/15 a frame
// on average (32000 Hz / 59.94 Hz).
const samples = first.frames.map((f) => f.pcm.length / 4);
assert.ok(samples.every((n) => n === 480 || n === 640), `per frame: ${[...new Set(samples)]}`);
const total = samples.reduce((a, b) => a + b, 0);
assert.equal(total, Math.ceil((FRAMES * 8008) / 15 / 160) * 160);
assert.ok(first.frames.some((f) => f.pcm.some((b) => b !== 0)), 'something is heard');

assert.equal(sha(second.frames.map((f) => f.pcm)), sha(first.frames.map((f) => f.pcm)), 'the same sound twice');
assert.deepEqual(second.log, first.log, 'the same requests twice');

// The game's state, frame for frame, with the sound on and off.
for (let n = 0; n < FRAMES; n += 1) {
  assert.ok(first.frames[n].state.equals(silent[n].state), `state of frame ${n - 123}`);
}

// The decomp's requests: each fighter's entry, then the stage's music.
const sounds = first.log
  .filter((entry) => entry[1] === 1)
  .map(([frame, , id, volume, pan, track, channel]) => ({ frame, id, volume, pan, track, channel }));
const entries = sounds.filter((s) => s.id === ENTRY_SOUND);
assert.equal(entries.length, 2, `one entry sound per fighter: ${JSON.stringify(sounds.slice(0, 8))}`);
for (const e of entries) {
  assert.deepEqual(
    { volume: e.volume, pan: e.pan, track: e.track, channel: e.channel },
    { volume: 254, pan: 128, track: 0x16, channel: 7 },
  );
}
assert.ok(first.log.some((entry) => entry[1] === 2 && entry[0] === 0), 'the stage music, before the first frame');

// Restoring the sound's match turns the sound off.
callOk('msl_core_batch_match_save_size', batch, 0, out);
const snapshotSize = u32(out);
const snapshot = alloc(snapshotSize);
callOk('msl_core_batch_save_match', batch, 0, snapshot, snapshotSize, out);
callOk('msl_core_batch_restore_match', batch, 0, snapshot, snapshotSize);
setInputs(0);
callOk('msl_core_batch_step_matches', batch, inputs, INPUT_SIZE, 0, 0);
assert.equal(module._msl_sound_pcm_frames(), 0, 'off after a restore');

module._msl_core_batch_destroy(batch);
module._msl_core_game_data_destroy(gameData);

// A start after one that named missing files begins where a first start
// does: a new module with the files written before its only start sounds the
// same and makes the same requests.
const allNames = [...new Set(namesWritten)];
module = await newModule();
setup();
writeFiles(allNames);
reset();
assert.equal(start(), 0, 'one start with every file there');
const fresh = play(true);
assert.equal(sha(fresh.map((f) => f.pcm)), sha(first.frames.map((f) => f.pcm)), 'a first start sounds the same');
assert.deepEqual(log(), first.log, 'a first start makes the same requests');
module._msl_core_batch_destroy(batch);
module._msl_core_game_data_destroy(gameData);
console.log(
  `sound run: ${FRAMES} frames, ${first.log.length} requests, ${total} stereo samples; ` +
    `${(Math.min(first.elapsed, second.elapsed) / FRAMES).toFixed(3)} ms a frame with the sound on, ` +
    `${(silentElapsed / FRAMES).toFixed(3)} off`,
);
