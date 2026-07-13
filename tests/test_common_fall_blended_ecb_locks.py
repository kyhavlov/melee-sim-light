from __future__ import annotations

import numpy as np


def _one_step_action(replay: str, ports: list[int], rec: int, p: int) -> tuple[int, int]:
    import msl_binding

    from tests.replay_buffers_loader import load_replay_buffers

    loaded = load_replay_buffers(replay, ports=ports)
    b = loaded.buffers
    handle = msl_binding.init(batch_size=1, num_players=loaded.num_players,
                              ucf_enabled=1, ucf_cardinals_1_0_enabled=0)
    try:
        from tools.eval.validation_dtypes import COMPARE_DTYPE

        out = np.zeros((1, int(msl_binding.sizes()["compare"])), dtype=np.uint8)
        msl_binding.reseed_seed(handle, b.seed_u8()[rec:rec + 1])
        msl_binding.step_input(handle, b.prev_input_u8()[rec:rec + 1], b.input_u8()[rec:rec + 1])
        msl_binding.write_compare(handle, out)
        row = out.view(COMPARE_DTYPE).reshape(())
        return int(row["action_id"][p]), int(loaded.rows.ref_t1["action_id"][rec, p])


    finally:
        msl_binding.destroy(handle)


def test_fox_fall_blended_ecb_bottom_keeps_backward_drift_fall_airborne() -> None:
    # CommonFall directional-blend ECB bottom (fox mask bit 1<<0): fox's live CollData bottom
    # during backward-drift Fall is the Fall<->FallB blend (3.86-3.91 -> 4.73-4.87), which keeps
    # the swept bottom above the BF side platform where the unblended table lands a frame early.
    # Probe witness: MSL_FALL_FLOOR_PROBE on Game_20260313T121034 frames 3570-3575 (p0 fox,
    # ecb/desired bottom 4.467..4.738 with x130=0, no landing until frame 3575).
    out_act, ref_act = _one_step_action(
        "replays/validation/puff/Game_20260313T121034.slpz", [1, 2], 3696, 0)
    assert ref_act == 29
    assert out_act == 29


def test_fox_fall_blended_ecb_bottom_owns_retired_stale_platform_suppressor_witness() -> None:
    # SDS rec 7169 (Dream Land, fox fastfall through the floor line at x~-18 while the blended
    # bottom keeps him above it) was previously enforced by the
    # suppress_fall_stale_platform_first_hard_floor_land facing-vs-drift proxy; the blended
    # ECB bottom now owns the row directly.
    out_act, ref_act = _one_step_action(
        "replays/validation/dream_land_recent/ShadyDecimalStarling.slpz", [1, 2], 7169, 0)
    assert ref_act == 29
    assert out_act == 29


def test_puff_walkoff_shallow_hard_floor_landing_not_suppressed() -> None:
    # The retired proxy over-suppressed genuine landings for characters whose Fall bottoms clamp
    # to the root: puff walk-off from the YS left platform lands on the main floor via an
    # ordinary sweep (probe witness: frame 670 root 1.11 -> -0.19 with bottom 0, ret=1).
    out_act, ref_act = _one_step_action(
        "replays/validation/puff/wallbounce_teeter_ys.slpz", [1, 2], 792, 0)
    assert ref_act == 42
    assert out_act == 42


def test_falco_fall_blended_ecb_bottom_keeps_shallow_fastfall_airborne() -> None:
    # Falco mask bit 1<<0: probe witness MSL_FALL_FLOOR_PROBE on ToughOutlyingChicken p1 (port 4)
    # frames 6216-6224 -- plain-Fall CollData bottoms 5.25-5.55 with x130=0 (the CommonFall
    # directional blend, far above the raw Fall table) keep him airborne through frame 6223;
    # the game lands only at frame 6224. The unblended table lands one frame early.
    out_act, ref_act = _one_step_action(
        "replays/validation/sheik/ToughOutlyingChicken.slpz", [2, 4], 6345, 1)
    assert ref_act == 29
    assert out_act == 29


def test_sheik_blended_hurt_pose_owns_marth_uair_no_hit() -> None:
    # The reseeded seed-lane x4/smid stay live in the runtime blend lanes for data-mask owners:
    # the game's mv.co.fall.x4 poses the JObj (hurt bones), not only the CollData ECB
    # (refs/melee ftCo_Fall.c::ftCo_800CC988 re-applies x4 to the skeleton every frame).
    # UnusedLivelyLouse rec 2843: game sheik falls (x130=16, Fall f4) past marth's rising uair
    # with no contact; the unblended hurt pose swings her bones into the swing (full phantom hit).
    out_act, ref_act = _one_step_action(
        "replays/validation/sheik/UnusedLivelyLouse.slpz", [2, 4], 2843, 0)
    assert ref_act == 29
    assert out_act == 29


