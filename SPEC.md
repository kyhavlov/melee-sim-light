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
- Game (minimum):
  - `stage`, `is_teams`, and optional `randall_phase` (can be constant on FD), optional items.

Important implication: because `slippi-ai` consumes a **one-hot action id** (size `0x18F`), this sim must maintain an **action/state machine with GALE01 action ids** (at least for the states reachable by Fox/Falco on FD), not just “coarse locomotion.”

## Scope (v1)

### Domain

- Stage: **Final Destination**
- Characters: **Fox, Falco**
- Players: **2** (design supports **4**)
- Items/projectiles: **lasers** (as a minimal projectile system)
- Camera, rendering, audio: none

### Required Gameplay Systems

To reach “90–95% like real Melee” for the target domain, v1 must include:

1) **Input processing**
- Digital buttons and analog sticks sampled at frame boundaries.
- Configurable “legalization”/clamping consistent with controller conventions.
- **UCF is enabled by default** (because it applies to essentially all modern replay data we will validate against).
  - Include a feature flag for **“UCF 1.0 cardinals”** (more recent/niche) since suites may differ.

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
vs using root `pos_y`, but there are two **explicit teacher-forcing compatibility hacks** in the current implementation
to avoid expanding the reseed schema while we are still in one-step eval mode:

- **dy==0 + prev_on_ground: skip y constraint**: if the fighter had `prev_on_ground=1` and root `dy==0`, we do not require
  `y_bot` to be within epsilon of the surface for a segment to be considered. This prevents spurious de-grounding when the
  ECB offset changes due to pose/animation but we do not track `prev_ecb_off` in the seed state.
- **Landing-frame vel_y preservation**: on the air→ground transition frame, we preserve the pre-collision `speed_y_self`
  (which can remain negative in Slippi post-frames) and only zero `speed_y_self` on the subsequent grounded frame.

These are not intended to be relied on as “mechanics”; they should be revisited once we model a more faithful ECB/collision
pipeline (including any required prior-frame state) and/or once we validate ordering against decomp more directly.

5) **Combat**
- Hurtboxes/hitboxes extracted from character animation/move files:
  - hitbox positions tied to animation bones/transforms
  - hitbox active windows per action/anim frame
  - hurtbox set per anim frame (goal: extremely close to real; simplification is last resort)
- Hit resolution:
  - hitlag, hitstun, knockback, tumble
  - DI (goal: close to real; only simplify once extremely close)
  - shield interaction (see next section)

**Combat Mutations (Pass 1, current)**
- **BODY-only**: world-space hitbox spheres vs world-space hurtcap capsules, with existing grounded/airborne gating.
- **Shield-safe**: if a hitbox overlaps the defender shield bubble, that (attacker, defender, hitbox_id) is treated as SHIELD
  and does not apply BODY mutations.
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
- **Rehit suppression (simplified, conservative)**: a per-(attacker, defender) latch suppresses repeated hits for that pair
  (ignoring `hitbox_id`) until hitboxes clear or the attacker msid changes.
  - Rationale: we do not have per-hitbox hitlists/timers yet; this conservative policy reduces one-step false positives where
    multiple active hitboxes would otherwise re-hit immediately after hitlag ends.
  - Latch clear rules (current): clears on full hitbox clear (`hitbox_count==0`), attacker msid change, or defender instance_id
    change; it does not clear when a specific hitbox_id is disabled.
  - Missing decomp pieces: per-hitbox hitlist entries, rehit-rate timers, hitbox refresh ordering vs collision, clanks/trades,
    and full hurtbox eligibility (intangibility, thrown-fighter rules, etc.). These need to be added before enabling percent /
    knockback / hitstun mutations.

### `state_flags` Ownership (seed vs derived)

The dataset exposes 5 raw bytes of `state_flags` captured from fighter offsets `(0x2218, 0x221A, 0x221B, 0x221C, 0x221F)`
in that order (see `refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm`). The sim treats `state_flags` as **partially
sim-owned**:

**Sim-owned derived outputs (overwritten by the sim each step)**
- `0x221A` bit `0x20` (`isHitlag`): derived from `hitlag > 0` (kept consistent when combat applies hitlag and as timers decrement).
- `0x221B` bit `0x80` (`isShieldActive`): derived from whether the shield bubble is active (`shield_radius > 0`).
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
  - `0x221C` bit `0x02` (`isHitstun`) is used as a gate for hitstun decrement timing, but is not mutated by the sim yet.
  - `0x221C` bit `0x20` (powershield active) is consulted for shield-damage gating, but is not mutated by the sim yet.

