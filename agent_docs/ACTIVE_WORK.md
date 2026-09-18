# Active work

## PR #24 — publishing approved review

- Local branch: `feat/gamewatch-only`, rebased at `dba08b40` onto `de64f76a`
  (merged Mewtwo PR #23). The user approved committing/pushing the cleanup and
  merging after fresh CI. The reviewed remote parent is `bda971d6`.
- Review is complete. Gates, fixes and remaining evidence
  limits are in [GAMEWATCH_SUPPORT.md](GAMEWATCH_SUPPORT.md#integration-review--2026-09-17).
- Final owners: imported `ftGameWatch`, ten item owners and `itmaterial`.
  Canonical state is `Fighter.fv/mv.gw`, item unions and shared game-data tables.
  Consumers are source callbacks, DAT loading, projection and save/restore.
- Deletion boundary: five former attack-entry abort stubs are removed. Chef's
  shared attribute pointer retains its native width; there is no parallel state
  or fallback. Both fighter admissions and all existing locks are preserved.
- Next: publish with an explicit lease, require fresh CI, and merge #24.
  Then remove obsolete docs on main, commit that cleanup, and prepare CPU replay
  PR #25 for a separate review decision. Do not merge #25 as part of this task.

## Protected workspace work

The unrelated `experiment/million-fps-proof` changes remain in stash
`pre-mewtwo-pr-23-review-2026-09-17` (`10f5e030`). Do not apply or discard them
as part of this review.
