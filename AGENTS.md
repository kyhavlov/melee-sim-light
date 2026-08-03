# AGENTS.md — melee-sim-light

## Goal and supported domain

Build a high-performance, batched, deterministic Melee simulator for RL. Supported fighters are
Fox, Falco, Marth, Sheik, Zelda, Captain Falcon, Jigglypuff, Peach, Luigi, Mario, Dr. Mario,
Samus, the Ice Climbers (Popo with the CPU-mimic Nana follower), Pikachu, Donkey Kong, and
Ganondorf. RL 1.0 covers singles and doubles on Final Destination, Battlefield, Fountain of
Dreams, frozen Pokemon Stadium, Yoshi's Story, and Dream Land N64. UCF is enabled by default.

Correctness comes from gameplay-relevant source completion: port the relevant decomp call graphs,
persistent state, tables, callback ownership, and scheduler order. Organize implementation by
source owner, not replay row. Validation challenges a source-completion claim; it is not the
implementation queue.

## Structural work

1. Before a multi-owner or representation packet, record its final owner, canonical state,
   consumers, displaced code/state, and deletion boundary in
   `agent_docs/ACTIVE_WORK.md`.
2. Do not add synchronized dual state, setter hooks, compatibility flags, fallback dispatch, or a
   production bridge for an incomplete cutover.
3. Establish the approved final representation and deletion boundary first, then recover
   correctness and performance inside it. Never restore displaced machinery merely because an
   incomplete cut is red or slower.
4. Log material experiments before and after execution. Stop and reconvene after two hours without
   a retained result/completed deletion boundary, after two failures on the same architectural
   unknown, or whenever benchmark provenance or the intended final path is uncertain.
5. Never blanket-delete or replace substantial dirty work without explicit approval and a salvage
   inventory. Do not commit unless the user or active goal authorizes it.
6. All durable performance history lives under `agent_docs/performance/`. Before a performance
   packet, search that directory for the proposed owner, symbols, and approach, then review
   `agent_docs/performance/README.md` and the relevant records. Add or update an indexed record
   when an architectural candidate is rejected or when new evidence materially changes an earlier
   conclusion.

## Runtime and source requirements

- No heap allocation after initialization on gameplay, observation, reset, copy, or save/restore.
- Use deterministic ordering. Immutable game data is shared; mutable state belongs to one match
  and must save/restore at any batch index.
- The production API is one single-threaded batch per process. Do not add an internal worker pool.
- New repository-owned symbols use the plain `msl_`/`Msl`/`MSL_` namespace. Existing
  `msl_core_`/`MslCore`/`MSL_CORE_` names are cleanup debt, not a naming precedent; when a plain
  name would collide, use the actual subsystem owner rather than a generic `core` qualifier.
- Gameplay lives in C under `src/`. Python is cold extraction/codegen/replay/report tooling only.
- Gameplay logic must be backed nearby by `refs/melee`, matching asm, `refs/slippi-ssbm-asm`, or
  extracted `data/`. Do not add replay-fit constants or character-id proxies for missing state.
- Stage geometry and fighter animation/move/hitbox/hurtbox data come from extracted game files.
- A runtime artifact change updates extractor, deterministic layout, required loader, and fresh
  extraction smoke together.
- Preserve upstream-shaped formatting. No root command may blanket-format imported gameplay.

## Routine workflow

- Routine commands should finish within five seconds and normally remain below ten seconds.
  Announce fresh full builds, full replay gates, and production viewer builds first. Occasional
  slower commands are ok if necessary/justified.
- Use root `Makefile` targets: `source-check`, `native-smoke`, `validation-suite`, `wasm-smoke`,
  `viewer-smoke`, and the bounded benchmark targets.
- The full supported-domain gate is a material checkpoint, not an inner loop.
- Bit-exact suite identities (aggregate results, classification snapshots, output locks) are
  authoritative only from linux/amd64 GNU-toolchain builds: CI, a native Linux host, or the Linux
  container on macOS. macOS-built binaries — arm64 or the Rosetta `HOST_TARGET_ARCH=x86_64`
  profile — have no recorded bit-exact equivalence and must not record suite results.
- Put forensic outputs under ignored `reports/triage/` and never hand-edit generated results.
- New-core plans and evidence live only under `agent_docs/`.
- A performance commit must include implementation, gates, and refreshed retained evidence in
  `agent_docs/performance/HISTORY.md`.
