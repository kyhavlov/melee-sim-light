import { customElement } from "solid-element";
import { MiniApp, setReplayPointerWrapper } from "~/components/MiniApp";
import { jump, pause, setFrameData } from "~/state/replayStore";
import { Frame } from "~/common/types";

interface HTMLSlippiViewer extends HTMLElement {
  setReplay(replayFile: File): void;
  setReplayData(replayData: unknown): void;
  setLiveReplayData(replayData: unknown): void;
  setFrame(frame: number): void;
  setFrameData(frameNumber: number, frame: Frame): void;
  pausePlayback(): void;
  spectate(wsUrl: string): void;
  clear(): void;
}

customElement("slippi-viewer", { zipsBaseUrl: "/" },
  (props, { element }) => {
    // The @font-face rule used in the Material Icons CSS must be declared
    // in the main document.
    // https://stackoverflow.com/a/60526280
    element.innerHTML = '<link href="https://fonts.googleapis.com/icon?family=Material+Icons|Material+Icons+Outlined" rel="stylesheet" />';

    element.setReplay = (file: File) => {
      setReplayPointerWrapper({ mode: "replay", file });
    };
    element.setReplayData = (replayData: unknown) => {
      setReplayPointerWrapper({ mode: "replay-data", replayData });
    };
    element.setLiveReplayData = (replayData: unknown) => {
      setReplayPointerWrapper({ mode: "live-data", replayData });
    };
    element.setFrame = (frame: number) => {
      jump(frame);
    };
    element.setFrameData = (frameNumber: number, frame: Frame) => {
      setFrameData(frameNumber, frame);
    };
    element.pausePlayback = () => {
      pause();
    };
    element.spectate = (url: string) => {
      setReplayPointerWrapper({ mode: "spectate", url });
    };
    element.clear = () => {
      setReplayPointerWrapper(null);
    };
    return (<MiniApp zipsBaseUrl={props.zipsBaseUrl} /> as HTMLSlippiViewer);
  });
