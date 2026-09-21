import createRAF, { targetFPS } from "@solid-primitives/raf";
import { batch, createEffect, createResource, ResourceReturn } from "solid-js";
import { createStore } from "solid-js/store";
import {
  actionNameById,
  characterNameByExternalId,
  characterNameByInternalId,
} from "~/common/ids";
import {
  Frame,
  PlayerInputs,
  PlayerSettings,
  PlayerState,
  PlayerUpdate,
  PlayerUpdateWithNana,
} from "~/common/types";
import { ReplayData } from "~/common/types";
import { parseReplay } from "~/parse/parser";
// import { currentSelectionStore } from "~/state/selectionStore";
import { CharacterAnimations, fetchAnimations } from "~/viewer/animationCache";
import { animationFrameIndex } from "~/viewer/animationFrame";
import { actionMapByInternalId } from "~/viewer/characters";
import { Character } from "~/viewer/characters/character";
import { getPlayerOnFrame, getStartOfAction } from "~/viewer/viewerUtil";
import colors from "tailwindcss/colors";
// import { fileStore } from "~/state/fileStore";
import { decode } from "@shelacek/ubjson";

export interface RenderData {
  playerState: PlayerState;
  playerInputs: PlayerInputs;
  playerSettings: PlayerSettings;

  // main render
  path?: string;
  innerColor: string;
  outerColor: string;
  transforms: string[];

  // shield/shine renders
  animationName: string;
  characterData: Character;
}

export interface ReplayStore {
  replayData?: ReplayData;
  animations: (CharacterAnimations | undefined)[];
  isLoading: boolean,
  frame: number;
  renderDatas: RenderData[];
  fps: number;
  framesPerTick: number;
  running: boolean;
  rendererMode: boolean;
  zoom: number;
  isDebug: boolean;
  isFullscreen: boolean;
}
export const defaultReplayStoreState: ReplayStore = {
  frame: 0,
  renderDatas: [],
  animations: Array(4).fill(undefined),
  isLoading: false,
  fps: 60,
  framesPerTick: 1,
  running: false,
  rendererMode: false,
  zoom: 1,
  isDebug: false,
  isFullscreen: false
};

const [replayState, setReplayState] = createStore<ReplayStore>(
  defaultReplayStoreState
);

export const replayStore = replayState;

export function speedNormal(): void {
  batch(() => {
    setReplayState("fps", 60);
    setReplayState("framesPerTick", 1);
  });
}

export function speedFast(): void {
  setReplayState("framesPerTick", 2);
}

export function speedSlow(): void {
  setReplayState("fps", 30);
}

export function zoomIn(): void {
  setReplayState("zoom", (z) => z * 1.01);
}

export function zoomOut(): void {
  setReplayState("zoom", (z) => z / 1.01);
}

export function toggleDebug(): void {
  setReplayState("isDebug", (isDebug) => !isDebug);
}

export function toggleFullscreen(): void {
  setReplayState("isFullscreen", (isFullscreen) => !isFullscreen);
}

export function togglePause(): void {
  running() ? stop() : start();
}

export function pause(): void {
  stop();
}

export function setRendererMode(enabled: boolean): void {
  setReplayState("rendererMode", enabled);
  if (enabled) {
    stop();
  }
}

export function jump(target: number): void {
  setReplayState("frame", wrapFrame(replayState, target));
}

export function setFrameData(frameNumber: number, frame: Frame): void {
  if (!replayState.replayData) return;
  batch(() => {
    setReplayState("replayData", "frames", frameNumber, frame);
    setReplayState("frame", frameNumber);
  });
}

// percent is [0,1]
export function jumpPercent(percent: number): void {
  setReplayState(
    "frame",
    Math.round((replayState.replayData?.frames.length ?? 0) * percent)
  );
}

export function adjust(delta: number): void {
  setReplayState("frame", (f) => wrapFrame(replayState, f + delta));
}

const [running, start, stop] = createRAF(
  targetFPS(
    () =>
      setReplayState("frame", (f) =>
        wrapFrame(replayState, f + replayState.framesPerTick)
      ),
    () => replayState.fps
  )
);
createEffect(() => setReplayState("running", running()));

