import { createMemo, For, Match, Show, Switch } from "solid-js";
import { itemNamesById } from "~/common/ids";
import {
  HitboxUpdate,
  ItemUpdate,
  PlayerUpdate,
  NonReactiveState,
} from "~/common/types";
import { access } from "~/state/accessor";
import { showHitboxes } from "~/state/displayStore";

// TODO: characters projectiles

// Item kinds matched by id: the Slippi name table has no name for Disable and
// Oil Panic, and the Game & Watch names are ambiguous. Ids follow
// src/runtime/item_projection.h.
const ITEM_DRMARIO_VITAMIN = 49;
const ITEM_LINK_HOOKSHOT = 62;
const ITEM_CLINK_HOOKSHOT = 63;
const ITEM_NESS_PKFIRE = 66;
const ITEM_NESS_PKFIRE_PILLAR = 67;
const ITEM_NESS_PKFLASH = 68;
const ITEM_NESS_PKTHUNDER = 69;
const ITEM_NESS_PKTHUNDER_TRAIL_FIRST = 70;
const ITEM_NESS_PKTHUNDER_TRAIL_LAST = 73;
const ITEM_NESS_PKFLASH_EXPLODE = 78;
const ITEM_PIKACHU_THUNDER = 81;
const ITEM_MARIO_CAPE = 83;
const ITEM_DRMARIO_SHEET = 84;
const ITEM_SHEIK_VANISH = 85;
const ITEM_YOSHI_EGG_LAY = 87;
const ITEM_PIKACHU_TJOLT_GROUND = 89;
const ITEM_PIKACHU_TJOLT_AIR = 90;
const ITEM_SAMUS_GRAPPLE = 96;
const ITEM_BOWSER_FLAME = 100;
const ITEM_PEACH_PARASOL = 103;
const ITEM_ICECLIMBER_ICE = 106;
const ITEM_ICECLIMBER_BLIZZARD = 107;
const ITEM_YOSHI_STAR = 88;
const ITEM_BOBOMB = 6;
const ITEM_MR_SATURN = 7;
const ITEM_BEAM_SWORD = 12;
const ITEM_ZELDA_DIN_FIRE = 108;
const ITEM_ZELDA_DIN_FIRE_EXPLODE = 109;
const ITEM_MEWTWO_DISABLE = 110;
const ITEM_MEWTWO_SHADOW_BALL = 112;
const ITEM_GAMEWATCH_GREENHOUSE = 114;
const ITEM_GAMEWATCH_MANHOLE = 115;
const ITEM_GAMEWATCH_FIRE = 116;
const ITEM_GAMEWATCH_PARACHUTE = 117;
const ITEM_GAMEWATCH_JUDGE = 120;
const ITEM_GAMEWATCH_PANIC = 121;
const ITEM_GAMEWATCH_CHEF = 122;
const ITEM_GAMEWATCH_RESCUE = 124;

// Game & Watch SpecialS1..SpecialS9 (ground) and SpecialAirS1..9 action ids.
const GAMEWATCH_JUDGE_GROUND_FIRST = 355;
const GAMEWATCH_JUDGE_AIR_FIRST = 364;

// Note: Most items coordinates and sizes are divided by 256 to convert them
// from hitboxspace to worldspace.
export function Item(props: { item: ItemUpdate }) {
  const itemName = createMemo(() => itemNamesById[props.item.typeId]);
  return (
    <>
      <ItemHitbox item={props.item} />
      <Switch>
      <Match when={itemName() === "Needle(thrown)"}>
        <Needle item={props.item} />
      </Match>
      <Match when={itemName() === "Sheik's chain"}>
        <SheikChain item={props.item} />
      </Match>
      <Match when={itemName() === "Fox's Laser"}>
        <FoxLaser item={props.item} />
      </Match>
      <Match when={itemName() === "Falco's Laser"}>
        <FalcoLaser item={props.item} />
      </Match>
      <Match when={itemName() === "Turnip"}>
        <Turnip item={props.item} />
      </Match>
      <Match when={itemName() === "Yoshi's egg(thrown)"}>
        <YoshiEgg item={props.item} />
      </Match>
      <Match when={itemName() === "Luigi's fire"}>
        <LuigiFireball item={props.item} />
      </Match>
      <Match when={itemName() === "Mario's fire"}>
        <MarioFireball item={props.item} />
      </Match>
      <Match when={itemName() === "Missile"}>
        <Missile item={props.item} />
      </Match>
      <Match when={itemName() === "Samus's bomb"}>
        <SamusBomb item={props.item} />
      </Match>
      <Match when={itemName() === "Samus's chargeshot"}>
        <SamusChargeshot item={props.item} />
      </Match>
      <Match when={itemName() === "Shyguy (Heiho)"}>
        <FlyGuy item={props.item} />
      </Match>
      <Match
        when={
          itemName() === "Link's bomb" || itemName() === "Young Link's bomb"
        }
      >
        <LinkBomb item={props.item} />
      </Match>
      <Match
        when={
          itemName() === "Link's boomerang" ||
          itemName() === "Young Link's boomerang"
        }
      >
        <LinkBoomerang item={props.item} />
      </Match>
      <Match when={itemName() === "Arrow" || itemName() === "Fire Arrow"}>
        <LinkArrow item={props.item} />
      </Match>
      <Match when={props.item.typeId === ITEM_DRMARIO_VITAMIN}>
        <DrMarioVitamin item={props.item} />
      </Match>
      <Match
        when={
          props.item.typeId === ITEM_MARIO_CAPE ||
          props.item.typeId === ITEM_DRMARIO_SHEET
        }
      >
        <MarioCape item={props.item} />
      </Match>
      <Match
        when={
          props.item.typeId === ITEM_LINK_HOOKSHOT ||
          props.item.typeId === ITEM_CLINK_HOOKSHOT ||
          props.item.typeId === ITEM_SAMUS_GRAPPLE
        }
      >
        <Grapple item={props.item} />
      </Match>
      <Match when={props.item.typeId === ITEM_NESS_PKFIRE}>
        <NessPkFire item={props.item} />
      </Match>
      <Match when={props.item.typeId === ITEM_NESS_PKFIRE_PILLAR}>
        <NessPkFirePillar item={props.item} />
      </Match>
      <Match when={props.item.typeId === ITEM_NESS_PKFLASH}>
        <NessPkFlash item={props.item} />
      </Match>
      <Match when={props.item.typeId === ITEM_NESS_PKFLASH_EXPLODE}>
        <NessPkFlashExplode item={props.item} />
      </Match>
      <Match
        when={
          props.item.typeId === ITEM_NESS_PKTHUNDER ||
          (props.item.typeId >= ITEM_NESS_PKTHUNDER_TRAIL_FIRST &&
            props.item.typeId <= ITEM_NESS_PKTHUNDER_TRAIL_LAST)
        }
      >
        <NessPkThunder item={props.item} />
      </Match>
      <Match when={props.item.typeId === ITEM_PIKACHU_THUNDER}>
        <PikachuThunder item={props.item} />
      </Match>
      <Match
        when={
          props.item.typeId === ITEM_PIKACHU_TJOLT_GROUND ||
          props.item.typeId === ITEM_PIKACHU_TJOLT_AIR
        }
      >
        <PikachuThunderJolt item={props.item} />
      </Match>
      <Match when={props.item.typeId === ITEM_SHEIK_VANISH}>
        <SheikVanish item={props.item} />
      </Match>
      <Match when={props.item.typeId === ITEM_YOSHI_EGG_LAY}>
        <YoshiEggLay item={props.item} />
      </Match>
      <Match when={props.item.typeId === ITEM_BOWSER_FLAME}>
        <BowserFlame item={props.item} />
      </Match>
      <Match when={props.item.typeId === ITEM_PEACH_PARASOL}>
        <PeachParasol item={props.item} />
      </Match>
      <Match when={props.item.typeId === ITEM_ICECLIMBER_ICE}>
        <IceClimberIce item={props.item} />
      </Match>
      <Match when={props.item.typeId === ITEM_ICECLIMBER_BLIZZARD}>
        <IceClimberBlizzard item={props.item} />
      </Match>
      <Match when={props.item.typeId === ITEM_ZELDA_DIN_FIRE}>
        <ZeldaDinFire item={props.item} />
      </Match>
      <Match when={props.item.typeId === ITEM_ZELDA_DIN_FIRE_EXPLODE}>
        <ZeldaDinFireExplode item={props.item} />
      </Match>
      <Match when={props.item.typeId === ITEM_BOBOMB}>
        <Bobomb item={props.item} />
      </Match>
      <Match when={props.item.typeId === ITEM_MR_SATURN}>
        <MrSaturn item={props.item} />
      </Match>
      <Match when={props.item.typeId === ITEM_BEAM_SWORD}>
        <BeamSword item={props.item} />
      </Match>
      <Match when={props.item.typeId === ITEM_MEWTWO_DISABLE}>
        <MewtwoDisable item={props.item} />
      </Match>
      <Match when={props.item.typeId === ITEM_MEWTWO_SHADOW_BALL}>
        <MewtwoShadowBall item={props.item} />
      </Match>
      <Match when={props.item.typeId === ITEM_GAMEWATCH_GREENHOUSE}>
        <GameWatchGreenhouse item={props.item} />
      </Match>
      <Match when={props.item.typeId === ITEM_GAMEWATCH_MANHOLE}>
        <GameWatchManhole item={props.item} />
      </Match>
      <Match when={props.item.typeId === ITEM_GAMEWATCH_FIRE}>
        <GameWatchFire item={props.item} />
      </Match>
      <Match when={props.item.typeId === ITEM_GAMEWATCH_PARACHUTE}>
        <GameWatchParachute item={props.item} />
      </Match>
      <Match when={props.item.typeId === ITEM_GAMEWATCH_JUDGE}>
        <GameWatchJudge item={props.item} />
      </Match>
      <Match when={props.item.typeId === ITEM_GAMEWATCH_PANIC}>
        <GameWatchPanic item={props.item} />
      </Match>
      <Match when={props.item.typeId === ITEM_GAMEWATCH_CHEF}>
        <GameWatchChef item={props.item} />
      </Match>
      <Match when={props.item.typeId === ITEM_GAMEWATCH_RESCUE}>
        <GameWatchRescue item={props.item} />
      </Match>
      <Match when={props.item.typeId === ITEM_YOSHI_STAR}>
        <YoshiStar item={props.item} />
      </Match>
    </Switch>
    </>
  );
}