6) **Shield**
- Shield health/decay/regeneration (approx ok).
- Shieldstun + basic pushback.
- Roll/spotdodge out of shield.
  - Not modeled yet: EscapeF/EscapeB root-motion (`fp->x6A4_transNOffset`) and mid-roll facing flip (`ftCheckThrowB3`); we currently apply friction-only and use anim-end to return to Wait.
  - Not modeled yet: escape invincibility / hurtbox state changes during EscapeN/EscapeF/EscapeB.
- **Known approximation (current): shield bubble center** is currently approximated at fighter `(pos_x, pos_y, z=0)` and ignores:
  - shield joint placement / per-character offsets (decomp: `ftColl_8007B1B8` stores `shield_hit.bone` + `shield_hit.offset`; refs/melee/src/melee/ft/ftcoll.c:1370-1383)
  - shield tilting / TransN-driven orientation (refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c)
  This will matter once SHIELD contacts gate real combat / shield pokes.

7) **Projectiles (lasers)**
- Spawn and integrate laser entities.
- Laser collision/hit application.

## What “Approximate” Means Here

Allowed approximations (v1):
- Do not match 1-ULP float results; use stable FP but prioritize consistent ordering.
- Very fine-grained mechanics (SDI/ASDI nuances, shield angle/pokes, etc.) may be deferred **only after** core behavior is already extremely close and the remaining work is dominated by tiny float/ordering details.

Disallowed approximations (v1):
- Replacing GALE01 action ids with coarse categories in the observation.
- Omitting ledge interaction entirely on FD.
- Omitting shield, hitlag, or hitstun (policies strongly depend on these).

## Data Extraction Requirements

### Inputs

- `SSBM.iso` (or extracted `_iso/` directory).

### Outputs (checked-in small JSON/binary; large assets ignored)

- `data/stages/final_destination.*`:
  - collision geometry (polylines/segments) + ledge metadata + blast zones
  - coordinate system definition (origin, units, axes) consistent across engine
- `data/characters/fox.*`, `data/characters/falco.*`:
  - per-action animation metadata (duration, keyframes, bones)
  - per-action hurtbox sets and hitbox sets per frame (or per keyframe interval)
  - movement parameters used by actions (where encoded in files)
- `data/common/*`:
  - physics constants and shared parameters

Extraction scripts must be deterministic and versioned:
- Same ISO → same extracted outputs (byte-for-byte) for a given extractor version.
- Any manual overrides go into a clearly separated overlay file (e.g. `data/overrides/*.json`).

## Simulation Model

### Coordinate system and units

- Adopt the game’s coordinate system (right-handed/left-handed as extracted).
- Store positions/velocities in `float32` or `float64` (configurable):
  - Default `float32` for throughput.
  - Enable `float64` for debugging/comparison if needed.
- All geometry extracted is expressed in this same coordinate space.

### Per-frame update order (must be fixed)

One frame of `step()` should be ordered deterministically. Proposed order:

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
3. Build the **same embedded observation vector** used by `slippi-ai` for both:
   - reference: from replay states (`slippi-ai` parsing/embedding)
   - sim: from simulator state mapped to the same schema
4. Compute metrics and regressions over the suite.

Notes:
- This is “teacher-forced” evaluation: it answers “is our one-step transition function correct on the support of real gameplay states?”
- Reseeding must be deterministic and should avoid “cheating” by copying fields that the simulator is supposed to derive (e.g., if we track derived timers, we should seed only what is observable/authoritative for that frame).

Known teacher-forcing limitations (must be tracked and eventually removed, not treated as “engine truth”):
Notes:
- Input-history tilt timers (`x670`/`x671`), TURN internals (`frames_to_turn`/`has_turned`), and KneeBend internals (`jump_input`/`is_short_hop`) are derived during preprocessing and are part of the seed schema.
- TURN seeding uses a causal derivation that does not look ahead to future facing flips; it includes a **deterministic assumption** that `ftCo_Turn_Anim_Inner` applies once on the entry frame (matching the sim’s update ordering). Do **not** tune this assumption via one-step mismatch metrics; revisit it once richer entry-history seeding lands.
- FallSpecial mode `mv.co.fallspecial.xC` is not present in Slippi post-frame data. We currently seed it with a **best-effort inference** from the reseeded state (default `xC=1`; set `xC=0` when `fall_fast==0` and reseeded `speed_y_self < -terminal_vel`). This is decomp-motivated (see `ftCo_FallSpecial_Phys` branch structure), but still an inference; do **not** tune it against one-step metrics.