export async function setReplay(replayFile: File): Promise<void> {
  const replayData = parseReplay(
    decode(await replayFile.arrayBuffer(), { useTypedArrays: true })
  );
  setReplayData(replayData);
}

export function setReplayData(replayData: ReplayData | unknown): void {
  batch(() => {
    setReplayState({
      replayData: replayData as ReplayData,
      frame: 0,
      renderDatas: [],
      rendererMode: false,
    });
  });
  start();
}

export function setLiveReplayData(replayData: ReplayData | unknown): void {
  batch(() => {
    setReplayState({
      replayData: replayData as ReplayData,
      frame: 0,
      renderDatas: [],
      rendererMode: true,
    });
  });
  stop();
}

const animationResources: ResourceReturn<CharacterAnimations | undefined, unknown>[] = [];
for (let playerIndex = 0; playerIndex < 4; playerIndex++) {
  animationResources.push(
    createResource(
      () => {
        const replay = replayState.replayData;
        if (replay === undefined) {
          return undefined;
        }
        const playerSettings = replay.settings.playerSettings[playerIndex];
        if (playerSettings === undefined) {
          return undefined;
        }
        const playerUpdate =
          replay.frames[replayState.frame].players[playerIndex];
        if (playerUpdate === undefined) {
          return playerSettings.externalCharacterId;
        }
        if (
          playerUpdate.state.internalCharacterId ===
          characterNameByInternalId.indexOf("Zelda")
        ) {
          return characterNameByExternalId.indexOf("Zelda");
        }
        if (
          playerUpdate.state.internalCharacterId ===
          characterNameByInternalId.indexOf("Sheik")
        ) {
          return characterNameByExternalId.indexOf("Sheik");
        }
        return playerSettings.externalCharacterId;
      },
      (id) => (id === undefined ? undefined : fetchAnimations(id))
    )
  );
}
animationResources.forEach(([dataSignal], playerIndex) =>
  createEffect(() =>
    // I can't use the obvious setReplayState("animations", playerIndex,
    // dataSignal()) because it will merge into the previous animations data
    // object, essentially overwriting the previous characters animation data
    // forever
    setReplayState("animations", (animations) => {
      const newAnimations = [...animations];
      newAnimations[playerIndex] = dataSignal();
      return newAnimations;
    })
  )
);

createEffect(() => {
  const dataSignals = animationResources.map(([dataSignal]) => dataSignal);
  setReplayState("isLoading", dataSignals.some(a => a.loading));
})

createEffect(() => {
  if (replayState.replayData === undefined) {
    return;
  }
  setReplayState(
    "renderDatas",
    replayState.replayData.frames[replayState.frame].players
      .filter((playerUpdate) => playerUpdate)
      .flatMap((playerUpdate) => {
        const animations = replayState.animations[playerUpdate.playerIndex];
        if (animations === undefined) return [];
        const renderDatas = [];
        renderDatas.push(
          computeRenderData(replayState, playerUpdate, animations, false)
        );
        if (playerUpdate.nanaState != null) {
          renderDatas.push(
            computeRenderData(replayState, playerUpdate, animations, true)
          );
        }
        return renderDatas;
      })
  );
});

