from __future__ import annotations

from pathlib import Path

import json
import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE, read_dataset
from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)

ACT_WAIT = 0x000E
SM_WAIT = 2
STAGE_FD = 32
CHAR_FOX = 1
ACT_GUARD = 179
ACT_GUARD_SET_OFF = 181
ACT_DAMAGE_FALL = 38
ACT_DOWN_BOUND_D = 191


def _size(sizes: dict[str, int], key: str) -> int:
    if key in sizes:
        return int(sizes[key])
    return int(sizes[f"{key}_v0"])


def _run_rollout_window(dataset_path: Path, start: int, stop: int) -> tuple[np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    sizes = binding.sizes()
    seed_stride = _size(sizes, "seed")
    input_stride = _size(sizes, "input")
    compare_stride = _size(sizes, "compare")
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])
    raw = samples.view(np.uint8).reshape(len(samples), -1)
    out_bytes = np.zeros((1, compare_stride), dtype=np.uint8, order="C")

    def field_bytes(record: int, off: int, stride: int) -> np.ndarray:
        return np.array(
            raw[record : record + 1, off : off + stride],
            dtype=np.uint8,
            order="C",
            copy=True,
        )

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed_rollout(handle, field_bytes(start, seed_off, seed_stride))
        for record in range(start, stop + 1):
            binding.step_input(
                handle,
                field_bytes(record, prev_off, input_stride),
                field_bytes(record, input_off, input_stride),
            )
            binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape(1)[0].copy()
    finally:
        binding.destroy(handle)
    return samples["ref_t1"][stop].copy(), out


def _run_aggregate_rollout_window(dataset_path: Path, start: int, stop: int) -> tuple[np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    sizes = binding.sizes()
    seed_stride = _size(sizes, "seed")
    input_stride = _size(sizes, "input")
    compare_stride = _size(sizes, "compare")
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])
    raw = samples.view(np.uint8).reshape(len(samples), -1)
    out_bytes = np.zeros((1, compare_stride), dtype=np.uint8, order="C")

    def field_bytes(record: int, off: int, stride: int) -> np.ndarray:
        return np.array(
            raw[record : record + 1, off : off + stride],
            dtype=np.uint8,
            order="C",
            copy=True,
        )

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, field_bytes(start, seed_off, seed_stride))
        for record in range(start, stop + 1):
            binding.step_input_replay_frame_rng(
                handle,
                field_bytes(record, seed_off, seed_stride),
                field_bytes(record, prev_off, input_stride),
                field_bytes(record, input_off, input_stride),
            )
            binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape(1)[0].copy()
    finally:
        binding.destroy(handle)
    return samples["ref_t1"][stop].copy(), out


@pytest.mark.integration
@pytest.mark.parametrize("record", [940, 941])
def test_common_grounded_player_nudge_before_grab_connect(record: int) -> None:
    # Replay-real lock for the common grounded fighter-overlap nudge lane:
    # - Fighter_8006A360 runs ftCommon_8007E0E4 before Fighter_procUpdate.
    # - ftCommon_8007DD7C accumulates +/-p_ftCommonData->x450 on horizontal pushbox overlap.
    # - Fighter_procUpdate applies xF8_playerNudgeVel before self/KB velocity integration.
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, record, 1)

    # Keep this scoped to the grab-connect neighborhood that exposed downstream rollout drift.
    assert int(seed["action_id"][1]) == 212
    assert int(ref["action_id"][1]) in (212, 213)
    assert int(out["action_id"][1]) == int(ref["action_id"][1])

    for p in (0, 1):
        for field in ("action_id", "action_frame", "on_ground", "hitlag", "hitstun"):
            assert int(out[field][p]) == int(ref[field][p]), f"p{p} {field}"
        assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=2e-6), (
            f"p{p} pos_x"
        )
        assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-6), (
            f"p{p} pos_y"
        )


