# Captain Falcon Port Plan

Instantiation of `agent_docs/ADDING_A_CHARACTER.md` for Captain Falcon
(branch `newchar-falcon`). Follow that guide's phases/gates; this file records
the Falcon-specific facts, the port-specific risk areas, and progress.

## Phase 0 — Facts (verified against decomp/repo)

- **Ids**: internal `FTKIND_CAPTAIN = 2`
  (`refs/melee/src/melee/ft/forward.h`), Slippi/CSS external id **0**.
- **Archives**: `PlCa.dat` / `PlCaAJ.dat` — NOT yet in `_iso/`; extract from
  `SSBM.iso` via `tools/extraction/iso_extract.py`.
- **Decomp anchors**: `refs/melee/src/melee/ft/chara/ftCaptain/`,
  prefix `ftCa_`, ftData symbol `ftDataCaptain` (verify exact symbol name in
  the archive), own submotion enum `ftCa_SM_*` in `ftCaptain/forward.h`
  (Falcon is a donor — Ganon is his clone, not the reverse).
- **Articles**: none. `has_articles=False`,
  `exports_item_article_constants=False`.
- **Ext-attrs**: `ftCaptain_DatAttrs`
  (`refs/melee/src/melee/ft/chara/ftCaptain/types.h`) — 35-field layout
  (`specialn_*`, `specials_*`, `specialhi_*`, `speciallw_*`); hand-write the
  layout in `extract_character_attrs.py` like `MarsAttributes`.
- **Specials inventory** (17 char MotionStates from `ftCo_MS_Count`, plus the
  6 item-swing states):
  - `SpecialN` / `SpecialAirN` — Falcon Punch (stick-Y angle-diff attrs).
  - `SpecialSStart` / `SpecialS` / `SpecialAirSStart` / `SpecialAirS` —
    Raptor Boost: hit/miss branching, own grav/terminal-vel attrs, air-miss
    ends in FallSpecial (audit the `ftCo_80096900` callsite args per the
    FallSpecial `mv.co.fallspecial.xC` trap), separate miss/hit landing lag.
  - `SpecialHi` / `SpecialAirHi` / `SpecialHiCatch` / `SpecialHiThrow` /
    `SpecialHiThrow1` — Falcon Dive: **command grab**. Victims enter common
    `ftCo_MS_CaptureCaptain` / `ftCo_MS_ThrownFF`. No ported character has a
    catch-special; expect real extension of shared capture/attach/throw
    owners (`grab_attachment.c`, `throw_flow.c`), not just registry plumbing.
    Explosion release + freefall + regrab.
  - `SpecialLw` / `SpecialLwEnd` / `SpecialAirLw` / `SpecialAirLwEnd` /
    `SpecialLwEndAir` / `SpecialAirLwEndAir` — Falcon Kick: ground<->air
    crossings are distinct action states; per-state collision-callback audit
    (Sheik Chain lesson) applies directly.

## Phase 1 — Registry + data pipeline

Gate: `build_data` end-to-end; existing chars' data binaries byte-identical.

1. Registry rows: `tools/extraction/char_registry.py` `CharInfo(falcon)`,
   `src/char_registry.h` mirror, `src/ids.h` `MSL_CHAR_ID_FALCON = 2`.
2. Full `uv run python -m tools.extraction.build_data` (never keep/commit a
   `--chars` subset tree). Per-extractor watch items:
   - `extract_character_attrs.py`: the `ftCaptain_DatAttrs` layout.
   - `extract_fighter_anims.py`: anim count entry; check dynamic-chain block
     (verify whether any Falcon hurtcaps ride chain bones).
   - `extract_fighter_parts.py`: Falcon Dive catch geometry bones must be in
     the extracted pose set.
   - `extract_attack_id_move_id.py`: verify no merged-table id collisions.
   - motion-state owners: expect callback-id renumber churn (by design).
3. C loaders loop `MSL_CHAR_REGISTRY`; add Falcon ext keys to `char_params.c`
   as per-char optionals with defaults + loud diagnostics.
4. `tools/slippi/action_state_tables.py` + history helpers (article-less
   pass-throughs).

## Phase 2 — Replays + baseline