function computeRenderData(
  replayState: ReplayStore,
  playerUpdate: PlayerUpdate,
  animations: CharacterAnimations,
  isNana: boolean
): RenderData {
  const playerState = (playerUpdate as PlayerUpdateWithNana)[
    isNana ? "nanaState" : "state"
  ];
  const playerInputs = (playerUpdate as PlayerUpdateWithNana)[
    isNana ? "nanaInputs" : "inputs"
  ];
  const playerSettings = replayState
    .replayData!.settings.playerSettings.filter(Boolean)
    .find((settings) => settings.playerIndex === playerUpdate.playerIndex)!;

  const startOfActionFrame = getStartOfAction(playerState);
  const startOfActionPlayerState: PlayerState = (
    getPlayerOnFrame(
      playerUpdate.playerIndex,
      startOfActionFrame,
    ) as PlayerUpdateWithNana
  )[isNana ? "nanaState" : "state"];
  const actionName = actionNameById[playerState.actionStateId];
  const characterData = actionMapByInternalId[playerState.internalCharacterId];
  const animationName =
    characterData.animationMap.get(actionName) ??
    characterData.specialsMap.get(playerState.actionStateId) ??
    actionName ??
    // Action ids past the shared table with no per-character entry (Kirby's
    // copy specials) draw nothing instead of throwing inside the render loop.
    "";
  const animationFrames = animations[animationName];
  // RebirthWait calls the common wait-animation callback, which can restart/select a wait anim
  // after the AObj ends while the action-state timer continues.
  // refs/melee/src/melee/ft/ft_0D4D.c::ftCo_RebirthWait_Anim
  // refs/melee/src/melee/ft/ftwaitanim.c::ftCo_8008A7A8
  const visualActionFrameCounter =
    actionName === "RebirthWait"
      ? playerState.frameNumber - startOfActionFrame
      : playerState.actionStateFrameCounter;
  // TODO: validate L cancels, other fractional frames, and one-indexed
  // animations. I am currently just flooring.
  const frameIndex = animationFrameIndex({
    animationName,
    internalCharacterId: playerState.internalCharacterId,
    animationIndex: playerState.animationIndex,
    actionStateFrameCounter: visualActionFrameCounter,
    animationFrames,
    loopAfterSourceEnd: actionName === "RebirthWait",
  });
  // To save animation file size, duplicate frames just reference earlier
  // matching frames such as "frame20".
  const animationPathOrFrameReference = animationFrames?.[frameIndex];
  const path =
    animationPathOrFrameReference !== undefined &&
    (animationPathOrFrameReference.startsWith("frame") ?? false)
      ? animationFrames?.[
          Number(animationPathOrFrameReference.slice("frame".length))
        ]
      : animationPathOrFrameReference;
  const rotation =
    animationName === "DamageFlyRoll"
      ? getDamageFlyRollRotation(replayState, playerState)
      : isSpacieUpB(playerState)
      ? getSpacieUpBRotation(replayState, playerState)
      : 0;
  // Some animations naturally turn the player around, but facingDirection
  // updates partway through the animation and incorrectly flips the
  // animation. The solution is to "fix" the facingDirection for the duration
  // of the action, as the animation expects. However upB turnarounds and
  // Jigglypuff/Kirby mid-air jumps are an exception where we need to flip
  // based on the updated state.facingDirection.
  const facingDirection = actionFollowsFacingDirection(animationName)
    ? playerState.facingDirection
    : startOfActionPlayerState.facingDirection;
  return {
    playerState,
    playerInputs,
    playerSettings,
    path,
    innerColor: getPlayerColor(
      replayState,
      playerUpdate.playerIndex,
      playerState.isNana
    ),
    outerColor:
      startOfActionPlayerState.lCancelStatus === "missed"
        ? "red"
        : playerState.hurtboxCollisionState !== "vulnerable"
        ? "blue"
        : "black",
    transforms: [
      `translate(${playerState.xPosition} ${playerState.yPosition})`,
      // TODO: rotate around true character center instead of current guessed
      // center of position+(0,8)
      `rotate(${rotation} 0 8)`,
      `scale(${characterData.scale} ${characterData.scale})`,
      `scale(${facingDirection} 1)`,
      "scale(.1 -.1) translate(-500 -500)",
    ],
    animationName,
    characterData,
  };
}

// DamageFlyRoll default rotation is (0,1), but we calculate rotation from (1,0)
// so we need to subtract 90 degrees. Quick checks:
// 0 - 90 = -90 which turns (0,1) into (1,0)
// -90 - 90 = -180 which turns (0,1) into (-1,0)
// Facing direction is handled naturally because the rotation will go the
// opposite direction (that scale happens first) and the flip of (0,1) is still
// (0, 1)
function getDamageFlyRollRotation(
  replayState: ReplayStore,
  playerState: PlayerState
): number {
  const previousState = (
    getPlayerOnFrame(
      playerState.playerIndex,
      playerState.frameNumber - 1
    ) as PlayerUpdateWithNana
  )[playerState.isNana ? "nanaState" : "state"];
  const deltaX = playerState.xPosition - previousState.xPosition;
  const deltaY = playerState.yPosition - previousState.yPosition;
  return (Math.atan2(deltaY, deltaX) * 180) / Math.PI - 90;
}