Metrics (initial):
- `action_id` match rate (and optional ±N frame window around transitions).
- Position error (x/y): mean, 95p, max.
- Boolean exactness: `on_ground`, `facing`, `is_dead` (and optionally derived flags like `hurtbox_state != 0`).
- Discrete exactness: `jumps_left`, `stocks_left` (or tolerate rare off-by-1 early).
- Event alignment: stock loss within ±N frames; hit events within ±N frames (if detectable from replay).
- Items/projectiles (optional early): presence + kinematics on a fixed-capacity set of slots (stable ordering by instance id).

Acceptance for “v1 usable” should be expressed as thresholds on these metrics over a fixed suite.

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

## Milestones

1. **Scaffold**
   - Extract FD collision + Fox/Falco move/anim/hitbox data from ISO into stable files.
   - Implement batched core state + deterministic step loop skeleton.
2. **Locomotion parity (behavioral)**
   - Idle/walk/dash/run/turn, jump/fall/landing, fastfall, ledge grab/hang/getup.
3. **Combat core**
   - Hitboxes/hurtboxes from extracted data, hitlag/hitstun/knockback/DI, shield.
4. **Lasers**
   - Spawn/integrate/collide and apply damage/knockback.
5. **Evaluation harness**
   - Replay-driven observation parity + policy-consistency scoring with a fixed suite.
6. **Doubles enablement**
   - Turn on `num_players=4` with teams; verify invariants and performance.

## Coverage & Initialization Targets

- **Action coverage (v1)**: include every GALE01 action state that appears in the Fox/Falco FD validation suite (and keep this list current as the suite evolves).
- **Start state**: match the real match start state as closely as possible so we can seed a recurrent policy’s hidden state by replaying the exact match-start prefix it expects.

## Mechanics Inventory (prioritized backlog)

Maintain a running prioritized list of mechanics (the intent is to eventually support everything; ordering is for execution planning).
Add newly discovered mechanics here immediately (even if we’re not ready to implement them yet), and keep priorities updated as we learn what matters for suites/models.

Highest priority (policy-critical / always exercised):
- Input sampling + **UCF** (baseline) + optional “UCF 1.0 cardinals”
- UCF 0.84 pad-buffer emulation (stateful): per-port ring buffer + `sdrop_up_frames` are seedable (dashback/shielddrop/OoS fixes should consume this later without teacher-forcing hacks)
- Suite-driven action coverage: auto-extract GALE01 `action_id`s present in the suite and implement those first (keep the extracted set checked-in and updated)
- Match start + respawn/death: initial timers/flags, stocks decrement rules, blast zones, respawn platform + invulnerability windows (for stable reseeding + policy hidden-state warmup)
- Timer semantics: hitlag/hitstun/action_frame increment/skip rules; landing lag + IASA/interrupt gating; “frozen” vs “advancing” phases
- FD collision + ledges + blast zones (from stage files)
- ECB-like collision proxy and grounded/ledge gating (aim very close to real; only simplify as a last resort)
- Animation/subaction driving: per-action timeline + animation frame progression aligned to timers/interrupt rules
- Anim/script timebase (`anim_frame_f32`): seeded from Slippi post-frame `state_age` (fp->cur_anim_frame float). Today we maintain it approximately by applying integer `action_frame` deltas during `step()` and resetting to `0.0f` on action enters (does **not** yet model decomp `fp->frame_speed_mul` fractional carry / hitlag coupling). Negative/NaN is treated as `0.0f` for move-script sampling and pose indexing.
- Hurtbox/hitbox attachment to extracted animation/bone transforms (including TransN/root motion when applicable)
- Hitlag, hitstun, knockback, tumble, DI (and damage/percent application)
- Shield core (hp/decay, shieldstun, pushback) + out-of-shield options
- Lasers (projectile core)
- Grabs: grab boxes, hold, pummel, throws, throw trajectories/DI, mash-out rules
- Specials (Fox/Falco): shine, lasers, side-B, up-B (including key edge cases like shorten/firefox angles)

Medium priority (important but can follow once core is stable):
- SDI / ASDI / smash DI nuances
- Techs (in-place/roll), missed-tech bounces, jab resets, **Amsah tech** behavior near ledge
- Shield angling / poke behavior
- Edge cases around ledge intangibility windows and ledge regrab rules
- Exact invulnerability sources (respawn, ledge, moves)

Lower priority / domain expansion:
- Items beyond lasers
- More stages (platforms, slopes, Randall, moving collisions)
- More characters
- Doubles-specific mechanics (team hit rules, teammate collision nuances)
