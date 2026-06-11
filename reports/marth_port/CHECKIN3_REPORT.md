# Check-in 3 Report: Marth specials from decomp first principles

State: uncommitted on `newchar` (pending review approval).
Implemented in order: ftMarthAttributes extraction -> Dolphin Slash -> Shield Breaker ->
Dancing Blade -> Counter. One checkpoint, slices reported separately below.

## 1. Extracted ftMarthAttributes (MarsAttributes ext block) and consumers

Extraction: tools/extraction/extract_character_attrs.py gains a per-character "ext-attr layout"
tag (marth = "mars_sword"; reusable for Roy). 28 keys written into data/characters/marth.json
with mechanic-position names; parsed into MslCharParams (src/char_params.{h,c}) as
optional-with-defaults so other characters are unaffected.

| Field (offset) | Value | Consumer |
|---|---|---|
| specialn_charge_max_seconds (x0) | 4 | SB loop force-release (marth_specials.c loop anim) |
| specialn_release_damage_base (x4) | 7 | SB End0 hitbox damage override (hitboxes.c) |
| specialn_release_damage_per_second (x8) | 5 | same (base + seconds * per_second) |
| specialn_entry_vel_divisor (xC) | 1.25 | SB entry velocity divide |
| specialn_start_friction (x10) | ... | SB ground/air friction (marth_specials_phys) |
| specials_air_entry_vel_x_divisor (x14) | ... | DB air entry vel.x divide |
| specials_air_friction (x18) | ... | DB air friction |
| specials_air_entry_vel_y (x1C) | ... | DB first-air-swing hop (x222C gate) |
| specials_fall_accel / terminal_vel (x20/x24) | ... | DB air fall |
| specialhi_freefall_mobility_mul (x28) | ... | DS post-launch drift + FallSpecial mobility |
| specialhi_landing_lag_frames (x2C) | 34 | DS LandingFallSpecial rate (locomotion + anim_timebase) |
| specialhi_breverse_stick_threshold (x30) | ... | DS B-reverse (script throw-flags window) |
| specialhi_angle_stick_threshold / max_degrees (x34/x38) | ... / 20 | DS launch angle tilt |
| specialhi_air_entry_vel_x_mul (x3C) | ... | DS air entry vel.x |
| specialhi_launch_decay_mul (x40) | ... | DS air-variant launch decay |
| specialhi_fall_accel / terminal_vel (x44/x48) | ... | DS post-launch fall |
| speciallw_air_entry_vel_x_divisor (x4C) | ... | Counter air entry |
| speciallw_air_friction / fall_accel / terminal_vel (x50/x54/x58) | ... | Counter air phys |
| speciallw_counter_damage_mul (x5C) | 1.0 | countered-damage stash (Roy/Emblem LwHit override path) |
| speciallw_counter_shield_strength (x60) | 11.0 | extracted; not yet consumed (shield_unk lanes) |
| speciallw_counter_desc_bone/offset/size (x64..x74) | 3 / (0,-0.5,0) / 8.5 | extracted; intercept currently uses hurtcap contact (descriptor geometry deferred) |

Also extracted: chain submotions via extract_special_msids `extra_script_msids`
(marth: 298, 302, 304-311, 313-320 = SpecialNEnd1 + all Dancing Blade stages), scoped OFF for
fox/falco (their validated runtime consumes the absence of those leftover scripts - HIS:562 lock).
All 32 marth special movescripts now extracted (hitboxes, set_cmd_var windows, throw-flags).

## 2. Implementation per family (decomp anchors in src/marth_specials.c)

- Dolphin Slash (367/368): entry vel mods; pre-launch IASA angle tilt (stick beyond x34 scales
  up to x38 degrees) and B-reverse on the script throw-flags window; launch velocity =
  anim TransN root-motion delta rotated by the stick angle (ft_80085154) with facing alignment
  and the x40 air decay; descent phase fall(x44,x48) + drift*x28; anim end ->
  FallSpecial(x28,x2C); landing -> LandingFallSpecial at the x2C=34-frame rate.
- Shield Breaker (341-348): entry divide; Start->Loop on anim end; loop charge counter
  (specialn_charge_frames); B release -> End0 with damage override base+sec*per_sec applied at
  hitbox spawn (hitboxes.c); forced End1 (full-charge anim) past 4s; ground/air variants.
- Dancing Blade (349-366): 4-stage chain; per-stage advance window = the stage movescript's
  cmd0 pulses (move_tables_special_cmd_var_value_at_frame); early-press lockout (cmd1);
  stage 2 Hi/Lw, stages 3-4 Hi/S/Lw variant select by stick at press; first-air-swing hop
  gated by specials_air_used (fv.ms.x222C); air fall/friction from specials_* attrs.
