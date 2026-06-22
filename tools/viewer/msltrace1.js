const TRACE_FORMAT = "MSLTRACE1";
const SCHEMA_VERSION = 1;
const SPARSE_DELTA_ENCODING = "sparse-delta-v1";
const KEYFRAME_INTERVAL = 60;

const INPUT_FIELDS = ["buttons", "mainX", "mainY", "cX", "cY", "l", "r"];
const FRAME_FIELDS = ["frame", "randomSeed", "players"];
const STAGE_FIELDS = ["randallExists", "randallX", "randallY"];
const PLAYER_FIELDS = [
  "charId",
  "actionId",
  "actionFrame",
  "x",
  "y",
  "facing",
  "grounded",
  "percent",
  "shield",
  "stocks",
  "jumps",
  "hitlag",
  "hitstun",
  "hurtbox",
  "reflect",
  "fastfall",
  "shielding",
  "inHitstun",
  "powershield",
  "dead",
];
const ITEM_FIELDS = [
  "alive",
  "typeId",
  "state",
  "owner",
  "x",
  "y",
  "vx",
  "vy",
  "facing",
  "damage",
  "timer",
  "spawnId",
  "misc0",
  "misc1",
  "misc2",
];

const HURTBOX_STATES = ["vulnerable", "invulnerable", "intangible"];
const BUTTONS = {
  A: 0x0100,
  B: 0x0200,
  X: 0x0400,
  Y: 0x0800,
  Z: 0x0010,
  L: 0x0040,
  R: 0x0020,
  D_UP: 0x0008,
};

function requireObject(value, label) {
  if (!value || typeof value !== "object" || Array.isArray(value)) {
    throw new Error(`${label} must be an object`);
  }
  return value;
}

function requireArray(value, label) {
  if (!Array.isArray(value)) {
    throw new Error(`${label} must be an array`);
  }
  return value;
}

function fieldIndex(fields, name, label) {
  const idx = fields.indexOf(name);
  if (idx < 0) {
    throw new Error(`${label} missing required field ${name}`);
  }
  return idx;
}

function applyChanges(row, changes, fieldCount, label) {
  for (const change of requireArray(changes, label)) {
    if (!Array.isArray(change) || change.length !== 2) {
      throw new Error(`${label} contains an invalid change`);
    }
    const idx = Number(change[0]);
    if (!Number.isInteger(idx) || idx < 0 || idx >= fieldCount) {
      throw new Error(`${label} contains out-of-range field index ${change[0]}`);
    }
    row[idx] = change[1];
  }
}

function changedFields(previous, current) {
  if (!previous) {
    return current.map((value, idx) => [idx, value]);
  }
  const changes = [];
  for (let idx = 0; idx < current.length; idx += 1) {
    if (current[idx] !== previous[idx]) {
      changes.push([idx, current[idx]]);
    }
  }
  return changes;
}

function roundNumber(value) {
  if (typeof value !== "number" || !Number.isFinite(value)) {
    return 0;
  }
  const rounded = Math.round(value * 1000000) / 1000000;
  return Object.is(rounded, -0) ? 0 : rounded;
}

function externalCharId(internalCharId) {
  if (internalCharId === 1) return 2;
  if (internalCharId === 7) return 19;
  if (internalCharId === 18) return 9;
  if (internalCharId === 19) return 18;
  if (internalCharId === 22) return 20;
  return internalCharId;
}

function viewerTeamId(teamId) {
  if (teamId === 1) return 2;
  if (teamId === 2) return 1;
  return teamId;
}

