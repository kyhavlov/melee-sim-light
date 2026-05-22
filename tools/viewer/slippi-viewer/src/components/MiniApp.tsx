import { Show } from "solid-js";
import { Viewer } from "~/components/viewer/Viewer";
import { fetchAnimations } from "~/viewer/animationCache";
import "~/state/spectateStore";
import { setZipsBaseUrl } from "~/state/spectateStore";
import style from "~/css/index.css";
import muiStyle from "~/css/mui.css";
import { replayPointer } from "~/state/accessor";

export { setReplayPointerWrapper } from "~/state/accessor";

/**
 * THE VISION FOR PORTABLE VIEWER
 * - solid-element provides the ability to create a web component custom
 *   element from a solidjs component alongside rollup/babel
 *   * https://giancarlobuomprisco.com/solid/building-widgets-solidjs-web-components
 * - Custom elements can have observed attributes
 *   * https://developer.mozilla.org/en-US/docs/Web/API/Web_components/Using_custom_elements#custom_element_lifecycle_callbacks
 * - Custom elements can have custom methods
 *   * https://stackoverflow.com/a/55480022
 *
 * - Create a custom element with the following API:
 *   * const viewer = document.querySelector("slippi-viewer");
 *   * viewer.setReplay(replayData: ArrayBuffer): void
 *   * viewer.spectate(wsUrl: string): void
 *     - spectator_mode can provide a wrapper component which just takes
 *       streamid as an observed attribute if desired
 *   * viewer.clear(): void
 */

type MiniAppProps = {
  zipsBaseUrl?: string
};

export function MiniApp({ zipsBaseUrl }: MiniAppProps) {
  if (zipsBaseUrl) {
    setZipsBaseUrl(zipsBaseUrl);
  }

  // Get started fetching the most popular characters
  void fetchAnimations(20); // Falco
  void fetchAnimations(2); // Fox
  void fetchAnimations(0); // Falcon
  void fetchAnimations(9); // Marth

  return (
    <>
      <style>
        {style}
        {muiStyle}
      </style>

      <div class="flex max-h-full flex-grow flex-col gap-2 px-0">
        <Show when={Boolean(replayPointer())}>
          <Viewer />
        </Show>
      </div>
    </>
  );
}
