# Replay arithmetic profiles

## Finding

Four July recordings (two Mewtwo/Fox, two Fox/Game & Watch, all from the same
recorder) fail strict comparison under the metadata default. The two Mewtwo
failures were traced to Fox's common airborne knockback decay,
`Fighter_procUpdate` in `src/melee/ft/fighter.c`, corresponding to retail
GALE01 `fnmsubs` at **0x8006B9E4**. This is not a fighter callback defect.

| Recording | First output mismatch | Default mismatch rows | First writer operands (binary32 hex) |
| --- | ---: | ---: | --- |
| `mewtwo_fox_20260702T170056` | 1000 | 50 | a=3d50e560, b=00000000, c=00000000 |
| `mewtwo_fox_shfair_only` | 4041 | 20 | a=3d50e560, b=00000000, c=00000000 |
| `gamewatch_fox_20260707T142746` | 6573 | 62 | not traced; same recorder, same profile differential |
| `gamewatch_fox_20260707T172242` | 1146 | 68 | not traced; same recorder, same profile differential |

Here a is knockback decay, b is sin(knockback angle), and c is incoming vertical
knockback. GDB observes match frame_id 999/4040 during the step producing output
1000/4041. Both prefixes are otherwise bit-exact. The horizontal operands at
that step are respectively b=3f800000, c=3f7fa958 / c=3fb8af62.

Retail negates after fused subtraction: `-(a*b-c)`, producing **-0**.
The historical Dolphin JIT instead fuses `c-a*b`, producing **+0**.
Both recordings contain +0. The source-backed simulator implementation already
supports both semantics through `online_fnmsubs_zero`; the variable name is
historical and does not limit the capability to online games.

Local matching retail asm is `/home/blewf/git/melee/build/GALE01/asm/melee/ft/fighter.s`;
Dolphin implementation is
`/home/blewf/git/slippi-Ishiiruka/Source/Core/Core/PowerPC/Jit64/Jit_FloatingPoint.cpp`.
Its accurate path computes subtraction then flips the sign; the legacy FMA path
uses the reverse subtraction instruction. That source evidence and the exact
operands explain the difference without a tolerance or a zero clamp.

The user identifies the recorder as Mainline Slippi Dolphin launched through
Slippi Launcher. The historical executable/settings are unavailable. We infer
**arithmetic behavior**, not a particular build or version. Neither replay
metadata, CPU identity, nor current launcher contents prove historical behavior.

## Representation and reproduction

The validator accepts `--fnmsubs-profile retail|dolphin-legacy`; suite replay
entries accept the same `fnmsubs_profile` field. This maps directly onto the
existing match capability, before match initialization. It changes no replay
bytes, comparison rules, runtime layout, or public default. Omission preserves
existing metadata defaults. There is no automatic retry or per-replay gameplay
branch. Selected profiles are printed and retained in Python results.
Benchmark export preserves the same capability and separates its cache entries.

The four losslessly compressed fixtures and provenance hashes are recorded in
[ARITHMETIC_PROFILE_CAPTURES.json](ARITHMETIC_PROFILE_CAPTURES.json). They form
`replays/suites/legacy_arithmetic.json`. The suite needs Mewtwo and Mr. Game &
Watch admission; until both are supported it stays outside the aggregate and
its replay tests skip with that reason, and it joins the aggregate with output
locks once they land:

```sh
make fnmsubs-smoke
.venv/bin/python -m tools.validation.validate_replay --no-build \
  --suite replays/suites/legacy_arithmetic.json --characters "Mewtwo,Game & Watch,Fox"
.venv/bin/python -m pytest -q tests/test_arithmetic_profile.py
```

Override that suite with `--fnmsubs-profile retail` to reproduce all four strict
failures. Expected default Mewtwo fingerprints are `19f2a7103073800d` (July 2)
and `bf3c7026a929b2cd` (shfair). Do not normalize signed zeros: a later `atan2f`
can distinguish them and change an angle-dependent result.

## Tests and limits

- `fnmsubs_smoke.c` checks twelve explicit operand cases under both profiles:
  captured operands, signed-zero combinations, nonzero cancellation, ordinary
  nonzero results, and rounded underflow that must not be treated as exact
  cancellation. It runs under `native-smoke` without game data.
- Replay tests alternate legacy, retail, metadata, and legacy on one persistent
  runner. Legacy must be fully exact; retail/metadata must reproduce the original
  mismatch counts and first frames. Zero-equal diagnostics remain disabled. A
  supported-roster corpus replay checks the override and benchmark plumbing
  without the fighter dependency.
- Invalid profiles are rejected at suite and native boundaries. Benchmark tapes
  differ only at the existing capability byte, and cache keys differ by profile.
- This is a native regression and source/asm comparison, not a newly executed
  independent PPC hardware or Dolphin interpreter oracle.

Other users of this instruction include horizontal knockback and shield
knockback. This finding motivates checking their arithmetic profile when the
same residual appears; it does not establish that every float discrepancy has
this cause. Existing ExPhil CPU captures are independent examples of the same
profile distinction. No broad profile reassignment is justified.

Ignored traces and run outputs: `reports/triage/gamewatch_mewtwo/zero-*`.

## Validation results

All four fixtures pass exactly under the explicit legacy profile
(43,211 transitions) and reproduce the table above under retail or the
metadata default. The supported-domain aggregate is unchanged: no lock, tolerance
or classification changes.