// The item's first live hit capsule from the sim's viewer lane, drawn like
// fighter hitboxes when the hitbox display is on (Fox/Falco side special,
// Yoshi's landing stars, PK Fire pillar, Blizzard, Bowser's flames).
function ItemHitbox(props: { item: ItemUpdate }) {
  const visible = createMemo(
    () => showHitboxes() && (props.item.hitboxRadius ?? 0) > 0
  );
  return (
    <Show when={visible()}>
      <circle
        cx={props.item.hitboxX}
        cy={props.item.hitboxY}
        r={props.item.hitboxRadius}
        fill="#ef4444"
        fill-opacity={0.22}
        stroke="#b91c1c"
        stroke-opacity={0.6}
        stroke-width={0.4}
      />
    </Show>
  );
}

function SamusChargeshot(props: { item: ItemUpdate }) {
  // charge levels go 0 to 7. State 0 is the shot charging in the arm cannon;
  // its item position is frozen at spawn (near her head), so draw the held
  // charge at the cannon. While charging, misc3 stays 0 (the level is copied
  // to the item at release), but the sim's viewer lane carries the shot's
  // joint scale, which itsamuschargeshot grows every frame; use it so the
  // ball visibly steps up as the charge builds.
  const hitboxesByChargeLevel = [300, 400, 500, 600, 700, 800, 900, 1200];
  const anchor = useAnchor(props.item);
  const level = createMemo(() =>
    Math.min(Math.max(props.item.chargeShotChargeLevel, 0), 7)
  );
  const held = createMemo(
    () => props.item.state === 0 && !props.item.isChargeShotLaunched
  );
  const radius = createMemo(() => {
    const scale = props.item.visualScale ?? 0;
    if (held() && scale > 0) {
      // Full charge is about joint scale 2.6, matching the 1200/256 hitbox.
      return Math.max(300 / 256, (scale / 2.6) * (1200 / 256));
    }
    return hitboxesByChargeLevel[level()] / 256;
  });
  const x = createMemo(() =>
    held() ? anchor.x() + anchor.facing() * 8 : props.item.xPosition
  );
  const y = createMemo(() => (held() ? anchor.y() + 8.5 : props.item.yPosition));
  return (
    <>
      <circle
        cx={x()}
        cy={y()}
        r={radius()}
        fill="darkgray"
        opacity={held() ? 0.6 : 1}
      />
    </>
  );
}

// Mewtwo and Game & Watch articles. Accessory articles are attached to a
// fighter bone in the source, and the item's own position stays frozen at
// its spawn point, so attached shapes are drawn relative to the owner using
// offsets measured at spawn (see agent_docs/VIEWER_ARTICLES.md). Projectiles
// (Chef sausages, thrown Shadow Ball, Disable) use the item position.

/** Owner position and facing, or the item's own when there is no owner. */
function useAnchor(item: ItemUpdate) {
  const owner = createMemo(() => {
    const frame = access("frames")[item.frameNumber];
    return item.owner >= 0 ? frame?.players[item.owner]?.state : undefined;
  });
  const x = createMemo(() => owner()?.xPosition ?? item.xPosition);
  const y = createMemo(() => owner()?.yPosition ?? item.yPosition);
  const facing = createMemo(() =>
    owner()?.facingDirection ?? (item.facingDirection >= 0 ? 1 : -1)
  );
  return { owner, x, y, facing };
}

/** Live fighter hitboxes of the item's owner (world space), if any. */
function ownerHitboxes(anchor: ReturnType<typeof useAnchor>) {
  return createMemo(() => anchor.owner()?.hitboxes ?? []);
}

