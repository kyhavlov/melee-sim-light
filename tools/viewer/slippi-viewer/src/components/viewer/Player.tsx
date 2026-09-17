import { createMemo, For, Match, Show, Switch } from "solid-js";
import { characterNameByExternalId } from "~/common/ids";
import { RenderData } from "~/common/types";
import { access } from "~/state/accessor";
import { showHitboxes } from "~/state/displayStore";
import { getPlayerOnFrame, getStartOfAction } from "~/viewer/viewerUtil";

export function Players() {
  return (
    <>
      <For each={access("renderDatas")}>
        {(renderData: RenderData) => (
          <>
            <path
              transform={renderData.transforms.join(" ")}
              d={renderData.path}
              fill={renderData.innerColor}
              stroke-width={2}
              stroke={renderData.outerColor}
            />
            <Shield renderData={renderData} />
            <Shine renderData={renderData} />
            <Hitboxes renderData={renderData} />
            <SleepBubbles renderData={renderData} />
            <Misfire renderData={renderData} />
            <Buried renderData={renderData} />
            <Frozen renderData={renderData} />
            <HitElementEffect renderData={renderData} />
            <YoshiEggShell renderData={renderData} />
            <PsiMagnet renderData={renderData} />
          </>
        )}
      </For>
    </>
  );
}

function Shield(props: { renderData: RenderData }) {
  // [0,60]
  const shieldHealth = createMemo(
    () => props.renderData.playerState.shieldSize
  );
  // [0,1]. If 0 is received, set to 1 because user may have released shield
  // during a Guard-related animation. As an example, a shield must stay active
  // for 8 frames minimum before it is dropped even if the player releases the
  // trigger early.
  // For GuardDamage the shield strength is fixed and ignores trigger updates,
  // so we must walk back to the first frame of stun and read trigger there.
  const triggerStrength = createMemo(() =>
    props.renderData.animationName === "GuardDamage"
      ? getPlayerOnFrame(
          props.renderData.playerSettings.playerIndex,
          getStartOfAction(props.renderData.playerState)
        ).inputs.processed.anyTrigger
      : props.renderData.playerInputs.processed.anyTrigger === 0
      ? 1
      : props.renderData.playerInputs.processed.anyTrigger
  );
  // Source light-shield amount when the sim publishes it, else the trigger
  // analog (the same quantity before the source's 0.3 dead zone).
  const shieldStrength = createMemo(() => {
    const source = props.renderData.playerState.shieldStrength;
    if (typeof source === "number" && Number.isFinite(source)) return source;
    return Math.max(0, Math.min(1, (triggerStrength() - 0.3) / 0.7));
  });
  // Formulas from https://www.ssbwiki.com/Shield#Shield_statistics
  const triggerStrengthMultiplier = createMemo(
    () => 1 - (0.5 * (triggerStrength() - 0.3)) / 0.7
  );
  const shieldSizeMultiplier = createMemo(
    () => ((shieldHealth() * triggerStrengthMultiplier()) / 60) * 0.85 + 0.15
  );
  const shieldTiltDistance = createMemo(
    () => props.renderData.characterData.shieldSize * 0.25
  );
  const shieldX = createMemo(() => {
    const x = props.renderData.playerState.shieldX;
    return typeof x === "number" && Number.isFinite(x)
      ? x
      : props.renderData.playerState.xPosition +
          props.renderData.characterData.shieldOffset[0] *
            props.renderData.playerState.facingDirection +
          (props.renderData.playerState.shieldTiltX ?? 0) *
            shieldTiltDistance();
  });
  const shieldY = createMemo(() => {
    const y = props.renderData.playerState.shieldY;
    return typeof y === "number" && Number.isFinite(y)
      ? y
      : props.renderData.playerState.yPosition +
          props.renderData.characterData.shieldOffset[1] +
          (props.renderData.playerState.shieldTiltY ?? 0) *
            shieldTiltDistance();
  });
  const shieldRadius = createMemo(() => {
    const r = props.renderData.playerState.shieldRadius;
    return typeof r === "number" && Number.isFinite(r) && r > 0
      ? r
      : props.renderData.characterData.shieldSize * shieldSizeMultiplier();
  });
  return (
    <>
      <Show
        when={["GuardOn", "Guard", "GuardReflect", "GuardDamage"].includes(
          props.renderData.animationName
        )}
      >
        <circle
          cx={shieldX()}
          cy={shieldY()}
          r={shieldRadius()}
          fill={props.renderData.innerColor}
          // Shield strength from the source (1 = hard/digital shield,
          // lower = lighter analog shield, which is larger but weaker):
          // fade continuously and add a dashed rim below full strength.
          opacity={0.25 + 0.35 * shieldStrength()}
          stroke={props.renderData.outerColor}
          stroke-width={shieldStrength() < 0.99 ? 0.6 : 0}
          stroke-dasharray={shieldStrength() < 0.99 ? "1.5 1" : undefined}
        />
      </Show>
    </>
  );
}

