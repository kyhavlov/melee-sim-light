import { createRoot, createSignal } from "solid-js";

// Viewer display preferences that are independent of replay/spectate mode.
// Hitbox circles default off; the live page persists the choice per browser.
export const { showHitboxes, setShowHitboxes, toggleHitboxes } = createRoot(
  () => {
    const [showHitboxes, setShowHitboxes] = createSignal(false);
    const toggleHitboxes = () => setShowHitboxes((value) => !value);
    return { showHitboxes, setShowHitboxes, toggleHitboxes };
  }
);
