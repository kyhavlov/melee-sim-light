# Fox shield / spot-dodge replay discrepancy

Resolved under an explicit historical UCF profile, 2026-09-15. No gameplay
change, comparison relaxation, or new runtime profile flag was needed.

## Recording and first divergence

The retained Erickfm Mewtwo/Fox recording on Yoshi's Story is
`replays/validation/mewtwo_erickfm/master-master-5de7543e8002c09425f1f2d4.slpz`.
It has protocol 3.14 and empty metadata. Source hash is in
`MEWTWO_ERICKFM_INVENTORY.json`.

Fox lands, enters GuardOn at frame352, then rotates his main stick from right
toward down-right while holding L. At frame355 the recorded Fox remains in
GuardOn (178); the modern-profile simulator enters EscapeN (235).

A native debugger trace at the actual decision records:

- main stick (0.7, -0.7), neutral C-stick;
- horizontal tilt timer14, vertical tilt timer2;
- floor index3, flags18: solid ground, no platform flag (bit8);
- call chain: Fighter IASA -> ftCo_GuardOn_IASA -> ftCo_8009980C ->
  msl_ucf_suppress_spotdodge.

This rules out missing raw C-stick data as the cause of this decision.

## Source explanation

`External/UCF 0.8/Logic/UCF SD.asm`, injection 0x800998A4, suppresses a
spot dodge when the C-stick is not commanding down, the main stick passes
its rim test, the horizontal tilt timer exceeds3, and stick Y is above -0.8.
It does not require a platform. Fox's traced state meets these conditions.

`External/UCF 0.84/UCF/UCF Shield Drop.asm` additionally requires a platform.
The simulator already implements both source owners in
`src/runtime/match.c::msl_ucf_suppress_spotdodge`, selected by the existing
`ucf_shield_drop_084_enabled` capability. Assembly was inspected in the local
`/home/blewf/git/slippi-ssbm-asm` checkout.

## Results and reproducer

Use `validate_one` with `backend="native"`, `played_on="network"`,
`ucf_cardinals_1_0_enabled=False`, and `ucf_shield_drop_084_enabled=False`:
all 9,458 transitions match exactly, with zero signed-zero allowances.
Keeping the latter option true reproduces frame355 after477 exact transitions.

Run `.venv/bin/python -m pytest -q tests/test_mewtwo_erickfm_profile.py`.
Tests preserve the recorded input/state evidence and alternate old/new/old
profiles on one persistent runner, checking both full-game exactness and the
original mismatch. Both tests pass.

The profile is inferred from source behavior and full-game agreement; stripped
metadata does not identify the original Dolphin executable or every historical
setting. The fixture remains outside the aggregate supported-domain suite.
This adds one exact recording under documented assumptions to the prior22
Mewtwo recordings; it does not establish public support on its own.
