import {
  PlayerState,
  PlayerUpdate,
  PlayerUpdateWithNana
} from "~/common/types";
import { access } from "~/state/accessor";

export function getStartOfAction(
  playerState: PlayerState
): number {
  let earliestStateOfAction = (
    getPlayerOnFrame(
      playerState.playerIndex,
      playerState.frameNumber
    ) as PlayerUpdateWithNana
  )[playerState.isNana ? "nanaState" : "state"];
  while (true) {
    const testEarlierState = getPlayerOnFrame(
      playerState.playerIndex,
      earliestStateOfAction.frameNumber - 1
    )?.[playerState.isNana ? "nanaState" : "state"];
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