function playerSettingsFromMatch(match, idx) {
  const player = match.players[idx];
  const charId = Number(player.charId || 0);
  return {
    playerIndex: idx,
    port: Number(player.port || idx + 1),
    externalCharacterId: externalCharId(charId),
    internalCharacterIds: [charId],
    playerType: idx === 0 ? 0 : 1,
    startStocks: 4,
    costumeIndex: 0,
    teamShade: 0,
    handicap: 9,
    teamId: viewerTeamId(Number(player.teamId || 0)),
    staminaMode: false,
    silentCharacter: false,
    lowGravity: false,
    invisible: false,
    blackStockIcon: false,
    metal: false,
    startGameOnWarpPlatform: false,
    rumbleEnabled: false,
    cpuLevel: 0,
    offenseRatio: 1.0,
    defenseRatio: 1.0,
    modelScale: 1.0,
    controllerFix: "UCF",
    nametag: "",
    displayName: `P${idx + 1}`,
    connectCode: "",
  };
}

function viewerSettings(trace) {
  const match = trace.match;
  const stageId = Number(match.stageId || 0);
  const playerCount = Number(match.numPlayers || match.players.length);
  return {
    replayFormatVersion: "3.9.0.0",
    startTimestamp: trace.createdAt || "2026-05-21T00:00:00Z",
    isTeams: Boolean(match.isTeams),
    stageId,
    isPal: false,
    isFrozenStadium: stageId === 3,
    platform: "dolphin",
    consoleNickname: "melee-sim-light",
    timerType: "counting down",
    characterUiPlacesCount: playerCount,
    gameType: "stock",
    friendlyFireOn: false,
    isBreakTheTargetsOrTitleDemo: false,
    isClassicOrAdventureMode: false,
    isHomeRunContestOrEventMatch: false,
    isSingleButtonMode: false,
    timerCountsDuringPause: false,
    bombRain: false,
    itemSpawnRate: "off",
    selfDestructScoreValue: -1,
    timerStart: 480,
    damageRatio: 1.0,
    playerSettings: Array.from({ length: playerCount }, (_, idx) =>
      playerSettingsFromMatch(match, idx)
    ),
  };
}

function inputObject(frameNumber, playerIndex, row, inputLookup) {
  const buttons = Number(row[inputLookup.buttons] || 0);
  const physical = {
    a: Boolean(buttons & BUTTONS.A),
    b: Boolean(buttons & BUTTONS.B),
    x: Boolean(buttons & BUTTONS.X),
    y: Boolean(buttons & BUTTONS.Y),
    z: Boolean(buttons & BUTTONS.Z),
    start: false,
    dPadLeft: false,
    dPadRight: false,
    dPadDown: false,
    dPadUp: Boolean(buttons & BUTTONS.D_UP),
    rTriggerAnalog: Number(row[inputLookup.r] || 0),
    rTriggerDigital: Boolean(buttons & BUTTONS.R),
    lTriggerAnalog: Number(row[inputLookup.l] || 0),
    lTriggerDigital: Boolean(buttons & BUTTONS.L),
  };
  const processed = {
    a: physical.a,
    b: physical.b,
    x: physical.x,
    y: physical.y,
    z: physical.z,
    start: false,
    dPadLeft: false,
    dPadRight: false,
    dPadDown: false,
    dPadUp: physical.dPadUp,
    rTriggerDigital: physical.rTriggerDigital,
    lTriggerDigital: physical.lTriggerDigital,
    joystickX: Number(row[inputLookup.mainX] || 0),
    joystickY: Number(row[inputLookup.mainY] || 0),
    cStickX: Number(row[inputLookup.cX] || 0),
    cStickY: Number(row[inputLookup.cY] || 0),
    anyTrigger: Math.max(physical.lTriggerAnalog, physical.rTriggerAnalog),
  };
  return { frameNumber, playerIndex, isNana: false, physical, processed };
}