function MewtwoShadowBall(props: { item: ItemUpdate }) {
  // states: 0 = charging in hand, 1-8 = thrown (state 8 is the ordinary
  // full-speed shot), 9 = reflected/absorbed transition. misc2 is the mode
  // (0 held, 1 released); misc3 is the charge counter 0..7.
  // The sim's viewer lane carries the rendered joint's world position and
  // uniform scale (the orbit and growth from the article's animation), so
  // draw there when present; otherwise fall back to a hand offset and a
  // radius proportional to the charge counter.
  const anchor = useAnchor(props.item);
  const held = createMemo(
    () => props.item.state === 0 && !props.item.isChargeShotLaunched
  );
  const level = createMemo(() =>
    Math.min(Math.max(props.item.chargeShotChargeLevel, 0), 7)
  );
  const hasVisual = createMemo(
    () => (props.item.visualScale ?? 0) > 0 && props.item.visualX !== undefined
  );
  // Full charge is joint scale 2.6 with a 6.2-unit hitbox, so 2.4 per unit.
  const radius = createMemo(() =>
    hasVisual() ? Math.max(0.8, props.item.visualScale! * 2.4) : 1.6 + level() * 0.65
  );
  const x = createMemo(() =>
    hasVisual()
      ? props.item.visualX!
      : held()
      ? anchor.x() + anchor.facing() * 7
      : props.item.xPosition
  );
  const y = createMemo(() =>
    hasVisual()
      ? props.item.visualY!
      : held()
      ? anchor.y() + 9
      : props.item.yPosition
  );
  return (
    <>
      <circle
        cx={x()}
        cy={y()}
        r={radius()}
        fill="#7e22ce"
        fill-opacity={held() ? 0.55 : 0.85}
        stroke="#3b0764"
        stroke-width={0.6}
      />
      <circle
        cx={x()}
        cy={y()}
        r={radius() * 0.45}
        fill="#e9d5ff"
        fill-opacity={0.8}
      />
    </>
  );
}

function MewtwoDisable(props: { item: ItemUpdate }) {
  // A short-lived eye beam that travels 2.7 units per frame from Mewtwo's
  // head height; 6-frame lifetime.
  const anchor = useAnchor(props.item);
  return (
    <>
      <line
        x1={anchor.x() + anchor.facing() * 4}
        y1={anchor.y() + 12}
        x2={props.item.xPosition}
        y2={props.item.yPosition}
        stroke="#4c1d95"
        stroke-width={0.5}
        stroke-opacity={0.5}
      />
      <circle
        cx={props.item.xPosition}
        cy={props.item.yPosition}
        r={1.3}
        fill="#4c1d95"
      />
    </>
  );
}

function GameWatchGreenhouse(props: { item: ItemUpdate }) {
  // Jab bug spray: a spray can in the leading hand and a cloud in front.
  const anchor = useAnchor(props.item);
  const puffs = [
    [3, 0, 1.4],
    [5.5, 0.8, 1.7],
    [8, 0, 1.9],
  ];
  return (
    <g fill="#a3e635" fill-opacity={0.7} stroke="#4d7c0f" stroke-width={0.4}>
      <rect
        x={anchor.x() + anchor.facing() * 4 - 0.9}
        y={anchor.y() + 4}
        width={1.8}
        height={2.6}
        fill="darkgray"
      />
      <For each={puffs}>
        {([dx, dy, r]) => (
          <circle
            cx={anchor.x() + anchor.facing() * (4 + dx)}
            cy={anchor.y() + 5.5 + dy}
            r={r}
          />
        )}
      </For>
    </g>
  );
}

function GameWatchManhole(props: { item: ItemUpdate }) {
  // Down tilt: a manhole cover flips up from the ground in front (spawned
  // 12.4 units ahead at ground level).
  const anchor = useAnchor(props.item);
  return (
    <rect
      x={anchor.x() + anchor.facing() * 12.4 - 5.5}
      y={anchor.y() + 0.4}
      width={11}
      height={1.6}
      rx={0.5}
      fill="darkgray"
      stroke="#374151"
      stroke-width={0.4}
    />
  );
}

function GameWatchFire(props: { item: ItemUpdate }) {
  // Forward smash: the torch flame, spawned 4.8 units ahead at head height.
  const anchor = useAnchor(props.item);
  const x = createMemo(() => anchor.x() + anchor.facing() * 7);
  const y = createMemo(() => anchor.y() + 13);
  return (
    <>
      <line
        x1={anchor.x() + anchor.facing() * 3}
        y1={anchor.y() + 9}
        x2={x()}
        y2={y() - 1}
        stroke="#78350f"
        stroke-width={0.8}
        stroke-linecap="round"
      />
      <circle cx={x()} cy={y()} r={3} fill="#f97316" fill-opacity={0.85} />
      <circle cx={x()} cy={y() + 0.8} r={1.5} fill="#fde047" />
    </>
  );
}

function GameWatchParachute(props: { item: ItemUpdate }) {
  // Neutral air: canopy above the body, attached to TransN. State 1 marks
  // the canopy folding after landing.
  const anchor = useAnchor(props.item);
  const top = createMemo(() => anchor.y() + 15);
  const half = 6;
  return (
    <g opacity={props.item.state === 1 ? 0.5 : 1}>
      <path
        d={`M ${anchor.x() - half} ${top()} A ${half} ${half} 0 0 0 ${
          anchor.x() + half
        } ${top()} Z`}
        fill="#f43f5e"
        fill-opacity={0.7}
        stroke="#881337"
        stroke-width={0.5}
      />
      <line
        x1={anchor.x() - half}
        y1={top()}
        x2={anchor.x()}
        y2={anchor.y() + 8}
        stroke="#374151"
        stroke-width={0.4}
      />
      <line
        x1={anchor.x() + half}
        y1={top()}
        x2={anchor.x()}
        y2={anchor.y() + 8}
        stroke="#374151"
        stroke-width={0.4}
      />
    </g>
  );
}

// Game & Watch's back-air turtle (118) and up-air puffs (119) are not drawn:
// their hitboxes are on the fighter and the hitbox overlay shows the hits.

function GameWatchJudge(props: { item: ItemUpdate }) {
  // Side special: draw the number only; the hammer's hit shows through the
  // hitbox overlay. The number is the item's animation frame, which is not on
  // the wire; recover it from the owner's motion state (SpecialS1..9 =
  // 355..363, SpecialAirS1..9 = 364..372).
  const anchor = useAnchor(props.item);
  const number = createMemo(() => {
    const action = anchor.owner()?.actionStateId ?? -1;
    if (
      action >= GAMEWATCH_JUDGE_GROUND_FIRST &&
      action < GAMEWATCH_JUDGE_GROUND_FIRST + 9
    ) {
      return action - GAMEWATCH_JUDGE_GROUND_FIRST + 1;
    }
    if (
      action >= GAMEWATCH_JUDGE_AIR_FIRST &&
      action < GAMEWATCH_JUDGE_AIR_FIRST + 9
    ) {
      return action - GAMEWATCH_JUDGE_AIR_FIRST + 1;
    }
    return 0;
  });
  return (
    <Show when={number() > 0}>
      <rect
        x={anchor.x() - 3.2}
        y={anchor.y() + 17}
        width={6.4}
        height={7}
        rx={0.8}
        fill="white"
        stroke="black"
        stroke-width={0.5}
      />
      <text
        x={anchor.x()}
        y={-(anchor.y() + 18.6)}
        transform="scale(1 -1)"
        text-anchor="middle"
        style={{ font: "bold 6px sans-serif" }}
        fill={number() === 9 ? "#dc2626" : "black"}
        textContent={String(number())}
      />
    </Show>
  );
}

function GameWatchPanic(props: { item: ItemUpdate }) {
  // Down special release: the stored oil splashes forward from the bucket.
  const anchor = useAnchor(props.item);
  const drops = [
    [4, 8, 4.5],
    [9, 6.5, 3.2],
    [13, 4.5, 2.2],
    [16, 2.5, 1.4],
  ];
  return (
    <g fill="#1e293b" fill-opacity={0.75}>
      <For each={drops}>
        {([dx, dy, r]) => (
          <circle
            cx={anchor.x() + anchor.facing() * dx}
            cy={anchor.y() + dy}
            r={r}
          />
        )}
      </For>
    </g>
  );
}

