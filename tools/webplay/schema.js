export const CHAR_FOX = 1;
export const CHAR_FALCO = 22;
export const STAGE_FINAL_DESTINATION = 32;

export const MATCH_CONFIG_SIZE = 36;
export const INPUT_SIZE = 32;
export const COMPARE_SIZE = 1022;
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
