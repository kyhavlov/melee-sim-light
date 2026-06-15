# Sheik New-Character Stage Plan

Draft owner: Codex worker.
Branch target: create `newchar-sheik` from the current `newchar` branch tip in `/mnt/nvme0/projects/melee-sim-light-newchar`.
Primary objective: implement Sheik as a high-fidelity supported character, using the Marth port guide as the operating checklist and source/data/probe evidence as the acceptance bar.

This is a planning contract. Do not start implementation until this plan is reviewed and the Sheik/Zelda policy below is resolved by the user/reviewer.

Worktree convention:

- Canonical planning contract: `agent_docs/SHEIK_STAGE_PLAN.md` in the implementation worktree.
- Stage 0 may create `reports/triage/newchar_sheik/STAGE_PLAN.md` as an execution snapshot with run-specific source commit, branch path, commit policy, and reviewer command details, but the tracked `agent_docs/SHEIK_STAGE_PLAN.md` remains the durable contract.
- Implementation branch source: the current `newchar` branch HEAD, not `dev` and not `/mnt/nvme0/projects/melee-sim-light` unless the user explicitly redirects.
- Implementation location after Stage 0: either switch this worktree to `newchar-sheik` or create `/mnt/nvme0/projects/melee-sim-light-newchar-sheik`, but record the chosen path before code changes and use only that path in reviewer packets.
- Do not mirror planning or reviewer artifacts into `/mnt/nvme0/projects/melee-sim-light`; that main/dev worktree is not part of this Sheik port unless explicitly requested.
- Keep a single canonical `reports/triage/newchar_sheik/worklog.md` in the implementation worktree after Stage 0; if a sibling worktree is created, copy this tracked plan there and treat the sibling copy as canonical from that point forward.
- If this tracked plan is still uncommitted when Stage 0 starts, carry it into `newchar-sheik` deliberately and make it part of the Stage 0 review packet. Do not let branch creation or worktree switching orphan the plan as an ignored/local-only artifact.

Autonomous goal interpretation:

- A `/goal` to complete this plan means "advance through the stages under this contract," not "skip review, commit, or validation gates to finish faster."
- The worker should keep going without asking the user when the next step is local, source-bounded, and covered by the current stage contract.
- The worker must pause and ask the user only for product-scope decisions, unavailable external credentials/data, persistent reviewer-chat failure, commit authorization when Stage 0 did not grant it, or a blocker that cannot be resolved with local code/probes/tests.
- Persistent reviewer approval is required before each stage is considered complete. If the reviewer requests changes, fix them and re-request review before advancing.
- Stage commits are allowed only if Stage 0 records automatic stage-commit authorization. Otherwise, approval means "ready for user commit decision," not "commit now."
- If an implementation stage grows into unrelated owner families, split the work into coherent reviewed slices rather than returning one giant undifferentiated patch.

## Operating Rules

- Work stage-by-stage. Do not start the next stage until the current stage is reviewed and either committed or explicitly approved to continue uncommitted.
- Use the persistent reviewer Codex chat after every stage packet. Reviewer approval is intermediate only; final approval still comes from the user/main reviewer.
- Every reviewer request must include a complete packet; do not rely on reviewer-chat memory.
- Follow the Stage 0 commit policy. Until Stage 0 records automatic stage-commit authorization, leave work uncommitted unless explicitly told to commit that stage.
- Do not stage changes for review packets. Stage only immediately before an authorized commit, after reviewer approval and passing gates.
- No replay row hacks, dataset-name logic, record-id logic, or validation-fit runtime branches.
- No character-id proxy in shared systems if decomp/source data or generated tables can express the owner.
- New runtime-required data fields must include extractor/export, stable binary/JSON contract, runtime loader, packaged source-artifact/no-decomp fallback if applicable, docs, and tests in the same stage.
- Runtime gameplay changes need nearby source/data pointers.
- Rejected experiments go only in `reports/triage/newchar_sheik/worklog.md`.
- Held-out replay telemetry is measurement only. Do not use held-out rows as locks or tune directly to them.
- If a source-complete owner is bounded by decomp/data/probe evidence, implement the owner generally even if only one current Sheik row exposes it. This is source completion, not replay fitting.
- If a proposed owner regresses existing supported characters, narrow by source ownership or document the blocker; do not paper over it with replay exceptions.

