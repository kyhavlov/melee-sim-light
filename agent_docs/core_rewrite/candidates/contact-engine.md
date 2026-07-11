# Candidate: Stateful Fighter Contact Engine

Verdict: recommended second vertical rewrite; highest eventual combat payoff, but broader and more
ordering-sensitive than map collision.

## Boundary

The correct owner is larger than damage calculation:

```text
causal MSLFTSC1 execution
  -> persistent HitCapsules and command state
  -> priority-9 hitbox endpoint refresh and source-equivalent lazy geometry
  -> priority-12 catch owner
  -> priority-13 traversal and bounded per-fighter DmgLog scratch
  -> persistent pending-contact fields
  -> priority-14 Fighter_ProcessHit
```

Items, throws, and character-specific contacts must enter the same source pending-field/aftermath
ordering where source does. A temporary adapter packet is acceptable during cutover, but a generic
persistent packet queue is not the final source shape. DamageFlyRoll RNG policy remains a separable
site and should not distort the deterministic contact architecture.

## Current scale

The audited fighter-contact runtime is about 22,985 C LOC plus roughly 1,342 lines of headers. The
deterministic subtotal after excluding the 3,086-line `combat_damageflyroll.c` policy surface is
about 19,899 runtime LOC. Major files include:

- `combat.c`: 8,016 LOC;
- `combat_body.c`: 4,016 LOC;
- `hitboxes.c`: 3,405 LOC;
- `combat_damageflyroll.c`: 3,086 LOC;
- hitlists, shields, hurtboxes, and reflector paths.

The corresponding source owner is concentrated in approximately 5-7k relevant LOC across:

- `refs/melee/src/melee/ft/ftcoll.c`;
- bounded `refs/melee/src/melee/lb/lbcollision.c` primitives;
- `refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC`;
- bounded `ftaction.c`, `ftCo_Guard.c`, and `ftCo_Damage.c` behavior.

The deterministic surface still has a large deletion opportunity, but the 8-12k estimate must be
reassessed as source slices land; it must not imply deletion of the separately retained RNG policy.

## Structural faults

### 1. Hitbox snapshots are rebuilt instead of mutated

`hitboxes_refresh()` scans the extracted timeline through the current animation frame and derives
current and previous hitbox snapshots. It then reconstructs enabled edges, groups, victim lists,
frozen hitlag carry, same-action restarts, and callsite-flag exceptions.

Source script commands mutate four persistent `HitCapsule` slots. Victim lists clear/copy only on
specific disabled-to-enabled or hit-group changes. `Fighter_ChangeMotionState` applies its
procedural `SkipHit` rule at transition time.

`MSLFTSC1` contains most required typed events, but it is not yet complete enough to retire the
older timeline: Falcon's checked-in manifest records two omitted `remove_hitbox` events, and the
extractor/runtime payload contract for `remove_hitbox` and `set_hitbox_size` needs completion. Phase
2 begins with that artifact audit, then retires the older timeline only after parity is proven.

### 2. Traversal and mutation order differ from source

Melee's relevant order is:

- priority 12: catch/grab;
- priority 13: fighter collision and DmgLog selection;
- priority 14: `Fighter_ProcessHit`.

Selection is stable and mutates victim lists immediately. Shield admission precedes BODY
fallthrough for a candidate. Current code uses attacker-oriented passes, unordered-pair clank
scratch, deferred damage scratch, and later application. Simultaneous reciprocal contacts therefore
require compensating mechanisms.

Catch is a separate priority-12 owner (`Fighter_UnkProcessGrab_8006CA5C`) which invokes grab
callbacks directly. It shares live HitCapsule/hurt geometry but does not enter the DmgLog or
ProcessHit ladder.

### 3. ProcessHit ownership is distributed

Ordinary combat, item, throw, and special paths directly apply resolved state in many places.
`combat_processhit_consume()` explicitly models only a subset of the source late consumer. Falcon
Dive needs a bespoke delayed reciprocal packet because the general ordering owner is absent.

Source resets `dmg_log0[20]` and `dmg_log1[20]` inside each priority-13 fighter-collision invocation,
then `ftColl_8007AB48`/`ftColl_8007AB80` collapse them into persistent fighter pending fields. Those
fields, not DmgLog arrays, survive to priority 14. The rewrite should reproduce that lifetime and
the ProcessHit precedence ladder rather than retain a universal packet queue.

### 4. Geometry is published more than once

Hurtbox, BODY, shield, and reflector code independently derives overlapping pose/capsule state and
contains local action/stage corrections. Source has ordinary HitCapsules, including Catch-element
capsules; there is no separate `CatchCapsule` object. `ShieldDesc` and `ReflectDesc` are setup
descriptors, while live contact state uses shield/reflect/absorb `HitResult` and attribute lanes.

