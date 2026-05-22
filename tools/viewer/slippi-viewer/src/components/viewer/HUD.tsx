import { createMemo } from "solid-js";
import { For } from "solid-js/web";
import { PlayerSettings } from "~/common/types";
import { LiveIcon } from "~/components/common/icons";
import { PlayerHUD } from "~/components/viewer/PlayerHUD";
import { Timer } from "~/components/viewer/Timer";
import { access } from "~/state/accessor";

export function HUD() {
  const playerIndexes = createMemo(() =>
    access("settings").playerSettings.filter(Boolean)
      .map((playerSettings: PlayerSettings) => playerSettings.playerIndex)
  );
  return (
    <>
      <Timer />
      <For each={playerIndexes()}>
        {(playerIndex) => <PlayerHUD player={playerIndex} />}
      </For>
    </>
  );
}
