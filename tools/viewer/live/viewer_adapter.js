import {
  BUTTONS,
  ITEM_SIZE,
  VIEWER_HITBOX_SIZE,
  VIEWER_PLAYER_SIZE,
  itemOffsets,
  viewerCameraOffsets,
  viewerHitboxOffsets,
  viewerOffsets,
  viewerPlayerOffsets,
  viewerStageOffsets,
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

function externalCharId(internalCharId) {
  if (internalCharId === 1) return 2;
  if (internalCharId === 2) return 0;
  if (internalCharId === 7) return 19;
  if (internalCharId === 9) return 12;
  if (internalCharId === 17) return 7;
  if (internalCharId === 18) return 9;
  if (internalCharId === 19) return 18;
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

export function viewerSettingsFromState(state) {
  const numPlayers = u8(state, viewerOffsets.numPlayers);
  const playerSettings = [];
  for (let idx = 0; idx < numPlayers; idx += 1) {
    const base = viewerOffsets.players + idx * VIEWER_PLAYER_SIZE;
    const charId = u8(state, base + viewerPlayerOffsets.charId);
    playerSettings.push({
      playerIndex: idx,
      port: idx + 1,
      externalCharacterId: externalCharId(charId),
      internalCharacterIds: [charId],
      playerType: idx === 0 ? 0 : 1,
      startStocks: u8(state, viewerOffsets.stockCount),
      costumeIndex: 0,
      teamShade: 0,
      handicap: 9,
      teamId: u8(state, base + viewerPlayerOffsets.teamId),
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
  const stageId = u32(state, viewerOffsets.stageId);
  return {
    replayFormatVersion: "3.9.0.0",
    startTimestamp: "2026-04-08T00:00:00Z",
    isTeams: Boolean(u8(state, viewerOffsets.isTeams)),
    stageId,
    isPal: false,
    isFrozenStadium: stageId === 3,
    platform: "dolphin",
    consoleNickname: "melee-sim-light-browser",
    timerType: "counting down",
    characterUiPlacesCount: numPlayers,
    gameType: "stock",
    friendlyFireOn: Boolean(u8(state, viewerOffsets.friendlyFire)),
    isBreakTheTargetsOrTitleDemo: false,
    isClassicOrAdventureMode: false,
    isHomeRunContestOrEventMatch: false,
    isSingleButtonMode: false,
    timerCountsDuringPause: false,
    bombRain: false,
    itemSpawnRate: "off",
    selfDestructScoreValue: -1,
    timerStart: 480,
    damageRatio: f32(state, viewerOffsets.damageRatio),
    playerSettings,
  };
}

function playerHitboxes(state, playerBase) {
  const hitboxes = [];
  const base = playerBase + viewerPlayerOffsets.hitboxes;
  for (let id = 0; id < 4; id += 1) {
    const off = base + id * VIEWER_HITBOX_SIZE;
    const radius = f32(state, off + viewerHitboxOffsets.radius);
    if (!u8(state, off + viewerHitboxOffsets.enabled) || !(radius > 0)) {
      continue;
    }
    hitboxes.push({
      id,
      x: f32(state, off + viewerHitboxOffsets.x),
      y: f32(state, off + viewerHitboxOffsets.y),
      z: f32(state, off + viewerHitboxOffsets.z),
      radius,
      damage: f32(state, off + viewerHitboxOffsets.damage),
      bonePartId: u16(state, off + viewerHitboxOffsets.bonePartId),
    });
  }
  return hitboxes;
}

export function viewerFrameFromState(state, frameNumber, controllersByPlayer) {
  const numPlayers = u8(state, viewerOffsets.numPlayers);
  const players = [];
  for (let idx = 0; idx < numPlayers; idx += 1) {
    const base = viewerOffsets.players + idx * VIEWER_PLAYER_SIZE;
    const flags = base + viewerPlayerOffsets.stateFlags;
    const flags2218 = u8(state, flags);
    const flags221a = u8(state, flags + 1);
    const flags221b = u8(state, flags + 2);
    const flags221c = u8(state, flags + 3);
    const hurtboxState = u8(state, base + viewerPlayerOffsets.hurtboxState);
    const shieldRadius = f32(state, base + viewerPlayerOffsets.shieldRadius);
    const hasShieldBubble = Number.isFinite(shieldRadius) && shieldRadius > 0;
    const sourceShieldX = f32(state, base + viewerPlayerOffsets.shieldX);
    const sourceShieldY = f32(state, base + viewerPlayerOffsets.shieldY);
    const hasShieldCenter =
      hasShieldBubble && Number.isFinite(sourceShieldX) && Number.isFinite(sourceShieldY);
    const controller = controllersByPlayer[idx] ?? {};
    players.push({
      frameNumber,
      playerIndex: idx,
      inputs: controllerInput(frameNumber, idx, controller),
      state: {
        frameNumber,
        playerIndex: idx,
        isNana: false,
        internalCharacterId: u8(state, base + viewerPlayerOffsets.charId),
        actionStateId: u16(state, base + viewerPlayerOffsets.actionId),
        xPosition: f32(state, base + viewerPlayerOffsets.posX),
        yPosition: f32(state, base + viewerPlayerOffsets.posY),
        facingDirection: u8(state, base + viewerPlayerOffsets.facing) === 0 ? -1.0 : 1.0,
        percent: f32(state, base + viewerPlayerOffsets.percent),
        shieldSize: f32(state, base + viewerPlayerOffsets.shieldHp),
        shieldX: hasShieldCenter ? sourceShieldX : undefined,
        shieldY: hasShieldCenter ? sourceShieldY : undefined,
        shieldRadius: hasShieldBubble ? shieldRadius : undefined,
        shieldTiltX: hasShieldBubble
          ? f32(state, base + viewerPlayerOffsets.shieldTiltX)
          : undefined,
        shieldTiltY: hasShieldBubble
          ? f32(state, base + viewerPlayerOffsets.shieldTiltY)
          : undefined,
        lastHittingAttackId: u8(state, base + viewerPlayerOffsets.lastAttackLanded),
        currentComboCount: u8(state, base + viewerPlayerOffsets.comboCount),
        lastHitBy: u8(state, base + viewerPlayerOffsets.lastHitBy),
        stocksRemaining: u8(state, base + viewerPlayerOffsets.stocks),
        actionStateFrameCounter: i16(state, base + viewerPlayerOffsets.actionFrame),
        hitstunRemaining: u16(state, base + viewerPlayerOffsets.hitstun),
        isGrounded: Boolean(u8(state, base + viewerPlayerOffsets.onGround)),
        lastGroundId: u16(state, base + viewerPlayerOffsets.groundId),
        jumpsRemaining: u8(state, base + viewerPlayerOffsets.jumpsLeft),
        lCancelStatus: null,
        hurtboxCollisionState: HURTBOX_STATES[hurtboxState] ?? "vulnerable",
        animationIndex: u32(state, base + viewerPlayerOffsets.animationIndex),
        selfInducedAirXSpeed: f32(state, base + viewerPlayerOffsets.speedAirXSelf),
        selfInducedAirYSpeed: f32(state, base + viewerPlayerOffsets.speedYSelf),
        attackBasedXSpeed: f32(state, base + viewerPlayerOffsets.speedXAttack),
        attackBasedYSpeed: f32(state, base + viewerPlayerOffsets.speedYAttack),
        selfInducedGroundXSpeed: f32(state, base + viewerPlayerOffsets.speedGroundXSelf),
        hitlagRemaining: u16(state, base + viewerPlayerOffsets.hitlag),
        hitboxes: playerHitboxes(state, base),
        isReflectActive: Boolean(flags2218 & 0x10),
        isFastfalling: Boolean(flags221a & 0x08),
        isShieldActive: Boolean(flags221b & 0x80),
        isInHitstun: Boolean(flags221c & 0x02),
        isHittingShield: false,
        isPowershieldActive: Boolean(flags221c & 0x20),
        isDead: Boolean(u8(state, base + viewerPlayerOffsets.isDead)),
        isOffscreen: false,
      },
    });
  }

  const items = [];
  for (let idx = 0; idx < 15; idx += 1) {
    const off = viewerOffsets.items + idx * ITEM_SIZE;
    if (!u8(state, off + itemOffsets.exists)) {
      continue;
    }
    items.push({
      frameNumber,
      slot: idx,
      typeId: u16(state, off + itemOffsets.type),
      state: u8(state, off + itemOffsets.state),
      facingDirection: f32(state, off + itemOffsets.direction),
      xVelocity: f32(state, off + itemOffsets.velX),
      yVelocity: f32(state, off + itemOffsets.velY),
      xPosition: f32(state, off + itemOffsets.posX),
      yPosition: f32(state, off + itemOffsets.posY),
      damageTaken: u16(state, off + itemOffsets.damage),
      expirationTimer: f32(state, off + itemOffsets.timer),
      spawnId: u32(state, off + itemOffsets.spawnId),
      samusMissileType: u8(state, off + itemOffsets.misc0),
      peachTurnipFace: u8(state, off + itemOffsets.misc1),
      isChargeShotLaunched: false,
      chargeShotChargeLevel: u8(state, off + itemOffsets.misc2),
      owner: i8(state, off + itemOffsets.owner),
    });
  }

  const stage = viewerOffsets.stage;
  const camera = viewerOffsets.camera;
  return {
    frameNumber,
    randomSeed: u32(state, viewerOffsets.randomSeed),
    players,
    items,
    stage: {
      frameNumber,
      fodLeftPlatformHeight: u8(state, stage + viewerStageOffsets.fodValid + 1)
        ? f32(state, stage + viewerStageOffsets.fodHeight + 4)
        : undefined,
      fodRightPlatformHeight: u8(state, stage + viewerStageOffsets.fodValid)
        ? f32(state, stage + viewerStageOffsets.fodHeight)
        : undefined,
      randall: u8(state, stage + viewerStageOffsets.randallExists)
        ? {
            exists: true,
            x: f32(state, stage + viewerStageOffsets.randallX),
            y: f32(state, stage + viewerStageOffsets.randallY),
          }
        : undefined,
    },
    camera: {
      eye: {
        x: f32(state, camera + viewerCameraOffsets.eyeX),
        y: f32(state, camera + viewerCameraOffsets.eyeY),
        z: f32(state, camera + viewerCameraOffsets.eyeZ),
      },
      interest: {
        x: f32(state, camera + viewerCameraOffsets.interestX),
        y: f32(state, camera + viewerCameraOffsets.interestY),
        z: f32(state, camera + viewerCameraOffsets.interestZ),
      },
      fov: f32(state, camera + viewerCameraOffsets.fov),
    },
  };
}
