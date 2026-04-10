# Melee Sim Light — Specification

## Purpose

Build a high-performance, deterministic, batched simulator for **SSBM-like** gameplay suitable for RL, starting with **Fox vs Falco on Final Destination**, while being designed so it is trivial to enable **4 players (2v2)** later.

The simulator is explicitly **not** attempting 1:1 float/ordering parity with GALE01; instead it targets **behavioral fidelity** high enough that an existing policy (e.g. from `slippi-ai`) does not obviously “break” when driven by simulator observations and when its actions are applied in the simulator.

## Non-Negotiables

- **Data-driven from game files**:
  - Stage collision/models/coordinates are extracted from the game’s stage files.
  - Character animation/move/hitbox/hurtbox data are extracted from the game’s character files.
  - No hand-entered frame data tables as the primary source (small compatibility overrides are allowed but must be explicitly tracked).
- **Deterministic**: same inputs → same outputs for a given build/config.
- **Performance-critical, batched, vectorized**:
  - **No allocations after initialization** (including “hidden” allocations in logging, formatting, container growth, etc.).
  - Built as a **vectorized/batched env** with cache-local hot loops (SoA/AoSoA layouts, fixed-capacity pools).
- **2-player first, 4-player trivial**: all core systems operate on `N ∈ {2,4}` players with the same code paths; only config limits differ.

## Target Interfaces

### Step API (core engine)

- `init(config, batch_size, num_players)` → immutable layout + mutable state buffers
- `reset(batch_mask, initial_state_spec)` → deterministic spawn/reset
- `step(inputs[batch][num_players])` → advance exactly one 60 Hz frame
- `get_observations(obs_out[batch][viewpoint])` → packed observations for policy

Notes:
- “viewpoint” means observation may be emitted from P1/P2 perspective (and later 4 viewpoints for doubles).
- `num_players` is runtime-configurable but storage is sized for `MAX_PLAYERS=4`.

### Observation Compatibility (initial goal)

The initial observation schema should be compatible with `slippi-ai`’s `Game` embedding defaults:

- Per-player (minimum):
  - `percent`, `facing`, `x`, `y`, `action` (GALE01 action id), `character`, `hurtbox_state` (0/1/2),
    `jumps_left`, `shield_strength`, `on_ground`, `is_dead`, `stocks_left`
    - `hurtbox_state` semantics (Slippi-seeded, decomp-shaped eligibility): `0` vulnerable, `1` invincible, `2` intangible.
      Intangible prevents BODY hurtcapsule checks; invincible may still register a contact (attacker hitlag) but blocks defender percent/KB/hitstun writes.
      Decomp: `refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B868`, `refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8`. Slippi: `tools/slippi/combat_history.py`.
- Game (minimum):
  - `stage`, `is_teams`, and optional `randall_phase` (can be constant on FD), optional items.

Important implication: because `slippi-ai` consumes a **one-hot action id** (size `0x18F`), this sim must maintain an **action/state machine with GALE01 action ids** (at least for the states reachable by Fox/Falco on FD), not just “coarse locomotion.”

## Scope (Initial Target)

### Domain

- Stage: **Final Destination**
- Characters: **Fox, Falco**
- Players: **2** (design supports **4**)
- Items/projectiles: **lasers** (as a minimal projectile system)
- Camera, rendering, audio: none

### Required Gameplay Systems

To reach “90–95% like real Melee” for the target domain, the initial target must include:

1) **Input processing**
- Digital buttons and analog sticks sampled at frame boundaries.
- Configurable “legalization”/clamping consistent with controller conventions.
- **UCF is enabled by default** (because it applies to essentially all modern replay data we will validate against).
  - “UCF 1.0 cardinals” should be treated as suite/dataset configuration (more recent/niche), not an ad-hoc gameplay toggle.

2) **Action/state machine (GALE01 action ids)**
- Action ids are stored per player and updated each frame.
- Transitions are defined by:
  - current action id
  - input-derived intents (stick, buttons)
  - timers (anim frame, hitlag, hitstun, IASA-like windows)
  - environment contacts (grounded, ledge, wall)
  - combat events (was_hit, shield_hit, grabbed)
- Action logic is predominantly table/data-driven:
  - per-action flags and timers
  - per-action movement modifiers (where appropriate)
  - per-action hitbox/hurtbox enable windows (from extracted animation data)

3) **Kinematics**
- Ground motion: acceleration, traction/friction, turnaround, dash/run, slope ignored (FD is flat).
- Air motion: gravity, terminal velocity, drift, fastfall, jump physics (including double jumps).
- Landing: landing lag and state transitions.
- Ledge interactions: grab, hang, climb/roll/jump options (needed for FD edge behavior).

4) **Stage collision**
- Collision queries against extracted FD collision geometry:
  - ground intersection, ledge detection, blast zones
  - basic wall/ceiling can be supported but FD mainly requires ground + ledge + blast zones
- Character-body collision proxy (ECB-like) should aim to be **very close to the real game**:
  - stable grounded detection
  - ledge grab gating
  - consistent resolution that does not jitter
  - only “simplified” as an absolute last resort when the remaining gaps are tiny float/ordering mismatches

### FD Grounding Notes (ECB-bottom)

Current FD grounding uses an ECB-bottom proxy derived from extracted animation matrices. This improves false `on_ground`
vs using root `pos_y`. The current implementation is decomp-shaped in two important ways:

- Grounding comparisons use **previous-frame ECB** when comparing pre/post integration positions (similar to how mpColl uses
  `prev_ecb` vs `ecb`), so pose-driven ECB changes do not spuriously de-ground grounded actions when root `dy==0`.
- On the *landing* frame (air→ground), we preserve pre-collision `speed_y_self` (which can remain negative in Slippi
  post-frames) and only zero it on the subsequent grounded frame.

There are still teacher-forcing conveniences (to tolerate minor seed alignment error), but they are intended to be bounded
and decomp-motivated rather than arbitrary heuristics.

5) **Combat**
- Hurtboxes/hitboxes extracted from character animation/move files:
  - hitbox positions tied to animation bones/transforms
  - hitbox active windows per action/anim frame
  - hurtbox set per anim frame (goal: extremely close to real; simplification is last resort)
- Hit resolution:
  - hitlag, hitstun, knockback, tumble
  - DI (goal: close to real; only simplify once extremely close)
  - shield interaction (see next section)

### Combat Mutations (current)
- **BODY-only**: world-space hitbox spheres vs world-space hurtcap capsules, with existing grounded/airborne gating.
- **Shield-safe**: if a hitbox overlaps the defender shield bubble, that (attacker, defender, hitbox_id) is treated as SHIELD
  and does not apply BODY mutations.
- **Damage/KB writeback reshaping (BODY hits)**: percent/hitlag/hitstun/KB-vel/damage-state writes are decomp-shaped but still
  incomplete until the full collision multiplier chain into `ftColl_80079AB0` (attack/defense ratios + `gm_8016B248()`) is modeled.
  Known suite blip (2026-01-27): `TreasuredBackKangaroo.msl` net `+1` discrete mismatch due to `mismatch.facing +3` and
  `mismatch.on_ground -2` after the reshape.
- **SHIELD minimal mutations** (current):
  - Resolve at most 1 shield hit per attacker→defender per frame (deterministic `hitbox_id` order).
  - Apply decomp-backed shield HP depletion and enter `GuardSetOff` (shieldstun) on the defender.
  - Apply hitlag (same decomp `ftCommon_CalcHitlag` path as BODY; no SFX, no KB/percent, no clanks/trades).
- **Collision timing / translation ordering (decomp-backed, GALE01 “normal match” procs)**:
  - pri `1`: anim advance / pose timebase (`refs/melee/src/melee/ft/fighter.c::Fighter_8006A360` → `ftAnim_8006EBA4`)
  - pri `3`: per-action `input_cb` (`refs/melee/src/melee/ft/fighter.c::Fighter_Spaghetti_8006AD10`)
  - pri `4`: per-action `phys_cb` + integration mutating `fp->cur_pos` (`refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate`)
  - pri `6` / `9`: model translate + collision primitive refresh
    (`refs/melee/src/melee/ft/fighter.c::Fighter_procMap` and `refs/melee/src/melee/ft/fighter.c::Fighter_8006C80C` / `ftColl_8007AE80`)
  - pri `13`: fighter-vs-fighter collision (incl. shield overlap) (`refs/melee/src/melee/ft/fighter.c::Fighter_8006CB94` → `refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70`)
  - See also `docs/DECOMP_PROC_ORDER.md` (proc priorities, plus the exact referenced call sites).
  - Implication: fighter-vs-fighter collision uses the **post-integration** `fp->cur_pos` translation (i.e. no “pre-physics translate” frame).
    Any sim-side compensation that effectively shifts already-computed world primitives to a different translation frame is a **current approximation**
    for an ordering mismatch, not a decomp-backed rule.
- **Deterministic selection**: at most 1 BODY hit per attacker→defender per frame; prefer lowest `hitbox_id`, then lowest
  `hurtcap_id` (matches debug contact ordering).
- **Rehit suppression (hitlists, decomp-shaped; PARTIAL)**: the sim uses a decomp-inspired hitlist keyed by
  `(attacker, hit_group, victim)` with a per-entry cooldown derived from extracted hitbox metadata (`hitbox_u16_7` low 8 bits).
  - Clear/copy on enable edges: when a hitbox becomes enabled (or its `hit_group` changes), hitlists are cleared unless a
    currently-enabled sibling hitbox with the same `hit_group` can be copied (decomp anchor: `ftColl_800768A0`).
  - Countdown decrement: decremented once per frame for groups that are active (and frozen under hitlag) (`src/hitlist.c`).
  - Victim identity: decomp stores a raw victim pointer; the sim uses Slippi `instance_id` as a proxy but intentionally does
    **not** treat an `instance_id` bump on motion-state entry (`ft_800895E0`) as a “new victim” (rebinding proxy identity instead).
    True “pointer changed” is approximated by death/respawn states (heuristic boundary; see `src/hitlist.c`).
- Remaining decomp pieces: exact countdown timing/order (`lbColl_80008A5C`) relative to collision acceptance, “type” code
  semantics (`lbColl_80008688`/`lbColl_80008820`), fighter-vs-item clanks/trades, and full hurtbox eligibility (intangibility,
  thrown-fighter rules, etc.). These are required to make BODY/SHIELD mutations (percent/KB/hitstun/shield HP) consistently correct.

#### Facing rotation note (world primitives)

Decomp applies fighter facing by rotating the root part about Y (e.g. `ftPartSetRotY(fp, 0, (M_PI_2 * fp->facing_dir))`),
which mixes X/Z for pose-derived world primitives.

**Current sim policy (v1):**
- Pose-derived **hitboxes** and **hurtcaps** apply the same decomp-shaped root rotY90 (X/Z mix) for facing, for consistency
  across combat geometry and item spawn transforms.
- This is **not** an SSANIM axis remap: we are only applying the decomp-facing root rotation; the extracted pose matrices
  remain in the same canonical basis (see `docs/SSANIM_AXIS_BASIS.md`).

### `state_flags` Ownership (seed vs derived)

The dataset exposes 5 raw bytes of `state_flags` captured from fighter offsets `(0x2218, 0x221A, 0x221B, 0x221C, 0x221F)`
in that order (see `refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm`). The sim treats `state_flags` as **partially
sim-owned**:

**Sim-owned derived outputs (overwritten by the sim each step)**
- `0x221A` bit `0x20` (`isHitlag`): derived from `hitlag > 0` (kept consistent when combat applies hitlag and as timers decrement).
- `0x221B` bit `0x80` (`isShieldActive`): derived from whether the shield bubble is active (`shield_radius > 0`).
- `0x221C` bit `0x02` (`isHitstun`): derived from `hitstun > 0` (kept consistent as combat applies hitstun and as timers decrement).
- `0x221C` bit `0x04` (`x221C_b5`, “detection/inert hitbox touching shield bubble”): decomp sets
  `victim_fp->x221C_b5 = true` during the fighter-vs-fighter collision pass only on the shield-overlap branch for `HitElement_Inert`
  (`refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70`), and clears it in the post-collision consumer
  (`refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC`). See `docs/DECOMP_PROC_ORDER.md`.
  - **Sim timing semantics (decomp-shaped)**: this bit is set during `combat_resolve()` when we observe an inert (`HitElement_Inert`) shield
    overlap, and cleared by `combat_processhit_consume()` at the start of the *next* sim frame (our stand-in for
    `Fighter_ProcessHit_8006D1EC`). As a result, it is visible in the post-step output for one frame.

**Seed-only passthrough (currently)**
- All other `state_flags` bits are passed through from the seed to output unchanged (even if the sim consults them as gates).
  Examples:
  - `0x221C` bit `0x20` (powershield active) is consulted for shield-damage gating, but is not mutated by the sim yet.

6) **Shield**
- Shield health/decay/regeneration (approx ok).
- Shieldstun + basic pushback.
- Roll/spotdodge out of shield.
  - Not modeled yet: EscapeF/EscapeB root-motion (`fp->x6A4_transNOffset`) and mid-roll facing flip (`ftCheckThrowB3`); we currently apply friction-only and use anim-end to return to Wait.
  - Not modeled yet: escape invincibility / hurtbox state changes during EscapeN/EscapeF/EscapeB.
- Shield bubble center is **data-driven** from ISO-derived shield-tilt tables and sim guard-tilt state.
  Remaining approximations:
  - We do not yet model the full `shield_hit.bone` + `shield_hit.offset` semantics used in collision (`ftColl_8007B1B8`; refs/melee/src/melee/ft/ftcoll.c:1370-1383).
  - We do not yet model full 3D rotation/TransN/root-motion for shield placement (see `docs/SSANIM_AXIS_BASIS.md`).

7) **Projectiles (lasers)**
- Spawn and integrate laser entities.
- Laser collision/hit application.

## What “Approximate” Means Here

Allowed approximations (Initial Target / RL 1.0):
- Do not match 1-ULP float results; use stable FP but prioritize consistent ordering.
- Very fine-grained mechanics (SDI/ASDI nuances, shield angle/pokes, etc.) may be deferred **only after** core behavior is already extremely close and the remaining work is dominated by tiny float/ordering details.

Disallowed approximations (Initial Target / RL 1.0):
- Replacing GALE01 action ids with coarse categories in the observation.
- Omitting ledge interaction entirely on FD.
- Omitting shield, hitlag, or hitstun (policies strongly depend on these).

## Data Extraction Requirements

### Inputs

- `SSBM.iso` (or extracted `_iso/` directory).

### Outputs (generated ISO-derived artifacts)

- `data/**` is treated as generated and is gitignored by default.
  - Exception: `data/common/ft_common_data.json` is tracked (small, deterministic, and a canonical constants source).
- Prefer `.bin` artifacts for new extracted tables (exact bytes; avoids JSON typing issues). JSON is acceptable only when it is
  small and intentionally human-readable.

Extraction scripts must be deterministic and reproducible:
- Same ISO inputs → same extracted outputs (byte-for-byte) for a given extractor.
- If an extractor changes, regenerate the corresponding artifacts; avoid mixing gameplay changes and extraction changes in a single review step.

## Simulation Model

### Coordinate system and units

- Adopt the game’s coordinate system (right-handed/left-handed as extracted).
- Store positions/velocities in `float32` or `float64` (configurable):
  - Default `float32` for throughput.
  - Enable `float64` for debugging/comparison if needed.
- All geometry extracted is expressed in this same coordinate space.

### Per-frame update order (must be fixed)

One frame of `step()` must be ordered deterministically. The canonical implementation order lives in `src/step.c` (see also
`docs/DECOMP_PROC_ORDER.md` for decomp context). The list below is a conceptual breakdown of phases:

