# RL 1.0 Completion Checklist

This checklist is the current family-level tracker for **finish-the-sim** work:
close shared owner families with decomp-first passes, leave only narrow
decomp-justified residuals, and avoid row-shaped bridge fixes unless the owner
family is already closed.

Last updated:
- Date: 2026-04-14
- Scope baseline: shared throw/thrown substrate and common damage-owner family effectively closed; checklist reflects post-closure RL 1.0 priority ordering.

## Status Legend

- **Closed / effectively closed**: the shared owner path is in place; remaining behavior is narrow per-move or adjacent-family detail.
- **Active / next deep pass**: a shared owner family that still needs a completionist decomp audit and cleanup pass.
- **Residual cleanup**: the shared owner is largely closed; remaining work is narrow and should stay within that family boundary.
- **Blocked / split first**: do not patch runtime yet; first make the family seed-visible, decomp-visible, or split out of a mixed bucket.
- **Bridge**: compensating logic that exists because a shared owner path is still missing or mis-owned.
- **Residual**: a real move-specific or adjacent-family difference that should remain if decomp supports it.

## Priority Order

Recommended sequence for the next deep passes:

1. **Capture / grab owner family**
2. **Guard release / guard timer / guardreflect family**
3. **Locomotion / grounded transition / motion-entry timing family**
4. **Knockdown / passive contact owner**
5. **Core combat followup family**
6. **Fox/Falco special-move families**
7. **Ledge / collision-env parity**
8. **Item-owner seed cleanup + item identity family**
9. **Match-flow / entry / respawn ownership**
10. **Mixed-bucket split**

## Closed / Effectively Closed

### Shared throw/thrown substrate
- Status: `closed / effectively closed`
- Owner boundary: shared `Throw*` / `Thrown*` attached ownership, pending-release collision suppression, and shared release callback/timeline path
- Primary sim files: `src/grab_attachment.c`, `src/throw_flow.c`, `src/mpcoll_*`, adjacent seed/runtime support
- Acceptance bar already met:
  - attached/release substrate shared by default
  - throw-core bridges materially removed
  - remaining work is per-throw pulse/article behavior only
- Remaining residuals:
  - `ThrowLw` pulse-family timing owner
  - other decomp-justified per-throw pulse/article tails

### Common damage-owner family
- Status: `closed / effectively closed`
- Owner boundary: `Fighter_ProcessHit` -> `ftCo_8008DCE0` -> `Damage*` / `DamageFly*` / `DamageFlyRoll`
- Primary sim files: `src/combat.c`, `src/timers.c`, `src/mpcoll_ground.c`, seed/runtime support in `src/api.*` and `src/state.*`
- Acceptance bar already met:
  - shared damage-entry owner in place
  - old runtime bridge lanes materially collapsed
  - remaining `DamageFlyRoll` tails treated as explicit residual admission/carry work, not fuzzy bridge debt
- Remaining residuals:
  - `DamageFlyRoll` admission/carry tails
  - adjacent source-clear / match-flow lanes outside this owner boundary

## Active / Next Deep Passes

### 1. Capture / grab owner family
- Status: `active / next deep pass`
- Roadmap families: `F03_capturewait_bridge`
- Owner boundary: `CatchPull`, `CapturePulled`, `CaptureWait`, attach-point selection, victim callback ordering, pummel / breakout adjacency
- Primary sim files: `src/grab_flow.c`, `src/grab_attachment.c`, `src/api.*`, `tools/slippi/seed_history.py`, `tools/slippi/make_dataset_from_slp.py`
- Acceptance bar:
  - first-steady `CaptureWait` lane is seed-visible when required
  - replay-shaped victim timer carries are removed
  - grab victim attach/callback ownership is shared by default

### 2. Guard release / guard timer / guardreflect family
- Status: `active / next deep pass`
- Roadmap families: `F01_guard_release_collision`, `F02_guard_timer_flags`, parts of `F15_guard_item_ownership`
- Owner boundary: `Guard`, `GuardSetOff`, `GuardReflect`, shield-release ordering, shield-hit followups, reflect timing, timer/state-flag ownership
- Primary sim files: `src/combat.c`, `src/items.c`, `src/timers.c`, `src/step.c`
- Acceptance bar:
  - shield-hit followups owned by one coherent guard family path
  - timer/state-flag behavior settles at the animation callback boundary
  - item/laser interactions no longer force broad guard bridges

### 3. Locomotion / grounded transition / motion-entry timing family
- Status: `active / next deep pass`
- Roadmap families: `F10_grounded_transition_resolution`, `F11_locomotion_action_frame`, `F12_instance_id_transition_only`
- Owner boundary: Dash / Walk / Turn / KneeBend / motion-entry selector timing, pure motion-entry instance-id bumps, grounded selector timing
- Primary sim files: `src/anim_timebase.c`, `src/step.c`, `src/match_flow.c`, `src/api.c`
- Acceptance bar:
  - motion-entry ownership is shared and deterministic
  - pure timing rows are not spread across unrelated guard/combat fixes
  - surviving locomotion tails are narrow residuals only

### 4. Knockdown / passive contact owner
- Status: `active / next deep pass`
- Roadmap families: `F07_knockdown_grounding`
- Owner boundary: DamageFly contact owner for Passive / PassiveStand / DownBound / landing selection
- Primary sim files: `src/mpcoll_ground.c`, `src/ledge.c`, `src/locomotion.c`
- Acceptance bar:
  - one damage-fly contact owner chooses grounded outcomes consistently
  - no ledge or ECB regressions

