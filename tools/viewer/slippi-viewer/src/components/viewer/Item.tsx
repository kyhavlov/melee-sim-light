import { createMemo, For, Match, Switch } from "solid-js";
import { itemNamesById } from "~/common/ids";
import {
  HitboxUpdate,
  ItemUpdate,
  PlayerUpdate,
  NonReactiveState,
} from "~/common/types";
import { access } from "~/state/accessor";

// TODO: characters projectiles

// Note: Most items coordinates and sizes are divided by 256 to convert them
// from hitboxspace to worldspace.
export function Item(props: { item: ItemUpdate }) {
  // Kirby's copied projectiles are their own item kinds ("Kirby copy Fox's
  // Laser (B)") but draw like the originals.
  const itemName = createMemo(() =>
    itemNamesById[props.item.typeId]?.replace(/^Kirby copy /, "").replace(/ \(B\)$/, "")
  );
  return (
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
      <Match
        when={
          itemName() === "Kirby's Copy Star" || itemName() === "Kirby's Spit Star"
        }
      >
        <KirbyStar item={props.item} />
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
    </Switch>
  );
}

function SamusChargeshot(props: { item: ItemUpdate }) {
  // charge levels go 0 to 7
  const hitboxesByChargeLevel = [300, 400, 500, 600, 700, 800, 900, 1200];
  return (
    <>
      <circle
        cx={props.item.xPosition}
        cy={props.item.yPosition}
        r={hitboxesByChargeLevel[props.item.chargeShotChargeLevel] / 256}
        fill="darkgray"
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
  // face: props.item.peachTurnipFace
  const ownerState = createMemo(() => getOwner(props.item).state);
  const held = createMemo(
    () => props.item.state === 0 || props.item.state === 4
  );
  return (
    <>
      <circle
        cx={held() ? ownerState().xPosition : props.item.xPosition}
        cy={held() ? ownerState().yPosition + 8 : props.item.yPosition}
        r={600 / 256}
        fill="darkgray"
        opacity={held() ? 0.5 : 1}
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

// The hat-drop star (0x34) and the spit star (0x35): a spinning five-point
// star at the item position, sized by the item's role.
function KirbyStar(props: { item: ItemUpdate }) {
  const radius = createMemo(() =>
    itemNamesById[props.item.typeId] === "Kirby's Spit Star" ? 4 : 2.5
  );
  const points = createMemo(() => {
    const spin = (props.item.frameNumber * 12) % 360;
    const outer = radius();
    const inner = outer * 0.45;
    const pts: string[] = [];
    for (let i = 0; i < 10; i += 1) {
      const r = i % 2 === 0 ? outer : inner;
      const angle = ((spin + i * 36) * Math.PI) / 180;
      pts.push(
        `${props.item.xPosition + r * Math.cos(angle)},${
          props.item.yPosition + r * Math.sin(angle)
        }`
      );
    }
    return pts.join(" ");
  });
  return <polygon points={points()} fill="gold" stroke="black" stroke-width={0.3} />;
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
