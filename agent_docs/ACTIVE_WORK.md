# Active work

## Sequential PR integration

Order: #30 PPC reset ownership, #26 arithmetic profiles, #27 Roy, #28 Pichu,
#29 Kirby. Review and merge one at a time, then perform the approved replay
admission cleanup locally without committing or pushing that final packet.

Current owner: PPC scalar construction. Cached costume models belong to
GameData, consumed by Fighter_Create and Zelda/Sheik or Nana construction.
Fighter archives and animation banks remain per Match. Displace only lazy
costume allocation from the Match arena; no runtime bridge or native change.
The scalar smoke checks ownership and deterministic reconstruction on reset.

Pending replay rules and the decision/experiment log are under ignored
`reports/triage/pr_sequence_20260920/`; consolidate stable validation policy
into `agent_docs/VALIDATION.md` after the five merges.

## Protected workspace work

The unrelated `experiment/million-fps-proof` changes remain in stash
`pre-mewtwo-pr-23-review-2026-09-17` (`10f5e030`). Do not apply or discard them.
