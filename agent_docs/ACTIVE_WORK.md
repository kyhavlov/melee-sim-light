# Active work

## Sequential PR integration

Order: #30 PPC reset ownership, #26 arithmetic profiles, #27 Roy, #28 Pichu,
#29 Kirby. Review and merge one at a time, then perform the approved replay
admission cleanup locally without committing or pushing that final packet.

Completed: #30 `59251229`, #26 `9ce438d3`, each after local checks and fresh CI.

Current owner: Roy admission through upstream `ftFe_Init` and shared `ftMs_*`
move owners. GameData owns extracted Roy archives/costumes and the shared Marth
attribute layout; Match retains fighter state and source callbacks. API, scalar,
batch, extraction, validation and viewer registries consume the admission.
Replace only the unsupported-kind boundary with these source owners; no copied
move logic or replay-specific behavior. Review the 128 MiB shared data reserve,
four-Roy pool bounds, all five costumes and the network-console capture default.

Pending replay rules and the decision/experiment log are under ignored
`reports/triage/pr_sequence_20260920/`; consolidate stable validation policy
into `agent_docs/VALIDATION.md` after the five merges.

## Protected workspace work

The unrelated `experiment/million-fps-proof` changes remain in stash
`pre-mewtwo-pr-23-review-2026-09-17` (`10f5e030`). Do not apply or discard them.
