---
name: melee-skill-maintenance
description: Use when adding or updating repo-local Codex skills for Melee engine knowledge, especially to keep skills stable, source-backed, and resistant to becoming stale.
---

# Melee Skill Maintenance

## Purpose
Use this skill when creating or editing Melee-domain skills. Skills should capture stable engine reasoning and durable workflows, not the simulator’s current implementation state.

Other Melee-domain skills should trigger lightweight maintenance automatically: during final handoff for relevant work, note stable facts or repeatable investigation patterns that should be promoted. If the current task already includes skill/doc cleanup, update the relevant skill directly; otherwise surface a short candidate list without turning every prompt into a process checklist.

## What Belongs In Skills
- Stable Melee engine concepts confirmed by decomp, asm, extracted data, or reusable probes.
- Investigation procedures that apply across multiple owners.
- Evidence standards for source-backed changes.
- Guardrails that prevent row-fitting, stale seed lanes, broad tolerances, or validation hiding.

## What Does Not Belong In Skills
- Current validation counts or replay-specific status.
- Current schema sizes, cache versions, generated table versions, or report field churn.
- Current simulator helper/function names unless they are stable public commands central to the workflow.
- Temporary bridges, rejected experiments, or one-replay workarounds.
- Claims that should live in source comments, tests, active architecture docs,
  or the active worklog.

## Update Procedure
1. Decide whether the new knowledge is stable Melee-domain knowledge or current simulator state.
2. If it is stable, cite the evidence type:
   - `refs/melee/src/...`,
   - `refs/melee/build/GALE01/asm/...`,
   - `refs/slippi-ssbm-asm/...`,
   - extracted game data family,
   - committed probe/test artifact.
3. Add the smallest durable rule or checklist that would change future agent behavior.
4. Keep `SKILL.md` concise. Prefer a short rule over long explanation.
5. If a skill starts accumulating implementation details, move those details to `agent_docs/`, tests, or source comments and leave only navigation guidance in the skill.

## Automatic Candidate Habit
- At the end of substantial Melee engine work, ask internally: “Did this prove a stable engine fact or reusable investigation pattern?”
- If yes, include a concise `Skill candidate` note in the handoff, or update the relevant skill directly when skill maintenance is already in scope.
- If the finding is about current simulator behavior, validation shape, a temporary bridge, or a single replay, do not promote it.

## Review Checklist
- Does this still make sense after the simulator is refactored?
- Is the claim backed by Melee source/decomp/data rather than current sim behavior?
- Would this guidance help on a different replay or owner family?
- Is it short enough to load during real work without wasting context?

## Updating This Skill
Update this meta-skill only when the policy for repo-local skills changes. Do not use it as a dumping ground for gameplay facts.
