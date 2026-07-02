from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


def _run_one_step_action_anim_ground(
    *,
    dataset_path: Path,
    record: int,
    player: int,
    expected_action_id: int,
    expected_anim_index: int,
    expected_on_ground: int,
) -> None:
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    num_records = int(samples.shape[0])
    assert num_records > record, f"replay too short for regression check: num_records={num_records}"

    view = samples[record : record + 1]
    assert int(view.shape[0]) == 1

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
        prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        seed_bytes[:] = np.frombuffer(view["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
        prev_input_bytes[:] = np.frombuffer(view["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        input_bytes[:] = np.frombuffer(view["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got_action_id = int(out["action_id"][0, player])
        got_anim_index = int(out["animation_index"][0, player])
        got_on_ground = int(out["on_ground"][0, player])
    finally:
        binding.destroy(handle)

    assert got_action_id == expected_action_id, (
        f"record={record} p={player} expected action_id={expected_action_id}, got {got_action_id}"
    )
    assert got_anim_index == expected_anim_index, (
        f"record={record} p={player} expected animation_index={expected_anim_index}, got {got_anim_index}"
    )
    assert got_on_ground == expected_on_ground, (
        f"record={record} p={player} expected on_ground={expected_on_ground}, got {got_on_ground}"
    )


@pytest.mark.integration
def test_knockdown_clusterA_damageflytop_does_not_spuriously_land_into_downboundu() -> None:
    # Cluster A representative:
    # - seed/ref = DamageFlyTop (90) but previous sim out = DownBoundU (183)
    # replays/.../AttachedGoodNaturedGuanaco.slpz record 1027 p=0
    root = Path(__file__).resolve().parents[1]
    rel = (
        "replays/validation/"
        "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz"
    )
    dataset_path = root / rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {rel}")

    _run_one_step_action_anim_ground(
        dataset_path=dataset_path,
        record=1027,
        player=0,
        expected_action_id=90,  # MSL_ACT_DAMAGE_FLY_TOP
        expected_anim_index=180,  # MSL_SM_DAMAGE_FLY_TOP
        expected_on_ground=0,
    )


@pytest.mark.integration
def test_knockdown_clusterB_downfowardd_does_not_drop_to_fall_on_fd_edge() -> None:
    # Cluster B representative:
    # - seed/ref = DownFowardD (196) but previous sim out = Fall (29)
    # replays/.../TreasuredBackKangaroo.slpz record 6075 p=1
    root = Path(__file__).resolve().parents[1]
    rel = (
        "replays/validation/"
        "cardinal_1.0_recent/TreasuredBackKangaroo.slpz"
    )
    dataset_path = root / rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {rel}")

    _run_one_step_action_anim_ground(
        dataset_path=dataset_path,
        record=6075,
        player=1,
        expected_action_id=196,  # MSL_ACT_DOWN_FOWARD_D
        expected_anim_index=196,  # MSL_SM_DOWN_FOWARD_D
        expected_on_ground=1,
    )


@pytest.mark.integration
def test_knockdown_clusterC_downbackd_does_not_drop_to_fall_on_fd_edge() -> None:
    # Cluster C representative:
    # - seed/ref = DownBackD (197) but previous sim out = Fall (29)
    # replays/.../AttachedGoodNaturedGuanaco.slpz record 2140 p=0
    root = Path(__file__).resolve().parents[1]
    rel = (
        "replays/validation/"
        "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz"
    )
    dataset_path = root / rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {rel}")

    _run_one_step_action_anim_ground(
        dataset_path=dataset_path,
        record=2140,
        player=0,
        expected_action_id=197,  # MSL_ACT_DOWN_BACK_D
        expected_anim_index=197,  # MSL_SM_DOWN_BACK_D
        expected_on_ground=1,
    )