- Counter (369-372): window = the movescript's cmd1 pulses ([5,30) ground); armed window
  intercepts fighter BODY contacts in combat.c THROUGH THE EXTRACTED DESCRIPTOR GEOMETRY:
  the AbsorbDesc sphere (bone 3, offset (0,-0.5,0), r=8.5) is posed at the defender's live
  animation via anim_pose_get_matrix and tested against the attacking HitCapsule
  (marth_counter_desc_overlaps_hitbox); on overlap both sides take standard CalcHitlag hitlag,
  marth takes no damage/KB, stores dmg*x5C, faces the attacker, enters LwHit (370/372);
  counterattack damage stays script-authored (the x5C override is the Roy/Emblem branch);
  sibling same-swing capsules consumed; outside the window (or off the sphere) marth is hit
  normally. Back-hit interception verified by test (bone-centered sphere, not facing-gated).
  FAIL-CLOSED: missing descriptor data or an unposeable bone returns no-intercept (no silent
  fall-back to body-contact admission); proven by test with a Slippi no-submotion sentinel
  seed. The descriptor bone (part 3) is now included in the anim pose extraction's needed-part
  set (extract_fighter_anims; marth pose table 21 -> 22 joints) so live counter rows always
  pose - the closed path is reachable only from degenerate seeds.

## 3. Generic refactors vs Marth-specific code

Generic refactors (benefit every future character):
1. THE BIG ONE - shared action-id range gating: fox/falco specials (MSL_ACT_FX_*, 341-372)
   collide numerically with every other character's specials. Added
   msl_char_id_is_spacie() (char_registry.h) and completed a FULL AUDIT of every
   MSL_ACT_FX_* runtime predicate in src/ (see the audit table below). Without this,
   marth's specials were being driven by fox's Illusion/Firefox/Shine state machines.
2. move_tables: cmd-var value-0 pulse caching + move_tables_special_cmd_var_value_at_frame
   (script-owned open/close windows) + move_tables_special_throw_flags_window. Any future
   char's script-windowed mechanics ride these.