function Shine(props: { renderData: RenderData }) {
  const characterName = createMemo(
    () =>
      characterNameByExternalId[
        props.renderData.playerSettings.externalCharacterId
      ]
  );
  return (
    <>
      <Show
        when={
          ["Fox", "Falco"].includes(characterName()) &&
          (props.renderData.animationName.includes("SpecialLw") ||
            props.renderData.animationName.includes("SpecialAirLw"))
        }
      >
        <Hexagon
          x={props.renderData.playerState.xPosition}
          // TODO get true shine position, shieldY * 3/4 is a guess.
          y={
            props.renderData.playerState.yPosition +
            (props.renderData.characterData.shieldOffset[1] * 3) / 4
          }
          r={6}
        />
      </Show>
    </>
  );
}

// Active fighter hitboxes from the wire (up to four per fighter, world
// space, radius already scaled). Drawn translucent so attacks whose visuals
// live in effects rather than the silhouette (Samus's forward air, Ness's
// PSI aerials and dash attack, Sheik's up special burst) still show where
// they hit.
function Hitboxes(props: { renderData: RenderData }) {
  const hitboxes = createMemo(() =>
    showHitboxes() ? props.renderData.playerState.hitboxes ?? [] : []
  );
  return (
    <For each={hitboxes()}>
      {(hitbox) => (
        <circle
          cx={hitbox.x}
          cy={hitbox.y}
          r={hitbox.radius}
          fill="#ef4444"
          fill-opacity={0.22}
          stroke="#b91c1c"
          stroke-opacity={0.6}
          stroke-width={0.4}
        />
      )}
    </For>
  );
}

// Buried by Donkey Kong's down special: Bury (294), BuryWait (295),
// BuryJump (296). The silhouette falls back to a tumble pose; cover the lower
// half with a mound so the fighter reads as stuck in the ground.
const BURY_ACTION_IDS = [294, 295];

function Buried(props: { renderData: RenderData }) {
  const buried = createMemo(() =>
    BURY_ACTION_IDS.includes(props.renderData.playerState.actionStateId)
  );
  const x = createMemo(() => props.renderData.playerState.xPosition);
  const y = createMemo(() => props.renderData.playerState.yPosition);
  const height = createMemo(
    () => props.renderData.characterData.shieldOffset[1] * 1.3
  );
  return (
    <Show when={buried()}>
      <ellipse
        cx={x()}
        cy={y() + height() * 0.15}
        rx={8}
        ry={2.4}
        fill="#78350f"
        stroke="#451a03"
        stroke-width={0.5}
      />
      <path
        d={`M ${x() - 7.5} ${y() + height() * 0.15} Q ${x() - 4} ${y() + height() * 0.15 + 4} ${x() - 1} ${y() + height() * 0.15 + 1.5} T ${x() + 4} ${y() + height() * 0.15 + 3.5} T ${x() + 7.5} ${y() + height() * 0.15}`}
        fill="#92400e"
        stroke="#451a03"
        stroke-width={0.5}
      />
    </Show>
  );
}