## Stage Preflight Hard Stops

Before starting each stage, write the preflight result into `reports/triage/newchar_sheik/worklog.md`. Stop and ask for direction instead of proceeding if any hard stop fails.

- Confirm the active implementation path with `pwd`; it must match the Stage 0 recorded implementation path.
- Confirm branch state with `git status --short --branch`; branch must be `newchar-sheik` after Stage 0, and must be based on the recorded `newchar` source commit.
- Confirm `git submodule status refs/Ishiiruka`; if any probe/submodule change is required, it must be committed/pushed/fetchable before the superproject points at it.
- Confirm all required generated/source artifacts are tracked or deliberately ignored. Reviewer packets must include `git status --porcelain=v1 -uall`, not only tracked modifications.
- Confirm there is no dependency on ignored local data, datasets, Dolphin binaries, or generated reports unless the stage packet explains how a fresh checkout recreates them.
- Confirm the Sheik/Zelda policy and commit policy are resolved before Stage 1. Do not infer them from replay convenience.
- Confirm no held-out replay filename appears in the official training/eval suite manifest.
- If validation reports are refreshed, record the exact baseline path used for `validation_report_diff`.

Stage commit policy:

- Stage 0 decision: automatic stage commits are authorized after persistent-reviewer approval and all stage gates pass.
- The worker may commit a stage after the persistent reviewer approves and all stage gates pass.
- If automatic stage commits are not authorized, leave the stage uncommitted after reviewer approval and ask the user before committing.
- Never commit with unresolved reviewer findings, missing untracked package files, dirty submodule state, or failing gates.

## Reviewer Chat Template

Use this template for each persistent reviewer request. Fill it completely; do not rely on reviewer-chat memory.

```text
Review the current uncommitted stage packet in <Stage 0 recorded implementation path>.

Stage:
<stage name>

Implementation path and branch:
<pwd, branch, source commit, submodule status>

Objective:
<objective>

Non-goals:
<non-goals>

Changed files:
<tracked and untracked files from git status --porcelain=v1 -uall>

Retained changes:
<summary>

Rejected experiments:
<summary or none>

Validation:
<commands and results>

Known caveats:
<caveats>

Commit policy:
<automatic stage commit authorized after reviewer approval: yes/no>

Please review as a strict code reviewer. Focus on bugs, source-shape violations, data-contract/package holes, missing tests, validation gaps, and scope creep.
Findings first with file:line refs. Do not approve if any gate is missing.
```

Reviewer CLI handoff:

- Persistent reviewer conversation id: `019d8d43-e2eb-7163-90b8-23f2d600d9cd`.
- Preferred non-interactive resume command:
  `codex exec resume -o reports/triage/newchar_sheik/reviewer_responses/<stage>.md 019d8d43-e2eb-7163-90b8-23f2d600d9cd - < reports/triage/newchar_sheik/reviewer_packets/<stage>.md`
- Smoke proof: `codex exec resume` returned `REVIEWER_EXEC_THREAD_OK` and wrote `reports/triage/newchar_sheik/reviewer_smoke_exec.txt`.
- Avoid the interactive TUI form (`codex resume ...`) for automation; it requires a PTY and stays open after the response.
- Run the command from the active implementation worktree so relative paths resolve correctly.
- Reviewer prompt file convention: `reports/triage/newchar_sheik/reviewer_packets/<stage>.md`.
- Reviewer response file convention: `reports/triage/newchar_sheik/reviewer_responses/<stage>.md`.

## Sheik/Zelda Transform Policy

Resolved Stage 0 policy for this pass:

