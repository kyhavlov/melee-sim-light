---
name: melee-rng-replay-exactness
description: Use when classifying Melee rollout desyncs involving RNG gates, replay-exact stream phase, DamageFlyRoll selection, item/stage randomness, or RL-vs-replay correctness boundaries.
---

# Melee RNG / Replay Exactness

## Purpose
Use this skill to avoid mistaking replay-exact RNG phase for deterministic gameplay bugs, while still requiring proof before marking a mismatch out of scope.

## Stable Model
- RNG decisions are real source owners.
- RL gameplay correctness usually needs distributional behavior, not exact replay stream phase, unless the simulator explicitly models the full RNG stream.
- A rollout first mismatch can be RNG out-of-scope only if the earliest causal divergence is the RNG-consuming decision itself.
- “One-step exact from real seed” is not evidence that a rollout mismatch is RNG. You must trace the rolled-out segment to the earliest causal divergence.
- Some RNG gates are downstream of deterministic setup. If pre-gate state differs, fix the deterministic owner first.

## Classification Procedure
1. Locate the rollout segment start and first differing gameplay field.
2. Trace backward within the segment to the earliest causal divergence.
3. For RNG classification, prove all of:
   - the source function consumes RNG at that decision,
   - deterministic pre-gate state is aligned or equivalent,
   - the only meaningful difference is the RNG decision outcome,
   - no earlier non-RNG divergence exists in the segment.
4. Record confidence:
   - observed trace: runtime/source trace proves the RNG site was reached,
   - inferred source gate: source says the gate exists but current runtime did not reach or trace it,
   - unclassified: missing or contradictory evidence.
5. Do not alter canonical validation scoring unless the user explicitly asks for a display/report overlay.

## Guardrails
- Do not implement exact-RNG bridges just to erase replay mismatches unless the project explicitly chooses replay-exact stream modeling.
- Do not ignore RNG-labeled rows in canonical validation silently.
- Do not classify a mismatch as RNG if contact, position, hit source, damage setup, grounded/airborne state, or victim ownership diverged earlier.

## Updating This Skill
Update only for stable RNG gate reasoning, source-backed RNG site facts, or classification standards. During final handoff for RNG/replay-exactness work, proactively note any newly confirmed stable RNG gate fact or repeatable classification pattern that belongs here; if the task already includes skill/doc cleanup, update the skill directly. Do not add temporary exception lists, validation counts, or report formatting details.