// Frozen by an ice attack: DamageIce (325) and DamageIceJump (326).
const FROZEN_ACTION_IDS = [325, 326];

function Frozen(props: { renderData: RenderData }) {
  const frozen = createMemo(() =>
    FROZEN_ACTION_IDS.includes(props.renderData.playerState.actionStateId)
  );
  const x = createMemo(() => props.renderData.playerState.xPosition);
  const y = createMemo(() => props.renderData.playerState.yPosition);
  const height = createMemo(
    () => props.renderData.characterData.shieldOffset[1] * 2.2
  );
  return (
    <Show when={frozen()}>
      <rect
        x={x() - height() * 0.35}
        y={y() - 1}
        width={height() * 0.7}
        height={height()}
        rx={1.5}
        fill="#bae6fd"
        fill-opacity={0.55}
        stroke="#0284c7"
        stroke-width={0.6}
      />
    </Show>
  );
}

// Fox and Falco's Fire Fox / Fire Bird are not drawn as flames; the charge
// and travel hits are fighter hitboxes and show through the hitbox overlay.

// Element of the last hit taken (HitElement in refs/melee lb/forward.h):
// 1 fire, 2 electric, 13 dark. Shown while the hit is still in effect, i.e.
// during hitlag (electric paralysis) or hitstun (burning / dark flames).
const HIT_ELEMENT_FIRE = 1;
const HIT_ELEMENT_ELECTRIC = 2;
const HIT_ELEMENT_DARK = 13;

function HitElementEffect(props: { renderData: RenderData }) {
  const state = () => props.renderData.playerState;
  const element = createMemo(() => state().lastHitElement ?? 0);
  const active = createMemo(
    () =>
      (state().hitlagRemaining > 0 || state().hitstunRemaining > 0) &&
      (element() === HIT_ELEMENT_FIRE ||
        element() === HIT_ELEMENT_ELECTRIC ||
        element() === HIT_ELEMENT_DARK)
  );
  const x = createMemo(() => state().xPosition);
  const y = createMemo(
    () => state().yPosition + props.renderData.characterData.shieldOffset[1]
  );
  const size = createMemo(
    () => props.renderData.characterData.shieldOffset[1] * 1.1
  );
  const frame = createMemo(() => state().frameNumber);
  const tongues = [0, 60, 120, 180, 240, 300];
  return (
    <Show when={active()}>
      <Switch>
        <Match when={element() === HIT_ELEMENT_ELECTRIC}>
          <For each={tongues}>
            {(deg) => {
              const angle = createMemo(() => ((deg + frame() * 47) * Math.PI) / 180);
              const reach = createMemo(() => size() * (0.9 + ((frame() + deg) % 3) * 0.15));
              return (
                <line
                  x1={x() + Math.cos(angle()) * size() * 0.4}
                  y1={y() + Math.sin(angle()) * size() * 0.4}
                  x2={x() + Math.cos(angle() + 0.5) * reach()}
                  y2={y() + Math.sin(angle() + 0.5) * reach()}
                  stroke="#facc15"
                  stroke-width={0.7}
                  stroke-linecap="round"
                />
              );
            }}
          </For>
        </Match>
        <Match when={true}>
          <For each={tongues}>
            {(deg) => {
              const angle = createMemo(() => ((deg + frame() * 23) * Math.PI) / 180);
              const flicker = createMemo(() => 0.7 + ((frame() + deg) % 4) * 0.12);
              return (
                <circle
                  cx={x() + Math.cos(angle()) * size() * 0.55}
                  cy={y() + Math.sin(angle()) * size() * 0.7 + size() * 0.2 * flicker()}
                  r={size() * 0.32 * flicker()}
                  fill={element() === HIT_ELEMENT_DARK ? "#7e22ce" : "#f97316"}
                  fill-opacity={0.55}
                />
              );
            }}
          </For>
        </Match>
      </Switch>
    </Show>
  );
}

