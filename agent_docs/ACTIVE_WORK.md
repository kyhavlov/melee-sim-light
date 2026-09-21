# Active work

## Sequential PR integration

Order: #30 PPC reset ownership, #26 arithmetic profiles, #27 Roy, #28 Pichu,
#29 Kirby. Review and merge one at a time, then perform the approved replay
admission cleanup locally without committing or pushing that final packet.

Completed: #30 `59251229`, #26 `9ce438d3`, #27 `12f2522f`, each after local
checks and fresh CI.

Current owner: Pichu admission through upstream `ftPc_Init` and shared `ftPk_*`
move and Pikachu item owners. GameData owns Pichu archives, four costumes and
translated Pikachu-shaped attributes; Match owns fighter and article state.
API, scalar, batch, extraction, validation and viewer registries consume the
admission. Replace only the unsupported-kind boundary; retain source-authored
recoil, item kinds and callbacks without copied move logic. Verify four-Pichu
pool bounds and combined Roy/Pichu preload inside the existing shared reserve.

Pending replay rules and the decision/experiment log are under ignored
`reports/triage/pr_sequence_20260920/`; consolidate stable validation policy
into `agent_docs/VALIDATION.md` after the five merges.

## Protected workspace work

The unrelated `experiment/million-fps-proof` changes remain in stash
`pre-mewtwo-pr-23-review-2026-09-17` (`10f5e030`). Do not apply or discard them.
