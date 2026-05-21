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
- **Damage/KB writeback reshaping (BODY hits)**: fighter BODY, item BODY, and throw-release producers now feed a shared
  ProcessHit-shaped resolved-damage consumer in `src/combat.c`. Producers still own source-specific lanes (fighter/item/throw
  facing, item consume/persist, throw weight/DI, attached-victim suppression), while the consumer owns the common aftermath:
  no-KB cleanup, KB velocity/state entry, hitstun flags, hitlag post-entry flags, source lanes, stale queue, and combo
  bookkeeping. This is still incomplete until remaining producer inputs and exception families are fully decomp-owned, but
  the common mutation point is intentionally shaped around:
  `refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC`,
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0`,
  `refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}`,
  and `refs/melee/src/melee/it/itcoll.c::it_80272460`.
- **Residual frame-start HitCapsule identity**: when a fighter takes damage before the common
  BODY resolver reaches its own still-live HitCapsule in the same frame, fighter BODY producers
  use frame-start `fp->x2068/x206C` and frame-start fighter instance attribution for that residual
  capsule. This matches the source split where `ftColl_8007ABD0` creates/stales the HitCapsule
  before `Fighter_ProcessHit_8006D1EC` can rewrite the attack/source identity through a same-frame
  Damage* entry, and prevents reciprocal hits from becoming unstaled post-damage attacks.
  Covered by the CNM Yoshi's Story simultaneous Shine/up-smash lock in
  `tests/test_combat_attack_id_snapshot_replay_real_locks.py`.
- **Research note (2026-05-16, damage modifier/source shape)**: the decomp-faithful boundary is
  still the same producer/consumer split, not a generic post-hoc damage modifier pass. `ftColl`
  and item/throw producers populate pre-`Fighter_ProcessHit` accumulators (`x1838_percentTemp`,
  `x183C_applied`, `kb_applied`, element/facing/source lanes) and apply pre-hit modifiers such as
  staling before derived quantities are computed. `Fighter_ProcessHit_8006D1EC` then consumes the
  resolved lanes once, applies common aftermath, and clears the transient damage fields. Future
  modifier work should complete missing producer lanes or seedable internals, not add compensating
  runtime bridges after the consumer.
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
  - Replay-seeded shield-hit hitlag rows can carry accepted per-HitCapsule victims from the
    previous GuardSetOff onset. The proof is the replay-visible ShieldDesc admission lanes plus
    both-fighter hitlag; the seed is stamped onto the following frozen hitlag episode, not the
    pre-contact row.
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
  - EscapeF post-frame-20 BODY hurtcap collision uses the motion-entry root-facing lane
    (`facing_dir1`) after the extracted script emits `set_hit_status(0)` and
    `set_throw_flags(hit_idx=0)`, because `lb_8000B1CC` consumes the root JObj collision matrix
    rather than replay-visible scalar facing. EscapeB/EscapeN are not widened; their extracted
    scripts do not emit the same throw-flag event at the vulnerable boundary.
  - Not modeled yet: EscapeF/EscapeB root-motion (`fp->x6A4_transNOffset`) and the full
    `ftCheckThrowB3` facing/update path; we currently apply friction-only and use anim-end to
    return to Wait.
  - Not modeled yet: escape invincibility / hurtbox state changes during EscapeN/EscapeF/EscapeB.
- Shield-drop-through from GuardOn/Guard/GuardReflect follows the source IASA order:
  Guard defensive options run before `ftCo_8009A080` platform pass, but UCF 0.84's Axe-method
  shield-drop hook suppresses the shared EscapeN entry on rim-coordinate platform inputs so pass
  can win. Fresh non-shield-owned no-submotion GuardOn rows do not consume the same input callback
  again for pass. The pass gate uses Melee's synthesized `held_inputs & HSD_PAD_LR` lane, so
  analog trigger-held shield rows are eligible even without digital L/R bits. When `GuardSetOff_Anim`
  ends into Guard before input dispatch, the destination `Guard_IASA` can also consume this same
  platform-pass tail; `GuardSetOff_IASA` itself remains empty.
  (`src/action.c`, `src/locomotion.c`; refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  ftCo_GuardOn_IASA,ftCo_Guard_IASA,ftCo_GuardSetOff_Anim,ftCo_GuardReflect_IASA},
  refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A080,
  refs/melee/src/melee/ft/fighter.c:1868-1890, refs/ucf/src/shielddrop/shielddrop.S).
- GuardOn powershield re-entry uses the source `ftCo_80093694` owner: `mv.co.guard.x0`, the
  current digital L/R edge, and `x672_input_timer_counter` all gate `GuardOn -> GuardReflect`.
  For no-submotion GuardOn snapshots whose previous source state belongs to the generated
  `MSLMSO01` grounded-locomotion guard-entry IASA class, the simulator uses a transient
  frame-start x672 copy so fresh shield-entry rows with an analog trigger already held plus a fresh
  digital L/R edge do not advance the trigger timer before the GuardOn IASA gate. Landing,
  Ottotto, grounded attack, appeal, and steady shield-owned GuardOn snapshots keep the ordinary
  current x672 gate.
  (`src/action.c`, `src/input.c`, `src/locomotion.c`, `data/motion_state/owners/{fox,falco}.bin`;
  refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_IASA,ftCo_80093694,ftCo_8009388C},
  refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_IASA,
  refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::ftCo_Attack12_IASA,
  refs/melee/src/melee/ft/fighter.c::Fighter_Spaghetti_8006AD10).
- Shield HP drain/recharge ordering keeps Guard entry provenance transient to the immediate
  `GuardOn`/spotdodge callback window after entry. A frozen no-submotion `GuardOn` row can still
  use its frame-start lightshield owner for the current drain, but the `Wait_IASA -> GuardOn` entry
  marker is consumed after that short handoff window so it cannot stale-carry into later
  `GuardOn_IASA` decisions.
  Expired no-submotion `GuardReflect` terminal rows likewise drain from the frame-start
  GuardOn/Reflect snapshot before refreshing the stored lightshield owner for the following Guard
  row. If a catch connects after Guard* Anim drain and leaves the victim in `CapturePulled*`, the
  same post-frame shield recharge gate applies because the live ShieldDesc family has been exited
  before late `Fighter_ProcessHit`.
  (`src/action.c`, `src/grab_flow.c`;
  refs/melee/src/melee/ft/fighter.c::{Fighter_procUpdate,Fighter_UnkProcessGrab_8006CA5C,Fighter_ProcessHit_8006D1EC},
  refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800921DC,ftCo_800925A4,ftCo_GuardOn_IASA,ftCo_Guard_IASA,ftCo_GuardReflect_Anim},
  refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800DAADC).
- Shield bubble center is **data-driven** from ISO-derived shield-tilt tables and sim guard-tilt state.
  - Dash `x4` held-shield entry through `ftCo_Dash_IASA -> ftCo_80091AD8 -> ftCo_800923B4`
    installs `ShieldDesc` before item collision; same-step laser shield contact uses the live
    laser scaleZ endpoint lane and, without explicit hidden `ShieldBounced` seed provenance, takes
    the laser `HitShield` destroy path. Fresh Dash powershield `GuardReflect` remains on the
    ReflectDesc no-hit snapshot owner.
  Remaining approximations:
  - We do not yet model the full `shield_hit.bone` + `shield_hit.offset` semantics used in collision (`ftColl_8007B1B8`; refs/melee/src/melee/ft/ftcoll.c:1370-1383).
  - We do not yet model full 3D rotation/TransN/root-motion for shield placement (see `docs/SSANIM_AXIS_BASIS.md`).

7) **Projectiles (lasers)**
- Spawn and integrate laser entities.
- Laser collision/hit application.
- Item BODY hits are accumulated during the generic item collision pass, and Fox/Falco laser
  destruction is consumed later by the item `dmg_dealt` callback. A laser that BODY-hits one
  fighter therefore remains eligible to hit later fighters in the same item pass; those later
  contacts use the same pre-pass `it_80272460` staled HitCapsule.damage and replace older hitlag
  with the fresh `Fighter_ProcessHit` item-hitlag value. Per-HitCapsule victims_1 hitlists still
  suppress already-recorded item/fighter victims.
  (`src/items.c`, `src/combat.c`; refs/melee/src/melee/it/item.c::{Item_80269C5C,Item_8026A294,OnGiveDamageThink},
  refs/melee/src/melee/it/itcoll.c::{it_802706D0,it_8026FAC4,it_80272460},
  refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC,
  refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688).
- Fox DownBack terminal laser BODY uses the `ftColl_8007925C -> lbColl_8000805C` hurt-radius
  release-edge lane when the extracted hit-status table shows the current DownBack pose frame has
  just released a nonzero x1988 window. Ordinary grounded vulnerable rows, shield defensive
  options, PassiveStand, and Falco DownBack controls stay on the exact x58/x4C matrix/local-radius
  lane instead of borrowing the release-edge broadphase.
  (`src/items.c`; refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C,
  refs/melee/src/melee/ft/chara/ftCommon/ftCo_Down.c::ftCo_Down_Coll,
  refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58,lbColl_804D7A38};
  data/hurtcaps/{fox,falco}.bin, data/scripts/{fox,falco}.bin).
- Late aerial Blaster Loop terminal rows use the raw script `cmd_vars[0]` window from MSLFTSC1 as
  the BODY-contact boundary. While `ftFx_SpecialAirNLoop_IASA` still has cmd0 active, laser phantom
  and full BODY contacts remain eligible; after the script clear, terminal no-repeat
  `SpecialAirNLoop` rows reject same-family laser BODY overlap until the action transitions or
  lands. `data/items/lasers.bin` v5 also exposes the article hitbox `x138` mask for the separate
  `gm_8016B1C4` item-hitbox gate, but standard validation rows do not enable that game-rule path.
  (`src/items.c`, `src/move_tables.c`, `data/items/lasers.bin`;
  refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
  ftFx_SpecialAirNLoop_IASA,ftFx_SpecialAirNLoop_Coll},
  refs/melee/src/melee/ft/ftaction.c::ftAction_80071820,
  refs/melee/src/melee/it/it_2725.c::it_802790C0,
  refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C).
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

- `data/**` is treated as generated and is gitignored by default, with explicit tracked
  exceptions for tiny runtime-required contract files such as `data/common/ft_common_data.json`
  and `data/items/item_common.json`.
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

One frame of `step()` must be ordered deterministically. Public callers enter through `src/step.c`,
but the canonical phase scheduler lives in `src/fighter_callbacks.c` (see also
`docs/DECOMP_PROC_ORDER.md` for decomp context). The scheduler is intentionally shaped around the
fighter proc/callback order in `refs/melee/src/melee/ft/fighter.c`:

1. Frame-begin caches / transient clears.
2. Pre-input Anim phase:
   - install the prior-frame input snapshot for callback owners that run before current input,
   - tick timers and post-hitlag callbacks in the `Fighter_8006A1BC` / `Fighter_8006A360` window,
   - advance the animation timebase,
   - run modeled per-fighter Anim callbacks through `MslFighterCallbackContext`,
   - run global pre-input owners and ProcessHit-style cleanup that belongs after prio-1 callbacks.
3. Input phase: apply current inputs/UCF and consume after-input hitlag callbacks.
4. IASA phase: run match-flow and per-action IASA/state-transition owners.
5. Phys phase: run fighter-owned item spawn hooks, camera-target flags, and velocity/root integration.
6. Collision phase: run attachment, stage/mpColl, ledge, and post-collision action owners.
7. Primitive refresh phase: apply deferred animation ticks, dynamic pose state, shields, hurtboxes, and hitboxes.
8. Item collision / combat phase: tick hitlists, refresh defensive primitives, update item collision,
   resolve combat when enabled, then run post-combat item/knockdown owners.
9. Post-frame phase: apply deferred post-combat ticks, publish output-facing flags/timers, promote
   previous-frame seed lanes, and clear one-step seed-owned transients.

The wrapper in `src/step.c` must stay thin. New callback-order work should extend the scheduler
or subsystem callback bodies, not recreate a parallel frame order.

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
- Collision/combat works for all pairwise interactions `p != q`. The simulator teams domain assumes
  Team Attack is on, so `is_teams`/`team_id` are match-flow and observation lanes, not fighter
  hit/grab/shield eligibility filters. Four-player validation suites must declare
  `"team_attack_on": true`, and preprocessing rejects 4-player teams replays whose Slippi start
  bitfield does not prove Team Attack ON.
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
- Animation rate (`frame_speed_mul_f32`) is not exposed by Slippi post-frames. We seed it via a strictly-causal derivation during preprocessing; the C core currently models decomp-backed rate changes for Landing* actions (LandingAir* / LandingFallSpecial), including x67F L-cancel lag division from hitlag-latched LR edges, causal GuardSetOff entry shieldstun (`ftCo_80092F2C`) reconstruction, and the hitlag freeze gate. GuardSetOff last-hitlag rows that require the hidden `x19A4/lightshield_amount` owner exposed only after hitlag use the explicit non-causal `guard_setoff_exit_frame_speed_mul_f32` lane instead of weakening the `frame_speed_mul_f32` contract.
- TURN seeding uses a causal derivation that does not look ahead to future facing flips; it includes a **deterministic assumption** that `ftCo_Turn_Anim_Inner` does **not** tick on the entry frame (matching the sim’s current update ordering: the Turn flip tick only runs if Turn was already active at frame start). Do **not** tune this assumption via one-step mismatch metrics; revisit it once richer entry-history seeding lands.
- FallSpecial mode `mv.co.fallspecial.xC` is not present in Slippi post-frame data. We currently seed it with a **best-effort inference** from the reseeded state (default `xC=1`; set `xC=0` when `fall_fast==0` and reseeded `speed_y_self < -terminal_vel`). This is decomp-motivated (see `ftCo_FallSpecial_Phys` branch structure), but still an inference; do **not** tune it against one-step metrics.
- FallSpecial is a common airborne locomotion state for physics/collision, but it does **not** own the generic JumpAerial IASA callback. Runtime must not allow double-jump entry from `FallSpecial/FallSpecialF/FallSpecialB`, even if `jumps_left` remains nonzero after EscapeAir/air-dodge handoff (`src/locomotion.c`; refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c, refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_800CB870).
- Grabbed/thrown victim attachment offsets `grab_offset_{y,z}` map to `fp->x1A70.{y,z}`. For non-low `CaptureWait* -> ThrownF/B/Hi` entry, runtime now initializes the offsets from the victim's static `TransN - XRotN` pose analog before applying the thrown accessory anchor; decomp initializes `fp->x1A70` from bones and `ftCo_800DE508` applies it during `Thrown*` (`src/grab_attachment.c`, `src/grab_flow.c`; refs/melee/src/melee/ft/fighter.c::Fighter_UnkUpdateVecFromBones_8006876C, refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_800DE3FC,ftCo_800DE508}). Reseed-inferred offsets remain the fallback outside the direct source-backed entry slice.
- CaptureWaitHi/Lw mash rate is now owned live in the shared grab-flow runtime. Decomp `ftCo_CaptureWaitHi_Anim` writes `ftAnim_SetAnimRate(x3B4)` when `ftCommon_GrabMash(..., x3A8)` succeeds, uses `x3B0` as the hold timer before returning to rate `1.0`, and decrements the shared grab timer / counter in the same callback (`src/grab_flow.c`, `data/common/ft_common_data.json`).
- CaptureWaitHi/Lw jump latch uses the IASA edge lane, not held buttons. `fn_800DC014` checks `fp->input.x668 & HSD_PAD_XY`, so runtime writes `capture_wait_jump_latch` from `input_buttons_pressed` during the narrow `capture_wait_counter < capture_wait_jump_latch_window_frames` window. This does not replace the separate first-steady CaptureWait ownership bridge below (`src/grab_flow.c`; refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::fn_800DC014).
- First-steady `CaptureWait` ownership no longer uses either cross-row rate carry or a replay-shaped phase bit. Preprocessing causally seeds only the shared hidden owner lanes that persist across frames (`grab_timer`, `capturewait.x0`, `capturewait.x4`, the deferred jump latch, and the breakout-resolve bit); runtime derives the first-steady extra victim tick directly from replay-visible owner/victim slot ordering, frame-start `CaptureWait af=1 <- af=0` continuity, and current-frame `ftCommon_GrabMash` activity in the shared CatchPull/CatchWait/CatchAttack/CaptureWait path (`tools/slippi/seed_history.py`, `tools/slippi/make_dataset_from_slp.py`, `src/api.c`, `src/grab_flow.c`; decomp: `ftCo_Attack100.c::{ftCo_CaptureWaitHi_Anim,ftCommon_GrabMash,ftCo_800DA698,ftCo_CaptureCut_Enter,fn_800DA1D8,ftCo_CaptureWaitHi_Coll,fn_800DBAC4,fn_800DBBF8}`).
- Catch selection must honor the common x1A6A/x1A68 target mask before capsule overlap. `Catch`/`CatchDash` entry installs attacker `x1A68=1` through `ftCommon_8007E2D0`; `DownBound` installs victim `x1A6A=0x1FF`, while `DownBound/DownDamage -> DownWait` installs `x1A6A=1` through `ftCommon_8007E2F4`. Since `ftColl_80078A2C` rejects `(victim.x1A6A & attacker.x1A68) != 0`, DownBound/DownWait/DownDamage victims are not grabbable even if their ordinary grabbable hurt capsules overlap the catch bubble (`src/combat.c`; refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D8C54, refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::{ftCo_8009794C,ftCo_80097E8C,ftCo_80097F38}, refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::ftCo_8009F184, refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C).
- Catch target selection uses the collision-skeleton HitCapsule point consumed by `lbColl_80007ECC`, not a second enlarged pose-space center. For enlarged models, runtime removes the generic pose-space `model_scaling` expansion before catch-only narrowphase; it does not extend catch reach for smaller models. A rejected all-hitbox/all-scale correction regressed validation and remains part of the broader hitbox geometry owner, not catch selection (`src/combat.c`; refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078A2C,ftColl_8007AD18}, refs/melee/src/melee/lb/lbcollision.c::lbColl_80007ECC, refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC).
- Catch target selection also runs the source wall-obstruction gate after target-mask / vulnerable checks and before selecting the nearest victim. `ft_80084CE4` tests the segment between attacker and victim ECB midpoints against the side-specific wall graph selected by relative X (`mpCheckLeftWall` or `mpCheckRightWall`); runtime consumes the extracted MSLSTG01 wall graph and fighter-solid line metadata rather than replay row ids. This is neutral on Final Destination's current catch rows but closes the shared narrowphase owner for wall-separated grab attempts (`src/combat.c`; refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C, refs/melee/src/melee/ft/ft_081B.c::ft_80084CE4, refs/melee/src/melee/mp/mplib.c::{mpCheckLeftWall,mpCheckRightWall}, `data/stages/bin/*.bin::MSLSTG01`).
- Rapid-jab entry is owned by the shared Attack11/12/13 IASA pre-gate, not by a Fox-specific row rule. `ftCo_Attack_800D6A50` increments `fp+0x1A54` on A press/release edges, compares it against `ftCo_DatAttrs.x98` (`data/characters/{fox,falco}.json::rapid_jab_window`), and enters `Attack100Start` only after the move script has set x2218_b2. `ftCo_800D6B00` immediately calls `ftAnim_8006EBA4`, so `Attack100Start` first serializes at frame 1. `Attack100Start/Loop/End` Anim callbacks run before the Loop IASA callback: `Attack100Loop_Anim` consumes script `set_throw_flags(hit_idx=0)` checkpoints before current-frame A can set `mv.co.attack100.x4`, and the latch only affects the next checkpoint (`src/locomotion.c`, `src/move_tables.c`; refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::{checkAttack11,ftCo_Attack11_IASA,ftCo_Attack12_IASA,ftCo_Attack13_IASA}, refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_800D6B00,ftCo_Attack_800D6A50,ftCo_Attack100Start_Anim,ftCo_Attack100Loop_IASA,ftCo_Attack100Loop_Anim,ftCo_Attack100End_Anim}, refs/melee/src/melee/ft/types.h::ftCo_DatAttrs).
- Repeated-hit rapid-jab drift uses the shared combo push timer, not root motion or a jab-specific velocity rule. `ftColl_800763C0` arms `fp->x2092` from common-data `x4D8` once repeated same-attack `combo_count` reaches `x4C4`; `ftColl_80076528` then decrements the timer and applies `comboCount_Push` along the floor normal using `x4C8/x4D0/x4D4`. Slippi exposes the low byte of `fp->x2090` but not `x2092`, so one-step seeds reconstruct `combo_push_timer_x2092` causally from combo-count increments while rollout/runtime arms it directly on accepted hits (`src/combat.c`, `src/physics.c`, `tools/slippi/combo_history.py`; refs/melee/src/melee/ft/ftcoll.c::{ftColl_800763C0,ftColl_80076528}, `data/common/ft_common_data.json`).
- Attack100Loop restart also refreshes attack identity. On the restart band (`cur_anim_frame >= 0 && cur_anim_frame < frame_speed_mul`), `ftCo_Attack100Loop_Anim` calls `ft_800892A0`, which bumps `x206C_attack_instance` for the current same move id before new loop hitboxes are interpreted. This lets repeated rapid-jab hits enter the stale queue as separate same-move instances and prevents over-damage in long rapid-jab strings (`src/attack_identity.c`, `src/locomotion.c`; refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Attack100Loop_Anim, refs/melee/src/melee/ft/ft_0881.c::{ft_800892A0,inlineC0}, refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromFighter).
- CapturePulledHi/CaptureWaitHi -> CaptureWaitLw floor handoff is owned by the decomp floor-mask path: `CaptureWaitHi_Coll -> ft_80083C00/ft_80082578 -> mpColl_800477E0 -> fn_800DBAC4/fn_800DBBF8 -> ftCommon_8007D7FC/ftCommon_8007D6A4`. Runtime enters `CaptureWaitLw` and resets `jumps_left` only when the reconstructed `mpColl_800477E0` floor-mask probe succeeds. This replaces the rejected character-id / stale-ground proxy; generic capture/mpColl code must not use a character branch to stand in for missing floor-contact provenance.
- Grounded low-capture collision (`CapturePulledLw`, `CaptureWaitLw`, `CaptureDamageLw`) uses the allow-ground-to-air floor wrapper after the `fn_800DAD18` attachment delta: `*_Coll -> ft_8008403C -> ft_80082708 -> mpColl_8004B108`. Runtime therefore allows this narrow owner to project the victim back down to the floor after the capture Phys delta; the generic grounded anti-snap rule remains in place for unrelated actions (`src/mpcoll_ground.c`; refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_CapturePulledLw_Coll,ftCo_CaptureWaitLw_Coll,ftCo_CaptureDamageLw_Coll}, refs/melee/src/melee/ft/ft_081B.c::{ft_8008403C,ft_80082708}).
- Same-frame catch-connect can run the victim's current action callback before the grab owner's connect callback, then immediately run the captured victim's collision callback before the first replay-visible captured frame. For QGD's CatchDash row, vanilla runs victim `KneeBend -> JumpF -> AttackAirHi`, owner `CatchDash -> CatchDashPull`, then victim `CapturePulledHi -> CapturePulledLw`; `fn_800DAADC` calls the victim `fp+0x21A8` callback, so `ftCo_CapturePulledHi_Coll -> ft_80083C00/mpColl_800477E0 -> fn_800DAECC/fn_800DAEEC` can consume a same-frame floor mask. Runtime accepts either the normal ECB floor-mask probe or a locked CollData floor id (`ground_id` plus `ecb_lock_timer`) for this immediate capture-root floor callback; DamageFly-family pre/prev/seed-prev actions are excluded because their stale floor id remains owned by `DamageFly_Coll` until capture entry. The older startup-complete `KneeBend -> AttackAirHi` action-history bridge is removed; retained runtime requires the source-shaped locked CollData floor owner instead of action-pair provenance. Grounded `CapturePulledLw` catch-connect entry does not run the newly-entered Lw Phys/Coll in that catch callback; steady Lw deltas begin on the next `Fighter_procUpdate` pass (`src/locomotion.c`, `src/grab_flow.c`, `src/mpcoll_ground.c`; refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_UnkProcessGrab_8006CA5C}, refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Anim, refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll, refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_CapturePulledHi_Coll,ftCo_CapturePulledLw_Phys,ftCo_CapturePulledLw_Coll,fn_800DAADC,fn_800DAECC,fn_800DAEEC}, refs/melee/src/melee/ft/ft_081B.c::{ft_8008403C,ft_80083C00}, refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B108,mpColl_800477E0}).
- Pass/shield-drop floor skip remains active if catch-connect captures the fighter before the
  same-frame `CapturePulledHi_Coll -> mpColl_800477E0` floor-mask callback. `ftCo_8009A184` /
  `ftCo_8009A228` call `mpUpdateFloorSkip` after entering Pass, and `mpColl_800477E0` passes that
  `CollData.floor_skip` into the floor query. Runtime therefore treats replay-prefix `Pass`
  rows with a carried platform/height-transform floor id as a floor-skip owner for the
  capture-root fallback too, so FoD moving platforms are skipped and the lower fighter-solid floor
  can own the immediate `CapturePulledLw` landing (`src/mpcoll_ground.c`;
  refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::{ftCo_8009A184,ftCo_8009A228},
  refs/melee/src/melee/mp/mpcoll.c::{mpUpdateFloorSkip,mpColl_800477E0}).
- Catch-connect also retires the grabbed victim's pre-connect outgoing hitboxes before the later common BODY collision pass. The vanilla proc order runs `Fighter_UnkProcessGrab_8006CA5C` at priority `0xC`, calls `ftColl_80078A2C`, then `ftColl_80078754` and the owner's `grab_cb` / victim's `grabbed_cb`; common fighter collision (`Fighter_8006CB94 -> ftColl_80078C70`) runs afterward at priority `0xD`. Runtime therefore clears already-refreshed victim hitboxes on catch connect so a captured victim's stale Attack* capsules cannot still damage the grab owner later in the same frame (`src/grab_flow.c`; refs/melee/src/melee/ft/fighter.c::{Fighter_UnkProcessGrab_8006CA5C,Fighter_8006CB94}, refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078A2C,ftColl_80078754,ftColl_80078C70}, refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::fn_800DAADC).
- Grabbed/thrown victim anchor still uses a suite-focused proxy outside the proven entry subsets. For non-low `CaptureWait* -> ThrownF/B/Hi` entry, the stale pre-entry world-offset bridge is intentionally not used; placement is attachment-owned from static `x1A70` plus the current owner anchor.
- `ThrownF/B/Hi` attached victim placement samples the owner anchor through the float AObj/JObj track path before applying the static `x1A70` residual. This matches the `HSD_AObjInterpretAnim -> lb_8000B1CC -> ftCo_800DE508` owner on fractional ThrowHi/ThrowB frames; integer SSANIM01 interpolation drifts on GAT's attached window and release placement. The predicate is explicitly limited to `ThrownF/B/Hi`; `CaptureWait*`/`CapturePulled*` and `ThrownLw` remain on their separate paths. Broadening this float-track anchor into low throw moved QGD contact/hitlag timing earlier and was rejected. On non-low release, `ftCo_800DD724 -> ftCo_800DDDE4` samples the post-advance float JObj pose, but same-script-frame ThrowB flip/release uses the pre-flip pose-facing snapshot because the JObj has already been interpreted before the throw flags are consumed; the scalar facing still flips for post-frame state (`src/grab_attachment.c`, `src/throw_flow.c`; refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508, refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD724,ftCo_800DDDE4}, refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim, refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC, `data/moves/{fox,falco}.json` `ftCo_SM_ThrowB` set_throw_flags).
- `CatchDash_Phys` calls `ft_80085030` with the same `p_ftCommonData->x64 * co_attrs.gr_friction` scalar used by the catch family. Extracted motion-state data for `ftCo_SM_CatchDash` has `x10_animCurrFlags` bit0 clear (`fp->x594_b0 == false`), so this source path takes `ft_80085030`'s friction fallback; a rejected TransN-present proxy regressed one-step float X error because it admitted a root-motion branch the motion data does not enable. `Catch`/`CatchDash` collision callbacks are explicit floor-loss owners: `ftCo_Catch_Coll`/`ftCo_CatchDash_Coll -> ft_800841B8(..., fn_800D8E30) -> ftCo_Fall_Enter`. Together these prevent modelplay jump-cancel grab / dash-grab states from carrying stale Dash slide into offstage airborne Wait (`src/physics.c`, `src/locomotion.c`; refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_CatchDash_Phys,ftCo_Catch_Coll,ftCo_CatchDash_Coll,fn_800D8E30}, refs/melee/src/melee/ft/ft_081B.c::{ft_80085030,ft_800841B8}, refs/melee/src/melee/ft/fighter.c::Fighter_ActionStateChange_800693AC, `data/anims/{fox,falco}.tracks.bin` / `Pl{Fx,Fc}.dat` `ftCo_SM_CatchDash` flags).
- Grounded floor-loss `ftCo_Fall_Enter` preserves and clamps `fp->self_vel.x`; it does not copy
  the current `fp->gr_vel` scalar into air velocity. This matters when a same-frame grounded
  callback has already updated `gr_vel` for the post-frame state before collision reports no floor:
  `ftCo_Fall_Enter` clamps `self_vel.x`, then `ftCommon_8007D5D4` clears `gr_vel`. Runtime therefore
  initializes floor-loss Fall from the self-velocity lane. The Dream Land left-ledge dash-dance
  probe (`manual_repros/set11/dreamland_dash_dance_left_ledge.json`, locked by
  `tests/test_modelplay_set11_manual_repros.py`) shows vanilla continuing down-left in Fall instead
  of taking the sim's old inward post-Phys ground speed and relanding on the ledge floor (`src/locomotion.c`;
  refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter,
  refs/melee/src/melee/ft/ftcommon.c::{ftCommon_ClampAirDrift,ftCommon_8007D5D4}).
- Platform-drop `Pass` remains a cliff-catch-capable airborne collision owner. `ftCo_Pass_Coll`
  routes through `ft_80082F28`, which runs `ftCliffCommon_80081298` after airborne collision; holding
  down still rejects the catch inside the cliff common gate, but releasing down before the ledge
  window allows `Pass -> CliffCatch` without first waiting for `Pass_Anim -> Fall`. The Yoshi's Story
  platform-drop manual repro is locked with both release-positive and hold-down-negative coverage in
  `tests/test_modelplay_set11_manual_repros.py` (`src/ledge.c`;
  refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_Pass_Coll,
  refs/melee/src/melee/ft/ft_081B.c::ft_80082F28,
  refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081298).
- Fastfall has no decomp owner for an up-stick "cancel" while staying in the same airborne motion:
  `ftCommon_CheckFallFast` only latches `fp->fall_fast`, and `ft_80084DB0` keeps applying
  `ftCommon_FallFast` while the bit is set. The bit clears through motion-state changes that do not
  carry `Ft_MF_KeepFastFall`, not from later up input alone (`src/physics.c`;
  refs/melee/src/melee/ft/ftcommon.c::{ftCommon_CheckFallFast,ftCommon_FallFast},
  refs/melee/src/melee/ft/ft_081B.c::ft_80084DB0).
- Throw-side blaster pulses recompute the launch vector for every `throw_flags_b0` pulse from `ftFx_SpecialN_FtGetHoldJoint` and `ftFx_SpecialN_ItGetHoldJoint`, then call `it_8029C6CC`. The sim therefore samples the current float-pose hold-joint vector for ThrowB/Hi/Lw shots and does not reuse the latest live shot velocity for later ThrowHi pulses. ThrowB has a separate root-facing nuance: `ftCo_800DD724` flips scalar `facing_dir` at release, but does not re-enter the motion state or reinstall root JObj Y rotation, so later ThrowB laser joint sampling uses the motion-entry `facing_dir1` root pose while gameplay scalar facing remains flipped. The old latest-shot velocity bridge was a stale proxy after float-pose sampling landed: in the GAT ThrowHi rollout it made frame-20/24 reuse the frame-18 right/up vector, while source/replay rotate the gun between pulses. A modelplay-rerun19 ThrowB current-position experiment was rejected: although `it_8029C504` computes `spawn.pos` through `it_8026BB68`, `Item_80268B18` initializes `item->pos` from `spawn.prev_pos`, while `spawn.pos` is copied into `item->xDD4_itemVar.foxlaser.pos`; promoting `spawn.pos` into replay-visible `item_pos` regressed one-step float/error reports and is not retained without a dedicated item-var/previous-position lane. Falco ThrowLw's slower frame-25 post-hitlag pulse shares the BODY callback predicate and must recompose the integer SSANIM01 `TransN` tail for the state1 hold-joint position; the faster Fox-victim control remains on the existing float hold-joint path. Replay-real locks cover the GAT ThrowB root-facing positive, the GAT frame-20/24 ThrowHi rollout positive, PRH's frame-25 ThrowLw state1 height, and the existing AGG/GAT/QGD/TBK one-step controls (`src/items.c`; refs/melee/src/melee/ft/fighter.c::{Fighter_ChangeMotionState,ftPartSetRotY,Fighter_8006A1BC,Fighter_8006A360}, refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD724,ftCo_800DD4B0}, refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{ftFx_SpecialN_FtGetHoldJoint,ftFx_SpecialN_ItGetHoldJoint,ftFx_Throw_Anim}, refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C504}, refs/melee/src/melee/it/item.c::Item_80268B18, `data/moves/{fox,falco}.json` ThrowHi/ThrowB/ThrowLw `set_throw_spawn_projectile` events, `data/anims/{fox,falco}.bin` TransN tail).
- Falco ThrowHi has a command-cursor carry split after the frame-18 pulse on 4/3 victim-weight
  throw-rate rows: the frame-20 `set_throw_spawn_projectile` command can be reached by
  `ftAction_80073354` without serializing the state1 article until the following Anim callback.
  Rollout reseed reconstructs this as internal `throw_command_deferred_pulse_frame` from causal
  seed state: extracted pulse order, `throw_pulse_crossed_prev_frame`, current rate, exactly one
  live state1 Falco laser, and same-source victim provenance. The retained runtime shape is not
  stage-gated; 1.25x crossed-prev frame-18 rows continue to serialize the second article in the
  current callback, while 4/3 rows record frame-20 provenance and spawn from crossed-prev frame 20
  on the next callback. The same runtime command split also covers Fox/Falco 4/3 rows where the
  frame-18 article was consumed immediately and only same-source victim provenance / combo state
  remains; the fighter timebase may still snap to action frame 20 on the source callback, but the
  article pulse waits for the following `ftFx_Throw_Anim` consume (`src/api.c`, `src/items.c`,
  `src/state.{c,h}`, `src/fighter_callbacks.c`, `src/anim_timebase.c`;
  refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354},
  refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim,
  refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD4B0,ftCo_800DD724},
  `data/moves/{fox,falco}.json` `ftCo_SM_ThrowHi` events).
- `ThrowLw/ThrownLw` attached placement now applies the shared decomp `ftCo_800DB368 -> ftCo_800DE508` owner for the supported Fox/Falco low-throw window: resolve the thrower's `FtPart_TransN2` constraint through the extracted part table (`data/characters/{fox,falco}.json::grab_capture_anchor_part_id`), then apply the victim static `x1A70` local offset. Fractional owner frames sample the live HSD AObj/JObj local-SRT track (`HSD_AObjInterpretAnim -> lb_8000B1CC`); exact integer frames hit the SSANIM01 matrix fast path where root `TransN` is stored as a stripped tail, so only those frames explicitly recompose `data/anims/{fox,falco}.bin` TransN. This replaces the older frame-25/rate-only vertical slice and the stale reseed `grab_offset` bridge for `ThrownLw`. The low-throw path intentionally does **not** use a broad collision-pose shortcut: the retained split is the narrow data-contract boundary between live float local tracks and stripped integer SSANIM matrices. Entry is an immediate owner too: `ftCo_800DE3FC` calls `ftCo_800DB368`, installs `ftCo_800DE508`, and immediately ticks the thrown victim, so the first replay-visible `ThrownLw` frame is already placed on the attachment anchor rather than preserving `CaptureWaitLw` root position. The periodic attached-position update is gated by the victim's post-decrement hitlag latch because `ftCo_800DE508` is an accessory1 callback and `Fighter_CallAcessoryCallbacks_8006C624` returns early under `x2219_b5` hitlag, running only accessory3; release keeps its separate immediate owner. Replay-real positives cover PRH/FSP/QGD low-throw placement, QGD/PEC/PRH entry handoff, and PRH active-hitlag freeze, while QGD guards that the retained path does not reintroduce the earlier false hitlag/contact timing. Sources: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800DB368`, `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508`, `refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim`, `refs/melee/src/melee/ft/fighter.c::Fighter_CallAcessoryCallbacks_8006C624`, `refs/melee/src/melee/ft/ftparts.c::ftParts_GetBoneIndex`, `refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC`, `data/anims/{fox,falco}.bin` TransN tail.
- Throw-release damage velocity has a same-callback owner: after throw KB is installed, `ftCo_800DD724 -> ftCo_800DDDE4 -> ftCo_800DE7C0 -> ftCo_8008E5A4` applies DI before victim damage physics. Because `ftCo_800DD724` is reached from the Throw Anim callback before `Fighter_procUpdate` installs current input, the simulator now consumes throw release in the pre-input Anim callback phase; the detached `Fall` row used by the remaining item-ordering bridge is not an actionable `Fall_IASA` source, and `ThrownF/B/Hi/Lw` IASA remains empty. The deferred simulator damage path reads the preserved pre-input stick lane when applying immediate DI in `combat_apply_throw_hit_core`; the L/R `x1AC` multiplier is not applied on this no-hitlag path because it belongs to `ftCo_Damage_OnExitHitlag`. Deferred release rows keep the temporary Fall placeholder non-physical while the throw hit is pending, then apply same-frame airborne Fighter knockback decay and Damage* gravity before integrating release displacement; this matches the source order where release immediately enters Damage before the victim Phys callback, without giving the placeholder a generic Fall drift frame. A fully immediate throw-damage migration is intentionally not retained until the item scheduler can represent the decomp projectile preemption rows directly; applying throw damage before the current `items_update()` pass creates hard validation reds by bypassing same-frame throw-side laser/item ownership. Throw hit capsule float damage is created by `set_throw_hitbox` before later same-instance low-throw laser contacts can stale-queue the shared throw attack instance, so deferred throw-release damage excludes same-instance stale entries while still honoring prior instances of the same move. Release-local floor contact is probed after deferred placement with the same `ftCo_800DDDE4 -> mpColl_800471F8 -> DamageFly_Coll -> ftCo_80090184` owner; when it succeeds, DownBound/Passive entry projects remaining KB onto the grounded floor tangent through `ftCommon_8007CCE8`, clearing vertical KB on flat FD floors. The next DamageFly frame after low-throw release may still carry an ECB-lock countdown, but its floor sweep uses the live DamageFly ECB bottom rather than the generic ground-to-air zero-bottom approximation; the zero-bottom lock remains for active-hitlag ground-to-air damage rows. The FSP low-throw release now reaches DownBound and raw +1.0 throw damage in-frame; the remaining ~0.068 X residual is still the constrained release snapshot, not floor/stale ownership (`src/combat.c`, `src/throw_flow.c`, `src/physics.c`, `src/knockdown.c`, `src/mpcoll_ground.c`; refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD724,ftCo_800DDDE4}, refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_ThrownF_IASA,ftCo_ThrownB_IASA,ftCo_ThrownHi_IASA,ftCo_ThrownLw_IASA,ftCo_800DE7C0}, refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_8008E5A4,ftCo_DamageFly_Coll,ftCo_80090184,ftCo_DamageFly_Phys,ftCo_Damage_Phys,ftCo_Damage_OnExitHitlag}, refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}, refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_8009794C, refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007CCE8, refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_LoadECB_JObj}, refs/melee/build/GALE01/asm/melee/ft/ftaction.s::ftAction_80071E04, refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_8007ABD0).
- `ThrowHi/ThrownHi` release timing keeps the shared `ftCo_800DD4B0` throw anim rate alive while the live attached owner/victim pair exists, and the thrower can continue carrying that 4/3 AObj rate after release while throw-side blaster commands remain in the script. The owner and victim both enter through `ftCo_800DD398` / `ftCo_800DE3FC` with the same rate and immediate `ftAnim_8006EBA4` tick; for Fox/Falco's data-backed 4/3 rate, repeated Q16.16 advances can land one LSB below an integer and delay integer script-frame observations by a rollout frame. Runtime snaps only that one-LSB boundary on source-owned ThrowB/ThrowHi release/projectile command frames, while `src/items.c` separately owns whether a snapped ThrowHi frame-20 command serializes an article in the current or following callback. Broader all-throw rate restoration remains rejected because it regressed ThrowF release float locks (`src/anim_timebase.c`, `src/grab_flow.c`, `src/items.c`; refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD4B0,ftCo_800DD398,ftCo_800DD724}, refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE3FC).
- Attached `Thrown*` victims now use one shared attachment-owned collision/release substrate by default: attached `ThrownF/B/Hi/Lw` rows no longer let generic stage collision own grounded / wall / ledge state during the attached window, and release-frame victims use the same shared pending-release collision suppression before later throw-hit / item resolution (`src/grab_attachment.c`, `src/mpcoll_ground.c`, `src/mpcoll_env.c`, `src/mpcoll_wall_ceil.c`, `src/throw_flow.c`). The remaining throw gaps are no longer in generic attached/release substrate or common release callback ownership; they are the decomp-justified per-throw pulse/article differences in `src/items.c` / `src/combat.c`, plus ThrowLw's attached pulse-25 post-hitlag anim-rate owner in `src/anim_timebase.c` that belongs to the same `ftFx_Throw_Anim` pulse family rather than shared throw core.

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
- AttackS3 angle-variant hitbox commands (2026-04-26): `AttackS3Hi`, `AttackS3HiS`,
  `AttackS3LwS`, and `AttackS3Lw` keep their live angled submotion pose for matrix sampling, but
  missing hitbox command-event lookups fall back to the extracted common `ftCo_SM_AttackS3`
  command list. Source basis: the `ftmotionstates.c` AttackS3* entries all use
  `ftCo_AttackS3_{Anim,IASA,Phys,Coll}`, while `data/moves/{fox,falco}.json` currently stores the
  shared side-tilt hitbox script under `moves["ftCo_SM_AttackS3"].events`. This is an event-table
  alias only; it is not a BODY admission shortcut and still uses the normal pose/hurtcap overlap
  selector.
- Core combat/contact continuation pass (2026-04-24): retained decomp-backed runtime/seed cleanup
  for the next residual block. Common airborne `Damage_IASA` and grounded DamageHi/N/Lw
  `Damage_IASA` use the live
  `mv.co.damage.x14` jump-buffer snapshot with XY and hitlag-gated tap-jump seed producers
  (`ftCo_8008F744`, `doIasa`, `ftCo_Damage_IASA`, `ftCo_Jump_GetInput`), including grounded
  hitstun-lockout rows that later inject XY before the post-hitstun `Wait_IASA` delegate. The
  tap-jump producer uses the full x671 `< p_ftCommonData->x74` window, not only the x671==0 edge,
  matching `ftCo_Jump_GetInput`; Damage hitlag now carries the `ftCo_8008DCE0` damage-entry and
  `ftCo_Damage_OnEveryHitlag` SDI-consume x670/x671 resets into replay seeds, inferred from action
  entry and prior hitlag SDI displacement so adjacent teacher-forced rows do not reuse stale timer
  windows. First active-hitlag rows also allow the narrow callback-local SDI-radius crossing where
  damage entry has reset replay-visible x670/x671 to `0xFE` but vanilla consumes the newly
  radius-eligible stick. The radius predicate uses `Fighter_Spaghetti_8006AD10`'s
  deadzoned `fp->input.lstick` vector, not raw pad axes; borderline raw diagonals whose off-axis
  component is deadzone-zeroed stay below `sdi_min_stick_mag` and do not consume
  `ftCo_Damage_OnEveryHitlag` until the source vector itself is radius-eligible. DamageFly rows
  require frame-start `Fighter_Spaghetti_8006AD10` input-segment evidence from the source
  `lb_8000D148` companion-counter reset (`x679_x`/`x67A_y`); held high-magnitude sticks and
  DamageFly rows without that source input-crossing evidence are negative controls and do not
  retrigger the bridge. Fresh airborne `DamageAir*` entry from same-frame BODY contact publishes
  the root already
  resolved by the pre-hit motion-state collision callback (`Fighter_procMap`) before
  `Fighter_ProcessHit -> ftCo_8008DCE0`; later active-hitlag floorhug projection requires actual
  `ftCo_Damage_OnEveryHitlag` SDI-consume provenance, so a held downward stick after x670/x671 reset
  cannot repeatedly snap the frozen DamageAir root to floor bias. The stay-airborne floorhug
  continuation may project a carried static FD hard-floor `CollData.floor.index`, but soft
  platforms require a current `mpColl_80044628_Floor` bottom-sweep hit before
  `mpColl_80044948_Floor` can correct the root; replay-visible carried platform ids alone are not
  enough to snap active-hitlag SDI upward onto a platform. Pokemon Stadium and other complex
  hard-floor families need a broader callback-current floor owner before sharing the FD
  `DamageAir*` active-hitlag floorhug branch. AttackAir
  same-frame IASA checks aerial B-special admission before JumpAerial (`ftCo_AttackAir.c::DO_IASA`,
  `ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput`); JumpF/JumpB -> EscapeAir floor handoff
  projects through the decomp floor wrapper (`ftCo_EscapeAir_Coll`, `ft_80082C74`,
  `mpLib_8004DD90_Floor`). Sustained locked `EscapeAir_Coll` platform landings use
  `floor_sweep_prev_pos` as the callback-visible `CollData.prev_pos` start and admit the late
  above-root `mpColl_80044838_Floor(ignore_bottom=true)` phase when the current root has penetrated
  the accepted platform by the loaded EscapeAir ECB-bottom depth. If the seed carries a preserved
  desired-bottom owner, oversized distinct-platform snaps must either have swept that desired bottom
  across the accepted floor or stay airborne; root projection alone cannot synthesize a large
  platform lift while the desired bottom stayed below the line. This keeps shallower first crossings
  airborne while allowing countdown-6 platform landings on legal stages whose callback-local floor
  precondition has reached the floor. FoD-specific locked EscapeAir rows can also expose a zero
  visible EscapeAir bottom while source CollData still carries a positive desired/previous bottom;
  runtime admits only the platform-bottom sweep into MSLSTG01 FoD platform lines before applying the
  same root projection. Transformed side platforms retain the lock-countdown boundary (timer 3
  airborne, timer 2 publishable), while static center-platform and fresh Jump/JumpAerial entry rows
  use their own callback-local sweep. Sustained airborne `DamageFly*` rows over FoD transformed
  platforms also seed `floor_sweep_prev_pos` from the callback-visible CollData owner when the same
  action continues in active hitstun without hitlag: `ft_80081DD4` has already copied
  `coll.cur_pos` from the current fighter root at the start of `DamageFly_Coll`, and the hidden
  transformed-platform state is the stale sweep source. This keeps FoD soft-platform crossings from
  firing one frame early. Hard-floor rows on other stages keep the normal previous-public-row sweep
  so real `DamageFly* -> Passive/DownBound` crossings remain intact.
  (`src/mpcoll_ground.c`; refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll,
  refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C},
  refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpColl_80046904,mpColl_80044838_Floor});
  `GuardSetOff_Anim` may enter Guard and then same-frame GuardOff through
  the normal Guard IASA release path; and terminal `GuardReflect_Anim` with an expired timer can
  snapshot into Guard before release consumption. Common-air FD walljump rows promote the hidden
  `wall_jump_input_timer` / `x2110_walljumpWallSide` phase from prefix-causal replay history, using
  ISO-derived `co_attrs.x148` and ftCommonData x768/x76C/x770/x774 at runtime rather than a generic
  visible wall-hug branch.
  Replay locks:
  `tests/test_core_combat_damage_contact_followup_replay_real_locks.py`,
  `tests/test_damageair_hitstun_exit_jumpbuffer_replay_real_locks.py`,
  `tests/test_locomotion_attackair_landing_contact_y_regression.py`, `tests/test_locomotion.py`,
  `tests/test_guard_callback_order_replay_real_locks.py`, and
  `tests/test_damagefly_passivewalljump_replay_real_locks.py`. Fresh taxonomy from regenerated
  primary and aggregate datasets after the current continuation: primary `441`, aggregate `3113`.
  The latest retained callback followup models the DownBound `mpLib_8004DD90_Floor` endpoint clamp
  before allow-ground-to-air Fall exit, seeds the narrow FD `DamageFlyTop` persisted CollData
  wall-side/index lane, and seeds `LandingFallSpecial` frame speed from the source landing-lag
  scalar: EscapeAir common `x344`, Illusion/Phantasm `da->x50`, and Firefox/Firebird `da->x90`.
  Common `Damage_Coll` now treats the seeded final CollData_X130_Locked frame as locked-bottom
  input for the `ft_80081DD4 -> mpColl_800473CC` floor callback, while DamageAir/DamageFly retain
  separate timing. Runtime supported grounded Shine launches on a persisted ledge-floor line also
  install the same 10-frame ECB lock, so rollout carries the locked-bottom floor callback through
  hitlag before the `DamageFly_Coll` hitlag-exit `DownBound` handoff. The same pass also keeps
  `DamageAir -> Landing` hitstun clear ownership on `ftCo_Landing_Enter_Basic`, preserves same-frame
  `DamageAir -> AttackAir` IASA entry as airborne through the entry collision pass, and admits the
  narrow active-hitlag `DownDamage_Coll` resting / downward-KB floor callback path.
  Current target-family counts after the floor/landing owner split:
  primary `F13a=0`, `F27a=0`, `F27b=0`, `F27c=0`, `F27d=0`; aggregate
  `F13a=0`, `F27a=0`, `F27b=0`, `F27c=0`, `F27d=0`. The rows formerly
  grouped there now sit in narrower owners: `F10n_common_fall_landing_timebase`
  for common EscapeAir/FallSpecial/LandingFallSpecial phase rows,
  `F18_damage_tech_timer_seed_surface` for DamageFly floor-contact / tech /
  hidden CollData provenance, `F08d`/`F08c` for damage timer/transition
  adjacency, `F09c`/`F10a` for aerial/grounded action-entry adjacency, and
  `F10b`/`F20`/`F28`/`F29` for combat-contact fallout. Fresh totals remain
  primary `441`, aggregate `3113`. Active aggregate heads are `F01=567`, `F25=373`,
  `F10b=209`, `F09c=193`, `F12b=148`, `F26=137`, `F03=135`, `F08a=131`,
  `F09a=114`, `F18=110`, `F10n=105`, `F08d=104`, `F09b=98`, `F10f=79`,
  `F28=68`, `F10d=67`, `F29=64`, `F10a=48`, `F19=42`, and `F10j=38`.
  Current uncommitted runtime/seed movement from `f4b` is aggregate `3141 -> 3113`,
  aggregate `F27b 128 -> 104`, and aggregate `F27d 29 -> 25`; primary remains `441`.
  The later taxonomy owner split is separate: it moves `F13a/F27a/F27b/F27c/F27d` to zero by
  re-owning remaining rows into `F18`/`F10n`/`F08d`/`F08c`/`F09c`/`F10a`/`F10b`/`F20`/`F28`/`F29`
  without changing the total mismatch counts.
- Walljump env-flag precision (2026-04-25): `mpColl` wall fallback contacts set
  `Collide_*WallPush`, but only ECB side-point contact sets `Collide_*WallHug`; common-air
  `ftWallJump_8008169C` must consume `WallHug`, not the whole wall mask. The replay seed bridge
  for hidden `wall_jump_input_timer` / `x2110_walljumpWallSide` is now explicitly one-step-owned
  and cleared after the reseeded frame, so rollouts require live `WallHug` again. This removes the
  modelplay rerun17 false `PassiveWallJump` entries while preserving the replay-real one-step
  walljump seed lock. Sources: `refs/melee/src/melee/mp/mpcoll.c::mpColl_80044E10_RightWall`,
  `refs/melee/src/melee/mp/mpcoll.c::mpColl_80045B74_LeftWall`,
  `refs/melee/src/melee/ft/ftwalljump.c::ftWallJump_8008169C`.
  Rejected followup trials include generic walljump runtime entry without the hidden walljump timer
  / persisted CollData wall seed surface, broad `DamageFall` terminal IASA suppression, broad
  EscapeAir steady floor projection, terminal damage ECB locked-bottom expansion, broad fresh/late
  JumpAerial -> EscapeAir floor projection, generic DamageFly root projection, visible
  DamageFlyTop wall-hug recovery, ordinary Fall early floor-sweep suppression, frame-start ECB-lock
  consumption, JumpAerial-entry EscapeAir floor suppression, sustained early-lock EscapeAir
  `action_frame<=4` landing suppression, root-below-floor guarded early-lock EscapeAir suppression,
  generic airborne DownBound floor-sweep/resting-contact suppression, visible late DamageFlyN root
  projection, x67C/x67D/x67E-gated DamageFlyN projection, broad continued DownDamage projection,
  JumpAerial -> EscapeAir immediate-entry landing suppression, and terminal airborne DownBound
  ledge-floor Fall conversion; none is retained. A broad
  final-lock-bottom experiment for all DamageAir/DamageFly/Damage ground rows worsened taxonomy
  (`primary F27b 25 -> 32`, `aggregate F27b 128 -> 135`); the retained subset is common
  Damage_Coll only. Visible DamageFlyN projection fixed sampled Passive/DownBound rows but worsened
  `F27a` (`primary 9 -> 35`, aggregate `101 -> 455`); adding x67 timer context still worsened
  aggregate `F27a 101 -> 429`. Broad continued DownDamage projection worsened `F27d`
  (`primary 0 -> 16`, aggregate `29 -> 149`); terminal airborne DownBound Fall conversion
  worsened `F27d` (`primary 0 -> 66`, aggregate `25 -> 175`). The
  early-lock EscapeAir variants fixed the primary `QuerulousGrandDinosaur:157` airborne row but
  either worsened `F13a` (`primary 7 -> 31`, `aggregate 86 -> 298`) or row-swapped into
  `QuerulousGrandDinosaur:2102` with no count movement. The DownBound suppression did not move the
  representative `ImpassionedAlarmedTarsier:8904` / `HungryImportantSnake:5146` rows.
  Later rejected trials in this owner split include a narrower `DamageFlyHi` upward-KB floor
  suppression (`primary 441 -> 492`, `F27a 9 -> 60`) and adding the missing
  teacher-forced `CollData.prev_pos.x` seed lane; the X lane is retained as a decomp-backed
  seed surface but did not move target counts by itself.
- Ledge callback parity pass (2026-04-19): MissFoot now participates in the decomp cliff-catch
  collision wrapper; slow ledge options share quick-option attach / air-to-ground ownership;
  terminal CliffCatch can consume same-proc CliffWait attack/escape/jump IASA but not climb/drop
  because `mv.co.cliff.x8` starts false; Cliff x1990 invulnerability is entry-owned; Z maps to
  CliffAttack before LR-lane escape; and ordinary Fall-family one-step reseeds apply the x2064
  pre-collision cooldown tick without broadening SpecialHi ledge admission. Native preprocessing
  also reconstructs the x2064 cooldown for Damage* entries that interrupt a cliff-owned fighter:
  source `ftCo_8008E908` sets `x2064_ledgeCooldown` while old `x221D_b7` is still live, before the
  Damage* motion-state change clears cliff ownership. Replay-real locks:
  `tests/test_ledge_collision_env_parity_replay_real_locks.py`.
- Ledge / collision-env continuation (2026-04-19): Cliff option terminal callbacks now reuse the
  decomp Wait IASA locomotion tail after `ftCommon_8007D92C`; DownDamage same-action floor contacts
  refresh jumps through the common air->ground helper when mpColl already reports ground; airborne
  DownBound refreshes persisted `floor.index` across connected FD floor seams; same-action
  DamageAir floor contact refreshes only the replay-visible jump count while preserving fastfall
  state; and Ottotto edge ownership now covers Walk/Landing-style `ft_80084280` teeter admission
  plus the immediate L-stick Ottotto -> KneeBend edge-loss path. Ottotto / OttottoWait IASA also
  follows the source Jump -> Dash -> crouch -> Turn -> Ottotto-walk tail: ordinary Turn uses
  `ftCo_Turn_CheckInput`, while Ottotto walk uses the extracted `p_ftCommonData->x474` threshold
  (`data/common/ft_common_data.json::ottotto_walk_stick_x_threshold`). Broad EscapeAir ledge
  projection, full DamageAir transfer, and broad DamageFly root projection experiments were
  rejected due taxonomy or lock regressions.
- Static pass-through platform floors are now part of the runtime fighter floor graph for supported
  non-FD stages. Platform admission follows the source split: airborne/common collision callbacks
  can land from above through `mpColl_80046904` with `CollisionFlagAir_PlatformPassCallback`,
  grounded fighters remain on their current platform through `mpLib_8004DD90_Floor`, and
  down-input Pass uses the extracted `p_ftCommonData->{x464,x468,x470,x46C}` thresholds/velocity
  plus `mpUpdateFloorSkip`-shaped skip gating. FoD floor collision consumes seeded
  replay/eval raw grIzumi platform heights when present and otherwise free-runs the
  `grIzumi_801CC358` RNG/wait/target scheduler from generated `GrIz.dat::yakumono_param`
  metadata; both paths update MSLSTG01 platform transform records before fighter collision. FoD
  side-platform collision uses the source `mpLib_80055E9C` MapLine transform
  `world_y = source_local_y + current_height * 0.75`, distinct from the viewer/visual platform
  scale. Yoshi's Story keeps the raw center raised segment debug-visible but
  non-fighter-solid and admits Randall as a generated transformed pass-through floor. Grounded
  riders already attached to a moving stage-object floor inherit that transform through the
  generated `MSLMSO01` `GROUNDED_STAGE_OBJECT_CARRY_COLL` callback class, covering
  Wait/Walk/Run/Squat/Landing/LandingAir/grounded attack/guard plus the downed/passive-family
  callbacks that source routes through `ft_80084104` (`ftCo_Down_Coll`,
  `ftCo_DownAttack_Coll`, `ftCo_PassiveStand_Coll`) and the grounded Catch/Throw callbacks that
  source routes through `ft_800841B8 -> ft_800827A0 -> mpColl_8004B2DC`. DownBound/DownWait/
  DownStand/DownSpot, Passive, and DownDamage keep their separate downed collision owners and do not
  borrow this carry class. Frozen Pokemon Stadium fighter-solid policy is
  data-backed by MSLSTG01 current-domain metadata, keeping transformation lines visible for
  debug/data APIs while suppressing them from fighter collision.
  (`src/mpcoll_ground.c`, `src/locomotion.c`;
  refs/melee/src/melee/mp/mpcoll.c::{`mpColl_80046904`,`mpUpdateFloorSkip`,
  `mpColl_80044628_Floor`},
  refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c).
- AttackAir floor contact is owned by `AttackAir_Coll -> ft_80082C74` even on same-frame
  hitlag-start contacts, so an aerial that hits while touching a platform still enters `Landing` /
  `LandingAir*` from the extracted command-script `cmd_vars[0]` window instead of remaining as a
  grounded AttackAir. The same `ft_80082C74 -> mpColl_800471F8` owner accepts connected
  hard-floor edge handoffs by first requiring the `mpColl_80044628_Floor` bottom sweep, then using
  `mpColl_80044838_Floor(ignore_bottom=true)` when the loaded AttackAir ECB bottom is above the
  callback root. The same source handoff can publish source-trusted FoD height-transform platforms
  once the callback root is past the shallow first-phase platform-contact band; `AttackAir_Coll`
  does not pass `ftCo_80096CC8`, so held-down input alone is not a platform-pass reject for those
  deeper root-below contacts. Shallow first-phase and carried `floor_skip` contacts remain scoped to
  height-transform platform pass-through. Grounded
  ThrowF/B/Hi/Lw floor-end behavior shares the
  `ftCo_Throw*_Coll -> ft_800841B8 -> ft_800827A0 -> mpColl_8004B2DC` owner; connected FoD
  slope-to-flat handoffs use the floor line returned by `mpLib_8004DD90_Floor` instead of
  extrapolating the carried slope past its endpoint, while `mpColl_8004A45C_Floor` endpoint snap
  keeps off-end throws rooted to the current floor edge rather than publishing an airborne throw at
  platform height. Replay/modelplay locks cover MGS ThrowF right-main-floor seam projection,
  Battlefield AttackAirN hitlag landing, and Yoshi ThrowF platform-edge rooting (`src/locomotion.c`,
  `src/mpcoll_ground.c`; refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c,
  refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c,
  refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c,
  refs/melee/src/melee/ft/ft_081B.c, refs/melee/src/melee/mp/mpcoll.c).
- Fresh `JumpAerialF/B -> EscapeAir` hard-floor contacts consume the preserved
  `CollData_X130_Locked` desired ECB bottom in the same `EscapeAir_Coll` callback. The bottom sweep
  uses the current locked desired bottom before `mpColl_80044838_Floor(ignore_bottom=true)` projects
  the root to the accepted floor, so FoD hard-floor rows like EWT's right ledge floor enter
  `LandingFallSpecial` from the fresh EscapeAir callback rather than staying airborne. Sustained
  soft-platform EscapeAir countdown guards remain separate from this fresh hard-floor bottom-sweep
  owner (`src/mpcoll_ground.c`; refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA,
  refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll,
  refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C},
  refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_800471F8,
  mpColl_80044628_Floor,mpColl_80044838_Floor}).
- MSLSTG01 v9 raw `MapLine` links are exposed as runtime substrate for source-shaped
  `mpLineGetPrev/Next` and non-kind traversal. Frozen Pokemon Stadium applies the active
  fighter-solid mask to walls/ceilings as well as floors, so inactive transformation/platform-side
  shell lines remain visible in data/debug output but do not participate in fighter wall collision.
  (`src/stage_collision.c`, `src/mpcoll_wall_ceil.c`; refs/melee/src/melee/mp/mplib.c::{
  `mpLineGetPrev`,`mpLineGetNext`,`mpCheckLeftWall`,`mpCheckRightWall`},
  refs/slippi-ssbm-asm/Online/Core/Hacks/Stadium/IngameCheckIfFrozen.asm).
- Grounded inline2 map collision now shares one ordered wall/ceiling/floor scratch for the source
  `mpColl_8004ACE4` sequence: left/right wall candidate collection and envelope collision run
  before horizontal squeeze, ceiling uses both direct sweep and raw wall-adjacent fallback, floor
  resolution can consume those same-frame wall side bits through the raw `mpColl_80044628_Floor`
  adjacent-floor fallback, successful floor contact retries ceiling with the carried source
  squeeze flags for vertical squeeze, and the final
  `mpColl_8004A908_Floor -> mpColl_80044838_Floor` disconnected-floor retry remains after the
  ordinary wall/floor/ceiling loop, even when the ordinary current-floor projection still reports a
  floor. That lets a moving FoD height platform replace a still-valid main-floor `CollData.floor`
  only when the source retry's bottom/side-midpoint sweep actually intersects the disconnected
  platform; distant live scheduler platforms remain rejected by the sweep. Floor-adjacent ledge
  walls are excluded through raw line
  ownership rather than stage ids, preserving FD/cardinal ledge rows while allowing Battlefield
  non-connected wall/floor corners to resolve in source order. (`src/mpcoll_ground.c`,
  `src/mpcoll_wall_ceil.c`; refs/melee/src/melee/mp/mpcoll.c::{
  `mpColl_8004ACE4`,`mpColl_80044628_Floor`,`mpColl_80044838_Floor`,
  `mpColl_80044AD8_Ceiling`,`mpColl_8004A908_Floor`,`mpCollSqueezeHorizontal`,
  `mpCollSqueezeVertical`,`mpColl_80048AB0_RightWall`,`mpColl_800491C8_RightWall`,
  `mpColl_80049778_LeftWall`,`mpColl_80049EAC_LeftWall`}).
- Yoshi's Story Shy Guys (`It_Kind_Heiho`) are admitted as stage-owned item objects, not Fox/Falco
  articles. Their source callback recomputes `item->x40_vel` from item-animation dynamic-bone state
  before generic item integration; state 0 waits on hidden `itemVar.heiho.x24` initialized from the
  spawn-group ordinal by `it_802D8618`. Runtime now admits replay-seeded/eval Shy Guy stage timer,
  active-motion/state-delay/lifecycle slices: state-0 delay, active state 1/4 X-speed, state 2
  generated item max-fall clamp plus state 3 gravity, blast-bound clear, and the active dynamic-bone Y
  recompute from the generated `MSLSTIO1` GrSt.dat Heiho child-JObj `HSD_A_J_TRAY` FObj delta table
  using previous velocity plus active AObj phase. Existing replay items seed active state 1/4
  X-speed only from prefix-causal evidence: runtime-spawned Shy Guys use the scheduler RNG owner,
  while already-live replay reseeds mark the speed lane from the first visible nonzero state 1/4
  velocity row forward and do not backfill earlier zero-velocity active rows. Active state 1/4 wall
  turnarounds use
  the generated Heiho Article `ItemAttr.x40` fixed ECB source plus `ItemAttr.x60` scale and the
  `it_80276308` wall-contact owner after item position integration; the replay seed lane
  reconstructs the resulting 20-frame `itemVar.heiho.x24` turn cooldown from prefix-visible
  velocity sign flips. The retained phase lanes are hidden runtime state, not replay target
  position/velocity bridges. Fixed-ECB floor reset only admits source-shaped floor entry and avoids
  treating already-below-floor lateral motion as a fresh `it_8026DA70` reset
  (`src/stage_collision.c`, `src/items.c`;
  refs/melee/src/melee/it/it_266F.c::it_8026DA70,
  refs/melee/src/melee/mp/mplib.c::mpCheckFloorRemap,
  refs/melee/src/melee/it/items/itheiho.c::itHeiho_UnkMotion1_Coll).
  Runtime also models the source no-live-Heiho stage
  scheduler's timer, discarded reset-timer RNG consume, two `set_shyguy_spawn_count` calls, and
  per-spawn jitter; replay rollouts seeded during a no-live countdown carry an explicit
  `stage_yoshi_shyguy_spawn_rng_seed_u32` lane and install it only at the zero-timer spawn callback,
  then live Heiho frames return to seed-owned RNG state. This replaces the old synthetic
  `+0x10000` Shy Guy frame clock but still does not close full autonomous Yoshi Shy Guy ownership:
  unrelated global HSD consumer order before the spawn and some free-running stage-object item
  interactions remain open. Knocked/falling state 2/3 and low-damage return-flight state 4 blast
  clear are the generic item post-Phys owner (`Item_802697D4 -> Item_802696CC`), so they clear on
  exact side/bottom blast bounds without the active-state-1 `it_802D9714` 20-unit return margin.
  Fighter HitCapsules with the extracted item-interaction bit can also damage active/return-flight
  Shy Guys through the source item-hurtbox path (`it_802703E8 -> it_80270E30 -> it_802D8EC8`);
  this is the same state-2/3 damage lifecycle as laser item-vs-item hits, not a player BODY proxy.
  Dream Land Whispy/apple scheduling is also separate and must not be substituted with replay-next lanes
  (`src/items.c`, `data/stage_items/yoshi_shyguy.bin`; refs/melee/src/melee/gr/grstory.c::grStory_801E3418,
  refs/melee/src/melee/it/items/itheiho.c::{
  it_802D8618,itHeiho_UnkMotion0_Phys,itHeiho_UnkMotion1_Phys,itHeiho_UnkMotion2_Phys,
  itHeiho_UnkMotion3_Phys,itHeiho_UnkMotion4_Phys,itHeiho_UnkMotion1_Coll,
  itHeiho_UnkMotion4_Coll,it_802D8EC8,it_802D98C4},
  refs/melee/src/melee/it/itcoll.c::{it_802703E8,it_80270E30},
  refs/melee/src/melee/it/item.c::{Item_802697D4,Item_802696CC},
  refs/melee/src/melee/it/it_2725.c::{it_80275DFC,it_80276308}).
- Dream Land Whispy wind is a stage-owned fighter horizontal force, not item motion. The runtime
  consumes generated `MSLWHSP1` GrOp.dat wind speed/rectangles and applies the current
  `grOldPupupu.xDC` wind direction after fighter collision/platform carry, matching
  `ftColl_GetWindOffsetVec` ordering. Teacher-forced eval derives only the current hidden wind
  direction and remaining active-window carry prefix-causally in native preprocessing; it does not
  seed replay-next fighter position or velocity. The carry is bounded by the source
  `grOldPupupu_802113E0` active window (`xD0` in `(45, 320)`) instead of treating every active row
  as a fresh full-length episode. This closes the dense Dream Land fighter `pos_x` p95 wind band. The remaining
  Dream Land float-norm p95 outlier was not Whispy apple p95; float-norm autopsy identified sparse
  throw-side Fox/Falco laser item lifecycle rows. Runtime now advances the throw-side blaster item
  spawn counter from extracted throw-pulse ordinals and keeps carried ThrowB state-1 laser articles
  on their source item-BODY path instead of replaying stale pulse/despawn ownership. Whispy apple
  lifecycle remains separate and is not modeled by this wind/throw-laser slice.
  (`src/stage_collision.c`, `data/stage_items/dream_whispy.bin`;
  refs/melee/src/melee/gr/groldpupupu.c::{grOldPupupu_802113E0,fn_802112F4},
  refs/melee/src/melee/ft/ftcoll.c::ftColl_GetWindOffsetVec,
  refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate;
  refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim,
  refs/melee/src/melee/it/item.c::Item_80267AA8,
  refs/melee/src/melee/it/itcoll.c::{it_8026FA2C,it_8026FAC4,it_80272460}).
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
| Stage collision + ECB (FD ground/ledge points, grounding, `ground_id`) | **PARTIAL** | mpColl-shaped FD ground contact is implemented (floor + wall/ceiling passes + persistence); DownBound floor-index persistence across FD seams and narrow Ottotto FD-edge handoffs are modeled. The ledge/collision-env checklist bucket is closed in fresh taxonomy (`F17=0`, `F10c=0`, `F22=0` in primary and aggregate). Remaining row evidence is split to exact adjacent owners: pure `CollData.floor.index` visibility (`F10m`), common Fall/Landing timebase (`F10n`), common Ottotto teeter handoff (`F10o`), and SpecialAirHi/Bound collision callback timing (`F10p`, kept out of `F22`). |
| Ledge system (Cliff* actions, quick/slow options) | **PARTIAL** | Ledge-grab mask now uses collision-stage prev/cur snapshots; occupancy includes quick/slow option actions; MissFoot can CliffCatch; Cliff option terminal callbacks can consume Wait IASA locomotion tails; Cliff x1990 and x2064 terminal cooldown seed/runtime ownership are narrowed. The generic ledge/collision-env taxonomy buckets are closed (`F17=0`, `F10c=0`). |
| Knockdown/tech (DownBound/Wait/Stand/Attack + rolls) | **CLOSED FOR SHARED PASSIVE/DOWNBOUND SELECTOR** | `DamageFly*`/`DamageFall` floor contact shares one decomp-shaped selector for `PassiveStandF/B` -> `Passive` -> `DownBound`; `x680`/`x684` tech timers now distinguish pre-hitlag L/R tech presses from hitlag-active latched presses. Fresh primary and aggregate taxonomy have `F07_knockdown_grounding=0`. `F18_damage_tech_timer_seed_surface` is active again as a narrower DamageFly floor-contact / hidden CollData provenance owner after the floor/landing split; it is not shared Passive / PassiveStand / DownBound selector debt. |
| Combat geometry (hurtcaps/hitboxes/shields pose-driven) | **PARTIAL** | Core data-driven primitives exist; remaining parity depends on exact facing/axis + attachment nuances. |
| Damage pipeline (BODY + SHIELD, GuardSetOff, hitlag/hitstun/KB states) | **PARTIAL / RESIDUAL CLEANUP ACTIVE** | Core damage admission and several followup lanes are in. Recent retained work covers `DamageFall_IASA` x670 handoff parity, airborne `DownDamage_Anim` -> `Fall`, common airborne `Damage_IASA` AttackAir/JumpAerial dispatch, `DownDamage_Coll` floor-contact participation, DownDamage contact facing parity, grounded Damage ledge-slip MissFoot, GuardSetOff shield-hit hitlist carry, `DamageAir` x14 snapshot behavior, AttackAir B-special-before-JumpAerial IASA ordering, JumpF/JumpB -> EscapeAir floor-wrapper handoff, GuardSetOff -> Guard -> same-frame GuardOff ordering, terminal GuardReflect -> Guard snapshot ordering, common `Damage_Coll` seeded final ECB-lock floor callback, `DamageAir -> Landing` hitstun clear, and same-frame `DamageAir -> AttackAir` entry floor suppression. Current aggregate residual owners are `F01_guard_release_collision`, `F25_camera_box_visibility_x221f`, `F10b_grounded_combat_adjacency`, `F09c_aerial_action_entry_adjacency`, `F12b_adjacent_instance_counter_order`, `F26_damageflyroll_rng_stream_seed_surface`, `F03_capturewait_bridge`, `F08a_damage_identity_bookkeeping_residual`, `F09a_aerial_stateflag_hurtbox_adjacency`, `F18_damage_tech_timer_seed_surface`, `F10n_common_fall_landing_timebase`, `F08d_damage_timer_scalar_residual`, `F09b_aerial_bookkeeping_adjacency`, `F10f_grounded_attack_adjacency`, `F28_body_contact_candidate_narrowphase_owner`, `F10d_hurtbox_stateflag_adjacency`, `F29_aerial_contact_hitlag_provenance`, `F10a_grounded_selector_transition`, `F19_specialn_blaster_article`, and `F10j_turnrun_exit_microphase`. `F13a` and `F27a/F27b/F27c/F27d` are zero in the active taxonomy and are not current residual owners. |
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
    - CliffWait climb/drop “previous-stick neutral reset” latch (`src/ledge.c`).

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
     - `refs/melee/src/melee/ft/ftcoll.c::{checkTipLog,inlineB1,ftColl_80076ED8}` (fighter phantom-hit path:
       `victims_2` gate + `coll_distance < p_ftCommonData->x7A8`; starts victim hitlag without percent/KB/state entry)
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
   - AttackAirB jump-entry collision-scale subset:
     - General hitbox placement still uses the extracted pose matrices plus `fighter_scale_y *
       model_scaling`, but the `GAT:2221` front-door slice needs one narrower correction.
     - For first-active-frame `AttackAirB` BODY checks on the enable-edge slice, decomp cancels
       per-character `model_scaling` on the collision subtree via
       `ftAnim_8006FA58 -> ftCommon_8007F6A4`; a scale_y-only counterfactual is used as the
       acceptance gate for the proven subset so far:
       - jump-entry victims like `GAT:2221`, and
       - shallow `DamageFlyTop` victims like `QGD:285` where model-scaled hitbox placement creates
         a one-frame-early false BODY hit before the real follow-on hit lands.
     - Keep the already-proven SpecialAirHi/Firefox launch subtree as a separate local-pose owner;
       that family still needs its dedicated XRotN rotation + scaling path.
     - Decomp anchors: `refs/melee/src/melee/ft/fighter.c::Fighter_UpdateModelScale`,
       `refs/melee/src/melee/ft/ftanim.c::ftAnim_8006FA58`,
       `refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007F6A4`,
       `refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC`.
   - SpecialAirHi victim hurtcap launch rotation:
     - Firefox/Firebird launch writes `rotateModel = atan2f(self_vel.y, self_vel.x * facing_dir)`
       and applies it through `ftPartSetRotX(..., FtPart_XRotN)`.
     - Recent AGN rows show attacker AttackAirLw hitboxes already match live vanilla; the missing
       owner is the victim `SpecialAirHi` hurtcap pose on that launch family.
     - Runtime currently applies the launch XRotN local rotation across the SpecialAirHi hurtcap
       refresh path for Fox/Falco while this family is active.
     - Decomp anchors: `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
       ftFox_SpecialHi_RotateModel,ftFx_SpecialAirHi_Enter,ftFx_SpecialAirHi_Coll}`.
   - This does **not** mean PP#3 is “done”: on the current suite, the target mismatch fields did not move yet, and the remaining work is
     primarily about *rehit-rate timers and ordering*.
   - Current conclusion (asm-first): GALE01 fighter-side code does not appear to write `HitCapsule.x40_b4` (the 8-bit rehit countdown
     field). Rehit behavior for fighters appears driven by hitbox enable/disable + `hit_group` (`HitCapsule.x4`) sharing/clears, while
     items/projectiles do write `x40_b4` in at least some paths.
  - Create-frame post-contact hitlag seed boundary: teacher-forced Slippi rows can start inside
     the hitlag window from a BODY hit that was accepted on the same hitbox create pose frame.
     Runtime must first apply the normal `ftAction_8007121C -> ftColl_800768A0` enable-edge
     clear/copy, then materialize the replay-derived `lbColl_80008688` victims_1 state only when
     attacker/defender hitlag, defender hitstun, and `instance_hit_by` prove the accepted BODY
     source. Negative locks cover hitlag-adjacent rows where `instance_hit_by`, victim hitstun, or
     dense group hitlist state do not prove that BODY source, so this is not a broad "if hitlag then
     seed hitlist" suppressor. This prevents same-window rehit on rollout while leaving fresh
     create-frame hits eligible. Source refs: `refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C`,
     `refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}`,
     `refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688`,
     `refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC`.
   - DownAttackU/DownAttackD -> late LandingFallSpecial dense-hitlist BODY boundary:
     legacy dense `combat_hitlist_cd` can prove a carried `HitCapsule.victims_1` entry on late
     LandingFallSpecial frames even when the reconstructed attacker hitbox reaches a same-pose
     `ftColl_800768A0` clear lane. Combat selection consumes this dense fallback only after the
     phantom/tip-log (`checkTipLog` / `victims_2`) branch and only to suppress full BODY damage.
     Because decomp `HitVictim` keys are fighter pointers, not Slippi instance ids, stale
     `combat_hitlist_victim_iid` proxies rebind while the victim fighter object is alive; death/rebirth
     remains the pointer-change boundary, matching `src/hitlist.c`.
     Same-frame victim action-entry rows are excluded because the dense fallback lacks per-HitCapsule
     clear/copy provenance for the new victim action frame. Replay locks:
     `PutridJoyousOryx.msl:4092` is the late LandingFallSpecial positive; `HungryImportantSnake.msl:7485`
     is the same dense-seed shape on LandingFallSpecial entry and must still admit BODY hitlag.
     Source anchors:
     `refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}`,
     `refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}`.
   - Late AttackAirB shield/body contact vs stale dense group seed:
     dense group seeds materialized from legacy `combat_hitlist_cd` now carry an internal provenance
     marker in fighter `HitVictim.id32`; true runtime or authoritative per-HitCapsule entries keep
     that field clear. This lets rollout trim only stale dense-seed entries on later active-snapshot
     frames when a neutral Guard victim is otherwise blocked by a replay seed that cannot prove the
     current `HitCapsule.victims_1` owner. It does not erase authoritative per-HitCapsule victims_1
     seeds. Positive/negative locks: `PositiveRevolvingHyena.msl:6822..6831`.
     Source anchors:
     `refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076CBC,ftColl_80076ED8}`,
     `refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}`,
     `refs/melee/src/melee/lb/types.h::HitCapsule`.
   - GuardOn-origin GuardReflect powershield hidden victims_1 latch:
     `ftColl_80076CBC` registers the attacker HitCapsule victim through `ftColl_80076808` before
     the `x221C_b2` powershield branch suppresses ordinary shield damage / GuardSetOff effects.
     Replay-visible state can therefore look like a no-hit GuardReflect window while the hidden
     `HitCapsule.victims_1` latch still suppresses later BODY fallthrough from the same hit group.
     Replay-rollout reconstruction is limited to hitbox create edges where the defender is still
     in GuardOn-origin `GuardReflect` with powershield provenance and the dense group seed names the
     victim. Ordinary `Guard`/`GuardSetOff` transitions stay excluded because the dense group lane
     cannot prove their per-HitCapsule clear/copy owner.
     Positive/negative locks: `Game_20260509T152622.msl:2048..2064` and the existing TBK/GAT
     GuardReflect final/expired-x14 controls.
     Source anchors:
     `refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C`,
     `refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076808,ftColl_800768A0,ftColl_80076CBC,ftColl_80078C70}`,
     `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80094138`,
     `refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008688,lbColl_8000ACFC}`.
   - Grounded SpecialLwStart -> terminal DamageFlyTop dense-hitlist boundary:
     a grounded Shine entry can overlap a terminal same-port DamageFlyTop victim while the replay
     seed still carries three hidden proofs: dense group-0 `HitCapsule.victims_1`, explicit
     x198C=1/x1994 colanim provenance, and an older same-port `instance_hit_by` proxy. In that
     narrow lane, combat consumes the dense seed after phantom/tip-log handling to suppress the
     immediate stale BODY rehit. The x198C proof is stored as a seed-only internal bit and does not
     raise generic BODY hit-status, because broad x198C=1 hitstun reseeding regresses unrelated
     contacts. Aerial SpecialLwStart remains excluded; `QuerulousGrandDinosaur.msl:235` proves the
     same dense/x198C shape can still be a real aerial Shine hit. Positive lock:
     `DistinctCaringCobra.msl:6431`.
     Source anchors:
     `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLw_Enter`,
     `refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}`,
     `refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}`,
     `refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm`.
   - Grounded SpecialLwStart vs same-frame Turn internal-facing boundary:
     `ftColl_80078C70` walks fighter pairs through the entity list, while `ftCo_Turn_Anim_Inner`
     can flip `fp->facing_dir` into a `has_turned=1` microphase before the replay-visible facing
     byte catches up. A later-slot grounded Shine entry-created frame-0 HitCapsule can otherwise
     consume the simulator's internal-facing Turn hurtcap pose and create a false BODY hit. The
     retained suppressor is limited to grounded SpecialLwStart entry, later attacker slot, earlier
     grounded Turn defender with `turn_has_turned=1`, and no defender hitlag/hitstun; pre-turn
     Turn (`turn_has_turned=0`) and aerial SpecialLwStart remain on the normal BODY path. Locks:
     `DistinctCaringCobra.msl:3864` positive no-hit, `HilariousVillainousGiraffe.msl:5200` and
     `TubbyCurlyHerring.msl:3376` same-shape hit controls.
     Source anchors:
     `refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70`,
     `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Anim_Inner`,
     `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLw_Enter`,
     `data/moves/{fox,falco}.json`.
   - Squat-family grounded Shine platform-pass vs active airborne DamageFly boundary:
     a frame-start grounded Squat/SquatWait/SquatRv state can enter grounded Reflector, then
     immediately platform-pass into `SpecialAirLwStart` while preserving the ground-start
     submotion/hitbox. `ftFx_SpecialLwStart_Pass` explicitly creates the reflect hit after the
     action has switched to aerial start. If that creator is a later entity, an earlier active
     airborne Damage/DamageFly fighter has already run its collision callback for the frame, so the
     fresh HitCapsule cannot damage it until a later pair phase. Ordinary aerial Shine entries stay
     on the normal BODY path; aggregate controls prove those hits are real. The victim family is
     table-backed by MSLMSO01 `DAMAGE_*_COLL` classes. Lock: doubles
     `Game_20260509T152622.msl:3154`.
     Source anchors:
     `refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70`,
     `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_IASA`,
     `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{ftFx_SpecialLwStart_Pass,ftFx_SpecialAirLw_Enter}`,
     `data/motion_state/owners/{fox,falco}.bin`.
   - Grounded SpecialLwStart vs airborne DamageAir2 tail-pose boundary:
     the temporary Shine/DamageAir2 entry-pose blocker is limited to Fox's dynamic tail capsule
     (`data/hurtcaps/fox.bin` cap12 / FtPart 18), the replay-proven false-contact owner that still
     needs full source-order dynamic/AObj state. Non-tail DamageAir2 hurtcaps remain on the normal
     `lbColl_8000805C` / `lbColl_80006E58` matrix-radius BODY path, so valid cap2 contacts are
     admitted while TBK's tail contacts stay suppressed. Locks: `DelayedSuperbGuanaco.msl:5004->5041`
     rollout positive, `TreasuredBackKangaroo.msl:1575/5265` tail-contact negatives.
     Source anchors:
     `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLw_Enter`,
     `refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}`,
     `refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}`,
     `refs/melee/src/melee/ft/ftdynamics.c::{ftCo_8009DD94,ftCo_8009E318}`,
     `refs/melee/src/melee/lb/lb_00F9.c::lb_8001044C`.

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

4) **Damage modifiers parity (stale queue + multipliers + armor/no-damage gates)** (**PARTIAL**)
   - Goal: make percent/hitlag/hitstun/KB numerically meaningful; stop “percent drift” being dominated by missing modifiers.
   - Current runtime has a shared pre-`Fighter_ProcessHit` damage product for represented fighter BODY, item BODY, and throw-release
     producers. That product keeps raw HitCapsule damage, staled/applied float damage, env-damage, KB damage, move id, and attack instance
     together before percent/hitlag/KB/stale bookkeeping are derived.
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
     - `refs/melee/src/melee/ft/ftcoll.c::ftColl_80078998` (item hit stale/combo side effects)
     - `refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464` (reflect hit writes item reflect/owner snapshot before item collision)
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
     - Items/projectiles: owner (`item->owner`), `attack_id` (`it->xD88_attackID`), and
       `attack_instance` (`it->xD8C_attack_instance`). Reflected projectiles update the new
       owner's stale table with the item attack identity; replay preprocessing matches
       `last_hit_by_instance` against current/previous item rows because the item can be destroyed
       before the damage post-frame is serialized.
   - Per-player/per-entity damage multipliers that influence suite-visible fields:
     - Staling multiplier (from stale table).
     - Reflect/absorb multipliers on projectiles (damage and speed multipliers).
     - Any remaining per-victim or per-attacker multipliers that affect percent/KB/hitlag/hitstun (must be decomp-sourced).
   - No-damage/armor gating internals:
     - A representation of “damage is negated/absorbed” vs “damage is applied but reduced”, plus any remaining “armor HP” style value
       if applicable (decomp shows a subtract-then-apply pattern on some flags in `ftcoll.c`).
     - Runtime must not emulate true armor/no-KB by broad post-hoc ProcessHit gates. Missing fields such as the `x221C_b4` armor flag,
       `dmg.x1834`, and no-KB ownership need explicit seed/runtime state before their behavior can move into the shared damage product.

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
- Script owner events: `data/scripts/{fox,falco}.bin` (`MSLFTSC1 v2`), cached by
  `src/move_tables.c` for hit status, hurtbox state masks, airborne-state events, x221C state
  flags, command-variable windows/pulses, and IASA/throw/script hitbox products.

Counts note:
- `suite_count` is the total occurrences across **both** `seed_t.animation_index` and `ref_t1.animation_index`, restricted
  to the first `num_players` ports in each record.
- Table columns (`hitboxes`, legacy `hurtbox_states`/`hit_status`, `IASA windows`) are historical
  **non-empty coverage** signals from before the unified `MSLFTSC1` cache:
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
| `refs/melee/src/melee/ft/ft_0D4D.c::ftCo_800D4FF4` (Rebirth entry) | Vanilla `Player_GetSpawnPlatformPos` / `Player_GetFacingDirection`, currently backed by `data/stages/final_destination.json` (`respawn_points`, `cam_bounds_world`) plus replay raw player slot (`seed_t.source_port0`) | `src/match_flow.c` |
| `refs/melee/src/melee/mp/mplib.c::mpLib_DrawZones` (blast/camera zone sources) | (Need) explicit blast zone rect for FD extracted into `data/stages/final_destination.json` (or a `data/stages/*.bin` v2) | Implemented for suite; remaining gap is extracting and consuming the canonical blastzone rect(s) from stage data. |

Match-flow closure notes:
- `Player_GetSpawnPlatformPos` and `Player_GetFacingDirection` are player-slot keyed. The sim's
  compact local p0/p1 index is not always the raw replay player slot in aggregate suites, so
  Rebirth respawn position/facing uses `seed_t.source_port0` before falling back to local player
  index. This is locked by `tests/test_match_flow_rebirth_owner_replay_real_locks.py`.
- Rebirth position uses two source player-slot roles, not a single camera-bound proxy:
  `gm_1601.c::fn_8016719C` stores the spawn-platform target in
  `Player_SetSpawnPlatformPos`, while `Player_80032768` stores the start coordinate later loaded by
  `Fighter_UnkInitReset_80067C98`. Most supported stages expose that start Y through the
  world-camera-top role; Pokemon Stadium's source start coordinate is `y=120` while its active
  camera/death bounds remain wider. This is locked by the Pokemon Stadium Rebirth row in
  `tests/test_match_flow_rebirth_owner_replay_real_locks.py`.
- Match-start neutral spawn is separate from stock respawn. The only Slippi neutral-spawn ASM path
  in runtime setup is the 4-player teams `init_match` branch; Rebirth stays on the vanilla
  `Player_GetSpawnPlatformPos` owner and must not inherit neutral-start coordinates.
- Zero-stock players do not auto-Rebirth. In teams, `gm_16AE.c::fn_8016B918_inline` restores a
  zero-stock player only through explicit stock share from a teammate with more than one stock; until
  then Slippi exposes an inactive zeroed DeadDown slot rather than a live Rebirth platform fighter.
- Some team-stock inter-stock rows are serialized by Slippi as `char_id=0, stocks=0, DeadDown`
  while the source match-flow timer is still counting down to Rebirth. The seed lane
  `match_flow_pending_rebirth_char_id` carries the static fighter kind for those rows and is
  consumed only when the Dead* timer reaches zero; before that transition the replay-visible slot
  remains zeroed. The lane is not keyed by action run length alone: generation requires teams
  context, same-team stock-share availability (`stocks > 1` on the teammate), and prefix-visible
  transition/continuity into the zeroed pending slot so terminal eliminated DeadDown slots remain
  unseeded.
- Dead* -> Rebirth runs `Fighter_UnkProcessDeath_80068354 ->
  Fighter_UnkInitReset_80067C98` before `Fighter_ChangeMotionState(Rebirth)`. That reset clears
  percent/temp percent and reloads shield HP from `ftCommonData.x260_startShieldHealth`; rollout
  must not carry depleted shield HP onto the first Rebirth row.
- Current FD `respawn_points` are extractor-owned stage data, not Slippi neutral-start data. The
  extractor still uses an FD-only stage-point heuristic until the canonical `stage_info.x280`
  point-id mapping is extracted directly (see `tools/extraction/extract_stage_collision.py`).
- Dead*/Rebirth*/Entry* skip common stage-collision callbacks. Wall/ceiling `CollData` provenance
  is cleared while in those match-flow states so a pre-death underside contact cannot project the
  first post-Rebirth Fall frame back to FD's underside. This is locked by
  `tests/test_modelplay_manual_respawn_collision_provenance_regression.py`.
- Dead*/Rebirth states set `fp->x2219_b1`, which makes `Fighter_8006CB94` skip the common
  fighter collision pass and makes item collision reject that fighter as a target. This is a combat
  eligibility gate, not a visible `hurtbox_state` rewrite: Slippi can still report a pose-derived
  hurtbox state while the platform bit prevents BODY/catch/item hits. Sources:
  `refs/melee/src/melee/ft/ft_0D31.c::{ftCo_800D3680,ftCo_800D3950,ftCo_800D3BC8,ftCo_800D3E40,ftCo_800D4580,ftCo_800D481C}`,
  `refs/melee/src/melee/ft/ft_0D4D.c::{ftCo_800D4FF4,ftCo_800D5600}`,
  `refs/melee/src/melee/ft/fighter.c::Fighter_8006CB94`,
  `refs/melee/src/melee/it/itcoll.c::it_80272460`.
- `ftCo_RebirthWait_IASA` runs priority aerial special checks before fallback Fall-style exits and
  still applies `ftColl_8007B7A4(gobj, p_ftCommonData->x5D8)` on exit. That x1994/x198C write is
  visible as Slippi `hurtbox_state=1` on RebirthWait -> SpecialAirNStart rows.
- The same RebirthWait -> Fall x1994 owner persists across later locomotion actions until
  `Fighter_8006A360` decrements the timer to zero. Mid-window teacher-forced seeds carry an
  explicit `colanim_rebirth_fall_x1994_seed` provenance bit so runtime can expire x198C on the
  correct frame instead of stale-carrying merged `hurtbox_state=1`. This is not a generic x1994
  trust rule; the seed lane is limited to the RebirthWait -> Fall source path.
  Sources: `refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::ftCo_RebirthWait_{Anim,IASA}`,
  `refs/melee/src/melee/ft/fighter.c::Fighter_8006A360`,
  `refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm`.
- Dead* match-flow actions keep `dmg.x18C4_source_ply` through terminal x18C8 source-clear ticks
  before the Rebirth reset. Dead-flow callbacks still consume source attribution for KO/suicide and
  death-effect bookkeeping, and the eventual clear is owned by
  `Fighter_UnkInitReset_80067C98`. Sources:
  `refs/melee/src/melee/ft/ft_0D31.c::{ftCo_800D331C,ftCo_800D34E0}` and
  `refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_UnkInitReset_80067C98}`.
- DeadUpStar's first active animation tick consumes the one-frame phase-0 timer, then writes the
  phase-1 vertical self velocity from `p_ftCommonData->x514 * Stage_GetCamBoundsTopOffset() -
  cur_pos.y` divided by `p_ftCommonData->x508`. The sim models that boundary from the total
  DeadUpStar countdown and extracted FD camera top; it does not recompute the velocity after phase 1
  has started. Source: `refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::ftCo_DeadUpStar_Anim`.
- DeadUpFall top-blast selection uses `p_ftCommonData->x520` as the percent threshold after
  `HSD_Randi(100)+1`. Runtime consumes that source RNG site in `ftCo_800D3158` when the batch owns
  the modeled HSD RNG stream (`init_match` / simulator-owned rollouts). Replay rollout may also
  consume it on the immediate reseed frame, where Slippi's frame-start seed and the sim's modeled
  same-frame prefix consumers are available; later replay-rollout frames do not use the validation
  replay clock as an HSD-stream substitute. The live `Camera_8003010C()` predicate is modeled as
  per-batch camera mode (`0 = normal`, `1 = CAMERA_FREE`); CAMERA_FREE forces DeadUpStar after the
  source RNG draw. Replay one-step seeds expose the frame-start random seed but not all same-frame
  prefix consumers or live camera mode, so replay-future DeadUpStar/DeadUpFall labels must not
  choose or phase this RNG draw. No camera-bounds proxy is retained.
- DeadUpFall/HitCamera phase timing is source-owned by the following ftCommonData fields. The sim
  derives the existing `match_flow_timer` lane causally from action-prefix history for DeadUpFall
  actions `6/7/8/9/10`, uses `x524/x528` for the DeadUpFall -> HitCamera countdown, and uses
  `x52C/x530/x534` plus `x550/x554/x558` for the HitCamera hold -> phase-3 self-velocity/fall
  update. The visible `speed_y_self` lane is source-owned by `ftCo_DeadUpFall_Anim` +
  `ftCo_DeadUpFall_Phys`; the runtime also carries the hidden `mv.co.unk_deadup.x50/x5C` scratch
  owner for the supported no-ice path. The `xD4_unk_vel` / `ftAnim_80070FD0` branch is gated by
  `fp->x2222_b6`; current supported Fox/Falco no-ice DeadUpFall entry has no source path setting
  that bit, so no future-position replay bridge is introduced for it.
  Sources: `refs/melee/src/melee/ft/ft_0D31.c::{ftCo_DeadUpFall_Anim,ftCo_DeadUpFall_Phys}`,
  `refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate`,
  `refs/melee/src/melee/ft/ftanim.c::ftAnim_80070FD0`.
- DeadUpFall/HitCamera camera ownership refreshes the live fighter camera target through
  `ftCo_DeadUpFall_Cam -> ftCamera_80076320 -> ftCamera_UpdateCameraBox`. The hidden x50/x5C model
  offset is carried for pose/effect ownership, but the CObj/scissor/projection portion of
  `ftLib_80086A8C` remains outside the replay-visible gameplay surface; broad stage-bounds proxies
  are not retained.
  Sources: `refs/melee/src/melee/ft/ft_0D4D.c::ftCo_DeadUpFall_Cam`,
  `refs/melee/src/melee/ft/ftcamera.c::{ftCamera_80076320,ftCamera_UpdateCameraBox}`,
  `refs/melee/src/melee/ft/ftlib.c::ftLib_80086A8C`.

#### Locomotion core (ground/air, jumps, fastfall, landing, airdodge/escapes)

| Read first (decomp) | Data artifacts (ISO-derived) | Code owner / gaps |
|---|---|---|
| `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA` | `data/common/ft_common_data.json` (stick thresholds, timers) | `src/locomotion.c` |
| `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA` | `data/characters/{fox,falco}.json` (walk/run/traction/turn/jump params) | `src/locomotion.c` |
| `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Anim` and `ftCo_JumpAerial.c::ftCo_JumpAerial_Enter_Basic` | `data/anims/{fox,falco}.bin` (anim end frames; `SSANIM01` tables) | `src/locomotion.c` (jump timers), `src/physics.c` |
| `refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_80099A58` | `data/common/ft_common_data.json` (EscapeAir deadzones/force) | `src/action.c` + `src/locomotion.c` |

Grounded motion-entry timing notes:
- Shared motion-state entry side effects belong to `src/anim_timebase.h::msl_anim_timebase_enter`,
  which models `Fighter_ChangeMotionState` plus `ft_800890D0` and `ft_800895E0`.
- Dash animation-end is an Anim-callback owner, not a late IASA tail. `ftCo_Dash_Anim` enters Wait
  through `ft_8008A2BC` before the frame's destination `Wait_IASA` selector runs, so Dash end rows
  must route through the shared Dash->Wait->destination entry bundle rather than a direct Dash IASA
  shortcut.
  Refs: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_Anim`,
  `refs/melee/src/melee/ft/ft_0892.c::{ft_8008A2BC,ft_8008A348}`,
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA`.
- LandingAir/Landing animation-end has the same destination-Wait callback shape. When
  `ftCo_LandingAir_Anim` delegates through `ftCo_Landing_Anim` into `ft_8008A2BC`, the frame can
  still dispatch destination `Wait_IASA`; held L/R plus the down-stick gate enters `EscapeN` via
  `ftCo_80099794` before `ftCo_80091A4C` guard entry. This helper is Wait-only; Walk/Turn/Squat
  IASA paths keep their own ordering and do not use this pre-guard spotdodge gate.
  Refs: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_Anim`,
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Anim`,
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA`,
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_80099794`.
- Escape* animation-end rows can also enter destination Wait before the frame's destination
  `Wait_IASA` selector runs. The retained slice only admits the source-owned pre-guard spotdodge
  check (`ftCo_80099794`) from that destination Wait, using Melee's synthesized `HSD_PAD_LR` lane
  (digital L/R/Z or analog trigger past the common deadzone). It intentionally does not run
  attacks, held-B specials, or the full grounded special dispatcher from Escape* end, because
  `EscapeN_IASA` is empty and Escape* callbacks do not call `ftCo_800D68C0`.
  Refs: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::{ftCo_Escape_Anim,ftCo_EscapeN_Anim,ftCo_EscapeN_IASA,ftCo_80099794}`,
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA`,
  `refs/melee/src/melee/ft/fighter.c:1868-1890`.
- Grounded Attack* destination transitions carry `fp+0x2218` bit0 from the source callback, not
  from a later previous-action row rule. `ftAction_80071950` command events set
  `fp->allow_interrupt`; when an Attack* Anim callback exits to Wait/SquatWait with that bit live,
  or when `AttackDash`, `AttackS3`, `AttackHi3`, `AttackS4`, `AttackHi4`, or `AttackLw4` IASA
  consumes that lane and enters a non-attack destination in the same fighter proc, runtime writes
  the visible allow-interrupt bit at the transition site. Post-frame state_flags refresh no longer
  re-derives this from `prev_action_id` destination shapes.
  Refs: `refs/melee/src/melee/ft/ftaction.c::ftAction_80071950`,
  `refs/melee/src/melee/ft/chara/ftCommon/{ftCo_AttackDash.c,ftCo_AttackS3.c,ftCo_AttackHi3.c,ftCo_AttackS4.c,ftCo_AttackHi4.c,ftCo_AttackLw4.c}`,
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA`.
- Fall/Jump/JumpAerial/MissFoot and Fox/Falco aerial blaster floor contact use the shared
  `ft_80082B1C` Wait-vs-Landing velocity split. The threshold is `ftCo_800D0EC8(fp)`, computed via
  `ftCo_CalcYScaledKnockback(Fighter_804D6524->x30, fp->x34_scale.y, p_ftCommonData->x310)`.
  Runtime consumes the extracted `data/common/ft_common_data.json` keys
  `basic_landing_wait_gravity_mult_x30` and `basic_landing_wait_scale_param_x310` through
  `src/common_params.h::msl_ftco_80082b1c_enters_wait`; this owner remains a narrow checkpoint and
  does not close EscapeAir/LandingFallSpecial or AttackAir landing residuals.
  Refs: `refs/melee/src/melee/ft/ft_081B.c::{ft_80082B1C,ftCo_AirCatchHit_Coll}`,
  `refs/melee/src/melee/ft/ftchangeparam.c::{ftCo_800D0EC8,ftCo_CalcYScaledKnockback}`.
- Dash IASA terminal friction is a callback-phase owner, not an action-id shortcut. When
  `ftCo_Dash_IASA` reaches the terminal branch (`mv.co.dash.x4 != 0` and the consumed callback
  anim frame crosses `mv.co.dash.x44`), it applies `p_ftCommonData->x298` to `self_vel.x` before
  changing into the destination grounded state. This includes GuardOn and GuardReflect entries from
  Dash; rows whose callback frame has not crossed the branch must keep the ordinary Dash carry. The
  terminal-scalar marker is transient within the current step and exists only to preserve the
  callback ordering into same-frame GuardReflect item collision.
  Refs: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA`,
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_Enter,ftCo_80091A4C,ftCo_GuardReflect_Enter}`.
- Dash root-motion exits carry the `Fighter_ChangeMotionState` terminal gr-velocity clamp before
  any resumed Dash IASA terminal scalar. `Fighter_ChangeMotionState` snapshots the previous
  motion's x594 root-motion flags, loads the destination motion, and if the destination is not
  root-motion owned it clamps `fp->gr_vel` to `co_attrs.dash_run_terminal_velocity`. For
  `Dash -> Turn` this clamp runs before `ftCo_Dash_IASA` falls through to the terminal scalar, so
  super-terminal Dash rows scalar from the terminal velocity rather than the replay-visible
  super-terminal Dash carry. `QuerulousGrandDinosaur.msl:8609:p0` is the rollout-critical lock.
  Refs: `refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState`,
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA`,
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Enter_Smash`.
- Dash/Run/RunDirect->AttackDash entry is also a same-proc ordering owner. Source runs the
  locomotion IASA callback before Phys, so an A press can enter AttackDash and run
  `ftCo_AttackDash_Phys -> ft_80085030` in the same fighter update. Runtime restores that narrow
  entry Phys velocity immediately after the source-shaped AttackDash entry tick; it is not a
  generic AttackDash position nudge. `TreasuredBackKangaroo.msl:2399:p1` locks the former stationary
  Dash->AttackDash frame, and `PositiveRevolvingHyena.msl:9954:p1` locks the matching Run entry
  frame that gates a downstream DownBound BODY contact.
  Refs: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA`,
  `refs/melee/src/melee/ft/chara/ftCommon/{ftCo_Run.c,ftCo_RunDirect.c}`,
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::{doEnter,ftCo_AttackDash_Phys}`,
  `refs/melee/src/melee/ft/ft_081B.c::ft_80085030`.
- `ft_80085030` / `ft_800850E0` must branch on `fp->x594_b0`, not on TransN track presence.
  `Fighter_ChangeMotionState` sources this bit from `ftData_80085FD4_ret.x10_b0`; SSANIMT1 v3
  stores it as `uses_root_motion`. This keeps AttackDash on the root-motion branch while ordinary
  Attack11 rows with TransN data, such as PRH's Squat -> Attack11 entry, use the ground-friction
  fallback. The same Attack11 entry edge must not seed `mv.co.attack1.x0`; `checkAttack11` clears
  it and `Attack11_IASA` only owns frames that began in Attack11.
  Refs: `refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState`,
  `refs/melee/src/melee/ft/ft_081B.c::{ft_80085030,ft_800850E0}`,
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::{checkAttack11,ftCo_Attack11_IASA}`.
- Grounded `ThrowF/B/Hi/Lw` Phys callbacks all call `ft_80085004 -> ft_80085030`. Weighted throws
  can advance the live AObj at fractional frames, so the root-motion velocity target must use the
  SSANIMT1/FObj float TransN owner rather than the integer SSANIM01 tail. `ThrownF/B/Hi/Lw` Phys
  and Coll remain empty; the root-motion ground-speed writer belongs only to the throw owner.
  Refs: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_ThrowF_Phys,ftCo_ThrowB_Phys,ftCo_ThrowHi_Phys,ftCo_ThrowLw_Phys}`,
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_ThrownF_Phys,ftCo_ThrownF_Coll}`,
  `refs/melee/src/melee/ft/ft_081B.c::{ft_80085004,ft_80085030}`,
  `refs/melee/src/sysdolphin/baselib/{aobj.c,fobj.c}`.
- The replay-real late Dash->Turn row `QuerulousGrandDinosaur.msl:5968:p0` exercises this ordering:
  the prior direct Dash IASA shortcut entered Turn with one fewer motion-entry bundle, while the
  decomp-shaped path reaches the same Turn through the callback-owned Dash end / destination
  selector ordering and matches the `x2088` instance id.
- Walk action-frame ownership is shared by `ftCo_Walk_Anim -> ftWalkCommon_800DFDDC` and Walk type
  retargeting by `ftWalkCommon_800DFEC8`. Runtime carries the callback-selected `mv_x0` in
  `walk_anim_source_vel`; replay one-step seeds reconstruct same-Walk steady rows from the next
  exposed Slippi walk rate as a narrow non-causal replay-facing hidden-owner lane because the
  callback-written rate is visible one row after the anim tick that consumed it. The general
  `frame_speed_mul_f32` seed lane remains causal. On Walk type-change rows, `ftWalkCommon_800DFDDC`
  has one hidden branch (`ft_GetGroundFrictionMultiplier(fp) < 1`) that chooses either hidden
  `mv.co.walk.x0` or current `gr_vel` for the tick before `ftWalkCommon_800DFEC8` remaps the phase.
  Replay one-step uses the narrow `walk_retarget_tick_source_vel_f32` lane only for those retarget
  ticks; this keeps `walk_anim_source_vel` as the callback-source lane instead of overloading it
  with retarget-only reconstruction.
  Fresh Walk entries can reach `ftCo_Walk_Anim` with a zero seed-owned timebase carry from the prior
  action; the first post-entry steady tick uses the entry motion speed (`1.0f`) before
  `ftWalkCommon_800DFDDC` writes the velocity-scaled Walk rate for the next frame.
- Walk/Run/other looping AObj timelines wrap in the shared immediate tick helper after entry ticks.
  This matters for Walk type-change rows where `ftWalkCommon_800DFEC8` retargets the phase and the
  next HSD AObj interpretation wraps the destination action-frame instead of leaving it past the
  destination end frame.
- Run action-frame ownership follows the same pattern for `ftCo_Run_Anim`: runtime carries the
  callback-selected `vel` in `run_anim_source_vel`; replay one-step seeds reconstruct same-Run
  steady rows from the next exposed Run rate as a narrow non-causal replay-facing lane, while
  keeping `frame_speed_mul_f32` causal.
- Run/RunDirect, RunBrake, TurnRun, and Dash jump admission use
  `ftCo_Jump.c::fn_800CAF78` (`p_ftCommonData->x80` stick-y threshold). Walk, Turn, Squat,
  Ottotto, and Landing continue to use the narrower `ftCo_Jump_CheckInput` edge path unless their
  own decomp owner says otherwise.
- Run/RunDirect `fn_800CAF78` entries into KneeBend do not run `ftCo_KneeBend_IASA` again in the
  same fighter proc. `Fighter_procUpdate` dispatches the frame-start motion state's input callback;
  a newly-entered KneeBend waits until the next proc before it can consume catch, SpecialHi, or
  AttackHi4/C-stick-up interrupts. This same callback boundary applies to Guard jump-OoS
  `ftCo_800CB024` KneeBend entries. Sources:
  `refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate`,
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_IASA`,
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::{fn_800CAF78,ftCo_800CB024}`,
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_IASA`.
- RunBrake animation-end dispatches the destination Wait input callback in the same fighter proc.
  `ftCo_RunBrake_Anim` exits through `ft_8008A2BC`; the following `ftCo_Wait_IASA` locomotion tail
  can immediately consume current stick into Turn/Walk/Dash/Squat/Jump. Runtime uses the shared
  Wait_IASA locomotion subset at this boundary instead of a squat-only terminal bridge.
- KneeBend short-hop ownership follows callback phase order. `ftCo_KneeBend_Anim` enters JumpF/B
  when `cur_anim_frame >= jump_startup_time`; `ftCo_KneeBend_IASA` calls
  `ftCo_KneeBend_Check_ShortHop` only while the state remains KneeBend after Anim. A jump-button or
  tap-jump release observed on the same frame as the Anim-owned takeoff is too late to set
  `mv.co.kneebend.is_short_hop`; runtime must use only an earlier latched bit or the replay seed
  on that takeoff frame. Source: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::{
  ftCo_KneeBend_Anim,ftCo_KneeBend_IASA,ftCo_KneeBend_Check_ShortHop}`.
- Mid-KneeBend seed derivation must preserve the entry callback that created
  `mv.co.kneebend.jump_input`. Dash/Run-family entries use `ftCo_Jump.c::fn_800CAF78`, so an
  L-stick jump can be sourced by `p_ftCommonData->x80` even when the stick is below the ordinary
  tap-jump `x74` threshold. Non-Dash/Run entries keep the normal `ftCo_Jump_GetInput` ordering.
- KneeBend startup-complete JumpF/B can still run destination Jump IASA in the same proc. After
  `ftCo_KneeBend_Anim -> ftCo_Jump_Enter`, `ftCo_Jump_IASA` reaches `ftCo_800CB870`, so a fresh
  current-frame jump edge/tap can immediately enter `JumpAerialF/B`. That path uses
  `ftCo_JumpAerial_Enter_Basic` velocity/jump ownership, overwriting the ground-jump X/Y velocity
  and consuming the remaining jump. `ftCo_Jump_Enter` writes `x671=0xFE` in the pre-input Anim
  phase, then `Fighter_Spaghetti_8006AD10` can overwrite that transient before `ftCo_Jump_IASA`
  reads `ft_did_jump`; fresh stick-threshold crossings become the post-frame x671 seed, while
  already-held stick-Y clamps back to `0xFE`. Sources:
  `refs/melee/src/melee/ft/chara/ftCommon/{ftCo_KneeBend.c,ftCo_Jump.c}` and
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::{ftCo_800CB870,ft_did_jump,
  ftCo_JumpAerial_Enter_Basic}`.
- Steady frame-start `JumpF/B` can also consume `ftCo_800CB870` before BODY collision. In
  teacher-forced rows whose seed came from a pre-input JumpF/B entry and a fresh stick-threshold
  crossing, runtime consumes the reconstructed x671 timer window; held-up rows remain `0xFE` and do
  not become a broad double-jump shortcut. Source path:
  `Fighter_Spaghetti_8006AD10 -> Fighter_procUpdate -> ftCo_Jump_IASA -> ftCo_800CB870`.
- Grounded AttackS4 frame-7 hold rows are owned by the extracted `start_smash_charge` action-script
  event (`data/moves/{fox,falco}.json::ftCo_SM_AttackS4`, hold_frames=60), not by a generic
  locomotion action-frame bridge.
- Grounded smash-charge knockback uses the same `smash_attrs` owner, not a contact-geometry
  shortcut. `ftAction_80073008` / opcode 56 seeds `SmashState_PreCharge`; `ftCo_800DF0D0` promotes
  it to `SmashState_Charging` only while current held A is set; `ftCo_Damage_CalcKnockback` then
  multiplies by `p_ftCommonData->kb_smashcharge_mul`. Replay rows like FSP AttackHi4 af=1 -> af=2
  BODY contact therefore get the 1.2x hitstun/KB path only when the current input promotes the
  extracted `start_smash_charge` event. Source: `refs/melee/src/melee/ft/ftaction.c::ftAction_80073008`,
  `refs/melee/src/melee/ft/ft_0DF0.c::{ftCo_800DEE84,ftCo_800DF0D0}`, and
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_CalcKnockback`; data:
  `data/moves/{fox,falco}.json` and `data/common/ft_common_data.json::kb_smashcharge_mul`.
- Released grounded-smash HitCapsule damage uses the attacker-side `smash_attrs` release state.
  `ftCo_800DF0D0` leaves `SmashState_Release` and the held-frame count live after A release;
  `ftColl_8007ABD0` calls `ftCo_800DEEB8` before writing `HitCapsule.damage`. One-step reseeds
  therefore carry `smash_charge_state`, held frames, hold max, and saved anim rate from the
  prefix-visible charge episode, derived in native preprocessing from MSLFTSC1 `start_smash_charge`
  and held-A history. This is separate from defender-side `kb_smashcharge_active`.
- Turn post-flip facing is owned by `ftCo_Turn_Anim_Inner`: once `frames_to_turn` has expired and
  `has_turned` is set, the first steady post-flip Turn row may need facing reconstructed from the
  decomp flip even on smash-turn rows whose `x8` latch still owns later Dash admission. The repair
  is limited to that one post-flip steady row and does not generalize to TurnRun or attack entries.
- First-tick Turn -> KneeBend facing is a narrow replay-facing hidden-owner reconstruction:
  `ftCo_Turn_IASA` temporarily exposes `mv.co.turn.facing_after`, then `ftCo_Jump_CheckInput` can
  enter KneeBend in the same callback, but Slippi only exposes the winning facing on the next
  post-frame. The `turn_kneebend_facing_override_u8` lane is non-causal, Turn-only, and does not
  change the general facing owner for later Turn phases.
- TurnRun final-frame down-stick rows use the decomp-shaped exit path:
  `ftCo_TurnRun_Anim` checks `fn_800CA644`; when the hidden exit microphase rejects Run, it enters
  Wait through `ft_8008A2BC` and the destination `Wait_IASA` selector owns Squat. The runtime keeps
  this branch local to final TurnRun down-stick rows.
- TurnRun final-frame Run handoff uses the post-pivot facing. `ftCo_TurnRun_Enter` leaves the
  motion-entry sign in `facing_dir1`; `ftCo_TurnRun_Anim` may then flip current `facing_dir` when
  its script-owned turn gate sees ground velocity cross the pivot. The animation-end `fn_800CA644`
  check tests held stick against current facing if that pivot is already exposed, otherwise against
  the pending final pivot.
- TurnRun mid-state pivot freeze is script-triggered and callback-consumed. MSLFTSC1 exposes the
  common `ftCo_SM_TurnRun` `set_cmd_var(idx=1)` event; `ftCo_TurnRun_Anim` consumes it by writing
  `frame_speed_mul=0` and marking `mv.co.turnrun.x14`. RunBrake -> TurnRun can enter with a
  preserved anim frame past the script event, but the freeze is consumed on the next steady TurnRun
  Anim callback, not on the entry row. Teacher-forced seeds that already expose the post-flip facing
  but still carry the prior zero rate resume before advancing the next tick (`src/anim_timebase.c`,
  `src/move_tables.c`; refs/melee/src/melee/ft/chara/ftCommon/ftCo_TurnRun.c::{
  ftCo_TurnRun_Enter,ftCo_TurnRun_Anim}; data/scripts/{fox,falco}.bin::ftCo_SM_TurnRun).
- TurnRun Phys keeps using the entry-facing `mv.co.turnrun.accel_mul`, not current facing after
  the mid-state pivot. `ftCo_TurnRun_Enter` copies the pre-turn `facing_dir` into
  `accel_mul`; `ftCo_TurnRun_Phys` tests `accel_mul * accel < 0` on later callbacks. Runtime maps
  this to `facing_dir1`, so post-flip TurnRun rows continue accelerating toward the opposite-stick
  target instead of falling back to friction. FSP `9687/9688` lock the post-flip acceleration owner.
- Pure motion-entry `instance_id` ownership remains `ft_800895E0/x2073` plus the global
  `plAttack_80037B08` counter by default. Simultaneous fighter entries and hidden prior consumers
  use `motion_entry_instance_id_override_u16`, a non-causal teacher-forced replay seed lane that
  supplies the post-entry `fp->x2088` when Slippi post-frames do not expose HSD fighter-proc/global
  counter order. It may be derived from replay `t+1` / post-frame state (`ref_t1`), so it is not
  prefix-causal engine truth. It is held live for the one-step frame so chained same-frame entries
  settle on the replay-visible final id, then cleared before rollout carry. Rollout/live runtime must
  not rely on this lane except for states explicitly reseeded from replay. Source-owned extra writers
  already modeled in runtime stay excluded from the teacher-forced hidden-order lane; currently that
  means AttackLw3's `x21EC -> ft_80089824` path, while SpecialN loop restarts remain in their
  explicit callback lane.
- For RL1.0 checklist closure, the two lanes above are explicit non-causal replay seed lanes, not
  fully causal runtime simulation state. Their validation-population audit is intentionally narrow:
  `turn_kneebend_facing_override_u8` populates only `Turn -> KneeBend` rows. The
  `motion_entry_instance_id_override_u16` lane remains tied to `ft_800895E0` /
  `ft_80089824` / `plAttack_80037B08` ordering evidence and is audited by action-family/action-id
  population tables so it cannot silently become an unreviewed catch-all.

#### Ledge system (cliff catch/occupancy/options)

| Read first (decomp) | Data artifacts (ISO-derived) | Code owner / gaps |
|---|---|---|
| `refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081298` (cliff catch check) | `data/stages/final_destination.json` (`segments[*].ledge` + segment endpoints) | `src/ledge.c` (new), `src/stage_collision.c` |
| `refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081370` (enter cliff catch) | (Need) ledge occupancy / cliff id representation (per-side, per-player) | Missing: cliff occupancy + refresh rules |
| `refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_CliffWait_IASA` | `data/common/ft_common_data.json` (ledge option windows / timers) | Missing: cliff options state machine |

#### Defense (shield, shieldstun/GuardSetOff, OoS options)

| Read first (decomp) | Data artifacts (ISO-derived) | Code owner / gaps |
|---|---|---|
| `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091A4C` (guard entry) | `data/common/ft_common_data.json` (shield health/decay/recharge constants) | `src/guard_lifecycle.h` names shared guard timers/provenance; `src/action.c`, `src/shields.c` consume it |
| `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_80099894` (roll/spotdodge) | `data/anims/{fox,falco}.bin` (escape anim end frames) | `src/action.c` (Escape*), `src/locomotion.c` (root-motion gaps) |
| `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardDamage_Anim` (GuardSetOff / stun loop) | `data/common/ft_common_data.json` (shieldstun duration rules) | `src/guard_lifecycle.h` centralizes timer/x10 representation; `src/combat.c` enters `GuardSetOff`; `src/action.c` owns per-frame GuardSetOff callbacks |
| `refs/melee/src/melee/ft/chara/ftCommon/ftCo_ShieldBreakDown.c`, `ftCo_ShieldBreakStand.c`, `ftCo_Furafura.c` | `data/common/ft_common_data.json` (`shield_break_reset_health`, Furafura timer constants x2F8/x2FC/x300/x304) | `src/action.c` owns `ShieldBreakDown -> ShieldBreakStand -> Furafura -> Wait` and Furafura mash/timer decrement |

GuardSetOff grounded motion note:
- Replay-real laser shield-hit entry rows already expose two live horizontal lanes on the same grounded `GuardSetOff` hitlag frame:
  - `speed_ground_x_self` is the fresh recoil `gr_vel`,
  - `speed_air_x_self` is the still-live grounded `self_vel.x` carried from the pre-contact locomotion row.
- Decomp-backed owner chain:
  - `refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC` stores the shield-hit sign lane in `specialn_facing_dir`,
  - `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C` consumes that lane to write `gr_vel`,
  - `refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate` does not collapse `self_vel.x` onto `gr_vel` on the same frozen entry row.
- Practical implication: do not fit item shield-hit GuardSetOff motion from replay `speed_ground_x_self` alone. The remaining blocker is the item-side `specialn_facing_dir` sign owner for those rows.

Guard lifecycle substrate:
- `src/guard_lifecycle.h` is the shared source vocabulary for GuardReflect `x14/x18` timers,
  GuardOn/GuardSetOff `x10` representation, GuardReflect no-submotion provenance, and
  ShieldDesc-vs-ReflectDesc ownership. This keeps `action.c`, `shields.c`, `state_flags.c`, and
  `combat.c` aligned with the same decomp owner boundaries instead of carrying parallel local
  predicates. Source anchors:
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800921DC,ftCo_80092450,ftCo_8009388C,ftCo_80093A50,ftCo_80093BC0,ftCo_GuardReflect_Anim,ftCo_GuardSetOff_Anim}`.

GuardSetOff post-hitlag ASDI:
- `ftCo_80092F2C` installs `post_hitlag_cb = ftCo_800932DC` on `GuardSetOff` entry.
- `Fighter_8006A1BC` calls `Fighter_8006D10C` when hitlag reaches zero before the next
  `Fighter_procUpdate` input refresh, so the callback consumes the prior input snapshot in the
  simulator's pre-input hitlag-exit phase.
- `ftCo_800932DC` is grounded-only and applies floor-tangent displacement when
  `abs(lstick.x) >= sdi_radius`: `floor.normal * (lstick.x * asdi_step_mul * shield_sdi_mul)`.
- Data/constants: `data/common/ft_common_data.json::{sdi_radius,asdi_step_mul,shield_sdi_mul}`.
- Replay-real locks: `AGN:4047` covers the positive last-hitlag displacement, `DCC:2235` covers
  the no-horizontal-stick negative, and `AGN:4035..4059` covers the rollout-visible prevention of
  the downstream AttackHi4 miss -> Catch/CaptureWait cascade.

GuardSetOff active-hitlag SDI:
- `ftCo_80092F2C` also installs `hitlag_cb = ftCo_80093240` on `GuardSetOff` entry.
- `Fighter_procUpdate` refreshes current input before calling the hitlag callback, so fresh
  horizontal stick entry can apply while shieldstun is still frozen in active hitlag.
- `ftCo_80093240` is grounded-only and applies floor-tangent displacement when `allow_sdi`,
  `abs(lstick.x) >= sdi_radius`, and callback-local `x670` is inside the SDI window:
  `floor.normal * (lstick.x * sdi_step_mul * shield_sdi_mul)`. This uses x4B8/x4C0, not
  post-hitlag ASDI x4BC/x4C0.
- Retained seed boundary: replay-visible fresh directional entry, plus the bounded high-damage
  first-carry phase where `x19A4` proves a GuardSetOff shield-hit owner, frame-start `x679 == 0`
  proves the input segment is still on its first carry tick, and hitlag remains active after the
  prio-0 decrement. Later held-stick `x679` ticks and last-hitlag rows have passed the
  callback-local reset boundary and stay excluded until an explicit callback-local `x670` lane is
  available. `FEH:5348` locks the fresh-entry positive, `FEH:5349` locks the high-damage first-carry
  positive, `FEH:5347` locks the outside-window negative, and `CNM:8214` locks a stale low-damage
  visible-counter carry negative.
- Source anchors:
  - `refs/melee/src/melee/ft/fighter.c::{Fighter_procUpdate,Fighter_Spaghetti_8006AD10}`
  - `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80092F2C,ftCo_80093240}`
- Validation posture: this source-complete active-hitlag slice improves aggregate rollout
  best/max/first/streak and one-step float/discrete totals. It also shifts
  `DelayedSuperbGuanaco.msl` median-only rollout distribution (`245 -> 199`) while DSG hard
  rollout metrics improve (`first 40 -> 38`, `non-seeded 19 -> 17`, `streak_count 39 -> 37`), so
  the median drop is documented as a distribution-only tradeoff rather than a replay-count red.

GuardSetOff post-hitlag GuardReflect timer bits:
- `GuardSetOff_Anim` calls `ftCo_80093BC0` on the first non-hitlag callback after
  `Fighter_8006A1BC` decrements hitlag to zero. `ftCo_80093BC0` clears `x221C_b1` from
  `mv.co.guard.x14` independently from the longer `x18`/`x221C_b2` powershield-active lane, so
  the first post-hitlag `GuardSetOff` row can legitimately publish only `x221C_b2` while x18
  remains live. Once both timers have expired, same-action steady `GuardSetOff` self-loops clear
  stale `x221C_b2` at the `guard.x10 == init - 1` countdown boundary when no post-hitlag owner lane
  remains.
- Source anchors:
  - `refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}`
  - `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardSetOff_Anim,ftCo_80093BC0,ftCo_80092F2C}`

GuardSetOff -> Guard overlap nudge:
- `GuardSetOff_Anim` can enter `Guard` through `ftCo_800928CC` before the same
  `Fighter_8006A360` pass reaches the common `ftCommon_8007E0E4` grounded-overlap helper. That
  same-frame destination `Guard` row still receives the `p_ftCommonData->x450` player nudge when
  pushboxes overlap. In rollout, `prev_action_id` has advanced to `Guard`, so this source handoff is
  recovered from the promoted `seed_prev_action_id` lane, but only for the direct `Guard`
  destination; GuardSetOff -> Escape turnover rows keep their separate locked owner.
- Source anchors:
  - `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardSetOff_Anim,ftCo_800928CC}`
  - `refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}`
  - `refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}`

#### Combat geometry (hurtcaps/hitboxes/shields overlap classification)

| Read first (decomp) | Data artifacts (ISO-derived) | Code owner / gaps |
|---|---|---|
| `refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70` (fighter-vs-fighter collision pass) | `data/hurtcaps/{fox,falco}.bin` (`MSLHURT1 v1`) + `data/anims/{fox,falco}*.bin` (pose) | `src/hurtboxes.c`, `src/hurtcaps_tables.c`, `src/anim_pose.c` |
| `refs/melee/src/melee/ft/ftaction.c::ftAction_80073240` (script timers use `cur_anim_frame`) | `data/hitboxes/{fox,falco}.bin` (`MSLHITB1 v1`) | `src/hitboxes.c`, `src/hitboxes_tables.c` |
| `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c` (shield bubble placement) | `data/shields/{fox,falco}.bin` (shield-tilt table) | `src/shields.c` (placement) |

- Stopped non-loop fighter AObj terminal pose:
  - `HSD_AObjInterpretAnim` sets `AOBJ_NO_ANIM` once a non-looping fighter animation reaches
    `end_frame`, but BODY collision still samples the stopped JObj local SRT via `lb_8000B1CC`.
  - Native probe evidence on Falco `DamageFlyHi` (`DistinctCaringCobra.msl:8565`) shows the high
    hurtcap JObjs at `curr_frame=end_frame=29`, `AOBJ_NO_ANIM`, carrying the final loaded FObj
    `p1` local SRT, not the pre-stop interpolated frame-28 value. The extractor/native bake path
    mirrors that terminal value for generated `data/anims/{fox,falco}*.bin` so active-hitstun
    DamageFly victims use the same stopped pose for BODY candidate ordering.
  - This is an extraction/AObj semantics fix, not a DamageFly row shortcut: runtime still consumes
    the ordinary pose tables through `src/hurtboxes.c` and the normal `ftColl_80078C70` BODY path.
  - Sources: `refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim`,
    `refs/melee/src/sysdolphin/baselib/fobj.c::HSD_FObjInterpretAnim`,
    `refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Anim`.

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
| `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_ThrowF_Anim` | Throw release/params (script-derived) | **PARTIAL**: the common `ThrowF/B/Hi/Lw -> Thrown*` attached/release substrate and shared release callback/timeline path are now shared by default in `src/grab_attachment.c` / `src/throw_flow.c` / `src/mpcoll_*`; remaining gaps are the intentional per-throw article / pulse differences, including ThrowLw's attached pulse-25 post-hitlag anim-rate owner in `src/anim_timebase.c` (not more generic throw-core substrate debt). |

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
- Grab/Capture/Throw completeness: common Fox/Falco throw/thrown substrate is in; remaining grab/throw mismatches now concentrate in
  `Capture*` follow-up frames and the intentional per-throw blaster pulse/article families (`ThrowB` / `ThrowHi` / `ThrowLw`) rather than shared
  attached/release substrate debt.
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
   - Acceptance checks: reduce remaining ledge mismatch clusters without action-specific hacks; keep
     `Collide_LedgeGrabMask` generation in the collision-env owner.

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
- Stale-move queue (staling) + damage multipliers: seeded via replay-history derivation, but
  `fp->x206C_attack_instance` is not exposed by Slippi. The derivation models the `ft_800890D0`
  action-transition bump and the Fox/Falco SpecialN Loop -> Loop `ft_800892A0` bump that gives
  repeated blaster shots distinct stale-table identities. Other `ft_800892A0` callsites remain
  owner-specific until they become suite-visible.
- Ledge occupancy + ledge refresh timer(s) + per-action ledge regrab restrictions.
- Ledge option `mv.co.cliff.x8` gate is currently approximated via a “previous-stick neutral reset” check for climb/drop on CliffWait
  (`src/ledge.c`); same-proc CliffCatch -> CliffWait IASA intentionally excludes climb/drop because
  `ftCo_8009A804` initializes `mv.co.cliff.x8 = 0`.
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

Fox/Falco special-owner split (2026-04-17):
- The previous broad `F10e_special_move_adjacency` taxonomy bucket is split by decomp owner:
  - `F19_specialn_blaster_article`: SpecialN Start/Loop/End, aerial landing handoff, blaster gun,
    laser article, and SpecialN damage/landing exits. Source paths:
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c`,
    `refs/melee/src/melee/it/items/itfoxblaster.c`, and
    `refs/melee/src/melee/it/items/itfoxlaser.c`.
  - `F20_speciallw_shine_reflector`: SpecialLw Start/Loop/Hit/Turn/End, release-lag latch,
    air/ground handoff, reflector bubble, and Shine contact. Source paths:
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c` and
    `refs/melee/src/melee/ft/ftcoll.c::ftColl_CreateReflectHit`.
  - `F21_specials_illusion_phantasm`: SpecialS Start/Main/End, air/ground handoff,
    LandingFallSpecial source lag, ghost article spawn/position, and Illusion/Phantasm contact.
    Source paths: `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c` and
    `refs/melee/src/melee/it/items/itfoxillusion.c`.
  - `F22_specialhi_firefox_firebird`: SpecialHi hold/launch/fall/landing/bound, launch travel,
    XRotN pose, and Firefox collision continuation. Source path:
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c`.
  - `F23_special_common_entry_dispatch`: common grounded/aerial special input dispatch into
    Fox/Falco special starts. Source paths:
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D68C0`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialS.c::ftCo_SpecialS_CheckInput`, and
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput`.
  - `F24_special_adjacent_instance_order`: source-backed special motion-entry instance counter
    rows, currently limited to SpecialN loop restarts that install OnChangeAction and call
    `ft_80089824`. Direct special-boundary instance-only rows without that callback are generic
    `ft_800895E0` / `plAttack_80037B08` adjacent instance ordering (`F12b`) or damage identity
    (`F08a`).
- `F13_specialhi_landing` is no longer a mixed FireFox/common-freefall label. True FireFox rows
  go to `F22`; common `FallSpecial` / `LandingFallSpecial` / `EscapeAir` rows go to
  `F13a_common_fallspecial_landing`. `ThrowLw` / `ThrownLw` and other throw-side blaster pulse
  rows go to `F14b_per_throw_pulse_bookkeeping`.
- Runtime change retained from this pass:
  - `SpecialAirLwLoop` / `SpecialAirLwEnd` floor contact uses a narrow locked-bottom mpColl subset
    while an ECB lock is active, so `ftFx_SpecialAirLw{Loop,End}_Coll -> ft_80081D0C ->
    AirToGround` can resolve ground contact without broad startup grounding. The subset requires
    the frame-start action to already be Loop/End; otherwise `SpecialAirLwStart_Anim` can enter
    Loop before collision and incorrectly inherit the loop/end floor-contact policy on the startup
    handoff frame.
  - `SpecialAirLw* -> SpecialLw*` and `SpecialHiFall -> SpecialHiLanding` handoffs refresh
    `jumps_left` through the `ftCommon_8007D7FC` owner. SpecialHiFall -> SpecialHiLanding also
    copies live self velocity into `gr_vel` through `ftCommon_8007D6A4`; `ftFx_SpecialHiLanding_Phys`
    then applies the character x7C ground-momentum friction before common ground movement.
  - `SpecialLwEnd` / `SpecialAirLwEnd` anim-end exits call `ftCommon_8007DB24` and
    `ftCommon_8007D92C`; the simulator now models the locked same-frame destination IASA slices
    covered by replay-real rows: grounded Wait can enter ordinary/backward Turn through
    `ftCo_Turn_CheckInput`, and aerial Fall can consume JumpAerial input through
    `ftCo_Fall_IASA_Inner -> ftCo_JumpAerial_CheckInput`. Source paths:
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{ftFx_SpecialLwEnd_Anim,ftFx_SpecialAirLwEnd_Anim}`,
    `refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D92C`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c`, and
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA_Inner`.
  - Shine Anim-callback B-release latch uses the pre-input snapshot, matching
    `Fighter_8006A360` running before `Fighter_procUpdate`; current-frame inputs still own
    SpecialLw entry and IASA. Source paths:
    `refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}` and
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{ftFx_SpecialLwStart_Anim,ftFx_SpecialLwLoop_Anim,ftFx_SpecialLwTurn_Anim,ftFx_SpecialLwHit_Anim}`.
  - Aerial Reflector Phys owns vertical fall through `mv.fx.SpecialLw.gravityDelay`: Start
    suppresses fall until the extracted `reflector_gravity_delay_frames` countdown expires, while
    Loop/Hit/Turn/End apply `reflector_fall_accel` through `ftCommon_Fall`. The Start ->
    Loop/Turn continuation handoff frame itself does not double-tick fall; the next continuation
    frame does. Sources:
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{ftFox_SpecialLw_SetVars,ftFx_SpecialAirLwStart_Phys,ftFx_SpecialAirLwLoop_Phys,ftFx_SpecialAirLwHit_Phys,ftFx_SpecialAirLwTurn_Phys,ftFx_SpecialAirLwEnd_Phys}`,
    `data/characters/{fox,falco}.json::{reflector_gravity_delay_frames,reflector_fall_accel}`.
  - Shine reflector contact now applies the reflect-hit callback path and item-owned reflect
    transfer for laser overlap. Grounded Shine overlap also tests the projectile-origin segment
    because `ftColl_80077464` receives `Item*` and uses `item->pos` for reflect-direction
    ownership; aerial overlap remains on the extracted laser hitbox offsets to avoid re-reflecting
    already nearby projectiles during `SpecialAirLwHit`. Source paths:
    `refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_80077464}`,
    `refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC`,
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLwHit_Enter`, and
    `refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Phys,it_8029C4D4}`.
  - Shine Start hitlag rows with explicit replay-derived x1990 provenance preserve x198C=2 through
    the hitlag-exit frame. Similar Start rows without x1990/x1994 provenance still need a seed or
    forensic lane; they are not patched from replay reference.
  - Sustained `EscapeAir` ECB-lock floor-hug rows can remain airborne while root Y is already at
    the floor bias. Dolphin probe `reports/triage/20260418T081729Z_dolphin_forensic_row` confirms
    ImpassionedAlarmedTarsier rec=5809 keeps `ground_or_air=Air` through this floor-hug window.
    Runtime suppresses only sustained, non-ledge `EscapeAir` floor-bias resting/sweep grounding
    under active ECB lock; fresh jump/air-dodge entry landing remains on `ft_80082C74`.
  - `FallSpecial_Coll` uses `ft_80083090` and enters `LandingFallSpecial` through
    `ftCo_80096D28` on accepted floor contact. Runtime no longer uses a post-physics root
    projection bridge for this owner: it requires the `mpColl_80047E14`/`mpColl_80043754`
    callback-visible floor sweep to accept first, then applies the `mpColl_80044838_Floor` snap
    from that accepted callback result. Replay-real locks cover both platform/hard-floor rows that
    remain `FallSpecial` until the callback-visible sweep crosses and the following positive
    `LandingFallSpecial` frame. Static soft-platform first root crossings are kept airborne for the
    platform-callback handoff; the next already-below-platform callback owns the
    `LandingFallSpecial` publication.
  - `ftCo_EscapeAir_Anim` enters `FallSpecial` through `ftCo_80096900`, whose `inline0` calls
    `Fighter_ChangeMotionState(..., Ft_MF_KeepFastFall)`. Runtime preserves the fastfall bit and
    lets that same entered `FallSpecial_Coll` frame consume the callback-visible floor owner when
    the source sweep accepts; the fresh handoff uses the just-loaded FallSpecial ECB bottom from
    `data/ecb/*_bottom.bin`, so rows where that live bottom is still above floor remain airborne in
    FallSpecial. Sustained FallSpecial rows use the carried CollData/root endpoint, except shallow
    rows whose frame-start root is already inside the loaded FallSpecial ECB-bottom neighborhood;
    those consume the current ECB endpoint from `mpColl_LoadECB_inline` rather than a later generic
    projection bridge. The same
    `ftCo_80096900` call writes `mv.co.fallspecial.landing_lag`; runtime carries that scalar
    (EscapeAir common `x344`, Illusion/Phantasm `da->x50`, Firefox/Firebird `da->x90`) into
    `ftCo_LandingFallSpecial_Enter` so the entry row advances at `(end_frame + 0.1) / landing_lag`.
    Landing and `LandingFallSpecial` root-y publication consumes the same mpLib floor-contact owner
    as ordinary floor projection; `mpLib_8004DD90_Floor` contributes the small floor bias once, so
    entry code must not add a second simulator-local bias on top of current mpColl contact.
  - `EscapeAir_Coll` uses `ft_80082C74` and can enter `LandingFallSpecial` from the persisted
    CollData floor index. One-step reseeds can start after the previous ECB bottom has already
    crossed that floor, so runtime adds a bounded projection only when the frame-start previous ECB
    bottom is already below the persisted floor. Active-lock rows whose previous ECB bottom is still
    above floor remain airborne through the existing negative locks; the rejected broad EscapeAir
    projection is not restored. Locked same-frame JumpAerial -> EscapeAir rows use the
    frame-start `CollData.last_pos` owner from `ft_80082C74`, not the older replay t-1
    `floor_sweep_prev_pos` lane; while `CollData_X130_Locked` is live, runtime preserves the
    desired ECB bottom through named EscapeAir locked-bottom owners: replay-seeded CollData_X130,
    live JumpAerial soft/height-transform provenance, and live JumpAerial hard-floor provenance.
    On non-FD legal-stage hard floors and platform-domain same-frame entries, replay-prefix rows
    that expose an active lock but no desired-bottom lane use the same frame-start root sweep; FD
    stays on the existing desired-bottom/locked-owner branch because owner-zero free-running FD
    states are a separate hidden-state gap. Sustained soft platforms and ledges remain on their
    separate EscapeAir guards. Sources:
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll`,
    `refs/melee/src/melee/ft/ft_081B.c::ft_80082C74`, and
    `refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80043754,mpColl_80044628_Floor,mpColl_80044838_Floor}`.
  - Sustained `AttackAirN`/`AttackAirHi`/`AttackAirLw` on FoD height-transform platforms uses the common
    `AttackAir_Coll -> ft_80082C74 -> mpColl_800471F8` floor handoff, but the moving-platform
    transform can expose an ECB-bottom platform contact before the retained submotion owner should
    publish `LandingAir*`. Runtime keeps the generated `MSLMSO01` N/Hi/Lw submotion boundary
    airborne for four source-shaped cases: down-held endpoint contacts that publish the explicit
    `CollData.floor_skip` lane, shallow first contacts while the extracted MSLFTSC1 first
    HitCapsule create->clear phase is live, already-live `floor_skip` carry into the first
    hard-floor crossing after that platform pass, and height-transform platform writebacks where
    both callback root samples are already below the platform and the ECB-bottom precondition for
    `mpColl_80044838_Floor(ignore_bottom=true)` is absent. Released/no-owner first crossings,
    source-trusted FoD platform contacts where the root is already deeper than the shallow
    first-phase band, static platforms/supports, ordinary hard floors, and other AttackAir submotions
    still publish `Landing`/`LandingAir*` through the normal floor handoff. Replay-prefix rows
    reconstruct the same hidden episode as `floor_skip_segment_id_u16/valid` from generated MSLMSO01
    submotion ownership, MSLFTSC1 first hitbox phase, current/prior input, and generated FoD
    transform metadata. Sources:
    `data/motion_state/owners/{fox,falco}.bin::MSLMSO01`,
    `data/scripts/{fox,falco}.bin::MSLFTSC1`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll`,
    `refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}`,
    `refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}`,
    and `data/stages/bin/griz.json::platform_transforms`.
  - Same-frame `JumpAerial -> EscapeAir` keeps the pre-entry JumpAerial `CollData.ecb` as
    `prev_ecb` for the EscapeAir map callback. Runtime reconstructs that bottom point from the
    generated `MSLMSO01` submotion id for the frame-start JumpAerial action instead of sampling the
    entered EscapeAir pose for both endpoints. This lets the real
    `mpCollInterpolateECB -> mpColl_80044628_Floor` bottom sweep publish late transformed-platform
    `LandingFallSpecial` rows. The older down-held EscapeAir transformed-platform fallback is
    removed: platform pass-through is owned by explicit `CollData.floor_skip` and callbacks that
    pass `ftCo_80096CC8`; `EscapeAir_Coll -> ft_80082C74` does not use that callback.
    The same unlocked, already-visible EscapeAir pre-entry handoff keeps shallow static-platform
    oversteps airborne when the final root correction is larger than the current EscapeAir vertical
    step; those rows have not reached the source floor-publication phase even though the
    callback-local bottom sweep can see the platform. Same-frame `JumpAerial_IASA -> EscapeAir`
    entries, rows with preserved desired-bottom / CollData_X130 ownership, and deeper same-owner
    Dream Land crossings remain ordinary `EscapeAir_Coll` landings. A carried FoD height-transform
    platform floor id that is already horizontally off-span is still a valid CollData_X130 source
    owner for the EscapeAir entry: runtime preserves the JumpAerial desired bottom for the following
    callback instead of clearing it against the stale transformed platform height, and the adjacent
    hard-floor handoff then lands through the ordinary `EscapeAir_Coll` floor search. Direct
    replay-seeded owner-1 rows from the same `JumpAerial -> EscapeAir` lineage use the explicit
    desired-bottom seed lane to admit non-platform floor projection only when that desired bottom
    crosses the carried floor this frame; sustained `EscapeAir -> EscapeAir` lock rows stay on their
    separate source window. First-frame JumpAerial entries whose preserved desired bottom is the
    zero-bottom handoff can also publish a FoD transformed-platform floor when the previous/root
    projection proves the same platform crossing; the final missing-bottom guard does not discard
    that proven `EscapeAir_Coll` root projection, while later carried JumpAerial frames stay on the
    existing interpolation/airborne controls. Fresh ground-jump `JumpF/JumpB -> EscapeAir` entries
    have a separate FoD height-platform owner: when `ftCo_80099A58` enters EscapeAir before
    Fighter_procMap, `EscapeAir_Coll/mpColl_800471F8` may accept the source-trusted grIzumi
    height-transform platform under the root even though `CollData.floor.index` still names the main
    floor. Sustained `EscapeAir` rows at high `CollData_X130_Locked` countdowns remain in
    `mpCollInterpolateECB`'s gap and cannot use root projection alone to snap onto a distinct FoD
    side platform; countdown <= 2 is the retained side-platform publication phase. That owner is
    restricted to generated `MSLSTG01` height-transform platforms with source-trusted live height;
    ordinary stages and ground-jump airdodges without a height-platform line remain on the existing
    airborne guards.
    Sources: `data/motion_state/owners/{fox,falco}.bin::MSLMSO01`,
    `data/stages/bin/griz.json::platform_transforms`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll`,
    `refs/melee/src/melee/ft/ft_081B.c::ft_80082C74`, and
    `refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB,mpColl_80044628_Floor,mpColl_80044838_Floor,mpUpdateFloorSkip}`.
  - Sustained locked `EscapeAir` transformed-platform projection keeps the same source split:
    remaps to a distinct transformed floor can stay airborne during the early lock window, but a
    same-carried transformed floor accepted by the sustained `EscapeAir_Coll` handoff is an ordinary
    `LandingFallSpecial` callback result. Similarly, flat cliff/ledge final guards only suppress
    shallow snaps; once the accepted floor is deeper than the entered EscapeAir ECB-bottom extent
    plus mpColl's vertical ECB unit, `ft_80082C74 -> mpColl_800471F8` owns the landing. Sources:
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll`,
    `refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}`, and
    `refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}`.
  - Sustained `JumpF`/`JumpB`, `Fall`, and `JumpAerial` fastfall rows use the same source split: FoD
    height-transform platform contacts can remain airborne until the callback-local floor handoff
    publishes, but static-y stage-object support transforms are ordinary pass-through platform
    landings. In particular, a neutral-stick `Fall_Coll -> ft_800831CC -> mpColl_80047E14`
    crossing onto Randall publishes `Landing` on the generated support floor instead of reusing the
    FoD height-transform suppression. Runtime now publishes the same generated transformed-platform
    `CollData.floor_skip` when a common-air root crossing is rejected by `ftCo_80096CC8`, then
    preserves that skip while the same airborne common-air callback family remains active. This
    closes free-running `JumpB` pass-through rows that start before the replay-prefix seed lane is
    serialized. Replay-prefix FoD rows also reconstruct active `CollData.floor_skip` for
    `Fall_Coll`/`Jump_Coll` because `ft_800831CC` and `ft_800835B0` pass the same
    `ftCo_80096CC8` soft-platform predicate as JumpAerial. `ElatedWearyTermite.msl:5954` and
    `ParallelTemptingElk.msl:1510` lock that carry after a down-held transformed-platform pass;
    adjacent FoD AttackAir rows remain separate evidence for the deeper ECB-bottom or
    scheduler-phase owner rather than this common-air floor-skip lane. Sources:
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Coll`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Coll`,
    `refs/melee/src/melee/ft/ft_081B.c::{ft_800831CC,ft_800835B0}`, and
    `refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80047E14}`.
  - FoD platform height seeds now retain a three-bit source bitmask in the former stage padding lane:
    direct Slippi `fod_platform` current-height events, current grounded-contact reconstruction,
    and same-step contact reconstruction. Runtime transformed-platform trust uses that source bitmask
    to distinguish current grIzumi/mpLib platform heights from stale sparse carries without enabling
    the free-running FoD scheduler on teacher-forced replay seeds. `ParallelTemptingElk.msl:9136`
    locks the low-left-platform same-step contact row. When a rollout seed starts while a side
    platform is parked at the generated hidden target, preprocessing may also seed the current
    hidden-return countdown from the next source-visible upward platform height. Runtime installs
    that lane only as grIzumi phase 4 at `platform_motion.hidden_target_height`; it is not a broad
    sparse-height source bit and does not reconstruct arbitrary visible wait/target RNG phase.
    The free-running match-start scheduler preserves the
    `grIzumi_801CC358` lower-visible-stop boundary: when the platform reaches the generated
    `min_visible_height`, source publishes that final movement frame before the next wait phase
    installs `xC6`, so the later hidden-descent choice samples the following frame-start HSD value.
    `MilkyGracefulStingray.msl:720/1729` locks both the first visible descent and the later hidden
    descent decision without shifting ordinary home/max-height waits. Sources:
    `refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358`,
    `refs/melee/src/melee/mp/mplib.c::mpLib_80055E9C`, and
    `data/stages/bin/griz.bin::MSLSTG01 platform_transforms`.
    Same-step platform-contact rows may also seed a deferred FoD velocity: the source collision
    height belongs to the current landing frame, while the next grIzumi delta becomes valid only
    after that frame. Runtime promotes that deferred velocity during post-frame transient cleanup so
    sustained grounded riders follow the moving platform without pre-advancing the landing callback.
    `ParallelTemptingElk.msl:3257->3352` locks that boundary.
    The live match-start scheduler updates the platform geometry, but sustained `Landing` /
    `LandingAir*` callbacks still preserve their carried hard-floor CollData unless a same-step
    contact bit, a fresh action-entry handoff, or live grIzumi velocity within the current CollData
    ECB lift envelope proves the side platform is the callback-local floor owner;
    `ElatedWearyTermite.msl:838` locks the stale no-snap boundary and
    `ElatedWearyTermite.msl:1537->1538` locks the live moving-platform release retry.
    Source-owned FoD heights that are within generated grIzumi initial/target constants snap the
    transformed collision line to the extracted constant before mpLib floor projection. This is not
    a generic platform-y clamp: it covers named JObj poses from `MSLSTG01` (`platform_transform`
    initial heights and `platform_motion` home/min/max/hidden heights) so rounded Slippi
    `fod_platform` event floats do not move source collision floors by one floor-bias unit.
  - A replay rollout seeded at true match start (`frame_id <= -123`) may trust the extracted FoD
    initial side-platform heights even when the sparse Slippi platform event/contact lane is not yet
    marked valid. `grIzumi_801CCBDC` initializes the side-platform JObjs from stage data before the
    first match frame and before later random target/phase updates; this is narrower than enabling
    the hidden scheduler from an arbitrary replay seed. `MilkyGracefulStingray.msl:442` locks the
    initial-height side-platform Damage landing, while later invalid sparse rows still require
    explicit source height/velocity evidence. Sources:
    `refs/melee/src/melee/gr/grizumi.c::{grIzumi_801CCBDC,grIzumi_801CC358}` and
    `data/stages/bin/griz.bin::MSLSTG01 platform_transform.y_const`.
  - Supported-stage floor material friction is generated into `MSLSTG01` per floor segment from
    `mpLib_800569EC(MapLine.lo_flags & 0xFF)`. Grounded knockback decay and grounded
    attacker-shield pushback now use the current `ground_id`'s generated material multiplier before
    falling back to the legacy seed lane when no loaded stage table is available. This closes FoD
    Passive slide rows on material-2 floor segment `3`, where vanilla decays grounded KB by
    `1.5 * co_attrs.gr_friction * p_ftCommonData->x200` instead of the seeded default `1.0`.
    Sources:
    `refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate`,
    `refs/melee/src/melee/ft/ft_081B.c::ft_GetGroundFrictionMultiplier`,
    `refs/melee/src/melee/mp/mpcoll.c::mpColl_8004CA6C`,
    `refs/melee/src/melee/mp/mplib.c::mpLib_800569EC`, and
    `data/stages/bin/*.bin::MSLSTG01 segment.ground_friction_mul`.
  - Sustained `JumpAerialF/B` static-platform pass-through uses the generated pose ECB bottom as
    the final publication boundary. A just-released down-held platform pass can leave the
    callback-local JumpAerial root below a static soft platform; when the generated pose bottom is
    shallowly below that platform, the lite sim's zero-bottom/root projection must not publish
    `Landing` from a floor the source `ftCo_80096CC8` platform callback is still rejecting. Released
    rows whose live pose bottom remains above the platform keep the ordinary landing path even when
    their roots are below the platform. Sources:
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Coll`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8`,
    `refs/melee/src/melee/ft/ft_081B.c::ft_800835B0`, and
    `refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}`.
  - Replay rollout keeps a live `frame_id` clock for generated stage-object motion even when the
    Slippi frame-start RNG seed remains seed-owned. Randall collision consumes
    `data/stages/bin/grst.bin::MSLSTG01 platform_path` records through `Ground_801C2FE0` /
    `grStory_801E3370`; freezing the frame clock at the replay reseed row can collide aerial
    Side-B or common-air callbacks with an old cloud phase. Normal `reseed_seed()` remains
    one-step/teacher-forced and does not advance `frame_id` or `frame_pre_random_seed`; for
    no-live-Heiho Yoshi's Story scheduler rows, preprocessing stores
    `seed_t.frame_pre_random_seed` from the simulated pre-frame (`input_t := pre(i)`) so
    `grStory_801E3418` starts from the callback frame's Slippi/HSD seed rather than the previous
    post-frame row. For replay rollouts that start before the timer reaches zero, preprocessing
    also stores `stage_yoshi_shyguy_spawn_rng_seed_u32`, the source frame-start stream for that
    future zero-timer spawn callback; runtime installs it only when the scheduler consumes Shy Guy
    RNG.
  - `Fall_Coll` fastfall rows whose loaded ECB bottom is above the fighter root can still publish
    hard-floor and ledge-floor landings through the shared flags-6 callback owner when a
    prefix-causal `CollData_X130_Locked` owner or a true adjacent ledge-floor continuation owns the
    crossing. Runtime asks the generated stage graph and `ftCo_80096CC8` platform predicate for the
    callback-visible root crossing, then applies the source
    `mpColl_80044838_Floor(ignore_bottom=true)` root projection. The retained negatives are stale
    non-fastfall lock rows, stale same-ledgerow floor.index snapshots, and the probed expanded-model
    non-fastfall center-floor -> ledge first-crossing window, where live `CollData.ecb.bottom`
    remains above the ledge floor for one more callback; fastfall ledge crossings remain on the
    ordinary `Fall_Coll` landing path. Sources:
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll`,
    `refs/melee/src/melee/ft/ft_081B.c::ft_800831CC`, and
    `refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044628_Floor,mpColl_80044838_Floor}`.
  - Terminal `DamageFly*` rows can enter `Fall` through `ftCo_DamageFly_IASA ->
    ftCo_DamageFall_IASA -> ftCo_Fall_Enter` before the same frame's map callback. The destination
    `Fall_Coll` still consumes the callback-local `CollData.floor.index` and floor-sweep root
    carried from the source DamageFly state; if that carried root and the current root are already
    below the same hard floor, `mpColl_80044838_Floor(ignore_bottom=true)` may project the root to
    Landing immediately. Runtime bounds this to hard non-ledge floors, terminal post-hitstun Fall
    entries from DamageFly-family source state, and the callback-visible carried-root displacement
    rather than a replay row. Sources:
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_IASA`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{ftCo_Fall_Enter,ftCo_Fall_Coll}`,
    `refs/melee/src/melee/ft/ft_081B.c::ft_800831CC`, and
    `refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044838_Floor}`.
  - `Fall_Coll` and `JumpAerial_Coll` transformed/stage-object platform rejection paths must
    preserve the rejected airborne root Y when the final publication guard keeps the fighter
    airborne; otherwise a suppressed landing still leaves the root snapped to the platform floor.
    `JumpAerial_Coll` uses that stay-airborne owner only while a generated platform floor.index is
    already carried. Fresh hard-floor-index rows that cross onto FoD's side platforms publish
    ordinary Landing through `ft_800835B0`, even at terminal fastfall speed. On Fall's looping AObj
    boundary, a carried generated platform floor.index can remain live for the first post-wrap
    callback before a non-ledge hard-floor handoff publishes Landing. The retained Fall split is the
    generated platform floor owner plus `AOBJ_LOOP` timing: loop-wrap hard-floor rows stay airborne
    only while the callback-local previous root is still above the accepted hard floor; adjacent
    ledge-floor rows and the following already-below hard-floor row still land through ordinary
    `Fall_Coll`. Sources:
    `data/stages/bin/*.bin::MSLSTG01 platform transform records`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{ftCo_Fall_Anim,ftCo_Fall_Coll}`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Coll`,
    `refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBE8`,
    `refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim`, and
    `refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044838_Floor}`.
  - While `CollData_X130_Locked` is live, `mpColl_LoadECB_inline` refreshes the current pose
    extents but preserves `desired_ecb.bottom.y`. Teacher-forced reseed now carries that hidden
    desired bottom through `ecb_lock_bottom_rel_y_f32/valid` for air-jump-origin lock episodes,
    derived natively from extracted ECB tables and replay-prefix lock history; rollout carries the
    live `coll_desired_ecb_bottom_rel_y` field. The retained runtime consumer is currently scoped to
    active-lock `EscapeAir_Coll`, the Dolphin-probed owner family; extending AttackAir/Damage/
    SpecialAirN consumers needs separate row locks because the broad consumer regressed current
    aggregate validation. FoD side-platform EscapeAir rows can expose the timer-3 frame before the
    source callback publishes a floor result, but the same locked CollData desired bottom must carry
    into the timer-2 frame where `EscapeAir_Coll` accepts the transformed platform. Runtime preserves
    FoD replay-seeded CollData_X130 desired-bottom rows only after the current floor decision has
    completed; it does not let replay-seeded desired-bottom evidence publish a same-frame landing.
    The timer-2 landing still goes through the existing FoD platform bottom-sweep/root-projection
    owner. Sources:
    `refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4`,
    `refs/melee/src/melee/ft/ftcommon.c::ftCommon_UnlockECB`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll`,
    `refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB,mpColl_80044628_Floor,mpColl_80044838_Floor}`,
    `data/ecb/*`.
  - FireFox/FireBird rebound ownership now includes `SpecialAirHi` floor collision into
    `SpecialHiBound` and airborne `SpecialHiBound` anim-end into common `FallSpecial`, consuming
    jumps as `ftFx_SpecialHiBound_Anim` writes `x1968_jumpsUsed = max_jumps`.
    Sources: `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    ftFx_SpecialAirHi_Coll,ftFx_SpecialHiBound_Enter,ftFx_SpecialHiBound_Anim}`.
  - `SpecialHiFall` / `SpecialHiBound` exits that call `ftCo_80096900` preserve the pre-entry
    fastfall bit because the helper enters `FallSpecial` with `Ft_MF_KeepFastFall`. The common
    motion-state table does not encode this callsite-specific flag, so `enter_fall_special_via_ftco_80096900`
    restores `fall_fast` after the shared motion-entry bundle.
    Sources: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::inline0`,
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    ftFx_SpecialHiFall_Anim,ftFx_SpecialHiBound_Anim}`.
  - `SpecialHiLanding_Anim` enters Wait during the Anim callback, and destination Wait IASA can
    consume Walk/Squat/Turn input in the same fighter proc. The runtime returns to the grounded
    locomotion IASA tail after the Wait motion change rather than stopping at a raw Wait snapshot.
    Sources: `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiLanding_Anim`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA`, and
    `refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}`.
  - Pure `state_flags[4]` taxonomy rows are now split to `F25_camera_box_visibility_x221f`.
    Slippi records fp+0x221F for camera-subject visibility; those rows are owned by
    `ftLib_80086A8C` / `Camera_80030CFC` and stage camera bounds, not by the visible special
    action. Sources:
    `refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm`,
    `refs/melee/src/melee/ft/ftlib.c::ftLib_80086A8C`,
    `refs/melee/src/melee/cm/camera.c::Camera_80030CFC`, and `src/state_flags.c`.
  - `SpecialNEnd_Anim` exits through `ft_8008A2BC`; the destination Wait IASA can then consume the
    buttonless forward `ftCo_Dash_CheckInput` branch in the same fighter proc. The retained runtime
    slice is limited to that forward Dash branch and does not claim earlier Wait_IASA button owners
    or the backward turn-smash branch. Sources:
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialNEnd_Anim`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA`, and
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_CheckInput`.
  - `SpecialAirNEnd_Anim` exits through `ftCo_Fall_Enter` when the blaster landing-lag attr is
    zero; the destination Fall IASA can then consume same-proc JumpAerial input. The retained
    runtime slice is limited to the destination Fall double-jump path and uses the shared
    `ftCo_JumpAerial_Enter_Basic` velocity/jump/ECB-lock ownership. Sources:
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirNEnd_Anim`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Enter_Basic`, and
    `refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4`.
  - Shine Start seed history now preserves the hidden x198C lane when the entry script's x1988=2
    masks a prior visible x198C=1 row, and carries that provenance through hitlag-frozen frame-1
    starts. It also trusts explicit x1990+x1994 seed provenance on Cliff/Fall -> SpecialAirLwStart
    rows instead of dropping both timers under the x1988 mask. Sources:
    `refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm`,
    `refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B62C,ftColl_8007B760,ftColl_8007B7A4,ftColl_8007B868}`,
    `refs/melee/src/melee/ft/fighter.c::Fighter_8006A360`, and
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{ftFx_SpecialLw_Enter,ftFx_SpecialAirLw_Enter}`.
  - SpecialN Loop -> Loop restart seed overrides are now populated only for replay-visible same-action
    loop restarts. The owner is `ftFx_SpecialN_OnChangeAction`, which calls `ft_80089824` and consumes
    `plAttack_80037B08`; this does not reopen the rejected broad special-boundary instance override.
    Sources: `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
    ftFx_SpecialNLoop_Anim,ftFx_SpecialAirNLoop_Anim,ftFx_SpecialN_OnChangeAction}`,
    `refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_80089824`, and
    `refs/melee/src/melee/pl/plattack.c::plAttack_80037B08`.
  - F24 audit result: direct special-boundary `motion_entry_instance_id_override_u16` rows that only
    depend on hidden same-frame counter order are now treated as teacher-forced seed-surface rows,
    not special gameplay/runtime behavior. The lane may read the next post-frame id
    (`post_instance_id[1:]`, equivalent to `ref_t1`) and is therefore non-causal; it is valid only
    when a replay row is reseeded for one-step evaluation. Sources:
    `refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::{ft_800895E0,ft_80089824}` and
    `refs/melee/src/melee/pl/plattack.c::plAttack_80037B08`.
  - F24 taxonomy is now limited to source-backed special callbacks such as SpecialN Loop restart
    `ftFx_SpecialN_OnChangeAction -> ft_80089824`. Direct special-boundary pure `instance_id`
    rows that do not install x21EC/OnChangeAction are generic adjacent instance ordering (`F12b`)
    or damage identity (`F08a`) rows, not B-special state-machine residuals.
    Sources: `refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState`,
    `refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::{ft_800895E0,ft_80089824}`,
    `refs/melee/src/melee/pl/plattack.c::plAttack_80037B08`.
  - `F23_special_common_entry_dispatch` is now source-gated to seed actions whose IASA callbacks
    actually call the common special dispatchers. `KneeBend`, `LandingFallSpecial`, and active
    per-special states are excluded from F23; the remaining aggregate F23 rows are
    `PassiveWallJump` / `JumpAerial` callers that reach `ftCo_SpecialAir_CheckInput`.
    Sources:
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_IASA`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_LandingFallSpecial_Enter_Basic`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA`, and
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::ftCo_PassiveWall_IASA`.
  - `F21_specials_illusion_phantasm` is zero after taxonomy hardening: pure aerial Side-B
    hurtbox-state rows move to `F09a`, Side-B combo/source rows move to `F09b`, and Side-B damage
    contact rows move to `F08f`. Pure `SpecialHiHoldAir` hurtbox-state rows likewise move to
    `F09a` because `ftFx_SpecialHiHoldAir_IASA` is empty and those rows have no launch/travel or
    collision fields; Firefox Bound/Fall/Landing/launch rows remain in `F22`.
    Source: `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiHoldAir_IASA`.
  - `ftFx_SpecialAirHi_Enter` consumes all jumps with `fp->x1968_jumpsUsed = ca->max_jumps`.
    Runtime now writes the equivalent Slippi `jumps_left=0` on `SpecialHiHoldAir` ->
    `SpecialAirHi` launch entry. Bound/Fall/cliff-catch timing rows remain in `F22`.
    Source: `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialAirHi_Enter`.
  - `ftFx_SpecialHiHold{Air}_Anim -> ftFx_SpecialAirHi_Enter` is a prio-1 Anim callback path, so
    launch direction reads the pre-input `fp->input.lstick` snapshot before
    `Fighter_Spaghetti_8006AD10` installs current-frame input. Runtime uses `prev_input_main_*`
    for this anim-end launch direction, still applying `data/common/ft_common_data.json`
    `lstick_deadzone_{x,y}` and `data/characters/{fox,falco}.json` Firefox/Firebird launch attrs.
    This preserves the older HVG deadzone fix and closes TCH's changed-input HoldAir launch split.
    Sources: `refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_Spaghetti_8006AD10}`,
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    ftFx_SpecialHiHold_Anim,ftFx_SpecialHiHoldAir_Anim,ftFx_SpecialAirHi_Enter}`,
    `data/common/ft_common_data.json`, `data/characters/{fox,falco}.json`.
  - `ftFx_SpecialHiBound_Enter` enters `SpecialHiBound`, immediately ticks anim via
    `ftAnim_8006EBA4`, then scales horizontal self velocity by `ftFox_DatAttrs.x84`
    (`firefox_bound_vel_x`). Runtime applies that extracted data scalar only on the
    `SpecialAirHi_Coll -> SpecialHiBound` entry row, not while still traveling in `SpecialAirHi`.
    While the rebound remains airborne, `ftFx_SpecialHiBound_Phys` then replaces vertical
    self-velocity from `ft_800851C0` / `fp->x6A4_transNOffset.y` and applies horizontal
    `ftCommon_8007CF58` air friction, so the rebound path follows the Bound root-motion arc instead
    of the pre-bound launch velocity.
    Sources: `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    ftFx_SpecialAirHi_Coll,ftFx_SpecialHiBound_Enter,ftFx_SpecialHiBound_Phys}`,
    `refs/melee/src/melee/ft/ft_081B.c::ft_800851C0`,
    `refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007CF58`,
    `data/characters/{fox,falco}.json`.
  - Ledge occupancy now includes slow ledge options (`CliffClimbSlow`, `CliffAttackSlow`,
    `CliffEscapeSlow`, `CliffJumpSlow1`) as well as the already-modeled quick variants. This keeps
    `ftCliffCommon_80081298` from admitting a `SpecialHiFall` CliffCatch onto a ledge already
    occupied by the other fighter's slow ledge option, while preserving unoccupied
    `SpecialHiFall -> CliffCatch` rows. `CliffJump*2` remains excluded because it no longer uses
    the attach snap / occupancy path. Sources:
    `refs/melee/src/melee/ft/ftcliffcommon.c::{ftCliffCommon_80081298,ftCliffCommon_80081370}`,
    `refs/melee/src/melee/ft/chara/ftCommon/forward.h`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c`, and
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffJump.c`.
  - `SpecialHiFall -> CliffCatch` consumes the same `Collide_LedgeGrabMask` authority as other
    cliff-check callbacks after `ftFx_SpecialHiFall_Coll` fails to take its prior
    `ft_CheckGroundAndLedge` landing branch. The aggregate `Collide_Edge` bit alone is not a
    post-mask CliffCatch veto; decomp suppresses ledge-grab generation only when side-specific
    `Collide_LeftEdge/RightEdge` is already present inside `mpColl_80046904`. Sources:
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiFall_Coll`,
    `refs/melee/src/melee/mp/mpcoll.c::mpColl_80046904`, and
    `refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081298`.
  - `CliffJumpSlow2` and `CliffJumpQuick2` both use `ftCo_CliffJump2_Phys`: the same-frame
    `CliffJump1 -> CliffJump2` handoff skips `ft_80084DB0` through `mv.co.cliffjump.x0`, even
    though visible `action_frame` is already 1 from the immediate `ftAnim_8006EBA4` tick, and
    steady `CliffJump2` frames apply the common air helper. Runtime keys that first-frame skip on
    the frame-start `prev_action_id` (`CliffJumpSlow1/Quick1`) rather than a broad action-frame
    predicate. Source:
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffJump.c::{ftCo_8009B2F8,ftCo_CliffJump2_Phys}`.
  - GuardOn/Guard/GuardSetOff/GuardReflect do not route grounded Down-B through `ftCo_800D68C0`;
    `GuardOff_IASA` is the shield-exit callback that can dispatch specials. Runtime therefore
    blocks grounded Shine entry from the active shield/shieldstun callbacks while preserving the
    GuardOff path.
    Source: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_IASA,ftCo_Guard_IASA,ftCo_GuardSetOff_IASA,ftCo_GuardReflect_IASA,ftCo_GuardOff_IASA}`.
  - Shine rows whose replay destination has already entered `Damage*` while the sim remains in the
    same `SpecialLw*` defender state are shared ProcessHit/damage-transition ownership, not
    reflector state-machine ownership. These move to `F08c_damage_state_transition_adjacency`.
    Rows where a Shine hitbox is only the selected false BODY candidate move to
    `F08f_body_contact_candidate_filter_residual`, and grounded Shine-entry hitlag/source/state-flag
    bookkeeping moves to `F10b_grounded_combat_adjacency`. Mixed Shine action/contact rows where
    Shine itself is the active callback remain in `F20`.
    Sources: `refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c`, and
    `refs/melee/src/melee/ft/ftcoll.c::ftColl_8007A06C`.
- Status after this pass:
  - The broad `F10e` taxonomy split is complete, but Fox/Falco specials are not closed. Named
    residual owners remain active, led by `F20_speciallw_shine_reflector`,
    aggregate-only `F19_specialn_blaster_article`, and `F22_specialhi_firefox_firebird`.
    `F21_specials_illusion_phantasm` is zero after hard-moving its non-Side-B-owner tails. After the forced
    dataset rebuild, Shine Loop projectile-origin reflector fix, FallSpecial shallow projection,
    EscapeAir prev-ECB-bottom projection, SpecialHiBound owner slice, and SpecialHiLanding
    destination-Wait IASA slice, camera-visibility split, SpecialNEnd destination-Wait Dash
    slice, F21/F22/F23 taxonomy boundary hardening, Shine x1988/x198C seed provenance, and
    SpecialN loop-restart instance ordering, F24 audit narrowing that removed the
    non-prefix-causal special-boundary override, F24 direct-boundary hard movement to generic
    instance/damage identity owners, and pure Shine source-bookkeeping movement to
    combat/aerial bookkeeping, AttackLw3 entry instance-callback ownership, AttackDash -> Shine
    allow-interrupt carry, F13a pure source-bookkeeping movement, and SpecialAirNLoop common-Damage
    scalar movement to `F08d`, SpecialAirNLoop -> End / Landing callback-order fixes, pure
    ThrowHi/ThrowB/ThrowLw score-source movement to common throw
    bookkeeping, common aerial B-before-A dispatch for JumpAerial / PassiveWallJump rows, and
    prev-special-only generic Landing/Ottotto/Fall and source-bookkeeping tail movement to shared
    mpColl/combat owners, no-lock sustained EscapeAir current-ECB collision narrowing, and slow
    ledge-option occupancy for SpecialHiFall CliffCatch blocking, and F20 BODY/contact bookkeeping
    boundary hardening plus SpecialLw-owned JumpAerial IASA re-entry suppression and final
    aggregate-tail owner moves, current aggregate taxonomy is total `5907`, with `F13a=0`,
    `F20=0`, `F24=0`, `F14b=0`, `F19=0`, `F22=0`, `F23=0`, `F21=0`, `F25=373`, and `F10e=0`.
- Rejected bridge/experiment:
  - A broad locked-bottom collision rule for all Fox/Falco aerial special callbacks, including
    `SpecialAirLwStart`, fixed some loop rows but grounded Shine startup several frames early and
    regressed aggregate taxonomy. The kept owner is limited to the decomp-supported loop/end
    AirToGround subset and is protected by replay-real locks.
  - A broad Shine Loop hidden-hurtbox clear from merged Slippi `hurtbox_state` fixed isolated
    rows but regressed many persistent Loop invulnerability rows; it was removed.
  - A broad hidden-colanim reseed that initialized runtime x198C directly from the explicit seed
    lanes regressed primary taxonomy from `633` to `26293`; it was reverted. The remaining x1988 /
    x198C split for special hurtbox tails needs more precise hidden-state evidence rather than a
    broad merged-state rewrite.
  - A broad aerial projectile-origin reflector fallback that included `SpecialAirLwHit` re-entered
    the hit callback on already nearby projectiles; the kept item-origin slice is limited to Shine
    Loop states, where `ftColl_80077464` consumes the projectile `Item*` / `item->pos` overlap.
  - A broad EscapeAir floor-contact/root-projection pass fixed some representative rows but
    regressed primary taxonomy from `633` to `811` and aggregate from `6144` to `6837` by admitting
    too many new `LandingFallSpecial` rows. The kept F13a slices are limited to decomp-backed
    `FallSpecial_Coll` shallow projection and the `EscapeAir_Coll` prev-ECB-bottom crossing subset;
    active-lock EscapeAir rows whose previous ECB bottom is still above floor remain excluded by
    replay-real negative locks.
  - A broad SpecialS/SpecialHi hidden-colanim clear based only on missing x1990/x1994 provenance was
    tested and rejected: the SpecialHiHoldAir variant cleared x198C too early on rows where replay
    keeps hurtbox_state=2, and the SpecialS-only variant regressed aggregate taxonomy by increasing
    F21/F23 hurtbox rows. Remaining SpecialS/SpecialHi hurtbox tails need seed-visible x1988/x198C
    provenance or a narrower decomp-backed owner, not a merged Slippi hurtbox-state clear.
  - DamageAir / DamageFly aerial B-special dispatch now follows the common damage IASA gate:
    `ftCo_Damage_IASA` and `ftCo_DamageFly_IASA` delegate into `Fall_IASA_Inner` /
    `DamageFall_IASA` only when `x221C_b6` is clear, then `ftCo_SpecialAir_CheckInput` owns
    Up/Down/Side/Neutral special selection.
    Sources: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_IASA,ftCo_DamageFly_IASA}`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA_Inner`.
  - Low-KB `DamageAir` floor contact can keep the visible DamageAir motion while still running the
    common `ftCommon_8007D7FC -> ftCommon_8007D6A4` grounding helper; replay-visible effects include
    `gr_vel <- self_vel.x` and jump refresh, while fastfall/state-flag visibility remains a separate
    lane. The immediate grounded `KneeBend` followup may enter grounded SpecialHi from a current B+Up
    edge: `ftCo_KneeBend_IASA` calls the misleadingly named `ftCo_Attack100_CheckInput`, which
    dispatches `ftData_SpecialHi` when `Fighter_UnkIncrementCounters_8006ABEC` has reset `x686` via
    `ftCo_800D6928`. This is not generic KneeBend B-special admission; Side/Neutral/Down-B remain
    excluded.
    Sources: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll`,
    `refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D7FC,ftCommon_8007D6A4}`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_IASA`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_Attack100_CheckInput,ftCo_800D6928}`,
    `refs/melee/src/melee/ft/fighter.c::Fighter_UnkIncrementCounters_8006ABEC`.
  - DamageFly / DamageFlyRoll / FlyReflect active-hitstun jump buffering uses the same `doIasa`
    x14 snapshot owner as common Damage: while `x221C_b6` is set, `ftCo_Jump_GetInput` stores
    `mv.co.damage.x0` in `mv.co.damage.x14`; after hitstun ends, `ftCo_DamageFly_Anim` consumes
    that snapshot through `inlineC0` before entering `DamageFall`, or `DamageFall_IASA` consumes it
    through the JumpAerial path. The JumpAerial entry row uses the buffered x/y snapshot; following
    JumpAerial Phys rows apply live drift from current input. FlyReflectWall/Ceil delegate their
    Anim/IASA callbacks to the same DamageFly owners, so replay seed derivation must keep their
    active-hitstun rows in the x14 family. This is required for rollout carry such as QGD
    `DamageFlyN` x14 buffering into `JumpAerialF`, and FEH `FlyReflectWall` terminal x14 buffering
    into `JumpAerialB`.
    Sources: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{doIasa,inlineC0,ftCo_DamageFly_Anim,ftCo_DamageFly_IASA,ftCo_DamageFlyRoll_IASA}`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_FlyReflect.c::{ftCo_FlyReflect_Anim,ftCo_FlyReflect_IASA}`.
  - DamageFly active-hitstun meteor-cancel jumps use the earlier `doIasa` immediate escape branch,
    not the delayed x14 buffer. `ftColl_8007AC68` marks 260..280 degree KB as
    `mv.co.damage.x1A`; `doIasa` decrements the p_ftCommonData->x7F0 `x1B` lockout, then a
    qualifying jump input clears KB velocity/hitstun and enters JumpAerial while `x221C_b6` was
    still active at frame start. Runtime sets x1A from the source knockback angle on damage entry;
    replay reseed consumes the bit-packed `damage_post_hitlag_cb_kind & 0x80` lane so later
    DI-adjusted KB vectors cannot create false meteor-cancel eligibility. The retained escape
    implementation is JumpAerial-only; SpecialHi meteor-cancel admission remains an open
    source-owner boundary. GAT-doubles `1140 -> 1146` locks the immediate `DamageFlyN ->
    JumpAerialF` escape.
    Sources: `refs/melee/src/melee/ft/ftcoll.c::ftColl_8007AC68`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_CalcAngle,doIasa}`,
    `data/common/ft_common_data.json`.
  - DamageFall's terminal X-stick Fall gate uses the callback-visible `x670_timer_lstick_tilt_x`
    after `Fighter_Spaghetti_8006AD10` updates current-frame input history, then applies the strict
    `x670 < p_ftCommonData->x214` comparison. Held same-direction X stick therefore advances from
    `0` to `1` before `ftCo_DamageFly_IASA -> ftCo_DamageFall_IASA`; it must not be rewound to the
    previous row's edge timer. HIS `2323` locks the non-Fall frame and HIS `2324` locks the adjacent
    PassiveStandF floor-tech handoff.
    Sources: `refs/melee/src/melee/ft/fighter.c::Fighter_Spaghetti_8006AD10`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA`,
    `data/common/ft_common_data.json` (`damagefall_fall_tilt_max_frames`).
  - Fresh `DamageFly* -> DamageFall` entry rows run destination `DamageFall_Phys` in the same
    `Fighter_procUpdate`, so `ft_80084DB0` gravity updates `self_vel.y` before position
    integration. Steady `DamageFall` rows keep the seed-driven current-frame displacement bridge
    until the broader DamageFall/KB interaction is closed. TCH `710` locks the fresh-entry path;
    AGN `2959` locks the steady negative.
    Sources: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Anim`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::{ftCo_80090780,ftCo_DamageFall_Phys}`,
    `refs/melee/src/melee/ft/ft_081B.c::ft_80084DB0`.
  - DownBound x1994 is a hidden timer/pose owner, not a BODY damage suppressor. Reseeded
    DownBoundU/D rows can carry `x1994` from `Fighter_8006A360` even when Slippi's snapshot is still
    airborne; after same-frame floor contact, DownBound Anim advances before collision consumers.
    Runtime therefore uses the post-Anim DownBound collision pose on grounded `x1994>0` rows with
    vulnerable `hurtbox_state`, while keeping damage eligibility in the normal BODY path. TBK
    `2400 -> 2402` locks the false high-hurtcap miss, and PRH `9954 -> 9958` locks the real
    AttackDash hit once the attacker entry Phys is correct.
    Sources: `refs/melee/src/melee/ft/fighter.c::Fighter_8006A360`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::{ftCo_DownBound_Anim,ftCo_DownBound_Coll}`,
    `refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B7A4,ftColl_8007B868,ftColl_80076ED8,ftColl_80078C70}`.
  - `DownDamage_Phys` delegates to `ftCo_Damage_Phys`, so airborne DownDamage hitlag-exit rows
    apply the same common Damage gravity/drift owner before position integration. Active-hitlag
    DownDamage floor-contact rows still route through `ftCo_DownDamage_Coll -> ft_80081DD4`; after
    common Damage Phys has resumed, the floor-contact fallback must use the post-hitlag root point
    plus downward attack-KB rather than the older `self_vel == 0` resting shortcut.
    DownDamageU and DownDamageD also share the common `allow_sdi` / `ftCo_Damage_OnEveryHitlag`
    owner installed by `ftCo_8009F184 -> ftCo_8008DCE0`: active hitlag can consume the same
    timer-window SDI displacement, and hitlag exit consumes the same ASDI/DI lane before
    `ftCo_8008E5A4` rotates knockback. The hitlag-exit DI path uses the GALE01 MSL
    `atan2f`/`sinf`/`cosf` approximations and PPC fused multiply-add/subtract ordering for
    `ftCo_8008E5A4`, not host libm trig, because those ULP differences carry into rollout X/Y
    position drift after long DamageFly streaks. PTE `7598/7599` locks the DownDamageU parity while
    the preceding no-window row stays frozen; Game_20260514T181413 `1423` locks the prior-input
    DamageFlyN DI math.
    Sources: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Phys`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_8008DCE0,ftCo_Damage_OnEveryHitlag,ftCo_Damage_OnExitHitlag,ftCo_8008E5A4}`,
    `refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Damage.s::ftCo_8008E5A4`,
    `refs/melee/src/MSL/trigf.c::{sinf,cosf}`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::{ftCo_DownDamage_Phys,ftCo_DownDamage_Coll}`,
    `refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4`.
  - Slippi records `last_hit_by` in the raw controller-port domain. The replay seed now carries
    `source_port0[player]`, and runtime source writes/comparisons use that lane while gameplay
    arrays remain local-slot indexed. This fixes same-owner source attribution without a replay-row
    branch.
    Sources: `refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm`,
    `refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8`,
    `refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC`.
  - `Fighter_8006A360` owns the terminal `dmg.x18C8` source-owner countdown. The generated
    `source_clear_terminal_phase` seed lane remains authoritative for teacher-forced one-step
    terminal rows, but replay-seeded rollouts also need a live terminal owner when an active
    x18C8 run reaches `timer==1` after the seed. Passive/Down continuation rows with live
    owner-set provenance, combo source, last attack, no hitlag/hitstun, and no `x221F_b3` retain
    the source owner while retiring the countdown instead of publishing sentinel source `6`.
    CDO `12020 -> 12024/12025` locks the replay-real PassiveStandF case; TCH `441` remains the
    AttackAirN negative control where the terminal seed lane is clear and the ordinary source clear
    publishes sentinel `6`.
    Sources: `refs/melee/src/melee/ft/fighter.c::{Fighter_ChangeMotionState,Fighter_8006A360}`,
    `refs/melee/src/melee/ft/ftcoll.c::ftColl_800764DC`,
    `refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm`, and
    `data/attack_id/move_id/{fox,falco}.bin::x9_b1`.
  - Pure Shine-context `last_hit_by`, `last_attack_landed`, `combo_count`, hurtbox/state-flag, and
    hitlag-only rows are taxonomy owned by shared combat/state/contact-hitlag owners
    (`F10b`/`F09b`, `F10d`/`F09a`, or `F09d`), not by the SpecialLw state machine. Grounded
    Shine-entry rows whose only remaining fields are hitlag, Slippi's hitlag state flag, and
    stale/source/instance bookkeeping also route to `F10b`; aerial Shine entry rows whose only
    fields are hitlag plus Slippi's hitlag state flag route to `F09d`. Rows where a Shine hitbox is
    the selected false BODY candidate route to the BODY candidate-filter owner (`F08f`). `F20` is
    zero in the refreshed primary and aggregate special-family taxonomy.
    Sources: `refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007A06C,ftColl_8007BE3C,ftColl_80078C70,ftColl_80076ED8}`,
    `refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}`,
    and `refs/melee/src/melee/pl/plstale.c::{plStale_UpdateStaleMovesFromFighter,plStale_UpdateStaleMovesFromItem}`.
  - SpecialAirNLoop can be the defender context when common BODY damage has already entered
    `Damage*`. Rows whose remaining fields are damage scalar/contact lanes (`hitlag`, facing,
    attack speed, or hitlag state flags) are owned by shared damage/contact resolution (`F08d`),
    not by the SpecialN blaster article state machine. Pure SpecialN hurtbox and source/scoreboard
    rows are owned by shared aerial state/combat bookkeeping (`F09a`, `F09b`, or `F10b`).
    SpecialN item/article/action/loop handoff rows stay in `F19`.
    Sources: `refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0`,
    `refs/melee/src/melee/ft/ftcoll.c::ftColl_8007A06C`.
  - SpecialN/SpecialAirN entry preserves `fp->x8c_kb_vel` (`speed_{x,y}_attack`). Grounded
    `ftFx_SpecialN_Enter` clears only `gr_vel` and `self_vel`, while aerial
    `ftFx_SpecialAirN_Enter` does not clear self velocity at all; neither path clears the
    knockback velocity lane after `Fighter_ChangeMotionState(..., flags=0)`. DamageFall ->
    SpecialAirNStart can therefore carry DamageFlyRoll knockback into the first blaster frame.
    Sources: `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{ftFx_SpecialN_Enter,ftFx_SpecialAirN_Enter}`,
    `refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState`.
  - Active SpecialN loop/end handoffs use extracted `cmd_vars[0]` command-window data plus the
    seeded `x67D` B timer to infer whether the persistent `mv.fx.SpecialN.isBlasterLoop` latch was
    set by an in-window B edge. Held prior-B rows preserve the existing latch behavior, while stale
    and current-frame-only B timers enter End. Same-action Loop->Loop instance ordering still uses
    only the explicit `motion_entry_instance_id_override_u16` seed lane. Deep under-floor
    `SpecialAirNLoop_Anim -> SpecialAirNEnd` rows run the entered End collision callback through
    `AirCatchHit_Coll -> Landing_Enter_Basic`, preserving both ground and air self-X lanes on the
    Landing frame. `F19` is zero in the refreshed primary and aggregate special-family taxonomy.
    Sources: `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
    ftFx_SpecialNLoop_Anim,ftFx_SpecialAirNLoop_Anim,ftFx_SpecialAirNEnd_Coll}`,
    `refs/melee/src/melee/ft/ft_081B.c::ftCo_AirCatchHit_Coll`, and
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Enter_Basic`.
  - Rows where SpecialN/Shine is only the previous action and the current row is already generic
    Landing/Ottotto/Fall or grounded source bookkeeping route to shared mpColl/combat owners.
    Active SpecialN loop/end handoff rows and mixed Shine contact/action rows remain eligible for
    `F19`/`F20` if they reappear, but the refreshed special-family taxonomy has both buckets at
    zero after the runtime and owner moves above.
    Sources: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c`,
    `refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor`,
    `refs/melee/src/melee/ft/ftcoll.c::ftColl_8007BE3C`, and
    `refs/melee/src/melee/pl/plstale.c`.
  - Remaining Firefox/Firebird aggregate tails that carry only SpecialAirHi <-> Bound
    action-frame/animation timing are shared collision callback ordering (`F10c`); pure steady Bound
    `jumps_left` rows are shared aerial state/bookkeeping (`F09a`); and the grounded KneeBend ->
    SpecialHiHold tail after DamageAir is grounded selector adjacency (`F10a`). `F22` is zero in
    the refreshed primary and aggregate special-family taxonomy.
    Sources: `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    ftFx_SpecialAirHi_Coll,ftFx_SpecialHiBound_Coll,ftFx_SpecialHiBound_Enter,
    ftFx_SpecialHiBound_Anim}`, `refs/melee/src/melee/mp/mpcoll.c`, and
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c`.
  - Remaining action-aligned ThrowLw/ThrownLw/ThrowHi hitlag, Slippi contact-bit, hurtbox, and
    source-id tails are common throw/item/contact bookkeeping (`F14`), not per-throw special-family
    pulse ownership. The rejected ThrowLw current-pulse bridge remains excluded. `F14b` is zero in
    the refreshed primary and aggregate special-family taxonomy.
    Sources: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c`,
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim`, and
    `refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007A06C,ftColl_8007BE3C}`.
  - Common `FallSpecial` / `LandingFallSpecial` / `EscapeAir` rows that have already entered
    `Damage*` are owned by shared damage/contact resolution, while pure ground-id, state/hurtbox,
    action/on-ground/jump landing timing, and pure LandingFallSpecial action-frame tails route to
    shared mpColl/state/landing owners (`F10c`, `F09a`, or `F10d`). `F13a` is zero in the refreshed
    primary and aggregate special-family taxonomy.
    Sources: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_FallSpecial_Coll`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c`.
  - Sustained EscapeAir rows that no longer carry `CollData_X130_Locked` use the normal
    `EscapeAir_Coll -> ft_80082C74 -> mpColl_800471F8` floor owner. Runtime no longer has a
    no-lock vertical-frame suppression bridge; once the lock has cleared, platform/floor crossings
    can enter `LandingFallSpecial` through the ordinary callback. Locked-window and remap guards
    remain scoped to the callback-local CollData lifetime they model. Sources:
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll`,
    `refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}`, and
    `refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}`.
  - Sustained locked `EscapeAir` on the same carried ledge floor uses the same CollData floor owner
    as same-platform continuations: if the floor-sweep source row was already below that carried
    ledge and the final writeback snap is shallower than the entered EscapeAir ECB bottom, runtime
    keeps the row airborne until `ft_80082C74/mpColl_800471F8` reaches the real floor handoff depth.
    This is generated stage-line metadata (`is_ledge`) plus CollData/ECB depth, not a stage-id or
    replay-row exception. Sources:
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll`,
    `refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}`,
    `refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_LoadECB_inline,mpCollInterpolateECB,mpColl_80044838_Floor}`.
  - Fresh `JumpAerialF/B -> EscapeAir` ledge publication also uses the explicit cliff-floor seed
    owner when visible `CollData.floor.index` already equals the same flat cliff ledge. A shallow
    same-line `mpColl_80044838_Floor` writeback stays airborne in `EscapeAir`; later/deeper callback
    phases still publish `LandingFallSpecial`. The retained gate is bounded by generated line
    metadata (`is_ledge` and non-slope) plus the source ledge cooldown window after
    `Fighter_procUpdate`'s pre-callback decrement, not by stage id or replay row. Sources:
    `refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081370`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll`,
    `refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}`,
    `refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}`.
  - SpecialHi rows that have already entered `Damage*`, plus pure `SpecialHiLanding` ground-id
    tails, route to shared damage/mpColl owners. Launch, Bound, Fall, and CliffCatch rows remain in
    `F22`. Just-entered `SpecialHiBound` rows stay airborne because `ftFx_SpecialHiBound_Enter`
    does not call `ftCommon_8007D7FC`; later `SpecialHiBound_Coll` owns ground conversion.
    Sources: `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c`,
    `refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor`.
  - `LandingFallSpecial` with the hidden `landing.allow_interrupt` lane set can dispatch grounded
    Shine through `ftCo_Landing_IASA -> ftCo_800D68C0`, matching the shared Landing special chain
    instead of blocking all LandingFallSpecial specials.
    Sources: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::{
    ftCo_Landing_IASA,ftCo_LandingFallSpecial_Enter}`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D68C0`.
  - Opening input lock countdowns are replay post-frame "remaining locked steps" lanes. At seed
    frame `-40`, value `1` represents the VS-overlay clear that runs before raw frame `-39` inputs,
    so that current input is visible to `Fighter_procUpdate`. This lets
    `Landing_IASA -> ftCo_Jump_CheckInput` consume the first legal jump edge at the VS-start
    boundary and prevents a grounded/aerial SpecialN split one frame later. The prior locked frame
    still saved physical stick into `input.x630/x634` before resetting x670/x671 to `0xFE`, so a
    held horizontal stick on the clear boundary can walk but must not become a fresh dash flick; a
    true threshold crossing on the clear frame remains dash-eligible. Other locked states still
    blank inputs through
    `Fighter_UnkInitLoad_80068914_Inner1`. Sources:
    `refs/melee/src/melee/ft/fighter.c::{Fighter_procUpdate,Fighter_Spaghetti_8006AD10,Fighter_UnkInitLoad_80068914_Inner1}`,
    `refs/melee/src/melee/gm/gm_16AE.c::fn_8016B7F8`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA`.
  - SpecialLw Loop/Turn/End IASA can consume aerial jump into `JumpAerial*`; that same callback
    does not then run destination `JumpAerial` special dispatch again on the same B/up input edge.
    Runtime marks the Shine-owned JumpAerial handoff for the current step so the later generic
    aerial B-special pass cannot immediately re-enter Firefox.
    Sources: `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
    ftFx_SpecialAirLwLoop_IASA,ftFx_SpecialAirLwTurn_IASA,ftFx_SpecialAirLwEnd_Anim}`,
    `refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate`.
  - Pure ThrowHi/ThrowB/ThrowLw `combo_count` / source-score rows are common throw/item
    bookkeeping (`F14`), not special-move state-machine residuals. Mixed throw pulse rows with
    hitlag, state flags, hurtbox, article, or item-contact fields remain in `F14b`.
    Sources: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_ThrowHi_Anim,ftCo_800DD724,ftCo_800DE7C0,ftCo_800DDDE4}`,
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim`,
    `refs/melee/src/melee/pl/plstale.c::{plStale_UpdateStaleMovesFromFighter,plStale_UpdateStaleMovesFromItem}`.
  - Item-owner taxonomy split:
    - Throw article rows now split into `F14c_throw_article_lifetime` (item spawn/despawn/slot/xDA8
      from `set_throw_spawn_projectile`) and `F14d_throw_source_scoreboard` (player-side source,
      hitlag/contact, stale/combo bookkeeping from item-domain throw hits).
    - Guard item rows split into `F15a_reflect_owner_transfer` (reflected owner/xDA8 transfer timing)
      and `F15b_guard_laser_lifetime` (laser shield-hit, shield-bounce, and despawn lifetime).
    - Generic item identity rows split into `F16a_item_slot_compaction_identity`,
      `F16b_blaster_article_identity`, `F16c_illusion_phantasm_lifetime`, and
      `F16d_item_body_lifetime`.
    - The coarse `F14_throw_item_bookkeeping`, `F15_guard_item_ownership`, and
      `F16_item_identity_residual` buckets are taxonomy-only legacy labels after this split and no
      longer emit in refreshed primary/aggregate taxonomy.
    Sources: `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim`,
    `refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}`,
    `refs/melee/src/melee/ft/ftcoll.c::{ftColl_80077464,ftColl_8007646C,ftColl_800763C0}`,
    `refs/melee/src/melee/it/item.c::{Item_80269F14,Item_80269DC8}`,
    `refs/melee/src/melee/it/itcoll.c::it_80272460`,
    `refs/melee/src/melee/it/items/{itfoxlaser.c,itfoxblaster.c,itfoxillusion.c}`,
    `refs/slippi-ssbm-asm/Recording/SendItemInfo.s`, and
    `tools/slippi/make_dataset_from_slp.py::_fill_items_fixed`.
  - Guard laser lifetime runtime slice:
    - `Item_80269DC8` routes eligible shield contacts either through `shield_bounced` (keep the
      projectile alive) or `hit_shield` (destroy the projectile). Until the authoritative
      `xDCE/xC54/xC58` shield-bounce internals are promoted into explicit state, the simulator keeps
      shield-bounce only for already-aged, high-shield glancing contacts; lower-shield fresh
      GuardSetOff contacts take the `HitShield` destruction path.
    - Steady `Guard` no-submotion snapshots (`action_frame=-1`, `animation_index=-1`) are split by
      item callback provenance. Replay-carried/high-bit rows stay on the settled point sample and do
      not get a fresh authored-offset shield sweep, preserving GAT/QGD carried-shot negatives. A
      laser in the pure frame-start `fp+0x2218_b5` behavior lane (`state_flags[0] == 0x04`, with high
      command bits clear) uses the authored HitCapsule offsets for the immediate
      `it_8029C4D4 -> Item_80269DC8` shield callback only while the owner is still in the
      `ftFx_SpecialNLoop_Anim` / `ftFx_SpecialAirNLoop_Anim` callback phase that owns the shot, even
      when the defender is serialized as steady Guard. `IAT:3575` locks this positive, while
      `DCC:6517` locks the high-bit negative where the shot serializes instead and `DCC:2231` locks
      the pure-byte landing carry negative.
    - No-submotion Guard-family item BODY uses the same `ftColl_8007925C -> lbColl_8000805C`
      motion-state collision matrix owner instead of replay-exported stale world hurtcaps when
      `fp+0x221B_b0` proves a live ShieldDesc and shield/reflect did not consume the laser first.
      `GuardOn` and `GuardReflect` source their BODY pose from `ftCo_SM_GuardOn`, `Guard` from
      `ftCo_SM_Guard`, and `GuardSetOff` from `ftCo_SM_GuardDamage`. The frame-start lightshield
      BODY sample is still excluded when the raw `fp+0x2218_b2` command bit (`0x20`) is set, so
      GAT/DCC command-lane BODY controls remain on the current item sample; doubles
      `Game_20260509T152622:{3578,3616}` lock the allow-interrupt BODY miss variant. The adjacent
      shield path stays source-owned: `3579` locks ordinary HitShield/GuardSetOff, and `3617` locks
      hidden `ftColl_80077688 -> Item_80269DC8` ShieldBounced admission through the existing
      `item_shield_bounce_valid` seed lane plus the free-running bounce-normal predicate. This is
      not a broad shield-contact closure; carried-laser rows without ShieldBounced source proof
      remain on the reduced settled-point shield proxy.
    - Replay-real positive lock: `GracefulAttachedTurtle.msl:773` now enters GuardSetOff and clears
      the Falco laser article. Existing keepalive controls (`GAT:2276`, `GAT:5280`) remain live.
    Sources: `refs/melee/src/melee/it/item.c::Item_80269DC8`,
    `refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C504,itFoxlaser_UnkMotion1_Anim,it_8029C4D4,itFoxLaser_Logic94_ShieldBounced,itFoxLaser_Logic94_HitShield}`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091A4C`.
  - Item damage facing source owner:
    - `ftColl_8007A06C` item-damage intake does not always use the owner fighter root to choose
      `fp->dmg.facing_dir_1`. When `abs(item->x40_vel.x) < ItemCommonData->x78_float`, the sign is
      owned by item position vs victim position; otherwise it is owned by item velocity sign.
      Runtime loads `x78_float` from `data/items/item_common.json::item_damage_facing_velocity_threshold`
      and applies the same position/velocity split for `combat_apply_item_hit`.
    - Replay-real locks: `GracefulAttachedTurtle.msl:{923,3386}` cover ordinary item-hit facing
      directions, and `HungryImportantSnake.msl:1673` covers the stationary Phantasm item case where
      item position and owner root are on opposite sides of the victim.
    Sources: `refs/melee/src/melee/ft/ftcoll.c::ftColl_8007A06C`,
    `refs/melee/src/melee/it/types.h::ItemCommonData::x78_float`.
  - Illusion / Phantasm article lifetime runtime/seed slice:
    - BODY-hit Illusion/Phantasm articles persist but do not enter generic item hitlag. Decomp
      `OnGiveDamageThink` copies damage into `xCA8`, then
      `itFoxIllusion_Logic14_DmgDealt` clears `xCA8`; the later `checkHitLag(xCA8)` branch is
      skipped, so `xD44_lifeTimer` continues to tick through the victim hitlag window.
    - The article animation callback can run after the owner exits Side-B during the same simulated
      frame. Runtime preserves the frame-start Side-B motion for the
      `ftFx_SpecialS_CheckGhostRemove` lifetime gate so the current item tick is not prematurely
      destroyed.
    - Replay-real locks: `DistinctCaringCobra.msl:{4762,4764}` cover BODY-hit state transition and
      expiry through victim hitlag; `HilariousVillainousGiraffe.msl:6975` and
      `ImpassionedAlarmedTarsier.msl:7712` cover same-step owner-exit lifetime ticks.
    Sources: `refs/melee/src/melee/it/item.c::{OnGiveDamageThink,checkHitLag}`,
    `refs/melee/src/melee/it/items/itfoxillusion.c::{
    itFoxIllusion_Logic14_DmgDealt,itFoxillusion_UnkMotion0_Anim,itFoxillusion_UnkMotion2_Anim}`,
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialS_CheckGhostRemove`.
  - Grounded Illusion / Phantasm entry velocity runtime slice:
    - Grounded Side-B input enters through the common `ftCo_SpecialS_CheckInput -> doEnter`
      owner before the character-specific `ftFx_SpecialSStart_Enter` path. `doEnter` damps
      `gr_vel` by `co_attrs.xB8` and `ft_GetGroundFrictionMultiplier`, then
      `ftFx_SpecialSStart_Enter` divides the damped ground velocity by
      `x28_FOX_ILLUSION_GROUND_VEL_X`. The simulator extracts `co_attrs.xB8` as
      `data/characters/{fox,falco}.json::side_special_ground_entry_vel_mul` and applies it only
      on grounded Side-B entry; aerial `ftCo_SpecialAir -> ftFx_SpecialAirSStart_Enter` still
      divides self velocity by the Side-B x28 attr without the grounded xB8 damping.
      When the entry source is Dash, `ftCo_Dash_IASA` continues after the successful
      `ftCo_SpecialS_CheckInput` call and applies its terminal `p_ftCommonData->x54` ground-velocity
      scalar before the entered SpecialSStart physics callback runs. That Dash-only callback tail
      is modeled separately from Wait/Run grounded Side-B entry.
    - Replay-real lock: `FavorableSuperficialPig.msl:1613` covers Run -> SpecialSStart/Main/End;
      without xB8 damping the rollout carries a large positive X residual through the whole
      Illusion/Phantasm sequence.
      `Game_20260514T181413.msl:1532` covers Dash -> SpecialSStart clearing the residual Dash
      velocity through the Dash-IASA terminal scalar plus SpecialSStart friction.
    Sources: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialS.c::{
    ftCo_SpecialS_CheckInput,doEnter}`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput`,
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
    ftFx_SpecialSStart_Enter,ftFx_SpecialAirSStart_Enter}`.
  - Self-play 181413 rollout continuation owners:
    - CatchWait -> ThrowF/B/Hi entry installs the thrown accessory callback during IASA, then the
      thrower's Phys callback moves the owner root before `Fighter_CallAcessoryCallbacks_8006C624`
      places the victim. Runtime runs post-Phys placement for non-low Thrown* entry rows while
      keeping the separate low-throw handoff split.
    - Basic Landing entered from a late AttackAir keeps the raw `fp+0x2218` allow-interrupt bit
      already set by `ftAction_80071950`; `ftCo_Landing_Enter_Basic` sets the Landing mv flag but
      does not clear the raw bit before Slippi serializes `state_flags[0]`.
    - Terminal DamageFly/DamageFall IASA can publish Fall on the same row hitstun clears, but a
      newly enabled BODY HitCapsule from an already-running same-action attack cannot consume that
      post-IASA target until the next collision frame. Same-frame action entries such as
      Squat -> SpecialLwStart stay on the ordinary BODY path; already-active hitboxes and the next
      Fall row remain ordinary BODY contacts.
    - SpecialAirHi floor contact uses the same live JObj ECB owner as the retained wall/ledge
      SpecialHi paths: `mpColl_LoadECB_JObj` samples collision joints after
      `ftFox_SpecialHi_RotateModel` mutates FtPart_XRotN, and `mpColl_80044628_Floor` consumes the
      rotated bottom sweep. A stale carried floor id does not suppress a real live bottom crossing.
    - `Game_20260514T181413.msl:{531,1522,2361,2559,2560}` cover the retained replay-real
      positives and adjacent controls. The remaining `rec2463+` percent residual is a larger
      live `ifMagnify`/camera-target owner gap; it is not classified as exact RNG.
    Sources: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{
    ftCo_800DD398,ftCo_ThrowF_Phys}`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_800DE3FC,ftCo_800DE508}`,
    `refs/melee/src/melee/ft/ftaction.c::{ftAction_80071950,ftAction_8007121C}`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::{
    ftCo_Landing_Enter,ftCo_Landing_Enter_Basic}`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    ftCo_8008F744,ftCo_DamageFly_IASA}`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA`,
    `refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}`,
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    ftFox_SpecialHi_RotateModel,ftFx_SpecialAirHi_Coll,ftFx_SpecialHiBound_Enter}`,
    `refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpColl_80044628_Floor}`.
  - Blaster gun lifetime runtime slice:
    - `ftFx_SpecialNEnd_Anim` clears `fp->fv.fx.x222C_blasterGObj` before leaving
      SpecialNEnd through `ft_8008A2BC`; the gun article's
      `itFoxblaster_UnkMotion8_Anim` then observes that NULL pointer via
      `ftFx_SpecialN_CheckRemoveBlaster` and clears the item. The simulator no longer carries a
      generic one-frame gun linger after the owner exits the SpecialN family. Throw-side blaster gun
      lifetime remains cmd-var-owned by `ftFx_Throw_Anim` and its `cmd_vars[1]` switch.
    - Replay-real locks: `BlondHardHippopotamus.msl:666` and
      `PositiveRevolvingHyena.msl:8222` cover stale SpecialNEnd gun clear; `PRH:2460` and
      `ImpassionedAlarmedTarsier.msl:140` cover active SpecialAirNLoop gun retention while adjacent
      stale/end guns are cleared.
    Sources: `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
    ftFx_SpecialNEnd_Anim,ftFx_SpecialN_CheckRemoveBlaster,ftFx_Throw_Anim}`,
    `refs/melee/src/melee/it/items/itfoxblaster.c::itFoxblaster_UnkMotion8_Anim`.
  - Laser BODY disabled-contact runtime slice:
    - Hit-status `1` (`HurtCapsule_Disabled`) can still own laser item contact/despawn without
      routing through `Fighter_ProcessHit` damage. Runtime now lets the BODY geometry path test
      disabled fighter capsules, then clears the laser without changing fighter hitlag, hitstun, or
      source lanes.
    - Replay-real locks: `PositiveRevolvingHyena.msl:{4757,4778}` cover disabled hurtcap contact
      clearing the Falco laser without damage; `TreasuredBackKangaroo.msl:197` remains a vulnerable
      non-flinch laser BODY-hit negative control.
    - Fresh taxonomy after this slice: primary total `611` (down from `615`);
      `F14c=10`, `F14d=2`, `F15a=11`, `F15b=8`, `F16a=0`, `F16b=0`, `F16c=0`, `F16d=31`.
      Aggregate total `5401` (down from `5425`);
      `F14c=480`, `F14d=76`, `F15a=21`, `F15b=124`, `F16a=2`, `F16b=35`, `F16c=0`,
      `F16d=205`. Section-6 and ledge/collision-env families remain closed
      (`F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`).
    Sources: `refs/melee/src/melee/lb/forward.h::HurtCapsuleState`,
    `refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Phys,it_8029C4D4}`,
    `refs/melee/src/melee/it/itcoll.c`.
  - Blaster gun xDA8 residual split:
    - Pure blaster-gun `item_instance_id` rows are not item lifetime/ownership rows when
      `item_exists`, type, state, and owner already match. Slippi records item `xDA8_short`; the
      generic fighter-parent item spawn path copies the owner fighter's `fp->x2088` into xDA8.
      These rows now route to the existing adjacent fighter instance-counter owner
      (`F12b_adjacent_instance_counter_order`) instead of `F16b_blaster_article_identity`.
    - Replay row evidence: `BlondHardHippopotamus.msl:94`, `HungryImportantSnake.msl:3056`,
      `ImpassionedAlarmedTarsier.msl:2847`, `MotionlessAggressiveJay.msl:5759`,
      `PositiveRevolvingHyena.msl:2441`, and `TubbyCurlyHerring.msl:7410` all have pure gun
      xDA8 mismatches while the item article identity fields match the blaster gun. Aggregate
      taxonomy after the hard move keeps total `5401` but reduces `F16b` from `35` to `26` and
      moves the 9 pure-xDA8 rows to `F12b`.
    Sources: `refs/melee/src/melee/it/it_2725.c::it_8027B070`,
    `refs/slippi-ssbm-asm/Recording/SendItemInfo.s`,
    `refs/melee/src/melee/ft/ft_0892.c::ft_800895E0`.
  - ThrowHi first-pulse BODY carry slice:
    - The first `set_throw_spawn_projectile` pulse for ThrowHi can spawn a throw-side laser while
      the victim is already in same-owner throw damage from the prior source lane. If that victim is
      on the non-projectile X side of the first-pulse segment, current BODY geometry must not
      consume the fresh article or advance combo/source bookkeeping; front-side victims still
      resolve through the normal BODY path. The discriminator is tied to the throw-side laser
      velocity from `ftFx_Throw_Anim`'s hold-joint launch vector, not to dataset row identity.
    - Replay-real locks: `BlondHardHippopotamus.msl:{527,1206}` cover non-projectile-side carry,
      `BlondHardHippopotamus.msl:480` covers the front-side first-pulse consume control, and
      `BHH:937` is now covered by the later same-character callback phase rather than this carry
      slice.
    - Fresh taxonomy after this slice: primary total `611` unchanged;
      `F14c=10`, `F14d=2`, `F15a=11`, `F15b=8`, `F16a=0`, `F16b=0`, `F16c=0`, `F16d=31`.
      Aggregate total `5393` (down from `5401`); `F14c=475` (down from `480`),
      `F14d=75` (down from `76`), `F15a=21`, `F15b=124`, `F16a=2`, `F16b=26`,
      `F16c=0`, `F16d=205`. Section-6 and ledge/collision-env families remain closed
      (`F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`).
      Sources: `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim`,
      `refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}`,
      `refs/melee/src/melee/it/itcoll.c::it_80272460`,
      `data/moves/{fox,falco}.json moves["ftCo_SM_ThrowHi"].events`.
  - ThrowHi crossed-prev frame-20 article spawn slice:
    - `ftAction_80073354` can advance the throw command cursor across the frame-20
      `set_throw_spawn_projectile` command and `ftFx_Throw_Anim` can consume `throw_flags_b0` in
      the source frame before the next teacher-forced seed. Slippi does not expose that consumed
      command cursor, but the prefix-causal `throw_pulse_crossed_prev_frame` lane records the
      frame-20 crossing. Runtime now re-emits only this ThrowHi frame-20 crossed-prev state1 shot
      when the owner does not already have a live state1 throw shot; existing article carry/despawn
      rows stay on the normal lifetime owner.
    - Replay-real locks: `BlondHardHippopotamus.msl:{938,1673,4337}` cover the recovered frame-20
      article spawn, while `BHH:1208` (owner already has a state1 throw shot) remains the adjacent
      negative control. `BHH:937` moved to the later same-character item callback phase.
    - Fresh taxonomy after this slice: primary total `611` unchanged;
      `F14c=10`, `F14d=2`, `F15a=11`, `F15b=8`, `F16a=0`, `F16b=0`, `F16c=0`, `F16d=31`.
      Aggregate total `5192` (down from `5269`); `F14c=405` (down from `475`),
      `F14d=67` (down from `75`), `F15a=21`, `F15b=124`, `F16a=0`, `F16b=0`,
      `F16c=0`, `F16d=116`. Section-6 and ledge/collision-env families remain closed
      (`F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`).
    Sources: `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim`,
    `refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}`,
    `data/moves/{fox,falco}.json moves["ftCo_SM_ThrowHi"].events`.
  - Falco ThrowHi frame-24 carried BODY slice:
    - The final Falco ThrowHi `set_throw_spawn_projectile` pulse can coexist with an already-hit
      victim still frozen in same-owner hitlag. In that narrow frame-24 pulse window, replay shows
      the fresh/carrying state1 article should not be consumed as a new BODY hit when the victim's
      replay-facing damage provenance has already cleared (`last_attack_landed==0`).
    - Runtime suppresses only Falco ThrowHi state1 BODY contact on current or crossed-prev frame 24,
      requires the high-hitlag carry phase after runtime timer tick, and leaves the lower-hitlag
      follow-up handoff, Fox frame-18/frame-20, and active damage rows on the normal BODY path.
    - Replay-real locks: `GracefulAttachedTurtle.msl:3426` and
      `TreasuredBackKangaroo.msl:5090` cover the positive frame-24 carry rows; `GAT:3427` covers
      the lower-hitlag handoff that must consume normally, and `BHH:1208` remains the negative Fox
      frame-20 control. `BHH:937` moved to the later same-character item callback phase.
    - Fresh taxonomy after this slice and the handoff refinement: primary total `595` (down from
      `611`); `F14c=0` (down from `10`), `F14d=0` (down from `2`), `F15a=11`, `F15b=8`,
      `F16a=0`, `F16b=0`, `F16c=0`, `F16d=31`. Aggregate total `5184` (down from `5192`);
      `F14c=400`, `F14d=66`, `F15a=21`, `F15b=112`, `F16a=0`, `F16b=0`, `F16c=0`,
      `F16d=106`. Section-6 and ledge/collision-env families remain closed
      (`F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`).
    Sources: `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim`,
    `refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}`,
    `refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C4D4`,
    `data/moves/falco.json moves["ftCo_SM_ThrowHi"].events`.
  - Grounded laser BODY segment slice:
    - Laser item collision is segment-owned: `itFoxlaser_UnkMotion1_Phys` snapshots the previous
      projectile position and `it_8029C4D4` dispatches contact over the previous-to-current
      segment. Runtime now uses that swept segment for grounded late-Dash, Dash-to-Turn, and
      AttackHi3 item BODY rows that were missed by current-point probing. Early Dash remains
      excluded because it is still a proven false-positive slice until the full grounded
      hurt-status / pose discriminator is promoted.
    - `it_8029C4D4` delegates to the generic item stage-collision helper `it_8026E9A4`; this is not
      floor-only. Laser articles crossing FD wall or ceiling segments set `lifeTimer=1` through the
      same callback path. PPA `1159 -> 1161` locks the left-wall-under-ledge boundary that otherwise
      leaves an expired laser alive long enough to shift later item instance/slot identity.
    - Replay-real locks: `DistinctCaringCobra.msl:7619` covers late-Dash damage/clear,
      `BlondHardHippopotamus.msl:5148` covers Dash-to-Turn item clear without damage entry, and
      `HilariousVillainousGiraffe.msl:1024` covers AttackHi3 damage/clear. Adjacent no-hit
      controls `DCC:7618`, `BHH:5147`, and `HVG:1023` keep the branch from becoming a generic
      grounded sweep.
    - Fresh taxonomy after this slice: primary total `611` unchanged;
      `F14c=10`, `F14d=2`, `F15a=11`, `F15b=8`, `F16a=0`, `F16b=0`, `F16c=0`, `F16d=31`.
      Aggregate total `5342` (down from `5393`); `F14c=475`, `F14d=75`, `F15a=21`,
      `F15b=124`, `F16a=2`, `F16b=22`, `F16c=0`, `F16d=161` (down from `205`).
      Section-6 and ledge/collision-env families remain closed
      (`F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`).
    Sources: `refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Phys,it_8029C4D4}`,
    `refs/melee/src/melee/it/it_266F.c::it_8026E9A4`,
    `refs/melee/src/melee/it/itcoll.c::it_80272460`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::{ftCo_Dash_Anim,ftCo_Dash_IASA}`.
  - Laser item phantom / tip-log BODY slice:
    - Small positive item BODY overlaps now take the same phantom/tip-log owner as fighter BODY
      hits: `coll_distance < p_ftCommonData->x7A8` starts victim hitlag and item attribution without
      percent, KB, damage-state entry, stale update, or projectile consume. Damage input is still
      normalized through the item-hit path (`it_80272460`) before hitlag is calculated.
    - Replay-real locks: `TubbyCurlyHerring.msl:8969` covers Fall hitlag-only attribution with the
      Falco laser alive, and `ImpassionedAlarmedTarsier.msl:1580` covers the same lane during
      SpecialAirNLoop. Adjacent no-contact controls `TCH:8968` and `IAT:1579` stay baseline, while
      following full BODY-hit controls `TCH:8970` and `IAT:1581` still consume the projectile and
      enter damage.
    - Fresh taxonomy after this slice: primary total `611` unchanged;
      `F14c=10`, `F14d=2`, `F15a=11`, `F15b=8`, `F16a=0`, `F16b=0`, `F16c=0`, `F16d=31`.
      Aggregate total `5310` (down from `5342`); `F14c=475`, `F14d=75`, `F15a=21`,
      `F15b=124`, `F16a=2`, `F16b=13` (down from `22`), `F16c=0`, `F16d=146` (down from `161`).
      Section-6 and ledge/collision-env families remain closed
      (`F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`).
    Sources: `refs/melee/src/melee/ft/ftcoll.c::{checkTipLog,inlineB1,ftColl_80076ED8}`,
    `refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC`,
    `refs/melee/src/melee/it/itcoll.c::it_80272460`.
  - Laser HitCapsule x58/x4C scale ownership:
    - Item hitcapsules carry `x58 -> x4C` across `it_8027137C`: the previous post-frame x4C is
      copied into x58, then current x4C is rebuilt from the current JObj transform. Fox/Falco laser
      anim advances `foxlaser.scale` once per item anim update, so BODY sweeps must use the
      previous post-frame scale for x58 and the current post-anim scale for x4C. Using the current
      scale for both ends over-extends trailing laser BODY segments and creates false consumes.
    - Replay-real locks: `BlondHardHippopotamus.msl:641` and
      `ImpassionedAlarmedTarsier.msl:1792` cover false trailing BODY consumes that now stay alive;
      adjacent controls `BHH:640` and `IAT:1791` stay alive, and `IAT:1793` still performs the
      following full BODY hit. `GracefulAttachedTurtle.msl:7215` is a negative sentinel proving this
      is not the rejected unscaled-offset fallback.
    - The forensic row runner now emits `item_laser_probes` with scaled/unscaled laser segments,
      hurtcap flags, margins, age, scale, and item/ref/out deltas for targeted item BODY rows.
    - Fresh taxonomy after this slice: primary total `611` unchanged;
      `F14c=10`, `F14d=2`, `F15a=11`, `F15b=8`, `F16a=0`, `F16b=0`, `F16c=0`, `F16d=31`.
      Aggregate total `5281` (down from `5303`); `F14c=475`, `F14d=75`, `F15a=21`,
      `F15b=124`, `F16a=0`, `F16b=0`, `F16c=0`, `F16d=126` (down from `144`).
      Section-6 and ledge/collision-env families remain closed
      (`F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`).
    Sources: `refs/melee/src/melee/it/itcoll.c::it_8027137C`,
    `refs/melee/src/melee/it/items/itfoxlaser.c::itFoxlaser_UnkMotion1_Anim`,
    `refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C`.
  - Passive hidden-colanim Fox-laser BODY guard:
    - Passive keeps the system colanim hit-status lane across motion entry
      (`ftCo_MF_Passive | Ft_MF_KeepColAnimHitStatus`). On the replay-proven Fox-laser Passive
      row, visible hurtbox/contact geometry can admit an item BODY overlap while the hidden
      colanim owner still rejects damage and item consume. Runtime now keeps type-54 Fox lasers
      alive on Passive BODY overlap and leaves fighter source/combo fields untouched.
    - This slice is deliberately not a generic Passive or hit-status shortcut: Falco type-55
      Passive laser contacts remain on the normal BODY consume path.
    - Replay-real locks: `TreasuredBackKangaroo.msl:6197` covers the Fox-laser keepalive; the
      negative sentinel `PositiveRevolvingHyena.msl:3886` proves Falco-laser Passive contact still
      consumes.
    - Fresh taxonomy after this slice: primary total `587` (down from `595`);
      `F14c=0`, `F14d=0`, `F15a=10`, `F15b=8`, `F16a=0`, `F16b=0`, `F16c=0`,
      `F16d=27` (down from `31`). Aggregate total `5176` (down from `5184`);
      `F14c=400`, `F14d=66`, `F15a=18`, `F15b=112`, `F16a=0`, `F16b=0`,
      `F16c=0`, `F16d=102` (down from `106`). Section-6 and ledge/collision-env families
      remain closed (`F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`).
    Sources: `refs/melee/src/melee/ft/chara/ftCommon/forward.h::ftCo_MF_Passive`,
    `refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState`,
    `refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B868`.
  - Fox Illusion end-state BODY sweep now promotes `ghostEffectPos[2]` as the prefix-causal
    previous hitcapsule endpoint. Decomp owner: `ftFox_SpecialS_SetPhys` advances the ghost ring as
    `ghost2 = ghost1; ghost1 = ghost0; ghost0 = cur_pos`, `itFoxillusion_UnkMotion{0,1}_Phys`
    copies `ghostEffectPos[1]` into the article position, and `it_8027137C` preserves the previous
    x4C endpoint in x58 before rebuilding current collision state. Runtime keeps main-state rows on
    the existing ghost[1] point owner until their remaining hitlist/callback discriminator is
    exposed, and only admits the ghost[2]->ghost[1] sweep for Fox Illusion end-state rows.
    Replay-real locks: `DistinctCaringCobra.msl:4761` covers the missing Fox Illusion BODY hit, and
    adjacent `DCC:4760` proves the lane does not hit the preceding AttackAir entry row. Seed-history
    coverage proves `derive_illusion_ghost_pos012` carries ghost[2] from the previous ghost[1].
    Fresh taxonomy after this slice: primary total `611` unchanged;
    `F14c=10`, `F14d=2`, `F15a=11`, `F15b=8`, `F16a=0`, `F16b=0`, `F16c=0`, `F16d=31`.
    Aggregate total `5269` (down from `5281`); `F14c=475`, `F14d=75`, `F15a=21`,
    `F15b=124`, `F16a=0`, `F16b=0`, `F16c=0`, `F16d=116` (down from `126`).
    Section-6 and ledge/collision-env families remain closed
    (`F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`).
    Sources: `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFox_SpecialS_SetPhys`,
    `refs/melee/src/melee/it/items/itfoxillusion.c::{
    itFoxillusion_UnkMotion0_Phys,itFoxillusion_UnkMotion1_Phys}`,
    `refs/melee/src/melee/it/itcoll.c::it_8027137C`.
  - Blaster gun Dead* exit lifetime slice:
    - When a SpecialN/SpecialAirN blaster owner crosses directly into a common Dead* state, the
      blaster gun article does not persist as a SpecialNEnd linger. Dead* states are outside
      `ftFx_SpecialN_GetBlasterAction`; `itFoxblaster_UnkMotion8_Anim` therefore reaches the
      `blaster_action == 9` clear path and consumes the stale gun while preserving already-spawned
      laser shots.
    - Replay-real locks: `PriceyPartialAlbatross.msl:1185` covers `SpecialAirNEnd -> DeadDown`
      stale-gun clear with the live laser compacted into slot 0, and adjacent row
      `PriceyPartialAlbatross.msl:1184` keeps the pre-death SpecialAirNEnd gun alive.
    Sources: `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
    ftFx_SpecialN_GetBlasterAction,ftFx_SpecialN_CheckRemoveBlaster}`,
    `refs/melee/src/melee/it/items/itfoxblaster.c::itFoxblaster_UnkMotion8_Anim`,
    `refs/melee/src/melee/ft/ftmotionstates.c`,
    `refs/melee/src/melee/ft/chara/ftCommon/forward.h::ftCommon_MotionState`.
  - Rebirth blaster-gun spawn fallout:
    - `TubbyCurlyHerring.msl:3062` was closed by the match-flow owner: RebirthWait IASA now enters
      `SpecialAirNStart` through the priority aerial-special branch and applies the RebirthWait
      exit x1994/x198C colanim write before the destination row. The remaining blaster-gun article
      state/animation details are no longer routed through `F04_match_flow_rebirth`; future gun
      state-machine parity belongs to `F19_specialn_blaster_article`.
    - `DistinctCaringCobra.msl:546` is also not a blaster article identity owner: a pre-combat debug
      step enters `FX_SPECIAL_AIR_N_START` and spawns the Falco gun, then combat moves the player to
      `DamageAir2`; the reference goes straight to `DamageAir2` with no gun. That false-gun row is
      action-entry / combat-order fallout and routes to `F09c_aerial_action_entry_adjacency`.
    - Pure laser `item_instance_id` echoes at `HungryImportantSnake.msl:6603` and
      `PutridJoyousOryx.msl:2235` are not independent slot-compaction owners. In both rows the
      mismatching slot is explained by the neighboring old-laser consume/keepalive disagreement, so
      these pure laser instance rows route to `F16d_item_body_lifetime`.
    - Fresh taxonomy after the Dead* lifetime slice plus these hard moves: primary total `611`;
      `F14c=10`, `F14d=2`, `F15a=11`, `F15b=8`, `F16a=0`, `F16b=0`, `F16c=0`, `F16d=31`.
      Aggregate total `5303`; `F14c=475`, `F14d=75`, `F15a=21`, `F15b=124`, `F16a=2`,
      `F16b=5` (down from `13`), `F16c=0`, `F16d=142` (down from `146`). Section-6 and
      ledge/collision-env families remain closed (`F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`).
      After folding the two pure laser instance echoes into `F16d`, aggregate remains `5303` with
      `F16a=0`, `F16b=0`, and `F16d=144`.
    Sources: `refs/melee/src/melee/ft/chara/ftCommon/forward.h::ftCommon_MotionState`,
    `refs/melee/src/melee/ft/ftmotionstates.c`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_IASA`,
    `refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState`.
  - SpecialN landing gun/shot slot echo hard move:
    - `HungryImportantSnake.msl:6603/6604` and `PutridJoyousOryx.msl:2235` are SpecialN Loop /
      AirLoop -> Landing handoff rows where the blaster gun and shot slots echo across the action
      transition. They do not represent an independent item BODY consume-vs-persist owner:
      `HIS:6603` has a gun in slot 0 and the shot identity moving through slots 1/2 while p0 enters
      Landing, `HIS:6604` is the immediate continuation, and `PJO:2235` has the same AirLoop ->
      Landing slot churn. These route to `F09c_aerial_action_entry_adjacency`.
    - Negative controls stay in item BODY lifetime: `TBK:6197` and `HIS:6544` are type-54 laser
      false-consume rows with no SpecialN landing slot echo, and `PPA:5141` / `PRH:8137` are
      remaining type-55 false BODY rows. They stay in `F16d_item_body_lifetime`.
    - Fresh taxonomy after this hard move: primary total `611`; `F14c=10`, `F14d=2`, `F15a=11`,
      `F15b=8`, `F16a=0`, `F16b=0`, `F16c=0`, `F16d=31`. Aggregate total `5192`;
      `F14c=405`, `F14d=67`, `F15a=21`, `F15b=112` (down from `124`), `F16a=0`,
      `F16b=0`, `F16c=0`, `F16d=106` (down from `116`). Section-6 and ledge/collision-env
      families remain closed (`F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`).
    Sources: `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
    ftFx_SpecialNLoop_Anim,ftFx_SpecialAirNLoop_Anim,ftFx_SpecialNEnd_Anim}`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Enter_Basic`,
    `refs/slippi-ssbm-asm/Recording/SendItemInfo.s`.
  - Guard-context pure blaster-gun xDA8 hard move:
    - `AttachedGoodNaturedGuanaco.msl:124` is not a reflect owner-transfer row: the type-75
      blaster gun has matching existence/type/state/owner and only `item_instance_id`
      (`item->xDA8_short`) differs while the peer guard context is adjacent. This is the same
      generic fighter-parent item xDA8 / instance-counter owner as the earlier pure blaster-gun
      rows, so taxonomy now routes pure gun xDA8 rows before guard/reflect item routing.
    - Fresh taxonomy after this hard move: primary total `595`; `F14c=0`, `F14d=0`,
      `F15a=10` (down from `11`), `F15b=8`, `F16a=0`, `F16b=0`, `F16c=0`, `F16d=31`.
      Aggregate total `5184`; `F14c=400`, `F14d=66`, `F15a=18` (down from `21`),
      `F15b=112`, `F16a=0`, `F16b=0`, `F16c=0`, `F16d=106`. Section-6 and
      ledge/collision-env families remain closed (`F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`).
    Sources: `refs/melee/src/melee/it/it_2725.c::it_8027B070`,
    `refs/slippi-ssbm-asm/Recording/SendItemInfo.s`.
  - Spawn-frame powershield reflect owner/xDA8 commit:
    - Newly spawned SpecialN lasers can overlap GuardReflect in the same item logic pass. For that
      spawn-frame-only path, runtime now commits the replay-visible owner and `xDA8_short`
      (`item_instance_id`) when the powershield reflect snapshot is staged, matching
      `ftColl_80077464` -> `Item_80269F14` before Slippi's item post-frame record.
    - Older reflected lasers outside the retained GuardOn-follow-up / aged ReflectDesc lanes remain
      on the staged-owner lane; the broad same-frame transfer is still rejected because it
      over-transfers steady GuardReflect keepalive rows.
    - Open residual / package-boundary negative: GAT `5223 -> 5281` reflected-laser keepalive is
      not a replay-exact ShieldBounced closure in this package. It stays on the visible negative
      path until explicit ShieldBounced/live normal-owner provenance replaces the broader
      shield-contact inference.
    - Replay-real locks: `AttachedGoodNaturedGuanaco.msl:428` and `AGG:3345` cover the positive
      spawn-frame transfer rows.
    - Fresh taxonomy after this slice: primary total `583` (down from `587`);
      `F14c=0`, `F14d=0`, `F15a=6` (down from `10`), `F15b=8`, `F16a=0`, `F16b=0`,
      `F16c=0`, `F16d=27`. Aggregate total `5174` (down from `5176`);
      `F14c=400`, `F14d=66`, `F15a=16` (down from `18`), `F15b=112`, `F16a=0`,
      `F16b=0`, `F16c=0`, `F16d=102`. Section-6 and ledge/collision-env families remain closed
      (`F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`).
    Sources: `refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464`,
    `refs/melee/src/melee/it/item.c::Item_80269F14`,
    `refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C504,it_8029C4D4}`.
  - Aged GuardReflect ReflectDesc owner/xDA8 and HitShield handoff:
    - Aged Falco lasers now commit owner/xDA8 when they are still approaching the reflector, overlap
      the vertical lane of the GuardReflect shield-bone `ReflectDesc`, and are not on the final x14
      handoff tick. This closes `GAT:4828` and `TBK:7448` without reopening the rejected broad
      transfer rows.
    - Aged GuardReflect lasers below that shield-bone lane, or inside it on the final seeded x14
      tick, route to `Item_80269DC8` / `itFoxLaser_Logic94_HitShield` destruction rather than the
      reflect transfer path.
    - Replay-real locks: positives `GAT:4828` and `TBK:7448`, plus broad-transfer negatives
      `GAT:2274`, `GAT:2275`, `GAT:9479`.
    - Fresh taxonomy after this slice: primary total `568` (down from `583`);
      `F14c=0`, `F14d=0`, `F15a=2` (down from `6`), `F15b=4` (down from `8`),
      `F16a=0`, `F16b=0`, `F16c=0`, `F16d=27`. Aggregate total `5144` (down from
      `5174`); `F14c=400`, `F14d=66`, `F15a=10` (down from `16`), `F15b=108`
      (down from `112`), `F16a=0`, `F16b=0`, `F16c=0`, `F16d=102`. Section-6 and
      ledge/collision-env families remain closed (`F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`).
    Sources: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_8009370C,ftCo_80093BC0}`,
    `refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_80077464}`,
    `refs/melee/src/melee/it/item.c::{Item_80269F14,Item_80269DC8}`,
    `refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C4D4,itFoxLaser_Logic94_HitShield}`.
  - GuardOn-follow-up ReflectDesc owner/xDA8 and pure-state final-x14 HitShield handoff:
    - Latest decomp refresh (`refs/melee` `4e62f34a`) confirms `ftCo_8009388C` can enter
      GuardReflect from GuardOn and immediately call `ftCo_8009370C` to install ReflectDesc before
      the next seed-visible x14/x18 post-frame. Runtime now commits owner/xDA8 for fresh frozen
      GuardReflect rows whose visible previous owner is GuardOn, the aged laser is still approaching,
      and the laser overlaps the shield-bone ReflectDesc vertical lane. This closes primary
      `GAT:6207`.
    - Final seeded x14 HitShield handoff is retained only for the pure reflect-descriptor
      `fp+0x2218` state byte after the GuardReflect tick (`0x04`, with no high
      command/interrupt bits). This closes primary `TBK:2323` while aggregate controls with
      `0x20/0x40/0x80` high bits remain live GuardReflect articles instead of false HitShield
      destroys.
    - Post-callback final-x14 handoff can also occur when the seed-visible x14 is still `2` but
      `ftCo_GuardReflect_Anim -> ftCo_80093BC0` has ticked the live x14 lane to `1` before item
      collision. That seed>1 extension is narrower than the seeded-final lane: it still requires
      the pure reflect-state byte and ReflectDesc vertical lane, and additionally requires live
      ShieldDesc bubble overlap so same-shaped GuardReflect keepalive rows do not become broad
      HitShield destroys. This closes aggregate `MAJ:259`.
    - Replay-real locks: GuardOn-follow-up positive `GAT:6207`, final-x14 HitShield positive
      `TBK:2323`, adjacent keepalive control `TBK:2322`, aged positives `GAT:4828`/`TBK:7448`,
      and broad-transfer negatives `GAT:2274`/`GAT:2275`/`GAT:9479`.
    - Fresh taxonomy after this slice: primary total `556` (down from `568`);
      `F14c=0`, `F14d=0`, `F15a=0` (down from `2`), `F15b=0` (down from `4`),
      `F16a=0`, `F16b=0`, `F16c=0`, `F16d=27`. Aggregate total `5134` (down from
      `5144`); `F14c=400`, `F14d=66`, `F15a=10`, `F15b=104` (down from `108`),
      `F16a=0`, `F16b=0`, `F16c=0`, `F16d=102`. Section-6 and ledge/collision-env families remain
      closed (`F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`).
    Sources: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_IASA,ftCo_8009388C,ftCo_8009370C,ftCo_GuardReflect_Anim,ftCo_80093BC0}`,
    `refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_80077464}`,
    `refs/melee/src/melee/it/item.c::{Item_80269F14,Item_80269DC8}`,
    `refs/melee/src/melee/it/items/itfoxlaser.c::itFoxLaser_Logic94_HitShield`,
    `refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm`.
  - Dash-terminal same-frame GuardReflect owner/xDA8 transfer and aged ShieldDesc boundary:
    - Dash -> GuardReflect can reach the `ftCo_Dash_IASA` terminal scalar branch before the guard
      entry installs `ReflectDesc`. When a same-frame SpecialN laser then collides with that fresh
      GuardReflect descriptor, runtime commits the replay-visible owner and `xDA8_short`
      immediately, matching `ftColl_80077464 -> Item_80269F14`. The speed/orientation update is
      still consumed by the item callback, so the immediate transfer defers the same-owner speed
      write instead of applying a broad velocity rewrite.
    - Fighter-vs-fighter ShieldDesc uses the source entry path, not a broad active-x14
      GuardReflect rule. Guard-origin `ftCo_8009388C` clears `x221B_b0` and installs only
      ReflectDesc until `ftCo_80093BC0` recreates ShieldDesc; locomotion-origin
      `ftCo_80091A4C -> ftCo_800939B4 -> ftCo_80093A50` calls `ftCo_80092450` before creating
      ReflectDesc, so the first no-submotion frame can still shield-hit a same-frame fighter
      HitCapsule and use the `x221C_b2` powershield shield-damage gate. MAJ:2217 is the positive
      locomotion-entry lock. Landing IASA can enter the same guard-check front door
      (`ftCo_Landing_IASA -> ftCo_80091A4C -> ftCo_800939B4 -> ftCo_80093A50`), so Landing-origin
      GuardReflect entry also keeps ShieldDesc live for same-frame fighter shield collision; AGN
      `2369 -> 2390` locks that boundary. PRH:1510 remains the guard-origin ReflectDesc-only
      negative.
    - Aged GuardReflect owner/xDA8 transfer requires the frame-start shield descriptor active bit
      (`fp+0x221B_b0`, Slippi `state_flags[2] & 0x80`) in addition to the existing x14/x18 timer
      and ReflectDesc geometry checks. GuardReflect timer carry without that descriptor bit follows
      the shield-hit/destruction branch and must not stage a reflect snapshot. This is a minimal
      seed/runtime lane for the descriptor provenance consumed by the shield/reflect callbacks; it
      is not a character, replay, or stale-state proxy.
    - Replay-real locks cover the same-frame Dash-terminal transfer, a later Dash/GuardReflect
      shield-hit negative, retained aged transfer positives, and aged false-transfer negatives whose
      visible geometry is reflect-like but whose frame-start ShieldDesc bit is absent.
    Sources: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800939B4,ftCo_80093A50,ftCo_8009388C,ftCo_8009370C,ftCo_GuardReflect_Anim,ftCo_80093BC0}`,
    `refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_8007B1B8,ftColl_80077464,ftColl_80076CBC}`,
    `refs/melee/src/melee/it/item.c::Item_80269F14`,
    `refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm`.
  - Dash `ftCo_80091AD8` GuardOn laser-shield ordering:
    - `ftCo_Dash_IASA` keeps the `dash.x4 != 0 && cur_anim_frame <= x44` slice inside the early
      Dash branch. That branch can test EscapeF/item/catch/attack followups, but it does not fall
      through to the generic guard helpers, so a fresh L/R press in that slice must not enter
      GuardReflect.
    - The later `dash.x4 != 0` handoff slice reaches `ftCo_80091AD8 -> ftCo_800923B4` and can
      enter GuardOn from held shield input after spending the early Dash branch. In vanilla ordering
      this handoff exposes the GuardOn ShieldDesc after the current frame's item collision pass, so
      the newly-created descriptor is not eligible for same-step SpecialN laser shield contact; the
      laser shield hit appears on the next row through the normal GuardSetOff owner. Dash `x4 == 0`
      rows that reach `ftCo_80091AD8` directly are not deferred and can take same-step shield
      contact.
    - Runtime therefore gates the fresh GuardReflect admission by the Dash `x4/x44` source branch
      and tags GuardOn entries that came through the Dash `dash.x4 != 0` / `ftCo_80091AD8` handoff
      slice so item shield precedence can defer only that same-frame contact. This replaces a
      visible fresh-input proxy with the decomp branch predicate and preserves Dash rows that
      legitimately enter GuardReflect or same-frame GuardSetOff.
    Sources: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091AD8,ftCo_800923B4}`.
  - Guard ShieldDesc replay-only accept/miss seed:
    - `ftColl_80078C70` checks the defender ShieldDesc path before BODY hurtcap selection. A
      replay-visible GuardSetOff plus attacker/defender hitlag proves the hidden
      `lbColl_80007BCC -> ftColl_80076CBC` shield path accepted; a replay-visible BODY damage hit
      plus attacker/defender hitlag proves the ShieldDesc path did not accept, because accepted
      shield contact suppresses BODY. The seed lane stores that accept/miss result for one-step
      reseeds only; normal rollouts keep using live geometry.
    - Replay-real locks: `PositiveRevolvingHyena.msl:11352` covers the accepted GuardSetOff lane,
      and `ImpassionedAlarmedTarsier.msl:3702` covers the BODY-hit negative where a broad shield
      rim proxy would falsely enter GuardSetOff.
    Sources: `refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC,ftColl_80076ED8}`,
    `refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC`.
  - Guard ShieldDesc live no-tilt pose:
    - `ftCo_80091E78` updates `mv.co.guard.x8/x4` every active Guard frame, but only samples the
      angled Guard timeline (`ftAnim_80070710(..., x8)`) when `x4` is nonzero. For no-tilt Guard
      (`x4 == 0`), live ShieldDesc collision uses the current Guard pose, not the x8 neutral target.
      For `0 < x4 < 1`, source blends that angled pose against the same current/no-tilt pose via
      `ftAnim_80070108(..., 1 - x4, x4, ft_data->x20)`. Runtime applies this frame-0 base locally
      inside the fighter-vs-fighter `lbColl_80007BCC` helper; the shared `shield_pos_*` bubble stays
      with the replay-visible/item-shield owner. This keeps tiny x4 residues from over-admitting
      BODY-vs-shield rim contacts without regressing projectile shield accepts.
    - `lbColl_80007BCC` forwards the ShieldDesc JObj matrix into `lbColl_80006E58`; the final local
      matrix radius term therefore carries the defender's extracted `co_attrs.model_scaling` in
      addition to the live ftCo_80091D58 shield scale. Runtime applies that model-scale term to the
      ShieldDesc overlap radius instead of a character proxy or fixed tolerance.
    - Final-x14 no-submotion `GuardReflect` is a narrower ShieldDesc handoff phase. The frozen
      replay-visible shield radius already represents the live shield JObj scale after
      `ftCo_80093BC0 -> ftCo_80092450`; fighter-vs-fighter shield admission must not add the
      generic model-scale radius or enable-edge sweep proxy without explicit accepted ShieldDesc
      provenance. TBK `1403 -> 1427` locks the near-rim shine BODY negative.
    - Active no-submotion `GuardReflect` has a split ShieldDesc/ReflectDesc owner. The
      `ftCo_8009388C` path clears `x221B_b0` and creates ReflectDesc only; `ftCo_80093BC0`
      recreates ShieldDesc after x14 expires. Without a teacher-forced ShieldDesc accept seed,
      active-x14 no-submotion `GuardReflect` snapshots therefore must not use the stale shield
      bubble to suppress BODY. PRH `1510` locks the negative boundary where a stale ShieldDesc
      proxy incorrectly takes `GuardSetOff`, while the source-shaped owner lets the later BODY path
      apply `DamageFlyHi`.
    - Locomotion-origin `GuardReflect` is the opposite active-x14 entry lane:
      `ftCo_80091A4C -> ftCo_800939B4 -> ftCo_80093A50` calls `ftCo_80092450`, then
      `ftCo_800921DC -> ftCo_80091E78(..., 0)` before fighter-vs-fighter collision. That
      entry frame uses the extracted GuardOn current-pose ShieldDesc center
      (`data/shields/{fox,falco}.bin::guard_on_xyz[0]`), not the settled Guard neutral center.
      PPA `3111 -> 3148` locks the Dash -> GuardReflect same-frame AttackAirB shield hit.
    - Expired-x14 no-submotion `GuardReflect` is past the ReflectDesc-only phase. `ftCo_80093BC0`
      has recreated ShieldDesc through `ftCo_80092450`, but if that shield overlap misses, BODY
      still consumes the GuardReflect motion-state's GuardOn submotion hurtcaps. GAT
      `11080 -> 11085` locks this BODY handoff; a mutated active-x14 control keeps the raw
      no-submotion/no-BODY boundary.
    - The exact active-to-expired callback boundary (`x14_seed==1 -> x14==0`) is also a live
      ShieldDesc recreation boundary. `ftCo_GuardReflect_Anim -> ftCo_80093BC0` recreates
      ShieldDesc before `ftColl_80078C70`, while x18/x221C_b2 can still suppress shield damage.
      Runtime admits that current-pose ShieldDesc only for `guard_reflect_origin_guardon_u8`
      episodes and current-only/new HitCapsules; persistent capsules keep their ordinary x58->x4C
      owner, and direct-locomotion powershields stay on the `ftCo_80093A50` lane. This avoids the
      adjacent GAT and PRH final-x14 false shield hits. TCH `5224 -> 5251` locks the positive, TCH
      x14 mutation locks pre-/post-boundary negatives, and GAT `11080 -> 11085` / PRH
      `1483 -> 1510` remain controls.
    - The later final-x14/x18 boundary has a separate carried-contact owner. When a non-GuardOn-origin
      no-submotion `GuardReflect` has x14 already expired but frame-start x18 still greater than 1,
      fighter BODY overlaps are kept out of full damage; runtime records a compact internal
      attacker/defender carry. On the following callback, if x18 expires and that same-pair carry is
      present, the persistent HitCapsule can consume the recreated ShieldDesc size/extent lane and
      enter `GuardSetOff`. This is not a broad x18-expiry shield accept: rows without the prior
      suppressed BODY overlap, and GuardOn-origin frozen BODY handoffs, stay on their existing
      boundaries. CDO `12022 -> 12050/12051` locks the BODY negative then GuardSetOff positive; MAJ
      `4166 -> 4472` and GAT `11080 -> 11085` remain controls.
    - The same `ftCo_8009388C` GuardOn -> GuardReflect path keeps the live GuardOn JObj pose for
      fighter-vs-fighter BODY hurtcaps on no-submotion snapshots. Runtime admits the GuardReflect
      submotion hurtcap fallback only when the transition provenance is GuardOn: live
      `prev_action_id` for same-step GuardOn entries, or `seed_prev_action_id` for already-seeded
      GuardReflect snapshots whose live prev has advanced before hurtcap refresh. Without that
      provenance, active no-submotion GuardReflect remains on the raw snapshot geometry and cannot
      become a broad BODY-contact shortcut.
    - Catch selection is a separate GuardReflect no-submotion owner. `ftColl_80078A2C` selects
      `Catch`/`CatchDash` victims from grabbable hurt capsules, not BODY-enabled capsules or the
      ShieldDesc path. Late GuardReflect snapshots with no serialized submotion therefore compute
      the GuardOn-table grabbable capsule pose locally inside catch selection only; they do not
      populate global BODY/debug hurtcap state. PJO `4221 -> 4240` locks the positive catch
      connection, while the same row's debug BODY candidates remain empty.
    - The input-side `GuardReflect_IASA -> Catch_CheckInput` branch is gated by the frame-start x14
      ReflectDesc timer on no-submotion snapshots. If x14 is still live when
      `ftCo_80093BC0` ticks, the same frozen row does not expose a fresh A+LR catch consume; once
      x14 has expired, x18 alone keeps powershield-active state bits but does not suppress
      `Catch_CheckInput`. CDO `1328` locks the live-x14 negative and TCH `11792` locks the x18-only
      catch positive (`src/grab_flow.c`; refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
      ftCo_GuardReflect_Anim,ftCo_80093BC0,ftCo_GuardReflect_IASA},
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Catch_CheckInput).
    - No-submotion `GuardOn` entry snapshots (`animation_index=-1`, `action_frame<0`) use
      `data/shields/{fox,falco}.bin::guard_on_xyz[0]` with the same live model-scaled ShieldDesc
      bone only when the previous post-frame owner is not already a shield action. Source path:
      `ftCo_800924C0` creates ShieldDesc, immediately ticks GuardOn, then `ftCo_800921DC` zeroes
      the shield joint translate and calls `ftCo_80091E78(..., 0)`. `lbColl_80007BCC` consumes that
      current-pose bone before the first settled Guard frame. Later no-submotion GuardOn rows are
      already inside shield ownership and keep their normal tilt/neutral placement; PRH `11242`
      locks this negative. GuardReflect and nonzero-x4 angled Guard retain the earlier
      neutral-target collision-subtree scaling path until their separate pose owners are proved.
    - The same raw `fp+0x2218_b1` command-pose shield-center lane applies to steady `Guard` as well
      as `GuardOn` on the newborn SpecialN article pass. A first-frame laser checks the live command
      pose before the settled Guard bubble can be used by later item callbacks; DSG `7430` locks the
      Guard positive while existing GuardOn controls keep the one-frame scope
      (`src/items.c`; refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm,
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091E78,ftCo_80092450},
      refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C504,itFoxlaser_UnkMotion1_Anim}).
    - `fp->lightshield_amount` is a Guard/GuardOn/GuardReflect latch, not just current trigger
      input. `ftCo_800925A4` snapshots the previous value into `mv.co.guard.x2C`, and when the
      current trigger is below the shield deadzone it preserves that value instead of recomputing a
      hard-shield bubble. Already-shielding no-submotion GuardOn rows therefore consume the latched
      lightshield scale and must not promote `lbColl_80006E58`'s broadphase extent into a final
      ShieldDesc accept. FSP `3874 -> 3875` locks the negative shine/GuardOn boundary, and clearing
      the hidden latch on the same seed restores the false GuardSetOff hit.
    - Dolphin probe evidence: `PositiveRevolvingHyena.msl:11352` accepts ShieldDesc with Falco's
      shield center at roughly `(39.718, 9.900, 0.598)`, matching Guard frame 0 after Falco's
      model scale. The old runtime used frame 10 without model scaling and missed before falling
      into BODY.
    - Replay-real runtime locks cover PRH `11336 -> 11352` accepted ShieldDesc, IAT
      `3831 -> 3847` no-submotion GuardOn accepted ShieldDesc, FSP `3874 -> 3875` already-shielding
      no-submotion GuardOn lightshield negative, IAT `3686 -> 3702` BODY negative, and DCC
      `9072 -> 9094` locks the steady-Guard BODY negative where a neutral-pose / unscaled-radius
      proxy falsely entered GuardSetOff. This is not a broad shield-rim suppressor.
    Sources: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
      ftCo_80091BC4,ftCo_80091E78,ftCo_800924C0,ftCo_800921DC}`,
    `refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC`,
    `data/shields/{fox,falco}.bin`, `data/characters/{fox,falco}.json`.
  - Guard tilt no-submotion BODY pose:
    - Steady `Guard` animation processing runs `ftCo_800925A4 -> ftCo_80091BC4` and then
      `ftCo_80091E78(..., 1)` before fighter collision. Runtime therefore updates
      `mv.co.guard.x8/x4` and ShieldDesc geometry before hurtcaps are refreshed, then samples the
      same extracted Guard AObj/JObj collision matrices for no-submotion `Guard` BODY hurtcap
      endpoints and matrix-radius checks. The BODY narrowphase also follows the
      `lbColl_8000805C`/`ftCommon_8007F804` split: with no non-unit `x34_scale.z` transform, the
      sampled live JObj depth is preserved; when that hidden transform lane is later promoted, the
      endpoint z overwrite should consume the `arg6 = cur_pos.z` path before `lbColl_80006E58`.
    - This is source-owned live pose state, not a ShieldDesc-miss seed bridge: replay
      `combat_shield_contact_hb_kind` can still decide shield-vs-BODY ordering, but it no longer
      gates whether BODY gets the angled Guard pose.
    Sources: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091E78,ftCo_80091D58}`,
    `refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70`,
    `refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_8000805C}`,
    `data/anims/{fox,falco}.tracks.bin`, `data/anims/{fox,falco}.bin`,
    `data/shields/{fox,falco}.bin::MSLSHLD1`.
  - Guard hold-drain ShieldBreakFly entry:
    - `ftCo_800925A4` drains shield HP during Guard/GuardOn/GuardReflect animation processing.
      When the drain crosses below zero, it clears the active shield state and calls
      `ftCo_80098B20`, which changes motion to `ShieldBreakFly` and immediately ticks the
      animation. Runtime now takes that transition on the drain-crossing frame instead of leaving
      the fighter in Guard for one more row.
    - `ftCo_80098B20` seeds vertical velocity from `co_attrs.x94`; this is extracted as
      `data/characters/{fox,falco}.json::shield_break_initial_velocity`. The entry frame then runs
      `ftCo_ShieldBreakFly_Phys -> ft_80084EEC`, so gravity is applied before the replay-visible
      position/velocity snapshot. `ftCommon_8007D5D4` flips the fighter airborne and locks ECB
      with `x1968_jumpsUsed=1`, so Slippi-visible `jumps_left` becomes `max_jumps - 1` while the
      break-entry post-frame still exposes the Guard floor id. `ftCo_80098B20` also calls
      `ftColl_8007B62C(..., 2)`, and ShieldBreakFall/Down/Stand keep that hit status until Furafura
      clears it. The visible `0.07` shield HP on PRH's break row is normal inactive-shield recharge
      after the break, not a replay-fit reset constant.
    - `ShieldBreakFly_Coll` and `ShieldBreakFall_Coll` use `ft_80082C74(..., ftCo_80098E3C)`.
      On floor contact, `ftCo_80098E3C` refreshes grounded bookkeeping through `ftCommon_8007D7FC`
      and enters `ShieldBreakDownU/D`, selected by `ftCo_80097570` from the live HipN matrix. Runtime
      samples the same SSANIM pose predicate for Fox/Falco and does not let ShieldBreakFly remain as
      a grounded airborne action after mpColl reports floor contact.
    - Replay-real locks cover PRH's final low-shield Guard frame and the following break row, so
      this is a drain-crossing owner rather than a broad low-shield action shortcut.
    - The same shield-break family now carries the `ShieldBreakStand{U,D}_Anim` end handoff:
      when `ftAnim_IsFramesRemaining` is false, `ftCo_80099010` enters `Furafura` without an
      immediate animation tick, resets shield HP from `p_ftCommonData->x280`, and then the normal
      inactive-shield recharge step contributes the replay-visible `30.07` shield HP. Because
      `ftCo_80099010` no longer uses ShieldBreakStand's `KeepColAnimHitStatus | SkipColAnim`
      motion flags, it also clears the x198C colanim hit-status/timer lanes on Furafura entry. This
      closes PRH's `ShieldBreakStandU -> Furafura` rollout break before the later contact/damage row.
    Sources: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800925A4`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_ShieldBreakFly.c::{ftCo_80098B20,ftCo_ShieldBreakFly_Phys,ftCo_ShieldBreakFly_Coll}`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_ShieldBreakFall.c::ftCo_ShieldBreakFall_Coll`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_ShieldBreakDown.c::ftCo_80098E3C`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_80097570`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_ShieldBreakStand.c::ftCo_ShieldBreakStand_Anim`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Furafura.c::ftCo_80099010`,
    `refs/melee/src/melee/ft/ft_081B.c::ft_80084EEC`,
    `refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B62C`, and
    `data/common/ft_common_data.json`, `data/characters/{fox,falco}.json`.
  - Airborne Fall Falco-laser BODY hurtcap-Z lane:
    - `ftColl_8007925C` routes item BODY through `lbColl_8000805C`, passing
      `ftCommon_8007F804(fp)` and `fp->cur_pos.z`; `lbColl_8000805C` rewrites both hurt capsule
      endpoint Z values before the segment/capsule test. Runtime now mirrors that flattened-Z
      source lane for state0 Falco lasers hitting vulnerable airborne `Fall` rows.
    - The retained branch is guarded by the item victim-ring surface: if the defender already
      carries the same owner attack id (`last_attack_landed == item_attack_id`), the row stays on
      the existing keepalive path, matching `it_8026FAC4` / `lbColl_80008688` victim-list
      ownership instead of creating a fresh BODY consume.
    - Replay-real locks: `TreasuredBackKangaroo.msl:2901` covers the positive BODY hit and laser
      consume; adjacent `TBK:2900` stays alive; `PriceyPartialAlbatross.msl:4124` protects the
      same-attack victim-ring negative.
    - Fresh taxonomy after this slice: primary total `543` (down from `556`);
      `F14c=0`, `F14d=0`, `F15a=0`, `F15b=0`, `F16a=0`, `F16b=0`, `F16c=0`,
      `F16d=14` (down from `27`). Aggregate total `5121` (down from `5134`);
      `F14c=400`, `F14d=66`, `F15a=10`, `F15b=104`, `F16a=0`, `F16b=0`,
      `F16c=0`, `F16d=89` (down from `102`). Section-6 and ledge/collision-env families remain
      closed (`F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`).
    Sources: `refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077C60}`,
    `refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80008688}`,
    `refs/melee/src/melee/it/itcoll.c::{it_8026FAC4,it_8026FA2C}`.
  - Grounded Dash-to-Turn Falco-laser BODY `lbColl` hurt-radius lane:
    - `ftColl_8007925C` reaches item BODY only after reflect/absorb/shield, then calls
      `lbColl_8000805C`; `lbColl_8000805C` forwards `arg1->scale` and
      `lbColl_804D7A38 * fp->x34_scale.y` to `lbColl_80006E58`, whose broad/effective radius is
      `hit_radius + hurt_radius * arg11`. Runtime now promotes that decomp radius only for the
      lower/mid hurtcap Falco-laser Dash->Turn handoff left after the grounded segment slice.
    - The retained branch stays on scaled laser hitcap positions and does not revive the rejected
      unscaled-offset fallback. High/head-only caps remain on the exact path after `TBK:4136`
      proved broad all-cap radius promotion is a false consume.
    - Replay-real locks: `GracefulAttachedTurtle.msl:7215` is the positive BODY consume; adjacent
      `GAT:7214` stays alive; high-cap negative `TreasuredBackKangaroo.msl:4136` stays on Turn with
      the laser alive; the previous-scale test now keeps `GAT:7214` as the unscaled-fallback
      sentinel.
    - Fresh taxonomy after this slice: primary total `528` (down from `543`);
      `F14c=0`, `F14d=0`, `F15a=0`, `F15b=0`, `F16a=0`, `F16b=0`, `F16c=0`,
      `F16d=0` (down from `14`). Aggregate total `5106` (down from `5121`);
      `F14c=400`, `F14d=66`, `F15a=10`, `F15b=104`, `F16a=0`, `F16b=0`,
      `F16c=0`, `F16d=75` (down from `89`). Section-6 and ledge/collision-env families remain
      closed (`F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`).
    Sources: `refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077C60}`,
    `refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Enter_Smash`.
  - Rejected EscapeF frame-20 Falco-laser BODY slice:
    - `MSLFTSC1` `set_hit_status` explains the first vulnerable EscapeF frame, but the retained runtime does not
      promote EscapeF to the grounded lbColl hurt-radius lane. Debug evidence points to the broader
      live item BODY hurt-capsule/JObj pose-selection owner; a local EscapeF action-frame slice is
      not retained.
  - Terminal x1990 hidden-colanim item BODY guard:
    - `Fighter_8006A360` decrements `x1990` and can clear visible `x198C` for Slippi t+1 before
      the item slot compare, but the same frame's item BODY pass still follows
      `ftColl_8007925C`'s `x1988 == 2 || x198C == 2` collision-status gate.
    - Runtime now carries a one-frame internal `colanim_terminal_x1990_item_body_guard` when
      `x198C=2`, `x1990=1`, `x1994=0`, and `x2221_b0=0`, so terminal hidden-status rows can keep
      the laser alive while visible `hurtbox_state` clears. Replay-real locks are in
      `tests/test_laser_body_colanim_terminal_replay_real_locks.py`: positive `HIS:6544` and a
      disabled-contact negative `PRH:4757`.
    Sources: `refs/melee/src/melee/ft/fighter.c::Fighter_8006A360`,
    `refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_8007B868}`.
  - Terminal x1990+x1994 hidden-colanim item BODY carry:
    - Explicit prefix-causal seed lanes can carry both `x1990` and `x1994` outside Shine Start.
      When `x1990` expires from 1 while `x1994` remains live, `Fighter_8006A360` sets `x198C=1`;
      `ftColl_8007925C` only blocks item BODY on collision-status value 2, so the same item pass
      must not treat the stale merged `hurtbox_state=2` seed snapshot as intangible.
    - Runtime splits the terminal marker: value `1` keeps the existing no-`x1994` guard, while
      value `2` admits the ordinary BODY/item-lifetime path. Replay-real locks are in
      `tests/test_laser_body_colanim_terminal_replay_real_locks.py`: positive `PRH:8054` and
      adjacent non-terminal negative `PRH:8053`.
    - Fresh taxonomy after this aggregate-only slice: primary total `528`; primary item-owner
      families remain closed (`F14c/F14d/F15a/F15b/F16a/F16b/F16c/F16d=0`). Aggregate total
      `5079`; remaining aggregate item rows are `F14c=400`, `F14d=66`, `F15a=10`, `F15b=104`,
      `F16d=52`, with `F16a/F16b/F16c=0`. Section-6 and ledge/collision-env families remain
      closed (`F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`). The checklist item remains active.
    Sources: `refs/melee/src/melee/ft/fighter.c::Fighter_8006A360`,
    `refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C`.
  - Previous-action SpecialN landing slot-echo taxonomy hard move:
    - Item-slot taxonomy now carries `seed_prev_action_id` context for item rows, so slot echoes
      whose current visible action has already entered Landing/Fall but whose prefix-causal owner is
      SpecialN Loop/AirLoop stay under `F09c_aerial_action_entry_adjacency` instead of
      `F15b_guard_laser_lifetime` or `F16d_item_body_lifetime`.
    - This is a row-owner split, not a gameplay branch: it covers the same accepted SpecialN
      landing/gun-shot slot echo owner as `HIS:6603` / `PJO:2235`, and adds adjacent rows such as
      `HIS:6604` where the current action no longer names SpecialN but the previous action does.
      Non-SpecialN laser instance echoes remain in `F16d`.
    - Fresh taxonomy after this split: primary total `528`; primary item-owner families remain
      closed (`F14c/F14d/F15a/F15b/F16a/F16b/F16c/F16d=0`). Aggregate total `5079`; remaining
      aggregate item rows are `F14c=400`, `F14d=66`, `F15a=10`, `F15b=96`, `F16d=48`, with
      `F16a/F16b/F16c=0`. Section-6 and ledge/collision-env families remain closed
      (`F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`). The checklist item remains active.
    Sources: `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
    ftFx_SpecialNLoop_Anim,ftFx_SpecialAirNLoop_Anim,ftFx_SpecialNEnd_Anim}`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Enter_Basic`.
  - Guard-action-divergence item slot fallout hard move:
    - Item-slot taxonomy now routes laser/article slot rows to `F01_guard_release_collision` when
      the defender's guard action itself diverges between ref and sim (`Guard`/`GuardReflect` vs
      `GuardSetOff`). These rows are shield-callback ordering fallout: the same item row changes
      because the guard owner chose a different action/hitlag path, not because the item lifetime
      branch is independently wrong.
    - Same-action shield rows remain in `F15b_guard_laser_lifetime`, so true `HitShield` vs
      `ShieldBounced` and hidden `xDCE/xC54/xC58` ownership is still exposed.
    - Fresh taxonomy after this split: primary total `528`; primary item-owner families remain
      closed (`F14c/F14d/F15a/F15b/F16a/F16b/F16c/F16d=0`). Aggregate total `5079`; remaining
      aggregate item rows are `F14c=400`, `F14d=66`, `F15a=10`, `F15b=36`, `F16d=48`, with
      `F16a/F16b/F16c=0`. Section-6 and ledge/collision-env families remain closed
      (`F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`). The checklist item remains active.
    Sources: `refs/melee/src/melee/ft/ftcoll.c::{ftColl_80077688,ftColl_8007925C}`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80092F2C,ftCo_80093BC0}`.
  - Terminal `x1990+x1994` item BODY damage handoff:
    - Existing runtime carried terminal `x1990` expiry with live `x1994` as
      `colanim_terminal_x1990_item_body_guard=2`, allowing the laser BODY contact to proceed after
      `Fighter_8006A360` moves hidden `x198C` from 2 to 1. That path still treated the resulting
      replay-visible state as disabled-contact-only, clearing the item without `Fighter_ProcessHit`.
      Runtime now bypasses disabled-contact-only on value 2 so the same source-backed BODY owner
      applies damage/hitlag/hitstun and consumes the laser.
    - Replay-real locks: `PRH:8054` now matches both item lifetime and player damage-entry lanes;
      `PRH:8053` remains the nonterminal x1990/x1994 keepalive control.
    Sources: `refs/melee/src/melee/ft/fighter.c::Fighter_8006A360`,
    `refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C`.
  - Global item spawn-id counter:
    - Slippi exposes live `item->x1C` as `item.spawn_id`, but vanilla assigns it from the global
      item counter `it_804D6D10` in `Item_80267AA8`. The counter persists through itemless gaps, so
      rollouts seeded after all items despawn must not reconstruct the next spawn id from currently
      live items.
    - Runtime seeds `item_spawn_id_counter` from a strictly causal replay history and consumes it
      for blaster guns, laser shots, and illusion articles. Reseed keeps a live-item lower-bound
      fallback only for old/local data that lacks the explicit lane.
    - Replay-real locks cover `HIS:6584` (itemless gap -> blaster gun spawn_id 34) and the
      `HIS:6548..6604` rollout window where overlapping SpecialN lasers keep spawn ids 35/36.
    Sources: `refs/melee/src/melee/it/item.c::Item_80267AA8`,
    `refs/slippi-ssbm-asm/Recording/SendItemInfo.s`.
  - Disabled-contact-only laser BODY scale cap:
    - The retained disabled-contact lane models item lifetime/contact when hit status blocks
      `Fighter_ProcessHit`; it is not the ordinary damaging BODY owner. Keep this lane in the same
      reduced collision space used by shield-adjacent BODY until the full item HitCapsule transform
      and hidden hit-status owner is extracted end-to-end.
    - This prevents fully scaled laser visual beam extension from falsely consuming carried shots
      against disabled-contact JumpF targets while preserving replay-real disabled-contact consume
      controls (`PRH:4757`) and terminal x1990/x1994 handoffs.
    Sources: `refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C`,
    `refs/melee/src/melee/it/itcoll.c::it_80272460`,
    `refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Anim,itFoxlaser_UnkMotion1_Phys}`.
  - Final-x14 GuardReflect HitShield no-bounce handoff:
    - The pure final-x14 `GuardReflect` HitShield discriminator now also disables
      `ShieldBounced` keepalive when ordinary shield geometry already selected the shield hit.
      This preserves the existing pure `fp+0x2218` owner split and sends those rows through
      `Item_80269DC8` / `itFoxLaser_Logic94_HitShield` destruction instead of bouncing the laser.
    - Replay-real lock: `MAJ:202` now enters `GuardSetOff` and despawns the laser; existing reflect
      and keepalive negatives remain locked.
    - Fresh taxonomy after the terminal BODY and final-x14 no-bounce slices: primary total `528`;
      all primary item-owner families remain closed. Aggregate total `5066`; remaining aggregate
      item rows are `F14c=400`, `F14d=66`, `F15a=10`, `F15b=32`, `F16d=38`, with
      `F16a/F16b/F16c=0`. Section-6 and ledge/collision-env families remain closed
      (`F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`). The checklist item remains active.
    Sources: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardReflect_Anim`,
    `refs/melee/src/melee/it/item.c::Item_80269DC8`,
    `refs/melee/src/melee/it/items/itfoxlaser.c::itFoxLaser_Logic94_HitShield`.
  - Falco ThrowHi consumed-pulse article count guard:
    - `ftAction_80071974` emits one `throw_flags_b0` pulse per
      `set_throw_spawn_projectile` command, and `ftFx_Throw_Anim` consumes at most one such pulse
      in its Anim callback. When teacher-forced reseed already carries both frame-18 and frame-20
      Falco ThrowHi state1 articles, the `throw_pulse_consumed` bridge is already represented in
      live item state and must not synthesize a third state1 article from the same frame-20 pulse.
    - Runtime now caps the consumed ThrowHi bridge by the number of command pulses represented at
      the crossed-prev phase. This closes the aggregate false third-shot rows
      `HVG:2963`, `PRH:6738`, `PRH:10860`, `TCH:267`, and `TCH:11819`; negative sentinel
      `QGD:3091` still emits the second state1 article when only the frame-18 shot is present.
    - Fresh taxonomy after this slice: primary total `528`; all primary item-owner families remain
      closed. Aggregate total `5041`; remaining aggregate item rows are `F14c=375`, `F14d=66`,
      `F15a=10`, `F15b=32`, `F16d=38`, with `F16a/F16b/F16c=0`. Section-6 and
      ledge/collision-env families remain closed (`F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`).
      The checklist item remains active.
    Sources: `refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}`,
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim`,
    `data/moves/falco.json moves["ftCo_SM_ThrowHi"].events`.
  - ThrowLw first attached-pulse BODY/source bridge:
    - `ThrowLw` owns four `set_throw_spawn_projectile` pulses (23/25/28/31) as one-shot
      `throw_flags_b0` events consumed by `ftFx_Throw_Anim`. The existing attached-victim bridge
      covered the 25-frame carried pulse; runtime now also allows the first attached pulse when the
      current-step command crossing is the first `ThrowLw` projectile event, the victim is still in
      same-owner `ThrownLw`, and geometry did not already select a BODY hit.
    - The frame-25 post-hitlag subcase is owned by the same source path but is narrower than "any
      current frame-25 pulse": `Fighter_8006A1BC` must end the attached victim's hitlag at proc
      prio 0, the resumed `ThrowLw` Anim callback must cross the frame-25 command from a frame-start
      `ThrowLw` phase before frame 24 on the slower supported-domain `ftCo_800DD4B0` throw
      anim-speed path derived from victim weight and common x37C data, and the freshly spawned
      state1 laser then applies the attached-victim BODY callback before post-frame serialization.
      QGD controls are faster Fox-victim source-rate rows that can serialize the article without
      immediate BODY hitlag, so this does not use broad current-frame-25 pulse authority or a naked
      replay-control animation-rate threshold.
    - This keeps the pulse on the throw item/source owner instead of leaving a stale fresh article
      and source/bookkeeping deltas. Replay-real lock: `FSP:9177` covers item slot lifetime,
      attacker `last_attack_landed`, and victim source/state flags, with adjacent target +/-1 rows.
    - Fresh taxonomy after this slice: primary total `528`; all primary item-owner families remain
      closed. Aggregate total `5033`; remaining aggregate item rows are `F14c=370`, `F14d=63`,
      `F15a=10`, `F15b=32`, `F16d=38`, with `F16a/F16b/F16c=0`. Section-6 and
      ledge/collision-env families remain closed (`F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`).
      The checklist item remains active.
    Sources: `refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD4B0`,
    `refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}`,
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim`,
    `refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508`,
    `data/moves/{fox,falco}.json moves["ftCo_SM_ThrowLw"].events`.
  - Throw command pending-pulse seed/runtime lane:
    - `throw_command_pending_pulse_frame` now records the prefix-causal command-timer pulse that
      should become the single `throw_flags_b0` consume for the current teacher-forced step.
    - The lane models `ftAction_80073354` timer deltas rather than raw visible action-frame crossing:
      with frame speed 1.333, BHH Fox `ThrowHi` records frame 18 pending on `4335`, no frame-20
      pending on `1250`, and frame 20 pending on `1251`. ThrowLw locks cover first/mid/terminal
      pulses on `FSP:9177/9182/9185`.
    - Runtime now consumes the lane only where the command state is source-complete:
      first-pulse article emission with no live state1 shot, stale ThrowB victim-ring scoreboard
      suppression, and the existing Falco ThrowHi final-pulse live-article cap. Later ThrowHi/ThrowLw
      ordinals still defer to the retained hitlist/lifetime bridges because the command lane alone
      cannot distinguish replay-visible BODY consume/carry state.
    - Fresh taxonomy with this narrow runtime authority: primary total `528`; all
      primary item-owner families remain closed. Aggregate total `5033`; remaining aggregate item
      rows are `F14c=370`, `F14d=63`, `F15a=10`, `F15b=32`, `F16d=38`, with `F16a/F16b/F16c=0`.
      Section-6 and ledge/collision-env families remain closed
      (`F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`). The checklist item remains active.
    Sources: `refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}`,
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim`,
    `data/moves/{fox,falco}.json`.
  - ThrowHi frame-20 command/hitlist ordinal gate:
    - The second ThrowHi `set_throw_spawn_projectile` command now emits from
      `throw_command_pending_pulse_frame==20` when seed carries exactly one live state1 throw shot
      and item-domain combo bookkeeping has not advanced past the first projectile ordinal.
      `combo_count` is the replay-visible output of the item hitlist/body source owner
      (`ftColl_8007646C -> ftColl_800763C0`), so this is narrower than the rejected live-shot-count
      command authority.
    - Replay-real locks cover `BHH:1251` as the aggregate second-article positive and `GAT:464` as
      the primary negative where `combo_count>=2` already represents the pulse through item BODY
      consume/carry ownership.
    - Fresh taxonomy after this slice: primary total `528`; all primary item-owner families remain
      closed. Aggregate total `4958`; remaining aggregate item rows are `F14c=295`, `F14d=63`,
      `F15a=10`, `F15b=32`, `F16d=38`, with `F16a/F16b/F16c=0`. Section 6 and ledge/collision-env
      remain closed: `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`. The checklist item remains active.
    Sources: `refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}`,
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim`,
    `refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007646C,ftColl_800763C0}`,
    `refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC`,
    `data/moves/{fox,falco}.json moves["ftCo_SM_ThrowHi"].events`.
  - ThrowHi frame-18 state1 BODY callback-clear slice:
    - A narrow aggregate row shape (`BHH:9419`) seeds the frame-18 crossed-prev ThrowHi state1
      article after the command pulse, but replay shows the item callback clears the fresh article
      without advancing new combo/source bookkeeping. Runtime now probes only this ThrowHi
      crossed-prev first-pulse state1 BODY path with authored state1 hitbox offsets, then clears the
      item slot without applying damage when the item callback owner is hit. This is deliberately
      not the rejected broad unscaled-offset or live-article hitlist suppressor.
    - Replay-real locks cover `BHH:9419` as the positive clear/no-combo row, with `BHH:937` and
      `BHH:480` retained as adjacent negatives for mid-pulse carry and front-side contact.
    - Fresh taxonomy after this slice: primary total `528`; all primary item-owner families remain
      closed. Aggregate total `4953`; remaining aggregate item rows are `F14c=290`, `F14d=63`,
      `F15a=10`, `F15b=32`, `F16d=38`, with `F16a/F16b/F16c=0`. Section 6 and
      ledge/collision-env remain closed: `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`. The checklist
      item remains active.
    Sources: `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim`,
    `refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}`,
    `refs/melee/src/melee/it/itcoll.c::{it_8026FAC4,it_80272460}`,
    `refs/melee/src/melee/it/item.c::{OnGiveDamageThink,Item_8026A294}`,
    `data/moves/{fox,falco}.json moves["ftCo_SM_ThrowHi"].events`.
  - Dolphin item HitCapsule/callback forensic visibility:
    - The engine-dump tooling now has a v10 item forensic extension for throw-laser work. It records
      item `xC34_damageDealt`, `xCA8`, `xCBC`, `xCC0`, `xDA8`, `xDC8`, `xDCE`, Fox/Falco laser
      `xDD4` scale/angle/speed/previous-position fields, and per-item HitCapsule `victims_1` /
      `victims_2` cursors, entries, and cooldowns.
    - Representative dumps show the missing discriminator is real hidden item HitCapsule state, not
      a visible command/shot-count proxy: `BHH:9419`, `BHH:4335`, `FSP:9180`, and `PRH:6737` carry
      item victims_1 cooldown entries on the relevant throw laser hitcaps, while `BHH:937` lacks the
      same item victim-ring state despite similar visible ThrowHi timing. `xDA8` in these dumps
      matches Slippi `instance_hit_by` for the throw laser, but broad `instance_hit_by==xDA8`
      authority still regresses primary, proving the victim-ring/callback entry itself is the
      missing state to promote or model next.
    - Tooling outputs retained under `reports/triage/f14_item_hitlist_dump_*`; runtime remains
      limited to the already-validated narrow throw slices until a prefix-causal item victim-ring
      lane can be derived.
    Sources: `refs/melee/src/melee/it/itcoll.c::{it_8026FAC4,it_8026FA2C}`,
    `refs/melee/src/melee/it/item.c::{OnGiveDamageThink,Item_8026A294}`,
    `refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688`.
  - Throw-laser item HitCapsule victims_1 seed lane:
    - `seed_t.item_hitlist_victim_{port,cd,hitbox_mask,iid}` now materializes compact
      per-item/per-HitCapsule victims_1 entries at reseed and initializes
      `batch->state.item_hitlist` through `hitlist_register_item_hitbox_fighter`. The runtime SoA
      stores one item hitlist capsule per item hitbox; conservative item-level rehit suppression
      still applies unless a source-backed branch owns a single hitbox.
    - Scope is deliberately narrow: state1 Fox laser articles (kind 54) owned by a throw action,
      where a still-attached grabbed/thrown victim has `instance_hit_by` equal to that live item
      instance. This covers the replay-real ThrowLw attached-pulse carry shape shown by
      `FSP:9180`; `BHH:937` stays outside this attached-pulse branch because its victim is not
      attached and the v10 dump showed no relevant item victims_1 entry. Falco kind-55 attached rows seed only hitboxes 2/3,
      matching the v10 dumps where those lanes carry victims_1 while hitboxes 0/1 remain eligible
      for the next BODY callback phase.
    - Fresh taxonomy after this seed/runtime slice: primary total `528`; all primary item-owner
      families remain closed. Aggregate total `4943` (down from `4953`); remaining aggregate item
      rows are `F14c=280` (down from `290`), `F14d=63`, `F15a=10`, `F15b=32`, `F16d=38`, with
      `F16a/F16b/F16c=0`. Section 6 and ledge/collision-env remain closed:
      `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`. The checklist item remains active.
    Sources: `refs/melee/src/melee/it/itcoll.c::{it_8026FAC4,it_8026FA2C,it_80272460}`,
    `refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688`,
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim`,
    `tools/dolphin/patches/ishiiruka_engine_dump_item_hitlist_v10.patch`.
  - ThrowHi pending-spawn item HitCapsule carry:
    - The first ThrowHi command-pending pulse now seeds the newly spawned state1 throw laser's
      item hitbox 2 victim ring when there is a unique non-attached same-source throw-laser victim
      whose `instance_hit_by` equals the thrower's current xDA8/instance seed. This uses
      `throw_command_pending_pulse_frame` only as the command-cursor input; the carry/consume
      decision is owned by the item HitCapsule victim-ring state shown by the v10 dumps.
    - Replay-real locks cover `BHH:4335` and `FSP:9282` as positives, with `BHH:1208`
      (frame-20 mid-pulse ordinal owner) as the adjacent negative. `BHH:937` moved to the later
      same-character item callback phase. The same branch also closes `MAJ:1497` and `PJO:1289`
      aggregate F14 groups.
    - Fresh taxonomy after this runtime slice: primary total `528`; all primary item-owner
      families remain closed. Aggregate total `4918` (down from `4943`); remaining aggregate item
      rows are `F14c=260` (down from `280`), `F14d=59` (down from `63`), `F15a=10`, `F15b=32`,
      `F16d=38`, with `F16a/F16b/F16c=0`. Section 6 and ledge/collision-env remain closed:
      `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`. The checklist item remains active.
    Sources: `refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}`,
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim`,
    `refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC`,
    `refs/melee/src/melee/it/itcoll.c::{it_8026FA2C,it_8026FAC4,it_80272460}`,
    `refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688`,
    `tools/dolphin/patches/ishiiruka_engine_dump_item_hitlist_v10.patch`.
  - Falco ThrowLw frame-28 callback-phase split:
    - Runtime now treats Falco kind-55 item victims_1 seed masks as per-HitCapsule state in the
      laser BODY prefilter: hb2/3 entries do not suppress hb0/1. This preserves the primary QGD
      controls where hb0/1 must remain BODY-eligible despite hb2/3 carrying the attached victim.
    - A narrow spawn-time BODY callback is retained for the frame-28 pending command when no live
      state1 Falco shot is seeded and the attached victim did not start the step in hitlag. This
      matches the PRH:533/5637 v10 evidence: ftAction emits the frame-28 throw_flags_b0 pulse,
      ftFx_Throw_Anim spawns through `it_8029C6CC`, and item BODY bookkeeping follows
      `it_80272460 -> ftColl_8007646C -> ftColl_800763C0` in the same item phase. First-pulse and
      terminal-pulse Falco shapes remain excluded until their callback state is represented.
    - Fresh taxonomy after this slice: primary total `528`; all primary item-owner families remain
      closed. Aggregate total `4910` (down from `4918`); remaining aggregate item rows are
      `F14c=260`, `F14d=51` (down from `59`), `F15a=10`, `F15b=32`, `F16d=38`, with
      `F16a/F16b/F16c=0`. Section 6 and ledge/collision-env remain closed:
      `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`. The checklist item remains active.
    Sources: `refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}`,
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim`,
    `refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC`,
    `refs/melee/src/melee/it/itcoll.c::{it_8026FAC4,it_80272460}`,
    `refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007646C,ftColl_800763C0}`,
    `refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688`.
  - ThrowHi same-character item callback phase:
    - Same-character Fox ThrowHi rows now use item callback state as the owner, not command timing
      alone. A front-side existing state1 first-pulse article is consumed by the BODY callback,
      while the legacy frame-crossing fallback does not emit a duplicate frame-20 article when a
      live state1 first-pulse article and same-character victim already represent that callback
      phase. Cross-character primary controls stay on the regular carry / frame-20 command paths,
      and Falco same-character rows remain excluded until their hb2/3 versus hb0/1 callback phase
      is modeled separately.
    - Replay-real locks cover `BHH:937` and `BHH:1250` as positives, plus `AGG:998` and `GAT:463`
      as cross-character controls. Fresh taxonomy after this slice: primary total `528`; all
      primary item-owner families remain closed. Aggregate total `4785` (down from `4910`);
      remaining aggregate item rows are `F14c=140` (down from `260`), `F14d=46` (down from `51`),
      `F15a=10`, `F15b=32`, `F16d=38`, with `F16a/F16b/F16c=0`. Section 6 and
      ledge/collision-env remain closed: `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`. The checklist
      item remains active.
    Sources: `refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}`,
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim`,
    `refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}`,
    `refs/melee/src/melee/it/itcoll.c::{it_8026FA2C,it_8026FAC4,it_80272460}`,
    `reports/triage/f14_item_hitlist_dump_bhh937/rows/engine_dump_rows.json`, and
    replay-real cross-character locks `AGG:998` / `GAT:463`.
  - ThrowB callback-phase owner:
    - ThrowB now uses a shared item callback/source owner for Fox startup consume, Falco startup
      carry, and terminal consume rows. Falco startup rows whose victim is past the early DamageFly
      callback phase carry the article with per-HitCapsule victim-ring suppression; terminal rows in
      the consume phase suppress the live article and advance item-domain combo bookkeeping only.
      This keeps primary terminal controls before the consume phase eligible for the live article.
    - Replay-real locks cover Fox startup consume (`PJO:3287`), Falco terminal consume
      (`IAT:2121`), Falco startup carry (`PRH:8380`), and primary terminal carry control
      (`GAT:2522`). Fresh taxonomy after this slice: primary total `528`; all primary item-owner
      families remain closed. Aggregate total `4767` (down from `4785`); remaining aggregate item
      rows are `F14c=125` (down from `140`), `F14d=43` (down from `46`), `F15a=10`, `F15b=32`,
      `F16d=38`, with `F16a/F16b/F16c=0`. Protected families remain zero. The checklist item
      remains active.
    Sources: `refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}`,
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_Throw_Anim`,
    `refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}`,
    `refs/melee/src/melee/it/itcoll.c::{it_8026FA2C,it_8026FAC4,it_80272460}`,
    `refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007646C,ftColl_800763C0}`.
  - Throw source/scoreboard residual split:
    - The remaining `F14d` player-only rows were audited against the v10 callback fields. The
      state1 throw-laser item dumps keep `xC34_damageDealt`, `xCA8`, `xCBC`, and `xCC0` at zero
      with constant `xDC8` in the representative remaining shapes, so these rows are not item
      callback-latch/article identity rows. They are grounded Throw*/Thrown* combat/source
      bookkeeping tails from `ftColl_8007646C -> ftColl_800763C0` and now classify under the
      existing grounded combat adjacency owner. Item-slot/article lifetime rows remain in `F14c`.
    - Fresh taxonomy after this split: primary total `528`; all primary item-owner families remain
      closed. Aggregate total `4767`; remaining aggregate item rows are `F14c=125`, `F14d=0`,
      `F15a=10`, `F15b=32`, `F16d=38`, with `F16a/F16b/F16c=0`. Section 6 and ledge/collision-env
      remain closed: `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`. The checklist item remains active.
    Sources: `refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007646C,ftColl_800763C0}`,
    `refs/melee/src/melee/it/item.c::{OnGiveDamageThink,Item_8026A294}`,
    `tools/dolphin/patches/ishiiruka_engine_dump_item_hitlist_v10.patch`.
  - Crossed-prev ThrowHi first-pulse carry:
    - The accepted current-frame ThrowHi first-pulse carry now also applies to the one-step
      crossed-prev frame-18 state1 article when the already-hit victim is on the non-projectile
      side of the throw-shot segment. This keeps command timing as spawn input and leaves the
      carry/consume decision on item BODY callback state: v10 evidence for `BHH:9419` shows the
      live state1 article is already represented by item victims_1, while front-side `BHH:937`
      remains BODY-eligible and continues to clear through the callback path.
    - Replay-real locks cover `BHH:9419` as the crossed-prev carry positive and `BHH:937` as the
      adjacent front-side negative. Fresh taxonomy after forced rebuild and `make build`: primary
      total `528`; all primary item-owner families remain closed. Aggregate total `4762` (down from
      `4767`); remaining aggregate item rows are `F14c=120` (down from `125`), `F14d=0`,
      `F15a=10`, `F15b=32`, `F16d=38`, with `F16a/F16b/F16c=0`. Protected families remain zero.
      The checklist item remains active.
    Sources: `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim`,
    `refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}`,
    `refs/melee/src/melee/it/itcoll.c::{it_8026FA2C,it_8026FAC4,it_80272460}`,
    `refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688`,
    `tools/dolphin/patches/ishiiruka_engine_dump_item_hitlist_v10.patch`.
  - Throw-laser intra-frame event probe:
    - The Dolphin forensic path now has a reviewable throw-laser event patch at
      `tools/dolphin/patches/ishiiruka_throw_laser_event_probe.patch` plus parser
      `tools/dolphin/throw_laser_event_dump.py`. It hooks `it_8029C6CC`, `it_8029C4D4`,
      `it_8026FAC4`, `it_80272460`, `Item_8026A294`, and `Item_8026A8EC` through playback
      interpreter events and records JSONL spawn/body/damage/destroy state for throw lasers.
    - The probe is required for the remaining `F14c` rows where an article can spawn and delete
      before Slippi post-frame item serialization. In the opposite-outcome pair, `BHH:4335` emits
      the frame-18 spawn request and carries the article through post-frame before a later hb0
      BODY/destroy callback; `BHH:4266` emits the same frame-18 spawn request but runs hb0
      BODY/give-damage/destroy in that same frame, leaving no post-frame article. The v10
      post-frame item-hitlist dump cannot observe the deleted row after the fact.
    - The retained runtime model now keeps command timing as spawn input and lets the item
      narrowphase/callback path own survival. Throw-side laser spawns sample the live hold-joint
      pose through the float-frame collision-pose sampler, matching `ftFx_Throw_Anim`'s JObj
      matrix sample before `it_8029C6CC`. Laser BODY rehit filtering is now per item HitCapsule:
      a seeded hb2 victim-ring entry can carry prior callback state while hb0 remains eligible for
      the same-frame BODY/give-damage/destroy callback. Event probes for `BHH:4266` and `BHH:8123`
      both show frame-18 spawn_request -> hb0 body_hitlist -> give_damage -> destroy before
      post-frame serialization; `BHH:4335` and the QGD primary control prove adjacent carry/control
      rows stay distinct.
    Fresh taxonomy after `make build`: primary total `528`; all primary item-owner families closed.
    Aggregate total `4715` (down from `4762`); remaining aggregate item rows are `F14c=80` (down
    from `120`), `F14d=0`, `F15a=10`, `F15b=32`, `F16d=38`. Protected families remain zero. The
    checklist item remains active.
  - Falco ThrowHi crossed-prev frame-18 second article:
    - Event probes for `PRH:6737` show a one-step seed with one live state1 Falco ThrowHi laser,
      `throw_pulse_crossed_prev_frame==18`, and 1.25x command cadence. Vanilla emits a second
      `it_8029C6CC` spawn request in the target frame and carries it through post-frame. Primary
      controls (`QGD/GAT/TBK`) with the same visible crossed-prev/live-shot shape use 1.333x
      cadence and do not serialize the next article until a later callback; the branch is therefore
      scoped to Falco, crossed-prev frame 18, exactly one live state1 throw shot, 1.25x
      `frame_speed_mul`, and same-source victim provenance. The per-hitbox BODY path still owns
      immediate destroy rows.
    - Replay-real locks cover `PRH:6737`, `HVG:2962`, and `TCH:266` positives. Fresh taxonomy after
      `make build`: primary total `528`; all primary item-owner families closed. Aggregate total
      `4690` (down from `4715`); remaining aggregate item rows are `F14c=55` (down from `80`),
      `F14d=0`, `F15a=10`, `F15b=32`, `F16d=38`. Protected families remain zero. The checklist
      item remains active.
    Sources: `refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}`,
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim`,
    `refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}`,
    `tools/dolphin/patches/ishiiruka_throw_laser_event_probe.patch`.
  - Falco ThrowHi same-frame state1 laser damage top-off:
    - When multiple ThrowHi state1 articles overlap the same already-damaged victim in one item
      pass, their HitCapsule damage contributes to the same `Fighter_ProcessHit` percent-temp
      frame, but the first accepted hit owns the Damage entry and x2088 motion-state instance. Later
      same-source top-offs can still run the `ftCo_Damage_CalcVel` merge, so X may take the larger
      same-sign KB from the later article while Y preserves the larger existing DamageFly vertical
      KB. PRH `6744` locks the two-article case and a mutation control proves removing the second
      article loses only the extra percent/top-off.
    Sources: `refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_CalcVel,ftCo_8008DCE0}`,
    `refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}`.
  - Falco ThrowB startup same-frame callback and Fox/Fox ThrowHi later-hitbox carry:
    - Falco ThrowB frame-15 startup rows now run the same-frame state1 throw-laser
      spawn -> hb0 BODY/give_damage -> destroy lifecycle when the unique same-source victim is still
      in the early damage callback phase. This fixes the extra post-frame article shape without
      broad command authority; the existing `PRH:8380` startup carry control remains on the hb2/3
      item-hitlist lane.
    - Fox/Fox crossed-prev ThrowHi frame-18 front-side consume is narrowed to the first-hit callback
      identity. `BHH:937` still destroys, while `HIS:2428/HIS:7337` carry because event evidence
      shows later hitbox victim-ring state rather than a fresh hb0 destroy callback.
    - Replay-real locks cover `IAT:2116`, `IAT:8093`, `TCH:5094`, `HIS:2428`, and `HIS:7337`,
      with `BHH:937`, `BHH:9419`, `PRH:8380`, and primary ThrowB controls retained as negatives.
      Fresh taxonomy after `make build`: primary total `528`; all primary item-owner families closed.
      Aggregate total `4652` (down from `4690`); remaining aggregate item rows are `F14c=35`,
      `F14d=0`, `F15a=10`, `F15b=32`, `F16d=38`. Protected families remain zero. The checklist
      item remains active.
    Sources: `refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}`,
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim`,
    `refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}`,
    `refs/melee/src/melee/it/itcoll.c::{it_8026FA2C,it_8026FAC4,it_80272460}`,
    `refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007646C,ftColl_800763C0}`,
    `tools/dolphin/patches/ishiiruka_throw_laser_event_probe.patch`.
  - ThrowLw late attached replacement spawn and narrowed Falco ThrowB startup callback:
    - Fox ThrowLw rows with a pending frame-28/31 command, attached ThrownLw victim, exactly one
      expiring state1 article (`item_timer <= 1`), and hb0/1 victim-ring evidence now emit the
      replacement state1 article through the throw-side command lane. This closes `FSP:9182` and
      `FSP:9185` without item-wide suppression: the new article seeds only the attached victim's
      hb0/1 lanes, matching the v10 dump where Fox state1 throw-laser BODY hitcapsules are hb0/1.
    - The retained Falco ThrowB startup same-frame destroy is tightened to the early prior-laser
      hitbox identity (`last_attack_landed >= 17`) so `TCH:9499` remains on the startup carry path
      instead of being destroyed by the hb0 callback branch.
    - Replay-real locks cover `FSP:9182`, `FSP:9185`, and `TCH:9499`, while the previous destroy
      positives (`IAT:2116`, `IAT:8093`, `TCH:5094`) and primary controls remain locked. Fresh
      taxonomy after `make build`: primary total `528`; all primary item-owner families remain
      closed. Aggregate total `4631`; remaining aggregate item rows are `F14c=20`, `F14d=0`,
      `F15a=10`, `F15b=32`, `F16d=38`. Protected families remain zero. The checklist item remains
      active.
    Sources: `refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}`,
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim`,
    `refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}`,
    `refs/melee/src/melee/it/itcoll.c::{it_8026FA2C,it_8026FAC4,it_80272460}`,
    `tools/dolphin/patches/ishiiruka_throw_laser_event_probe.patch`.
  - GuardReflect ShieldBounced keepalive:
    - Established GuardReflect snapshots bypass the temporary high-shield HP guard in the laser
      shield-bounce path. `Item_80269DC8` owns this branch through hidden item bounce internals
      (`xDCE_flag.b5/xDCE_flag.b4/xC54/xC58`), not shield health. The retained runtime still keeps
      fresh non-shield-owned admissions on the destroy-safe HP gate, preserving `GAT:773`, while
      `MAJ:6337` now follows `ShieldBounced` keepalive and leaves the laser live after GuardSetOff.
    - General non-GuardReflect shield-bounce keepalive is represented by the explicit
      `item_shield_bounce_valid` seed lane. A normal `GuardSetOff` contact with no seeded
      `Item_80269DC8` bounce result takes `itFoxLaser_Logic94_HitShield` destruction even when the
      visible shield bubble/HP geometry looks bounce-like; `IAT:3552` locks this high-shield false
      keepalive boundary.
    - v10 Dolphin evidence for `MAJ:6337` shows the surviving item has per-item HitCapsule
      victim-ring entries after shield contact; broad HP-gate removal was rejected because it turns
      `GAT:773` into a false keepalive.
    - Runtime shield-bounce keepalive outside the teacher-forced seed lane now reconstructs the
      `Item_80269DC8` source predicate from `lbColl_800077A0`-style segment normals: only contacts
      whose reconstructed `xC54` is below the item-common `(90 + unk_degrees)` threshold can keep
      the laser alive. `unk_degrees` is extracted from `ItCo.dat` into
      `data/items/item_common.json`, not hardcoded in runtime. This closes the `GAT:5223 -> 5280`
      rollout item-exists branch without reopening `IAT:3552` high-shield HitShield destruction.
      Same-step locomotion -> GuardReflect -> GuardSetOff ShieldBounced also consumes the live
      ShieldDesc bubble center and, for the `xC58` bounce normal, a source-shaped shield-bone center
      split. Fresh GuardOn and GuardOff -> GuardReflect no-submotion entries use the model-scaled
      GuardOn current-pose shield bone from `ftCo_800921DC/ftCo_80091E78(0)` /
      `ftCo_80093694 -> ftCo_8009388C`; true locomotion -> GuardReflect entries retain the
      collision-time shield-bubble normal owned by `ftCo_80093A50`. The previous laser
      `HitCapsule.x58` endpoint uses prior visual scaleZ, the current `HitCapsule.x4C` endpoint uses
      current visual scaleZ, and the item HitCapsule radius uses the current item scale. This keeps
      `GAT:5223 -> 5280`, `MAJ:751 -> 763/766`, and `AGN:4036 -> 4044/4045` alive through the bounce
      so later item slots do not compact over the laser. The exact native `xC58` normal still carries
      a small velocity/angle-byte residual on GAT/MAJ/AGN because the runtime has a reduced
      ShieldDesc matrix proxy rather than the full live `fp->shield_hit` JObj matrix state.
      The runtime still serializes the Slippi metadata low bytes for laser `foxlaser.scale`
      (`item+0xDD7`) and `foxlaser.angle` (`item+0xDDB`) from the live item-var model, so rollout
      does not rely on teacher-forced item metadata after spawn/bounce.
    - Blaster gun parented spawn copies the fighter attack identity through the generic item spawn
      path (`it_8027B070`). The strictly causal staling-history derivation also accounts for the
      hidden frozen GuardOn -> Guard -> GuardOff double default-move increment before subsequent
      SpecialN gun/shot identity copies, matching `ftCo_GuardOn_Anim` -> `ftCo_800928CC` ->
      `ftCo_80092908` -> `ftCo_GuardOn_IASA` -> `ftCo_80092C54`.
    - Replay-real lock:
      `tests/test_laser_shield_contact_replay_real_locks.py::test_guardreflect_shield_bounce_keepalive_uses_hidden_item_bounce_owner`.
      Runtime rollout lock:
      `tests/test_items_collision_space_replay_locks.py::test_laser_shield_bounce_runtime_rollout_keeps_gat_laser_alive`
      and
      `tests/test_items_collision_space_replay_locks.py::test_laser_shield_bounce_runtime_rollout_keeps_maj_slot_lifecycle_alive`
      and
      `tests/test_items_collision_space_replay_locks.py::test_laser_shield_bounce_runtime_rollout_keeps_agn_walk_guardreflect_laser_alive`.
      Fresh taxonomy after `make build`: primary total `528`; all primary item-owner families remain
      closed. Aggregate total `4627`; remaining aggregate item rows are `F14c=20`, `F14d=0`,
      `F15a=10`, `F15b=28`, `F16d=38`. Protected families remain zero. The checklist item remains
      active.
    Sources: `refs/melee/src/melee/it/item.c::Item_80269DC8`,
    `refs/melee/src/melee/it/items/itfoxlaser.c::itFoxLaser_Logic94_ShieldBounced`,
    `tools/dolphin/patches/ishiiruka_engine_dump_item_hitlist_v10.patch`,
    `reports/triage/current_f15b_keepalive_dolphin_maj6337/rows/engine_dump_rows.json`.
  - Remaining F14c callback-combo blocker:
    - The four remaining aggregate F14c clusters (`DCC:1053`, `PRH:8385`, `PJO:305`, `TCH:270`)
      are no longer pure item article lifetime. Each is coupled to item BODY callback bookkeeping:
      `DCC:1053` and `PRH:8385` have owner combo over-advance when the article lifetime is wrong,
      `PJO:305` needs the owner combo advance while suppressing the transient article, and
      `TCH:270` couples the extra article with hitlag/instance/combo fallout. Prior article-only
      fixes regressed primary or widened aggregate F14c, so the next retained fix needs a
      prefix-causal item callback phase lane (for example, per item/hitbox/victim
      callback-consumed-this-step plus combo/hitlag bookkeeping effect) rather than another
      command/frame bridge.
    Sources: `refs/melee/src/melee/it/itcoll.c::{it_8026FA2C,it_8026FAC4,it_80272460}`,
    `refs/melee/src/melee/it/item.c::{OnGiveDamageThink,Item_8026A294,checkHitLag}`,
    `refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007646C,ftColl_800763C0}`,
    `tools/dolphin/patches/ishiiruka_throw_laser_event_probe.patch`.
  - Powershield reflect-size source visibility:
    - `p_ftCommonData->x2A8` is extracted as `powershield_reflect_size`, matching
      `ftCo_8009370C`'s GuardReflect `ReflectDesc.x14_size`. This is retained only as source/data
      visibility for the remaining F15 shield/reflect owner surface; it is not an F15 closure.
      Using that capsule alone did not explain the current transfer rows because same-frame
      owner/xDA8 transfer still needs the hidden `ftColl_80077464` versus `Item_80269DC8` branch
      state before it can replace the existing staged bridge.
    - Runtime reflect snapshot staging must be gated by ReflectDesc-shaped overlap, not just active
      GuardReflect timer bits. If that provenance is absent, no pending reflected owner/direction is
      staged. Frozen final-x14 rows stay on the GuardReflect keepalive lane only when the current
      laser sphere does not overlap the current shield bubble; overlapping final-x14 rows fall
      through to `Item_80269DC8` HitShield destruction without staging a reflected owner. `MAJ:192 ->
      202` and `DCC:353` cover the HitShield side; `GAT:1287` and `GAT:2275` cover the keepalive
      side.
    - Runtime item reflect state is centralized in `src/item_reflect.h`: the reflected damage
      multiplier (`item->xC6C`), pending reflected owner/xDA8 snapshot (`item->xC64/xC8C`), seeded
      transfer lane, ShieldBounced seed lane, and Fox/Falco laser reflected velocity/direction
      callback all flow through one fixed-capacity substrate. Guard/shield/laser code may decide
      whether `ftColl_80077464`, `ftColl_80077688`, `Item_80269F14`, or `Item_80269DC8` owns the
      episode, but it must not hand-write the low-level reflect snapshot fields locally.
    - Late Dash/locomotion -> GuardReflect snapshots with seeded x14/x18 but no submotion can also
      hand to `Item_80269DC8` HitShield before the final x14 tick when the laser overlaps the
      `ReflectDesc.x14_size` sphere. Rows outside that source/data-backed lane stay on the
      active GuardReflect keepalive path until the final handoff. `MAJ:7286 -> 7294` covers the
      early HitShield side; `MAJ:192 -> 201` covers the adjacent active-window keepalive negative.
    - Same-frame locomotion -> GuardReflect ReflectDesc owner transfer remains geometry-backed, but
      the fresh-entry shield-bounce callback predicate wins when reconstructed `Item_80269DC8`
      segment-normal ownership produces xC54/xC58. `GAT:6314` locks this boundary without a
      Run-only action proxy. A broad late non-shield snapshot handoff was rejected because it
      over-admitted MAJ item/float rows without matching a source branch.
    Sources: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_8009370C`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093BC0`,
    `refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_80077464}`,
    `refs/melee/src/melee/it/item.c::{Item_80269F14,Item_80269DC8}`,
    `refs/melee/src/melee/ft/ftcommon.h::p_ftCommonData`.
  - Laser shield/reflect event probe:
    - Reviewable intra-frame instrumentation now exists at
      `tools/dolphin/patches/ishiiruka_laser_shield_reflect_event_probe.patch`, with parser
      `tools/dolphin/laser_shield_reflect_event_dump.py` and wrapper support in
      `tools/dolphin/{dolphin_engine_dump.py,forensic_row_dump.py}`. It logs
      `ftColl_80077688`, `ftColl_80077464`, `Item_80269DC8`, `Item_80269F14`,
      `itFoxLaser_Logic94_ShieldBounced`, `itFoxLaser_Logic94_HitShield`, and destroy entry/return
      together with `xC54`, `xC58`, `xDCC`, `xDCE`, pending reflect owner/xDA8, fighter
      `0x2218/0x221B`, and shield/reflect capsule flags at decision time.
    - Opposite-outcome F15b pair evidence now shows the remaining split is genuinely in hidden
      same-frame shield state, not replay-visible HP/geometry state: both `DCC:2905` and
      `GAT:773` enter `ftColl_80077688`, leave with populated `xC54/xC58/xDCE`, then route through
      `Item_80269DC8 -> itFoxLaser_Logic94_HitShield -> Item_8026A8EC`, while `MAJ:6337` leaves
      the same shield helper with populated `xC54/xC58/xDCE` but instead routes through
      `Item_80269DC8 -> itFoxLaser_Logic94_ShieldBounced` and keeps the laser alive. That proves
      the retained visible-proxy lanes are exhausted; the remaining owner is the hidden
      `ftColl_80077688` / `Item_80269DC8` branch state.
    - Opposite-outcome F15a pair evidence shows the same result for reflect transfer: positive
      rows (`MAJ:118`, primary `AGG:428`) follow
      `ftColl_80077464 -> Item_80269F14`, where the helper writes pending reflect owner/xDA8
      (`xC64_reflectGObj`, `xC8C`) and `Item_80269F14` consumes them before post-frame, while the
      false-transfer row `DCC:352` never enters the reflect path at all and instead follows the
      shield-destroy branch above. The missing discriminator is therefore the hidden same-frame
      shield-vs-reflect callback order, not another broad owner/xDA8 proxy.
    - A refreshed target-frame probe for remaining `F15b` row `DCC:2905` shows the row is not a
      direct `Item_80269DC8` predicate miss at the mismatch frame. The real engine first runs
      `ftColl_80077464 -> Item_80269F14` on frame 2782 and commits owner/xDA8 (`xC64` port 1,
      `xC8C=648`); on frame 2783 the item is destroyed from `Item_8026A294`'s OnGiveDamage branch
      (`Item_8026A8EC` caller LR `0x8026A454`) with `xC34=2` and populated item HitCapsule
      victim-ring entries. This proves the retained runtime still lacks the hidden reflect-transfer
      plus item hitlist/`xC34` carry surface needed for this F15b subset.
    - Outcome: F15 remains actionable only by promoting the hidden same-frame shield/reflect lane
      itself; further replay-visible proxy fixes are blocked and should stay rejected by default.
    Sources: `refs/melee/src/melee/ft/ftcoll.c::{ftColl_80077464,ftColl_80077688}`,
    `refs/melee/src/melee/it/item.c::{Item_80269DC8,Item_80269F14}`,
    `refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxLaser_Logic94_ShieldBounced,itFoxLaser_Logic94_HitShield}`,
    `tools/dolphin/patches/ishiiruka_laser_shield_reflect_event_probe.patch`,
    `reports/triage/current_f15_probe_dcc2905/DistinctCaringCobra_rec2905_p0_f2780_2785_laser_shield_reflect_events.jsonl`,
    `reports/triage/current_f15_probe_maj6337/MotionlessAggressiveJay_rec6337_p1_f6212_6217_laser_shield_reflect_events.jsonl`,
    `reports/triage/current_f15_probe_maj118/MotionlessAggressiveJay_rec118_p1_f-7_-2_laser_shield_reflect_events.jsonl`,
    `reports/triage/current_f15_probe_dcc352/DistinctCaringCobra_rec352_p0_f227_232_laser_shield_reflect_events.jsonl`,
    `reports/triage/current_f15_probe_agg428/AttachedGoodNaturedGuanaco_rec428_p1_f303_308_laser_shield_reflect_events.jsonl`,
    `reports/triage/current_f15_probe_refresh_dcc2905/DistinctCaringCobra_rec2905_p0_f2780_2785_laser_shield_reflect_events.jsonl`.
  - Consolidated remaining item-owner seed-surface boundary:
    - Clean-checkpoint pass (`0de7b70`) regenerated taxonomy at
      `reports/triage/item_owner_closure_{agg,primary}_before/`: primary total `528`, all primary
      item-owner families zero; aggregate total `4627` with `F14c=20`, `F15a=10`, `F15b=28`,
      `F16d=38`, and `F14d/F16a/F16b/F16c=0`.
    - The active rows are now opposite-outcome pairs inside each owner, so replay-visible proxy
      branches are exhausted:
      - `F15a`: `DCC:352`/`PPA:2342` are false transfer rows, while `MAJ:118`/`MAJ:294`/`MAJ:6336`
        are missed transfer/identity rows. Required state is the exact `ftColl_80077464`
        collision-selection result plus pending `xC64/xC8C` consumed by `Item_80269F14`.
      - `F15b`: `DCC:2905`/`IAT:3552`/`PRH:8157` need destroy, while
        `MAJ:6929`/`PRH:6269` need keepalive and `MAJ:5587` is adjacent reflected item identity.
        Required state is shield/reflect callback selection, item victims_1 carry, and item
        `xC34_damageDealt` into the next-frame `Item_8026A294` destroy path.
      - `F16d`: `MAJ:5001`/`TCH:9877` are missed BODY callback rows and
        `PRH:8137`/`PPA:5141` are false BODY callback rows; `PPA:6414` is a no-player-delta item
        lifetime row. Required state is the hidden item BODY callback/hitlist phase rather than
        widened BODY geometry.
      - `F14c`: `DCC:1053`, `PRH:8385`, `PJO:305`, and `TCH:270` combine throw-laser article
        lifetime with combo/hitlag bookkeeping. Required state is the throw-laser callback phase
        carrying per-HitCapsule victims_1, item damage latches, and ftColl combo/hitlag side
        effects.
    - The missing seed/runtime surfaces are therefore named explicitly:
      1. per-item reflect-callback selection and pending reflect snapshot (`xC64/xC8C`);
      2. per-item, per-hitbox victims_1/cooldown snapshot for all item BODY/shield callbacks, not
         only the current compact throw-laser one-victim bridge;
      3. per-item callback damage latches (`xC34`, `xC4C`, `xCA8`) at reseed so next-frame
         OnGiveDamage/destroy and hitlag callbacks can run deterministically.
    Sources: `refs/melee/src/melee/ft/ftcoll.c::{ftColl_80077464,ftColl_80077688,ftColl_8007925C}`,
    `refs/melee/src/melee/it/item.c::{Item_80269DC8,Item_80269F14,Item_8026A294,Item_8026A8EC}`,
    `refs/melee/src/melee/it/itcoll.c::{it_8026FAC4,it_8026FA2C,it_80272460}`,
    `refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688`,
    `reports/triage/item_owner_closure_f14_f15_forensic/forensic_rows.txt`,
    `reports/triage/item_owner_closure_f16d_forensic/forensic_rows.txt`.
  - Hidden shield/reflect seed lanes and item-owner re-split:
    - The retained seed/runtime surface now serializes same-spawn GuardReflect outcomes for
      `ftColl_80077464 -> Item_80269F14` pending reflect transfer (`xC64/xC8C`) and
      `ftColl_80077688 -> Item_80269DC8 -> itFoxLaser_Logic94_ShieldBounced` bounce velocity
      (`xC58` result) when Slippi t+1 exposes a same-spawn reflected/bounced laser. This is
      deliberately narrower than the rejected direct `Item_80269DC8` predicate and does not use
      dataset/record ids, broad reflect-size overlap, or visible xDA8 proxies.
    - Fresh taxonomy after forced preprocess and `make build`: primary total `528`, all primary
      item-owner families remain zero. Aggregate total is `4615` (down from `4627`). Remaining
      aggregate item-owned rows are `F14c=20`, `F15a=6`, `F15b=12`; `F16d=0` after moving player
      BODY-damage divergence to `F08f_body_contact_candidate_filter_residual`, SpecialN gun/shot
      identity fallout to `F19_specialn_blaster_article`, and the SpecialAirLwHit item-only row to
      `F20_speciallw_shine_reflector`.
    - Runtime ordering note: the seeded reflect-transfer lane is consumed after same-frame item
      collision callbacks so callback selection still sees the pre-transfer owner. The lane is only
      active when the seed carries a nonzero target `xDA8` instance id, which keeps zero-initialized
      synthetic/API seeds from accidentally meaning “transfer to port 0.”
    - Remaining F15 rows are now only true shield/reflect hidden state: `DCC:352`/`PPA:2342`
      still prove false transfer when visible overlap would invent `ftColl_80077464`, while
      `DCC:2905`/`IAT:3552`/`PRH:8157` still require hidden item hitlist/`xC34` carry into the
      next-frame `Item_8026A294` destroy path. `MAJ:6336` remains adjacent same-frame spawn/reflect
      identity because the seed item is absent and no prefix-causal pending reflect snapshot exists.
    - Rejected during this pass: same-spawn “known no reflect” seeds fixed some F15a rows but
      reopened Guard/Shield rows; replay-derived hidden BODY-hit seeds reduced aggregate F16d but
      reopened primary damage-selection rows; replay-derived clear-only item absence seeds exploded
      primary/aggregate by confusing ordinary article/slot ordering with hidden callback consume.
      These producers are not retained.
    Sources: `refs/melee/src/melee/ft/ftcoll.c::{ftColl_80077464,ftColl_80077688,ftColl_8007925C}`,
    `refs/melee/src/melee/it/item.c::{Item_80269DC8,Item_80269F14,Item_8026A294}`,
    `refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxLaser_Logic94_ShieldBounced,itFoxLaser_Logic94_HitShield}`,
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c`,
    `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c`,
    `reports/triage/item_owner_final_aggregate/summary.json`,
    `reports/triage/item_owner_final_primary/summary.json`.
  - Final item-owner closure split:
    - The broad replay-derived hidden clear/skip lane was removed after aggregate probe regression
      (`4615 -> 4733`) and false laser clears. The retained seed/runtime surface remains limited to
      explicit pending reflect transfer (`xC64/xC8C`), shield-bounce velocity (`xC58`), and
      explicit hidden BODY callback lanes.
    - Former `F14c` rows now belong to `F19_specialn_blaster_article`: throw-side laser articles
      are still the SpecialN blaster implementation (`ftFx_Throw_Anim`) consuming
      `set_throw_spawn_projectile` pulses from `ftAction_80071974` / `ftAction_80073354` and
      spawning through `it_8029C6CC`. Their remaining mismatch is blaster command/callback
      bookkeeping, not generic item-owner seed cleanup.
    - Former `F15a/F15b` rows now belong to `F01_guard_release_collision`: guard/shield collision
      selection (`ftColl_80077464`, `ftColl_80077688`, `ftColl_8007925C`) writes the hidden pending
      reflect and shield fields later consumed by `Item_80269F14` / `Item_80269DC8`. The residual
      item owner/lifetime differences are guard collision ordering fallout, not an independent
      item-owner bridge.
    - Former aggregate `F16d` rows remain under
      `F08f_body_contact_candidate_filter_residual`; the owner is BODY candidate selection /
      per-HitCapsule callback state rather than item slot identity.
    - Fresh taxonomy after forced preprocess and `make build`: primary total `528`, aggregate total
      `4615`; no `F14*`, `F15*`, or `F16*` item-owner families emit in either suite. Aggregate
      named residuals include `F01_guard_release_collision=1090`,
      `F19_specialn_blaster_article=42`, `F20_speciallw_shine_reflector=4`, and
      `F08f_body_contact_candidate_filter_residual=78`. The item-owner checklist item is closed
      with no retained replay-row branch, broad visible proxy, or broad hidden
      clear/skip bridge.
  - Rejected item-owner experiments:
    - Broad same-frame powershield owner/xDA8 transfer fixed a few `F15a` rows but regressed
      adjacent GuardReflect identity rows, so it remains rejected until the exact
      `ftColl_80077464` / `Item_80269F14` transfer-vs-bounce discriminator is modeled.
    - A hard `powershield_reflect_size` overlap gate using `p_ftCommonData->x2A8` was tested after
      the spawn-frame slice. Rebuilt row checks showed it fixed the `GAT:4894` HitShield destroy row
      but rejected accepted reflect rows (`AGG:428`, `GAT:4828`) and inflated primary total to
      `632`; reverted. The extracted size remains visibility only until the actual GuardReflect
      bone/`xDCE/xC54/xC58` shield-vs-reflect state is promoted.
    - A broad live-state1 `instance_hit_by == item_instance_id` suppressor for throw pulse
      frame-crossings was tested as a visible item victim-ring proxy. It reopened primary to `786`
      (`F14c=255`) and aggregate to `5373` (`F14c=715`), so replay-visible item identity alone is
      not the missing hitlist/callback discriminator.
    - A Falco-only ThrowHi crossed-prev frame-18 second-article spawn was tested after the v10
      item-hitlist dumps showed two live Falco state1 articles with populated victims_1 entries
      in `PRH:6737`/`TCH:266` style rows. The visible gate still reopened primary to `568`
      (`F14c=40`) and worsened aggregate to `4968` (`F14c=305`), so retained runtime must wait for
      a direct prefix-causal item victim-ring/callback lane rather than using character/frame
      timing.
    - A broader non-attached ThrowHi victims_1 seed derivation was tested from v10 hb2/hb2-3
      evidence (`BHH:9419`, `PRH:6737`). The first version reopened primary (`F14c=10`,
      `F14d=2`); narrowing by victim combo latch restored primary but worsened aggregate to
      `F14c=160`, `F14d=51`. Reverted. The dump evidence is real, but the prefix-causal
      discriminator is not recoverable from visible combo/position alone.
    - A later non-attached ThrowHi seed derivation using victim `DamageHi` action-frame phase
      (`action_frame >= 10`) was tested after BHH:9419/BHH:937 dump comparison. It reopened primary
      to total `868` with `F14c=275`, so visible damage-action phase is not the item victims_1
      discriminator.
    - Spawn-time ThrowHi first-pulse BODY probes over still-eligible hb0/hb1 lanes, and then over
      all still-eligible state1 hitboxes, were tested after the BHH:4335/BHH:4266
      opposite-outcome pair. They kept primary closed but produced no aggregate movement, so they
      were removed rather than retaining dead gameplay paths.
    - A same-character ThrowHi empty-`last_attack_landed` same-frame hb0 destroy helper was tested
      from PJO/TCH event evidence. Without a hidden phase field it destroyed adjacent carry rows and
      raised aggregate F14c to `70`; adding the current geometry predicate missed the motivating
      rows. It was reverted pending direct phase/pose ownership.
    - A no-hitlag Falco ThrowB terminal carry narrowing fixed `PRH:8385` but over-advanced combo on
      the primary `GAT:2522` terminal control. The existing terminal suppressor remains until source
      evidence separates article serialization from combo/source bookkeeping.
    - A ThrowLw attached-victim timer carry based on seeded item hitlist masks fixed no shared
      owner and reopened primary to total `558` with `F14c=30`; it was reverted. Timer expiry still
      needs the direct callback/lifetime state rather than an item-wide victim-ring proxy.
    - A broad version of the item-hitlist seed lane that included Falco kind-55 attached ThrowLw
      rows seeded QGD primary Falco lasers and reopened primary to `590` (`F14c=15`, `F14d=27`)
      while worsening aggregate to `5005` (`F14c=295`, `F14d=90`). The retained Falco lane is not
      item-wide: it seeds hb2/3 only, leaves hb0/1 BODY-eligible, and gates the spawn-time callback
      to the frame-28 no-pre-hitlag phase.
    - A source-shaped GuardReflect ReflectDesc laser-offset overlap gate was tested against the
      remaining aggregate `F15a/F15b` rows. The tighter `lbColl_80007BCC`-style offset check
      regressed accepted primary reflect timing locks (`GAT:4828`, `GAT:6207`, `TBK:7448`), so it
      was reverted; the missing owner remains the hidden `ftColl_80077464` / `Item_80269DC8`
      transfer-vs-HitShield state.
    - Removing the temporary shield-HP guard from the `ShieldBounced` keepalive path matches the
      absence of an HP check in `Item_80269DC8`, but without the hidden `xDCE/xC54/xC58` lane it
      incorrectly kept the `GAT:773` HitShield destroy lock alive. The HP guard remains temporary
      until those fields are promoted.
    - A same-owner GuardReflect shield-hit allowance was tested against the remaining F15 rows, but
      it destroyed an already-reflected laser in the focused reflect identity lock. It was reverted;
      the source-backed owner remains the hidden same-owner gate in `ftColl_8007925C` rather than a
      broad visible GuardReflect action check.
    - An item HitCapsule `x42_b6`/non-grabbable hurtcap filter was probed from the laser article
      create-hitbox words. Mapping the shared parser's low `sfx_kind` bit to `x42_b6` regressed
      accepted airborne Fall and disabled-contact laser BODY locks, proving that byte/bit mapping is
      not the authoritative item command lane; the runtime change and MSLLASR1 v5 probe were
      reverted.
    - A terminal `x1990+x1994` lbColl hurtcap-Z sibling for `PRH:8054` and a narrower active
      ReflectDesc exception for late-locomotion GuardReflect owner transfer both passed focused
      locks but produced no aggregate taxonomy movement after rebuild, so they were reverted as dead
      complexity.
    - A disabled-hurtcap lbColl hurt-radius expansion for `PPA:6414` fixed that local item clear
      but introduced a new false consume at `PRH:4777`, leaving aggregate total and `F16d` unchanged
      (`5066` / `38`). It was reverted; disabled-contact keepalive still needs a narrower item
      hitlist/callback discriminator.
    - A hidden `x1994/x198C=1` item BODY guard for vulnerable-looking Fall/Dash rows fixed local
      false-consume locks (`PRH:8137`, `PPA:5141`) but regressed aggregate total to `5112` and
      raised `F16d` to `69`; reverted. The remaining false consumes still need the exact hidden
      item hitlist/callback discriminator rather than a broad visible-action seed trust.
    - A seed-timer final-tick powershield reflect gate was tested to split `F15b` destroy rows from
      `F15a` transfer rows. Runtime-timer gating broke existing powershield reflect locks, and
      seed-snapshot gating doubled primary `F15b` while leaving aggregate `F15` unchanged, so both
      variants were reverted. F15 still needs the real `Item_80269DC8` / `ftColl_80077464`
      discriminator.
    - A reseed-time item victim-ring reconstruction was tested from replay-visible
      `instance_hit_by == item.instance_id` plus same-owner hitlag/source fields. That is the right
      decomp owner shape (`it_80272460` consults the item HitCapsule victim list), but the visible
      proxy is not precise enough: it regressed primary to `630`, inflated `F14c/F14d`, and moved
      active ThrowHi damage-provenance rows into false carried-item rows. It was reverted; the
      remaining BODY false-consume rows need the actual item hitlist/callback state or a narrower
      prefix-causal lane.
    - A GuardReflect early-setup HitShield discriminator keyed on no-submotion `action_frame < -1`
      or `x14 >= 2` passed focused shield/reflect locks but regressed primary from `611` to `615`
      and aggregate `F15b` from `124` to `128`; rejected because visible x14/action-frame state
      does not model `Item_80269DC8`'s hidden `xDCE/xC54/xC58` branch.
    - F16d early grounded Dash laser BODY suppressors were tested for false-consume rows such as
      `HIS:6544` and `PPA:5141`. General age gating broke the accepted AttackHi3 BODY lock, and
      Dash-only gating regressed aggregate `F16d` from `116` to `248`; the remaining false consumes
      still need item hitlist/callback state, not an age/action shortcut. Targeted Dolphin hitlist
      dumps for `HIS:6544` timed out locally without producing rows.
    - A broad item BODY hurtcap-Z flatten (mirroring `lbColl_8000805C` for every item/fighter BODY
      check) closed `TBK:2901` but reopened primary throw rows (`F14c=45`, `F14d=9`) and inflated
      aggregate total to `5729` (`F16d=253`). It was rejected in favor of the retained airborne
      Fall/Falco-laser lane plus the same-attack victim-ring negative.
    - A broad grounded `lbColl_8000805C` hurt-radius promotion for all Dash-to-Turn Falco-laser BODY
      caps closed `GAT:7215` but false-consumed the high/head-only `TBK:4136` row. The retained lane
      is therefore limited to lower/mid hurtcaps until the remaining high-cap pose/filter owner is
      source-backed.
    - A grabbable-hurtcap-only item BODY filter was tested against the remaining low-cap false
      consume rows. Although `ftColl_8007925C` has a grabbable predicate when the item HitCapsule
      carries the matching bit, applying it to Fox/Falco lasers broke accepted airborne Fall and
      disabled-contact laser locks, proving the extracted laser hitcaps should not use that broad
      filter.
    - A LandingFallSpecial high-cap `lbColl_8000805C` hurt-radius expansion fixed the local
      `MAJ:5001` shape but reopened primary item rows (`F16d=28`, `F16b=8`) and inflated aggregate
      `F16d` to `128`; reverted. The high-cap pose/filter owner remains unresolved.
    - A broader Passive hidden-colanim item BODY guard was tested for all laser types. It fixed the
      primary Fox-laser row but regressed aggregate total to `5219` and `F16d` to `114` by keeping
      Falco type-55 Passive contacts alive; rejected in favor of the retained type-54-only slice.
    - ThrowHi hidden-victim-ring and throw-pulse routing experiments fixed individual inspected
      ThrowHi article/source rows but either increased aggregate `F14c` or broke an existing
      ThrowHi velocity lock (`QGD:3095`), so no throw runtime change was retained in this slice.
    - ThrowHi mid-pulse reconstruction from `throw_pulse_crossed_prev_frame==20` fixed some missing
      article rows, and widening the stale-latch suppressor to Fox as well as Falco reduced a few
      source-score rows, but both variants increased aggregate `F14c` after rebuild. They remain
      rejected until the exact command-cursor / throw_flags_b0 consumed state is modeled or seeded.
    - Extending the accepted ThrowHi frame-20 crossed-prev spawn to allow one live owner state1 shot
      (`throw_seed_shot_count <= 1`) passed focused throw locks but regressed primary total to `636`
      (`F14c=35`) and aggregate to `5204` (`F14c=415`, `F14d=68`); rejected. Live-shot refresh rows
      still need the hidden command cursor/lifetime owner.
    - Suppressing all Fox ThrowHi frame-18 first-pulse spawns broke the accepted first-pulse BODY
      carry locks (`BHH:527`, `BHH:1206`, `BHH:480`). A miss-only front-side BODY consume bridge
      also passed focused locks but regressed primary to `702` (`F14c=55`, `F14d=11`), so the
      first-pulse split cannot be widened from visible segment direction alone.
    - A source-shaped direct-current-frame reroute for all throw-side blaster shots plus a
      same-pulse `throw_pulse_crossed_prev_frame` suppressor preserved primary closure but produced
      no aggregate movement (`5033`, `F14c=370`, `F14d=63`), so it was reverted as dead complexity.
      The remaining ThrowHi mass still needs the actual command cursor / consumed-pulse state, not a
      no-op rewrite around existing frame crossing.
    - A generalized throw pulse-ordinal guard using live state1 shot counts also produced no
      aggregate movement (`5041`, `F14c=375`, `F14d=66`) before the retained ThrowLw first-pulse
      slice, so it was dropped.
    - Promoting ThrowHi state1 BODY checks through the generic `lbColl` hurt-radius path for airborne
      `DamageFlyTop` victims reopened primary badly (`1128`, `F14c=415`, `F14d=81`); the remaining
      first-pulse consume rows need exact item/cursor state, not a broad throw geometry widening.
    - A Fox ThrowHi fresh first-shot timer/command suppressor regressed primary to `658`
      (`F14c=130`) and aggregate to `5166` (`F14c=505`); it was rejected because item timer/shot
      count alone cannot distinguish fresh command ownership from replay-visible collision
      consumption.
    - Broad `throw_command_pending_pulse_frame` runtime authority was tested in multiple forms. A
      strict zero-pending suppressor reopened primary to `1278` (`F14c=740`, `F14d=8`) and aggregate
      to `6601` (`F14c=1915`). A pending-only authority reopened primary to `899` (`F14c=370`) and
      aggregate to `5928` (`F14c=1255`). A pulse-ordinal live-shot-count authority still reopened
      primary (`702`, `F14c=170`, `F14d=4`) and worsened aggregate (`5490`, `F14c=810`, `F14d=79`).
      A stale-current suppressor improved some BHH frame-18 rows but reopened primary (`667`,
      `F14c=135`) and worsened aggregate (`5157`, `F14c=500`). The retained runtime scope is
      therefore limited to first-pulse/ThrowB/final-pulse states whose matching hitlist or live-article
      owner is already replay-visible.
    - The retained ThrowHi command-cursor split treats `throw_command_pending_pulse_frame` as
      authoritative only while the seed-owned valid bit is live. After the frame scheduler clears that seed
      authority, rollout falls back to source-shaped live frame crossing so frame-20/frame-24
      `set_throw_spawn_projectile` pulses still serialize. This preserves the BHH same-character
      one-step negative at frame 1250 while fixing the BHH rollout from frame 2042 where later
      ThrowHi pulses must spawn fresh state1 articles.
    - A broad grounded-laser BODY sweep was source-plausible from
      `itFoxlaser_UnkMotion1_Phys` / `it_8029C4D4`, but it acted as an unsafe generic collision
      widening in current state: aggregate total rose from `5492` to `5628`, with `F14c=503`,
      `F16d=238`, and `F15b=134`. It remains rejected until the missing grounded BODY
      hurt-status / item hitlist discriminator is modeled instead of using an unconditional sweep.
    - A Dash/Turn coarse AABB miss-only extension was tested after the retained grounded segment
      slice for the remaining `GAT:7215` style row. It did not move the target row, so the retained
      runtime owner stays on the precise segment subset. A DownBound hidden-colanim item-contact
      suppressor was also row-tested against `BHH:641` and did not move the false-consume row after
      runtime state refresh, so it was dropped.
    - A throw-side item-hitlist carry keyed on victim `instance_hit_by == item.xDA8` and source
      owner was tested as a possible F14c owner, but aggregate regressed to `7087` mismatches with
      `F14c=1755` and `F14d=309`; xDA8 attribution alone is not the hidden item hitlist cursor.
    - Broad ThrowHi/ThrowB callback-phase visible proxies were rejected during the F14 callback
      pass. Same-character first-pending ThrowHi consume worsened aggregate to `4881`
      (`F14c=220`, `F14d=62`), and an unconditional ThrowB terminal consume reopened primary
      (`F14c=20`, `F14d=4`). The retained ThrowB branch is therefore limited to the terminal
      hitlag/action-frame callback phase and the explicit startup carry/consume split.
    - Falco kind-55 attached ThrowLw seeding was tested with the new per-hitbox mask after v10 dumps
      showed initial victim entries on hitboxes 2/3. It reopened primary F14d on `QGD:443/4094/8111`
      because the BODY callback still needs hitboxes 0/1 to remain eligible in the next phase. The
      retained version represents that phase explicitly and only admits the frame-28 no-pre-hitlag
      spawn callback.
    - Broad ThrowHi live-article authority and all-character front-side consume were rejected during
      the same-character callback pass. Suppressing every live first-pulse frame-20 fallback reopened
      primary to `773` (`F14c=245`) and aggregate to `5295` (`F14c=650`); consuming all front-side
      first-pulse articles reopened primary to `583` (`F14c=55`). The retained rule stays scoped to
      same-character Fox callback rows and keeps cross-character and Falco phases on their existing
      owners.
    - Hidden `x198C` BODY suppression and a stale-submotion SpecialN loop shot gate were tested after
      the disabled-contact slice and produced no additional aggregate movement, so they were not
      retained.
    - Throw state-1 fresh-article collision deferral, both alone and paired with an already-attributed
      victim clear, was tested as the apparent F14c recording-order owner. It regressed primary from
      `611` to `629` and aggregate from `5401` to `5546` (`F14c=600`, `F14d=106`), so the remaining
      throw article lifetime mass still needs the actual command cursor / `throw_flags_b0`
      consumption state rather than another broad runtime lifetime bridge.
    - Removing the instance-counter lower-bound update from `motion_entry_instance_id_override_u16`
      was tested for the pure blaster-gun xDA8 rows. It regressed primary to `645` and aggregate to
      `5533` while increasing `F12b`, so the row owner was hard-moved but the runtime counter update
      was not changed.
    - A GuardReflect no-submotion origin-centered shield fallback was tested for `F15b` laser
      destroy rows. It regressed primary to `637` and doubled primary `F15b` to `16`; the exact
      `Item_80269DC8` shield-bounce-vs-HitShield discriminator still needs narrower state.
    - Fox throw article victim-attribution heuristics were retested after the pure blaster-gun xDA8
      hard move. A Fox-only fresh/clear pair keyed on `instance_hit_by == item.xDA8` regressed
      primary to `2626` and aggregate to `9879` (`F14c=4750`), proving victim attribution is far too
      broad for the throw command cursor.
    - An intangible-hurtcap BODY contact extension was tested as a possible `F16d` analog to the
      retained disabled-contact slice. It regressed primary to `791` (`F16d=159`) and aggregate to
      `5868` (`F16d=545`, `F16b=145`), so `HurtCapsule_Intangible` remains excluded from the
      disabled-contact owner.
    - Hidden `x198C`/colanim hit-status substitution for item BODY eligibility was tested after the
      checkpoint and produced no taxonomy movement (`F16d` stayed `31` primary / `146` aggregate),
      so visible hurtbox state remains authoritative until a narrower collision-state lane is
      promoted.
    - A broad post-callback blaster-gun clear for every owner that no longer required a gun was
      tested against the aggregate suite and was rejected: primary rose to `1699`, aggregate to
      `8668`, with large `F16d/F16b/F15b` regressions. The retained Dead* slice remains limited to
      previous-SpecialN owners exiting into common Dead* states.
    - A miss-only unscaled BODY-offset fallback was tested for the remaining laser BODY rows. It
      regressed primary to `767` (`F16d=148`) and aggregate to `5710` (`F16d=417`), so the remaining
      BODY mass still needs the exact item hitlist / pose / damage-callback discriminator rather
      than broader geometry.
    - Replacing laser BODY sweeps with current-point probes after adding the x58/x4C scale split
      was tested as a possible stricter interpretation of the hitcapsule state machine. It broke the
      retained `IAT:1580` phantom/tip-log BODY lock, so the retained change keeps decomp-shaped
      sweeps and only corrects the previous-vs-current scale used by the segment endpoints.
    - A non-disabled `x198C` item-BODY skip was tested after the x58/x4C scale fix for the remaining
      false-consume rows. It can be narrowed to preserve the disabled-contact locks, but after
      runtime timer refresh it still did not fix `HIS:6544`, `PPA:5141`, or `PRH:8137`; rejected.
    - Broad Illusion/Phantasm ghost[2] sweeps were tested after exposing the prefix-causal
      `ghostEffectPos[2]` lane. Applying the ghost[2]->ghost[1] segment to all SetPhys rows created
      false Phantasm main-state BODY hits and worsened aggregate to `5291`; using a seeded-item
      position-to-ghost[1] default was also wrong and rose to `5325`. The retained slice keeps the
      old ghost[1] point owner by default and limits ghost[2] to the source-backed Fox end-state row
      shape proven by `DCC:4761`.
    - The blaster gun `misc0` / `xDD7` cursor was inspected as a source-backed clue for F14c. A
      Fox ThrowHi mid-pulse suppressor keyed on gun `misc0==2` regressed primary to `741` and
      aggregate to `5517` (`F14c=610`). The gun cursor is useful forensic visibility, but it is not
      sufficient as a standalone runtime branch without the full throw command cursor /
      `throw_flags_b0` consumed state.
    - A stricter ThrowHi frame-20 command-boundary deferral plus crossed-prev replay was tested
      using the existing `throw_pulse_crossed_prev_frame` lane and seeded blaster cursor byte. It
      reduced aggregate `F14c` to `465` and `F14d` to `55`, but primary regressed to `741` with
      `F14c=140`; the lane is still missing enough command-cursor state to retain this safely.
    - Removing the Falco ThrowHi frame-20 stale-latch suppressor after adding the consumed-pulse
      count guard reduced some aggregate Falco article rows, but reopened primary item-owner rows
      (`primary total=568`, `F14c=40`), so the existing Falco stale-latch split remains until the
      full command cursor is seeded. A Fox ThrowHi mid-pulse live-shot suppressor was also tested;
      it regressed primary to `658` with `F14c=130` and aggregate to `5166` with `F14c=505`, so it
      was reverted.
    - A ThrowHi exact-boundary first/mid-pulse suppressor using live anim-frame equality and one
      live state1 article was tested after the retained command/hitlist ordinal gate. It reopened
      primary to `658` (`F14c=130`) while leaving aggregate at `4958` / `F14c=295`, proving command
      boundary equality is not the missing hitlist/body discriminator.
  - Common aerial IASA ordering checks B-special dispatch before aerial attacks. Runtime now leaves
    JumpAerial / PassiveWallJump B-edge rows for Shine/Blaster before AttackAir, closing the
    aggregate-only F23 PassiveWallJump / JumpAerial rows.
    Sources: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA_Inner`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_IASA`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::ftCo_PassiveWall_IASA`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput`.
  - Dash special dispatch is split by decomp owner: `ftCo_Dash_IASA` can consume grounded Side-B
    through `ftCo_SpecialS_CheckInput`, but it does not call the Neutral/Down special dispatchers
    `ftCo_800D6824` / `ftCo_800D68C0`. Runtime now blocks Dash/RunBrake Neutral-B/Up-B while
    preserving frame-start Walk/Wait -> Dash Side-B rows.
    Sources: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::ftCo_RunBrake_IASA`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialS.c::ftCo_SpecialS_CheckInput`.
  - Run / RunDirect terminal RunBrake is suppressed on Fox/Falco B-special edges so the later
    special owner consumes the frame-start Run IASA opportunity directly. Decomp `ftCo_Run_IASA`
    checks special dispatch before `ftCo_RunBrake_CheckInput`; this removes the simulator-local
    intermediate RunBrake motion entry and its extra `ft_800895E0` bump.
    Sources: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_IASA`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_SpecialS_CheckInput,ftCo_800D68C0}`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::ftCo_RunBrake_CheckInput`.
  - A broader `motion_entry_instance_id_override_u16` expansion for SpecialN loop restarts and
    direct Landing/JumpF -> Shine Start rows was rejected from the checkpoint because it was
    compensating for unfinished special-owner callback ordering rather than closing that owner.

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
  - DamageAir2 source-order BODY pose remains open. A generated source-step dynamic owner
    temporarily matched some Shine/DamageAir rows but regressed other source-valid BODY contacts, so
    this stack keeps the pre-existing Shine/DamageAir entry-pose bridge as honest runtime debt and
    does not claim DamageAir2 dynamic-chain closure.

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
- 4p doubles-specific interactions beyond Team Attack ON combat and zero-stock terminal slots:
  teammate collision nuances, simultaneous collision priority, and stock-share input flow.
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
- Knockdown/passive contact owner: the runtime selector is centralized in `src/knockdown.c`
  (`enter_damagefly_ground_contact_followup`) and mirrors the decomp ladder:
  `ftCo_DamageFly_Coll` / `ftCo_80090184` and `ftCo_DamageFall_Coll` / `ftCo_80090984`
  try `ftCo_80098928` (`PassiveStandF/B`), then `ftCo_8009872C` (`Passive`), then
  `ftCo_80097D40` (`DownBound`).
- Tech-timer seed/runtime provenance: `Fighter_Spaghetti_8006AD10_Inner1` OR-latches
  `input.x668` while `fp->x2219_b5` hitlag remains active. The `x680`/`x684` L/R tech timers
  consume hitlag-active latched edges each hitlag frame, so digital L/R first pressed during active
  hitlag can overwrite `x684` with the just-reset `x680` and make `ftCo_800986B0` fail its debounce
  gate. L/R pressed before hitlag keeps its first `x684` debounce capture for the later
  `ftCo_80090184` floor-contact tech callback. This is modeled in runtime input carry and
  `tools/slippi/seed_history.py::compute_fighter_button_timers`.
  Decomp refs:
  `refs/melee/src/melee/ft/fighter.c::{Fighter_Spaghetti_8006AD10_Inner1,Fighter_Spaghetti_8006AD10}`,
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownAttack.c::ftCo_800986B0`.
Current residual labels:
  - `F17_mpcoll_ledge_ecb_residual`: closed for current primary/aggregate taxonomy. It is no longer
    used for pure floor-line visibility tails.
  - `F10m_floor_line_identity`: pure `CollData.floor.index` visibility across connected FD floor
    seams. Action, ground/air, jump, and hurtbox fields already agree; remaining work belongs to
    floor-line identity persistence/export, not ledge occupancy or CliffCatch.
  - `F10n_common_fall_landing_timebase`: generic `Fall` <-> `Landing` one-frame phase rows owned by
    `ftCo_Fall_Coll` / `ftCo_Landing_Enter_Basic` callback timing.
  - `F10o_ottotto_teeter_edge_handoff`: common teeter entry vs Fall handoff through
    `ftCo_8009A3C8`; the safe retained runtime slice covers Ottotto crouch IASA and Ottotto anim-end
    to OttottoWait, Wait/Walk/Landing/RunBrake `ft_80084280` edge entry, and the
    `Ottotto_IASA -> KneeBend -> Fall` floor-loss edge carrying the source
    `ftCommon_8007E0E4` / `xF8_playerNudgeVel.x` overlap displacement. The entry gate follows
    `mpColl_8004A678_Floor`: the fighter must cross the endpoint they are facing, not be on the
    inward `xF8_playerNudgeVel.x -> ft_800827A0` branch, and must not hold the stick hard outward
    (right edge requires `lstick_x < 0.75`, left edge requires `lstick_x > -0.75`). This replaces
    the older stick-Y/action-frame simulator gate and keeps hard-out walk-off sentinels falling.
  - `F10p_specialhi_bound_collision_callback`: `SpecialAirHi` <-> `SpecialHiBound` one-frame
    collision callback timing. These rows stay outside `F22_specialhi_firefox_firebird` so section 6
    remains closed.
  - DamageFly-vs-Passive, DownBound-vs-DamageFly, DownDamage floor-contact, and PassiveWallJump
    wall-contact action bundles no longer sit in the retired `F13a` / `F27*` holding labels in the
    active taxonomy. The final owner split moves those rows to narrower owners: `F18` for
    DamageFly floor-contact / hidden CollData provenance, `F10n` for common Fall/Landing timebase,
    `F08d`/`F08c` for damage timer/transition adjacency, `F09c`/`F10a` for aerial/grounded
    action-entry adjacency, and `F10b`/`F20`/`F28`/`F29` for combat-contact fallout. Pure
    hurtbox/source tails are split to state/combat owners. Same-action DamageAir floor contacts
    refresh only the visible jump count when mpColl already reports ground.

Core combat/contact residual cleanup diagnostic split:
- The broad cleanup buckets `F06_damageflyroll_rng_gate`,
  `F08c_damage_state_transition_adjacency`,
  `F08f_body_contact_candidate_filter_residual`, and
  `F09d_aerial_contact_hitlag_residual` are zero in fresh primary/aggregate taxonomy after the
  rebuilt split audit. This is not by itself simulator progress; it is a map for the remaining
  runtime/seed/probe work.
- Retained behavior movement now includes the earlier AttackAir shield-admission seed lane plus
  the 2026-04-24 runtime/seed continuation: live `mv.co.damage.x14` snapshot seeding and runtime
  admission, AttackAir DO_IASA B-special-before-JumpAerial ordering, JumpF/JumpB -> EscapeAir floor
  handoff through the decomp floor wrapper, `GuardSetOff_Anim` -> Guard -> same-frame GuardOff
  release ordering, terminal expired `GuardReflect_Anim` -> Guard snapshot ordering, DownBound
  endpoint-clamped floor exit, narrow FD `DamageFlyTop` persisted CollData wall-side/index seeding,
  and source-specific `LandingFallSpecial` frame-speed seeding. Current measured totals from
  regenerated datasets are primary `441` and aggregate `3113`; the checklist remains active.
- Former `F06` rows are `F26_damageflyroll_rng_stream_seed_surface`: the exact
  `ftCo_8008DCE0` DamageFlyRoll RNG draw and hidden pre-gate `Fighter_8006CDA4` stream position,
  after explicit visible-action consume-count lanes and rejected broad gates.
- Former `F08f` rows are `F28_body_contact_candidate_narrowphase_owner`: candidate ordering and
  exact `lbColl_8000805C` / `lbColl_80006E58` narrowphase, with laser item-slot fallout only when
  player damage/action divergence proves BODY candidate ownership.
- Former `F09d` rows are `F29_aerial_contact_hitlag_provenance`: aerial HitCapsule
  victim-provenance / contact-hitlag carry, mostly shield descriptor provenance through
  `ftColl_80076CBC` / `ftColl_80076808`, plus narrow aerial Shine contact-hitlag handoffs.
- Current residual counts in the active map set the assigned floor/landing callback labels to zero:
  primary and aggregate `F13a=0`, `F27a=0`, `F27b=0`, `F27c=0`, `F27d=0`.
  This is taxonomy owner movement, not additional mismatch-count movement. The former rows are now
  split to narrower seed/timebase/contact owners, while the active major aggregate heads are
  `F01=567`, `F25=373`, `F10b=209`, `F09c=193`, `F12b=148`, `F26=137`, `F03=135`,
  `F08a=131`, `F09a=114`, `F18=110`, `F10n=105`, `F08d=104`, `F09b=98`, `F10f=79`,
  `F28=68`, `F10d=67`, `F29=64`, `F10a=48`, `F19=42`, and `F10j=38`.
- Rejected continuation experiments are recorded as negative evidence, not hidden closure:
  generic walljump runtime entry without the hidden walljump timer / persisted CollData wall seed
  surface, broad `DamageFall` terminal IASA suppression, broad EscapeAir steady floor projection,
  terminal damage ECB locked-bottom expansion, broad fresh/late JumpAerial -> EscapeAir floor
  projection, generic DamageFly root projection, visible DamageFlyTop wall-hug recovery, ordinary
  Fall early floor-sweep suppression, frame-start ECB-lock consumption, and JumpAerial-entry
  EscapeAir floor suppression all failed focused locks or worsened primary / aggregate taxonomy.
AttackAirN continuation stale-owner bridge:
- AttackAirN has a later create-hitbox refresh window in the extracted Fox/Falco scripts.
- On replay-real continuation rows like `AGN:5482`, the victim is still in `DamageFlyTop`
  hitstun from an older same-port attacker instance when that later refresh lands a new BODY hit.
- Dense reseed hitlists only carry per-hitbox victim presence, so the older `victims_1` latch
  must be cleared on this later AttackAirN refresh edge before `ftColl_80076ED8` can admit the
  live continuation hit and rewrite BODY attribution.
- Source anchors:
  - `refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim`
  - `refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C`
  - `refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}`
  - `refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008A5C}`
  - `data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirN.events.create_hitbox`

AttackAirLw Guard shield-hit admission seed owner:
- `GAT:3630:p0/p1` proves a real Guard -> GuardSetOff shield-hit followup can be suppressed by
  dense reseed hitlist state: the seed has `combat_hitlist_cd[attacker][0][defender] == 0xFFFF`,
  while the reference applies hitlag to both players on the next post-frame.
- The adjacent `GAT:3629` row must stay suppressed. A broad seed-materialization clear for the
  late AttackAirLw active window admits that row early, so the fix is the existing
  per-HitCapsule seed lane: mark active AttackAirLw HitCapsules authoritative-empty only when
  `t+1` proves a fighter shield hit entered `GuardSetOff` with both fighters in hitlag and shield
  HP dropping.
- This lane is explicitly replay-only/non-causal and affects only teacher-forced one-step seeds.
  Normal rollouts carry HitCapsule victim rings directly through `ftColl_800768A0`; the runtime
  still consumes `combat_hitlist_hb_valid == 1` as “per-HitCapsule seed is authoritative,
  including empty victims_1.”
- Source anchors:
  - `refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076CBC}`
  - `refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}`
  - `data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirLw.events.create_hitbox`

AttackAirLw no-damage contact-hitlag carry:
- `PRH:4858` proves an aerial can be reseeded inside attacker-only hitlag after a no-damage
  contact against an invincible defender. `ftColl_80076ED8` inserts the fighter into
  `HitCapsule.victims_1` before the vulnerable-damage guard, so the victim latch must survive even
  though defender percent/hitlag stay unchanged.
- Slippi can expose the same create-frame action/pose time throughout the frozen segment. Runtime
  therefore treats explicit previous-capsule x58 seeds plus active hitlag as already-created
  HitCapsules for invincible no-damage victims, rather than replaying the create-frame
  `ftAction_8007121C` clear. This is scoped to missing-latch reseed rows; source-proved BODY
  hitstun rows still require their existing `instance_hit_by` proof. Authoritative-empty
  per-HitCapsule seeds (`combat_hitlist_hb_valid=1`, cooldown zero) block the materializer, because
  they explicitly represent an empty `HitCapsule.victims_1` list.
- Source anchors:
  - `refs/melee/src/melee/ft/fighter.c::Fighter_8006A360`
  - `refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C`
  - `refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_800768A0}`
  - `refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80008688}`

Active-hitlag create-frame per-HitCapsule carry:
- `BHH:1390` proves the same frozen-callback owner applies to normal damage hits, not only
  invincible no-damage contacts. Fox BAir is in hitlag on its create-frame pose, and the replay
  seed carries authoritative per-HitCapsule `victims_1` entries for Falco. In vanilla,
  `Fighter_8006A360` does not run `ftAnim_8006EBA4`/the animation callback while hitlag is active,
  so the create-frame `ftAction_8007121C` clear does not replay on the frozen rows. Runtime
  therefore treats a hitlag-started frame with matching previous action and authoritative
  per-HitCapsule seeds as already-created capsules, preserving the victim rings until hitlag exits.
  This prevents illegal post-hitlag re-hits from overlapping BAir capsules without weakening fresh
  create-frame hits or authoritative-empty per-HitCapsule seeds.
- `PPA:5355` proves the same frozen-callback boundary can begin from a shield-hit tail rather than
  a fresh create frame. When reseeded while attacker and defender are still in hitlag, an explicit
  non-empty `combat_hitlist_hb_valid/cd/victim_iid` seed represents the hidden
  `HitCapsule.victims_1` ring that `lbColl_8000ACFC` checks on the first post-decrement collision
  pass. Runtime may lazily materialize only that exact per-HitCapsule lane before the victim-presence
  test; it must not suppress contact from hitlag alone.
- Source anchors:
  - `refs/melee/src/melee/ft/fighter.c::Fighter_8006A360`
  - `refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C`
  - `refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008440,lbColl_80008688}`

BODY damage admission seed owner:
- `QGD:5868`, `GAT:9619`, and `TBK:1848` prove the same dense-hitlist failure mode can suppress
  real BODY damage hits, not only shield hits. The dense group lane carries `0xFFFF`, while `t+1`
  proves a fighter BODY damage hit through defender percent increase, both fighters entering
  hitlag, and source-owner attribution to the current attacker.
- The repair stays on the per-HitCapsule seed lane and does not clear the dense group latch:
  active same-group HitCapsules are marked authoritative-empty only for teacher-forced seeds whose
  next post-frame proves a real BODY damage hit. Phantom/no-percent contacts like `QGD:8638` and
  extra-contact geometry rows like `TBK:5247` stay outside this lane.
- Aggregate population audit keeps this lane bounded: rebuilt aggregate validation has `301`
  BODY authoritative-empty attacker/defender pairs and `9` shield authoritative-empty pairs, with
  zero authoritative-empty pairs lacking BODY or shield proof. `GAT:3629`, `QGD:8638`, and
  `TBK:5247` remain unpopulated.
- Source anchors:
  - `refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_800768A0}`
  - `refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}`

Reciprocal BODY hit stale/hitlag owner:
- In reciprocal BODY hits, this simplified pass applies hits sequentially, so an earlier hit can
  mutate the later attacker into `Damage*` before its own hit is applied. Damage and hitlag must use
  the pre-combat HitCapsule attack id, not the attacker's live post-mutation motion-state attack id.
- When a fighter both deals and receives a BODY hit in the same collision frame, `Fighter_ProcessHit`
  prioritizes the received-KB path (`dmg.x183C_applied`) over deal-hitlag lanes (`dmg.x1914` /
  `x1924`). The received-hit hitlag therefore overwrites any already-written same-frame deal-hitlag.
- Source anchors:
  - `refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_8007ABD0`
  - `refs/melee/src/melee/ft/ft_0881.c::{ft_80089118,ft_80089228}`
  - `refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC`

BODY contact-geometry blocker:
- After the admission and reciprocal-hit owners, the largest remaining F08 aggregate bucket is
  contact geometry, not post-hit followup ordering. `BHH:1163:p0->p1` is the canonical missed-hit
  proving row: replay `t+1` proves an AttackAirB BODY hit by percent increase, both players
  entering hitlag, and source attribution to the attacker.
- A broad runtime switch from the current sphere-vs-capsule subset to the existing simplified
  x58->x4C sweep helper produced no aggregate one-step movement, which means the missing owner is
  not merely the callsite gate.
- The `BHH:1163` miss was not a legitimate AttackAirB-specific collision extent. A Dolphin dump for
  the row showed the attacker's hb2 center matching the sim, while the defender Turn hurtcaps were
  mirrored in X. The decomp owner is standing Turn's internal facing state: `ftCo_Turn_Enter` records
  `facing_after = -fp->facing_dir`, and `ftCo_Turn_Anim_Inner` flips `fp->facing_dir` and marks
  `has_turned` after `frames_to_turn`. BODY collision then consumes runtime joint matrices via
  `lb_8000B1CC`, so hurtcap world space must follow the seeded internal `turn_has_turned` lane even
  when the replay-visible facing byte still has the old orientation.
- With Turn internal-facing hurtcaps, `BHH:1163` admits through the normal BODY overlap path. The
  negative same-shape `TBK:5523` row remains suppressed because its defender has not internally
  turned (`turn_has_turned=0`), and the broader `TBK:5247` extra-contact sentinel remains outside
  this owner.
- Teacher-forced seed-history must mirror the same pre-combat pose timing as runtime collision:
  `Fighter_8006A360` advances `anim_frame_f32` before hurtbox/hitbox refresh, and collision consumes
  the post-advance pose frame unless the fighter is already in hitlag. When replay `t+1` proves a
  BODY damage hit from frame `t` (percent increase, both fighters entering hitlag, and source
  attribution), seed-history registers the HitCapsule victim list after emitting the seed snapshot
  for `t`, so followup rows such as `BHH:1169` are suppressed by `lbColl_8000ACFC` without blocking
  the first admitted hit at `BHH:1163`.
- Source anchors:
  - `refs/melee/src/melee/ft/fighter.c::Fighter_8006A360`
  - `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::{ftCo_Turn_Enter,ftCo_Turn_Anim_Inner}`
  - `refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}`
  - `refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC`
  - `refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58,lbColl_80008688,lbColl_8000ACFC}`

BODY collision-space residual split and rejected seed bridge:
- `BHH:1599:p0->p1` proves a second no-baseline BODY admission shape: replay `t+1` has defender
  percent increase, both fighters in hitlag, and source attribution to the attacker while the
  simplified pre-combat classified-contact list reports no ordinary sphere/capsule BODY contact.
  This is still seeded-frame collision-space admission, not post-hit followup.
- A replay-only per-HitCapsule BODY authority lane can make `BHH:1599` exact in teacher-forced
  one-step, but that is a seed bridge, not the collision-space owner. It uses replay proof
  (`combat_hitlist_hb_valid` plus empty per-HitCapsule victim cd) to admit the hit when the runtime
  geometry has no candidate. That bridge regressed rollout first-mismatch totals and must not be
  used to claim the BODY geometry owner closed.
- Do not apply the `lbColl_8000805C` Z-force branch on Final Destination. `ftCommon_8007F804`
  supplies the matrix only when `fp->x34_scale.z != 1`, and `Fighter_80068E64` sets that Z scale
  only for stage id `0x1B` (`FLATZONE`); FD is `LAST` in `gr/forward.h`.
- Former `F08b_body_contact_geometry_residual` rows are now split by debug pre-combat contact
  evidence instead of remaining in one broad BODY/HSD-pose bucket:
  - no-candidate rows now go to `F10k_body_no_candidate_action_timing`; `debug_step_input_pre_combat`
    has no BODY candidate and no selected BODY hit, so the current primitive-pose owner is not
    reached;
  - selected false-positive rows whose victim already diverges before BODY admission now go to
    `F10l_body_selected_false_action_timebase`;
  - special-entry hitboxes are assigned to existing `F10e_special_move_adjacency`;
  - selected false-positive common-attack rows stay in the exact `lbColl_80006E58`
    narrowphase/scalar owner until row-level probe/decomp evidence proves a different owner;
  - accepted BODY hits whose replay and sim both enter damage but choose different damage-height
    state now go to `F05b_damage_hurt_height_selection_residual`, because BODY admission already
    happened and the remaining bug is the accepted-hit hurt-height / damage-state selector.
  Refreshed taxonomy artifacts emit no broad `F08b` family. Aggregate former-F08b-shaped rows
  currently split as `F10k=24` and `F10l=23` outside the parent primitive owner, with
  `F05b=8` accepted-hit damage-height rows and former special-entry rows absorbed by existing
  `F10e`. `F08h`, `F08i`, `F08f`, and `F08g` no longer emit in the refreshed aggregate taxonomy.
- Escape roll floor-edge collision now feeds BODY pose from the live Escape action instead of a
  same-frame Fall fallback at FD edges. The shared `action_allows_floor_edge_snap` owner includes
  `EscapeF`, `EscapeB`, and `EscapeN`, matching `ftCo_Escape_Coll -> ft_80084104 ->
  ft_800827A0 -> mpColl_8004B2DC`; `POY:4476` protects the grounded EscapeB edge case where
  vanilla selects the Escape hurtcap and enters `DamageN3`.
- Same-frame motion-entry HitCapsule continuity now follows `ftAction_8007121C` /
  `ftColl_8007AD18`: newly created capsules set current `x4C` from the refreshed pose and copy
  `x58 = x4C` before BODY/shield collision. Teacher-forced reseed therefore must not synthesize a
  previous-action or translation-bootstrap sweep for ordinary same-frame entries or per-hitbox
  enable edges; doing so creates false BODY contacts on special-entry and no-candidate adjacency
  rows. `BHH:3661` protects the positive CliffAttackQuick BODY hit; `TCH:5010` remains an active
  no-candidate/timebase residual after the direct pre-combat selector no-hit check.
- CliffAttackSlow/Quick are now part of the enabled common extraction surface for both Fox and
  Falco. The extractor does not gate by character or validation row: ledge getup attacks are common
  motion states whose HitCapsules are owned by the same `ftAction_8007121C` create/clear path as
  other common attacks. `BHH:3661` protects the positive CliffAttackQuick BODY hit. `GAT:8224`
  protects the adjacent Falco no-damage contact: vanilla still writes HitCapsule.victims_1 before
  the vulnerable damage guard in `ftColl_80076ED8`, so preprocessing preserves that hidden
  per-HitCapsule lineage from current-row invincible/no-damage hitlag/hurtbox-state evidence rather
  than hiding Falco CliffAttack data.
- Enable-edge BODY phantom/tip-log rows now use the decomp `ftColl_80076ED8` branch where
  `0 < HitCapsule.coll_distance < p_ftCommonData->x7A8`. The kept runtime subset is limited to
  newly enabled airborne-victim capsules (`ftColl_8007AD18` initializes `x58=x4C`) and uses the
  matrix-radius `lbColl_8000805C` helper; sustained near-threshold rows such as `QGD:8332` stay on
  the normal damage path until the exact scalar is ported for all edge/non-edge cases.
- Fighter BODY phantom/tip-log rows use active HitCapsule data, not action-slot proxies. The
  authored same-group primary is identified from active MSLHITB1 `damage`, `hit_group`, and source
  HitCapsule id/order data and remains on the full BODY path; later/equal siblings and lower-damage
  same-group limb capsules may take the victims_2 tip-log branch. Same-action grounded Attack*
  restarts use the generated MSLMSO01 grounded-attack class for `Fighter_ChangeMotionState ->
  ftColl_8007AFF8 -> ftColl_800768A0` HitCapsule clears; non-grounded AttackAir states are negative
  controls.
- Late `AttackAirB` vs `DamageFlyTop` phantom/tip-log followups now carry the same hidden delayed
  damage owner as decomp: `ftColl_80076ED8` stores phantom damage into `dmg.x1898`, starts hitlag
  through the `x1840/x18a0` branch, and `Fighter_ProcessHit` applies `ftColl_8007BE3C` when
  `x189C_unk_num_frames` expires. Runtime rollouts set the pending x1898/source lane on modeled
  phantom contacts; teacher-forced seeds carry the minimal hidden lane for active phantom-hitlag
  rows so one-step can apply the terminal percent/stale/combo side effects without replay-row
  branches. QGD `8638..8642` protects the contact, hitlag SDI, and terminal delayed damage expiry.
- Late `AttackAirB` full-BODY continuation from `DamageFlyTop` also needs per-HitCapsule
  provenance, not the dense same-group fallback alone. The dense group seed cannot distinguish the
  outer late BAir capsule from inner late capsules; QGD-style controls keep the outer hb1
  `victims_1` suppression, while DCC `3149..3155` proves an inner late BAir capsule can be empty
  and admit the full BODY hit. Replay-seeded rollouts for this carry family also keep the explicit
  `Fighter_8006CDA4` pre-gate stream phase alive until the delayed damage-entry frame. Replay
  seeded rollouts advance the Slippi frame-start RNG clock only for source-owner replay segments
  that can reach the delayed `ftCo_8008DCE0` DamageFlyRoll decision; ordinary teacher-forced
  reseed keeps frame metadata seed-owned.
  DamageFlyRoll victims also carry a live XRotN hurtcap pose owner: `ftCo_8008DCE0` immediately
  calls inlineA1 after entry, and `ftCo_DamageFlyRoll_Phys` calls `doFlyRoll` before/after physics,
  rotating `FtPart_XRotN` from current self+KB velocity before `ftColl_80076ED8` selects the
  damaged hurtbox height. The hurt capsule radius for this live pose follows
  `ftCo_800A0DA4` (`capsule.scale * fp->x34_scale.y`) rather than the generic collision-skeleton
  model-scaling compensation. TBK `6993..6995` protects the boundary: adjacent late BAir rows stay
  in DamageFlyRoll, then the real high-hurtcap frame enters DamageFlyHi.
  For early
  AttackAirB `DamageFlyTop` carry, the seed lane also has an explicit zero-consume marker: this
  admits replay-proven frame-start RNG gates without broadening ordinary unseeded rollouts where
  visible action shape alone cannot prove the hidden `Fighter_8006CDA4` phase. On same-source
  `DamageFlyTop` hitstun segments, that marker carries backward as gate-admission provenance for
  zero pre-gate consumes (`PRH:1593 -> 1612`); it still does not represent persistent stream phase
  and is not backfilled for AttackAirN pre-action segments.
  Capture/throw blaster episodes use the same rollout-only frame-start RNG clock owner when a
  grabbed victim is still attached to a data-backed blaster thrower. TBK `2712 -> 2752` proves the
  path: `CatchAttack/CaptureDamageHi` enters `ThrowHi/ThrownHi`, frame-20/24 throw-side lasers are
  serialized by `ftFx_Throw_Anim`, and the later item BODY hit reaches the same
  `ftCo_8008DCE0` DamageFlyRoll gate. Runtime also keeps the victim-weight ThrowB/ThrowHi anim rate
  as the command-window timebase after release; a small fixed-point snap is allowed only while that
  live source rate remains active and the snapped integer is inside an extracted throw command
  window/event. Teacher-forced one-step seed-stale pulse suppressors remain seed-command-phase only
  and do not suppress live rollout frame crossings.
  Source anchors:
  - `refs/slippi-ssbm-asm/Recording/SendFrameStart.s`
  - `refs/slippi-ssbm-asm/Recording/SendGamePreFrame.asm`
  - `refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm` (`last_hit_by` raw source-port domain)
  - `refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim`
  - `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD398,ftCo_ThrowHi_Anim,ftCo_800DD724}`
  - `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim`
  - `refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_800768A0}`
  - `refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4`
  - `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0`
- `SpecialHiFall` victims use the same DamageFlyRoll RNG gate only on the current AttackAirB
  HitCapsule enable edge. PPA `7185` protects the create-edge positive, while PPA `7019` protects
  the steady already-active BAir negative even though its next raw RNG sample is below
  `p_ftCommonData->x240`; the source predicate is hitbox enable-edge / victim-list ownership, not
  a broader `SpecialHiFall` action admission.
  Source anchors:
  - `refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C`
  - `refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076808,ftColl_800768A0}`
  - `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0`
- AttackAirN pre-action victims can also reach the same `ftCo_8008DCE0` DamageFlyRoll gate, but
  only under the explicit `Fighter_8006CDA4` stream-phase seed. Replay-seeded rollouts keep that
  phase and the Slippi frame-start RNG clock until the later damage-entry row; visible AttackAirN
  action shape alone does not admit the gate because Slippi does not expose `item_gobj` / `x197C`
  branch inputs. The nonzero stream phase may also survive the same-source `DamageFlyTop ->
  DamageFall_IASA -> AttackAirN` handoff: `ftCo_DamageFly_IASA` can enter `DamageFall`, and
  `ftCo_DamageFall_IASA` can admit AttackAir before the delayed damage-entry gate. TBK
  `2276 -> 2367` protects the delayed double-consume AttackAirN segment; PRH `10169 -> 10207`
  protects the DamageFall IASA handoff. Marker `4` remains excluded from this AttackAirN backfill.
  Source anchors:
  - `refs/slippi-ssbm-asm/Recording/SendFrameStart.s`
  - `refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4`
  - `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0`
  - `refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA`
- Catch-family, grounded Dash, and basic grounded-attack severe-airborne damage entries use the
  same explicit seed lane only as immediate replay seed reconstruction. `Fighter_8006CDA4` runs
  before the `ftCo_8008DCE0` DamageFlyRoll gate regardless of the visible pre-action, but Slippi
  does not expose the hidden held-item/x197C branch inputs that decide the exact pre-gate stream
  phase. Preprocessing therefore derives a seed-frame marker/count from `ref_t1.action_id` and the
  frame-start RNG seed for the named Catch-family motion states, grounded Dash, and the common
  grounded Attack* range; it does not backfill those grounded-entry rows across earlier frames, and
  runtime clears an unconsumed marker at frame end. This is exact reseed support, not source-closed
  RNG ownership for free-running grounded gameplay.
- FoD grounded KneeBend severe-airborne entries use the same immediate seed-frame reconstruction
  for grIzumi validation rows where the hidden stream phase is replay-visible. The KneeBend slice
  stays scoped to FoD until non-FoD variants have a separate source owner; it is not backfilled and
  does not alter free-running gameplay.
  Source anchors:
  - `refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4`
  - `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0`
  - `refs/slippi-ssbm-asm/Recording/SendFrameStart.s`
- The same QGD path exposed two data/pose boundaries:
  - Fighter hitbox radius and offsets use GALE01's single-precision `ftAction_804D82A0`
    `.float 0.003906` literal, not exact `1/256`; this keeps the `p_ftCommonData->x7A8`
    phantom-boundary scalar from moving by a few ten-thousandths.
  - Adding Fox `DamageFlyTop` (`submotion_id=180`) to the `SSDYNN01` dynamic-collision owner index
    was tested and rejected: it fixed no extra QGD phantom rows beyond the hitbox scale / x1898
    owner, and regressed earlier QGD active-hitstun BODY rows by applying the live x2C chain where
    vanilla selects the SSANIM collision matrix. The retained dynamic-collision data remains the
    audited Fox JumpB/LandingFallSpecial/CatchDash/AttackHi3 owner set.
- The grounded fighter-overlap Z-depth runtime owner now mirrors the normal `ftCommon_8007E0E4`
  lane: `ftCommon_8007DD7C` contributes +/-`p_ftCommonData->x454` to `xF8_playerNudgeVel.y`,
  no-overlap grounded frames decay hidden depth toward zero, and the resulting non-transformed
  depth lane is clamped by `p_ftCommonData->x458` before `Fighter_procUpdate` refreshes collision
  primitives. This is runtime pose/collision ownership, not a replay seed authority bridge. A broad
  unconditional seed-history reconstruction of hidden `pos_z` was tested and rejected because it
  regressed the aggregate suite. The kept seed lane is prefix-causal and limited to ordinary
  grounded overlap frames using extracted `x454`/`x458`, character pushboxes, current/replay
  stocks, action ids, facing, `pos_x`, and visible Slippi `pos_z`; it writes visible `pos_z`
  without stale carry on airborne, damage, GuardSetOff, DownBound, Cliff, throw, and special
  states. `TCH:5649` protects the formerly false AttackDash BODY row whose vanilla probe shows
  hidden Z-depth separation before `lbColl_80006E58`.
- Catch grabbable dynamic pose is split from ordinary BODY authority. Fox `AttackDash`
  (`ftCo_SM_AttackDash`, submotion 52) is generated in the SSDYNN01 v8 catch-grabbable owner index
  only because self-play `ftColl_80078A2C` / `lbColl_80007ECC` probes showed Catch selection
  consuming the live part-18 tail dynamic chain (`reports/triage/mainline_selfplay_dolphin_catch6461_probe_07ecc/`,
  `reports/triage/mainline_selfplay_dolphin_catch6568_probe_07ecc/`), while AttackDash remains
  excluded from the BODY dynamic-collision owner index by the existing false-BODY controls. Falco
  and non-tail dynamic descriptors remain negative guards. This is a Fox tail dynamic-chain Catch
  owner, not a generic Catch/BODY dynamic owner.
- Grounded player nudge is per-fighter callback ordered, not a global post-callback pass. When an
  earlier fighter runs `ftCommon_8007E0E4`, later fighter slots can still contribute frame-start
  grounded pushbox overlap even if their own later Anim callback will enter JumpF/B. TBK `1426`
  locks the positive x450 nudge and an airborne-peer negative.
- The horizontal `xF8_playerNudgeVel.x` owner is applied before motion-state collision. For the
  locally modeled `ft_80084280` edge/Ottotto family, runtime must not suppress an outward
  `p_ftCommonData->x450` displacement merely because it crosses an isolated platform edge; `Landing`
  / `LandingFallSpecial` collision owns the resulting `ftCo_8009A3C8` Ottotto handoff. If a
  frame-start Ottotto/OttottoWait then enters Turn through IASA, the same pre-IASA nudge is still
  applied before the entered motion's collision pass observes floor loss. LDW `2581 -> 2615` locks
  the Battlefield platform overlap case where LandingFallSpecial receives the outward `+0.3` nudge,
  enters `Ottotto`, then falls from the platform edge after the frame-start Ottotto Turn IASA.
  The same source owner is now table-backed for grounded callbacks whose decomp Coll bodies call
  `ft_80083F88(gobj)`: generated `MSLMSO01` class `FT80083F88_GROUND_TO_AIR_COLL` identifies the
  `ft_80082708 -> mpColl_8004B108` ground-to-air family. The retained runtime consumes only the
  audited KneeBend subset: `MGS:616` locks the FoD top-platform `SquatRv -> KneeBend -> Fall` case
  where the source x450 nudge moves the KneeBend root past the static platform edge before
  collision. The broader all-grounded off-edge nudge remains gated until unrelated
  collision-owner families are modeled; an all-class experiment caused unrelated Battlefield float
  drift. Sources:
  `refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}`,
  `refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}`,
  `refs/melee/src/melee/ft/ft_081B.c::{ft_80084280,ft_80083F88,ft_80082708}`,
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::{ftCo_8009A3C8,ftCo_Ottotto_IASA}`,
  `refs/melee/src/melee/ft/chara/ftCommon/{ftCo_Turn.c,ftCo_KneeBend.c,ftCo_SquatRv.c}`,
  `refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108`,
  `data/motion_state/owners/{fox,falco}.bin`.
- Grounded floor endpoint snap admission is table-backed by MSLMSO01 rather than a runtime
  action-id list. The generated `FT800827A0_EDGE_SNAP_COLL` class marks callbacks that reach
  `ft_800827A0 -> mpColl_8004B2DC -> mpColl_8004A45C_Floor`, including grounded attacks,
  down-roll/down-attack, escape roll/spotdodge, catch/throw, side appeal, passive stand, and
  grounded Side-B end. DownBound/DownWait/DownStand remain on the separate
  `ft_80082708 -> mpColl_8004B108` ground-to-air owner. Sources:
  `refs/melee/src/melee/ft/ft_081B.c::{ft_800827A0,ft_80084104,ft_800841B8,ft_80082708}`,
  `refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B2DC,mpColl_8004A45C_Floor,mpColl_8004B108}`,
  `data/motion_state/owners/{fox,falco}.bin`.
- Damage family runtime predicates use generated MSLMSO01 ownership instead of repeated local
  action lists where the decomp row already carries the distinction: `DAMAGE_AIR` and
  `DAMAGE_GROUND` come from MotionState submotion symbols; `DAMAGE_*_COLL` classes continue to own
  collision-callback families. Source-specific exceptions such as DownDamageU/D remain explicit
  only where the downed callback re-enters the damage hitlag owner through `ftCo_8009F184`.
  Sources: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c`,
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c`,
  `data/motion_state/owners/{fox,falco}.bin`.
- Grounded Attack* IASA delegation uses generated MSLMSO01 owner classes for the Wait_IASA
  special, locomotion-tail, and catch/guard subsets instead of hand-maintained runtime switches.
  These classes are keyed from decomp IASA callback symbols such as `ftCo_AttackS3_IASA`,
  `ftCo_AttackS4_IASA`, and `ftCo_AttackDash_IASA`; procedural ordering remains in locomotion.
  Sources: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack*.c`,
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA`,
  `data/motion_state/owners/{fox,falco}.bin`.
- Runtime callback-owner gates no longer compare raw MSLMSO callback ids for EscapeAir collision or
  grounded Side-B Start/Main collision. Generated `ESCAPE_AIR_COLL` and
  `FX_SPECIALS_GROUND_B108_COLL` classes own those predicates; AttackAir platform/floor-skip
  classification is centralized through one generated callback/submotion owner helper before
  phase-specific MSLFTSC1 checks are applied.
- Basic airborne floor-contact Wait-vs-Landing selection uses generated MSLMSO01
  `FT80082B1C_BASIC_LANDING_COLL` ownership instead of a local action list. The class is generated
  from collision callbacks that route through `ft_80082B1C`, including Jump/Fall/CliffJump2 and
  Fox/Falco airborne blaster catch-hit callbacks. Sources:
  `refs/melee/src/melee/ft/ft_081B.c::{ft_80082B1C,ft_800831CC,ft_800835B0}`,
  `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::*_Coll`,
  `data/motion_state/owners/{fox,falco}.bin`.
- FoD generated slope persistence uses the same source floor traversal in jump-squat and immediate
  EscapeAir handoffs. `KneeBend_Coll -> ft_80083F88 -> ft_80082708 -> mpColl_8004B108` consumes
  signed `mpLib_8004DD90_Floor` correction on generated slopes. If `KneeBend_Anim` enters Jump and
  same-callback Jump IASA enters EscapeAir before Fighter_procMap, the following
  `EscapeAir_Coll -> ft_80082C74 -> mpColl_800471F8` publishes the returned connected floor
  contact, not the stale slope contact carried from the seed row. EWT `9753` and `9755` lock the
  slope and slope-to-flat variants; static platform and non-KneeBend controls keep the old
  anti-snap boundary. Sources:
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::{ftCo_KneeBend_Coll,ftCo_KneeBend_Anim}`,
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_IASA`,
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll`,
  `refs/melee/src/melee/ft/ft_081B.c::{ft_80082708,ft_80082C74}`,
  `refs/melee/src/melee/mp/{mpcoll.c::{mpColl_8004B108,mpColl_800471F8},mplib.c::mpLib_8004DD90_Floor}`.
- Taxonomy now records the victim's post-timebase/pre-combat action when splitting debug-selected
  false-positive rows. If the victim already diverged from the replay destination before BODY
  collision, the row is assigned to `F10l`; if there is no pre-combat candidate, it is assigned to
  `F10k`; otherwise common-attack false positives remain in the active `lbColl` narrowphase split.
- The parent BODY geometry/HSD pose collision checklist is not closed by taxonomy relabeling
  alone; it is closed only because the remaining former-F08b-shaped rows now have row-level
  evidence outside the parent primitive owner. `FSP:5765` / `PPA:3182` are no-candidate rows
  before BODY candidate construction and remain in action/hitbox timing (`F10k`), `FSP:4852` /
  `HIS:5950` diverge before BODY admission and remain in action-timebase (`F10l`), and
  `HIS:5029` / `IAT:6288` both accept BODY but differ in hurt-height damage selection
  (`F05b`). No broad BODY geometry/admission bucket remains under another name.
- BODY admission now runs the `lbColl_8000805C` / `lbColl_80006E58` matrix-radius predicate
  directly when hurt-bone pose data is available. The old simple world sphere/capsule test is only
  a missing-data fallback, not a prefilter. `PPA:2614` protects the grounded `AttackAirB` vs
  `AttackHi3` row where matrix scalar admission succeeds even though the simple prefilter has no
  BODY candidate. Source paths: `refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70` and
  `refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}`.
- `BHH:1599` has now been narrowed with debug closest-pair instrumentation:
  - current decomp-shaped x58->x4C sweep proxy is nearly stationary for Fox AttackAirN hb1
    (`pose_prev=13`, `pose_cur=14`), so the miss is not a missing sweep call;
  - the nearest current BODY pair is attacker hb1 to defender cap12, margin `-0.329` under the
    stabilized geometry (`distance=5.380`, `sum_r=5.051`);
  - a collision-subtree scale-cancellation probe reduced that miss to `-0.107` but still did not
    admit the hit and regressed primary/cardinal one-step from `717` to `1381`, so it was rejected;
  - a global matrix-radius/narrow-phase probe using the existing `lbColl_80006E58` approximation
    still rejected `BHH:1599`, so the remaining blocker is not simply the broad/simple
    sphere-capsule callsite.
- Forensic support now decodes active Dolphin engine-dump HitCapsule/HurtCapsule records into
  extracted row JSON (`tools/dolphin/engine_dump_io.py`, `tools/dolphin/extract_engine_dump_rows.py`)
  so the next capture can compare engine `x58/x4C`, hurtcap `a_pos/b_pos`, radii, flags, and
  offsets directly against `tools/eval/run_forensic_rows.py` closest-pair output.
- `reports/triage/20260416_bhh1599_*_dolphin_primitive_dump` confirms the active playback dump
  records the correct post-frame primitive payloads, but it is a post-collision snapshot: the frame
  where the hit appears already has the defender in `DamageFlyN`/hitlag. It can confirm the
  attacker hb1 world center/size at the hit frame, but it cannot expose the defender's pre-
  `ftColl_80078C70` AttackHi3 hurtcap endpoints for the collision that caused the transition.
- The remaining BODY geometry blocker is therefore the exact runtime primitive/pose state consumed
  by `lbColl_8000805C`/`lbColl_80006E58` for grounded attack hurtcaps near create/steady frames.
  Current extracted SSANIM pose plus movescript hitbox data leaves the real contact just outside the
  collision envelope, while local owner-shaped probes that can be derived from current data either
  do not admit the row or cause same-owner aggregate churn.
- A pre-`ftColl_80076ED8` interpreter probe for `BHH:1599` captures the actual selected pair:
  p0 AttackAirN hb1 (`HitCapsule.x58=(16.118845,13.620139,0.388264)`,
  `x4C=(16.928013,10.219827,0.399950)`, `scale=3.495870`) against p1 hurtcap 12
  (`bone_idx=18`, `a_pos=(19.932968,8.830592,-2.885571)`,
  `b_pos=(18.661987,7.971865,-2.966146)`, `scale=1.62`). The hit x58/x4C lane matches the
  explicit reseed lane; the remaining mismatch is the defender live JObj matrix used by
  `lb_8000B1CC`.
- The same probe rules out the earlier suspected scalar owners for this row: defender
  `cur_anim_frame=4.0`, `x898_unk=0.0`, `x8A4_animBlendFrames=0.0`, and `frame_speed_mul=1.0`.
  The live `HSD_JObj` for hurtcap 12 has local shoulder rotations that differ from the extracted
  `SSANIM01` AttackHi3 frame-4 locals even though the FigaTree header and track descriptors match
  the extracted action. The next implementable owner is therefore the HSD JObj/AObj live-pose state
  used by `ftAnim_8006E9B4`/`ftAnim_8006E7B8` before collision, not a hitlist or
  `lbColl_80006E58` distance predicate.
- A local runtime probe that advanced grounded AttackHi3 hurtcaps and promoted the matrix-space
  `lbColl_80006E58` predicate into primary BODY selection was rejected: it starts a hit on
  `BHH:1599`, but selects an earlier low hurtcap (`DamageFlyLw`) while vanilla selects the live
  hurt part 18 and enters `DamageFlyN`. This proves the remaining work is not just enabling the
  `lbColl` predicate or shifting the visible animation frame; the missing state is the live
  HSD_JObj pose/dynamics chain feeding `lb_8000B1CC`.
- `BHH:1599` also proves why this owner is split from core combat followup: the divergence happens
  before `ftColl_80076ED8` admits a BODY hit. The defender has `dynamics_num=1`, and decomp updates
  dynamic bone sets through `ftCo_8009DD94` / `lb_8001044C` while `ftAnim_8006E7B8` skips
  animation on flagged dynamic subtrees and `ftAnim_8006EED4` can reanimate a toggled subtree from
  `fp->x590`. The sim now extracts the target-domain `ftData.x2C` dynamic descriptors into
  `data/anims/{fox,falco}.dyn.bin`, keeps runtime dynamic-node pose state in fixed-capacity
  per-player arrays, updates that state before hurtcap refresh, and lets hurtcap world endpoints
  sample a dynamic collision matrix before `lbColl_8000805C` runs. BODY admission still uses the
  normal `ftColl_80078C70` -> `lbColl_8000805C` predicate. `SSDYNN01` v7 carries the audited
  dynamic-collision owner submotion index, reserved-empty source-step owner submotion index,
  cone-clamp owner submotion index, and `ftData.x2C->x8` collider rows, so C gameplay no
  longer gates this owner on raw Fox/JumpB/Catch/CatchDash/AttackHi3 msid branches. Runtime carries
  dynamic-node state sequentially only inside generated dynamic-collision owner submotions; local
  non-owner submotions clear the state instead of preserving an unseeded hidden carry. The
  implemented runtime surface is intentionally one-set for Fox/Falco
  (`Fox: [17,18,19,20]`, `Falco: []`) and the loader rejects present multi-set or oversized-chain
  `SSDYNN01` data until a set-indexed state surface is needed.
- A runtime hardcoded primitive overlay for Fox `AttackHi3` / frame 4 / hurtcap 12 and a generated
  one-slice data overlay were tested and rejected as final owner implementations. A broader static
  grounded-common-attack dynamics bake was also rejected: it fixed `BHH:1599` but regressed primary
  and aggregate validation by applying Fox's dynamic-tail descriptor without the persisted
  `lb_8001044C` dynamic-node state. The retained implementation surface is data/decomp-driven:
  `tools/extraction/extract_fighter_anims.py` parses `ftData.x2C` (`BoneDynamicsDesc` stride
  `0x18`, descriptor constants stride `0x3C`) and emits dynamic chains; `src/anim_pose.c` loads the
  chains and carries runtime dynamic-node rotations/positions. The update model follows the
  `lb_8001044C` segment-vector path for the supported target domain: previous child position,
  current animation segment vector, descriptor follow/down/decay constants, carried correction
  axis/angle, descriptor `+0x68` cone clamp, and source `ftData.x2C->x8` segment/sphere avoidance
  produce the next child position and collision-matrix rotation before `lb_8000B1CC`. The
  source-step subset is hard-disabled in
  runtime for this stack: generated data must keep the index empty and the loader rejects non-empty
  source-step indexes until descriptor natural direction, max-step, local dynamic JObj rotation
  writeback, and source-order timing land as one validation-clean owner. Current
  collision owners keep the validated current-segment cone approximation. Non-sequential replay seeds
  reconstruct the same deterministic
  state by replaying that action-local dynamic update from frame 0 to the seeded integer animation
  frame. This replay
  is `O(action_frame)` on non-sequential reseed/pre-combat reconstruction only; normal sequential
  rollout carries the fixed dynamic state forward. No replay authority, record-id branch, cap/frame
  primitive injection, runtime overlay table, or broad permissive geometry sweep is used.
- This partially models the Fox JumpB/LandingFallSpecial/Catch/CatchDash/AttackHi3 /
  `SSDYNN01`
  dynamic-chain collision-pose sub-owner. The retained runtime is source-bounded for the supported
  segment/sphere surface, but full `lb_8001044C` ownership remains incomplete for non-source-step
  submotions until their natural-direction/JObj-rotation cone paths are separately audited.
  JumpB was added after Dolphin pre-ftColl probes on `PPA:3182` showed Falco AttackAirB's hitbox
  already matched runtime, while Fox hurtcap-12 endpoints consumed the live `ftData.x2C` dynamic
  chain before `lb_8000B1CC`. The retained v7 contract treats the collision-owner index as
  current-frame dynamic matrix ownership even when the `lb_8001044C` update has no nonzero
  correction carry; a broad JumpB facing flip was rejected because it fixed `PPA:3182` but regressed
  protected aggregate BODY rows and rollout totals.
  AttackDash remains intentionally excluded after the HIS/FSP static-chain probes showed vanilla
  rejecting the dynamic-tail contact in those rows; the runtime still consumes only the `SSDYNN01`
  owner index and has no C row/msid gate. CatchDash was added after the SDS:299 collision probe
  showed vanilla's live part-18 tail endpoints below Falco grounded Shine while the old partial
  dynamic-chain cone admitted a false BODY hit. Catch was added after MGS:4921..4923 showed Falco
  DAir's live hitbox overlapping Fox's part-18 tail cap only after Catch frame 10; the same dynamic
  chain with the `SSDYNN01` cone-clamp owner index keeps frames 8 and 9 no-hit while admitting the
  vanilla `DamageN2` on frame 10. The cone-clamp index intentionally names Catch and not CatchDash:
  SDS:299 is the negative that proves the current-segment cone approximation is not the full
  source-step/natural-direction owner for all dynamic collision msids. DamageAir2 remains excluded
  after the source-step
  experiment traded TBK/DSG/FSP/PJO rows instead of closing the shared source-order dynamic pose
  path. Terminal Fox `DamageFly*` also keeps one same-action create-edge guard for AttackHi3 vs the
  part-18 tail chain: QGD:7173 shows static SSANIM endpoints admitting the fresh up-tilt HitCapsule
  one collision frame before vanilla, while QGD:7174 proves the already-live HitCapsule hits
  normally on the following frame. The source owner is the terminal DamageFly callback episode, so
  runtime uses MSLMSO01's `MSL_MS_CLASS_DAMAGE_FLY` instead of a Top-only action test. The guard is
  still bounded to the source-owned terminal/create-edge/tail slice: non-Fox, non-tail, already-live
  HitCapsules, and same-frame new action entries stay on the ordinary BODY path. A broader
  all-DamageFly create-edge suppression without the AttackHi3/Fox-tail provenance regressed
  aggregate one-step and rollout validation and was rejected.
  owner. Together with
  Turn internal-facing hurtcaps, authoritative HitCapsule `victims_1` preservation, GuardSetOff
  shield-hit onset lineage, swept/same-group hitbox-vs-hitbox clank, decomp-ordered clank
  same-group suppression, hidden x1990 visible-clear seed ownership, Escape floor-edge BODY pose,
  same-frame/enable-edge HitCapsule x58/x4C continuity, CliffAttack hitbox extraction, enable-edge
  phantom/tip-log handling, matrix-first lbColl BODY admission, and late AttackAirHi HitCapsule
  latch preservation, the former broad `F08b_body_contact_geometry_residual` bucket is now a
  precise active work map rather than a closure claim. Remaining former-F08b-shaped rows are
  `F10k_body_no_candidate_action_timing`, `F10l_body_selected_false_action_timebase`, and
  `F05b_damage_hurt_height_selection_residual`, with former special-entry rows in existing
  `F10e_special_move_adjacency`. This is not a replay bridge: no
  replay-proof BODY admission, dataset/record branch, cap/frame primitive injection, one-slice
  overlay, broad static bake, or permissive BODY sweep is used.
- AttackAirN neutral dense-latch preservation is a narrow seed-surface owner, not BODY admission:
  when the fallback dense hitlist materializes a neutral defender whose victim iid still matches
  the live fighter, runtime keeps `HitCapsule.victims_1` suppression because `lbColl_8000ACFC`
  keys on victim object presence and does not consult `instance_hit_by`. This protects `HIS:2752`
  only on the Wait entry lane while the existing stale-owner clear lanes still admit proven
  AttackAir refresh hits (`HIS:2753`, `HIS:3126`), eliminating the former `F08f` candidate-filter
  residual without replay-proof admission. Source
  paths: `refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008A5C}`,
  `refs/melee/src/melee/lb/types.h::HitCapsule`, and
  `refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8`.
- AttackAirF dense-latch carry into grounded GuardOn admission uses the same victim-pointer owner
  on rollout create edges: if a replay rollout starts before the exact per-HitCapsule victims_1
  state is active, the dense group seed can still prove a live victim latch. When the later
  AttackAirF create edge reaches a grounded shield-input admission frame before ShieldDesc
  ownership is installed, materialize that latch and rebind the stale Slippi `instance_id` proxy
  unless the victim crossed a death/rebirth pointer boundary. `HVG:7959 -> 7970` proves this must
  suppress the otherwise false GuardSetOff shield hit; clearing the dense seed admits that hit.
  Source paths: `refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80078C70}`,
  `refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}`, and
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardOn_Enter`.
- Late AttackAirHi dense-latch preservation applies the same HitCapsule owner to the UpAir
  late-window recreate surface: Fox/Falco UpAir clears early hitboxes and recreates same-group late
  hitboxes at frame 11. After that recreate edge, `lbColl_8000ACFC` suppresses by victim pointer;
  Slippi `instance_hit_by` can still name an older source and the proxy `instance_id` can advance on
  a same-frame Wait entry. `FSP:7079` protects preserving the current-port victims_1 latch instead
  of clearing it as stale dense fallback. Source paths:
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim`,
  `refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C`, and
  `refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}`.
- AttackAirHi create-frame dense-latch preservation is also needed for terminal same-source
  `DamageFlyTop` hitstun victims: when rollout starts before UpAir's first HitCapsule create edge,
  the dense group seed can prove vanilla's `victims_1` already contains the live victim pointer even
  though replay BODY attribution still names the previous same-port source instance. Runtime may
  materialize that dense latch only for `AttackAirHi` create frames against same-source
  `DamageFlyTop` victims whose current instance id matches the seed; the following
  authoritative-empty per-HitCapsule seed owns the clear/admit boundary for the real next-frame hit.
  `TBK:5247` protects the suppressing row and `TBK:5248` protects the admitted-hit row. Source
  paths: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim`,
  `refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}`, and
  `refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008A5C}`.
- AttackAirLw no-clear same-slot payloads initialize hidden HitCapsule provenance from legacy dense
  seeds during reseed, and the live script payload path also materializes that provenance when the
  create payload is replayed. Falco DAir's late create payload can update damage/offset fields
  without disabling the slot or changing `hit_group`, so
  `ftAction_8007121C` does not call the `ftColl_800768A0` clear path and the existing
  `victims_1` latch persists until a later source clear/create or victim object boundary. Exact
  per-HitCapsule empty seeds still own the real admit boundary (`PEC:344`), while dense fallback is
  limited to the no-clear payload phase (`PEC:341..343`). Source paths:
  `data/scripts/falco.bin` (`MSLFTSC1 ftCo_SM_AttackAirLw`),
  `refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C`,
  `refs/melee/src/melee/ft/ftcoll.c::ftColl_800768A0`, and
  `refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}`.
- Same-frame common locomotion entry hurtcaps now preserve the previous live JObj pose for BODY
  collision when input/IASA enters Wait/Run/Walk after the prio-1 animation tick and the entry path
  does not call an immediate `ftAnim_8006EBA4`. The replay-visible action/timebase can already be
  the new state, but `lb_8000B1CC` still consumes the previous interpreted JObj matrix for this
  collision pass. This fixes the WalkSlow->Wait, Dash->Run, and SquatRv->WalkSlow aerial false
  BODY clusters (`DCC:8844`, `IAT:6287`, `TCH:5842`) without changing BODY admission authority.
  Source paths: `refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}`,
  `refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4`, and
  `refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC`.
- Common airborne `Damage_Anim -> ftCo_Fall_Enter` splits motion-state side effects from the Fall
  pose/timebase commit. `Fighter_ChangeMotionState` side effects (`ft_800890D0`,
  `ft_800895E0`, facing_dir1, smash attr clears) run immediately in callback order, but
  `ftCo_Fall_Enter` does not call an immediate `ftAnim_8006EBA4`, so the previous DamageAir pose is
  still used for same-frame collision until the post-combat deferred Fall timebase commit.
  `HVG:7959 -> 7964` proves this ordering: p0's Fall instance id advances before p1's same-frame
  AttackAirF entry. Source paths:
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Anim`,
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter`, and
  `refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState`.
- The hidden HitCapsule shield/body lineage sub-owner is also now represented on the
  per-HitCapsule seed lane rather than as a geometry bridge:
  - accepted shield/body contacts register `HitCapsule.victims_1` across all active same-group
    capsules (`ftColl_80076808`/`inlineB0`);
  - `combat_hitlist_hb_valid=1` means the seed lane is authoritative for that exact slot,
    including non-empty victim lists and authoritative empty lists;
  - the dense stale-latch cleanup in `tools/slippi/make_dataset_from_slp.py` and the runtime
    materialization trim in `src/hitboxes.c` must not erase authoritative per-HitCapsule lanes.
    This fixes replay-real AttackDash/AttackAirLw shield-lineage rows such as
    `PRH:1830..1834`, `IAT:11146..11147`, and the GuardSetOff shield-damage onset rows
    `BHH:1803..1804` without a replay-proof BODY admission bridge.
  - Retained bridge: the AttackAirLw -> fresh GuardOn replay-seeded rollout dense-hitlist trim is
    validation-mode/provenance-specific. It exists because the dense group seed still cannot encode
    the exact per-HitCapsule `victims_1` clear/copy provenance for this boundary. Delete it when
    the replay seed carries the needed per-HitCapsule provenance owner instead of relying on dense
    same-group materialization.
  - Open residual / package-boundary negative: AGN `5167 -> 5168` AttackAirN new-hit admission is
    not a replay-exact owner closure in this package. It remains a visible negative lock until
    per-hitbox/live HitCapsule provenance can replace the coarse dense group lane without a
    replay-row bridge.
  - SpecialHi dense group seed materialization is narrowed to source-proven accepted-hit episodes:
    the coarse dense group lane can suppress a live SpecialHi BODY contact only when the stored
    victim instance is current, `instance_hit_by` names the attacker instance, and `last_hit_by`
    names the attacker source port. This preserves post-hitlag DownBound/knockdown repeats where
    the HitVictim pointer still owns suppression after hitstun clears, but lets unrelated
    SpecialHi charge/launch inactive-gap dense entries clear through the live BODY callback. This
    fixes QGD dense SpecialHi positives and HIS `8513` without turning the dense group fallback into
    replay-proof authority. Authoritative per-HitCapsule seed lanes remain exact.
  - HIS `2752` AttackAirN -> first neutral Wait BODY suppression is retained as replay-rollout
    reconstruction debt, not source-closed live HitCapsule authority. The helper requires
    `replay_rollout_reseeded`; ordinary free-running and one-step reseed paths cannot consume the
    x18c8/last-hit fallback when row-local dense HitCapsule provenance is absent. Delete this bridge
    when a prefix-causal per-HitCapsule victim/provenance lane can materialize the actual
    `HitCapsule.victims_1` state through long rollout segments.
  - GuardSetOff onset provenance is replay-visible when the defender enters GuardSetOff hitlag and
    shield HP drops, even if the previous visible action was not Guard-family (for example
    DownStandD). That proves the prior shield branch `ftColl_80076CBC` wrote the same-group
    HitCapsule `victims_1` list; seed-history stamps authoritative per-HitCapsule lanes for the
    affected shield-hit onset rows.
- The hitbox-vs-hitbox clank sub-owner now uses the decomp swept HitCapsule predicate and
  same-group clank suppression:
  - `ftColl_80078C70` checks grounded hitbox-vs-hitbox clank before BODY hitbox-vs-hurtcap
    admission;
  - `lbColl_80007AFC` routes to `lbColl_80006094`, consuming each HitCapsule's previous/current
    center (`x58 -> x4C`) and radius rather than a current-center sphere/sphere test. The shared
    segment helper handles degenerate `x58 == x4C` create/enable-edge capsules as point-vs-segment
    tests, matching `ftColl_8007AD18` instead of falling back to endpoint distance;
  - `ftColl_8007699C` writes the clank victim across active HitCapsules sharing the same
    `HitCapsule.x4` group via `inlineA0`/`inlineA1`, but `ftColl_80078C70` runs that inside the
    per-HitCapsule loop. The first accepted same-group clank owns the group's hitlag/rebound
    damage for that fighter pair; later same-group clank candidates and BODY admission for that
    pair are suppressed by the refreshed HitVictim entries. Those type-3 victims_1 entries are
    persistent HitCapsule state, so hitlag-tail rollouts and one-step reseeds materialize them from
    the per-HitCapsule seed lane when swept clank geometry plus replay-visible clank hitlag prove
    that side branch;
  - the `ftCommonData.x3CC` damage-delta checks inside `ftColl_8007699C` are side-specific, not a
    single reciprocal boolean. Each side only receives clank hitlag/rebound and same-group victim
    suppression when its own `(int)HitCapsule.damage - x3CC < (int)other.damage` branch runs.
    `HitCapsule.damage` is the already-produced collision damage after stale/smash scaling, so a
    staled Shine can suppress BODY through its side's clank branch while the higher-damage attack
    receives no ReboundStop;
  - clank geometry is allowed to run before replay-reconstructed BODY victim rings prefilter the
    pair. HHG:8674 proves a stale BODY ring can otherwise mask a live AttackDash/AttackHi3 clank,
    while FSP:467 protects the degenerate enable-edge point-capsule side; this is a
    seed-reconstruction boundary, not a replay-authority admission branch.
  ReboundStop entry runs from the post-physics collision pass; `ftCo_80099D9C` writes the
  `mv.co.rebound.x0` value through `ftCommon_800804A0` (`xE8_ground_accel_2`), so the entry frame
  does not overwrite the already-reported ground velocity. The same source owner stores
  `mv.co.rebound.anim_start = (co_attrs.x9C + 0.1) / dmg.x191C`, which must survive frozen
  ReboundStop hitlag until `ReboundStop_Anim -> ftCo_80099E44` enters Rebound. The queued xE8 lane
  is consumed by the first Rebound physics frame: movement uses old `gr_vel`, then post-frame
  ground velocity receives xE8. The xE8 sign is owned by clank-local `dmg.facing_dir`, written from
  relative fighter root positions in `ftColl_8007699C` inlineA0/inlineA1, not by replay-visible
  scalar facing. Teacher-forced ReboundStop hitlag-tail seeds reconstruct the pending xE8/rate
  lanes from the future visible Rebound transition; these are explicit non-causal one-step lanes.
  Runtime clank entry writes both causally from `dmg.x191C`. This fixes FSP:5466, HHG:8674,
  FSP:467, and the FSP:472/473 Rebound transition without a BODY admission bridge or row-id branch.
- GuardSetOff shield hits split the hidden shield-damage owners:
  `ftColl_80076CBC` writes `x19A4` as the max integer hit damage for hitlag/shieldstun, while
  `Fighter_ProcessHit_8006D1EC` consumes the separate `x19A0_shieldDamageTaken` accumulator for
  shield HP. Teacher-forced rows seed x19A4 from replay-visible GuardSetOff + both-fighter hitlag;
  same-frame special contact owners such as shine may use extracted active HitCapsule damage because
  hitlag alone is only a lower bound for x19A4. Multi-hitbox attack contacts keep the hitlag lower
  bound until exact per-HitCapsule shield-contact order is extracted. They seed x19A0 only when
  replay t->t+1 shield HP proves `x19A0 > x19A4`; this is an explicit non-causal one-step lane.
  x19A0<=x19A4 multi-contact rows remain runtime-selected-contact ownership. Replay-real positives:
  FSP:3100, HHG:5544, PRH:7124, HVG:4489. Negative: QGD:3938.
  Sources: `refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC`,
  `refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC`,
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C`.
- Continuing GuardOn raise-shield no-submotion rows have a separate fighter-vs-fighter ShieldDesc
  pose owner. After `ftCo_800924C0` enters GuardOn, `ftCo_GuardOn_Anim` continues calling
  `ftCo_80091E78` while `mv.co.guard.x10` remains live, but Slippi still exposes the row as
  `animation_index=-1/action_frame=-1`. For rows whose previous seed action was already shield-owned,
  fighter shield collision samples the GuardOn x20 target extracted in `data/shields/{fox,falco}.bin`
  instead of the settled Guard neutral bubble. First visible GuardOn entry remains owned by the
  existing entry ShieldDesc lane; broad x20 on entry over-admits nearby persistent AttackAirB/Shine
  controls. The extra ShieldDesc.size term is retained only for aerial AttackAir capsules in this
  continuing raise-shield lane; non-AttackAir specials remain on the established lightshield bubble
  owner until their exact x58/x4C shield narrowphase is closed. This item does not close the broader
  ShieldDesc/shield-contact family: `src/combat.c` still uses a labeled reduced
  `lbColl_80007BCC` extent proxy for some near-boundary sphere/segment overlap rows while the full
  source JObj/extent narrowphase remains open. Positive: DSG:6313. Negatives:
  DSG:6311/6312, IAT GuardOn entry, HVG AttackAirB, FSP Shine lightshield.
  Sources: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800924C0,
  ftCo_GuardOn_Anim,ftCo_80091E78}`, `refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70`,
  `refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC`.
- Hidden color-animation x1990 seed ownership now treats replay-visible vulnerable snapshots as an
  observable clear of stale cliff/ledge x1990, while preserving the hidden x1994 invincible-contact
  lane only for source-proven Damage/DownBound consumers. A visible vulnerable non-damage /
  non-DownBound row clears replay-history x1994 provenance before later cliff/EscapeAir masking can
  hide it. This follows Slippi's post-frame
  `x1988 != 0 ? x1988 : x198C` emission and removes the old x1990-owned candidate-filter split
  without weakening DownBound sentinels.
  CliffCatch/Wait and derived cliff-option episodes remain x1990-only at reseed unless a real
  x1994 producer is proven separately: the source ledge path calls `ftColl_8007B760(..., x49C)`,
  not `ftColl_8007B7A4`. This prevents stale damage x1994 reconstruction from surfacing as
  invincible-contact status when CliffJumpQuick2 x1990 expires (`HVG:5101`) or when EscapeAir lands
  into LandingFallSpecial after a cliff-invulnerability episode (`PJO:2718->2738`).
- SpecialHi/AirHi wall collision now uses the live JObj ECB owner for the supported Fox/Falco
  wall domain when `ft_CheckGroundAndLedge` runs the airborne mpColl wall path. The
  collision ECB is rebuilt from the extracted ftData_x44_t `ecb_joints`
  (`data/characters/{fox,falco}.json`) through the collision-pose matrices, applies model scaling
  and the same FtPart_XRotN `rotateModel` owner used by SpecialHi hit/hurt primitives. Because
  SSANIM01 matrices are facing-independent, the fighter root Y rotation maps extracted local Z into
  stage X before `mpColl_LoadECB_JObj` reads the point; both wall sides use that faced-Z X basis
  after XRotN, not the matrix-local X component. The helper then applies
  `mpColl_LoadECB_JObj`'s `x12C` horizontal recenter before local
  `mpColl_800454A4_RightWall` / `mpColl_80046224_LeftWall` airborne envelope resolution. The
  retained candidate list is the
  decomp-ordered side, bottom, and top sweeps; a broader current side-edge sweep was tested and
  only retained for the SpecialAirHi left-wall envelope where it matches HVG/MAJ without broadening
  non-SpecialHi wall actions. Wall sweep checks use raw `mpCheck{Left,Right}Wall` endpoints plus
  `mpLineIntersectionV`'s local endpoint clamp, not `mpLib_8004ED5C` linked-line extension. QGD
  9372/9373/9374 are positive locks, QGD 9371 guards pre-contact, BHH 6392 guards horizontal
  identity/recenter behavior, TCH 2601 guards the FD-lip endpoint boundary, GAT 3099 guards
  right-wall/left-wall separation, and HVG 4780..4801 guards the former left-wall -> PassiveWall
  cascade.
- SpecialAirHi's left-wall launch path now uses the source-shaped
  `mpColl_80045B74_LeftWall -> mpColl_80046224_LeftWall` side/bottom/top candidate list and
  airborne ECB envelope min-X resolution for `ftFx_SpecialAirHi_Coll` rows. It also refreshes
  wall metadata while hitlag is active: `Fighter_8006A360` gates Anim/IASA/Phys, but
  `Fighter_procMap` still calls the collision callback, so stale SpecialAirHi Hug bits must not
  carry into DamageFly hitlag-exit wall-tech checks. This is intentionally narrower than all
  left-wall actions: a generic left-wall envelope was tested and regressed unrelated rows. The
  retained MAJ 8878 and HVG 4780 rollout locks protect the left-wall envelope and hitlag refresh
  boundaries.
- The same `ftFx_SpecialAirHi_Coll` wall/ceiling contact path also owns collision-facing during
  launch. If the contact normal is within `90 + ftFox_DatAttrs.x94_FOX_FIREFOX_BOUND_ANGLE` of
  `self_vel`, source writes `fp->facing_dir = sign(fp->self_vel.x)` and recomputes rotateModel.
  Runtime loads x94 from `data/characters/{fox,falco}.json::firefox_bound_angle_degrees`; TCH
  6989/6990/6998 locks the angle boundary and later `SpecialHiFall -> CliffCatch` rollout
  dependency.
- Up-B and aerial Side-B recovery exits consume all jumps through `ftCo_80096900(..., unk=true)`:
  grounded source states take `ftCommon_8007D60C`, while airborne source states take
  `ftCommon_UseAllJumps`. Recovery FallSpecial therefore must not allow a later X/Y press to become
  JumpAerial.
- Grounded Fox/Falco Side-B collision has two different floor-loss owners. `SpecialSStart_Coll` and
  `SpecialS_Coll` call `ft_80082708 -> mpColl_8004B108`, so leaving the floor during start/main
  converts to `SpecialAirSStart` / `SpecialAirS` while preserving animation frame. `SpecialSEnd_Coll`
  calls `ft_800827A0 -> mpColl_8004B2DC`; at a floor endpoint that path can use
  `mpColl_8004A45C_Floor` and keep the fighter grounded/snapped at the ledge in `SpecialSEnd`
  instead of entering actionable Fall. Sources:
  `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{ftFx_SpecialSStart_Coll,ftFx_SpecialS_Coll,ftFx_SpecialSEnd_Coll}`,
  `refs/melee/src/melee/ft/ft_081B.c::{ft_80082708,ft_800827A0}`, and
  `refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B108,mpColl_8004B2DC,mpColl_8004A45C_Floor}`.
- The grounded Side-B start/main floor-loss owner also consumes `mpColl_80043754`'s substep stop
  point for large TransN root movement. `mpColl_80043754` splits callback motion at the 6-unit
  root/ECB threshold; when `mpColl_8004ACE4` cannot project the carried floor at an intermediate
  substep, it stops the callback there before `ftFx_SpecialS_GroundToAir` enters aerial Side-B.
  FoD `ElatedWearyTermite.msl:7115` locks that grounded substep publication, while the adjacent
  already-aerial Side-B row stays on the normal airborne root step. Sources:
  `data/motion_state/owners/{fox,falco}.bin::MSLMSO01 coll_cb_by_action`,
  `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{ftFx_SpecialS_Coll,ftFx_SpecialS_GroundToAir}`,
  and `refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpColl_8004ACE4}`.
- Aerial Side-B Start/Main/End wall collision uses the full airborne mpColl wall envelope.
  `ftFx_SpecialAirSStart_Coll`, `ftFx_SpecialAirS_Coll`, and `ftFx_SpecialAirSEnd_Coll` call
  `ft_CheckGroundAndLedge`, which runs `mpColl_800473CC` or `mpColl_800471F8` and then
  `mpColl_80046904`; wall collision therefore needs the side/bottom/top candidate envelope
  (`mpColl_80045B74_LeftWall -> mpColl_80046224_LeftWall`,
  `mpColl_80044E10_RightWall -> mpColl_800454A4_RightWall`) rather than the point-only wall
  fallback. Dream Land `FlippantEnchantedHorse.msl:7692..7699` locks Start -> Main and Main -> End
  wall-envelope publication. This grants Side-B the source Push/Hug bits from mpColl, but it is not
  a common-air walljump callback and has no same-callback `ftWallJump_8008169C` consumer. Sources:
  `data/motion_state/owners/{fox,falco}.bin::MSLMSO01 class_bits`,
  `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{ftFx_SpecialAirSStart_Coll,ftFx_SpecialAirS_Coll,ftFx_SpecialAirSEnd_Coll}`,
  `refs/melee/src/melee/ft/ft_081B.c::ft_CheckGroundAndLedge`, and
  `refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_800471F8,mpColl_80046904}`.
- DamageFly hitlag-exit ASDI near a persisted wall uses the same CollData wall index provenance:
  `ftCo_Damage_OnExitHitlag` applies ASDI before DI/LSI, while `Fighter_procMap` will run the
  DamageFly collision callback later in the frame. When hitlag-refresh has cleared the Hug env bit
  but CollData still carries a left/right wall index, runtime projects only the immediate ASDI
  normal component away from that wall and preserves tangent ASDI plus `ftCo_8008E5A4` DI/LSI. It
  must not re-stamp Hug or run a full wall envelope while hitlag is still frozen; doing so
  over-admits PassiveWall. On the actual hitlag-exit frame, however, decomp keeps the
  pre-`ftCo_Damage_OnExitHitlag` CollData.prev_pos sweep root and then lets DamageFlyRoll_Coll
  consume the post-ASDI cur_pos for WallHug / FlyReflectWall. GAT `3107..3113` locks this boundary:
  the active-hitlag refresh remains Push-only, but the exit-frame sweep can enter FlyReflectWall
  via `ftCo_800C15F4`. HVG `4780..4829` locks the former F04 left-wall -> PassiveWall ->
  missed-death cascade.
- HIS `1673` remains a one-step strict-only facing residual after the retained Side-B / BODY
  contact fixes: Fox correctly enters `DamageFlyTop` with matching hitlag, hitstun, percent, and
  position, but the sim computes `dmg.facing_dir_1` from the current source root after the attacker
  has crossed the victim. Replay keeps the contact-time source-facing sign from the accepted
  hitbox collision. This is not a new gameplay branch; the source-shaped follow-up is a minimal
  contact-time `dmg.facing_dir_1` provenance lane for delayed BODY damage entry, not a row-local
  HIS special case.
- Common DamageFly wall projection also consumes `mpColl_LoadECB_JObj`'s horizontal ECB recenter
  before the wall candidate pass. For narrow JObj spans, decomp recenters the sampled left/right
  x-extents around zero and then applies the final +/-2 clamp from `mpColl_LoadECB_inline`; this is
  the shape used by `mpColl_80044E10_RightWall` / `mpColl_80045B74_LeftWall`, not a replay
  tolerance. PPA `5576/5577` lock the right-wall case where the corrected side extent matches the
  FD wall projection exactly. The current bottom/top side-edge candidates from
  `mpLib_800511A4_RightWall` / `mpLib_800515A0_LeftWall` are Push-only; WallHug remains owned by
  the side-point branch or by an explicit one-step `mpcoll_wall_*` seed lane for the narrow
  DamageFlyTop wall-callback bridge. Persisted-index recovery is likewise Push-only, so PPA's
  Push-only right-wall correction cannot manufacture PassiveWall, while DCC/HIS one-step
  `DamageFlyTop -> PassiveWall{Jump}` rows still consume the seed-owned WallHug phase.
- Sloped-wall side-point Hug uses `mpLineIntersection`'s decomp half-space clamp, not a strict
  geometric segment intersection. On Dream Land's right wall, `DamageFlyTop` can begin the
  side-point sweep infinitesimally across the sloped wall line; vanilla accepts that within the
  source `0.1` clamp, sets `Collide_RightWallHug` in `mpColl_80044E10_RightWall`, and
  `ftCo_DamageFly_Coll -> ftCo_800C17CC -> ftCo_800C18A8` enters `FlyReflectWall`. The retained
  runtime slice applies this clamp only to the side-point Hug sweeps; Push-only bottom/top/edge
  candidates stay on the existing strict intersection until the broader sloped-wall push/envelope
  owner is closed with separate locks. FEH `9195` locks the `DamageFlyTop -> FlyReflectWall`
  velocity owner, while PPA controls keep Push-only wall projection from manufacturing
  PassiveWall/FlyReflect. Sources: `refs/melee/src/melee/mp/mplib.c::mpLineIntersection`,
  `refs/melee/src/melee/mp/mpcoll.c::mpColl_80044E10_RightWall`,
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll`, and
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_FlyReflect.c::{ftCo_800C15F4,ftCo_800C18A8}`.
- Common Jump/Fall walljump callbacks use the `ft_800831CC` / `ft_800835B0` path through
  `ft_80083090_inline` and `mpColl_80047E14` flags-6 wall collision, not the `0xA`
  PassiveWall timer helper. When that callback preserves WallHug on FD's left wall,
  `ftWallJump_8008169C` still tests the frame-start `fp->pos_delta.x` written by
  `Fighter_8006A360`, so runtime uses the previous post-frame position lane rather than the
  post-collision wall projection displacement. HVG `4650..4672` locks the boundary: Hug is visible
  at `4666` without early `PassiveWallJump`, and `4667` enters `PassiveWallJump`. One-step seed
  preprocessing reconstructs the hidden `wall_jump_input_timer` / `x2110_walljumpWallSide` phase
  from prefix-visible common-air root movement against
  `data/characters/{fox,falco}.json::walljump_setup_x_delta_threshold`, but serializes that hidden
  phase only on rows where `ftWallJump_8008169C`'s stick-away admission branch can consume it. FEH
  `8797/8798/8799` locks the Dream Land right-wall setup/carry boundary: neutral rows remain
  ordinary JumpAerialF, and the fresh stick-away row enters PassiveWallJump.
- PassiveWall / PassiveWallJump entry uses the two-phase `ftCo_800C1E64` placement owner: snapshot
  the outgoing wall-side ECB point, enter the target motion, then run `ft_80081F2C`, whose
  `mpColl_80048464 -> mpColl_LoadECB_JObj(..., 0xA)` pass forces horizontal ECB side points to
  +/-1 before wall endpoint projection. `ftCommon_8007E2FC` clears both self velocity and attack
  knockback velocity on the same entry. DCC `4809/4810` locks the pre-Hug negative and the exact
  left-wall PassiveWall entry placement/velocity clear.
- GuardSetOff active-hitlag shield SDI uses `ftCo_80093240` on every grounded GuardSetOff frame
  whose hitlag remains nonzero after the hitlag decrement. The callback consumes the current
  `Fighter_Spaghetti_8006AD10` X-stick timer window and displaces along the floor tangent by the
  shield SDI scalar; only after hitlag reaches zero does the owner switch to the separate
  `ftCo_800932DC` post-hitlag ASDI callback. FEH `11426/11427/11428` locks the fresh X pulse, the
  last active-hitlag held-timer carry tick, and the following post-hitlag transition.
- Steady PassiveWall / PassiveWallJump collision keeps using the same `ft_800831CC` /
  `ft_80083318` wall-envelope owner after the startup timer expires. The generated MSLMSO01
  `COMMON_AIR_WALLJUMP_COLL` class now includes `ftCo_PassiveWall_Coll`, so airborne wall
  projection consumes the side/bottom/top `mpColl_80046904` envelope instead of a point-local
  side sweep while Phys remains the PassiveWall-specific fall/friction callback. PPA `907..911`
  locks the former Yoshi's Story PassiveWallJump row where replay clamped X to the wall envelope
  and the sim had moved by self velocity only. Sources:
  `refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::ftCo_PassiveWall_Coll`,
  `refs/melee/src/melee/ft/ft_081B.c::{ft_800831CC,ft_80083318,ft_80083090_inline}`,
  and `refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80047F40,mpColl_80046904}`.
- Guard floor-loss ledge-slip uses the explicit `ft_800845B4` MissFoot branch, not a generic
  grounded floor-loss rule. A terminal LandingFallSpecial row can run
  `ftCo_Landing_Anim -> ft_8008A2BC` into Wait in the prio-1 Anim proc, then the later input proc
  can enter GuardOn from held L/R/Z; `Fighter_procMap` then dispatches GuardOn_Coll, and
  `ft_800845B4` enters `ftCo_8009F39C` only when mpColl reports the facing/opposite ledge-slip
  bit. HVG `520` is the replay-real positive; clearing shield HP on that same seed falls normally,
  proving this is the GuardOn collision owner rather than a broad Landing/Wait edge-exit shortcut.
- Fighter phantom/tip-log delayed damage now carries the explicit `dmg.x189C_unk_num_frames`
  countdown alongside hidden `dmg.x1898` and source slot. `Fighter_ProcessHit_8006D1EC` decrements
  x189C independently of the replay-visible hitlag timer and applies x1898 through
  `ftColl_8007BE3C` when it expires, so a later shield hitlag source can overlap the delayed
  phantom percent application. Seed derivation only stamps active x189C rows when the hitlag-start
  frame had no immediate percent delta; full damage hits that apply percent as hitlag starts are
  not x1898 carriers. PRH 6830..6834 is the replay-real guard shield-poke case: the first BODY
  contact enters victim-only phantom hitlag, the next frame enters GuardSetOff from shield contact,
  and x1898 applies while GuardSetOff hitlag is still active. The only runtime admission outside
  the exact reconstructed `x7A8` scalar is scoped to the steady no-tilt Guard live-pose gap:
  `ftCo_80091E78(..., 1)` uses the current/no-tilt Guard pose directly when `mv.co.guard.x4` is
  effectively zero, while the generic extracted Guard matrix remains a coarse stand-in for that
  live JObj collision matrix. Nonzero-tilt, visible-submotion, enable-edge, and non-neutral Guard
  rows stay on the exact source `x7A8` predicate; PRH 6830 has a mutation lock proving the
  no-tilt gap is not a broad Guard BODY suppressor.
- Downed `DownBound*` phantom/tip-log contacts use the same `ftColl_80076ED8` source lane when
  `lbColl_8000805C/lbColl_80006E58` reports a positive collision distance within x7A8 and the
  victim has no hitstun. The stored x1898 damage is half of the stale-scaled HitCapsule.damage
  produced by `ft_80089228`, not half of the raw script damage. GAT `2739..2754` locks the
  same rollout segment where a downed phantom hit coexists with a later GuardSetOff shield hit.
- Fighter-vs-fighter ShieldDesc checks against `GuardSetOff` use the live `ftCo_SM_GuardDamage`
  shield-bone matrix from `ft_data->x8->x11`: `ftCo_80092450` installs a zero-offset size-1
  ShieldDesc on that bone, and `ftCo_GuardSetOff_Anim` only calls `ftCo_80091D58` to rescale it
  while frames remain. This is distinct from steady Guard tilt-table placement and avoids a broad
  shield radius/extent patch. GAT `2747` locks the hitbox-local boundary: AttackAirN hb1 hits the
  GuardSetOff ShieldDesc while hb0/hb2 still miss.
- Released grounded smash attacks keep the smash-charge state through the hitbox release frame.
  `ftAction_80073008` extracts the start-smash-charge scalar from the action script as the low
  16-bit argument multiplied by the single-precision `ftAction_804D82A0` literal
  (`.float 0.003906`), while `ftCo_800DEF38/ftCo_800DF0D0` preserve the
  held-frame count until release and `ftColl_8007ABD0` scales the released HitCapsule damage.
  This is attacker release-state ownership, not defender-side `kb_smashcharge_mul` ownership.
- Grounded DownDamage with remaining hidden x0 on animation end enters DownWait through
  `ftCo_80097F38` and preserves that remaining timer into `mv.co.downwait.x0`; it must not
  reinitialize the full p_ftCommonData->x424 DownWait duration. This is the jab-reset path for
  weak hits on downed victims.
- The fighter button-history timer lane treats a Z edge as an A edge for `x67C`, matching
  `Fighter_Spaghetti_8006AD10`'s effective `HSD_PAD_A | HSD_PAD_LR` mapping before
  `ftCo_80098400` admits `DownAttack*` from `DownBound*`. MSL's compact replay button domain does
  not expose the synthetic `HSD_PAD_LR` bit, so runtime and seed history map the Z macro only into
  the A-timer consumer and keep physical L/R timers on their explicit L/R/Z trigger lane. HIS
  `5121..5146` locks the positive `DownBoundU -> DownAttackU` path; clearing only Z from the same
  episode is the negative.
- Terminal `DownBound*` rows evaluate `ftCo_80098400` before current-frame input history refresh.
  When a same-frame A edge has already reset the simulator's `x67C`, the source pre-input A timer
  is recovered from `x683` and still must pass the post-entry `x67C < action_frame` freshness gate.
  This admits GAT `6713` (`DownBoundD -> DownAttackD`) without turning stale same-frame A rows into
  getup attack; DCC `2060` remains the stale-timer/roll negative.
- Terminal `DownFoward*` / `DownBack*` rows that finish their animation run the destination
  motion state's Phys once through the normal `Fighter_procUpdate` Phys slot after
  `ftCo_Down_Anim -> ft_8008A2BC`; the simulator must not apply an extra pre-Phys
  `ft_80084F3C` before the destination collision callback resolves floor loss. This matters on
  FoD ledge/platform edges where `Wait_Coll -> ft_80084104` can enter `MissFoot` after exactly one
  destination-ground-friction step (`src/knockdown.c`;
  refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate,
  refs/melee/src/melee/ft/chara/ftCommon/ftCo_Down.c::ftCo_Down_Anim,
  refs/melee/src/melee/ft/ft_0892.c::ft_8008A2BC,
  refs/melee/src/melee/ft/ft_081B.c::{ft_80084F3C,ft_80084104}).
- `DownWait* -> DownStand*` stays on `ftCo_800980BC`'s explicit `input.x668 & HSD_PAD_LR` /
  analog-trigger edge owner; physical Z alone does not enter DownStand.
- DownDamage contact preserves the downed victim's visible facing from `ftCo_8009F184`, but
  `ftCo_8008DCE0` uses the collision-owned `dmg.facing_dir_1` lane for knockback velocity. Reverse
  shine on a downed victim therefore can launch opposite the victim's visible downed facing.
- DamageFly no-tech wall/ceiling contact now follows the FlyReflect owner:
  `ftCo_DamageFly_Coll` checks wall tech first, then `ftCo_800C17CC`; `ftCo_800C18A8` mirrors
  self+KB velocity across the wall/ceiling normal, scales by p_ftCommonData->x1BC, enters
  FlyReflectWall/Ceil, seeds the x18 repeat-reflect lockout from x1C0, and starts the x1990
  colanim hit-status timer from x1B8. Runtime loads x1B0/x1B8/x1BC/x1C0 from
  `data/common/ft_common_data.json`.
- Common grounded Appeal/Taunt admission is a normal IASA edge owner, not a match-flow special case.
  Wait/Walk/Turn/Squat/Landing/Ottotto callbacks call `ftCo_800DE9D8` after guard and before
  jump/dash/locomotion; `ftCo_800DE9B8` consumes `input.x668 & HSD_PAD_DPADUP`, so held D-Pad Up
  without a fresh edge does not enter Appeal. `ftCo_800DEAE8` selects AppealSL only when the left
  animation exists; Fox/Falco FD extracted anim data has no common AppealSL timeline, so facing-left
  rows still enter common AppealSR. HVG `588..619` locks the rollout branch.
- Late JumpAerial -> EscapeAir floor admission consumes CollData.desired_ecb.bottom as a one-frame
  `EscapeAir_Coll` publication owner when the post-Anim JumpAerial IASA sweep remains above a
  non-platform floor and the following EscapeAir callback crosses that desired bottom through the
  floor. Runtime preserves the live JumpAerial desired-bottom owner across the EscapeAir entry even
  when the visible carried floor id is stale/off-domain, then allows the following Yoshi's Story
  sloped ledge floor crossing to publish `LandingFallSpecial` through `ft_80082C74`. Sustained
  EscapeAir continuations cannot reuse that source slice unless the current callback reestablishes a
  fresh desired-bottom crossing. The 182447 frozen-PS replay and CNM Yoshi rollout locks cover the
  positive `EscapeAir -> LandingFallSpecial` floor rows.
- Airborne `DownBound*` floor-loss uses the active CollData.floor segment, not a stage side-ledge
  proxy. `ftCo_DownBound_Coll -> ft_80082708 -> mpColl_8004B108` can enter Fall from ordinary
  side-ledge floor loss and generated stage-object platform endpoints, but a soft-platform
  CollData.floor row must stay on platform geometry until platform contact/endpoint ownership is
  reestablished. Yoshi's Story soft-platform rows therefore do not consume the generic stage
  side-ledge helper merely because the root X is near the main stage ledge; CNM `6944` and FoD MGS
  `3782` lock the platform-vs-stage-object boundary.
- DeadUpFallHitCamera publishes `fp->x221F_b1` at the phase-3 expiry boundary, the same
  `ftCo_DeadUpFall_Anim` case that calls `ftCo_800D34E0` for stock loss. MSL uses the shared
  DeadUpFall phase countdown from `data/common/ft_common_data.json` so the post-frame that first
  loses stock also carries x221F_b1; the preceding phase-3 hold stays clear.
- Top-blast DeadUpFall vs DeadUpStar selection remains exact RNG-stream-phase debt. The source gate
  is `ftCo_800D3158` using `HSD_Randi(100)+1` and `Camera_8003010C`; deterministic pre-gate
  top-blast DamageFlyTop state is modeled, but replay-exact HSD RNG phase is outside RL 1.0
  rollout closure. Reviewed residual rows are kept in `replays/validation_exceptions.json` as
  report-level approved exceptions, not simulator behavior.
- Source anchors:
  - `refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}`
  - `refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}`
  - `refs/melee/src/melee/ft/ftdynamics.c::{ftCo_8009DD94,ftCo_8009E318}`
  - `refs/melee/src/melee/ft/ftanim.c::{ftAnim_8006E9B4,ftAnim_8006E7B8,ftAnim_8006EED4}`
  - `refs/melee/src/sysdolphin/baselib/jobj.c::{HSD_JObjAnim,HSD_JObjSetupMatrixSub,HSD_JObjMakeMatrix}`
  - `refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC`
  - `refs/melee/src/melee/ft/fighter.c::Fighter_80068E64`
  - `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{ftFox_SpecialHi_RotateModel,ftFx_SpecialAirHi_Coll}`
  - `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_OnExitHitlag,ftCo_DamageFly_Coll,ftCo_8008E5A4}`
  - `refs/melee/src/melee/ft/chara/ftCommon/ftCo_FlyReflect.c::{ftCo_800C15F4,ftCo_800C17CC,ftCo_800C18A8}`
  - `refs/melee/src/melee/ft/ft_0DF0.c::{ftCo_800DEF38,ftCo_800DF0D0}`
  - `refs/melee/src/melee/ft/ftaction.c::ftAction_80073008`
  - `refs/melee/src/melee/ft/ftcoll.c::ftColl_8007ABD0`
  - `refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::{ftCo_DownDamage_Anim,ftCo_8009F184}`
  - `refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::{ftCo_80097F38,ftCo_DownWait_IASA,ftCo_DownBound_Anim}`
  - `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Down.c::{ftCo_80098400,ftCo_Down_CheckInput}`
  - `refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate,Fighter_Spaghetti_8006AD10,Fighter_Spaghetti_8006AD10_Inner1}`
  - `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Anim`
  - `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardOn_Coll`
  - `refs/melee/src/melee/ft/ft_081B.c::ft_800845B4`
  - `refs/melee/src/melee/ft/chara/ftCommon/ftCo_MissFoot.c::ftCo_8009F39C`
  - `refs/melee/src/melee/ft/chara/ftCommon/ftCo_AppealS.c::{ftCo_800DE9B8,ftCo_800DE9D8,ftCo_800DEAE8,ftCo_AppealS_Anim}`
  - `refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_procMap}`
  - `refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA`
  - `refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll`
  - `refs/melee/src/melee/ft/ft_081B.c::ft_80082C74`
  - `refs/melee/src/melee/ft/ft_0D31.c::{ftCo_DeadUpFall_Anim,ftCo_800D3158,ftCo_800D34E0}`
  - `refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpColl_80044E10_RightWall,mpColl_800454A4_RightWall,mpColl_80045B74_LeftWall,mpColl_80046224_LeftWall}`
  - `refs/melee/src/melee/gr/forward.h::{FLATZONE,LAST}`
