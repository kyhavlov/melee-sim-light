# Engineering documentation

[ACTIVE_WORK.md](ACTIVE_WORK.md) tracks open work and pending decisions.
Completed work belongs in the references below; historical status labels in
dated reports are not a current task queue.

| Topic | Reference |
| --- | --- |
| Host setup, extraction and gate requirements | [ENVIRONMENT.md](ENVIRONMENT.md) |
| Performance contract, retained changes and rejected experiments | [performance index](performance/README.md) |
| Current performance checkpoint | [PERFORMANCE.md](PERFORMANCE.md) |
| Completed source/correctness work | [September 8 completion](CORRECTNESS_COMPLETION_2026-09-08.md) |
| Replay classifications and capture limitations | [audit](CLASSIFIED_REPLAY_AUDIT_2026-09-08.md), [closure](CLASSIFIED_REPLAY_CLOSURE_2026-09-08.md) |
| Earlier branch integration | [DECOMP_DEV_INTEGRATION.md](DECOMP_DEV_INTEGRATION.md) |
| Yoshi and Bowser admission | [YOSHI_BOWSER_SUPPORT.md](YOSHI_BOWSER_SUPPORT.md) |
| Mewtwo admission and review | [move tests](MEWTWO_MOVE_TESTS.md), [recordings](MEWTWO_TARGETED_REPLAYS.md) |
| Game & Watch admission and review | [GAMEWATCH_SUPPORT.md](GAMEWATCH_SUPPORT.md) |

## Historical work log

The completed 7,306-line mixed work log is preserved in
[commit 5018738c](https://github.com/kyhavlov/melee-sim-light/blob/5018738c8b2da68330823bb20fee37a5044841cd/agent_docs/ACTIVE_WORK.md).
It includes earlier character admissions, source-owner investigations, performance
experiments and the Mewtwo review. Its old "active" labels are historical.
The original Game & Watch packet is preserved in
[commit bda971d6](https://github.com/kyhavlov/melee-sim-light/blob/bda971d65815425b7915af60e0ebf3edf5d7d840/agent_docs/ACTIVE_WORK.md#mr-game--watch-admission--2026-09-15).

The cleanup keeps the existing topic reports and performance records, moves
completed Mewtwo review findings into its support notes, and uses these immutable
snapshots for older detail instead of duplicating the entire log in another file.
