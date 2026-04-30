# Agent Meta Notes

These notes capture between-cycle process takeaways for long rollout/correctness work. They are
not gameplay specification; use `SPEC.md` for source-backed mechanics.

## Long-Work Cycle Hygiene

- Keep a live worklog as a review manifest, not just a narrative. The canonical field list lives in
  `docs/LONG_WORK_PROMPT.md`; do not duplicate or drift it here.
- Snapshot each completed owner immediately. Do not rely on reconstructing intent from a large
  dirty diff later.
- After each retained owner, run a validation diff against the cycle baseline before selecting the
  next owner. Keep a short regression ledger in the worklog; every local one-step, rollout, or
  float regression must be fixed or explicitly justified before moving on.
- Once an owner is validation-clean, save a binary patch snapshot such as
  `git diff --binary > reports/triage/itemNN_owner_name.patch`. Snapshots are for
  recovery/bisection only; the worklog remains the review manifest.
- Packaging is a real risk area. Shared files such as `SPEC.md`, `src/items.c`, `src/combat.c`,
  `src/action.c`, validation reports, and schema/data files accumulate unrelated hunks quickly.
- Runtime-required generated data must get a data-contract decision immediately:
  - tracked tiny contract files need `.gitignore` exceptions and guard tests
  - large/local generated artifacts stay ignored with regeneration commands documented
- Nested Dolphin probe changes are local probe state unless explicitly exported under
  `tools/dolphin/patches/`.
- Before moving to another owner, run a short review self-audit:
  - source-port vs local-slot domains are explicit
  - carries/provenance are scoped per source owner, hitbox, victim, or phase as appropriate
  - stale collision/contact IDs are not used as current provenance without a latch
  - gameplay constants are data/source-backed
  - tests are behavioral locks, not source-text grep checks when runtime coverage is possible

## Target Selection

- Use disruptive rollout clusters as the primary target selector, but treat repeated, early,
  coherent, or modelplay-visible float residuals as first-class signals.
- Max float error alone is usually a poor ranker because downstream rollout divergence can create
  huge late-position/velocity errors after an earlier owner break.

## Data-Backed Owner Workflow

- Treat the generated data substrates as part of the first-pass autopsy for every serious
  mismatch:
  - `MSLMSO01`: MotionState Anim/IASA/Phys/Coll/Cam callback owner classes.
  - `MSLSTG01`: stage collision line ids, line kinds, flags, ledge/platform bits, endpoints, and
    raw stage points. Reserved spawn/respawn/camera/blast fields are not source-backed yet.
  - `MSLPART1`: static fighter part order, parent links, JObj flags, and named anchors.
  - `MSLITAR1`: Fox/Falco item/article constants and sim-char to GALE01 FighterKind mapping.
  - `MSLFTSC1`: decoded, stable fighter script events.
- A replay row is usually only the symptom. Before adding a local branch, ask which table-backed
  owner family the action/item/stage/part/script event belongs to and whether the same owner should
  fix adjacent rows.
- Prefer table promotion over hardcoding when the source data exists. Adding a small extractor,
  class bit, helper, or known-row test is part of the fix, not scope creep.
- Use exact table migrations when equivalence can be proven exhaustively across supported
  Fox/Falco actions or known data rows.
- Do not turn procedural behavior into fake data. Ledge eligibility, mpColl branch ordering,
  capture/throw provenance, GuardReflect descriptor state, and SpecialHi pose lifetime still require
  source-owner modeling unless a table actually expresses the distinction.
- If a table is used only as a guard or diagnostic, say so in the worklog. Do not imply it closes a
  gameplay owner until runtime behavior uses it.
- For rollout-disruptive targets, start with `tools.eval.next_desync_investigation` and keep the
  packet path in the worklog. The packet should frame the first hypothesis and evidence sources,
  but source/decomp/data ownership still decides the fix.

## Investigation Discipline

- Treat each mismatch as an entry point, not the patch boundary.
- Stay on the selected owner until fixed. A failed experiment should refine the source predicate,
  seed surface, callback order, or missing hidden-state hypothesis.
- Do not mark an owner blocked merely because replay-visible state is insufficient. First attempt
  local paths: decomp/asm audit, extraction, probe, instrumentation, or the smallest explicit
  seed/internal lane.
- Hidden seed/internal lanes must update the full contract surface immediately: C structs, Python
  dtype/schema, `DATA_CONTRACT.md`, schema guards, and preprocess notes.
- Avoid row-fit fixes:
  - no replay id, dataset id, row id, character-id proxy, stale-state shortcut, or broad tolerance
    hack in gameplay code
  - broadening is allowed only when source-shaped
  - narrowing must use decomp/data predicates or explicit seed/probe evidence
- For any new action-family predicate, item-kind distinction, part/anchor id, stage segment query,
  or script-frame condition, either use the generated data tables or document why they do not
  express the needed owner.
- Add positive and negative replay-real locks around each retained owner boundary.

## Current Hard Parts

- Remaining bugs are often hidden-state/provenance issues, not missing simple callbacks:
  - live JObj/AObj pose and constrained-joint snapshots
  - HitCapsule `victims_1` / dense-vs-per-hitbox hitlist provenance
  - mpColl candidate lists, floor masks, wall/ceil envelope resolution
  - source-port vs local-slot ownership domains
  - hitlag/IASA/action-entry callback phase ordering
- Reduced geometry proxies are a major source of float residuals:
  - BODY/contact hurtcap pose and HitCapsule centers
  - ShieldDesc placement and shield/body boundary
  - laser/item collision normals and shield-bounce/reflection provenance
- Replay-visible state is often one frame too coarse. The successful fixes usually model or seed a
  narrow hidden owner instead of deriving behavior from visible row shape.

## Tooling Notes

- Faster parallel `make test`, cache-aware preprocess, and parallel validation reduce the general
  feedback cost.
- Fresh disruptive rollout simulation/ranking remains the main slow loop when raw rows must be
  regenerated. Use rerank when existing rows are still valid.
- Validation reports should remain generator-produced only.
- Use `uv run python -m tools.eval.validation_report_diff --before <baseline> --after reports/validation`
  to summarize long validation reports and catch replay-level regressions hidden by suite wins.
- For modelplay-visible regressions, use compact fixtures under `tests/fixtures/modelplay/`, not
  full `reports/modelplay/**/trace.json` artifacts.
