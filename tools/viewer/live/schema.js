export * from "./schema.generated.js";

export const CHAR_FOX = 1;
export const CHAR_FALCON = 2;
export const CHAR_SHEIK = 7;
export const CHAR_PEACH = 9;
export const CHAR_JIGGLYPUFF = 15;
export const CHAR_LUIGI = 17;
export const CHAR_MARIO = 0;
export const CHAR_DRMARIO = 21;
export const CHAR_SAMUS = 13;
export const CHAR_MARTH = 18;
export const CHAR_ZELDA = 19;
export const CHAR_FALCO = 22;

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
  Object.freeze({ id: CHAR_PEACH, label: "Peach" }),
  Object.freeze({ id: CHAR_JIGGLYPUFF, label: "Jigglypuff" }),
  Object.freeze({ id: CHAR_LUIGI, label: "Luigi" }),
  Object.freeze({ id: CHAR_MARIO, label: "Mario" }),
  Object.freeze({ id: CHAR_DRMARIO, label: "Dr. Mario" }),
  Object.freeze({ id: CHAR_SAMUS, label: "Samus" }),
  Object.freeze({ id: CHAR_MARTH, label: "Marth" }),
  Object.freeze({ id: CHAR_ZELDA, label: "Zelda" }),
  Object.freeze({ id: CHAR_FALCO, label: "Falco" }),
]);

export const BUTTONS = Object.freeze({
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
});
