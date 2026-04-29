function roundInput(value) {
  return Math.round((value || 0) * 10000) / 10000;
}

function compactController(controller) {
  return [
    controller.buttons || 0,
    roundInput(controller.mainX),
    roundInput(controller.mainY),
    roundInput(controller.cX),
    roundInput(controller.cY),
    roundInput(controller.l),
    roundInput(controller.r),
  ];
}

function timestampForFilename(date = new Date()) {
  return date.toISOString().replace(/[:.]/g, "-");
}

export function traceInputFromController(frameNumber, controller) {
  return [frameNumber, compactController(controller)];
}

function compactInputs(inputTrace, frameCount) {
  const inputs = [];
  const neutral = compactController({});
  for (let frame = 0; frame <= frameCount; frame += 1) {
    inputs.push(inputTrace[frame]?.[1] || neutral);
  }
  return inputs;
}

export function saveWebplayTrace({
  replayData,
  frameCount,
  seed,
  inputTrace,
  sourceLabel,
}) {
  if (!replayData || frameCount < 0) {
    throw new Error("No webplay trace is available yet.");
  }

  const exportedAt = new Date();
  const payload = {
    format: "melee-sim-light-webplay-input-trace-v1",
    exportedAt: exportedAt.toISOString(),
    seed,
    frameCount,
    source: sourceLabel,
    settings: replayData.settings,
    ending: replayData.ending,
    inputFields: ["buttons", "mainX", "mainY", "cX", "cY", "l", "r"],
    p1Inputs: compactInputs(inputTrace, frameCount),
    p2: "neutral",
    notes: [
      "Compact webplay trace for reproducing the interactive prefix with melee-sim-light.",
      "p1Inputs is indexed by frame from 0 through frameCount; P2 is neutral.",
      "Full modelplay viewer frames are intentionally omitted to keep this artifact small.",
    ],
  };

  const blob = new Blob([JSON.stringify(payload)], {
    type: "application/json",
  });
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = `webplay_input_trace_${timestampForFilename(exportedAt)}_f${frameCount}.json`;
  document.body.appendChild(anchor);
  anchor.click();
  anchor.remove();
  URL.revokeObjectURL(url);
  return anchor.download;
}
