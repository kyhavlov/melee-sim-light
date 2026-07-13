# Candidate: Motion-State Execution Kernel

Verdict: required governing architecture. Phase 1 introduced the Coll slice; Phase 2 completes it
through the causal fighter-execution/contact chain rather than a standalone scheduler packet.

## Source contract

Melee's fighter lifecycle is compact conceptually:

1. `Fighter_ChangeMotionState` installs a MotionState, initializes live callback identities, applies
   callsite transition semantics, initializes animation/script state, and resets callback-owned
   transients.
2. Ordered fighter processes advance timers and animation.
3. Exactly one installed Anim callback runs.
4. Current input is published and exactly one installed IASA callback runs.
5. Exactly one Phys callback runs, followed by shared integration.
6. Exactly one Coll callback runs.
7. Accessory, grab, collision, and ProcessHit lanes run at their registered priorities.

MotionState callbacks are defaults, not immutable row lookups. Source code can overwrite live
callbacks after entry (for example Escape and ItemThrow collision callbacks). The important phase
invariant is:

> Resolve the live installed callback once at phase entry. If it changes motion state, do not invoke the
> destination callback recursively in that same phase. The next phase resolves the new row.

The relevant process spine is:

| Priority | Source owner |
|---:|---|
| 0 | timers and hitlag entry/exit callbacks (`Fighter_8006A1BC`) |
| 1 | AObj/script advance and one live Anim callback (`Fighter_8006A360`) |
| 3 | controller publication and one live input/IASA callback (`Fighter_Spaghetti_8006AD10`) |
| 4 | one live Phys callback and shared integration (`Fighter_procUpdate`) |
| 6 | ECB-lock decrement and one live Coll callback (`Fighter_procMap`) |
| 8 | accessory2/accessory1, or accessory3 during hitlag |
| 9 | accessory4, then fighter collision-primitive refresh |
| 12 | catch/grab resolution |
| 13 | fighter collision traversal and pending-contact selection |
| 14 | `Fighter_ProcessHit` |

Source anchors:

- `refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState`
- `refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360,Fighter_procUpdate,Fighter_procMap}`
- `refs/melee/src/melee/ft/types.h::MotionState`
- `refs/melee/src/melee/ft/ftaction.c::{ftAction_80073240,ftAction_80073354,ftAction_8007349C}`

## Current evidence

- A strict text inventory finds at least 246 direct writes to `state.action_id` and 305 direct
  writes to `state.animation_index`, spread across 19 runtime files.
- There are 217 occurrences of the main animation-entry helper and 17 of its policy wrapper.
- `Fighter_ChangeMotionState` is referenced 317 times across 42 source files, often in comments
  explaining a local reconstruction of one side effect.
- The exact `MSLMSO01` callback-ID accessors exist, but gameplay does not use the Anim, IASA, Phys,
  or Coll IDs to initialize live dispatch lanes.
- `anim_timebase.c` mixes the generic AObj clock with action-specific landing, throw, turn,
  run-brake, smash, capture, common-fall, and special logic.
- `move_tables.h` exposes about 60 semantic queries over the typed script timeline instead of one
  causal command interpreter.

The partial entry helper currently owns only a subset of source behavior: facing snapshot,
fastfall approximation, several local transient clears, attack identity, instance identity, and a
source timer. It does not receive the procedural transition flags.

## The flag distinction

This is the clearest correctness fault in the current entry model.

```text
MotionState.x4_flags
  -> row identity metadata passed to ft_800895E0

Fighter_ChangeMotionState(..., flags, ...)
  -> behavior chosen by this specific entry call
```

The bit namespaces overlap, but the values are not interchangeable. A destination can be entered
by multiple call sites with different procedural flags. Current code knows this and contains
exceptions in hitbox, landing, throw, and fastfall paths.

The replacement API must therefore require explicit procedural flags:

```c
msl_fighter_change_motion_state(batch, idx,
                                destination_action,
                                transition_flags,
                                anim_start,
                                anim_speed,
                                anim_blend);
```

The name and final argument shape are not decided. The semantic requirement is.

