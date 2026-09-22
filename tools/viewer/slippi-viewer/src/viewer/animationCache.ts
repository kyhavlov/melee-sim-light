import { createSignal } from "solid-js";
import { unzipSync, strFromU8 } from "fflate";
import { zipsBaseUrl } from "~/state/spectateStore";

export type AnimationFrames = string[];
export interface CharacterAnimations {
  [animationName: string]: AnimationFrames;
}

const animationsCache = new Map<number, CharacterAnimations>();

export const fetchAnimations = async (
  externalCharacterId: number
): Promise<CharacterAnimations> => {
  if (animationsCache.has(externalCharacterId)) {
    return animationsCache.get(externalCharacterId) as CharacterAnimations;
  }

  let zipUrl = zipsBaseUrl();
  if (!zipUrl.endsWith("/")) zipUrl += "/";
  zipUrl += characterZipUrlByExternalId[externalCharacterId];

  const animations = await load(zipUrl);
  animationsCache.set(externalCharacterId, animations);
  return animations;
};

const characterZipUrlByExternalId = [
  "zips/captainFalcon.zip",
  "zips/donkeyKong.zip",
  "zips/fox.zip",
  "zips/mrGameAndWatch.zip",
  "zips/kirby.zip",
  "zips/bowser.zip",
  "zips/link.zip",
  "zips/luigi.zip",
  "zips/mario.zip",
  "zips/marth.zip",
  "zips/mewtwo.zip",
  "zips/ness.zip",
  "zips/peach.zip",
  "zips/pikachu.zip",
  "zips/iceClimbers.zip",
  "zips/jigglypuff.zip",
  "zips/samus.zip",
  "zips/yoshi.zip",
  "zips/zelda.zip",
  "zips/sheik.zip",
  "zips/falco.zip",
  "zips/youngLink.zip",
  "zips/doctorMario.zip",
  "zips/roy.zip",
  "zips/pichu.zip",
  "zips/ganondorf.zip",
];

async function load(url: string): Promise<CharacterAnimations> {
  const response = await fetch(url);
  const animationsZip = await response.blob();
  const fileBuffers = unzipSync(
    new Uint8Array(await animationsZip.arrayBuffer())
  );
  return Object.fromEntries(
    Object.entries(fileBuffers).map(([name, buffer]) => [
      name.replace(".json", ""),
      JSON.parse(strFromU8(buffer)),
    ])
  );
}


// Kirby's copy hats, baked as overlays by tools/viewer/bake (one zip per
// copied fighter kind, same animation names and frame indices as kirby.zip).
// Loaded on first sight of a hat; the version signal lets render effects
// re-run once a zip arrives.
const kirbyHatZipByKind: Record<number, string> = {
  0: "zips/kirbyHatMr.zip", 1: "zips/kirbyHatFx.zip", 2: "zips/kirbyHatCa.zip",
  3: "zips/kirbyHatDk.zip", 5: "zips/kirbyHatKp.zip", 6: "zips/kirbyHatLk.zip",
  7: "zips/kirbyHatSk.zip", 8: "zips/kirbyHatNs.zip", 9: "zips/kirbyHatPe.zip",
  10: "zips/kirbyHatPp.zip", 12: "zips/kirbyHatPk.zip", 13: "zips/kirbyHatSs.zip",
  14: "zips/kirbyHatYs.zip", 17: "zips/kirbyHatLg.zip", 18: "zips/kirbyHatMs.zip",
  19: "zips/kirbyHatZd.zip", 20: "zips/kirbyHatCl.zip", 21: "zips/kirbyHatDr.zip",
  23: "zips/kirbyHatPc.zip", 24: "zips/kirbyHatGw.zip", 25: "zips/kirbyHatGn.zip",
  26: "zips/kirbyHatFe.zip",
};
const kirbyHatCache = new Map<number, CharacterAnimations | null>();
const [kirbyHatVersion, setKirbyHatVersion] = createSignal(0);

export function getKirbyHatAnimations(kind: number): CharacterAnimations | undefined {
  kirbyHatVersion();
  const cached = kirbyHatCache.get(kind);
  if (cached !== undefined) return cached ?? undefined;
  const name = kirbyHatZipByKind[kind];
  if (name === undefined) return undefined;
  kirbyHatCache.set(kind, null);
  let zipUrl = zipsBaseUrl();
  if (!zipUrl.endsWith("/")) zipUrl += "/";
  load(zipUrl + name)
    .then((animations) => {
      kirbyHatCache.set(kind, animations);
      setKirbyHatVersion((v) => v + 1);
    })
    .catch(() => {
      // Missing overlay (unbaked hat): keep the null so we stop retrying.
    });
  return undefined;
}