function GameWatchChef(props: { item: ItemUpdate }) {
  // Neutral special sausages: states 0 = flying, 1 = resting on the ground.
  // misc1 is the trajectory index; spin the sausage while it flies.
  const spin = createMemo(() =>
    props.item.state === 0 ? (props.item.frameNumber * 25) % 360 : 0
  );
  return (
    <ellipse
      cx={props.item.xPosition}
      cy={props.item.yPosition}
      rx={2.2}
      ry={1.1}
      transform={`rotate(${spin()} ${props.item.xPosition} ${props.item.yPosition})`}
      fill="#b45309"
      stroke="#78350f"
      stroke-width={0.4}
    />
  );
}

function GameWatchRescue(props: { item: ItemUpdate }) {
  // Up special: the firefighter trampoline stays where it spawned while
  // Game & Watch launches. State 1 is the aerial variant.
  const x = createMemo(() => props.item.xPosition);
  const y = createMemo(() => props.item.yPosition);
  return (
    <>
      <rect
        x={x() - 6}
        y={y() + 2}
        width={12}
        height={1.4}
        rx={0.5}
        fill="#ef4444"
        stroke="#7f1d1d"
        stroke-width={0.4}
      />
      <line
        x1={x() - 4.5}
        y1={y() + 2}
        x2={x() - 5.5}
        y2={y() - 1}
        stroke="#374151"
        stroke-width={0.5}
      />
      <line
        x1={x() + 4.5}
        y1={y() + 2}
        x2={x() + 5.5}
        y2={y() - 1}
        stroke="#374151"
        stroke-width={0.5}
      />
    </>
  );
}

function SamusBomb(props: { item: ItemUpdate }) {
  // states: 1 = falling, 3 = exploding
  return (
    <>
      <circle
        cx={props.item.xPosition}
        cy={props.item.yPosition}
        r={(props.item.state === 3 ? 1536 : 500) / 256}
        fill="darkgray"
      />
    </>
  );
}

function Missile(props: { item: ItemUpdate }) {
  // samusMissileTypes: 0 = homing missile, 1 = smash missile
  return (
    <>
      <circle
        cx={props.item.xPosition}
        cy={props.item.yPosition}
        r={(props.item.samusMissileType === 0 ? 500 : 600) / 256}
        fill="darkgray"
      />
    </>
  );
}

function MarioFireball(props: { item: ItemUpdate }) {
  return (
    <>
      <circle
        cx={props.item.xPosition}
        cy={props.item.yPosition}
        r={600 / 256}
        fill="darkgray"
      />
    </>
  );
}

function LuigiFireball(props: { item: ItemUpdate }) {
  return (
    <>
      <circle
        cx={props.item.xPosition}
        cy={props.item.yPosition}
        r={500 / 256}
        fill="darkgray"
      />
    </>
  );
}

function YoshiEgg(props: { item: ItemUpdate }) {
  // states: 0 = held, 1 = thrown, 2 = exploded
  const ownerState = createMemo(() => getOwner(props.item).state);
  return (
    <>
      <circle
        cx={
          props.item.state === 0 ? ownerState().xPosition : props.item.xPosition
        }
        cy={
          props.item.state === 0
            ? ownerState().yPosition + 8
            : props.item.yPosition
        }
        r={props.item.state === 2 ? 2500 / 256 : 1000 / 256}
        fill="darkgray"
        opacity={props.item.state === 1 ? 1 : 0.5}
      />
    </>
  );
}

function Turnip(props: { item: ItemUpdate }) {
  // states: 0 = held (first pickup), 1 = bouncing, 2 = thrown, 3 = dropped,
  // 4 = held after being caught. Once a turnip has been held before,
  // itPeachTurnip_Logic56_PickedUp re-enters hold as state 4, whose anim/phys
  // are no-ops, so the item's own position stays frozen at the catch point.
  // face (misc1): 0-4 ordinary faces, 5 = winking, 6 = dot eyes, 7 = stitch
  // face; the rarer faces deal more damage, so mark them.
  const ownerState = createMemo(() => getOwner(props.item).state);
  const held = createMemo(
    () => props.item.state === 0 || props.item.state === 4
  );
  const x = createMemo(() =>
    held() ? ownerState().xPosition : props.item.xPosition
  );
  const y = createMemo(() =>
    held() ? ownerState().yPosition + 8 : props.item.yPosition
  );
  const face = createMemo(() => props.item.peachTurnipFace);
  const r = 600 / 256;
  return (
    <g opacity={held() ? 0.5 : 1}>
      <circle cx={x()} cy={y()} r={r} fill="darkgray" />
      <line
        x1={x() - 0.6}
        y1={y() + r}
        x2={x() - 1.2}
        y2={y() + r + 1.6}
        stroke="#15803d"
        stroke-width={0.5}
      />
      <line
        x1={x() + 0.6}
        y1={y() + r}
        x2={x() + 1.2}
        y2={y() + r + 1.6}
        stroke="#15803d"
        stroke-width={0.5}
      />
      <Switch>
        <Match when={face() === 7}>
          {/* stitch face: crossed stitches */}
          <line x1={x() - 1.2} y1={y() + 0.9} x2={x() - 0.4} y2={y() + 0.1} stroke="black" stroke-width={0.35} />
          <line x1={x() - 1.2} y1={y() + 0.1} x2={x() - 0.4} y2={y() + 0.9} stroke="black" stroke-width={0.35} />
          <line x1={x() + 0.4} y1={y() + 0.9} x2={x() + 1.2} y2={y() + 0.1} stroke="black" stroke-width={0.35} />
          <line x1={x() + 0.4} y1={y() + 0.1} x2={x() + 1.2} y2={y() + 0.9} stroke="black" stroke-width={0.35} />
          <line x1={x() - 1} y1={y() - 0.9} x2={x() + 1} y2={y() - 0.9} stroke="black" stroke-width={0.35} />
        </Match>
        <Match when={face() === 6}>
          {/* dot eyes */}
          <circle cx={x() - 0.8} cy={y() + 0.5} r={0.25} fill="black" />
          <circle cx={x() + 0.8} cy={y() + 0.5} r={0.25} fill="black" />
        </Match>
        <Match when={face() === 5}>
          {/* winking */}
          <circle cx={x() - 0.8} cy={y() + 0.5} r={0.4} fill="black" />
          <line x1={x() + 0.3} y1={y() + 0.5} x2={x() + 1.3} y2={y() + 0.5} stroke="black" stroke-width={0.35} />
          <path d={`M ${x() - 0.8} ${y() - 0.7} Q ${x()} ${y() - 1.4} ${x() + 0.8} ${y() - 0.7}`} fill="none" stroke="black" stroke-width={0.35} />
        </Match>
        <Match when={true}>
          <circle cx={x() - 0.8} cy={y() + 0.5} r={0.4} fill="black" />
          <circle cx={x() + 0.8} cy={y() + 0.5} r={0.4} fill="black" />
          <path d={`M ${x() - 0.8} ${y() - 0.7} Q ${x()} ${y() - 1.4} ${x() + 0.8} ${y() - 0.7}`} fill="none" stroke="black" stroke-width={0.35} />
        </Match>
      </Switch>
    </g>
  );
}

