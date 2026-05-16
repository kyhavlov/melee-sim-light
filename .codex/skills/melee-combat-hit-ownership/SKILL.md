---
name: melee-combat-hit-ownership
description: Use when investigating Melee ProcessHit, hitboxes, hit groups, BODY/hurtbox authority, DmgLog, shield/reflect contact, hitlag, hitstun, or damage ownership.
---

# Melee Combat Hit Ownership

## Purpose
Use this skill to decide which source entity owns a combat mismatch: authored hitbox, hit group, hurtbox/BODY geometry, DmgLog/ProcessHit, shield/contact state, item hit, throw hit, or RNG gate.

## Stable Model
- Authored hitbox data and procedural hit-resolution code are separate owners.
- Hit groups matter. A decision based on one hit group should not be made from unrelated active hitboxes.
- BODY/hurtbox authority is not the same as hitbox authority. Contact geometry can decide whether a hit exists before damage math begins.
- DmgLog/ProcessHit bookkeeping controls damage, hitlag, hitstun, stale queues, collision attributes, and source writeback.
- Shield/reflect/contact handling has its own descriptor state and should not be replaced with a generic sphere/extent proxy unless explicitly marked as open debt.
- Item hits and fighter hits can share damage resolution concepts but often differ in source ownership and article/item state.

## Investigation Procedure
1. Identify the first combat-owned divergence:
   - hit/no-hit,
   - target selection,
   - damage/percent,
   - hitlag/hitstun,
   - knockback/action family,
   - shield/contact state,
   - source writeback.
2. Separate detection from resolution:
   - detection: hitbox, hurtbox/BODY, shield/contact geometry, item/article ownership,
   - resolution: ProcessHit/DmgLog/stale/hitlag/hitstun/source writes.
3. Use authored hitbox/script data for static distinctions such as damage, group, id, element, timing, or article.
4. Use decomp/asm for procedural distinctions such as hit selection, shield descriptor state, stale queue, or follow-up action choice.
5. Add a positive lock and a nearby negative lock that proves the retained predicate is not a row-shaped exception.

## Guardrails
- Do not choose hitbox by action id plus slot if hitbox data can express the authored owner.
- Do not scan all hitboxes when a source argument names a specific group.
- Do not conflate replay-real locks that assert known residual mismatches with closed owner positives.
- Do not use broad damage/percent tolerances to hide combat owner gaps.

## Updating This Skill
Add only stable combat-engine facts or durable investigation patterns backed by decomp/asm/extracted data. During final handoff for combat/hit/shield work, proactively note any newly confirmed stable combat owner fact or repeatable analysis pattern that belongs here; if the task already includes skill/doc cleanup, update the skill directly. Keep current simulator refactor details and validation outcomes out of the skill.