@pytest.mark.integration
def test_guardsetoff_turnover_nudge_uses_promoted_seed_prev_action_on_rollout() -> None:
    # Replay-real rollout lock for a GuardSetOff_Anim -> Guard handoff:
    # - Fighter_8006A360 runs GuardSetOff_Anim before ftCommon_8007E0E4.
    # - When GuardSetOff_Anim enters Guard, the same frame still owns the common x450 pushbox nudge.
    # - Rollout carries that source through seed_prev_action_id after prev_action_id has advanced to
    #   Guard, so the turnover bridge must consume seed-prev provenance without double-counting
    #   ordinary Guard nudge.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardSetOff_Anim,ftCo_800928CC}
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    p = 1
    handoff_record = 3860
    seed = samples["seed_t"][handoff_record]
    assert int(seed["action_id"][p]) == ACT_GUARD
    assert int(seed["seed_prev_action_id"][p]) == ACT_GUARD_SET_OFF

    ref, out = _run_rollout_window(dataset_path, 3851, handoff_record)
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_GUARD
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=2e-6)

    # The exact position guards both halves of the boundary: missing seed-prev provenance leaves the
    # row +0.3000 too far right, while double-counting common + turnover nudge would overshoot left.
    assert float(ref["pos_x"][p]) == pytest.approx(22.10211181640625, abs=2e-6)


@pytest.mark.integration
def test_damagefall_to_downboundd_first_frame_friction_not_double_applied_fsp_9392() -> None:
    # FSP rollout lock for the DamageFall_Coll -> DownBoundD landing owner:
    # - DamageFall_Coll routes through ftCo_80090984 into the same DownBound entry ladder used by
    #   DamageFly floor contact.
    # - The first DownBoundD frame has already run DownBound_Phys / ft_80084F3C once; the generic
    #   grounded friction bucket must not apply a second step before publishing gr_vel.
    # - The immediate 9392 gr_vel/pos_x check protects the source owner. The downstream 9553
    #   GuardSetOff check proves the repaired position reaches the real shield contact without a
    #   shield-geometry shortcut.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::{
    #   ftCo_DamageFall_Coll,ftCo_80090984}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Phys
    # refs/melee/src/melee/ft/ft_084E.c::ft_80084F3C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    seed_9392 = ds.samples["seed_t"][9392]
    assert int(seed_9392["action_id"][1]) == ACT_DOWN_BOUND_D
    assert int(seed_9392["seed_prev_action_id"][1]) == ACT_DAMAGE_FALL

    ref_9392, out_9392 = _run_aggregate_rollout_window(dataset_path, 0, 9392)
    assert int(out_9392["action_id"][1]) == int(ref_9392["action_id"][1]) == ACT_DOWN_BOUND_D
    assert int(out_9392["action_frame"][1]) == int(ref_9392["action_frame"][1]) == 1
    assert float(out_9392["speed_ground_x_self"][1]) == pytest.approx(
        float(ref_9392["speed_ground_x_self"][1]), abs=2e-6
    )
    assert float(out_9392["pos_x"][1]) == pytest.approx(float(ref_9392["pos_x"][1]), abs=8e-6)

    ref_9553, out_9553 = _run_aggregate_rollout_window(dataset_path, 0, 9553)
    assert int(out_9553["action_id"][0]) == int(ref_9553["action_id"][0]) == ACT_GUARD_SET_OFF
    assert int(out_9553["hitlag"][0]) == int(ref_9553["hitlag"][0]) == 7


def test_common_grounded_player_nudge_exact_overlap_uses_player_order() -> None:
    # Decomp tie path:
    # - ftCommon_8007DD7C keeps a `phi_r28` flag while walking the global fighter GObj list.
    # - When pushbox centers are exactly equal, fighters before/after the current GObj pick opposite
    #   signs for p_ftCommonData->x450.
    # Sim tie contract: player slot order is the deterministic GObj-list proxy for this two-player
    # lite sim.
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007DD7C
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    common = json.loads((root / "data/common/ft_common_data.json").read_text())
    nudge_x = float(common["player_nudge_x"])

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["facing"][0, :2] = np.uint8(1)
    seed["on_ground"][0, :2] = np.uint8(1)
    seed["ground_id"][0, :2] = np.uint16(1)
    seed["pos_x"][0, :2] = np.float32(0.0)
    seed["pos_y"][0, :2] = np.float32(0.0001)
    seed["action_id"][0, :2] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, :2] = np.uint32(SM_WAIT)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)

    sizes = binding.sizes()
    seed_stride = _size(sizes, "seed")
    input_stride = _size(sizes, "input")
    compare_stride = _size(sizes, "compare")
    out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)
    neutral = np.zeros((1,), dtype=INPUT_DTYPE).view(np.uint8).reshape((1, input_stride))

    handle = binding.init(batch_size=1, num_players=2)
    try:
        binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        binding.step_input(handle, neutral, neutral)
        binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        binding.destroy(handle)

    assert float(out["pos_x"][0]) == pytest.approx(+nudge_x, abs=1e-6)
    assert float(out["pos_x"][1]) == pytest.approx(-nudge_x, abs=1e-6)
    assert int(out["on_ground"][0]) == 1
    assert int(out["on_ground"][1]) == 1


