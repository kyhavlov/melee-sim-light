# Architecture Baseline

This inventory records why a first-principles rewrite is justified and which existing substrates
should be reused.

## Scale

`src/` contains 140,238 lines of C and headers. `src/state.h` is 1,642 lines and declares about
635 pointer-backed state lanes. A text inventory finds 136 seed/replay/provenance references in that
file alone.

The candidate surfaces are not perfectly disjoint, but their scale is informative:

| Surface | Current size | Source comparison |
|---|---:|---:|
| map collision (`mpcoll_*`, stage collision, ledge, staging) | about 28k LOC | about 9-10k relevant LOC |
| fighter contact runtime | about 23k C LOC plus 1.3k headers; about 19.9k deterministic subtotal excluding DamageFlyRoll policy | about 5-7k relevant LOC |
| `locomotion.c` + `action.c` + `physics.c` | 11,897 LOC | about 8,782 selected common-action LOC |
| animation/script/hit/hurt/state publication cluster | about 13.5k LOC | source spread across `fighter.c`, `ftanim.c`, `ftaction.c`, and collision owners |
| grab/capture/throw | about 4,350 LOC | comparatively close to source shape |

These are architectural estimates, not line-for-line equivalence claims. The simulator includes
SoA plumbing and deterministic data access that the game does not; the decomp includes rendering,
audio, and unsupported behavior that the simulator should omit.

## Repeated structural faults

### 1. Global subsystem passes substitute for installed callbacks

Melee stores one Anim, IASA, Phys, Coll, and Cam callback from the current MotionState. Its ordered
fighter processes call exactly one callback in each phase. The simulator instead runs broad module
passes and action switches in sequence.

This lets later modules observe a state entered by an earlier module in the same logical phase. The
repair vocabulary is visible throughout the runtime: frame-start snapshots, destination callbacks,
fresh-entry exclusions, consumed flags, deferred ticks, and module-owned velocity booleans.

### 2. Snapshot reconstruction substitutes for persistent source state

Examples:

- `hitboxes_refresh()` replays script history through the current frame and rebuilds live slots;
- state-flag publication infers transition clears from current/previous action combinations;
- collision reconstructs source phases and roots through late packets;
- many command-script consumers ask whether a semantic window is active instead of executing a
  command once into live state.

This makes replay reseed convenient but free-running ownership ambiguous. Source objects should be
initialized once and then mutated causally.

### 3. MotionState row flags and transition flags are conflated

The decomp has two distinct values:

- `new_motion_state->x4_flags`, used by identity bookkeeping such as `ft_800895E0`;
- the procedural `flags` argument passed by a specific `Fighter_ChangeMotionState` call, used for
  `SkipHit`, `KeepFastFall`, `SkipAnim`, and other transition behavior.

`msl_motion_state_enter_side_effects()` explicitly says it does not plumb the procedural flags and
approximates fastfall ownership from the row flags. Other code then adds exceptions:

- AttackAir rows contain `SkipHit`, while ordinary AttackAir entry passes only `KeepFastFall`;
- LandingAir rows contain `SkipHit`, while the landing helper enters with `Ft_MF_None`;
- Throw rows contain `SkipHit`, while common throw entry also passes zero.

This distinction alone explains a substantial amount of hitcapsule and transition-entry machinery.

### 4. Replay/evaluator provenance leaks into gameplay

Across `src/`, 54 files mention teacher-forced, replay-seed, seed-bridge, rollout-bridge, or similar
ownership, with roughly 588 matching references. Some are legitimate comments or initialization
surfaces. Others directly branch on rollout reseed state in collision and combat gameplay.

The replacement rule is strict: native preprocessing may initialize a real hidden source field;
normal gameplay may not choose physics or collision behavior because it is the first frame after an
evaluator reseed.

### 5. Post-frame publication repairs earlier ownership

`state_flags.c`, `hitboxes.c`, `hitlist.c`, and related files contain many conditions of the form
“if action changed from X to Y, emulate the reset which `Fighter_ChangeMotionState` would have
performed.” This is downstream reconstruction of an event the simulator itself initiated. The
reset belongs at the transition point.

## Existing data substrates to retain

### `MSLMSO01`: MotionState rows

The artifact already contains, for every supported `(character, action)` row:

- submotion ID;
- row `x4_flags` and motion word;
- exact Anim, IASA, Phys, Coll, and Cam callback IDs.

The exact callback accessors exist in `src/motion_state_owners.c`, but the gameplay runtime does not
use them. Motion entry should translate the extracted row identities into stable live callback
handler kinds; later source overrides must be able to replace those live lanes. Instead, current
extraction derives 62 semantic class bits from manually curated callback-symbol sets. Those classes
were useful during incremental development; exact live callback dispatch should delete most of
them as each owner family migrates.

### `MSLFTSC1`: typed fighter script events

The artifact schema exposes 21 typed gameplay event kinds, including hitbox create/update/clear,
command variables, throw flags, interrupt enable, hit/hurt state, airborne state, smash charge, and
throw hitboxes. It is small: roughly 28-36 KiB per supported character. The checked-in Falcon
manifest still records two omitted `remove_hitbox` payloads, and payload support for
`set_hitbox_size`/`remove_hitbox` must be audited before the older hitbox timeline can be retired.

The current `move_tables.h` exposes about 60 specialized semantic query APIs over those events.
Migrated actions should instead have one command cursor and live command state. The typed artifact
is adequate for that interpreter; it should not be replaced by another parallel timeline.

### `MSLSTG01`: stage map data

The stage artifact already carries raw endpoints, stable segment IDs, line flags, links, joint IDs,
normals, platform transforms, and path metadata. The map-collision rewrite should begin with a field
audit, but most of the source `mpLib` inputs already exist. Missing source fields should extend the
one artifact contract rather than become stage or action predicates.

### Pose, parts, hurtcaps, and item/article tables

`MSLPART1`, extracted animation/pose data, hurtcaps, shields, and item/article tables remain the
right immutable inputs. The fault is not data-driven geometry; it is duplicated publication and
history reconstruction around that data.

## Source anchors

- Motion entry and process order: `refs/melee/src/melee/ft/fighter.c`
- MotionState row definition: `refs/melee/src/melee/ft/types.h::MotionState`
- Common and character MotionState tables: `refs/melee/src/melee/ft/ftmotionstates.c` and character
  `*_Init.c` files
- Script interpreter: `refs/melee/src/melee/ft/ftaction.c`
- Animation timebase: `refs/melee/src/melee/ft/ftanim.c` and HSD AObj sources
- Map collision: `refs/melee/src/melee/mp/{mpcoll.c,mplib.c}` and
  `refs/melee/src/melee/ft/ft_081B.c`
- Fighter contact: `refs/melee/src/melee/ft/ftcoll.c`, bounded `lb/lbcollision.c`, and
  `Fighter_ProcessHit_8006D1EC`

## Consequence for implementation

The program should not begin by making existing switches prettier. It should introduce the missing
source object or process, move one complete owner family onto it, and delete every displaced
semantic class, bridge, and late repair in the same reviewable packet.