- Sheik is the in-scope supported character and must be implemented as close to vanilla as practical from decomp/source data.
- Zelda is not a full supported character in this pass.
- Zelda stays a private transform-support subset by default. Do not add Zelda as a public registry/default character unless runtime or preprocessing proves that a public character id/data path is required.
- Minimal Zelda support is in scope only where needed for vanilla Sheik behavior:
  - extract/load enough Zelda registry/data to avoid pipeline holes if Sheik's Down-B, Zelda Down-B, or match-start transform references Zelda state;
  - implement enough Down-B transform plumbing to swap Sheik <-> Zelda in source-shaped state;
  - support Zelda Down-B back to Sheik if needed for replay/runtime continuity;
  - do not implement Zelda non-transform specials, full combat polish, or Zelda validation burndown as part of Sheik completion.
- Any unsupported Zelda action outside transform/down-B should be explicit and source-safe: either blocked from validation selection, treated as unsupported terminal/error according to existing project policy, or given minimal inert behavior only if the user approves it.
- If Zelda is added to any registry/default build path, the same packaging rules apply as for Sheik: generated data, no-decomp artifacts, loader coverage, and tests. Partial support is not allowed to create a registry character that silently fails `build_data`, preprocessing, or native init.

Open questions to resolve:

- Does match-start Sheik selection in the target replay/data path require modeling Zelda -> Sheik transform, or does Slippi/engine seed directly as Sheik for selected Sheik?
- If a later stage proves a public Zelda registry/default path is required, what exact additional public loadability/unsupported-action behavior should be exposed?
- If Zelda enters a non-transform action during a Sheik replay, should preprocessing reject that replay, runtime enter unsupported terminal behavior, or continue with minimal common-action data?
- What exact behavior is acceptable for Zelda's Down-B to Sheik during this pass?

Non-negotiable boundary:

- Do not let "minimal Zelda" become a second full character port during this plan.

## Stage 0 - Contract, Branch, And Baseline Setup

Objectives:

- Create `newchar-sheik` from the current `newchar` branch tip in `/mnt/nvme0/projects/melee-sim-light-newchar`.
- Record the exact source commit with `git rev-parse HEAD` before branching.
- Record the chosen implementation path: this worktree switched to `newchar-sheik`, or sibling worktree `/mnt/nvme0/projects/melee-sim-light-newchar-sheik`.
- Create and maintain:
  - `agent_docs/SHEIK_STAGE_PLAN.md`
  - `reports/triage/newchar_sheik/STAGE_PLAN.md` if a run-specific execution snapshot is useful
  - `reports/triage/newchar_sheik/worklog.md`
  - `reports/triage/newchar_sheik/reviewer_packets/`
  - `reports/triage/newchar_sheik/reviewer_responses/`
- Record branch tip, submodule pin, data manifest state, and current validation baseline.
- Resolve and write final Sheik/Zelda policy before Stage 1 code changes.
- Resolve and write commit policy before Stage 1 code changes: automatic commits after persistent-reviewer approval, or explicit user approval before each commit.
- Record the persistent reviewer conversation id and exact Codex CLI resume command in this file.

Expected changed surfaces:

- Triage docs and `agent_docs/SHEIK_STAGE_PLAN.md` only unless the user asks to create the branch immediately.

Non-goals:

- No gameplay code.
- No data extraction changes.
- No replay intake.
- No Sheik implementation.

Validation gates:

- `git status --short --branch`
- `git rev-parse HEAD`
- `git merge-base --is-ancestor <recorded-newchar-source-commit> HEAD` after branch/worktree creation
- `git submodule status refs/Ishiiruka`
- `git -C refs/Ishiiruka ls-remote origin engine-dump-v12-probes` if the Sheik work will rely on Dolphin probe changes
- `make fmt-check`
- `git diff --check && git diff --cached --check`

Reviewer checkpoint:

- Reviewer confirms the plan, stage gates, and Sheik/Zelda policy are explicit enough to constrain implementation.
- Reviewer confirms the persistent reviewer-chat invocation works by reviewing this plan packet before code work starts.

Commit rule:

- Commit after persistent-reviewer approval and passing Stage 0 gates, because automatic stage commits are authorized for this branch.

## Stage 1 - Registry, Data Pipeline, And No-Decomp Packaging

Objectives:

- Add Sheik to every registry surface:
  - extraction registry (`tools/extraction/char_registry.py`);
  - C registry and ids (`src/char_registry.h`, `src/ids.h`);
  - any Slippi/external id mapping needed for preprocessing and suites.