3. extract_special_msids extra_script_msids channel (multi-stage special chains).
4. extract_character_attrs ext-attr layout registry (per-char special-attr struct families).
5. Exported msl_locomotion_enter_fall_special_via_ftco_80096900 (custom-lag FallSpecial entry).
6. anim_timebase LandingFallSpecial entry-rate re-derivation honors the live prev-action lane
   (stale seed_prev no longer clobbers a live FallSpecial chain's forwarded lag) - marth-scoped
   to protect validated spacie lock behavior.
7. Up-special preempts tap-jump in jump_input_from_edges (decomp Wait_IASA ordering) - generic.
8. New generic state lanes: special_cmd0/1/2, specialn_charge_frames, speciallw_countered_damage,
   special_stick_angle, specials_air_used, fallspecial_mobility_mul, speciallw_counter_window.

Marth-specific code (all decomp-backed, refs/melee/.../ftMars/):
- src/marth_specials.{c,h} (the four families' state machines, phys, entry dispatch).
- hitboxes.c SB End0 damage override block; combat.c Counter intercept pair
  (marth_counter_intercepts_contact / marth_counter_trigger); locomotion.c +
  anim_timebase.c DS landing-lag cases. Each gated on MSL_CHAR_ID_MARTH.

## 4. Tests (tests/test_marth_specials.py - 17, all decomp-expectation-driven)

DS: ground launch+FallSpecial handoff; air launch + LandingFallSpecial duration == 34;
air entry vel multiplier. SB: ground quick release End0->Wait; full-charge forces End1
(never End0); charge-scaled damage on a live victim lands on the base+5/s ladder; air family.
DB: all-four-stage chain with per-stage data-driven windows; stage-2 Hi variant; stage-3 Lw
variant; no chain without input; air first-swing hop == specials_air_entry_vel_y.
Counter (5 tests): window pulses sane; states enter/exit; **trigger inside window** (no damage
to marth, LwHit entered, counterattack damages fox); **whiff outside window** (marth hit
normally); **return damage is script-authored**; **back hit countered** (descriptor sphere is
bone-centered, not facing-gated). 20 specials tests total (back-hit + fail-closed added).

## 5. Metrics

Marth replay smoke (verification only):
- one-step discrete: 684 -> 241 / 1,079,562 (0.022%)
- rollout first-mismatches: 404 -> 55; median streak 75 -> 231

Fox/Falco validate-all vs HEAD: "no suite total changes", no replay-level regressions,
no reds of any class - byte-stable through all the shared-code FX gating.

## 6. Gates

- build_data clean regen: byte-identical (diff -rq vs pre-regen backup: 0 lines)
- make build BUILD_FORCE=1: pass
- coverage suites: 84 common-action + 17 specials = 101 passed
- full make test: 3246 passed, 1183 skipped
- fox/falco validate-all + validation_report_diff: clean (above)
- make fmt-check: pass; git diff --check && --cached --check: clean

## MSL_ACT_FX_* audit (complete; every runtime site classified)

Method: static scan of all src/*.c for MSL_ACT_FX_* references, classified per enclosing
function. Verdicts:

| File | Disposition |
|---|---|
| locomotion.c | GATED: landing selector branches (AirCatchHit chain, AirSEnd, HiFall, AirHi rebound, SEnd floor-loss, HiLanding), shine platform-pass floor-skip, both Illusion state-machine clusters (`ms != NULL && spacie`), side-special transitions + specialhi helpers (fn-top guards), submotion_for_action + landing_contact_y_owner (char param) |
| physics.c | GATED: integrate FX velocity branches (inline), shine-air fall + ground-friction predicates (char param) |
| mpcoll_ground.c | GATED: all 6 specialhi/specialairn floor predicates (char param or fn-top), mpcoll_ground_apply inline comparisons, specialhi platform-pass projection (ctx->char_id) |
| mpcoll_wall_ceil.c | GATED: wall-ASDI producer (char param), HiHoldAir ledge-air-coll (local char_id), apply-site inline |
| mpcoll_env.c | GATED: ledge-grab FX clause hoisted |
| combat.c | GATED: 25+ owners (shine pose bridges, reflector sources, residual hitcapsule, speciallw defer pair, defender_hit_status shine entry, damage_enter HiFall, body-damage-log d_idx pair, dense-seed LW_START pair, all damageflyroll_* seed-context owners via a_idx/d_idx identity guards) |
| hitboxes.c | GATED: specialhi pose owner (pre-existing char gate) + seed-bridge victim predicate (char param) |
| hurtboxes.c | GATED: refresh shine-start entry (inline) + pose owner (pre-existing) |
| ledge.c | GATED + MARTH RULE: FX cliffcatch cases now spacie-only; Marth Dolphin Slash grabs ledge only while descending (ftMs_SpecialHi_Coll ft_800831CC branch) |
| anim_timebase.c | GATED: AirNLoop fastfall-wrap clear; LandingFallSpecial entry-rate re-derivation honors live FallSpecial chains (marth-scoped) + marth DS lag source |
| api.c | GATED: firefox-launch victim reseed predicate (char param), shine-start seed bridges x2, rollout clock owner, reseed landing-lag source split (+ marth DS case), debug sampler xrotn |
| state_flags.c | GATED: reflecting-flag owner (fn-top), shine-start platform-pass + side-special x221C clusters (inline) |
| items.c | GATED: laser-defender AirNLoop contact suppression (d_idx can be any char). DOCUMENTED-SAFE: all remaining owners key on the laser/illusion/reflector ARTICLE OWNER, and articles only spawn for spacie owners (has_articles registry; marth spawn paths do not exist) |
| instance_id.c | GATED: blaster Loop->Loop x21EC rule (char param; ftMs_SpecialNLoop installs no callback) |
| blaster.c / shine.c | DOCUMENTED-SAFE: every update loop begins with an is_fox_falco gate (blaster.c:850/871/1174, shine.c:571); enter_*/predicates are static and only reachable from those gated loops or from already char-gated callers |
| hitlist.c | GATED: specialhi attacker predicate (char param) |
| action_ids.h | GATED: msl_action_allows_fastfall FX AirN-family + HiFall cases (char param; Marth's air SB uses plain ftCommon_Fall, no fastfall); msl_action_owns_x2219_collision_skip carries only Dead*/Rebirth common ids (no FX, false positive) |
| damage_terminal_owner.h | GATED: firefox-launch victim predicate (char param) + the three damageflyroll pre-action FX cases (LW_END / AIR_HI / HI_FALL, case-local d_idx char checks) |
| specialhi_pose.h | GATED: msl_specialhi_rotate_model_action (char param; callers in fighter_callbacks.c, api.c x3, bindings/msl_preprocess_native.c updated) |
| shielddesc_geometry.h | GATED: shine-start enable-edge shield-extent lane (a_idx inline) |
| reflector_bubbles.c | GATED: local action_is_shine (char param) + both shine-start pass-reflecting clusters (inline) |
| items.c (2nd pass) | BOUNDARY-ENCODED: illusion_owner_motion_is_active, illusion_spawn_from_fighter, laser_airborne_damagefall (o_idx), item_try_shine_reflect_contact + item_reflector_owner_is_shine_callback_state (reflector owner), action_is_illusion_dash/end/setphys + article-spawn predicates (char param); the owner-spacie article invariant is now enforced at every helper boundary, not just documented |

Verification: fox/falco validate-all after the full audit (src/*.c AND src/*.h AND bindings) =
"no suite total changes", no reds (every added gate is pass-through for spacie rows); full
suite 3248 passed.

## Known deferrals
- speciallw_counter_shield_strength (x60 -> shield_unk0/1) extracted, not consumed (no
  shieldstun-analog lane consumer yet; the intercept handles hitlag directly).
- Projectile countering: the intercept hooks fighter-vs-fighter BODY contacts; item (laser)
  contacts vs a countering Marth are not yet routed through the descriptor (replay has no such
  rows; burn-phase candidate with a synthetic test).
- Counter sphere vs swept (prev->cur) hitbox segment: current test uses same-frame positions;
  the body admission upstream already did swept hurtcap overlap, so the descriptor check is a
  same-frame refinement on admitted contacts.
- DB ground stop-at-ledge (StopAtLedge coll) and mid-stage ground<->air msid swaps preserving
  anim frame are not modeled; exits use Wait/Fall. Burn-phase candidates if the replay's 55
  remaining breaks implicate them (replay has air side-B only).
