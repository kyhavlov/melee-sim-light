# AGENTS.md — melee-sim-light

## Goal and supported domain

Build a high-performance, batched, deterministic Melee simulator for RL. Supported fighters are
Fox, Falco, Marth, Sheik, Zelda, Captain Falcon, Jigglypuff, Peach, Luigi, Mario, Dr. Mario,
Samus, the Ice Climbers (Popo with the CPU-mimic Nana follower), Pikachu, Donkey Kong, and
Ganondorf, Yoshi, Bowser, Ness, Link, Young Link, Mewtwo, Mr. Game & Watch, Roy, Pichu, and Kirby. RL 1.0 covers
singles, three-player teams, and doubles on Final Destination, Battlefield, Fountain of
Dreams, frozen Pokemon Stadium, Yoshi's Story, and Dream Land N64. UCF is enabled by default.

Correctness comes from gameplay-relevant source completion: port the relevant decomp call graphs,
persistent state, tables, callback ownership, and scheduler order. Organize implementation by
source owner, not replay row. Validation challenges a source-completion claim; it is not the
implementation queue.

## Implementation

- Gameplay lives in C under `src/`; Python handles extraction, code generation,
  replay tooling and reporting.
- Back gameplay changes with `refs/melee`, matching assembly,
  `refs/slippi-ssbm-asm` or extracted data. Do not fit behavior to individual replays.
- Use one single-threaded batch per process. Immutable game data is shared;
  mutable state belongs to a match and must support reset/copy/save/restore at any index.
- No heap allocation after initialization on gameplay, observation, reset, copy
  or save/restore paths. Size fighter reserves by the number of that fighter,
  including followers, with separate capacity for stage-owned entities.
- Refactors should replace the old representation completely. Do not introduce
  synchronized duplicate state, compatibility flags or bridges for incomplete cutovers.
- Use `msl_`, `Msl` and `MSL_` for new repository-owned symbols.
- Preserve upstream formatting. Do not blanket-format imported gameplay.
- Runtime artifact changes update the extractor, layout, loader and fresh extraction
  check together. Fighter admissions update this roster, both `supported_character`
  gates, `VALIDATION_CHARACTERS` and the viewer's `externalCharId` mapping.

## Verification

- Use the root Makefile's focused checks and `source-check`, `native-smoke`,
  `validation-suite`, `wasm-smoke`, `viewer-smoke` and bounded benchmark targets.
- Run the full replay gate at material checkpoints, not after every edit.
  Announce full builds, replay gates and production viewer builds before running them.
- Recorded bit-exact suite results and output locks require certified Linux/amd64
  GNU builds. See `agent_docs/ENVIRONMENT.md`; macOS results are not interchangeable.
- Compare performance on the same host, compiler and workload. Preserve gameplay
  and report the relevant correctness checks with any claimed improvement.

## Workspace and documentation

- Keep `agent_docs/ACTIVE_WORK.md` short and limited to open work. For a large
  refactor, record the intended state ownership and what old code will be removed.
- Keep scratch experiments and raw measurements under ignored `reports/triage/`.
  Never hand-edit generated validation results.
- Keep current benchmark commands and results in `agent_docs/performance/BASELINE.md`.
  Put change-specific evidence in commit descriptions. Do not recreate historical
  journals, failed-attempt archives, duplicate ledgers or completed-task status docs.
- Keep durable correctness facts beside their source or tests. Preserve
  `agent_docs/validation/*.json`; validation tooling consumes those records.
- Preserve unrelated dirty work and stashes. Substantial deletion needs user
  authorization and a salvage inventory. Commit only when authorized.
