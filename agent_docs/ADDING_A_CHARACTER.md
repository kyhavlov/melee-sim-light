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
  turnips...)? Set `has_articles` for the source fact, but do not route the
  character through an existing article exporter unless that exporter knows the
  character's article contract. For example, `exports_item_article_constants`
  means the character's article contract is implemented in MSLITAR1. Fox/Falco
  export laser/illusion constants; Sheik exports thrown-Needle item kinds,
  lifetimes, Article hurtbox geometry, and hitbox damage. Do not enable it for
  a character until the extractor, runtime loader, no-decomp/package path, and
  focused article locks exist.
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
2. Run validation directly from the replay suite; normal validation derives
   `ValidationReplayBuffers` in memory and does not write row-cache files.
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
   `QuestionableHarmfulPanther.slpz:2131:p0`: seed RunBrake frame 14 carried
   `runbrake_cmd0=1`, but the frame-15 `set_cmd_var(0,0)` script event had
   already cleared the `ftCo_RunBrake_IASA -> fn_800C9CEC` TurnRun gate. Add a
   replay negative on the clear frame and an adjacent positive before the clear.
   Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::ftCo_RunBrake_IASA`,
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_TurnRun.c::fn_800C9CEC`, and
   `data/moves/<char>.json::moves.ftCo_SM_RunBrake.events`.
   **Short motion-var lanes may need source reconstruction even when Slippi has
   a `misc_as` field**: do not assume an exposed-but-zero `fp+0x2340` value is
   authoritative for every MotionState overlay. Sheik exposed this through
   `AttackDash -> CatchDash`: `ftCo_AttackDash_SetMv0` writes
   `p_ftCommonData->x68` into `mv.co.attackdash.x0`, and `ftCo_800D8AE0`
   consumes that countdown on held L/R, including the source `HSD_PAD_LR` macro
   produced by raw Z input. Public rows serialized zero for that short lane, so
   preprocessing reconstructs the countdown from extracted common data and
   visible AttackDash age. Keep this as a seed-only hidden-state reconstruction;
   runtime still owns the live SetMv0 write and normal consume/decrement path.
   Source also maps analog trigger pressure into `HSD_PAD_LR`, but that live
   path should not be widened until the separate SetMv0 callback timing is
   modeled directly. Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_SetMv0`,
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D8AE0`,
   `refs/melee/src/melee/ft/fighter.c::Fighter_Spaghetti_8006AD10`, and
   `data/common/ft_common_data.json::attackdash_x0_init_frames`.
   **Hidden-lane preprocessing LUTs must be registry-backed, not incumbent-char
   backed**: when a seed lane depends on a per-character extracted attr, build
   the preprocessing LUT from the manifest / `data/characters/<char>.json` for
   every registered character. Marth exposed this through basic Turn:
   `ftCo_Turn_Enter_Basic` copies the character's `turn_frames` into
   `mv.co.turn.frames_to_turn`; a Fox/Falco-only LUT seeded Marth as zero, so a
   first steady `Turn` row could consume strong stick and spuriously enter
   `Dash`. Add a positive for the new character's ordinary `Wait -> Turn`
   hidden countdown and keep existing spacie Turn controls. Source anchor:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::{ftCo_Turn_Enter_Basic,ftCo_Turn_Anim_Inner}`.
   Keep replay seed reconstruction on raw source-script windows unless the
   hidden source lane itself proves a post-clear latch. Runtime helpers may
   include convenience tails for live callback carry, but copying those tails
   into preprocessing can synthesize stale hidden state. Marth-suite
   `RipeWealthySeahorse` exposed this on Falco `SpecialNLoop`: the live helper
   `move_tables_special_cmd0_active_at_frame()` has a latch-clear tail, while
   the replay seed for `mv.fx.SpecialN.isBlasterLoop` must use the raw MSLFTSC1
   `cmd_var[0]` interval from `ftFx_SpecialNLoop_IASA`.
   **Visible animation frame is not a hidden special timer**: special callbacks
   may hold or freeze the public animation frame while a private `mv.*.x0`
   timer keeps advancing. Treat these as seed-lane candidates when replay
   reseed can start inside the held segment. Sheik Chain exposed this through
   `ftSk_SpecialS_CheckInitChain`: Start holds visible frame 25 while
   `mv.sk.specials.x0` reaches `ftSeakAttributes::x20`, and active Chain
   consumes a release latch set by the previous IASA callback, not the current
   Anim callback. If the hidden timer/latch is callback-owned, hitlag must also
   be part of the seed contract: `Fighter_8006A1BC` decrements hitlag before
   `Fighter_8006A360` resumes non-hitlag Anim/IASA callbacks, so frozen rows
   must not advance the hidden special lane. Sheik Chain hitlag at
   `sheik_demo_game.slpz:4625..4628:p0` exposed this after the Start timer was
   otherwise source-correct. Source anchors:
   `refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{ftSk_SpecialS_CheckInitChain,ftSk_SpecialS_Anim,ftSk_SpecialAirS_Anim,ftSk_SpecialS_IASA,ftSk_SpecialAirS_IASA}`;
   `refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}`.
   **Hidden special state can outlive the special action family**: do not reset
   replay seed lanes just because the visible action leaves the move. Sheik
   Needle count (`fv.sk.x0`) persists through `SpecialNCancel -> Wait` and a
   later `SpecialAirNEnd` still shoots the stored needles. Only source
   decrement/clear owners (`shootNeedles`, damage/death callbacks) should change
   the lane.
   **Run entry collision pose can diverge from replay seed timebase**:
   `ftCo_Run_Anim` writes the next animation rate from hidden `mv.co.run.x4`
   only on reduced-friction floors; ordinary floors use `fp->gr_vel`. One-step
   seeds can still expose the entry rate (`1.0`) when there is no next same-Run
   row to reconstruct the callback rate, so BODY contact on the first
   post-entry Run frame may need a collision-pose bridge without changing the
   global action timebase. Marth exposed this at
   `InternalPowerlessWallaby.slpz:5292:p0`: replay-visible Run frame 1 crossed
   the next integer pose in MSL and missed a vanilla UpAir BODY hit. Add a
   first-post-entry positive and adjacent controls; do not promote this into a
   broad live-fractional Run pose owner unless Dolphin/source evidence proves
   the whole action family. Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::{ftCo_Run_Enter_Full,ftCo_Run_Anim}`,
   `refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70`, and
   `refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}`.
   **Collision-pose seed bridges must stay inside the extracted animation
   domain**: if a hidden timer such as `x1994` proves that a seeded row should
   sample the post-Anim DownBound pose, advance only to a concrete non-looping
   AObj pose. Do not synthesize `end_frame` itself as another collision frame:
   for DownBoundD, extracted `end_frame=26` means source can sample frame 25
   before the later DownStand transition, while frame 26 is outside the live
   pose domain and can admit false BODY contact. Add an earlier-frame positive
   where the bridge advances to the whiffing pose and a terminal-frame negative
   where the bridge clamps instead of advancing past `end_frame - 1`. Source
   anchors:
   `refs/melee/src/melee/ft/fighter.c::Fighter_8006A360`,
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::{ftCo_DownBound_Anim,ftCo_DownBound_Coll}`,
   and `refs/melee/src/melee/ft/ftanim.c::ftAnim_IsFramesRemaining`.
   Do not create hidden seed latches while source callbacks are suspended by
   hitlag. `Fighter_8006A1BC` decrements hitlag before `Fighter_8006A360`
   resumes non-hitlag callback/physics ownership, so `ftCommon_CheckFallFast`
   cannot set `fp->fall_fast` on frozen `hitlag > 1` frames. A fastfall latch
   may persist if it was already true, and the immediate `hitlag == 1` exit row
   can latch after the decrement. Add a held-down hitlag-exit positive and a
   neutral-input hitlag-exit negative when a new character's attacks expose
   hitlag-frozen aerial rows; Marth exposed the negative at
   `InternalPowerlessWallaby.slpz:483:p1`. Source anchors:
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
   `QuestionableHarmfulPanther.slpz:90:p0` on a Dream Land platform and
   `QuestionableHarmfulPanther.slpz:3900:p0` on Dream Land's main hard floor;
   existing downward positives include `ParallelTemptingElk.slpz:2170:p1` and
   `ShadyDecimalStarling.slpz:571:p1`. Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Anim`,
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_IASA`,
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll`,
   and `refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044838_Floor}`.
   **Do not collapse mpLib floor corrections into floor-plane snaps**:
   `mpLib_8004DD90_Floor` returns a signed correction and the wrapper adds it
   back to `CollData.cur_pos.y`. On fast downward landing contacts, the f32
   order `cur_y + ((floor_y - cur_y) + 0.0001)` can differ from
   `floor_y + 0.0001`. When adding a character, lock at least one official
   `Fall_Coll -> Landing_Enter_Basic` row on a legal-stage hard floor and keep
   the entry publication separate from later grounded correction residuals.
   Marth exposed this on Dream Land at `QuestionableHarmfulPanther.slpz:532:p1`.
   Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll`,
   `refs/melee/src/melee/ft/ft_081B.c::{ft_80083090_inline,ft_80082B1C}`,
   and `refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor`.
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
   `InternalPowerlessWallaby.slpz:290:p0`,
   `InternalPowerlessWallaby.slpz:4210:p0`,
   `ParallelFamiliarZebra.slpz:7821:p0`, and
   `QuestionableHarmfulPanther.slpz:4388:p1`; the Yoshi slope boundary is
   `LoudDullGoat.slpz:3757..3758:p1` for the unlinked side-platform remap and
   `MetallicUniqueGrouse.slpz:{947:p1,1990:p0,3967:p1}` for same/connected floor
   chain positives. Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA`,
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll`,
   `refs/melee/src/melee/ft/ft_081B.c::ft_80082C74`,
   `refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}`,
   and `data/stages/bin/*.bin::MSLSTG01 fighter_solid/is_ledge/floor graph links`.
   **FoD static-y platform remaps need callback-local CollData endpoints**:
   the center platform is generated as `MSLSTG01 platform_transforms(kind=static_y)`,
   but source still admits it through `mpCheckFloorRemap` using
   `CollData.prev_pos + prev_ecb.bottom` and `CollData.cur_pos + ecb.bottom`.
   Do not resample the previous bottom from the visible action pose when a new
   character exposes a transformed-platform airdodge/landing row; probe or seed
   the actual CollData previous/current ECB interval. Add a positive where the
   previous bottom is above and current bottom below the static-y floor, plus a
   spacie/adjacent negative where both endpoints are already below. Sheik
   exposed this with FoD `EscapeAir -> LandingFallSpecial` rows; the control was
   a Fox FoD row where `mpColl_80044628_Floor` returned false before floor
   publication. Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll`,
   `refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}`,
   `refs/melee/src/melee/mp/mplib.c::{mpCheckFloorRemap,mpLineIntersectionH}`,
   and `data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=static_y)`.
   **No-lock EscapeAir height-platform crossings are not bounded by raw root
   speed**: after `CollData_X130_Locked` clears, `EscapeAir_Coll` still samples
   the callback-local ECB bottom segment against the live transformed platform
   line. A visible floor-to-current-bottom gap larger than `speed_y_self` can be
   source-valid on FoD height platforms because platform motion and ECB
   interpolation both participate in the callback interval. Keep the raw
   vertical-speed overstep guard for static/ordinary soft-platform controls, but
   do not apply it to generated `MSLSTG01 platform_transforms(kind=height)` lines
   without direct CollData/probe evidence. Sheik exposed this with sustained
   `EscapeAir -> LandingFallSpecial` FoD rows; adjacent static/ordinary
   no-contact controls must remain airborne. Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll`,
   `refs/melee/src/melee/ft/ft_081B.c::ft_80082C74`,
   `refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}`,
   and `data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)`.
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
   `InternalPowerlessWallaby.slpz:{1550,1966,8480}`; the hard-floor
   `VictoriousSpitefulAlpaca.slpz:8175` row is a separate floor-publication
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
   `WellWornSmallGoshawk.slpz:679:p0`; `LawfulInsistentMeerkat.slpz:4967:p0`
   remains the legitimate source-ECB landing control. Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::{ftCo_JumpAerial_IASA,ftCo_JumpAerial_Coll}`,
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll`,
   `refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_800835B0}`, and
   `refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}`.
   **AttackAir script-facing is a data-backed script event, not an action-id
   shortcut**: `ftCo_AttackAir_Anim` consumes `throw_flags_b3` through
   `ftCheckThrowB3` and flips scalar facing when the decoded script pulse
   fires. Audit `data/moves/<char>.json::MSLFTSC1` events for each aerial
   instead of assuming BAir behavior from the common action id. Runtime checks
   must cross the pulse on the command-script/AObj fixed-point timeline
   (`anim_frame_fp - frame_speed_mul_fp -> anim_frame_fp`), not on
   `prev_action_frame -> anim_frame_f32`: action age can diverge under
   non-1.0 rates and teacher-forced reseeds. Also split the scalar-facing
   consumers from pose consumers: ledge-side/action checks can use the
   just-flipped scalar facing, while same-tick BODY/hitbox pose may still match
   the already-interpreted pre-flip root/JObj orientation. Marth BAir exposed
   this distinction; Fox/Falco BAir are adjacent negatives because their decoded
   scripts do not publish the same pulse. Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim`,
   `refs/melee/src/melee/ft/inlines.h::ftCheckThrowB3`,
   `refs/melee/src/melee/ft/ftaction.c::ftAction_800718A4`, and
   `data/moves/<char>.json::moves.ftCo_SM_AttackAirB.events`.
   **Multi-hit AttackAir late tails need the same floor-source audit**: after
   the final `clear_hitboxes` command, `cmd_var[0]` may still be active and the
   action can still enter `LandingAir*`, but FoD height-platform publication
   still needs current/same-step/live platform authority or a callback-local
   bottom/projection floor producer. Do not treat the cmd_var tail, sparse
   platform height, or `GROUND_CONTACT`-only source bit as proof that a
   reconstructed platform line may publish a landing. Add a no-current-source
   positive after the final clear, a no-current-source active-script positive,
   and current-source negatives on both side platforms. Marth exposed this at
   `InternalPowerlessWallaby.slpz:{1037,1038,9718}:p1` and
   `VigorousRelievedLlama.slpz:3110:p0`; current-source landing controls are
   `InternalPowerlessWallaby.slpz:{488,3095}:p1`. Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll`,
   `refs/melee/src/melee/ft/ft_081B.c::ft_80082C74`,
   `refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}`,
   `data/motion_state/owners/<char>.bin::MSLMSO01 submotion_id`,
   `data/moves/<char>.json::moves.ftCo_SM_AttackAir*.events`, and
   `data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)`.
   **Frame-start Fall after AttackAir still needs a current platform owner**:
   `ftCo_AttackAir_Anim` can enter `ftCo_Fall_Enter` before the collision
   callback, and if the fighter is already airborne `ftCo_Fall_Enter` does not
   refresh CollData with `ftCommon_8007D5D4`. A reseeded frame-start `Fall`
   whose previous visible action was already `Fall` may therefore still carry
   AttackAir provenance, but that carry alone is not enough to publish Landing
   on a FoD height platform. Require a current direct/contact platform source,
   live scheduler/velocity source, or the callback-local bottom/projection floor
   producer before allowing `mpColl_80044838_Floor` to root-snap to a transformed
   platform. Keep same-frame `AttackAir* -> Fall` rows on the ordinary
   AttackAir/Fall callback path and add current-source landing negatives so this
   guard does not suppress real platform contacts. Sheik exposed the no-current
   source boundary at `SnarlingHelplessBeaver.slpz:2464:p0`; current-source and
   same-frame controls came from `ZestyPreciousTurtle.slpz` and
   `ConstantStiffOtter.slpz`. Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim`,
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{ftCo_Fall_Enter,ftCo_Fall_Coll}`,
   `refs/melee/src/melee/ft/ft_081B.c::ft_800831CC`,
   `refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044628_Floor,mpColl_80044838_Floor}`,
   and `data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)`.
   **AttackAirLw first-create publication is a script-timeline
   boundary**: DAir-style platform publication should be audited against the
   decoded first `create_hitbox` command, in the callback-visible action-frame
   coordinate, before trusting FoD height-platform contacts. A reconstructed
   platform line before that script edge is not enough to publish
   `LandingAirLw`; the adjacent first-create row must still land. Add
   pre-first-create airborne positives and first-create landing controls from
   the official suite, and keep the boundary data-driven from
   `data/moves/<char>.json::moves.ftCo_SM_AttackAirLw.events` rather than a
   character/action hardcode. Falco DAir in the Marth suite exposed this at
   `VigorousRelievedLlama.slpz:{8576,8577,8578}:p1`. Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll`,
   `refs/melee/src/melee/ft/ftaction.c::ftAction_800718A4`,
   `refs/melee/src/melee/ft/ft_081B.c::ft_80082C74`,
   `refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}`,
   `data/moves/<char>.json::moves.ftCo_SM_AttackAirLw.events`, and
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
   `InternalPowerlessWallaby.slpz:1240:p1` and the fastfall negative at
   `ParallelFamiliarZebra.slpz:5199:p1`. Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{ftCo_FallSpecial_Coll,ftCo_80096CC8,ftCo_80096D28}`,
   `refs/melee/src/melee/ft/ft_081B.c::ft_80083090`,
   `refs/melee/src/melee/ft/ftcommon.c::ftCommon_CheckFallFast`,
   `refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044628_Floor,mpColl_80044838_Floor}`,
   `data/stages/bin/*.bin::MSLSTG01 floor graph links`.
   **FallSpecial physics depends on `ftCo_80096900`'s hidden entry arguments**:
   `mv.co.fallspecial.xC` is not implied by the destination action id. Fox/Falco
   Firefox and Illusion end callsites pass `arg1=1`, while Marth Dolphin Slash
   calls `ftCo_80096900(gobj, 0, 1, 0, x28, x2C)`. When xC is zero,
   `ftCo_FallSpecial_Phys` uses `ca->fast_fall_velocity` as the terminal clamp
   even if the public `fall_fast` bit is clear. New character ports must audit
   each special's `ftCo_80096900` callsite arguments and encode xC=0 sources in
   `data/characters/<char>.json::fallspecial_xc0_source_fx_kind_mask`, keyed by
   `MslMsFxSpecialKind`. Seed reconstruction may also accept velocity proof
   once the row has already crossed ordinary terminal, but it must not infer xC
   from the common FallSpecial action, raw character id, or raw SpecialHi action
   ids. Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{ftCo_80096900,ftCo_FallSpecial_Phys}`,
   `refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialHi.c::ftMs_SpecialHi_80138884`,
   and `refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c`.
   **Thrower AObj rate can outlive victim attachment**: `ftCo_800DD398`
   installs the victim-weight throw animation rate on both thrower and victim,
   and `ftCo_800DD724` release/detach does not reset the thrower's rate. The
   thrower's own `ftAnim_IsFramesRemaining` exit can therefore occur on a
   detached/no-victim replay row that still needs the original throw AObj rate.
   Do not reconstruct throw end timing only from current attachment pointers; use
   source throw-rate provenance, decoded release-frame evidence, and extracted
   non-looping animation end frames. Marth ThrowF exposed this at the post-release
   terminal boundary, where repeated fixed-point 4/3-rate accumulation can sit a
   few LSB below the source f32 clamp. Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD398,ftCo_800DD724,ftCo_ThrowF_Anim}`,
   `refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim`,
   `data/characters/<char>.json::weight_independent_throws_mask`, and
   `data/anims/<char>.tracks.bin`.
   **Low throws can hit with fighter HitCapsules before the release flag**:
   do not assume ThrowLw timing follows the Fox/Falco projectile-pulse pattern.
   Sheik Down Throw creates ordinary fighter hitboxes at frame 31, enters
   attacker-side hitlag, then reaches `set_throw_flags(hit_idx=0)` at frame 36.
   `Fighter_8006A1BC` clears hitlag ownership before `Fighter_8006A360` advances
   the thrower AObj again, while the victim is still attached and not visibly in
   hitlag. New characters need a decoded-script audit for
   `create_hitbox`-before-release throws and adjacent controls proving the throw
   does not release before the script flag. Source anchors:
   `refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}`,
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_ThrowLw_Anim,ftCo_800DD724}`,
   and `data/scripts/<char>.bin` (`MSLFTSC1 create_hitbox` /
   `set_throw_flags`).
   **MotionState rows are not proof that angled attack variants are available**:
   common dispatchers such as `AttackS3::decideAngle` check the source
   animation/subaction pointer before choosing an angled variant. A character can
   have MotionState ids for `AttackS3Hi`/`AttackS3Lw` while the extracted
   `SSANIMT1` end frame is zero, meaning vanilla falls back to the neutral
   variant. When adding a character, audit ground-attack direction selectors
   against extracted animation availability, not just MSLMSO01 MotionState
   presence. Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS3.c::decideAngle` and
   `data/anims/<char>.tracks.bin`.
   **Common Fall static-platform carries need fastfall and current floor-owner
   proof**: `Fall_Coll -> ft_800831CC -> mpColl_80047E14` must first produce a
   callback-local platform floor hit before `mpColl_80044838_Floor` can publish
   a root snap. A non-fastfall row whose previous/current roots are already
   below a static soft platform and whose `CollData.floor.index` still carries
   that same platform id should remain airborne; the carried id is not a fresh
   floor acceptance. Keep true crossings and fastfall rows on the ordinary
   bottom-sweep path. Marth exposed the non-fastfall stale-carry boundary at
   `WellWornSmallGoshawk.slpz:5727..5729:p1`; aggregate fastfall
   `DelayedSuperbGuanaco.slpz:200:p1` protects the adjacent landing path.
   Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll`,
   `refs/melee/src/melee/ft/ft_081B.c::ft_800831CC`,
   `refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}`,
   and `data/stages/bin/*.bin::MSLSTG01 platform flags`.
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
   `VictoriousSpitefulAlpaca.slpz:4317:p0` (Dash frame-start pose, Fair hitbox
   0, hurtcap 6/bone 29); aggregate leak guards include
   `MotionlessAggressiveJay.slpz:734:p1` and
   `PriceyPartialAlbatross.slpz:5142:p0`. Source/proof anchors:
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
   `WellWornSmallGoshawk.slpz:8444..8445:p1` (`EscapeN -> GuardOn` under Fox
   NAir). Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800924C0,ftCo_GuardOn_Anim,ftCo_800925A4,ftCo_80091E78}`,
   `refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState`,
   `refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70`,
   `refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C`, and
   `data/shields/<char>.bin` / `data/hurtcaps/<char>.json`.
   **Continuing no-submotion GuardOn ShieldDesc uses the live GuardOn current
   pose, not steady Guard**: `ftColl_80078C70` checks `ShieldDesc` before BODY
   for each incoming HitCapsule, and the ShieldDesc bone is republished by
   `ftCo_GuardOn_Anim -> ftCo_80091E78` even when Slippi still reports
   `animation_index=-1` / `action_frame=-1`. Do not place the shield bubble
   from the steady `Guard` neutral table on these sustained GuardOn snapshots.
   Reconstruct the source-order publication from `mv.co.guard` lanes and the
   extracted `data/shields/<char>.bin::guard_on_xyz` current-pose trajectory,
   with adjacent rows proving no-contact and terminal-contact boundaries. Marth
   exposed this at `QuestionableHarmfulPanther.slpz:2597..2600:p1` and
   `RipeWealthySeahorse.slpz:4901..4905:p0`; Fox/Falco terminal no-contact rows
   protect the shared path from becoming a broad terminal-GuardOn shield hit.
   Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_Anim,ftCo_800925A4,ftCo_80091E78}`,
   `refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_8007B1B8}`,
   `refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC`, and
   `data/shields/<char>.bin::MSLSHLD1`.
   **Guard admission belongs to the specific IASA callback, not to all grounded
   locomotion**: `Run`, `Dash`, `Wait`, `Walk`, `Turn`, and squat-family
   callbacks can reach `ftCo_80091A4C`, but `ftCo_TurnRun_IASA` only checks
   `fn_800CAF78` (jump). Do not include `TurnRun` in a broad grounded
   shield-entry allow-list just because adjacent run states do. Add a Marth
   `TurnRun` held-shield negative and a spacie `Run` positive so future
   characters keep the callback boundary rather than inheriting guard entry
   from an action-family proxy. Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_TurnRun.c::ftCo_TurnRun_IASA`,
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_IASA`, and
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091A4C`.
   **No-submotion shield item BODY needs an authoritative exact miss**:
   `ftColl_8007925C` routes item BODY through `lbColl_8000805C` after
   ShieldDesc/ReflectDesc miss. If a no-submotion Guard-family row has enough
   data to evaluate the source matrix/local-radius path for a laser HitCapsule,
   a miss from that path should not be re-admitted by a reduced replay-visible
   sphere/capsule fallback. The frame-start lightshield sample only chooses the
   item endpoint consumed by source; it is not permission to replace `lbColl`'s
   miss with a broader proxy. Add both a pass-through lightshield positive and
   adjacent laser-shield hit controls so future characters do not turn this into
   a shield suppressor. Source anchors:
   `refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C`,
   `refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C`, and
   `refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Phys,it_8029C4D4}`.
   **Laser shield contact is article endpoint phase plus defender ShieldDesc,
   not projectile-owner action alone**: no-submotion `Guard` rows with raw
   `x2218=0x04` can look like stale carried lasers, but source item collision
   still tests `itFoxlaser_UnkMotion1_Phys`'s previous endpoint against the
   current endpoint in `it_8029C4D4` once the previous endpoint has left the
   sub-identity startup scale slice. For new characters, check both the
   projectile article's scale/endpoint phase and the defender's
   `fp+0x221B_b0` ShieldDesc lane before classifying a laser miss as a
   projectile-owner replay artifact. Do not merge this with all `x2218=0x44`
   rows: B1 plus reflect-behavior stays on the point sample when the owner starts
   the frame outside the blaster loop, but remains a live-segment article callback
   when the owner starts the frame in SpecialNLoop even if it lands later that
   same frame. Marth exposed the hit
   side at `RipeWealthySeahorse.slpz:{365,10682}:p0`; Fox
   `DistinctCaringCobra.slpz:2231` remains the adjacent startup negative, and
   `DelayedSuperbGuanaco.slpz:10435` / `ThisVioletRaccoon.slpz:1037` protect the
   `0x44` mature-endpoint post-loop negative. Source anchors:
   `refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Anim,itFoxlaser_UnkMotion1_Phys,it_8029C4D4}`,
   `refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077688}`, and
   `refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm` (`fp+0x221B`).
   For GuardReflect, do not collapse the BODY sample rule to the visible action:
   `ftCo_GuardReflect_Anim` decrements `mv.co.guard.x18` before item collision.
   Pure `x2218=0x04` rows whose source x18 expires use the current item sample
   and can be damaged, while live x18 rows and `allow_interrupt|reflect`
   carries (`x2218 & 0xA4 == 0x84`) remain on the frame-start sample. Keep both
   positives and expired-timer negatives. Also preserve existing grounded
   `LandingFallSpecial` exact-Z owners when adding Guard-family item BODY
   matrix paths; that action is not shield state, but `ftColl_8007925C ->
   lbColl_8000805C` still owns flattened-Z laser BODY contact.
   **Do not infer item BODY x34_scale.z from visible Fall**:
   `lbColl_8000805C` only overwrites hurtcap endpoint Z when
   `ftCommon_8007F804(fp)` returns the fighter's `x44_mtx`, which requires
   non-unit `fp->x34_scale.z`. Ordinary airborne `Fall` rows should stay on the
   seed-visible hurtcap-depth path unless a probe/seed lane proves that hidden
   transform owner. Marth exposed the trap at
   `VictoriousSpitefulAlpaca.slpz:2008:p0`, where a state0 Falco laser sits near
   Marth's high Fall cap2; broad Fall-as-Z-force logic incorrectly entered
   DamageAir while vanilla kept the laser and fighter alive. Keep positive laser
   BODY locks such as TBK/SDS, plus a high-cap no-hit negative, so future
   characters do not inherit spacie-shaped assumptions about hurtcap depth.
   Source anchors: `refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007F804`,
   `refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C`, and
   `refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C`.
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
   `InternalPowerlessWallaby.slpz:4229:p0`; controls include
   `InternalPowerlessWallaby.slpz:464:p0` (continuing Guard still takes BODY)
   and `VictoriousSpitefulAlpaca.slpz:5706:p0` (first Guard with stale dense
   victim iid still takes BODY). Source anchors:
   `data/scripts/<char>.bin::MSLFTSC1 AttackAirN create_hitbox/hit_group`,
   `refs/melee/src/melee/ft/chara/ftCommon/forward.h::ftCo_MF_AttackAirN`,
   `refs/melee/src/melee/ft/fighter.c::{Fighter_ChangeMotionState,Fighter_ProcessHit_8006D1EC}`,
   `refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80078C70}`,
   and `refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}`.
   Shield-hit versions of this owner need the same audit after hitlag releases:
   `ftColl_80076CBC` can register a defender in `HitCapsule.victims_1` during
   `GuardSetOff`, then later `Guard` rows with replay-proven ShieldDesc misses
   still consult that hidden list before BODY. If the simulator uses a visible
   action/instance id as the seed proxy, rebind only from the source victim
   pointer proof and keep adjacent late-create negatives so this does not become
   a broad aerial-vs-Guard shield suppressor.
   No-hitlag release rows need the same seed reconstruction before any
   "proven current hitlag" guard: same MSLFTSC1 `AttackAirN` create lifetime,
   replay ShieldDesc contact marker, and current defender iid are enough to
   restore the source `HitCapsule` victim list for the first `Guard` row.
   Marth exposed this at `RipeWealthySeahorse.slpz:225:p0`.
   Damaging BODY hitlag tails have the same hidden-state shape. During hitlag,
   `Fighter_8006A360` skips the action script, so previous `x914` HitCapsules
   and their `victims_1` rings survive even if a one-step seed lacks explicit
   hitlist lanes. If replay-visible frame-start hitlag/hitstun and
   `instance_hit_by`/`last_hit_by` name the current attacker instance/source,
   that is valid proof that `ftColl_80076ED8 -> inlineB0 -> lbColl_80008688`
   inserted the victim into every active same-group HitCapsule. Keep the bridge
   reseed-only, let authoritative per-HitCapsule empty seeds win, and add an
   adjacent first-hit positive plus an attribution-cleared negative. Marth Fair
   exposed this at `MetallicUniqueGrouse.slpz:2116:p0`.
   **Dense hitlist seed bridges must follow script create/clear bands**:
   if a replay seed only has the legacy dense group victim lane, do not
   materialize that state into a newly-created HitCapsule just because a spacie
   row needed it. First classify the move's extracted `create_hitbox` /
   `clear_hitboxes` timeline from `data/moves/<char>.json` or `MSLFTSC1`.
   Multi-band aerials with a later create band can have a bounded
   create-edge carry owner; single-band aerials should use the ordinary
   `ftAction_8007121C -> ftColl_800768A0` clear/copy rule unless
   per-HitCapsule seed/probe evidence proves otherwise. The runtime gate should
   read as a table-backed script-timeline predicate, not a character-id or
   action-id-only proxy. This rule applies to one-step reseed initialization
   and rollout seed initialization: a stale dense hit_group seed from an
   earlier band must not suppress a later-band BODY contact when the extracted
   script has already crossed `clear_hitboxes` and no authoritative
   per-HitCapsule seed lane exists. Marth `AttackAirN` exposed this at
   `InternalPowerlessWallaby.slpz:477:p0`; adjacent `476/478` keep the
   no-hit-before-band and seeded-hitlag-tail boundaries honest. Source anchors:
   `refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C`,
   `refs/melee/src/melee/ft/ftcoll.c::ftColl_800768A0`,
   `refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008440,lbColl_8000ACFC}`,
   and `data/moves/<char>.json::moves.*.events`.
   Do not treat visible `LandingFallSpecial` as a guard-admission owner by
   action id. `ftCo_Landing_IASA` reaches guard input only after
   `mv.co.landing.allow_interrupt` passes, so hitlist seed bridges and stale
   trims must consume `landing_fallspecial_allow_interrupt` for that action.
   New characters with non-spacie special landing callsites will expose stale
   dense victims if this hidden bit is ignored. Source anchor:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::{ftCo_LandingFallSpecial_Enter,ftCo_Landing_IASA}`.
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
   `LoudDullGoat.slpz:90:p0` and `VictoriousSpitefulAlpaca.slpz:91:p1` in
   free-running rollout, with the one-step latch case at
   `VictoriousSpitefulAlpaca.slpz:3269:p1`; `MediumVirtualPig.slpz:2593:p0`
   proved the same-frame Squat-entry negative.
   Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_80099F9C`,
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_IASA_inline`,
   and `data/common/ft_common_data.json::{floor_skip_frames,pass_tilt_max_frames,pass_stick_threshold}`.
   **Character specials can create `CollData.floor_skip` without entering Pass**:
   audit every special collision callback that calls `ftCo_8009A134` or `mpUpdateFloorSkip`.
   The runtime must write the real floor-skip owner and replay preprocessing must serialize the
   hidden skip when a one-step seed starts after the pass-through. Sheik Vanish exposed this in
   `SpecialAirHiStart_1`: while `mv.sk.specialhi.xC < ftSeakAttributes::x3C`,
   `ftSk_SpecialAirHiStart_1_Coll` consumes static platform contact through
   `ftCo_8009A134 -> mpUpdateFloorSkip` and stays airborne; later `xC` contacts ground normally
   unless `CollData.floor_skip` was already live. Add a replay positive for the first consumed
   platform frame, a carried-skip positive, and a late-`xC` no-skip negative. Source anchors:
   `refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::{
   ftSk_SpecialAirHiStart_1_Anim,ftSk_SpecialAirHiStart_1_Coll}`,
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A134`, and
   `data/characters/sheik.json::{sheik_vanish_travel_frames,
   sheik_vanish_ground_contact_min_frames}`.
   **Ottotto -> Squat floor loss is a floor-sweep provenance audit, not an
   Ottotto row exception**: `ftCo_Ottotto_IASA` can enter `Squat` from down
   input and the same fighter proc can then run `Squat_Coll`. If the current
   `floor_sweep_prev_pos` starts at the callback-visible root, `Squat_Coll`
   may consume the frame-start `xF8_playerNudgeVel` overlap displacement before
   checking the facing floor endpoint. If the seed already carries a different
   floor-sweep start, that previous collision/nudge packet has already been
   consumed and the port must not synthesize a second fresh nudge. Add a
   same-proc floor-loss positive, a carried-sweep Squat negative, and the
   following-Squat floor-loss control when a new character's pushbox or stage
   position reaches ledges differently. Marth-suite WWS exposed this at
   `WellWornSmallGoshawk.slpz:{2145,2146,8411}:p0`.
   Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::ftCo_Ottotto_IASA`,
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::{ftCo_800D5FB0,ftCo_Squat_Coll}`,
   `refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate`, and
   `refs/melee/src/melee/ft/ft_081B.c::{ft_80083F88,ft_80082708}`.
   **MissFoot first-callback floor contacts are ECB-lock sensitive**:
   `ftCo_8009F39C` enters MissFoot through `ftCommon_8007D5D4`, setting
   `CollData_X130_Locked` / `ecb_lock` before the next `MissFoot_Coll`.
   That first callback may see a shallow same-floor contact through
   `ft_80082F28 -> ft_CheckGroundAndLedge -> mpColl_800473CC`, but vanilla can
   keep the fighter airborne instead of serializing `Wait`/`Landing`. Lock a
   first-callback positive under active ECB lock and a sustained/no-lock
   MissFoot negative that still lands normally; do this for the new character
   and at least one non-owner control so the rule does not become a char-id
   proxy. Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_MissFoot.c::{ftCo_8009F39C,ftCo_MissFoot_Coll}`,
   `refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4`,
   `refs/melee/src/melee/ft/fighter.c::Fighter_procMap`,
   `refs/melee/src/melee/ft/ft_081B.c::ft_CheckGroundAndLedge`, and
   `refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_800473CC}`.
   **Captured-victim escape states need their own Phys/Coll sweep**:
   `CaptureJump` is not just a visual continuation of `CaptureCut`. Its Phys
   callback applies `ftCommon_Fall` gravity plus `ftCommon_8007D268` air drift
   without inheriting the generic fastfall latch, and its Coll callback enters
   `ftCo_AirCatchHit_Coll -> ft_80082B1C`, so floor contact can publish
   `Landing` or `Wait`. Put this in the generated MotionState owner table
   (`MSLMSO01`), not a local replay-row or action-id exception. Add positive
   rows where `CaptureJump` lands, and adjacent negatives proving
   `CapturePulled*`/`CaptureWait*` do not inherit the same floor-contact path.
   Marth exposed this at `QuestionableHarmfulPanther.slpz:3110:p0` and
   `WellWornSmallGoshawk.slpz:{5180,5322}:p0`. Source anchors:
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
   point (Marth ThrowF/ThrowLw and Sheik ThrowLw publish; Marth ThrowB and
   Fox/Falco controls do not). Probe at least one forward throw/CaptureCut, one adjacent
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
   `QuestionableHarmfulPanther.slpz:231..265:p0`: the wrong final facing mirrored
   terminal DamageFlyTop hurtcaps and changed the later `AttackHi3` BODY contact.
   Add an in-range throw positive and an out-of-range adjacent throw negative for
   each new throw table family. Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4`,
   `refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Thrown.s::ftCo_800DE7C0`,
   and `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0`.
6. **Low-throw damage-state argument audit**: low throws do not merely override
   the knockback angle to vertical. `ftCo_800DD724` calls
   `ftCo_800DE7C0(victim, thrower, fp->motion_id == ThrowLw)`, and the thrown
   helper passes a non-`-1` damage-state argument to `ftCo_8008DCE0`. That
   argument first promotes the damage severity to the tumble/fly path, then
   forces the final damage motion to `DamageFlyTop`. If a new character's
   low-throw release rows match percent, hitstun, and vertical velocity but stay
   in `DamageAir*`, audit this arg lane rather than fitting the visible angle.
   Add low-throw positives and forward/back/up throw negatives. Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724`,
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_800DE7C0,calcKnockbackAngle}`,
   and `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0`.
7. **Pre-release throw BODY hitbox audit**: throw scripts can carry ordinary
   `create_hitbox` capsules before `set_throw_flags(0)` releases the victim.
   While the defender is still attached in `Thrown*`, source can publish damage
   plus thrower-side hitlag/bookkeeping without starting victim hitlag or
   entering Damage*. Do not treat these capsules as `set_throw_hitbox` release
   damage, and do not route attached-victim throw BODY contacts through the
   ordinary non-attached BODY victim-hitlag owner. Add an attached-victim
   positive and a non-attached negative around the same BODY overlap. Source
   anchors: `data/moves/<char>.json::moves.ftCo_SM_Throw*` `create_hitbox`
   versus `set_throw_hitbox` / `set_throw_flags`,
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724`,
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_800DE508,ftCo_Thrown*_Phys,ftCo_Thrown*_Coll}`,
   and `refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007891C}`.
8. **CapturePulled/Wait/Damage attachment pose audit**: grounded catch-connect
   entry (`fn_800DAADC`) and steady victim Phys (`fn_800DAD18`) are different
   source phases. Grounded entry installs CapturePulledLw and publishes only the
   immediate collision callback; the next steady CapturePulled/Wait/Damage Phys
   aligns victim XRotN to the grabber's `capturedamage.x18` by sampling live JObj
   world positions through `lb_8000B1CC` after AObj interpretation. Do not reuse
   integer SSANIM pose matrices for this delta just because the spacie rows are
   close: Marth CatchPull exposed a persistent X drift at
   `QuestionableHarmfulPanther.slpz:191..264:p0`. Add a steady pulled/wait
   positive and a grounded connect negative for every new character with a remapped
   grab/capture anchor. Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{fn_800DAADC,fn_800DAD18}`,
   `refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim`, and
   `refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC`.
9. **Offensive hitbox TransN pose audit**: sword/limb hitboxes are published
   from the live JObj matrix via `lb_8000B1CC`, not just the stripped SSANIM01
   joint matrix. If the animation's `data/anims/<char>.tracks.bin`
   `uses_root_motion` bit (`fp->x594_b0`) is clear, active hitboxes under
   FtPart_XRotN may need the SSANIM01 TransN tail recomposed before root facing.
   If `uses_root_motion` is set, Phys has already consumed that TransN into
   `cur_pos` through `ft_80085030`, so adding it again will double-shift spacie
   root-motion attacks. Marth AttackS4 exposed this at
   `ParallelFamiliarZebra.slpz:1838:p0`: Dolphin selected the 14-damage hb0 sour
   hit using a live sword JObj matrix equal to the extracted matrix plus the
   current TransN tail; without the tail MSL selected the 20-damage hb3 tipper.
   Add a motivating new-character positive and a root-motion negative (Fox/Falco
   AttackS4 is a good control) when auditing sword/contact rows. Source anchors:
   `refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C`,
   `refs/melee/src/melee/ft/ft_081B.c::{ft_80085030,ft_800850E0}`,
   `refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC`, and
   `data/anims/<char>.{bin,tracks.bin}`.
   Also audit the victim BODY owner before changing attacker hitboxes: sword
   rows can expose missing opponent dynamic-chain collision data. WWS:2580
   looked like a Marth Fair hitbox-order bug, but Dolphin `lbColl_8000805C`
   showed Marth hb0/hb1/hb2 missing and hb3 hitting Fox's live EscapeAir tail
   chain. The retained fix was adding Fox `ftCo_SM_EscapeAir` (`msid=44`) to
   the generated `SSDYNN01` collision-owner index, with WWS no-hit adjacent
   controls, not a Marth Fair branch. Source anchors:
   `refs/melee/src/melee/ft/ftdynamics.c::{ftCo_8009DD94,ftCo_8009E318}`,
   `refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}`,
   `refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C`, and
   `data/anims/fox.dyn.bin`.
9. **HitCapsule damage publication audit**: do not recompute stale damage at
   shield/BODY contact time. Source `create_hitbox` and `set_hitbox_damage`
   commands call `ftColl_8007ABD0`, which applies scale/smash-release damage
   and `ft_80089228` staling before writing `HitCapsule.damage`; later
   `ftColl_80076CBC` / BODY hit processing consumes that frozen value. Audit a
   new character's active-hitbox rows with repeated stale moves, especially
   sword multi-hitbox attacks whose shield hitlag can look correct while shield
   HP still exposes the selected capsule. Keep exact ShieldDesc packet
   selection separate from stale damage publication: Marth Fair fully exposed
   the stale-freeze rule, while Marth d-tilt still points at exact
   `lbColl_80007BCC` packet geometry as the remaining owner. Source anchors:
   `refs/melee/src/melee/ft/ftaction.c::{ftAction_8007121C,ftAction_8007162C}`,
   `refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007ABD0,ftColl_80076CBC}`,
   `refs/melee/src/melee/ft/ft_0881.c::{ft_80089118,ft_80089228}`, and
   `data/scripts/<char>.bin::MSLFTSC1`.
10. **DamageFlyRoll selected-source audit**: for high-KB grounded entries, do
   not trust visible animation labels or character id as the gate owner.
   Source `ftCo_8008DCE0` can clear `ground_or_air` inside the severe grounded
   branch and then sample the airborne DamageFlyRoll RNG gate. Classify these
   rows from the captured pre-damage action plus selected DmgLog HitCapsule
   payload. Marth exposed this at `InternalPowerlessWallaby.slpz:3983:p0`: the
   seed action is `Wait`, the submotion looks Fall-like, but source selected
   Marth `AttackLw3` hb0 (`damage=9`, `angle=30`, `kbg=40`, `bkb=40`) before
   the terminal roll gate. Add a positive, adjacent controls, and a mutation or
   synthetic negative proving that visible grounded state alone does not admit
   the roll. Source anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0`,
   `refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}`, and
   `data/moves/<char>.json::moves.<AttackLw3-like submotion>.events`.
11. **Catch/CatchDash collision-skeleton scale audit**: do not assume the
   spacie catch-scale correction applies uniformly to a new character's grab
   scripts. Audit `data/moves/<char>.json::{ftCo_SM_Catch,ftCo_SM_CatchDash}`
   `create_hitbox` bone ids and lock standing-Catch positives/negatives
   separately from dash-grab positives. The current source-backed split is:
   non-root authored Catch capsules and CatchDash keep the existing
   `lbColl_80007ECC` collision-skeleton scale counterfactual, while
   root-authored standing Catch stays on the root-scaled pose path. Marth
   required this because standing Catch root bubbles should connect on
   `VictoriousSpitefulAlpaca.slpz:6788` / `RipeWealthySeahorse.slpz:1811` but
   should not over-admit simultaneous standing Catch at
   `InternalPowerlessWallaby.slpz:7737`; Marth root CatchDash still connects at
   `InternalPowerlessWallaby.slpz:7311`. Source anchors:
   `refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C`,
   `refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078A2C,ftColl_8007AD18}`,
   and `refs/melee/src/melee/lb/lbcollision.c::lbColl_80007ECC`.
12. **CommonFall/FallAerial/FallSpecial hidden pose blend audit**: new-character
   contact bugs can be caused by the opponent's common-state live pose, not the
   new character's hitboxes. `ftCo_Fall_Anim_Inner` maintains hidden
   `mv.co.fall*.x4` from air-drift ratio and stores the selected FallF/FallB
   family `smid` before collision. Because this scalar is history-owned, a
   one-step replay seed cannot infer it from only the current row's drift or
   action age; preprocessing must carry the prefix recurrence explicitly when
   Fall-family collision or BODY pose depends on the hidden blend. The
   publication path is local-SRT, not final
   matrix interpolation: `ftCo_800CC988` calls `ftAnim_8006FE9C` starting at
   `FtPart_TransN`, so `TopN` and other pre-TransN ancestors stay on the active
   selected submotion while TransN descendants are blended through
   `lb_8000C490`. Slippi does not serialize `x4`; one-step reseed must seed it
   from replay prefix history, and free-running runtime must carry the hidden
   entry-frame recurrence because a visible post-frame
   `action_frame==0` Fall-family row has already passed the source Anim owner.
   Locks for this owner should assert the hidden `x4/smid` lane directly before
   BODY/contact selection and compute expected values from
   `data/common/ft_common_data.json` plus the row character's
   `data/characters/<char>.json::air_drift_max`; do not bake Fox/Falco drift
   caps, common-data thresholds, lerp factors, or Fall-family submotion ids into
   the test as duplicated literals.
   If a Fall-family surface row still disagrees after the BODY pose is correct,
   separate hidden CollData ECB seed reconstruction from live `mpColl` behavior.
   Slippi does not serialize CollData current/desired ECB; a new character may
   need an explicit `data/characters/<char>.json::common_fall_blended_ecb_seed_mask`
   only when source/probe evidence shows reseeded Fall/FallAerial/FallSpecial
   rows consume the hidden CommonFall ECB packet. Reconstruct that packet from
   the same live collision-pose JObj matrices used by `mpColl_LoadECB_JObj`;
   do not linearly interpolate extracted fixed ECB extents as a shortcut.
   Sheik exposed this for ordinary
   `Fall`: a bounded `mpColl_80047E14` / `mpColl_80044628_Floor` probe showed
   vanilla accepting/rejecting the same floor sweep from CommonFall-blended ECB
   bottoms, not from the raw neutral Fall table, and the Battlefield hard-floor
   boundary (`ToughOutlyingChicken.slpz:3812/3813`) proved the scalar extent blend
   was still too low. Do not promote that to a shared free-running collision
   rule without aggregate controls or a direct `mpColl_LoadECB_inline` /
   `mpCollInterpolateECB` probe.
   Sustained ledge-adjacent `EscapeAir_Coll` wall publication is a separate
   explicit character overlay:
   `data/characters/<char>.json::escapeair_carried_floor_wall_source`. Only set
   it when source/probe evidence shows the character's live carried ledge-floor
   provenance should publish the generated `MSLSTG01` adjacent wall after floor
   rejection. Sheik/Zelda currently set this; Fox/Falco/Marth are controls that
   reject a broad all-character sustained owner.
   Marth UAir exposed the strong positive on `LoudDullGoat.slpz:4539:p0`;
   aggregate Falco rows `DistinctCaringCobra.slpz:5573:p1` and
   `ImpassionedAlarmedTarsier.slpz:6428:p1` exposed the tight no-hit and
   entry-tick positive controls. Add a motivating positive plus adjacent
   no-hit/entry-phase controls before adjusting hitbox endpoints. Source
   anchors:
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Anim_Inner`,
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_800CC988`,
   `refs/melee/src/melee/ft/ftanim.c::ftAnim_8006FE9C`,
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallAerial.c::ftCo_FallAerial_Anim`,
   `refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_FallSpecial_Anim`,
   and `data/common/ft_common_data.json::{common_fall_blend_air_drift_threshold,common_fall_blend_lerp}`.
13. **Grounded Damage -> Wait_IASA ordering audit**: terminal grounded
    DamageHi/N/Lw delegates to the full `ftCo_Wait_IASA` ordering once
    `x221C_b6` clears. Catch input is before grounded A-attacks, so raw Z
    synthesis (`held_inputs` LR plus `input.x668` A) must enter Catch instead
    of being consumed as Attack11. Marth exposed this at
    `VictoriousSpitefulAlpaca.slpz:387:p0`. Add a positive with Z/LR+A catch
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
   Character-specific ext-attr layouts must be promoted in the extractor before
   runtime code lands. Sheik uses `ftSeak/types.h::ftSeakAttributes`, exported
   as `data/characters/sheik.json::sheik_*`; do not copy constants from replay
   rows or infer them from another character's attr struct.
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
   - KneeBend is not a generic grounded special resolver callsite.
     `ftCo_KneeBend_IASA` calls `ftCo_Attack100_CheckInput`, so the only
     special it can enter is Up-B through the `x686 == 0` up+B presence lane.
     Do not let diagonal B become Side-B/Neutral-B/Down-B from KneeBend just
     because Wait resolves those directions. Sheik exposed this with
     `sheik_demo_game.slpz:{1259,1786}:p0`.
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
   - Ground-jump IASA is part of the aerial special surface. `ftCo_Jump_IASA`
     calls `ftCo_SpecialAir_CheckInput` before item throw, EscapeAir,
     AttackAir, and JumpAerial checks, so a fresh B edge during JumpF/JumpB can
     enter `SpecialAir*` directly. Do not whitelist only Fall/FallAerial and
     JumpAerial when adding a new character's aerial special dispatcher.
4. **Grounded phys owners**: ground specials use ft_80084F3C/ft_80084FA8
   (friction + anim-root-motion exchange) — wire for all the char's ground
   specials or they'll slide/stick wrongly.
   Grounded Side-B may also have common pre-dispatch `ftCo_SpecialS_CheckInput`
   / `doEnter` velocity writes before the character-specific enter callback.
   Audit the complete entry path with probe/decomp before retaining runtime
   behavior: Marth `LoudDullGoat.slpz:713:p1` proved both the common xB8 damping
   and a later vanilla `0.25x` write before S1 physics. Modeling only the first
   write regressed rollout, so the owner must be completed through script /
   action-entry data rather than copied as a local partial constant.
5. **Ground<->air swaps**: frame-preserving action swaps for every special that
   can cross the boundary mid-move (walk-off, landing, Stadium transform).
   Test air->ground AND ground->air per family; document the ones with no
   reachable seed surface.
   Special action ids do not imply submotion ids by offset. Use the generated
   MotionState action->submotion table (`MSLMSO01`) for every special entry and
   action swap. Sheik Vanish is the practical trap: the travel action enters at
   frame 35 with anim rate 0 while keeping the start submotion, so an
   action-offset assumption publishes the wrong pose/ECB.
   Special families also need per-state collision callback audits, not one
   blanket "same move swaps ground/air" rule. Sheik Chain proved that
   Start/Active/End use source-distinct callbacks: aerial Start/Active/End call
   `ft_80081D0C`, grounded Start/Active/End call `ft_800827A0`, aerial Start
   floor contact swaps to grounded Start, but aerial Active floor contact enters
   aerial retract and grounded Active floor loss enters grounded retract. Put the
   callback classes in `MSLMSO01` and lock active-vs-start boundaries so future
   characters do not inherit a plausible but wrong generic special swap helper.
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
   Persistent per-character special state is its own seed/provenance audit.
   Sheik Needles use `fv.sk.x0` for stored needle count; timers/latches live in
   move-vars. If one-step replay reconstruction needs those lanes, add explicit
   seed fields and native preprocessing derivation before claiming replay
   exactness. For article-publishing specials, lock the same-frame publication
   boundary separately: source may create a held article in an Anim callback or
   a thrown article from an accessory callback before ordinary item simulation
   advances it.
   Common Landing IASA is also a character-special dispatch site, not only a
   locomotion/attack callback. `ftCo_Landing_IASA` gates on normal landing lag,
   then checks grounded specials before grounded attacks and guard, while
   `LandingAir*` IASA is empty. When adding a character's B-special resolver,
   wire a source-ordered Landing-family entry path with early-lag and
   LandingAir negatives; do not rely on a late generic Wait/Fall special update
   pass to catch Landing rows after attack/guard has already run.
   Special-spawned articles need their own source-owner audit beyond the
   fighter MotionState code. For Sheik, thrown Needle correctness required
   MSLITAR1 article extraction from `ftSk_Init_OnLoad` / `itseakneedlethrown.c`
   / `it_8027163C`, plus runtime item hurtbox contact, DmgReceived callback,
   item hitlag, and fighter hitlist locks. Do not treat "the special enters and
   spawns an item" as complete unless the article's collision/callback lifecycle
   is also data-backed and covered by positive/negative controls. Audit
   publication, lifetime, and destruction as separate owners: Sheik Vanish smoke
   spawns from `fn_80112ED8` installed by both normal travel entry and Start0
   ground/air collision swaps, then `itseakvanish` owns the lifetime decrement;
   Sheik Chain spawns at `ftSeakAttributes::x1C` and destroys at `x28`. A
   source-correct MotionState transition can still leave stale or missing
   article rows if the item owner is not modeled.
   Do not infer article publication from the final action serialized at
   post-frame. Sheik Needle Start creates the held article in `Start_Anim`
   immediately before entering Loop, and Loop IASA may enter End in the same
   fighter proc. The held article is still Start-owned. Conversely, held-item
   destruction can be item-Anim-owned before fighter Anim/IASA clears the
   pointer on a later frame; Sheik's held Needle survives the same-frame
   Loop-to-Cancel transition because `itseakneedleheld` samples `fv.sk.x4`
   before `ftSk_SpecialNCancel_Anim` clears it, then clears on the next item
   Anim tick. Live articles also need live item Anim/Phys/Coll callbacks; keep
   replay-only seed bridges limited to hidden provenance such as hitlag or
   source-owned latches, not as a gate around ordinary free-running item
   simulation. Finally, separate article state publication from article
   geometry: Sheik Chain state transitions are fighter callback/timer-owned,
   Chain spawn publication is anchored to the source part sampled by
   `lb_8000B1CC` (`FtPart_L3rdNa` for Sheik) rather than fighter root, and
   Vanish smoke article publication similarly samples `FtPart_HipN` before the
   item spawn. These spawn anchors are article-publication owners; full Chain
   segment pose and hitboxes and Vanish smoke BODY damage require separate article
   physics/extraction audit. For projectile articles, do not collapse item
   hurtboxes and item HitCapsules into one field: Sheik Needle's Article
   hurtbone descriptor owns fighter-HitCapsule-to-item DmgReceived contact,
   while its state-0 item script HitCapsules own item-BODY damage into
   fighters. Extract both surfaces and lock grounded/aerial target flags plus
   adjacent no-contact rows. For hit-capable articles, do not confuse article
   visual lifetime with HitCapsule active lifetime, and watch for common item
   setup overwriting local lifetime seeds: Sheik Vanish
   writes a local 60-frame lifetime, then `it_8027518C` overwrites xD44 from
   `ItemCommonData::xF8` to the replay-visible 80-frame lifetime. The state-0
   item script still shrinks the HitCapsule at frames 7 and 11 and removes it
   at frame 13. Extract timed set-size/remove events for hit-capable articles
   instead of keeping the create_hitbox payload active until item destruction;
   retain runtime damage only after the item collision/update phase is bounded
   by source or probe.
9. **GATE**: all specials tests green; fox/falco validate-all "no suite total
   changes" (byte-stable) after every retained change.

## Phase 5 — Replay burn-down (gate: remaining rows classified char-boundary vs shared debt)

Methodology: report diff -> witness -> owner (validation reports plus
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
   use explicit replay path + record + player coordinates to capture a
   frame window around any validation row. The v12 dump records the hidden
   fighter lanes (`fp+0x670/0x674` input timers, `fp+0x2344/48/4C` capture
   words, TransN position, `fp+0x1A50` GrabMash latches) plus per-frame
   hitbox world centers, items, hitlists. Alignment that always holds:
   dump row R = end-of-frame R-1 state + the input record for frame R;
   validation seed row F = dump row F+1. Validate a derivation by diffing the
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
- **Validation reports must be regenerated after ANY seed-derivation change.**
  Normal validation derives seed lanes directly from replay files; rerun the
  affected validation suites instead of relying on stale derived artifacts.
- **Baseline-stash attribution before repinning stale locks.** When
  newly-runnable tests fail (e.g. after adding local debug replays from
  `replays/debug/*.slp`), prove provenance before touching pins: stash the
  working diff, rebuild, rerun the failures, and compare against a baseline
  validation-buffer build when needed. Repin only rows proven to now match
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
   `tools/viewer/assets/character_zips.tsv` (zip + sha + slippilab URL; sha
   must match `refs/slippilab/public/zips/<char>.zip` if that checkout is
   present, since `fetch_assets.sh` prefers it over the network),
   `tools/viewer/live/schema.js`, `tools/viewer/live/build_wasm.sh`
   `viewer_chars`, and main.js character wiring. Keep
   `tests/test_live_viewer_schema.py::test_live_viewer_supported_characters_have_packaged_animation_zips`
   and `test_live_viewer_supported_characters_are_required_by_wasm_build`
   green so a dropdown entry cannot ship without the Slippi renderer zip or
   without its simulator data in the WASM build preflight. Sheik exposed this
   as an infinite loading screen when `sheik.zip` was missing from the
   manifest. Then actually play: every special, ground and air, at ledges, on
   slopes, vs shield. Capture anything weird with the viewer's msltrace
   recording.
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
- Seed-derivation changes require rerunning the affected validation suites
  before any measurement means anything.
- GrabMash-family callbacks read fp inputs one serialized row behind the
  post-frame row they affect; anim-rate writes are post-advance (the engine's
  pre-input callback phase models the deferral — do not add another).
- After `make validate-all`, review `reports/validation/` diffs deliberately -
  restore only unintended regenerated reports, keep intended ones.
