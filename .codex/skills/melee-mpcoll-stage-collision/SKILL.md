---
name: melee-mpcoll-stage-collision
description: Use when investigating Melee mpColl, ECB, floor publication/rejection, soft-platform admission, ledges/cliffs, transformed platforms, or stage-line collision ownership.
---

# Melee mpColl / Stage Collision

## Purpose
Use this skill to keep collision fixes source-shaped. The goal is to identify whether the owner is common mpColl ordering, ECB geometry, stage-line metadata, platform transform state, ledge/cliff logic, or a MotionState collision callback.

## Stable Model
- mpColl decisions are phase-sensitive: candidate collision, floor publication, rejection, and final fighter state are distinct steps.
- ECB geometry is source state. Width, bottom, top, root position, and transformed coordinates should not be adjusted with replay-fit clamps.
- Soft platforms and hard floors have different admission rules.
- Ledge/cliff behavior is procedural; do not replace it with a stage id or action id proxy when source ordering is needed.
- Stage-specific behavior should usually be expressed through stage-line metadata, transformed platform state, or callbacks, not raw stage id.
- Seeded provenance is transient unless live runtime reasserts it.

## Investigation Procedure
1. Determine the first collision-owned field that diverges: ground id, floor/air state, pos_y projection, ECB endpoint, ledge state, wall/ceil result, or raw collision flag.
2. Classify the involved surface:
   - hard floor,
   - soft platform,
   - ledge/cliff,
   - wall/ceiling,
   - transformed/moving platform.
3. Trace the MotionState collision callback and mpColl helper path in decomp/asm.
4. Check whether extracted stage-line data already expresses the distinction.
5. Prove publication lifetime:
   - what candidate was admitted,
   - what candidate was rejected,
   - when published floor state is cleared or carried.
6. Add tests for both the admitted case and adjacent rejected case.

## Guardrails
- Do not fix collision with broad y clamps or tolerances.
- Do not let one-step seed provenance stale-carry into later rollout frames unless source state carries it.
- Do not make FoD/Battlefield/etc. stage-id branches when generated stage geometry or line metadata can express the owner.
- If transformed-platform behavior is involved, compare against non-transformed and cardinal stages before retaining the owner.

## Updating This Skill
Update only for stable mpColl/ECB/stage-collision facts confirmed from decomp, asm, or extracted stage data. During final handoff for collision/platform/ledge work, proactively note any newly confirmed stable collision fact or repeatable investigation pattern that belongs here; if the task already includes skill/doc cleanup, update the skill directly. Do not add current helper names, validation counts, or temporary bridge descriptions.
