export const CHAR_FOX = 1;
export const CHAR_FALCON = 2;
export const CHAR_SHEIK = 7;
export const CHAR_FALCO = 22;
export const CHAR_MARTH = 18;
export const CHAR_ZELDA = 19;
export const STAGE_FOUNTAIN_OF_DREAMS = 2;
export const STAGE_POKEMON_STADIUM = 3;
export const STAGE_YOSHIS_STORY = 8;
export const STAGE_DREAM_LAND_N64 = 28;
export const STAGE_BATTLEFIELD = 31;
export const STAGE_FINAL_DESTINATION = 32;

export const SUPPORTED_STAGES = Object.freeze([
  Object.freeze({ id: STAGE_FINAL_DESTINATION, label: "FD", name: "Final Destination" }),
  Object.freeze({ id: STAGE_BATTLEFIELD, label: "BF", name: "Battlefield" }),
  Object.freeze({ id: STAGE_FOUNTAIN_OF_DREAMS, label: "FoD", name: "Fountain of Dreams" }),
  Object.freeze({ id: STAGE_POKEMON_STADIUM, label: "PS", name: "Pokemon Stadium" }),
  Object.freeze({ id: STAGE_YOSHIS_STORY, label: "YS", name: "Yoshi's Story" }),
  Object.freeze({ id: STAGE_DREAM_LAND_N64, label: "DL", name: "Dream Land N64" }),
]);

export const SUPPORTED_CHARACTERS = Object.freeze([
  Object.freeze({ id: CHAR_FOX, label: "Fox" }),
  Object.freeze({ id: CHAR_FALCON, label: "Captain Falcon" }),
  Object.freeze({ id: CHAR_SHEIK, label: "Sheik" }),
  Object.freeze({ id: CHAR_ZELDA, label: "Zelda" }),
  Object.freeze({ id: CHAR_FALCO, label: "Falco" }),
  Object.freeze({ id: CHAR_MARTH, label: "Marth" }),
]);

export const MATCH_CONFIG_SIZE = 36;
export const INPUT_SIZE = 32;
export const COMPARE_SIZE = 1022;
export const STAGE_STATE_SIZE = 38;
export const SHIELD_BUBBLES_SIZE = 64;
export const MAX_PLAYERS = 4;
export const MAX_HITBOXES = 4;
export const HITBOX_SIZE = 40;
export const HITBOX_PLAYER_SIZE = 160;
export const HITBOXES_SIZE = 640;
export const ITEM_SIZE = 48;

export const BUTTONS = {
  A: 0x0100,
  B: 0x0200,
  X: 0x0400,
  Y: 0x0800,
  Z: 0x0010,
  L: 0x0040,
  R: 0x0020,
  START: 0x1000,
  D_LEFT: 0x0001,
  D_RIGHT: 0x0002,
  D_DOWN: 0x0004,
  D_UP: 0x0008,
};

export const matchConfigOffsets = {
  stageId: 0,
  frameId: 4,
  randomSeed: 8,
  damageRatio: 12,
  numPlayers: 16,
  isTeams: 17,
  stockCount: 18,
  players: 20,
  playerSize: 4,
};

export const inputPlayerOffsets = {
  buttons: 0,
  mainX: 2,
  mainY: 3,
  cX: 4,
  cY: 5,
  l: 6,
  r: 7,
  size: 8,
};

export const compareOffsets = {
  frameId: 0,
  randomSeed: 4,
  stageId: 8,
  numPlayers: 12,
  isTeams: 13,
  teamId: 16,
  charId: 20,
  posX: 24,
  posY: 40,
  speedAirXSelf: 56,
  speedGroundXSelf: 72,
  speedYSelf: 88,
  speedXAttack: 104,
  speedYAttack: 120,
  facing: 136,
  onGround: 140,
  isDead: 144,
  actionId: 149,
  actionFrame: 157,
  jumpsLeft: 165,
  stocks: 169,
  percent: 173,
  shieldHp: 189,
  hitlag: 205,
  hitstun: 213,
  hurtboxState: 225,
  animationIndex: 237,
  stateFlags: 282,
  items: 302,
};

export const itemOffsets = {
  exists: 0,
  state: 1,
  type: 2,
  owner: 4,
  direction: 12,
  velX: 16,
  velY: 20,
  posX: 24,
  posY: 28,
  damage: 32,
  timer: 36,
  spawnId: 40,
  misc0: 44,
  misc1: 45,
  misc2: 46,
};

export const hitboxOffsets = {
  x: 0,
  y: 4,
  z: 8,
  radius: 12,
  damage: 16,
  u16_0: 20,
  u16_1: 24,
  u16_3: 28,
  bonePartId: 32,
  enabled: 36,
};

export const stageStateOffsets = {
  fodHeight: 0,
  fodValid: 8,
  fodSource: 10,
  fodSchedulerPhase: 12,
  fodSchedulerValid: 14,
  fodSchedulerTimer: 16,
  fodSchedulerTarget: 20,
  randallExists: 28,
  randallX: 30,
  randallY: 34,
};