function decodeInputStreams(inputs, frameCount, numPlayers) {
  const neutralFields = ["buttons", "mainX", "mainY", "cX", "cY", "l", "r"];
  const fields = inputs?.fields || neutralFields;
  const lookup = Object.fromEntries(fields.map((name, idx) => [name, idx]));
  for (const name of neutralFields) {
    fieldIndex(fields, name, "inputs.fields");
  }
  const streams = inputs?.players || [];
  const decoded = [];
  for (let playerIndex = 0; playerIndex < numPlayers; playerIndex += 1) {
    const rows = streams[playerIndex] || [];
    const out = new Array(frameCount);
    let current = new Array(fields.length).fill(0);
    let rowIndex = 0;
    for (let frame = 0; frame < frameCount; frame += 1) {
      while (rowIndex < rows.length && Number(rows[rowIndex][1]) <= frame) {
        const [op, rowFrame, payload] = rows[rowIndex];
        if (Number(rowFrame) !== frame) {
          break;
        }
        if (op === 0) {
          current = requireArray(payload, "input keyframe").slice();
        } else if (op === 1) {
          current = current.slice();
          applyChanges(current, payload, fields.length, "input delta");
        } else {
          throw new Error(`unsupported input op ${op}`);
        }
        rowIndex += 1;
      }
      out[frame] = current;
    }
    decoded.push(out);
  }
  return { fields, lookup, decoded };
}

function stageObject(frameNumber, row, lookup) {
  const randallExists = Boolean(Number(row?.[lookup.randallExists] || 0));
  return {
    frameNumber,
    randall: randallExists
      ? {
          exists: true,
          x: Number(row[lookup.randallX] || 0),
          y: Number(row[lookup.randallY] || 0),
        }
      : undefined,
  };
}

function decodeStage(stage, frameCount) {
  const fields = stage?.fields || STAGE_FIELDS;
  const lookup = Object.fromEntries(fields.map((name, idx) => [name, idx]));
  for (const name of STAGE_FIELDS) {
    fieldIndex(fields, name, "stage.fields");
  }
  const rows = stage?.rows || [];
  const out = new Array(frameCount);
  let current = new Array(fields.length).fill(0);
  let rowIndex = 0;
  for (let frame = 0; frame < frameCount; frame += 1) {
    while (rowIndex < rows.length && Number(rows[rowIndex][1]) === frame) {
      const [op, rowFrame, payload] = rows[rowIndex];
      if (Number(rowFrame) !== frame) {
        throw new Error("invalid stage row");
      }
      if (op === 0) {
        current = requireArray(payload, "stage keyframe").slice();
      } else if (op === 1) {
        current = current.slice();
        applyChanges(current, payload, fields.length, "stage delta");
      } else {
        throw new Error(`unsupported stage op ${op}`);
      }
      rowIndex += 1;
    }
    out[frame] = stageObject(frame, current, lookup);
  }
  return out;
}

function viewerPlayer(frameNumber, playerIndex, row, playerLookup, inputs) {
  const hurtbox = Number(row[playerLookup.hurtbox] || 0);
  return {
    frameNumber,
    playerIndex,
    inputs,
    state: {
      frameNumber,
      playerIndex,
      isNana: false,
      internalCharacterId: Number(row[playerLookup.charId] || 0),
      actionStateId: Number(row[playerLookup.actionId] || 0),
      xPosition: Number(row[playerLookup.x] || 0),
      yPosition: Number(row[playerLookup.y] || 0),
      facingDirection: Number(row[playerLookup.facing] || 1),
      percent: Number(row[playerLookup.percent] || 0),
      shieldSize: Number(row[playerLookup.shield] || 0),
      lastHittingAttackId: 0,
      currentComboCount: 0,
      lastHitBy: 0,
      stocksRemaining: Number(row[playerLookup.stocks] || 0),
      actionStateFrameCounter: Number(row[playerLookup.actionFrame] || 0),
      hitstunRemaining: Number(row[playerLookup.hitstun] || 0),
      isGrounded: Boolean(row[playerLookup.grounded]),
      lastGroundId: 0,
      jumpsRemaining: Number(row[playerLookup.jumps] || 0),
      lCancelStatus: null,
      hurtboxCollisionState: HURTBOX_STATES[hurtbox] || "vulnerable",
      selfInducedAirXSpeed: 0,
      selfInducedAirYSpeed: 0,
      attackBasedXSpeed: 0,
      attackBasedYSpeed: 0,
      selfInducedGroundXSpeed: 0,
      hitlagRemaining: Number(row[playerLookup.hitlag] || 0),
      isReflectActive: Boolean(row[playerLookup.reflect]),
      isFastfalling: Boolean(row[playerLookup.fastfall]),
      isShieldActive: Boolean(row[playerLookup.shielding]),
      isInHitstun: Boolean(row[playerLookup.inHitstun]),
      isHittingShield: false,
      isPowershieldActive: Boolean(row[playerLookup.powershield]),
      isDead: Boolean(row[playerLookup.dead]),
      isOffscreen: false,
    },
  };
}