### 5. Core combat followup family
- Status: `active / next deep pass`
- Roadmap families: `F08_damage_resolution_combat`, `F09_aerial_combat_resolution`
- Owner boundary: BODY hits, aerial continuation, hitlag/hitstun/continuation ordering after damage admission
- Primary sim files: `src/combat.c`, `src/timers.c`, `src/items.c`, `src/step.c`
- Acceptance bar:
  - BODY-hit and aerial followup ownership is coherent
  - shared combat continuation paths replace row-shaped followup fixes

### 6. Fox/Falco special-move families
- Status: `active / next deep pass`
- Roadmap families: `F13_specialhi_landing` plus mixed special-move slices currently split across `F99`
- Owner boundary: `SpecialN`, `SpecialS`, `SpecialHi`, `SpecialLw`, `ThrownLw`, and their article/pulse/landing owners
- Primary sim files: `src/items.c`, `src/combat.c`, `src/anim_timebase.c`, `src/locomotion.c`, character-specific modules
- Acceptance bar:
  - special moves are grouped by decomp owner, not patched row-by-row
  - `SpecialHi` landing/fall and `SpecialLw` / `ThrownLw` pulse behavior live in named family work, not misc buckets

## Remaining Major Owner Families

### Ledge / collision-env parity
- Status: `active`
- Owner boundary: ledge occupancy, refresh, grab-mask ownership, FD edge behavior
- Primary sim files: `src/mpcoll_env.c`, `src/ledge.c`, `src/step.c`
- Acceptance bar:
  - edge behavior owned by collision substrate rather than action-local exceptions

### Item-owner seed cleanup + item identity family
- Status: `active`
- Roadmap families: `F14_throw_item_bookkeeping`, `F15_guard_item_ownership`, `F16_item_identity_residual`
- Owner boundary: item owner/slot identity, throw-item bookkeeping, guard-hit item attribution, seed/runtime ownership split
- Primary sim files: `src/items.c`, `src/combat.c`, `src/api.*`, `tools/slippi/seed_history.py`
- Acceptance bar:
  - item rows stop masquerading as combat/guard rows
  - remaining item identity behavior is deterministic residual cleanup

### Match-flow / entry / respawn ownership
- Status: `active`
- Roadmap families: `F04_match_flow_rebirth`, adjacent identity-reset rows
- Owner boundary: entry, respawn, stock reset, rebirth, identity reset, match-flow state flags
- Primary sim files: `src/match_flow.c`, `src/api.c`, `src/timers.c`
- Acceptance bar:
  - respawn/entry ownership is grouped as one family
  - rebirth parity no longer depends on scattered timer repairs

## Residual Cleanup Families

### Locomotion timing tails
- Status: `residual cleanup`
- Roadmap families: `F11_locomotion_action_frame`
- Boundary: narrow timer/callback cleanup after larger locomotion owners settle

### Grounded transition tails
- Status: `residual cleanup`
- Boundary: surviving Dash/Walk/Turn/KneeBend timing edges after the shared selector family closes

### SpecialHi landing / fall continuation
- Status: `residual cleanup`
- Roadmap families: `F13_specialhi_landing`
- Boundary: narrow `SpecialHi` continuation work once the broader special-move family is decomp-shaped

### DamageFlyRoll admission / carry tails
- Status: `residual cleanup`
- Boundary: jump-aerial / `AttackAirB` carry behavior after the common damage-owner closure

### Throw / item pulse tails
- Status: `residual cleanup`
- Roadmap families: `F14_throw_item_bookkeeping`
- Boundary: throw-item bookkeeping and release-adjacent identity cleanup after shared throw-core closure

### Guard / item attribution tails
- Status: `residual cleanup`
- Roadmap families: `F15_guard_item_ownership`
- Boundary: guard-hit item ownership and shield/laser attribution after the main guard family closes

### Item identity tails
- Status: `residual cleanup`
- Roadmap families: `F16_item_identity_residual`
- Boundary: deterministic slot / instance cleanup after the seed/runtime split is fixed

### Per-throw pulse/article timing edges
- Status: `residual cleanup`
- Boundary: `ThrowLw` pulse-family and other per-throw article timing edges only if decomp proves they are truly per-throw

## Blocked / Split First

### Mixed-bucket split
- Status: `blocked / split first`
- Roadmap families: `F99_misc_other`
- Boundary: mixed residual bucket that still contains unrelated behaviors
- Acceptance bar:
  - split into named families before runtime patching
  - no direct `src/` work should target this bucket as-is

## Out of Scope / Later

- [ ] Broad roster expansion beyond Fox/Falco.
- [ ] 4-player doubles interactions that are not already shared by the current code paths.
- [ ] Camera, rendering, and audio.
- [ ] Low-value cosmetic parity gaps that do not move an owner family.
- [ ] Any new mechanic family unless it is an obvious high-leverage owner boundary.

## Usage / Handoff Rule

- Pick one shared owner family, read the decomp boundary first, replace bridge logic with the shared owner path, and leave only decomp-justified residuals.
- Do not call a family done while it still depends on a bridge or a replay-shaped row fix.
- When a family is effectively closed, add focused replay-real locks, validate, and update this checklist with the remaining residuals only.