- NEED FROM USER: 2-3 `.slp` replays with Falcon on different legal stages.
- Intake recipe (verified against the marth/sheik suites):
  1. Drop `.slp` files under `replays/validation/falcon/`.
  2. Write `replays/suites/falcon.json`: `name`, `notes`, `ucf_enabled`,
     `ucf_cardinals_1_0_enabled`, and per-replay `{replay: <.slpz path>,
     ports: [..], stage_id, characters: {port: Name}}` (see
     `replays/suites/marth.json`). External char name "Captain Falcon"
     (Slippi external id 0).
  3. `make slpz-convert-suite SUITE=replays/suites/falcon.json` converts
     `.slp` -> `.slpz` in place.
  4. `make validate SUITE=replays/suites/falcon.json` for one-step numbers;
     record baseline (zero char-specific C) in this file + action inventory.
- Record zero-char-C baseline (one-step mismatch %, rollout first-breaks) and
  the replay action inventory; everything missing (likely Falcon Punch,
  landed Falcon Dive) is decomp-tests + webplay-only verification.

## Phase 3 — Common-action coverage

Remove falcon from the exclude set in
`tests/test_char_common_action_coverage.py` (~84 tests). Expect new
stage-geometry/ECB boundaries rather than the already-fixed hardcode classes;
consult the trap catalog in ADDING_A_CHARACTER.md Phase 3 when rows diverge.

## Phase 4 — Specials, decomp-first

IMPORTANT ORDERING: do not land falcon runtime C until the Phase 2 replay
baseline (zero char-specific C) has been recorded.

1. `src/falcon_specials.c/.h` wired via `fighter_callbacks.c` / `action.c`.
2. **Action-id collision audit**: every `MSL_ACT_*`-keyed site in `src/*.c`,
   headers, bindings gated/table-widened; produce the classified table.

   AUDIT RESULT (2026-07-06, pre-specials): no falcon-hazardous char-blind
   sites remain. All char-range (341+) action-id sites in src/ + bindings/
   classified:
   - Positively char-gated (fail-closed early returns):
     `marth_specials.c`, `sheik_specials.c`, `combat.c` Counter intercepts
     (`marth_counter_intercepts_contact` requires `MSL_CHAR_ID_MARTH`;
     descriptor geometry FAILS CLOSED), `combat_body.c` (per-char guards),
     `state_flags.c` (Marth gate at :101, fx-kind switch at :154),
     `items_sheik.c` (caller gated on `char_id == SHEIK` at combat.c:1238;
     chain sites gated by sheik-chain item provenance lanes),
     `reflector_bubbles.c` (spacie predicate at :17).
   - Table-backed via MSLMSO01 `fx_special_kind` (falcon lanes all zero =>
     no special semantics until Phase 4 assigns them):
     `action_ids.h::msl_action_allows_fastfall` default branch,
     `damage_terminal_owner.h`, `specialhi_pose.h`, `shielddesc_geometry.h`,
     `instance_id.c`, `anim_timebase.c`, `physics.c`, `hitboxes.c`,
     `hitlist.c`, `ledge.c`, `locomotion.c`, `locomotion_landing.c`,
     `combat_damageflyroll.c`.
   - Documented-safe lookup keys (not predicates): `api.c:412-425` attack-id
     table lookups keyed by spacie-only laser/illusion item families.
   - Verified live: falcon B-press ground/air no-ops safely (stays
     Wait/Fall; stick falls through to tap-jump), so pre-specials falcon
     rows cannot enter 341+ except via replay reseed, and every consumer
     above fails closed for those rows.
   When implementing falcon specials, extend `extract_motion_state_owners`
   fx_special_kind/class lanes (or falcon-kind analogues) rather than adding
   raw action-id compares; the fastfall/FallSpecial/pose surfaces above then
   pick up falcon rows through data.
3. Verify `ms_b_entry_mask` B-dispatch surface; wire grounded phys owners
   (`ft_80084F3C` / `ft_80084FA8`) for Falcon Kick / Raptor Boost.
4. Ground<->air swaps per family; Falcon Kick's explicit crossover states
   need their `MSLMSO01` callback classes locked.