function decodeItems(items, frameCount) {
  if (!items) {
    return Array.from({ length: frameCount }, () => []);
  }
  if (items.encoding !== SPARSE_DELTA_ENCODING) {
    throw new Error(`unsupported items encoding ${items.encoding}`);
  }
  const fields = requireArray(items.fields, "items.fields");
  const lookup = Object.fromEntries(fields.map((name, idx) => [name, idx]));
  const rows = requireArray(items.rows, "items.rows");
  const out = Array.from({ length: frameCount }, () => []);
  const slots = new Map();
  let rowIndex = 0;
  for (let frame = 0; frame < frameCount; frame += 1) {
    while (rowIndex < rows.length && Number(rows[rowIndex][1]) === frame) {
      const [op, rowFrame, slotValue, payload] = rows[rowIndex];
      const slot = Number(slotValue);
      if (Number(rowFrame) !== frame || !Number.isInteger(slot)) {
        throw new Error("invalid item row");
      }
      if (op === 0) {
        slots.set(slot, requireArray(payload, "item keyframe").slice());
      } else if (op === 1) {
        const current = (slots.get(slot) || new Array(fields.length).fill(0)).slice();
        applyChanges(current, payload, fields.length, "item delta");
        if (Number(current[lookup.alive] || 0) === 0) {
          slots.delete(slot);
        } else {
          slots.set(slot, current);
        }
      } else {
        throw new Error(`unsupported item op ${op}`);
      }
      rowIndex += 1;
    }
    out[frame] = [...slots.entries()].map(([slot, row]) => ({
      frameNumber: frame,
      slot,
      typeId: Number(row[lookup.typeId] || 0),
      state: Number(row[lookup.state] || 0),
      owner: Number(row[lookup.owner] ?? -1),
      xPosition: Number(row[lookup.x] || 0),
      yPosition: Number(row[lookup.y] || 0),
      xVelocity: Number(row[lookup.vx] || 0),
      yVelocity: Number(row[lookup.vy] || 0),
      facingDirection: Number(row[lookup.facing] || 0),
      damageTaken: Number(row[lookup.damage] || 0),
      expirationTimer: Number(row[lookup.timer] || 0),
      spawnId: Number(row[lookup.spawnId] || 0),
      samusMissileType: Number(row[lookup.misc0] || 0),
      peachTurnipFace: Number(row[lookup.misc1] || 0),
      isChargeShotLaunched: false,
      chargeShotChargeLevel: Number(row[lookup.misc2] || 0),
    }));
  }
  return out;
}

function hurtboxCode(value) {
  if (value === "invulnerable") return 1;
  if (value === "intangible") return 2;
  return 0;
}

function playerRowFromViewer(player) {
  const state = player?.state || {};
  return [
    Number(state.internalCharacterId || 0),
    Number(state.actionStateId || 0),
    roundNumber(Number(state.actionStateFrameCounter || 0)),
    roundNumber(Number(state.xPosition || 0)),
    roundNumber(Number(state.yPosition || 0)),
    Number(state.facingDirection || 1) < 0 ? -1 : 1,
    state.isGrounded ? 1 : 0,
    roundNumber(Number(state.percent || 0)),
    roundNumber(Number(state.shieldSize || 0)),
    Number(state.stocksRemaining || 0),
    Number(state.jumpsRemaining || 0),
    Number(state.hitlagRemaining || 0),
    Number(state.hitstunRemaining || 0),
    hurtboxCode(state.hurtboxCollisionState),
    state.isReflectActive ? 1 : 0,
    state.isFastfalling ? 1 : 0,
    state.isShieldActive ? 1 : 0,
    state.isInHitstun ? 1 : 0,
    state.isPowershieldActive ? 1 : 0,
    state.isDead ? 1 : 0,
  ];
}

