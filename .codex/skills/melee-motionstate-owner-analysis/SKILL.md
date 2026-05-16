---
name: melee-motionstate-owner-analysis
description: Use when deciding whether a mismatch belongs to a Melee MotionState callback/action-family owner, animation/script timeline owner, or a narrower replay row.
---

# Melee MotionState Owner Analysis

## Purpose
Use this skill before retaining gameplay logic keyed by actions. The goal is to identify the broadest source-backed owner: callback identity, action family, animation script event, or explicit state lane.

## Stable Model
- Action ids are evidence, not patch boundaries.
- MotionState callback pointers define behavior families more reliably than local action lists.
- Animation scripts and callback code split ownership:
  - scripts can produce timed events, hitboxes, articles, flags, and rate changes,
  - callbacks can consume those events or write procedural state.
- Adjacent actions with the same callback class often need the same owner unless source data proves otherwise.
- A local action-id list is weaker than a generated callback/script predicate and needs an explicit reason.

## Investigation Procedure
1. Translate the mismatch into gameplay terms: situation, vanilla behavior, sim behavior, timing, and visible consequence.
2. Identify the candidate owner from stable data/source:
   - MotionState callback/class data,
   - decoded script events,
   - fighter part/anchor metadata,
   - item/article constants,
   - stage segment metadata,
   - decomp callback code.
3. Ask whether the owner covers a family:
   - same callback across multiple actions,
   - same script event shape,
   - same article/item kind,
   - same stage-line class.
4. Prefer table-backed predicates when the distinction exists in generated data.
5. If source data exists but is not exposed, promote extraction/table data rather than adding a new local action-id branch.

## Guardrails
- Do not use character id as a proxy for missing callback or data ownership.
- Do not encode procedural callback behavior as fake table data.
- Do not stop at a downstream row fix when an upstream MotionState owner is locally feasible.
- Add positive and negative locks at the owner boundary, not just the motivating replay row.

## Updating This Skill
Add only durable MotionState reasoning patterns or stable engine facts. During final handoff for MotionState/action-family work, proactively note any newly confirmed stable owner distinction or repeatable analysis pattern that belongs here; if the task already includes skill/doc cleanup, update the skill directly. Keep current simulator implementation details, generated table versions, and validation results in repo docs, tests, source comments, or worklogs instead.