// Rotation will be whatever direction the player was holding at blastoff. The
// default rotation of the animation is (1,0), so we need to subtract 180 when
// facing left, and subtract 0 when facing right.
// Quick checks:
// 0 - 0 = 0, so (1,0) is unaltered when facing right
// 0 - 180 = -180, so (1,0) is flipped when facing left
function getSpacieUpBRotation(
  replayState: ReplayStore,
  playerState: PlayerState
): number {
  const velocityRotation = spacieUpBRotationFromCurrentVelocity(playerState);
  if (velocityRotation !== undefined) {
    return velocityRotation;
  }
  const startOfActionPlayer = getPlayerOnFrame(
    playerState.playerIndex,
    getStartOfAction(playerState)
  );
  const joystickDegrees =
    ((startOfActionPlayer.inputs.processed.joystickY === 0 &&
    startOfActionPlayer.inputs.processed.joystickX === 0
      ? Math.PI / 2
      : Math.atan2(
          startOfActionPlayer.inputs.processed.joystickY,
          startOfActionPlayer.inputs.processed.joystickX
        )) *
      180) /
    Math.PI;
  return (
    joystickDegrees -
    ((startOfActionPlayer as PlayerUpdateWithNana)[
      playerState.isNana ? "nanaState" : "state"
    ].facingDirection === -1
      ? 180
      : 0)
  );
}

function spacieUpBRotationFromCurrentVelocity(
  playerState: PlayerState
): number | undefined {
  const velocityX =
    (playerState.isGrounded
      ? playerState.selfInducedGroundXSpeed
      : playerState.selfInducedAirXSpeed) + playerState.attackBasedXSpeed;
  const velocityY = playerState.selfInducedAirYSpeed + playerState.attackBasedYSpeed;
  if (
    !Number.isFinite(velocityX) ||
    !Number.isFinite(velocityY) ||
    (velocityX === 0 && velocityY === 0)
  ) {
    return undefined;
  }

  // Decomp: ftFx_SpecialAirHi_Coll can rewrite facing and recompute
  // mv.fx.SpecialHi.rotateModel from current self_vel after wall/floor/ceiling contact.
  // The SVG path is mirrored by facingDirection, so compute the visible rotation in that mirrored
  // basis instead of reusing the launch-stick direction across a mid-Up-B facing flip.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
  //   ftFx_SpecialAirHi_Enter,ftFx_SpecialAirHi_Coll,ftFox_SpecialHi_RotateModel}
  const facing = playerState.facingDirection === -1 ? -1 : 1;
  return (Math.atan2(velocityY * facing, velocityX * facing) * 180) / Math.PI;
}

// All jumps and upBs either 1) Need to follow the current frame's
// facingDirection, or 2) Won't have facingDirection change during the action.
// In either case we can grab the facingDirection from the current frame.
function actionFollowsFacingDirection(animationName: string): boolean {
  return (
    animationName.includes("Jump") ||
    ["SpecialHi", "SpecialAirHi"].includes(animationName)
  );
}

function isSpacieUpB(playerState: PlayerState): boolean {
  const character = characterNameByInternalId[playerState.internalCharacterId];
  return (
    ["Fox", "Falco"].includes(character) &&
    [355, 356, 357, 358, 359].includes(playerState.actionStateId)
  );
}

export function getPlayerColor(
  replayState: ReplayStore,
  playerIndex: number,
  isNana: boolean
): string {
  if (replayState.replayData!.settings.isTeams) {
    const settings =
      replayState.replayData!.settings.playerSettings[playerIndex];
    return [
      [colors.red["800"], colors.red["600"]],
      [colors.green["800"], colors.green["600"]],
      [colors.blue["800"], colors.blue["600"]],
    ][settings.teamId][isNana ? 1 : settings.teamShade];
  }
  return [
    [colors.red["700"], colors.red["600"]],
    [colors.blue["700"], colors.blue["600"]],
    [colors.yellow["500"], colors.yellow["400"]],
    [colors.green["700"], colors.green["600"]],
  ][playerIndex][isNana ? 1 : 0];
}

function wrapFrame(replayState: ReplayStore, frame: number): number {
  if (!replayState.replayData) return frame;
  return (
    (frame + replayState.replayData.frames.length) %
    replayState.replayData.frames.length
  );
}