- Add Zelda registry/data only to the extent required by the Stage 0 policy.
- Extract/load Sheik's base data from `PlSk.dat` and associated archives.
- Add Sheik-specific character attribute layout from decomp, not guessed JSON keys.
- Ensure default `build_data` includes Sheik and preserves Fox/Falco/Marth artifacts.
- Ensure no-decomp/source-artifact fallback covers all source-derived required data for Sheik.
- Add tests that fail on missing packaged source artifacts for Sheik, and for Zelda if any public/default Zelda data path is added.
- The no-decomp test must prove the fallback path without reading local `refs/melee` or untracked generated artifacts; use the repo's existing no-decomp isolation pattern or add one in the same stage.
- Add a minimal native init/loadability smoke for Sheik data. If Zelda is public, add the same for Zelda; if private, add a transform-support artifact smoke instead.

Expected changed surfaces:

- `tools/extraction/char_registry.py`
- `src/char_registry.h`
- `src/ids.h`
- `tools/extraction/extract_character_attrs.py`
- generated `data/characters/sheik.json`
- generated per-character data under `data/`
- source artifacts under `tools/extraction/source_artifacts/` for any decomp-derived fallback bins
- data/package tests
- docs if new data keys are introduced

Non-goals:

- No Sheik special behavior implementation.
- No replay-row validation fixes.
- No Zelda full support beyond the approved transform policy.
- No broad shared runtime changes except loader/registry fixes required to make data load loudly and correctly.

Source/data evidence required:

- Melee internal character ids and Slippi external ids from existing mapping/source.
- Decomp anchors for Sheik data and attributes, expected under `refs/melee/src/melee/ft/chara/ftSeak/` or the actual decomp path.
- `PlSk.dat`/animation archive names from ISO/decomp.

Validation gates:

- `uv run python -m tools.extraction.build_data --chars fox,falco,marth,sheik` and the default-registry build path if this branch changes defaults.
- Fresh no-decomp packaged artifact test covering `--chars fox,falco,marth,sheik` and Zelda if policy requires it.
- `uv run pytest -q tests/test_extract_data_public_cli.py tests/test_known_data_artifacts.py tests/test_motion_state_owners_table.py`
- `make build BUILD_FORCE=1`
- `make test`
- `make fmt-check`
- `git diff --check && git diff --cached --check`

Reviewer checkpoint:

- Reviewer checks registry completeness, no-decomp packaging, runtime-required data docs, and absence of shared char-id proxies.

Commit rule:

- Follow the Stage 0 commit policy. If automatic stage commits are authorized, commit after persistent-reviewer approval and passing gates; otherwise ask the user before committing.

## Stage 2 - Engine Boot, Common Actions, And Coverage Suite

Objectives:

- Make Sheik initialize, reseed, step, pose, and compare without unsupported data holes.
- Add Sheik to common-action coverage tests.
- Fix common/shared systems only when decomp/data prove the owner and the fix is generally correct.
- Verify common states before chasing specials:
  - Wait, Walk, Dash, Run, Turn, TurnRun, RunBrake;
  - Jump, JumpAerial, Fall, FallSpecial, Landing, LandingAir;
  - Guard, GuardOn, GuardSetOff, GuardReflect;
  - Damage, DownBound, tech/passive states;
  - Catch/Throw common states where data is already available.

Expected changed surfaces:

- common-action tests
- runtime loaders or shared helpers if Sheik exposes a real data/owner gap
- generated motion-state/script/animation data
- `agent_docs/ADDING_A_CHARACTER.md` only for durable new-character lessons

Non-goals:

- No Sheik specials except transform policy smoke if match-start requires it.
- No replay-specific burn-down.
- No held-out tuning.
- No official Sheik suite inclusion until boot/common-action behavior can run without loader or common-state holes.

Source/data evidence required:

- MotionState owner tables (`MSLMSO01`) for each common action owner.
- Script timeline events (`MSLFTSC1`) for IASA/cmd_var/allow_interrupt ownership.
- Decomp callback anchors for any common runtime fixes.

Validation gates:

- `uv run pytest -q tests/test_char_common_action_coverage.py -k sheik`
- synthetic or fixture smoke for `init -> reseed -> step_input -> write_compare` on Sheik
- `make build BUILD_FORCE=1`
- focused tests for any retained shared fix
- `make test`
- `make validate-all`
- `uv run python -m tools.eval.validation_report_diff --before HEAD --after reports/validation --top 220`
- `make fmt-check`
- `git diff --check && git diff --cached --check`

Reviewer checkpoint:

- Reviewer checks common-action parity, whether fixes are shared/data-backed, and whether any Sheik-specific branches should be generated-data predicates.

Commit rule:

- Follow the Stage 0 commit policy. If automatic stage commits are authorized, commit after persistent-reviewer approval and passing gates; otherwise ask the user before committing.

## Stage 3 - Sheik Replay Intake And Baseline Metrics

Objectives:

- Pull a fresh Sheik replay pool from SlippiLab for this port, then split it into:
  - an official Sheik training/eval suite that can be used for locators, locks, and burn-down;
  - a held-out Sheik telemetry set used only for before/after measurement.
- Build the official Sheik training/eval suite from supported legal-stage replays.
- Exclude or flag replays that enter unsupported Zelda behavior outside the Stage 0 transform policy.
- Preprocess Sheik replays and record baseline one-step/rollout metrics before Sheik-specific gameplay fixes.
- Define the numeric parity target before Stage 4:
  - official-suite target based on current Fox/Falco/Marth supported-character rates;
  - held-out telemetry target used only for reporting;
  - exact metric formula, denominator, and baseline path.
- Build action inventory from the suite:
  - actions seen/missing;
  - specials seen/missing;
  - item/article interactions;
  - stages and platform/ledge exposure.
- Include the local targeted `sheik_demo_game` webplay fixture in the official
  Sheik training/eval suite. It is a specials diagnostic replay for Needles,
  Chain, Vanish, and common movement/combat interactions during those specials;
  it is not held-out telemetry and it does not cover Down-B/transform.
- Build held-out Sheik telemetry set as read-only measurement.
- Define suite admission rules before downloading/adding replays: human-vs-human, singles unless explicitly testing doubles viability, supported legal stages, UCF settings, frozen Stadium, no CPU/controller contamination, no unsupported characters, and no unsupported Zelda episodes beyond the Stage 0 policy.
- Keep training/eval and held-out replay filenames disjoint. Held-out rows must never become direct lock targets or implementation selectors; they are only aggregate telemetry after a clean retained stage.
- Write a replay-selection manifest before preprocessing, with one row per candidate replay:
  - SlippiLab/source reference if available;
  - local filename and checksum;
  - players/characters/stage/rules/settings/UCF/frozen-Stadium evidence;
  - accepted split (`official_train_eval`, `held_out`, or `rejected`);
  - rejection reason for every rejected replay.
- Freeze the held-out set after baseline metrics. Replace a held-out replay only if the manifest proves it violates admission rules; record the replacement and reason before any burn-down continues.

Expected changed surfaces:

- `replays/validation/...`
- `replays/suites/...`
- generated `datasets/...` if committed policy allows validation replay addition
- reports under `reports/triage/newchar_sheik/`
- possibly validation report files if the official suite becomes part of committed validation

Non-goals:

- No gameplay fixes selected from held-out rows.
- No burndown against held-out records.
- No source-free replay fitting.
- No CPU/controller-type contaminated rows.
- No overlap between official training/eval and held-out replay sets.

Validation gates:

- `uv run python -m tools.slippi.preprocess_suite --suite <sheik_suite> --datasets-dir datasets --workers <n>`
- `make validate-marth` or equivalent existing controls remain clean if adding a new suite target is not ready.
- Sheik one-step and rollout locator generation commands documented in worklog.
- `sheik_demo_game` preprocessing/eval/locator output and action inventory
  documented in the worklog, including any live-path-only webplay failures that
  replay reseed eval does not expose.
