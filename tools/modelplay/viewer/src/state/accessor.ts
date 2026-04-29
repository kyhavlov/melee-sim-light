import { createRoot, createSignal } from "solid-js";
import { GameSettings } from "~/common/types";

import { replayStore, setLiveReplayData, setReplay, setReplayData, setRendererMode } from "~/state/replayStore";
import { spectateStore, nonReactiveState, setWsUrl } from "~/state/spectateStore";

type ReplayPointer = {
  mode: "replay",
  file: File
} | {
  mode: "replay-data",
  replayData: unknown
} | {
  mode: "live-data",
  replayData: unknown
} | {
  mode: "spectate",
  url: string
};

type ViewerMode = "replay" | "spectate";
type ViewerStateAttribute = "settings" | "ending" | "frames" | "replayFormatVersion" | "animations" | "isLoading" | "frame" | "renderDatas" | "framesPerTick" | "running" | "rendererMode" | "zoom" | "isDebug" | "isFullscreen" | "watchingLive" | "disconnected" | "currentFrame";

type API = {
  replayPointer(): ReplayPointer | null,
  setReplayPointerWrapper(p: ReplayPointer | null): void;
};

export const { replayPointer, setReplayPointerWrapper } = createRoot<API>(() => {
  const [replayPointer, setReplayPointer] = createSignal<ReplayPointer | null>(null);

  const setReplayPointerWrapper = (p: ReplayPointer | null) => {
    if (p === null) {
      setWsUrl(null);
      setRendererMode(false);
    }

    if (p?.mode === "spectate") {
      setRendererMode(false);
      setWsUrl(p.url);
    } else if (p?.mode === "replay") {
      setRendererMode(false);
      setReplay(p.file);
    } else if (p?.mode === "replay-data") {
      setReplayData(p.replayData);
    } else if (p?.mode === "live-data") {
      setLiveReplayData(p.replayData);
    }
    setReplayPointer(p);
  }

  return { replayPointer, setReplayPointerWrapper };
});

// TODO: Typing
export function access(attribute: ViewerStateAttribute): any {
  const pointerMode = replayPointer()?.mode;
  const mode: ViewerMode | undefined =
    pointerMode === "spectate" ? "spectate" : pointerMode ? "replay" : undefined;

  if (!mode) {
    return undefined;
  }

  // Computed attributes
  switch (attribute) {
    case "currentFrame":
      const frames = access("frames");
      return frames === undefined ? undefined : frames[access("frame")];
  }

  const attributeDictionary = {
    "settings": {
      "replay": () => replayStore.replayData?.settings,
      "spectate": () => spectateStore.playbackData?.settings
    },
    "ending": {
      "replay": () => replayStore.replayData?.ending,
      "spectate": () => spectateStore.playbackData?.ending
    },
    "frames": {
      "replay": () => replayStore.replayData?.frames,
      "spectate": () => nonReactiveState.gameFrames
    },
    "replayFormatVersion": {
      "replay": () => replayStore.replayData?.settings.replayFormatVersion,
      "spectate": () => nonReactiveState.replayFormatVersion
    },
    "animations": {
      "replay": () => replayStore.animations,
      "spectate": () => spectateStore.animations
    },
    "isLoading": {
      "replay": () => replayStore.isLoading,
      "spectate": () => spectateStore.isLoading
    },
    "frame": {
      "replay": () => replayStore.frame,
      "spectate": () => spectateStore.frame
    },
    "renderDatas": {
      "replay": () => replayStore.renderDatas,
      "spectate": () => spectateStore.renderDatas
    },
    "framesPerTick": {
      "replay": () => replayStore.framesPerTick,
      "spectate": () => spectateStore.framesPerTick
    },
    "running": {
      "replay": () => replayStore.running,
      "spectate": () => spectateStore.running
    },
    "rendererMode": {
      "replay": () => replayStore.rendererMode,
      "spectate": () => false
    },
    "zoom": {
      "replay": () => replayStore.zoom,
      "spectate": () => spectateStore.zoom
    },
    "isDebug": {
      "replay": () => replayStore.isDebug,
      "spectate": () => spectateStore.isDebug
    },
    "isFullscreen": {
      "replay": () => replayStore.isFullscreen,
      "spectate": () => spectateStore.isFullscreen
    },
    "watchingLive": {
      "replay": () => false,
      "spectate": () => spectateStore.watchingLive
    },
    "disconnected": {
      "replay": () => false,
      "spectate": () => spectateStore.disconnected
    }
  };

  // let modeDict: ModeDict

  // if (!attributeDictionary.hasOwnProperty(attribute)) {
  //   modeDict = {
  //     "replay": () => replayStore[attribute],
  //     "spectate": () => spectateStore[attribute]
  //   }
  // }

  return attributeDictionary[attribute][mode]();
}
