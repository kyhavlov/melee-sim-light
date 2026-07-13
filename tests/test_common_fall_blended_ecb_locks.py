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
    # earlier. (The companion frame-5056 row rec 5178 stays on the blend-pose fidelity ledger:
    # the sim's blended bottom is ~0.12 low against a 0.03 game margin.)
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