function Bobomb(props: { item: ItemUpdate }) {
  // Peach's rare pull. states: 7 = held after the pull, 9 = thrown, 11 and
  // above = exploding. Held items keep a frozen position, so follow the
  // owner like turnips.
  const ownerState = createMemo(() => getOwner(props.item).state);
  const held = createMemo(() => props.item.state === 7);
  const exploding = createMemo(() => props.item.state >= 11);
  const x = createMemo(() =>
    held() ? ownerState().xPosition : props.item.xPosition
  );
  const y = createMemo(() =>
    held() ? ownerState().yPosition + 8 : props.item.yPosition
  );
  return (
    <g opacity={held() ? 0.6 : 1}>
      <Show
        when={!exploding()}
        fallback={
          <circle cx={x()} cy={y()} r={9} fill="#f97316" fill-opacity={0.7} />
        }
      >
        <circle cx={x()} cy={y()} r={2.4} fill="#1f2937" />
        <line
          x1={x()}
          y1={y() + 2.4}
          x2={x() + 1}
          y2={y() + 4}
          stroke="#78350f"
          stroke-width={0.4}
        />
        <circle cx={x() + 1} cy={y() + 4.2} r={0.5} fill="#fbbf24" />
      </Show>
    </g>
  );
}

function MrSaturn(props: { item: ItemUpdate }) {
  // states: 4 = held after pickup, 5 = thrown, 1 = sliding/bouncing,
  // 2 = resting.
  const ownerState = createMemo(() => getOwner(props.item).state);
  const held = createMemo(() => props.item.state === 4);
  const x = createMemo(() =>
    held() ? ownerState().xPosition : props.item.xPosition
  );
  const y = createMemo(() =>
    held() ? ownerState().yPosition + 8 : props.item.yPosition
  );
  return (
    <g opacity={held() ? 0.6 : 1}>
      <ellipse cx={x()} cy={y()} rx={2.8} ry={2.2} fill="#d6b48a" stroke="#78350f" stroke-width={0.4} />
      <circle cx={x() + props.item.facingDirection * 2.6} cy={y() - 0.2} r={0.8} fill="#d6b48a" stroke="#78350f" stroke-width={0.4} />
      <circle cx={x() + 0.8} cy={y() + 0.6} r={0.3} fill="black" />
      <circle cx={x() - 0.8} cy={y() + 0.6} r={0.3} fill="black" />
    </g>
  );
}

function BeamSword(props: { item: ItemUpdate }) {
  // states: 2 = held, 3 = dropped/thrown, 4 = resting.
  const ownerState = createMemo(() => getOwner(props.item).state);
  const held = createMemo(() => props.item.state === 2);
  const x = createMemo(() =>
    held() ? ownerState().xPosition + ownerState().facingDirection * 3 : props.item.xPosition
  );
  const y = createMemo(() =>
    held() ? ownerState().yPosition + 8 : props.item.yPosition
  );
  const angle = createMemo(() => (held() ? -50 * ownerState().facingDirection : 0));
  return (
    <g transform={`rotate(${angle()} ${x()} ${y()})`} opacity={held() ? 0.8 : 1}>
      <line x1={x() - 1.5} y1={y()} x2={x() + 1.5} y2={y()} stroke="#374151" stroke-width={0.9} stroke-linecap="round" />
      <line x1={x() + 1.5} y1={y()} x2={x() + 9} y2={y()} stroke="#22d3ee" stroke-width={0.8} stroke-linecap="round" />
    </g>
  );
}

function DrMarioVitamin(props: { item: ItemUpdate }) {
  // A spinning two-tone capsule.
  const spin = createMemo(() => (props.item.frameNumber * 30) % 360);
  const x = createMemo(() => props.item.xPosition);
  const y = createMemo(() => props.item.yPosition);
  return (
    <g transform={`rotate(${spin()} ${x()} ${y()})`}>
      <path
        d={`M ${x() - 2.4} ${y() - 1.1} h 2.4 v 2.2 h -2.4 a 1.1 1.1 0 0 1 0 -2.2 Z`}
        fill="#ef4444"
        stroke="#7f1d1d"
        stroke-width={0.3}
      />
      <path
        d={`M ${x()} ${y() - 1.1} h 2.4 a 1.1 1.1 0 0 1 0 2.2 h -2.4 Z`}
        fill="#fde68a"
        stroke="#7f1d1d"
        stroke-width={0.3}
      />
    </g>
  );
}

function MarioCape(props: { item: ItemUpdate }) {
  // Cape / Super Sheet: attached to the hand during SpecialS, frozen item
  // position. Sweep the cape from behind to in front over the swing using the
  // owner's action frame.
  const anchor = useAnchor(props.item);
  const swing = createMemo(() => {
    const frame = anchor.owner()?.actionStateFrameCounter ?? 0;
    // Source swing lasts about 12 frames after a 6-frame windup.
    return Math.min(1, Math.max(0, (frame - 5) / 12));
  });
  const angle = createMemo(() => (-140 + swing() * 200) * anchor.facing());
  const hx = createMemo(() => anchor.x() + anchor.facing() * 2);
  const hy = createMemo(() => anchor.y() + 8);
  const color = props.item.typeId === ITEM_DRMARIO_SHEET ? "#f8fafc" : "#facc15";
  return (
    <g transform={`rotate(${angle()} ${hx()} ${hy()})`}>
      <path
        d={`M ${hx()} ${hy()} L ${hx() + 8} ${hy() + 2.5} Q ${hx() + 9.5} ${hy()} ${hx() + 8} ${hy() - 2.5} Z`}
        fill={color}
        stroke="#713f12"
        stroke-width={0.4}
      />
    </g>
  );
}

function Grapple(props: { item: ItemUpdate }) {
  // Hookshot (Link, Young Link) and Samus's grapple beam. The tether length
  // is not on the wire and the item position is frozen at the hand. While
  // the grab hitbox is live it rides the tip, so use the owner's farthest
  // hitbox as the tip; otherwise extend along the facing on an envelope over
  // the owner's action frame (states 1 = extending, 3 = extended,
  // 4 = retracting).
  const anchor = useAnchor(props.item);
  const airborne = createMemo(() => !(anchor.owner()?.isGrounded ?? true));
  const frame = createMemo(() => anchor.owner()?.actionStateFrameCounter ?? 0);
  const maxLength = props.item.typeId === ITEM_SAMUS_GRAPPLE ? 42 : 36;
  // Hand anchor: the item position is frozen at spawn, so follow the owner.
  const x0 = createMemo(() => anchor.x() + anchor.facing() * 3);
  const y0 = createMemo(() => anchor.y() + 8.5);
  const tipFromHitbox = createMemo(() => {
    const hitboxes = anchor.owner()?.hitboxes ?? [];
    let best: { x: number; y: number } | undefined;
    let bestDistance = 2;
    for (const hitbox of hitboxes) {
      const distance = Math.hypot(hitbox.x - x0(), hitbox.y - y0());
      if (distance > bestDistance) {
        bestDistance = distance;
        best = hitbox;
      }
    }
    return best;
  });
  const envelopeLength = createMemo(() => {
    switch (props.item.state) {
      case 1:
        return Math.min(maxLength, Math.max(0, (frame() - (airborne() ? 5 : 7)) * 4));
      case 3:
        return maxLength;
      case 4:
        return Math.max(0, maxLength - Math.max(0, (frame() - (airborne() ? 34 : 55)) * 4));
      default:
        return 0;
    }
  });
  // The sim publishes the far end of the link chain (hook tip / beam end,
  // and the ledge grab point while hanging); prefer it, then the live grab
  // hitbox, then the frame envelope.
  const tip = createMemo(() =>
    props.item.tipX !== undefined && props.item.tipY !== undefined
      ? { x: props.item.tipX, y: props.item.tipY }
      : tipFromHitbox()
  );
  const x1 = createMemo(() => tip()?.x ?? x0() + anchor.facing() * envelopeLength());
  const y1 = createMemo(() => tip()?.y ?? y0());
  const visible = createMemo(() => {
    if (tip() !== undefined) {
      // A chain resting at the hand (windup, or reeled in) is not drawn.
      return Math.hypot(tip()!.x - x0(), tip()!.y - y0()) > 2.5;
    }
    return envelopeLength() > 0;
  });
  const beam = props.item.typeId === ITEM_SAMUS_GRAPPLE;
  return (
    <Show when={visible()}>
      <line
        x1={x0()}
        y1={y0()}
        x2={x1()}
        y2={y1()}
        stroke={beam ? "#38bdf8" : "#6b7280"}
        stroke-width={beam ? 1.2 : 0.6}
        stroke-dasharray={beam ? undefined : "1 0.6"}
        stroke-linecap="round"
      />
      <Show when={!beam}>
        <rect
          x={x1() - 1}
          y={y1() - 1}
          width={2}
          height={2}
          fill="#374151"
        />
      </Show>
    </Show>
  );
}

