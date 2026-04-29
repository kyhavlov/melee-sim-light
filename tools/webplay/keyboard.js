import { BUTTONS } from "./schema.js";

const held = new Set();

const gameplayKeys = new Set([
  "w",
  "a",
  "s",
  "d",
  "t",
  "g",
  "f",
  "h",
  "x",
  "c",
  "v",
  "b",
  "n",
  "q",
  "e",
  "r",
]);

export function installKeyboard(resetCallback) {
  window.addEventListener(
    "keydown",
    (event) => {
      const key = event.key.toLowerCase();
      if (!gameplayKeys.has(key)) {
        return;
      }
      event.preventDefault();
      event.stopImmediatePropagation();
      if (key === "r") {
        resetCallback();
        return;
      }
      held.add(key);
    },
    true
  );

  window.addEventListener(
    "keyup",
    (event) => {
      const key = event.key.toLowerCase();
      if (!gameplayKeys.has(key)) {
        return;
      }
      event.preventDefault();
      event.stopImmediatePropagation();
      held.delete(key);
    },
    true
  );
}

function axis(negativeKey, positiveKey) {
  const negative = held.has(negativeKey) ? -1 : 0;
  const positive = held.has(positiveKey) ? 1 : 0;
  return negative + positive;
}

export function readKeyboardController() {
  let buttons = 0;
  if (held.has("x")) buttons |= BUTTONS.A;
  if (held.has("c")) buttons |= BUTTONS.B;
  if (held.has("v")) buttons |= BUTTONS.X;
  if (held.has("b")) buttons |= BUTTONS.Y;
  if (held.has("n")) buttons |= BUTTONS.Z;
  if (held.has("q")) buttons |= BUTTONS.L;
  if (held.has("e")) buttons |= BUTTONS.R;

  return {
    buttons,
    mainX: axis("a", "d"),
    mainY: axis("s", "w"),
    cX: axis("f", "h"),
    cY: axis("g", "t"),
    l: held.has("q") ? 1 : 0,
    r: held.has("e") ? 1 : 0,
  };
}