function itemRowFromViewer(item) {
  return [
    1,
    Number(item.typeId || 0),
    Number(item.state || 0),
    Number(item.owner ?? -1),
    roundNumber(Number(item.xPosition || 0)),
    roundNumber(Number(item.yPosition || 0)),
    roundNumber(Number(item.xVelocity || 0)),
    roundNumber(Number(item.yVelocity || 0)),
    roundNumber(Number(item.facingDirection || 0)),
    Number(item.damageTaken || 0),
    roundNumber(Number(item.expirationTimer || 0)),
    Number(item.spawnId || 0),
    Number(item.samusMissileType || 0),
    Number(item.peachTurnipFace || 0),
    Number(item.chargeShotChargeLevel || 0),
  ];
}

function stageRowFromViewer(stage) {
  const randall = stage?.randall;
  return [
    randall?.exists ? 1 : 0,
    roundNumber(Number(randall?.x || 0)),
    roundNumber(Number(randall?.y || 0)),
  ];
}

function encodeInputStreams(inputTrace, frameTotal, numPlayers) {
  const streams = Array.from({ length: numPlayers }, () => []);
  const previous = Array.from({ length: numPlayers }, () => null);
  const neutral = [0, 0, 0, 0, 0, 0, 0];
  for (let frame = 0; frame < frameTotal; frame += 1) {
    for (let playerIndex = 0; playerIndex < numPlayers; playerIndex += 1) {
      const row = (inputTrace?.[frame]?.[playerIndex + 1] || neutral).map((value) =>
        typeof value === "number" ? roundNumber(value) : value
      );
      const isKeyframe = frame === 0 || frame % KEYFRAME_INTERVAL === 0;
      const changes = changedFields(previous[playerIndex], row);
      if (isKeyframe || previous[playerIndex] === null) {
        streams[playerIndex].push([0, frame, row]);
      } else if (changes.length > 0) {
        streams[playerIndex].push([1, frame, changes]);
      }
      previous[playerIndex] = row;
    }
  }
  return streams;
}

function encodeFrameRows(replayData, frameTotal, numPlayers) {
  const rows = [];
  let previousPlayers = null;
  let previousSeed = null;
  for (let frame = 0; frame < frameTotal; frame += 1) {
    const replayFrame = replayData.frames[frame];
    if (!replayFrame) {
      throw new Error(`Trace has a missing frame at ${frame}.`);
    }
    const players = [];
    for (let playerIndex = 0; playerIndex < numPlayers; playerIndex += 1) {
      players.push(playerRowFromViewer(replayFrame.players?.[playerIndex]));
    }
    const seed = Number(replayFrame.randomSeed || 0);
    const isKeyframe = frame === 0 || frame % KEYFRAME_INTERVAL === 0;
    if (isKeyframe || previousPlayers === null) {
      rows.push([0, frame, seed, players]);
    } else {
      rows.push([
        1,
        frame,
        seed !== previousSeed ? seed : null,
        players.map((player, idx) => changedFields(previousPlayers[idx], player)),
      ]);
    }
    previousPlayers = players;
    previousSeed = seed;
  }
  return rows;
}