1. Sample inputs → compute per-player intents (buttons edges, stick direction, triggers).
2. Resolve global timers (match timer not needed, but per-entity timers are).
3. Apply state machine transitions that happen “at frame start” (e.g. exit hitlag).
4. Movement integration:
   - apply action-specific movement modifiers
   - apply gravity/traction/friction
   - integrate velocity → tentative position
5. Stage collision resolution:
   - resolve ground collision and grounded state
   - resolve ledge-grab attempts and ledge state
6. Update animation frame counters and bone transforms.
7. Spawn/update hitboxes/hurtboxes for the new animation frame.
8. Combat resolution:
   - detect overlaps
   - apply hitlag/hitstun/knockback/shield effects
   - spawn projectiles/hit effects as needed
9. Projectile integration and collision.
10. Apply end-of-frame transitions (landing state, IASA-like exits if modeled here).
11. Emit observations (or leave in buffers for `get_observations`).

If ordering differs from Melee internally, that is acceptable only if it improves stability and doesn’t degrade behavior; however ordering must be fixed and documented because it affects determinism.

## Architecture & Performance

### Core representation

- `MAX_PLAYERS = 4`.
- State stored as SoA arrays sized `[batch_size][MAX_PLAYERS]` for per-player fields.
- Entity pools for projectiles/items sized for worst-case per batch; free lists with no allocations.

### What should be SoA (hot path)

In practice, **everything that is read/written every frame** should be SoA (or AoSoA with a small lane width).

Minimum SoA sets:
- Per-player kinematics: `pos_x/y`, `vel_x/y`, `facing`, `on_ground`, `ground_normal`, `airborne_timer`
- Per-player high-frequency state: `action_id`, `action_frame`, `iasa_frame`, `flags`, `invuln_timer`
- Per-player combat: `percent`, `hitlag`, `hitstun`, `tumble`, `shield_hp`, `stocks`, `jumps_left`
- Input history: current + previous button bitmasks, stick values, trigger values (for edges/“pressed this frame”)

Combat geometry SoA (recommended):
- Active hitboxes per player: fixed `MAX_HITBOXES_PER_PLAYER`, SoA fields like `center_x/y`, `radius`, `damage`, `angle`, `kbg`, `bkb`, `hitlag_mult`, `shieldstun_mult`, `owner`, `enabled`
- Active hurtboxes per player: fixed `MAX_HURTBOXES_PER_PLAYER`, SoA fields like `center_x/y`, `radius`, `bone_id`, `enabled`, `state`

### Additional performance callouts

- **Fixed-capacity everything**: no `realloc`/growth paths; all pools pre-sized (players, projectiles, transient contacts, hit events).
- **Stable memory layout**:
  - align hot arrays to cache lines (e.g. 64B),
  - keep frequently co-accessed fields adjacent,
  - prefer compact integer types for timers/ids where possible (`u16/u8`) to reduce bandwidth.
- **Avoid per-entity indirection** in hot loops:
  - store extracted action/move tables in contiguous arrays with direct indexing (no hash maps),
  - precompute per-action pointers/offsets into frame data so stepping is O(1).
- **AoSoA lane option**: consider processing batch in fixed lanes (e.g. 8/16 envs) for SIMD-friendly loops while keeping determinism and simplicity.
- **Explicit “no hidden alloc” policy** in debug tooling (e.g., disable per-frame formatting/logging; accumulate counters in fixed buffers).

### Determinism guidelines

- No data-dependent iteration ordering (e.g. unordered maps) in the step path.
- Stable tie-breaking rules for collision/hit resolution.
- No parallel floating-point reductions that reorder operations unless explicitly deterministic.

### Extensibility to doubles

Design choices to keep “2→4 players” trivial:
- All loops iterate `p in 0..num_players`.
- Collision/combat works for all pairwise interactions `p != q` and supports team filtering.
- Observation emitter supports multiple viewpoints:
  - singles: 2 viewpoints (P1, P2)
  - doubles: 4 viewpoints (one per port) with a stable port mapping policy
- Config includes `is_teams` and team ids per player, even if singles uses trivial teams.

## Validation & Evaluation (no RL training runs)

### A) Replay-driven observation parity (teacher-forced / reseeded)

Goal: measure per-frame correctness/coverage without requiring long-horizon rollout parity (which is expected to diverge early in a “lite” sim).

Process:
1. Extract controller inputs per frame from Slippi replays (for Fox/Falco on FD).
2. For each frame `t`:
   - **Reseed** the simulator state from the replay-derived reference state at frame `t` (as close as possible to the real engine state representation we model).
   - Apply the replay’s recorded inputs for frame `t` and run exactly one `step()` to predict frame `t+1`.
   - Compare simulator outputs for `t+1` against the replay-derived reference at `t+1`.
3. Compute metrics and regressions over the suite from direct field comparisons.
4. Optionally, compute the `slippi-ai` embedded observation vectors for both sides as an additional “will the model break?”
   lens (not the primary correctness target).

Notes:
- This is “teacher-forced” evaluation: it answers “is our one-step transition function correct on the support of real gameplay states?”
- Reseeding must be deterministic and should avoid “cheating” by copying fields that the simulator is supposed to derive (e.g., if we track derived timers, we should seed only what is observable/authoritative for that frame).

Known teacher-forcing limitations (must be tracked and eventually removed, not treated as “engine truth”):
- Input-history tilt timers (`x670`/`x671`), TURN internals (`frames_to_turn`/`has_turned`), and KneeBend internals (`jump_input`/`is_short_hop`) are derived during preprocessing and are part of the seed schema.
- Animation rate (`frame_speed_mul_f32`) is not exposed by Slippi post-frames. We seed it via a strictly-causal derivation during preprocessing; the C core currently only models decomp-backed rate changes for Landing* actions (LandingAir* / LandingFallSpecial) plus the hitlag freeze gate.
- TURN seeding uses a causal derivation that does not look ahead to future facing flips; it includes a **deterministic assumption** that `ftCo_Turn_Anim_Inner` does **not** tick on the entry frame (matching the sim’s current update ordering: the Turn flip tick only runs if Turn was already active at frame start). Do **not** tune this assumption via one-step mismatch metrics; revisit it once richer entry-history seeding lands.
- FallSpecial mode `mv.co.fallspecial.xC` is not present in Slippi post-frame data. We currently seed it with a **best-effort inference** from the reseeded state (default `xC=1`; set `xC=0` when `fall_fast==0` and reseeded `speed_y_self < -terminal_vel`). This is decomp-motivated (see `ftCo_FallSpecial_Phys` branch structure), but still an inference; do **not** tune it against one-step metrics.
- Grabbed/thrown victim attachment offsets `grab_offset_{y,z}` (decomp analog: `fp->x1A70.{y,z}`) are reseed-inferred from the seeded snapshot as `(pos - anchor) / scale` (`src/grab_attachment.c:144`). Decomp updates `fp->x1A70` rollout-style from bones (`refs/melee/src/melee/ft/fighter.c::Fighter_UnkUpdateVecFromBones_8006876C`); this is acceptable for one-step eval but a known rollout gap.
- Grabbed/thrown victim anchor uses `FtPart_RHandN` as a suite-focused proxy (`src/grab_attachment.c:49`) because the runtime-reparented `FtPart_XRotN` joint used by decomp (`ftCo_Thrown.c::ftCo_800DE508`) cannot currently be represented from SSANIM01 v3 pose matrices for the relevant motion states.
- Grabbed/thrown victims currently still run through stage collision before attachment overrides position (`src/step.c:104`). Decomp’s thrown/capture Phys/Coll callbacks are empty (`ftCo_ThrownF_Phys/Coll` etc.); consider skipping stage collision for grabbed victims in rollout (and updating invariants like `on_ground` explicitly) instead of “collide then override pos”.

Metrics (initial):
- `action_id` match rate (and optional ±N frame window around transitions).
- Position error (x/y): mean, 95p, max.
- Boolean exactness: `on_ground`, `facing`, `is_dead` (and optionally derived flags like `hurtbox_state != 0`).
- Discrete exactness: `jumps_left`, `stocks_left` (or tolerate rare off-by-1 early).
- Event alignment: stock loss within ±N frames; hit events within ±N frames (if detectable from replay).
- Items/projectiles (optional early): presence + kinematics on a fixed-capacity set of slots (stable ordering by instance id).

Acceptance should be expressed as thresholds on these metrics over a fixed suite.

### A2) Short-horizon rollout windows (stretch goal)

After one-step metrics are strong, add a harder but still bounded test:
- Reseed at frame `t`, then roll out `K` steps (e.g. `K ∈ {5, 15, 60}`) using replay inputs, and compare against reference across the window.
- This catches multi-step timer drift and ordering issues without requiring full replay-length parity.

### B) Offline policy-consistency (teacher-forced, no rollout)

Goal: an existing `slippi-ai` policy reacts similarly to simulator observations as it does to real observations.

Process:
1. For a dataset of real frames, compute:
   - `obs_real[t]` from replay state
   - `obs_sim[t]` from simulator state produced by the reseeded one-step (or short-horizon window) procedure above
2. Feed both through the same frozen policy network.
3. Compare outputs:
   - KL divergence on action logits (or on per-control distributions)
   - Button probability L1/L2 diffs
   - Stick mean/variance diffs (if policy is continuous)

This provides a fast, automatable “will it break the model?” signal without long closed-loop runs.

### C) Short closed-loop smoke test (optional, not a proof)

Run policy in the simulator for ~5–20 seconds and assert:
- no NaNs / infinities
- no stuck states (e.g., action id frozen forever)
- action distribution not degenerate (e.g., always “do nothing”)

## RL 1.0 Roadmap

### RL 1.0 Definition (What “Good Enough for RL” Means)

RL 1.0 means this simulator can be used as a primary training environment for Fox/Falco on Final Destination (2 players)
without the game dynamics becoming obviously “not Melee” for competent policies.

### RL 1.0 Exit Criteria (Scorecard)

**Direction: lower is better** (0 is perfect).

This scorecard is the *hard* RL 1.0 gate. RL 1.0 is achieved when **all** criteria below hold for the canonical validation suite:

- `make test`
- `make validate OUT=reports/validation/one_step_suite_eval.txt`

(Do not “eyeball” parity. Use the one-step suite eval outputs as the objective signal.)

#### Discrete mismatches (teacher-forced, one-step)

All budgets are per replay **and** must hold in the `== suite summary ==` aggregate.

Primary gate:
- `overall.discrete_mismatch`: **< 100 per replay** (and `< 100 * num_replays` in suite summary).

Per-field budgets (report keys in `reports/validation/one_step_suite_eval.txt`):

| Report key | Budget (per replay) | Why it matters |
|---|---:|---|
| `mismatch.stocks` | 0 | Match flow invariant; mis-seeding here breaks everything. |
| `mismatch.is_dead` | 0 | Match flow invariant; must be exact. |
| `mismatch.action_id` | ≤ 15 | Core “what state are we in?”; large error breaks policy learning. |
| `mismatch.action_frame` | ≤ 20 | Timebase correctness; drives movescript/hurtbox/hitbox sampling. |
| `mismatch.animation_index` | ≤ 15 | Submotion identity; must agree with extracted msid tracks. |
| `mismatch.on_ground` | ≤ 10 | Ground/air branching; dominates locomotion/defense correctness. |
| `mismatch.ground_id` | ≤ 5 | “Which line” identity; required for stable land/ledge/tech behavior. |
| `mismatch.facing` | ≤ 10 | Drives mirrored geometry + move direction; impacts combat heavily. |
| `mismatch.jumps_left` | ≤ 5 | Jump gating; needed for RL plausibility and recovery correctness. |
| `mismatch.l_cancel` | ≤ 2 | High policy impact; timing-sensitive and should be near-exact. |
| `mismatch.hitlag` | ≤ 5 | Timing substrate; errors desync action_frame + cancel windows. |
| `mismatch.hitstun` | ≤ 10 | Controls control-lockout; errors desync DI/SDI later. |
| `mismatch.hurtbox_state` | ≤ 5 | Eligibility state affects whether hits should apply. |
| `mismatch.state_flags` | ≤ 10 | Output bytes must reflect sim-owned bits (hitlag/hitstun/shield active) and stable passthrough for the rest. |
| `mismatch.instance_id` | 0 | Identity must be deterministic (respawns/items must not “reshuffle ids”). |
| `mismatch.instance_hit_by` | ≤ 5 | Combat bookkeeping; improves attribution and follow-on fields. |
| `mismatch.last_hit_by` | ≤ 5 | Combat bookkeeping; used by combo logic and analysis. |
| `mismatch.last_attack_landed` | ≤ 5 | Combat bookkeeping; should follow hit identity/timing rules. |
| `mismatch.combo_count` | ≤ 5 | Combat bookkeeping; depends on hit attribution + hitstun timing. |
| `mismatch.item_exists` | 0 | Items are compared across **all 15 slots**; slot identity must be deterministic. |
| `mismatch.item_type` | 0 | Ditto: type must be stable under deterministic slot allocation. |
| `mismatch.item_state` | 0 | Ditto: item state machine must be deterministic and suite-correct. |
| `mismatch.item_owner` | 0 | Ditto: ownership attribution must match reference. |
| `mismatch.item_instance_id` | 0 | Ditto: instance ids must be deterministic and stable. |

Notes:
- `state_flags` is compared as 5 raw bytes per player-frame (see “`state_flags` Ownership” above).
- `item_*` fields are compared across all 15 global item slots per record (see `tools/eval/run_one_step_eval.py`); meeting the `< 100` total gate is not possible if slot identity is unstable.

#### Float errors (teacher-forced, one-step)

Primary gate:
- `overall.float_norm_mae_p95` (suite summary): **≤ 0.0030**
  - This is the mean absolute error normalized per-float-field by the 0.95-quantile of the reference magnitude (see `tools/eval/run_one_step_eval.py::_float_norm_mae_p95`).

Group budgets (computed with the same normalization rule, but restricted to the listed fields):

| Group | Fields (report keys `err.*`) | Budget (`float_norm_mae_p95`) |
|---|---|---:|
| Position / self-velocity | `pos_x`, `pos_y`, `speed_air_x_self`, `speed_ground_x_self`, `speed_y_self` | ≤ 0.0030 |
| Damage / KB surface | `percent`, `speed_x_attack`, `speed_y_attack` | ≤ 0.0030 |
| Items / projectiles | `item_pos_x`, `item_pos_y`, `item_vel_x`, `item_vel_y` | ≤ 0.0040 |
| Defense | `shield_hp` | ≤ 0.0025 |

Evaluator note:
- RL 1.0 requires printing the **group-restricted** float metrics (per replay + suite summary), using the exact same computation rule as
  `overall.float_norm_mae_p95` but restricting the keys included in the aggregation.
- Required additional output keys (names are part of the contract; add to `tools/eval/run_one_step_eval.py` output):
  - `overall.float_norm_mae_p95.group.position_self_velocity` over: `err.pos_x`, `err.pos_y`, `err.speed_air_x_self`,
    `err.speed_ground_x_self`, `err.speed_y_self`
  - `overall.float_norm_mae_p95.group.damage_kb_surface` over: `err.percent`, `err.speed_x_attack`, `err.speed_y_attack`
  - `overall.float_norm_mae_p95.group.items_projectiles` over: `err.item_pos_x`, `err.item_pos_y`, `err.item_vel_x`, `err.item_vel_y`
  - `overall.float_norm_mae_p95.group.defense` over: `err.shield_hp`
- Exact computation rule (must match `tools/eval/run_one_step_eval.py::_float_norm_mae_p95`):
  - For each included float field `k`, compute `scale_k = quantile_0.95(abs(ref_k))` over **all compared scalar values** for that field
    in the replay/suite scope; clamp `scale_k = max(scale_k, 1e-6)`.
  - Aggregate across fields by summing normalized absolute error and dividing by total scalar count:
    - `group_norm_sum = Σ_k (Σ_i abs(err_k[i]) / scale_k)`
    - `group_count = Σ_k count(err_k)`
    - `group_float_norm_mae_p95 = group_norm_sum / group_count` (0 if `group_count==0`)
  - The group metric is not a per-field average; it is a **count-weighted** aggregate across all included scalar entries.

