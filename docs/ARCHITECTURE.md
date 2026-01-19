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
  - `msl_api.h` / `msl_api.c`: public C API + seed/compare packing
  - `msl_config.h` / `msl_config.c`: immutable runtime config (`MslConfig`)
  - `msl_state.h` / `msl_state.c`: hot SoA allocation/ownership (`MslStateSoA`)
  - `msl_step.h` / `msl_step.c`: **frame scheduler** (ordered list of passes)
  - `msl_pass_*.{h,c}`: individual “phase” implementations (currently stubs)
- `python/`: CPython+NumPy extension (`msl_binding.c`) + build config
- `tools/`: dataset generation and one-step suite validation

## Core runtime objects

### `MslBatch`

Defined in `src/msl_batch_internal.h`.

- Owns:
  - `batch_size`
  - `MslConfig config` (immutable toggles like UCF flags)
  - `MslStateSoA state` (SoA pointers, allocated once in `msl_state_alloc`)

### `MslConfig`

Defined in `src/msl_config.h`.

- Intended contents:
  - small set of “mode” toggles (UCF, cardinals, teams enablement)
  - **not** file paths or extracted tables
- Loaded/initialized at `msl_batch_create` time and treated as immutable.

### `MslStateSoA`

Defined in `src/msl_state.h`.

- Hot SoA arrays sized for:
  - `[batch][MAX_PLAYERS]` for per-player fields
  - `[batch][MAX_ITEMS]` for fixed-slot items
- Allocation policy:
  - one-time allocation at init (`msl_state_alloc`)
  - zero allocations in `step`

## Frame step (phase scheduler)

The “step” is structured as an explicit list of phases so we can:
- match (or approximate) the real engine’s ordering,
- move phases around without entangling code,
- isolate correctness issues to a single pass.

Current scheduler lives in `src/msl_step.c` and calls passes in-order:

1. `msl_pass_input_apply_v0` (input sampling / UCF legalization / edge detection)
2. `msl_pass_action_update_v0` (action/state transitions + per-action callbacks)
3. `msl_pass_physics_integrate_v0` (kinematics integration; gravity/traction/etc)
4. `msl_pass_stage_collision_v0` (FD collision + ECB/grounding/ledge gating)
5. `msl_pass_hurtboxes_refresh_v0` (hurtboxes/hitboxes attached to bones/ECB)
6. `msl_pass_combat_resolve_v0` (hit resolution: hitlag/hitstun/KB/shield, etc)
7. `msl_pass_items_update_v0` (projectiles/items update/collision)

Notes:
- These functions are stubs today (the “empty sim”), but the structure is the contract.
- If we discover ordering differences, we change the call order in `src/msl_step.c` and keep the pass boundaries stable.
- Hitlag/hitstun gating likely requires conditional skipping/modifying of multiple passes; handle that centrally in the scheduler.

## Seeding & validation

- Reseed path: `msl_batch_reseed_seed_v0` copies replay-derived state into SoA.
- Step path: `msl_batch_step_input_v0` calls the scheduler (`msl_step_one_frame_v0`).
- Compare path: `msl_batch_write_compare_v0` packs SoA back into `MslCompareV0`.

Validation is “teacher-forced reseeded one-step” (see `AGENTS.md`), so the seed+step+compare pipeline is the main correctness surface for early development.

## Adding a new gameplay system (workflow)

1. Decide which pass owns it (or whether a new pass is needed).
2. Add any new required state fields to `MslStateSoA` (and seed/compare schemas if needed).
3. Implement the logic inside that pass using SoA loops over `[batch][player]` (and fixed pools for entities).
4. Use the one-step suite metrics to confirm you improved the relevant fields without regressions.

