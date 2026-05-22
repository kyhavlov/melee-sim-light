import ReconnectingWebSocket from "reconnecting-websocket";
import { CommandPayloadSizes } from "~/common/types";
import { parsePacket } from "./liveParser";

export type WorkerState = {
  /**
   * The version of the .slp spec that was used when the file was created. Some
   * fields are only present after certain versions.
   */
  replayFormatVersion?: string,
  payloadSizes?: CommandPayloadSizes
};

/**
 * Message schemas
 *
 * Inputs (event.data):
 * - { type: "connect", value: <WebSocket url string> }
 * - disconnect?
 *
 * Outputs:
 * - { type: "game_data", value: GameEvent[] }
 */

type WorkerInput = { type: "connect", value: string };

const workerState: WorkerState = {
  replayFormatVersion: undefined,
  payloadSizes: undefined
};

onmessage = (event: MessageEvent<WorkerInput>) => {
  switch (event.data.type) {
    case "connect":
      connectWS(event.data.value);
      break;
  }
};

function connectWS(wsUrl: string) {
  console.log("Connecting to stream:", wsUrl);
  const ws = new ReconnectingWebSocket(wsUrl);
  ws.binaryType = "arraybuffer";
  console.log("Connection successful.");

  ws.onmessage = (msg) => {
    handleGameData(msg.data);
  };

  ws.onopen = () => {
    postMessage({ type: "connected", value: null });
    console.log("WebSocket opened");
  }

  ws.onerror = (err) => {
    postMessage({ type: "disconnected", value: "error" });
    console.error("WebSocket error:", err);
  };

  ws.onclose = (msg) => {
    postMessage({ type: "disconnected", value: "closed" });
    console.log("WebSocket closed:", msg);
  };
}

function handleGameData(payload: ArrayBuffer) {
  const gameEvents = parsePacket(
    new Uint8Array(payload),
    workerState
  );
  postMessage({ type: "game_data", value: gameEvents });
}

export default "";