Allowed exceptions (ideally empty):
- If an exception is added, it must state (a) why it cannot be solved from Slippi-only truth, and (b) what Dolphin engine-dump/probe would supply the missing internal.

#### Roadmap completeness audit (scorecard → owners → reduction slices)

Audit checklist (keep this section current as scorecard/suite evolve):
- Every scorecard key appears in the “Field-to-System Ownership Map” with a single primary owner (no orphan keys).
- Every scorecard key has at least one plausible reduction slice (parity project or vertical slice) that directly reduces it.
- If a key cannot plausibly be reduced from Slippi-visible truth + extracted tables (with causal preprocessing for history-dependent
  internals), it must be listed under “Allowed exceptions” with a concrete engine-dump/probe plan.

Scorecard keys → primary reduction slices (non-exhaustive; list at least one per key):

| Scorecard key | Primary owner (see ownership map) | Primary reduction slice(s) |
|---|---|---|
| `overall.discrete_mismatch` | Validation harness | Reduce via per-key fixes below (not a direct target itself). |
| `mismatch.stocks`, `mismatch.is_dead`, `mismatch.instance_id` | Match flow + deterministic allocation | Match flow slice (death/respawn/ids) (already DONE). |
| `mismatch.action_id`, `mismatch.action_frame`, `mismatch.animation_index` | Action/timebase | Action coverage + ordering contracts (Parity Project #5 + suite action coverage). |
| `mismatch.on_ground`, `mismatch.ground_id` | Stage collision/ECB | Parity Project #1 (mpColl-style ground contact). |
| `mismatch.l_cancel` | Locomotion/landing | mpColl parity + aerial landing state slice (timing). |
| `mismatch.facing`, `mismatch.jumps_left` | Locomotion/transitions | Locomotion vertical slice + ordering contracts (Parity Project #5). |
| `mismatch.hitlag`, `mismatch.hitstun` | Damage pipeline | Parity Project #3 (hitlists/eligibility) + Parity Project #4 (damage modifiers) + timer/ordering contracts. |
| `mismatch.hurtbox_state` | Combat geometry + damage pipeline | Eligibility gates (intangibility/invuln/throw rules) + correct action-frame driven hurtbox modes (Parity Project #3). |
| `mismatch.instance_hit_by`, `mismatch.last_hit_by`, `mismatch.last_attack_landed`, `mismatch.combo_count` | Hit identity/timing | Parity Project #3 (hitlists/rehit semantics) + attribution ordering. |
| `mismatch.state_flags` | Mixed | Expand sim-owned bits + implement remaining state/flag gates (often tied to Parity Projects #3–#5). |
| `mismatch.item_*` | Items/projectiles | Deterministic item slot identity + item collision/reflect ordering (items slice; interacts with Parity Project #3 and #4 for projectile hits). |
| `overall.float_norm_mae_p95` | Validation harness | Reduce via per-group slices below; must be tracked with group metrics. |
| `err.pos_*`, `err.speed_*_self` | Physics + collision | mpColl parity + physics ordering (Parity Project #1 + #5). |
| `err.percent`, `err.speed_*_attack` | Damage pipeline | Parity Project #3 (correct hits) + Parity Project #4 (stale/multipliers/armor) + KB decay correctness. |
| `err.shield_hp` | Defense | Shield system completeness (and correct hit classification from Parity Project #3). |
| `err.item_*` | Items/projectiles | Item spawn/physics/collision + reflect/absorb behavior (items slice + Parity Project #4). |

RL 1.0 acceptance criteria

- Deterministic: identical input streams produce identical outputs.
- Performance: no allocations after init; batched/SoA hot path; stable step cost.
- 4p-ready: all logic works for num_players ∈ {2,4} without rewriting systems; only config limits differ.
- Data-driven: core physics/constants and all stage/character/movescript-derived geometry/tables come from ISO-derived artifacts.
- Validation:
  - One-step suite eval remains the primary gate (teacher-forced).
  - RL 1.0 does not require full rollout parity, but must avoid obvious “explosions” (e.g., percent/KB/hitstun diverging wildly in normal exchanges).

### Workflow Rule (Core for RL 1.0)

Reach RL 1.0 by completing coherent vertical slices (match flow → locomotion → defense → combat/damage → specials/grabs → ledge/tech),
rather than enabling one-off mutations that depend on missing upstream bookkeeping.

### Roadmap Status (as of 2026-02-01)

This section is the “what is actually done” source of truth for agents. If a deeper section below contradicts this, update the deeper
section or regenerate it; do not rely on stale tables.

Recent deltas to reflect here (do not let these get “lost in chat logs”):
- Ledge-grab mask ordering is now collision-stage prev/cur snapshot based (captured around `stage_collision_apply()` and consumed
  post-collision); regression locked for TreasuredBackKangaroo records 1806/1807 (`tests/test_ledge_grab_treasuredbackkangaroo_regression.py`).
- Build now forces C extension rebuild to avoid stale `.so` issues (Makefile change).
- ECB tz/ty axis contract is locked for RL 1.0; any axis remap is a separate audit project (see `docs/SSANIM_AXIS_BASIS.md`).
- Grab/throw release is now implemented (data-driven from `data/moves/*.json`): release/detach + throw-hit apply are wired through
  `src/throw_flow.c` and `combat_apply_throw_hit()`, with throw-release regressions covered by integration tests.
- Guard release no longer transitions to GuardOff early: `mv.co.guard.xC/x10` release lockout and `fp->lightshield_amount` latch are now
  explicit seeded internals derived causally in preprocessing (see `tools/slippi/seed_history.py`).
- Laser “phantom hits” (sim false positives) were reduced by aligning spawn transform + hurtcaps scaling/facing rotation with decomp and
  fixing fighter-driven spawn ordering (pre-physics).

Legend:
- **DONE**: implemented and suite-stable; remaining gaps are low-impact edge cases.
- **PARTIAL**: implemented enough to reduce suite mismatches, but known missing internals/ordering/gates remain.
- **TODO**: mostly seed carry-through; not yet implemented in the C core.

| Area | Status | Notes / remaining blockers |
|---|---|---|
| Validation harness (one-step eval, seeded internals, prefix-invariance tests) | **DONE** | Primary correctness gate for RL 1.0. |
| Perf architecture (SoA, no hot-path allocs, deterministic ordering) | **DONE** | Keep it “allocation-free after init”. |
| Match flow (Entry/Death/Rebirth/Stocks/Blastzones) | **DONE** | Suite-level `mismatch.stocks` and `mismatch.is_dead` brought to 0. |
| Anim/script timebase (`cur_anim_frame`, `frame_speed_mul`, hitlag freeze) | **DONE** | Q16.16 accumulator + seeded `frame_speed_mul_f32`. |
| Input pipeline + UCF (legalization, pad buffer, key timers/counters) | **PARTIAL** | Core UCF-on suite behavior is covered; more input-history gates remain as needed. |
| Locomotion core (walk/dash/run/jumps/landing/fall/airdodge/escapes) | **PARTIAL** | Broad coverage exists; gaps remain in less-common action branches and ordering edge cases. |
| Stage collision + ECB (FD ground/ledge points, grounding, `ground_id`) | **PARTIAL** | mpColl-shaped FD ground contact is implemented (floor + wall/ceiling passes + persistence); remaining gaps are mpColl substeps/platform lines and residual mismatch clusters. |
| Ledge system (Cliff* actions, quick options) | **PARTIAL** | Ledge-grab mask now uses collision-stage prev/cur snapshots; remaining parity needs occupancy/refresh/cooldowns + mpColl substrate. |
| Knockdown/tech (DownBound/Wait/Stand/Attack + rolls) | **PARTIAL** | Now suite-positive; remaining parity depends on deeper ground-contact semantics. |
| Combat geometry (hurtcaps/hitboxes/shields pose-driven) | **PARTIAL** | Core data-driven primitives exist; remaining parity depends on exact facing/axis + attachment nuances. |
| Damage pipeline (BODY + SHIELD, GuardSetOff, hitlag/hitstun/KB states) | **PARTIAL** | Big pieces are in; still missing full rehit/hitlist, stale queue, and many modifiers. |
| Items/projectiles | **PARTIAL** | Laser/blaster coverage is in and reduces `item_*` mismatches; item system parity is incomplete beyond suite needs. |
| Grabs/throws | **PARTIAL** | Attachment substrate exists and throw release/detach + throw-hit apply are implemented (data-driven). Remaining gaps: capture point selection/coverage, pummel/breakout rules, and suite-needed action coverage beyond release frames. |

### Field-to-System Ownership Map (Validation Outputs)

The one-step eval emits a fixed set of output keys (see `reports/validation/one_step_suite_eval.txt`). Every key must have an owner:
if it mismatches, file/fix it under the owning system/parity project (do not “patch symptoms” in unrelated modules).

| Report key (as printed) | Owning system / parity project | Notes / typical blockers |
|---|---|---|
| `overall.discrete_mismatch` | Validation harness | Aggregate; should be interpreted via the per-field rows below. |
| `overall.float_norm_mae_p95` | Validation harness | Aggregate; see per-group budgets in the RL 1.0 scorecard. |
| `mismatch.stocks` | Match flow | Must be exact (stocks/KO/death/respawn). |
| `mismatch.is_dead` | Match flow | Must be exact (death flags / blastzone checks). |
| `mismatch.instance_id` | Match flow + deterministic allocation | Must be deterministic across respawns; also impacts “who hit whom” bookkeeping. |
| `mismatch.action_id` | Action/state machine (vertical slices) | “What state are we in?”; blocked by missing upstream substrates (mpColl, hitlists) if a state depends on them. |
| `mismatch.action_frame` | Anim/script timebase | Must match Q16.16 accumulator semantics + hitlag freeze. |
| `mismatch.animation_index` | Anim/submotion identity + extracted anim tracks | Depends on correct msid selection and timebase; often exposed by specials/guard submotions. |
| `mismatch.facing` | Locomotion + action transitions | Usually a state-machine ordering issue (turn/turnrun/escape/roll flips) rather than physics. |
| `mismatch.jumps_left` | Locomotion (jump gating) | Depends on correct ground/air transitions + jump consumption rules. |
| `mismatch.on_ground` | Stage collision/ECB (mpColl parity project) | Requires correct prev/current ECB usage and stable floor selection. |
| `mismatch.ground_id` | Stage collision/ECB (mpColl parity project) | “Which line” identity; unstable without mpColl-style tie-break + persistence rules. |
| `mismatch.l_cancel` | Locomotion + aerial attack landing | Needs correct landing frame detection (mpColl), and correct action_frame timing. |
| `mismatch.hitlag` | Damage pipeline | Depends on hit resolution + hitlag timers + timebase freeze coupling. |
| `mismatch.hitstun` | Damage pipeline | Depends on hit resolution + KB/percent modifiers + hitlists/rehit timing. |
| `mismatch.hurtbox_state` | Combat geometry + damage pipeline | State/mode gates (intangible/invuln/throw hurtbox changes). |
| `mismatch.instance_hit_by` | Hit identity/timing (hitlists parity project) | Needs per-hitbox/per-target hitlists + deterministic tie-breaks. |
| `mismatch.last_hit_by` | Hit identity/timing (hitlists parity project) | Attribution must follow decomp ordering and hit resolution rules. |
| `mismatch.last_attack_landed` | Hit identity/timing (hitlists parity project) | Driven by hit identity + whether hit applied BODY/SHIELD. |
| `mismatch.combo_count` | Hit identity/timing (hitlists parity project) | Depends on hit attribution + hitstun timing coherence. |
| `mismatch.state_flags` | Mixed: defense + combat + timers | See “`state_flags` Ownership (seed vs derived)”; sim-owned bytes must be kept consistent. |
| `mismatch.item_exists` | Items/projectiles | Compared across 15 slots; requires deterministic slot identity + lifecycle. |
| `mismatch.item_type` | Items/projectiles | Deterministic type assignment + spawn plumbing (blaster/laser). |
| `mismatch.item_state` | Items/projectiles | Item state machine parity (at least for suite-needed items). |
| `mismatch.item_owner` | Items/projectiles | Owner attribution (fighter ↔ item hooks). |
| `mismatch.item_instance_id` | Items/projectiles + deterministic allocation | Stable ids; typically breaks when allocation/free ordering differs. |
| `err.pos_x` | Physics + stage collision | Often blocked by mpColl floor selection/resolution order. |
| `err.pos_y` | Physics + stage collision | Ditto; landing frame handling is the usual culprit. |
| `err.speed_air_x_self` | Physics integration | Drift/air accel + timebase ordering; should not be “tuned” off suite artifacts. |
| `err.speed_ground_x_self` | Physics + ground contact substrate | Traction/friction depends on stable `on_ground` + correct floor-relative speed. |
| `err.speed_y_self` | Physics + stage collision | Gravity/fastfall/landing frame semantics. |
| `err.speed_x_attack` | Damage pipeline (KB) | KB velocity + decay; blocked by missing damage modifiers and hitlists timing. |
| `err.speed_y_attack` | Damage pipeline (KB) | Same as `speed_x_attack`. |
| `err.percent` | Damage pipeline (damage modifiers parity project) | Percent drift is dominated by missing stale queue/modifiers once hit identity is correct. |
| `err.shield_hp` | Defense (shield) | HP drain/regen + hit depletion; blocked by missing powershield/angle subrules only if suite exercises them. |
| `err.item_pos_x` | Items/projectiles | Projectile integration + spawn offsets + collision. |
| `err.item_pos_y` | Items/projectiles | Ditto. |
| `err.item_vel_x` | Items/projectiles | Ditto. |
| `err.item_vel_y` | Items/projectiles | Ditto. |

### Ordering Rule: “Parity Projects” (finish-once, cross-cutting layers)

To avoid churn, we treat some work as **parity projects**: once done, they become the stable substrate that many systems build on.
Prefer completing these projects in order rather than “patching symptoms” in higher-level states.

1) **mpColl-style ground contact state machine** (**PARTIAL**, FD implemented)
   - Goal: stable, decomp-shaped `on_ground` + `ground_id` + “which line” selection with correct prev/current ECB usage.
   - Why now: it unblocks landing/knockdown/ledge correctness without per-state hacks.

   **Decomp entrypoints (read first; file::function)**
   - `refs/melee/src/melee/mp/mpcoll.c::mpCollPrev` (prev/current bookkeeping and sanity checks)
   - `refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB` + `mpCollInterpolateECB` (ECB/desired ECB update)
   - `refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A908_Floor` (floor contact stability; prev_ecb.bottom vs ecb.bottom)
   - `refs/melee/src/melee/mp/mpcoll.c::mpColl_80044838_Floor` + `mpColl_80044948_Floor` (floor query variants / ignore-bottom gates)
   - `refs/melee/src/melee/mp/mpcoll.c::mpColl_80044164` + `mpColl_800443C4` (ledge detection helpers)
   - `refs/melee/src/melee/lb/types.h::CollData` (what state exists and what persists across frames)
   - `refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate` + `Fighter_procMap` (where collision fits in proc ordering)
   - `refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007DD7C` (example of floor.index persistence/graph traversal)

   **Inputs/outputs contract (what this system owns)**
   - Inputs (per fighter, per frame):
     - `cur_pos` (root translation after integration), and the prior-frame position used for “crossing” tests.
     - ECB geometry: `ecb` and `prev_ecb` (pose-driven), plus any “desired_ecb” interpolation semantics if needed.
     - Stage floor segments (FD) + line connectivity/metadata (e.g., endpoint flags, ledge tags).
     - Any “skip” gates (e.g., `floor_skip`) that intentionally disable floor collision for a window.
   - Outputs (owned, written by collision):
     - `on_ground` (binary contact state for state-machine branching).
     - `ground_id` / line identity: stable id for the contacted floor line (FD segment id first; later general line ids).
     - Contact metadata needed downstream: floor normal, contact point, “env flags”/ledge-grab masks (even if only partially used on FD today).
   - Must **not** own:
     - Action transitions (e.g., entering Landing/CliffCatch) — collision provides facts; the action/state machine consumes them.
     - Timebase (`action_frame`) — collision must be consistent with it, but does not decide it.

   **Required internal state (and whether it must be seeded)**
   - Must persist across sim frames (derivable during rollout; may need explicit reseed fields if missing from Slippi):
     - `prev_pos`/`last_pos` (collision “crossing” frame depends on prior translation; see `CollData.prev_pos`/`last_pos`).
     - `prev_ecb` and current `ecb` (pose-driven; `prev_ecb` is a one-frame lag).
     - Current floor contact (`floor.index` / `ground_id`) to support contact stability and line-graph traversal across frames.
     - `env_flags` + `prev_env_flags` (collision environment flags used by ledge/landing/tech gates).
     - Skip/override gates: `floor_skip`, `joint_id_skip`, `joint_id_only`, ledge snap params.
   - Reseed rule: any state above that cannot be deterministically reconstructed from the replay seed for frame `t` must be promoted into the explicit seed schema (don’t add hidden latches).

   **Step ordering contract (where it runs)**
   - Collision runs on the **post-integration** translation (after `phys_cb` mutates position/vel), and before any action transitions that branch on `on_ground`/`ground_id` for frame `t+1`.
   - Landing/ledge/tech logic must consult collision outputs from this step; do not special-case individual actions to “fix grounding”.

   **Replaces these former approximations**
   - Legacy FD-only “stickiness/endpoint bias” grounding heuristics and non-decomp snapping rules (now owned by the mpColl-shaped passes:
     `src/mpcoll_ground.c`, `src/mpcoll_wall_ceil.c`, and their persistence tests under `tests/`).
   - Any per-action “force airborne” policies added solely to compensate for unstable floor selection (should be deleted when found).
   - Downstream hacks that depend on missing collision-env semantics (still being reduced under Parity Project #2 and the ledge risk register):
     - “outside-only grab” ledge catch gate (`src/ledge.c:376`).
     - CliffWait climb/drop “previous-stick neutral reset” latch (`src/ledge.c:190`).

   **DONE when**
   - `mismatch.on_ground` and `mismatch.ground_id` become scorecard-compliant **without** any per-action grounding special cases.
   - The collision module exposes a stable, minimal interface that higher-level systems call (landing, ledge, knockdown/tech), and those systems stop owning ad hoc collision latches.

2) **Collision-env flags + ledge grab mask parity** (**PARTIAL**)
   - Goal: decomp-shaped `Collide_LedgeGrabMask`-equivalent so ledge catch can be scheduled post-collision without regressions.
   - Ordering/inputs (lock-in):
     - Ledge-grab mask generation must use the collision-stage “prev/cur” snapshots captured around the stage-collision pass (not generic frame-to-frame `prev_pos`).
     - Schedule it **post-collision** (after `stage_collision_apply`) so CliffCatch decisions consume the collision outputs for this frame.
     - Decomp anchors: `refs/melee/src/melee/mp/mpcoll.c::mpColl_80043754`, `mpColl_80046904`, `mpColl_800443C4`.
     - Why it matters: prevents a 1-frame-early `RightLedgeGrab` in TreasuredBackKangaroo records 1806/1807 under one-step eval.
   - Depends on: (1).

3) **Hit/hurt eligibility + rehit semantics (hitlists/timers)** (**PARTIAL**)
   - Goal: per-hitbox/per-target hitlists + cooldowns (replace conservative pair latch), consistent for fighters and items.
   - Depends on: (1) for stable contact/landing frames; uses move/hit status artifacts already present.

   **Decomp entrypoints (read first; file::function)**
   - Hit capsule victim list representation + core ops:
     - `refs/melee/src/melee/lb/types.h::HitCapsule` (two victim lists, insertion indices, per-entry countdown)
     - `refs/melee/src/melee/lb/lbcollision.c::lbColl_80008440` (clear both victim lists)
     - `refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688` (victims_1 “seen?” + insert + optional countdown set)
     - `refs/melee/src/melee/lb/lbcollision.c::lbColl_80008820` (victims_2 “seen?” + insert + optional countdown set)
     - `refs/melee/src/melee/lb/lbcollision.c::lbColl_80008A5C` (per-frame countdown decrement + expiry clear)
   - Fighter-vs-fighter collision + where the hitlists are consumed:
     - `refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70` (fighter-vs-fighter collision pass; adds victims during hit acceptance)
     - `refs/melee/src/melee/ft/ftcoll.c::ftColl_80076808` (share hitlist updates across hitboxes with the same `HitCapsule.x4`)
     - `refs/melee/src/melee/ft/ftcoll.c::ftColl_800768A0` (copy hit capsule victim lists across same-`x4` hitboxes, else clear)
     - `refs/melee/src/melee/ft/fighter.c::Fighter_8006CB94` (proc that calls `ftColl_80078C70`; priority 13)
     - `refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC` (post-collision consumer; priority 14)
     - `docs/DECOMP_PROC_ORDER.md` (priority schedule; where collision/consume sits relative to hitlag/anim/phys)
   - Item/projectile collision (must follow the same hitlist semantics):
     - `refs/melee/src/melee/it/itcoll.c::it_8026FA2C` / `it_8026FAC4` (item vs item; updates victims_1)
     - `refs/melee/src/melee/it/itcoll.c::it_8026FC00` (item victim list update for “phantom/tip log” style list; uses victims_2)
     - `refs/melee/src/melee/it/itcoll.c` (per-frame decrement call site for items: `lbColl_80008A5C` on each active item hitbox)

   **Inputs/outputs contract (what this system owns)**
   - Owns (must compute/update deterministically, allocation-free):
     - Per-hit-capsule victim bookkeeping that answers: “May this hit capsule affect this target on this frame?”
     - Per-entry cooldown countdown behavior (rehit-rate semantics), including deterministic expiry and deterministic eviction when full.
     - Hit capsule sharing semantics: multiple hitboxes with the same “group id” (`HitCapsule.x4`) must share the same victim lists.
     - A stable “hit identity token” for downstream consumers (damage/stale/combo attribution), containing enough info to attribute
       the accepted hit consistently.
   - Does **not** own:
     - Geometry overlap detection (hitbox↔hurtbox/shield intersection math).
     - Damage/KB/hitlag/hitstun numeric calculation (that’s the damage pipeline).
     - State machine transitions (Damage states, GuardSetOff, etc.).
     - Item allocation/lifecycle (item pool identity is its own system).

   **Required internal state (and what defines hit identity)**
   - Per hit capsule (fighter hitboxes and item/projectile hitboxes) that can persist across frames:
     - Two victim lists matching the decomp shape:
       - `victims_1[]`: “main victim list” entries `(victim_entity_id, cooldown_frames_remaining)`
       - `victims_2[]`: a second list used by some collision subpaths (e.g. “phantom/tip log” patterns)
     - Deterministic insertion/eviction policy when full:
       - Decomp uses ring indices (`HitCapsule.x44` / `HitCapsule.x45`) as overwrite pointers.
     - Per-capsule `rehit_rate_frames` (decomp: `HitCapsule.x40_b4`) sourced from extracted hitbox data (`data/hitboxes/*.bin`).
   - Hit identity keys (minimum; used for both suppression + attribution):
     - `source_kind` (fighter vs item/projectile)
     - `attacker_instance_id` (stable across respawns per seed; see `mismatch.instance_id` ownership)
     - `defender_instance_id`
     - `hit_group_id` (decomp: `HitCapsule.x4`; the “shared hitlist id” across hitboxes)
     - `hitbox_id` (0..N within a group) **only** if needed for tie-breaking when multiple hitboxes in the same group overlap
     - `element` / collision class only if the decomp uses it to choose which victim list to consult/update
   - Teacher-forced reseed requirement:
     - Because one-step eval reseeds at frame `t`, any hitlist state that influences whether frame `t` can apply a hit must be in the
       explicit seed schema (no hidden “rollout-only” state).
     - Seed values must be causally reconstructible from replay history + extracted tables (see “Seed state philosophy” above).

   **Step ordering contract (hitlists vs hitlag vs collision)**
   - Decomp-shaped ordering anchor (see `docs/DECOMP_PROC_ORDER.md`):
     - Hit capsule world endpoints are refreshed before fighter-vs-fighter collision (priority 9 before 13).
     - Fighter-vs-fighter collision runs at priority 13 (`ftColl_80078C70`).
     - Post-collision consumption runs at priority 14 (`Fighter_ProcessHit_8006D1EC`).
   - Hitlist countdown decrement:
     - Must happen exactly once per sim frame per active hit capsule, and must be deterministic.
     - Reference behavior: `lbColl_80008A5C` decrements per-entry countdowns and clears entries when the countdown reaches 0.
     - **Open ordering question (must be resolved decomp-first or via probe):** whether this decrement is effectively “frame-start” or
       “frame-end” relative to collision acceptance, and whether it is skipped/frozen during hitlag for fighters. (Items explicitly
       call `lbColl_80008A5C` in `itcoll.c`.)
   - Hitlist insertion/update:
     - Happens on hit acceptance during the collision pass (fighters: `ftColl_*` paths calling `lbColl_80008688`/`_80008820`;
       items: `it_8026FA2C`/`it_8026FC00`).
   - Clear/refresh rules:
     - A “full clear hitboxes” operation must clear the relevant hitlists (decomp: `lbColl_80008440` on the hit capsule).
     - When multiple hitboxes share a group id (`HitCapsule.x4`), their hitlists must be kept in sync:
       - copy-from-sibling if present (`ftColl_800768A0`), else clear (`lbColl_80008440`).

   **Groundwork landed (PARTIAL)**
   - Replaced the old conservative per-(attacker, defender) latch (`combat_rehit_*`) with a decomp-shaped hitlist map + victim instance
     key + clear-on-enable + per-frame decrement.
   - This does **not** mean PP#3 is “done”: on the current suite, the target mismatch fields did not move yet, and the remaining work is
     primarily about *rehit-rate timers and ordering*.
   - Current conclusion (asm-first): GALE01 fighter-side code does not appear to write `HitCapsule.x40_b4` (the 8-bit rehit countdown
     field). Rehit behavior for fighters appears driven by hitbox enable/disable + `hit_group` (`HitCapsule.x4`) sharing/clears, while
     items/projectiles do write `x40_b4` in at least some paths.

   **DONE when (tie directly to RL 1.0 scorecard keys)**
   - `mismatch.instance_hit_by`, `mismatch.last_hit_by`, `mismatch.last_attack_landed`, `mismatch.combo_count` are scorecard-compliant
     without relying on conservative suppression (and without introducing new allocation/ordering hacks).
   - `mismatch.hitlag` and `mismatch.hitstun` mismatches materially reduce in cases where the current sim mis-applies or suppresses hits
     due to missing hitlists/eligibility bookkeeping (multi-frame overlaps, same-move repeats, item hits).
   - Rehit behavior is consistent for fighter hits and item/projectile hits (no “fighter-only” correctness).

   **Ambiguities to resolve as part of this work (not separate tasks)**
   - Exact timing of countdown decrement (`lbColl_80008A5C`) relative to collision acceptance and post-hit consume:
     - Does it run at frame start or frame end for fighters?
     - Does it freeze during hitlag (or is it gated by proc ordering/hitlag flags)?
   - Semantic meaning of the `lbColl_80008688` / `lbColl_80008820` “type” codes:
     - Which collision outcomes update which victim list and when countdowns are set/refreshed.
   - Whether fighters inline any hitlist rules (vs calling `lbColl_*` directly), and if so, what the exact equivalence is.

4) **Damage modifiers parity (stale queue + multipliers + armor/no-damage gates)** (**TODO**)
   - Goal: make percent/hitlag/hitstun/KB numerically meaningful; stop “percent drift” being dominated by missing modifiers.
   - Depends on: (3) for correct hit identity + timing.

   **Decomp entrypoints (read first; file::function)**
   - Stale queue + staling multiplier:
     - `refs/melee/src/melee/ft/ft_0881.c::ft_800890D0` (assign `attackID` + `attack_instance`)
     - `refs/melee/src/melee/ft/ft_0881.c::ft_80089118` (compute staling multiplier from the stale table)
     - `refs/melee/src/melee/ft/ft_0881.c::ft_80089228` (apply staling multiplier to damage)
     - `refs/melee/src/melee/ft/fighter.c::Fighter_800679B0` / `refs/melee/src/melee/ft/fighter.c` (`Fighter_804D6548` load; the
       per-recency staling decrement table used by `ft_80089118`)
     - `refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromFighter` (enqueue `(move_id, attack_instance)` on hit)
     - `refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromItem` (same, but source is an item)
     - `refs/melee/src/melee/pl/plstale.c::plStale_IncrementAttackInstance` (global `u16` instance counter; wraps, skips 0)
     - `refs/melee/src/melee/ft/ftcoll.c::ftColl_8007BE3C` (post-hit consumer; calls stale update for fighter vs item sources)
     - `refs/melee/src/melee/pl/types.h::StaleMoveTable` (10-entry ring buffer; `current_index` + `(move_id, instance)` pairs)
   - Reflect/absorb modifiers (suite-relevant if lasers are reflected/absorbed):
     - `refs/melee/src/melee/ft/ftcoll.c::ftColl_CreateReflectHit` (reflect bubble setup; attrs include damage/speed multipliers)
     - `refs/melee/src/melee/ft/ftcoll.c` (reflect hit handling around `ReflectAttr.*` and item reflect fields like `item->xC6C/xC70`)
     - `refs/melee/src/melee/it/itcoll.c` (absorb/reflect overlap logic and item collision subpaths)
   - Damage gating / armor/no-damage:
     - `refs/melee/src/melee/ft/ftcoll.c` (damage application branches that consult “no-damage/armor” flags, e.g. `x221C_b4`)

   **Required internal state (allocation-free, deterministic)**
   - Staling state (per player, per environment):
     - `staleAttackInstance` (global counter per match/env; decomp: `u16`, wraps, skips 0)
     - `StaleMoveTable` for each player:
       - `current_index` (ring pointer)
       - `StaleMoves[10]` entries: `(move_id, attack_instance)`
   - Source identity needed to update staling correctly:
     - Fighters: `attack_id` (`fp->x2068_attackID`) and `attack_instance` (`fp->x206C_attack_instance`)
     - Items/projectiles: `attack_id` (`it->xD88_attackID`) and `attack_instance` (`it->xD8C_attack_instance`)
   - Per-player/per-entity damage multipliers that influence suite-visible fields:
     - Staling multiplier (from stale table).
     - Reflect/absorb multipliers on projectiles (damage and speed multipliers).
     - Any remaining per-victim or per-attacker multipliers that affect percent/KB/hitlag/hitstun (must be decomp-sourced).
   - No-damage/armor gating internals:
     - A representation of “damage is negated/absorbed” vs “damage is applied but reduced”, plus any remaining “armor HP” style value
       if applicable (decomp shows a subtract-then-apply pattern on some flags in `ftcoll.c`).

   **Seeding rule (teacher-forced reseed: what is causal vs explicit vs engine-dump)**
   - Causally reconstructible from Slippi + extracted tables (preferred; implement in suite preprocessing):
     - The stale queue contents for each player at frame `t` can be reconstructed by replaying *only the stale update rules* over the
       replay history up to `t` (hit-confirmation events drive `plStale_Update*`), using extracted move/hitbox tables to map
       `(action_id, animation_index, action_frame)` to the appropriate `move_id` / `attack_id` used for staling.
     - The global `staleAttackInstance` counter can be reconstructed by mirroring the same “attack instance increment” rules over time
       (decomp: `plStale_IncrementAttackInstance` + `ft_800890D0` and the item equivalents).
   - Must be explicit seed internals (cannot be derived from just frame-`t` post-state alone):
     - `staleAttackInstance`
     - `stale_moves[player].current_index` and `stale_moves[player].StaleMoves[10]`
     - If needed for correctness: the source’s current `attack_id`/`attack_instance` for fighters and suite-relevant items.
   - Engine-dump required (allowed only if proven unavoidable):
     - If a suite contains hits where the `move_id` cannot be disambiguated from replay-visible state + extracted tables (e.g. multiple
       concurrent hitboxes with different `move_id` values whose identities are not inferable), that specific ambiguity must be listed
       under “Allowed exceptions” with a proposed probe signal.

   **Step ordering contract (modifiers relative to hit resolution)**
   - Apply damage modifiers before any downstream quantities derived from “damage dealt”:
     - damage → percent update
     - damage → hitlag frames
     - damage → hitstun frames
     - damage/percent → knockback velocity (and the resulting `err.speed_{x,y}_attack`)
   - Stale queue update happens after the hit is applied (decomp: `ftColl_8007BE3C` in the post-hit consumer path):
     - The current hit’s staling multiplier must be computed from the pre-hit stale table, then the stale table is updated to include
       this hit’s `(move_id, attack_instance)` entry.
   - No-damage/armor gates must short-circuit downstream effects:
     - If the hit is gated to “no damage”, it must not change percent, should not enqueue staling, and should apply only the decomp-backed
       non-damage side effects (e.g. hitlag/shieldstun only if the decomp does so).
   - Reflect/absorb modifiers apply at the moment the projectile interacts with the reflect/absorb bubble:
     - projectile ownership/velocity changes must occur deterministically before the next frame’s projectile integration so item kinematics
       (`err.item_vel_*`) remain stable.

   **Replaces these current approximations (delete once this lands)**
   - “No staling / no stale queue” behavior that makes `err.percent` drift dominated by missing modifiers rather than core hit identity.
   - Any stopgap “percent tuning” or suite-only heuristics (explicitly disallowed by the repo rules).

   **DONE when (tie directly to RL 1.0 scorecard keys)**
   - Float group budgets become achievable without tuning:
     - `err.percent` meets its float-group budget (Damage / KB surface group).
     - `err.speed_x_attack` and `err.speed_y_attack` meet their float-group budgets (Damage / KB surface group).
   - Discrete mismatch budgets materially improve where they depend on modifiers:
     - `mismatch.hitlag` and `mismatch.hitstun` reduce for modifier-sensitive hits (staling/reflect/armor cases).
   - Staling parity holds for both fighter hits and item/projectile hits (if suite exercises projectile hits).

   **Ambiguities to resolve as part of this work (not separate tasks)**
   - Staling reconstruction edge cases:
     - If multiple concurrent hit sources could make `move_id` attribution ambiguous from Slippi-visible state + extracted tables,
       identify the minimal additional signal (explicit seed internal or engine dump probe) and document it under “Allowed exceptions”.
   - Armor/no-damage gate semantics:
     - Which flags/timers must be explicit seed vs causally reconstructible.
     - Whether “no-damage” cases enqueue staling or not in each decomp path.
   - Reflect/absorb ordering details that affect suite-visible `item_*` kinematics/ownership:
     - Ensure ownership and velocity multipliers apply at the decomp-correct moment so item integration remains stable.

5) **Action ordering contracts (Anim/IASA/Phys/Coll) per bucket** (**PARTIAL**)
   - Goal: codify and enforce consistent per-frame ordering for groups of states (not per-state ad hoc).
   - Depends on: (1) for Coll semantics; also interacts with anim timebase (already DONE).

### Planning Gate: “Suite Coverage List”

Before claiming RL 1.0, maintain a current list of:

- All unique `action_id` values present in the suite (seed_t and ref_t1).
- All unique `animation_index` (submotion) values present in the suite.
- A mapping of those ids → names (from GALE01 `forward.h`) → implementation status in this repo.

(There is already tooling to list suite action_ids: `tools/eval/list_suite_action_ids.py`.)

Coverage & initialization targets:
- Action coverage: include every GALE01 action state that appears in the Fox/Falco FD validation suite (and keep this list current as the suite evolves).
- Start state: match the real match start state as closely as possible so we can seed a recurrent policy’s hidden state by replaying the exact match-start prefix it expects.

### 1) Suite Coverage List (current suite: `replays/suites/fox_falco_fd_ucf084_recent.json`)

This section is **suite-grounded**: it is intended to be regenerated any time the suite changes, and it should directly drive what we implement next.
If the status labels in the generated tables conflict with the “Roadmap Status” section above, assume the tables are stale and regenerate/update them.

How to regenerate (no extraction / no preprocess):
- Action ids: `uv run python -m tools.eval.list_suite_action_ids --suite replays/suites/fox_falco_fd_ucf084_recent.json --datasets-dir datasets`
- Animation indices (until a dedicated tool exists):
  - Add a `tools/eval/list_suite_animation_indexes.py` sibling to `list_suite_action_ids.py` that unions `seed_t.animation_index` + `ref_t1.animation_index`.
  - The implementation should mirror `tools/eval/list_suite_action_ids.py` exactly (same suite loader + dataset path mapping), just with `u32` counting.

Terminology / identity (Slippi ↔ GALE01 ↔ this repo):
- `action_id`: GALE01 `FtMotionId` / `ftCommon_MotionState` (plus character-specific motions starting at `ftCo_MS_Count`).
  - Names are taken from:
    - `refs/melee/src/melee/ft/chara/ftCommon/forward.h` (`ftCommon_MotionState`)
    - `refs/melee/src/melee/ft/chara/ftFox/forward.h` (Fox/Falco clone specials; see `ftFox_MotionState`)
- `animation_index`: Slippi post-frame `animation_index` (a submotion id / “msid”).
  - Names are taken from:
    - `refs/melee/src/melee/ft/chara/ftCommon/forward.h` (`ftCo_Submotion`)
    - `refs/melee/src/melee/ft/chara/ftFox/forward.h` (Fox/Falco clone specials; see `ftFx_Submotion`)
  - `0xFFFFFFFF` means “no submotion” (observed frequently in shield/guard-related states).

Status column meaning (used below):
- `implemented`: the C core actively simulates this state today (it can be entered/updated/exited without relying on reseed to “carry it”).
- `partial`: the C core has some targeted support (timers, drift/landing/IASA, damage entry, etc.) but does not fully model the state machine.
- `missing`: the state is currently treated as “carry-through” from seed, without dedicated action logic (high mismatch risk when the state would normally change due to inputs/environment).

#### Unique `action_id` set (seed_t + ref_t1)

Status note:
- The `status` column below is best-effort and can become stale as systems land quickly. The authoritative high-level status is
  “Roadmap Status (as of …)” above; regenerate/update the table rather than trusting stale labels.

Counts note:
- `suite_count` is the total occurrences across **both** `seed_t.action_id` and `ref_t1.action_id`, restricted to the
  first `num_players` ports in each record (i.e. we ignore the unused array slots for ports 3–4 in a 2p suite).

| action_id | GALE01 name (`forward.h`) | suite_count | status | owner module(s) |
|---:|---|---:|---|---|
| 0 (0x0000) | ftCo_MS_DeadDown | 1564 | missing | src/match_flow.c (new), src/stage_collision.c |
| 1 (0x0001) | ftCo_MS_DeadLeft | 360 | missing | src/match_flow.c (new), src/stage_collision.c |
| 2 (0x0002) | ftCo_MS_DeadRight | 120 | missing | src/match_flow.c (new), src/stage_collision.c |
| 4 (0x0004) | ftCo_MS_DeadUpStar | 1056 | missing | src/match_flow.c (new), src/stage_collision.c |
| 12 (0x000C) | ftCo_MS_Rebirth | 2400 | missing | src/match_flow.c (new), src/stage_collision.c |
| 13 (0x000D) | ftCo_MS_RebirthWait | 36 | missing | src/match_flow.c (new), src/stage_collision.c |
| 14 (0x000E) | ftCo_MS_Wait | 3563 | implemented | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 15 (0x000F) | ftCo_MS_WalkSlow | 420 | implemented | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 16 (0x0010) | ftCo_MS_WalkMiddle | 434 | implemented | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 17 (0x0011) | ftCo_MS_WalkFast | 190 | implemented | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 18 (0x0012) | ftCo_MS_Turn | 1804 | implemented | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 19 (0x0013) | ftCo_MS_TurnRun | 64 | implemented | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 20 (0x0014) | ftCo_MS_Dash | 9944 | implemented | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 21 (0x0015) | ftCo_MS_Run | 1808 | implemented | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 23 (0x0017) | ftCo_MS_RunBrake | 154 | implemented | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 24 (0x0018) | ftCo_MS_KneeBend | 5694 | implemented | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 25 (0x0019) | ftCo_MS_JumpF | 7992 | implemented | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 26 (0x001A) | ftCo_MS_JumpB | 2070 | implemented | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 27 (0x001B) | ftCo_MS_JumpAerialF | 5090 | implemented | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 28 (0x001C) | ftCo_MS_JumpAerialB | 1502 | implemented | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 29 (0x001D) | ftCo_MS_Fall | 2134 | implemented | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 32 (0x0020) | ftCo_MS_FallAerial | 166 | implemented | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 35 (0x0023) | ftCo_MS_FallSpecial | 336 | implemented | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 38 (0x0026) | ftCo_MS_DamageFall | 900 | partial | src/combat.c, src/timers.c |
| 39 (0x0027) | ftCo_MS_Squat | 708 | implemented | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 40 (0x0028) | ftCo_MS_SquatWait | 458 | implemented | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 41 (0x0029) | ftCo_MS_SquatRv | 82 | missing | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 42 (0x002A) | ftCo_MS_Landing | 3784 | implemented | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 43 (0x002B) | ftCo_MS_LandingFallSpecial | 3098 | implemented | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 44 (0x002C) | ftCo_MS_Attack11 | 334 | missing | src/action.c (new attacks), src/combat.c, src/hitboxes.c |
| 45 (0x002D) | ftCo_MS_Attack12 | 202 | missing | src/action.c (new attacks), src/combat.c, src/hitboxes.c |
| 50 (0x0032) | ftCo_MS_AttackDash | 1600 | missing | src/action.c (new attacks), src/combat.c, src/hitboxes.c |
| 53 (0x0035) | ftCo_MS_AttackS3S | 72 | missing | src/action.c (new attacks), src/combat.c, src/hitboxes.c |
| 55 (0x0037) | ftCo_MS_AttackS3Lw | 114 | missing | src/action.c (new attacks), src/combat.c, src/hitboxes.c |
| 56 (0x0038) | ftCo_MS_AttackHi3 | 1562 | missing | src/action.c (new attacks), src/combat.c, src/hitboxes.c |
| 57 (0x0039) | ftCo_MS_AttackLw3 | 340 | missing | src/action.c (new attacks), src/combat.c, src/hitboxes.c |
| 60 (0x003C) | ftCo_MS_AttackS4S | 766 | missing | src/action.c (new attacks), src/combat.c, src/hitboxes.c |
| 63 (0x003F) | ftCo_MS_AttackHi4 | 1262 | missing | src/action.c (new attacks), src/combat.c, src/hitboxes.c |
| 64 (0x0040) | ftCo_MS_AttackLw4 | 910 | missing | src/action.c (new attacks), src/combat.c, src/hitboxes.c |
| 65 (0x0041) | ftCo_MS_AttackAirN | 2180 | partial | src/action.c (new attacks), src/combat.c, src/hitboxes.c |
| 66 (0x0042) | ftCo_MS_AttackAirF | 412 | partial | src/action.c (new attacks), src/combat.c, src/hitboxes.c |
| 67 (0x0043) | ftCo_MS_AttackAirB | 3988 | partial | src/action.c (new attacks), src/combat.c, src/hitboxes.c |
| 68 (0x0044) | ftCo_MS_AttackAirHi | 766 | partial | src/action.c (new attacks), src/combat.c, src/hitboxes.c |
| 69 (0x0045) | ftCo_MS_AttackAirLw | 3651 | partial | src/action.c (new attacks), src/combat.c, src/hitboxes.c |
| 70 (0x0046) | ftCo_MS_LandingAirN | 704 | implemented | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 71 (0x0047) | ftCo_MS_LandingAirF | 212 | implemented | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 72 (0x0048) | ftCo_MS_LandingAirB | 1230 | implemented | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 73 (0x0049) | ftCo_MS_LandingAirHi | 220 | implemented | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 74 (0x004A) | ftCo_MS_LandingAirLw | 1468 | implemented | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 75 (0x004B) | ftCo_MS_DamageHi1 | 624 | partial | src/combat.c, src/timers.c |
| 76 (0x004C) | ftCo_MS_DamageHi2 | 162 | partial | src/combat.c, src/timers.c |
| 77 (0x004D) | ftCo_MS_DamageHi3 | 114 | partial | src/combat.c, src/timers.c |
| 78 (0x004E) | ftCo_MS_DamageN1 | 418 | partial | src/combat.c, src/timers.c |
| 79 (0x004F) | ftCo_MS_DamageN2 | 636 | partial | src/combat.c, src/timers.c |
| 80 (0x0050) | ftCo_MS_DamageN3 | 170 | partial | src/combat.c, src/timers.c |
| 82 (0x0052) | ftCo_MS_DamageLw2 | 126 | partial | src/combat.c, src/timers.c |
| 84 (0x0054) | ftCo_MS_DamageAir1 | 596 | partial | src/combat.c, src/timers.c |
| 85 (0x0055) | ftCo_MS_DamageAir2 | 696 | partial | src/combat.c, src/timers.c |
| 86 (0x0056) | ftCo_MS_DamageAir3 | 1436 | partial | src/combat.c, src/timers.c |
| 87 (0x0057) | ftCo_MS_DamageFlyHi | 2496 | partial | src/combat.c, src/timers.c |
| 88 (0x0058) | ftCo_MS_DamageFlyN | 3680 | partial | src/combat.c, src/timers.c |
| 89 (0x0059) | ftCo_MS_DamageFlyLw | 1090 | partial | src/combat.c, src/timers.c |
| 90 (0x005A) | ftCo_MS_DamageFlyTop | 11530 | partial | src/combat.c, src/timers.c |
| 91 (0x005B) | ftCo_MS_DamageFlyRoll | 1432 | partial | src/combat.c, src/timers.c |
| 178 (0x00B2) | ftCo_MS_GuardOn | 1136 | implemented | src/action.c, src/shields.c |
| 179 (0x00B3) | ftCo_MS_Guard | 1258 | implemented | src/action.c, src/shields.c |
| 180 (0x00B4) | ftCo_MS_GuardOff | 210 | implemented | src/action.c, src/shields.c |
| 181 (0x00B5) | ftCo_MS_GuardSetOff | 1030 | partial | src/action.c, src/shields.c |
| 182 (0x00B6) | ftCo_MS_GuardReflect | 530 | implemented | src/action.c, src/shields.c |
| 183 (0x00B7) | ftCo_MS_DownBoundU | 1210 | missing | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 184 (0x00B8) | ftCo_MS_DownWaitU | 116 | missing | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 186 (0x00BA) | ftCo_MS_DownStandU | 230 | missing | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 187 (0x00BB) | ftCo_MS_DownAttackU | 590 | missing | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 188 (0x00BC) | ftCo_MS_DownFowardU | 208 | missing | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 189 (0x00BD) | ftCo_MS_DownBackU | 70 | missing | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 191 (0x00BF) | ftCo_MS_DownBoundD | 572 | missing | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 192 (0x00C0) | ftCo_MS_DownWaitD | 84 | missing | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 193 (0x00C1) | ftCo_MS_DownDamageD | 36 | missing | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 194 (0x00C2) | ftCo_MS_DownStandD | 102 | missing | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 195 (0x00C3) | ftCo_MS_DownAttackD | 98 | missing | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 196 (0x00C4) | ftCo_MS_DownFowardD | 680 | missing | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 197 (0x00C5) | ftCo_MS_DownBackD | 412 | missing | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 199 (0x00C7) | ftCo_MS_Passive | 1012 | missing | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 200 (0x00C8) | ftCo_MS_PassiveStandF | 858 | missing | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 201 (0x00C9) | ftCo_MS_PassiveStandB | 776 | missing | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 203 (0x00CB) | ftCo_MS_PassiveWallJump | 104 | missing | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 212 (0x00D4) | ftCo_MS_Catch | 1544 | missing | src/grabs.c (new), src/combat.c |
| 213 (0x00D5) | ftCo_MS_CatchPull | 174 | missing | src/grabs.c (new), src/combat.c |
| 214 (0x00D6) | ftCo_MS_CatchDash | 270 | missing | src/grabs.c (new), src/combat.c |
| 215 (0x00D7) | ftCo_MS_CatchDashPull | 8 | missing | src/grabs.c (new), src/combat.c |
| 216 (0x00D8) | ftCo_MS_CatchWait | 450 | missing | src/grabs.c (new), src/combat.c |
| 217 (0x00D9) | ftCo_MS_CatchAttack | 1274 | missing | src/grabs.c (new), src/combat.c |
| 219 (0x00DB) | ftCo_MS_ThrowF | 240 | missing | src/grabs.c (new), src/combat.c |
| 220 (0x00DC) | ftCo_MS_ThrowB | 302 | missing | src/grabs.c (new), src/combat.c |
| 221 (0x00DD) | ftCo_MS_ThrowHi | 2076 | missing | src/grabs.c (new), src/combat.c |
| 222 (0x00DE) | ftCo_MS_ThrowLw | 252 | missing | src/grabs.c (new), src/combat.c |
| 223 (0x00DF) | ftCo_MS_CapturePulledHi | 24 | missing | src/grabs.c (new), src/combat.c |
| 224 (0x00E0) | ftCo_MS_CaptureWaitHi | 24 | missing | src/grabs.c (new), src/combat.c |
| 225 (0x00E1) | ftCo_MS_CaptureDamageHi | 46 | missing | src/grabs.c (new), src/combat.c |
| 226 (0x00E2) | ftCo_MS_CapturePulledLw | 158 | missing | src/grabs.c (new), src/combat.c |
| 227 (0x00E3) | ftCo_MS_CaptureWaitLw | 618 | missing | src/grabs.c (new), src/combat.c |
| 228 (0x00E4) | ftCo_MS_CaptureDamageLw | 1036 | missing | src/grabs.c (new), src/combat.c |
| 233 (0x00E9) | ftCo_MS_EscapeF | 1240 | implemented | src/action.c, src/shields.c |
| 234 (0x00EA) | ftCo_MS_EscapeB | 774 | implemented | src/action.c, src/shields.c |
| 235 (0x00EB) | ftCo_MS_EscapeN | 544 | implemented | src/action.c, src/shields.c |
| 236 (0x00EC) | ftCo_MS_EscapeAir | 544 | implemented | src/action.c, src/shields.c |
| 237 (0x00ED) | ftCo_MS_ReboundStop | 26 | missing | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 238 (0x00EE) | ftCo_MS_Rebound | 50 | missing | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 239 (0x00EF) | ftCo_MS_ThrownF | 92 | missing | src/grabs.c (new), src/combat.c |
| 240 (0x00F0) | ftCo_MS_ThrownB | 66 | missing | src/grabs.c (new), src/combat.c |
| 241 (0x00F1) | ftCo_MS_ThrownHi | 392 | missing | src/grabs.c (new), src/combat.c |
| 242 (0x00F2) | ftCo_MS_ThrownLw | 198 | missing | src/grabs.c (new), src/combat.c |
| 245 (0x00F5) | ftCo_MS_Ottotto | 40 | missing | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 247 (0x00F7) | ftCo_MS_FlyReflectWall | 46 | missing | src/locomotion.c, src/physics.c, src/stage_collision.c |
| 252 (0x00FC) | ftCo_MS_CliffCatch | 714 | missing | src/ledge.c (new), src/stage_collision.c |
| 253 (0x00FD) | ftCo_MS_CliffWait | 854 | missing | src/ledge.c (new), src/stage_collision.c |
| 255 (0x00FF) | ftCo_MS_CliffClimbQuick | 204 | missing | src/ledge.c (new), src/stage_collision.c |
| 257 (0x0101) | ftCo_MS_CliffAttackQuick | 116 | missing | src/ledge.c (new), src/stage_collision.c |
| 259 (0x0103) | ftCo_MS_CliffEscapeQuick | 392 | missing | src/ledge.c (new), src/stage_collision.c |
| 262 (0x0106) | ftCo_MS_CliffJumpQuick1 | 28 | missing | src/ledge.c (new), src/stage_collision.c |
| 263 (0x0107) | ftCo_MS_CliffJumpQuick2 | 74 | missing | src/ledge.c (new), src/stage_collision.c |
| 322 (0x0142) | ftCo_MS_Entry | 112 | missing | src/match_flow.c (new), src/stage_collision.c |
| 323 (0x0143) | ftCo_MS_EntryStart | 464 | missing | src/match_flow.c (new), src/stage_collision.c |
| 324 (0x0144) | ftCo_MS_EntryEnd | 480 | missing | src/match_flow.c (new), src/stage_collision.c |
| 344 (0x0158) | ftFx_MS_SpecialAirNStart | 1470 | missing | src/specials.c (new), src/items.c (projectiles) |
| 345 (0x0159) | ftFx_MS_SpecialAirNLoop | 4234 | missing | src/specials.c (new), src/items.c (projectiles) |
| 346 (0x015A) | ftFx_MS_SpecialAirNEnd | 32 | missing | src/specials.c (new), src/items.c (projectiles) |
| 347 (0x015B) | ftFx_MS_SpecialSStart | 30 | missing | src/specials.c (new), src/items.c (projectiles) |
| 348 (0x015C) | ftFx_MS_SpecialS | 8 | missing | src/specials.c (new), src/items.c (projectiles) |
| 349 (0x015D) | ftFx_MS_SpecialSEnd | 80 | missing | src/specials.c (new), src/items.c (projectiles) |
| 350 (0x015E) | ftFx_MS_SpecialAirSStart | 1028 | missing | src/specials.c (new), src/items.c (projectiles) |
| 351 (0x015F) | ftFx_MS_SpecialAirS | 158 | missing | src/specials.c (new), src/items.c (projectiles) |
| 352 (0x0160) | ftFx_MS_SpecialAirSEnd | 774 | missing | src/specials.c (new), src/items.c (projectiles) |
| 354 (0x0162) | ftFx_MS_SpecialHiHoldAir | 2664 | missing | src/specials.c (new), src/items.c (projectiles) |
| 356 (0x0164) | ftFx_MS_SpecialAirHi | 988 | missing | src/specials.c (new), src/items.c (projectiles) |
| 357 (0x0165) | ftFx_MS_SpecialHiLanding | 68 | missing | src/specials.c (new), src/items.c (projectiles) |
| 358 (0x0166) | ftFx_MS_SpecialHiFall | 340 | missing | src/specials.c (new), src/items.c (projectiles) |
| 360 (0x0168) | ftFx_MS_SpecialLwStart | 1148 | missing | src/specials.c (new), src/items.c (projectiles) |
| 361 (0x0169) | ftFx_MS_SpecialLwLoop | 692 | missing | src/specials.c (new), src/items.c (projectiles) |
| 363 (0x016B) | ftFx_MS_SpecialLwEnd | 148 | missing | src/specials.c (new), src/items.c (projectiles) |
| 364 (0x016C) | ftFx_MS_SpecialLwTurn | 18 | missing | src/specials.c (new), src/items.c (projectiles) |
| 365 (0x016D) | ftFx_MS_SpecialAirLwStart | 486 | missing | src/specials.c (new), src/items.c (projectiles) |
| 366 (0x016E) | ftFx_MS_SpecialAirLwLoop | 544 | missing | src/specials.c (new), src/items.c (projectiles) |
| 368 (0x0170) | ftFx_MS_SpecialAirLwEnd | 104 | missing | src/specials.c (new), src/items.c (projectiles) |
| 369 (0x0171) | ftFx_MS_SpecialAirLwTurn | 120 | missing | src/specials.c (new), src/items.c (projectiles) |

#### Unique `animation_index` set (seed_t + ref_t1)

“Extracted tables” columns indicate whether we currently have ISO/movescript-derived artifacts keyed by this `animation_index` (msid):
- Hitboxes: `data/hitboxes/{fox,falco}.bin` (`MSLHITB1 v1`)
- Hurtbox states: `data/hurtbox_states/{fox,falco}.bin` (`MSLHURM1 v1`)
- Hit status: `data/hit_status/{fox,falco}.bin` (`MSLHSTA1 v1`)
- IASA windows: `data/moves/{fox,falco}.json` (currently used by `src/move_tables.c` for `AttackAir*` only)

Counts note:
- `suite_count` is the total occurrences across **both** `seed_t.animation_index` and `ref_t1.animation_index`, restricted
  to the first `num_players` ports in each record.
- Table columns (`hitboxes`, `hurtbox_states`, `hit_status`, `IASA windows`) are **non-empty coverage** signals:
  - `Y` means we have at least one relevant entry for that `(character, msid)` in that table.
  - `N` means no relevant entries are present for that `(character, msid)` in that table (even if the file format has a
    sparse/implicit “empty” default).

| animation_index | name (`forward.h`) | suite_count | hitboxes (fox/falco) | hurtbox_states (fox/falco) | hit_status (fox/falco) | IASA windows (fox/falco) |
|---:|---|---:|---|---|---|---|
| 2 (0x00000002) | ftCo_SM_Wait1_0 | 5999 | N/N | N/N | N/N | N/N |
| 7 (0x00000007) | ftCo_SM_WalkSlow | 420 | N/N | N/N | N/N | N/N |
| 8 (0x00000008) | ftCo_SM_WalkMiddle | 434 | N/N | N/N | N/N | N/N |
| 9 (0x00000009) | ftCo_SM_WalkFast | 190 | N/N | N/N | N/N | N/N |
| 10 (0x0000000A) | ftCo_SM_Turn | 1804 | N/N | N/N | N/N | N/N |
| 11 (0x0000000B) | ftCo_SM_TurnRun | 64 | N/N | N/N | N/N | N/N |
| 12 (0x0000000C) | ftCo_SM_Dash | 9944 | N/N | N/N | N/N | N/N |
| 13 (0x0000000D) | ftCo_SM_Run | 1808 | N/N | N/N | N/N | N/N |
| 14 (0x0000000E) | ftCo_SM_RunBrake | 154 | N/N | N/N | N/N | N/N |
| 15 (0x0000000F) | ftCo_SM_Kneebend | 5694 | N/N | N/N | N/N | N/N |
| 16 (0x00000010) | ftCo_SM_JumpF | 7992 | N/N | N/N | N/N | N/N |
| 17 (0x00000011) | ftCo_SM_JumpB | 2070 | N/N | N/N | N/N | N/N |
| 18 (0x00000012) | ftCo_SM_JumpAerialF | 5090 | N/N | N/N | N/N | N/N |
| 19 (0x00000013) | ftCo_SM_JumpAerialB | 1502 | N/N | N/N | N/N | N/N |
| 20 (0x00000014) | ftCo_SM_Fall | 2134 | N/N | N/N | N/N | N/N |
| 23 (0x00000017) | ftCo_SM_FallAerial | 166 | N/N | N/N | N/N | N/N |
| 26 (0x0000001A) | ftCo_SM_FallSpecial | 336 | N/N | N/N | N/N | N/N |
| 29 (0x0000001D) | ftCo_SM_DamageFall | 1956 | N/N | N/N | N/N | N/N |
| 30 (0x0000001E) | ftCo_SM_Squat | 708 | N/N | N/N | N/N | N/N |
| 31 (0x0000001F) | ftCo_SM_SquatWait | 458 | N/N | N/N | N/N | N/N |
| 34 (0x00000022) | ftCo_SM_SquatRv | 82 | N/N | N/N | N/N | N/N |
| 35 (0x00000023) | ftCo_SM_Landing | 3784 | N/N | N/N | N/N | N/N |
| 36 (0x00000024) | ftCo_SM_LandingFallSpecial | 3098 | N/N | N/N | N/N | N/N |
| 39 (0x00000027) | ftCo_SM_GuardOff | 210 | N/N | N/N | N/N | N/N |
| 40 (0x00000028) | ftCo_SM_GuardDamage | 1030 | N/N | N/N | N/N | N/N |
| 41 (0x00000029) | ftCo_SM_EscapeN | 544 | N/N | N/N | Y/Y | N/N |
| 42 (0x0000002A) | ftCo_SM_EscapeF | 1240 | N/N | N/N | Y/Y | N/N |
| 43 (0x0000002B) | ftCo_SM_EscapeB | 774 | N/N | N/N | Y/Y | N/N |
| 44 (0x0000002C) | ftCo_SM_EscapeAir | 544 | N/N | N/N | Y/Y | N/N |
| 45 (0x0000002D) | ftCo_SM_Rebound | 50 | N/N | N/N | N/N | N/N |
| 46 (0x0000002E) | ftCo_SM_Attack11 | 334 | Y/Y | Y/Y | N/N | Y/Y |
| 47 (0x0000002F) | ftCo_SM_Attack12 | 202 | N/N | N/N | N/N | N/N |
| 52 (0x00000034) | ftCo_SM_AttackDash | 1600 | Y/Y | Y/Y | N/N | Y/Y |
| 55 (0x00000037) | ftCo_SM_AttackS3 | 72 | Y/Y | Y/Y | N/N | Y/Y |
| 57 (0x00000039) | ftCo_SM_AttackS3Lw | 114 | N/N | N/N | N/N | N/N |
| 58 (0x0000003A) | ftCo_SM_AttackHi3 | 1562 | Y/Y | Y/Y | N/N | Y/Y |
| 59 (0x0000003B) | ftCo_SM_AttackLw3 | 340 | Y/Y | Y/Y | N/N | Y/Y |
| 62 (0x0000003E) | ftCo_SM_AttackS4 | 766 | Y/Y | Y/Y | N/N | Y/Y |
| 66 (0x00000042) | ftCo_SM_AttackHi4 | 1262 | Y/Y | Y/Y | N/N | Y/Y |
| 67 (0x00000043) | ftCo_SM_AttackLw4 | 910 | Y/Y | Y/Y | N/N | Y/Y |
| 68 (0x00000044) | ftCo_SM_AttackAirN | 2180 | Y/Y | Y/Y | N/N | Y/Y |
| 69 (0x00000045) | ftCo_SM_AttackAirF | 412 | Y/Y | Y/Y | N/N | Y/Y |
| 70 (0x00000046) | ftCo_SM_AttackAirB | 3988 | Y/Y | Y/Y | N/N | Y/Y |
| 71 (0x00000047) | ftCo_SM_AttackAirHi | 766 | Y/Y | Y/Y | N/N | Y/Y |
| 72 (0x00000048) | ftCo_SM_AttackAirLw | 3651 | Y/Y | Y/Y | N/N | Y/Y |
| 73 (0x00000049) | ftCo_SM_LandingAirN | 704 | N/N | N/N | N/N | N/N |
| 74 (0x0000004A) | ftCo_SM_LandingAirF | 212 | N/N | N/N | N/N | N/N |
| 75 (0x0000004B) | ftCo_SM_LandingAirB | 1230 | N/N | N/N | N/N | N/N |
| 76 (0x0000004C) | ftCo_SM_LandingAirHi | 220 | N/N | N/N | N/N | N/N |
| 77 (0x0000004D) | ftCo_SM_LandingAirLw | 1468 | N/N | N/N | N/N | N/N |
| 165 (0x000000A5) | ftCo_SM_DamageHi1 | 624 | N/N | N/N | N/N | N/N |
| 166 (0x000000A6) | ftCo_SM_DamageHi2 | 162 | N/N | N/N | N/N | N/N |
| 167 (0x000000A7) | ftCo_SM_DamageHi3 | 114 | N/N | N/N | N/N | N/N |
| 168 (0x000000A8) | ftCo_SM_DamageN1 | 418 | N/N | N/N | N/N | N/N |
| 169 (0x000000A9) | ftCo_SM_DamageN2 | 636 | N/N | N/N | N/N | N/N |
| 170 (0x000000AA) | ftCo_SM_DamageN3 | 170 | N/N | N/N | N/N | N/N |
| 172 (0x000000AC) | ftCo_SM_DamageLw2 | 126 | N/N | N/N | N/N | N/N |
| 174 (0x000000AE) | ftCo_SM_DamageAir1 | 596 | N/N | N/N | N/N | N/N |
| 175 (0x000000AF) | ftCo_SM_DamageAir2 | 696 | N/N | N/N | N/N | N/N |
| 176 (0x000000B0) | ftCo_SM_DamageAir3 | 1436 | N/N | N/N | N/N | N/N |
| 177 (0x000000B1) | ftCo_SM_DamageFlyHi | 2496 | N/N | N/N | N/N | N/N |
| 178 (0x000000B2) | ftCo_SM_DamageFlyN | 3680 | N/N | N/N | N/N | N/N |
| 179 (0x000000B3) | ftCo_SM_DamageFlyLw | 1090 | N/N | N/N | N/N | N/N |
| 180 (0x000000B4) | ftCo_SM_DamageFlyTop | 11530 | N/N | N/N | N/N | N/N |
| 181 (0x000000B5) | ftCo_SM_DamageFlyRoll | 1432 | N/N | N/N | N/N | N/N |
| 183 (0x000000B7) | ftCo_SM_DownBoundU | 1210 | N/N | N/N | N/N | N/N |
| 184 (0x000000B8) | ftCo_SM_DownWaitU | 116 | N/N | N/N | N/N | N/N |
| 186 (0x000000BA) | ftCo_SM_DownStandU | 230 | N/N | N/N | N/N | N/N |
| 187 (0x000000BB) | ftCo_SM_DownAttackU | 590 | Y/Y | Y/Y | Y/Y | N/N |
| 188 (0x000000BC) | ftCo_SM_DownFowardU | 208 | N/N | N/N | N/N | N/N |
| 189 (0x000000BD) | ftCo_SM_DownBackU | 70 | N/N | N/N | N/N | N/N |
| 191 (0x000000BF) | ftCo_SM_DownBoundD | 572 | N/N | N/N | N/N | N/N |
| 192 (0x000000C0) | ftCo_SM_DownWaitD | 84 | N/N | N/N | N/N | N/N |
| 193 (0x000000C1) | ftCo_SM_DownDamageD | 36 | N/N | N/N | N/N | N/N |
| 194 (0x000000C2) | ftCo_SM_DownStandD | 102 | N/N | N/N | N/N | N/N |
| 195 (0x000000C3) | ftCo_SM_DownAttackD | 98 | Y/Y | Y/Y | Y/Y | N/N |
| 196 (0x000000C4) | ftCo_SM_DownFowardD | 680 | N/N | N/N | N/N | N/N |
| 197 (0x000000C5) | ftCo_SM_DownBackD | 412 | N/N | N/N | N/N | N/N |
| 199 (0x000000C7) | ftCo_SM_Passive | 1012 | N/N | N/N | N/N | N/N |
| 200 (0x000000C8) | ftCo_SM_PassiveStandF | 858 | N/N | N/N | N/N | N/N |
| 201 (0x000000C9) | ftCo_SM_PassiveStandB | 776 | N/N | N/N | N/N | N/N |
| 203 (0x000000CB) | ftCo_SM_PassiveWallJump | 104 | N/N | N/N | N/N | N/N |
| 210 (0x000000D2) | ftCo_SM_Ottotto | 40 | N/N | N/N | N/N | N/N |
| 212 (0x000000D4) | ftCo_SM_WallDamage | 46 | N/N | N/N | N/N | N/N |
| 216 (0x000000D8) | ftCo_SM_CliffCatch | 714 | N/N | N/N | N/N | N/N |
| 217 (0x000000D9) | ftCo_SM_CliffWait | 854 | N/N | N/N | N/N | N/N |
| 220 (0x000000DC) | ftCo_SM_CliffClimbQuick | 204 | N/N | N/N | N/N | N/N |
| 222 (0x000000DE) | ftCo_SM_CliffAttackQuick | 116 | N/N | N/N | N/N | N/N |
| 224 (0x000000E0) | ftCo_SM_CliffEscapeQuick | 392 | N/N | N/N | Y/Y | N/N |
| 227 (0x000000E3) | ftCo_SM_CliffJumpQuick1 | 28 | N/N | N/N | N/N | N/N |
| 228 (0x000000E4) | ftCo_SM_CliffJumpQuick2 | 74 | N/N | N/N | N/N | N/N |
| 238 (0x000000EE) | ftCo_SM_EntryStart | 464 | N/N | N/N | N/N | N/N |
| 242 (0x000000F2) | ftCo_SM_Catch | 1718 | Y/Y | Y/Y | N/N | N/N |
| 243 (0x000000F3) | ftCo_SM_CatchDash | 278 | Y/Y | Y/Y | N/N | N/N |
| 244 (0x000000F4) | ftCo_SM_CatchWait | 450 | Y/Y | Y/Y | N/N | N/N |
| 245 (0x000000F5) | ftCo_SM_CatchAttack | 1274 | N/N | N/N | N/N | N/N |
| 247 (0x000000F7) | ftCo_SM_ThrowF | 240 | Y/Y | Y/Y | N/N | N/N |
| 248 (0x000000F8) | ftCo_SM_ThrowB | 302 | Y/Y | Y/Y | N/N | N/N |
| 249 (0x000000F9) | ftCo_SM_ThrowHi | 2076 | Y/Y | Y/Y | N/N | N/N |
| 250 (0x000000FA) | ftCo_SM_ThrowLw | 252 | Y/Y | Y/Y | N/N | N/N |
| 251 (0x000000FB) | ftCo_SM_CapturePulledHi | 24 | N/N | N/N | N/N | N/N |
| 252 (0x000000FC) | ftCo_SM_CaptureWaitHi | 24 | N/N | N/N | N/N | N/N |
| 253 (0x000000FD) | ftCo_SM_CaptureDamageHi | 46 | N/N | N/N | N/N | N/N |
| 254 (0x000000FE) | ftCo_SM_CapturePulledLw | 158 | N/N | N/N | N/N | N/N |
| 255 (0x000000FF) | ftCo_SM_CaptureWaitLw | 618 | N/N | N/N | N/N | N/N |
| 256 (0x00000100) | ftCo_SM_CaptureDamageLw | 1036 | N/N | N/N | N/N | N/N |
| 262 (0x00000106) | ftCo_SM_ThrownF | 92 | Y/Y | Y/Y | N/N | N/N |
| 263 (0x00000107) | ftCo_SM_ThrownB | 66 | Y/Y | Y/Y | N/N | N/N |
| 264 (0x00000108) | ftCo_SM_ThrownHi | 392 | Y/Y | Y/Y | N/N | N/N |
| 265 (0x00000109) | ftCo_SM_ThrownLw | 198 | Y/Y | Y/Y | N/N | N/N |
| 298 (0x0000012A) | ftFx_SM_SpecialAirNStart | 1470 | Y/Y | Y/Y | N/N | N/N |
| 299 (0x0000012B) | ftFx_SM_SpecialAirNLoop | 4234 | Y/Y | Y/Y | N/N | N/N |
| 300 (0x0000012C) | ftFx_SM_SpecialAirNEnd | 32 | Y/Y | Y/Y | N/N | N/N |
| 301 (0x0000012D) | ftFx_SM_SpecialSStart | 30 | Y/Y | Y/Y | N/N | N/N |
| 302 (0x0000012E) | ftFx_SM_SpecialS | 8 | Y/Y | Y/Y | N/N | N/N |
| 303 (0x0000012F) | ftFx_SM_SpecialSEnd | 80 | Y/Y | Y/Y | N/N | N/N |
| 304 (0x00000130) | ftFx_SM_SpecialAirSStart | 1028 | Y/Y | Y/Y | N/N | N/N |
| 305 (0x00000131) | ftFx_SM_SpecialAirS | 158 | Y/Y | Y/Y | N/N | N/N |
| 306 (0x00000132) | ftFx_SM_SpecialAirSEnd | 774 | Y/Y | Y/Y | N/N | N/N |
| 308 (0x00000134) | ftFx_SM_SpecialHiHoldAir | 2664 | Y/Y | Y/Y | N/N | N/N |
| 309 (0x00000135) | ftFx_SM_SpecialHi | 988 | Y/Y | Y/Y | N/N | N/N |
| 310 (0x00000136) | ftFx_SM_SpecialHiLanding | 68 | N/N | N/N | N/N | N/N |
| 311 (0x00000137) | ftFx_SM_SpecialHiFall | 340 | N/N | N/N | N/N | N/N |
| 313 (0x00000139) | ftFx_SM_SpecialLwStart | 1148 | Y/Y | Y/Y | Y/Y | N/N |
| 314 (0x0000013A) | ftFx_SM_SpecialLwLoop | 710 | Y/Y | Y/Y | N/N | N/N |
| 316 (0x0000013C) | ftFx_SM_SpecialLwEnd | 148 | Y/Y | Y/Y | N/N | N/N |
| 317 (0x0000013D) | ftFx_SM_SpecialAirLwStart | 486 | Y/Y | Y/Y | Y/Y | N/N |
| 318 (0x0000013E) | ftFx_SM_SpecialAirLwLoop | 664 | Y/Y | Y/Y | N/N | N/N |
| 320 (0x00000140) | ftFx_SM_SpecialAirLwEnd | 104 | Y/Y | Y/Y | N/N | N/N |
| 4294967295 (0xFFFFFFFF) | MSL_ANIM_NONE (0xFFFFFFFF) | 5586 | N/N | N/N | N/N | N/N |

### 2) Mechanics-to-Data-to-Code matrix (decomp-first)

For each bucket below:
- **Read first**: the first GALE01 decomp entrypoints to read (file + functions).
- **Data artifacts**: ISO-extracted artifacts/tables we expect to drive the behavior (paths under `data/` and format names when known).
- **Code owner / gaps**: where the implementation lives today (or where it should live if absent).

Status note: this matrix can go stale as systems land quickly. If a row claims something is “Missing” but the sim already implements it,
update the row rather than re-deriving the same plan again.

#### Match flow (start, death/respawn/invuln, stocks, blastzones)

| Read first (decomp) | Data artifacts (ISO-derived) | Code owner / gaps |
|---|---|---|
| `refs/melee/src/melee/ft/fighter.c::Fighter_UnkProcessDeath_80068354` | `data/stages/final_destination.json` (collision segments; includes `unit_scale`) | `src/match_flow.c` (new), `src/stage_collision.c` |
| `refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState` | `data/common/ft_common_data.json` (common timers/constants; see `docs/DATA_CONTRACT.md`) | `src/action.c` (state transitions), `src/timers.c` |
| `refs/melee/src/melee/ft/chara/ftCommon/ftCo_DemoCallback0.c::ftCo_800C6150` (Rebirth entry) | (Need) spawn/respawn positions extracted from stage DAT (currently not represented explicitly in `data/stages/final_destination.json`) | Implemented for suite; remaining gap is making spawn/respawn positioning fully data-driven from extracted stage artifacts. |
| `refs/melee/src/melee/mp/mplib.c::mpLib_DrawZones` (blast/camera zone sources) | (Need) explicit blast zone rect for FD extracted into `data/stages/final_destination.json` (or a `data/stages/*.bin` v2) | Implemented for suite; remaining gap is extracting and consuming the canonical blastzone rect(s) from stage data. |

#### Locomotion core (ground/air, jumps, fastfall, landing, airdodge/escapes)

| Read first (decomp) | Data artifacts (ISO-derived) | Code owner / gaps |
|---|---|---|
| `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA` | `data/common/ft_common_data.json` (stick thresholds, timers) | `src/locomotion.c` |
| `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA` | `data/characters/{fox,falco}.json` (walk/run/traction/turn/jump params) | `src/locomotion.c` |
| `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Anim` and `ftCo_JumpAerial.c::ftCo_JumpAerial_Enter_Basic` | `data/anims/{fox,falco}.bin` (anim end frames; `SSANIM01` tables) | `src/locomotion.c` (jump timers), `src/physics.c` |
| `refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_80099A58` | `data/common/ft_common_data.json` (EscapeAir deadzones/force) | `src/action.c` + `src/locomotion.c` |

#### Ledge system (cliff catch/occupancy/options)

| Read first (decomp) | Data artifacts (ISO-derived) | Code owner / gaps |
|---|---|---|
| `refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081298` (cliff catch check) | `data/stages/final_destination.json` (`segments[*].ledge` + segment endpoints) | `src/ledge.c` (new), `src/stage_collision.c` |
| `refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081370` (enter cliff catch) | (Need) ledge occupancy / cliff id representation (per-side, per-player) | Missing: cliff occupancy + refresh rules |
| `refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_CliffWait_IASA` | `data/common/ft_common_data.json` (ledge option windows / timers) | Missing: cliff options state machine |

#### Defense (shield, shieldstun/GuardSetOff, OoS options)

| Read first (decomp) | Data artifacts (ISO-derived) | Code owner / gaps |
|---|---|---|
| `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091A4C` (guard entry) | `data/common/ft_common_data.json` (shield health/decay/recharge constants) | `src/action.c`, `src/shields.c` |
| `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_80099894` (roll/spotdodge) | `data/anims/{fox,falco}.bin` (escape anim end frames) | `src/action.c` (Escape*), `src/locomotion.c` (root-motion gaps) |
| `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardDamage_Anim` (GuardSetOff / stun loop) | `data/common/ft_common_data.json` (shieldstun duration rules) | Partial today: `src/combat.c` enters `GuardSetOff`, but per-frame behavior is incomplete |

#### Combat geometry (hurtcaps/hitboxes/shields overlap classification)

| Read first (decomp) | Data artifacts (ISO-derived) | Code owner / gaps |
|---|---|---|
| `refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70` (fighter-vs-fighter collision pass) | `data/hurtcaps/{fox,falco}.bin` (`MSLHURT1 v1`) + `data/anims/{fox,falco}*.bin` (pose) | `src/hurtboxes.c`, `src/hurtcaps_tables.c`, `src/anim_pose.c` |
| `refs/melee/src/melee/ft/ftaction.c::ftAction_80073240` (script timers use `cur_anim_frame`) | `data/hitboxes/{fox,falco}.bin` (`MSLHITB1 v1`) | `src/hitboxes.c`, `src/hitboxes_tables.c` |
| `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c` (shield bubble placement) | `data/shields/{fox,falco}.bin` (shield-tilt table) | `src/shields.c` (placement) |

#### Damage pipeline (percent, hitlag, hitstun, KB, damage state entry, rehit/hitlists)

| Read first (decomp) | Data artifacts (ISO-derived) | Code owner / gaps |
|---|---|---|
| `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0` (damage state entry) | `data/common/ft_common_data.json` (hitlag constants; damage tables) | `src/combat.c` |
| `refs/melee/src/melee/ft/fighter.c::Fighter_TakeDamage_8006CC7C` (percent accumulation) | (Need) stale-move queue tables (not yet extracted) | Missing: staling + damage multipliers |
| `refs/melee/src/melee/ft/fighter.c::Fighter_8006A1BC` and `Fighter_ProcessHit_8006D1EC` (hitlag toggles, cleanup) | (Seed/outputs) `state_flags` bytes (see “state_flags Ownership” above) | `src/timers.c`, `src/combat.c` |
| `refs/melee/src/melee/ft/ftcoll.c` (hitlist / rehit timers) | (Need) per-hitbox hitlist semantics (not yet extracted) | Partial today: conservative per-(attacker,defender) latch; replace with per-hitbox hitlists |

#### Specials (Fox + Falco B moves; suite-first)

| Read first (decomp) | Data artifacts (ISO-derived) | Code owner / gaps |
|---|---|---|
| `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_Enter` (Blaster) | `data/special_msids/{fox,falco}.json` (special msid list) + `data/moves/{fox,falco}.json` (script events) | Partial today: blaster loop/shot spawning is implemented via `src/action.c` + `src/items.c`; remaining Fox/Falco specials still need a decomp-shaped dispatch/state machine. |
| `refs/melee/src/melee/it/items/itfoxblaster.c` (blaster item + laser spawn plumbing) | (Need) projectile param tables (speed, lifetime, damage) extracted per character | Partial today: lasers are implemented and data-driven for the suite; remaining gap is generalizing item/projectile param extraction and item types beyond lasers. |

#### Grabs/throws

| Read first (decomp) | Data artifacts (ISO-derived) | Code owner / gaps |
|---|---|---|
| `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_CatchDash_Anim` (grab chain pieces) | `data/moves/{fox,falco}.json` (grab/throw script events; already extracted for some msids) | Missing: `src/grabs.c` (new) + constraints/attachment rules |
| `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_ThrowF_Anim` | Throw release/params (script-derived) | **PARTIAL**: release timing + throw hit params are extracted into `data/moves/*.json` and applied in `src/throw_flow.c`; remaining gaps are throw/capture action coverage and victim constraint modes beyond the release frame. |

#### Projectiles/items (lasers minimum)

| Read first (decomp) | Data artifacts (ISO-derived) | Code owner / gaps |
|---|---|---|
| `refs/melee/src/melee/it/items/itfoxblaster.c` (laser lifecycle/collision) | (Need) item/projectile type tables extracted into `data/items/` (new) | Partial today: deterministic fixed-capacity pool + laser lifecycle/collision exist in `src/items.c`; expand to a general item system only as suite demands. |
| `refs/melee/src/melee/ft/ft_0BF0.c` (fighter ↔ item hooks for blaster) | (Need) precise spawn offsets (bone + local offset) from extracted pose/movescript | Partial today: suite-needed fighter↔laser hooks exist; remaining gap is making spawn offsets fully data-driven and covering non-laser projectile hooks. |

### 3) Prioritized parity projects / slices (next 5–10)

Goal of this section: stop hopping between systems by front-loading substrate work, and only then doing vertical slices on top.

Completed slices (suite-positive, now “owned” by the sim):
- Anim/script timebase (Q16.16 `cur_anim_frame` + seeded `frame_speed_mul_f32` + hitlag freeze).
- Match flow (KO/death/respawn/entry) for the suite (stocks/is_dead to 0; KO timing fixed by integrating kb_vel).
- Shield/GuardSetOff loop and shield placement/tilt.
- Lasers/blaster + deterministic item pool (suite-needed subset).
- Ledge states (suite-needed subset) + Cliff* grounding fix.
- Knockdown cluster (DownBound/Wait/Stand/Attack + rolls) with root-motion sampling for rolls.

Next slices (in recommended order):

Driver shortlist (suite offender clusters as of the committed baseline in `reports/validation/one_step_suite_eval.txt`):
- Grab/Capture/Throw completeness: release/detach + throw-hit apply are in, but remaining mismatches concentrate in `Capture*`/`Thrown*`
  follow-up frames (constraint mode, capture point selection, pummel/breakout, and action coverage).
- Combat hit bookkeeping parity (hitlists/rehit/modifiers): a major driver of `mismatch.hitlag`, `mismatch.hitstun`, and `err.percent`
  once position/state sequencing is stable.

1. mpColl-style ground contact state machine (**parity project**, see Ordering Rule above)
   - Status: **PARTIAL** (FD floor + wall/ceiling + persistence landed; see `src/mpcoll_ground.c` and `tests/test_mpcoll_ground_persistence.py`).
   - Blocked by: nothing upstream (this is the next substrate).
   - Dependencies: existing ECB tables + stage geometry.
   - Acceptance checks: materially reduce residual `mismatch.on_ground` and `mismatch.ground_id` without per-action special cases.
   - Deliverable: one coherent ground-contact module that higher-level states call into.

2. Collision-env flags + ledge grab mask parity (**parity project**)
   - Status: **PARTIAL** (collision-stage prev/cur snapshot based ledge-grab mask is implemented and scheduled post-collision).
   - Blocked by: (1) mpColl parity for stable `env_flags`/line identity + occupancy/refresh/cooldowns.
   - Dependencies: (1).
   - Acceptance checks: reduce remaining ledge mismatch clusters without action-specific hacks; remove remaining “outside-only grab” approximation if still present.

3. Hitlists/rehit timers parity (**parity project**)
   - Blocked by: (1) mpColl parity (stable contact/landing frames; consistent “same-frame” ordering).
   - Dependencies: (1) and stable move/hit identity.
   - Acceptance checks: replace conservative rehit latch; improve hitlag/hitstun/percent timing coherence without regressions.

4. Damage modifiers parity (stale queue + multipliers + armor/no-damage gates) (**parity project**)
   - Blocked by: (3) hitlists parity (needs correct hit identity + timing before modifiers mean anything).
   - Dependencies: (3).
   - Acceptance checks: percent/KB/hitstun become numerically meaningful (reduce drift); avoid tuning off suite-only artifacts.

5. Grab/throw core (suite-first)
   - Current status: attachment substrate exists and throw release/detach + throw-hit apply are implemented (data-driven).
   - Remaining gaps: capture point selection/coverage, pummel/breakout, and victim constraint mode parity beyond the release frame.
   - Blocked by: (3) hitlists parity for full combat correctness; (1) mpColl parity is strongly preferred for stable contact semantics.
   - Decomp entrypoints: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::fn_800DAD18`,
     `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724`, and
     `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508`.
   - Acceptance checks: reduce `mismatch.action_id` for Catch*/Throw*/Thrown* follow-up frames and remaining `err.pos_* max` spikes attributable to
     constraint/attachment mismatches (not release frames).

6. Remaining Fox/Falco specials (B moves) + any suite-needed projectiles
   - Blocked by: (3)–(4) if the special applies hits/damage in ways that require correct rehit/modifiers (avoid patching around missing modifiers).
   - Dependencies: (3)–(4) for correct damage/rehit semantics; items framework for projectile specials.

7. DI/SDI/ASDI
   - Blocked by: (1)–(4); otherwise DI work degenerates into chasing upstream float/ordering mismatches.
   - Only start once (1)–(4) are “close enough” that we would otherwise be chasing small float mismatches.

### 4) Reseed/schema risk register (known missing internals)

When a mismatch strongly suggests a missing internal that cannot be reconstructed deterministically from replay-exposed fields, add it here (so we stop rediscovering it).

- `fp->frame_speed_mul` fractional carry / true `cur_anim_frame` accumulator (hitlag coupling).
- “Allow interrupt” / IASA gating latches beyond AttackAir* (many actions use DO_IASA with additional internal gates).
- Hitbox hitlists / per-hitbox rehit timers (replacing conservative pair latch).
- Stale-move queue (staling) + damage multipliers: seeded via replay-history derivation, but `fp->x206C_attack_instance` is not
  exposed by Slippi. We conservatively model the `ft_800890D0` bump (on action-state transitions) but do not model additional bump
  sites like `ft_800892A0`, which can change duplicate suppression in the stale table (see `tools/slippi/staling_history.py`).
- Ledge occupancy + ledge refresh timer(s) + per-action ledge regrab restrictions.
- Ledge option `mv.co.cliff.x8` gate is currently approximated via a “previous-stick neutral reset” check for climb/drop on CliffWait
  (`src/ledge.c:190`); this may need to become an explicit seeded/internal latch for full parity.
- Ledge catch region is currently approximated with an “outside-only grab” gate when using `ledge_grab_window_ok`
  (`src/ledge.c:376`), pending a decomp-shaped `Collide_LedgeGrabMask` / collision-env implementation.
- Grab state internals: grab attach points, breakouts, throw release frame/timers, and victim constraint mode.
- SSANIM / ECB axis mapping contract: do not change tz/ty basis mappings in the core sim as an ad-hoc “fix”; treat any axis remap as a
  separate audit project with explicit validation and documentation (see `docs/SSANIM_AXIS_BASIS.md`).
- Tech / knockdown thresholds and state vars (tumble, tech window timers, missed-tech timers).
- Projectile internals: per-projectile RNG/state, instance ids, and collision masks.
- Projectile reflect bubbles: current laser reflect gate is powershield-only (requires Slippi powershield bit) and does not yet model
  special-move reflect bubbles (reflect active without powershield). See `src/items.c` TODO(reflect).
- Known missing: blaster “gun” items (ItKind 74/75) + Slippi `item.id==0` cases. These show up in the Slippi item list and can cause
  fixed-slot churn (e.g. lasers shift slots when the gun appears/disappears). Decomp spawn/remove: `refs/melee/src/melee/it/items/itfoxblaster.c`
  (`it_802AE8A8` / `it_802AEAB4`) called from `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c` (`ftFx_SpecialN_Enter` /
  `ftFx_SpecialN_RemoveBlaster`).

### Systems Inventory (Running List, Prioritized)

This is a living, comprehensive list of Melee-relevant systems. Any time we become aware of a missing mechanic, add it here and place it at the right priority.

#### P0 — Must-have for RL 1.0 (target domain: Fox/Falco FD)

1) Match flow / state sequencing (**DONE**)
- Match start sequence / Ready-Go timing (for RNN warmup seeding).
- Stock loss, death, respawn, invulnerability, spawn positioning.
- Blastzones + KO rules (FD).
- Rollback handling: we validate on finalized frames only.

2) Input pipeline (UCF-on; suite-configured) (**PARTIAL**)
- Stick/button sampling at frame boundaries; legalization/clamp/deadzone consistent with validation datasets.
- UCF behavior that affects gameplay in this suite (dashback, shielddrop/pad buffer, and cardinals if the suite uses them).
- Input-history counters/timers that gate locomotion/defense/cancels (keep these as explicit seeded internals as needed).

3) Action/state machine + timebases (**DONE** for timebase, **PARTIAL** for full state machine)
- Core action-state transitions for all action_ids present in the suite (see Planning Gate above).
- Animation/script timebase:
  - `anim_frame_f32` is seeded from Slippi post-frame `state_age` (`fp->cur_anim_frame` float).
  - Sim policy (decomp-shaped, deterministic):
    - Maintain an internal signed Q16.16 accumulator mirroring `fp->cur_anim_frame` and advance it by a seeded/latched `frame_speed_mul`
      each frame (frozen during hitlag).
    - Derive `action_frame` as `floor(cur_anim_frame)` from that accumulator for table lookups and comparisons.
    - On motion-state entry, reset `cur_anim_frame` as `anim_start - frame_speed_mul` (per `Fighter_ChangeMotionState`), so the next
      anim-advance produces `anim_start`.
  - **Known blocker**: DamageAir hitstun hurtcap pose can diverge from both replay-visible `action_frame` and seeded `anim_frame_f32`.
    TBK rec=1575 Dolphin forensics show airborne DamageAir2 hurtcaps using a Damage AObj pose clock not represented in the current seed/model.
    Add an explicit DamageAir AObj pose-clock seed/model so BODY contact uses the correct hurtcap pose without temporary contact suppression.

4) Locomotion + physics core (**PARTIAL**)
- Ground/air movement, friction/traction, gravity/terminal velocity, fastfall, jumps (incl. double jump).
- Landing transitions and landing lag handling.

5) Stage collision + ECB fidelity (**PARTIAL**; mpColl-shaped FD grounding is implemented)
- FD ground/ledge/blastzone geometry from stage files.
- ECB-like grounding/ledge gating close enough to avoid false landings/false airborne.
- Stable `ground_id` behavior (segment/line identity).

6) Defense (**PARTIAL**)
- Shield bubble placement/tilt, HP drain/recharge, shieldstun / GuardSetOff.
- Grounded OoS options used by suite: roll, spotdodge, jump, airdodge, etc.
- Powershield gating behavior as needed by suite (don’t tune; decomp-first).

7) Combat geometry (**PARTIAL**)
- Hurtcapsules (pose-driven world endpoints, eligibility/modes, hit status).
- Hitboxes (movescript-driven, pose/world placement, flags/attrs).
- Shield bubble overlap classification and priority.

8) Damage pipeline (coherent) (**PARTIAL**)
- Body hits: percent accumulation, hitlag, hitstun, knockback velocity, damage state entry.
- Shield hits: shield HP depletion, GuardSetOff, hitlag inputs, inert/detection hitboxes behavior.
- Rehit/hitlist semantics closer than the current conservative pair latch (per-hitbox hitlists + timers).
- Stale-move queue + damage multipliers (decomp-first).
- “No damage”/armor/metal/other gating required for Fox/Falco suite correctness.

9) Fox/Falco full moveset coverage (suite-first) (**PARTIAL**)
- All moves (A + B) whose action states appear in the suite must be implemented with correct transitions/cancel windows.
- Special moves (B moves) for Fox/Falco are explicitly in-scope for RL 1.0.

10) Grabs/throws (**PARTIAL**)
- Attachment substrate exists (CapturePulled*/Wait*/Damage* + Thrown* victim driving); still missing throw release/detach/apply-throw-hit and full throw/capture state completeness (suite-first).
- Grab interactions can dominate policy behavior; missing this makes RL “not Melee” quickly.

11) Projectiles/items needed by suite (**PARTIAL**)
- Fox/Falco lasers at minimum (spawn/update/hit).
- Other items only if they appear in the suite; expand later.

#### P1 — Strongly preferred for RL 1.0 (often suite-dependent)

- Ledge system completeness: cliff catch, occupancy, cliff options, invuln windows, refresh rules.
- Knockdown/tumble/tech options (tech in place/roll/miss tech, getups).
- DI/SDI/ASDI (defer only if everything else is already extremely close; keep decomp-first plan).
- Tech nuance: Amsah tech near ledge (high policy impact; suite-dependent).

#### P2 — Stretch / post-1.0

- Short-horizon rollout parity (open-loop) on a subset of the suite.
- 4p doubles-specific interactions (team damage rules, teammate collision nuances, simultaneous collision priority).
- Broader stage roster and character roster.

### Milestones (Suggested Order)

M0 Foundation (**DONE**)
- One-step suite validation loop, seeded internals, deterministic stepping, no hot-path allocs.

M1 Timebase + match flow (**DONE**)
- Decomp-shaped anim/script timebase and match start/respawn/death/invuln/blastzones wired into the state machine.

M2 Ground contact substrate (mpColl parity) (**PARTIAL**)
- FD mpColl-shaped grounding (floor + wall/ceiling) and persistence are implemented; remaining work is tightening residual mismatch clusters
  and extending mpColl parity (substeps/platform lines/full callback coverage) as needed by the suite.

M3 Locomotion + defense completeness for suite (**PARTIAL**)
- Ensure all suite action_ids can be entered, updated, and exited without “getting stuck”.
- Ensure `action_frame` / `anim_frame_f32` semantics are consistent enough for movescript sampling.
- Shield + OoS + airdodge + core defensive interrupts.

M4 Combat/damage coherence (**PARTIAL**)
- Treat percent/KB/hitstun/action-entry as one coherent pipeline (avoid piecemeal).
- Replace conservative rehit pair latch with per-hitbox hitlists/timers.
- Add stale queue + damage modifiers after hit identity/timing is correct.

M5 Special moves + grabs (**TODO/PARTIAL**)
- Implement all Fox/Falco specials and grab system needed by suite.

M6 Ledge/tech/knockdown (**PARTIAL**)
- Add what the suite exercises; broaden as needed for RL plausibility.
