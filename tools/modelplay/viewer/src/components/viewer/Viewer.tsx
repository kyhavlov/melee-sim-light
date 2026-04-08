import { createMemo, For, Show } from "solid-js";
import { Camera } from "~/components/viewer/Camera";
import { HUD } from "~/components/viewer/HUD";
import { Players } from "~/components/viewer/Player";
import { Stage } from "~/components/viewer/Stage";
import { Item } from "~/components/viewer/Item";
import { SpectateControls } from "~/components/viewer/SpectateControls";
import { Controls } from "~/components/viewer/Controls";
import { LiveIcon, ReconnectingText } from "~/components/common/icons";
import { nonReactiveState, spectateStore } from "~/state/spectateStore";
import { access, replayPointer } from "~/state/accessor";

export function Viewer() {
  const items = createMemo(
    () => access("currentFrame")?.items ?? []
  );
  const showState = () => {
    console.log('spectateStore', spectateStore);
    console.log('nonReactiveState', nonReactiveState);
  };
  return (
    <>
      <Show when={access("isDebug")}>
        <button onClick={showState}>Debug</button>
        <div>
          <div>watchingLive: {String(access("watchingLive"))}</div>
          <div>Number of frames behind: {access("frames").length - access("frame")}</div>
        </div>
      </Show>

      <div class="flex flex-col overflow-y-auto relative">
        <Show
          when={access("settings") && access("frames").length > access("frame")} // this is really spectate-only behavior
          fallback={<div class="flex justify-center italic">{access("disconnected") ? "Reconnecting..." : "Waiting for game..."}</div>}
        >
          <Show
            when={access("disconnected")}
            fallback={
              <Show when={access("watchingLive")}>
                <LiveIcon title="Live" class="absolute top-4 left-4 w-12" />
              </Show>
            }
          >
            <ReconnectingText title="Reconnecting" class="absolute top-1/2 left-1/2 -translate-x-1/2 -translate-y-1/2 fill-red-500 stroke-red-600" />
          </Show>

          <Show when={!access("isLoading")} fallback={<div class="flex justify-center italic">Loading...</div>}>
            <svg class="rounded-t border bg-slate-50" viewBox="-365 -300 730 600">
              {/* up = positive y axis */}
              <g class="-scale-y-100">
                <Camera>
                  <Stage />
                  <Players />
                  <For each={items()}>{(item) => <Item item={item} />}</For>
                </Camera>
                <HUD />
              </g>
            </svg>
            {replayPointer()?.mode === "spectate" ? <SpectateControls /> : <Controls />}
          </Show>
        </Show>
      </div>
    </>
  );
}
