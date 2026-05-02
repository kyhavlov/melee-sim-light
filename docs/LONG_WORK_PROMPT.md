Work on the current branch/worktree. Do not commit. Do not be confused by prior messages: only a stop message given AFTER this one should cause you to stop and wrap up your work for review.

I'm giving you an open-ended long-work prompt so you can work for hours while I'm away. Keep an incremental scratch worklog of improvements as you accumulate them and leave retained changes uncommitted in the tree.

Before choosing the first target, read:
- AGENTS.md
- docs/AGENT_META_NOTES.md
- docs/RL10_COMPLETION_CHECKLIST.md
- docs/DATA_CONTRACT.md
- SPEC.md

Use docs/AGENT_META_NOTES.md as persistent process guidance, not as gameplay authority. Gameplay logic still needs source/decomp/data backing.

Worklog:
- If a prior open worklog exists and the tree is dirty, continue from it.
- If the tree is clean or no current worklog exists, start a new worklog at reports/triage/open_rollout_work_log.md.
- Treat the worklog as a live review manifest, not just a narrative.
- After each completed owner, snapshot progress immediately so later review does not require archaeology.

Repeat this loop indefinitely until I come back and stop you, or until the retained dirty stack becomes extremely large/unwieldy to continue safely:

0. Write starting baselines in the worklog.
1. Pick something to improve simulator correctness. Use your best judgment.
2. Continue investigating and working the selected item until it is done. Do not stop or change topics. If you need supporting functionality to finish or continue the investigation, add it and keep going until the selected item is done.
3. Write the completed item into the worklog with updated baselines and packaging notes.
4. When the item is done to our rules, return to step 1 and pick a new target area.

Done means retained changes have no unexplained regressions on mismatch, float error, or rollout
metrics. If a narrow metric tradeoff remains after investigation, keep it only when the owner fix is
source-backed, broader distributions improve, and the worklog explains why the tradeoff is real
rather than an unresolved local regression.

Process details to keep in mind:

- No hard cap on work duration or number of retained fixes; you should be able to work for a full day if needed.
- The worklog should include, for each selected owner/system:
  - selected owner/system
  - source/decomp/data basis
  - generated data substrates checked, and why they did or did not express the owner
  - changed files and owner-specific hunks in shared files
  - tests/locks added
  - SPEC/docs notes added
  - before/after one-step, rollout, disruptive score, and float metrics
  - rejected experiments and what they proved
  - current unresolved local path, if interrupted
  - packaging notes for shared-file hunks
- After each retained owner, run a validation diff against the cycle baseline, for example
  `uv run python -m tools.eval.validation_report_diff --before <baseline> --after reports/validation`.
  If any one-step, rollout, or float validation metric regresses, fix it or explicitly document the
  source-backed tradeoff before selecting another owner. Disruptive rollout rankings still need the
  existing disruptive rerun/rerank workflow when that report is part of the owner.
- Once the owner is validation-clean, save a binary patch snapshot, for example
  `git diff --binary > reports/triage/itemNN_owner_name.patch`, so later review can recover or
  bisect owner-specific changes. Do not treat snapshots as a substitute for the worklog manifest.
- Validation reports should be generator-produced only, and the worklog should note which retained owner refreshed them.
- When touching shared files, record hunk ownership in the worklog immediately.
- Runtime-required generated data must get a data-contract decision immediately:
  - tracked tiny contract files need .gitignore exceptions and guard tests
  - large/local generated artifacts stay ignored with documented regeneration commands
- After each retained owner, run a review self-audit before selecting another owner:
  - source-port vs local-slot domains are explicit and tested
  - carries/provenance are scoped per source owner, hitbox, victim, or phase as appropriate
  - stale collision/contact IDs are not used as current provenance without a latch
  - gameplay constants are data/source-backed
  - any new action-family, item-kind, part/anchor, stage-segment, or script-frame predicate uses
    generated tables where possible, or documents why the table data is insufficient
  - tests are behavioral locks, not source-text grep checks when runtime coverage is possible

Prioritization:

