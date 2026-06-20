import {
  BUTTONS,
  HITBOX_PLAYER_SIZE,
  HITBOX_SIZE,
  ITEM_SIZE,
  compareOffsets,
  hitboxOffsets,
  itemOffsets,
  stageStateOffsets,
} from "./schema.js";

const HURTBOX_STATES = ["vulnerable", "invulnerable", "intangible"];

function u8(view, offset) {
  return view.getUint8(offset);
}

function i8(view, offset) {
  return view.getInt8(offset);
}

function u16(view, offset) {
  return view.getUint16(offset, true);
}

function i16(view, offset) {
  return view.getInt16(offset, true);
}

function u32(view, offset) {
  return view.getUint32(offset, true);
}

function f32(view, offset) {
  return view.getFloat32(offset, true);
}

function arrU8(view, base, index) {
  return u8(view, base + index);
}

function arrU16(view, base, index) {
  return u16(view, base + index * 2);
}

function arrI16(view, base, index) {
  return i16(view, base + index * 2);
}

function arrU32(view, base, index) {
  return u32(view, base + index * 4);
}

function arrF32(view, base, index) {
  return f32(view, base + index * 4);
}

function externalCharId(internalCharId) {
  if (internalCharId === 1) return 2;
  if (internalCharId === 7) return 19;
  if (internalCharId === 18) return 9;
  if (internalCharId === 22) return 20;
  return internalCharId;
}

function controllerInput(frameNumber, playerIndex, controller) {
  const buttons = controller.buttons ?? 0;
  const processed = {
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
    rTriggerDigital: Boolean(buttons & BUTTONS.R),
    lTriggerDigital: Boolean(buttons & BUTTONS.L),
    joystickX: controller.mainX ?? 0,
    joystickY: controller.mainY ?? 0,
    cStickX: controller.cX ?? 0,
    cStickY: controller.cY ?? 0,
    anyTrigger: Math.max(controller.l ?? 0, controller.r ?? 0),
  };
  return {
    frameNumber,
    playerIndex,
    isNana: false,
    physical: {
      a: processed.a,
      b: processed.b,
      x: processed.x,
      y: processed.y,
      z: processed.z,
      start: false,
      dPadLeft: false,
      dPadRight: false,
      dPadDown: false,
      dPadUp: processed.dPadUp,
      rTriggerAnalog: controller.r ?? 0,
      rTriggerDigital: processed.rTriggerDigital,
      lTriggerAnalog: controller.l ?? 0,
      lTriggerDigital: processed.lTriggerDigital,
    },
    processed,
  };
}

export function viewerSettingsFromCompare(compare, { startStocks = 4 } = {}) {
  const numPlayers = u8(compare, compareOffsets.numPlayers);
  const playerSettings = [];
  for (let idx = 0; idx < numPlayers; idx += 1) {
    const charId = arrU8(compare, compareOffsets.charId, idx);
    playerSettings.push({
      playerIndex: idx,
      port: idx + 1,
      externalCharacterId: externalCharId(charId),
      internalCharacterIds: [charId],
      playerType: idx === 0 ? 0 : 1,
      startStocks,
      costumeIndex: 0,
      teamShade: 0,
      handicap: 9,
      teamId: arrU8(compare, compareOffsets.teamId, idx),
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
    });
  }
  return {
    replayFormatVersion: "3.9.0.0",
    startTimestamp: "2026-04-08T00:00:00Z",
    isTeams: Boolean(u8(compare, compareOffsets.isTeams)),
    stageId: u32(compare, compareOffsets.stageId),
    isPal: false,
    isFrozenStadium: u32(compare, compareOffsets.stageId) === 3,
    platform: "dolphin",
    consoleNickname: "melee-sim-light-browser",
    timerType: "counting down",
    characterUiPlacesCount: numPlayers,
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
    playerSettings,
  };
}