function NessPkFire(props: { item: ItemUpdate }) {
  // The bolt before it bursts into a pillar.
  const x = createMemo(() => props.item.xPosition);
  const y = createMemo(() => props.item.yPosition);
  const dx = createMemo(() => (props.item.facingDirection >= 0 ? 1 : -1) * 2.2);
  return (
    <polyline
      points={`${x() - dx()},${y() + 1} ${x() - dx() * 0.3},${y() - 0.6} ${x() + dx() * 0.3},${y() + 0.6} ${x() + dx()},${y() - 1}`}
      fill="none"
      stroke="#facc15"
      stroke-width={0.8}
      stroke-linejoin="round"
    />
  );
}

function NessPkFirePillar(props: { item: ItemUpdate }) {
  const x = createMemo(() => props.item.xPosition);
  const y = createMemo(() => props.item.yPosition);
  const flicker = createMemo(() => 1 + (props.item.frameNumber % 3) * 0.15);
  return (
    <>
      <ellipse cx={x()} cy={y() + 5} rx={3.2 * flicker()} ry={6.5 * flicker()} fill="#f97316" fill-opacity={0.6} />
      <ellipse cx={x()} cy={y() + 4} rx={1.8} ry={4 * flicker()} fill="#fde047" fill-opacity={0.8} />
    </>
  );
}

function NessPkFlash(props: { item: ItemUpdate }) {
  // Grows while B is held; life timer starts at 119.
  const age = createMemo(() => Math.max(0, 119 - props.item.expirationTimer));
  const r = createMemo(() => 1.5 + Math.min(1, age() / 60) * 4);
  return (
    <>
      <circle cx={props.item.xPosition} cy={props.item.yPosition} r={r() + 1} fill="#4ade80" fill-opacity={0.3} />
      <circle cx={props.item.xPosition} cy={props.item.yPosition} r={r()} fill="#22c55e" stroke="#14532d" stroke-width={0.4} />
    </>
  );
}

function NessPkFlashExplode(props: { item: ItemUpdate }) {
  const x = createMemo(() => props.item.xPosition);
  const y = createMemo(() => props.item.yPosition);
  return (
    <>
      <circle cx={x()} cy={y()} r={12} fill="#4ade80" fill-opacity={0.45} stroke="#15803d" stroke-width={0.5} />
      <circle cx={x()} cy={y()} r={6} fill="#bbf7d0" fill-opacity={0.8} />
    </>
  );
}

function NessPkThunder(props: { item: ItemUpdate }) {
  // Head (69) plus trail segments (70..73) as fading dots.
  const head = props.item.typeId === ITEM_NESS_PKTHUNDER;
  const trailIndex = props.item.typeId - ITEM_NESS_PKTHUNDER_TRAIL_FIRST;
  return (
    <circle
      cx={props.item.xPosition}
      cy={props.item.yPosition}
      r={head ? 2.2 : 1.6 - trailIndex * 0.25}
      fill={head ? "#38bdf8" : "#7dd3fc"}
      fill-opacity={head ? 1 : 0.8 - trailIndex * 0.15}
      stroke={head ? "#0369a1" : undefined}
      stroke-width={0.4}
    />
  );
}

// Ness's bat (101, forward smash) is not drawn either; the hit is a
// fighter hitbox.

// Ness's yoyo (102) is not drawn; the up/down smash hits are fighter
// hitboxes and show through the hitbox overlay.

function PikachuThunder(props: { item: ItemUpdate }) {
  // The bolt spawns 150 units above Pikachu and descends; draw a jagged
  // column above the item position.
  const x = createMemo(() => props.item.xPosition);
  const y = createMemo(() => props.item.yPosition);
  const points = createMemo(() => {
    const out: string[] = [];
    for (let i = 0; i <= 8; i += 1) {
      const wobble = i % 2 === 0 ? -2.5 : 2.5;
      out.push(`${x() + (i === 8 ? 0 : wobble)},${y() + i * 5}`);
    }
    return out.join(" ");
  });
  return (
    <>
      <polyline points={points()} fill="none" stroke="#facc15" stroke-width={1.6} stroke-linejoin="round" />
      <polyline points={points()} fill="none" stroke="#fef9c3" stroke-width={0.6} stroke-linejoin="round" />
    </>
  );
}

function PikachuThunderJolt(props: { item: ItemUpdate }) {
  const spin = createMemo(() => (props.item.frameNumber * 45) % 360);
  const x = createMemo(() => props.item.xPosition);
  const y = createMemo(() => props.item.yPosition);
  return (
    <g transform={`rotate(${spin()} ${x()} ${y()})`}>
      <polygon
        points={`${x() - 2.2},${y()} ${x() - 0.4},${y() + 0.8} ${x()},${y() + 2.2} ${x() + 0.4},${y() + 0.8} ${x() + 2.2},${y()} ${x() + 0.4},${y() - 0.8} ${x()},${y() - 2.2} ${x() - 0.4},${y() - 0.8}`}
        fill="#facc15"
        stroke="#a16207"
        stroke-width={0.3}
      />
    </g>
  );
}

function SheikVanish(props: { item: ItemUpdate }) {
  const x = createMemo(() => props.item.xPosition);
  const y = createMemo(() => props.item.yPosition);
  return (
    <>
      <circle cx={x()} cy={y()} r={9} fill="#e2e8f0" fill-opacity={0.55} stroke="#64748b" stroke-width={0.5} />
      <circle cx={x()} cy={y()} r={4.5} fill="#f8fafc" fill-opacity={0.9} />
    </>
  );
}

function YoshiEggLay(props: { item: ItemUpdate }) {
  // The egg holding a swallowed fighter. Draw at the item position, which
  // tracks the victim.
  return (
    <ellipse
      cx={props.item.xPosition}
      cy={props.item.yPosition + 6}
      rx={5.5}
      ry={7}
      fill="#f8fafc"
      fill-opacity={0.85}
      stroke="#16a34a"
      stroke-width={0.8}
      stroke-dasharray="2 1.2"
    />
  );
}