Ordinary HitCapsule endpoints refresh at priority 9 through
`Fighter_8006C80C -> ftColl_8007AE80`. Hurt/shield/reflect positions are lazily materialized by
`lbColl` using `skip_update_pos`. A simulator may cache canonical primitives eagerly only after
proving that representation phase-equivalent; it should not describe eager publication as the
literal source owner.

### 5. Replay repair participates in selection

Hitbox and hitlist code contains substantial seed/provenance branching. The native preprocessor
should initialize concrete capsule and victim-list state once. Free-running contact selection must
not know that a replay seeded it.

## Target design

1. Complete and audit the MSLFTSC1 payload contract, then execute every supported
   contact-producing motion's script incrementally into live command and HitCapsule state. This
   coverage is required even when that motion's Anim/IASA/Phys callbacks migrate later in Phase 3.
2. Refresh ordinary HitCapsule endpoints at the source-equivalent priority and model lazy
   hurt/shield/reflect geometry, or prove an eager cache equivalent.
3. Port the bounded `lbColl` overlap and victim-list functions with source field order.
4. Keep priority-12 catch selection/callbacks separate from damage aftermath.
5. Traverse fighters, entities, and slots in stable priority-13 source order, using bounded
   `dmg_log0[20]`/`dmg_log1[20]` scratch and immediate victim-list mutation.
6. Collapse selected results into persistent fighter pending fields, then run the priority-14
   ProcessHit precedence ladder.
7. Convert item, ordinary throw, scripted throw, thrown-body, shield, reflector, and special
   producers to that shared source ordering. Temporary adapters must disappear with each cutover.
8. Delete snapshot rebuilding, pairwise clank approximation, geometry bridges, and delayed special
   aftermath as their source owners land.

The current DmgLog scratch capacity is 16 while source arrays use 20; the rewrite must use the
source capacity and per-current-fighter lifetime rather than preserve the smaller incidental array.

The live fighter capsule inventory must distinguish:

- four ordinary `x914` HitCapsules;
- two scripted throw payloads in `xDF4`;
- the separate thrown-body capsule at `x1064`.

Thrown-body versus thrown-body collision also has its own priority-13 slot (`ft_8007C4BC`).

## First reviewable cutover

Cover ordinary fighter BODY, shield, and clank contact through priority-13 pending-field selection
and priority-14 ProcessHit. Cut over priority-12 catch separately while reusing the same live
HitCapsule/geometry substrate. Route existing item and throw damage producers through temporary
adapters into the correct pending-field order if their full source traversal lands in the following
pending-field path. The migrated ordinary path must not fall back to old selection.

Synthetic locks should cover:

- doubles and non-adjacent ports;
- simultaneous reciprocal hits;
- shield-before-BODY precedence;
- clank and rebound ordering;
- per-HitCapsule victim-list clear/copy/rehit behavior;
- multiple DmgLogs and source capacity;
- hitlag-frozen capsules;
- motion entry with row flags differing from procedural flags;
- item and throw adapter/pending-field ordering.
- stale-queue update/writeback and attack/instance attribution;
- phantom versus full-hit selection;
- ProcessHit precedence among incoming damage, phantom, shield, clank, outgoing damage,
  reflect/absorb, and detect callbacks;
- ordinary, scripted-throw, and thrown-body capsule separation.

## Expected validation leverage

Directly contact-shaped one-step fields contain more than 2,500 correlated mismatch observations,
before counting Damage, Guard, Rebound, action, animation, and state-flag consequences. They are a
strong leverage signal, not 2,500 distinct causal defects.

## Why it follows map collision

- It benefits from the central transition and exact phase-dispatch pattern proven by the first
  rewrite.
- Causal script execution can reuse the same motion-entry ownership rather than inventing a combat
  entry layer.
- It has more multi-actor and cross-system ordering risk.
- `combat_damageflyroll.c` contains a large RNG/reseed policy surface that remains outside the
  deterministic contact cutover rather than blocking it.
- Map collision is also a major performance hotspot and has a more concentrated newer-character
  validation signal.

This ordering does not imply combat is less important. It avoids building the new contact engine on
top of the same partial lifecycle that caused many of its current exceptions.

## Risks

- Mature Fox/Falco controls are sensitive to victim-list and stable-order mistakes.
- Source order for doubles, simultaneous hits, shields, and items must be tested explicitly.
- A temporary packet adapter can become a permanent second path if deletion is not part of each
  cutover.
- Replay preprocessing must initialize live capsule lists without persistent validation caches or
  per-frame Python work.
- Float operation order is relevant only when it changes a discrete branch; bit-exactness remains
  out of scope.