function encodeItemRows(replayData, frameTotal) {
  const rows = [];
  const previousBySlot = new Map();
  for (let frame = 0; frame < frameTotal; frame += 1) {
    const liveSlots = new Set();
    const items = replayData.frames[frame]?.items || [];
    const isKeyframe = frame === 0 || frame % KEYFRAME_INTERVAL === 0;
    for (let liveIndex = 0; liveIndex < items.length; liveIndex += 1) {
      const item = items[liveIndex];
      const slot = Number(item.slot ?? liveIndex);
      liveSlots.add(slot);
      const row = itemRowFromViewer(item);
      const previous = previousBySlot.get(slot) || null;
      const changes = changedFields(previous, row);
      if (isKeyframe || previous === null) {
        rows.push([0, frame, slot, row]);
      } else if (changes.length > 0) {
        rows.push([1, frame, slot, changes]);
      }
      previousBySlot.set(slot, row);
    }
    for (const slot of [...previousBySlot.keys()].sort((a, b) => a - b)) {
      if (!liveSlots.has(slot)) {
        rows.push([1, frame, slot, [[0, 0]]]);
        previousBySlot.delete(slot);
      }
    }
  }
  return rows;
}

function encodeStageRows(replayData, frameTotal) {
  const rows = [];
  let previous = null;
  for (let frame = 0; frame < frameTotal; frame += 1) {
    const row = stageRowFromViewer(replayData.frames[frame]?.stage);
    const isKeyframe = frame === 0 || frame % KEYFRAME_INTERVAL === 0;
    const changes = changedFields(previous, row);
    if (isKeyframe || previous === null) {
      rows.push([0, frame, row]);
    } else if (changes.length > 0) {
      rows.push([1, frame, changes]);
    }
    previous = row;
  }
  return rows;
}

export function replayDataToMslTrace({
  replayData,
  frameCount,
  inputTrace,
  producer = { name: "melee-sim-light", version: null },
  matchStart = {},
  metadata = {},
}) {
  if (!replayData || !replayData.settings || !Array.isArray(replayData.frames)) {
    throw new Error("No replay data is available.");
  }
  const frameTotal = Number(frameCount) + 1;
  if (!Number.isInteger(frameTotal) || frameTotal <= 0) {
    throw new Error("No trace frames are available.");
  }
  const settings = replayData.settings;
  const playerSettings = settings.playerSettings || [];
  const numPlayers = Number(settings.characterUiPlacesCount || playerSettings.length || 2);
  const firstFrame = replayData.frames[0];
  const matchPlayers = [];
  for (let idx = 0; idx < numPlayers; idx += 1) {
    const player = firstFrame?.players?.[idx]?.state || {};
    const config = playerSettings[idx] || {};
    matchPlayers.push({
      port: Number(config.port || idx + 1),
      charId: Number(player.internalCharacterId || config.internalCharacterIds?.[0] || 0),
      teamId: Number(config.teamId || 0),
    });
  }
  return {
    format: TRACE_FORMAT,
    schemaVersion: SCHEMA_VERSION,
    producer,
    createdAt: new Date().toISOString(),
    match: {
      stageId: Number(settings.stageId || 0),
      numPlayers,
      isTeams: Boolean(settings.isTeams),
      players: matchPlayers,
      startFrame: 0,
      start: {
        traceFrame: 0,
        simFrameId: 0,
        randomSeed: Number(replayData.frames[0]?.randomSeed || 0),
        ...matchStart,
      },
    },
    inputs: {
      encoding: SPARSE_DELTA_ENCODING,
      keyframeInterval: KEYFRAME_INTERVAL,
      fields: INPUT_FIELDS,
      players: encodeInputStreams(inputTrace, frameTotal, numPlayers),
    },
    frames: {
      encoding: SPARSE_DELTA_ENCODING,
      keyframeInterval: KEYFRAME_INTERVAL,
      fields: FRAME_FIELDS,
      playerFields: PLAYER_FIELDS,
      rows: encodeFrameRows(replayData, frameTotal, numPlayers),
    },
    stage: {
      encoding: SPARSE_DELTA_ENCODING,
      keyframeInterval: KEYFRAME_INTERVAL,
      fields: STAGE_FIELDS,
      rows: encodeStageRows(replayData, frameTotal),
    },
    items: {
      encoding: SPARSE_DELTA_ENCODING,
      keyframeInterval: KEYFRAME_INTERVAL,
      fields: ITEM_FIELDS,
      rows: encodeItemRows(replayData, frameTotal),
    },
    metadata,
  };
}

