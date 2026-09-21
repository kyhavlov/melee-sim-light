# Active work

## Sequential PR integration

Order: #30 PPC reset ownership, #26 arithmetic profiles, #27 Roy, #28 Pichu,
#29 Kirby. Review and merge one at a time, then perform the approved replay
admission cleanup locally without committing or pushing that final packet.

Completed: #30 `59251229`, #26 `9ce438d3`, #27 `12f2522f`, #28 `53ebe390`,
each after local checks and fresh CI.

Current owner: upstream Kirby lifecycle, copy moves, capture/thrown callbacks
and Kirby item owners. GameData owns preloaded copy archives, six-costume hat
records and effect banks; Match owns `fp->fv.kb`, captured-victim state and
runtime accessory pools. Extraction, DAT translation, APIs, validation, pose,
item projection and viewer consume these source owners. Delete the displaced
45 Kirby abort stubs, eight common capture abort stubs and four no-op
Kirby hooks; no partial-copy mode or duplicate move
logic. Review all host union/offset adaptations, sealed pools, accessory
lifetime, copy/save/restore and Roy/Pichu copy admission together.

Pending replay rules and the decision/experiment log are under ignored
`reports/triage/pr_sequence_20260920/`; consolidate stable validation policy
into `agent_docs/VALIDATION.md` after the five merges. The submitted temporary
Kirby packet is preserved there; stable facts remain in source, the adaptation
ledger and `agent_docs/validation/kirby_provenance.json`.

## Protected workspace work

The unrelated `experiment/million-fps-proof` changes remain in stash
`pre-mewtwo-pr-23-review-2026-09-17` (`10f5e030`). Do not apply or discard them.
