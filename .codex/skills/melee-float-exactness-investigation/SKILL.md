---
name: melee-float-exactness-investigation
description: Use when investigating Melee float residuals, ULP-scale drift, f32 operation order, PPC/decomp math, position/velocity integration, or collision projection exactness.
---

# Melee Float Exactness Investigation

## Purpose
Use this skill when float drift is a first-class target. The goal is to identify a source-backed f32 writer/order owner, not to clamp values or relax tolerances.

## Stable Model
- Float-only residuals with matching discrete state can be real bugs.
- Small drift can cascade into later discrete mismatches through position, velocity, collision, hit detection, or blastzone checks.
- PPC instruction order and intermediate f32 stores matter. Algebraically equivalent C can differ by ULPs.
- Source order may involve separate self velocity, attack velocity, root motion, collision projection, and final publication.
- Downstream float errors after a discrete mismatch are usually less actionable than earlier float-only rows.

## Investigation Procedure
1. Start from the earliest discrete-matching float residual, not just the largest downstream residual.
2. Identify the field owner:
   - position integration,
   - self velocity,
   - attack velocity,
   - gravity/friction,
   - root motion,
   - collision projection,
   - percent/shield/item numeric state.
3. Compare likely source writers in decomp; use PPC asm when ULP/order exactness matters.
   When probing replay playback, compare the causal writer before resynchronization:
   `refs/slippi-ssbm-asm/Playback/Core/RestoreGameFrame.asm` can overwrite position,
   facing, action state and RNG from the recording. Later playback agreement is
   not independent proof of the arithmetic that originally produced those values.
4. Check for:
   - fused expressions where vanilla stores intermediate f32,
   - different add/multiply order,
   - physics-before-collision vs collision-before-physics ordering,
   - stale carried state from a prior phase.
5. Retain only bounded source-order fixes with positive and negative locks.

## Guardrails
- No generic clamps to replay values.
- No broad tolerance changes.
- No generic “use fma” or “avoid fma” pass.
- No replay/record gates.
- If exact source order is not locally recoverable, document the missing source evidence instead of fitting constants.

## Updating This Skill
Add only stable f32/PPC investigation patterns or source-backed math facts. During final handoff for float-exactness work, proactively note any newly confirmed stable f32/PPC ordering fact or repeatable investigation pattern that belongs here; if the task already includes skill/doc cleanup, update the skill directly. Keep current float offender rankings and simulator implementation details out of the skill.