function BowserFlame(props: { item: ItemUpdate }) {
  // One item per puff; the life timer runs down from about 28.
  const size = createMemo(() => 1.5 + Math.min(1, props.item.expirationTimer / 28) * 2);
  return (
    <>
      <circle cx={props.item.xPosition} cy={props.item.yPosition} r={size() + 0.8} fill="#f97316" fill-opacity={0.45} />
      <circle cx={props.item.xPosition} cy={props.item.yPosition} r={size()} fill="#fde047" fill-opacity={0.75} />
    </>
  );
}

function PeachParasol(props: { item: ItemUpdate }) {
  // Item state 1 is the open parasol (ftGetParasolStatus 4, entered by the
  // subaction parasol command during the float); state 2 is closed, the
  // default every new motion state restores (fighter.c -> status 6), which
  // covers the up-special launch and the fast-fall tuck. The item position
  // is frozen at spawn, so anchor on Peach.
  const anchor = useAnchor(props.item);
  const open = createMemo(() => props.item.state === 1);
  const top = createMemo(() => anchor.y() + 19);
  const half = 7;
  return (
    <Show
      when={open()}
      fallback={
        <>
          <line
            x1={anchor.x() + anchor.facing() * 2}
            y1={anchor.y() + 6}
            x2={anchor.x() + anchor.facing() * 3.5}
            y2={anchor.y() + 17}
            stroke="#374151"
            stroke-width={0.5}
          />
          <path
            d={`M ${anchor.x() + anchor.facing() * 2.4} ${anchor.y() + 9} L ${
              anchor.x() + anchor.facing() * 4.6
            } ${anchor.y() + 16.5} L ${anchor.x() + anchor.facing() * 1.4} ${
              anchor.y() + 16.5
            } Z`}
            fill="#f472b6"
            fill-opacity={0.8}
            stroke="#9d174d"
            stroke-width={0.4}
          />
        </>
      }
    >
      <path
        d={`M ${anchor.x() - half} ${top()} A ${half} ${half} 0 0 0 ${anchor.x() + half} ${top()} Z`}
        fill="#f472b6"
        fill-opacity={0.8}
        stroke="#9d174d"
        stroke-width={0.5}
      />
      <line x1={anchor.x()} y1={top()} x2={anchor.x()} y2={anchor.y() + 9} stroke="#374151" stroke-width={0.5} />
    </Show>
  );
}

function YoshiStar(props: { item: ItemUpdate }) {
  // Yoshi Bomb landing stars: one item per side, hitbox on the item.
  const x = createMemo(() => props.item.xPosition);
  const y = createMemo(() => props.item.yPosition);
  const spin = createMemo(() => (props.item.frameNumber * 20) % 360);
  const points = createMemo(() => {
    const out: string[] = [];
    for (let i = 0; i < 10; i += 1) {
      const r = i % 2 === 0 ? 3.2 : 1.4;
      const a = (i * Math.PI) / 5 - Math.PI / 2;
      out.push(`${x() + Math.cos(a) * r},${y() + Math.sin(a) * r}`);
    }
    return out.join(" ");
  });
  return (
    <polygon
      points={points()}
      transform={`rotate(${spin()} ${x()} ${y()})`}
      fill="#fde047"
      stroke="#a16207"
      stroke-width={0.4}
    />
  );
}

function IceClimberIce(props: { item: ItemUpdate }) {
  // states: 1 = launched from the hand, 2 = sliding along the ground.
  const x = createMemo(() => props.item.xPosition);
  const y = createMemo(() => props.item.yPosition);
  return (
    <rect
      x={x() - 2.4}
      y={y() - 0.4}
      width={4.8}
      height={4.4}
      rx={0.6}
      fill="#bae6fd"
      stroke="#0284c7"
      stroke-width={0.5}
    />
  );
}

function IceClimberBlizzard(props: { item: ItemUpdate }) {
  const x = createMemo(() => props.item.xPosition);
  const y = createMemo(() => props.item.yPosition);
  const puffs = [
    [0, 0, 1.6],
    [-1.4, 1.2, 1.0],
    [1.3, -1.0, 1.0],
  ];
  return (
    <g fill="#e0f2fe" fill-opacity={0.8} stroke="#0ea5e9" stroke-width={0.3}>
      <For each={puffs}>
        {([dx, dy, r]) => <circle cx={x() + dx} cy={y() + dy} r={r} />}
      </For>
    </g>
  );
}

function ZeldaDinFire(props: { item: ItemUpdate }) {
  // Din's Fire: state 0 = the ember travelling while B is held, state 1 =
  // stalled just before the burst. The explosion is a separate item.
  const flicker = createMemo(() => 1.6 + (props.item.frameNumber % 3) * 0.2);
  return (
    <>
      <circle
        cx={props.item.xPosition}
        cy={props.item.yPosition}
        r={flicker() + 0.8}
        fill="#f97316"
        fill-opacity={0.35}
      />
      <circle
        cx={props.item.xPosition}
        cy={props.item.yPosition}
        r={flicker()}
        fill="#ef4444"
        stroke="#7f1d1d"
        stroke-width={0.3}
      />
    </>
  );
}

function ZeldaDinFireExplode(props: { item: ItemUpdate }) {
  // The burst scale is an f32 in the item vars (not sampled on the wire), and
  // it grows with how long the ember travelled. Approximate it from the
  // burst's distance to Zelda, which is what the travel time produces.
  const anchor = useAnchor(props.item);
  const radius = createMemo(() => {
    const dx = props.item.xPosition - anchor.x();
    const dy = props.item.yPosition - anchor.y();
    const travel = Math.min(Math.hypot(dx, dy), 70) / 70;
    return 3 + travel * 5;
  });
  const age = createMemo(() => Math.max(0, 58 - props.item.expirationTimer));
  const spread = createMemo(() => Math.min(1, age() / 8));
  return (
    <>
      <circle
        cx={props.item.xPosition}
        cy={props.item.yPosition}
        r={radius() * spread()}
        fill="#f97316"
        fill-opacity={0.55}
        stroke="#b91c1c"
        stroke-width={0.5}
      />
      <circle
        cx={props.item.xPosition}
        cy={props.item.yPosition}
        r={radius() * spread() * 0.5}
        fill="#fde047"
        fill-opacity={0.8}
      />
    </>
  );
}

function Needle(props: { item: ItemUpdate }) {
  return (
    <>
      <circle
        cx={props.item.xPosition}
        cy={props.item.yPosition}
        r={500 / 256}
        fill="darkgray"
      />
    </>
  );
}

function SheikChain(props: { item: ItemUpdate }) {
  const frame = createMemo(() => access("frames")[props.item.frameNumber]);
  const owner = createMemo(() => {
    const ownerIndex = props.item.owner;
    return ownerIndex >= 0 ? frame()?.players[ownerIndex] : undefined;
  });
  const hitboxes = createMemo<HitboxUpdate[]>(
    () => owner()?.state.hitboxes ?? []
  );
  const pointString = createMemo(() =>
    hitboxes()
      .map((hitbox) => `${hitbox.x},${hitbox.y}`)
      .join(" ")
  );
  return (
    <>
      <polyline
        points={pointString()}
        fill="none"
        stroke="#4b5563"
        stroke-width={1.35}
        stroke-linecap="round"
        stroke-linejoin="round"
      />
      <For each={hitboxes()}>
        {(hitbox) => (
          <circle
            cx={hitbox.x}
            cy={hitbox.y}
            r={hitbox.radius}
            fill="#f59e0b"
            fill-opacity={0.28}
            stroke="#b45309"
            stroke-width={0.8}
          />
        )}
      </For>
    </>
  );
}

