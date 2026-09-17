# Mewtwo targeted Dolphin recordings — 2026-09-15

The private suite `replays/suites/mewtwo_targeted.json` contains **10 exact
recordings / 38,734 compared transitions**. These are additional to the earlier
12 exact games. The unresolved, stripped-metadata Erickfm diagnostic remains
separate; it has not become a pass.

## Human Teleport and Confusion recordings

The three new games are human Mewtwo on P2 versus CPU Marth on P3, Battlefield:

| Recording | Compared transitions | Teleport startup entries | Confusion entries | Direct Teleport → CliffCatch |
| --- | ---: | ---: | ---: | ---: |
| `Game_20260915T192036` | 11,710 | 66 | 1 | 6 |
| `Game_20260915T192357` | 2,701 | 9 | 8 | 1 |
| `Game_20260915T192506` | 9,034 | 17 | 18 | 0 |

Startup entries count states 353/356; Confusion counts 351/352. Direct catches
require an adjacent transition from states 353–358 to 252. There are 46 ledge
catch entries overall; only the seven direct transitions above are claimed as
Teleport-to-ledge evidence. These counts do not independently identify every
wall collision or platform crossing.

Lossless files: `replays/validation/mewtwo_teleport/`.
Provenance, SHA-256, metadata, and observed state entries:
[MEWTWO_TELEPORT_CAPTURES.json](MEWTWO_TELEPORT_CAPTURES.json).

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
`AccurateNmsub=true` selects retail arithmetic explicitly; the suite declares
`fnmsubs_profile: retail`. Its build manifest and per-replay hashes are retained
in [MEWTWO_INTERACTION_CAPTURES.json](MEWTWO_INTERACTION_CAPTURES.json).
No tolerances, classifications, or output locks changed.

## Reproduction and checks

```sh
.venv/bin/python -m tools.validation.validate_replay --no-build \
  --suite replays/suites/mewtwo_targeted.json \
  --characters Mewtwo,Falco,Ness,Samus,Marth
.venv/bin/python -m pytest -q tests/test_mewtwo_interaction_recordings.py \
  tests/test_slpz_replay_storage.py
```

The supported-domain gate remains 503 exact / 26 existing classified / zero
failures or errors across 5,059,922 transitions.

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
against Falco laser/Samus missile, partial/full Shadow Ball absorption, and
Teleport-to-ledge recovery. They do not establish every projectile matchup,
aerial reflection, all wall/platform cases, PPC/Wasm parity, or public admission.
No production gameplay changes were needed for these recordings.

Ignored execution evidence: `reports/triage/gamewatch_mewtwo/targeted-captures-*`,
`targeted-capture-tests.log`, and `dolphin-fixtures/`.