- Held-out metric command documented, with output written under `reports/triage/newchar_sheik/heldout_*`.
- Parity target recorded in `reports/triage/newchar_sheik/worklog.md` before gameplay burn-down begins.
- Replay-selection audit output saved under `reports/triage/newchar_sheik/replay_selection_audit.*`.
- Replay manifest saved under `reports/triage/newchar_sheik/replay_manifest.*` and checked for no filename/checksum overlap between official and held-out splits.

Reviewer checkpoint:

- Reviewer checks replay selection supportability, Stadium frozen filtering, Zelda-policy compliance, baseline reporting, and held-out/training separation.

Commit rule:

- Follow the Stage 0 commit policy, but do not commit suite inclusion until the persistent reviewer approves replay admission, baseline reports, and held-out/training separation.

## Stage 4 - Sheik Specials And Transform Core

Objectives:

- Implement Sheik specials from first principles using decomp/source and extracted data:
  - Neutral-B Needles: charge, storage, release, projectile/article ownership;
  - Side-B Chain: article/extension/hitbox/callback ownership;
  - Up-B Vanish: startup, intangible/visibility/hitbox, movement, landing/fall/death transitions;
  - Down-B Transform: Sheik <-> Zelda behavior according to Stage 0 policy.
- Use `sheik_demo_game` as an official diagnostic entry point for specials
  exposed in real play. Retained fixes still need source/data/probe ownership
  and adjacent controls; do not key behavior on the demo replay or its rows.
- Treat demo Needle source-clock and thrown-Needle bounce/contact mismatches as
  item/accessory callback probe tasks before retaining runtime behavior. The
  Needle End `shootNeedles` RNG owner is now a probe-backed replay seed
  contract; future Needle contact work still needs the same source/probe owner
  proof instead of row selection.
- Implement minimal Zelda transform support only within approved policy.
- Add focused source-shaped positives/negatives for each special family, including adjacent cancel/landing/death cases.

Expected changed surfaces:

- `src/sheik_specials.c` or existing specials dispatch architecture
- special dispatch/resolver tables
- articles/items data if Needles or Chain use article systems
- generated data extractors for article constants or special attrs
- C API/state fields if real hidden source state is needed
- tests for specials, articles, transform
- docs for new data/runtime fields

Non-goals:

- No full Zelda non-transform special implementation.
- No broad item/article refactor beyond Sheik-owned needs.
- No replay-local special branches.
- No "stub by doing nothing" transform behavior unless the Stage 0 policy explicitly allows that exact unsupported boundary.

Source/data evidence required:

- Sheik decomp files for each special callback.
- Article/item decomp or extracted article constants for needles/chain.
- MotionState callback and script timeline evidence for IASA/cancel/landing paths.
- Dolphin/scenario probes only when hidden runtime state is ambiguous and cannot be inferred from decomp/data.

Validation gates:

- focused special tests for every retained special owner
- transform policy tests, including unsupported Zelda boundary behavior
- no-decomp/package tests for any new article, special-attr, or transform-support data
- `make build BUILD_FORCE=1`
- `make test`
- Sheik official one-step/rollout validation
- `make validate-all`
- `uv run python -m tools.eval.validation_report_diff --before HEAD --after reports/validation --top 220`
- `make fmt-check`
- `git diff --check && git diff --cached --check`

Reviewer checkpoint:

- Reviewer checks source ownership of specials, transform policy containment, article/no-decomp packaging, and whether Zelda scope leaked.

Commit rule:

- Follow the Stage 0 commit policy. If automatic stage commits are authorized, commit after persistent-reviewer approval and passing gates; otherwise ask the user before committing.

## Stage 5 - Combat, Hitbox, Hurtbox, BODY, And Pose Audit

Objectives:

- Audit Sheik hitbox and hurtbox extraction against decomp/data and live Dolphin probes where needed.
- Verify BODY/hurtbox pose timing for common and special moves.
- Validate Needle/Chain hitbox ownership if they are article or extended-collision systems.
- Add positive/negative locks around hit/no-hit, shield/contact, hitlag, stale hitlist, and projectile/article contact boundaries.
- Treat Sheik limb/chain/needle geometry as its own data-contract audit, not as "Marth sword again" by analogy.

Expected changed surfaces:

