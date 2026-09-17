# Recorded CPU input validation — 2026-09-15

The native validator accepts Human and Cpu ports. CPU slot type and level are
retained in the source player owner. AI callbacks still run; recorded processed
sticks, trigger, buttons, and pre-frame RNG are published before input edges and
timers at the Slippi playback boundary (GALE01 0x8006B0DC). Physical controller
bytes remain separate. There is no position, action, damage, or velocity restore.

Source: `Playback/Core/RestoreGameFrame.asm`, `Recording/SendGamePreFrame.asm`,
`ftCo_800A2040`, and `gm_16AE.c::fn_8016D8AC`. ExPhil's
`docs/planning/FLOAT_INPUT_INJECTION_REVIEW.md` provided additional evidence that
CPU input floats must not be quantized onto the human controller's 1/80 grid.

## Boundaries

- This validates recorded CPU inputs; it does not admit autonomous CPU control
  through the public RL API.
- CPU Ice Climbers are rejected until their follower behavior is validated.
- Controller-only benchmark tapes reject CPU recordings, since they cannot
  represent the processed input stream. Their config layout revision is v3.
- Strict bitwise comparison and existing classifications are unchanged.

## Retained fixture

`replays/validation/cpu_inputs/exphil_peach_r1.slpz` is a lossless compressed copy
of `../exphil/eval_runs/0914_coverage_round/rollouts/peach/r1.slp`, recorded at
2026-09-14T20:33:03Z: P1 human Fox, P2 level-1 CPU Peach, Final Destination.
Original SHA-256:
`ac0f1f1b59f2adefaaf31858d47224aa355ef57894f35fe846e1f885b5cf2b78`.

Its first 5,000 transitions match exactly and form a regression test, including
reversed player-metadata ordering. Its full 5,658-transition run has 18 strict
mismatches at frames 5109–5126: expected +0, actual -0 in P1 knockback Y. This
fixture is not added as an authoritative full-game pass.

The 12 existing ExPhil CPU sources from `0911_g23a_live/cpu_rollouts/r{1,2}.slp`
and `0914_coverage_round/rollouts/{falco,fox,marth,peach,samus}/r{1,2}.slp`
ran through 67,867 transitions without runner errors. Every reported mismatch
field is knockback Y; first mismatch details show the historical +0/-0 arithmetic
difference. These are diagnostic runs, not 12 exact passes. See the separate
signed-zero handoff before assigning execution profiles.

## Checks

- `make source-check native-smoke viewer-schema-check`
- `pytest tests/test_cpu_replay_validation.py tests/test_melee_core_validation.py
  tests/test_melee_core_validation_runner.py tests/test_melee_core_replay_benchmark.py
  tests/test_slpz_replay_storage.py`
- `make validation-supported-domain VALIDATION_WORKERS=4 VALIDATION_ARGS="--timeout 30"`

The CPU smoke exercises physical P2/P3 ownership, exact negative-zero/subnormal
and off-grid float transport, follower exclusion, pending-input copy/save/restore,
and reset to human control. Existing supported-domain output locks remain
unchanged: 503 exact / 26 existing classified / zero failures or errors.
PPC replay validation is not established on this host; its pytest case skips
because the artifacts are unavailable. Ignored logs are under
`reports/triage/gamewatch_mewtwo/cpu-*`.