def test_falcon_fall_blended_ecb_bottom_delays_edge_landing_one_frame() -> None:
    # Falcon mask bit 1<<0: probe witness MSL_FALL_FLOOR_PROBE on FumblingSaneBeaver p1 (port 2)
    # frames 5050-5057 -- Fall entry ramps mv.co.fall.x4 0.44 -> 0.755 (smid 22) and the CollData
    # bottom rises 1.998 -> 3.677; the game lands at frame 5057 where the unblended table lands
    # earlier. (The companion frame-5056 margin row rec 5178 is locked separately below via the
    # +1 alternate-frame witness.)
    out_act, ref_act = _one_step_action(
        "replays/validation/falcon/FumblingSaneBeaver.slpz", [1, 2], 7030, 1)
    assert ref_act == 29
    assert out_act == 29


def test_fox_blended_shallow_fastfall_landing_lands_like_source() -> None:
    # The shallow-fastfall depth rule is a stand-in for the unblended Fall bottom and must not
    # apply when the data mask marks the blended ECB as the collision consumer: probe witness
    # MSL_FALL_FLOOR_PROBE on ElatedWearyTermite p0 frame 5843 -- the game sweeps the blended
    # bottom (4.204 -> 4.273, x4 0.4726 -> 0.5349 matching the seed lane bit-for-bit) across the
    # FoD floor at depth 0.21 and lands.
    out_act, ref_act = _one_step_action(
        "replays/validation/fountain_of_dreams_recent/ElatedWearyTermite.slpz", [1, 2], 5965, 0)
    assert ref_act == 42
    assert out_act == 42


def test_fox_blended_hurt_pose_alt_frame_keeps_illusion_clean_hit() -> None:
    # The alternate FallF/FallB skeleton runs one frame ahead of the base cur_anim_frame
    # (ftAnim_8006EDD0 reload + one advance per HSD_JObjAnimAll call; see
    # anim_pose.c::common_fall_blend_alt_anim_frame). JObj blend probe witnesses:
    # PutridJoyousOryx f5257 alt evals 1.0/2.0 on the reload frame and f5263 alt 0.0 vs base 7.0.
    # With the alternate pose sampled at base frame the blended leg capsule drops the game's
    # clean Illusion hit into the phantom sliver (x7A8=0.01, game overlap 0.122).
    out_act, ref_act = _one_step_action(
        "replays/validation/aggregate_recent/PutridJoyousOryx.slpz", [1, 2], 5385, 0)
    assert ref_act == 90
    assert out_act == 90


def test_falcon_blended_ecb_alt_frame_owns_edge_margin() -> None:
    # Same +1 alternate frame in the CollData ECB bottoms: FumblingSaneBeaver f5056 alt aobj 3.0
    # vs base 2.0 (probe); sampling the alternate at the base frame leaves the blended bottom
    # ~0.12 low against the game's 0.027 airborne margin and lands a frame early.
    out_act, ref_act = _one_step_action(
        "replays/validation/falcon/FumblingSaneBeaver.slpz", [1, 2], 5178, 1)
    assert ref_act == 29
    assert out_act == 29


def test_falco_reload_frame_compound_blend_owns_laser_graze_miss() -> None:
    # On the smid-switch (ftAnim_8006EDD0 reload) frame the consumed pose is the COMPOUND of two
    # ftAnim_8006FE9C passes: blend(blend(base@A, target@A, x4), target@A+1, x4) -- JObj blend
    # probe PositiveRevolvingHyena f8015 shows two lb_8000C490 passes with identical x4 whose
    # pass-2 base quaternion equals pass-1's output exactly. With the single-blend pose the
    # trailing falco laser beam offset ([-9.37] x scale 3.0) grazes p0's leg cap by 0.11 where
    # the game (compound pose, knee ~6.4deg more bent) misses by 0.08.
    out_act, ref_act = _one_step_action(
        "replays/validation/aggregate_recent/PositiveRevolvingHyena.slpz", [1, 2], 8137, 0)
    assert ref_act == 29
    assert out_act == 29


def test_falco_blended_bottom_lands_same_ledge_floor_crossing_this_frame() -> None:
    # The fall-same-floor-early one-frame delay is a stand-in for the unblended Fall bottom and
    # must not apply to blended-ECB owners: fall-floor probe PaleMajorEchidna p1 f3913 -- the
    # game's plain bottom sweep (blended bottoms 4.483 -> 4.518, x4 0.932 saturated) crosses the
    # Dream Land ledge floor at x -81.6 and lands this frame (ret=1, y snapped to 0).
    out_act, ref_act = _one_step_action(
        "replays/validation/sheik/PaleMajorEchidna.slpz", [1, 2], 4035, 1)
    assert ref_act == 42
    assert out_act == 42