5. **Falcon Dive catch/throw** — the genuinely new mechanic; touches shared
   capture/throw owners, so existing-char byte-stability after every change
   is the safety net. Audit `ftCa_SpecialHi.c` (`ftCo_AirCatchHit_Coll`,
   `doCatchAnim`, catch gravity attr), victim `CaptureCaptain` attach,
   `SpecialHiThrow/Throw1` release + explosion hit, victim `ThrownFF`, regrab.
6. Decomp-anchored unit tests per special, positives AND negatives
   (Marth scale: ~130 char tests).

### Specials decomp study (2026-07-06, full read of ftCa_Special{N,S,Hi,Lw}.c)

Common facts: all four families clear `cmd_vars`/`throw_flags` on entry; all
gfx (`efSync/efAsync`, `fv.ca.during_specials*` latches, `x2219_b0` flame/wind
latch, take_dmg/death2 cb `ftCa_Init_800E28C8` = gfx cleanup) are cosmetic-only
EXCEPT where noted. Every `ftCo_80096900` callsite passes `arg1=1`, so
`fallspecial_xc0_source_fx_kind_mask` stays 0 for falcon (ordinary FallSpecial
terminal clamp).

**SpecialN — Falcon Punch** (simplest; no hidden mv lanes):
- Air IASA consumes a `cmd_vars[0]` script pulse -> one-shot velocity impulse:
  `vel = specialn_vel_x * (sin, facing*cos)(angle)`, angle from stick-y clamped
  to [range_y_neg, range_y_pos], scaled by `specialn_angle_diff` deg->rad.
- Air Phys switches on `cmd_vars[1]`: 0 -> `ft_80084EEC` drift; 1 ->
  `self_vel *= specialn_vel_mul` per frame; 2 -> `ft_80084DB0` (fastfall).
- Ground Phys `ft_80084FA8`; ground Anim end -> `ft_8008A2BC` (Wait_IASA tail
  applies); air Anim end -> Fall.
- Coll: frame-preserving ground<->air swaps at `cur_anim_frame` (transition
  flags keep gfx/cmd state).
- `throw_flags_b1` pulses in Phys are gfx-only (punch flash), but the second
  pulse calls `ftCommon_8007DB24` — audit what that does before dismissing.

**SpecialS — Raptor Boost** (needs a NEW substrate: `hurtbox_detect_cb`):
- Start (gr/air) arms `fp->hurtbox_detect_cb = ftCa_SpecialS_OnDetect` while
  script `cmd_vars[0] != 0`. On the attacker's hitbox contacting a FIGHTER
  (or item kinds in source ranges) it transitions Start->S / AirStart->AirS
  (ground: `gr_vel *= specials_gr_vel_x`, vel.y=z=0; air: vel.z=0). The sim
  has no attacker-contact-driven state-transition mechanism today; this is
  the Raptor Boost hit/miss branch.
- Entry zeroes self_vel and gr_vel (ground) — common `SpecialS_CheckInput`
  pre-writes need the Marth LoudDullGoat audit treatment.
- Air Phys: `ft_80085134` + private `mv.ca.specials.grav` accumulator
  (`specials_grav`/`specials_terminal_vel`) written to self_vel.y (AirStart
  only when `cmd_vars[1]==1`; AirS unconditional). Seed lane candidate.
- Ends: AirStart/AirS Anim end -> `ftCo_80096900(...,1, miss/hit lag)`;
  ground Start Coll `cmd_vars[2]==0` -> `ft_80084104` (no-pass coll),
  else off-edge -> FallSpecial(miss lag); wall in facing dir + cmd0==1 ->
  stop at wall (`ft_8008A2BC`). S_Coll off-edge -> FallSpecial(hit lag).
  AirStart/AirS Coll landing -> LandingFallSpecial(miss/hit lag); AirS also
  writes `gr_vel = self_vel.x` before landing.

**SpecialHi — Falcon Dive** (command grab; touches shared capture/throw):
- Enter installs `x21EC` reset helper (`jumpsUsed=max_jumps`,
  `mv.ca.specialhi = {x0: specialhi_air_var, vel: 0, b0/b1: false}`,
  `cmd_vars[1] = specialhi_unk2`) and arms catch bubbles via
  `ftCommon_8007E2D0(fp, 2, onCatch=ftCa_SpecialLw_800E5128, NULL,
  ftCo_8009CA0C)`.
