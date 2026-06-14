# Adding a Character

Exhaustive checklist for porting a new character, distilled from the Marth port
(branch `newchar`, commits `3a2d8813..d25e6882`). Follow the phases in order; each
has a hard gate. The repeated theme: **the pipeline is registry-driven, but every
port so far has found char-blind hardcodes that silently no-op for the new
character** — the verification steps exist to surface those, do not skip them.

Conventions used below: `<char>` is the pipeline name (e.g. `marth`), `ftXx_` the
decomp prefix, `PlXx.dat` the fighter archive.

---

## Phase 0 — Facts and prerequisites

Collect before writing any code:

- **Ids**: Melee internal id (ft/types.h FighterKind order; Fox=1, Marth=18,
  Falco=22) and Slippi/CSS external id (Fox=2, Marth=9, Falco=20). These are
  different id spaces; seeds/binding use internal, replay metadata external.
- **Archives**: `PlXx.dat` + `PlXxAJ.dat` must exist in `_iso/` (extract via
  `tools/extraction/iso_extract.py` if missing).
- **Decomp anchors**: `refs/melee/src/melee/ft/chara/ftXxx/` directory, the
  `ftDataXxx` public symbol, the per-char function prefix, and — for clone
  characters — the **donor submotion enum** (Falco reuses ftFox's `ftFx_SM_*`;
  a clone's submotion_dir/prefix point at the donor).
- **Articles**: does the character spawn items/articles (lasers, projectiles,
  turnips...)? Sets `has_articles` and decides whether the item pipeline needs
  work (Phase 4).
- **Specials inventory**: list every special action state from the decomp
  (`ftCo`-range ids are shared; char ranges start at 341+) with ground/air
  variants, charge states, followups. Marth: SpecialN ShieldBreaker
  (charge, 341-348), SpecialS Dancing Blade (4 stages x 3 directions x air,
  349-366), SpecialHi Dolphin Slash (367-368), SpecialLw Counter (369-372).
- **Validation replays**: ideally 2-3 `.slp` files containing the character on
  different stages (aggregate suites support multiple replays - see
  `replays/suites/aggregate_recent.json`). One replay will NOT cover all moves —
  Marth's single replay had zero ground specials, zero Counter — and every hole
  in the action inventory becomes decomp-tests-plus-webplay-only verification.
  The decomp is the implementation source; replays only verify.

## Phase 1 — Registry + data pipeline (gate: build_data end-to-end, fox/falco bins byte-identical)

1. **Registry rows**:
   - `tools/extraction/char_registry.py`: add a `CharInfo` (all fields above).
   - `src/char_registry.h`: mirror the entry in `MSL_CHAR_REGISTRY`.
   - `src/ids.h`: `MSL_CHAR_ID_<CHAR> = <internal id>`.
2. **Run** `uv run python -m tools.extraction.build_data` (chars default to the full
   registry, so the new registry row is picked up automatically; `--chars` exists only
   for debugging subsets) and chase failures extractor by extractor.
   **PITFALL**: a subset `--chars` run writes `data/manifest.json` without the omitted
   characters. The manifest is the preprocessing source of truth: every per-char map
   (landing lag, walk/run anim rates, friction, owners) silently resolves empty for a
   missing char. Preprocessing now refuses replays whose chars are absent from the
   manifest, but don't commit/keep a subset data tree. Every extractor is registry-driven
   but has per-char caveats:
   - `extract_character_attrs.py` — common attrs come from ftData; the
     **char-specific ext-attr block (`ftData.x4`)** needs an explicit layout
     (Marth: 28-key `MarsAttributes` mars_sword layout). Write the layout from
     the decomp's `ftXxxAttributes` struct; this is hand work per character.
     Also: `damage_post_hitlag_sfx_{mid,high}_num` are per-char literals
     (sfx-only; defaults exist).
   - `extract_fighter_anims.py` — anim count table entry; SSDYNN01 dynamic
     chains are **owner-gated**: chars without collision-owner msids emit an
     empty dyn block. KNOWN LIMIT: the runtime dynamic-chain state contract is
     4 nodes; Marth's cape/hair chains (3 chains, 12 nodes) exceed it and were
     proven cosmetic via a lineage+collider data test. For a character whose
     hurtboxes/collision ride long chains, this contract must be extended.
   - `extract_fighter_hurtcapsules.py` — check whether any hurtcaps sit on
     dynamic-chain bones (Marth: bones 60/70/71); static rest pose may misplace
     them. Prove it harmless (data test) or fix the pose source.
   - `extract_ecb_bottom.py` / `extract_ecb_extents.py` — per-action ECB tables.
     The extra-msid comment blocks (e.g. DamageAir2/3) apply to all chars.
   - `extract_fighter_parts.py` — joint/bone tables. If the character has a
     special needing descriptor geometry (Marth Counter's AbsorbDesc bone),
     make sure that bone is in the extracted pose set (Marth: 22 joints).
   - `extract_fighter_moves.py` + `extract_fighter_script_timeline.py` —
     movescripts; allow_interrupt frames feed the IASA dispatch later.
   - `extract_attack_id_move_id.py` — per-char MF namespaces are merged; verify
     no id collisions in the merged table.
   - `extract_motion_state_owners.py` — registry specs + submotion enums.
     **Callback ids renumber by design** when a char is added; the manifest
     regen in `tests/conftest.py` handles it — expect motion-state-owner test
     churn, not breakage.
   - `extract_special_msids.py`, `extract_shield_tilt_table.py`,
     `extract_staling_move_id.py`, staling weights — straightforward registry
     loops.
   - `extract_item_articles.py` — skipped for article-less chars.
3. **C loaders**: all loop `MSL_CHAR_REGISTRY` (anim_pose, anim_table,
   hitboxes_tables, hurtcaps_tables, shield_tilt_table, char_params,
   special_msids, ecb_tables, staling_tables, script_events, attack_id_tables,
   motion_state_owners, api.c match-init gate). `char_params.c` treats
   char-family-specific keys (e.g. spacie ext keys) as per-char optional with
   defaults + loud parse diagnostics — add the new char's ext keys the same way.
4. **tools/slippi**: `action_state_tables.py` is registry-driven (it was the
   last `{1,22}` hardcode found; the x9_b1 lanes were silently wrong for marth).
   The combat/anim/staling history helpers are char-id-keyed pass-throughs for
   article-less chars; an article-having char needs its article data module
   (cf. `item_article_data.py`).
5. **GATE**: clean-state regen of `data/` from empty produces identical numbers;
   binding `init/destroy` + pose reads for the new char work;
   **fox/falco data binaries are byte-identical** to before your change (this
   catches generalization mistakes immediately and is non-negotiable).

## Phase 2 — Replay intake + first numbers (gate: pipeline runs, baseline documented)

1. `.slp` -> `.slpz` (`make slpz-convert-validation` / `slpz-convert-suite`),
   place under `replays/validation/<suite>/`, add
   `replays/suites/<suite>.json` (ports, chars, stage).
2. `make preprocess SUITE=replays/suites/<suite>.json` into `datasets/`.
3. Engine smoke: init/reseed/step/write_compare on sampled rows.
4. Record the **first numbers** (one-step mismatch count + rollout
   first-breaks/median) with ZERO char-specific C — this is your baseline.
   Marth: 0.107% one-step from generic code alone.
5. Build the **replay action inventory**: which action ids appear, which
   specials are present/missing. This scopes what the replay can and cannot
   verify (everything missing must be verified by decomp-anchored unit tests
   and live play instead). Treat the suite label as provenance, not ownership:
   a new-character replay can still expose an opponent, item, stage, or common
   bug. Always identify the actor `char_id`, MotionState callback, and seed lane
   before assigning a cluster to the new character.

## Phase 3 — Common-action coverage (gate: coverage suite green for the char)

1. Add the character to `tests/test_char_common_action_coverage.py` (the CHARS
   dict is registry-driven minus an exclude set — usually just remove the
   exclusion). ~84 parameterized tests with expectations derived from the
   char's own extracted data: dash/run physics, jumps, aerials, smash charge,
   throws, shield, ledge, tech, etc. This suite IS the porting checklist.
2. Expect to find char-blind hardcodes. The class found for Marth:
   `src/move_tables.c` had a `char_slot` `{1,22}` hardcode plus a 2-entry cache —
   gated dash->run, runbrake, turnrun, jab combos, smash charge, throw
   hitboxes, catchattack, hit_status, hurtbox masks were ALL silently NULL for
   the new char. Grep for fixed-size per-char caches and `{1,22}`/`char_slot`
   patterns when a test fails mysteriously.
   **Script-phase seed lanes are not IASA-phase truth**: replay seed lanes such
   as `runbrake_cmd0` serialize the previous post-frame hidden value. If the
   current frame's Anim/script callback can run before IASA, re-check the
   extracted script table at the already-advanced animation frame before gating
   runtime behavior. Marth exposed this at
   `QuestionableHarmfulPanther.msl:2131:p0`: seed RunBrake frame 14 carried
   `runbrake_cmd0=1`, but the frame-15 `set_cmd_var(0,0)` script event had
   already cleared the `ftCo_RunBrake_IASA -> fn_800C9CEC` TurnRun gate. Add a
   replay negative on the clear frame and an adjacent positive before the clear.
   Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::ftCo_RunBrake_IASA`,
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_TurnRun.c::fn_800C9CEC`, and
   `data/moves/<char>.json::moves.ftCo_SM_RunBrake.events`.
   Keep replay seed reconstruction on raw source-script windows unless the
   hidden source lane itself proves a post-clear latch. Runtime helpers may
   include convenience tails for live callback carry, but copying those tails
   into preprocessing can synthesize stale hidden state. Marth-suite
   `RipeWealthySeahorse` exposed this on Falco `SpecialNLoop`: the live helper
   `move_tables_special_cmd0_active_at_frame()` has a latch-clear tail, while
   the replay seed for `mv.fx.SpecialN.isBlasterLoop` must use the raw MSLFTSC1
   `cmd_var[0]` interval from `ftFx_SpecialNLoop_IASA`.
   Do not create hidden seed latches while source callbacks are suspended by
   hitlag. `Fighter_8006A1BC` decrements hitlag before `Fighter_8006A360`
   resumes non-hitlag callback/physics ownership, so `ftCommon_CheckFallFast`
   cannot set `fp->fall_fast` on frozen `hitlag > 1` frames. A fastfall latch
   may persist if it was already true, and the immediate `hitlag == 1` exit row
   can latch after the decrement. Add a held-down hitlag-exit positive and a
   neutral-input hitlag-exit negative when a new character's attacks expose
   hitlag-frozen aerial rows; Marth exposed the negative at
   `InternalPowerlessWallaby.msl:483:p1`. Source anchors:
   `refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}`,
   `refs/melee/src/melee/ft/ft_081B.c::ft_80084DB0`, and
   `refs/melee/src/melee/ft/ftcommon.c::ftCommon_CheckFallFast`.
   **Common collision owners can be stage-shape-sensitive**: fresh
   `KneeBend -> Jump -> EscapeAir` reaches `EscapeAir_Coll` in the same
   callback, but floor publication is not simply "same floor id still present."
   Downward airdodge handoffs can publish `LandingFallSpecial` through
   `ft_80082C74 -> mpColl_800471F8 -> mpColl_80044838_Floor`; a horizontal
   airdodge already resting at the carried legal-stage floor's root bias stays
   airborne on both soft platforms and hard floors. Add both positives and
   negatives when porting a character whose start positions or ECBs exercise
   different legal-stage floor geometry. Marth exposed this at
   `QuestionableHarmfulPanther.msl:90:p0` on a Dream Land platform and
   `QuestionableHarmfulPanther.msl:3900:p0` on Dream Land's main hard floor;
   existing downward positives include `ParallelTemptingElk.msl:2170:p1` and
   `ShadyDecimalStarling.msl:571:p1`. Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Anim`,
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_IASA`,
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll`,
   and `refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044838_Floor}`.
   **JumpAerial -> EscapeAir first-callback floor checks are floor-graph
   sensitive**: the entry callback can publish fighter-solid ledge/hard-floor
   bottom sweeps using the pre-entry JumpAerial prev ECB and the loaded EscapeAir
   current ECB. Flat non-platform floors are admitted directly. Generated sloped
   ledges are admitted only when the carried floor already names that slope or is
   connected through the extracted floor graph; an unlinked carried floor such as
   a side platform waits until the following EscapeAir callback's runtime floor
   producer. Add flat positives, same/connected-slope positives, and unlinked
   slope-remap negatives when a new character's ECB shape reaches ledges
   differently from Fox/Falco. Marth exposed flat positives at
   `InternalPowerlessWallaby.msl:290:p0`,
   `InternalPowerlessWallaby.msl:4210:p0`,
   `ParallelFamiliarZebra.msl:7821:p0`, and
   `QuestionableHarmfulPanther.msl:4388:p1`; the Yoshi slope boundary is
   `LoudDullGoat.msl:3757..3758:p1` for the unlinked side-platform remap and
   `MetallicUniqueGrouse.msl:{947:p1,1990:p0,3967:p1}` for same/connected floor
   chain positives. Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA`,
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll`,
   `refs/melee/src/melee/ft/ft_081B.c::ft_80082C74`,
   `refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}`,
   and `data/stages/bin/*.bin::MSLSTG01 fighter_solid/is_ledge/floor graph links`.
   **AttackAir floor publication is script-shape and stage-source sensitive**:
   `AttackAir_Coll -> ft_80082C74 -> mpColl_800471F8` may publish
   `LandingAir*` only after the callback has a source-owned
   `mpColl_80044628_Floor` ECB-bottom result. Do not infer a FoD
   height-platform landing only because the seed names a plausible platform
   line and the public root can be snapped there. For single-create fair-style
   scripts, add a positive where a no-current-source FoD height platform stays
   airborne, a positive where a current/same-step platform source lands, and a
   multi-create spacie fair negative so future characters do not inherit the
   pending owner through action id alone. Marth exposed this at
   `InternalPowerlessWallaby.msl:{1550,1966,8480}`; the hard-floor
   `VictoriousSpitefulAlpaca.msl:8175` row is a separate floor-publication
   audit and should not be folded into the height-platform rule. Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll`,
   `refs/melee/src/melee/ft/ft_081B.c::ft_80082C74`,
   `refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}`,
   `data/motion_state/owners/<char>.bin::MSLMSO01 submotion_id`,
   `data/moves/<char>.json::moves.ftCo_SM_AttackAirF.events`, and
   `data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)`.
   **Same-frame JumpAerial -> AttackAir landings consume the pre-entry
   callback ECB**: `JumpAerial_IASA` can enter `AttackAir*` before
   `Fighter_procMap`, but the floor packet may still have been produced by the
   JumpAerial collision callback's `CollData` lifetime. When auditing a new
   character's aerial ECBs, compare both the destination `AttackAir` pose and
   the pre-entry JumpAerial callback ECB before deciding whether a platform
   bottom-sweep should publish `LandingAir*` or stay airborne in the entered
   aerial. Add a no-hit positive where both JumpAerial callback bottoms are
   below the floor, and an adjacent landing control where the JumpAerial
   callback ECB crosses even if the destination aerial ECB is already below.
   Marth-suite WWS exposed the no-hit boundary at
   `WellWornSmallGoshawk.msl:679:p0`; `LawfulInsistentMeerkat.msl:4967:p0`
   remains the legitimate source-ECB landing control. Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::{ftCo_JumpAerial_IASA,ftCo_JumpAerial_Coll}`,
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll`,
   `refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_800835B0}`, and
   `refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}`.
   **Multi-hit AttackAir late tails need the same floor-source audit**: after
   the final `clear_hitboxes` command, `cmd_var[0]` may still be active and the
   action can still enter `LandingAir*`, but FoD height-platform publication
   still needs current/same-step/live platform authority or a callback-local
   bottom/projection floor producer. Do not treat the cmd_var tail as proof that
   a reconstructed platform line may publish a landing. Add a no-current-source
   positive after the final clear, plus current-source negatives on both side
   platforms. Marth exposed this at
   `InternalPowerlessWallaby.msl:{1037,1038}:p1`; current-source landing
   controls are `InternalPowerlessWallaby.msl:{488,3095}:p1`. Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll`,
   `refs/melee/src/melee/ft/ft_081B.c::ft_80082C74`,
   `refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}`,
   `data/motion_state/owners/<char>.bin::MSLMSO01 submotion_id`,
   `data/moves/<char>.json::moves.ftCo_SM_AttackAir*.events`, and
   `data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)`.
   **FallSpecial connected hard-floor publication consumes frame-start fastfall
   provenance**: `FallSpecial_Coll -> ft_80083090 -> mpColl_80047E14` can
   publish `LandingFallSpecial` from a source-owned `CollData.prev_pos` /
   `floor.index` endpoint onto a connected hard floor even when the loaded
   FallSpecial ECB bottom is still above the floor. Do not broaden this into
   "any off-span carried floor lands": fastfall rows need the frame-start
   internal `fp->fall_fast` lane, because Slippi's raw fp+0x221A bit can
   disagree with the derived internal lane and same-frame callbacks may clear
   the mutable latch before collision. Add a positive connected-floor
   publication, a previous-callback negative, a no-source-endpoint negative,
   and a frame-start-fastfall negative. Marth exposed the positive at
   `InternalPowerlessWallaby.msl:1240:p1` and the fastfall negative at
   `ParallelFamiliarZebra.msl:5199:p1`. Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{ftCo_FallSpecial_Coll,ftCo_80096CC8,ftCo_80096D28}`,
   `refs/melee/src/melee/ft/ft_081B.c::ft_80083090`,
   `refs/melee/src/melee/ft/ftcommon.c::ftCommon_CheckFallFast`,
   `refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044628_Floor,mpColl_80044838_Floor}`,
   `data/stages/bin/*.bin::MSLSTG01 floor graph links`.
   **Same-frame GuardOn entry BODY pose can be owned by the frame-start
   action, not GuardOn's submotion**: `ftCo_800924C0` enters `GuardOn` with
   `Ft_MF_SkipAnim` after the source action's Anim proc has already interpreted
   that frame's live JObj tree. If ShieldDesc misses and BODY collision falls
   through via `ftColl_80078C70 -> lbColl_8000805C`, vanilla can consume the
   frame-start action pose while Slippi publishes `GuardOn` with
   `animation_index=-1` / `action_frame=-1`. Do not rebuild BODY capsules from
   `ftCo_SM_GuardOn` frame 0 for these same-frame entry rows. Add a motivating
   positive where the previous common-action pose admits BODY contact, plus
   adjacent GuardOn/GuardSetOff negatives so the entry-pose owner does not
   broaden steady shield rows. Marth exposed this at
   `VictoriousSpitefulAlpaca.msl:4317:p0` (Dash frame-start pose, Fair hitbox
   0, hurtcap 6/bone 29); aggregate leak guards include
   `MotionlessAggressiveJay.msl:734:p1` and
   `PriceyPartialAlbatross.msl:5142:p0`. Source/proof anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800924C0`,
   `refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_ChangeMotionState}`,
   `refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70`,
   `refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C`, and the
   Dolphin collision probe for VSA:4317.
   **Continuing no-submotion GuardOn BODY pose can override stale previous-action
   hurtcaps when the live tilt/x10 owner is active**: after the entry frame,
   `ftCo_GuardOn_Anim` keeps incrementing `mv.co.guard.x0`, `ftCo_800925A4`
   ticks `x10`, and `ftCo_80091E78` blends the live GuardOn JObj chain toward
   the selected Guard tilt target before fighter collision. Slippi may still
   publish `animation_index=-1` / `action_frame=-1`, and a reseed can carry
   nonempty previous-action BODY hurtcaps. Do not treat those stale hurtcaps as
   source-owned just because `hurtcap_count != 0` when continuing GuardOn has
   nonzero `x10` and `mv.co.guard.x4`; rebuild from the extracted
   GuardOn/Guard source pose and `mv.co.guard` lanes. Existing no-submotion
   Guard/GuardDamage rows and explicit debug-authored BODY geometry remain
   authoritative unless their hurtcap list is absent. Add a no-early-hit
   positive and an adjacent same-family hit negative so the fix is a pose owner,
   not a broad shield suppressor. Marth exposed this at
   `WellWornSmallGoshawk.msl:8444..8445:p1` (`EscapeN -> GuardOn` under Fox
   NAir). Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800924C0,ftCo_GuardOn_Anim,ftCo_800925A4,ftCo_80091E78}`,
   `refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState`,
   `refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70`,
   `refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C`, and
   `data/shields/<char>.bin` / `data/hurtcaps/<char>.json`.
   **SkipHit aerial victim lists can carry through GuardOn -> Guard**:
   multi-hit aerials with `Ft_MF_SkipHit` can preserve HitCapsule `x914`
   victim rings across active windows while the defender transitions from
   `GuardOn` into no-submotion `Guard`. If ShieldDesc misses, BODY fallthrough
   still checks `lbColl_8000ACFC` before percent/KB. Do not treat a first
   steady-Guard frame as a fresh empty victim list when dense hitlist/source
   lanes prove the current defender iid is still in the same hit group and
   BODY attribution names the same source port from an older attacker instance.
   Add a positive first-Guard dense-latch row, plus negatives for continuing
   Guard and stale victim-iid rows. Marth exposed this at
   `InternalPowerlessWallaby.msl:4229:p0`; controls include
   `InternalPowerlessWallaby.msl:464:p0` (continuing Guard still takes BODY)
   and `VictoriousSpitefulAlpaca.msl:5706:p0` (first Guard with stale dense
   victim iid still takes BODY). Source anchors:
   `data/scripts/<char>.bin::MSLFTSC1 AttackAirN create_hitbox/hit_group`,
   `refs/melee/src/melee/ft/chara/ftCommon/forward.h::ftCo_MF_AttackAirN`,
   `refs/melee/src/melee/ft/fighter.c::{Fighter_ChangeMotionState,Fighter_ProcessHit_8006D1EC}`,
   `refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80078C70}`,
   and `refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}`.
   **Squat platform-pass countdown is hidden state, not current-input truth**:
   `ftCo_80099F9C` arms `mv.co.squat.x0/x4` when down is held on a platform,
   but `ftCo_Squat_IASA_inline` later consumes the countdown without rechecking
   `lstick.y`. Free-running runtime must carry the hidden latch across Squat
   frames so a released-stick consume frame still enters `Pass`. Teacher-forced
   one-step rows that do not serialize the latch may only reconstruct it from
   bounded visible evidence: Squat owner, platform floor, the `x470` action-age
   window, `x671` still inside the arm/consume window, and a previous-frame down
   input. The helper belongs to `ftCo_Squat_IASA`; do not arm/consume it on the
   same source callback pass that entered Squat from another state through
   destination-Wait IASA. Add a rollout positive for release-after-arm, an
   adjacent one-frame-too-early negative, a no-visible-latch negative, and a
   non-Marth callback-order negative so this does not turn into "any Squat frame
   3 on a platform passes." Marth exposed this at
   `LoudDullGoat.msl:90:p0` and `VictoriousSpitefulAlpaca.msl:91:p1` in
   free-running rollout, with the one-step latch case at
   `VictoriousSpitefulAlpaca.msl:3269:p1`; `MediumVirtualPig.msl:2593:p0`
   proved the same-frame Squat-entry negative.
   Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_80099F9C`,
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_IASA_inline`,
   and `data/common/ft_common_data.json::{floor_skip_frames,pass_tilt_max_frames,pass_stick_threshold}`.
   **Captured-victim escape states need their own Phys/Coll sweep**:
   `CaptureJump` is not just a visual continuation of `CaptureCut`. Its Phys
   callback applies `ftCommon_Fall` gravity plus `ftCommon_8007D268` air drift
   without inheriting the generic fastfall latch, and its Coll callback enters
   `ftCo_AirCatchHit_Coll -> ft_80082B1C`, so floor contact can publish
   `Landing` or `Wait`. Put this in the generated MotionState owner table
   (`MSLMSO01`), not a local replay-row or action-id exception. Add positive
   rows where `CaptureJump` lands, and adjacent negatives proving
   `CapturePulled*`/`CaptureWait*` do not inherit the same floor-contact path.
   Marth exposed this at `QuestionableHarmfulPanther.msl:3110:p0` and
   `WellWornSmallGoshawk.msl:{5180,5322}:p0`. Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_CaptureJump_Phys,ftCo_CaptureJump_Coll}`,
   `refs/melee/src/melee/ft/ft_081B.c::{ftCo_AirCatchHit_Coll,ft_80082B1C}`,
   and `data/motion_state/owners/<char>.bin::MSLMSO01`.
3. Re-run the replay numbers after each retained common-action fix. Do not
   carry stale burn-down totals in review notes; as of the current Marth branch
   the public reports show Marth one-step 1238 and rollout median 147, and the
   useful signal is the fresh diff against that batch's baseline.
4. **Throw/capture release publication audit**: do not stop after the throw
   hitbox table works. `ftCo_800DDDE4` samples the selected throw-side TransN2
   part, writes the victim's `x1A70` release vector, clears/restores XRotN
   collision state, writes `CollData.last_pos`, and then calls
   `mpColl_800471F8` before damage entry. New characters can remap the extracted
   `grab_capture_anchor_part_id` (Marth: part 88; Fox/Falco controls publish
   part 71/65), but the runtime owner is the explicit
   `throw_release_mpcoll_floor_publication_mask`, not the anchor id. Only the
   probed throw directions and admitted `mpColl_80043754` floor sweep should
   publish the floor-hit substep root instead of the raw below-floor `x1A70`
   point (Marth ThrowF/ThrowLw publish; Marth ThrowB and Fox/Falco controls do
   not). Probe at least one forward throw/CaptureCut, one adjacent
   throw-direction variant, and one non-new-char negative before assuming spacie
   release placement generalizes. Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4`,
   `refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80043754}`, and
   the probe-backed character-data mask in
   `data/characters/*::throw_release_mpcoll_floor_publication_mask`.
5. **Throw-release damage facing audit**: throw damage entry has two source
   facing lanes. `ftCo_800DDDE4` writes `victim->dmg.facing_dir_1 =
   -thrower->facing_dir`; `ftCo_8008DCE0` uses that lane for knockback velocity.
   Separately, `ftCo_800DE7C0` passes a final visible/root facing override of
   `-dmg.facing_dir_1` into `ftCo_8008DCE0` when the raw throw-hit angle is
   strictly between 90 and 270 degrees. Do not collapse the KB sign and final
   facing into one lane when a new character's throw table differs from the
   spacies. Marth ThrowHi (raw angle 93) exposed this at
   `QuestionableHarmfulPanther.msl:231..265:p0`: the wrong final facing mirrored
   terminal DamageFlyTop hurtcaps and changed the later `AttackHi3` BODY contact.
   Add an in-range throw positive and an out-of-range adjacent throw negative for
   each new throw table family. Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4`,
   `refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Thrown.s::ftCo_800DE7C0`,
   and `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0`.
6. **CapturePulled/Wait/Damage attachment pose audit**: grounded catch-connect
   entry (`fn_800DAADC`) and steady victim Phys (`fn_800DAD18`) are different
   source phases. Grounded entry installs CapturePulledLw and publishes only the
   immediate collision callback; the next steady CapturePulled/Wait/Damage Phys
   aligns victim XRotN to the grabber's `capturedamage.x18` by sampling live JObj
   world positions through `lb_8000B1CC` after AObj interpretation. Do not reuse
   integer SSANIM pose matrices for this delta just because the spacie rows are
   close: Marth CatchPull exposed a persistent X drift at
   `QuestionableHarmfulPanther.msl:191..264:p0`. Add a steady pulled/wait
   positive and a grounded connect negative for every new character with a remapped
   grab/capture anchor. Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{fn_800DAADC,fn_800DAD18}`,
   `refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim`, and
   `refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC`.
7. **Offensive hitbox TransN pose audit**: sword/limb hitboxes are published
   from the live JObj matrix via `lb_8000B1CC`, not just the stripped SSANIM01
   joint matrix. If the animation's `data/anims/<char>.tracks.bin`
   `uses_root_motion` bit (`fp->x594_b0`) is clear, active hitboxes under
   FtPart_XRotN may need the SSANIM01 TransN tail recomposed before root facing.
   If `uses_root_motion` is set, Phys has already consumed that TransN into
   `cur_pos` through `ft_80085030`, so adding it again will double-shift spacie
   root-motion attacks. Marth AttackS4 exposed this at
   `ParallelFamiliarZebra.msl:1838:p0`: Dolphin selected the 14-damage hb0 sour
   hit using a live sword JObj matrix equal to the extracted matrix plus the
   current TransN tail; without the tail MSL selected the 20-damage hb3 tipper.
   Add a motivating new-character positive and a root-motion negative (Fox/Falco
   AttackS4 is a good control) when auditing sword/contact rows. Source anchors:
   `refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C`,
   `refs/melee/src/melee/ft/ft_081B.c::{ft_80085030,ft_800850E0}`,
   `refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC`, and
   `data/anims/<char>.{bin,tracks.bin}`.
8. **Catch/CatchDash collision-skeleton scale audit**: do not assume the
   spacie catch-scale correction applies uniformly to a new character's grab
   scripts. Audit `data/moves/<char>.json::{ftCo_SM_Catch,ftCo_SM_CatchDash}`
   `create_hitbox` bone ids and lock standing-Catch positives/negatives
   separately from dash-grab positives. The current source-backed split is:
   non-root authored Catch capsules and CatchDash keep the existing
   `lbColl_80007ECC` collision-skeleton scale counterfactual, while
   root-authored standing Catch stays on the root-scaled pose path. Marth
   required this because standing Catch root bubbles should connect on
   `VictoriousSpitefulAlpaca.msl:6788` / `RipeWealthySeahorse.msl:1811` but
   should not over-admit simultaneous standing Catch at
   `InternalPowerlessWallaby.msl:7737`; Marth root CatchDash still connects at
   `InternalPowerlessWallaby.msl:7311`. Source anchors:
   `refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C`,
   `refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078A2C,ftColl_8007AD18}`,
   and `refs/melee/src/melee/lb/lbcollision.c::lbColl_80007ECC`.
9. **CommonFall/FallAerial/FallSpecial hidden pose blend audit**: new-character
   contact bugs can be caused by the opponent's common-state live pose, not the
   new character's hitboxes. `ftCo_Fall_Anim_Inner` maintains hidden
   `mv.co.fall*.x4` from air-drift ratio and stores the selected FallF/FallB
   family `smid` before collision. The publication path is local-SRT, not final
   matrix interpolation: `ftCo_800CC988` calls `ftAnim_8006FE9C` starting at
   `FtPart_TransN`, so `TopN` and other pre-TransN ancestors stay on the active
   selected submotion while TransN descendants are blended through
   `lb_8000C490`. Slippi does not serialize `x4`; one-step reseed must
   reconstruct it from visible action age, and free-running runtime must carry
   the hidden entry-frame recurrence because a visible post-frame
   `action_frame==0` Fall-family row has already passed the source Anim owner.
   Locks for this owner should assert the hidden `x4/smid` lane directly before
   BODY/contact selection and compute expected values from
   `data/common/ft_common_data.json` plus the row character's
   `data/characters/<char>.json::air_drift_max`; do not bake Fox/Falco drift
   caps, common-data thresholds, lerp factors, or Fall-family submotion ids into
   the test as duplicated literals.
   Marth UAir exposed the strong positive on `LoudDullGoat.msl:4539:p0`;
   aggregate Falco rows `DistinctCaringCobra.msl:5573:p1` and
   `ImpassionedAlarmedTarsier.msl:6428:p1` exposed the tight no-hit and
   entry-tick positive controls. Add a motivating positive plus adjacent
   no-hit/entry-phase controls before adjusting hitbox endpoints. Source
   anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Anim_Inner`,
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_800CC988`,
   `refs/melee/src/melee/ft/ftanim.c::ftAnim_8006FE9C`,
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallAerial.c::ftCo_FallAerial_Anim`,
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_FallSpecial_Anim`,
   and `data/common/ft_common_data.json::{common_fall_blend_air_drift_threshold,common_fall_blend_lerp}`.
10. **Grounded Damage -> Wait_IASA ordering audit**: terminal grounded
    DamageHi/N/Lw delegates to the full `ftCo_Wait_IASA` ordering once
    `x221C_b6` clears. Catch input is before grounded A-attacks, so raw Z
    synthesis (`held_inputs` LR plus `input.x668` A) must enter Catch instead
    of being consumed as Attack11. Marth exposed this at
    `VictoriousSpitefulAlpaca.msl:387:p0`. Add a positive with Z/LR+A catch
    input and a negative where only A remains and the row still falls through to
    Attack11. Source anchors:
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_IASA`,
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA`, and
    `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Catch_CheckInput`.

## Phase 4 — Specials, decomp-first (gate: per-special unit tests + full audit table)

This is the bulk of the work. Implement from the decomp; the replay will not
cover most of it.

1. **State machines**: `src/<char>_specials.c/.h` — enter/phys/coll/IASA per
   special, driven by the extracted ext-attrs. Wire dispatch in
   `fighter_callbacks.c` / `action.c`.
2. **THE ACTION-ID COLLISION AUDIT** (the single biggest source of bugs):
   char-range action ids overlap across characters (341+ means different moves
   per char). Before Marth, ~60 runtime sites keyed on `MSL_ACT_FX_*` were
   char-blind — fox's special state machines were driving marth's ids. Every
   such site must be gated (`msl_char_id_is_spacie(...)` or per-char) or
   widened to char-parameterized data. Audit `src/*.c` AND headers AND bindings
   (`action_ids.h` fastfall set, `damage_terminal_owner.h`,
   `specialhi_pose.h`, `shielddesc_geometry.h`, `reflector_bubbles.c`,
   `items.c`, `state_flags.c`, `instance_id.c`). Produce a complete table:
   every site classified gated / char-param-widened / documented-safe.
3. **B-dispatch surface** (ftCommon side, per new char only needs verifying but
   was built during Marth):
   - `ms_b_entry_mask`: per-action-per-direction entry table from the full
     ftCommon `*_IASA` enumeration. Direction masks are action-specific
     (Dash=side-only, KneeBend=up-only post-entry, SquatWait=up/down,
     Turn=no-neutral, RunBrake/TurnRun=none, RunDirect/SquatRv/OttottoWait
     included; AppealS blocked when data-absent).
   - Grounded resolution order is source order: SpecialS -> up -> neutral
     (D6824) -> down (D68C0). Watch same-frame races (Run_IASA dispatches
     before RunBrake entry — the brake entry frame honors Run's chain).
   - Attack-IASA specials (AttackS4 direct + Wait_IASA delegators) gate on the
     script's allow_interrupt frame via move_tables.
   - Post-hitstun delegates: grounded Damage, airborne DamageAir/DamageFly,
     DamageFall (tumble side-B). PassiveWall with walltech-timer block.
   - B-reverse: ftCo handling, NOT nested under cmd0==0.
   - Aerial up-B input buffer (x68B lane) — currently a locked test + source
     TODO; check whether the new char's timing exposes it.
   - GuardOff x1C window.
4. **Grounded phys owners**: ground specials use ft_80084F3C/ft_80084FA8
   (friction + anim-root-motion exchange) — wire for all the char's ground
   specials or they'll slide/stick wrongly.
5. **Ground<->air swaps**: frame-preserving action swaps for every special that
   can cross the boundary mid-move (walk-off, landing, Stadium transform).
   Test air->ground AND ground->air per family; document the ones with no
   reachable seed surface.
6. **Anim-end common-state handoffs**: if a char-special Anim callback exits
   through `ftCo_Fall_Enter`, `ftCo_80096900`, `ft_8008A2BC`, or another
   common-state entry, audit whether the destination state's IASA can run in the
   same `Fighter_procUpdate`. Marth exposed this on airborne Dancing Blade:
   `ftMs_SpecialAirS1_Anim` enters Fall at the terminal frame, and a same-frame
   jump edge then reaches `ftCo_Fall_IASA_Inner -> ftCo_800CB870`, producing
   `JumpAerialF` without ever serializing the intermediate Fall row. Add a real
   witness lock plus an adjacent no-input negative, and add an action-id-overlap
   negative when the same numeric special id means a different move for another
   character (Marth action 358 is `SpecialAirS1`; Fox/Falco action 358 is
   `SpecialHiFall`). Source anchors:
   `refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialS.c::ftMs_SpecialAirS1_Anim`
   and `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{ftCo_Fall_Enter,ftCo_Fall_IASA_Inner}`.
   The same audit applies when an Anim callback enters another char-special
   MotionState, not only a common action. Marth Shield Breaker `Start_Anim`
   enters `Loop`, and later in the same `Fighter_procUpdate` the destination
   `Loop_IASA` can observe released B and enter `End0`; `Start_IASA` is empty,
   so modeling this as a Start-state release is the wrong owner. Lock both the
   released-B positive and the held-B negative, and preserve source entry frames
   from helper callbacks (`ftMs_SpecialN_80137354/801373B8` enter End0/End1 at
   frame 1). Source anchors:
   `refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialN.c::{ftMs_SpecialNStart_Anim,ftMs_SpecialAirNStart_Anim,ftMs_SpecialNLoop_IASA,ftMs_SpecialAirNLoop_IASA,ftMs_SpecialN_80137354,ftMs_SpecialN_801373B8}`.
   Grounded `ft_8008A2BC` exits also need the destination `Wait_IASA` tail, not
   a serialized one-frame `Wait`: Marth grounded Dancing Blade S1/S2 and Shield
   Breaker End exposed held-shield `GuardOn`, down-stick `Squat`, side-stick
   `Turn`, and shield-edge `GuardReflect` on the same source frame as the
   special's terminal Anim callback. Audit the full source order for these exits
   (`specials -> catch -> grounded attacks -> spotdodge-before-guard -> guard ->
   jump/dash/squat/turn/walk`) and lock a no-input negative so the bridge does
   not invent commands. Source anchors:
   `refs/melee/src/melee/ft/ft_0892.c::{ft_8008A2BC,ft_8008A348}`,
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA`, and
   `refs/melee/src/melee/ft/chara/ftMars/ftMs_Special{N,S,Lw}.c`.
7. **Per-special mechanics** found for Marth that generalize:
   - charge specials: charge-damage hitbox override (SB), charge persistence;
   - launch specials: TransN-driven launch + special landing lag (DS 34f);
   - ledge interaction: stop-at-ledge classification from ftXx coll data (DB);
     the ledge-grab `cd->ecb.bottom.x` term is CENTERED (bottom.x = 0) because
     mpCollInterpolateECB snaps cd->ecb to desired_ecb at time=1.0 - do NOT add
     posed bottom-X tables (over-fit; breaks flip-pose apex catches);
   - cliffcatch cmd sequencing: cmd1 arms in the Phys descent branch (vanilla
     two-pass Coll), teacher-forced cmd1/cmd2 reseed derivation;
  - counter/absorb specials: combat intercept (`combat.c`), descriptor-sphere
    geometry bone-posed and FAIL-CLOSED (no body-admission fallback), real
    projectile geometry threaded through ALL `combat_apply_item_hit` callers
    (8 of them; geometry-less callers fail closed), and post-frame
    `state_flags[2]` publication from the descriptor lifecycle. Marth Counter
    proved this is not the same owner as the combat window:
    `ftColl_8007B1B8` publishes `x221B_b0` (Slippi `0x80`),
    `ftMs_SpecialLw_Anim` immediately sets `x221B_b1` (`0x40`), and the
    CounterHit `Fighter_ChangeMotionState` reset clears `b0` while the
    post-frame row can still expose `b1` (`0xC0 -> 0x40`). Audit descriptor
    provenance separately from descriptor geometry: Anim-created Counter
    descriptors write `shield_unk0/1 = MarsAttributes::x60` and can apply the
    x60 hitlag floor, but the ground/air swap helpers recreate the descriptor
    without restoring those lanes. If replay rows can start after a swap, seed
    this provenance as an explicit hidden lane; current grounded/aerial state and
    current action id are not sufficient once a swapped grounded Counter has
    continued for more than one frame. Add activation-edge, ground/air-swap
    carry, hit-transition clear, lingering same-group contact, and
    same-numeric-action non-character negative locks.
     Source anchors:
     `refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialLw.c`,
     `refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B1B8`,
     `refs/melee/src/melee/ft/fighter.c`, and
     `refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm`;
   - per-special hitlag/SFX attrs.
8. **Tests**: decomp-anchored unit tests per special — frame windows,
   velocities, transition gates, full-chain dispatch tests (e.g. down-B during
   run). Marth ended at ~130 char tests. Every reviewer pass found real bugs;
   write the negative tests too (e.g. "no special from jab IASA").
9. **GATE**: all specials tests green; fox/falco validate-all "no suite total
   changes" (byte-stable) after every retained change.

## Phase 5 — Replay burn-down (gate: remaining rows classified char-boundary vs shared debt)

Methodology: locate -> witness -> owner (`make rollout-locate`,
`reports/triage/` tooling).

- Re-derive reseed lanes the preprocessor can't see (op52 `x221C_u16_y` event
  floor on frame-preserving swaps, teacher-forced cmd lanes).
- Expect a long tail of SHARED debt that the new char merely exposes
  (ECB-interpolation platform-landing rows, KB float parity, inherited drift
  chains — prove with clean-reseed byte-exactness). Classify and document;
  don't force char-specific fixes onto shared rows.
- Keep fox/falco byte-stable throughout.

## Phase 5.5 — Dolphin ground truth (when replay analysis ties)

When two lane conventions or bridge polarities tie on every replay-visible
observable (the capture x2344/x8 matrix and the x670 bridge both burned weeks
this way), stop A/B-testing configurations and capture truth:

1. **Hidden-lane engine dumps** (full JIT speed, minutes per window):
   `tools.dolphin.forensic_row_dump --row <dataset>:<rec>:<p>` captures a
   frame window around any dataset row. The v12 dump records the hidden
   fighter lanes (`fp+0x670/0x674` input timers, `fp+0x2344/48/4C` capture
   words, TransN position, `fp+0x1A50` GrabMash latches) plus per-frame
   hitbox world centers, items, hitlists. Alignment that always holds:
   dump row R = end-of-frame R-1 state + the input record for frame R;
   dataset seed row F = dump row F+1. Validate a derivation by diffing the
   seed lane against the dump lane over the whole window BEFORE changing
   the engine — 50 exact rows is a proof, a matrix of suite totals is not.
   The required Dolphin build is pinned by the `refs/Ishiiruka` submodule
   gitlink. Initialize it with `git submodule update --init refs/Ishiiruka`.
   Probe instrumentation lives as commits in `refs/Ishiiruka`, never as patch
   files in this repo; commit and push the submodule branch, then update and
   commit the superproject gitlink.
2. **Interpreter event traces** (PC-hook probes: within-frame call order,
   register-level gate inputs): use only focused, env-gated probes committed in
   `refs/Ishiiruka` and documented in `tools/dolphin/README.md`. Keep playback
   in JIT for the prefix, then switch to interpreter only for a small bounded
   target window through the wrapper. Interpreter mode is extremely slow; do not
   request long consecutive windows. Record the exact witness rows. The
   `ftCo_800DDDE4` throw-release probe is the model: it captured the
   TransN2/XRotN selection and `mpColl_800471F8` publication phases without
   changing normal Dolphin playback.

Process rules learned the hard way (apply to ALL ports):

- **Check refs/ucf before inventing bridges.** Playback Dolphin applies UCF
  gecko CODE patches the decomp never shows. If a verified witness
  contradicts the shipped instructions (disassembly + RAM constants both
  check out), the answer is a gecko: the x670 "bridge-favored class" was
  UCF 0.84's tumble component hooking the DamageFall IASA compare.
- **Datasets must be regenerated after ANY seed-derivation change**
  (`make preprocess` for every suite). Seed lanes are baked into .msl files;
  a derivation fix that isn't re-preprocessed silently tests nothing.
- **Baseline-stash attribution before repinning stale locks.** When
  newly-runnable tests fail (e.g. after building local debug datasets from
  `replays/debug/*.slp`), prove provenance before touching pins: stash the
  working diff, rebuild, rerun the failures, and if needed rebuild one
  dataset under baseline preprocessing. Repin only rows proven to now match
  ref (or otherwise source-backed); xfail with a documented reason what
  cannot be honestly repinned; never blindly re-anchor a "known divergence"
  test to whatever the current build emits.

## Phase 6 — Live-path verification (gate: webplay clean + clip matrix exits 0)

**The most important lesson of the port.** Replay validation is structurally
blind to live-path bugs: reseeding fills CollData/lock lanes that live play
leaves stale, so an entire class of collision/dispatch bugs never appears in
replay numbers. Marth shipped replay-clean with: broken ground-special
dispatch (no ground specials in the replay), stuck DS landings, dead
B-reverse, and airdodge-through-stage kills. You MUST:

1. **Webplay viewer** (`make viewer`): add the character —
   `tools/viewer/assets/character_zips.tsv` (zip + sha + slippilab URL),
   `tools/viewer/live/schema.js`, main.js character wiring. Then actually play:
   every special, ground and air, at ledges, on slopes, vs shield. Capture
   anything weird with the viewer's msltrace recording.
2. **Trace-replay debugging**: a captured `.msltrace.json` (sparse-delta-v1)
   replays natively — seed from the trace's own row near the failure and feed
   its recorded inputs (reproduces to ~0.04 units when synthetic repro guesses
   miss). This is the primary live-bug debugging loop.
3. **Stage-clip fuzz matrix** (`tools/eval/fuzz_live_clip.py`, dev-only): add
   the char (registry-driven), run
   `uv run python -m tools.eval.fuzz_live_clip --mode sweep --matrix
   --save-violations <json>`; the deterministic sweep (ledgedash /
   fall-into-stage / boundary-approach x 6 stages) must exit 0. Harness realism
   rules if you extend it: airborne seeds carry ground_id 0xFFFF (zero fakes a
   carried strip), spawns must not pre-penetrate the hull, boundary cases come
   from live run-off prefixes, and the oracle is trajectory-resolution based —
   validate any oracle change against the known pre-fix kills.
4. **Hidden-lane debugging tools**: `msl_binding.debug_write_colldata_ecb` +
   `tests/test_colldata_ecb_substrate._colldata_ecb_dtype` dump the CollData
   ECB/floor-probe lanes per frame (this cracked both airdodge clip bugs);
   the floor probe lanes record owner/reject-reason per frame. For wall-side
   issues there are no probe lanes yet — temporary `__LINE__`-printing macro
   wrappers around the probe/begin helpers work (build with
   `setup.py build_ext --inplace`, revert after).

## Phase 7 — Standing gates (run after EVERY retained change)

- `uv run python -m pytest -n 4 -q` (never more than 4 workers).
- `make validate-all` (workers 3) + `uv run python -m
  tools.eval.validation_report_diff --before HEAD --after reports/validation
  --top 220`: fox/falco aggregate baseline must not move unexplained ("no
  suite total changes", no hard reds). Afterwards, inspect the report diffs:
  if the reports are not part of the intended change, restore only the
  generated validation report files after confirming they contain no intended
  updates; if core sim logic changed and refreshed reports are required, keep
  them.
- `make fmt` / `make fmt-check`; `git diff --check`.
- err.pos_x scan on the char's suite when touching physics/collision.
- `make build BUILD_FORCE=1` before final review.
- Commit ONLY at approved check-in boundaries.

## Known traps, quick list

- Two id spaces (internal vs external char id) — never mix.
- Clone chars reuse the donor's submotion enum.
- Char-range action ids collide across chars; audit every action-id-keyed site
  including headers and bindings.
- `{1,22}`-style hardcodes and fixed-2-slot caches silently NULL for new chars.
- Motion-state callback ids renumber on regen by design (conftest manifest).
- Dynamic chains: 4-node runtime contract; hurtcaps on chain bones need proof.
- Ext-attrs block needs a hand-written per-char layout from the decomp struct.
- One replay never covers ground specials/counters — decomp tests + webplay.
- Reseed-based repros mask live-path bugs; verify fixes against live lane
  evolution (trace replay), and prefer last-resort owners over relaxing
  validated gates when scoping collision changes.
- Stage geometry JSON is UNSCALED dat coords; world = json x `unit_scale`.
- Counter/absorb intercepts: fail closed when geometry is missing.
- A witness that contradicts the disassembly usually means a UCF gecko
  (refs/ucf), not a hidden lane.
- Seed-derivation changes require `make preprocess` on every suite before any
  measurement means anything.
- GrabMash-family callbacks read fp inputs one serialized row behind the
  post-frame row they affect; anim-rate writes are post-advance (the engine's
  pre-input callback phase models the deferral — do not add another).
- After `make validate-all`, review `reports/validation/` diffs deliberately -
  restore only unintended regenerated reports, keep intended ones.
