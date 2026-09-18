# Targeted Mewtwo tests — 2026-09-15

`make mewtwo-smoke` runs 39 targeted move scenarios and three charge/projectile
stress scenarios. It is also a prerequisite of `make native-smoke`.

These C tests combine focused source-callback checks with scheduled contact and
animation tests using extracted game data. They are not independently recorded
Dolphin oracles and do not establish every interaction or public admission.

## Coverage

| Move | Checks | Source owners |
| --- | --- | --- |
| Confusion | Ground/air capture, shield capture, out-of-range miss, release and capture-cut cleanup | `ftMt_SpecialS.c`, `ftCo_CaptureMewtwo.c`, `ftCo_ThrownMewtwo.c`, `ftCo_CaptureCut.c` |
| Confusion reflection | Fox laser and full Shadow Ball reversal; original projectile ownership retained; inactive controls take damage; reflection survives ground/air transitions and clears on exit | `ftMt_SpecialS.c`, `ftcoll.c`, item reflection callbacks |
| Confusion boost | Repeated aerial use does not grant another boost; landing through the move resets eligibility | `ftMt_SpecialS.c` |
| Disable | Grounded facing target enters DamageBind; facing-away target is untouched; airborne facing target takes damage without binding; expiry/damage/death callbacks clear article references | `ftMt_SpecialLw.c`, `itmewtwodisable.c`, `ftcoll.c`, `ftCo_DamageBind.c` |
| Teleport | Ground/air startup, travel and recovery; left/right/up/down and neutral direction; invisibility during travel and visibility afterward; aerial jump consumption and special fall | `ftMt_SpecialHi.c` |
| Shadow Ball | Ground/air charge, cancel and release; charge retained on cancel; partial charge cleared on damage, full charge retained; both cleared on death; aerial recoil and projectile ownership | `ftMt_SpecialN.c`, `ftMt_Init.c`, `itmewtwoshadowball.c`, common damage/death owners |
| State continuity | Charging article ownership relocates; copied/restored matches produce identical outputs and RNG through release | Match copy/save/restore and Shadow Ball article graph |
| Concurrency | Four simultaneous reflect windows, Disable articles, and Teleport travel states; two/four full-charge releases; twenty forward-throw projectile factories | Fighter callbacks, article factories and sealed pools |

Initial setups place Fox above Mewtwo for aerial contact because Fox falls faster
and Confusion boosts Mewtwo upward. Setup coordinates live only in tests. Missing
contact is not treated as evidence of a successful negative test unless a paired
positive establishes that the interaction occurs.

## Memory evidence

The 20-projectile factory stress is conservative: four fighters cannot form four
simultaneous thrower/victim pairs. It tests overlapping article demand, not that
impossible arrangement of grabs. No test changes positions/actions to force a
hit after the move begins.

The original four-Mewtwo reserve peaked at 797/825 FObj animation tracks, below
the limit but failing the 20% headroom rule used by `article_pool_smoke`. Raising
the additive reserve from 128 to 192 tracks per Mewtwo gives:

| Scenario | Track peak/capacity | Animation-object peak/capacity |
| --- | --- | --- |
| Two charge/releases | 131/697 | 42/282 |
| Four charge/releases | 205/1081 | 58/410 |
| Twenty forward-throw projectiles | 797/1081 | 186/410 |
| Four Confusions / Disables / Teleports, each | 57/1081 | 26/410 |

The throw burst leaves about 26% spare track capacity. Both smoke executables
check that stepping does not grow the initialized arena or allocation count.
Charge/throw stress scenarios require at least 20% spare FObj/AObj capacity.
This is bounded stress evidence, not a proof of every possible move overlap.

On this linux/amd64 build `sizeof(HSD_FObj)` is 64 bytes. The reserve increase adds
4 KiB of track storage per Mewtwo (16 KiB for four), plus allocation/relocation
bookkeeping. `HSD_FObjInterpretAnimAll` visits linked live tracks, while
`HSD_ObjAlloc` pops a free-list node; neither scans all reserved slots per frame.
Initialization and copy/save/restore process more storage. No throughput
benchmark was performed, so cache and batch-throughput effects remain unmeasured.

## Results

- `make native native-smoke source-check -j8`: passed.
- `make mewtwo-smoke`: passed after final test cleanup.
- The seven scripted interaction recordings pass exactly with metadata
  arithmetic defaults. Five further diagnostic recordings (three human-versus-CPU
  Teleport games and two legacy-Dolphin arithmetic captures) pass only with the
  recorded-CPU-input and recording-arithmetic validation lanes and ship there.

No comparison tolerances, classifications, or existing replay locks changed. Native
results do not establish PPC/Wasm equivalence. Independent targeted Dolphin
captures now cover Falco laser/Samus missile reflection and partial/full Shadow
Ball absorption; see
[MEWTWO_TARGETED_REPLAYS.md](MEWTWO_TARGETED_REPLAYS.md). Other projectile matchups,
aerial reflection, and specifically identified wall/platform cases remain outside
that evidence.

Ignored evidence: `reports/triage/gamewatch_mewtwo/mewtwo-{moves,headroom-before,
targeted-regression,targeted-replays}.log`.

## PR admission scope

This PR admits Mewtwo only. The aggregate adds seven interaction recordings
and their output locks to the existing 529 cases. Evidence from the earlier
combined Mewtwo/Game & Watch development branch does not establish this PR's
PPC, optimized-build, or all-stage restore coverage.

Review verification on 2026-09-17 independently passed the seven Mewtwo
recordings on native and PPC, the 536-case native aggregate, `native-smoke`,
`viewer-smoke` (including Wasm), and all 69 Python tests. Review fixes regenerated
the incorrect upstream snapshot digest, tracked the stage-lifecycle smoke's
header dependencies, and removed stale combined-admission metadata.
The source imports match the pin except for the ledgered attribute
self-assignment removal; gameplay and existing output locks are unchanged.

The cleanup was pushed as `5018738c`; CI passed and PR #23 merged at `de64f76a`
on 2026-09-17. The [historical work log](README.md#historical-work-log) retains
the full review and pre-merge integration analysis.