export function viewerFrameFromCompare(
  compare,
  frameNumber,
  controllersByPlayer,
  stageState = null,
  shieldBubbles = null,
  hitboxes = null
) {
  const numPlayers = u8(compare, compareOffsets.numPlayers);
  const players = [];
  for (let idx = 0; idx < numPlayers; idx += 1) {
    const flagsBase = compareOffsets.stateFlags + idx * 5;
    const flags2218 = u8(compare, flagsBase);
    const flags221a = u8(compare, flagsBase + 1);
    const flags221b = u8(compare, flagsBase + 2);
    const flags221c = u8(compare, flagsBase + 3);
    const hurtboxState = arrU8(compare, compareOffsets.hurtboxState, idx);
    const controller = controllersByPlayer[idx] ?? {};
    const shieldRadius = shieldBubbles ? arrF32(shieldBubbles, 0, idx * 4 + 3) : 0;
    const hasShieldBubble = Number.isFinite(shieldRadius) && shieldRadius > 0;
    players.push({
      frameNumber,
      playerIndex: idx,
      inputs: controllerInput(frameNumber, idx, controller),
      state: {
        frameNumber,
        playerIndex: idx,
        isNana: false,
        internalCharacterId: arrU8(compare, compareOffsets.charId, idx),
        actionStateId: arrU16(compare, compareOffsets.actionId, idx),
        xPosition: arrF32(compare, compareOffsets.posX, idx),
        yPosition: arrF32(compare, compareOffsets.posY, idx),
        facingDirection: arrU8(compare, compareOffsets.facing, idx) === 0 ? -1.0 : 1.0,
        percent: arrF32(compare, compareOffsets.percent, idx),
        shieldSize: arrF32(compare, compareOffsets.shieldHp, idx),
        shieldX: hasShieldBubble ? arrF32(shieldBubbles, 0, idx * 4) : undefined,
        shieldY: hasShieldBubble ? arrF32(shieldBubbles, 0, idx * 4 + 1) : undefined,
        shieldRadius: hasShieldBubble ? shieldRadius : undefined,
        lastHittingAttackId: 0,
        currentComboCount: 0,
        lastHitBy: 0,
        stocksRemaining: arrU8(compare, compareOffsets.stocks, idx),
        actionStateFrameCounter: arrI16(compare, compareOffsets.actionFrame, idx),
        hitstunRemaining: arrU16(compare, compareOffsets.hitstun, idx),
        isGrounded: Boolean(arrU8(compare, compareOffsets.onGround, idx)),
        lastGroundId: 0,
        jumpsRemaining: arrU8(compare, compareOffsets.jumpsLeft, idx),
        lCancelStatus: null,
        hurtboxCollisionState: HURTBOX_STATES[hurtboxState] ?? "vulnerable",
        animationIndex: arrU32(compare, compareOffsets.animationIndex, idx),
        selfInducedAirXSpeed: arrF32(compare, compareOffsets.speedAirXSelf, idx),
        selfInducedAirYSpeed: arrF32(compare, compareOffsets.speedYSelf, idx),
        attackBasedXSpeed: arrF32(compare, compareOffsets.speedXAttack, idx),
        attackBasedYSpeed: arrF32(compare, compareOffsets.speedYAttack, idx),
        selfInducedGroundXSpeed: arrF32(compare, compareOffsets.speedGroundXSelf, idx),
        hitlagRemaining: arrU16(compare, compareOffsets.hitlag, idx),
        hitboxes: playerHitboxes(hitboxes, idx),
        isReflectActive: Boolean(flags2218 & 0x10),
        isFastfalling: Boolean(flags221a & 0x08),
        isShieldActive: Boolean(flags221b & 0x80),
        isInHitstun: Boolean(flags221c & 0x02),
        isHittingShield: false,
        isPowershieldActive: Boolean(flags221c & 0x20),
        isDead: Boolean(arrU8(compare, compareOffsets.isDead, idx)),
        isOffscreen: false,
      },
    });
  }

  const items = [];
  for (let idx = 0; idx < 15; idx += 1) {
    const off = compareOffsets.items + idx * ITEM_SIZE;
    if (!u8(compare, off + itemOffsets.exists)) {
      continue;
    }
    items.push({
      frameNumber,
      slot: idx,
      typeId: u16(compare, off + itemOffsets.type),
      state: u8(compare, off + itemOffsets.state),
      facingDirection: f32(compare, off + itemOffsets.direction),
      xVelocity: f32(compare, off + itemOffsets.velX),
      yVelocity: f32(compare, off + itemOffsets.velY),
      xPosition: f32(compare, off + itemOffsets.posX),
      yPosition: f32(compare, off + itemOffsets.posY),
      damageTaken: u16(compare, off + itemOffsets.damage),
      expirationTimer: f32(compare, off + itemOffsets.timer),
      spawnId: u32(compare, off + itemOffsets.spawnId),
      samusMissileType: u8(compare, off + itemOffsets.misc0),
      peachTurnipFace: u8(compare, off + itemOffsets.misc1),
      isChargeShotLaunched: false,
      chargeShotChargeLevel: u8(compare, off + itemOffsets.misc2),
      owner: i8(compare, off + itemOffsets.owner),
    });
  }

  return {
    frameNumber,
    randomSeed: u32(compare, compareOffsets.randomSeed),
    players,
    items,
    stage: {
      frameNumber,
      // Debug stage-state platform order follows C runtime owner: 0=right, 1=left.
      fodLeftPlatformHeight:
        stageState && stageState.getUint8(stageStateOffsets.fodValid + 1)
          ? f32(stageState, stageStateOffsets.fodHeight + 4)
          : undefined,
      fodRightPlatformHeight:
        stageState && stageState.getUint8(stageStateOffsets.fodValid)
          ? f32(stageState, stageStateOffsets.fodHeight)
          : undefined,
      randall:
        stageState && stageState.getUint8(stageStateOffsets.randallExists)
          ? {
              exists: true,
              x: f32(stageState, stageStateOffsets.randallX),
              y: f32(stageState, stageStateOffsets.randallY),
            }
          : undefined,
    },
  };
}

function playerHitboxes(hitboxes, playerIndex) {
  if (!hitboxes) {
    return [];
  }
  const out = [];
  const base = playerIndex * HITBOX_PLAYER_SIZE;
  for (let hb = 0; hb < 4; hb += 1) {
    const off = base + hb * HITBOX_SIZE;
    const enabled = f32(hitboxes, off + hitboxOffsets.enabled) !== 0;
    const radius = f32(hitboxes, off + hitboxOffsets.radius);
    if (!enabled || !(radius > 0)) {
      continue;
    }
    out.push({
      id: hb,
      x: f32(hitboxes, off + hitboxOffsets.x),
      y: f32(hitboxes, off + hitboxOffsets.y),
      z: f32(hitboxes, off + hitboxOffsets.z),
      radius,
      damage: f32(hitboxes, off + hitboxOffsets.damage),
      bonePartId: f32(hitboxes, off + hitboxOffsets.bonePartId),
    });
  }
  return out;
}