// Swallowed by Yoshi: YoshiEgg (277) and KirbyYoshiEgg (332 in the common
// table). The silhouette is hidden and an egg drawn at the victim's position.
const YOSHI_EGG_ACTION_IDS = [277, 332];

function YoshiEggShell(props: { renderData: RenderData }) {
  const egged = createMemo(() =>
    YOSHI_EGG_ACTION_IDS.includes(props.renderData.playerState.actionStateId)
  );
  const x = createMemo(() => props.renderData.playerState.xPosition);
  const y = createMemo(() => props.renderData.playerState.yPosition);
  const size = createMemo(
    () => props.renderData.characterData.shieldOffset[1] * 0.9
  );
  return (
    <Show when={egged()}>
      <ellipse
        cx={x()}
        cy={y() + size()}
        rx={size() * 0.8}
        ry={size()}
        fill="#f8fafc"
        stroke="#16a34a"
        stroke-width={0.8}
      />
      <circle cx={x() - size() * 0.3} cy={y() + size() * 1.3} r={size() * 0.14} fill="#16a34a" />
      <circle cx={x() + size() * 0.35} cy={y() + size() * 0.7} r={size() * 0.14} fill="#16a34a" />
      <circle cx={x() + size() * 0.1} cy={y() + size() * 1.6} r={size() * 0.1} fill="#16a34a" />
    </Show>
  );
}

// Ness's PSI Magnet: SpecialLwStart/Hold/Hit/End (367..370). The absorb
// field is an effect, not an item.
const NESS_PSI_MAGNET_ACTION_IDS = [367, 368, 369, 370];

function PsiMagnet(props: { renderData: RenderData }) {
  const characterName = createMemo(
    () =>
      characterNameByExternalId[
        props.renderData.playerSettings.externalCharacterId
      ]
  );
  const active = createMemo(
    () =>
      characterName() === "Ness" &&
      NESS_PSI_MAGNET_ACTION_IDS.includes(
        props.renderData.playerState.actionStateId
      )
  );
  const x = createMemo(
    () =>
      props.renderData.playerState.xPosition +
      props.renderData.playerState.facingDirection * 6
  );
  const y = createMemo(() => props.renderData.playerState.yPosition + 7);
  const pulse = createMemo(
    () => 1 + ((props.renderData.playerState.frameNumber % 8) / 8) * 0.12
  );
  return (
    <Show when={active()}>
      <circle cx={x()} cy={y()} r={7 * pulse()} fill="#38bdf8" fill-opacity={0.3} stroke="#0284c7" stroke-width={0.5} />
      <circle cx={x()} cy={y()} r={3.5 * pulse()} fill="#e0f2fe" fill-opacity={0.6} />
    </Show>
  );
}

// Sing victims: DamageSong (297) and DamageSongWait (298); DamageSongRv (299)
// is the wake-up. Jigglypuff's Rest is SpecialLwL/R (369, 371) and the aerial
// variants (370, 372); she sleeps through it too.
const SLEEP_ACTION_IDS = [297, 298];
const JIGGLYPUFF_REST_ACTION_IDS = [369, 370, 371, 372];

