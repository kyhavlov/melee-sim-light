# Packet 3: Damage, Down, and Passive

Status: complete.

Baseline: `452a143f` (`Rewrite common grounded map collision`).

## Delivered

- Stable live handlers cover Damage, DamageFly/FlyReflect, DamageFall, DownDamage, DownReflect,
  DownBound, DownWait/Stand/Spot/Attack, Passive, PassiveStand, PassiveWall, and PassiveCeil.
- Damage, Down, and passive callbacks consume the shared persistent CollData kernel and publish
  transitions immediately; no late floor/ground/wall coordinator reruns their geometry.
- The `ftCo_Damage_Coll` ladder is centralized: high-KB DownBound, medium-KB Landing, then the
  low-KB same-motion ground transfer. DamageFly/Fall, DownDamage, and DownReflect retain their
  distinct source ladders.
- ProcessHit preserves the outgoing map callback's current/previous ECB on entry, while
  `Fighter_ChangeMotionState` supplies the destination JObj desired ECB. The destination desired
  packet is not incorrectly frozen through hitlag.
- Damage landing floor publication synchronizes Fighter root, CollData `last_pos`, and substep
  current state so the next DownBound callback cannot sweep from a pre-snap root inside the stage.
- Wall tech, ceiling tech, fly-reflect, floor loss, hitlag/SDI, and knockdown handoffs use exact
  callback identity and live state rather than replay-row predicates.

## Result

The retained mechanics are decomp-backed owner behavior, including fixes that do not necessarily
move a current validation row. Source-wrong modelplay-only floorhug locks were deleted rather than
restoring the displaced coordinator's compensating path. Remaining isolated Damage/Down rollout
breaks are follow-up entries in `../residuals/phase1-validation.md`, not alternate runtime logic.
