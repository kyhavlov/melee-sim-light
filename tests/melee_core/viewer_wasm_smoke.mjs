import assert from "node:assert/strict";

import { MslWasmSim } from "../../tools/viewer/live/sim.js";
import {
  BUTTONS,
  CHAR_FOX,
  CHAR_MEWTWO,
  CHAR_FALCO,
  CHAR_JIGGLYPUFF,
  CHAR_PEACH,
  CHAR_SHEIK,
  CHAR_YOSHI,
  CHAR_BOWSER,
  CHAR_NESS,
  CHAR_LINK,
  CHAR_YOUNG_LINK,
  STAGE_FINAL_DESTINATION,
  STAGE_FOUNTAIN_OF_DREAMS,
  STAGE_YOSHIS_STORY,
  SUPPORTED_CHARACTERS,
  SUPPORTED_STAGES,
} from "../../tools/viewer/live/schema.js";
import {
  viewerFrameFromState,
  viewerSettingsFromState,
} from "../../tools/viewer/live/viewer_adapter.js";
import { replayDataToMslTrace } from "../../tools/viewer/msltrace1.js";

const neutral = () => ({
  buttons: 0,
  mainX: 0,
  mainY: 0,
  cX: 0,
  cY: 0,
  l: 0,
  r: 0,
});
const controllers = (p1 = neutral(), p2 = neutral()) => [p1, p2];

const sim = await MslWasmSim.create();
let frameNumber = 0;
const frame = (pads = controllers()) =>
  viewerFrameFromState(sim.viewerView(), frameNumber, pads);
const step = (pads = controllers()) => {
  sim.step(pads);
  frameNumber += 1;
  return frame(pads);
};
const reset = (options = {}) => {
  frameNumber = 0;
  sim.reset(options);
  return frame();
};

