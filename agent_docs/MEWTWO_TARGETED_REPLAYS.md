# Mewtwo targeted Dolphin recordings — 2026-09-15

The private suite `replays/suites/mewtwo_targeted.json` contains **7 exact
recordings / 15,289 compared transitions**, all controller-scripted headless
Dolphin interactions with two human ports. Three further human-versus-CPU
Teleport games and two legacy-Dolphin arithmetic captures exist from the same
effort; they need the recorded-CPU-input and recording-arithmetic validation
lanes and ship with those, not with Mewtwo admission.

## Scripted interaction recordings

All seven captures use actual headless Dolphin gameplay, two human controller
slots, Final Destination, and controller inputs only. No positions, action
states, damage, RNG, or playback state are injected. Games end by walking
Mewtwo offstage until the match completes, yielding finalized Slippi files.

| Scenario | Observed evidence | Compared transitions |
| --- | --- | ---: |
| `reflect` | Nine Falco lasers reverse X velocity; original owner retained; Mewtwo takes no damage | 2,171 |
| `reflect_control` | No Confusion: lasers do not reverse and deal damage | 2,103 |
| `absorb` | First partial Shadow Ball deals 8%; Ness absorbs the next partial shot, heals to 0%, and later absorbs a full shot | 2,539 |
| `absorb_control` | No PSI Magnet: three shots hit, including the full shot; Ness reaches over 36% before losing a stock | 2,547 |
| `missile` | Four Samus missiles reverse; original owner retained; Mewtwo takes no damage | 2,133 |
| `missile_control` | No Confusion: missiles deal damage without reversing | 1,895 |
| `missile_early` | Confusion starts too early: missiles deal damage without reversing | 1,901 |

Absorption checks require Ness's actual `SpecialLwHit` state (369), disappearance
of the corresponding Shadow Ball on the next frame, projectile states 3 and 8
(partial/full), and healing without a stock change. Merely playing Magnet or
having an article disappear is not counted as absorption.

The installed Dolphin executable's SHA-256 is
`b694802a4210a5b0b5b6307713fc05e7596d57e6c7b4b44ae09aa7dc70bf644c`.
`AccurateNmsub=true` selects retail arithmetic explicitly, which matches the
validator's metadata default for these captures. Its build manifest and
per-replay hashes are retained
in [MEWTWO_INTERACTION_CAPTURES.json](MEWTWO_INTERACTION_CAPTURES.json).
No tolerances, classifications, or existing output locks changed. Seven new
output locks cover these recordings.

## Reproduction and checks

```sh
.venv/bin/python -m tools.validation.validate_replay --no-build \
  --suite replays/suites/mewtwo_targeted.json \
  --characters Mewtwo,Falco,Ness,Samus
.venv/bin/python -m pytest -q tests/test_mewtwo_interaction_recordings.py \
  tests/test_slpz_replay_storage.py
```

The seven recordings extend the supported-domain gate from 529 to 536 cases:
510 exact / 26 existing classified / zero failures or errors.

The coverage/storage tests pass (12 tests). They verify positive interactions
and their inactive/early controls; full simulated output is checked separately
by the strict replay suite.

To regenerate a scripted scenario, run from an ExPhil checkout with its compiled
bridge and libmelee_ex dependencies. Supply a Dolphin executable supporting the
recorded arithmetic option, an ISO, and a fresh absolute output directory:

```sh
devenv shell -- elixir -pa '_build/dev/lib/*/ebin' \
  /path/to/melee-sim-light/tools/validation/record_mewtwo_interactions.exs \
  missile /absolute/output/directory /path/to/dolphin-emu-headless /path/to/melee.iso
```

Scenario names match the table. Ports 51950–51956 are reserved per scenario by
this script; it uses isolated Dolphin homes and does not edit the user's launcher
configuration. The retained recorder was run successfully for the final missile
fixture. Earlier development captures used the same controller schedules;
failed/incomplete launch and quit experiments stay under ignored reports.

## Remaining scope

These fixtures provide independent Dolphin evidence for grounded Confusion
against Falco laser/Samus missile and partial/full Shadow Ball absorption.
They do not establish Teleport-to-ledge recovery, every projectile matchup,
aerial reflection, all wall/platform cases, PPC/Wasm parity, or public admission.
No production gameplay changes were needed for these recordings.

Ignored execution evidence: `reports/triage/gamewatch_mewtwo/targeted-captures-*`,
`targeted-capture-tests.log`, and `dolphin-fixtures/`.
