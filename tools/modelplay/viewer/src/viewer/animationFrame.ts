type AnimationFrameIndexArgs = {
  animationName: string;
  internalCharacterId: number;
  animationIndex?: number;
  actionStateFrameCounter: number;
  animationFrames?: string[];
  loopAfterSourceEnd?: boolean;
};

const sourceFrameCountByCharAndMsid = new Map<string, number>([
  ["1:2", 120],
  ["1:3", 120],
  ["22:2", 240],
  ["22:3", 264],
]);

function sourceFrameCount(internalCharacterId: number, animationIndex?: number): number | undefined {
  if (animationIndex === undefined) return undefined;
  return sourceFrameCountByCharAndMsid.get(`${internalCharacterId}:${animationIndex}`);
}

function visualFrameCount(animationName: string, animationFrames?: string[]): number {
  if (animationFrames === undefined || animationFrames.length === 0) return 1;

  // Fox Wait1 is packed as 120 unique visual frames followed by frame0 references
  // in tools/modelplay/viewer/public/zips/fox.zip. The source AObj is 120 frames
  // (data/anims/fox.tracks.bin msid 2/3), so using the raw bundled length makes
  // long Wait/RebirthWait renders freeze on frame0 for frames 120..200.
  if (animationName === "Wait1") {
    let n = animationFrames.length;
    while (n > 1 && animationFrames[n - 1] === "frame0") n -= 1;
    return n;
  }

  return animationFrames.length;
}

export function animationFrameIndex({
  animationName,
  internalCharacterId,
  animationIndex,
  actionStateFrameCounter,
  animationFrames,
  loopAfterSourceEnd = false,
}: AnimationFrameIndexArgs): number {
  const frameCount = visualFrameCount(animationName, animationFrames);
  if (frameCount <= 0) return 0;
  const frame = Math.floor(Math.max(0, actionStateFrameCounter));

  // Wait source AObj length is authoritative. Falco's source AObjs are longer
  // than the bundled SVG sequence, so play the available visual frames across
  // the source timeline instead of wrapping early or freezing at the end.
  //
  // data/anims/fox.tracks.bin: SSANIMT1 msid 2/3 end_frame=120
  // data/anims/falco.tracks.bin: SSANIMT1 msid 2 end_frame=240, msid 3 end_frame=264
  // refs/melee/src/melee/ft/ftwaitanim.c::ftCo_8008A7A8
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_IsFramesRemaining
  if (animationName === "Wait1") {
    const sourceFrames = sourceFrameCount(internalCharacterId, animationIndex);
    if (sourceFrames !== undefined) {
      const sourceLast = sourceFrames - 1;
      const visualLast = frameCount - 1;
      const sourceFrame = loopAfterSourceEnd ? frame % sourceFrames : Math.min(frame, sourceLast);
      return Math.min(visualLast, Math.floor((sourceFrame * visualLast) / sourceLast));
    }
  }

  return frame % frameCount;
}
