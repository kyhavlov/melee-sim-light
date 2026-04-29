function roundInput(value) {
  return Math.round((value || 0) * 10000) / 10000;
}

function compactController(controller) {
  return [
    controller.buttons || 0,
    roundInput(controller.mainX),
    roundInput(controller.mainY),
    roundInput(controller.cX),
    roundInput(controller.cY),
    roundInput(controller.l),
    roundInput(controller.r),
  ];
}

function timestampForFilename(date = new Date()) {
  return date.toISOString().replace(/[:.]/g, "-");
}

function sanitizeFilenamePart(value) {
  return String(value || "")
    .trim()
    .replace(/\s+/g, "_")
    .replace(/[^A-Za-z0-9._-]+/g, "_")
    .replace(/_+/g, "_")
    .replace(/^[._-]+|[._-]+$/g, "")
    .slice(0, 80);
}

export function traceInputFromControllers(frameNumber, controllers) {
  return [
    frameNumber,
    compactController(controllers?.[0]),
    compactController(controllers?.[1]),
  ];
}

function compactInputs(inputTrace, frameCount, playerIndex) {
  const inputs = [];
  const neutral = compactController({});
  for (let frame = 0; frame <= frameCount; frame += 1) {
    inputs.push(inputTrace[frame]?.[playerIndex + 1] || neutral);
  }
  return inputs;
}

function roundNumber(value) {
  if (typeof value !== "number" || !Number.isFinite(value)) {
    return 0;
  }
  return Math.round(value * 10000) / 10000;
}

const PLAYER_DEBUG_FIELDS = [
  "char",
  "action",
  "actionFrame",
  "x",
  "y",
  "facing",
  "ground",
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

const ITEM_DEBUG_FIELDS = [
  "frame",
  "slot",
  "type",
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

const HURTBOX_CODE = {
  vulnerable: 0,
  invulnerable: 1,
  intangible: 2,
};

function boolCode(value) {
  return value ? 1 : 0;
}

function playerDebugRow(player) {
  const state = player?.state || {};
  return [
    state.internalCharacterId || 0,
    state.actionStateId || 0,
    state.actionStateFrameCounter || 0,
    roundNumber(state.xPosition),
    roundNumber(state.yPosition),
    roundNumber(state.facingDirection),
    boolCode(state.isGrounded),
    roundNumber(state.percent),
    roundNumber(state.shieldSize),
    state.stocksRemaining || 0,
    state.jumpsRemaining || 0,
    state.hitlagRemaining || 0,
    state.hitstunRemaining || 0,
    HURTBOX_CODE[state.hurtboxCollisionState] ?? 0,
    boolCode(state.isReflectActive),
    boolCode(state.isFastfalling),
    boolCode(state.isShieldActive),
    boolCode(state.isInHitstun),
    boolCode(state.isPowershieldActive),
    boolCode(state.isDead),
  ];
}

function itemDebugRow(frameNumber, item, liveIndex) {
  return [
    frameNumber,
    item.slot ?? liveIndex,
    item.typeId || 0,
    item.state || 0,
    item.owner || 0,
    roundNumber(item.xPosition),
    roundNumber(item.yPosition),
    roundNumber(item.xVelocity),
    roundNumber(item.yVelocity),
    roundNumber(item.facingDirection),
    item.damageTaken || 0,
    roundNumber(item.expirationTimer),
    item.spawnId || 0,
    item.samusMissileType || 0,
    item.peachTurnipFace || 0,
    item.chargeShotChargeLevel || 0,
  ];
}

function compactDebugRows(frames, frameCount) {
  const frameRows = [];
  const itemRows = [];
  for (let frameNumber = 0; frameNumber <= frameCount; frameNumber += 1) {
    const frame = frames[frameNumber];
    if (!frame) {
      throw new Error(`Trace has a missing frame at ${frameNumber}.`);
    }
    frameRows.push([
      frameNumber,
      frame.randomSeed || 0,
      playerDebugRow(frame.players?.[0]),
      playerDebugRow(frame.players?.[1]),
    ]);
    const items = frame.items || [];
    for (let liveIndex = 0; liveIndex < items.length; liveIndex += 1) {
      itemRows.push(itemDebugRow(frameNumber, items[liveIndex], liveIndex));
    }
  }
  return { frameRows, itemRows };
}

export function saveWebplayTrace({
  replayData,
  frameCount,
  seed,
  inputTrace,
  sourceLabel,
  traceName = "",
}) {
  if (!replayData || frameCount < 0) {
    throw new Error("No webplay trace is available yet.");
  }

  const exportedAt = new Date();
  const trimmedTraceName = String(traceName || "").trim();
  const safeTraceName = sanitizeFilenamePart(trimmedTraceName);
  const outputName = safeTraceName.replace(/\.json$/i, "") || `webplay_debug_trace_${timestampForFilename(exportedAt)}_f${frameCount}`;
  const { frameRows, itemRows } = compactDebugRows(replayData.frames, frameCount);
  const payload = {
    format: "melee-sim-light-webplay-debug-trace-v1",
    exportedAt: exportedAt.toISOString(),
    name: trimmedTraceName || null,
    seed,
    frameCount,
    source: sourceLabel,
    settings: replayData.settings,
    ending: replayData.ending,
    inputFields: ["buttons", "mainX", "mainY", "cX", "cY", "l", "r"],
    p1Inputs: compactInputs(inputTrace, frameCount, 0),
    p2Inputs: compactInputs(inputTrace, frameCount, 1),
    debug: {
      frameFields: ["frame", "randomSeed", "p1", "p2"],
      playerFields: PLAYER_DEBUG_FIELDS,
      itemFields: ITEM_DEBUG_FIELDS,
      frameRows,
      itemRows,
    },
    notes: [
      "Compact webplay trace for reproducing and debugging the interactive prefix with melee-sim-light.",
      "p1Inputs and p2Inputs are indexed by frame from 0 through frameCount.",
      "debug.frameRows and debug.itemRows are array-encoded to avoid repeated per-frame object keys.",
    ],
  };

  const blob = new Blob([JSON.stringify(payload)], {
    type: "application/json",
  });
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = `${outputName}.json`;
  document.body.appendChild(anchor);
  anchor.click();
  anchor.remove();
  URL.revokeObjectURL(url);
  return anchor.download;
}