@pytest.mark.integration
@pytest.mark.parametrize("record", [8600, 8601, 8602, 8603, 8604, 8605, 8606])
def test_common_grounded_player_nudge_does_not_push_over_fd_right_floor_edge(record: int) -> None:
    # Negative lock for the floor-bound safety gate:
    # - Preflight caught GracefulAttachedTurtle rows 8599..8606 as new seed==ref on_ground rows when
    #   common x450 was allowed to move p0 from x=85.5656967 to x=85.8656998.
    # - That push crosses the current FD floor segment endpoint in this simplified mpColl model.
    # - Still-grounded Ottotto frames keep the common nudge floor-endpoint guard; the floor-loss
    #   exception is covered separately below.
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007E0E4
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::ftCo_Ottotto_Coll
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, record, 0)
    p = 0
    assert int(seed["on_ground"][p]) == 1
    assert int(ref["on_ground"][p]) == 1
    assert int(out["on_ground"][p]) == 1
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=2e-6)


@pytest.mark.integration
def test_common_grounded_player_nudge_can_cross_connected_fd_floor_seam() -> None:
    # Replay-real positive for the x450 nudge at a connected FD floor seam:
    # - ftCommon_8007E0E4 computes xF8_playerNudgeVel before Fighter_procUpdate.
    # - Squat_Coll then runs ft_80082708 -> mpColl_8004B108, which may resolve the new floor.index
    #   after the nudge crosses the right-lip/main-floor endpoint.
    # - This is not the same as pushing past the outer floor edge; that negative remains covered by
    #   GracefulAttachedTurtle 8600..8606 above.
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082708
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, 8338, 1)
    p = 1
    assert int(seed["action_id"][p]) == 39  # Squat
    assert int(seed["ground_id"][p]) == 2  # FD right lip floor
    assert int(ref["ground_id"][p]) == 1  # FD main floor after connected-seam nudge/collision
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p])
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=2e-6)


@pytest.mark.integration
def test_ottotto_kneebend_floor_loss_carries_source_player_nudge() -> None:
    # Replay-real positive for the same x450 owner at the Ottotto edge-loss boundary:
    # - Ottotto_IASA enters KneeBend from a jump input.
    # - KneeBend_Coll immediately loses the floor and enters Fall.
    # - Source computes ftCommon_8007E0E4 / xF8_playerNudgeVel.x before that callback, so the
    #   outgoing Fall row keeps the +x450 placement.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::ftCo_Ottotto_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Coll
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, 8607, 0)
    p = 0
    assert int(seed["action_id"][p]) == 245  # Ottotto
    assert int(seed["on_ground"][p]) == 1
    assert int(ref["action_id"][p]) == 29  # Fall
    assert int(ref["on_ground"][p]) == 0
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p])
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=2e-6)


@pytest.mark.integration
def test_fod_squatrv_kneebend_floor_loss_uses_ft80083f88_nudge_owner() -> None:
    # Replay-real positive for the same x450 source owner through an ft_80083F88 collision callback:
    # - SquatRv_IASA enters KneeBend from a jump input while overlapping a grounded peer.
    # - Fighter_8006A360 computes xF8_playerNudgeVel before Fighter_procUpdate.
    # - KneeBend_Coll calls ft_80083F88 -> ft_80082708 -> mpColl_8004B108, so the nudged FoD
    #   top-platform edge position can leave ground and enter Fall on that same frame.
    # data/motion_state/owners/{fox,falco}.bin::MSLMSO01 class FT80083F88_GROUND_TO_AIR_COLL
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SquatRv.c::ftCo_SquatRv_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Coll
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80083F88,ft_80082708}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "MilkyGracefulStingray.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, 616, 1)
    p = 1
    assert int(seed["action_id"][p]) == 24  # KneeBend
    assert int(seed["seed_prev_action_id"][p]) == 39  # SquatRv
    assert int(ref["action_id"][p]) == 29  # Fall
    assert int(ref["on_ground"][p]) == 0
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p])
    assert int(out["jumps_left"][p]) == int(ref["jumps_left"][p])
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=2e-6)
