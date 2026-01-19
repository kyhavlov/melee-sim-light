# Architecture (living)

This doc is the high-level code overview for agents working on `melee-sim-light`.
Keep it updated as we add systems.

## Goals / Constraints

- Deterministic: same inputs → same outputs for a given build/config.
- Performance-critical: SoA/AoSoA state, no allocations after initialization, fixed-capacity pools.
- C owns all gameplay logic; Python is a thin batch I/O wrapper.
- 2-player now, trivial to extend to 4-player (same code paths; config-only).

## Top-level layout

- `src/`: C simulator core
  - `api.h` / `api.c`: public C API + seed/compare packing
  - `config.h` / `config.c`: immutable runtime config (`MslConfig`)
  - `state.h` / `state.c`: hot SoA allocation/ownership (`MslStateSoA`)
  - `step.h` / `step.c`: **frame scheduler** (ordered list of passes)
  - `*.{h,c}` phases: `input`, `action`, `physics`, `stage_collision`, `hurtboxes`, `combat`, `items` (currently stubs)
- `python/`: CPython+NumPy extension (`msl_binding.c`) + build config
- `tools/`: dataset generation and one-step suite validation

## Core runtime objects

### `MslBatch`

Defined in `src/batch_internal.h`.

- Owns:
  - `batch_size`
  - `MslConfig config` (immutable toggles like UCF flags)
  - `MslStateSoA state` (SoA pointers, allocated once in `state_alloc`)

### `MslConfig`

Defined in `src/config.h`.

- Intended contents:
  - small set of “mode” toggles (UCF, cardinals, teams enablement)
  - **not** file paths or extracted tables
- Loaded/initialized at `msl_batch_create` time and treated as immutable.

### `MslStateSoA`

Defined in `src/state.h`.

- Hot SoA arrays sized for:
  - `[batch][MAX_PLAYERS]` for per-player fields
  - `[batch][MAX_ITEMS]` for fixed-slot items
- Allocation policy:
  - one-time allocation at init (`state_alloc`)
  - zero allocations in `step`

## Frame step (phase scheduler)

The “step” is structured as an explicit list of phases so we can:
- match (or approximate) the real engine’s ordering,
- move phases around without entangling code,
- isolate correctness issues to a single pass.

Current scheduler lives in `src/step.c` and calls passes in-order:

1. `input_apply` (input sampling / UCF legalization / edge detection)
2. `action_update` (action/state transitions + per-action callbacks)
3. `physics_integrate` (kinematics integration; gravity/traction/etc)
4. `stage_collision_apply` (FD collision + ECB/grounding/ledge gating)
5. `hurtboxes_refresh` (hurtboxes/hitboxes attached to bones/ECB)
6. `combat_resolve` (hit resolution: hitlag/hitstun/KB/shield, etc)
7. `items_update` (projectiles/items update/collision)

Notes:
- These functions are stubs today (the “empty sim”), but the structure is the contract.
- If we discover ordering differences, we change the call order in `src/step.c` and keep the pass boundaries stable.
- Hitlag/hitstun gating likely requires conditional skipping/modifying of multiple passes; handle that centrally in the scheduler.

## Seeding & validation

- Reseed path: `msl_batch_reseed_seed` copies replay-derived state into SoA.
- Step path: `msl_batch_step_input` calls the scheduler (`step_one_frame`).
- Compare path: `msl_batch_write_compare` packs SoA back into `MslCompare`.

Validation is “teacher-forced reseeded one-step” (see `AGENTS.md`), so the seed+step+compare pipeline is the main correctness surface for early development.

## Tests (guardrails)

Fast tests live under `tests/` and are intended to run in <1s.

- Unit tests (default): dataset format, rollback dedupe, and “no allocations after init”.
- Integration (`-m integration`): validate **existing** local `data/` artifacts for self-consistency (does not rebuild ISO data).

## Adding a new gameplay system (workflow)

1. Decide which pass owns it (or whether a new pass is needed).
2. Add any new required state fields to `MslStateSoA` (and seed/compare schemas if needed).
3. Implement the logic inside that pass using SoA loops over `[batch][player]` (and fixed pools for entities).
4. Use the one-step suite metrics to confirm you improved the relevant fields without regressions.
