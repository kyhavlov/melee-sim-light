from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _step_one_row(dataset_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]
    assert int(row.shape[0]) == 1

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    try:
        seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, seed_stride
        ).copy()
        prev_input_bytes = np.frombuffer(
            row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
        ).reshape(1, input_stride).copy()
        input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        ).copy()
        out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
        seed = row["seed_t"][0]
        ref = row["ref_t1"][0]
        return seed, ref, out
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_kneebend_takeoff_frame_jump_iasa_can_enter_jumpaerial_dcc_4643() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    # Source ordering lock:
    # - ftCo_KneeBend_Anim enters JumpF/B in the anim callback once startup completes.
    # - The destination Jump IASA then runs later in the same proc and can consume a fresh
    #   current-frame jump edge/tap through ftCo_800CB870 into JumpAerial.
    # - JumpAerial_Enter_Basic overwrites the just-created ground-jump velocity and consumes the
    #   remaining jump, so the takeoff frame has no ground-jump X drift here.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::{
    #   ftCo_800CB870,ftCo_JumpAerial_Enter_Basic
    # }
    seed, ref, out = _step_one_row(dataset_path, 4643)
    p = 1

    assert int(seed["action_id"][p]) == 24  # KneeBend
    assert int(seed["action_frame"][p]) == 4
    assert int(seed["jumps_left"][p]) == 2

    assert int(ref["action_id"][p]) == 27  # JumpAerialF
    assert int(ref["action_frame"][p]) == 0
    assert int(ref["jumps_left"][p]) == 0

    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["action_frame"][p]) == int(ref["action_frame"][p])
    assert int(out["jumps_left"][p]) == int(ref["jumps_left"][p])
    assert np.isclose(float(out["pos_x"][p]), float(ref["pos_x"][p]), atol=1e-6)
    assert np.isclose(float(out["pos_y"][p]), float(ref["pos_y"][p]), atol=1e-6)
    assert np.isclose(float(out["speed_air_x_self"][p]), float(ref["speed_air_x_self"][p]), atol=1e-6)
    assert np.isclose(float(out["speed_y_self"][p]), float(ref["speed_y_self"][p]), atol=1e-6)


@pytest.mark.integration
def test_kneebend_takeoff_frame_release_keeps_full_jump_dcc_9255() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    # Source ordering lock:
    # - ftCo_KneeBend_Anim enters JumpF before ftCo_KneeBend_IASA can call
    #   ftCo_KneeBend_Check_ShortHop on the same frame.
    # - Releasing X/Y on the Anim-owned takeoff frame is therefore too late to convert the jump
    #   into a short hop; only an earlier latch or seeded latch may do that.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::{
    #   ftCo_KneeBend_Anim,ftCo_KneeBend_IASA,ftCo_KneeBend_Check_ShortHop
    # }
    seed, ref, out = _step_one_row(dataset_path, 9255)
    p = 0

    assert int(seed["action_id"][p]) == 24  # KneeBend
    assert int(seed["action_frame"][p]) == 2
    assert int(seed["kneebend_is_short_hop"][p]) == 0
    assert int(seed["kneebend_jump_input"][p]) == 3  # JumpInput_XY

    assert int(ref["action_id"][p]) in (25, 26)  # JumpF/B
    assert int(ref["action_frame"][p]) == 0
    assert np.isclose(float(ref["speed_y_self"][p]), 3.680000066757202)

    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["action_frame"][p]) == int(ref["action_frame"][p])
    assert np.isclose(float(out["speed_y_self"][p]), float(ref["speed_y_self"][p]))
    assert np.isclose(float(out["pos_y"][p]), float(ref["pos_y"][p]))


@pytest.mark.integration
def test_kneebend_seeded_short_hop_still_uses_hop_velocity_his_154() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/HungryImportantSnake.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    # Negative boundary: the takeoff-frame latch suppression must not erase a real short-hop bit
    # that was already produced by an earlier KneeBend_IASA frame and carried in the seed.
    seed, ref, out = _step_one_row(dataset_path, 154)
    p = 0

    assert int(seed["action_id"][p]) == 24  # KneeBend
    assert int(seed["action_frame"][p]) == 2
    assert int(seed["kneebend_is_short_hop"][p]) == 1
    assert int(seed["kneebend_jump_input"][p]) == 3  # JumpInput_XY

    assert int(ref["action_id"][p]) in (25, 26)  # JumpF/B
    assert int(ref["action_frame"][p]) == 0
    assert np.isclose(float(ref["speed_y_self"][p]), 2.0999999046325684)

    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["action_frame"][p]) == int(ref["action_frame"][p])
    assert np.isclose(float(out["speed_y_self"][p]), float(ref["speed_y_self"][p]))
    assert np.isclose(float(out["pos_y"][p]), float(ref["pos_y"][p]))


@pytest.mark.integration
def test_kneebend_dash_run_stick_threshold_seed_latches_short_hop_dcc_1989() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    # Seed bridge lock:
    # - p1 entered KneeBend from the Dash/Run-family IASA path. That path calls fn_800CAF78,
    #   which uses p_ftCommonData->x80 for L-stick jump detection instead of the ordinary
    #   ftCo_Jump_GetInput tap-jump x74 threshold.
    # - The replay-visible stick is between those thresholds, so mid-KneeBend reseeds must carry
    #   the latched LStick jump input and short-hop flag rather than guessing from current input.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::fn_800CAF78
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::{
    #   ftCo_KneeBend_Enter,ftCo_KneeBend_Check_ShortHop
    # }
    seed, ref, out = _step_one_row(dataset_path, 1989)
    p = 1

    assert int(seed["action_id"][p]) == 24  # KneeBend
    assert int(seed["action_frame"][p]) == 4
    assert int(seed["kneebend_jump_input"][p]) == 1  # JumpInput_LStick
    assert int(seed["kneebend_is_short_hop"][p]) == 1

    assert int(ref["action_id"][p]) == 26  # JumpB
    assert int(ref["action_frame"][p]) == 0
    assert np.isclose(float(ref["speed_y_self"][p]), 1.899999976158142, atol=1e-6)

    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["action_frame"][p]) == int(ref["action_frame"][p])
    assert np.isclose(float(out["speed_y_self"][p]), float(ref["speed_y_self"][p]), atol=1e-6)
    assert np.isclose(float(out["pos_y"][p]), float(ref["pos_y"][p]), atol=1e-6)
