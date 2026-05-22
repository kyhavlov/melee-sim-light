import {
  PlayerState,
  PlayerUpdate,
  PlayerUpdateWithNana
} from "~/common/types";
import { access } from "~/state/accessor";

function stateForPlayerUpdate(
  playerUpdate: PlayerUpdate | undefined,
  isNana: boolean
): PlayerState | undefined {
  return (playerUpdate as PlayerUpdateWithNana | undefined)?.[
    isNana ? "nanaState" : "state"
  ];
}

function isActionStartBoundary(
  playerState: PlayerState,
  candidateState: PlayerState
): boolean {
  const previousState = stateForPlayerUpdate(
    getPlayerOnFrame(playerState.playerIndex, candidateState.frameNumber - 1),
    playerState.isNana
  );
  return (
    previousState === undefined ||
    previousState.actionStateId !== candidateState.actionStateId ||
    previousState.actionStateFrameCounter > candidateState.actionStateFrameCounter
  );
}

function getStartOfActionFromFrameCounter(
  playerState: PlayerState
): number | undefined {
  const actionFrame = playerState.actionStateFrameCounter;
  if (!Number.isFinite(actionFrame) || actionFrame < 0) return undefined;

  const estimatedStart = playerState.frameNumber - Math.floor(actionFrame);
  for (let delta = -2; delta <= 2; delta += 1) {
    const candidateFrame = estimatedStart + delta;
    if (candidateFrame > playerState.frameNumber) continue;
    const candidateState = stateForPlayerUpdate(
      getPlayerOnFrame(playerState.playerIndex, candidateFrame),
      playerState.isNana
    );
    if (
      candidateState !== undefined &&
      candidateState.actionStateId === playerState.actionStateId &&
      isActionStartBoundary(playerState, candidateState)
    ) {
      return candidateFrame;
    }
  }
  return undefined;
}

export function getStartOfAction(
  playerState: PlayerState
): number {
  const fastStart = getStartOfActionFromFrameCounter(playerState);
  if (fastStart !== undefined) return fastStart;

  let earliestStateOfAction = stateForPlayerUpdate(
    getPlayerOnFrame(playerState.playerIndex, playerState.frameNumber),
    playerState.isNana
  ) as PlayerState;
  while (true) {
    const testEarlierState = stateForPlayerUpdate(
      getPlayerOnFrame(playerState.playerIndex, earliestStateOfAction.frameNumber - 1),
      playerState.isNana
    );
    if (
      testEarlierState === undefined ||
      testEarlierState.actionStateId !== earliestStateOfAction.actionStateId ||
      testEarlierState.actionStateFrameCounter >
        earliestStateOfAction.actionStateFrameCounter
    ) {
      return earliestStateOfAction.frameNumber;
    }
    earliestStateOfAction = testEarlierState;
  }
}

export function getPlayerOnFrame(
  playerIndex: number,
  frameNumber: number,
): PlayerUpdate {
  return access("frames")[frameNumber]?.players[playerIndex];
}
