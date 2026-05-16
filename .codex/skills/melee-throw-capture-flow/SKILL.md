---
name: melee-throw-capture-flow
description: Use when investigating Melee Catch, Capture, thrown states, throw release scheduling, throw hit callbacks, grab interruption, or victim/thrower ownership.
---

# Melee Throw / Capture Flow

## Purpose
Use this skill for grab and throw bugs where actor ownership can be confusing. The goal is to trace thrower and victim phase ownership before changing runtime behavior.

## Stable Model
- Catch, Capture, and thrown states are multi-actor owners. Thrower callbacks and victim state can both be source authority.
- Throw release timing is phase-sensitive. Release may be scheduled by thrower animation/callback code and consumed by victim/damage/collision code later.
- Capture continuation and interruption often depend on hidden phase/state, not just visible action id.
- Throw hit callbacks can behave like combat hits but are not always owned by ordinary active hitboxes.
- A replay seed may reconstruct hidden throw/capture phase for one-step exactness; that is not automatically source-closed free-running gameplay.

## Investigation Procedure
1. Write the mismatch in actor terms: thrower, victim, action states, timing, and which actor changed first.
2. Trace the thrower MotionState callback and victim Capture/Thrown state callback separately.
3. Identify the release/hit/interrupt boundary:
   - scheduled this frame,
   - pending from an earlier phase,
   - consumed by combat,
   - consumed by collision/action transition.
4. Check whether source data or decoded scripts express the release/hit frame.
5. Add controls for:
   - same throw family positive,
   - adjacent non-throw or non-capture negative,
   - stale pending release not leaking into later gameplay.

## Guardrails
- Do not key throw behavior to a replay record or single action row.
- Do not let reconstructed seed phase persist past the source frame unless the engine state carries it.
- Do not treat one actor’s visible action as proof that the other actor’s callback already ran.

## Updating This Skill
Update only for stable throw/capture engine facts or recurring proof patterns. During final handoff for throw/capture work, proactively note any newly confirmed stable multi-actor flow fact or repeatable proof pattern that belongs here; if the task already includes skill/doc cleanup, update the skill directly. Do not add current simulator lane names, report numbers, or temporary replay-reseed bridges.
