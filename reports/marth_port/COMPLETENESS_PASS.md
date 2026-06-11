# Marth Completeness Pass (pre-burn, after check-in 3)

State: uncommitted on `newchar` (pending review). Goal: implement known missing/incomplete
Marth support decomp-first, independent of what the current replay exercises.

## Implemented (decomp-first)

1. Dancing Blade ground STOP-AT-LEDGE - data-driven, zero new C.
   - Root cause: marth's special collision callbacks (ftMs_*) were unclassified in the
     MSLMSO01 owners extraction, so all class bits were 0 and the grounded DB stages slid
     straight off edges while staying in the grounded action.
   - Fix: extract_motion_state_owners.py classifies the ftMs callbacks from their decomp
     bodies: ftMs_SpecialAirS1/S2/S3/S4_Coll + ftMs_SpecialLw_Coll grounded branches call
     ft_800827A0 -> CLASS_FT800827A0_EDGE_SNAP_COLL; the air branches + SpecialAirN*/AirLw*
     call ft_80081D0C -> CLASS_FT80081D0C_AIR_COLL. The existing generic consumers do the
     rest (x clamps at 85.5666 on FD, stays grounded, teeters).
   - refs/melee/src/melee/ft/chara/ftMars/ftMs_Special{S,N,Lw}.c (Coll handlers)
   - Test: test_db_ground_stops_at_ledge. Fox/falco owners bins byte-identical.

2. Ground <-> air variant swaps preserving animation frame (SB 341..344<->345..348, DB
   stages +/-9, Counter 369/370<->371/372).
   - marth_special_try_air_to_ground_swap / try_ground_to_air_swap (marth_specials.c) wired
     into the locomotion landing selector (before generic Landing) and the grounded
     floor-loss branch. Air->ground also clears the air-side-special freshness (fv.ms.x222C).
   - refs ftMs_SpecialS_80137748/80137CBC/80137D60, ftMs_SpecialN_80136A1C/80136DB4,
     ftMs_SpecialLw_80138D38/80138DD0 (all enter the paired msid at fp->cur_anim_frame).
   - Tests, full family coverage:
     air->ground: test_db_air_stage_landing_swaps_to_ground_stage_preserving_frame,
     test_sb_air_loop_landing_swaps_to_ground_loop,
     test_counter_air_landing_swaps_to_ground_counter (all three assert no generic Landing
     interposes).
     ground->air: test_sb_ground_walkoff_swaps_to_air_variant_preserving_frame - end-to-end
     through the one route reachable on static stages (SB's ft_80082708 colls allow walk-off;
     DB/Counter's ft_800827A0 stop-at-ledge class makes grounded floor loss unreachable
     outside Stadium transform floor removal, which has no synthetic seed surface; their
     mappings share the same helper/wiring and are covered by the air->ground direction).

3. Counter projectile/item intercept (generic over projectile owners).
   - Hook: combat_apply_item_hit (the single item-vs-fighter damage intake), gated by the
     same armed-window predicate and the descriptor sphere tested against the REAL projectile
     geometry: every caller now threads item_pos_y and the per-state contact radius
     (lp->size/state1_size or the live hp.radius). The knocked Shy Guy path passes its real
     contact capsule (the carried source hitbox hx/hy/hr - the same sweep tested against the
     defender's hurtcaps), so stage-item Counter uses true geometry too; the one caller with
     no live contact position (spawn-time attached laser) passes a negative radius and
     Counter FAILS CLOSED for that contact. End-to-end knocked-Shy-Guy counter rows are not
     synthesizable today (no per-item-slot seed surface; spawn is Yoshi's stage state
     machinery) - the path shares marth_counter_desc_overlaps_point with the laser tests.
     Consumes the projectile, stashes dmg*x5C, faces it, enters LwHit. No item-type or
     replay special-casing.
   - refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B1B8,ftColl_80077688}
   - Tests: test_counter_intercepts_projectile (in-descriptor laser countered, zero damage),
     test_counter_projectile_whiffs_after_window (late counter -> laser connects),
     test_counter_does_not_intercept_projectile_above/below_descriptor (BF platform-height
     lasers pass an armed counter untouched - these fail on any proxy-Y geometry).

4. Counter shield-strength hitlag floor (the x60 lane, previously extracted-not-consumed).
   - Source proof: ftColl_8007B1B8 stores da->x60 into fp->shield_unk0; the shield-contact
     tail copies shield_unk0 into BOTH fighters' x1964; Fighter_ProcessHit consumes x1964 as
     a hitlag-frames minimum (fighter.c: `if (x195c_hitlag_frames < x1964) ... = x1964`).
   - Implemented as a hitlag floor (11 frames for marth) on both sides of the fighter
     intercept and on the defender for the item intercept.
   - Test: test_counter_hitlag_floor_from_shield_strength (weak jab ~4f CalcHitlag -> 11f).

## Investigated and intentionally closed/deferred (with source evidence)

5. Cape/hair dynamic chains: PROVEN COSMETIC for collision - the long-carried deferral is
   CLOSED, not deferred.
   - Marth's 3 suppressed SSDYNN01 chains root at parts 45/50/55 (nodes 45-48, 50-53,
     55-58) and define ZERO collision capsules (empty collider lists; fox's tail chain by
     contrast has 4).
   - Every hurtcap bone lineage (incl. the suspected 60/70/71) ascends through static bones
     only: 60->59->22->21->4->...; 70/71->69->67->22->... - never through a chain node.
   - Encoded as a permanent data assertion: test_marth_hurtcaps_do_not_ride_suppressed_
     dynamic_chains (fails loudly if a future extraction/costume change violates either
     premise).

6. Deferred (unchanged, with reasons):
   - Counter sphere vs swept (prev->cur) hitbox segment: same-frame refinement on contacts
     already admitted by the swept hurtcap overlap upstream.
   - DB/Counter ground->air end-to-end rows: reachable only via Pokemon Stadium transform
     floor removal (FoD platforms sink INTO the stage and transfer the fighter; they never
     remove the floor under one). Stadium transform state has no seed surface today; the
     shared swap helper is end-to-end tested via SB walk-off and the per-family mappings via
     the three air->ground tests.
   - Cliff roll/jump option rows + pummel timing depth (CI2 dispositions stand).

## Validation

- Marth tests: 30 specials + 84 common-action = 114 passed.
- make build BUILD_FORCE=1: pass; make fmt-check: pass; diff checks clean.
- Full make test: 3259 passed, 1183 skipped.
- Fox/Falco validate-all: "no suite total changes", no regressions, no reds
  (fox/falco owners bins byte-identical through the classification change).
- Marth replay smoke (secondary evidence): one-step 241 -> 238 / 1,079,562; rollout
  first-mismatches 55 (unchanged); median streak 231.