- IASA cmd0 pulse -> `x2_b1 = true` (launch begun) + B-reverse when
  `|stick.x| > specialhi_input_var` (UpdateFacing + rotY flip).
- Phys is TransN/anim-root-motion driven: `mv.vel` lane accumulates
  `x74_anim_vel`, drift via `ftCommon_8007D050/8007D3A8` scaled by
  `specialhi_horz_vel` and `specialhi_air_friction_mul`. Seed lanes:
  `mv.ca.specialhi.vel.{x,y}`, `x2_b0/b1`, `x0`.
- Coll: landing -> `x2_b1 ? LandingFallSpecial(specialhi_landing_lag) :
  ft_80083B68`; ledge grab ONLY when `x2_b1` (ftCliffCommon_80081298/370).
- onCatch: attacker -> SpecialHiCatch; `ftCommon_8007E2F4(fp,511)` /
  `8007E2FC`; grounded victim: `ftCo_800DB368(vic, fp)` + `x221B_b7=true` +
  accessory4 cb snaps ATTACKER pos to victim pos each frame; air victim: no
  snap (`x221B_b7=false`). Victim enters common `CaptureCaptain`.
- Catch Anim end -> doCatchAnim: attacker -> SpecialHiThrow (Throw0),
  `ftCo_800DE2A8` + `ftCo_800DE7C0(vic,0,0)` (victim -> ThrownFF explosion
  release). Throw0 Anim end -> **Fall** (not freefall: this is why Dive
  renews after a connect). Throw0 Phys: after cmd0 pulse (`x2_b0`) runs
  SpecialHi_Phys + `specialhi_catch_grav` fall blend; Coll landing ->
  LandingFallSpecial(specialhi_landing_lag).
- Whiff Anim end -> `ftCo_80096900(...,
  specialhi_freefall_air_spd_mul, specialhi_landing_lag)` (freefall).

**SpecialLw — Falcon Kick** (6 states; ground<->air is phase-flip AND
state-swap depending on segment):
- Ground enter: `mv.ca.speciallw = {x0: 0, friction: 1.0}`,
  `deal_dmg_cb = ftCa_SpecialHi_800E400C`: each dealt hit (up to
  `speciallw_unk2` hits) does `friction *= speciallw_on_hit_spd_modifier`
  (the on-hit slowdown). deal_dmg_cb is a second NEW substrate need.
- Phys applies `self_vel *= mv.friction` each frame; ground uses
  `ftCommon_8007E5AC + ft_80085088` (slope-following), air branch inside the
  same action uses `ftPartSetRotZ(0) + ft_80085134` — the travel action
  CONTINUES across walk-offs via `8007D5D4/8007D7FC` phase flips (no motion
  state change), unlike most specials.
- Travel Anim end: grounded -> SpecialLwEnd (anim rate
  `speciallw_ground_lag_mul`); airborne -> SpecialLwEndAir. Air travel
  (AirLw) Anim end -> AirLwEndAir -> Fall; AirLw Coll landing -> AirLwEnd at
  rate `speciallw_landing_lag_mul`.
- End-state Phys friction owners: LwEnd ground `cmd_vars[2] ?
  speciallw_ground_traction * gr_friction : ft_80084F3C`; AirLwEnd
  `speciallw_air_landing_traction`; LwEndAir air `cmd_vars[0] ? drift :
  no-drift`.
- **Wall rebound**: grounded travel Coll with `cmd_vars[0] != 0` and
  wall-hug in the facing direction -> `ftCa_MS_SpecialHiThrow1` (the
  backflip; MF name `SpecialLwRebound`). Throw1: Anim end -> Fall, Phys
  `ft_80085134`, Coll = `ftCo_AirCatchHit_Coll`. Despite the name it is a
  Falcon Kick state, not a throw.

**New shared substrate needed (sequence carefully, existing chars byte-stable):**
1. `hurtbox_detect_cb` analogue (attacker contact -> state transition) for
   Raptor Boost.
2. `deal_dmg_cb` analogue (on-hit mv mutation) for Falcon Kick slowdown.
3. Catch-special flow: `ftCommon_8007E2D0` catch-bubble arming, victim
   `CaptureCaptain` capture family, attacker/victim position anchoring
   (accessory4), `ThrownFF` release. Check what `grab_attachment.c` /
   `throw_flow.c` already express before adding owners.