- hitbox/hurtbox extraction data if Sheik exposes missing contracts
- `src/combat.c`, `src/hitboxes.c`, `src/hurtboxes.c`, `src/items.c` only for source-backed shared fixes
- probe scripts/configs if live pose evidence is needed
- focused combat tests

Non-goals:

- No manual hitbox endpoint overrides unless decomp/data prove an extractor contract gap and the overlay is audited.
- No replay-fit hitlist or BODY branches.

Source/data evidence required:

- `MSLFTSC1` script event windows.
- extracted hitbox endpoint/part ids.
- Dolphin live hitbox/BODY samples for ambiguous pose timing.
- decomp callback order for runtime pose sampling.
- If extracted pose/hitbox data is wrong, fix the extractor/data contract before adding runtime compensation.

Validation gates:

- focused combat/hitbox/hurtbox locks
- Sheik one-step/rollout validation
- spacie/Marth aggregate controls
- `make test`
- `make validate-all`
- report diff clean versus stage baseline
- formatting and diff checks

Reviewer checkpoint:

- Reviewer checks extraction-vs-runtime owner, no stale seed provenance materialized into free-running hitlist state, and adjacent controls.

Commit rule:

- Follow the Stage 0 commit policy. If automatic stage commits are authorized, commit after persistent-reviewer approval and passing gates; otherwise ask the user before committing.

## Stage 6 - Throw, Capture, Ledge, Landing, Damage, And Stage-Collision Burn-Down

Objectives:

- Use official Sheik rollout/one-step locators to choose high-impact owner families.
- Treat mismatch clusters as source-owner audits:
  - throw/capture victim handoff;
  - mpColl floor/ledge publication;
  - landing/action-entry callback ordering;
  - ECB/pose sampling;
  - damage/tech/downbound transitions;
  - guard/shield contact-state publication.
- Implement source-complete shared fixes when bounded, with positives and adjacent negatives.
- Keep each retained slice small enough that a reviewer can isolate the source owner and validation movement. Do not bundle unrelated owner families just because they are all Sheik-exposed.

Expected changed surfaces:

- shared runtime systems only when source/data prove owner.
- focused locks from official Sheik suite or synthetic source-shaped controls.
- `agent_docs/ADDING_A_CHARACTER.md` for durable lessons only.
- worklog updates for every retained/rejected owner.

Non-goals:

- No held-out row locks.
- No metric-driven broad gates.
- No one-row exception campaign.

Source/data evidence required:

- decomp callback order, `MSLMSO01`, `MSLFTSC1`, stage data (`MSLSTG01`), part/anchor data (`MSLPART1`), and Dolphin probes where hidden runtime publication is ambiguous.

Validation gates after each retained slice:

- focused tests for positives/negatives
- Sheik official one-step validation
- Sheik official rollout validation
- `make validate-all`
- report diff clean versus slice baseline
- held-out measurement logged after clean retained slice
- exact replay-level regression scan from `validation_report_diff`; no hard/distribution/unclassified reds
- `make fmt-check`
- `git diff --check && git diff --cached --check`

Reviewer checkpoint:

- Reviewer checks source-owner classification, controls, no regressions, no hidden held-out tuning.

Commit rule:

- Follow the Stage 0 commit policy. Commit coherent reviewed slices, not every experiment.

## Stage 7 - Metrics Parity, Documentation, And Final Packaging

Objectives:

- Bring Sheik official suite into the Stage 3 recorded parity target against current supported characters.
- Report held-out Sheik metrics without tuning to held-out rows.
- Ensure data package and no-decomp source artifacts are complete for Sheik and any approved minimal Zelda support.
- Ensure webplay packaging includes every public Sheik viewer surface:
  `tools/viewer/live/schema.js` dropdown entry, any main.js defaults/wiring, and
  `tools/viewer/assets/character_zips.tsv` with `sheik.zip` checksum/URL, plus
  `tools/viewer/live/build_wasm.sh` `viewer_chars` so the WASM build preflights
  Sheik data. The guards are
  `tests/test_live_viewer_schema.py::test_live_viewer_supported_characters_have_packaged_animation_zips`
  and
  `tests/test_live_viewer_schema.py::test_live_viewer_supported_characters_are_required_by_wasm_build`;
  Sheik previously appeared in the dropdown while the local build omitted the
  renderer zip, leaving the page stuck loading.
