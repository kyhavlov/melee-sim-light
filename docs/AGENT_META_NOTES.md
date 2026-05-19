# Agent Meta Notes

These notes capture between-cycle process takeaways for long rollout/correctness work. They are
not gameplay specification; use `SPEC.md` for source-backed mechanics.

## Target Selection

- Start from a balanced census, not just the largest raw disruptive cluster:
  - one-step taxonomy for direct owner evidence
  - validation report diffs for suite/replay movement
  - per-replay and per-stage normalized FD-vs-non-FD deltas for platform-stage work
  - rollout/disruptive clusters for stability, cascade impact, and concrete packet autopsies
- For stage-target selection, use `uv run python -m tools.eval.stage_rollout_summary` before
  choosing a replay. It reads existing validation reports and ranks stages by rollout first
  mismatches per 1k records without changing canonical scoring.
- For stage-collision owner autopsies, use
  `uv run python -m tools.eval.locate_rollout_desyncs ... --include-stage-segments` to append
  seed/current/ref `ground_id` metadata joined from `MSLSTG01` line kind, platform flag/transform,
  slope, ledge, and material fields.
- Use disruptive rollout clusters as a target selector when the owner is rollout-visible, but do
  not over-centralize on them while broad one-step/platform systems are still missing.
- Treat repeated, early, coherent, or modelplay-visible float residuals as first-class signals.
- Max float error alone is usually a poor ranker because downstream rollout divergence can create
  huge late-position/velocity errors after an earlier owner break.

## Data-Backed Owner Workflow

- Treat the generated data substrates as part of the first-pass autopsy for every serious
  mismatch:
  - `MSLMSO01`: MotionState Anim/IASA/Phys/Coll/Cam callback owner classes.
  - `MSLSTG01`: stage collision line ids, line kinds, flags, ledge/platform bits,
    fighter-solid/current-domain policy, stage-object support tags, endpoints, raw stage points,
    platform transforms/motions/paths, and spawn/respawn/camera/blast roles.
  - `MSLPART1`: static fighter part order, parent links, JObj flags, and named anchors.
  - `MSLITAR1`: Fox/Falco item/article constants and sim-char to GALE01 FighterKind mapping.
  - `MSLFTSC1`: decoded, stable fighter script events.
- Do not turn procedural behavior into fake data. Ledge eligibility, mpColl branch ordering,
  capture/throw provenance, GuardReflect descriptor state, and SpecialHi pose lifetime still require
  source-owner modeling unless a table actually expresses the distinction.

## Investigation Discipline

- Stay on the selected owner until fixed. A failed experiment should refine the source predicate,
  seed surface, callback order, or missing hidden-state hypothesis.
- Do not mark an owner blocked merely because replay-visible state is insufficient. First attempt
  local paths: decomp/asm audit, extraction, probe, instrumentation, or the smallest explicit
  seed/internal lane.
- Hidden seed/internal lanes must update the full contract surface immediately: C structs, Python
  dtype/schema, `DATA_CONTRACT.md`, schema guards, and preprocess notes.
- For any new action-family predicate, item-kind distinction, part/anchor id, stage segment query,
  or script-frame condition, either use the generated data tables or document why they do not
  express the needed owner.

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
