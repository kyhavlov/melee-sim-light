---
name: melee-frame-callback-order
description: Use when investigating Melee frame-step ordering, callback phase ownership, action entry timing, IASA, physics, collision, accessory/item phases, or one-frame early/late state transitions.
---

# Melee Frame Callback Order

## Purpose
Use this skill to reason about *when* vanilla writes a state field. The output should be a phase-order claim backed by decomp, asm, or probe evidence, not a row-shaped simulator patch.

## Stable Model
- MotionState callbacks are source owners. A field mismatch often means the simulator ran the right logic in the wrong phase.
- Action entry and animation-frame progression are separate questions. Matching `action_id` or pose does not prove `action_frame` is written at the same boundary.
- Common phase families to distinguish:
  - input sampling and command interpretation,
  - MotionState animation callbacks,
  - IASA callbacks,
  - physics callbacks,
  - collision callbacks,
  - accessory/item callbacks,
  - post-collision bookkeeping and publication to replay/API state.
- A one-frame error is usually a phase-boundary issue until disproven.
- One-step exactness from a replay seed does not prove rollout causality; rolled-out pre-state and phase history still matter.
- Slippi's `Playback/Core/RestoreGameFrame.asm` hooks GALE01 `0x8006B0DC`: processed inputs are restored after AI/controller sampling and before input edges/timers. Without resync, followers retain game-calculated inputs; RNG restoration is separately gated by resync. Do not conflate these paths with a validator's recorded-RNG policy.

## Investigation Procedure
1. Write the observed mismatch in phase terms: field, actor, frame/record, vanilla value, sim value, and whether the sim is early or late.
2. Identify the MotionState and callback family from decomp or extracted MotionState data.
3. Trace the writer and the neighboring phase calls in:
   - `refs/melee/src/melee/ft/...`
   - `refs/melee/src/melee/gm/...`
   - `refs/melee/build/GALE01/asm/...` when exact instruction order matters.
4. Record the minimal ordering claim:
   - “Field F is written by function G after phase H and before phase I.”
5. Only then change simulator ordering or state lifetime. Keep the patch scoped to that phase owner.

## Guardrails
- Do not infer callback order from current simulator code.
- Do not use an action id or replay row as the owner when the callback family is available.
- Do not fix one row by moving broad logic across phases unless the decomp ordering supports that entire move.
- If ordering is not locally recoverable, add probe/tool evidence before retaining gameplay behavior.

## Updating This Skill
Update this skill only for stable engine facts confirmed by decomp, asm, or reusable probes. During final handoff for callback/phase-order work, proactively note any newly confirmed stable phase-order fact or repeatable investigation pattern that belongs here; if the task already includes skill/doc cleanup, update the skill directly. Do not add current simulator helper names, validation counts, cache versions, or one-replay workarounds.