function FoxLaser(props: { item: ItemUpdate }) {
  // There is a 4th hitbox for the first frame only at -3600 (hitboxspace) with
  // size 400 / 256 that I am skipping.
  const hitboxOffsets = [-200, -933, -1666].map((x) => x / 256);
  const hitboxSize = 300 / 256;
  // Throws and deflected lasers are not straight horizontal
  const rotations = createMemo(() => {
    const direction = Math.atan2(props.item.yVelocity, props.item.xVelocity);
    return [Math.cos(direction), Math.sin(direction)];
  });
  return (
    <>
      <line
        x1={
          props.item.xPosition +
          hitboxOffsets[0] * props.item.facingDirection * rotations()[0]
        }
        y1={
          props.item.yPosition +
          hitboxOffsets[0] * props.item.facingDirection * rotations()[1]
        }
        x2={
          props.item.xPosition +
          hitboxOffsets[hitboxOffsets.length - 1] *
            props.item.facingDirection *
            rotations()[0]
        }
        y2={
          props.item.yPosition +
          hitboxOffsets[hitboxOffsets.length - 1] *
            props.item.facingDirection *
            rotations()[1]
        }
        stroke="red"
      />
      <For each={hitboxOffsets}>
        {(hitboxOffset) => (
          <circle
            cx={
              props.item.xPosition +
              hitboxOffset * props.item.facingDirection * rotations()[0]
            }
            cy={
              props.item.yPosition +
              hitboxOffset * props.item.facingDirection * rotations()[1]
            }
            r={hitboxSize}
            fill="red"
          />
        )}
      </For>
    </>
  );
}

function FalcoLaser(props: { item: ItemUpdate }) {
  const hitboxOffsets = [-200, -933, -1666, -2400].map((x) => x / 256);
  const hitboxSize = 300 / 256;
  // Throws and deflected lasers are not straight horizontal
  const rotations = createMemo(() => {
    const direction = Math.atan2(props.item.yVelocity, props.item.xVelocity);
    return [Math.cos(direction), Math.sin(direction)];
  });
  return (
    <>
      <line
        x1={
          props.item.xPosition +
          hitboxOffsets[0] * props.item.facingDirection * rotations()[0]
        }
        y1={
          props.item.yPosition +
          hitboxOffsets[0] * props.item.facingDirection * rotations()[1]
        }
        x2={
          props.item.xPosition +
          hitboxOffsets[hitboxOffsets.length - 1] *
            props.item.facingDirection *
            rotations()[0]
        }
        y2={
          props.item.yPosition +
          hitboxOffsets[hitboxOffsets.length - 1] *
            props.item.facingDirection *
            rotations()[1]
        }
        stroke="red"
      />
      <For each={hitboxOffsets}>
        {(hitboxOffset) => (
          <circle
            cx={
              props.item.xPosition +
              hitboxOffset * props.item.facingDirection * rotations()[0]
            }
            cy={
              props.item.yPosition +
              hitboxOffset * props.item.facingDirection * rotations()[1]
            }
            r={hitboxSize}
            fill="red"
          />
        )}
      </For>
    </>
  );
}

function FlyGuy(props: { item: ItemUpdate }) {
  return (
    <>
      <circle
        cx={props.item.xPosition}
        cy={props.item.yPosition}
        r={5 * 0.85}
        fill="#aa0000"
      />
    </>
  );
}

function LinkBomb(props: { item: ItemUpdate }) {
  // states: 0 = held (also re-entered on catch/pickup), 2 = thrown,
  // 3 = dropped/falling, 4 = resting on the ground, 5 = exploding,
  // 6 = knocked airborne. Held bombs keep a stale item position, so draw
  // them at the owner like held turnips.
  const ownerState = createMemo(() => getOwner(props.item).state);
  const held = createMemo(() => props.item.state === 0);
  return (
    <>
      <circle
        cx={held() ? ownerState().xPosition : props.item.xPosition}
        cy={held() ? ownerState().yPosition + 8 : props.item.yPosition}
        r={(props.item.state === 5 ? 2500 : 500) / 256}
        fill="darkgray"
        opacity={held() ? 0.5 : 1}
      />
    </>
  );
}

function LinkBoomerang(props: { item: ItemUpdate }) {
  // states: 0 = in hand during the throw windup, 1 = outbound, 2 = returning,
  // 3 = spinning down after stalling
  const ownerState = createMemo(() => getOwner(props.item).state);
  const held = createMemo(() => props.item.state === 0);
  const x = createMemo(() =>
    held() ? ownerState().xPosition : props.item.xPosition
  );
  const y = createMemo(() =>
    held() ? ownerState().yPosition + 8 : props.item.yPosition
  );
  const spin = createMemo(() => (props.item.frameNumber * 40) % 360);
  const armLength = 900 / 256;
  return (
    <g transform={`rotate(${spin()} ${x()} ${y()})`} opacity={held() ? 0.5 : 1}>
      <line
        x1={x() - armLength}
        y1={y()}
        x2={x() + armLength}
        y2={y()}
        stroke="darkgray"
        stroke-width={0.8}
        stroke-linecap="round"
      />
      <line
        x1={x()}
        y1={y() - armLength}
        x2={x()}
        y2={y() + armLength}
        stroke="darkgray"
        stroke-width={0.8}
        stroke-linecap="round"
      />
    </g>
  );
}

function LinkArrow(props: { item: ItemUpdate }) {
  // states: 0 = nocked while the bow charges, 1 = flying, 2 = stopped by a
  // shield, 3/4 = stuck in the ground
  const ownerState = createMemo(() => getOwner(props.item).state);
  const nocked = createMemo(() => props.item.state === 0);
  const direction = createMemo(() => {
    if (
      props.item.state === 1 &&
      (props.item.xVelocity !== 0 || props.item.yVelocity !== 0)
    ) {
      return Math.atan2(props.item.yVelocity, props.item.xVelocity);
    }
    return props.item.facingDirection >= 0 ? 0 : Math.PI;
  });
  const x = createMemo(() =>
    nocked() ? ownerState().xPosition : props.item.xPosition
  );
  const y = createMemo(() =>
    nocked() ? ownerState().yPosition + 8 : props.item.yPosition
  );
  const halfLength = 1200 / 256;
  const color = createMemo(() =>
    itemNamesById[props.item.typeId] === "Fire Arrow" ? "orangered" : "darkgray"
  );
  return (
    <line
      x1={x() - Math.cos(direction()) * halfLength}
      y1={y() - Math.sin(direction()) * halfLength}
      x2={x() + Math.cos(direction()) * halfLength}
      y2={y() + Math.sin(direction()) * halfLength}
      stroke={color()}
      stroke-width={0.7}
      stroke-linecap="round"
      opacity={nocked() ? 0.5 : 1}
    />
  );
}

function getOwner(item: ItemUpdate): PlayerUpdate {
  return access("frames")[item.frameNumber].players[item.owner];
}
