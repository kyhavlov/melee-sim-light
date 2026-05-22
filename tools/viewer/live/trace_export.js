import { replayDataToMslTrace } from "../msltrace1.js";

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

function sanitizeFilenamePart(value) {
  return String(value || "")
    .trim()
    .replace(/\s+/g, "_")
    .replace(/[^A-Za-z0-9._-]+/g, "_")
    .replace(/_+/g, "_")
    .replace(/^[._-]+|[._-]+$/g, "")
    .slice(0, 80);
}

export function traceInputFromControllers(frameNumber, controllers) {
  return [
    frameNumber,
    compactController(controllers?.[0]),
    compactController(controllers?.[1]),
  ];
}

export function saveLiveTrace({
  replayData,
  frameCount,
  seed,
  inputTrace,
  sourceLabel,
  traceName = "",
}) {
  if (!replayData || frameCount < 0) {
    throw new Error("No live trace is available yet.");
  }

  const exportedAt = new Date();
  const trimmedTraceName = String(traceName || "").trim();
  const safeTraceName = sanitizeFilenamePart(trimmedTraceName);
  const outputName =
    safeTraceName.replace(/(?:\.msltrace)?\.json$/i, "") ||
    `live_trace_${timestampForFilename(exportedAt)}_f${frameCount}`;
  const payload = replayDataToMslTrace({
    replayData,
    frameCount,
    inputTrace,
    producer: {
      name: "melee-sim-light live viewer",
      version: null,
    },
    metadata: {
      provenance: {
        source: sourceLabel,
        seed,
        exportedAt: exportedAt.toISOString(),
        name: trimmedTraceName || null,
      },
    },
  });

  const blob = new Blob([JSON.stringify(payload)], {
    type: "application/json",
  });
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = `${outputName}.msltrace.json`;
  document.body.appendChild(anchor);
  anchor.click();
  anchor.remove();
  URL.revokeObjectURL(url);
  return anchor.download;
}