export function mslTraceToReplayData(trace) {
  requireObject(trace, "trace");
  if (trace.format !== TRACE_FORMAT) {
    throw new Error(`unsupported trace format ${trace.format}`);
  }
  if (trace.schemaVersion !== SCHEMA_VERSION) {
    throw new Error(`unsupported trace schemaVersion ${trace.schemaVersion}`);
  }
  const match = requireObject(trace.match, "trace.match");
  const players = requireArray(match.players, "trace.match.players");
  const frames = requireObject(trace.frames, "trace.frames");
  if (frames.encoding !== SPARSE_DELTA_ENCODING) {
    throw new Error(`unsupported frames encoding ${frames.encoding}`);
  }
  const playerFields = requireArray(frames.playerFields, "frames.playerFields");
  const playerLookup = Object.fromEntries(playerFields.map((name, idx) => [name, idx]));
  for (const name of ["charId", "actionId", "actionFrame", "x", "y", "facing", "grounded", "percent", "stocks"]) {
    fieldIndex(playerFields, name, "frames.playerFields");
  }
  const rows = requireArray(frames.rows, "frames.rows");
  const frameCount = rows.length === 0 ? 0 : Math.max(...rows.map((row) => Number(row[1]))) + 1;
  const inputState = decodeInputStreams(trace.inputs, frameCount, players.length);
  const stageFrames = decodeStage(trace.stage, frameCount);
  const itemFrames = decodeItems(trace.items, frameCount);
  const replayFrames = new Array(frameCount);
  let currentPlayers = null;
  let currentSeed = 0;
  for (const row of rows) {
    const [op, frameNumberValue, randomSeed, payload] = row;
    const frameNumber = Number(frameNumberValue);
    if (!Number.isInteger(frameNumber) || frameNumber < 0) {
      throw new Error(`invalid frame number ${frameNumberValue}`);
    }
    if (op === 0) {
      currentPlayers = requireArray(payload, "frame keyframe").map((player) =>
        requireArray(player, "player keyframe").slice()
      );
      currentSeed = randomSeed == null ? 0 : Number(randomSeed);
    } else if (op === 1) {
      if (!currentPlayers) {
        throw new Error("frame delta before keyframe");
      }
      if (randomSeed != null) {
        currentSeed = Number(randomSeed);
      }
      const changesByPlayer = requireArray(payload, "frame delta");
      currentPlayers = currentPlayers.map((player, idx) => {
        const next = player.slice();
        applyChanges(next, changesByPlayer[idx] || [], playerFields.length, "player delta");
        return next;
      });
    } else {
      throw new Error(`unsupported frame op ${op}`);
    }
    if (currentPlayers.length !== players.length) {
      throw new Error(`frame ${frameNumber} has ${currentPlayers.length} players, expected ${players.length}`);
    }
    replayFrames[frameNumber] = {
      frameNumber,
      randomSeed: currentSeed,
      players: currentPlayers.map((player, playerIndex) =>
        viewerPlayer(
          frameNumber,
          playerIndex,
          player,
          playerLookup,
          inputObject(frameNumber, playerIndex, inputState.decoded[playerIndex][frameNumber], inputState.lookup)
        )
      ),
      items: itemFrames[frameNumber] || [],
      stage: stageFrames[frameNumber],
    };
  }
  return {
    settings: viewerSettings({ ...trace, match }),
    frames: replayFrames,
    ending: {
      gameEndMethod: "GAME!",
      quitInitiator: -1,
    },
  };
}

export async function loadMslTraceFile(file) {
  return mslTraceToReplayData(JSON.parse(await file.text()));
}
