# Active work

## Sequential PR integration

Order: #30 PPC reset ownership, #26 arithmetic profiles, #27 Roy, #28 Pichu,
#29 Kirby. Review and merge one at a time, then perform the approved replay
admission cleanup locally without committing or pushing that final packet.

Completed: #30 merged as `59251229` after local PPC/native reset checks and CI.

Current owner: replay capture configuration. Explicit `fnmsubs_profile` maps
onto the existing Match capability before initialization; suite loading,
validation and benchmark export consume it. Runtime math/state and comparison
stay unchanged. Remove obsolete fighter-admission skips and temporary arithmetic
reports after preserving evidence in the validation guide/provenance record.
Review also found two UCF shield-drop flags omitted at benchmark export; forward
the existing canonical suite fields and verify the serialized configuration.

Pending replay rules and the decision/experiment log are under ignored
`reports/triage/pr_sequence_20260920/`; consolidate stable validation policy
into `agent_docs/VALIDATION.md` after the five merges.

## Protected workspace work

The unrelated `experiment/million-fps-proof` changes remain in stash
`pre-mewtwo-pr-23-review-2026-09-17` (`10f5e030`). Do not apply or discard them.