- Update docs with durable process lessons:
  - `agent_docs/ADDING_A_CHARACTER.md`
  - `agent_docs/DATA_CONTRACT.md`
  - `agent_docs/SPEC.md` if source/system behavior belongs there
- Prepare final commit-by-commit review packet for the original reviewer.

Expected changed surfaces:

- docs
- validation reports
- final worklog and reviewer packet summaries
- no-decomp/package tests if artifacts changed

Non-goals:

- No final metric polishing by replay exception.
- No expansion from Sheik into full Zelda unless explicitly approved as a new project.

Final gates:

- `make build BUILD_FORCE=1`
- `make test`
- Sheik official one-step/rollout validation
- `make validate-all`
- `make viewer-build` or an equivalent focused viewer asset/schema check after
  Sheik is exposed in the live-viewer dropdown.
  `tools/viewer/live/schema.js::SUPPORTED_CHARACTERS`,
  `tools/viewer/assets/character_zips.tsv`, and
  `tools/viewer/live/build_wasm.sh` `viewer_chars` must stay in lockstep so
  selecting Sheik cannot hang on a missing animation zip or missing WASM data
  preflight.
- `uv run python -m tools.eval.validation_report_diff --before <pre-sheik-baseline> --after reports/validation --top 220`
- held-out measurement summary under `reports/triage/newchar_sheik/`
- no-decomp packaged build test for all registry chars
- fresh clone/package thought check: every tracked data/source artifact required by no-decomp build is included; no ignored local artifact is required for tests to pass
- `make fmt-check`
- `git diff --check && git diff --cached --check`
- submodule status clean/fetchable if probes changed

Reviewer checkpoint:

- Persistent reviewer approves final packet.
- User/main reviewer performs commit-by-commit final review.

Commit rule:

- Final cleanup/docs commit follows the Stage 0 commit policy, with persistent-reviewer approval required before any commit.

## Initial Risk Register

- Sheik/Zelda transform scope can explode if not decided before data model work.
- Needles and Chain may require article/item substrate work; do not fake them as ordinary hitboxes if source uses articles.
- Vanish likely touches visibility/intangibility/death/landing callback timing; probe if decomp does not reveal publication phase.
- Dynamic bones/hurtboxes may differ from Marth's "cosmetic enough" conclusion; audit Sheik's actual hurtcaps and articles.
- Mixed Sheik/Zelda validation replays may enter unsupported Zelda states; suite curation must honor the transform policy.
- Adding Zelda partial data may affect registry-default build_data and no-decomp artifacts; tests must cover this explicitly.
- MotionState callback ids may renumber when Sheik/Zelda are added; tests should validate symbols/versions rather than stale numeric assumptions where possible.

## Stage Packet Checklist

Every stage packet must include:

- Stage objective and non-goals.
- Exact changed files.
- Source/data/probe evidence summary.
- Retained changes.
- Rejected experiments.
- Focused positive/negative locks.
- Validation commands and results.
- Report diff result and baseline path.
- Held-out metric delta if applicable.
- Docs updated or explicit reason no docs changed.
- Reviewer response and resolution.

Reviewer finding loop:

- If the persistent reviewer returns any finding, do not advance stages and do not commit.
- Record each finding in the worklog with the resolution: fixed, rejected with source-backed reason, or escalated to user.
- Send a follow-up reviewer packet after fixes. The follow-up packet must include the original finding text, changed files, and the validation rerun.

Stage completion standard:

- A stage is complete only when its objectives, non-goals, evidence requirements, validation gates, and reviewer finding loop are all satisfied.
- A stage is not complete merely because one retained fix passed validation.
- Stage 6 is complete only when the official Sheik locator queue reaches the Stage 7 parity target established from Stage 3 metrics, or when the highest-impact remaining owner families are explicitly blocked with exact missing source/probe evidence and reviewer approval.
- Held-out improvements are never a completion substitute; they are telemetry after official-suite/source-owned work.