4. Seed lanes for replay reconstruction: `mv.ca.specials.grav`,
   `mv.ca.specialhi.{vel,x0,x2_b0,x2_b1}`, `mv.ca.speciallw.{x0,friction}`,
   plus the `x221B_b7` anchor bit and cmd-var script windows (MSLFTSC1).

## Phase 5 / 5.5 — Replay burn-down, Dolphin ground truth

Classify remaining rows char-boundary vs shared debt. When analysis ties,
use the pinned `refs/Ishiiruka` probe workflow (NEED FROM USER: working
Dolphin build environment if probes become necessary).

## Phase 6 — Live-path verification

- Viewer: `tools/viewer/assets/character_zips.tsv` (NEED: falcon Slippi
  renderer zip + sha), `tools/viewer/live/schema.js`, `build_wasm.sh`
  `viewer_chars`, main.js wiring; keep the live-viewer schema tests green.
- Play every special ground/air/ledge/slope/vs-shield; capture msltrace on
  anything weird. Falcon Dive at ledge and Falcon Kick at walk-offs are the
  obvious live-only risk spots.
- `tools/eval/fuzz_live_clip.py --mode sweep --matrix` must exit 0.

## Phase 7 — Standing gates (after EVERY retained change)

- `uv run python -m pytest -n 4 -q`
- `make validate-all` + `tools.eval.validation_report_diff` (no unexplained
  existing-char movement; restore unintended report churn deliberately)
- `make fmt` / `make fmt-check`; `git diff --check`
- `make build BUILD_FORCE=1` before final review.

## Progress log

- 2026-07-06: plan written; Phase 0 facts verified.
- 2026-07-06: Phase 1 registry + pipeline done: PlCa* extracted to `_iso/`;
  registry rows (py + C + ids.h); `ftData_Table_Unk0[2].count = 318` anim
  entry; `CAPTAIN_SPECIAL_ATTRS_LAYOUT` (35 keys, `falcon_*`) + layout test;
  fixed `_stable_update` dropping non-legacy ext-attr prefixes. `build_data`
  end-to-end; existing-char data byte-identical except documented
  motion-state callback-id renumber (verified pure via symbol-level diff).
  Source artifacts refreshed (owners all chars + falcon; added missing
  zelda attack_id/staling fallback bins). Binding smoke: falcon vs fox
  init/reseed/step, dash/run/jumpsquat(4f)/jab all correct via generic path.
  Coverage suite auto-included falcon: 82/84 passed immediately; fixed 2
  test-side issues (FD runway too short for Falcon's run speed; shield
  radius expectation ignored 12-frame hold drain — now uses measured hp).
  Repinned EscapeAir_Coll callback id 339->400. Full pytest green except
  environmental failures (missing local replays/heldout + replays/debug
  trees, unrelated to port).
- 2026-07-06: rebased `newchar-falcon` onto origin/dev (19 commits: rollout/
  combat/mpcoll owner fixes, extract_motion_state_owners extension, new
  `escapeair_carried_floor_wall_source` overlay). Regenerated data with dev
  extractors; byte-identity gate re-verified on the new base (attack_id/
  staling bins identical to dev's tracked artifacts; owner churn proven pure
  renumber; EscapeAir_Coll still 400). Fixed one more registry hardcode:
  `test_known_data_artifacts` private characters dir omitted falcon. Suite:
  2091 passed; only failures are the owner-confirmed ignorable environmental
  set (replays/heldout, replays/debug, manual_repros/set13 — repo owner says
  validation replays are enough).
- 2026-07-06: committed Phase 0/1 (`Add Falcon registry data pipeline`).
  Phase 4 prep while replays pending: action-id collision audit complete
  (result recorded above — zero falcon-hazardous sites; fx_special_kind
  table fails closed for falcon rows), Phase 2 intake recipe verified
  turnkey (preprocessing is manifest+registry driven), full specials decomp
  study recorded above. Falcon B-press verified safe no-op live.
  BLOCKED on: falcon .slp replays (user fetching). Do NOT land falcon
  runtime C before the Phase 2 zero-char-C baseline is recorded.
