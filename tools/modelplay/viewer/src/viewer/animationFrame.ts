type AnimationFrameIndexArgs = {
  animationName: string;
  internalCharacterId: number;
  animationIndex?: number;
  actionStateFrameCounter: number;
  frameCount: number;
};

const sourceFrameCountByCharAndMsid = new Map<string, number>([
  ["22:2", 240],
  ["22:3", 264],
]);

function sourceFrameCount(internalCharacterId: number, animationIndex?: number): number | undefined {
  if (animationIndex === undefined) return undefined;
  return sourceFrameCountByCharAndMsid.get(`${internalCharacterId}:${animationIndex}`);
}

export function animationFrameIndex({
  animationName,
  internalCharacterId,
  animationIndex,
  actionStateFrameCounter,
  frameCount,
}: AnimationFrameIndexArgs): number {
  if (frameCount <= 0) return 0;
  const frame = Math.floor(Math.max(0, actionStateFrameCounter));

  // Falco Wait source AObjs are longer than the bundled SVG sequence. Play the
  // available visual frames across the source timeline instead of wrapping early
  // or freezing at the end.
  //
  // data/anims/falco.tracks.bin: SSANIMT1 msid 2 end_frame=240, msid 3 end_frame=264
  // refs/melee/src/melee/ft/ftwaitanim.c::ftCo_8008A7A8
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_IsFramesRemaining
  if (animationName === "Wait1") {
    const sourceFrames = sourceFrameCount(internalCharacterId, animationIndex);
    if (sourceFrames !== undefined && sourceFrames > frameCount) {
      const sourceLast = sourceFrames - 1;
      const visualLast = frameCount - 1;
      return Math.min(visualLast, Math.floor((Math.min(frame, sourceLast) * visualLast) / sourceLast));
    }
  }

  return frame % frameCount;
}