function SleepBubbles(props: { renderData: RenderData }) {
  const characterName = createMemo(
    () =>
      characterNameByExternalId[
        props.renderData.playerSettings.externalCharacterId
      ]
  );
  const asleep = createMemo(() => {
    const action = props.renderData.playerState.actionStateId;
    if (SLEEP_ACTION_IDS.includes(action)) return true;
    return (
      characterName() === "Jigglypuff" &&
      JIGGLYPUFF_REST_ACTION_IDS.includes(action)
    );
  });
  const headX = createMemo(
    () =>
      props.renderData.playerState.xPosition +
      props.renderData.playerState.facingDirection *
        props.renderData.characterData.shieldOffset[0]
  );
  const headY = createMemo(
    () =>
      props.renderData.playerState.yPosition +
      props.renderData.characterData.shieldOffset[1] * 1.6
  );
  // Three z's drift up and away on a 45-frame cycle.
  const phase = createMemo(
    () => (props.renderData.playerState.frameNumber % 45) / 45
  );
  const zs = [0, 1, 2];
  return (
    <Show when={asleep()}>
      <For each={zs}>
        {(index) => {
          const t = createMemo(() => (phase() + index / 3) % 1);
          return (
            <text
              x={headX() + props.renderData.playerState.facingDirection * (3 + t() * 6)}
              y={-(headY() + t() * 10)}
              transform="scale(1 -1)"
              style={{ font: `bold ${3 + index * 1.2}px sans-serif` }}
              fill={props.renderData.innerColor}
              stroke="black"
              stroke-width={0.25}
              opacity={1 - t()}
              textContent="z"
            />
          );
        }}
      </For>
    </Show>
  );
}

// Luigi's Green Missile misfire: SpecialSMisfire (348) and
// SpecialAirSMisfire (354). The zip has no separate silhouette for these
// states, so mark the launch with a burst trailing behind him.
const LUIGI_MISFIRE_ACTION_IDS = [348, 354];

function Misfire(props: { renderData: RenderData }) {
  const characterName = createMemo(
    () =>
      characterNameByExternalId[
        props.renderData.playerSettings.externalCharacterId
      ]
  );
  const misfiring = createMemo(
    () =>
      characterName() === "Luigi" &&
      LUIGI_MISFIRE_ACTION_IDS.includes(
        props.renderData.playerState.actionStateId
      )
  );
  const tailX = createMemo(
    () =>
      props.renderData.playerState.xPosition -
      props.renderData.playerState.facingDirection * 6
  );
  const tailY = createMemo(
    () =>
      props.renderData.playerState.yPosition +
      props.renderData.characterData.shieldOffset[1] * 0.6
  );
  const flicker = createMemo(
    () => 1 + (props.renderData.playerState.frameNumber % 2) * 0.3
  );
  const sparks = [
    [-3, 3],
    [-5, -2],
    [-8, 1],
    [-6, 5],
    [-9, -4],
  ];
  return (
    <Show when={misfiring()}>
      <circle
        cx={tailX()}
        cy={tailY()}
        r={5 * flicker()}
        fill="#f97316"
        fill-opacity={0.6}
      />
      <circle
        cx={tailX()}
        cy={tailY()}
        r={2.5 * flicker()}
        fill="#fde047"
        fill-opacity={0.9}
      />
      <For each={sparks}>
        {([dx, dy]) => (
          <circle
            cx={tailX() + props.renderData.playerState.facingDirection * dx * flicker()}
            cy={tailY() + dy}
            r={0.9}
            fill="#ef4444"
          />
        )}
      </For>
    </Show>
  );
}

function Hexagon(props: { x: number; y: number; r: number }) {
  const hexagonHole = 0.6;
  const sideX = Math.sin((2 * Math.PI) / 6);
  const sideY = 0.5;
  const offsets = [
    [0, 1],
    [sideX, sideY],
    [sideX, -sideY],
    [0, -1],
    [-sideX, -sideY],
    [-sideX, sideY],
  ];
  const points = createMemo(() =>
    offsets
      .map(([xOffset, yOffset]) =>
        [props.r * xOffset + props.x, props.r * yOffset + props.y].join(",")
      )
      .join(",")
  );
  const maskPoints = createMemo(() =>
    offsets
      .map(([xOffset, yOffset]) =>
        [
          props.r * xOffset * hexagonHole + props.x,
          props.r * yOffset * hexagonHole + props.y,
        ].join(",")
      )
      .join(",")
  );
  return (
    <>
      <defs>
        <mask id="innerHexagon">
          <polygon points={points()} fill="white" />
          <polygon points={maskPoints()} fill="black" />
        </mask>
      </defs>
      <polygon points={points()} fill="#8abce9" mask="url(#innerHexagon)" />
    </>
  );
}