## Target ownership

The central entry function should perform the RL-relevant source sequence, omitting only proven
render/audio-only work:

- install action, submotion, row flags, motion word, and live callback identities;
- snapshot entry facing and position-owned state;
- apply procedural preservation/clear flags to HitCapsules, throw exception, hurt/colanim state,
  fastfall, floor skip, command state, and other modeled fighter flags;
- update attack identity and action instance identity in source order;
- initialize the AObj/script cursor and command variables;
- apply start-frame command semantics (`SkipAnim`, `UpdateCmd`, immediate ticks) exactly;
- clear accessory/contact callbacks and modeled motion-local transients;
- expose a small, explicit callback-local setup surface for the source entry helper to run after
  the generic transition.

The central function should mutate the live source objects immediately. It should not set a broad
`entered_this_frame` bit and ask downstream publishers to reconstruct all of its effects later.

## Callback dispatch

Translate extracted callback symbols to generated stable handler kinds during data generation, then
store those kinds in per-fighter live callback lanes. Raw numeric IDs are currently assigned from
an alphabetically sorted symbol set and can shift when that set changes; runtime code must not
hardcode them. Use compact switches over the live kinds and avoid C function pointers if they harm
batching or code generation.

The runtime must honor explicit source callback overrides after motion entry. Dispatching by the
current action row on every phase would silently undo those overrides and is not source-shaped.

A migrated callback family should have four explicit handlers where source does:

- Anim handler;
- IASA handler;
- Phys handler;
- Coll handler.

Shared decomp helpers should remain shared C helpers. Distinct extracted symbols may translate to
the same stable handler kind only when source behavior is actually identical for the supported
domain.

## Script execution

For migrated actions, keep a persistent command cursor and execute `MSLFTSC1` events once when the
source timebase crosses them. Live outputs include:

- command variables;
- interrupt and jab flags;
- throw flags/pulses;
- HitCapsule definitions and victim-list lifecycle;
- hit/hurt status;
- airborne-state commands;
- smash charge and throw hitboxes.

This replaces “active at frame” queries and whole-history replay. Native reseed preprocessing may
derive the cursor and live command state from replay history once.

## Migration rule

Do not convert every callback to a new dispatcher while leaving every old subsystem pass intact.
For each vertical family:

1. add the handler identities needed by that family;
2. route its entry and callbacks through the kernel;
3. make old passes explicitly skip migrated owners;
4. delete the old branch, fresh-entry markers, semantic classes, and bridge lanes;
5. run focused locks and diagnostic validation, then continue through the connected source family.

Map collision is the first architectural cutover and is complete. It proves live Coll ownership but
does not complete the Anim/IASA/Phys or contact chain. Phase 2 must finish the causal entry,
AObj/script/HitCapsule, priority-9 primitive, priority-12/13 selection, and priority-14 ProcessHit
surface together. It may expand through any connected common or character callback required to
make that source chain real; it must not preserve a partial lifecycle behind compatibility lanes.

## Expected deletion

The kernel itself may add code initially. Its value is the deletion it enables across:

- action/animation assignments and entry helpers;
- callback-family class bits;
- `prev_action`/negative-frame/fresh-entry inference;
- deferred animation tick policies;
- post-frame state-flag repair;
- hitcapsule preservation exceptions;
- specialized script-window query APIs.

A packet that adds the kernel but deletes no old owner is not a useful checkpoint.

## Risks and locks

- Same-phase callback recursion is easy to introduce accidentally.
- Some source callbacks are installed dynamically outside MotionState rows; accessory and hitlag
  callback lanes need explicit state.
- Callback numeric IDs must be stable across extraction or translated to stable generated kinds.
- Replay seeds do not expose the command cursor or all motion-local variables. Preprocessing must
  initialize real state, not create first-frame gameplay branches.
- Naive indirect dispatch or full-batch scans per callback family can regress performance.

Synthetic tests should cover same-action re-entry, procedural flags differing from row flags,
Anim-to-destination-IASA handoff, skipped animation, immediate command execution, and multiple
motion entries in one frame.
