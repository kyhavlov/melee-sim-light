Work on the current branch/worktree. Do not commit. Do not be confused by prior messages: only a stop message given AFTER this one should cause you to stop and wrap up your work for review.

I'm giving you an open-ended long-work prompt so you can work for hours while I'm away. Keep an incremental scratch worklog of improvements as you accumulate them and leave retained changes uncommitted in the tree.

Before choosing the first target, read:
- AGENTS.md
- agent_docs/AGENT_META_NOTES.md
- agent_docs/DATA_CONTRACT.md
- agent_docs/SPEC.md

Use agent_docs/AGENT_META_NOTES.md as persistent process guidance, not as gameplay authority. Gameplay logic still needs source/decomp/data backing.

Worklog:
- If a prior open worklog exists and the tree is dirty, continue from it.
- If the tree is clean or no current worklog exists, start a new worklog at reports/triage/open_rollout_work_log.md.
- Treat the worklog as a live review manifest, not just a narrative.
- After each completed owner, snapshot progress immediately so later review does not require archaeology.

Repeat this loop indefinitely until I come back and stop you. Do not stop after bare behavior-neutral
substrate or a tiny row/window patch. Also do not back out correct source-clear mechanics solely
because current validation metrics are unchanged: if a decomp/data-backed behavior is bounded,
allocation-safe, on the RL 1.0/system path, and covered by focused positive/negative tests, retain
it as source-completion work.

0. Write starting baselines in the worklog.
1. Pick something to improve simulator correctness. Use your best judgment.
2. Continue investigating and working the selected item until it is done. Do not stop or change topics. If you need supporting functionality to finish or continue the investigation, add it and keep going until the selected item is done.
   Before retaining any runtime fix, perform this hard generality gate:
   - Identify the broadest decomp/data-backed source owner that explains the observed bug family.
   - If that general owner is locally feasible and would cover other plausible rollout/replay bugs,
     implement it instead of the narrower downstream fix.
   - This is required even when current validation/replay suites do not contain every variant the
     general owner would cover.
   - A narrow action/row/stage fix may remain only as temporary worklog evidence while building the
     general owner; it is not the retained runtime shape.
3. Write the completed item into the worklog with updated baselines and packaging notes.
4. When the item is done to our rules, return to step 1 and pick a new target area.

Process details to keep in mind:

- No hard cap on work duration or number of retained fixes; you should be able to work for a full day if needed.
- After each retained owner, run a validation diff against the cycle baseline, for example
  `uv run python -m tools.eval.validation_report_diff --before <baseline> --after reports/validation`.
  If any one-step, rollout, or float validation metric regresses, fix it or explicitly document the
  source-backed tradeoff before selecting another owner. Disruptive rollout rankings still need the
  existing disruptive rerun/rerank workflow when that report is part of the owner.
- Once the owner is validation-clean, save a binary patch snapshot, for example
  `git diff --binary > reports/triage/itemNN_owner_name.patch`, so later review can recover or
  bisect owner-specific changes. Do not treat snapshots as a substitute for the worklog manifest.
- Runtime-required generated data must get a data-contract decision immediately:
  - tracked tiny contract files need .gitignore exceptions and guard tests
  - large/local generated artifacts stay ignored with documented regeneration commands

Prioritization:

- Prioritize source-owner closure and simulator correctness; metrics choose between plausible
  owners, but do not define the patch boundary.
- Gameplay-critical rollout owners outrank replay-exact/render-only state; low-priority
  camera/viewer/cosmetic lanes should not drive target selection unless they feed gameplay
  ownership.
- Proactively remove runtime bridges/proxies/fallbacks when they are in or adjacent to the owner
  you are touching. Search the touched runtime/seed surfaces for bridge/proxy/fallback wording,
  classify each hit in the worklog, and replace real runtime bridges with the source/data owner.
  Do not relabel bridge debt as "source" unless the implementation changed to the real owner.
- Start from one-step taxonomy, validation report diffs, per-replay/per-stage normalized deltas,
  rollout/disruptive reports, float outliers, and modelplay-visible bugs.
- For platform-stage work, explicitly compare non-FD replays against FD/cardinal behavior using
  normalized rates. Do not choose solely by raw aggregate totals.
- Use disruptive rollout clusters when a target is rollout-visible, but do not over-bias toward
  disruptive rankings while broad one-step/platform systems are still missing.
- Before patching a selected disruptive row/cluster, run
  `uv run python -m tools.eval.next_desync_investigation --suite <suite> --datasets-dir datasets`
  and record the packet path in the worklog.
- Treat high repeated float residuals as first-class signals when they precede discrete mismatch,
  repeat under the same owner, drive rollout disruption, or show up as modelplay-visible
  position/velocity bugs.
- When a bridge/proxy/fallback is found, continue through the surrounding owner cluster before
  moving on. A single removed bridge is not enough if the same file/family still has adjacent
  runtime bridge debt.

Investigation rules:

- Treat a mismatch as an entry point, not the patch boundary.
- Before retaining a fix, translate the motivating mismatch into gameplay terms in the worklog:
  situation, vanilla-vs-sim behavior, timing, player-visible meaning, likely source owner, and
  confidence level. Row ids, fields, metrics, and packets are evidence, not the patch boundary.
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
- Do not stop at a downstream fix when the source points to an implementable upstream owner. The
  upstream owner is the task, even if the downstream fix is already validation-clean.
- If an experiment fails, do not immediately revert it unless it is truly a dead end. Continue investigating the same owner with deeper evidence: decomp C, raw game asm, Slippi asm/labels, probes, extraction, instrumentation, or minimal seed/internal lanes.
- No replay id, dataset id, row id, character-id proxy, stale-state shortcut, or broad tolerance hack in gameplay code.
- Bridge/proxy/fallback audit terms are evidence, not naming cleanup. If the code is still a
  compensating path, keep the debt wording honest and replace the owner; do not just rename it.
- If hidden state is required, add the smallest explicit seed/internal lane or local probe/tooling path that exposes the real owner.
- If a hidden seed/internal lane is added or changed, immediately update C structs, Python dtype/schema, agent_docs/DATA_CONTRACT.md, schema guards, and preprocess notes.
- Add positive and negative replay-real locks for each retained owner boundary.
- For modelplay-visible regressions, tests must use compact fixtures under tests/fixtures/modelplay/, not full reports/modelplay/**/trace.json artifacts.

Important hidden-state rule:

For each selected owner, insufficient visible replay state is not a stop condition. Expose the
missing hidden state, live pose, callback phase, or extracted data through a local probe,
extraction, instrumentation, or minimal seed/internal lane, then continue the same owner. If a path
appears impossible, the current representation is probably wrong; switch to extraction/probe/
instrumentation/decomp work for that owner. Failed experiments refine the owner hypothesis; they
do not permit target switching.

Validation cadence:

- After each retained owner, run focused tests and cheap checks.
- Run full validation after schema/data-contract changes.
- Suite validation builds seed rows directly from `.slp` files by default; force preprocess primary
  + aggregate only when persistent `.msl` cache encoding/metadata or cache regeneration is part of
  the change being validated.
- Do not refresh validation reports for test-only changes. If gameplay/runtime logic changes,
  reports must be generator-produced and validation deltas must be compared against the cycle
  baseline.

Hard rule:

Do not commit. Leave retained work dirty for review unless explicitly told to package/commit.