try {
  const heapSize = sim.module.HEAPU8.buffer.byteLength;
  const initial = reset({
    p1Char: CHAR_FOX,
    p2Char: CHAR_JIGGLYPUFF,
    stageId: STAGE_FINAL_DESTINATION,
    seed: 7,
  });
  const settings = viewerSettingsFromState(sim.viewerView());
  assert.equal(settings.playerSettings.length, 2);
  assert.equal(initial.players.length, 2);
  assert(initial.players.every((player) => Number.isFinite(player.state.xPosition)));
  assert(Number.isFinite(initial.camera.fov));

  // Exercise every public selector against the same long-lived module and
  // match storage. This is also the no-growth reset contract used by the UI.
  for (let index = 0; index < SUPPORTED_CHARACTERS.length; index += 1) {
    const character = SUPPORTED_CHARACTERS[index];
    const stage = SUPPORTED_STAGES[index % SUPPORTED_STAGES.length];
    const current = reset({
      p1Char: character.id,
      p2Char: CHAR_FOX,
      stageId: stage.id,
      seed: index + 10,
    });
    assert.equal(current.players[0].state.internalCharacterId, character.id);
    assert.equal(viewerSettingsFromState(sim.viewerView()).stageId, stage.id);
    step();
  }

  // Exercise the imported special callbacks and article creation through the
  // wasm32 function table, with the same sealed module allocation.
  for (const character of [CHAR_YOSHI, CHAR_BOWSER, CHAR_NESS, CHAR_LINK, CHAR_YOUNG_LINK, CHAR_MEWTWO]) {
    for (const [mainX, mainY] of [[0, 0], [1, 0], [0, 1], [0, -1]]) {
      reset({ p1Char: character, stageId: STAGE_FINAL_DESTINATION });
      let sawSpecial = false;
      let sawArticle = false;
      for (let index = 0; index < 160; index += 1) {
        const active = index < 30;
        const current = step(controllers(active
          ? { ...neutral(), buttons: BUTTONS.B, mainX, mainY }
          : neutral()));
        sawSpecial ||= current.players[0].state.actionStateId >= 341;
        sawArticle ||= current.items.length > 0;
      }
      assert(sawSpecial, `character ${character} special (${mainX}, ${mainY})`);
      if ((character === CHAR_YOSHI && mainY === 1) ||
          ([CHAR_BOWSER, CHAR_NESS, CHAR_LINK, CHAR_YOUNG_LINK, CHAR_MEWTWO].includes(character) &&
           mainX === 0 && mainY === 0)) {
        assert(sawArticle, `character ${character} special article`);
      }
      assert.equal(sim.module.HEAPU8.buffer.byteLength, heapSize);
    }
  }

  reset({ stageId: STAGE_FOUNTAIN_OF_DREAMS });
  const fountain = step();
  assert(
    Number.isFinite(fountain.stage.fodLeftPlatformHeight) &&
      Number.isFinite(fountain.stage.fodRightPlatformHeight),
  );
  reset({ stageId: STAGE_YOSHIS_STORY });
  assert(step().stage.randall?.exists);

  // Seed 7 sends Yoshi's first Shy Guy group through Sheik's spawn point.
  // Reproduce the live-viewer contact that exercises stage-item damage credit.
  reset({ p1Char: CHAR_SHEIK, stageId: STAGE_YOSHIS_STORY, seed: 7 });
  let hitShyGuy = false;
  for (let index = 0; index < 2400; index += 1) {
    const attack = index >= 2100 && index < 2300 && index % 12 === 0;
    const current = step(
      controllers(attack ? { ...neutral(), buttons: BUTTONS.A } : neutral()),
    );
    if (
      current.items.some((item) => item.typeId === 210 && item.damageTaken > 0)
    ) {
      hitShyGuy = true;
      break;
    }
  }
  assert(hitShyGuy, "live viewer never damaged Yoshi's Shy Guy");

  reset({ p1Char: CHAR_FOX });
  let sawHitbox = false;
  for (let index = 0; index < 20; index += 1) {
    const pads = controllers(index === 0 ? { ...neutral(), buttons: BUTTONS.A } : neutral());
    if (step(pads).players[0].state.hitboxes.length > 0) {
      sawHitbox = true;
    }
  }
  assert(sawHitbox, "live viewer never observed Fox's jab hitbox");

  reset({ p1Char: CHAR_FOX });
  let sawShield = false;
  let neutralShield;
  for (let index = 0; index < 12; index += 1) {
    const pads = controllers({ ...neutral(), buttons: BUTTONS.L, l: 1 });
    const player = step(pads).players[0].state;
    if (player.isShieldActive && (player.shieldRadius ?? 0) > 0) {
      sawShield = true;
      neutralShield = player;
    }
  }
  assert(sawShield, "live viewer never observed a source shield bubble");
  assert.equal(neutralShield.shieldX, undefined);
  assert.equal(neutralShield.shieldY, undefined);
  let tiltedShield = neutralShield;
  for (let index = 0; index < 20; index += 1) {
    const pads = controllers({
      ...neutral(),
      buttons: BUTTONS.L,
      l: 1,
      mainX: 0.4,
    });
    tiltedShield = step(pads).players[0].state;
  }
  assert(
    Math.abs(tiltedShield.shieldTiltX ?? 0) > 0.01 ||
      Math.abs(tiltedShield.shieldTiltY ?? 0) > 0.01,
    "live viewer shield projection did not expose Guard tilt",
  );

  reset({ p1Char: CHAR_PEACH, stageId: STAGE_FINAL_DESTINATION, seed: 31 });
  let sawItem = false;
  for (let index = 0; index < 90; index += 1) {
    const pull = index < 3;
    const pads = controllers({
      ...neutral(),
      buttons: pull ? BUTTONS.B : 0,
      mainY: pull ? -1 : 0,
    });
    if (step(pads).items.length > 0) {
      sawItem = true;
      break;
    }
  }
  assert(sawItem, "live viewer never observed Peach's pulled item");

  // A one-stock fighter running off FD exercises the UI's death/reset gate.
  reset({ p1Char: CHAR_FOX, stocks: 1, stageId: STAGE_FINAL_DESTINATION });
  let sawDeath = false;
  for (let index = 0; index < 700; index += 1) {
    const current = step(controllers({ ...neutral(), mainX: -1 }));
    if (current.players[0].state.isDead) {
      sawDeath = true;
      break;
    }
  }
  assert(sawDeath, "live viewer never reached its death/reset condition");
  const afterDeathReset = reset();
  assert.equal(afterDeathReset.players[0].state.isDead, false);

  // Death reconstructs source character attributes through a per-kind
  // callback. Cover the non-Fox live-viewer pairing that first exposed this
  // wasm32 indirect-call boundary.
  for (const character of [CHAR_PEACH, CHAR_FALCO]) {
    reset({
      p1Char: character,
      p2Char: character === CHAR_PEACH ? CHAR_FALCO : CHAR_PEACH,
      stocks: 1,
      stageId: STAGE_FINAL_DESTINATION,
    });
    let died = false;
    for (let index = 0; index < 700; index += 1) {
      const current = step(controllers({ ...neutral(), mainX: -1 }));
      if (current.players[0].state.isDead) {
        died = true;
        break;
      }
    }
    assert(died, `live viewer never reached ${character}'s death path`);
  }

  // Exercise the live pairing through repeated jump/air-dodge/landing color
  // scripts rather than only selector bootstrap and neutral movement.
  reset({
    p1Char: CHAR_PEACH,
    p2Char: CHAR_FALCO,
    stageId: STAGE_FINAL_DESTINATION,
    seed: 41,
  });
  for (let index = 0; index < 1200; index += 1) {
    const phase = index % 90;
    const controller = neutral();
    if (phase === 0) controller.buttons = BUTTONS.X;
    if (phase === 8) {
      controller.buttons = BUTTONS.L;
      controller.l = 1;
      controller.mainY = -1;
    }
    step(controllers(controller));
  }

  let fuzzState = 0x41c6ce57;
  const fuzzButtons = [
    0,
    BUTTONS.A,
    BUTTONS.B,
    BUTTONS.X,
    BUTTONS.Y,
    BUTTONS.Z,
    BUTTONS.L,
    BUTTONS.R,
  ];
  let fuzzController = neutral();
  for (let index = 0; index < 600; index += 1) {
    if (index % 6 === 0) {
      fuzzState = (Math.imul(fuzzState, 1664525) + 1013904223) >>> 0;
      const buttons = fuzzButtons[fuzzState & 7];
      fuzzController = {
        ...neutral(),
        buttons,
        mainX: ((fuzzState >>> 3) % 3) - 1,
        mainY: ((fuzzState >>> 5) % 3) - 1,
        cX: ((fuzzState >>> 7) % 3) - 1,
        cY: ((fuzzState >>> 9) % 3) - 1,
        l: buttons === BUTTONS.L ? 1 : 0,
        r: buttons === BUTTONS.R ? 1 : 0,
      };
    }
    try {
      step(controllers(fuzzController));
    } catch (error) {
      throw new Error(
        `Peach/Falco free-running smoke trapped at fuzz frame ${index}`,
        { cause: error },
      );
    }
  }

  const pads = controllers({ ...neutral(), mainX: 1 });
  const traceFrame0 = frame();
  const traceFrame1 = step(pads);
  const trace = replayDataToMslTrace({
    replayData: {
      settings: viewerSettingsFromState(sim.viewerView()),
      frames: [traceFrame0, traceFrame1],
      ending: { gameEndMethod: "GAME!", quitInitiator: -1 },
    },
    frameCount: 1,
    inputTrace: [
      [0, [0, 0, 0, 0, 0, 0, 0], [0, 0, 0, 0, 0, 0, 0]],
      [1, [0, 1, 0, 0, 0, 0, 0], [0, 0, 0, 0, 0, 0, 0]],
    ],
    producer: { name: "melee_core viewer smoke", version: "phase6" },
  });
  assert.equal(trace.format, "MSLTRACE1");
  assert.equal(sim.module.HEAPU8.buffer.byteLength, heapSize);
  console.log(
    JSON.stringify({
      status: "PASS",
      characters: SUPPORTED_CHARACTERS.length,
      stages: SUPPORTED_STAGES.length,
      hitbox: sawHitbox,
      shield: sawShield,
      item: sawItem,
      shy_guy_hit: hitShyGuy,
      death_reset: sawDeath,
      wasm_memory_bytes: heapSize,
    }),
  );
} finally {
  sim.destroy();
}