- Prioritize rollout-visible impact, but do not over-bias toward easy fixability.
- Start from disruptive rollout reports and one-step taxonomy.
- Use disruptive rollout clusters as the primary target selector.
- Before patching a selected disruptive row/cluster, run
  `uv run python -m tools.eval.next_desync_investigation --suite <suite> --datasets-dir datasets`
  and record the packet path in the worklog.
- Treat high repeated float residuals as first-class signals, especially:
  - early float divergence before discrete mismatch
  - repeated same-owner float deltas
  - float divergence tied to a top rollout/disruptive owner
  - modelplay-visible float/position/velocity bugs
- Prefer large blast-radius/high-score clusters.
- Prefer coherent shared owners over isolated row wins.
- Prefer decomp/data-backed mechanics and generated-table owner predicates.
- Stay on a major selected owner until fixed.
- Only take adjacent smaller wins when they naturally belong to the same owner family.

Investigation rules:

- Treat a mismatch as an entry point, not the patch boundary.
- Before patching a row, do a data-backed owner autopsy:
  - `MSLMSO01` MotionState callback/classes for action-family and callback-owner identity
  - `MSLFTSC1` script timeline events for frame/script-owned transitions and pulses
  - `MSLSTG01` stage segment ids, kinds, flags, ledges/platforms, endpoints, and raw stage points
  - `MSLPART1` fighter part order, parent links, JObj flags, and named anchors
  - `MSLITAR1` item/article constants and item-kind ownership
  - explicit seed/provenance lanes where the source owner is hidden at replay surface
- If the owner distinction exists in generated data, use or extend the table-backed helper instead
  of adding a local action-id list, item-kind list, part id, stage id, or magic frame condition.
- If source data exists but the current artifact does not expose it, prefer extraction/table
  promotion plus known-row guards over a hardcoded branch.
- Do not encode procedural behavior as fake table data. Ledge eligibility, mpColl ordering,
  capture/throw provenance, GuardReflect descriptor state, SpecialHi pose lifetime, and similar
  callback-local mechanics still need source-owner modeling unless a table actually expresses the
  distinction.
- Do not hop between unrelated clusters after a failed experiment.
- Use failed experiments to refine the source predicate, seed surface, callback order, probe need, extraction need, or hidden-state need.
- If an experiment fails, do not immediately revert it unless it is truly a dead end. Continue investigating the same owner with deeper evidence: decomp C, raw game asm, Slippi asm/labels, probes, extraction, instrumentation, or minimal seed/internal lanes.
- No replay id, dataset id, row id, character-id proxy, stale-state shortcut, or broad tolerance hack in gameplay code.
- If hidden state is required, add the smallest explicit seed/internal lane or local probe/tooling path that exposes the real owner.
- If a hidden seed/internal lane is added or changed, immediately update C structs, Python dtype/schema, DATA_CONTRACT.md, schema guards, and preprocess notes.
- Add positive and negative replay-real locks for each retained owner boundary.
- For modelplay-visible regressions, tests must use compact fixtures under tests/fixtures/modelplay/, not full reports/modelplay/**/trace.json artifacts.

Important hidden-state rule:

For each selected owner, do not mark it blocked or move on merely because visible replay state is insufficient. Missing hidden state/live pose/callback phase/extracted data means the next step is to expose it locally via probe, extraction, instrumentation, or a minimal seed/internal lane. “Blocked” is NOT ALLOWED. You have EVERYTHING you need to solve ANY ISSUE in building the sim between the reference materials like game decomp, game asm, Slippi asm/labels, local probes, and extraction. If you write “needs probe/extraction/seed lane,” immediately attempt that lane before considering any other owner. If a path appears impossible, treat that as evidence that the current representation is wrong: switch from gameplay patching to extraction/probe/instrumentation/decomp work for the same owner, and keep the worklog centered on that owner. Failed experiments refine the same owner hypothesis; they do not permit target switching.

Validation cadence:

- After each retained owner, run focused tests and cheap checks.
- Run full validation before handoff, before packaging, and after schema/data-contract changes.
- Suite validation builds seed rows directly from `.slp` files by default; force preprocess primary
  + aggregate only when persistent `.msl` cache encoding/metadata or cache regeneration is part of
  the change being validated.
- Before final handoff, run the appropriate full validation set for the retained dirty stack.

Hard rule:

Do not commit. Leave retained work dirty for review unless explicitly told to package/commit.
